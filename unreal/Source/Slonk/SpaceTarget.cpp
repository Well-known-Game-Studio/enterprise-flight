#include "SpaceTarget.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "ExplosionEffect.h"
#include "Materials/MaterialInterface.h"
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

void ASpaceTarget::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyRadius();
}

void ASpaceTarget::BeginPlay()
{
	Super::BeginPlay();
	Health = MaxHealth;
	ApplyRadius();
	if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(BodyMaterial, TEXT("Asteroid"), /*bLit*/ true))
	{
		Body->SetMaterial(0, Mat);
	}
}

void ASpaceTarget::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (SettleTicks > 0)
	{
		--SettleTicks;
		if (MaxHealth > 0.f && Health > MaxHealth) { Health = MaxHealth; }
		ApplyRadius();
	}
	AddActorLocalRotation(TumbleRate * DeltaTime);
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
