#include "PlanetBody.h"
#include "PlanetTerrainComponent.h"
#include "PlanetWeather.h"
#include "PlanetCities.h"
#include "Async/Async.h"
#include "SlonkTiming.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "RockMeshes.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
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
	CollisionSphere->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
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

	auto MakeDyn = [this](const TCHAR* Name, USceneComponent* Parent)
	{
		UDynamicMeshComponent* C = CreateDefaultSubobject<UDynamicMeshComponent>(Name);
		C->SetupAttachment(Parent);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetMobility(EComponentMobility::Movable);
		C->SetVisibility(false);
		C->CastShadow = false;
		C->bCastDynamicShadow = false;
		C->bAffectDistanceFieldLighting = false;
		return C;
	};
	FarBody = MakeDyn(TEXT("FarBody"), Body);
	// Under the cloud shell so it inherits the shell's height scale and yaw.
	CloudsMesh = MakeDyn(TEXT("CloudsMesh"), Clouds);

	Rings = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Rings"));
	Rings->SetupAttachment(Body);
	Rings->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Rings->SetGenerateOverlapEvents(false);
	Rings->SetMobility(EComponentMobility::Movable);
	Rings->SetVisibility(false);
	Rings->CastShadow = false;
	Rings->bCastDynamicShadow = false;
	Rings->bAffectDistanceFieldLighting = false;
	Rings->SetTranslucentSortPriority(2);

	SurfaceMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_PlanetSurface.M_PlanetSurface")));
	CloudMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_PlanetClouds.M_PlanetClouds")));
	BuildingMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_Building.M_Building")));
	AtmosphereMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_PlanetAtmosphere.M_PlanetAtmosphere")));
	RingMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_PlanetRings.M_PlanetRings")));
	TerrainMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_PlanetTerrain.M_PlanetTerrain")));
	RingRockMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::AsteroidPath));
}

namespace
{
	float SmoothStepAny(float Lo, float Hi, float V)
	{
		const float Den = Hi - Lo;
		if (FMath::IsNearlyZero(Den)) { return V >= Hi ? 1.f : 0.f; }
		const float T = FMath::Clamp((V - Lo) / Den, 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}
	float Fbm1(float X, int32 Octaves)
	{
		float Sum = 0.f, Amp = 0.5f, Freq = 1.f;
		for (int32 i = 0; i < Octaves; ++i)
		{
			Sum += Amp * FMath::PerlinNoise1D(X * Freq);
			Amp *= 0.5f;
			Freq *= 2.1f;
		}
		return 0.5f + 0.5f * Sum;
	}
}

void APlanetBody::BuildRingProfile()
{
	// A radial density table shared by the ring material, the ring-shadow terms and the particle field, so all
	// three agree exactly on where the ringlets and gaps are.
	if (RingProfile.Num() == 512 && FMath::IsNearlyEqual(RingProfileSeed, Style.RingSeed) && FMath::IsNearlyEqual(RingProfileDetail, Style.RingDetail))
	{
		return;
	}
	const int32 N = 512;
	RingProfile.SetNum(N);
	RingTint.SetNum(N);
	const float Seed = Style.RingSeed;
	const float Inner = Style.RingInner, Outer = FMath::Max(Style.RingOuter, Inner + 0.05f);
	for (int32 i = 0; i < N; ++i)
	{
		const float X = (i + 0.5f) / N;
		const float R = Inner + X * (Outer - Inner);
		const float N1 = Fbm1(R * 9.f + Seed, 5);
		const float N2 = Fbm1(R * 60.f * Style.RingDetail + Seed * 1.3f, 3);
		float D = FMath::Clamp((0.65f * N1 + 0.35f * N2 - 0.30f) * 2.4f, 0.f, 1.f);
		D *= 1.f - 0.93f * SmoothStepAny(0.03f, 0.f, FMath::Abs(X - 0.63f));
		D *= 1.f - 0.7f * SmoothStepAny(0.014f, 0.f, FMath::Abs(X - 0.81f));
		D *= 1.f - 0.5f * SmoothStepAny(0.010f, 0.f, FMath::Abs(X - 0.36f));
		D *= SmoothStepAny(0.f, 0.05f, X) * SmoothStepAny(1.f, 0.92f, X) * (1.f - 0.45f * X);
		RingProfile[i] = D;
		RingTint[i] = Fbm1(R * 4.f + Seed * 0.7f, 3);
	}
	RingProfileSeed = Style.RingSeed;
	RingProfileDetail = Style.RingDetail;

	if (!RingProfileTexture)
	{
		RingProfileTexture = UTexture2D::CreateTransient(N, 1, PF_B8G8R8A8);
		RingProfileTexture->SRGB = false;
		RingProfileTexture->Filter = TF_Bilinear;
		RingProfileTexture->AddressX = TA_Clamp;
		RingProfileTexture->AddressY = TA_Clamp;
		RingProfileTexture->CompressionSettings = TC_VectorDisplacementmap;
#if WITH_EDITORONLY_DATA
		RingProfileTexture->MipGenSettings = TMGS_NoMipmaps;
#endif
	}
	FTexture2DMipMap& Mip = RingProfileTexture->GetPlatformData()->Mips[0];
	uint8* Data = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	for (int32 i = 0; i < N; ++i)
	{
		Data[i * 4 + 0] = 0;                                              // B
		Data[i * 4 + 1] = (uint8)FMath::RoundToInt(RingTint[i] * 255.f);   // G
		Data[i * 4 + 2] = (uint8)FMath::RoundToInt(RingProfile[i] * 255.f); // R
		Data[i * 4 + 3] = 255;
	}
	Mip.BulkData.Unlock();
	RingProfileTexture->UpdateResource();
}

float APlanetBody::RingDensityAt(float X) const
{
	if (RingProfile.Num() == 0 || X < 0.f || X > 1.f)
	{
		return 0.f;
	}
	const float F = X * (RingProfile.Num() - 1);
	const int32 I0 = FMath::Clamp((int32)F, 0, RingProfile.Num() - 1);
	const int32 I1 = FMath::Min(I0 + 1, RingProfile.Num() - 1);
	return FMath::Lerp(RingProfile[I0], RingProfile[I1], F - I0);
}

FString APlanetBody::GetDisplayName() const
{
	return PlanetName.IsEmpty() ? GetActorNameOrLabel() : PlanetName;
}

float APlanetBody::GetMeanDensityGcc() const
{
	switch (ResolvedArchetype)
	{
	case EPlanetArchetype::GasGiant: return 1.3f;
	case EPlanetArchetype::IceGiant: return 1.6f;
	case EPlanetArchetype::Ice: return 3.0f;
	case EPlanetArchetype::Mars: return 3.9f;
	case EPlanetArchetype::Desert: return 4.5f;
	case EPlanetArchetype::Ocean: return 5.0f;
	case EPlanetArchetype::Lava: return 5.0f;
	case EPlanetArchetype::Venus: return 5.2f;
	case EPlanetArchetype::Moon:
		switch (ResolvedMoonKind)
		{
		case EMoonKind::Icy: return 1.9f;
		case EMoonKind::Lava: return 3.5f;
		default: return 3.3f;
		}
	default: return 5.5f;
	}
}

double APlanetBody::GetSurfaceGravityCmS2() const
{
	// g = (4/3) pi G rho R for a uniform sphere, in SI, then to cm/s^2.
	const double G = 6.674e-11, Rho = GetMeanDensityGcc() * 1000.0, R = GetRadiusCm() / 100.0;
	return (4.0 / 3.0) * PI * G * Rho * R * 100.0;
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

FVector APlanetBody::GetSeedLocation()
{
	if (!bSeedLocationSet)
	{
		SeedLocation = GetActorLocation();
		bSeedLocationSet = true;
	}
	return SeedLocation;
}

void APlanetBody::ResolveStyleFromArchetype()
{
	const FVector L = GetSeedLocation();
	const uint32 Hash = HashCombine(HashCombine(GetTypeHash(FMath::RoundToInt64(L.X / 1000.0)), GetTypeHash(FMath::RoundToInt64(L.Y / 1000.0))), GetTypeHash(FMath::RoundToInt64(L.Z / 1000.0)));
	FRandomStream Rng(static_cast<int32>(Hash));
	// Draws added later use their own stream so the rolls below (rings, spin, ice caps) stay as they were.
	FRandomStream Extra(static_cast<int32>(Hash) * 31 + 7);

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
		S.OceanRoughness = 0.25f;
		break;
	case EPlanetArchetype::Desert:
		S.SeaLevel = Rng.FRandRange(-0.05f, 0.1f);
		S.OceanColor = FLinearColor(0.28f, 0.20f, 0.12f); // dry basins
		S.OceanRoughness = 0.85f;                          // ...so no water-like mirror reflections
		S.LandColor = Jitter(FLinearColor(0.50f, 0.31f, 0.15f), 0.25f);
		S.CraterStrength = 0.45f;
		S.MariaStrength = 0.2f;
		S.CraterScale = 1.3f;
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
		S.LavaEmissive = 160.f;
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
	case EPlanetArchetype::IceGiant:
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
		S.CloudSpinDegPerSec = 0.0006f;
		S.AtmosphereColor = A;
		S.SunsetColor = B;
		S.AtmosphereDepth = 0.1f;
		S.Intensity = 2.6f;
		S.Haze = 0.2f;
		// A gas giant has no terrain: relief would put bumps on its limb.
		S.ReliefFraction = 0.f;
		// Palette families (drawn from the extra stream so the rolls above stay put): the band noise stands in
		// for height, so "ocean" is the belt colour, "shallow" its warm edge, "land"/"land2" the darker belts
		// and "mountain" the bright zones. Finer bands and a lighter cloud deck add the layering.
		{
			const bool bIce = Type == EPlanetArchetype::IceGiant;
			const float Family = PaletteFamily >= 0.f ? PaletteFamily : (bIce ? 0.95f : Extra.FRand());
			FLinearColor Belt, Edge, Dark, Dark2, Zone, Cloud, Atmo, Sunset;
			if (Family < 0.45f)      // Jovian: cream, rust and orange belts, white zones
			{
				Belt = FLinearColor(0.88f, 0.76f, 0.52f); Edge = FLinearColor(0.96f, 0.90f, 0.74f);
				Dark = FLinearColor(0.58f, 0.32f, 0.18f); Dark2 = FLinearColor(0.84f, 0.50f, 0.26f);
				Zone = FLinearColor(0.96f, 0.94f, 0.88f); Cloud = FLinearColor(0.98f, 0.95f, 0.88f);
				Atmo = FLinearColor(0.95f, 0.75f, 0.50f); Sunset = FLinearColor(1.0f, 0.5f, 0.2f);
			}
			else if (Family < 0.8f)  // Saturnian: pale gold and tan, soft zones
			{
				Belt = FLinearColor(0.90f, 0.80f, 0.52f); Edge = FLinearColor(0.97f, 0.92f, 0.74f);
				Dark = FLinearColor(0.58f, 0.38f, 0.24f); Dark2 = FLinearColor(0.84f, 0.60f, 0.34f);
				Zone = FLinearColor(0.98f, 0.95f, 0.86f); Cloud = FLinearColor(0.97f, 0.93f, 0.82f);
				Atmo = FLinearColor(0.95f, 0.82f, 0.55f); Sunset = FLinearColor(1.0f, 0.6f, 0.25f);
			}
			else                     // Neptunian: azure and violet belts, white streaks
			{
				Belt = FLinearColor(0.16f, 0.38f, 0.98f); Edge = FLinearColor(0.38f, 0.58f, 1.0f);
				Dark = FLinearColor(0.08f, 0.12f, 0.58f); Dark2 = FLinearColor(0.40f, 0.24f, 0.88f);
				Zone = FLinearColor(0.86f, 0.90f, 1.0f); Cloud = FLinearColor(0.94f, 0.96f, 1.0f);
				Atmo = FLinearColor(0.35f, 0.55f, 1.0f); Sunset = FLinearColor(0.70f, 0.55f, 1.0f);
			}
			auto J = [&](const FLinearColor& C) { return FLinearColor(C.R * Extra.FRandRange(0.92f, 1.08f), C.G * Extra.FRandRange(0.92f, 1.08f), C.B * Extra.FRandRange(0.92f, 1.08f), 1.f); };
			S.OceanColor = J(Belt); S.ShallowColor = J(Edge); S.LandColor = J(Dark); S.LandColor2 = J(Dark2);
			S.MountainColor = J(Zone); S.CloudColor = J(Cloud); S.AtmosphereColor = J(Atmo); S.SunsetColor = Sunset;
			S.SeaLevel = Extra.FRandRange(0.42f, 0.56f);
			S.BandScale *= Extra.FRandRange(1.2f, 1.8f);
			// The zones carry the high white deck, the belts show the deeper, darker cloud: a little over half covered.
			S.CloudCoverage = Extra.FRandRange(0.5f, 0.6f);
			S.CloudScale = 4.0f;
			if (bIce)
			{
				// Colder and crisper than a gas giant: little white haze, a deep blue limb, a darker base.
				S.Haze = 0.04f;
				S.Intensity = 2.2f;
				S.CloudCoverage = 0.12f;
				S.OceanRoughness = 0.85f;
				S.LandRoughness = 0.85f;
			}
		}
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
		S.CloudSpinDegPerSec = 0.0012f;		// Venus: the deck laps the planet in four days
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
		S.CraterScale = 1.4f;
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
		// Fewer, broader hills than before: the old scale 5 read as a uniform stipple from orbit.
		S.NoiseScale = 3.2f;
		S.bClouds = false;
		S.bAtmosphere = false;
		const float K = Rng.FRand();
		ResolvedMoonKind = bForceMoonKind ? ForcedMoonKind : (K < 0.55f ? EMoonKind::Rocky : (K < 0.85f ? EMoonKind::Icy : EMoonKind::Lava));
		switch (ResolvedMoonKind)
		{
		case EMoonKind::Icy:
			// Bright, smooth ice with sparse craters and a faint blue cast.
			S.LandColor = Jitter(FLinearColor(0.62f, 0.70f, 0.78f), 0.1f);
			S.LandColor2 = Jitter(FLinearColor(0.62f, 0.70f, 0.80f), 0.1f);
			S.MountainColor = FLinearColor(0.78f, 0.82f, 0.88f);
			S.CraterStrength = 0.4f;
			S.MariaStrength = 0.6f;
			S.CraterScale = 0.85f;
			S.LandRoughness = 0.55f;
			S.NormalStrength = 0.12f;
			S.DetailNormalScale = 3.f;
			S.ReliefFraction = 0.005f;
			// Lineae on most icy moons: the long stained cracks of a Europa.
			S.Lineae = bForceMoonKind ? 0.9f : (Rng.FRand() < 0.7f ? Rng.FRandRange(0.5f, 1.f) : 0.f);
			break;
		case EMoonKind::Lava:
			// Dark basalt with molten basins glowing through.
			S.SeaLevel = Rng.FRandRange(0.18f, 0.3f);
			S.OceanColor = FLinearColor(0.9f, 0.18f, 0.02f);
			S.ShallowColor = FLinearColor(1.0f, 0.55f, 0.1f);
			S.LavaEmissive = 110.f;
			S.OceanRoughness = 0.7f;
			S.LandColor = FLinearColor(0.12f, 0.10f, 0.09f);
			S.LandColor2 = FLinearColor(0.22f, 0.15f, 0.11f);
			S.MountainColor = FLinearColor(0.3f, 0.24f, 0.2f);
			S.CraterStrength = 0.4f;
			S.MariaStrength = 0.f;
			S.CraterScale = 1.2f;
			S.LandRoughness = 0.9f;
			S.NormalStrength = 0.22f;
			S.DetailNormalScale = 4.f;
			S.ReliefFraction = 0.012f;
			break;
		default:
			S.LandColor = Jitter(FLinearColor(0.20f, 0.20f, 0.19f), 0.2f);
			S.CraterStrength = 1.f;
			S.MariaStrength = 0.8f;
			S.CraterScale = 1.f;
			S.LandColor2 = Jitter(FLinearColor(0.13f, 0.13f, 0.13f), 0.2f);
			S.MountainColor = FLinearColor(0.30f, 0.30f, 0.29f);
			S.LandRoughness = 0.95f;
			S.NormalStrength = 0.2f;
			S.DetailNormalScale = 3.5f;
			S.ReliefFraction = 0.010f;
			break;
		}
		// A big moon now and then keeps a deep orange haze (Titan): overcast top to bottom, thick layer.
		if (bForceHazy || (RadiusKm > 1800.f && ResolvedMoonKind != EMoonKind::Lava && Extra.FRand() < 0.3f))
		{
			bHazyMoon = true;
			S.bAtmosphere = true;
			S.AtmosphereColor = Jitter(FLinearColor(1.0f, 0.62f, 0.25f), 0.1f);
			S.SunsetColor = FLinearColor(1.0f, 0.45f, 0.15f);
			S.AtmosphereDepth = 0.1f;
			S.Haze = 0.6f;
			S.Intensity = 2.2f;
			S.bClouds = true;
			S.CloudCoverage = 1.0f;
			S.CloudColor = Jitter(FLinearColor(0.95f, 0.72f, 0.38f), 0.08f);
			S.LandColor = FLinearColor(0.35f, 0.24f, 0.14f);
			S.LandColor2 = FLinearColor(0.28f, 0.20f, 0.12f);
			S.MountainColor = FLinearColor(0.42f, 0.30f, 0.20f);
			S.CraterStrength = 0.2f;
		}
		break;
	}
	}

