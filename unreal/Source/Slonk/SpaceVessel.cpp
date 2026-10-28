#include "SpaceVessel.h"

#include "Components/DynamicMeshComponent.h"
#include "ExplosionEffect.h"
#include "HullDebris.h"
#include "Engine/World.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "MeshPrims.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SlonkMaterials.h"
#include "SlonkLoadSubsystem.h"
#include "SpaceTarget.h"
#include "ShieldComponent.h"
#include "RunningLightsComponent.h"
#include "Engine/DamageEvents.h"
#include "UDynamicMesh.h"
#include "StarshipPawn.h"
#include "WarpFlash.h"
#include "SlonkGameMode.h"
#include "FloatingOriginComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

namespace
{
	UDynamicMeshComponent* MakePart(AActor* Owner, const TCHAR* Name, USceneComponent* Parent, bool bCollide)
	{
		UDynamicMeshComponent* C = Owner->CreateDefaultSubobject<UDynamicMeshComponent>(Name);
		C->SetupAttachment(Parent);
		C->SetMobility(EComponentMobility::Movable);
		C->SetGenerateOverlapEvents(false);
		C->CastShadow = false;
		C->bCastDynamicShadow = false;
		C->bAffectDistanceFieldLighting = false;
		if (bCollide)
		{
			C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			C->SetCollisionObjectType(ECC_WorldDynamic);
			C->SetCollisionResponseToAllChannels(ECR_Block);
			C->EnableComplexAsSimpleCollision();
			C->bUseAsyncCooking = true;
		}
		else
		{
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		return C;
	}

	using namespace SlonkMesh;

	/** Moves a finished scratch mesh into the component. The hulls are built in detached UDynamicMesh objects:
	 *  every GeometryScript append on a component's own mesh broadcasts a change and the component rebuilds its
	 *  render data, which made a few thousand boxes (the Borg cube) take half a minute. */
	void Finish(UDynamicMeshComponent* C, UDynamicMesh* Scratch)
	{
		if (!C || !Scratch) { return; }
		FGeometryScriptCalculateNormalsOptions NO;
		UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(Scratch, NO);
		C->GetDynamicMesh()->SetMesh(MoveTemp(Scratch->GetMeshRef()));
		C->NotifyMeshUpdated();
		C->UpdateCollision(false);
	}
}

ASpaceVessel::ASpaceVessel()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
	Hull = MakePart(this, TEXT("Hull"), Root, true);
	Glow = MakePart(this, TEXT("Glow"), Root, false);
	GlowWarm = MakePart(this, TEXT("GlowWarm"), Root, false);
	Shield = CreateDefaultSubobject<UShieldComponent>(TEXT("Shield"));
	Shield->SetupAttachment(Root);
	RunningLights = CreateDefaultSubobject<URunningLightsComponent>(TEXT("RunningLights"));
	RunningLights->SetupAttachment(Root);
	HullMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::HullPath));
	GlowMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::GlowPath));
	Tags.Add(ASpaceTarget::TargetableTag);
}

void ASpaceVessel::SetBeyondHorizon(bool bBeyond)
{
	bBeyondHorizon = bBeyond;
	SetActorHiddenInGame(bBeyond);
	if (Hull) { Hull->SetVisibility(!bBeyond); }
	if (Glow) { Glow->SetVisibility(!bBeyond); }
	if (GlowWarm) { GlowWarm->SetVisibility(!bBeyond); }
	if (RunningLights) { RunningLights->SetVisibility(!bBeyond, true); }
}

FVector ASpaceVessel::ToUniverse(const FVector& World) const
{
	if (const UFloatingOriginComponent* Origin = ASlonkGameMode::FindFloatingOrigin(GetWorld()))
	{
		return Origin->WorldToUniverse(World);
	}
	return World;
}

FVector ASpaceVessel::ToWorld(const FVector& Universe) const
{
	if (const UFloatingOriginComponent* Origin = ASlonkGameMode::FindFloatingOrigin(GetWorld()))
	{
		return Origin->UniverseToWorld(Universe);
	}
	return Universe;
}

void ASpaceVessel::PreRegisterAllComponents()
{
	Super::PreRegisterAllComponents();
	SetBeyondHorizon(IsLocationBeyondHorizon(PreRegistrationLocation(this)));
}

void ASpaceVessel::BeginPlay()
{
	Super::BeginPlay();
	Health = MaxHealth;
	// Built through the load queue so the first seconds of a level show progress instead of one long frame.
	USlonkLoadSubsystem::Enqueue(this, FString::Printf(TEXT("Building %s"), *GetDisplayName()), [this]() { Rebuild(); }, LoadWeight);
}

void ASpaceVessel::ApplyMaterials()
{
	if (!HullMID)
	{
		if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(HullMaterial, TEXT("Hull"), true))
		{
			HullMID = UMaterialInstanceDynamic::Create(Mat, this);
		}
	}
	if (!GlowMID)
	{
		if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(GlowMaterial, TEXT("Glow"), false))
		{
			GlowMID = UMaterialInstanceDynamic::Create(Mat, this);
		}
	}
	// A dynamic mesh component ignores SetMaterial until it has material slots; ConfigureMaterialSet always applies.
	if (HullMID)
	{
		HullMID->SetVectorParameterValue(TEXT("HullColor"), HullColor);
		HullMID->SetScalarParameterValue(TEXT("PanelScale"), 400.f / FMath::Max(SizeCm, 1.f));
		HullMID->SetScalarParameterValue(TEXT("Roughness"), HullRoughness);
		HullMID->SetScalarParameterValue(TEXT("Metallic"), HullMetallic);
		HullMID->SetScalarParameterValue(TEXT("Specular"), HullSpecular);
		ApplyWindowParams(HullMID);
		TArray<UMaterialInterface*> Set = { HullMID.Get() };
		AccentMIDs.SetNum(HullAccentColors.Num());
		for (int32 i = 0; i < HullAccentColors.Num(); ++i)
		{
			if (!AccentMIDs[i])
			{
				AccentMIDs[i] = UMaterialInstanceDynamic::Create(HullMID->Parent, this);
			}
			AccentMIDs[i]->SetVectorParameterValue(TEXT("HullColor"), HullAccentColors[i]);
			AccentMIDs[i]->SetScalarParameterValue(TEXT("PanelScale"), 400.f / FMath::Max(SizeCm, 1.f));
			AccentMIDs[i]->SetScalarParameterValue(TEXT("Roughness"), HullRoughness);
			AccentMIDs[i]->SetScalarParameterValue(TEXT("Metallic"), HullMetallic);
			AccentMIDs[i]->SetScalarParameterValue(TEXT("Specular"), HullSpecular);
			ApplyWindowParams(AccentMIDs[i]);
			Set.Add(AccentMIDs[i]);
		}
		Hull->ConfigureMaterialSet(Set);
	}
	if (GlowMID)
	{
		GlowMID->SetVectorParameterValue(TEXT("GlowColor"), GlowColor);
		GlowMID->SetScalarParameterValue(TEXT("Intensity"), GlowIntensity);
		Glow->ConfigureMaterialSet({ GlowMID.Get() });
	}
	if (!WarmMID && GlowMID)
	{
		WarmMID = UMaterialInstanceDynamic::Create(GlowMID->Parent, this);
	}
	if (WarmMID && GlowWarm)
	{
		WarmMID->SetVectorParameterValue(TEXT("GlowColor"), WarmGlowColor);
		WarmMID->SetScalarParameterValue(TEXT("Intensity"), GlowIntensity);
		GlowWarm->ConfigureMaterialSet({ WarmMID.Get() });
	}
}

void ASpaceVessel::ApplyWindowParams(UMaterialInstanceDynamic* MID) const
{
	if (!MID)
	{
		return;
	}
	MID->SetVectorParameterValue(TEXT("WindowSaucerCenter"), FLinearColor(WindowSaucerCenter.X, WindowSaucerCenter.Y, WindowSaucerCenter.Z, 1.f));
	MID->SetScalarParameterValue(TEXT("WindowSaucerRadius"), WindowSaucerRadius);
	MID->SetScalarParameterValue(TEXT("WindowSaucerThickness"), WindowSaucerThickness);
	MID->SetScalarParameterValue(TEXT("WindowHullMaxY"), WindowHullMaxY);
	MID->SetScalarParameterValue(TEXT("WindowHullMaxZ"), WindowHullMaxZ);
	MID->SetScalarParameterValue(TEXT("WindowHullMinZ"), WindowHullMinZ);
	MID->SetScalarParameterValue(TEXT("WindowHullMinX"), WindowHullMinX);
	MID->SetScalarParameterValue(TEXT("WindowHullMaxX"), WindowHullMaxX);
	MID->SetScalarParameterValue(TEXT("WindowPitch"), WindowPitch);
	MID->SetScalarParameterValue(TEXT("WindowDeck"), WindowDeck);
	MID->SetScalarParameterValue(TEXT("WindowGlow"), WindowGlow);
	MID->SetScalarParameterValue(TEXT("WindowOnFraction"), WindowOnFraction);
}

void ASpaceVessel::Rebuild()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || !Hull || !Glow)
	{
		return;
	}
	// Scratch meshes with no listeners; Finish moves them into the components in one step each.
	UDynamicMesh* HM = NewObject<UDynamicMesh>(this, NAME_None, RF_Transient);
	UDynamicMesh* GM = NewObject<UDynamicMesh>(this, NAME_None, RF_Transient);
	WarmMesh = GlowWarm ? NewObject<UDynamicMesh>(this, NAME_None, RF_Transient) : nullptr;
	FRandomStream Rng(Seed);
	PendingLights.Reset();
	PendingFloods.Reset();
	CurrentMaterialID() = 0;
	BuildMeshes(HM, GM, Rng);
	CurrentMaterialID() = 0;
	Finish(Hull, HM);
	Finish(Glow, GM);
	if (GlowWarm) { Finish(GlowWarm, WarmMesh); }
	WarmMesh = nullptr;
	bBuilt = true;
	ApplyMaterials();
	if (RunningLights)
	{
		RunningLights->MaxRadiusCm = SizeCm * 0.012f;
		RunningLights->MinAngularRadius = 0.002f;
		RunningLights->SetLights(PendingLights);
		RunningLights->SetFloodlights(PendingFloods);
	}
	OnMeshesBuilt();
	if (Shield)
	{
		Shield->SetShellExtent(FVector(GetBoundingRadiusCm() * 1.15f));
	}
}

