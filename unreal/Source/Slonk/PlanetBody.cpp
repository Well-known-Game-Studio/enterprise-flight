#include "PlanetBody.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "UDynamicMesh.h"
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
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->SetupAttachment(Body);
	CollisionSphere->SetUsingAbsoluteScale(true);
	CollisionSphere->SetMobility(EComponentMobility::Movable);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionSphere->SetCollisionObjectType(ECC_WorldStatic);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionSphere->SetGenerateOverlapEvents(false);
	CollisionSphere->SetHiddenInGame(true);
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetGenerateOverlapEvents(false);
	// A spinning 1000 km movable sphere is a terrible shadow caster (it churns the virtual shadow maps every frame).
	Body->CastShadow = false;
	Body->bCastDynamicShadow = false;
	Body->bAffectDistanceFieldLighting = false;

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

	DetailBody = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("DetailBody"));
	DetailBody->SetupAttachment(Body);
	DetailBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DetailBody->SetGenerateOverlapEvents(false);
	DetailBody->SetMobility(EComponentMobility::Movable);
	DetailBody->SetVisibility(false);
	DetailBody->CastShadow = false;
	DetailBody->bCastDynamicShadow = false;
	DetailBody->bAffectDistanceFieldLighting = false;

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
	// Selector values >= 0.92 stay "Moon" so the PCG moon spawner keeps working.
	if (T < 0.16f) return EPlanetArchetype::Terrestrial;
	if (T < 0.28f) return EPlanetArchetype::Ocean;
	if (T < 0.40f) return EPlanetArchetype::Desert;
	if (T < 0.50f) return EPlanetArchetype::Ice;
	if (T < 0.57f) return EPlanetArchetype::Lava;
	if (T < 0.68f) return EPlanetArchetype::GasGiant;
	if (T < 0.80f) return EPlanetArchetype::Venus;
	if (T < 0.92f) return EPlanetArchetype::Mars;
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
		S.LandColor = Jitter(FLinearColor(0.36f, 0.46f, 0.20f), 0.25f);
		S.CraterStrength = 0.1f;
		S.LandColor2 = Jitter(FLinearColor(0.48f, 0.47f, 0.26f), 0.25f);
		S.CloudCoverage = Rng.FRandRange(0.35f, 0.55f);
		S.AtmosphereColor = Jitter(FLinearColor(0.35f, 0.6f, 1.0f), 0.15f);
		S.IceCapStart = Rng.FRandRange(0.72f, 0.8f);
		S.NightLightIntensity = Rng.FRand() < 0.6f ? 6.f : 0.f;
		S.ReliefFraction = 0.006f;
		break;
	case EPlanetArchetype::Ocean:
		S.SeaLevel = Rng.FRandRange(0.7f, 0.85f);
		S.OceanColor = Jitter(FLinearColor(0.05f, 0.30f, 0.60f), 0.3f);
		S.LandColor = FLinearColor(0.44f, 0.48f, 0.30f);
		S.CloudCoverage = Rng.FRandRange(0.5f, 0.7f);
		S.AtmosphereColor = Jitter(FLinearColor(0.3f, 0.65f, 1.0f), 0.15f);
		S.AtmosphereDepth = 0.07f;
		break;
	case EPlanetArchetype::Desert:
		S.SeaLevel = Rng.FRandRange(-0.05f, 0.1f);
		S.OceanColor = FLinearColor(0.28f, 0.20f, 0.12f); // dry basins
		S.LandColor = Jitter(FLinearColor(0.50f, 0.31f, 0.15f), 0.25f);
		S.CraterStrength = 0.45f;
		S.MariaStrength = 0.2f;
		S.LandColor2 = Jitter(FLinearColor(0.68f, 0.53f, 0.32f), 0.2f);
		S.MountainColor = FLinearColor(0.38f, 0.26f, 0.2f);
		S.NoiseScale = 2.2f;
		S.IceCapStart = Rng.FRand() < 0.5f ? 0.92f : 2.f;
		S.CloudCoverage = Rng.FRandRange(0.05f, 0.2f);
		S.AtmosphereColor = Jitter(FLinearColor(0.95f, 0.65f, 0.4f), 0.2f);
		S.SunsetColor = FLinearColor(1.0f, 0.35f, 0.1f);
		S.AtmosphereDepth = 0.04f;
		S.Intensity = 1.4f;
		S.ReliefFraction = 0.008f;
		break;
	case EPlanetArchetype::Ice:
		S.SeaLevel = Rng.FRandRange(0.3f, 0.5f);
		S.OceanColor = FLinearColor(0.55f, 0.65f, 0.75f);
		S.CraterStrength = 0.35f;
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
		S.LandColor = FLinearColor(0.10f, 0.08f, 0.07f);
		S.CraterStrength = 0.25f;
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
		S.ReliefFraction = 0.010f;
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
	case EPlanetArchetype::Venus:
		// A yellow-white cloud deck hides everything; the surface only matters for the odd gap.
		S.SeaLevel = -0.3f;
		S.LandColor = FLinearColor(0.55f, 0.42f, 0.22f);
		S.LandColor2 = FLinearColor(0.7f, 0.55f, 0.3f);
		S.MountainColor = FLinearColor(0.45f, 0.35f, 0.22f);
		S.IceCapStart = 2.f;
		S.CraterStrength = 0.2f;
		S.bClouds = true;
		S.CloudCoverage = Rng.FRandRange(0.9f, 1.0f);
		S.CloudScale = 3.f;
		S.CloudColor = Jitter(FLinearColor(1.0f, 0.88f, 0.55f), 0.1f);
		S.CloudSpinDegPerSec = 1.2f;
		S.CloudDrift = 0.08f;
		S.CloudHeightFraction = 0.012f;
		S.AtmosphereColor = Jitter(FLinearColor(1.0f, 0.8f, 0.45f), 0.1f);
		S.SunsetColor = FLinearColor(1.0f, 0.5f, 0.2f);
		S.AtmosphereDepth = 0.11f;
		S.Haze = 0.25f;
		S.Intensity = 2.6f;
		S.ReliefFraction = 0.004f;
		break;
	case EPlanetArchetype::Mars:
		S.SeaLevel = Rng.FRandRange(-0.05f, 0.12f);
		S.OceanColor = FLinearColor(0.08f, 0.16f, 0.28f);
		S.ShallowColor = FLinearColor(0.2f, 0.25f, 0.3f);
		S.LandColor = Jitter(FLinearColor(0.50f, 0.24f, 0.11f), 0.2f);
		S.LandColor2 = Jitter(FLinearColor(0.64f, 0.36f, 0.18f), 0.2f);
		S.MountainColor = FLinearColor(0.42f, 0.24f, 0.16f);
		S.IceCapStart = Rng.FRandRange(0.8f, 0.88f);
		S.IceColor = FLinearColor(0.95f, 0.95f, 0.95f);
		S.NoiseScale = 2.6f;
		S.CraterStrength = 0.55f;
		S.MariaStrength = 0.25f;
		S.CloudCoverage = Rng.FRandRange(0.12f, 0.3f);
		S.CloudColor = FLinearColor(1.0f, 0.98f, 0.95f);
		S.AtmosphereColor = Jitter(FLinearColor(0.95f, 0.62f, 0.42f), 0.15f);
		S.SunsetColor = FLinearColor(0.6f, 0.7f, 1.0f); // Martian sunsets go blue
		S.AtmosphereDepth = 0.035f;
		S.Haze = 0.08f;
		S.Intensity = 1.2f;
		S.ReliefFraction = 0.012f;
		break;
	case EPlanetArchetype::Moon:
	default:
	{
		S.SeaLevel = -0.3f; // no "ocean" pits: everything is land (fBm can dip a little below 0)
		S.IceCapStart = 2.f;
		S.NoiseScale = 5.f;
		S.bClouds = false;
		S.bAtmosphere = false;
		const float K = Rng.FRand();
		ResolvedMoonKind = K < 0.55f ? EMoonKind::Rocky : (K < 0.85f ? EMoonKind::Icy : EMoonKind::Lava);
		switch (ResolvedMoonKind)
		{
		case EMoonKind::Icy:
			// Bright, smooth ice with sparse craters and a faint blue cast.
			S.LandColor = Jitter(FLinearColor(0.62f, 0.70f, 0.78f), 0.1f);
			S.LandColor2 = Jitter(FLinearColor(0.62f, 0.70f, 0.80f), 0.1f);
			S.MountainColor = FLinearColor(0.78f, 0.82f, 0.88f);
			S.CraterStrength = 0.35f;
			S.MariaStrength = 0.5f;
			S.LandRoughness = 0.55f;
			S.NormalStrength = 0.15f;
			S.DetailNormalScale = 4.f;
			S.ReliefFraction = 0.005f;
			break;
		case EMoonKind::Lava:
			// Dark basalt with molten basins glowing through.
			S.SeaLevel = Rng.FRandRange(0.18f, 0.3f);
			S.OceanColor = FLinearColor(0.9f, 0.18f, 0.02f);
			S.ShallowColor = FLinearColor(1.0f, 0.55f, 0.1f);
			S.LavaEmissive = 3.f;
			S.OceanRoughness = 0.7f;
			S.LandColor = FLinearColor(0.12f, 0.10f, 0.09f);
			S.LandColor2 = FLinearColor(0.22f, 0.15f, 0.11f);
			S.MountainColor = FLinearColor(0.3f, 0.24f, 0.2f);
			S.CraterStrength = 0.4f;
			S.MariaStrength = 0.f;
			S.LandRoughness = 0.9f;
			S.NormalStrength = 0.3f;
			S.DetailNormalScale = 6.f;
			S.ReliefFraction = 0.012f;
			break;
		default:
			S.LandColor = Jitter(FLinearColor(0.20f, 0.20f, 0.19f), 0.2f);
			S.CraterStrength = 1.f;
			S.MariaStrength = 0.8f;
			S.LandColor2 = Jitter(FLinearColor(0.13f, 0.13f, 0.13f), 0.2f);
			S.MountainColor = FLinearColor(0.30f, 0.30f, 0.29f);
			S.LandRoughness = 0.95f;
			S.NormalStrength = 0.3f;
			S.DetailNormalScale = 6.f;
			S.ReliefFraction = 0.010f;
			break;
		}
		break;
	}
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
	// Weather moves against or well ahead of the ground so the deck visibly slides over the surface.
	Style.CloudSpinDegPerSec = (Rng.FRand() < 0.5f ? -1.f : 1.f) * Rng.FRandRange(0.6f, 1.4f) * FMath::Max(FMath::Abs(SpinDegPerSec), 0.3f);
	Style.CloudDrift = Rng.FRandRange(0.02f, 0.05f);
	if (Type == EPlanetArchetype::GasGiant)
	{
		Style.CloudSpinDegPerSec *= 1.5f;
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
	if (!DetailSurfaceMID)
	{
		if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(SurfaceMaterial, TEXT("PlanetSurfaceDetail"), true))
		{
			DetailSurfaceMID = UMaterialInstanceDynamic::Create(Mat, this);
		}
	}
	if (DetailSurfaceMID && bDetailBuilt && DetailBody->GetMaterial(0) != DetailSurfaceMID)
	{
		DetailBody->SetMaterial(0, DetailSurfaceMID);
	}
	EnsureMID(CloudMID, CloudMaterial, Clouds, TEXT("PlanetClouds"), true);
	EnsureMID(AtmosphereMID, AtmosphereMaterial, Atmosphere, TEXT("PlanetAtmosphere"), false);

	const FPlanetStyle& S = Style;
	// A per-planet offset so two planets with the same style never share a coastline.
	const FVector L = GetActorLocation();
	const FLinearColor Seed(FMath::Frac(L.X / 7.3e9) * 100.f, FMath::Frac(L.Y / 5.1e9) * 100.f, FMath::Frac(L.Z / 3.7e9) * 100.f, 0.f);

	for (UMaterialInstanceDynamic* Mid : { SurfaceMID.Get(), DetailSurfaceMID.Get() })
	{
		if (!Mid)
		{
			continue;
		}
		// Relief only on the dense mesh: on the 5k-triangle far sphere it just shows the facets.
		const bool bDetail = Mid == DetailSurfaceMID;
		Mid->SetScalarParameterValue(TEXT("PlanetRadiusCm"), GetRadiusCm());
		Mid->SetScalarParameterValue(TEXT("CloudScale"), S.CloudScale);
		Mid->SetScalarParameterValue(TEXT("Coverage"), S.bClouds ? S.CloudCoverage : 0.f);
		Mid->SetVectorParameterValue(TEXT("CloudSeedOffset"), Seed * 0.37f);
		Mid->SetScalarParameterValue(TEXT("CloudYaw"), FMath::DegreesToRadians(CloudYawDeg));
		Mid->SetVectorParameterValue(TEXT("OceanColor"), S.OceanColor);
		Mid->SetVectorParameterValue(TEXT("LandColor"), S.LandColor);
		Mid->SetVectorParameterValue(TEXT("LandColor2"), S.LandColor2);
		Mid->SetVectorParameterValue(TEXT("MountainColor"), S.MountainColor);
		Mid->SetVectorParameterValue(TEXT("IceColor"), S.IceColor);
		Mid->SetVectorParameterValue(TEXT("SeedOffset"), Seed);
		Mid->SetScalarParameterValue(TEXT("SeaLevel"), S.SeaLevel);
		Mid->SetScalarParameterValue(TEXT("IceCapStart"), S.IceCapStart);
		Mid->SetScalarParameterValue(TEXT("NoiseScale"), S.NoiseScale);
		Mid->SetScalarParameterValue(TEXT("OceanRoughness"), S.OceanRoughness);
		Mid->SetScalarParameterValue(TEXT("LandRoughness"), S.LandRoughness);
		Mid->SetVectorParameterValue(TEXT("ShallowColor"), S.ShallowColor);
		Mid->SetScalarParameterValue(TEXT("NormalStrength"), S.NormalStrength);
		Mid->SetScalarParameterValue(TEXT("CraterStrength"), S.CraterStrength);
		Mid->SetScalarParameterValue(TEXT("MariaStrength"), S.MariaStrength);
		Mid->SetScalarParameterValue(TEXT("DetailNormalScale"), S.DetailNormalScale);
		Mid->SetScalarParameterValue(TEXT("NightLightIntensity"), S.NightLightIntensity);
		Mid->SetVectorParameterValue(TEXT("NightLightColor"), S.NightLightColor);
		Mid->SetScalarParameterValue(TEXT("LavaEmissive"), S.LavaEmissive);
		Mid->SetScalarParameterValue(TEXT("BandStrength"), S.BandStrength);
		Mid->SetScalarParameterValue(TEXT("BandScale"), S.BandScale);
		Mid->SetScalarParameterValue(TEXT("BandWarp"), S.BandWarp);
		Mid->SetVectorParameterValue(TEXT("SunDirection"), FLinearColor(CachedStarDirection.X, CachedStarDirection.Y, CachedStarDirection.Z, 0.f));
		// HeightScale is the displacement (cm) of the tallest peak: the material normalises land height to 0..1.
		Mid->SetScalarParameterValue(TEXT("HeightScale"), bDetail ? S.ReliefFraction * GetRadiusCm() : 0.f);
	}
	if (CloudMID)
	{
		// Unlit clouds: match the radiance a white lit surface would have under the scene's sunlight (E / pi).
		float SunLux = 30.f;
		if (UWorld* World = GetWorld())
		{
			TActorIterator<ADirectionalLight> LightIt(World);
			if (LightIt)
			{
				if (const UDirectionalLightComponent* LightComp = Cast<UDirectionalLightComponent>(LightIt->GetLightComponent()))
				{
					SunLux = LightComp->Intensity;
				}
			}
		}
		// Cloud albedo ~0.75: brightness is the radiance of a white cloud top under the sun.
		CloudMID->SetScalarParameterValue(TEXT("CloudBrightness"), SunLux / PI * 0.75f);
		CloudMID->SetScalarParameterValue(TEXT("CloudNightLevel"), SunLux / PI * 0.002f);
		CloudMID->SetVectorParameterValue(TEXT("CloudDrift"), FLinearColor(S.CloudDrift, S.CloudDrift * 0.35f, 0.f, 0.f));
		CloudMID->SetVectorParameterValue(TEXT("SunDirection"), FLinearColor(CachedStarDirection.X, CachedStarDirection.Y, CachedStarDirection.Z, 0.f));
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
	if (CollisionSphere) { CollisionSphere->SetSphereRadius((float)RadiusCm, false); }

	Clouds->SetVisibility(Style.bClouds && !bBeyondHorizon);
	// Keep the cloud deck well inside the scattering layer (the volumetric atmosphere is ~AtmosphereDepth/4 deep).
	const float MaxCloudHeight = Style.bAtmosphere ? Style.AtmosphereDepth * 0.25f * 0.45f : Style.CloudHeightFraction;
	Clouds->SetRelativeScale3D(FVector(1.0 + FMath::Min(Style.CloudHeightFraction, MaxCloudHeight)));
	Atmosphere->SetVisibility(Style.bAtmosphere && !bUsingSkyAtmosphere && !bBeyondHorizon);
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

void APlanetBody::SetBeyondHorizon(bool bBeyond)
{
	bBeyondHorizon = bBeyond;
	SetActorHiddenInGame(bBeyond);
#if WITH_EDITOR
	if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		SetIsTemporarilyHiddenInEditor(bBeyond);
	}
#endif
	if (Body) { Body->SetVisibility(!bDetailActive && !bBeyond); }
	if (DetailBody) { DetailBody->SetVisibility(bDetailActive && !bBeyond); }
	if (Clouds) { Clouds->SetVisibility(Style.bClouds && !bBeyond); }
	if (Atmosphere) { Atmosphere->SetVisibility(Style.bAtmosphere && !bUsingSkyAtmosphere && !bBeyond); }
}