	// Volumetric layer per world: a Rayleigh scale height as a fraction of the radius by archetype, jittered, with
	// the layer top five scale heights up. Stylised thick (Earth's is 0.13% of its radius) so the limb reads from a
	// few radii out; the clouds and relief sit inside the dense part instead of on top of it.
	if (S.bAtmosphere)
	{
		// Real scale heights in km (Earth 8.5, Venus 16, Mars 11, Titan 40, Jupiter 27): the sky has to read right
		// from a canyon floor, and from orbit a real atmosphere is the thin blue rim it is in photographs.
		float ScaleKm = 8.5f;
		switch (Type)
		{
		case EPlanetArchetype::Ocean: ScaleKm = 9.f; break;
		case EPlanetArchetype::Desert: ScaleKm = 7.f; break;
		case EPlanetArchetype::Ice: ScaleKm = 6.5f; break;
		case EPlanetArchetype::Lava: ScaleKm = 11.f; break;
		case EPlanetArchetype::Venus: ScaleKm = 16.f; break;
		case EPlanetArchetype::Mars: ScaleKm = 11.f; break;
		case EPlanetArchetype::GasGiant: ScaleKm = 27.f; break;      // Jupiter's hydrogen, 27 km
		case EPlanetArchetype::IceGiant: ScaleKm = 20.f; break;
		case EPlanetArchetype::Moon: ScaleKm = 40.f; break;       // Titan-like haze
		default: break;
		}
		ScaleKm *= Extra.FRandRange(0.85f, 1.2f);
		S.AtmosphereScaleHeightKm = ScaleKm;
		S.AtmosphereHeightKm = FMath::Min(S.AtmosphereScaleHeightKm * 12.f, RadiusKm * 0.15f);
	}
	// The volumetric cloud layer per world, km above the surface, and the cloud shell's height with it.
	{
		switch (Type)
		{
		case EPlanetArchetype::Terrestrial: S.CloudBaseKm = Rng.FRandRange(1.5f, 2.5f); S.CloudThicknessKm = 6.f; S.CloudDensity = 1.f; S.CloudDeck = 0.f; break;
		case EPlanetArchetype::Ocean: S.CloudBaseKm = Rng.FRandRange(1.5f, 2.5f); S.CloudThicknessKm = 6.f; S.CloudDensity = 1.f; S.CloudDeck = 0.3f; break;
		case EPlanetArchetype::Desert: S.CloudBaseKm = Rng.FRandRange(3.f, 5.f); S.CloudThicknessKm = 4.f; S.CloudDensity = 0.6f; S.CloudDeck = 0.f; break;
		case EPlanetArchetype::Ice: S.CloudBaseKm = Rng.FRandRange(1.2f, 2.f); S.CloudThicknessKm = 5.f; S.CloudDensity = 0.8f; S.CloudDeck = 0.4f; break;
		case EPlanetArchetype::Lava: S.CloudBaseKm = 2.f; S.CloudThicknessKm = 6.f; S.CloudDensity = 1.2f; S.CloudDeck = 0.5f; break;
		// Venus: the sulphuric acid deck from 48 to 70 km, continuous and opaque.
		case EPlanetArchetype::Venus: S.CloudBaseKm = 48.f; S.CloudThicknessKm = 22.f; S.CloudDensity = 2.f; S.CloudDeck = 1.f; break;
		// Mars: thin water-ice clouds 10 to 20 km up, wisps.
		case EPlanetArchetype::Mars: S.CloudBaseKm = 10.f; S.CloudThicknessKm = 10.f; S.CloudDensity = 0.6f; S.CloudDeck = 0.f; break;
		// The giants: a deep layer over the painted deck (the 10 bar level), zones and belts from the weather map;
		// an ice giant only a few white streaks over its blue haze.
		case EPlanetArchetype::GasGiant: S.CloudBaseKm = 8.f; S.CloudThicknessKm = 50.f; S.CloudDensity = 1.2f; S.CloudDeck = 0.f; break;
		case EPlanetArchetype::IceGiant: S.CloudBaseKm = 5.f; S.CloudThicknessKm = 25.f; S.CloudDensity = 0.6f; S.CloudDeck = 0.f; break;
		default:
			// A hazy moon (Titan): a photochemical haze from 30 km up to 90, thin and continuous.
			if (S.bClouds) { S.CloudBaseKm = 30.f; S.CloudThicknessKm = 60.f; S.CloudDensity = 0.15f; S.CloudDeck = 1.f; }
			else { S.CloudThicknessKm = 0.f; }
			break;
		}
		// The shell sits at the top of the layer (Earth 10 km, not the 38 km of the old 0.6%).
		if (S.CloudThicknessKm > 0.f) { S.CloudHeightFraction = (S.CloudBaseKm + S.CloudThicknessKm) / FMath::Max(RadiusKm, 1.f); }
	}

