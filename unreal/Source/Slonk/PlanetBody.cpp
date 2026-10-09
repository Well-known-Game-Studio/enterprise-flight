#include "PlanetBody.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "SlonkMaterials.h"
#include "UObject/ConstructorHelpers.h"

const FName APlanetBody::StarTag(TEXT("Star"));

namespace
{
	UStaticMeshComponent* MakeShell(AActor* Owner, const TCHAR* Name, USceneComponent* Parent, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* C = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(Parent);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->CastShadow = false;
		C->bCastDynamicShadow = false;
		C->SetMobility(EComponentMobility::Movable);
		if (Mesh)
		{
			C->SetStaticMesh(Mesh);
		}
		return C;
	}
}

APlanetBody::APlanetBody()
{
	PrimaryActorTick.bCanEverTick = true;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionObjectType(ECC_WorldStatic);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetGenerateOverlapEvents(false);

	// Higher-poly unit sphere (radius 1 cm) so the silhouette stays round at 1000 km.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/ArtTools/RenderToTexture/Meshes/S_1_Unit_Sphere_HI.S_1_Unit_Sphere_HI"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FallbackSphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Mesh = nullptr;
	if (SphereFinder.Succeeded())
	{
		Mesh = SphereFinder.Object;
		MeshUnitRadius = 1.f;
	}
	else if (FallbackSphereFinder.Succeeded())
	{
		Mesh = FallbackSphereFinder.Object;
		MeshUnitRadius = 50.f;
	}
	if (Mesh)
	{
		Body->SetStaticMesh(Mesh);
	}

	Clouds = MakeShell(this, TEXT("Clouds"), Body, Mesh);
	Atmosphere = MakeShell(this, TEXT("Atmosphere"), Body, Mesh);
	Atmosphere->SetTranslucentSortPriority(1);

	SurfaceMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_PlanetSurface.M_PlanetSurface")));
	CloudMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_PlanetClouds.M_PlanetClouds")));
	AtmosphereMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_PlanetAtmosphere.M_PlanetAtmosphere")));
}

FString APlanetBody::GetDisplayName() const
{
	return PlanetName.IsEmpty() ? GetActorNameOrLabel() : PlanetName;
}

double APlanetBody::GetRadiusCm() const
{
	return FMath::Max(RadiusKm, 1.f) * 100000.0;
}

void APlanetBody::PCGPostSpawn()
{
	ApplyAppearance();
}

EPlanetArchetype APlanetBody::PickArchetype(FRandomStream& Rng) const
{
	const float T = ArchetypeSelector >= 0.f ? ArchetypeSelector : Rng.FRand();
	// Weighted: plenty of rocky worlds, a few gas giants, the odd lava world.
	if (T < 0.22f) return EPlanetArchetype::Terrestrial;
	if (T < 0.38f) return EPlanetArchetype::Ocean;
	if (T < 0.54f) return EPlanetArchetype::Desert;
	if (T < 0.68f) return EPlanetArchetype::Ice;
	if (T < 0.76f) return EPlanetArchetype::Lava;
	if (T < 0.92f) return EPlanetArchetype::GasGiant;
	return EPlanetArchetype::Moon;
}

