#include "SpaceVessel.h"

#include "Components/DynamicMeshComponent.h"
#include "ExplosionEffect.h"
#include "HullDebris.h"
#include "Engine/World.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SlonkMaterials.h"
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
		}
		else
		{
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		return C;
	}

	FGeometryScriptPrimitiveOptions Opts()
	{
		FGeometryScriptPrimitiveOptions O;
		return O;
	}

	using PF = UGeometryScriptLibrary_MeshPrimitiveFunctions;

	void Ellipsoid(UDynamicMesh* M, const FVector& Centre, const FVector& Radii, int32 Steps = 14)
	{
		PF::AppendSphereBox(M, Opts(), FTransform(FRotator::ZeroRotator, Centre, Radii), 1.f, Steps, Steps, Steps);
	}
	/** Capsule along local +X. */
	void CapsuleX(UDynamicMesh* M, const FVector& Start, float Length, float Radius, const FRotator& Extra = FRotator::ZeroRotator)
	{
		// AppendCapsule builds along +Z from its base; turn Z onto X.
		const FRotator R = (FQuat(Extra) * FQuat(FRotator(-90.f, 0.f, 0.f))).Rotator();
		PF::AppendCapsule(M, Opts(), FTransform(R, Start), Radius, Length, 6, 16, 0, EGeometryScriptPrimitiveOriginMode::Base);
	}
	void Box(UDynamicMesh* M, const FVector& Centre, const FVector& Dims, const FRotator& Rot = FRotator::ZeroRotator)
	{
		PF::AppendBox(M, Opts(), FTransform(Rot, Centre), Dims.X, Dims.Y, Dims.Z, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Center);
	}
	void CylinderZ(UDynamicMesh* M, const FVector& Base, float Radius, float Height, int32 Steps = 24)
	{
		PF::AppendCylinder(M, Opts(), FTransform(FRotator::ZeroRotator, Base), Radius, Height, Steps, 0, true, EGeometryScriptPrimitiveOriginMode::Base);
	}
	void ConeZ(UDynamicMesh* M, const FVector& Base, float R0, float R1, float Height, int32 Steps = 24)
	{
		PF::AppendCone(M, Opts(), FTransform(FRotator::ZeroRotator, Base), R0, R1, Height, Steps, 1, true, EGeometryScriptPrimitiveOriginMode::Base);
	}
	void TorusZ(UDynamicMesh* M, const FVector& Centre, float Major, float Minor, int32 MajorSteps = 48, int32 MinorSteps = 12)
	{
		FGeometryScriptRevolveOptions RO;
		PF::AppendTorus(M, Opts(), FTransform(FRotator::ZeroRotator, Centre), RO, Major, Minor, MajorSteps, MinorSteps, EGeometryScriptPrimitiveOriginMode::Center);
	}
	void Finish(UDynamicMeshComponent* C)
	{
		FGeometryScriptCalculateNormalsOptions NO;
		UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(C->GetDynamicMesh(), NO);
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
	Rebuild();
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
		ApplyWindowParams(HullMID);
		Hull->ConfigureMaterialSet({ HullMID.Get() });
	}
	if (GlowMID)
	{
		GlowMID->SetVectorParameterValue(TEXT("GlowColor"), GlowColor);
		GlowMID->SetScalarParameterValue(TEXT("Intensity"), GlowIntensity);
		Glow->ConfigureMaterialSet({ GlowMID.Get() });
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
	MID->SetScalarParameterValue(TEXT("WindowPitch"), WindowPitch);
	MID->SetScalarParameterValue(TEXT("WindowDeck"), WindowDeck);
	MID->SetScalarParameterValue(TEXT("WindowGlow"), WindowGlow);
}