	if (Rng.FRand() < 0.25f)
	{
		S.IceCapStart = FMath::Min(S.IceCapStart, Rng.FRandRange(0.7f, 0.9f));
	}
	Style = S;
	// The layer was placed from sea level, but on a world with relief most land stands a kilometre or more above
	// it, so over the plateaus people fly across and land on the cloud base sat on the ground. Lift the layer by
	// the typical land height, read once from the height field at a coarse wavelength (a hundred-odd samples).
	if (Style.CloudThicknessKm > 0.f && Style.ReliefFraction > 0.f && Style.BandStrength < 0.5f
		&& Type != EPlanetArchetype::GasGiant && Type != EPlanetArchetype::IceGiant)
	{
		const FPlanetTerrainParams P = MakeTerrainParams();
		TArray<double> Land;
		const int32 N = 128;
		for (int32 i = 0; i < N; ++i)
		{
			const double Z = 1.0 - 2.0 * (i + 0.5) / N;
			const double Rxy = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
			const double Phi = i * 2.399963229728653; // Fibonacci sphere
			const FPlanetTerrainSample Sm = SlonkTerrain::Sample(P, FVector(Rxy * FMath::Cos(Phi), Rxy * FMath::Sin(Phi), Z), 2.0e6);
			if (!Sm.bWater && Sm.HeightCm > 0.0) { Land.Add(Sm.HeightCm); }
		}
		if (Land.Num() >= 8)
		{
			// The upper-middle of the land heights (not the median): the uplands are where the mountains, and the
			// shuttle landings, are, and a base that clears two thirds of the land reads as a sky, not a ceiling.
			Land.Sort();
			const float LandKm = (float)FMath::Clamp(Land[(Land.Num() * 2) / 3] / 1.0e5, 0.0, 6.0);
			Style.CloudBaseKm += LandKm;
			Style.CloudHeightFraction = (Style.CloudBaseKm + Style.CloudThicknessKm) / FMath::Max(RadiusKm, 1.f);
			UE_LOG(LogTemp, Log, TEXT("%s: cloud base lifted %.1f km over the land (median %.1f km, peaks %.1f km) to %.1f km"), *GetDisplayName(), LandKm, Land[Land.Num() / 2] / 1.0e5, Land.Last() / 1.0e5, Style.CloudBaseKm);
		}
	}
	// Real day lengths (9 to 70 hours): the ground under a ship flying the terrain must not race past at km/s.
	SpinDegPerSec = Rng.FRandRange(0.1f, 0.8f) * (Rng.FRand() < 0.2f ? -1.f : 1.f) * 0.014f;
	if (Type == EPlanetArchetype::GasGiant || Type == EPlanetArchetype::IceGiant)
	{
		SpinDegPerSec *= 2.f;
	}
	// Weather drifts over the ground at a jet-stream pace (30 to 70 m/s on an Earth): the ray-marched layer is
	// anchored to the shell's frame, so anything faster races across the sky from the ground.
	Style.CloudSpinDegPerSec = (Rng.FRand() < 0.5f ? -1.f : 1.f) * Rng.FRandRange(0.3f, 0.7f) * 0.001f * (6371.f / FMath::Max(RadiusKm, 300.f));
	Style.CloudDrift = Rng.FRandRange(0.02f, 0.05f);
	if (Type == EPlanetArchetype::GasGiant || Type == EPlanetArchetype::IceGiant)
	{
		Style.CloudSpinDegPerSec *= 1.5f;
	}

	// Rings: most gas giants, some ice worlds, the odd rocky planet. Rolled last so the draws above are stable.
	float RingChance = 0.f;
	switch (Type)
	{
	case EPlanetArchetype::GasGiant: RingChance = 0.7f; break;
	case EPlanetArchetype::IceGiant: RingChance = 1.f; break;
	case EPlanetArchetype::Ice: RingChance = 0.35f; break;
	case EPlanetArchetype::Ocean: case EPlanetArchetype::Terrestrial: case EPlanetArchetype::Desert: RingChance = 0.1f; break;
	default: break;
	}
	if (Rng.FRand() < RingChance)
	{
		Style.bRings = true;
		Style.RingInner = Rng.FRandRange(1.25f, 1.7f);
		Style.RingOuter = Style.RingInner + Rng.FRandRange(0.5f, 1.5f);
		Style.RingSeed = Rng.FRandRange(0.f, 100.f);
		Style.RingDetail = Rng.FRandRange(0.7f, 1.4f);
		Style.RingOpacity = Rng.FRandRange(0.6f, 1.f);
		Style.RingColor = Type == EPlanetArchetype::GasGiant ? Jitter(FLinearColor(0.82f, 0.76f, 0.64f), 0.15f) : Jitter(FLinearColor(0.78f, 0.82f, 0.9f), 0.1f);
		if (Type == EPlanetArchetype::IceGiant)
		{
			// A light, narrow ring of comet ice: a thin bright band, translucent and clumpy.
			Style.RingInner = Rng.FRandRange(1.85f, 2.0f);
			Style.RingOuter = Style.RingInner + Rng.FRandRange(0.28f, 0.42f);
			Style.RingOpacity = Rng.FRandRange(0.25f, 0.35f);
			Style.RingDetail = 1.8f;
			Style.RingColor = Jitter(FLinearColor(0.90f, 0.95f, 1.0f), 0.04f);
		}
	}
}

TArray<UMaterialInstanceDynamic*> APlanetBody::SurfaceMIDs() const
{
	TArray<UMaterialInstanceDynamic*> Out;
	if (SurfaceMID) { Out.Add(SurfaceMID); }
	if (DetailSurfaceMID) { Out.Add(DetailSurfaceMID); }
	for (UMaterialInstanceDynamic* Mid : PatchMIDs)
	{
		if (Mid) { Out.Add(Mid); }
	}
	return Out;
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
	if (CloudMID && bFarBuilt && CloudsMesh->GetMaterial(0) != CloudMID)
	{
		CloudsMesh->SetMaterial(0, CloudMID);
	}
	if (SurfaceMID && bFarBuilt && FarBody->GetMaterial(0) != SurfaceMID)
	{
		FarBody->SetMaterial(0, SurfaceMID);
	}
	EnsureMID(AtmosphereMID, AtmosphereMaterial, Atmosphere, TEXT("PlanetAtmosphere"), false);

	const FPlanetStyle& S = Style;
	// A per-planet offset so two planets with the same style never share a coastline.
	const FVector L = GetSeedLocation();
	const FLinearColor Seed(FMath::Frac(L.X / 7.3e9) * 100.f, FMath::Frac(L.Y / 5.1e9) * 100.f, FMath::Frac(L.Z / 3.7e9) * 100.f, 0.f);

	if (!RingMID && Rings)
	{
		if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(RingMaterial, TEXT("PlanetRings"), false))
		{
			RingMID = UMaterialInstanceDynamic::Create(Mat, this);
		}
	}
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
	const float RingInner = S.bRings ? S.RingInner : 0.f;
	const float RingOuter = S.bRings ? S.RingOuter : 0.f;
	if (S.bRings)
	{
		BuildRingProfile();
	}

	for (UMaterialInstanceDynamic* Mid : SurfaceMIDs())
	{
		// Relief only on the dense meshes: on the 5k-triangle far sphere it just shows the facets.
		const bool bDetail = Mid != SurfaceMID;
		Mid->SetScalarParameterValue(TEXT("CraterScale"), S.CraterScale);
		Mid->SetScalarParameterValue(TEXT("RingInner"), RingInner);
		Mid->SetScalarParameterValue(TEXT("RingOuter"), RingOuter);
		Mid->SetScalarParameterValue(TEXT("RingSeed"), S.RingSeed);
		Mid->SetScalarParameterValue(TEXT("RingDetail"), S.RingDetail);
		Mid->SetScalarParameterValue(TEXT("RingOpacity"), S.RingOpacity);
		if (RingProfileTexture) { Mid->SetTextureParameterValue(TEXT("RingProfile"), RingProfileTexture); }
		Mid->SetScalarParameterValue(TEXT("PlanetRadiusCm"), GetRadiusCm());
		Mid->SetScalarParameterValue(TEXT("CloudScale"), S.CloudScale);
		Mid->SetScalarParameterValue(TEXT("Coverage"), S.bClouds ? S.CloudCoverage : 0.f);
		// Worlds with a ray-marched layer get the shell broken into kilometre cells near the planet, so the two
		// match at the hand-over; the giants' decks stay whole.
		Mid->SetScalarParameterValue(TEXT("CellStrength"), S.CloudThicknessKm > 0.f ? 0.85f : 0.f);
		// While the Sky Atmosphere is parked here the shell is lit by the sun through that atmosphere (the same
		// reddening toward the terminator the ray-marched layer gets); elsewhere by its own analytic sun.
		Mid->SetScalarParameterValue(TEXT("SkyLit"), bUsingSkyAtmosphere ? 1.f : 0.f);
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
		// A giant's shell zones let the painted belts show through: the deck is the look, the shell its white tops.
		CloudMID->SetScalarParameterValue(TEXT("CloudOpacity"), (IsGiant() ? 0.6f : 0.85f) * CloudShellFade);
		// Unlit clouds: match the radiance a white lit surface would have under the scene's sunlight (E / pi).
		CloudMID->SetScalarParameterValue(TEXT("RingInner"), RingInner);
		CloudMID->SetScalarParameterValue(TEXT("RingOuter"), RingOuter);
		CloudMID->SetScalarParameterValue(TEXT("RingSeed"), S.RingSeed);
		CloudMID->SetScalarParameterValue(TEXT("RingDetail"), S.RingDetail);
		CloudMID->SetScalarParameterValue(TEXT("RingOpacity"), S.RingOpacity);
		if (RingProfileTexture) { CloudMID->SetTextureParameterValue(TEXT("RingProfile"), RingProfileTexture); }
		// Cloud albedo ~0.75: brightness is the radiance of a white cloud top under the sun.
		CloudMID->SetScalarParameterValue(TEXT("CloudBrightness"), SunLux / PI * 0.75f);
		CloudMID->SetScalarParameterValue(TEXT("CloudNightLevel"), SunLux / PI * 0.002f);
		CloudMID->SetVectorParameterValue(TEXT("CloudDrift"), FLinearColor(S.CloudDrift, S.CloudDrift * 0.35f, 0.f, 0.f));
		CloudMID->SetVectorParameterValue(TEXT("SunDirection"), FLinearColor(CachedStarDirection.X, CachedStarDirection.Y, CachedStarDirection.Z, 0.f));
		CloudMID->SetScalarParameterValue(TEXT("Coverage"), S.CloudCoverage);
		CloudMID->SetScalarParameterValue(TEXT("CloudScale"), S.CloudScale);
		CloudMID->SetVectorParameterValue(TEXT("CloudColor"), S.CloudColor);
		CloudMID->SetVectorParameterValue(TEXT("SeedOffset"), Seed * 0.37f);
		if (WeatherMap) { CloudMID->SetTextureParameterValue(TEXT("WeatherMap"), WeatherMap); }
		CloudMID->SetScalarParameterValue(TEXT("WeatherStrength"), WeatherStrength);
		CloudMID->SetScalarParameterValue(TEXT("CloudDeck"), S.CloudDeck);
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
	for (UMaterialInstanceDynamic* Mid : { CloudMID.Get(), AtmosphereMID.Get(), RingMID.Get() })
	{
		if (Mid) { Mid->SetScalarParameterValue(TEXT("SkyBehind"), SkyBehind); }
	}
	for (UMaterialInstanceDynamic* Mid : SurfaceMIDs()) { Mid->SetScalarParameterValue(TEXT("SkyBehind"), SkyBehind); }
	if (RingMID)
	{
		RingMID->SetScalarParameterValue(TEXT("RingInner"), S.RingInner);
		RingMID->SetScalarParameterValue(TEXT("RingOuter"), S.RingOuter);
		RingMID->SetScalarParameterValue(TEXT("RingSeed"), S.RingSeed);
		RingMID->SetScalarParameterValue(TEXT("RingDetail"), S.RingDetail);
		RingMID->SetScalarParameterValue(TEXT("RingOpacity"), S.RingOpacity);
		RingMID->SetVectorParameterValue(TEXT("RingColor"), S.RingColor);
		if (RingProfileTexture) { RingMID->SetTextureParameterValue(TEXT("RingProfile"), RingProfileTexture); }
		// Ice particles ~0.8 albedo under the scene sun; the material does its own lighting.
		RingMID->SetScalarParameterValue(TEXT("RingBrightness"), SunLux / PI * 0.8f);
	}
	UpdateSunDirectionParams();
}