void APlanetBody::ResolveStyleFromArchetype()
{
	const FVector L = GetActorLocation();
	const uint32 Hash = HashCombine(HashCombine(GetTypeHash(FMath::RoundToInt64(L.X / 1000.0)), GetTypeHash(FMath::RoundToInt64(L.Y / 1000.0))), GetTypeHash(FMath::RoundToInt64(L.Z / 1000.0)));
	FRandomStream Rng(static_cast<int32>(Hash));

	EPlanetArchetype Type = Archetype == EPlanetArchetype::Auto ? PickArchetype(Rng) : Archetype;
	ResolvedArchetype = Type;
	FPlanetStyle S; // defaults = temperate terrestrial
	auto Jitter = [&Rng](const FLinearColor& C, float Amount) { return C * Rng.FRandRange(1.f - Amount, 1.f + Amount); };

	switch (Type)
	{
	case EPlanetArchetype::Terrestrial:
		S.SeaLevel = Rng.FRandRange(0.45f, 0.6f);
		S.LandColor = Jitter(FLinearColor(0.28f, 0.36f, 0.16f), 0.25f);
		S.LandColor2 = Jitter(FLinearColor(0.55f, 0.45f, 0.28f), 0.25f);
		S.CloudCoverage = Rng.FRandRange(0.35f, 0.55f);
		S.AtmosphereColor = Jitter(FLinearColor(0.35f, 0.6f, 1.0f), 0.15f);
		S.NightLightIntensity = Rng.FRand() < 0.6f ? 6.f : 0.f;
		break;
	case EPlanetArchetype::Ocean:
		S.SeaLevel = Rng.FRandRange(0.7f, 0.85f);
		S.OceanColor = Jitter(FLinearColor(0.03f, 0.2f, 0.42f), 0.3f);
		S.LandColor = FLinearColor(0.35f, 0.38f, 0.25f);
		S.CloudCoverage = Rng.FRandRange(0.5f, 0.7f);
		S.AtmosphereColor = Jitter(FLinearColor(0.3f, 0.65f, 1.0f), 0.15f);
		S.AtmosphereDepth = 0.07f;
		break;
	case EPlanetArchetype::Desert:
		S.SeaLevel = Rng.FRandRange(-0.05f, 0.1f);
		S.OceanColor = FLinearColor(0.35f, 0.25f, 0.15f); // dry basins
		S.LandColor = Jitter(FLinearColor(0.52f, 0.32f, 0.16f), 0.25f);
		S.LandColor2 = Jitter(FLinearColor(0.85f, 0.7f, 0.45f), 0.2f);
		S.MountainColor = FLinearColor(0.38f, 0.26f, 0.2f);
		S.NoiseScale = 2.2f;
		S.IceCapStart = Rng.FRand() < 0.5f ? 0.92f : 2.f;
		S.CloudCoverage = Rng.FRandRange(0.05f, 0.2f);
		S.AtmosphereColor = Jitter(FLinearColor(0.95f, 0.65f, 0.4f), 0.2f);
		S.SunsetColor = FLinearColor(1.0f, 0.35f, 0.1f);
		S.AtmosphereDepth = 0.04f;
		S.Intensity = 1.4f;
		S.ReliefFraction = 0.004f;
		break;
	case EPlanetArchetype::Ice:
		S.SeaLevel = Rng.FRandRange(0.3f, 0.5f);
		S.OceanColor = FLinearColor(0.55f, 0.65f, 0.75f);
		S.LandColor = FLinearColor(0.8f, 0.85f, 0.9f);
		S.LandColor2 = FLinearColor(0.65f, 0.7f, 0.78f);
		S.MountainColor = FLinearColor(0.72f, 0.74f, 0.8f);
		S.IceCapStart = 0.5f;
		S.OceanRoughness = 0.15f;
		S.CloudCoverage = Rng.FRandRange(0.2f, 0.4f);
		S.AtmosphereColor = FLinearColor(0.6f, 0.75f, 1.0f);
		S.AtmosphereDepth = 0.05f;
		S.Intensity = 1.5f;
		break;
	case EPlanetArchetype::Lava:
		S.SeaLevel = Rng.FRandRange(0.2f, 0.35f);
		S.OceanColor = FLinearColor(1.0f, 0.25f, 0.03f); // molten
		S.ShallowColor = FLinearColor(1.0f, 0.6f, 0.1f);
		S.LavaEmissive = 4.f;
		S.LandColor = FLinearColor(0.08f, 0.06f, 0.05f);
		S.LandColor2 = FLinearColor(0.2f, 0.12f, 0.08f);
		S.MountainColor = FLinearColor(0.25f, 0.2f, 0.18f);
		S.IceCapStart = 2.f;
		S.OceanRoughness = 0.6f;
		S.bClouds = Rng.FRand() < 0.5f;
		S.CloudCoverage = 0.25f;
		S.CloudColor = FLinearColor(0.35f, 0.3f, 0.28f);
		S.AtmosphereColor = FLinearColor(1.0f, 0.4f, 0.1f);
		S.SunsetColor = FLinearColor(1.0f, 0.2f, 0.05f);
		S.AtmosphereDepth = 0.05f;
		S.NightFactor = 0.25f;
		S.ReliefFraction = 0.006f;
		break;
	case EPlanetArchetype::GasGiant:
	{
		// Banded: ocean = band colour A, land = band colour B, very high noise scale squashed later by spin.
		const FLinearColor A = Jitter(FLinearColor(0.78f, 0.62f, 0.42f), 0.3f);
		const FLinearColor B = Jitter(FLinearColor(0.55f, 0.42f, 0.3f), 0.3f);
		S.SeaLevel = 0.5f;
		S.OceanColor = A;
		S.LandColor = B;
		S.LandColor2 = A * 0.9f;
		S.MountainColor = A;
		S.IceCapStart = 2.f;
		S.NoiseScale = Rng.FRandRange(0.8f, 1.4f);
		S.BandStrength = 0.92f;
		S.BandScale = Rng.FRandRange(10.f, 18.f);
		S.BandWarp = Rng.FRandRange(0.3f, 0.9f);
		S.NormalStrength = 0.05f;
		S.ShallowColor = A * 0.9f;
		S.OceanRoughness = 0.7f;
		S.LandRoughness = 0.7f;
		S.bClouds = true;
		S.CloudCoverage = 0.3f;
		S.CloudScale = 2.0f;
		S.CloudColor = A * 1.05f;
		S.CloudSpinDegPerSec = 0.4f;
		S.AtmosphereColor = A;
		S.SunsetColor = B;
		S.AtmosphereDepth = 0.08f;
		S.Intensity = 1.6f;
		break;
	}
	case EPlanetArchetype::Moon:
	default:
		S.SeaLevel = -0.05f; // no "ocean" pits: everything is land
		S.LandColor = Jitter(FLinearColor(0.32f, 0.31f, 0.3f), 0.2f);
		S.LandColor2 = Jitter(FLinearColor(0.2f, 0.2f, 0.2f), 0.2f);
		S.MountainColor = FLinearColor(0.45f, 0.44f, 0.43f);
		S.IceCapStart = 2.f;
		S.NoiseScale = 5.f;
		S.LandRoughness = 0.95f;
		S.NormalStrength = 0.15f;
		S.DetailNormalScale = 6.f;
		S.bClouds = false;
		S.bAtmosphere = false;
		S.ReliefFraction = 0.006f;
		break;
	}

	if (Rng.FRand() < 0.25f)
	{
		S.IceCapStart = FMath::Min(S.IceCapStart, Rng.FRandRange(0.7f, 0.9f));
	}
	Style = S;
	SpinDegPerSec = Rng.FRandRange(0.1f, 0.8f) * (Rng.FRand() < 0.2f ? -1.f : 1.f);
	if (Type == EPlanetArchetype::GasGiant)
	{
		SpinDegPerSec *= 2.f;
	}
}