void APlanetBody::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyAppearance();
}

void APlanetBody::PreRegisterAllComponents()
{
	Super::PreRegisterAllComponents();
	// Hide before any scene proxy can exist (see IFarBody).
	SetBeyondHorizon(IsLocationBeyondHorizon(PreRegistrationLocation(this)));
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
		CloudYawDeg = FMath::Fmod(CloudYawDeg + Style.CloudSpinDegPerSec * DeltaTime, 360.f);
		Clouds->SetRelativeRotation(FRotator(0.f, CloudYawDeg, 0.f));
		const float Yaw = FMath::DegreesToRadians(CloudYawDeg);
		if (SurfaceMID) { SurfaceMID->SetScalarParameterValue(TEXT("CloudYaw"), Yaw); }
		if (DetailSurfaceMID) { DetailSurfaceMID->SetScalarParameterValue(TEXT("CloudYaw"), Yaw); }
	}

	LODTimer -= DeltaTime;
	if (LODTimer <= 0.f)
	{
		LODTimer = 0.2f;
		// The floating origin keeps the player near the world origin, so distance from the origin is what the GPU sees.
		SetBeyondHorizon(IsLocationBeyondHorizon(GetActorLocation()));
		UpdateLOD();
	}

	// The star direction only changes as bodies move; refresh a few times a second.
	StarSearchTimer -= DeltaTime;
	if (StarSearchTimer <= 0.f)
	{
		StarSearchTimer = 0.25f;
		UpdateStarDirection();
		const FLinearColor SunDir(CachedStarDirection.X, CachedStarDirection.Y, CachedStarDirection.Z, 0.f);
		if (AtmosphereMID) { AtmosphereMID->SetVectorParameterValue(TEXT("SunDirection"), SunDir); }
		if (CloudMID) { CloudMID->SetVectorParameterValue(TEXT("SunDirection"), SunDir); }
		if (SurfaceMID) { SurfaceMID->SetVectorParameterValue(TEXT("SunDirection"), SunDir); }
		if (DetailSurfaceMID) { DetailSurfaceMID->SetVectorParameterValue(TEXT("SunDirection"), SunDir); }
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

void APlanetBody::SetUsingSkyAtmosphere(bool bEnabled)
{
	if (bUsingSkyAtmosphere != bEnabled)
	{
		bUsingSkyAtmosphere = bEnabled;
		Atmosphere->SetVisibility(Style.bAtmosphere && !bUsingSkyAtmosphere && !bBeyondHorizon);
	}
}

void APlanetBody::BuildDetailMesh()
{
	if (bDetailBuilt || !DetailBody)
	{
		return;
	}
	UDynamicMesh* Mesh = DetailBody->GetDynamicMesh();
	if (!Mesh)
	{
		return;
	}
	Mesh->Reset();
	FGeometryScriptPrimitiveOptions Options;
	const int32 Steps = FMath::Clamp(DetailSphereSteps, 8, 200);
	// Unit sphere (radius 1 cm) so the same world scale as the far sphere applies.
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereBox(Mesh, Options, FTransform::Identity, 1.f, Steps, Steps, Steps);
	FGeometryScriptCalculateNormalsOptions NormalOptions;
	UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(Mesh, NormalOptions);
	DetailBody->NotifyMeshUpdated();
	bDetailBuilt = true;
	// The material slot only exists once the mesh does.
	if (DetailSurfaceMID)
	{
		DetailBody->SetMaterial(0, DetailSurfaceMID);
	}
}

void APlanetBody::UpdateLOD()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}
	FVector ViewLoc;
	FRotator ViewRot;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);
	const double RadiusCm = GetRadiusCm();
	const double Dist = FVector::Dist(ViewLoc, GetActorLocation()) / RadiusCm;
	// Hysteresis so the swap never flickers at the boundary.
	const bool bWantDetail = bDetailActive ? Dist < DetailMeshRadii * 1.15 : Dist < DetailMeshRadii;
	if (bWantDetail == bDetailActive)
	{
		return;
	}
	bDetailActive = bWantDetail;
	if (bDetailActive)
	{
		BuildDetailMesh();
		// Unit sphere scales with the body; the far mesh used MeshUnitRadius units.
		DetailBody->SetRelativeScale3D(FVector(MeshUnitRadius));
	}
	DetailBody->SetVisibility(bDetailActive && !bBeyondHorizon);
	Body->SetVisibility(!bDetailActive && !bBeyondHorizon);
}