void APlanetBody::RequestWeatherMap(const FLinearColor& Seed)
{
	bWeatherRequested = true;
	FPlanetWeatherSpec Spec;
	Spec.Seed = FVector(Seed.R, Seed.G, Seed.B) * 0.37f;
	Spec.Scale = Style.CloudScale;
	// Ocean worlds run to long fronts; cold and thin-aired worlds to scattered masses.
	Spec.Fronts = Archetype == EPlanetArchetype::Ocean ? 0.7f : (Archetype == EPlanetArchetype::Terrestrial ? 0.5f : 0.3f);
	// Giants: zones and belts by latitude, festooned, with a few storm ovals; the type channel is zone against belt.
	Spec.Zonal = IsGiant() ? 1.f : 0.f;
	Spec.BandScale = Style.BandScale;
	// 20 km texels on an Earth, 40 on a small world: the cells are a few texels, the fronts hundreds.
	WeatherW = RadiusKm > 4000.f ? 2048 : 1024;
	WeatherH = WeatherW / 2;
	WeatherPixels = MakeShared<TArray<FColor>, ESPMode::ThreadSafe>();
	TSharedPtr<TArray<FColor>, ESPMode::ThreadSafe> Px = WeatherPixels;
	const int32 W = WeatherW, H = WeatherH;
	const FString Dump = FPlatformMisc::GetEnvironmentVariable(TEXT("SLONK_DUMP_WEATHER")).IsEmpty() ? FString() : FPaths::ProjectSavedDir() / TEXT("Weather") / GetName() + TEXT(".png");
	WeatherFuture = Async(EAsyncExecution::ThreadPool, [Spec, W, H, Px, Dump]()
	{
		SlonkWeather::Generate(Spec, W, H, *Px);
		if (!Dump.IsEmpty()) { SlonkWeather::SavePng(*Px, W, H, Dump); }
	});
}

void APlanetBody::PollWeatherMap(float DeltaTime)
{
	if (WeatherFuture.IsValid() && WeatherFuture.IsReady())
	{
		WeatherFuture = TFuture<void>();
		if (WeatherPixels.IsValid())
		{
			WeatherMap = SlonkWeather::MakeTexture(*WeatherPixels, WeatherW, WeatherH, *FString::Printf(TEXT("Weather_%s"), *GetName()));
			UE_LOG(LogTemp, Log, TEXT("%s: weather map %dx%d ready"), *GetName(), WeatherW, WeatherH);
		}
		WeatherPixels.Reset();
		if (WeatherMap && CloudMID) { CloudMID->SetTextureParameterValue(TEXT("WeatherMap"), WeatherMap); }
	}
	if (WeatherMap && WeatherStrength < 1.f)
	{
		// Fade the map in over the shell's own noise rather than swapping the sky in one frame.
		WeatherStrength = FMath::Min(1.f, WeatherStrength + DeltaTime * 0.5f);
		if (CloudMID) { CloudMID->SetScalarParameterValue(TEXT("WeatherStrength"), WeatherStrength); }
	}
}

bool APlanetBody::HasCities() const
{
	return Style.NightLightIntensity > 0.f && !IsGiant() && Style.ReliefFraction > 0.f && CityReachKm > 0.f;
}

void APlanetBody::ClearCities()
{
	if (Buildings && Buildings->GetInstanceCount() > 0) { Buildings->ClearInstances(); }
	CityActiveTowns.Reset();
	CityCentreLocal = FVector(1.0e30, 0.0, 0.0);
}

void APlanetBody::UpdateCities(const FVector& ViewLoc, float DeltaTime)
{
	// A finished build lands first.
	if (CityFuture.IsValid() && CityFuture.IsReady())
	{
		CityFuture = TFuture<void>();
		if (CityPending.IsValid())
		{
			if (!Buildings)
			{
				Buildings = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, TEXT("Buildings"));
				Buildings->SetupAttachment(GetRootComponent());
				Buildings->SetUsingAbsoluteScale(true);		// the root carries the sphere's scale; instances are planet-local cm
				Buildings->SetMobility(EComponentMobility::Movable);
				Buildings->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
				Buildings->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Buildings->SetCollisionProfileName(TEXT("BlockAll"));
				Buildings->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);		// the crew's boots
				Buildings->SetGenerateOverlapEvents(false);
				Buildings->CastShadow = true;
				Buildings->bAffectDistanceFieldLighting = false;
				Buildings->SetCullDistances(0, (int32)(CityReachKm * 100000.f));
				Buildings->NumCustomDataFloats = 3;		// the building's size in metres, for the window grid
				if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(BuildingMaterial, TEXT("Building"), false))
				{
					BuildingMID = UMaterialInstanceDynamic::Create(Mat, this);
					Buildings->SetMaterial(0, BuildingMID);
				}
				Buildings->RegisterComponent();
			}
			if (CityPending->Towns.Difference(CityActiveTowns).Num() > 0 || CityActiveTowns.Difference(CityPending->Towns).Num() > 0)
			{
				Buildings->ClearInstances();
				if (CityPending->Instances.Num() > 0)
				{
					Buildings->AddInstances(CityPending->Instances, false, false);
					for (int32 i = 0; i < CityPending->Instances.Num(); ++i)
					{
						const FVector Sc = CityPending->Instances[i].GetScale3D();
						Buildings->SetCustomDataValue(i, 0, (float)Sc.X, false);
						Buildings->SetCustomDataValue(i, 1, (float)Sc.Y, false);
						Buildings->SetCustomDataValue(i, 2, (float)Sc.Z, false);
					}
					Buildings->MarkRenderStateDirty();
				}
				CityActiveTowns = CityPending->Towns;
				UE_LOG(LogTemp, Log, TEXT("%s: %d towns, %d buildings within %.0f km"), *GetDisplayName(), CityPending->TownCount, CityPending->Instances.Num(), CityReachKm);
			}
		}
		CityPending.Reset();
	}
	if (BuildingMID)
	{
		// The material compares the sun with each instance's up in world space.
		BuildingMID->SetVectorParameterValue(TEXT("SunDirectionLocal"), FLinearColor(CachedStarDirection.X, CachedStarDirection.Y, CachedStarDirection.Z, 0.f));
	}
	CityTimer -= DeltaTime;
	if (CityTimer > 0.f || CityFuture.IsValid()) { return; }
	CityTimer = 0.7f;
	if (!HasCities()) { return; }
	const FVector Local = Body ? Body->GetComponentTransform().InverseTransformPosition(ViewLoc) : ViewLoc - GetActorLocation();
	const double AltitudeCm = Local.Size() - GetRadiusCm();
	if (AltitudeCm > CityReachKm * 100000.0 * 4.0)
	{
		if (CityActiveTowns.Num() > 0) { ClearCities(); }
		return;
	}
	// Only re-enumerate once the view has moved a sixth of the reach.
	const FVector Centre = Local.GetSafeNormal() * GetRadiusCm();
	if (FVector::Dist(Centre, CityCentreLocal) < CityReachKm * 100000.0 / 6.0) { return; }
	CityCentreLocal = Centre;
	const FPlanetTerrainParams P = TerrainParams;
	const double Reach = CityReachKm * 100000.0;
	CityPending = MakeShared<FCityBuild, ESPMode::ThreadSafe>();
	TSharedPtr<FCityBuild, ESPMode::ThreadSafe> Res = CityPending;
	CityFuture = Async(EAsyncExecution::ThreadPool, [P, Centre, Reach, Res]()
	{
		TArray<FTownSite> Towns;
		SlonkCities::EnumerateTowns(P, Centre, Reach, Towns);
		Res->TownCount = Towns.Num();
		for (const FTownSite& T : Towns)
		{
			Res->Towns.Add(T.Seed);
			if (Res->Instances.Num() > 60000) { break; }
			SlonkCities::BuildTown(P, T, Res->Instances, 5000);
		}
	});
}

float APlanetBody::PressureBarAt(double AltitudeCm) const
{
	const float ScaleKm = Style.AtmosphereScaleHeightKm > 0.f ? Style.AtmosphereScaleHeightKm : 20.f;
	return 10.f * FMath::Exp(-(float)(AltitudeCm / 100000.0) / ScaleKm);
}