void APlanetBody::ApplyMaterials()
{
	auto EnsureMID = [this](TObjectPtr<UMaterialInstanceDynamic>& Slot, const TSoftObjectPtr<UMaterialInterface>& Source, UStaticMeshComponent* Comp, const TCHAR* DebugName, bool bLitFallback)
	{
		if (!Slot)
		{
			if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(Source, DebugName, bLitFallback))
			{
				Slot = UMaterialInstanceDynamic::Create(Mat, this);
			}
		}
		if (Slot && Comp->GetMaterial(0) != Slot)
		{
			Comp->SetMaterial(0, Slot);
		}
	};
	EnsureMID(SurfaceMID, SurfaceMaterial, Body, TEXT("PlanetSurface"), true);
	EnsureMID(CloudMID, CloudMaterial, Clouds, TEXT("PlanetClouds"), true);
	EnsureMID(AtmosphereMID, AtmosphereMaterial, Atmosphere, TEXT("PlanetAtmosphere"), false);

	const FPlanetStyle& S = Style;
	// A per-planet offset so two planets with the same style never share a coastline.
	const FVector L = GetActorLocation();
	const FLinearColor Seed(FMath::Frac(L.X / 7.3e9) * 100.f, FMath::Frac(L.Y / 5.1e9) * 100.f, FMath::Frac(L.Z / 3.7e9) * 100.f, 0.f);

	if (SurfaceMID)
	{
		SurfaceMID->SetVectorParameterValue(TEXT("OceanColor"), S.OceanColor);
		SurfaceMID->SetVectorParameterValue(TEXT("LandColor"), S.LandColor);
		SurfaceMID->SetVectorParameterValue(TEXT("LandColor2"), S.LandColor2);
		SurfaceMID->SetVectorParameterValue(TEXT("MountainColor"), S.MountainColor);
		SurfaceMID->SetVectorParameterValue(TEXT("IceColor"), S.IceColor);
		SurfaceMID->SetVectorParameterValue(TEXT("SeedOffset"), Seed);
		SurfaceMID->SetScalarParameterValue(TEXT("SeaLevel"), S.SeaLevel);
		SurfaceMID->SetScalarParameterValue(TEXT("IceCapStart"), S.IceCapStart);
		SurfaceMID->SetScalarParameterValue(TEXT("NoiseScale"), S.NoiseScale);
		SurfaceMID->SetScalarParameterValue(TEXT("OceanRoughness"), S.OceanRoughness);
		SurfaceMID->SetScalarParameterValue(TEXT("LandRoughness"), S.LandRoughness);
		SurfaceMID->SetVectorParameterValue(TEXT("ShallowColor"), S.ShallowColor);
		SurfaceMID->SetScalarParameterValue(TEXT("NormalStrength"), S.NormalStrength);
		SurfaceMID->SetScalarParameterValue(TEXT("DetailNormalScale"), S.DetailNormalScale);
		SurfaceMID->SetScalarParameterValue(TEXT("NightLightIntensity"), S.NightLightIntensity);
		SurfaceMID->SetVectorParameterValue(TEXT("NightLightColor"), S.NightLightColor);
		SurfaceMID->SetScalarParameterValue(TEXT("LavaEmissive"), S.LavaEmissive);
		SurfaceMID->SetScalarParameterValue(TEXT("BandStrength"), S.BandStrength);
		SurfaceMID->SetScalarParameterValue(TEXT("BandScale"), S.BandScale);
		SurfaceMID->SetScalarParameterValue(TEXT("BandWarp"), S.BandWarp);
		SurfaceMID->SetVectorParameterValue(TEXT("SunDirection"), FLinearColor(CachedStarDirection.X, CachedStarDirection.Y, CachedStarDirection.Z, 0.f));
		// HeightScale is in cm per unit of (height - sea level); land tops out ~0.5 above sea level.
		SurfaceMID->SetScalarParameterValue(TEXT("HeightScale"), S.ReliefFraction * GetRadiusCm() * 2.f);
	}
	if (CloudMID)
	{
		CloudMID->SetScalarParameterValue(TEXT("Coverage"), S.CloudCoverage);
		CloudMID->SetScalarParameterValue(TEXT("CloudScale"), S.CloudScale);
		CloudMID->SetVectorParameterValue(TEXT("CloudColor"), S.CloudColor);
		CloudMID->SetVectorParameterValue(TEXT("SeedOffset"), Seed * 0.37f);
	}
	if (AtmosphereMID)
	{
		AtmosphereMID->SetVectorParameterValue(TEXT("AtmosphereColor"), S.AtmosphereColor);
		AtmosphereMID->SetVectorParameterValue(TEXT("SunsetColor"), S.SunsetColor);
		AtmosphereMID->SetScalarParameterValue(TEXT("AtmosphereDepth"), S.AtmosphereDepth);
		AtmosphereMID->SetScalarParameterValue(TEXT("Falloff"), S.Falloff);
		AtmosphereMID->SetScalarParameterValue(TEXT("HazePower"), S.HazePower);
		AtmosphereMID->SetScalarParameterValue(TEXT("Intensity"), S.Intensity);
		AtmosphereMID->SetScalarParameterValue(TEXT("Opacity"), S.Opacity);
		AtmosphereMID->SetScalarParameterValue(TEXT("Haze"), S.Haze);
		AtmosphereMID->SetScalarParameterValue(TEXT("SunsetStrength"), S.SunsetStrength);
		AtmosphereMID->SetScalarParameterValue(TEXT("NightFactor"), S.NightFactor);
		AtmosphereMID->SetVectorParameterValue(TEXT("SunDirection"), FLinearColor(CachedStarDirection.X, CachedStarDirection.Y, CachedStarDirection.Z, 0.f));
	}
}

