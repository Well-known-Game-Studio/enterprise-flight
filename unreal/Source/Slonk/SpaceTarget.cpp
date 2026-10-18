#include "SpaceTarget.h"
#include "SlonkTiming.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "ExplosionEffect.h"
#include "Materials/MaterialInterface.h"
#include "RockMeshes.h"
#include "SlonkMaterials.h"
#include "UObject/ConstructorHelpers.h"

const FName ASpaceTarget::TargetableTag(TEXT("Targetable"));

ASpaceTarget::ASpaceTarget()
{
	PrimaryActorTick.bCanEverTick = true;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionObjectType(ECC_WorldDynamic);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	Body->SetGenerateOverlapEvents(false);
	Body->SetMobility(EComponentMobility::Movable);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereFinder.Succeeded())
	{
		Body->SetStaticMesh(SphereFinder.Object);
	}

	BodyMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::AsteroidPath));
	Tags.Add(TargetableTag);
	ApplyRadius();
}

void ASpaceTarget::ApplyRadius()
{
	// Engine sphere is 100 cm in diameter.
	Body->SetRelativeScale3D(FVector(Radius / 50.f));
}

void ASpaceTarget::PCGPostSpawn()
{
	Health = MaxHealth;
	ApplyRadius();
}

void ASpaceTarget::RandomizeAppearance(FRandomStream& Rng)
{
	Radius = Rng.FRandRange(2000.f, 12000.f);
	TumbleRate = FRotator(Rng.FRandRange(-6.f, 6.f), Rng.FRandRange(-6.f, 6.f), Rng.FRandRange(-6.f, 6.f));
	MaxHealth = 60.f + Radius / 100.f;
	Health = MaxHealth;
	ApplyRadius();
}

void ASpaceTarget::SetBeyondHorizon(bool bBeyond)
{
	bBeyondHorizon = bBeyond;
	SetActorHiddenInGame(bBeyond);
#if WITH_EDITOR
	if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		SetIsTemporarilyHiddenInEditor(bBeyond);
	}
#endif
	if (Body) { Body->SetVisibility(!bBeyond); }
}

void ASpaceTarget::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyRadius();
}

void ASpaceTarget::BeginPlay()
{
	Super::BeginPlay();
	Health = MaxHealth;
	// Swap the placeholder sphere for a shaped rock. Game worlds only: the rock meshes are transient, and a
	// PCG-spawned actor saved with the level must not reference them.
	const uint32 Hash = GetTypeHash(GetFName()) ^ GetTypeHash(FMath::RoundToInt64(GetActorLocation().X / 1000.0));
	if (UStaticMesh* Rock = SlonkRocks::GetRockMesh(static_cast<int32>(Hash % SlonkRocks::NumVariants)))
	{
		Body->SetStaticMesh(Rock);
	}
	ApplyRadius();
	if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(BodyMaterial, TEXT("Asteroid"), /*bLit*/ true))
	{
		Body->SetMaterial(0, Mat);
	}
}

void ASpaceTarget::Tick(float DeltaTime)
{
	FSlonkScopeTimer SlonkTimer_(TEXT("Tick SpaceTarget"));
	Super::Tick(DeltaTime);
	if (SettleTicks > 0)
	{
		--SettleTicks;
		if (MaxHealth > 0.f && Health > MaxHealth) { Health = MaxHealth; }
		ApplyRadius();
	}
	AddActorLocalRotation(TumbleRate * DeltaTime);
	HorizonTimer -= DeltaTime;
	if (HorizonTimer <= 0.f)
	{
		HorizonTimer = 0.5f;
		SetBeyondHorizon(IsLocationBeyondHorizon(GetActorLocation()));
	}
}

void ASpaceTarget::PreRegisterAllComponents()
{
	Super::PreRegisterAllComponents();
	// Hide before any scene proxy can exist (see IFarBody).
	SetBeyondHorizon(IsLocationBeyondHorizon(PreRegistrationLocation(this)));
}

float ASpaceTarget::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (Health <= 0.f)
	{
		return Applied;
	}
	Health -= Applied;
	if (Health <= 0.f)
	{
		AExplosionEffect::SpawnExplosion(GetWorld(), GetActorLocation(), Radius * 2.5f, 1.4f);
		Destroy();
	}
	return Applied;
}