void ASpaceVessel::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bDying)
	{
		TickDying(DeltaTime);
		return;
	}
	HorizonTimer -= DeltaTime;
	if (HorizonTimer <= 0.f)
	{
		HorizonTimer = 0.5f;
		SetBeyondHorizon(IsLocationBeyondHorizon(GetVisualLocation()));
	}
}

float ASpaceVessel::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (Health <= 0.f)
	{
		return Applied;
	}
	if (Shield)
	{
		FVector Point = GetActorLocation();
		if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
		{
			Point = static_cast<const FPointDamageEvent&>(DamageEvent).HitInfo.ImpactPoint;
		}
		else if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
		{
			Point = static_cast<const FRadialDamageEvent&>(DamageEvent).Origin;
		}
		else if (DamageCauser)
		{
			Point = DamageCauser->GetActorLocation();
		}
		Shield->RegisterImpact(Point, FMath::Clamp(Applied / 20.f, 0.3f, 2.5f));
	}
	Health -= Applied;
	if (Health <= 0.f && !bDying)
	{
		BeginDying();
	}
	return Applied;
}

void ASpaceVessel::BeginDying()
{
	bDying = true;
	DeathTimer = DeathSeconds;
	DeathFlashTimer = 0.f;
	DeathRng.Initialize(Seed * 13 + 5);
	Tags.Remove(ASpaceTarget::TargetableTag);
	if (Shield) { Shield->SetVisibility(false); }
	// The first blast right away so the kill reads immediately.
	AExplosionEffect::SpawnExplosion(GetWorld(), GetActorLocation() + DeathRng.GetUnitVector() * SizeCm * 0.25f, SizeCm * 0.6f, 0.8f);
}