void APlanetBody::SetLayerReachKm(float ReachKm)
{
	if (FMath::IsNearlyEqual(LayerReachKm, ReachKm, 1.f)) { return; }
	const bool bWasOn = LayerReachKm < 1.0e8f;
	LayerReachKm = ReachKm;
	if (CloudMID) { CloudMID->SetScalarParameterValue(TEXT("LayerReachKm"), ReachKm); }
	if (bWasOn != (ReachKm < 1.0e8f)) { UpdateShellVisibility(); }
}

FTransform APlanetBody::GetCloudFrame() const
{
	const USceneComponent* Frame = CloudsMesh ? static_cast<const USceneComponent*>(CloudsMesh) : static_cast<const USceneComponent*>(Clouds);
	return Frame ? Frame->GetComponentTransform() : GetActorTransform();
}

void APlanetBody::UpdateSunDirectionParams()
{
	const FLinearColor SunDir(CachedStarDirection.X, CachedStarDirection.Y, CachedStarDirection.Z, 0.f);
	// The ring plane is the body's local XY, so shadows need the star bearing in each component's own frame
	// (the cloud shell is yawed relative to the body).
	auto LocalDir = [this](const USceneComponent* C)
	{
		const FVector L = C ? C->GetComponentTransform().InverseTransformVectorNoScale(CachedStarDirection).GetSafeNormal() : CachedStarDirection;
		return FLinearColor(L.X, L.Y, L.Z, 0.f);
	};
	const FLinearColor BodyLocal = LocalDir(Body);
	if (AtmosphereMID) { AtmosphereMID->SetVectorParameterValue(TEXT("SunDirection"), SunDir); }
	if (CloudMID)
	{
		CloudMID->SetVectorParameterValue(TEXT("SunDirection"), SunDir);
		CloudMID->SetVectorParameterValue(TEXT("SunDirectionLocal"), LocalDir(Clouds));
	}
	for (UMaterialInstanceDynamic* Mid : SurfaceMIDs())
	{
		Mid->SetVectorParameterValue(TEXT("SunDirection"), SunDir);
		Mid->SetVectorParameterValue(TEXT("SunDirectionLocal"), BodyLocal);
	}
	if (Terrain && bTerrainActive)
	{
		Terrain->SetVectorParameter(TEXT("SunDirection"), SunDir);
		Terrain->SetVectorParameter(TEXT("SunDirectionLocal"), BodyLocal);
	}
	if (RingMID)
	{
		RingMID->SetVectorParameterValue(TEXT("SunDirection"), SunDir);
		RingMID->SetVectorParameterValue(TEXT("SunDirectionLocal"), BodyLocal);
	}
}

void APlanetBody::ApplyAppearance()
{
	FSlonkScopeTimer SlonkTimer_(TEXT("ApplyAppearance PlanetBody"));
	if (bStyleFromArchetype)
	{
		ResolveStyleFromArchetype();
	}

	// Body is the root: set its world scale so the planet is exactly RadiusKm whatever the spawner did to the actor scale.
	const double RadiusCm = GetRadiusCm();
	Body->SetWorldScale3D(FVector(RadiusCm / MeshUnitRadius));
	if (CollisionSphere) { CollisionSphere->SetSphereRadius((float)RadiusCm, false); }

	// Keep the cloud deck well inside the scattering layer (the volumetric atmosphere is ~AtmosphereDepth/4 deep).
	const float MaxCloudHeight = Style.bAtmosphere ? Style.AtmosphereDepth * 0.25f * 0.45f : Style.CloudHeightFraction;
	Clouds->SetRelativeScale3D(FVector(1.0 + FMath::Min(Style.CloudHeightFraction, MaxCloudHeight)));
	Atmosphere->SetRelativeScale3D(FVector(1.0 + Style.AtmosphereDepth));

	UpdateStarDirection();
	BuildFarMeshes();
	ApplyMaterials();
	EnsureTerrain();
	BuildRings();
	UpdateShellVisibility();
}

FPlanetTerrainParams APlanetBody::MakeTerrainParams() const
{
	FPlanetTerrainParams P;
	const FPlanetStyle& S = Style;
	const double R = GetRadiusCm();
	const FVector L = const_cast<APlanetBody*>(this)->GetSeedLocation();
	P.Seed = FVector(FMath::Frac(L.X / 7.3e9) * 100.0, FMath::Frac(L.Y / 5.1e9) * 100.0, FMath::Frac(L.Z / 3.7e9) * 100.0);
	P.RadiusCm = R;
	// Real proportions: Earth's tallest peak is 0.14% of its radius, Mars's 0.6%. The style's ReliefFraction was
	// tuned for relief visible from orbit on the old displaced sphere, four times too tall for flying through.
	double ReliefFraction = S.ReliefFraction;
	switch (ResolvedArchetype)
	{
	case EPlanetArchetype::Mars: ReliefFraction = FMath::Min(ReliefFraction, 0.006f); break;
	case EPlanetArchetype::Moon: break;
	case EPlanetArchetype::Desert: case EPlanetArchetype::Venus: ReliefFraction = FMath::Min(ReliefFraction, 0.002f); break;
	default: ReliefFraction = FMath::Min(ReliefFraction, 0.0016f); break;
	}
	P.ReliefCm = FMath::Max(ReliefFraction * R, 1.0);
	P.OceanDepthCm = 0.0008 * R;
	P.SeaLevel = S.SeaLevel;
	P.ContinentScale = S.NoiseScale;
	P.Craters = S.CraterStrength;
	P.CraterScale = S.CraterScale;
	P.Maria = S.MariaStrength;
	P.Lineae = S.Lineae;
	P.IceCapStart = S.IceCapStart;
	P.bLava = S.LavaEmissive > 0.f;
	// Canyons scale with the body: an Earth gets a Grand Canyon, Mars a Valles Marineris.
	const double Earth = R / 6.371e8;
	switch (ResolvedArchetype)
	{
	case EPlanetArchetype::Ocean:   P.Mountains = 0.4f; P.Canyons = 0.3f; P.Strata = 0.3f; P.Moisture = 0.8f; P.CanyonDepthCm = 1.2e5 * Earth; P.CanyonWidthCm = 6.0e5 * Earth; break;
	case EPlanetArchetype::Desert:  P.Mountains = 0.5f; P.Canyons = 0.9f; P.Strata = 0.9f; P.Moisture = 0.05f; P.CanyonDepthCm = 2.0e5 * Earth; P.CanyonWidthCm = 9.0e5 * Earth; break;
	case EPlanetArchetype::Ice:     P.Mountains = 0.35f; P.Canyons = 0.2f; P.Strata = 0.3f; P.Moisture = 0.5f; P.CanyonDepthCm = 1.0e5 * Earth; P.CanyonWidthCm = 6.0e5 * Earth; break;
	case EPlanetArchetype::Lava:    P.Mountains = 0.5f; P.Canyons = 0.3f; P.Strata = 0.3f; P.Moisture = 0.f; P.CanyonDepthCm = 1.5e5 * Earth; P.CanyonWidthCm = 7.0e5 * Earth; break;
	case EPlanetArchetype::Mars:    P.Mountains = 0.7f; P.Canyons = 1.0f; P.Strata = 0.8f; P.Moisture = 0.f; P.bAirless = true; P.CanyonDepthCm = 6.0e5 * Earth * 1.9; P.CanyonWidthCm = 1.2e7 * Earth * 1.9; break;
	case EPlanetArchetype::Venus:   P.Mountains = 0.5f; P.Canyons = 0.3f; P.Strata = 0.3f; P.Moisture = 0.f; P.bAirless = true; P.CanyonDepthCm = 1.5e5 * Earth; P.CanyonWidthCm = 8.0e5 * Earth; break;
	case EPlanetArchetype::Moon:    P.Mountains = 0.3f; P.Canyons = 0.f; P.Strata = 0.2f; P.Moisture = 0.f; P.bAirless = true; break;
	default:                        P.Mountains = 0.6f; P.Canyons = 0.5f; P.Strata = 0.6f; P.Moisture = 0.55f; P.CanyonDepthCm = 1.6e5 * Earth; P.CanyonWidthCm = 7.0e5 * Earth; break;
	}
	return P;
}

void APlanetBody::EnsureTerrain()
{
	UWorld* World = GetWorld();
	const bool bWant = bUseTerrainChunks && World && World->IsGameWorld() && Style.ReliefFraction > 0.f && Style.BandStrength < 0.5f;
	if (!bWant)
	{
		if (Terrain) { Terrain->SetTerrainVisible(false); }
		bTerrainActive = false;
		return;
	}
	if (!Terrain)
	{
		Terrain = NewObject<UPlanetTerrainComponent>(this, TEXT("Terrain"));
		Terrain->SetupAttachment(Body);
		Terrain->RegisterComponent();
		Terrain->SetRelativeLocation(FVector::ZeroVector);
		Terrain->SetWorldScale3D(FVector::OneVector);
	}
	const FPlanetTerrainParams P = MakeTerrainParams();
	if (!bTerrainActive || P != TerrainParams)
	{
		TerrainParams = P;
		UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(TerrainMaterial, TEXT("PlanetTerrain"), true);
		Terrain->Configure(P, Mat);
	}
	bTerrainActive = true;
	Terrain->SetTerrainVisible(true);
	ApplyTerrainMaterialParams();
}

void APlanetBody::ApplyTerrainMaterialParams()
{
	if (!Terrain || !bTerrainActive) { return; }
	const FPlanetStyle& S = Style;
	Terrain->SetScalarParameter(TEXT("SkyBehind"), SkyBehind);
	Terrain->SetVectorParameter(TEXT("OceanColor"), S.OceanColor);
	Terrain->SetVectorParameter(TEXT("ShallowColor"), S.ShallowColor);
	Terrain->SetVectorParameter(TEXT("LandColor"), S.LandColor);
	Terrain->SetVectorParameter(TEXT("LandColor2"), S.LandColor2);
	Terrain->SetVectorParameter(TEXT("MountainColor"), S.MountainColor);
	Terrain->SetVectorParameter(TEXT("IceColor"), S.IceColor);
	Terrain->SetVectorParameter(TEXT("NightLightColor"), S.NightLightColor);
	Terrain->SetScalarParameter(TEXT("OceanRoughness"), S.OceanRoughness);
	Terrain->SetScalarParameter(TEXT("LandRoughness"), S.LandRoughness);
	Terrain->SetScalarParameter(TEXT("NightLightIntensity"), S.NightLightIntensity);
	Terrain->SetScalarParameter(TEXT("LavaEmissive"), S.LavaEmissive);
	Terrain->SetScalarParameter(TEXT("CloudScale"), S.CloudScale);
	Terrain->SetScalarParameter(TEXT("Coverage"), S.bClouds ? S.CloudCoverage : 0.f);
	Terrain->SetVectorParameter(TEXT("SunDirection"), FLinearColor(CachedStarDirection.X, CachedStarDirection.Y, CachedStarDirection.Z, 0.f));
}

