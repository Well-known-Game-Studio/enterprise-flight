#include "BorgCube.h"

#include "Components/DynamicMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "MeshPrims.h"
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
	LoadWeight = 4.f;
	// At the locked exposure a 0.1 albedo reads as mid-grey; the cube should be near-black metal.
	HullColor = FLinearColor(0.02f, 0.027f, 0.023f);
	// Rough, mostly metallic: kills the broad sun highlight that made the old cube read as light grey.
	HullRoughness = 0.9f;
	HullMetallic = 0.1f;
	HullSpecular = 0.f;
	GlowColor = FLinearColor(0.25f, 1.0f, 0.3f);
	VesselName = TEXT("Borg cube");
	BeamMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::BorgBeamPath));
}

void ABorgCube::BuildMeshes(UDynamicMesh* HM, UDynamicMesh* GM, FRandomStream& Rng)
{
	using namespace SlonkMesh;
	const float S = SizeCm;
	// The cube is a frame of heavy girders round a dark core, every face a regular lattice of plated cells,
	// recessed pits and conduit runs, with long rails crossing at the third lines. The surface floats a little
	// off the core, so there is depth and green light down in the gaps rather than strips painted on top.
	Box(HM, FVector::ZeroVector, FVector(S * 0.90f));
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int32 U = (Axis + 1) % 3, V = (Axis + 2) % 3;
		for (int32 a = -1; a <= 1; a += 2)
		{
			for (int32 b = -1; b <= 1; b += 2)
			{
				FVector C = FVector::ZeroVector, Dm(S * 0.05f);
				C[U] = a * S * 0.475f;
				C[V] = b * S * 0.475f;
				Dm[Axis] = S * 0.99f;
				Box(HM, C, Dm);
			}
		}
	}
	for (int32 c = 0; c < 8; ++c)
	{
		Box(HM, FVector((c & 1) ? 1.f : -1.f, (c & 2) ? 1.f : -1.f, (c & 4) ? 1.f : -1.f) * S * 0.47f, FVector(S * 0.07f));
	}
	const int32 N = 12;
	const float Cell = S * 0.96f / N;
	for (int32 Face = 0; Face < 6; ++Face)
	{
		const int32 Axis = Face / 2;
		const float Sign = (Face % 2) ? -1.f : 1.f;
		const int32 U = (Axis + 1) % 3, V = (Axis + 2) % 3;
		// A box on this face: (u, v) across it, standing Depth proud of a base plane Base * S from the centre.
		auto Place = [&](float u, float v, float Depth, float Du, float Dv, UDynamicMesh* M, float Base = 0.48f)
		{
			FVector C = FVector::ZeroVector;
			C[Axis] = Sign * (S * Base + Depth * 0.5f);
			C[U] = u; C[V] = v;
			FVector D = FVector::ZeroVector;
			D[Axis] = Depth; D[U] = Du; D[V] = Dv;
			Box(M, C, D);
		};
		// Rails at the third lines with clamps along them.
		for (int32 k = 1; k <= 2; ++k)
		{
			const float P = -S * 0.48f + S * 0.96f * k / 3.f;
			Place(P, 0.f, S * 0.035f, S * 0.02f, S * 0.96f, HM);
			Place(0.f, P, S * 0.035f, S * 0.96f, S * 0.02f, HM);
			for (int32 q = -4; q <= 4; ++q)
			{
				Place(P, q * S * 0.11f, S * 0.045f, S * 0.034f, S * 0.014f, HM);
				Place(q * S * 0.11f, P, S * 0.045f, S * 0.014f, S * 0.034f, HM);
			}
		}
		// Cells.
		for (int32 i = 0; i < N; ++i)
		{
			for (int32 j = 0; j < N; ++j)
			{
				const float u0 = -S * 0.48f + Cell * (i + 0.5f), v0 = -S * 0.48f + Cell * (j + 0.5f);
				const float Roll = Rng.FRand();
				if (Roll < 0.42f)
				{
					// A plate with a few small blocks on it.
					Place(u0, v0, S * 0.02f, Cell * 0.86f, Cell * 0.86f, HM);
					const int32 n = Rng.RandRange(1, 3);
					for (int32 k = 0; k < n; ++k)
					{
						Place(u0 + Rng.FRandRange(-0.3f, 0.3f) * Cell, v0 + Rng.FRandRange(-0.3f, 0.3f) * Cell, S * Rng.FRandRange(0.01f, 0.03f), Cell * Rng.FRandRange(0.15f, 0.4f), Cell * Rng.FRandRange(0.15f, 0.4f), HM, 0.5f);
					}
				}
				else if (Roll < 0.56f)
				{
					// A tower.
					Place(u0, v0, S * Rng.FRandRange(0.05f, 0.1f), Cell * Rng.FRandRange(0.4f, 0.7f), Cell * Rng.FRandRange(0.4f, 0.7f), HM);
				}
				else if (Roll < 0.82f)
				{
					// A pit: four walls round a green floor down by the core.
					Place(u0, v0, S * 0.004f, Cell * 0.55f, Cell * 0.55f, GM, 0.452f);
					const float w = Cell * 0.1f, h = Cell * 0.8f;
					Place(u0 - h * 0.5f + w * 0.5f, v0, S * 0.025f, w, h, HM);
					Place(u0 + h * 0.5f - w * 0.5f, v0, S * 0.025f, w, h, HM);
					Place(u0, v0 - h * 0.5f + w * 0.5f, S * 0.025f, h, w, HM);
					Place(u0, v0 + h * 0.5f - w * 0.5f, S * 0.025f, h, w, HM);
				}
				else
				{
					// A conduit run: a green strip under a grating of three bars.
					const bool bU = Rng.FRand() < 0.5f;
					Place(u0, v0, S * 0.004f, bU ? Cell * 0.9f : Cell * 0.12f, bU ? Cell * 0.12f : Cell * 0.9f, GM, 0.47f);
					for (int32 k = -1; k <= 1; ++k)
					{
						Place(u0 + (bU ? k * Cell * 0.3f : 0.f), v0 + (bU ? 0.f : k * Cell * 0.3f), S * 0.018f, bU ? Cell * 0.1f : Cell * 0.9f, bU ? Cell * 0.9f : Cell * 0.1f, HM);
					}
				}
			}
		}
		// A few larger vents glowing on the surface.
		for (int32 i = 0; i < 3; ++i)
		{
			Place(Rng.FRandRange(-0.4f, 0.4f) * S, Rng.FRandRange(-0.4f, 0.4f) * S, S * 0.006f, S * 0.03f, S * 0.03f, GM, 0.5f);
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