void ASpaceVessel::Rebuild()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || !Hull || !Glow)
	{
		return;
	}
	UDynamicMesh* HM = Hull->GetDynamicMesh();
	UDynamicMesh* GM = Glow->GetDynamicMesh();
	if (!HM || !GM)
	{
		return;
	}
	HM->Reset();
	GM->Reset();
	FRandomStream Rng(Seed);
	PendingLights.Reset();
	PendingFloods.Reset();
	BuildMeshes(HM, GM, Rng);
	Finish(Hull);
	Finish(Glow);
	bBuilt = true;
	ApplyMaterials();
	if (RunningLights)
	{
		RunningLights->MaxRadiusCm = SizeCm * 0.03f;
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

void AProceduralStarship::BuildMeshes(UDynamicMesh* HM, UDynamicMesh* GM, FRandomStream& Rng)
{
	// Everything in units of the ship length L, nose at +X, up +Z. Origin at the ship's middle.
	const float L = SizeCm;
	const float SaucerR = L * Rng.FRandRange(0.22f, 0.32f);
	const float SaucerT = SaucerR * Rng.FRandRange(0.16f, 0.26f);
	const FVector SaucerC(L * 0.5f - SaucerR, 0.f, L * Rng.FRandRange(0.10f, 0.16f));
	Ellipsoid(HM, SaucerC, FVector(SaucerR, SaucerR, SaucerT), 18);
	// Bridge module on top, sensor dome beneath.
	Ellipsoid(HM, SaucerC + FVector(0.f, 0.f, SaucerT * 0.85f), FVector(SaucerR * 0.18f, SaucerR * 0.18f, SaucerT * 0.5f), 8);
	Ellipsoid(HM, SaucerC - FVector(0.f, 0.f, SaucerT * 0.8f), FVector(SaucerR * 0.12f, SaucerR * 0.12f, SaucerT * 0.4f), 8);

	// Engineering hull: a capsule behind and below the saucer.
	const float HullR = L * Rng.FRandRange(0.045f, 0.07f);
	const float HullLen = L * Rng.FRandRange(0.36f, 0.48f);
	const FVector HullStart(-L * 0.5f + L * 0.05f, 0.f, -L * Rng.FRandRange(0.02f, 0.06f));
	CapsuleX(HM, HullStart, HullLen, HullR);
	// Window zones for the hull material: the saucer rim and the engineering hull's flanks.
	WindowSaucerCenter = SaucerC;
	WindowSaucerRadius = SaucerR;
	WindowSaucerThickness = SaucerT;
	WindowHullMaxY = HullR * 1.05f;
	WindowHullMaxZ = HullStart.Z + HullR * 0.9f;
	WindowPitch = L * 0.018f;
	WindowDeck = L * 0.012f;
	// Deflector dish at the hull's nose (glows).
	const FVector DishC = HullStart + FVector(HullLen + HullR * 1.9f, 0.f, 0.f);
	PF::AppendCone(GM, Opts(), FTransform(FRotator(-90.f, 0.f, 0.f), DishC - FVector(HullR * 0.25f, 0.f, 0.f)), HullR * 0.75f, HullR * 0.35f, HullR * 0.3f, 20, 1, true, EGeometryScriptPrimitiveOriginMode::Base);

	// Neck: a slanted slab from the hull's top to the saucer's underside.
	const FVector NeckA = HullStart + FVector(HullLen * 0.85f, 0.f, HullR * 0.6f);
	const FVector NeckB = SaucerC + FVector(-SaucerR * 0.55f, 0.f, -SaucerT * 0.6f);
	const FVector NeckDir = (NeckB - NeckA);
	const FRotator NeckRot = FRotationMatrix::MakeFromX(NeckDir.GetSafeNormal()).Rotator();
	Box(HM, (NeckA + NeckB) * 0.5f, FVector(NeckDir.Size() + HullR, L * 0.045f, L * 0.025f), NeckRot);

	// Nacelles: two (or four) capsules on pylons.
	const int32 Pairs = Rng.FRand() < 0.25f ? 2 : 1;
	const float NacR = L * Rng.FRandRange(0.03f, 0.042f);
	const float NacLen = L * Rng.FRandRange(0.38f, 0.5f);
	const float Spread = L * Rng.FRandRange(0.17f, 0.26f);
	const float Lift = L * Rng.FRandRange(0.08f, 0.16f);
	const float NacX = -L * 0.5f + L * Rng.FRandRange(0.02f, 0.1f);
	for (int32 Pair = 0; Pair < Pairs; ++Pair)
	{
		const float Z = Pair == 0 ? Lift : -Lift * 0.9f;
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			const FVector NacStart(NacX, Side * Spread, HullStart.Z + Z);
			CapsuleX(HM, NacStart, NacLen, NacR);
			// Pylon from the hull to the nacelle's middle.
			const FVector PA = HullStart + FVector(HullLen * 0.45f, Side * HullR * 0.5f, Z > 0 ? HullR * 0.5f : -HullR * 0.5f);
			const FVector PB = NacStart + FVector(NacLen * 0.5f, -Side * NacR * 0.4f, 0.f);
			const FVector PD = PB - PA;
			Box(HM, (PA + PB) * 0.5f, FVector(PD.Size(), L * 0.012f, L * 0.05f), FRotationMatrix::MakeFromX(PD.GetSafeNormal()).Rotator());
			// Glow: bussard collector at the front, a strip along the inner side.
			Ellipsoid(GM, NacStart + FVector(NacLen + NacR * 0.9f, 0.f, 0.f), FVector(NacR * 0.95f), 8);
			Box(GM, NacStart + FVector(NacLen * 0.5f, -Side * NacR * 0.95f, 0.f), FVector(NacLen * 0.75f, NacR * 0.25f, NacR * 0.45f));
		}
	}
	// Impulse engines at the saucer's rear.
	Box(GM, SaucerC + FVector(-SaucerR * 0.98f, 0.f, 0.f), FVector(SaucerR * 0.08f, SaucerR * 0.35f, SaucerT * 0.5f));

	// Running lights. Red to port (-Y), green to starboard (+Y), white strobes top, bottom and nose, steady amber
	// formation lights along the saucer rim and the nacelle tails. Every ship gets its own blink phase so a fleet
	// never flashes in unison.
	using RL = URunningLightsComponent;
	const float Bulb = L * 0.004f;
	const float Ph = Rng.FRand();
	const float Strobe = Rng.FRandRange(1.6f, 2.2f);
	const float Blink = Rng.FRandRange(1.1f, 1.5f);
	PendingLights.Add(FRunningLight(SaucerC + FVector(0.f, -SaucerR * 1.01f, 0.f), RL::PortRed(), ERunningLightMode::Blink, Blink, Ph, Bulb * 1.3f));
	PendingLights.Add(FRunningLight(SaucerC + FVector(0.f, SaucerR * 1.01f, 0.f), RL::StarboardGreen(), ERunningLightMode::Blink, Blink, Ph, Bulb * 1.3f));
	PendingLights.Add(FRunningLight(SaucerC + FVector(SaucerR * 1.01f, 0.f, 0.f), RL::StrobeWhite(), ERunningLightMode::Strobe, Strobe, Ph, Bulb * 1.2f));
	PendingLights.Add(FRunningLight(SaucerC + FVector(0.f, 0.f, SaucerT * 0.85f + SaucerT * 0.5f + Bulb), RL::StrobeWhite(), ERunningLightMode::Strobe, Strobe, Ph + 0.5f, Bulb * 1.2f));
	PendingLights.Add(FRunningLight(SaucerC - FVector(0.f, 0.f, SaucerT * 0.8f + SaucerT * 0.4f + Bulb), RL::StrobeWhite(), ERunningLightMode::Strobe, Strobe, Ph + 0.5f, Bulb * 1.2f));
	{
		// Formation lights at 35, 70, 110 and 145 degrees either side of the nose, alternating slow blinks.
		const float Angles[4] = { 35.f, 70.f, 110.f, 145.f };
		for (int32 k = 0; k < 8; ++k)
		{
			const float A = FMath::DegreesToRadians(Angles[k % 4] * (k < 4 ? 1.f : -1.f));
			const FVector P = SaucerC + FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * SaucerR * 1.005f;
			PendingLights.Add(FRunningLight(P, RL::FormationAmber(), ERunningLightMode::Blink, Blink * 2.f, Ph + 0.5f * (k % 2), Bulb * 0.6f, 0.45f));
		}
	}
	// Engineering hull: a white strobe under the belly and an amber tail light.
	PendingLights.Add(FRunningLight(HullStart + FVector(HullLen * 0.5f, 0.f, -HullR * 1.02f), RL::StrobeWhite(), ERunningLightMode::Strobe, Strobe, Ph + 0.25f, Bulb));
	PendingLights.Add(FRunningLight(HullStart + FVector(-HullR * 1.02f, 0.f, 0.f), RL::FormationAmber(), ERunningLightMode::Steady, 1.f, 0.f, Bulb, 0.7f));
	for (int32 Pair = 0; Pair < Pairs; ++Pair)
	{
		const float Z = Pair == 0 ? Lift : -Lift * 0.9f;
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			const FVector NacStart(NacX, Side * Spread, HullStart.Z + Z);
			// Tail of each nacelle: red/green by side, plus an outboard strobe at mid-length.
			PendingLights.Add(FRunningLight(NacStart + FVector(-NacR * 1.02f, 0.f, 0.f), Side < 0 ? RL::PortRed() : RL::StarboardGreen(), ERunningLightMode::Blink, Blink, Ph, Bulb));
			PendingLights.Add(FRunningLight(NacStart + FVector(NacLen * 0.5f, Side * NacR * 1.02f, 0.f), RL::StrobeWhite(), ERunningLightMode::Strobe, Strobe, Ph + 0.5f * Pair + 0.25f, Bulb * 0.9f));
			if (Pair == 0)
			{
				// Pylon root lamp pointing up at the nacelle's inboard underside, a lamp on the nacelle's top shoulder
				// shining aft along the registry, and one on its outboard shoulder for the flank.
				const FVector PylonRoot = HullStart + FVector(HullLen * 0.45f, Side * HullR * 0.5f, HullR * 0.5f);
				PendingFloods.Add(FHullFloodlight(PylonRoot + FVector(0.f, 0.f, HullR * 0.3f), NacStart + FVector(NacLen * 0.5f, -Side * NacR * 0.6f, -NacR * 0.75f), NacR * 1.1f, 14.f));
				PendingFloods.Add(FHullFloodlight(NacStart + FVector(NacLen * 0.85f, 0.f, NacR * 1.1f), NacStart + FVector(NacLen * 0.3f, 0.f, NacR * 0.9f), NacR * 0.9f, 12.f));
				PendingFloods.Add(FHullFloodlight(NacStart + FVector(NacLen * 0.85f, Side * NacR * 1.1f, NacR * 0.2f), NacStart + FVector(NacLen * 0.3f, Side * NacR * 0.95f, 0.f), NacR * 0.9f, 12.f));
			}
		}
	}
	// Saucer floodlights: lamps near the middle of the top and underside facing radially out across the plating.
	{
		const float Angles[6] = { 35.f, 90.f, 145.f, -35.f, -90.f, -145.f };
		for (float Deg : Angles)
		{
			const float A = FMath::DegreesToRadians(Deg);
			const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.f);
			for (int32 Up = -1; Up <= 1; Up += 2)
			{
				// No aft-facing lamps under the saucer: they would wash the neck and engineering hull.
				if (Up < 0 && FMath::Abs(Deg) > 100.f) { continue; }
				// Ellipsoid skin height at a fraction r of the radius is T * sqrt(1 - r^2).
				const FVector Lamp = SaucerC + Dir * SaucerR * 0.24f + FVector(0.f, 0.f, Up * (SaucerT * 0.97f + L * 0.004f));
				const FVector Patch = SaucerC + Dir * SaucerR * 0.68f + FVector(0.f, 0.f, Up * SaucerT * 0.73f);
				PendingFloods.Add(FHullFloodlight(Lamp, Patch, SaucerR * 0.16f, 14.f));
			}
		}
	}
	// Engineering hull flanks: from the stern forward along the name.
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		PendingFloods.Add(FHullFloodlight(HullStart + FVector(HullLen * 0.08f, Side * HullR * 1.1f, HullR * 0.1f), HullStart + FVector(HullLen * 0.75f, Side * HullR * 1.0f, 0.f), HullR * 0.7f, 14.f));
	}
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
		if (FVector::Dist(It->GetActorLocation(), Home) < It->SizeCm * 40.0)
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
	if (RunningLights) { RunningLights->SetVisibility(bVisible, true); }
}

