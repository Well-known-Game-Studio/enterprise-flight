#include "ExplosionEffect.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "SlonkMaterials.h"
#include "UObject/ConstructorHelpers.h"

AExplosionEffect::AExplosionEffect()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	Shell = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shell"));
	Shell->SetupAttachment(Root);
	Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shell->CastShadow = false;
	if (SphereFinder.Succeeded())
	{
		Shell->SetStaticMesh(SphereFinder.Object);
	}

	Core = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Core"));
	Core->SetupAttachment(Root);
	Core->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Core->CastShadow = false;
	if (SphereFinder.Succeeded())
	{
		Core->SetStaticMesh(SphereFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	Shockwave = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shockwave"));
	Shockwave->SetupAttachment(Root);
	Shockwave->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shockwave->CastShadow = false;
	Shockwave->SetUsingAbsoluteRotation(true);
	if (PlaneFinder.Succeeded())
	{
		Shockwave->SetStaticMesh(PlaneFinder.Object);
	}
	ShockwaveMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::ShockwavePath));

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Root);
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetIntensity(LightIntensity);
	Light->SetAttenuationRadius(60000.f);
	Light->SetLightColor(Color);
	Light->CastShadows = false;

	ShellMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::ExplosionPath));
}

void AExplosionEffect::BeginPlay()
{
	Super::BeginPlay();

	Seed = FMath::FRand() * 100.f;
	if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(ShellMaterial, TEXT("Explosion")))
	{
		ShellMID = UMaterialInstanceDynamic::Create(Mat, this);
		ShellMID->SetScalarParameterValue(TEXT("Seed"), Seed);
		ShellMID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		ShellMID->SetVectorParameterValue(TEXT("Color"), Color);
		Shell->SetMaterial(0, ShellMID);
		CoreMID = UMaterialInstanceDynamic::Create(Mat, this);
		CoreMID->SetScalarParameterValue(TEXT("Seed"), Seed + 37.f);
		CoreMID->SetScalarParameterValue(TEXT("Intensity"), Intensity * 3.f);
		CoreMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.85f, 0.6f));
		Core->SetMaterial(0, CoreMID);
	}
	if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(ShockwaveMaterial, TEXT("Shockwave")))
	{
		RingMID = UMaterialInstanceDynamic::Create(Mat, this);
		RingMID->SetVectorParameterValue(TEXT("Color"), Color);
		Shockwave->SetMaterial(0, RingMID);
	}
	Light->SetLightColor(Color);
	Light->SetAttenuationRadius(FMath::Max(MaxRadius * 15.f, 20000.f));
	SetLifeSpan(Duration + 0.1f);
	Tick(0.f);
}

void AExplosionEffect::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	Age += DeltaTime;
	const float A = Duration > 0.f ? FMath::Clamp(Age / Duration, 0.f, 1.f) : 1.f;

	// Fireball: grows quickly, lingers as it dissipates; core flashes and dies; ring races outward.
	const float Grow = 1.f - FMath::Pow(1.f - A, 3.f);
	const float ShellRadius = MaxRadius * (0.25f + 0.75f * Grow);
	const float CoreRadius = MaxRadius * 0.5f * FMath::Max(1.f - A * 2.2f, 0.f);
	Shell->SetRelativeScale3D(FVector(FMath::Max(ShellRadius, 1.f) / 50.f));
	Core->SetRelativeScale3D(FVector(FMath::Max(CoreRadius, 1.f) / 50.f));
	Core->SetVisibility(CoreRadius > 1.f);
	Light->SetIntensity(LightIntensity * (1.f - A) * (1.f - A));
	if (ShellMID) { ShellMID->SetScalarParameterValue(TEXT("Age"), A); }
	if (CoreMID) { CoreMID->SetScalarParameterValue(TEXT("Age"), FMath::Min(A * 2.2f, 1.f)); }
	if (Shockwave)
	{
		// Engine plane is 100 cm; the ring reaches 2.5 radii.
		const float RingSize = MaxRadius * 2.5f;
		Shockwave->SetRelativeScale3D(FVector(RingSize / 100.f));
		if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		{
			if (PC->PlayerCameraManager)
			{
				const FVector ToCam = PC->PlayerCameraManager->GetCameraLocation() - GetActorLocation();
				Shockwave->SetWorldRotation(FRotationMatrix::MakeFromZ(ToCam.GetSafeNormal()).Rotator());
			}
		}
		if (RingMID) { RingMID->SetScalarParameterValue(TEXT("Age"), A); }
	}
}

AExplosionEffect* AExplosionEffect::SpawnExplosion(UWorld* World, const FVector& Location, float Radius, float InDuration)
{
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AExplosionEffect* Fx = World->SpawnActorDeferred<AExplosionEffect>(AExplosionEffect::StaticClass(), FTransform(Location));
	if (Fx)
	{
		Fx->MaxRadius = Radius;
		Fx->Duration = InDuration;
		Fx->FinishSpawning(FTransform(Location));
	}
	return Fx;
}
