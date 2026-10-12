#include "SpaceVessel.h"

#include "Components/DynamicMeshComponent.h"
#include "ExplosionEffect.h"
#include "Engine/World.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SlonkMaterials.h"
#include "SpaceTarget.h"
#include "UDynamicMesh.h"

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
	if (HullMID)
	{
		HullMID->SetVectorParameterValue(TEXT("HullColor"), HullColor);
		HullMID->SetScalarParameterValue(TEXT("PanelScale"), 400.f / FMath::Max(SizeCm, 1.f));
		Hull->SetMaterial(0, HullMID);
	}
	if (GlowMID)
	{
		GlowMID->SetVectorParameterValue(TEXT("GlowColor"), GlowColor);
		Glow->SetMaterial(0, GlowMID);
	}
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
	BuildMeshes(HM, GM, Rng);
	Finish(Hull);
	Finish(Glow);
	bBuilt = true;
	ApplyMaterials();
}

void ASpaceVessel::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	HorizonTimer -= DeltaTime;
	if (HorizonTimer <= 0.f)
	{
		HorizonTimer = 0.5f;
		SetBeyondHorizon(IsLocationBeyondHorizon(GetActorLocation()));
	}
}

float ASpaceVessel::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (Health <= 0.f)
	{
		return Applied;
	}
	Health -= Applied;
	if (Health <= 0.f)
	{
		AExplosionEffect::SpawnExplosion(GetWorld(), GetActorLocation(), SizeCm * 1.5f, 1.8f);
		Destroy();
	}
	return Applied;
}

// ---------------------------------------------------------------------------------------------------------------
// Starship

AProceduralStarship::AProceduralStarship()
{
	MaxHealth = 400.f;
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
}

void AProceduralStarship::PickWaypoint()
{
	const FVector Dir = NavRng.GetUnitVector();
	Waypoint = HomeLocation + FVector(Dir.X, Dir.Y, Dir.Z * 0.25f) * PatrolRadiusCm * NavRng.FRandRange(0.4f, 1.f);
	bHasWaypoint = true;
}

void AProceduralStarship::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bHomeSet)
	{
		if (HomeLocation.IsNearlyZero())
		{
			HomeLocation = GetActorLocation();
		}
		NavRng.Initialize(Seed * 31 + 7);
		bHomeSet = true;
	}
	if (bBeyondHorizon)
	{
		return;
	}
	const FVector Here = GetActorLocation();
	if (!bHasWaypoint || FVector::Dist(Here, Waypoint) < SizeCm * 4.0)
	{
		PickWaypoint();
	}
	const FVector ToWp = (Waypoint - Here).GetSafeNormal();
	// Slow into the waypoint so the turn there is not a snap.
	const double Remaining = FVector::Dist(Here, Waypoint);
	const double TargetSpeed = FMath::Min(CruiseSpeed, FMath::Max(Remaining / 20.0, CruiseSpeed * 0.15));
	const FVector DesiredV = ToWp * TargetSpeed;
	const FVector DeltaV = DesiredV - Velocity;
	const double MaxDv = Acceleration * DeltaTime;
	Velocity += DeltaV.Size() > MaxDv ? DeltaV.GetSafeNormal() * MaxDv : DeltaV;
	SetActorLocation(Here + Velocity * DeltaTime);
	if (!Velocity.IsNearlyZero())
	{
		const FQuat Want = FRotationMatrix::MakeFromXZ(Velocity.GetSafeNormal(), FVector::UpVector).ToQuat();
		SetActorRotation(FQuat::Slerp(GetActorQuat(), Want, FMath::Clamp(TurnRate * DeltaTime, 0.f, 1.f)));
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
}

void ASpaceStation::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!FMath::IsNearlyZero(SpinDegPerSec))
	{
		AddActorLocalRotation(FRotator(0.f, SpinDegPerSec * DeltaTime, 0.f));
	}
}
