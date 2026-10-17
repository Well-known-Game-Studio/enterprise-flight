#include "BorgCube.h"

#include "Components/DynamicMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Kismet/GameplayStatics.h"
#include "PhaserBeam.h"
#include "PhotonTorpedo.h"
#include "SlonkMaterials.h"
#include "UDynamicMesh.h"

ABorgCube::ABorgCube()
{
	MaxHealth = 6000.f;
	SizeCm = 300000.f;
	DeathSeconds = 3.f;
	// At the locked exposure a 0.1 albedo reads as mid-grey; the cube should be near-black metal.
	HullColor = FLinearColor(0.03f, 0.035f, 0.033f);
	GlowColor = FLinearColor(0.25f, 1.0f, 0.3f);
	VesselName = TEXT("Borg cube");
	BeamMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::BorgBeamPath));
}

void ABorgCube::BuildMeshes(UDynamicMesh* HM, UDynamicMesh* GM, FRandomStream& Rng)
{
	using PF = UGeometryScriptLibrary_MeshPrimitiveFunctions;
	FGeometryScriptPrimitiveOptions O;
	const float S = SizeCm;
	auto Box = [&](UDynamicMesh* M, const FVector& C, const FVector& D, const FRotator& R = FRotator::ZeroRotator)
	{
		PF::AppendBox(M, O, FTransform(R, C), D.X, D.Y, D.Z, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Center);
	};
	// Core block, slightly inset so the surface structure stands proud of it.
	Box(HM, FVector::ZeroVector, FVector(S * 0.96f));
	// Per face: a lattice of beams, random greeble blocks, and green conduits.
	for (int32 Face = 0; Face < 6; ++Face)
	{
		const int32 Axis = Face / 2;
		const float Sign = (Face % 2) ? -1.f : 1.f;
		const int32 U = (Axis + 1) % 3, V = (Axis + 2) % 3;
		auto Place = [&](float u, float v, float Depth, float Du, float Dv, UDynamicMesh* M)
		{
			FVector C = FVector::ZeroVector;
			C[Axis] = Sign * (S * 0.48f + Depth * 0.5f);
			C[U] = u; C[V] = v;
			FVector D = FVector::ZeroVector;
			D[Axis] = Depth; D[U] = Du; D[V] = Dv;
			Box(M, C, D);
		};
		// Lattice: 4 beams each way.
		for (int32 k = 0; k < 4; ++k)
		{
			const float P = -S * 0.375f + S * 0.25f * k;
			Place(P, 0.f, S * 0.025f, S * 0.012f, S * 0.98f, HM);
			Place(0.f, P, S * 0.025f, S * 0.98f, S * 0.012f, HM);
		}
		// Greebles at three scales: a few large plates and towers, many mid blocks, a carpet of small ones.
		for (int32 i = 0; i < 6; ++i)
		{
			Place(Rng.FRandRange(-0.4f, 0.4f) * S, Rng.FRandRange(-0.4f, 0.4f) * S, S * Rng.FRandRange(0.05f, 0.11f), S * Rng.FRandRange(0.08f, 0.2f), S * Rng.FRandRange(0.08f, 0.2f), HM);
		}
		for (int32 i = 0; i < 70; ++i)
		{
			Place(Rng.FRandRange(-0.46f, 0.46f) * S, Rng.FRandRange(-0.46f, 0.46f) * S, S * Rng.FRandRange(0.02f, 0.06f), S * Rng.FRandRange(0.03f, 0.09f), S * Rng.FRandRange(0.03f, 0.09f), HM);
		}
		for (int32 i = 0; i < 160; ++i)
		{
			const bool bBar = Rng.FRand() < 0.4f;
			const float A = S * Rng.FRandRange(0.008f, 0.03f), B = bBar ? S * Rng.FRandRange(0.04f, 0.14f) : S * Rng.FRandRange(0.008f, 0.03f);
			Place(Rng.FRandRange(-0.48f, 0.48f) * S, Rng.FRandRange(-0.48f, 0.48f) * S, S * Rng.FRandRange(0.01f, 0.035f), Rng.FRand() < 0.5f ? A : B, Rng.FRand() < 0.5f ? B : A, HM);
		}
		// Pipes running across the face.
		for (int32 i = 0; i < 8; ++i)
		{
			const bool bAlongU = Rng.FRand() < 0.5f;
			const float Len = S * Rng.FRandRange(0.2f, 0.7f), W = S * Rng.FRandRange(0.006f, 0.012f);
			Place(Rng.FRandRange(-0.42f, 0.42f) * S, Rng.FRandRange(-0.42f, 0.42f) * S, S * 0.035f, bAlongU ? Len : W, bAlongU ? W : Len, HM);
		}
		// Conduits: thin glowing strips and a few nodes.
		const int32 G = Rng.RandRange(10, 16);
		for (int32 i = 0; i < G; ++i)
		{
			const bool bAlongU = Rng.FRand() < 0.5f;
			const float Len = S * Rng.FRandRange(0.08f, 0.3f), W = S * 0.006f;
			Place(Rng.FRandRange(-0.44f, 0.44f) * S, Rng.FRandRange(-0.44f, 0.44f) * S, S * 0.055f, bAlongU ? Len : W, bAlongU ? W : Len, GM);
		}
		for (int32 i = 0; i < 4; ++i)
		{
			const float D = S * Rng.FRandRange(0.015f, 0.03f);
			Place(Rng.FRandRange(-0.44f, 0.44f) * S, Rng.FRandRange(-0.44f, 0.44f) * S, S * 0.06f, D, D, GM);
		}
	}
}