void ASpaceVessel::TickDying(float DeltaTime)
{
	DeathTimer -= DeltaTime;
	DeathFlashTimer -= DeltaTime;
	if (DeathFlashTimer <= 0.f && DeathTimer > 0.f)
	{
		// Secondary explosions ripple across the hull.
		DeathFlashTimer = DeathRng.FRandRange(0.12f, 0.3f);
		const FVector At = GetActorLocation() + GetActorTransform().TransformVector(FVector(DeathRng.FRandRange(-0.45f, 0.45f), DeathRng.FRandRange(-0.3f, 0.3f), DeathRng.FRandRange(-0.15f, 0.15f)) * SizeCm);
		AExplosionEffect::SpawnExplosion(GetWorld(), At, SizeCm * DeathRng.FRandRange(0.25f, 0.5f), DeathRng.FRandRange(0.5f, 0.9f));
	}
	if (DeathTimer <= 0.f)
	{
		UWorld* World = GetWorld();
		const FVector Here = GetActorLocation();
		AExplosionEffect::SpawnExplosion(World, Here, SizeCm * 2.2f, 2.0f);
		AExplosionEffect::SpawnExplosion(World, Here, SizeCm * 1.2f, 1.2f);
		UMaterialInterface* Mat = HullMID ? static_cast<UMaterialInterface*>(HullMID) : SlonkMaterials::LoadOrFallback(HullMaterial, TEXT("Hull"), true);
		AHullDebris::Scatter(World, Here, WreckVelocity, SizeCm, Mat, HullColor, FMath::Clamp((int32)(SizeCm / 2500.f), 10, 40), Seed);
		Destroy();
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Starship

AProceduralStarship::AProceduralStarship()
{
	MaxHealth = 400.f;
	static const TCHAR* HullNames[3] = { TEXT("EchoHull0"), TEXT("EchoHull1"), TEXT("EchoHull2") };
	static const TCHAR* GlowNames[3] = { TEXT("EchoGlow0"), TEXT("EchoGlow1"), TEXT("EchoGlow2") };
	for (int32 i = 0; i < 3; ++i)
	{
		UDynamicMeshComponent* H = MakePart(this, HullNames[i], Root, false);
		UDynamicMeshComponent* G = MakePart(this, GlowNames[i], Root, false);
		H->SetVisibility(false);
		G->SetVisibility(false);
		EchoHulls.Add(H);
		EchoGlows.Add(G);
	}
}

namespace
{
	/** What a class builder leaves behind for the shared running lights, floodlights and window zones. */
	struct FShipLayout
	{
		FVector SaucerC = FVector::ZeroVector;
		float SaucerR = 1.f;            // radius across the beam; an oval saucer is SaucerStretch times longer fore-aft
		float SaucerStretch = 1.f;
		float RimHalf = 1.f;            // half-thickness at the rim
		float TopIn = 0.f, TopOut = 0.f, BotIn = 0.f, BotOut = 0.f;   // skin heights (from SaucerC.z) at 0.24 R and 0.68 R
		float Top = 0.f, Bottom = 0.f;  // bridge dome top and sensor dome bottom (from SaucerC.z)
		bool bHull = false;
		FVector HullStart = FVector::ZeroVector;   // stern point on the engineering hull's axis
		float HullLen = 0.f, HullR = 0.f;
		int32 NacCount = 0;
		FVector NacStart[4];            // aft axis point of each nacelle
		float NacLen = 0.f, NacH = 0.f, NacW = 0.f;
		FVector PylonRoot[4];
	};

	struct FShipCtx
	{
		UDynamicMesh* HM;
		UDynamicMesh* GM;
		UDynamicMesh* WM;
		FRandomStream& Rng;
		float L;
		FShipLayout& Lay;
	};

	// Profiles are unit outlines: saucers in units of their radius as (r, z), hulls and nacelles as (r, x).

	/** The refit saucer: flat underside with a sensor dome, a shallow upper dome stepping up to the B/C deck
	 *  superstructure and the bridge. Heights measured off the Enterprise-A mesh (rim 12 m thick, bridge 0.3 R up). */
	TArray<FVector2D> RefitSaucerProfile(float R)
	{
		return Scaled({ {0.f, -0.115f}, {0.14f, -0.115f}, {0.19f, -0.095f}, {0.24f, -0.088f}, {0.95f, -0.088f}, {1.f, -0.04f}, {1.f, 0.04f}, {0.96f, 0.088f},
			{0.75f, 0.105f}, {0.45f, 0.12f}, {0.33f, 0.165f}, {0.28f, 0.23f}, {0.17f, 0.27f}, {0.09f, 0.285f}, {0.06f, 0.30f}, {0.f, 0.30f} }, R, R);
	}

	/** The Excelsior saucer: flatter, with a broad raised oval superstructure. */
	TArray<FVector2D> ExcelsiorSaucerProfile(float R)
	{
		return Scaled({ {0.f, -0.10f}, {0.2f, -0.10f}, {0.26f, -0.07f}, {0.95f, -0.07f}, {1.f, -0.02f}, {1.f, 0.02f}, {0.95f, 0.07f}, {0.6f, 0.09f},
			{0.4f, 0.16f}, {0.36f, 0.20f}, {0.2f, 0.22f}, {0.1f, 0.23f}, {0.06f, 0.25f}, {0.f, 0.25f} }, R, R);
	}

	/** A film-era nacelle: flat aft cap, straight body, a short taper to the bussard cap. (r, x) with x from the aft end. */
	TArray<FVector2D> NacelleProfile(float Len, float Rad)
	{
		return Scaled({ {0.f, 0.f}, {0.8f, 0.f}, {0.97f, 0.08f}, {1.f, 0.2f}, {1.f, 0.88f}, {0.9f, 0.955f}, {0.6f, 0.985f}, {0.f, 1.f} }, Rad, Len);
	}

	/** A pair of nacelles at +-Y: revolved hull flattened to Width, a glowing bussard dome proud of the cap, the
	 *  warp grille as a long glow strip on the inboard flank, and an optional dorsal ridge. */
	void Nacelles(FShipCtx& C, float Y, float Z, float X0, float Len, float H, float W, bool bRidge)
	{
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			const int32 i = C.Lay.NacCount++;
			const FVector Start(X0, Side * Y, Z);
			RevolveX(C.HM, Start, NacelleProfile(Len, H), 20, 1.f, W / H);
			Ellipsoid(C.GM, Start + FVector(Len - H * 0.3f, 0.f, 0.f), FVector(H * 0.7f, W * 0.85f, H * 0.85f), 10);
			Box(C.GM, Start + FVector(Len * 0.5f, -Side * W * 0.97f, 0.f), FVector(Len * 0.55f, W * 0.14f, H * 0.75f));
			// The lit plate on the aft face.
			CylinderX(C.GM, Start - FVector(H * 0.08f, 0.f, 0.f), H * 0.42f, H * 0.1f, 16);
			if (bRidge)
			{
				Box(C.HM, Start + FVector(Len * 0.45f, 0.f, H * 0.95f), FVector(Len * 0.7f, W * 0.6f, H * 0.3f));
			}
			C.Lay.NacStart[i] = Start;
		}
		C.Lay.NacLen = Len;
		C.Lay.NacH = H;
		C.Lay.NacW = W;
	}

	/** A thin swept plate from Root (on the hull) to Tip (on the nacelle); the chords give the fore/aft extents at
	 *  each end, so the pylon can sweep back and up like the refit's. Both ends are buried a little in their hulls. */
	void Pylon(FShipCtx& C, int32 Idx, const FVector& Root, const FVector& Tip, float RootAft, float RootFwd, float TipAft, float TipFwd, float Thick)
	{
		const FVector D(0.f, Tip.Y - Root.Y, Tip.Z - Root.Z);
		const float Len = D.Size();
		const float Bury = Thick * 1.5f;
		Extrude(C.HM, FVector(0.f, Root.Y, Root.Z), FVector::ForwardVector, D / FMath::Max(Len, 1.f), { {RootAft, -Bury}, {RootFwd, -Bury}, {TipFwd, Len + Bury}, {TipAft, Len + Bury} }, Thick);
		C.Lay.PylonRoot[Idx] = Root;
	}

	/** Twin-emitter phaser banks: low oval turrets on the saucer skin at the given bearings and radius fraction. */
	void PhaserBanks(FShipCtx& C, const FVector& SC, float R, float Stretch, std::initializer_list<float> Bearings, float Frac, float TopZ, float BotZ)
	{
		for (float Deg : Bearings)
		{
			const float A = FMath::DegreesToRadians(Deg);
			const FVector At = SC + FVector(FMath::Cos(A) * Stretch, FMath::Sin(A), 0.f) * R * Frac;
			Ellipsoid(C.HM, At + FVector(0.f, 0.f, TopZ), FVector(R * 0.03f, R * 0.018f, R * 0.012f), 6);
			if (BotZ != 0.f)
			{
				Ellipsoid(C.HM, At + FVector(0.f, 0.f, BotZ), FVector(R * 0.03f, R * 0.018f, R * 0.012f), 6);
			}
		}
	}

	/** The refit cruiser. Proportions from the Enterprise-A mesh: saucer centre 0.266 L forward, radius 0.234 L, the
	 *  engineering hull's axis 0.049 L below the centreline, nacelles 0.155 L out and 0.085 L up, half a ship long. */
	void BuildConstitution(FShipCtx& C)
	{
		const float L = C.L;
		FRandomStream& Rng = C.Rng;
		FShipLayout& Lay = C.Lay;
		const float R = L * 0.234f * Rng.FRandRange(0.94f, 1.06f);
		const FVector SC(L * 0.266f, 0.f, L * 0.047f);
		RevolveZ(C.HM, SC, RefitSaucerProfile(R), 48);
		Lay.SaucerC = SC; Lay.SaucerR = R; Lay.RimHalf = R * 0.088f;
		Lay.TopIn = R * 0.245f; Lay.TopOut = R * 0.11f; Lay.BotIn = -R * 0.088f; Lay.BotOut = -R * 0.088f; Lay.Top = R * 0.30f; Lay.Bottom = -R * 0.115f;
		PhaserBanks(C, SC, R, 1.f, { 40.f, -40.f, 140.f, -140.f, 90.f, -90.f }, 0.62f, R * 0.112f, -R * 0.088f);
		// Impulse deck at the back of the superstructure, two vents glowing aft.
		Box(C.HM, SC + FVector(-R * 0.78f, 0.f, R * 0.14f), FVector(R * 0.32f, R * 0.26f, R * 0.1f));
		if (C.WM)
		{
			for (int32 S = -1; S <= 1; S += 2)
			{
				Box(C.WM, SC + FVector(-R * 0.945f, S * R * 0.07f, R * 0.15f), FVector(R * 0.012f, R * 0.08f, R * 0.06f));
			}
		}
		// Engineering hull: a revolved cigar, widest a third back, the deflector housing forward with a recessed mouth.
		const float AxisZ = -L * 0.049f;
		const FVector Stern(-L * 0.254f, 0.f, AxisZ);
		RevolveX(C.HM, Stern, Scaled({ {0.f, 0.f}, {0.03f, 0.f}, {0.036f, 0.054f}, {0.046f, 0.134f}, {0.054f, 0.224f}, {0.054f, 0.299f}, {0.05f, 0.339f}, {0.046f, 0.352f},
			{0.038f, 0.36f}, {0.036f, 0.374f}, {0.03f, 0.377f}, {0.024f, 0.377f}, {0.024f, 0.366f}, {0.f, 0.366f} }, L, L), 28);
		Lay.bHull = true; Lay.HullStart = Stern; Lay.HullLen = L * 0.377f; Lay.HullR = L * 0.054f;
		// Deflector dish sitting in the mouth of the housing.
		CylinderX(C.GM, FVector(L * 0.112f, 0.f, AxisZ), L * 0.023f, L * 0.006f, 24);
		// Hangar deck squaring off the aft top of the hull.
		Box(C.HM, FVector(-L * 0.2f, 0.f, AxisZ + L * 0.03f), FVector(L * 0.1f, L * 0.055f, L * 0.026f));
		// Neck leaning forward, with the torpedo launcher at its top.
		Extrude(C.HM, FVector::ZeroVector, FVector::ForwardVector, FVector::UpVector, { {-L * 0.045f, -L * 0.03f}, {L * 0.105f, -L * 0.03f}, {L * 0.165f, L * 0.04f}, {L * 0.02f, L * 0.04f} }, L * 0.03f);
		Box(C.HM, FVector(L * 0.095f, 0.f, -L * 0.005f), FVector(L * 0.05f, L * 0.032f, L * 0.022f));
		if (C.WM)
		{
			for (int32 S = -1; S <= 1; S += 2)
			{
				Box(C.WM, FVector(L * 0.1205f, S * L * 0.009f, -L * 0.009f), FVector(L * 0.002f, L * 0.006f, L * 0.006f));
			}
		}
		// Nacelles on pylons swept back and up.
		const float NacY = L * 0.155f * Rng.FRandRange(0.95f, 1.08f), NacZ = L * 0.085f * Rng.FRandRange(0.9f, 1.1f);
		const float NacLen = L * 0.49f * Rng.FRandRange(0.94f, 1.04f), NacH = L * 0.03f, NacW = L * 0.019f;
		Nacelles(C, NacY, NacZ, -L * 0.5f, NacLen, NacH, NacW, true);
		for (int32 S = -1; S <= 1; S += 2)
		{
			const FVector Root(0.f, S * L * 0.03f, AxisZ + L * 0.04f), Tip(0.f, S * (NacY - NacW * 0.8f), NacZ - NacH * 0.7f);
			Pylon(C, (S + 1) / 2, Root, Tip, -L * 0.1f, L * 0.02f, -L * 0.16f, -L * 0.08f, L * 0.012f);
		}
	}

	/** The light cruiser: the same saucer, grown, with a flat aft section instead of a secondary hull, nacelles
	 *  slung below on short pylons, and (usually) the roll bar with the torpedo pod over the back. */
	void BuildMiranda(FShipCtx& C)
	{
		const float L = C.L;
		FRandomStream& Rng = C.Rng;
		FShipLayout& Lay = C.Lay;
		const float R = L * 0.29f * Rng.FRandRange(0.95f, 1.05f);
		const FVector SC(L * 0.21f, 0.f, 0.f);
		RevolveZ(C.HM, SC, RefitSaucerProfile(R), 48);
		Lay.SaucerC = SC; Lay.SaucerR = R; Lay.RimHalf = R * 0.088f;
		Lay.TopIn = R * 0.245f; Lay.TopOut = R * 0.11f; Lay.BotIn = -R * 0.088f; Lay.BotOut = -R * 0.088f; Lay.Top = R * 0.30f; Lay.Bottom = -R * 0.115f;
		PhaserBanks(C, SC, R, 1.f, { 40.f, -40.f, 120.f, -120.f }, 0.62f, R * 0.112f, -R * 0.088f);
		// The aft section: a slab as thick as the rim, rounded at the back corners, a raised shuttle deck on top.
		const float T = R * 0.088f;
		TArray<FVector2D> Slab = { {L * 0.05f, -L * 0.21f}, {-L * 0.42f, -L * 0.21f} };
		Arc(Slab, FVector2D(-L * 0.42f, -L * 0.13f), L * 0.08f, -90.f, -180.f, 5);
		Slab.Add(FVector2D(-L * 0.5f, L * 0.13f));
		Arc(Slab, FVector2D(-L * 0.42f, L * 0.13f), L * 0.08f, 180.f, 90.f, 5);
		Slab.Add(FVector2D(L * 0.05f, L * 0.21f));
		Extrude(C.HM, FVector::ZeroVector, FVector::ForwardVector, FVector::RightVector, Slab, T * 2.f);
		Box(C.HM, FVector(-L * 0.22f, 0.f, T + L * 0.012f), FVector(L * 0.34f, L * 0.2f, L * 0.024f));
		// Impulse engines on the aft face either side of the shuttlebay.
		if (C.WM)
		{
			for (int32 S = -1; S <= 1; S += 2)
			{
				Box(C.WM, FVector(-L * 0.502f, S * L * 0.075f, 0.f), FVector(L * 0.004f, L * 0.06f, T * 1.1f));
			}
		}
		// Nacelles slung under the slab.
		const float NacY = L * 0.165f * Rng.FRandRange(0.95f, 1.05f), NacZ = -L * 0.11f * Rng.FRandRange(0.9f, 1.1f);
		const float NacLen = L * 0.45f * Rng.FRandRange(0.95f, 1.05f), NacH = L * 0.028f, NacW = L * 0.018f;
		Nacelles(C, NacY, NacZ, -L * 0.48f, NacLen, NacH, NacW, true);
		for (int32 S = -1; S <= 1; S += 2)
		{
			const FVector Root(0.f, S * L * 0.12f, -T), Tip(0.f, S * (NacY - NacW * 0.5f), NacZ + NacH * 0.6f);
			Pylon(C, (S + 1) / 2, Root, Tip, -L * 0.36f, -L * 0.24f, -L * 0.35f, -L * 0.27f, L * 0.014f);
		}
		// Roll bar with the torpedo pod and the phaser pods at its ends.
		if (Rng.FRand() < 0.75f)
		{
			for (int32 S = -1; S <= 1; S += 2)
			{
				Box(C.HM, FVector(-L * 0.2f, S * L * 0.2f, T + L * 0.07f), FVector(L * 0.05f, L * 0.016f, L * 0.14f));
				Box(C.HM, FVector(-L * 0.2f, S * L * 0.215f, T + L * 0.14f), FVector(L * 0.04f, L * 0.03f, L * 0.03f));
			}
			Box(C.HM, FVector(-L * 0.2f, 0.f, T + L * 0.14f), FVector(L * 0.06f, L * 0.43f, L * 0.02f));
			Box(C.HM, FVector(-L * 0.2f, 0.f, T + L * 0.165f), FVector(L * 0.12f, L * 0.07f, L * 0.035f));
			if (C.WM)
			{
				for (int32 S = -1; S <= 1; S += 2)
				{
					Box(C.WM, FVector(-L * 0.139f, S * L * 0.02f, T + L * 0.165f), FVector(L * 0.003f, L * 0.012f, L * 0.012f));
					Box(C.WM, FVector(-L * 0.261f, S * L * 0.02f, T + L * 0.165f), FVector(L * 0.003f, L * 0.012f, L * 0.012f));
				}
			}
		}
	}

	/** The big explorer: oval saucer, long leaning neck, long engineering hull with a flared boat tail, slender
	 *  nacelles level with the hull top on pylons swept back from the tail. */
	void BuildExcelsior(FShipCtx& C)
	{
		const float L = C.L;
		FRandomStream& Rng = C.Rng;
		FShipLayout& Lay = C.Lay;
		const float R = L * 0.19f * Rng.FRandRange(0.95f, 1.05f);
		const float Stretch = 1.25f;
		const FVector SC(L * 0.26f, 0.f, L * 0.055f);
		RevolveZ(C.HM, SC, ExcelsiorSaucerProfile(R), 48, FVector(Stretch, 1.f, 1.f));
		Lay.SaucerC = SC; Lay.SaucerR = R; Lay.SaucerStretch = Stretch; Lay.RimHalf = R * 0.07f;
		Lay.TopIn = R * 0.215f; Lay.TopOut = R * 0.085f; Lay.BotIn = -R * 0.07f; Lay.BotOut = -R * 0.07f; Lay.Top = R * 0.25f; Lay.Bottom = -R * 0.10f;
		PhaserBanks(C, SC, R, Stretch, { 45.f, -45.f, 135.f, -135.f }, 0.62f, R * 0.09f, -R * 0.07f);
		// Impulse housing at the saucer's tail with two wide vents.
		Box(C.HM, SC + FVector(-R * Stretch * 0.98f, 0.f, R * 0.06f), FVector(R * 0.25f, R * 0.5f, R * 0.12f));
		if (C.WM)
		{
			for (int32 S = -1; S <= 1; S += 2)
			{
				Box(C.WM, SC + FVector(-R * Stretch * 0.98f - R * 0.127f, S * R * 0.14f, R * 0.06f), FVector(R * 0.01f, R * 0.14f, R * 0.07f));
			}
		}
		// Long neck leaning forward.
		Extrude(C.HM, FVector::ZeroVector, FVector::ForwardVector, FVector::UpVector, { {-L * 0.03f, -L * 0.02f}, {L * 0.12f, -L * 0.02f}, {L * 0.27f, L * 0.05f}, {L * 0.1f, L * 0.05f} }, L * 0.022f);
		// Engineering hull, long and slightly wider than tall, with the deflector forward and the boat tail aft.
		const float AxisZ = -L * 0.05f;
		const FVector Stern(-L * 0.43f, 0.f, AxisZ);
		RevolveX(C.HM, Stern, Scaled({ {0.f, 0.f}, {0.03f, 0.f}, {0.04f, 0.1f}, {0.048f, 0.25f}, {0.05f, 0.45f}, {0.048f, 0.5f}, {0.042f, 0.53f}, {0.036f, 0.54f},
			{0.03f, 0.55f}, {0.024f, 0.55f}, {0.024f, 0.54f}, {0.f, 0.54f} }, L, L), 28, 1.f, 1.1f);
		Lay.bHull = true; Lay.HullStart = Stern; Lay.HullLen = L * 0.55f; Lay.HullR = L * 0.05f;
		CylinderX(C.GM, FVector(L * 0.111f, 0.f, AxisZ), L * 0.023f, L * 0.005f, 24);
		Box(C.HM, FVector(-L * 0.36f, 0.f, AxisZ + L * 0.01f), FVector(L * 0.16f, L * 0.15f, L * 0.045f));
		// Nacelles level with the hull top, pylons swept back from the boat tail.
		const float NacY = L * 0.16f * Rng.FRandRange(0.95f, 1.06f), NacZ = L * 0.03f * Rng.FRandRange(0.8f, 1.2f);
		const float NacLen = L * 0.48f * Rng.FRandRange(0.95f, 1.04f), NacH = L * 0.022f, NacW = L * 0.016f;
		Nacelles(C, NacY, NacZ, -L * 0.5f, NacLen, NacH, NacW, false);
		for (int32 S = -1; S <= 1; S += 2)
		{
			const FVector Root(0.f, S * L * 0.07f, AxisZ + L * 0.03f), Tip(0.f, S * (NacY - NacW * 0.8f), NacZ - NacH * 0.6f);
			Pylon(C, (S + 1) / 2, Root, Tip, -L * 0.42f, -L * 0.3f, -L * 0.38f, -L * 0.28f, L * 0.012f);
		}
	}

	/** Running lights and floodlights from the layout. Red to port (-Y), green to starboard (+Y), white strobes
	 *  top, bottom and nose, steady amber formation lights along the saucer rim and the nacelle tails. Every ship
	 *  gets its own blink phase so a fleet never flashes in unison. */
	void ShipLights(FShipCtx& C, TArray<FRunningLight>& Lights, TArray<FHullFloodlight>& Floods)
	{
		using RL = URunningLightsComponent;
		const float L = C.L;
		FRandomStream& Rng = C.Rng;
		const FShipLayout& Lay = C.Lay;
		const FVector SC = Lay.SaucerC;
		const float R = Lay.SaucerR, St = Lay.SaucerStretch;
		const float Bulb = L * 0.0012f;
		const float Ph = Rng.FRand();
		const float Strobe = Rng.FRandRange(1.6f, 2.2f);
		const float Blink = Rng.FRandRange(1.1f, 1.5f);
		Lights.Add(FRunningLight(SC + FVector(0.f, -R * 1.01f, 0.f), RL::PortRed(), ERunningLightMode::Blink, Blink, Ph, Bulb * 1.3f));
		Lights.Add(FRunningLight(SC + FVector(0.f, R * 1.01f, 0.f), RL::StarboardGreen(), ERunningLightMode::Blink, Blink, Ph, Bulb * 1.3f));
		Lights.Add(FRunningLight(SC + FVector(R * St * 1.01f, 0.f, 0.f), RL::StrobeWhite(), ERunningLightMode::Strobe, Strobe, Ph, Bulb * 1.2f));
		Lights.Add(FRunningLight(SC + FVector(0.f, 0.f, Lay.Top + Bulb), RL::StrobeWhite(), ERunningLightMode::Strobe, Strobe, Ph + 0.5f, Bulb * 1.2f));
		Lights.Add(FRunningLight(SC + FVector(0.f, 0.f, Lay.Bottom - Bulb), RL::StrobeWhite(), ERunningLightMode::Strobe, Strobe, Ph + 0.5f, Bulb * 1.2f));
		{
			// Formation lights at 35, 70, 110 and 145 degrees either side of the nose, alternating slow blinks.
			const float Angles[4] = { 35.f, 70.f, 110.f, 145.f };
			for (int32 k = 0; k < 8; ++k)
			{
				const float A = FMath::DegreesToRadians(Angles[k % 4] * (k < 4 ? 1.f : -1.f));
				const FVector P = SC + FVector(FMath::Cos(A) * St, FMath::Sin(A), 0.f) * R * 1.005f;
				Lights.Add(FRunningLight(P, RL::FormationAmber(), ERunningLightMode::Blink, Blink * 2.f, Ph + 0.5f * (k % 2), Bulb * 0.6f, 0.45f));
			}
		}
		if (Lay.bHull)
		{
			// Engineering hull: a white strobe under the belly and an amber tail light; floods along the flanks from the stern.
			Lights.Add(FRunningLight(Lay.HullStart + FVector(Lay.HullLen * 0.5f, 0.f, -Lay.HullR * 1.02f), RL::StrobeWhite(), ERunningLightMode::Strobe, Strobe, Ph + 0.25f, Bulb));
			Lights.Add(FRunningLight(Lay.HullStart + FVector(-Bulb, 0.f, 0.f), RL::FormationAmber(), ERunningLightMode::Steady, 1.f, 0.f, Bulb, 0.7f));
			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				Floods.Add(FHullFloodlight(Lay.HullStart + FVector(Lay.HullLen * 0.08f, Side * Lay.HullR * 1.1f, Lay.HullR * 0.1f), Lay.HullStart + FVector(Lay.HullLen * 0.75f, Side * Lay.HullR * 1.0f, 0.f), Lay.HullR * 0.7f, 14.f));
			}
		}
		for (int32 i = 0; i < Lay.NacCount; ++i)
		{
			const FVector NS = Lay.NacStart[i];
			const float Side = NS.Y < 0.f ? -1.f : 1.f;
			const float NacLen = Lay.NacLen, NacH = Lay.NacH, NacW = Lay.NacW;
			// Tail of each nacelle: red/green by side, plus an outboard strobe at mid-length.
			Lights.Add(FRunningLight(NS + FVector(-Bulb, 0.f, 0.f), Side < 0 ? RL::PortRed() : RL::StarboardGreen(), ERunningLightMode::Blink, Blink, Ph, Bulb));
			Lights.Add(FRunningLight(NS + FVector(NacLen * 0.5f, Side * NacW * 1.02f, 0.f), RL::StrobeWhite(), ERunningLightMode::Strobe, Strobe, Ph + 0.25f + 0.1f * i, Bulb * 0.9f));
			// Pylon root lamp pointing at the nacelle's inboard underside, a lamp on the nacelle's top shoulder shining
			// aft along the registry, and one on its outboard shoulder for the flank.
			const FVector PylonRoot = Lay.PylonRoot[i];
			Floods.Add(FHullFloodlight(PylonRoot + FVector(0.f, 0.f, NacH * 0.5f), NS + FVector(NacLen * 0.5f, -Side * NacW * 0.6f, -NacH * 0.75f), NacH * 1.1f, 14.f));
			Floods.Add(FHullFloodlight(NS + FVector(NacLen * 0.85f, 0.f, NacH * 1.1f), NS + FVector(NacLen * 0.3f, 0.f, NacH * 0.9f), NacH * 0.9f, 12.f));
			Floods.Add(FHullFloodlight(NS + FVector(NacLen * 0.85f, Side * NacW * 1.1f, NacH * 0.2f), NS + FVector(NacLen * 0.3f, Side * NacW * 0.95f, 0.f), NacH * 0.9f, 12.f));
		}
		// Saucer floodlights: lamps near the middle of the top and underside facing radially out across the plating.
		{
			const float Angles[6] = { 35.f, 90.f, 145.f, -35.f, -90.f, -145.f };
			for (float Deg : Angles)
			{
				const float A = FMath::DegreesToRadians(Deg);
				const FVector Dir(FMath::Cos(A) * St, FMath::Sin(A), 0.f);
				for (int32 Up = -1; Up <= 1; Up += 2)
				{
					// No aft-facing lamps under the saucer: they would wash the neck and engineering hull.
					if (Up < 0 && FMath::Abs(Deg) > 100.f) { continue; }
					const float LampZ = Up > 0 ? Lay.TopIn + L * 0.004f : Lay.BotIn - L * 0.004f;
					const float PatchZ = Up > 0 ? Lay.TopOut : Lay.BotOut;
					Floods.Add(FHullFloodlight(SC + Dir * R * 0.24f + FVector(0.f, 0.f, LampZ), SC + Dir * R * 0.68f + FVector(0.f, 0.f, PatchZ), R * 0.16f, 14.f));
				}
			}
		}
	}
}