double APlanetBody::GetSurfaceRadiusCm(const FVector& WorldPos) const
{
	const double R = GetRadiusCm();
	return bTerrainActive && Terrain ? R + Terrain->SurfaceHeightAtWorld(WorldPos) : R;
}

void APlanetBody::BuildFarMeshes()
{
	UWorld* World = GetWorld();
	if (bFarBuilt || !World || !World->IsGameWorld() || !FarBody || !CloudsMesh)
	{
		return;
	}
	for (UDynamicMeshComponent* C : { FarBody.Get(), CloudsMesh.Get() })
	{
		UDynamicMesh* Mesh = C->GetDynamicMesh();
		if (!Mesh) { return; }
		Mesh->Reset();
		FGeometryScriptPrimitiveOptions Options;
		const int32 Steps = C == FarBody ? 48 : 40;
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereBox(Mesh, Options, FTransform::Identity, 1.f, Steps, Steps, Steps);
		FGeometryScriptCalculateNormalsOptions NormalOptions;
		UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(Mesh, NormalOptions);
		C->NotifyMeshUpdated();
		C->SetRelativeScale3D(FVector(MeshUnitRadius));
	}
	CloudsMesh->SetTranslucentSortPriority(Clouds->TranslucencySortPriority);
	bFarBuilt = true;
}

void APlanetBody::UpdateShellVisibility()
{
	const bool bShow = !bBeyondHorizon;
	if (bFarBuilt)
	{
		// Game: generated spheres draw, the static placeholders only keep collision / editor display.
		Body->SetVisibility(false);
		Clouds->SetVisibility(false);
		FarBody->SetVisibility(bShow && !bDetailActive && !bTerrainActive);
		// With the ray-marched layer on, the shell stays drawn for the per-pixel distance fade beyond the layer's reach.
		CloudsMesh->SetVisibility(bShow && Style.bClouds && (CloudShellFade > 0.001f || LayerReachKm < 1.0e8f));
	}
	else
	{
		Body->SetVisibility(bShow && !bDetailActive && !bTerrainActive);
		Clouds->SetVisibility(bShow && Style.bClouds);
	}
	if (DetailBody) { DetailBody->SetVisibility(bShow && bDetailActive); }
	if (Atmosphere) { Atmosphere->SetVisibility(bShow && Style.bAtmosphere && !bUsingSkyAtmosphere); }
}

void APlanetBody::BuildRings()
{
	if (!Rings)
	{
		return;
	}
	UWorld* World = GetWorld();
	// Game worlds only: a PCG-spawned actor saved with the level should not carry the mesh.
	if (!Style.bRings || !World || !World->IsGameWorld())
	{
		Rings->SetVisibility(false);
		return;
	}
	if (!bRingsBuilt || !FMath::IsNearlyEqual(RingsBuiltInner, Style.RingInner) || !FMath::IsNearlyEqual(RingsBuiltOuter, Style.RingOuter))
	{
		UDynamicMesh* Mesh = Rings->GetDynamicMesh();
		if (!Mesh)
		{
			return;
		}
		Mesh->Reset();
		FGeometryScriptPrimitiveOptions Options;
		// Unit space: radii in planet radii, like the sphere meshes. Flat, so the material reads the radius per pixel.
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendDisc(Mesh, Options, FTransform::Identity, Style.RingOuter, 256, 6, 0.f, 360.f, Style.RingInner);
		FGeometryScriptCalculateNormalsOptions NormalOptions;
		UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(Mesh, NormalOptions);
		Rings->NotifyMeshUpdated();
		Rings->SetRelativeScale3D(FVector(MeshUnitRadius));
		bRingsBuilt = true;
		RingsBuiltInner = Style.RingInner;
		RingsBuiltOuter = Style.RingOuter;
	}
	if (RingMID && Rings->GetMaterial(0) != RingMID)
	{
		Rings->SetMaterial(0, RingMID);
	}
	Rings->SetVisibility(!bBeyondHorizon);
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
	if (Body && Clouds && DetailBody && Atmosphere) { UpdateShellVisibility(); }
	if (Rings) { Rings->SetVisibility(Style.bRings && bRingsBuilt && !bBeyond); }
	for (UHierarchicalInstancedStaticMeshComponent* H : RingRocks)
	{
		if (H) { H->SetVisibility(bRingParticlesActive && !bBeyond); }
	}
	for (UDynamicMeshComponent* P : Patches)
	{
		if (P) { P->SetVisibility(bPatchesActive && !bBeyond); }
	}
	if (Terrain) { Terrain->SetTerrainVisible(bTerrainActive && !bBeyond); }
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
	// Planets saved or spawned with the old stylised spin (0.3 deg/s) get a real day: the ground under a ship
	// flying the terrain must not move at 30 km/s.
	if (FMath::Abs(SpinDegPerSec) > 0.05f)
	{
		SpinDegPerSec = 0.0042f * (SpinDegPerSec / 0.3f);
	}
	ApplyAppearance();
}

void APlanetBody::Tick(float DeltaTime)
{
	FSlonkScopeTimer SlonkTimer_(TEXT("Tick PlanetBody"));
	Super::Tick(DeltaTime);
	if (SettleTicks > 0)
	{
		--SettleTicks;
		ApplyAppearance();
	}
	if (!FMath::IsNearlyZero(SpinDegPerSec))
	{
		AddActorLocalRotation(FRotator(0.f, SpinDegPerSec * DeltaTime, 0.f));
		SpinAccumDeg = FMath::Fmod(SpinAccumDeg + SpinDegPerSec * DeltaTime, 360.f);
		if (RingFrame)
		{
			RingFrame->SetRelativeRotation(FRotator(0.f, -SpinAccumDeg * (1.f - RingOrbitRateScale), 0.f));
		}
	}
	PollWeatherMap(DeltaTime);
	if (Clouds && Style.bClouds && !FMath::IsNearlyZero(Style.CloudSpinDegPerSec))
	{
		CloudYawDeg = FMath::Fmod(CloudYawDeg + Style.CloudSpinDegPerSec * DeltaTime, 360.f);
		Clouds->SetRelativeRotation(FRotator(0.f, CloudYawDeg, 0.f));
		const float Yaw = FMath::DegreesToRadians(CloudYawDeg);
		for (UMaterialInstanceDynamic* Mid : SurfaceMIDs())
		{
			Mid->SetScalarParameterValue(TEXT("CloudYaw"), Yaw);
		}
		if (Terrain && bTerrainActive) { Terrain->SetScalarParameter(TEXT("CloudYaw"), Yaw); }
	}

	LODTimer -= DeltaTime;
	if (LODTimer <= 0.f)
	{
		LODTimer = 0.2f;
		// The floating origin keeps the player near the world origin, so distance from the origin is what the GPU sees.
		SetBeyondHorizon(IsLocationBeyondHorizon(GetActorLocation()));
		UpdateLOD();
		// Worlds with a ray-marched layer get a weather map, laid over the shell and the layer alike: built when the
		// player comes within 40 radii (a 2k map is 10 MB, and a few dozen worlds have clouds), dropped past 80.
		if (Style.bClouds && Style.CloudThicknessKm > 0.f)
		{
			UWorld* World = GetWorld();
			APlayerController* PC = World && World->IsGameWorld() ? World->GetFirstPlayerController() : nullptr;
			if (PC)
			{
				FVector ViewLoc; FRotator ViewRot;
				PC->GetPlayerViewPoint(ViewLoc, ViewRot);
				const double Radii = FVector::Dist(ViewLoc, GetActorLocation()) / FMath::Max((double)GetRadiusCm(), 1.0);
				if (Radii < 40.0 && !bWeatherRequested)
				{
					const FVector L = GetSeedLocation();
					RequestWeatherMap(FLinearColor(FMath::Frac(L.X / 7.3e9) * 100.f, FMath::Frac(L.Y / 5.1e9) * 100.f, FMath::Frac(L.Z / 3.7e9) * 100.f, 0.f));
				}
				else if (Radii > 80.0 && bWeatherRequested && !WeatherFuture.IsValid())
				{
					bWeatherRequested = false;
					WeatherMap = nullptr;
					WeatherStrength = 0.f;
					if (CloudMID) { CloudMID->SetScalarParameterValue(TEXT("WeatherStrength"), 0.f); }
				}
			}
		}
	}

	// Terrain LOD every frame: the traversal is cheap and finished chunks should land the frame they are ready.
	if (bTerrainActive && Terrain && !bBeyondHorizon)
	{
		UWorld* World = GetWorld();
		APlayerController* PC = World && World->IsGameWorld() ? World->GetFirstPlayerController() : nullptr;
		if (PC)
		{
			FVector ViewLoc; FRotator ViewRot;
			PC->GetPlayerViewPoint(ViewLoc, ViewRot);
			Terrain->UpdateView(ViewLoc);
			UpdateCities(ViewLoc, DeltaTime);
		}
	}

	// The star direction only changes as bodies move; refresh a few times a second.
	StarSearchTimer -= DeltaTime;
	if (StarSearchTimer <= 0.f)
	{
		StarSearchTimer = 0.1f;
		UpdateStarDirection();
		// Also refreshes the body-local bearing, which the planet's spin changes every frame.
		UpdateSunDirectionParams();
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
		if (CloudMID) { CloudMID->SetScalarParameterValue(TEXT("SkyLit"), bEnabled ? 1.f : 0.f); }
		UpdateShellVisibility();
	}
}

void APlanetBody::SetSkyBehind(float K)
{
	if (FMath::IsNearlyEqual(SkyBehind, K, 1e-3f)) { return; }
	SkyBehind = K;
	for (UMaterialInstanceDynamic* Mid : { CloudMID.Get(), AtmosphereMID.Get(), RingMID.Get() })
	{
		if (Mid) { Mid->SetScalarParameterValue(TEXT("SkyBehind"), K); }
	}
	for (UMaterialInstanceDynamic* Mid : SurfaceMIDs()) { Mid->SetScalarParameterValue(TEXT("SkyBehind"), K); }
	// Relief worlds are drawn by their terrain quadtree from any distance, so the term lives in that material too.
	if (Terrain) { Terrain->SetScalarParameter(TEXT("SkyBehind"), K); }
}