void AProceduralStarship::SetMainStretch(float Stretch, float GlowBoost)
{
	const FVector Scale(Stretch, 1.f, 1.f);
	// Stretch forward from the tail so the ship seems to leap ahead.
	const FVector Offset(SizeCm * 0.5f * (Stretch - 1.f), 0.f, 0.f);
	for (USceneComponent* C : { static_cast<USceneComponent*>(Hull.Get()), static_cast<USceneComponent*>(Glow.Get()), static_cast<USceneComponent*>(RunningLights.Get()) })
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
	for (USceneComponent* C : { static_cast<USceneComponent*>(Hull.Get()), static_cast<USceneComponent*>(Glow.Get()), static_cast<USceneComponent*>(RunningLights.Get()) })
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
	SizeCm = 400000.f;
}

void ASpaceStation::BuildMeshes(UDynamicMesh* HM, UDynamicMesh* GM, FRandomStream& Rng)
{
	const float D = SizeCm; // ring diameter
	const float HubR = D * Rng.FRandRange(0.06f, 0.09f);
	const float HubH = D * Rng.FRandRange(0.55f, 0.8f);
	CylinderZ(HM, FVector(0.f, 0.f, -HubH * 0.5f), HubR, HubH, 28);
	// Windows round the hub: treat it as a tall "saucer" so the arc coordinate wraps round it.
	WindowSaucerCenter = FVector::ZeroVector;
	WindowSaucerRadius = HubR;
	WindowSaucerThickness = HubH * 0.5f;
	WindowPitch = D * 0.012f;
	WindowDeck = D * 0.01f;
	// Mushroom cap and a lower cone.
	ConeZ(HM, FVector(0.f, 0.f, HubH * 0.5f), HubR * 1.1f, D * Rng.FRandRange(0.14f, 0.2f), D * 0.08f, 28);
	CylinderZ(HM, FVector(0.f, 0.f, HubH * 0.5f + D * 0.08f), D * Rng.FRandRange(0.14f, 0.2f), D * 0.04f, 28);
	ConeZ(HM, FVector(0.f, 0.f, -HubH * 0.5f - D * 0.06f), D * 0.03f, HubR * 1.05f, D * 0.06f, 28);
	// One or two rings on spokes.
	const int32 Rings = Rng.FRand() < 0.4f ? 2 : 1;
	const int32 Spokes = Rng.RandRange(3, 8);
	for (int32 r = 0; r < Rings; ++r)
	{
		const float Z = Rings == 1 ? 0.f : (r == 0 ? HubH * 0.2f : -HubH * 0.2f);
		const float Major = D * 0.5f * (r == 0 ? 1.f : 0.75f);
		const float Minor = D * Rng.FRandRange(0.028f, 0.04f);
		TorusZ(HM, FVector(0.f, 0.f, Z), Major, Minor, 64, 14);
		for (int32 s = 0; s < Spokes; ++s)
		{
			const float A = 360.f * s / Spokes + r * 20.f;
			const FVector Dir(FMath::Cos(FMath::DegreesToRadians(A)), FMath::Sin(FMath::DegreesToRadians(A)), 0.f);
			Box(HM, Dir * (HubR + Major) * 0.5f + FVector(0.f, 0.f, Z), FVector(Major - HubR + Minor, Minor * 0.6f, Minor * 0.6f), FRotator(0.f, A, 0.f));
		}
		// Window band and docking beacons around the ring.
		TorusZ(GM, FVector(0.f, 0.f, Z + Minor * 0.35f), Major, Minor * 0.12f, 64, 6);
		TorusZ(GM, FVector(0.f, 0.f, Z - Minor * 0.35f), Major, Minor * 0.12f, 64, 6);
		const int32 Beacons = Spokes * 2;
		for (int32 b = 0; b < Beacons; ++b)
		{
			const float A = 360.f * b / Beacons;
			const FVector Dir(FMath::Cos(FMath::DegreesToRadians(A)), FMath::Sin(FMath::DegreesToRadians(A)), 0.f);
			Ellipsoid(GM, Dir * (Major + Minor * 0.9f) + FVector(0.f, 0.f, Z), FVector(Minor * 0.18f), 6);
		}
	}
	// Window strips on the hub.
	for (int32 k = 0; k < 4; ++k)
	{
		const float Z = -HubH * 0.4f + HubH * 0.8f * k / 3.f;
		TorusZ(GM, FVector(0.f, 0.f, Z), HubR * 1.0f, HubR * 0.04f, 28, 6);
	}

	// Running lights: red obstruction beacons on the mast top and keel, and a chase of white strobes around the
	// outer ring (each one a little later than the last) with steady amber docking markers between them.
	using RL = URunningLightsComponent;
	const float Bulb = D * 0.003f;
	const float TopZ = HubH * 0.5f + D * 0.08f + D * 0.04f;
	PendingLights.Add(FRunningLight(FVector(0.f, 0.f, TopZ + Bulb), RL::PortRed(), ERunningLightMode::Blink, 1.4f, 0.f, Bulb * 1.6f));
	PendingLights.Add(FRunningLight(FVector(0.f, 0.f, -HubH * 0.5f - D * 0.06f - Bulb), RL::PortRed(), ERunningLightMode::Blink, 1.4f, 0.5f, Bulb * 1.6f));
	{
		const float Major = D * 0.5f;
		const float Minor = D * 0.034f;
		const int32 Count = Spokes * 2;
		for (int32 b = 0; b < Count; ++b)
		{
			const float A = FMath::DegreesToRadians(360.f * b / Count + 180.f / Count);
			const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.f);
			PendingLights.Add(FRunningLight(Dir * (Major + Minor * 1.02f), RL::StrobeWhite(), ERunningLightMode::Strobe, 2.4f, (float)b / Count, Bulb));
			PendingLights.Add(FRunningLight(Dir * Major + FVector(0.f, 0.f, Minor * 1.02f), RL::FormationAmber(), ERunningLightMode::Steady, 1.f, 0.f, Bulb * 0.55f, 0.45f));
			PendingLights.Add(FRunningLight(Dir * Major - FVector(0.f, 0.f, Minor * 1.02f), RL::FormationAmber(), ERunningLightMode::Steady, 1.f, 0.f, Bulb * 0.55f, 0.45f));
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