void AProceduralStarship::BuildMeshes(UDynamicMesh* HM, UDynamicMesh* GM, FRandomStream& Rng)
{
	// Everything in units of the ship length L, nose at +X, up +Z, origin at the ship's middle.
	const float L = SizeCm;
	FShipLayout Lay;
	FShipCtx C{ HM, GM, WarmMesh, Rng, L, Lay };
	BuiltClass = ShipClass;
	if (BuiltClass == EStarshipClass::Auto)
	{
		const float P = Rng.FRand();
		BuiltClass = P < 0.5f ? EStarshipClass::Constitution : (P < 0.8f ? EStarshipClass::Miranda : EStarshipClass::Excelsior);
	}
	switch (BuiltClass)
	{
	case EStarshipClass::Miranda: BuildMiranda(C); break;
	case EStarshipClass::Excelsior: BuildExcelsior(C); break;
	default: BuildConstitution(C); break;
	}
	// Window zones for the hull material: the saucer rim and the engineering hull's flanks (the Miranda's slab sides).
	WindowSaucerCenter = Lay.SaucerC;
	WindowSaucerRadius = Lay.SaucerR * (1.f + Lay.SaucerStretch) * 0.5f;
	WindowSaucerThickness = Lay.RimHalf;
	if (Lay.bHull)
	{
		WindowHullMaxY = Lay.HullR * 1.05f;
		WindowHullMaxZ = Lay.HullStart.Z + Lay.HullR * 0.9f;
		WindowHullMinZ = Lay.HullStart.Z - Lay.HullR * 0.9f;
		WindowHullMinX = Lay.HullStart.X;
		WindowHullMaxX = Lay.HullStart.X + Lay.HullLen * 0.8f;
	}
	else
	{
		WindowHullMaxY = L * 0.215f;
		WindowHullMaxZ = Lay.RimHalf * 0.6f;
		WindowHullMinZ = -Lay.RimHalf * 0.6f;
		WindowHullMinX = -L * 0.5f;
		WindowHullMaxX = L * 0.05f;
	}
	WindowPitch = L * 0.018f;
	WindowDeck = L * 0.012f;
	ShipLights(C, PendingLights, PendingFloods);
}