void APlanetBody::SetCloudShellFade(float Fade)
{
	Fade = FMath::Clamp(Fade, 0.f, 1.f);
	if (FMath::IsNearlyEqual(CloudShellFade, Fade, 1e-3f)) { return; }
	const bool bWasShown = CloudShellFade > 0.001f;
	CloudShellFade = Fade;
	if (CloudMID) { CloudMID->SetScalarParameterValue(TEXT("CloudOpacity"), 0.85f * CloudShellFade); }
	if (bWasShown != (Fade > 0.001f)) { UpdateShellVisibility(); }
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
	FSlonkScopeTimer SlonkTimer_(TEXT("UpdateLOD PlanetBody"));
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

	if (bTerrainActive && Terrain)
	{
		// The quadtree covers every distance; the displaced spheres and patches stay off.
		if (bDetailActive) { bDetailActive = false; UpdateShellVisibility(); }
		SetPatchesActive(false);
		// The quadtree itself is updated every tick (Tick) so chunks land as fast as they cook.
		UpdateRingParticles(ViewLoc);
		return;
	}

	// Hysteresis so the swap never flickers at the boundary.
	const bool bWantDetail = bDetailActive ? Dist < DetailMeshRadii * 1.15 : Dist < DetailMeshRadii;
	if (bWantDetail != bDetailActive)
	{
		bDetailActive = bWantDetail;
		if (bDetailActive)
		{
			BuildDetailMesh();
			// Unit sphere scales with the body; the far mesh used MeshUnitRadius units.
			DetailBody->SetRelativeScale3D(FVector(MeshUnitRadius));
		}
		UpdateShellVisibility();
	}
	UpdatePatches(ViewLoc);
	UpdateRingParticles(ViewLoc);
}

void APlanetBody::ClearRingParticles()
{
	for (UHierarchicalInstancedStaticMeshComponent* H : RingRocks)
	{
		if (H && H->GetInstanceCount() > 0) { H->ClearInstances(); }
	}
	bRingParticlesActive = false;
	RingParticleCell = FIntVector(TNumericLimits<int32>::Max());
}

void APlanetBody::UpdateRingParticles(const FVector& ViewLoc)
{
	if (!Style.bRings || !Body || RingParticlesPerCell <= 0)
	{
		if (bRingParticlesActive) { ClearRingParticles(); }
		return;
	}
	const double R = GetRadiusCm();
	// Camera in the body's rotated frame, in cm (the ring plane is local XY).
	if (!RingFrame)
	{
		RingFrame = NewObject<USceneComponent>(this, TEXT("RingFrame"));
		RingFrame->SetupAttachment(Body);
		RingFrame->SetUsingAbsoluteScale(true);
		RingFrame->SetMobility(EComponentMobility::Movable);
		RingFrame->RegisterComponent();
		RingFrame->SetRelativeRotation(FRotator(0.f, -SpinAccumDeg * (1.f - RingOrbitRateScale), 0.f));
	}
	// Camera in the (non-spinning) ring frame, in cm; the ring plane is its XY.
	const FVector Local = RingFrame->GetComponentTransform().InverseTransformPosition(ViewLoc);
	const double Radial = FMath::Sqrt(Local.X * Local.X + Local.Y * Local.Y) / R;
	const double Margin = RingParticleCellCm * (RingParticleCellRange + 1) / R;
	const bool bNear = FMath::Abs(Local.Z) < RingParticleMaxPlaneDistanceRadii * R
		&& Radial > Style.RingInner - Margin && Radial < Style.RingOuter + Margin;
	if (!bNear)
	{
		if (bRingParticlesActive) { ClearRingParticles(); }
		return;
	}
	BuildRingProfile();
	if (RingRocks.Num() == 0)
	{
		UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(RingRockMaterial, TEXT("RingRock"), true);
		if (Mat && ResolvedArchetype == EPlanetArchetype::IceGiant)
		{
			// Comet ice rather than rock.
			UMaterialInstanceDynamic* Ice = UMaterialInstanceDynamic::Create(Mat, this);
			Ice->SetVectorParameterValue(TEXT("RockColorA"), FLinearColor(0.66f, 0.76f, 0.88f));
			Ice->SetVectorParameterValue(TEXT("RockColorB"), FLinearColor(0.80f, 0.87f, 0.95f));
			Ice->SetVectorParameterValue(TEXT("RockColorC"), FLinearColor(0.92f, 0.95f, 1.0f));
			Mat = Ice;
		}
		for (int32 V = 0; V < SlonkRocks::NumVariants; ++V)
		{
			UHierarchicalInstancedStaticMeshComponent* H = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, *FString::Printf(TEXT("RingRocks%d"), V));
			H->SetupAttachment(RingFrame);
			H->SetMobility(EComponentMobility::Movable);
			H->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			H->SetGenerateOverlapEvents(false);
			H->CastShadow = false;
			H->bCastDynamicShadow = false;
			H->bAffectDistanceFieldLighting = false;
			H->bAffectDynamicIndirectLighting = false;
			H->SetStaticMesh(SlonkRocks::GetRockMesh(V));
			if (Mat) { H->SetMaterial(0, Mat); }
			H->RegisterComponent();
			RingRocks.Add(H);
		}
	}
	// Columns over the ring plane: the sheet is only a few km thick, so rocks are placed in z by a gaussian
	// rather than wasted uniformly through a 3D cell.
	const FIntVector Cell(FMath::FloorToInt(Local.X / RingParticleCellCm), FMath::FloorToInt(Local.Y / RingParticleCellCm), 0);
	if (bRingParticlesActive)
	{
		// The planet's spin drags the field past a resting ship; only rebuild once it has drifted well off centre.
		const FVector Centre = (FVector(RingParticleCell) + FVector(0.5, 0.5, 0.0)) * RingParticleCellCm;
		const double Drift = FVector::Dist2D(Local, Centre) / RingParticleCellCm;
		if (Drift < 2.0)
		{
			return;
		}
	}
	// Instances are relative to the centre cell's origin (the ring frame is unscaled, so this is in cm).
	const FVector OriginCm = FVector(Cell) * RingParticleCellCm;
	for (UHierarchicalInstancedStaticMeshComponent* H : RingRocks)
	{
		H->SetRelativeLocation(OriginCm);
	}
	TArray<TArray<FTransform>> PerLayer;
	PerLayer.SetNum(RingRocks.Num());
	const float Thick = RingThicknessFraction * (float)R;
	const int32 Range = FMath::Clamp(RingParticleCellRange, 1, 8);
	const int32 SeedBase = (int32)(Style.RingSeed * 1000.f);
	for (int32 Y = -Range; Y <= Range; ++Y)
	{
		for (int32 X = -Range; X <= Range; ++X)
		{
			const FIntVector Key = Cell + FIntVector(X, Y, 0);
			const FVector CellMin = FVector(Key) * RingParticleCellCm;
			const uint32 Hash = HashCombine(HashCombine(GetTypeHash(Key.X), GetTypeHash(Key.Y)), GetTypeHash(SeedBase));
			FRandomStream Rng(static_cast<int32>(Hash));
			for (int32 i = 0; i < RingParticlesPerCell; ++i)
			{
				const float Gz = (Rng.FRand() + Rng.FRand() + Rng.FRand() - 1.5f) * 1.2f; // ~gaussian, sigma ~0.6
				const FVector P = CellMin + FVector(Rng.FRand() * RingParticleCellCm, Rng.FRand() * RingParticleCellCm, Gz * Thick);
				const float Rad = (float)(FMath::Sqrt(P.X * P.X + P.Y * P.Y) / R);
				const float Xf = (Rad - Style.RingInner) / FMath::Max(Style.RingOuter - Style.RingInner, 0.05f);
				if (Rng.FRand() > RingDensityAt(Xf))
				{
					continue;
				}
				const float T = FMath::Pow(Rng.FRand(), 2.5f);
				const float RockR = RingParticleMinRadiusCm * FMath::Pow(FMath::Max(RingParticleMaxRadiusCm / RingParticleMinRadiusCm, 1.f), T);
				const FRotator Rot(Rng.FRandRange(0.f, 360.f), Rng.FRandRange(0.f, 360.f), Rng.FRandRange(0.f, 360.f));
				PerLayer[Rng.RandRange(0, RingRocks.Num() - 1)].Add(FTransform(Rot, P - OriginCm, FVector(RockR / SlonkRocks::UnitRadius)));
			}
		}
	}
	for (int32 L = 0; L < RingRocks.Num(); ++L)
	{
		RingRocks[L]->ClearInstances();
		if (PerLayer[L].Num() > 0)
		{
			RingRocks[L]->AddInstances(PerLayer[L], false, false);
		}
		RingRocks[L]->SetVisibility(!bBeyondHorizon);
	}
	UE_LOG(LogTemp, Log, TEXT("PlanetBody %s: ring particles rebuilt, %d rocks"), *GetName(), PerLayer[0].Num() + (PerLayer.Num() > 1 ? PerLayer[1].Num() : 0));
	RingParticleCell = Cell;
	bRingParticlesActive = true;
}

void APlanetBody::EnsurePatchComponents()
{
	const int32 Levels = FMath::Clamp(PatchLevels, 0, 4);
	bool bNewMIDs = false;
	while (Patches.Num() < Levels)
	{
		const int32 L = Patches.Num();
		UDynamicMeshComponent* P = NewObject<UDynamicMeshComponent>(this, *FString::Printf(TEXT("TerrainPatch%d"), L));
		P->SetupAttachment(Body);
		P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		P->SetGenerateOverlapEvents(false);
		P->SetMobility(EComponentMobility::Movable);
		P->CastShadow = false;
		P->bCastDynamicShadow = false;
		P->bAffectDistanceFieldLighting = false;
		P->SetVisibility(false);
		// The displacement pushes vertices past the un-displaced bounds.
		P->BoundsScale = 1.15f;
		P->RegisterComponent();
		P->SetRelativeScale3D(FVector(MeshUnitRadius));
		Patches.Add(P);
		UMaterialInstanceDynamic* Mid = nullptr;
		if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(SurfaceMaterial, TEXT("PlanetSurfacePatch"), true))
		{
			Mid = UMaterialInstanceDynamic::Create(Mat, this);
		}
		PatchMIDs.Add(Mid);
		bNewMIDs = true;
	}
	PatchHalfExtents.SetNumZeroed(Patches.Num());
	if (bNewMIDs)
	{
		// New instances start with the material defaults; give them the planet's style straight away.
		ApplyMaterials();
	}
}