void ABorgCube::StopBeam()
{
	if (CuttingBeam)
	{
		CuttingBeam->Destroy();
		CuttingBeam = nullptr;
	}
	bBeamOn = false;
}

void ABorgCube::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopBeam();
	Super::EndPlay(EndPlayReason);
}

void ABorgCube::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (IsDying())
	{
		StopBeam();
		return;
	}
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Player = PC ? PC->GetPawn() : nullptr;
	if (!Player || bBeyondHorizon)
	{
		StopBeam();
		return;
	}
	const FVector Here = GetActorLocation();
	const FVector ToPlayer = Player->GetActorLocation() - Here;
	const double Dist = ToPlayer.Size();
	if (Dist < 1.0)
	{
		return;
	}
	const FVector Dir = ToPlayer / Dist;
	// Hysteresis: aggro inside the range, give up at twice it.
	if (!bHostile && Dist < AggroRangeCm) { bHostile = true; }
	if (bHostile && Dist > AggroRangeCm * 2.0) { bHostile = false; }
	if (!bHostile || bTractored)
	{
		StopBeam();
		Velocity = FVector::ZeroVector;
		return;
	}

	// Turn a face onto the player and close to the hold range.
	const FQuat Want = FRotationMatrix::MakeFromXZ(Dir, FVector::UpVector).ToQuat();
	SetActorRotation(FQuat::Slerp(GetActorQuat(), Want, FMath::Clamp(TurnRate * DeltaTime, 0.f, 1.f)));
	const double WantSpeed = Dist > HoldRangeCm ? ApproachSpeed : 0.0;
	const FVector DesiredV = Dir * WantSpeed;
	Velocity += (DesiredV - Velocity).GetClampedToMaxSize(ApproachSpeed * 0.5 * DeltaTime);
	SetActorLocation(Here + Velocity * DeltaTime);
	if (Root) { Root->ComponentVelocity = Velocity; }
	WreckVelocity = Velocity;

	// Cutting beam: bursts with pauses, straight at the player's hull.
	BeamTimer -= DeltaTime;
	if (BeamTimer <= 0.f)
	{
		bBeamOn = !bBeamOn;
		BeamTimer = bBeamOn ? BeamBurstSeconds : BeamPauseSeconds;
		if (!bBeamOn) { StopBeam(); }
	}
	if (bBeamOn && Dist < BeamRangeCm)
	{
		if (!CuttingBeam)
		{
			FActorSpawnParameters P;
			P.Owner = this;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			CuttingBeam = World->SpawnActorDeferred<APhaserBeam>(APhaserBeam::StaticClass(), GetActorTransform(), this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (CuttingBeam)
			{
				CuttingBeam->BeamColor = WeaponColor;
				CuttingBeam->BeamMaterial = BeamMaterial;
				CuttingBeam->BeamThickness = 900.f;
				UGameplayStatics::FinishSpawningActor(CuttingBeam, GetActorTransform());
			}
		}
		if (CuttingBeam)
		{
			const FVector Start = Here + Dir * SizeCm * 0.5;
			// The player's box ignores visibility traces, so aim at the near side of the hull directly.
			const double PlayerR = Player->GetComponentsBoundingBox().GetExtent().GetMax();
			const FVector Impact = Player->GetActorLocation() - Dir * PlayerR * 0.6;
			CuttingBeam->SetBeam(Start, Impact, true);
			FHitResult Hit;
			Hit.ImpactPoint = Impact;
			Hit.Location = Impact;
			Hit.ImpactNormal = -Dir;
			Hit.Normal = -Dir;
			Hit.HitObjectHandle = FActorInstanceHandle(Player);
			UGameplayStatics::ApplyPointDamage(Player, BeamDamagePerSecond * DeltaTime, Dir, Hit, nullptr, this, nullptr);
		}
	}
	else
	{
		StopBeam();
	}

	// Torpedoes.
	TorpedoTimer -= DeltaTime;
	if (TorpedoTimer <= 0.f && Dist < BeamRangeCm * 1.5)
	{
		TorpedoTimer = TorpedoInterval;
		FActorSpawnParameters P;
		P.Owner = this;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector Tube = Here + Dir * SizeCm * 0.55;
		if (APhotonTorpedo* T = World->SpawnActorDeferred<APhotonTorpedo>(APhotonTorpedo::StaticClass(), FTransform(Dir.Rotation(), Tube), this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			T->GlowColor = WeaponColor;
			T->Damage = 14.f;
			UGameplayStatics::FinishSpawningActor(T, FTransform(Dir.Rotation(), Tube));
			T->Launch(Dir, Velocity.Size(), Player, Dir);
		}
	}
}