void AProceduralStarship::OnMeshesBuilt()
{
	// Copies of the hull for the relativistic warp view, each with its own material instance so it can be tinted.
	UMaterialInterface* HullMat = SlonkMaterials::LoadOrFallback(HullMaterial, TEXT("Hull"), true);
	UMaterialInterface* GlowMat = SlonkMaterials::LoadOrFallback(GlowMaterial, TEXT("Glow"), false);
	EchoHullMIDs.SetNum(EchoHulls.Num());
	EchoGlowMIDs.SetNum(EchoGlows.Num());
	for (int32 i = 0; i < EchoHulls.Num(); ++i)
	{
		if (!EchoHulls[i] || !EchoGlows[i] || !Hull || !Glow)
		{
			continue;
		}
		EchoHulls[i]->GetDynamicMesh()->SetMesh(Hull->GetDynamicMesh()->GetMeshRef());
		EchoGlows[i]->GetDynamicMesh()->SetMesh(Glow->GetDynamicMesh()->GetMeshRef());
		EchoHulls[i]->NotifyMeshUpdated();
		EchoGlows[i]->NotifyMeshUpdated();
		if (HullMat && !EchoHullMIDs[i])
		{
			EchoHullMIDs[i] = UMaterialInstanceDynamic::Create(HullMat, this);
		}
		if (GlowMat && !EchoGlowMIDs[i])
		{
			EchoGlowMIDs[i] = UMaterialInstanceDynamic::Create(GlowMat, this);
		}
		if (EchoHullMIDs[i])
		{
			EchoHullMIDs[i]->SetVectorParameterValue(TEXT("HullColor"), HullColor);
			EchoHullMIDs[i]->SetScalarParameterValue(TEXT("PanelScale"), 400.f / FMath::Max(SizeCm, 1.f));
			EchoHullMIDs[i]->SetScalarParameterValue(TEXT("Roughness"), HullRoughness);
			EchoHullMIDs[i]->SetScalarParameterValue(TEXT("Metallic"), HullMetallic);
			EchoHullMIDs[i]->SetScalarParameterValue(TEXT("Specular"), HullSpecular);
			ApplyWindowParams(EchoHullMIDs[i]);
			EchoHulls[i]->ConfigureMaterialSet({ EchoHullMIDs[i].Get() });
		}
		if (EchoGlowMIDs[i])
		{
			EchoGlowMIDs[i]->SetVectorParameterValue(TEXT("GlowColor"), GlowColor);
			EchoGlowMIDs[i]->SetScalarParameterValue(TEXT("Intensity"), GlowIntensity);
			EchoGlows[i]->ConfigureMaterialSet({ EchoGlowMIDs[i].Get() });
		}
		EchoHulls[i]->SetVisibility(false);
		EchoGlows[i]->SetVisibility(false);
	}
}

void AProceduralStarship::PickWaypoint()
{
	const FVector Dir = NavRng.GetUnitVector();
	WaypointUniverse = HomeUniverse + FVector(Dir.X, Dir.Y, Dir.Z * 0.25f) * PatrolRadiusCm * NavRng.FRandRange(0.4f, 1.f);
	bHasWaypoint = true;
}

void AProceduralStarship::BeginPlay()
{
	Super::BeginPlay();
	// Traffic spawns ships deferred, so HomeLocation is already set here: convert it to universe space in the
	// frame it was given, before a floating-origin rebase can move the world frame under it.
	EnsureHome();
}

void AProceduralStarship::EnsureHome()
{
	if (bHomeSet)
	{
		return;
	}
	if (HomeLocation.IsNearlyZero())
	{
		HomeLocation = GetActorLocation();
	}
	HomeUniverse = ToUniverse(HomeLocation);
	NavRng.Initialize(Seed * 31 + 7);
	WarpTimer = NavRng.FRandRange(WarpIntervalMin, WarpIntervalMax);
	bHomeSet = true;
}

void AProceduralStarship::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	EnsureHome();
	if (bDying)
	{
		return;
	}
	if (WarpPhase != EShipWarpPhase::None)
	{
		TickWarp(DeltaTime);
		if (WarpPhase != EShipWarpPhase::Afterimages && WarpPhase != EShipWarpPhase::None)
		{
			return;
		}
	}
	if (bBeyondHorizon || bTractored)
	{
		Velocity = FVector::ZeroVector;
		return;
	}
	if (bCanWarp && WarpPhase == EShipWarpPhase::None)
	{
		WarpTimer -= DeltaTime;
		if (WarpTimer <= 0.f)
		{
			WarpTimer = NavRng.FRandRange(WarpIntervalMin, WarpIntervalMax);
			if (WarpToRandomStation())
			{
				return;
			}
		}
	}
	const FVector Here = GetActorLocation();
	const FVector Waypoint = ToWorld(WaypointUniverse);
	if (!bHasWaypoint || FVector::Dist(Here, Waypoint) < SizeCm * 4.0)
	{
		PickWaypoint();
	}
	const FVector ToWp = (ToWorld(WaypointUniverse) - Here).GetSafeNormal();
	// Slow into the waypoint so the turn there is not a snap.
	const double Remaining = FVector::Dist(Here, ToWorld(WaypointUniverse));
	const double TargetSpeed = FMath::Min(CruiseSpeed, FMath::Max(Remaining / 20.0, CruiseSpeed * 0.15));
	const FVector DesiredV = ToWp * TargetSpeed;
	const FVector DeltaV = DesiredV - Velocity;
	const double MaxDv = Acceleration * DeltaTime;
	Velocity += DeltaV.Size() > MaxDv ? DeltaV.GetSafeNormal() * MaxDv : DeltaV;
	SetActorLocation(Here + Velocity * DeltaTime);
	if (Root) { Root->ComponentVelocity = Velocity; }
	if (Hull) { Hull->ComponentVelocity = Velocity; }
	WreckVelocity = Velocity;
	if (!Velocity.IsNearlyZero())
	{
		const FQuat Want = FRotationMatrix::MakeFromXZ(Velocity.GetSafeNormal(), FVector::UpVector).ToQuat();
		SetActorRotation(FQuat::Slerp(GetActorQuat(), Want, FMath::Clamp(TurnRate * DeltaTime, 0.f, 1.f)));
	}
}

// ---- Warp -----------------------------------------------------------------------------------------------------

namespace
{
	constexpr double LightSpeedCmPerSec = 2.99792458e10;

	/** The speed of light the warp visuals use: real, or scaled down by the player's visualisation setting. */
	double VisualLightSpeed(const UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const AStarshipPawn* Pawn = PC ? Cast<AStarshipPawn>(PC->GetPawn()) : nullptr;
		return LightSpeedCmPerSec * (Pawn ? FMath::Clamp((double)Pawn->GetWarpVisualLightSpeedScale(), 1e-7, 1.0) : 1.0);
	}
}

bool AProceduralStarship::WarpToRandomStation()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const FVector Home = ToWorld(HomeUniverse);
	TArray<ASpaceStation*> Candidates;
	for (TActorIterator<ASpaceStation> It(World); It; ++It)
	{
		if (It->IsDying())
		{
			continue;
		}
		// Not the station we are already patrolling around.
		const double D = FVector::Dist(It->GetActorLocation(), Home);
		if (D < It->SizeCm * 40.0)
		{
			continue;
		}
		// Stations of other star systems are light-years off: patrols stay in their own system.
		if (D > 4.5e15)
		{
			continue;
		}
		Candidates.Add(*It);
	}
	if (Candidates.Num() == 0)
	{
		return false;
	}
	ASpaceStation* Dest = Candidates[NavRng.RandRange(0, Candidates.Num() - 1)];
	const FVector Offset = NavRng.GetUnitVector() * Dest->SizeCm * NavRng.FRandRange(4.f, 8.f);
	PatrolRadiusCm = Dest->SizeCm * 10.0;
	HomeLocation = Dest->GetActorLocation();
	HomeUniverse = ToUniverse(HomeLocation);
	WarpTo(Dest->GetActorLocation() + Offset);
	return true;
}

