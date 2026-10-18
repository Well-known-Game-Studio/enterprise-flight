#include "BorgCube.h"

#include "Components/DynamicMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "MeshPrims.h"
#include "Components/DynamicMeshComponent.h"
#include "PhysicsEngine/AggregateGeom.h"
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
	HullColor = FLinearColor(0.03f, 0.038f, 0.034f);
	WarmGlowColor = FLinearColor(1.0f, 0.58f, 0.16f);
	// Slot 1 olive panels, slot 2 copper pipework, slot 3 blue-grey steel; slot 0 is the dark base metal.
	HullAccentColors = { FLinearColor(0.038f, 0.048f, 0.034f), FLinearColor(0.085f, 0.055f, 0.038f), FLinearColor(0.032f, 0.036f, 0.046f) };
	// Hundreds of thousands of triangles: the engine refuses complex collision past 250k, so the cube collides as a box.
	if (Hull)
	{
		Hull->CollisionType = ECollisionTraceFlag::CTF_UseSimpleAsComplex;
	}
	// Rough, mostly metallic: kills the broad sun highlight that made the old cube read as light grey.
	HullRoughness = 0.8f;
	HullMetallic = 0.3f;
	HullSpecular = 0.05f;
	GlowColor = FLinearColor(0.25f, 1.0f, 0.3f);
	VesselName = TEXT("Borg cube");
	BeamMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::BorgBeamPath));
}