void APlanetBody::BuildPatchLevel(int32 Level, double H)
{
	using namespace UE::Geometry;
	UDynamicMeshComponent* Comp = Patches.IsValidIndex(Level) ? Patches[Level].Get() : nullptr;
	if (!Comp || !Comp->GetDynamicMesh())
	{
		return;
	}
	const int32 N = FMath::Clamp(PatchGridSteps, 16, 400);
	const int32 W = N + 1;
	// Skirt depth and the lowering of the coarser surface underneath must match (see ApplyPatchRegions).
	const double Skirt = FMath::Max(Style.ReliefFraction * 4.0, 0.002);

	FDynamicMesh3 M;
	M.EnableAttributes();
	M.Attributes()->SetNumUVLayers(3);
	FDynamicMeshNormalOverlay* NO = M.Attributes()->PrimaryNormals();
	FDynamicMeshUVOverlay* UV0 = M.Attributes()->GetUVLayer(0);
	FDynamicMeshUVOverlay* UV1 = M.Attributes()->GetUVLayer(1);
	FDynamicMeshUVOverlay* UV2 = M.Attributes()->GetUVLayer(2);
	// Border vertices that fall between two vertices of the next coarser level (four times the spacing) carry
	// those two parents' gnomonic coordinates in UV1/UV2; the material displaces them by the mean of the parents'
	// heights, so the two levels meet exactly. Every other vertex is its own parent.
	auto AddVert = [&](const FVector& Pos, const FVector& Normal, const FVector2f& Own, const FVector2f& PA, const FVector2f& PB)
	{
		const int32 Vid = M.AppendVertex(Pos);
		NO->AppendElement(FVector3f(Normal));
		UV0->AppendElement(Own);
		UV1->AppendElement(PA);
		UV2->AppendElement(PB);
		return Vid;
	};
	TArray<int32> GridTris, SkirtTris;
	auto AddTri = [&](int32 A, int32 B, int32 C, TArray<int32>& Group)
	{
		const int32 Tid = M.AppendTriangle(FIndex3i(A, B, C));
		if (Tid >= 0)
		{
			NO->SetTriangle(Tid, FIndex3i(A, B, C));
			UV0->SetTriangle(Tid, FIndex3i(A, B, C));
			UV1->SetTriangle(Tid, FIndex3i(A, B, C));
			UV2->SetTriangle(Tid, FIndex3i(A, B, C));
			Group.Add(Tid);
		}
	};
	auto UVAt = [&](int32 i, int32 j)
	{
		return FVector2f((float)((2.0 * i / N - 1.0) * H), (float)((2.0 * j / N - 1.0) * H));
	};
	auto DirAt = [&](int32 i, int32 j)
	{
		const double U = (2.0 * i / N - 1.0) * H;
		const double V = (2.0 * j / N - 1.0) * H;
		return (PatchCenter + PatchEast * U + PatchNorth * V).GetSafeNormal();
	};
	const bool bHasParent = Level + 1 < Patches.Num() && PatchHalfExtents.IsValidIndex(Level + 1) && PatchHalfExtents[Level + 1] > H;
	for (int32 j = 0; j < W; ++j)
	{
		for (int32 i = 0; i < W; ++i)
		{
			const FVector Dir = DirAt(i, j);
			const FVector2f Own = UVAt(i, j);
			const bool bBorder = (i == 0 || i == N || j == 0 || j == N);
			FVector Pos = Dir;
			FVector2f PA = Own, PB = Own;
			if (bHasParent && bBorder)
			{
				// Parent spacing is 4 of our cells along the border; snap to the enclosing parent vertices.
				const bool bAlongI = (j == 0 || j == N);
				const int32 k = bAlongI ? i : j;
				const int32 k0 = (k / 4) * 4;
				if (k0 != k)
				{
					const int32 k1 = FMath::Min(k0 + 4, N);
					const int32 ia = bAlongI ? k0 : i, ja = bAlongI ? j : k0;
					const int32 ib = bAlongI ? k1 : i, jb = bAlongI ? j : k1;
					PA = UVAt(ia, ja);
					PB = UVAt(ib, jb);
					// Sit on the parent's chord so the un-displaced surfaces coincide too.
					const double T = (double)(k - k0) / (k1 - k0);
					Pos = FMath::Lerp(DirAt(ia, ja), DirAt(ib, jb), T);
				}
			}
			AddVert(Pos, Dir, Own, PA, PB);
		}
	}
	for (int32 j = 0; j < N; ++j)
	{
		for (int32 i = 0; i < N; ++i)
		{
			const int32 A = j * W + i, B = A + 1, C = A + W, D = C + 1;
			AddTri(A, C, B, GridTris);
			AddTri(B, C, D, GridTris);
		}
	}
	// Skirt: the boundary loop counter-clockwise (opposite to the grid's edge traversal) hung down by Skirt.
	TArray<int32> Loop;
	for (int32 i = 0; i <= N; ++i) { Loop.Add(i); }
	for (int32 j = 1; j <= N; ++j) { Loop.Add(j * W + N); }
	for (int32 i = N - 1; i >= 0; --i) { Loop.Add(N * W + i); }
	for (int32 j = N - 1; j >= 1; --j) { Loop.Add(j * W); }
	TArray<int32> Lower;
	for (int32 Vid : Loop)
	{
		const FVector Pos = M.GetVertex(Vid);
		const FVector Dir = Pos.GetSafeNormal();
		const FVector2f Own = UV0->GetElement(Vid), PA = UV1->GetElement(Vid), PB = UV2->GetElement(Vid);
		Lower.Add(AddVert(Pos - Dir * Skirt, Dir, Own, PA, PB));
	}
	for (int32 k = 0; k < Loop.Num(); ++k)
	{
		const int32 P = Loop[k], Q = Loop[(k + 1) % Loop.Num()];
		const int32 Pd = Lower[k], Qd = Lower[(k + 1) % Loop.Num()];
		AddTri(P, Q, Pd, SkirtTris);
		AddTri(Q, Qd, Pd, SkirtTris);
	}
	if (SkirtTris.Num() < 2 * Loop.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("PlanetBody %s: patch skirt incomplete (%d of %d triangles)"), *GetName(), SkirtTris.Num(), 2 * Loop.Num());
	}
	// Front faces outward.
	if (GridTris.Num() > 0)
	{
		const FIndex3i T0 = M.GetTriangle(GridTris[0]);
		const FVector Centroid = (M.GetVertex(T0.A) + M.GetVertex(T0.B) + M.GetVertex(T0.C)) / 3.0;
		if (FVector::DotProduct(M.GetTriNormal(GridTris[0]), Centroid) < 0.0)
		{
			M.ReverseOrientation(false);
		}
	}
	Comp->GetDynamicMesh()->SetMesh(MoveTemp(M));
	if (PatchMIDs.IsValidIndex(Level) && PatchMIDs[Level] && Comp->GetMaterial(0) != PatchMIDs[Level])
	{
		Comp->SetMaterial(0, PatchMIDs[Level]);
	}
}

void APlanetBody::SetPatchesActive(bool bActive)
{
	if (bPatchesActive == bActive && !bActive)
	{
		return;
	}
	bPatchesActive = bActive;
	for (int32 L = 0; L < Patches.Num(); ++L)
	{
		if (Patches[L])
		{
			// Levels whose extent collapsed onto the previous one are not drawn.
			const bool bUsed = PatchHalfExtents.IsValidIndex(L) && PatchHalfExtents[L] > 0.0;
			Patches[L]->SetVisibility(bActive && bUsed && !bBeyondHorizon);
		}
	}
	ApplyPatchRegions();
}

void APlanetBody::ApplyPatchRegions()
{
	// Each surface lowers itself under the patch level drawn on top of it; the patch's skirt hides the step.
	const double R = GetRadiusCm();
	const float DepthCm = (float)(FMath::Max(Style.ReliefFraction * 4.0, 0.002) * R);
	const FLinearColor C(PatchCenter.X, PatchCenter.Y, PatchCenter.Z, 0.f);
	const FLinearColor E(PatchEast.X, PatchEast.Y, PatchEast.Z, 0.f);
	const FLinearColor Nn(PatchNorth.X, PatchNorth.Y, PatchNorth.Z, 0.f);
	auto Set = [&](UMaterialInstanceDynamic* Mid, double InnerExtent)
	{
		if (!Mid) { return; }
		Mid->SetVectorParameterValue(TEXT("InnerCenter"), C);
		Mid->SetVectorParameterValue(TEXT("InnerEast"), E);
		Mid->SetVectorParameterValue(TEXT("InnerNorth"), Nn);
		Mid->SetScalarParameterValue(TEXT("InnerHalfExtent"), bPatchesActive ? (float)InnerExtent : 0.f);
		Mid->SetScalarParameterValue(TEXT("InnerDepthCm"), DepthCm);
	};
	for (UMaterialInstanceDynamic* Mid : PatchMIDs)
	{
		if (Mid) { Mid->SetScalarParameterValue(TEXT("ParentBlend"), 1.f); }
	}
	double Top = 0.0;
	for (int32 L = 0; L < PatchMIDs.Num(); ++L)
	{
		const double Inner = L > 0 && PatchHalfExtents.IsValidIndex(L - 1) ? PatchHalfExtents[L - 1] : 0.0;
		Set(PatchMIDs[L], Inner);
		if (PatchHalfExtents.IsValidIndex(L) && PatchHalfExtents[L] > 0.0)
		{
			Top = PatchHalfExtents[L];
		}
	}
	Set(DetailSurfaceMID, Top);
	Set(SurfaceMID, 0.0);
}

void APlanetBody::UpdatePatches(const FVector& ViewLoc)
{
	const int32 Levels = FMath::Clamp(PatchLevels, 0, 4);
	if (Levels == 0 || !bDetailActive || !Body)
	{
		SetPatchesActive(false);
		return;
	}
	// Camera in the body's unit space (the body is scaled by its radius).
	const FVector Cam = Body->GetComponentTransform().InverseTransformPosition(ViewLoc);
	const double Dist = Cam.Size();
	const double Alt = Dist - 1.0;
	if (Dist < 1e-3 || Alt > PatchMaxAltitudeRadii)
	{
		SetPatchesActive(false);
		return;
	}
	EnsurePatchComponents();
	const FVector C = Cam / Dist;
	const double H0 = FMath::Max((double)PatchExtentPerAltitude * FMath::Max(Alt, 0.0), (double)PatchMinHalfExtent);
	const bool bRebuild = !bPatchesActive
		|| (C - PatchCenter).Size() > 0.3 * PatchHalfExtent0
		|| H0 > PatchHalfExtent0 * 1.3 || H0 < PatchHalfExtent0 / 1.3;
	if (bRebuild)
	{
		PatchCenter = C;
		FVector E = FVector::CrossProduct(FVector::ZAxisVector, C);
		if (E.SizeSquared() < 1e-6)
		{
			E = FVector::CrossProduct(FVector::XAxisVector, C);
		}
		PatchEast = E.GetSafeNormal();
		PatchNorth = FVector::CrossProduct(C, PatchEast).GetSafeNormal();
		PatchHalfExtent0 = H0;
		const double MaxExtent = 1.3; // ~52 degrees of arc in gnomonic units; wider is pointless on a sphere
		double Prev = 0.0;
		for (int32 L = 0; L < Patches.Num(); ++L)
		{
			const double H = FMath::Min(H0 * FMath::Pow(4.0, L), MaxExtent);
			PatchHalfExtents[L] = Prev >= MaxExtent ? 0.0 : H;
			Prev = H;
		}
		for (int32 L = 0; L < Patches.Num(); ++L)
		{
			if (PatchHalfExtents[L] > 0.0)
			{
				BuildPatchLevel(L, PatchHalfExtents[L]);
			}
		}
	}
	SetPatchesActive(true);
}