void AProceduralStarship::WarpTo(const FVector& DestinationWorld)
{
	UWorld* World = GetWorld();
	if (!World || WarpPhase == EShipWarpPhase::Spooling || WarpPhase == EShipWarpPhase::Travelling || bDying)
	{
		return;
	}
	if (WarpPhase == EShipWarpPhase::Afterimages)
	{
		FinishWarp();
	}
	const FVector Here = GetActorLocation();
	const FVector Delta = DestinationWorld - Here;
	const double Dist = Delta.Size();
	if (Dist < SizeCm * 20.0)
	{
		return;
	}
	WarpDirection = Delta / Dist;
	WarpStartUniverse = ToUniverse(Here);
	WarpEndUniverse = ToUniverse(DestinationWorld);
	// Warp factor chosen so the trip takes about TargetTripSeconds (TNG curve: speed = c * w^(10/3)).
	const double c = VisualLightSpeed(World);
	const double Factor = FMath::Clamp(FMath::Pow(Dist / (c * FMath::Max(TargetTripSeconds, 1.f)), 0.3), 1.5, 8.0);
	WarpSpeed = c * FMath::Pow(Factor, 10.0 / 3.0);
	WarpDepartTime = World->GetTimeSeconds() + StretchSeconds;
	WarpArriveTime = WarpDepartTime + Dist / WarpSpeed;
	WarpPhase = EShipWarpPhase::Spooling;
	PhaseTimer = 0.f;
	Velocity = FVector::ZeroVector;
	// Point down the track first; the stretch reads wrong otherwise.
	SetActorRotation(FRotationMatrix::MakeFromXZ(WarpDirection, FVector::UpVector).ToQuat());
	HomeUniverse = ToUniverse(DestinationWorld);
	bHasWaypoint = false;
}

bool AProceduralStarship::IsRelativisticView() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const AStarshipPawn* Pawn = PC ? Cast<AStarshipPawn>(PC->GetPawn()) : nullptr;
	return Pawn && Pawn->GetWarpVisualMode() == EWarpVisualMode::Relativistic;
}

void AProceduralStarship::BeginTravel()
{
	WarpPhase = EShipWarpPhase::Travelling;
	PhaseTimer = 0.f;
	SetMainStretch(1.f, 1.f);
	if (!IsRelativisticView())
	{
		AWarpStreakFlash::Spawn(GetWorld(), GetActorLocation(), WarpDirection, SizeCm, false, FLinearColor(0.7f, 0.85f, 1.f));
		SetMainVisible(false);
	}
}

void AProceduralStarship::EndTravel()
{
	SetActorLocation(ToWorld(WarpEndUniverse));
	WarpPhase = EShipWarpPhase::Arriving;
	PhaseTimer = 0.f;
	Velocity = FVector::ZeroVector;
	if (!IsRelativisticView())
	{
		AWarpStreakFlash::Spawn(GetWorld(), GetActorLocation(), WarpDirection, SizeCm, true, FLinearColor(0.7f, 0.85f, 1.f));
		SetMainVisible(true);
	}
	// Light from the trip keeps arriving for a long time for a distant observer; give up after ten minutes.
	AfterimageDeadline = GetWorld()->GetTimeSeconds() + 600.0;
}

void AProceduralStarship::FinishWarp()
{
	WarpPhase = EShipWarpPhase::None;
	ResetImages();
	SetMainStretch(1.f, 1.f);
	SetMainVisible(!bBeyondHorizon);
}

void AProceduralStarship::TickWarp(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	PhaseTimer += DeltaTime;
	switch (WarpPhase)
	{
	case EShipWarpPhase::Spooling:
		if (Now >= WarpDepartTime)
		{
			BeginTravel();
		}
		break;
	case EShipWarpPhase::Travelling:
	{
		const double S = FMath::Clamp(Now - WarpDepartTime, 0.0, WarpArriveTime - WarpDepartTime);
		SetActorLocation(ToWorld(WarpStartUniverse) + WarpDirection * (WarpSpeed * S));
		Velocity = WarpDirection * WarpSpeed;
		if (Root) { Root->ComponentVelocity = Velocity; }
		if (Hull) { Hull->ComponentVelocity = Velocity; }
		if (Now >= WarpArriveTime)
		{
			EndTravel();
		}
		break;
	}
	case EShipWarpPhase::Arriving:
		if (PhaseTimer >= StretchSeconds || IsRelativisticView())
		{
			WarpPhase = EShipWarpPhase::Afterimages;
			PhaseTimer = 0.f;
			SetMainStretch(1.f, 1.f);
		}
		break;
	default:
		break;
	}
	UpdateWarpVisuals();
}

FVector AProceduralStarship::GetVisualLocation() const
{
	if (WarpPhase == EShipWarpPhase::Travelling || WarpPhase == EShipWarpPhase::Afterimages)
	{
		const UWorld* World = GetWorld();
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (PC && PC->PlayerCameraManager && IsRelativisticView())
		{
			FShipImage Images[4];
			ComputeImages(PC->PlayerCameraManager->GetCameraLocation(), World->GetTimeSeconds(), Images);
			// The nearest visible image decides whether the actor renders at all.
			double Best = 1e300;
			FVector BestPos = GetActorLocation();
			for (const FShipImage& I : Images)
			{
				if (!I.bVisible) { continue; }
				const double D = I.Position.SizeSquared();
				if (D < Best) { Best = D; BestPos = I.Position; }
			}
			return BestPos;
		}
	}
	return GetActorLocation();
}

void AProceduralStarship::ComputeImages(const FVector& Observer, double Now, FShipImage Out[4]) const
{
	// Slots: 0 = the live ship (after arrival, once its light has had time to get here), 1 = the forward image,
	// 2 = the backward (time-reversed) image, 3 = the ship still sitting at the departure point as seen before the
	// light of its departure arrives. For a superluminal trip the observer sees nothing of the trip until the
	// light front from the closest point sweeps past, then the ship appears at once and splits into an image
	// running on to the destination and another running back to the start.
	for (int32 i = 0; i < 4; ++i) { Out[i] = FShipImage(); }
	const double c = VisualLightSpeed(GetWorld());
	const FVector P0 = ToWorld(WarpStartUniverse);
	const FVector P1 = ToWorld(WarpEndUniverse);
	const double Tau = Now - WarpDepartTime;        // time since departure
	const double SEnd = WarpArriveTime - WarpDepartTime;
	if (Tau < 0.0)
	{
		Out[3].Position = P0; Out[3].bVisible = true; Out[3].Doppler = 1.f;
		return;
	}
	// Departure-point image until the light of the departure gets here.
	if (Tau < FVector::Dist(P0, Observer) / c)
	{
		Out[3].Position = P0; Out[3].bVisible = true; Out[3].Doppler = 1.f;
	}
	// Live ship once the light of its arrival gets here.
	if (Now >= WarpArriveTime && (Now - WarpArriveTime) >= FVector::Dist(P1, Observer) / c)
	{
		Out[0].Position = GetActorLocation(); Out[0].bVisible = true; Out[0].Doppler = 1.f;
	}
	// Retarded positions along the track: |P0 + v s - O| = c (Tau - s).
	const FVector A = P0 - Observer;
	const FVector B = WarpDirection * WarpSpeed;
	const double qa = FVector::DotProduct(B, B) - c * c;
	const double qb = 2.0 * (FVector::DotProduct(A, B) + c * c * Tau);
	const double qc = FVector::DotProduct(A, A) - c * c * Tau * Tau;
	const double Disc = qb * qb - 4.0 * qa * qc;
	if (Disc >= 0.0 && FMath::Abs(qa) > 1e-6)
	{
		const double Root = FMath::Sqrt(Disc);
		const double S1 = (-qb + Root) / (2.0 * qa);
		const double S2 = (-qb - Root) / (2.0 * qa);
		const double Beta = WarpSpeed / c;
		auto Fill = [&](FShipImage& Img, double S)
		{
			if (S < 0.0 || S > SEnd || S > Tau)
			{
				return;
			}
			Img.Position = P0 + B * S;
			const FVector ToObs = (Observer - Img.Position).GetSafeNormal();
			const double CosTheta = FVector::DotProduct(WarpDirection, ToObs);
			Img.Doppler = (float)FMath::Clamp(1.0 / FMath::Max(FMath::Abs(1.0 - Beta * CosTheta), 1e-3), 0.02, 12.0);
			Img.bVisible = true;
		};
		// The larger s is the image further along the track (the forward one).
		Fill(Out[1], FMath::Max(S1, S2));
		Fill(Out[2], FMath::Min(S1, S2));
	}
}

void AProceduralStarship::SetMainVisible(bool bVisible)
{
	if (Hull) { Hull->SetVisibility(bVisible); }
	if (Glow) { Glow->SetVisibility(bVisible); }
	if (GlowWarm) { GlowWarm->SetVisibility(bVisible); }
	if (RunningLights) { RunningLights->SetVisibility(bVisible, true); }
}

void AProceduralStarship::SetMainStretch(float Stretch, float GlowBoost)
{
	const FVector Scale(Stretch, 1.f, 1.f);
	// Stretch forward from the tail so the ship seems to leap ahead.
	const FVector Offset(SizeCm * 0.5f * (Stretch - 1.f), 0.f, 0.f);
	for (USceneComponent* C : { static_cast<USceneComponent*>(Hull.Get()), static_cast<USceneComponent*>(Glow.Get()), static_cast<USceneComponent*>(GlowWarm.Get()), static_cast<USceneComponent*>(RunningLights.Get()) })
	{
		if (C)
		{
			C->SetRelativeScale3D(Scale);
			C->SetRelativeLocation(Offset);
		}
	}
	if (GlowMID)
	{
		GlowMID->SetScalarParameterValue(TEXT("Intensity"), GlowIntensity * GlowBoost);
	}
}

