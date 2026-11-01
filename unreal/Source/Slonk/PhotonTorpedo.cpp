#include "PhotonTorpedo.h"
#include "StarshipPawn.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "Components/BoxComponent.h"

#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "ExplosionEffect.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "SlonkMaterials.h"
#include "UObject/ConstructorHelpers.h"

APhotonTorpedo::APhotonTorpedo()
{
	PrimaryActorTick.bCanEverTick = true;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(150.f);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Collision->SetNotifyRigidBodyCollision(true);
	Collision->CanCharacterStepUpOn = ECB_No;
	SetRootComponent(Collision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	Casing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Casing"));
	Casing->SetupAttachment(Collision);
	Casing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Casing->CastShadow = false;
	if (SphereFinder.Succeeded())
	{
		Casing->SetStaticMesh(SphereFinder.Object);
	}

	Halo = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Halo"));
	Halo->SetupAttachment(Collision);
	Halo->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Halo->CastShadow = false;
	if (SphereFinder.Succeeded())
	{
		Halo->SetStaticMesh(SphereFinder.Object);
	}

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Collision);
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetIntensity(400000.f);
	Light->SetAttenuationRadius(25000.f);
	Light->SetLightColor(GlowColor);
	Light->CastShadows = false;

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->SetUpdatedComponent(Collision);
	Movement->ProjectileGravityScale = 0.f;
	Movement->bRotationFollowsVelocity = true;
	Movement->bShouldBounce = false;
	Movement->bSweepCollision = true;
	Movement->InitialSpeed = Speed;
	Movement->MaxSpeed = 0.f; // unlimited; homing can add speed

	Trail = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Trail"));
	Trail->SetupAttachment(Collision);
	Trail->SetAutoActivate(false);

	GlowMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::TorpedoPath));
}

void APhotonTorpedo::BeginPlay()
{
	Super::BeginPlay();

	if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(GlowMaterial, TEXT("Torpedo")))
	{
		Casing->SetMaterial(0, Mat);
		Halo->SetMaterial(0, Mat);
	}
	Casing->SetRelativeScale3D(FVector(VisualRadius / 50.f));
	Halo->SetRelativeScale3D(FVector(VisualRadius * 1.8f / 50.f));
	Light->SetLightColor(GlowColor);

	if (UNiagaraSystem* System = TrailSystem.LoadSynchronous())
	{
		Trail->SetAsset(System);
		Trail->Activate(true);
	}

	Collision->OnComponentHit.AddDynamic(this, &APhotonTorpedo::OnHit);
	SetLifeSpan(LifeSeconds);
}

void APhotonTorpedo::Launch(const FVector& Direction, double InheritedSpeed, AActor* HomingTarget, const FVector& AimDirection)
{
	const FVector Dir = Direction.GetSafeNormal();
	const double LaunchSpeed = Speed + FMath::Max(0.0, InheritedSpeed);
	Movement->InitialSpeed = LaunchSpeed;
	Movement->Velocity = Dir * LaunchSpeed;
	SetActorRotation(Dir.Rotation());
	// Straight out of the tube first; the turn toward the target is our own, in Tick, so it is a clean bank
	// at a set rate rather than the engine's homing acceleration bending the track from the first frame.
	Movement->bIsHomingProjectile = false;
	SeekTarget = HomingTarget;
	SeekDirection = AimDirection.GetSafeNormal();
	bSeeking = (HomingTarget != nullptr) || !SeekDirection.IsNearlyZero();

	if (AActor* MyOwner = GetOwner())
	{
		Collision->IgnoreActorWhenMoving(MyOwner, true);
		Collision->MoveIgnoreActors.AddUnique(MyOwner);
	}
}