void APlanetBody::ApplyAppearance()
{
	if (bStyleFromArchetype)
	{
		ResolveStyleFromArchetype();
	}

	// Body is the root: set its world scale so the planet is exactly RadiusKm whatever the spawner did to the actor scale.
	const double RadiusCm = GetRadiusCm();
	Body->SetWorldScale3D(FVector(RadiusCm / MeshUnitRadius));

	Clouds->SetVisibility(Style.bClouds);
	Clouds->SetRelativeScale3D(FVector(1.0 + Style.CloudHeightFraction));
	Atmosphere->SetVisibility(Style.bAtmosphere);
	Atmosphere->SetRelativeScale3D(FVector(1.0 + Style.AtmosphereDepth));

	UpdateStarDirection();
	ApplyMaterials();
}

FVector APlanetBody::GetStarDirection() const
{
	return CachedStarDirection;
}

void APlanetBody::UpdateStarDirection()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const FVector Here = GetActorLocation();

	AActor* StarActor = Star;
	if (!StarActor)
	{
		double BestDist = TNumericLimits<double>::Max();
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(StarTag))
			{
				const double D = FVector::DistSquared(It->GetActorLocation(), Here);
				if (D < BestDist)
				{
					BestDist = D;
					StarActor = *It;
				}
			}
		}
	}
	if (StarActor)
	{
		CachedStarDirection = (StarActor->GetActorLocation() - Here).GetSafeNormal();
		return;
	}
	TActorIterator<ADirectionalLight> LightIt(World);
	if (LightIt)
	{
		// Light travels along the light's forward vector, so the star is the other way.
		CachedStarDirection = -LightIt->GetActorForwardVector();
	}
}