void AProceduralStarship::ApplyImage(int32 Slot, const FShipImage& Image)
{
	UDynamicMeshComponent* H = Slot == 0 ? Hull.Get() : (EchoHulls.IsValidIndex(Slot - 1) ? EchoHulls[Slot - 1].Get() : nullptr);
	UDynamicMeshComponent* G = Slot == 0 ? Glow.Get() : (EchoGlows.IsValidIndex(Slot - 1) ? EchoGlows[Slot - 1].Get() : nullptr);
	UMaterialInstanceDynamic* HM = Slot == 0 ? HullMID.Get() : (EchoHullMIDs.IsValidIndex(Slot - 1) ? EchoHullMIDs[Slot - 1].Get() : nullptr);
	UMaterialInstanceDynamic* GM = Slot == 0 ? GlowMID.Get() : (EchoGlowMIDs.IsValidIndex(Slot - 1) ? EchoGlowMIDs[Slot - 1].Get() : nullptr);
	if (!H || !G)
	{
		return;
	}
	const bool bShow = Image.bVisible && !IsLocationBeyondHorizon(Image.Position);
	H->SetVisibility(bShow);
	G->SetVisibility(bShow);
	if (Slot == 0 && RunningLights)
	{
		RunningLights->SetVisibility(bShow, true);
	}
	if (Slot == 0 && GlowWarm)
	{
		GlowWarm->SetVisibility(bShow);
	}
	if (!bShow)
	{
		return;
	}
	H->SetWorldLocation(Image.Position);
	G->SetWorldLocation(Image.Position);
	if (Slot == 0 && RunningLights)
	{
		RunningLights->SetWorldLocation(Image.Position);
	}
	if (Slot == 0 && GlowWarm)
	{
		GlowWarm->SetWorldLocation(Image.Position);
	}
	// Doppler: blue and brighter when the light is compressed, red and dim when it is stretched. The factor
	// diverges where the two images are born, which reads as the flash of the ship appearing.
	const float X = FMath::Clamp(FMath::Log2(Image.Doppler), -3.f, 3.f);
	FLinearColor Tint = FLinearColor::White;
	float Bright = 1.f;
	// Brightness is eased rather than the full Doppler power law: a receding image at a fifth of the frequency
	// would otherwise be black on black, and the point is to be able to see where the images are.
	if (X > 0.f)
	{
		Tint = FMath::Lerp(FLinearColor::White, FLinearColor(0.5f, 0.7f, 2.0f), FMath::Min(X / 2.f, 1.f));
		Bright = 1.f + X * 1.2f;
	}
	else
	{
		Tint = FMath::Lerp(FLinearColor::White, FLinearColor(2.0f, 0.45f, 0.2f), FMath::Min(-X / 2.f, 1.f));
		Bright = FMath::Pow(2.f, X * 0.5f);
	}
	if (HM)
	{
		HM->SetVectorParameterValue(TEXT("HullColor"), HullColor * Tint * FMath::Clamp(Bright, 0.6f, 1.6f));
	}
	if (GM)
	{
		GM->SetVectorParameterValue(TEXT("GlowColor"), GlowColor * Tint);
		GM->SetScalarParameterValue(TEXT("Intensity"), GlowIntensity * FMath::Clamp(Bright, 0.4f, 6.f));
	}
}

void AProceduralStarship::ResetImages()
{
	for (int32 i = 0; i < EchoHulls.Num(); ++i)
	{
		if (EchoHulls[i]) { EchoHulls[i]->SetVisibility(false); }
		if (EchoGlows[i]) { EchoGlows[i]->SetVisibility(false); }
	}
	for (USceneComponent* C : { static_cast<USceneComponent*>(Hull.Get()), static_cast<USceneComponent*>(Glow.Get()), static_cast<USceneComponent*>(GlowWarm.Get()), static_cast<USceneComponent*>(RunningLights.Get()) })
	{
		if (C)
		{
			C->SetRelativeLocation(FVector::ZeroVector);
			C->SetRelativeScale3D(FVector::OneVector);
		}
	}
	if (HullMID) { HullMID->SetVectorParameterValue(TEXT("HullColor"), HullColor); }
	if (GlowMID)
	{
		GlowMID->SetVectorParameterValue(TEXT("GlowColor"), GlowColor);
		GlowMID->SetScalarParameterValue(TEXT("Intensity"), GlowIntensity);
	}
}