void APhotonTorpedo::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	Age += DeltaTime;
	if (bSeeking && Age > StraightRunSeconds && Movement && !bDetonated)
	{
		FVector Want = SeekDirection;
		if (SeekTarget.IsValid())
		{
			Want = (SeekTarget->GetActorLocation() - GetActorLocation()).GetSafeNormal();
		}
		const double CurSpeed = Movement->Velocity.Size();
		if (!Want.IsNearlyZero() && CurSpeed > 1.0)
		{
			const FVector Cur = Movement->Velocity / CurSpeed;
			const float MaxStep = FMath::DegreesToRadians(TurnRateDegPerSecond) * DeltaTime;
			const float Angle = FMath::Acos(FMath::Clamp((float)FVector::DotProduct(Cur, Want), -1.f, 1.f));
			FVector NewDir = Want;
			if (Angle > MaxStep)
			{
				const FVector Axis = FVector::CrossProduct(Cur, Want).GetSafeNormal();
				NewDir = Axis.IsNearlyZero() ? Cur : Cur.RotateAngleAxisRad(MaxStep, Axis);
			}
			Movement->Velocity = NewDir * CurSpeed;
			SetActorRotation(NewDir.Rotation());
		}
	}
	// Pulsing halo.
	const float Pulse = 1.f + 0.25f * FMath::Sin(Age * 30.f);
	Halo->SetRelativeScale3D(FVector(VisualRadius * 1.8f * Pulse / 50.f));
	Light->SetIntensity(400000.f * Pulse);
}

void APhotonTorpedo::OnHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (OtherActor == GetOwner())
	{
		return;
	}
	Detonate(Hit.ImpactPoint.IsNearlyZero() ? GetActorLocation() : FVector(Hit.ImpactPoint));
}

void APhotonTorpedo::Detonate(const FVector& Location)
{
	if (bDetonated)
	{
		return;
	}
	bDetonated = true;

	TArray<AActor*> Ignore;
	if (AActor* MyOwner = GetOwner())
	{
		Ignore.Add(MyOwner);
	}
	// The starships take the burst directly. The engine's radial damage traces the visibility channel to each
	// victim, which a hull's box ignores, so it fakes its contact at the box's centre and then scales the damage by
	// the distance to that centre: a hull three hundred metres long takes nothing from a burst off its rim. Each hull
	// in reach gets the event with the real nearest point of its box as the contact.
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AStarshipPawn> It(World); It; ++It)
		{
			AStarshipPawn* Ship = *It;
			if (!Ship || Ship == GetOwner() || !Ship->CollisionBox || Ship->IsActorBeingDestroyed()) { continue; }
			const FTransform& BoxT = Ship->CollisionBox->GetComponentTransform();
			const FVector E = Ship->CollisionBox->GetUnscaledBoxExtent();
			const FVector L = BoxT.InverseTransformPosition(Location);
			const FVector Closest = BoxT.TransformPosition(FVector(FMath::Clamp(L.X, -E.X, E.X), FMath::Clamp(L.Y, -E.Y, E.Y), FMath::Clamp(L.Z, -E.Z, E.Z)));
			const double Dist = FVector::Dist(Closest, Location);
			if (Dist > DamageRadius) { continue; }
			FRadialDamageEvent Event;
			Event.Origin = Location;
			Event.Params = FRadialDamageParams(Damage, Damage, 0.f, DamageRadius, 1.f);
			FHitResult Hit;
			Hit.Init(Location, Closest);
			Hit.bBlockingHit = true;
			Hit.Location = Closest;
			Hit.ImpactPoint = Closest;
			Hit.ImpactNormal = Dist > 1.0 ? (Location - Closest) / Dist : FVector::UpVector;
			Hit.Normal = Hit.ImpactNormal;
			Hit.Component = Ship->CollisionBox;
			Hit.HitObjectHandle = FActorInstanceHandle(Ship);
			Event.ComponentHits.Add(Hit);
			Ship->TakeDamage(Damage, Event, GetInstigatorController(), this);
			Ignore.Add(Ship);
		}
	}
	UGameplayStatics::ApplyRadialDamage(this, Damage, Location, DamageRadius, nullptr, Ignore, this, GetInstigatorController(), /*bDoFullDamage*/ true);
	AExplosionEffect::SpawnExplosion(GetWorld(), Location, ExplosionRadius, 1.0f);
	Destroy();
}
