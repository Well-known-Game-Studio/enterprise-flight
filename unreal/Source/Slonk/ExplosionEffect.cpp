#include "ExplosionEffect.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
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

	if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(ShellMaterial, TEXT("Explosion")))
	{
		Shell->SetMaterial(0, Mat);
		Core->SetMaterial(0, Mat);
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

	// Shell: expands fast then collapses; Core: bright ball that shrinks away.
	const float ShellRadius = MaxRadius * FMath::Sin(A * PI) ;
	const float CoreRadius = MaxRadius * 0.45f * (1.f - A) * (1.f - A);
	Shell->SetRelativeScale3D(FVector(FMath::Max(ShellRadius, 1.f) / 50.f));
	Core->SetRelativeScale3D(FVector(FMath::Max(CoreRadius, 1.f) / 50.f));
	Light->SetIntensity(LightIntensity * (1.f - A) * (1.f - A));
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