void AProceduralStarship::UpdateWarpVisuals()
{
	UWorld* World = GetWorld();
	if (!World || WarpPhase == EShipWarpPhase::None)
	{
		return;
	}
	const bool bRel = IsRelativisticView();
	const double Now = World->GetTimeSeconds();
	if (!bRel)
	{
		// Show style: stretch going in, hidden in transit (a streak marks the departure), stretched then relaxing on arrival.
		ResetImages();
		switch (WarpPhase)
		{
		case EShipWarpPhase::Spooling:
		{
			const float T = FMath::Clamp(PhaseTimer / FMath::Max(StretchSeconds, 0.01f), 0.f, 1.f);
			const float E = T * T * T;
			SetMainStretch(1.f + 9.f * E, 1.f + 4.f * E);
			SetMainVisible(!bBeyondHorizon);
			break;
		}
		case EShipWarpPhase::Travelling:
			SetMainVisible(false);
			break;
		case EShipWarpPhase::Arriving:
		{
			const float T = FMath::Clamp(PhaseTimer / FMath::Max(StretchSeconds, 0.01f), 0.f, 1.f);
			const float E = (1.f - T) * (1.f - T);
			SetMainStretch(1.f + 9.f * E, 1.f + 4.f * E);
			SetMainVisible(!bBeyondHorizon);
			break;
		}
		case EShipWarpPhase::Afterimages:
			FinishWarp();
			break;
		default:
			break;
		}
		return;
	}

	// Relativistic: draw the ship where its light says it is, as many times as the light says it is there.
	const APlayerController* PC = World->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	if (WarpPhase == EShipWarpPhase::Spooling)
	{
		// Nothing to see yet: the ship is still where it was.
		ResetImages();
		SetMainVisible(!bBeyondHorizon);
		return;
	}
	FShipImage Images[4];
	ComputeImages(PC->PlayerCameraManager->GetCameraLocation(), Now, Images);
	// Slot 3 (the departure-point image) borrows the third echo copy.
	ApplyImage(0, Images[0]);
	ApplyImage(1, Images[1]);
	ApplyImage(2, Images[2]);
	ApplyImage(3, Images[3]);
	if (WarpPhase == EShipWarpPhase::Afterimages)
	{
		const bool bAnyEcho = Images[1].bVisible || Images[2].bVisible || Images[3].bVisible;
		if ((!bAnyEcho && Images[0].bVisible) || Now > AfterimageDeadline)
		{
			FinishWarp();
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Station

ASpaceStation::ASpaceStation()
{
	MaxHealth = 5000.f;
	LoadWeight = 2.f;
	SizeCm = 400000.f;
}

void ASpaceStation::BuildMeshes(UDynamicMesh* HM, UDynamicMesh* GM, FRandomStream& Rng)
{
	using RL = URunningLightsComponent;
	const float D = SizeCm; // overall diameter
	UDynamicMesh* WM = WarmMesh;
	EStationStyle S = Style;
	if (S == EStationStyle::Auto)
	{
		const float P = Rng.FRand();
		S = P < 0.35f ? EStationStyle::Spacedock : (P < 0.55f ? EStationStyle::K7 : (P < 0.75f ? EStationStyle::Regula : EStationStyle::Ring));
	}
	// Panes the size of a ship's, not scaled with the station: at D * 0.012 a 4 km station had 50 m windows.
	WindowPitch = 550.f;
	WindowDeck = 380.f;
	WindowOnFraction = 0.4f;
	const float Bulb = D * 0.0007f;
	auto Dir = [](float Deg) { const float A = FMath::DegreesToRadians(Deg); return FVector(FMath::Cos(A), FMath::Sin(A), 0.f); };
	auto Beacon = [&](const FVector& P, float Phase) { PendingLights.Add(FRunningLight(P, RL::PortRed(), ERunningLightMode::Blink, 1.4f, Phase, Bulb * 1.6f)); };
	// A chase of white strobes round a circle, each a little later than the last.
	auto StrobeRing = [&](float Radius, float Z, int32 Count, float Deg0)
	{
		for (int32 b = 0; b < Count; ++b)
		{
			PendingLights.Add(FRunningLight(Dir(Deg0 + 360.f * b / Count) * Radius + FVector(0.f, 0.f, Z), RL::StrobeWhite(), ERunningLightMode::Strobe, 2.4f, (float)b / Count, Bulb));
		}
	};
	auto AmberRing = [&](float Radius, float Z, int32 Count, float Deg0)
	{
		for (int32 b = 0; b < Count; ++b)
		{
			PendingLights.Add(FRunningLight(Dir(Deg0 + 360.f * b / Count) * Radius + FVector(0.f, 0.f, Z), RL::FormationAmber(), ERunningLightMode::Steady, 1.f, 0.f, Bulb * 0.55f, 0.45f));
		}
	};
	// A lit gallery: a thin ring of glow just proud of the skin.
	auto Gallery = [&](const FVector& C, float Radius, float Minor, int32 Steps = 64) { TorusZ(GM, C, Radius, Minor, Steps, 6); };

	switch (S)
	{
	case EStationStyle::Spacedock:
	{
		// The cap: a vast dome, its waist carrying the docking-bay doors, sloping in underneath to the shaft.
		RevolveZ(HM, FVector::ZeroVector, Scaled({ {0.f, 0.f}, {0.07f, 0.f}, {0.3f, 0.12f}, {0.47f, 0.2f}, {0.5f, 0.26f}, {0.5f, 0.32f}, {0.47f, 0.37f}, {0.35f, 0.42f}, {0.2f, 0.46f}, {0.08f, 0.48f}, {0.f, 0.48f} }, D, D), 64);
		// Reactor pods under the cap, the shaft with its fins, the flared lower pod and the spire.
		for (int32 k = 0; k < 12; ++k)
		{
			Box(HM, Dir(30.f * k) * D * 0.3f + FVector(0.f, 0.f, D * 0.1f), FVector(D * 0.06f, D * 0.04f, D * 0.05f), FRotator(0.f, 30.f * k, 0.f));
		}
		CylinderZ(HM, FVector(0.f, 0.f, -D * 0.55f), D * 0.07f, D * 0.57f, 32);
		for (int32 k = 0; k < 8; ++k)
		{
			Box(HM, Dir(45.f * k) * D * 0.09f + FVector(0.f, 0.f, -D * 0.1f), FVector(D * 0.05f, D * 0.012f, D * 0.12f), FRotator(0.f, 45.f * k, 0.f));
		}
		RevolveZ(HM, FVector::ZeroVector, Scaled({ {0.f, -0.62f}, {0.08f, -0.62f}, {0.17f, -0.56f}, {0.19f, -0.48f}, {0.17f, -0.42f}, {0.08f, -0.38f}, {0.f, -0.38f} }, D, D), 40);
		CylinderZ(HM, FVector(0.f, 0.f, D * 0.48f), D * 0.012f, D * 0.16f, 12);
		Ellipsoid(HM, FVector(0.f, 0.f, D * 0.65f), FVector(D * 0.02f), 8);
		// Three docking-bay doors at the waist, lit from inside, with window galleries above and below.
		for (int32 k = 0; k < 3; ++k)
		{
			const float A = 120.f * k + 60.f;
			Box(HM, Dir(A) * D * 0.502f + FVector(0.f, 0.f, D * 0.29f), FVector(D * 0.012f, D * 0.12f, D * 0.07f), FRotator(0.f, A, 0.f));
			Box(WM ? WM : GM, Dir(A) * D * 0.506f + FVector(0.f, 0.f, D * 0.29f), FVector(D * 0.01f, D * 0.1f, D * 0.055f), FRotator(0.f, A, 0.f));
		}
		Gallery(FVector(0.f, 0.f, D * 0.23f), D * 0.492f, D * 0.003f);
		Gallery(FVector(0.f, 0.f, D * 0.35f), D * 0.489f, D * 0.003f);
		Gallery(FVector(0.f, 0.f, -D * 0.48f), D * 0.189f, D * 0.002f, 40);
		WindowSaucerCenter = FVector(0.f, 0.f, D * 0.3f);
		WindowSaucerRadius = D * 0.5f;
		WindowSaucerThickness = D * 0.06f;
		Beacon(FVector(0.f, 0.f, D * 0.67f + Bulb), 0.f);
		Beacon(FVector(0.f, 0.f, -D * 0.62f - Bulb), 0.5f);
		StrobeRing(D * 0.505f, D * 0.29f, 12, 0.f);
		AmberRing(D * 0.194f, -D * 0.48f, 6, 30.f);
		break;
	}
	case EStationStyle::K7:
	{
		// Hub with a domed top and a mast, a disc under it, three arms to domed pods.
		RevolveZ(HM, FVector::ZeroVector, Scaled({ {0.f, -0.14f}, {0.09f, -0.14f}, {0.1f, -0.1f}, {0.1f, 0.1f}, {0.09f, 0.14f}, {0.04f, 0.2f}, {0.f, 0.2f} }, D, D), 40);
		CylinderZ(HM, FVector(0.f, 0.f, -D * 0.17f), D * 0.13f, D * 0.025f, 40);
		CylinderZ(HM, FVector(0.f, 0.f, D * 0.2f), D * 0.008f, D * 0.12f, 8);
		Gallery(FVector(0.f, 0.f, D * 0.05f), D * 0.1f, D * 0.0015f, 40);
		Gallery(FVector(0.f, 0.f, -D * 0.05f), D * 0.1f, D * 0.0015f, 40);
		for (int32 k = 0; k < 3; ++k)
		{
			const float A = 120.f * k + Rng.FRandRange(-5.f, 5.f);
			const FVector PC = Dir(A) * D * 0.4f;
			Box(HM, Dir(A) * D * 0.27f, FVector(D * 0.32f, D * 0.05f, D * 0.04f), FRotator(0.f, A, 0.f));
			RevolveZ(HM, PC, Scaled({ {0.f, -0.07f}, {0.08f, -0.07f}, {0.1f, -0.03f}, {0.1f, 0.03f}, {0.08f, 0.07f}, {0.03f, 0.1f}, {0.f, 0.1f} }, D, D), 36);
			CylinderZ(HM, PC + FVector(0.f, 0.f, D * 0.1f), D * 0.005f, D * 0.08f, 8);
			Box(HM, PC + Dir(A) * D * 0.1f, FVector(D * 0.03f, D * 0.03f, D * 0.02f), FRotator(0.f, A, 0.f));
			Gallery(PC, D * 0.1f, D * 0.0015f, 48);
			Gallery(PC + FVector(0.f, 0.f, D * 0.05f), D * 0.09f, D * 0.0012f, 48);
			PendingLights.Add(FRunningLight(PC + FVector(0.f, 0.f, D * 0.18f + Bulb), RL::StrobeWhite(), ERunningLightMode::Strobe, 2.4f, k / 3.f, Bulb));
			PendingLights.Add(FRunningLight(PC + Dir(A) * D * 0.102f + FVector(0.f, 0.f, D * 0.03f), RL::FormationAmber(), ERunningLightMode::Steady, 1.f, 0.f, Bulb * 0.55f, 0.45f));
		}
		WindowSaucerCenter = FVector::ZeroVector;
		WindowSaucerRadius = D * 0.1f;
		WindowSaucerThickness = D * 0.1f;
		Beacon(FVector(0.f, 0.f, D * 0.32f + Bulb), 0.f);
		Beacon(FVector(0.f, 0.f, -D * 0.17f - Bulb), 0.5f);
		break;
	}
	case EStationStyle::Regula:
	{
		// Dome over a wide platform, a column with the mid module and lab pods, a drum at the foot and an antenna below.
		RevolveZ(HM, FVector::ZeroVector, Scaled({ {0.f, 0.2f}, {0.22f, 0.2f}, {0.26f, 0.25f}, {0.24f, 0.32f}, {0.15f, 0.38f}, {0.06f, 0.41f}, {0.f, 0.41f} }, D, D), 48);
		CylinderZ(HM, FVector(0.f, 0.f, D * 0.17f), D * 0.42f, D * 0.03f, 56);
		CylinderZ(HM, FVector(0.f, 0.f, -D * 0.4f), D * 0.06f, D * 0.57f, 24);
		CylinderZ(HM, FVector(0.f, 0.f, -D * 0.06f), D * 0.14f, D * 0.12f, 32);
		for (int32 k = 0; k < 4; ++k)
		{
			const float A = 90.f * k + 45.f;
			Box(HM, Dir(A) * D * 0.1f + FVector(0.f, 0.f, -D * 0.24f), FVector(D * 0.1f, D * 0.06f, D * 0.06f), FRotator(0.f, A, 0.f));
		}
		ConeZ(HM, FVector(0.f, 0.f, -D * 0.4f), D * 0.2f, D * 0.06f, D * 0.1f, 40);
		CylinderZ(HM, FVector(0.f, 0.f, -D * 0.5f), D * 0.2f, D * 0.1f, 40);
		CylinderZ(HM, FVector(0.f, 0.f, -D * 0.62f), D * 0.01f, D * 0.12f, 8);
		Gallery(FVector(0.f, 0.f, D * 0.22f), D * 0.228f, D * 0.002f, 48);
		Gallery(FVector(0.f, 0.f, 0.f), D * 0.14f, D * 0.0015f, 40);
		Gallery(FVector(0.f, 0.f, -D * 0.45f), D * 0.2f, D * 0.002f, 40);
		WindowSaucerCenter = FVector(0.f, 0.f, -D * 0.1f);
		WindowSaucerRadius = D * 0.14f;
		WindowSaucerThickness = D * 0.1f;
		Beacon(FVector(0.f, 0.f, D * 0.41f + Bulb), 0.f);
		Beacon(FVector(0.f, 0.f, -D * 0.62f - Bulb), 0.5f);
		StrobeRing(D * 0.425f, D * 0.185f, 8, 0.f);
		AmberRing(D * 0.205f, -D * 0.45f, 6, 0.f);
		break;
	}
	default:
	{
		// A wheel: hub with domed caps and a mast, a boxy ring on spokes, docking ports round the rim.
		RevolveZ(HM, FVector::ZeroVector, Scaled({ {0.f, -0.14f}, {0.07f, -0.14f}, {0.08f, -0.08f}, {0.08f, 0.08f}, {0.07f, 0.14f}, {0.f, 0.14f} }, D, D), 32);
		Ellipsoid(HM, FVector(0.f, 0.f, D * 0.14f), FVector(D * 0.06f, D * 0.06f, D * 0.03f), 10);
		Ellipsoid(HM, FVector(0.f, 0.f, -D * 0.14f), FVector(D * 0.06f, D * 0.06f, D * 0.03f), 10);
		CylinderZ(HM, FVector(0.f, 0.f, D * 0.16f), D * 0.01f, D * 0.15f, 8);
		const float Major = D * 0.47f, RW = D * Rng.FRandRange(0.026f, 0.034f), RH = D * Rng.FRandRange(0.04f, 0.05f);
		TArray<FVector2D> Section = RoundRect(RW, RH, RW * 0.4f, 3);
		for (FVector2D& P : Section) { P.X += Major; }
		RevolveZ(HM, FVector::ZeroVector, Section, 72);
		const int32 Spokes = Rng.RandRange(4, 6);
		for (int32 s = 0; s < Spokes; ++s)
		{
			const float A = 360.f * s / Spokes;
			Box(HM, Dir(A) * (D * 0.08f + Major) * 0.5f, FVector(Major - D * 0.08f + RW, RW * 0.8f, RH * 0.5f), FRotator(0.f, A, 0.f));
			Box(HM, Dir(A) * (Major + RW + D * 0.012f), FVector(D * 0.024f, D * 0.03f, D * 0.03f), FRotator(0.f, A, 0.f));
		}
		Gallery(FVector(0.f, 0.f, RH * 0.4f), Major + RW, D * 0.0018f, 72);
		Gallery(FVector(0.f, 0.f, -RH * 0.4f), Major + RW, D * 0.0018f, 72);
		for (int32 k = 0; k < 4; ++k)
		{
			Gallery(FVector(0.f, 0.f, -D * 0.1f + D * 0.2f * k / 3.f), D * 0.08f, D * 0.0012f, 32);
		}
		WindowSaucerCenter = FVector::ZeroVector;
		WindowSaucerRadius = D * 0.08f;
		WindowSaucerThickness = D * 0.14f;
		Beacon(FVector(0.f, 0.f, D * 0.31f + Bulb), 0.f);
		Beacon(FVector(0.f, 0.f, -D * 0.17f - Bulb), 0.5f);
		StrobeRing(Major + RW * 1.05f, 0.f, Spokes * 2, 180.f / (Spokes * 2));
		AmberRing(Major, RH * 1.02f, Spokes * 2, 180.f / (Spokes * 2));
		AmberRing(Major, -RH * 1.02f, Spokes * 2, 180.f / (Spokes * 2));
		break;
	}
	}
}

void ASpaceStation::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!FMath::IsNearlyZero(SpinDegPerSec))
	{
		AddActorLocalRotation(FRotator(0.f, SpinDegPerSec * DeltaTime, 0.f));
	}
}