void ABorgCube::BuildMeshes(UDynamicMesh* HM, UDynamicMesh* GM, FRandomStream& Rng)
{
	using namespace SlonkMesh;
	const float S = SizeCm;
	UDynamicMesh* WM = WarmMesh ? WarmMesh : GM;
	// After the screen cube: a dark core under a skin of panels of many sizes, split like a circuit board with
	// trenches between them, every panel dressed with pipe runs, clamps, small blocks, the odd diagonal plate or
	// ring, and clusters of tiny green and amber lights. A few recessed vents glow green through gratings.
	Box(HM, FVector::ZeroVector, FVector(S * 0.92f));
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int32 U = (Axis + 1) % 3, V = (Axis + 2) % 3;
		for (int32 a = -1; a <= 1; a += 2)
		{
			for (int32 b = -1; b <= 1; b += 2)
			{
				FVector C = FVector::ZeroVector, Dm(S * 0.03f);
				C[U] = a * S * 0.485f;
				C[V] = b * S * 0.485f;
				Dm[Axis] = S * 1.0f;
				Box(HM, C, Dm);
			}
		}
	}
	for (int32 c = 0; c < 8; ++c)
	{
		Box(HM, FVector((c & 1) ? 1.f : -1.f, (c & 2) ? 1.f : -1.f, (c & 4) ? 1.f : -1.f) * S * 0.48f, FVector(S * 0.045f));
	}
	struct FPanel { float u0, v0, u1, v1; };
	for (int32 Face = 0; Face < 6; ++Face)
	{
		const int32 Axis = Face / 2;
		const float Sign = (Face % 2) ? -1.f : 1.f;
		const int32 U = (Axis + 1) % 3, V = (Axis + 2) % 3;
		FVector Normal = FVector::ZeroVector;
		Normal[Axis] = Sign;
		// A box on this face: (u, v) across it, its base Depth0 * S out from the centre, Depth tall, turned by Yaw about the normal.
		auto Place = [&](float u, float v, float Base, float Depth, float Du, float Dv, UDynamicMesh* M, float Yaw = 0.f)
		{
			FVector C = FVector::ZeroVector;
			C[Axis] = Sign * (S * Base + Depth * 0.5f);
			C[U] = u; C[V] = v;
			FVector D = FVector::ZeroVector;
			D[Axis] = Depth; D[U] = Du; D[V] = Dv;
			Box(M, C, D, Yaw != 0.f ? FQuat(Normal, FMath::DegreesToRadians(Yaw)).Rotator() : FRotator::ZeroRotator);
		};
		// A round pipe lying on the face from (u0, v0) to (u1, v1), its axis Radius above the base plane, with ball joints.
		auto Pipe = [&](float u0, float v0, float u1, float v1, float Base, float Radius, bool bJoints)
		{
			FVector A = FVector::ZeroVector, B = FVector::ZeroVector;
			A[Axis] = B[Axis] = Sign * (S * Base + Radius);
			A[U] = u0; A[V] = v0; B[U] = u1; B[V] = v1;
			CylinderBetween(HM, A, B, Radius, 10);
			if (bJoints)
			{
				Ellipsoid(HM, A, FVector(Radius * 1.4f), 6);
				Ellipsoid(HM, B, FVector(Radius * 1.4f), 6);
			}
		};
		auto Slot = [&](int32 ID) { CurrentMaterialID() = ID; };
		// Split the face into panels: binary cuts along the longer side, with a trench between the halves.
		TArray<FPanel> Panels, Work;
		Work.Add({ -S * 0.47f, -S * 0.47f, S * 0.47f, S * 0.47f });
		while (Work.Num() > 0)
		{
			FPanel P = Work.Pop();
			const float W = P.u1 - P.u0, H = P.v1 - P.v0;
			const bool bSplit = FMath::Max(W, H) > S * 0.22f || (FMath::Max(W, H) > S * 0.12f && Rng.FRand() < 0.6f);
			if (!bSplit)
			{
				Panels.Add(P);
				continue;
			}
			const float Gap = Rng.FRand() < 0.3f ? S * 0.012f : S * 0.004f;
			const float F = Rng.FRandRange(0.33f, 0.67f);
			if (W >= H)
			{
				const float Cut = P.u0 + W * F;
				Work.Add({ P.u0, P.v0, Cut - Gap * 0.5f, P.v1 });
				Work.Add({ Cut + Gap * 0.5f, P.v0, P.u1, P.v1 });
			}
			else
			{
				const float Cut = P.v0 + H * F;
				Work.Add({ P.u0, P.v0, P.u1, Cut - Gap * 0.5f });
				Work.Add({ P.u0, Cut + Gap * 0.5f, P.u1, P.v1 });
			}
		}
		// Three long spines across the whole face, over everything, with clamps.
		for (int32 k = 0; k < 3; ++k)
		{
			const bool bU = Rng.FRand() < 0.5f;
			const float At = Rng.FRandRange(-0.4f, 0.4f) * S, Wd = S * Rng.FRandRange(0.008f, 0.014f);
			Slot(k == 1 ? 0 : 2);
			Pipe(bU ? -S * 0.48f : At, bU ? At : -S * 0.48f, bU ? S * 0.48f : At, bU ? At : S * 0.48f, 0.5f, Wd * 0.7f, false);
			Slot(3);
			for (int32 q = -7; q <= 7; ++q)
			{
				const float Along = q * S * 0.065f;
				Place(bU ? Along : At, bU ? At : Along, 0.5f, Wd * 1.5f, bU ? S * 0.01f : Wd * 2.2f, bU ? Wd * 2.2f : S * 0.01f, HM);
			}
			Slot(0);
		}
		// Long loose pipes wandering across panels: an axis-aligned run, an elbow, another run.
		for (int32 k = 0; k < 6; ++k)
		{
			float u = Rng.FRandRange(-0.42f, 0.42f) * S, v = Rng.FRandRange(-0.42f, 0.42f) * S;
			const float R = S * Rng.FRandRange(0.003f, 0.006f);
			Slot(Rng.FRand() < 0.7f ? 2 : 0);
			for (int32 Leg = 0; Leg < 3; ++Leg)
			{
				const bool bU = (Leg % 2 == 0) == (k % 2 == 0);
				const float Len = S * Rng.FRandRange(0.08f, 0.3f) * (Rng.FRand() < 0.5f ? 1.f : -1.f);
				const float nu = FMath::Clamp(bU ? u + Len : u, -S * 0.46f, S * 0.46f), nv = FMath::Clamp(bU ? v : v + Len, -S * 0.46f, S * 0.46f);
				Pipe(u, v, nu, nv, 0.49f, R, true);
				u = nu; v = nv;
			}
			Slot(0);
		}
		for (const FPanel& P : Panels)
		{
			const float W = P.u1 - P.u0, H = P.v1 - P.v0, cu = (P.u0 + P.u1) * 0.5f, cv = (P.v0 + P.v1) * 0.5f;
			// The plate itself, recessed or raised a little, so neighbours step against each other.
			const float Lift = S * (Rng.FRand() < 0.3f ? 0.0f : Rng.FRandRange(0.008f, 0.03f));
			const float Top = 0.46f + Lift / S;
			const float PlateRoll = Rng.FRand();
			Slot(PlateRoll < 0.62f ? 0 : (PlateRoll < 0.88f ? 1 : 3));
			Place(cu, cv, 0.46f, Lift + S * 0.004f, W, H, HM);
			Slot(0);
			const bool bVent = Rng.FRand() < 0.08f && W > S * 0.1f && H > S * 0.1f;
			if (bVent)
			{
				// A deep bay glowing green under a grating.
				Place(cu, cv, Top, S * 0.002f, W * 0.7f, H * 0.7f, GM);
				const int32 Bars = Rng.RandRange(4, 7);
				const bool bU = W > H;
				for (int32 i = 0; i < Bars; ++i)
				{
					const float t = -0.35f + 0.7f * (i + 0.5f) / Bars;
					Place(cu + (bU ? t * W : 0.f), cv + (bU ? 0.f : t * H), Top, S * 0.02f, bU ? S * 0.006f : W * 0.72f, bU ? H * 0.72f : S * 0.006f, HM);
				}
				continue;
			}
			// Frame rails along one or two edges.
			if (Rng.FRand() < 0.6f)
			{
				Place(cu, P.v0 + S * 0.004f, Top, S * 0.01f, W * 0.96f, S * 0.006f, HM);
			}
			if (Rng.FRand() < 0.4f)
			{
				Place(P.u0 + S * 0.004f, cv, Top, S * 0.01f, S * 0.006f, H * 0.96f, HM);
			}
			// Pipe runs with clamps.
			const int32 Pipes = Rng.RandRange(2, 6);
			for (int32 k = 0; k < Pipes; ++k)
			{
				const bool bU = Rng.FRand() < 0.5f;
				const float Wd = S * Rng.FRandRange(0.003f, 0.007f), Ht = S * Rng.FRandRange(0.006f, 0.014f);
				const float Len = (bU ? W : H) * Rng.FRandRange(0.55f, 0.95f);
				const float Off = Rng.FRandRange(-0.4f, 0.4f) * (bU ? H : W);
				const float u = bU ? cu : cu + Off, v = bU ? cv + Off : cv;
				const bool bRound = Rng.FRand() < 0.55f;
				if (bRound)
				{
					const float R = Wd * 0.75f;
					Slot(Rng.FRand() < 0.6f ? 2 : 3);
					Pipe(bU ? u - Len * 0.5f : u, bU ? v : v - Len * 0.5f, bU ? u + Len * 0.5f : u, bU ? v : v + Len * 0.5f, Top, R, true);
					if (Rng.FRand() < 0.4f)
					{
						// An elbow: a shorter leg at right angles from one end.
						const float Leg = Len * Rng.FRandRange(0.25f, 0.6f) * (Rng.FRand() < 0.5f ? 1.f : -1.f);
						const float eu = bU ? u + Len * 0.5f : u, ev = bU ? v : v + Len * 0.5f;
						Pipe(eu, ev, bU ? eu : eu + Leg, bU ? ev + Leg : ev, Top, R, true);
					}
					Slot(0);
				}
				else
				{
					Slot(Rng.FRand() < 0.3f ? 3 : 0);
					Place(u, v, Top, Ht, bU ? Len : Wd, bU ? Wd : Len, HM);
					Slot(0);
				}
				const int32 Clamps = FMath::Max(1, (int32)(Len / (S * 0.045f)));
				Slot(3);
				for (int32 q = 0; q < Clamps; ++q)
				{
					const float Along = -Len * 0.5f + Len * (q + 0.5f) / Clamps;
					Place(bU ? u + Along : u, bU ? v : v + Along, Top, Ht * 1.3f, bU ? Wd * 1.2f : Wd * 2.6f, bU ? Wd * 2.6f : Wd * 1.2f, HM);
				}
				Slot(0);
			}
			// Small blocks, scaled to the panel's area.
			const int32 Blocks = FMath::Clamp((int32)(W * H / (S * S) * 900.f), 4, 40);
			for (int32 k = 0; k < Blocks; ++k)
			{
				const float Du = S * Rng.FRandRange(0.005f, 0.025f), Dv = S * Rng.FRandRange(0.005f, 0.025f);
				const float BlockRoll = Rng.FRand();
				Slot(BlockRoll < 0.7f ? 0 : (BlockRoll < 0.85f ? 1 : 3));
				Place(Rng.FRandRange(P.u0 + Du, P.u1 - Du), Rng.FRandRange(P.v0 + Dv, P.v1 - Dv), Top, S * Rng.FRandRange(0.004f, 0.03f), Du, Dv, HM);
				Slot(0);
			}
			// Sub-plates: a second layer of smaller rectangles, each with its own blocks and maybe a pipe.
			const int32 Subs = FMath::Clamp((int32)(W * H / (S * S) * 70.f), 1, 5);
			for (int32 k = 0; k < Subs; ++k)
			{
				const float SW = W * Rng.FRandRange(0.25f, 0.55f), SH = H * Rng.FRandRange(0.25f, 0.55f);
				const float su = Rng.FRandRange(P.u0 + SW * 0.5f, P.u1 - SW * 0.5f), sv = Rng.FRandRange(P.v0 + SH * 0.5f, P.v1 - SH * 0.5f);
				const float SubLift = S * Rng.FRandRange(0.006f, 0.016f);
				Slot(Rng.FRand() < 0.4f ? 1 : 0);
				Place(su, sv, Top, SubLift, SW, SH, HM);
				Slot(0);
				const float SubTop = Top + SubLift / S;
				const int32 n = Rng.RandRange(2, 6);
				for (int32 q = 0; q < n; ++q)
				{
					const float Du = S * Rng.FRandRange(0.004f, 0.015f), Dv = S * Rng.FRandRange(0.004f, 0.015f);
					Place(Rng.FRandRange(su - SW * 0.45f, su + SW * 0.45f), Rng.FRandRange(sv - SH * 0.45f, sv + SH * 0.45f), SubTop, S * Rng.FRandRange(0.003f, 0.018f), Du, Dv, HM);
				}
				if (Rng.FRand() < 0.4f)
				{
					const bool bU = SW > SH;
					Place(su, sv, SubTop, S * 0.008f, bU ? SW * 0.9f : S * 0.004f, bU ? S * 0.004f : SH * 0.9f, HM);
				}
			}
			// Fin arrays and grids of small blocks: the repeated elements the real surface is covered in.
			if (Rng.FRand() < 0.35f)
			{
				const bool bU = Rng.FRand() < 0.5f;
				const int32 Fins = Rng.RandRange(6, 12);
				const float Pitch = S * 0.009f, FinLen = FMath::Min(W, H) * Rng.FRandRange(0.35f, 0.6f);
				const float fu = Rng.FRandRange(P.u0 + (bU ? Fins * Pitch : FinLen) * 0.5f + S * 0.01f, P.u1 - (bU ? Fins * Pitch : FinLen) * 0.5f - S * 0.01f);
				const float fv = Rng.FRandRange(P.v0 + (bU ? FinLen : Fins * Pitch) * 0.5f + S * 0.01f, P.v1 - (bU ? FinLen : Fins * Pitch) * 0.5f - S * 0.01f);
				Slot(3);
				for (int32 i = 0; i < Fins; ++i)
				{
					const float Off = (i - (Fins - 1) * 0.5f) * Pitch;
					Place(bU ? fu + Off : fu, bU ? fv : fv + Off, Top, S * 0.012f, bU ? S * 0.003f : FinLen, bU ? FinLen : S * 0.003f, HM);
				}
				Slot(0);
			}
			if (Rng.FRand() < 0.3f)
			{
				const int32 Rows = Rng.RandRange(2, 4), Cols = Rng.RandRange(4, 8);
				const float Pitch = S * 0.014f, Cube = S * 0.008f;
				const float gu = Rng.FRandRange(P.u0 + Cols * Pitch * 0.5f + S * 0.01f, P.u1 - Cols * Pitch * 0.5f - S * 0.01f);
				const float gv = Rng.FRandRange(P.v0 + Rows * Pitch * 0.5f + S * 0.01f, P.v1 - Rows * Pitch * 0.5f - S * 0.01f);
				for (int32 r = 0; r < Rows; ++r)
				{
					for (int32 cc = 0; cc < Cols; ++cc)
					{
						Place(gu + (cc - (Cols - 1) * 0.5f) * Pitch, gv + (r - (Rows - 1) * 0.5f) * Pitch, Top, S * Rng.FRandRange(0.006f, 0.014f), Cube, Cube, HM);
					}
				}
			}
			// A diagonal plate or a ring, now and then.
			if (Rng.FRand() < 0.22f && W > S * 0.08f && H > S * 0.08f)
			{
				const float Len = FMath::Min(W, H) * Rng.FRandRange(0.6f, 1.1f);
				Place(cu + Rng.FRandRange(-0.15f, 0.15f) * W, cv + Rng.FRandRange(-0.15f, 0.15f) * H, Top, S * 0.009f, Len, S * Rng.FRandRange(0.01f, 0.025f), HM, Rng.FRand() < 0.5f ? 45.f : -45.f);
			}
			if (Rng.FRand() < 0.08f && W > S * 0.1f && H > S * 0.1f)
			{
				const float R = FMath::Min(W, H) * Rng.FRandRange(0.2f, 0.35f);
				for (int32 k = 0; k < 12; ++k)
				{
					const float A = 30.f * k;
					const float Rad = FMath::DegreesToRadians(A);
					Place(cu + FMath::Cos(Rad) * R, cv + FMath::Sin(Rad) * R, Top, S * 0.008f, R * 0.55f, S * 0.006f, HM, A + 90.f);
				}
			}
			// Lights: a row or grid of tiny windows, mostly green, some amber, and a few strays.
			const int32 Clusters = Rng.RandRange(1, 3);
			for (int32 k = 0; k < Clusters; ++k)
			{
				const bool bU = Rng.FRand() < 0.5f;
				const int32 N = Rng.RandRange(3, 8), Rows = Rng.FRand() < 0.3f ? 2 : 1;
				const float Pitch = S * 0.011f, Sz = S * Rng.FRandRange(0.003f, 0.005f);
				const float u0 = Rng.FRandRange(P.u0 + S * 0.02f, FMath::Max(P.u0 + S * 0.02f, P.u1 - S * 0.02f - (bU ? N * Pitch : Rows * Pitch)));
				const float v0 = Rng.FRandRange(P.v0 + S * 0.02f, FMath::Max(P.v0 + S * 0.02f, P.v1 - S * 0.02f - (bU ? Rows * Pitch : N * Pitch)));
				UDynamicMesh* M = Rng.FRand() < 0.7f ? GM : WM;
				for (int32 r = 0; r < Rows; ++r)
				{
					for (int32 i = 0; i < N; ++i)
					{
						if (Rng.FRand() < 0.2f) { continue; }
						Place(u0 + (bU ? i : r) * Pitch, v0 + (bU ? r : i) * Pitch, Top + 0.001f, S * 0.004f, Sz, Sz, M);
					}
				}
			}
			const int32 Strays = Rng.RandRange(1, 5);
			for (int32 k = 0; k < Strays; ++k)
			{
				const bool bLine = Rng.FRand() < 0.3f;
				const float Sz = S * Rng.FRandRange(0.003f, 0.006f);
				Place(Rng.FRandRange(P.u0 + S * 0.01f, P.u1 - S * 0.01f), Rng.FRandRange(P.v0 + S * 0.01f, P.v1 - S * 0.01f), Top + 0.001f, S * 0.004f, bLine ? Sz * 6.f : Sz, Sz, Rng.FRand() < 0.6f ? GM : WM);
			}
		}
	}
}

void ABorgCube::OnMeshesBuilt()
{
	Super::OnMeshesBuilt();
	if (!Hull)
	{
		return;
	}
	FKAggregateGeom Geom;
	FKBoxElem Box(SizeCm, SizeCm, SizeCm);
	Geom.BoxElems.Add(Box);
	Hull->SetSimpleCollisionShapes(Geom, true);
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