void APlanetBody::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyAppearance();
}

void APlanetBody::BeginPlay()
{
	Super::BeginPlay();
	ApplyAppearance();
}

void APlanetBody::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (SettleTicks > 0)
	{
		--SettleTicks;
		ApplyAppearance();
	}
	if (!FMath::IsNearlyZero(SpinDegPerSec))
	{
		AddActorLocalRotation(FRotator(0.f, SpinDegPerSec * DeltaTime, 0.f));
	}
	if (Clouds && Style.bClouds && !FMath::IsNearlyZero(Style.CloudSpinDegPerSec))
	{
		Clouds->AddLocalRotation(FRotator(0.f, Style.CloudSpinDegPerSec * DeltaTime, 0.f));
	}

	// The star direction only changes as bodies move; refresh a few times a second.
	StarSearchTimer -= DeltaTime;
	if (StarSearchTimer <= 0.f)
	{
		StarSearchTimer = 0.25f;
		UpdateStarDirection();
		const FLinearColor SunDir(CachedStarDirection.X, CachedStarDirection.Y, CachedStarDirection.Z, 0.f);
		if (AtmosphereMID) { AtmosphereMID->SetVectorParameterValue(TEXT("SunDirection"), SunDir); }
		if (SurfaceMID) { SurfaceMID->SetVectorParameterValue(TEXT("SunDirection"), SunDir); }
	}
}

#if WITH_EDITOR
void APlanetBody::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);
	// Editor tools may set the actor transform after construction; keep the body sized by its radius.
	ApplyAppearance();
}
#endif
