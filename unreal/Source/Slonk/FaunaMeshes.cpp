#include "FaunaMeshes.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "FaunaProfile.h"
#include "ProcMeshBuild.h"
#include "UObject/Package.h"

using namespace UE::Geometry;

namespace
{
	using SlonkProcMesh::FBuild;
	namespace Vc = SlonkFauna::Vc;

	TMap<uint32, UStaticMesh*> GFaunaMeshes;
	/** Each holder's plans, by their meshes' keys (SlonkFaunaMeshes::HoldPlans). */
	TMap<const void*, TSet<uint32>> GHeldPlans;

	uint32 MeshKey(const FFaunaBodyPlan& Plan, EFaunaKind Kind) { return HashCombine(Plan.Hash(), GetTypeHash((uint8)Kind)); }

	/** Every cached mesh of a plan no holder holds, but an Earth plan's, out of the cache and the root set. */
	void LetGoUnheld()
	{
		TSet<uint32> Kept;
		for (const TPair<const void*, TSet<uint32>>& H : GHeldPlans) { Kept.Append(H.Value); }
		for (int32 K = 0; K < SlonkFauna::NumKinds; ++K) { Kept.Add(MeshKey(SlonkFauna::EarthPlan((EFaunaKind)K), (EFaunaKind)K)); }
		for (auto It = GFaunaMeshes.CreateIterator(); It; ++It)
		{
			if (Kept.Contains(It.Key())) { continue; }
			if (UStaticMesh* M = It.Value(); M && IsValid(M))
			{
				// A name of its own, so the plan's name is free for its mesh made again; freed once nothing draws it.
				M->RemoveFromRoot();
				M->Rename(*MakeUniqueObjectName(GetTransientPackage(), UStaticMesh::StaticClass(), TEXT("SlonkFauna_LetGo")).ToString(), nullptr,
					REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
			}
			It.RemoveCurrent();
		}
	}

	FVector4f VertexCol(float Surface, float Flex, float Side, float Joint) { return FVector4f(Surface, Flex, Side, Joint); }

	/** A triangle wound so its front (the side the renderer shows, as the mesh reads its winding) faces Facing. */
	void TriFacing(FBuild& B, int32 A, int32 Bv, int32 C, const FVector& Facing)
	{
		const FVector Pa = (FVector)B.Mesh.GetVertex(A), Pb = (FVector)B.Mesh.GetVertex(Bv), Pc = (FVector)B.Mesh.GetVertex(C);
		// FDynamicMesh3's own triangle normal (VectorUtil::Normal), for the engine's left-handed frame.
		const FVector N = FVector::CrossProduct(Pc - Pa, Pb - Pa);
		if (FVector::DotProduct(N, Facing) >= 0.0) { B.Tri(A, Bv, C); }
		else { B.Tri(A, C, Bv); }
	}

	/** A wing's colour at a point of it: its flex from the hinge to the tip, its side. */
	FVector4f WingCol(double Y)
	{
		const double Tip = SlonkFauna::UnitCm * 0.5;
		const float Flex = (float)FMath::Clamp((FMath::Abs(Y) - Vc::WingRootCm) / (Tip - Vc::WingRootCm), 0.0, 1.0);
		return VertexCol(Vc::SurfaceWing, Flex, Y > 0.0 ? Vc::SideRight : Vc::SideLeft, Vc::JointWing);
	}

	/**
	 * A bird of the plan, at a LOD: 60, 12 or 2 triangles. Its body a spindle along x (five sides and four rings, a cone
	 * to the nose and one to the rump), a bill, a fanned tail, and two wings each of three panels and a tip from the hinge
	 * on either side of the back to the tip half a span out; at LOD 1 a three-sided spindle, a panel a wing and the tail;
	 * at LOD 2 a triangle a wing, which still beats.
	 */
	void BuildBird(FBuild& B, const FFaunaBodyPlan& Plan, int32 Lod)
	{
		const double U = SlonkFauna::UnitCm;
		const double L = Plan.Length * U;
		const double Rg = FMath::Max(Plan.Girth * U * 0.5, 1.0);
		const double Nose = 0.36 * L, Rear = -0.4 * L;
		const double Root = Vc::WingRootCm, Tip = U * 0.5;
		const double Chord = FMath::Max(Plan.Chord * U, 4.0);
		const double TailRoot = -0.27 * L, TailLen = FMath::Max(Plan.Tail * U, 2.0);
		const FVector4f BodyC = VertexCol(Vc::SurfaceBody, 0.f, Vc::SideMiddle, Vc::JointRigid);
		const double WingZ = 0.3 * Rg;
		if (Lod >= 2)
		{
			for (const double S : { -1.0, 1.0 })
			{
				const int32 A = B.Vert(FVector(8.0, S * Root, WingZ), FVector::ZAxisVector, FVector2f(0.f, 0.f), WingCol(S * Root));
				const int32 Bv = B.Vert(FVector(Rear - 4.0, S * Root, WingZ), FVector::ZAxisVector, FVector2f(1.f, 0.f), WingCol(S * Root));
				const int32 C = B.Vert(FVector(-4.0, S * Tip, WingZ), FVector::ZAxisVector, FVector2f(0.5f, 1.f), WingCol(S * Tip));
				TriFacing(B, A, Bv, C, FVector::ZAxisVector);
			}
			return;
		}
		// The body.
		const int32 Sides = Lod == 0 ? 5 : 3;
		const TArray<double> Stations = Lod == 0 ? TArray<double>{ 0.12, 0.42, 0.72, 0.9 } : TArray<double>{ 0.5 };
		const TArray<double> Radii = Lod == 0 ? TArray<double>{ 0.55, 1.0, 0.85, 0.55 } : TArray<double>{ 1.0 };
		TArray<TArray<int32>> Rings;
		TArray<double> RingX;
		for (int32 r = 0; r < Stations.Num(); ++r)
		{
			const double X = FMath::Lerp(Rear, Nose, Stations[r]);
			TArray<int32> Ring;
			for (int32 k = 0; k < Sides; ++k)
			{
				const double A = UE_DOUBLE_TWO_PI * k / Sides + UE_DOUBLE_HALF_PI;
				const FVector D(0.0, FMath::Cos(A), FMath::Sin(A));
				Ring.Add(B.Vert(FVector(X, D.Y * Rg * Radii[r], D.Z * Rg * Radii[r] * 0.85), D, FVector2f((float)k / Sides, (float)Stations[r]), BodyC));
			}
			Rings.Add(Ring);
			RingX.Add(X);
		}
		auto Centroid = [&B](int32 A, int32 Bv, int32 C) { return ((FVector)B.Mesh.GetVertex(A) + (FVector)B.Mesh.GetVertex(Bv) + (FVector)B.Mesh.GetVertex(C)) / 3.0; };
		for (int32 r = 0; r + 1 < Rings.Num(); ++r)
		{
			for (int32 k = 0; k < Sides; ++k)
			{
				const int32 A = Rings[r][k], Bv = Rings[r][(k + 1) % Sides], C = Rings[r + 1][(k + 1) % Sides], Dv = Rings[r + 1][k];
				const FVector M1 = Centroid(A, Bv, C), M2 = Centroid(A, C, Dv);
				TriFacing(B, A, Bv, C, FVector(0.0, M1.Y, M1.Z));
				TriFacing(B, A, C, Dv, FVector(0.0, M2.Y, M2.Z));
			}
		}
		// Coned to the head and to the rump, each from a point inside the body.
		const int32 Head = B.Vert(FVector(Lod == 0 ? Nose : Nose + 3.0, 0.0, 0.0), FVector::XAxisVector, FVector2f(0.5f, 1.f), BodyC);
		const int32 Rump = B.Vert(FVector(Rear, 0.0, 0.0), -FVector::XAxisVector, FVector2f(0.5f, 0.f), BodyC);
		for (int32 k = 0; k < Sides; ++k)
		{
			const TArray<int32>& Front = Rings.Last();
			const TArray<int32>& Back = Rings[0];
			TriFacing(B, Front[k], Front[(k + 1) % Sides], Head, Centroid(Front[k], Front[(k + 1) % Sides], Head) - FVector(RingX.Last() - 2.0, 0.0, 0.0));
			TriFacing(B, Back[k], Back[(k + 1) % Sides], Rump, Centroid(Back[k], Back[(k + 1) % Sides], Rump) - FVector(RingX[0] + 2.0, 0.0, 0.0));
		}
		if (Lod == 0)
		{
			// The bill, a three-sided point from inside the head.
			const FVector4f BillC = VertexCol(Vc::SurfaceBeak, 0.f, Vc::SideMiddle, Vc::JointRigid);
			const double BillLen = 0.11 * L;
			const int32 B0 = B.Vert(FVector(Nose - 1.0, -0.3 * Rg, -0.15 * Rg), FVector(0.0, -1.0, -0.4), FVector2f(0.f, 0.f), BillC);
			const int32 B1 = B.Vert(FVector(Nose - 1.0, 0.3 * Rg, -0.15 * Rg), FVector(0.0, 1.0, -0.4), FVector2f(1.f, 0.f), BillC);
			const int32 B2 = B.Vert(FVector(Nose - 1.0, 0.0, 0.3 * Rg), FVector(0.0, 0.0, 1.0), FVector2f(0.5f, 0.f), BillC);
			const int32 BT = B.Vert(FVector(Nose + BillLen, 0.0, -0.05 * Rg), FVector::XAxisVector, FVector2f(0.5f, 1.f), BillC);
			const FVector Inside(Nose - 1.0, 0.0, 0.0);
			TriFacing(B, B0, B1, BT, Centroid(B0, B1, BT) - Inside);
			TriFacing(B, B1, B2, BT, Centroid(B1, B2, BT) - Inside);
			TriFacing(B, B2, B0, BT, Centroid(B2, B0, BT) - Inside);
		}
		// The tail: a fan behind the rump, notched at LOD 0, flexing from its root.
		{
			auto TailC = [&](double X) { return VertexCol(Vc::SurfaceTail, (float)FMath::Clamp((TailRoot - X) / TailLen, 0.0, 1.0), Vc::SideMiddle, Vc::JointTail); };
			const double TZ = 0.2 * Rg, End = TailRoot - TailLen;
			const int32 RL = B.Vert(FVector(TailRoot, -0.55 * Rg, TZ), FVector::ZAxisVector, FVector2f(0.f, 0.f), TailC(TailRoot));
			const int32 RR = B.Vert(FVector(TailRoot, 0.55 * Rg, TZ), FVector::ZAxisVector, FVector2f(1.f, 0.f), TailC(TailRoot));
			const int32 TL = B.Vert(FVector(End, -1.45 * Rg, TZ), FVector::ZAxisVector, FVector2f(0.f, 1.f), TailC(End));
			const int32 TR = B.Vert(FVector(End, 1.45 * Rg, TZ), FVector::ZAxisVector, FVector2f(1.f, 1.f), TailC(End));
			if (Lod == 0)
			{
				const double Notch = TailRoot - 0.85 * TailLen;
				const int32 TC = B.Vert(FVector(Notch, 0.0, TZ), FVector::ZAxisVector, FVector2f(0.5f, 0.85f), TailC(Notch));
				TriFacing(B, RL, RR, TC, FVector::ZAxisVector);
				TriFacing(B, RL, TC, TL, FVector::ZAxisVector);
				TriFacing(B, RR, TR, TC, FVector::ZAxisVector);
			}
			else
			{
				TriFacing(B, RL, RR, TR, FVector::ZAxisVector);
				TriFacing(B, RL, TR, TL, FVector::ZAxisVector);
			}
		}
		// The wings: from the hinge out, the leading edge swept back a little toward the tip, the chord narrowing.
		for (const double S : { -1.0, 1.0 })
		{
			auto V = [&](double X, double Y, float UVx, float UVy) { return B.Vert(FVector(X, S * Y, WingZ), FVector::ZAxisVector, FVector2f(UVx, UVy), WingCol(S * Y)); };
			if (Lod == 0)
			{
				const double Ys[4] = { Root, 18.0, 34.0, 46.0 };
				const double Lead[4] = { 5.0, 5.0, 3.0, -1.0 };
				const double Trail[4] = { 5.0 - Chord, 5.0 - 0.92 * Chord, 3.0 - 0.67 * Chord, -1.0 - 0.33 * Chord };
				int32 Le[4], Te[4];
				for (int32 i = 0; i < 4; ++i)
				{
					const float Fy = (float)((Ys[i] - Root) / (Tip - Root));
					Le[i] = V(Lead[i], Ys[i], 0.f, Fy);
					Te[i] = V(Trail[i], Ys[i], 1.f, Fy);
				}
				for (int32 i = 0; i < 3; ++i)
				{
					TriFacing(B, Le[i], Te[i], Te[i + 1], FVector::ZAxisVector);
					TriFacing(B, Le[i], Te[i + 1], Le[i + 1], FVector::ZAxisVector);
				}
				TriFacing(B, Le[3], Te[3], V(-4.0 - 0.15 * Chord, Tip, 0.5f, 1.f), FVector::ZAxisVector);
			}
			else
			{
				const int32 A = V(5.0, Root, 0.f, 0.f), Bv = V(5.0 - Chord, Root, 1.f, 0.f), C = V(-7.0, Tip, 1.f, 1.f), Dv = V(-2.0, Tip, 0.f, 1.f);
				TriFacing(B, A, Bv, C, FVector::ZAxisVector);
				TriFacing(B, A, C, Dv, FVector::ZAxisVector);
			}
		}
	}
}

namespace
{
	/** A walker's colour at a point of it: the surface, the flex from its joint, its side (or a leg's place in the gait),
	 *  its joint. */
	FVector4f WalkerCol(float Surface, float Flex, float SideOrPhase, float Joint) { return FVector4f(Surface, Flex, SideOrPhase, Joint); }
	float SideOf(double Y) { return Y > 0.5 ? Vc::SideRight : (Y < -0.5 ? Vc::SideLeft : Vc::SideMiddle); }
	/** Its UV: along the body from the rump (U) and up it (V), for the coat's shading. */
	FVector2f WalkerUV(const FVector& P) { return FVector2f((float)((P.X + 50.0) / 100.0), (float)(P.Z / 60.0)); }

	/** A ring of Sides about a centre in the plane square to Along (whose own up is Ax2, its side Ax1), radii R1 and R2,
	 *  starting StartA round, its normals out from the centre. */
	TArray<int32> WalkerRing(FBuild& B, const FVector& Centre, const FVector& Ax1, const FVector& Ax2, double R1, double R2, int32 Sides, double StartA,
		TFunctionRef<FVector4f(const FVector&)> Col)
	{
		TArray<int32> Ring;
		for (int32 k = 0; k < Sides; ++k)
		{
			const double A = UE_DOUBLE_TWO_PI * k / Sides + StartA;
			const FVector Off = Ax1 * (FMath::Cos(A) * R1) + Ax2 * (FMath::Sin(A) * R2);
			const FVector P = Centre + Off;
			Ring.Add(B.Vert(P, Off.GetSafeNormal(), WalkerUV(P), Col(P)));
		}
		return Ring;
	}
	/** The quads between two rings of a tube, each face wound to show the side its normals point to (out from its axis). */
	void WalkerBand(FBuild& B, const TArray<int32>& A, const TArray<int32>& C, const FVector& CentreA, const FVector& CentreC)
	{
		const int32 Sides = A.Num();
		for (int32 k = 0; k < Sides; ++k)
		{
			const int32 V0 = A[k], V1 = A[(k + 1) % Sides], V2 = C[(k + 1) % Sides], V3 = C[k];
			const FVector Mid = ((FVector)B.Mesh.GetVertex(V0) + (FVector)B.Mesh.GetVertex(V1) + (FVector)B.Mesh.GetVertex(V2) + (FVector)B.Mesh.GetVertex(V3)) * 0.25;
			const FVector Axis = (CentreA + CentreC) * 0.5;
			TriFacing(B, V0, V1, V2, Mid - Axis);
			TriFacing(B, V0, V2, V3, Mid - Axis);
		}
	}
	/** A ring closed facing Out: a triangle or a quad over a ring of three or four, else a fan to a point (Tip). Its own
	 *  vertices, lit as the cap they close (their normals Out), in the ring's places and colours. */
	void WalkerCap(FBuild& B, const TArray<int32>& Ring, const FVector& Tip, const FVector& Out, const FVector4f& Col)
	{
		TArray<int32> Cap;
		for (const int32 V : Ring) { Cap.Add(B.Vert((FVector)B.Mesh.GetVertex(V), Out, B.UV->GetElement(V), B.C->GetElement(V))); }
		if (Cap.Num() == 3) { TriFacing(B, Cap[0], Cap[1], Cap[2], Out); return; }
		if (Cap.Num() == 4)
		{
			TriFacing(B, Cap[0], Cap[1], Cap[2], Out);
			TriFacing(B, Cap[0], Cap[2], Cap[3], Out);
			return;
		}
		const int32 C = B.Vert(Tip, Out, WalkerUV(Tip), Col);
		for (int32 k = 0; k < Cap.Num(); ++k) { TriFacing(B, Cap[k], Cap[(k + 1) % Cap.Num()], C, Out); }
	}

	/**
	 * A walker of the plan (an Earth grazer, four legs), at a LOD: 150, 30 or 2 triangles, a metre from its tail's tip to
	 * its muzzle, its feet on z 0. LOD 0: a six-sided torso of five rings capped at the rump and the chest, the neck and
	 * head a four-sided tube of five rings from inside the chest to the muzzle, two horns, four three-sided legs from inside
	 * the torso to the hoof, a tail; LOD 1: a four-sided torso, a three-sided neck and head, a triangle a leg, the tail; LOD
	 * 2: one triangle standing along it and one lying over its back. The neck and head's flex runs from 0 at their root to 1
	 * at the muzzle, a leg's from 0 at its hip to 1 at its hoof (straight down it, so the material shears it straight).
	 */
	void BuildWalker(FBuild& B, const FFaunaBodyPlan& Plan, int32 Lod)
	{
		const float Joint = Vc::JointRigid;
		auto BodyCol = [&](const FVector& P) { return WalkerCol(Vc::SurfaceBody, 0.f, SideOf(P.Y), Joint); };
		const double Girth = FMath::Clamp((double)Plan.Girth, 0.2, 0.5) / 0.35;
		const FVector YAx = FVector::YAxisVector, ZAx = FVector::ZAxisVector;
		if (Lod >= 2)
		{
			// A speck: its side standing along it, its back over it.
			const FVector S0(-46.0, 0.0, 30.0), S1(50.0, 0.0, 42.0), S2(-6.0, 0.0, 58.0);
			const int32 A = B.Vert(S0, YAx, WalkerUV(S0), BodyCol(S0)), Bv = B.Vert(S1, YAx, WalkerUV(S1), BodyCol(S1)), Cv = B.Vert(S2, YAx, WalkerUV(S2), BodyCol(S2));
			TriFacing(B, A, Bv, Cv, YAx);
			const FVector T0(-46.0, -12.0 * Girth, 54.0), T1(-46.0, 12.0 * Girth, 54.0), T2(40.0, 0.0, 50.0);
			const int32 D = B.Vert(T0, ZAx, WalkerUV(T0), BodyCol(T0)), E = B.Vert(T1, ZAx, WalkerUV(T1), BodyCol(T1)), F = B.Vert(T2, ZAx, WalkerUV(T2), BodyCol(T2));
			TriFacing(B, D, E, F, ZAx);
			return;
		}
		const bool bFine = Lod == 0;
		// The torso.
		{
			const int32 Sides = bFine ? 6 : 4;
			const TArray<double> X = bFine ? TArray<double>{ -46.0, -32.0, -12.0, 6.0, 20.0 } : TArray<double>{ -44.0, 18.0 };
			const TArray<double> Zc = bFine ? TArray<double>{ 46.0, 47.0, 45.0, 46.0, 46.0 } : TArray<double>{ 46.0, 46.0 };
			const TArray<double> Rz = bFine ? TArray<double>{ 11.0, 13.0, 14.0, 13.5, 12.0 } : TArray<double>{ 12.5, 12.5 };
			const TArray<double> Ry = bFine ? TArray<double>{ 10.0, 13.0, 14.0, 13.0, 11.0 } : TArray<double>{ 12.0, 12.0 };
			const double StartA = bFine ? UE_DOUBLE_HALF_PI : UE_DOUBLE_HALF_PI * 0.5;
			TArray<TArray<int32>> Rings;
			for (int32 r = 0; r < X.Num(); ++r) { Rings.Add(WalkerRing(B, FVector(X[r], 0.0, Zc[r]), YAx, ZAx, Ry[r] * Girth, Rz[r] * Girth, Sides, StartA, BodyCol)); }
			for (int32 r = 0; r + 1 < Rings.Num(); ++r) { WalkerBand(B, Rings[r], Rings[r + 1], FVector(X[r], 0.0, Zc[r]), FVector(X[r + 1], 0.0, Zc[r + 1])); }
			const FVector4f Mid = WalkerCol(Vc::SurfaceBody, 0.f, Vc::SideMiddle, Joint);
			WalkerCap(B, Rings[0], FVector(X[0] - 3.0, 0.0, Zc[0]), -FVector::XAxisVector, Mid);
			WalkerCap(B, Rings.Last(), FVector(X.Last() + 4.0, 0.0, Zc.Last() - 1.0), FVector::XAxisVector, Mid);
		}
		// The neck and head: a tube from inside the chest up to the poll and down to the muzzle, each ring square to it.
		{
			const int32 Sides = bFine ? 4 : 3;
			const TArray<FVector> C = bFine ? TArray<FVector>{ FVector(14.0, 0.0, 40.0), FVector(24.0, 0.0, 47.0), FVector(34.0, 0.0, 53.0), FVector(43.0, 0.0, 47.0), FVector(48.5, 0.0, 42.0) }
				: TArray<FVector>{ FVector(14.0, 0.0, 40.0), FVector(34.0, 0.0, 53.0), FVector(48.5, 0.0, 42.0) };
			const TArray<double> R = bFine ? TArray<double>{ 8.0, 7.5, 6.5, 5.5, 4.0 } : TArray<double>{ 7.0, 6.0, 3.5 };
			TArray<TArray<int32>> Rings;
			for (int32 r = 0; r < C.Num(); ++r)
			{
				const FVector Along = (C[FMath::Min(r + 1, C.Num() - 1)] - C[FMath::Max(r - 1, 0)]).GetSafeNormal();
				const FVector Up = FVector::CrossProduct(Along, YAx).GetSafeNormal() * -1.0;
				const float Flex = (float)r / (C.Num() - 1);
				// The muzzle's ring is dark.
				const float Surface = r == C.Num() - 1 ? Vc::SurfaceBeak : Vc::SurfaceBody;
				Rings.Add(WalkerRing(B, C[r], YAx, Up.Z < 0.0 ? -Up : Up, R[r] * Girth, R[r], Sides, bFine ? UE_DOUBLE_PI * 0.25 : UE_DOUBLE_HALF_PI,
					[&](const FVector& P) { return WalkerCol(Surface, Flex, SideOf(P.Y), Vc::JointNeck); }));
			}
			for (int32 r = 0; r + 1 < Rings.Num(); ++r) { WalkerBand(B, Rings[r], Rings[r + 1], C[r], C[r + 1]); }
			if (bFine)
			{
				WalkerCap(B, Rings.Last(), C.Last(), (C.Last() - C[C.Num() - 2]).GetSafeNormal(), WalkerCol(Vc::SurfaceBeak, 1.f, Vc::SideMiddle, Vc::JointNeck));
				// Two horns from the poll, out and up.
				for (const double S : { -1.0, 1.0 })
				{
					const FVector H0(33.0, S * 4.0, 57.5), H1(36.0, S * 3.5, 57.0), H2(32.0, S * 11.0, 61.0);
					const FVector4f Hc = WalkerCol(Vc::SurfaceBeak, 0.5f, S > 0.0 ? Vc::SideRight : Vc::SideLeft, Vc::JointNeck);
					const int32 A = B.Vert(H0, ZAx, WalkerUV(H0), Hc), Bv = B.Vert(H1, ZAx, WalkerUV(H1), Hc), Cv = B.Vert(H2, ZAx, WalkerUV(H2), Hc);
					TriFacing(B, A, Bv, Cv, ZAx);
				}
			}
		}
		// The legs: from inside the torso to the hoof, the hind left, fore left, hind right and fore right a quarter of a
		// stride apart.
		{
			constexpr double Top = 38.0;
			struct FLeg { double X, Y; float Phase; };
			const FLeg Legs[4] = { { -33.0, -8.0, 0.f }, { 10.0, -8.0, 0.25f }, { -33.0, 8.0, 0.5f }, { 10.0, 8.0, 0.75f } };
			for (const FLeg& L : Legs)
			{
				const double Y = L.Y * Girth;
				auto LegCol = [&](const FVector& P)
				{
					const float Flex = (float)FMath::Clamp((Top - P.Z) / Top, 0.0, 1.0);
					return WalkerCol(Flex > 0.99f ? Vc::SurfaceBeak : Vc::SurfaceBody, Flex, L.Phase, Vc::JointLeg);
				};
				if (bFine)
				{
					const TArray<double> Z = { Top, 17.0, 0.0 };
					const TArray<double> R = { 3.8, 2.6, 2.4 };
					TArray<TArray<int32>> Rings;
					for (int32 r = 0; r < 3; ++r) { Rings.Add(WalkerRing(B, FVector(L.X, Y, Z[r]), FVector::XAxisVector, YAx, R[r], R[r], 3, 0.0, LegCol)); }
					for (int32 r = 0; r < 2; ++r) { WalkerBand(B, Rings[r], Rings[r + 1], FVector(L.X, Y, Z[r]), FVector(L.X, Y, Z[r + 1])); }
					WalkerCap(B, Rings.Last(), FVector(L.X, Y, 0.0), -ZAx, LegCol(FVector(L.X, Y, 0.0)));
				}
				else
				{
					const FVector Out(0.0, Y > 0.0 ? 1.0 : -1.0, 0.0);
					const FVector P0(L.X - 4.0, Y, Top), P1(L.X + 4.0, Y, Top), P2(L.X, Y, 0.0);
					const int32 A = B.Vert(P0, Out, WalkerUV(P0), LegCol(P0)), Bv = B.Vert(P1, Out, WalkerUV(P1), LegCol(P1)), Cv = B.Vert(P2, Out, WalkerUV(P2), LegCol(P2));
					TriFacing(B, A, Bv, Cv, Out);
				}
			}
		}
		// The tail: a strip hanging from the rump, flexing from its root.
		{
			auto TailCol = [](double Flex) { return WalkerCol(Vc::SurfaceTail, (float)Flex, Vc::SideMiddle, Vc::JointTail); };
			const FVector R0(-45.0, -1.5, 52.0), R1(-45.0, 1.5, 52.0), T0(-50.0, -1.0, 22.0), T1(-50.0, 1.0, 22.0);
			const FVector Back = -FVector::XAxisVector;
			const int32 A = B.Vert(R0, Back, WalkerUV(R0), TailCol(0.0)), Bv = B.Vert(R1, Back, WalkerUV(R1), TailCol(0.0));
			const int32 C = B.Vert(T1, Back, WalkerUV(T1), TailCol(1.0)), D = B.Vert(T0, Back, WalkerUV(T0), TailCol(1.0));
			TriFacing(B, A, Bv, C, Back);
			TriFacing(B, A, C, D, Back);
		}
	}
}

namespace
{
	namespace Wk = SlonkFauna::Walker;

	/** The Earth grazer's legs as BuildWalker lays them (its girth's share of their spread across). */
	void GrazerLegs(const FFaunaBodyPlan& Plan, Wk::FShape& S)
	{
		const double Girth = FMath::Clamp((double)Plan.Girth, 0.2, 0.5) / 0.35;
		struct FLegAt { double X, Y; float Phase; };
		const FLegAt Legs[4] = { { -33.0, -8.0, 0.f }, { 10.0, -8.0, 0.25f }, { -33.0, 8.0, 0.5f }, { 10.0, 8.0, 0.75f } };
		S.NumLegs = 4;
		for (int32 i = 0; i < 4; ++i)
		{
			const float X = (float)Legs[i].X, Y = (float)(Legs[i].Y * Girth);
			S.Legs[i].Hip = FVector3f(X, Y, 38.f);
			S.Legs[i].Knee = FVector3f(X, Y, 17.f);
			S.Legs[i].Foot = FVector3f(X, Y, 0.f);
			S.Legs[i].Phase = Legs[i].Phase;
		}
	}

	/**
	 * A six-legged grazer's body as its plan lays it, unit-mesh cm, a metre from its tail's tip to its muzzle, its feet on z
	 * 0: its barrel from its rump to its chest (its plan's Length of its length, its Girth's share round), lower than an
	 * Earth grazer's on its legs (Chord of its length high), a hump at its shoulders; its neck and head heavy (its Neck's
	 * heft), from inside its chest forward and a little up, the head carried low to the muzzle; its legs three pairs under
	 * its barrel, the middle pair set wider than the others (so no two feet of a side ever meet), in a tripod gait.
	 */
	struct FHexapodBody
	{
		double Gm = 1.0, X0 = -47.0, X1 = 19.0, Ry = 13.0, Rz = 12.5, Zc = 40.0, LegH = 31.0, Heft = 1.0;
		FVector NeckRoot = FVector::ZeroVector;
		double StationX[3] = {}, StationY[3] = {};
	};
	FHexapodBody HexapodBody(const FFaunaBodyPlan& P)
	{
		FHexapodBody H;
		H.Gm = FMath::Clamp((double)P.Girth, 0.28, 0.45) / 0.35;
		H.X1 = H.X0 + 100.0 * FMath::Clamp((double)P.Length, 0.6, 0.72);
		H.LegH = 100.0 * FMath::Clamp((double)P.Chord, 0.26, 0.36);
		H.Heft = FMath::Clamp((double)P.Neck, 0.5, 1.0);
		H.Rz = 12.5 * H.Gm;
		H.Ry = 14.5 * H.Gm;
		H.Zc = H.LegH + 0.55 * H.Rz;
		H.NeckRoot = FVector(H.X1 - 4.0, 0.0, H.Zc - 0.15 * H.Rz);
		const double Span = H.X1 - H.X0;
		H.StationX[0] = H.X0 + 0.17 * Span;
		H.StationX[1] = H.X0 + 0.5 * Span;
		H.StationX[2] = H.X1 - 0.13 * Span;
		H.StationY[0] = 7.0 * H.Gm;
		H.StationY[1] = 12.0 * H.Gm;
		H.StationY[2] = 7.0 * H.Gm;
		return H;
	}

	/**
	 * An insectoid's body as its plan lays it, unit-mesh cm, a metre from its abdomen's tip to its head's front, its feet on
	 * z 0: its abdomen (its plan's Length of its length, its Girth's share wide) behind, slung low, its belly 7 cm over the
	 * ground; its thorax before it, narrower and a little higher, carrying its six legs; its head (its Neck of its length)
	 * before that; each leg from a hip on the thorax's side up to a knee high over the body and down to a foot well out to
	 * its side (its Chord of its length out from the hip), the fore legs reaching forward, the middle out, the hind back; its
	 * antennae (its Tail of its length) forward and up from its head.
	 */
	struct FInsectBody
	{
		double Ry = 13.0, Rz = 10.0, Za = 18.0, Xa1 = 0.0, Xh0 = 36.0, RyT = 9.0, RzT = 7.5, Zt = 16.5, RyH = 6.0, RzH = 4.8, Zh = 17.5, Splay = 42.0, Antenna = 28.0;
		FVector NeckRoot = FVector::ZeroVector;
		/** Its legs in the shape's order: the hind, middle and fore left, then right. */
		FVector Hip[6], Knee[6], Foot[6];
		/** Each antenna's root and tip, left then right. */
		FVector AntennaRoot[2], AntennaTip[2];
	};
	constexpr double InsectBelly = 7.0;
	/** How high over the ground a six-legged grazer's muzzle's middle comes down to as it grazes, unit-mesh cm (an Earth
	 *  grazer's about 8): low enough to crop the grass, high enough that its muzzle's ring and its horns' tips stay over
	 *  the ground (Slonk.Fauna.AlienWalkers). */
	constexpr double HexapodGrazeClearCm = 4.0;

	/**
	 * How wide an alien walker's grazing circle may be, in its own lengths: a walker going its herd's way faces any way at
	 * all against where it stands on its circle, so its farthest-reaching part (Far, unit-mesh cm from its middle across
	 * the ground, at any swing of its head and any stance of its feet), grown SpeckMax times by the speck floor, and its
	 * circle may line up; together they keep inside the box its instance is culled by, whose nearest side across its middle
	 * is its rump's end (-50) or its widest point to its side (HalfY), padded as UPlanetInstancesComponent pads it (FrontX
	 * its front, which the box's own padding grows by). FrontX and HalfY are what its mesh reaches at the least, so its box
	 * is no smaller than taken here.
	 */
	double CircleShareFor(double Far, double FrontX, double HalfY)
	{
		const double Pad = SlonkFauna::KindDesc(EFaunaKind::Herd).CullPaddingUnits + 20.0;
		const double Side = FMath::Min(50.0 + 0.15 * 0.5 * (FrontX + 50.0), 1.15 * HalfY) + Pad;
		return FMath::Max((Side - SlonkFauna::Walker::SpeckMax * Far - 5.0) / 100.0, 0.0);
	}
	/** The farthest across the ground from a walker's middle a point of its head reaches at any swing about its neck's pivot
	 *  (Pivot, x and z), the point P (x and z) and RingR round it (a ring of the head's, as wide as that), Y out to its side. */
	double HeadReach(const FVector2D& Pivot, const FVector2D& P, double RingR, double Y)
	{
		return FVector2D(Pivot.X + FVector2D::Distance(P, Pivot) + RingR, Y).Size();
	}
	FInsectBody InsectBody(const FFaunaBodyPlan& P)
	{
		FInsectBody B;
		const double A = FMath::Clamp((double)P.Length, 0.4, 0.6), Hd = FMath::Clamp((double)P.Neck, 0.1, 0.22);
		const double Ga = FMath::Clamp((double)P.Girth, 0.2, 0.34);
		B.Splay = 100.0 * FMath::Clamp((double)P.Chord, 0.32, 0.5);
		B.Antenna = 100.0 * FMath::Clamp((double)P.Tail, 0.15, 0.42) * 0.85;
		B.Ry = 50.0 * Ga;
		B.Rz = 0.78 * B.Ry;
		B.Za = InsectBelly + B.Rz;
		B.Xa1 = -50.0 + 100.0 * A;
		B.Xh0 = 50.0 - 100.0 * Hd;
		B.RyT = 0.58 * B.Ry + 1.5;
		B.RzT = 0.82 * B.RyT;
		B.Zt = InsectBelly + B.RzT + 2.0;
		B.RyH = FMath::Clamp(0.42 * 100.0 * Hd, 4.5, 0.95 * B.RyT);
		B.RzH = 0.8 * B.RyH;
		B.Zh = B.Zt + 1.0;
		B.NeckRoot = FVector(B.Xh0 + 1.0, 0.0, B.Zt);
		const double Xt0 = B.Xa1 - 1.0, Xt1 = B.Xh0 + 1.0, Lt = Xt1 - Xt0;
		const double Angles[3] = { FMath::DegreesToRadians(138.0), FMath::DegreesToRadians(92.0), FMath::DegreesToRadians(48.0) };
		const double KneeZ = B.Zt + B.RzT + 6.0 + 0.12 * B.Splay;
		for (int32 i = 0; i < 6; ++i)
		{
			const int32 Station = i % 3;
			const double Side = i < 3 ? -1.0 : 1.0;
			B.Hip[i] = FVector(Xt0 + Lt * (0.25 + 0.25 * Station), Side * 0.75 * B.RyT, B.Zt - 0.25 * B.RzT);
			const FVector Out(FMath::Cos(Angles[Station]), Side * FMath::Sin(Angles[Station]), 0.0);
			B.Foot[i] = FVector(B.Hip[i].X, B.Hip[i].Y, 0.0) + Out * B.Splay;
			B.Knee[i] = FVector(B.Hip[i].X, B.Hip[i].Y, 0.0) + Out * (0.4 * B.Splay) + FVector(0.0, 0.0, KneeZ);
		}
		for (int32 s = 0; s < 2; ++s)
		{
			const double Side = s == 0 ? -1.0 : 1.0;
			B.AntennaRoot[s] = FVector(B.Xh0 + 0.75 * 100.0 * Hd, Side * 0.45 * B.RyH, B.Zh + 0.7 * B.RzH);
			B.AntennaTip[s] = B.AntennaRoot[s] + FVector(0.8, Side * 0.33, 0.5).GetSafeNormal() * B.Antenna;
		}
		return B;
	}
}

SlonkFauna::Walker::FShape SlonkFauna::Walker::ShapeOf(const FFaunaBodyPlan& P)
{
	FShape S;
	if (P.Wings != EFaunaWings::None || P.Limbs != EFaunaLimbs::Legs) { return S; }
	if (P.LimbPairs == 2)
	{
		// The Earth's grazer: the material's own rig, its stride and sizes the walker table's.
		S.Form = EForm::Grazer;
		GrazerLegs(P, S);
		return S;
	}
	if (P.LimbPairs != 3) { return S; }
	S.NumLegs = 6;
	if (P.Segments >= 3)
	{
		// An insectoid: its head dipped less than a grazer's to its food, its feet lifted higher (its knees higher still),
		// a carapace; a shorter stride for its length, so no two feet of a side meet; its footprint its legs' spread.
		const FInsectBody B = InsectBody(P);
		S.Form = EForm::Insectoid;
		for (int32 i = 0; i < 6; ++i) { S.Legs[i] = { (FVector3f)B.Hip[i], (FVector3f)B.Knee[i], (FVector3f)B.Foot[i], TripodPhase[i] }; }
		S.Rig.NeckX = (float)B.NeckRoot.X;
		S.Rig.NeckZ = (float)B.NeckRoot.Z;
		S.Rig.LiftCm = 8.f;
		S.Rig.PitchScale = 0.45f;
		S.Rig.Carapace = 1.f;
		S.StrideShare = 0.45;
		S.MaxSizeM = 3.5;
		// Its grazing circle no wider than leaves its farthest-reaching parts inside its box (CircleShareFor): an antenna's
		// tip at any swing of its head, and a planted foot carried up to half a stride off its place.
		double FarX = 50.0, FootY = 0.0, Far = 0.0;
		for (int32 s = 0; s < 2; ++s)
		{
			const FVector Tip = B.AntennaTip[s];
			FarX = FMath::Max(FarX, Tip.X);
			Far = FMath::Max(Far, HeadReach(FVector2D(B.NeckRoot.X, B.NeckRoot.Z), FVector2D(Tip.X, Tip.Z), 0.0, Tip.Y));
		}
		for (int32 i = 0; i < 6; ++i)
		{
			FootY = FMath::Max(FootY, FMath::Abs(B.Foot[i].Y));
			Far = FMath::Max(Far, FVector2D(B.Foot[i].X, B.Foot[i].Y).Size() + 0.5 * 100.0 * S.StrideShare);
		}
		S.CircleShare = CircleShareFor(Far, FarX, FootY);
	}
	else
	{
		// A six-legged grazer: its neck's root its own, its feet lifted a little higher than a grazer's (its legs bear more);
		// a longer stride than an insectoid's for its length (its middle feet set wider, so they pass the others); larger.
		const FHexapodBody H = HexapodBody(P);
		S.Form = EForm::Hexapod;
		for (int32 i = 0; i < 6; ++i)
		{
			const int32 Station = i % 3;
			const float Side = i < 3 ? -1.f : 1.f;
			const float X = (float)H.StationX[Station], Y = Side * (float)H.StationY[Station];
			S.Legs[i] = { FVector3f(X, Y, (float)H.LegH), FVector3f(X, Y, (float)(0.45 * H.LegH)), FVector3f(X, Y, 0.f), TripodPhase[i] };
		}
		S.Rig.NeckX = (float)H.NeckRoot.X;
		S.Rig.NeckZ = (float)H.NeckRoot.Z;
		S.Rig.LiftCm = 7.f;
		// Its head dipped to its food no lower than brings its muzzle a little over the grass, its horns' tips with it: its
		// swing the share of the Earth grazer's that turns its muzzle's middle (49 cm along, its head's last ring) about the
		// neck's pivot down to HexapodGrazeClearCm over the ground at the grazing pitch (all of it on a long-legged, short
		// plan whose muzzle clears the ground anyway).
		const double Rx = 49.0 - H.NeckRoot.X, Rz = (H.Zc - 0.45 * H.Rz) - H.NeckRoot.Z;
		const double Dip = FMath::Asin(FMath::Clamp((HexapodGrazeClearCm - H.NeckRoot.Z) / FMath::Max(FMath::Sqrt(Rx * Rx + Rz * Rz), 1e-3), -1.0, 1.0)) - FMath::Atan2(Rz, Rx);
		S.Rig.PitchScale = (float)FMath::Clamp(Dip / (double)GrazePitch, 0.0, 1.0);
		S.StrideShare = 0.5;
		S.MaxSizeM = 3.4;
		// Its grazing circle no wider than leaves its farthest-reaching parts inside its box (CircleShareFor), its narrow
		// barrel's side the box's nearest: its head's rings and its horns' tips at any swing of its head (as BuildHexapod lays
		// them), a planted foot carried up to half a stride off its place, its tail's tip swished out, its barrel's ends.
		const double Ln = 49.0 - H.NeckRoot.X, K = 0.85 + 0.5 * H.Heft, Wide = FMath::Max(FMath::Sqrt(H.Gm), 1.0);
		const FVector2D Pivot(H.NeckRoot.X, H.NeckRoot.Z);
		const FVector2D Rings[5] = { Pivot, FVector2D(H.NeckRoot.X + 0.3 * Ln, H.Zc + 0.25 * H.Rz), FVector2D(H.NeckRoot.X + 0.6 * Ln, H.Zc + 0.12 * H.Rz),
			FVector2D(H.NeckRoot.X + 0.85 * Ln, H.Zc - 0.2 * H.Rz), FVector2D(49.0, H.Zc - 0.45 * H.Rz) };
		const double RingR[5] = { 9.5, 9.0, 8.0, 6.5, 4.5 };
		double Far = FVector2D(50.0, 10.0 + 0.5 * H.Ry).Size();
		for (int32 r = 0; r < 5; ++r) { Far = FMath::Max(Far, HeadReach(Pivot, Rings[r], RingR[r] * K, RingR[r] * K * Wide)); }
		double HalfY = FMath::Max(0.85 * H.Ry, H.StationY[1]);
		if ((P.Ornament & 1) != 0)
		{
			// The horns' tips (from the poll, its fourth ring, 18 out to each side).
			const FVector2D Tip(Rings[3].X + 12.0, Rings[3].Y + 0.8 * RingR[3] * K + 9.0);
			Far = FMath::Max(Far, HeadReach(Pivot, Tip, 0.0, 18.0));
			HalfY = FMath::Max(HalfY, 18.0);
		}
		for (int32 i = 0; i < 6; ++i) { Far = FMath::Max(Far, FVector2D(S.Legs[i].Foot.X, S.Legs[i].Foot.Y).Size() + 4.2 + 0.5 * 100.0 * S.StrideShare); }
		S.CircleShare = CircleShareFor(Far, 49.0, HalfY);
	}
	// How far its footprint reaches from its middle over half its length: its feet and its body's ends (an insectoid's
	// antennae are thin, and left out).
	double Far = 50.0;
	for (int32 i = 0; i < S.NumLegs; ++i) { Far = FMath::Max(Far, (double)FVector2f(S.Legs[i].Foot.X, S.Legs[i].Foot.Y).Size() + 3.0); }
	S.Reach = Far / 50.0;
	return S;
}

namespace
{
	/** The axes of a ring of a bent tube (a leg) square to the way along it there (Along): the first level, across the leg. */
	void TubeAxes(const FVector& Along, FVector& OutAx1, FVector& OutAx2)
	{
		const FVector A = Along.GetSafeNormal();
		OutAx1 = (FMath::Abs(A.Z) < 0.95 ? FVector::CrossProduct(A, FVector::ZAxisVector) : FVector::CrossProduct(A, FVector::XAxisVector)).GetSafeNormal();
		OutAx2 = FVector::CrossProduct(OutAx1, A).GetSafeNormal();
	}

	/**
	 * A six-legged grazer of the plan, at a LOD: 174 to 179, 32 or 5 triangles. LOD 0: a six-sided barrel of five rings
	 * capped at the rump and the chest, a hump over its shoulders; its neck and head a four-sided tube of five rings from
	 * inside its chest to its muzzle, heavy and carried low; its horns (two big ones swept forward) and its crest (three
	 * plates down its back) as its plan wears them; six three-sided legs from inside its barrel to the hoof; a tail. LOD 1: a
	 * four-sided barrel, a three-sided neck and head, a triangle a leg, the tail; LOD 2: each flank standing along it where
	 * its barrel's is (the barrel's side whole and its head carried low, two triangles each) and a triangle lying over its
	 * back. Its colours as an Earth grazer's, each leg's blue its place in the tripod gait.
	 */
	void BuildHexapod(FBuild& B, const FFaunaBodyPlan& Plan, int32 Lod)
	{
		const FHexapodBody H = HexapodBody(Plan);
		const Wk::FShape S = Wk::ShapeOf(Plan);
		const float Joint = Vc::JointRigid;
		auto BodyCol = [&](const FVector& P) { return WalkerCol(Vc::SurfaceBody, 0.f, SideOf(P.Y), Joint); };
		const FVector YAx = FVector::YAxisVector, ZAx = FVector::ZAxisVector;
		const double Top = H.Zc + H.Rz;
		if (Lod >= 2)
		{
			// A speck: each flank standing along it where its barrel's is (low and long, the barrel's side whole, so its glow
			// organs' row is drawn over it as on its finer LODs, seen beside its back from the air; its head carried forward),
			// its back over it.
			for (const double Sd : { -1.0, 1.0 })
			{
				const double Y = Sd * 0.75 * H.Ry;
				const FVector Out(0.0, Sd, 0.0);
				const FVector S0(-48.0, Y, 0.62 * H.LegH), S1(50.0, 0.4 * Y, H.NeckRoot.Z - 0.3 * H.Rz), S2(H.X1 - 8.0, Y, Top + 3.0), S3(-48.0, Y, Top - 1.0);
				const int32 A = B.Vert(S0, Out, WalkerUV(S0), BodyCol(S0)), Bv = B.Vert(S1, Out, WalkerUV(S1), BodyCol(S1)), Cv = B.Vert(S2, Out, WalkerUV(S2), BodyCol(S2));
				const int32 Dv = B.Vert(S3, Out, WalkerUV(S3), BodyCol(S3));
				TriFacing(B, A, Bv, Cv, Out);
				TriFacing(B, A, Cv, Dv, Out);
			}
			const FVector T0(-47.0, -0.9 * H.Ry, Top - 2.0), T1(-47.0, 0.9 * H.Ry, Top - 2.0), T2(44.0, 0.0, H.Zc);
			const int32 D = B.Vert(T0, ZAx, WalkerUV(T0), BodyCol(T0)), E = B.Vert(T1, ZAx, WalkerUV(T1), BodyCol(T1)), F = B.Vert(T2, ZAx, WalkerUV(T2), BodyCol(T2));
			TriFacing(B, D, E, F, ZAx);
			return;
		}
		const bool bFine = Lod == 0;
		// The barrel, a hump over its shoulders.
		{
			const int32 Sides = bFine ? 6 : 4;
			const TArray<double> F = bFine ? TArray<double>{ 0.0, 0.2, 0.5, 0.8, 1.0 } : TArray<double>{ 0.03, 0.97 };
			const TArray<double> Kz = bFine ? TArray<double>{ 0.8, 0.96, 1.0, 1.05, 0.9 } : TArray<double>{ 0.95, 0.95 };
			const TArray<double> Ky = bFine ? TArray<double>{ 0.78, 0.96, 1.0, 0.98, 0.85 } : TArray<double>{ 0.92, 0.92 };
			const TArray<double> Dz = bFine ? TArray<double>{ 1.0, 1.0, 0.0, 2.5, 1.5 } : TArray<double>{ 0.5, 0.5 };
			const double StartA = bFine ? UE_DOUBLE_HALF_PI : UE_DOUBLE_HALF_PI * 0.5;
			TArray<TArray<int32>> Rings;
			TArray<FVector> Centres;
			for (int32 r = 0; r < F.Num(); ++r)
			{
				const FVector C(FMath::Lerp(H.X0, H.X1, F[r]), 0.0, H.Zc + Dz[r]);
				Centres.Add(C);
				Rings.Add(WalkerRing(B, C, YAx, ZAx, H.Ry * Ky[r], H.Rz * Kz[r], Sides, StartA, BodyCol));
			}
			for (int32 r = 0; r + 1 < Rings.Num(); ++r) { WalkerBand(B, Rings[r], Rings[r + 1], Centres[r], Centres[r + 1]); }
			const FVector4f Mid = WalkerCol(Vc::SurfaceBody, 0.f, Vc::SideMiddle, Joint);
			WalkerCap(B, Rings[0], Centres[0] - FVector(3.0, 0.0, 0.0), -FVector::XAxisVector, Mid);
			WalkerCap(B, Rings.Last(), Centres.Last() + FVector(4.0, 0.0, -1.0), FVector::XAxisVector, Mid);
		}
		// The neck and head: heavy, from inside the chest forward and a little up, then the head carried low to the muzzle.
		FVector Poll = FVector::ZeroVector;
		double PollR = 0.0;
		{
			const FVector R0 = H.NeckRoot;
			const double Ln = 49.0 - R0.X;
			const double K = 0.85 + 0.5 * H.Heft;
			const TArray<FVector> C = bFine
				? TArray<FVector>{ R0, FVector(R0.X + 0.3 * Ln, 0.0, H.Zc + 0.25 * H.Rz), FVector(R0.X + 0.6 * Ln, 0.0, H.Zc + 0.12 * H.Rz), FVector(R0.X + 0.85 * Ln, 0.0, H.Zc - 0.2 * H.Rz),
					FVector(49.0, 0.0, H.Zc - 0.45 * H.Rz) }
				: TArray<FVector>{ R0, FVector(R0.X + 0.6 * Ln, 0.0, H.Zc + 0.12 * H.Rz), FVector(49.0, 0.0, H.Zc - 0.45 * H.Rz) };
			const TArray<double> R = bFine ? TArray<double>{ 9.5, 9.0, 8.0, 6.5, 4.5 } : TArray<double>{ 8.5, 7.0, 4.0 };
			const int32 Sides = bFine ? 4 : 3;
			TArray<TArray<int32>> Rings;
			for (int32 r = 0; r < C.Num(); ++r)
			{
				const FVector Along = (C[FMath::Min(r + 1, C.Num() - 1)] - C[FMath::Max(r - 1, 0)]).GetSafeNormal();
				const FVector Up = FVector::CrossProduct(Along, YAx).GetSafeNormal() * -1.0;
				const float Flex = (float)r / (C.Num() - 1);
				const float Surface = r == C.Num() - 1 ? Vc::SurfaceBeak : Vc::SurfaceBody;
				Rings.Add(WalkerRing(B, C[r], YAx, Up.Z < 0.0 ? -Up : Up, R[r] * K * FMath::Sqrt(H.Gm), R[r] * K, Sides, bFine ? UE_DOUBLE_PI * 0.25 : UE_DOUBLE_HALF_PI,
					[&](const FVector& P) { return WalkerCol(Surface, Flex, SideOf(P.Y), Vc::JointNeck); }));
			}
			for (int32 r = 0; r + 1 < Rings.Num(); ++r) { WalkerBand(B, Rings[r], Rings[r + 1], C[r], C[r + 1]); }
			if (bFine) { WalkerCap(B, Rings.Last(), C.Last(), (C.Last() - C[C.Num() - 2]).GetSafeNormal(), WalkerCol(Vc::SurfaceBeak, 1.f, Vc::SideMiddle, Vc::JointNeck)); }
			Poll = C[FMath::Min(3, C.Num() - 1)];
			PollR = R[FMath::Min(3, C.Num() - 1)] * K;
		}
		if (bFine && (Plan.Ornament & 1) != 0)
		{
			// Two big horns from the poll, swept forward and out.
			for (const double Sd : { -1.0, 1.0 })
			{
				const FVector H0(Poll.X - 2.0, Sd * 3.5, Poll.Z + 0.8 * PollR), H1(Poll.X + 2.5, Sd * 3.0, Poll.Z + 0.8 * PollR), H2(Poll.X + 12.0, Sd * 18.0, Poll.Z + 0.8 * PollR + 9.0);
				const FVector4f Hc = WalkerCol(Vc::SurfaceBeak, 1.f, Sd > 0.0 ? Vc::SideRight : Vc::SideLeft, Vc::JointNeck);
				const int32 A = B.Vert(H0, ZAx, WalkerUV(H0), Hc), Bv = B.Vert(H1, ZAx, WalkerUV(H1), Hc), Cv = B.Vert(H2, ZAx, WalkerUV(H2), Hc);
				TriFacing(B, A, Bv, Cv, ZAx);
			}
		}
		if (bFine && (Plan.Ornament & 2) != 0)
		{
			// Three dark plates standing up along its back from its shoulders.
			for (int32 k = 0; k < 3; ++k)
			{
				const double Xa = FMath::Lerp(H.X1 - 4.0, H.X0 + 20.0, k / 3.0), Xb = Xa - 9.0, Z = H.Zc + H.Rz * 1.02 + (k == 0 ? 2.0 : 1.0);
				const FVector P0(Xa, 0.0, Z - 1.5), P1(Xb, 0.0, Z - 1.5), P2(Xa - 6.0, 0.0, Z + 7.0 - 1.5 * k);
				const FVector4f Cc = WalkerCol(Vc::SurfaceBeak, 0.f, Vc::SideMiddle, Joint);
				const int32 A = B.Vert(P0, YAx, WalkerUV(P0), Cc), Bv = B.Vert(P1, YAx, WalkerUV(P1), Cc), Cv = B.Vert(P2, YAx, WalkerUV(P2), Cc);
				TriFacing(B, A, Bv, Cv, YAx);
			}
		}
		// The legs: from inside the barrel to the hoof, each its place in the tripod gait.
		for (int32 i = 0; i < S.NumLegs; ++i)
		{
			const Wk::FLeg& L = S.Legs[i];
			const double Hip = L.Hip.Z;
			auto LegCol = [&](const FVector& P)
			{
				const float Flex = (float)FMath::Clamp((Hip - P.Z) / Hip, 0.0, 1.0);
				return WalkerCol(Flex > 0.99f ? Vc::SurfaceBeak : Vc::SurfaceBody, Flex, L.Phase, Vc::JointLeg);
			};
			const double X = L.Foot.X, Y = L.Foot.Y;
			if (bFine)
			{
				const TArray<double> Z = { Hip, (double)L.Knee.Z, 0.0 };
				const TArray<double> R = { 4.2, 3.0, 2.7 };
				TArray<TArray<int32>> Rings;
				for (int32 r = 0; r < 3; ++r) { Rings.Add(WalkerRing(B, FVector(X, Y, Z[r]), FVector::XAxisVector, YAx, R[r], R[r], 3, 0.0, LegCol)); }
				for (int32 r = 0; r < 2; ++r) { WalkerBand(B, Rings[r], Rings[r + 1], FVector(X, Y, Z[r]), FVector(X, Y, Z[r + 1])); }
				WalkerCap(B, Rings.Last(), FVector(X, Y, 0.0), -ZAx, LegCol(FVector(X, Y, 0.0)));
			}
			else
			{
				const FVector Out(0.0, Y > 0.0 ? 1.0 : -1.0, 0.0);
				const FVector P0(X - 4.5, Y, Hip), P1(X + 4.5, Y, Hip), P2(X, Y, 0.0);
				const int32 A = B.Vert(P0, Out, WalkerUV(P0), LegCol(P0)), Bv = B.Vert(P1, Out, WalkerUV(P1), LegCol(P1)), Cv = B.Vert(P2, Out, WalkerUV(P2), LegCol(P2));
				TriFacing(B, A, Bv, Cv, Out);
			}
		}
		// The tail: a short, thick strip from the rump, back and down, flexing from its root.
		{
			auto TailCol = [](double Flex) { return WalkerCol(Vc::SurfaceTail, (float)Flex, Vc::SideMiddle, Vc::JointTail); };
			const double Len = 100.0 * FMath::Clamp((double)Plan.Tail, 0.06, 0.22);
			const double Zr = H.Zc + 0.45 * H.Rz;
			const FVector R0(H.X0 + 1.0, -2.2, Zr), R1(H.X0 + 1.0, 2.2, Zr), T0(-50.0, -1.4, Zr - Len), T1(-50.0, 1.4, Zr - Len);
			const FVector Back = -FVector::XAxisVector;
			const int32 A = B.Vert(R0, Back, WalkerUV(R0), TailCol(0.0)), Bv = B.Vert(R1, Back, WalkerUV(R1), TailCol(0.0));
			const int32 C = B.Vert(T1, Back, WalkerUV(T1), TailCol(1.0)), D = B.Vert(T0, Back, WalkerUV(T0), TailCol(1.0));
			TriFacing(B, A, Bv, C, Back);
			TriFacing(B, A, C, D, Back);
		}
	}

	/**
	 * An insectoid of the plan, at a LOD: 168 to 171, 38 or 3 triangles. LOD 0: a six-sided abdomen of four rings, coned to
	 * its tip and to its waist; a four-sided thorax of three rings, capped; a four-sided head of three rings, capped; six legs,
	 * each a three-sided tube bent at its knee (rings at its hip, its knee and its foot); two antennae; mandibles and a horn as
	 * its plan wears them. LOD 1: a four-sided abdomen, a three-sided thorax and head, two triangles a leg (to its knee and
	 * from it), the antennae; LOD 2: a level diamond as wide as its legs' spread under a triangle standing along it. Its legs'
	 * green 0 at the hip, InsectKneeFlex at the knee and 1 at the foot (dark), their blue their places in the tripod gait; its
	 * head's green its neck's (0.3 at its back, 1 on), its antennae, mandibles and horn dark with it; its carapace rigid, its
	 * side by its sign.
	 */
	void BuildInsectoid(FBuild& B, const FFaunaBodyPlan& Plan, int32 Lod)
	{
		const FInsectBody I = InsectBody(Plan);
		const Wk::FShape S = Wk::ShapeOf(Plan);
		const float Joint = Vc::JointRigid;
		auto BodyCol = [&](const FVector& P) { return WalkerCol(Vc::SurfaceBody, 0.f, SideOf(P.Y), Joint); };
		const FVector YAx = FVector::YAxisVector, ZAx = FVector::ZAxisVector;
		const double Xt0 = I.Xa1 - 1.0, Xt1 = I.Xh0 + 1.0;
		if (Lod >= 2)
		{
			// A speck: a level diamond as wide as its feet's spread and as long as its body, and its side standing along it.
			double Wide = 0.0;
			for (int32 i = 0; i < 6; ++i) { Wide = FMath::Max(Wide, FMath::Abs(I.Foot[i].Y)); }
			const FVector Q0(-50.0, 0.0, 0.6 * I.Za), Q1((Xt0 + Xt1) * 0.5, -0.85 * Wide, 0.5 * I.Zt), Q2(50.0, 0.0, I.Zh), Q3((Xt0 + Xt1) * 0.5, 0.85 * Wide, 0.5 * I.Zt);
			const int32 A = B.Vert(Q0, ZAx, WalkerUV(Q0), BodyCol(Q0)), Bv = B.Vert(Q1, ZAx, WalkerUV(Q1), BodyCol(Q1)), C = B.Vert(Q2, ZAx, WalkerUV(Q2), BodyCol(Q2)), D = B.Vert(Q3, ZAx, WalkerUV(Q3), BodyCol(Q3));
			TriFacing(B, A, Bv, C, ZAx);
			TriFacing(B, A, C, D, ZAx);
			// Its side: from its abdomen's tip, its flank whole there (so its glow organs' row is drawn over it as on its finer
			// LODs), to its head.
			const FVector S0(-50.0, 0.0, InsectBelly), S1(50.0, 0.0, I.Zh - 2.0), S2(I.Xa1 - 8.0, 0.0, I.Za + I.Rz), S3(-50.0, 0.0, I.Za + 0.6 * I.Rz);
			const int32 E = B.Vert(S0, YAx, WalkerUV(S0), BodyCol(S0)), F = B.Vert(S1, YAx, WalkerUV(S1), BodyCol(S1)), G = B.Vert(S2, YAx, WalkerUV(S2), BodyCol(S2));
			const int32 H = B.Vert(S3, YAx, WalkerUV(S3), BodyCol(S3));
			TriFacing(B, E, F, G, YAx);
			TriFacing(B, E, G, H, YAx);
			return;
		}
		const bool bFine = Lod == 0;
		// The abdomen: big, slung low behind, its tip raised a little, coned to its tip and to its waist.
		{
			const int32 Sides = bFine ? 6 : 4;
			const TArray<double> F = bFine ? TArray<double>{ 0.06, 0.3, 0.62, 0.92 } : TArray<double>{ 0.15, 0.8 };
			const TArray<double> K = bFine ? TArray<double>{ 0.42, 0.92, 1.0, 0.72 } : TArray<double>{ 0.7, 0.9 };
			const double StartA = bFine ? UE_DOUBLE_HALF_PI : UE_DOUBLE_HALF_PI * 0.5;
			TArray<TArray<int32>> Rings;
			TArray<FVector> Centres;
			for (int32 r = 0; r < F.Num(); ++r)
			{
				const FVector C(FMath::Lerp(-50.0, I.Xa1, F[r]), 0.0, I.Za + 2.0 * (1.0 - F[r]));
				Centres.Add(C);
				Rings.Add(WalkerRing(B, C, YAx, ZAx, I.Ry * K[r], I.Rz * K[r], Sides, StartA, BodyCol));
			}
			for (int32 r = 0; r + 1 < Rings.Num(); ++r) { WalkerBand(B, Rings[r], Rings[r + 1], Centres[r], Centres[r + 1]); }
			const FVector4f Mid = WalkerCol(Vc::SurfaceBody, 0.f, Vc::SideMiddle, Joint);
			if (bFine)
			{
				WalkerCap(B, Rings[0], FVector(-50.0, 0.0, Centres[0].Z), -FVector::XAxisVector, Mid);
				WalkerCap(B, Rings.Last(), FVector(I.Xa1 + 1.0, 0.0, Centres.Last().Z), FVector::XAxisVector, Mid);
			}
			else
			{
				WalkerCap(B, Rings[0], Centres[0], -FVector::XAxisVector, Mid);
				WalkerCap(B, Rings.Last(), Centres.Last(), FVector::XAxisVector, Mid);
			}
		}
		if (bFine)
		{
			// The thorax: narrower and a little higher, carrying the legs.
			{
				const TArray<double> F = { 0.08, 0.5, 0.92 };
				const TArray<double> K = { 0.85, 1.0, 0.8 };
				TArray<TArray<int32>> Rings;
				TArray<FVector> Centres;
				for (int32 r = 0; r < 3; ++r)
				{
					const FVector C(FMath::Lerp(Xt0, Xt1, F[r]), 0.0, I.Zt);
					Centres.Add(C);
					Rings.Add(WalkerRing(B, C, YAx, ZAx, I.RyT * K[r], I.RzT * K[r], 4, UE_DOUBLE_PI * 0.25, BodyCol));
				}
				for (int32 r = 0; r < 2; ++r) { WalkerBand(B, Rings[r], Rings[r + 1], Centres[r], Centres[r + 1]); }
				const FVector4f Mid = WalkerCol(Vc::SurfaceBody, 0.f, Vc::SideMiddle, Joint);
				WalkerCap(B, Rings[0], Centres[0], -FVector::XAxisVector, Mid);
				WalkerCap(B, Rings.Last(), Centres.Last(), FVector::XAxisVector, Mid);
			}
			// The head: on its neck's joint, dipping to its food as a grazer's head does (its green 0.3 at its back, so it
			// turns whole about the joint).
			{
				const TArray<double> X = { I.Xh0 + 1.0, (I.Xh0 + 50.0) * 0.5, 49.0 };
				const TArray<double> K = { 0.8, 1.0, 0.7 };
				TArray<TArray<int32>> Rings;
				TArray<FVector> Centres;
				for (int32 r = 0; r < 3; ++r)
				{
					const FVector C(X[r], 0.0, I.Zh - (r == 2 ? 1.0 : 0.0));
					Centres.Add(C);
					const float Flex = r == 0 ? 0.3f : 1.f;
					Rings.Add(WalkerRing(B, C, YAx, ZAx, I.RyH * K[r], I.RzH * K[r], 4, UE_DOUBLE_PI * 0.25, [&](const FVector& P) { return WalkerCol(Vc::SurfaceBody, Flex, SideOf(P.Y), Vc::JointNeck); }));
				}
				for (int32 r = 0; r < 2; ++r) { WalkerBand(B, Rings[r], Rings[r + 1], Centres[r], Centres[r + 1]); }
				WalkerCap(B, Rings[0], Centres[0], -FVector::XAxisVector, WalkerCol(Vc::SurfaceBody, 0.3f, Vc::SideMiddle, Vc::JointNeck));
				WalkerCap(B, Rings.Last(), Centres.Last(), FVector::XAxisVector, WalkerCol(Vc::SurfaceBeak, 1.f, Vc::SideMiddle, Vc::JointNeck));
			}
		}
		else
		{
			// The thorax and head as one tube: the thorax held to the body, the head turning on its joint.
			const TArray<FVector> C = { FVector(Xt0 + 1.0, 0.0, I.Zt), FVector(I.Xh0 + 1.0, 0.0, I.Zt), FVector(49.0, 0.0, I.Zh - 1.0) };
			const TArray<double> Ry = { 0.9 * I.RyT, I.RyH, 0.7 * I.RyH }, Rz = { 0.9 * I.RzT, I.RzH, 0.7 * I.RzH };
			const TArray<float> Flex = { 0.f, 0.f, 1.f };
			const TArray<float> Jt = { Vc::JointRigid, Vc::JointNeck, Vc::JointNeck };
			TArray<TArray<int32>> Rings;
			for (int32 r = 0; r < 3; ++r)
			{
				const float Fl = Flex[r], J = Jt[r];
				Rings.Add(WalkerRing(B, C[r], YAx, ZAx, Ry[r], Rz[r], 3, UE_DOUBLE_HALF_PI, [&](const FVector& P) { return WalkerCol(Vc::SurfaceBody, Fl, SideOf(P.Y), J); }));
			}
			for (int32 r = 0; r < 2; ++r) { WalkerBand(B, Rings[r], Rings[r + 1], C[r], C[r + 1]); }
		}
		// The legs: up from the hip on the thorax's side to the knee high over the body, and down to the foot well out.
		for (int32 i = 0; i < 6; ++i)
		{
			const float Phase = S.Legs[i].Phase;
			const FVector Hip = I.Hip[i], Knee = I.Knee[i], Foot = I.Foot[i];
			auto ColAt = [&](float Flex) { return WalkerCol(Flex > 0.99f ? Vc::SurfaceBeak : Vc::SurfaceBody, Flex, Phase, Vc::JointLeg); };
			if (bFine)
			{
				// Its hip's ring square to its thigh, its knee's to the bend, its foot's level on the ground (none of it under the
				// ground), each turned on from the one before it so the tube does not twist; its cap facing down.
				const TArray<FVector> P = { Hip, Knee, Foot };
				const TArray<double> R = { 2.4, 1.7, 1.2 };
				const TArray<float> Flex = { 0.f, Wk::InsectKneeFlex, 1.f };
				FVector Ax1[3], Ax2[3];
				TubeAxes(Knee - Hip, Ax1[0], Ax2[0]);
				TubeAxes((Knee - Hip).GetSafeNormal() + (Foot - Knee).GetSafeNormal(), Ax1[1], Ax2[1]);
				{
					// The foot's ring level: of the level axes square to each other, the turn whose corners lie nearest the knee's.
					const FVector Flat1 = (Ax1[1] - FVector::ZAxisVector * Ax1[1].Z).GetSafeNormal();
					const FVector Flat2 = FVector::CrossProduct(FVector::ZAxisVector, Flat1);
					double Best = TNumericLimits<double>::Max();
					for (const double Sg : { 1.0, -1.0 })
					{
						double Sum = 0.0;
						for (int32 k = 0; k < 3; ++k)
						{
							const double A = UE_DOUBLE_TWO_PI * k / 3.0;
							Sum += FVector::Dist(Knee + (Ax1[1] * FMath::Cos(A) + Ax2[1] * FMath::Sin(A)) * R[1], Foot + (Flat1 * FMath::Cos(A) + Flat2 * Sg * FMath::Sin(A)) * R[2]);
						}
						if (Sum < Best) { Best = Sum; Ax1[2] = Flat1; Ax2[2] = Flat2 * Sg; }
					}
				}
				TArray<TArray<int32>> Rings;
				for (int32 r = 0; r < 3; ++r)
				{
					const float Fl = Flex[r];
					Rings.Add(WalkerRing(B, P[r], Ax1[r], Ax2[r], R[r], R[r], 3, 0.0, [&](const FVector&) { return ColAt(Fl); }));
				}
				for (int32 r = 0; r < 2; ++r) { WalkerBand(B, Rings[r], Rings[r + 1], P[r], P[r + 1]); }
				WalkerCap(B, Rings.Last(), Foot, -FVector::ZAxisVector, ColAt(1.f));
			}
			else
			{
				// To its knee and from it, each a triangle facing out from the body.
				const FVector Out = FVector(0.0, Foot.Y > 0.0 ? 1.0 : -1.0, 0.4).GetSafeNormal();
				const FVector A0 = Hip + FVector(-2.5, 0.0, 0.0), A1 = Hip + FVector(2.5, 0.0, 0.0);
				const int32 A = B.Vert(A0, Out, WalkerUV(A0), ColAt(0.f)), Bv = B.Vert(A1, Out, WalkerUV(A1), ColAt(0.f)), Cv = B.Vert(Knee, Out, WalkerUV(Knee), ColAt(Wk::InsectKneeFlex));
				TriFacing(B, A, Bv, Cv, Out);
				const FVector K0 = Knee + FVector(-1.8, 0.0, 0.0), K1 = Knee + FVector(1.8, 0.0, 0.0);
				const int32 D = B.Vert(K0, Out, WalkerUV(K0), ColAt(Wk::InsectKneeFlex)), E = B.Vert(K1, Out, WalkerUV(K1), ColAt(Wk::InsectKneeFlex)), F = B.Vert(Foot, Out, WalkerUV(Foot), ColAt(1.f));
				TriFacing(B, D, E, F, Out);
			}
		}
		// The antennae: thin and dark, forward and up from the head, turning with it.
		for (int32 s = 0; s < 2; ++s)
		{
			const FVector Root = I.AntennaRoot[s], Tip = I.AntennaTip[s];
			const FVector Across = FVector::CrossProduct(Tip - Root, FVector::YAxisVector).GetSafeNormal();
			const FVector Facing = FVector::CrossProduct(Tip - Root, Across).GetSafeNormal() * (s == 0 ? -1.0 : 1.0);
			const FVector4f Ac = WalkerCol(Vc::SurfaceBeak, 1.f, s == 0 ? Vc::SideLeft : Vc::SideRight, Vc::JointNeck);
			const FVector R0 = Root - Across * 0.7, R1 = Root + Across * 0.7;
			const int32 A = B.Vert(R0, Facing, WalkerUV(R0), Ac), Bv = B.Vert(R1, Facing, WalkerUV(R1), Ac), C = B.Vert(Tip, Facing, WalkerUV(Tip), Ac);
			TriFacing(B, A, Bv, C, Facing);
		}
		if (bFine && (Plan.Ornament & 1) != 0)
		{
			// Mandibles: two dark hooks from the head's front, toward each other.
			for (const double Sd : { -1.0, 1.0 })
			{
				const FVector M0(49.0, Sd * 0.55 * I.RyH, I.Zh - 0.45 * I.RzH), M1(47.0, Sd * 0.8 * I.RyH, I.Zh - 0.2 * I.RzH), M2(55.0, Sd * 0.1 * I.RyH, I.Zh - 0.55 * I.RzH);
				const FVector4f Mc = WalkerCol(Vc::SurfaceBeak, 1.f, Sd > 0.0 ? Vc::SideRight : Vc::SideLeft, Vc::JointNeck);
				const int32 A = B.Vert(M0, ZAx, WalkerUV(M0), Mc), Bv = B.Vert(M1, ZAx, WalkerUV(M1), Mc), C = B.Vert(M2, ZAx, WalkerUV(M2), Mc);
				TriFacing(B, A, Bv, C, ZAx);
			}
		}
		if (bFine && (Plan.Ornament & 2) != 0)
		{
			// A horn standing up and forward from the head.
			const FVector H0(I.Xh0 + 3.0, 0.0, I.Zh + 0.85 * I.RzH), H1(47.0, 0.0, I.Zh + 0.8 * I.RzH), H2(52.0, 0.0, I.Zh + I.RzH + 11.0);
			const FVector4f Hc = WalkerCol(Vc::SurfaceBeak, 1.f, Vc::SideMiddle, Vc::JointNeck);
			const int32 A = B.Vert(H0, YAx, WalkerUV(H0), Hc), Bv = B.Vert(H1, YAx, WalkerUV(H1), Hc), C = B.Vert(H2, YAx, WalkerUV(H2), Hc);
			TriFacing(B, A, Bv, C, YAx);
		}
	}
}

namespace
{
	/** A fish's colour at a point of it: its surface, how far it bends there (0 from the nose back to FishBendFromCm, 1 at
	 *  the tail's tip), its side, its joint (it undulates). */
	FVector4f FishCol(float Surface, const FVector& P)
	{
		const float Bend = (float)FMath::Clamp((Vc::FishBendFromCm - P.X) / (Vc::FishBendFromCm + 50.0), 0.0, 1.0);
		return FVector4f(Surface, Bend, P.Y > 0.5 ? Vc::SideRight : (P.Y < -0.5 ? Vc::SideLeft : Vc::SideMiddle), Vc::JointUndulate);
	}

	/**
	 * A fish of the plan (an Earth fish: streamlined, a forked tail, fins), at a LOD: 55, 19 or 2 triangles, a metre from
	 * its tail's tip to its nose, its middle at the origin, its back up. LOD 0: a six-sided body of four rings from the
	 * tail's root to the head, coned to the nose and to the tail's root; a forked tail of three triangles, a dorsal fin, an
	 * anal fin and two pectoral fins; LOD 1: a four-sided body of two rings, coned, the tail's two lobes and the dorsal fin;
	 * LOD 2: one triangle standing along it and one lying along it. Its UV is its place along it (U from the tail's tip)
	 * and up its side (V from its belly), for its countershading.
	 */
	void BuildFish(FBuild& B, const FFaunaBodyPlan& Plan, int32 Lod)
	{
		const double G = FMath::Clamp((double)Plan.Girth, 0.12, 0.35) / 0.2;
		const double Rz = 10.0 * G, Ry = 6.0 * G;
		const double Root = -30.0, Nose = 50.0, TailTip = -50.0;
		auto FishUV = [&](const FVector& P) { return FVector2f((float)((P.X + 50.0) / 100.0), (float)FMath::Clamp((P.Z + Rz) / (2.0 * Rz), 0.0, 1.0)); };
		auto V = [&](const FVector& P, const FVector& N, float Surface) { return B.Vert(P, N, FishUV(P), FishCol(Surface, P)); };
		const FVector YAx = FVector::YAxisVector, ZAx = FVector::ZAxisVector;
		if (Lod >= 2)
		{
			// A speck: its side standing along it, its back lying along it.
			const FVector S0(Nose, 0.0, 0.0), S1(TailTip, 0.0, 1.2 * Rz), S2(TailTip, 0.0, -1.2 * Rz);
			TriFacing(B, V(S0, YAx, Vc::SurfaceBody), V(S1, YAx, Vc::SurfaceBody), V(S2, YAx, Vc::SurfaceBody), YAx);
			const FVector T0(Nose, 0.0, 0.0), T1(-40.0, -1.2 * Ry, 0.0), T2(-40.0, 1.2 * Ry, 0.0);
			TriFacing(B, V(T0, ZAx, Vc::SurfaceBody), V(T1, ZAx, Vc::SurfaceBody), V(T2, ZAx, Vc::SurfaceBody), ZAx);
			return;
		}
		const bool bFine = Lod == 0;
		// The body: rings of an ellipse a little deeper than it is wide, their normals out from the axis.
		{
			const int32 Sides = bFine ? 6 : 4;
			const TArray<double> X = bFine ? TArray<double>{ Root, -12.0, 10.0, 30.0 } : TArray<double>{ -26.0, 10.0 };
			const TArray<double> K = bFine ? TArray<double>{ 0.25, 0.75, 1.0, 0.75 } : TArray<double>{ 0.45, 1.0 };
			const double StartA = UE_DOUBLE_HALF_PI;
			TArray<TArray<int32>> Rings;
			for (int32 r = 0; r < X.Num(); ++r)
			{
				TArray<int32> Ring;
				for (int32 k = 0; k < Sides; ++k)
				{
					const double A = UE_DOUBLE_TWO_PI * k / Sides + StartA;
					const FVector P(X[r], Ry * K[r] * FMath::Cos(A), Rz * K[r] * FMath::Sin(A));
					Ring.Add(V(P, FVector(0.0, FMath::Cos(A) / Ry, FMath::Sin(A) / Rz), Vc::SurfaceBody));
				}
				Rings.Add(Ring);
			}
			for (int32 r = 0; r + 1 < Rings.Num(); ++r)
			{
				for (int32 k = 0; k < Sides; ++k)
				{
					const int32 A = Rings[r][k], Bv = Rings[r][(k + 1) % Sides], C = Rings[r + 1][(k + 1) % Sides], D = Rings[r + 1][k];
					const FVector Mid = ((FVector)B.Mesh.GetVertex(A) + (FVector)B.Mesh.GetVertex(Bv) + (FVector)B.Mesh.GetVertex(C) + (FVector)B.Mesh.GetVertex(D)) * 0.25;
					const FVector Out(0.0, Mid.Y, Mid.Z);
					TriFacing(B, A, Bv, C, Out);
					TriFacing(B, A, C, D, Out);
				}
			}
			// Coned to the nose (a little below the axis, the mouth) and to the tail's root, each from its own vertices.
			const TArray<int32>& Front = Rings.Last();
			const TArray<int32>& Back = Rings[0];
			const int32 Tip = V(FVector(Nose, 0.0, -0.1 * Rz), FVector::XAxisVector, Vc::SurfaceBody);
			const int32 Stub = V(FVector(Root - 4.0, 0.0, 0.0), -FVector::XAxisVector, Vc::SurfaceBody);
			for (int32 k = 0; k < Sides; ++k)
			{
				const FVector Fm = ((FVector)B.Mesh.GetVertex(Front[k]) + (FVector)B.Mesh.GetVertex(Front[(k + 1) % Sides])) * 0.5;
				TriFacing(B, Front[k], Front[(k + 1) % Sides], Tip, FVector(0.5, Fm.Y, Fm.Z));
				const FVector Bm = ((FVector)B.Mesh.GetVertex(Back[k]) + (FVector)B.Mesh.GetVertex(Back[(k + 1) % Sides])) * 0.5;
				TriFacing(B, Back[k], Back[(k + 1) % Sides], Stub, FVector(-0.5, Bm.Y, Bm.Z));
			}
		}
		// The tail: forked, standing up from its root to its two tips (a notch between at LOD 0).
		{
			const FVector RT(Root - 1.0, 0.0, 0.3 * Rz), RB(Root - 1.0, 0.0, -0.3 * Rz), UT(TailTip, 0.0, 1.15 * Rz), LT(TailTip, 0.0, -1.15 * Rz);
			if (bFine)
			{
				const FVector Notch(-41.0, 0.0, 0.0);
				TriFacing(B, V(RT, YAx, Vc::SurfaceTail), V(UT, YAx, Vc::SurfaceTail), V(Notch, YAx, Vc::SurfaceTail), YAx);
				TriFacing(B, V(RT, YAx, Vc::SurfaceTail), V(Notch, YAx, Vc::SurfaceTail), V(RB, YAx, Vc::SurfaceTail), YAx);
				TriFacing(B, V(RB, YAx, Vc::SurfaceTail), V(Notch, YAx, Vc::SurfaceTail), V(LT, YAx, Vc::SurfaceTail), YAx);
			}
			else
			{
				const FVector Mid(Root - 1.0, 0.0, 0.0);
				TriFacing(B, V(Mid, YAx, Vc::SurfaceTail), V(UT, YAx, Vc::SurfaceTail), V(FVector(-42.0, 0.0, 0.0), YAx, Vc::SurfaceTail), YAx);
				TriFacing(B, V(Mid, YAx, Vc::SurfaceTail), V(FVector(-42.0, 0.0, 0.0), YAx, Vc::SurfaceTail), V(LT, YAx, Vc::SurfaceTail), YAx);
			}
		}
		// The dorsal fin, swept back from the back.
		TriFacing(B, V(FVector(10.0, 0.0, 0.95 * Rz), YAx, Vc::SurfaceTail), V(FVector(-8.0, 0.0, 0.8 * Rz), YAx, Vc::SurfaceTail),
			V(FVector(-6.0, 0.0, 1.55 * Rz), YAx, Vc::SurfaceTail), YAx);
		if (bFine)
		{
			// The anal fin, and the two pectoral fins behind the head, swept back and down.
			TriFacing(B, V(FVector(-12.0, 0.0, -0.7 * Rz), YAx, Vc::SurfaceTail), V(FVector(-24.0, 0.0, -0.35 * Rz), YAx, Vc::SurfaceTail),
				V(FVector(-23.0, 0.0, -1.1 * Rz), YAx, Vc::SurfaceTail), YAx);
			for (const double S : { -1.0, 1.0 })
			{
				const FVector Up = FVector(0.0, S * 0.35, 1.0).GetSafeNormal();
				TriFacing(B, V(FVector(26.0, S * 0.8 * Ry, -0.35 * Rz), Up, Vc::SurfaceTail), V(FVector(16.0, S * 0.9 * Ry, -0.45 * Rz), Up, Vc::SurfaceTail),
					V(FVector(12.0, S * (Ry + 7.0), -0.75 * Rz), Up, Vc::SurfaceTail), Up);
			}
		}
	}
}

namespace
{
	/** A jelly's colour at a point of it: its bell (rigid, how far down it from its crown) or a tentacle (undulating, how
	 *  far along it from its root), and its place round the bell. */
	FVector4f JellyCol(bool bTentacle, float Down, float Round)
	{
		return FVector4f(bTentacle ? Vc::SurfaceTail : Vc::SurfaceBody, Down, Round, bTentacle ? Vc::JointUndulate : Vc::JointRigid);
	}

	/**
	 * A sky-jelly of the plan (SlonkFauna::Floater), at a LOD: about 300, 60 or 16 triangles, its span across, its middle
	 * at the origin, its crown up. LOD 0: its bell lathed in 24 sides (a fan at its crown and three bands down to its rim,
	 * its seam drawn twice, at U 0 and 1), its rim's lobes scalloped between its ribs and drooping a little; its tentacles
	 * ribbons hanging from just inside its rim, tapering to their tips, 72 quads among them (12 each of 6, 9 of 8, 7 of 10,
	 * 6 of 12). LOD 1: its bell in 12 sides of two bands, its rim round; its tentacles 12 quads among them (at least one
	 * each). LOD 2: two shapes crossed on its axis, each its bell's dome as a pentagon over a strip of its ring canal at its
	 * rim, over its tentacles' sheet as a quad (as wide as its ribbons laid side by side, shared between the two), and a
	 * level diamond at its rim; its bell drawn whole. Its normals out of its bell's dome and its tentacles' sheets (the
	 * diamond's up), each face wound to show the side they point to.
	 */
	void BuildJelly(FBuild& B, const FFaunaBodyPlan& Plan, int32 Lod)
	{
		namespace Fl = SlonkFauna::Floater;
		const double Bell = Fl::BellOf(Plan), Lt = Fl::TentaclesOf(Plan);
		const double H = Fl::HalfHeightCm(Plan);
		const double Zr = H - 100.0 * Bell;
		const double A = Fl::RimCm, Bz = 100.0 * Bell;
		const int32 Sym = FMath::Max((int32)Plan.Symmetry, 3);
		const int32 Tents = Fl::TentacleCount(Plan);
		const double Cut = FMath::Clamp((double)Plan.Chord, 0.0, 0.2);
		if (Lod >= 2)
		{
			// A speck, its bell drawn whole: its pores are far under a pixel by now, and two layers of its bell deep (seen from
			// the side) they leave most of it covered. In each of two planes crossed on its axis, its dome as a pentagon (its
			// crown, its shoulders half way down it, and a tenth of its height over its rim) over a strip of its ring canal
			// down to its rim, over its tentacles' sheet; and a level diamond at its rim, so it shows from above and below as
			// its bell does. Its bell's V runs from 0.1 at its crown to 0.27 over its strip (under Floater::CrownV: no pore,
			// its ribs lit), its strip's about Floater::RingV (whole, lit as its ring canal is); its G is how far down it a
			// vertex is, so it pulses as its bell does.
			const double Ps = UE_DOUBLE_HALF_PI * 0.5;
			const double Rs = A * FMath::Pow(FMath::Sin(Ps), 0.85), Zs = Zr + Bz * FMath::Cos(Ps);
			const double Zq = Zr + 0.1 * Bz;
			const float Gq = (float)(FMath::Acos(0.1) / UE_DOUBLE_HALF_PI);
			auto SpeckV = [](float Down) { return 0.1f + 0.17f * Down; };
			// Its tentacles' sheet as wide as its ribbons laid side by side (3.2 cm of the unit mesh at their roots, 0.8 at
			// their tips), shared between the two planes, so it covers and glows about as they do.
			const double Wr = FMath::Min(0.8 * Tents, 0.8 * A), Wt = FMath::Min(0.2 * Tents, 0.55 * A);
			for (int32 Pl = 0; Pl < 2; ++Pl)
			{
				const FVector Ax = Pl == 0 ? FVector::XAxisVector : FVector::YAxisVector;
				const FVector Nrm = Pl == 0 ? FVector::YAxisVector : -FVector::XAxisVector;
				const float U0 = Pl == 0 ? 0.f : 0.25f;
				auto V = [&](double Across, double Z, bool bTentacle, float Down, float VTex)
				{
					const float Round = FMath::Frac(U0 + (Across < 0.0 ? 0.5f : 0.f));
					return B.Vert(Ax * Across + FVector(0.0, 0.0, Z), Nrm, FVector2f(Round, VTex), JellyCol(bTentacle, Down, Round));
				};
				const int32 Crown = V(0.0, H, false, 0.f, SpeckV(0.f));
				const int32 SL = V(-Rs, Zs, false, 0.5f, SpeckV(0.5f)), SR = V(Rs, Zs, false, 0.5f, SpeckV(0.5f));
				const int32 QL = V(-0.92 * A, Zq, false, Gq, SpeckV(1.f)), QR = V(0.92 * A, Zq, false, Gq, SpeckV(1.f));
				TriFacing(B, Crown, SL, SR, Nrm);
				TriFacing(B, SL, QL, QR, Nrm);
				TriFacing(B, SL, QR, SR, Nrm);
				const int32 RingL = V(-0.92 * A, Zq, false, Gq, Fl::RingV + 0.004f), RingR = V(0.92 * A, Zq, false, Gq, Fl::RingV + 0.004f);
				const int32 L = V(-0.92 * A, Zr, false, 1.f, Fl::RingV - 0.004f), R = V(0.92 * A, Zr, false, 1.f, Fl::RingV - 0.004f);
				TriFacing(B, RingL, L, R, Nrm);
				TriFacing(B, RingL, R, RingR, Nrm);
				const int32 TL = V(-Wr, Zr, true, 0.f, 1.f), TR = V(Wr, Zr, true, 0.f, 1.f);
				const int32 BL = V(-Wt, -H, true, 1.f, 1.f + (float)Lt), BR = V(Wt, -H, true, 1.f, 1.f + (float)Lt);
				TriFacing(B, TL, TR, BR, Nrm);
				TriFacing(B, TL, BR, BL, Nrm);
			}
			// The diamond, its corners on the two planes on its rim, pulsing with it (its V a little different at alternate
			// corners, so its UV has an area for its tangents): it covers about as much as its bell's porous dome does seen
			// from above or below, and its ribs light about as much of it.
			{
				auto Lv = [&](double X, double Y, float Round, float VTex)
				{
					return B.Vert(FVector(X, Y, Zr), FVector::ZAxisVector, FVector2f(Round, VTex), JellyCol(false, 1.f, Round));
				};
				const int32 E = Lv(A, 0.0, 0.f, SpeckV(1.f)), N = Lv(0.0, A, 0.25f, SpeckV(0.5f));
				const int32 W = Lv(-A, 0.0, 0.5f, SpeckV(1.f)), S = Lv(0.0, -A, 0.75f, SpeckV(0.5f));
				// Its southern half shares the east corner (U 0), so its U runs back through the round rather than on to 1: at a
				// speck a few pixels across that cannot be seen, and its ribs then light as much of it as the bell's do.
				TriFacing(B, E, N, W, FVector::ZAxisVector);
				TriFacing(B, E, W, S, FVector::ZAxisVector);
			}
			return;
		}
		const bool bFine = Lod == 0;
		// The bell: a dome lathed round its axis, from its crown (v 0) to its rim (v 1); at LOD 0 its rim scalloped between
		// its ribs (its lobes' tips on them, at whole sides), each lobe drooping a little.
		{
			const int32 Sides = bFine ? 24 : 12;
			const TArray<double> Vs = bFine ? TArray<double>{ 0.25, 0.5, 0.75, 1.0 } : TArray<double>{ 0.5, 1.0 };
			auto Ring = [&](double Vv)
			{
				TArray<int32> R;
				const double Phi = Vv * UE_DOUBLE_HALF_PI;
				for (int32 k = 0; k <= Sides; ++k)
				{
					const double Th = UE_DOUBLE_TWO_PI * k / Sides;
					const double Lobe = 0.5 + 0.5 * FMath::Cos(Sym * Th);
					const double Edge = FMath::Pow(Vv, 4.0);
					const double Rr = A * FMath::Pow(FMath::Sin(Phi), 0.85) * (bFine ? 1.0 - Cut * Edge * (1.0 - Lobe) : 1.0);
					const double Z = Zr + Bz * FMath::Cos(Phi) - (bFine ? 3.0 * Edge * Lobe : 0.0);
					const FVector Nrm(Bz * FMath::Sin(Phi) * FMath::Cos(Th), Bz * FMath::Sin(Phi) * FMath::Sin(Th), A * FMath::Cos(Phi));
					const float U = (float)k / Sides;
					R.Add(B.Vert(FVector(Rr * FMath::Cos(Th), Rr * FMath::Sin(Th), Z), Nrm, FVector2f(U, (float)Vv), JellyCol(false, (float)Vv, U)));
				}
				return R;
			};
			TArray<TArray<int32>> Rings;
			for (const double Vv : Vs) { Rings.Add(Ring(Vv)); }
			// The crown: a vertex of its own for each side (its U that side's middle), all at the crown, facing up.
			for (int32 k = 0; k < Sides; ++k)
			{
				const float U = (k + 0.5f) / Sides;
				const int32 C = B.Vert(FVector(0.0, 0.0, H), FVector::ZAxisVector, FVector2f(U, 0.f), JellyCol(false, 0.f, U));
				const FVector Mid = ((FVector)B.Mesh.GetVertex(Rings[0][k]) + (FVector)B.Mesh.GetVertex(Rings[0][k + 1])) * 0.5;
				TriFacing(B, C, Rings[0][k], Rings[0][k + 1], FVector(Mid.X, Mid.Y, 0.0) * (Bz / A) + FVector(0.0, 0.0, A));
			}
			for (int32 r = 0; r + 1 < Rings.Num(); ++r)
			{
				for (int32 k = 0; k < Sides; ++k)
				{
					const int32 V0 = Rings[r][k], V1 = Rings[r][k + 1], V2 = Rings[r + 1][k + 1], V3 = Rings[r + 1][k];
					// Out of the dome, as its normals point there.
					const FVector Out = FVector(B.N->GetElement(V0) + B.N->GetElement(V1) + B.N->GetElement(V2) + B.N->GetElement(V3));
					TriFacing(B, V0, V1, V2, Out);
					TriFacing(B, V0, V2, V3, Out);
				}
			}
		}
		// The tentacles: ribbons hanging from just inside the rim, between the ribs, across the way round the bell, facing
		// out from its axis, tapering from 3.2 cm of the unit mesh wide at their roots to 0.8 at their tips.
		{
			const int32 Quads = bFine ? FMath::Max(FMath::RoundToInt32(72.0 / Tents), 1) : FMath::Max(FMath::RoundToInt32(12.0 / Tents), 1);
			for (int32 t = 0; t < Tents; ++t)
			{
				const float Round = (t + 0.5f) / Tents;
				const double Th = UE_DOUBLE_TWO_PI * Round;
				const FVector Out(FMath::Cos(Th), FMath::Sin(Th), 0.0);
				const FVector Across(-FMath::Sin(Th), FMath::Cos(Th), 0.0);
				TArray<int32> Prev;
				for (int32 s = 0; s <= Quads; ++s)
				{
					const double F = (double)s / Quads;
					const double Along = 100.0 * Lt * F;
					const FVector C = Out * Fl::RootCm + FVector(0.0, 0.0, Zr - Along);
					const double W = FMath::Lerp(1.6, 0.4, F);
					const FVector2f Tex(Round, 1.f + (float)(Along / 100.0));
					TArray<int32> Row = { B.Vert(C - Across * W, Out, Tex, JellyCol(true, (float)F, Round)), B.Vert(C + Across * W, Out, Tex, JellyCol(true, (float)F, Round)) };
					if (Prev.Num() > 0)
					{
						TriFacing(B, Prev[0], Prev[1], Row[1], Out);
						TriFacing(B, Prev[0], Row[1], Row[0], Out);
					}
					Prev = MoveTemp(Row);
				}
			}
		}
	}
}

namespace
{
	namespace Wn = SlonkFauna::Wings;
	namespace Ff = SlonkFauna::FishForm;

	/**
	 * A body of rings round the x axis (an alien flier's or fish's), each of Sides about its middle (y 0, z Zc), Ry across
	 * and Rz up, its normals out from its axis, banded between the rings and coned to a point behind its first (Rear) and
	 * one ahead of its last (Nose), each face wound to show the side its normals point to. Its UV and colour by the caller,
	 * from a vertex's place and how far up round its ring it lies (-1 under it to 1 over it). Its first vertex StartA round
	 * from its side (a four-sided body's an eighth of a turn, so its flanks are upright faces at every LOD, as its speck's
	 * side is: its glow organs' row is seen alike on each).
	 */
	void LatheBody(FBuild& B, const TArray<double>& Xs, const TArray<double>& Ry, const TArray<double>& Rz, const TArray<double>& Zc, int32 Sides, const FVector& Rear, const FVector& Nose,
		TFunctionRef<FVector2f(const FVector&, double)> Uv, TFunctionRef<FVector4f(const FVector&)> Col, double StartA = UE_DOUBLE_HALF_PI)
	{
		TArray<TArray<int32>> Rings;
		for (int32 r = 0; r < Xs.Num(); ++r)
		{
			TArray<int32> Ring;
			for (int32 k = 0; k < Sides; ++k)
			{
				const double A = UE_DOUBLE_TWO_PI * k / Sides + StartA;
				const FVector P(Xs[r], FMath::Cos(A) * Ry[r], Zc[r] + FMath::Sin(A) * Rz[r]);
				const FVector N(0.0, FMath::Cos(A) / FMath::Max(Ry[r], 0.1), FMath::Sin(A) / FMath::Max(Rz[r], 0.1));
				Ring.Add(B.Vert(P, N, Uv(P, FMath::Sin(A)), Col(P)));
			}
			Rings.Add(Ring);
		}
		for (int32 r = 0; r + 1 < Rings.Num(); ++r)
		{
			const FVector Axis((Xs[r] + Xs[r + 1]) * 0.5, 0.0, (Zc[r] + Zc[r + 1]) * 0.5);
			for (int32 k = 0; k < Sides; ++k)
			{
				const int32 A = Rings[r][k], Bv = Rings[r][(k + 1) % Sides], C = Rings[r + 1][(k + 1) % Sides], D = Rings[r + 1][k];
				const FVector Mid = ((FVector)B.Mesh.GetVertex(A) + (FVector)B.Mesh.GetVertex(Bv) + (FVector)B.Mesh.GetVertex(C) + (FVector)B.Mesh.GetVertex(D)) * 0.25;
				TriFacing(B, A, Bv, C, Mid - Axis);
				TriFacing(B, A, C, D, Mid - Axis);
			}
		}
		const int32 Back = B.Vert(Rear, -FVector::XAxisVector, Uv(Rear, 0.0), Col(Rear));
		const int32 Front = B.Vert(Nose, FVector::XAxisVector, Uv(Nose, 0.0), Col(Nose));
		const FVector InBack(Xs[0] + 1.0, 0.0, Zc[0]), InFront(Xs.Last() - 1.0, 0.0, Zc.Last());
		for (int32 k = 0; k < Sides; ++k)
		{
			const TArray<int32>& R0 = Rings[0];
			const TArray<int32>& R1 = Rings.Last();
			const FVector Cb = ((FVector)B.Mesh.GetVertex(R0[k]) + (FVector)B.Mesh.GetVertex(R0[(k + 1) % Sides]) + Rear) / 3.0;
			const FVector Cf = ((FVector)B.Mesh.GetVertex(R1[k]) + (FVector)B.Mesh.GetVertex(R1[(k + 1) % Sides]) + Nose) / 3.0;
			TriFacing(B, R0[k], R0[(k + 1) % Sides], Back, Cb - InBack);
			TriFacing(B, R1[k], R1[(k + 1) % Sides], Front, Cf - InFront);
		}
	}
	/** A flat piece (a fin, a crest, a tail's vane) of its own vertices, facing Facing either way it is seen. */
	int32 FlatVert(FBuild& B, const FVector& P, const FVector& Facing, const FVector2f& Uv, const FVector4f& Col) { return B.Vert(P, Facing, Uv, Col); }

	/** An alien flier's body's UV: its place along it from the unit mesh's x of -50 (U) and round it (V 0 under it, 1 over it,
	 *  0.5 along its flanks). */
	FVector2f FlierBodyUV(const FVector& P, double Up) { return FVector2f((float)((P.X + 50.0) / 100.0), (float)(0.5 + 0.5 * Up)); }
	const FVector4f FlierBodyCol = VertexCol(Vc::SurfaceBody, 0.f, Vc::SideMiddle, Vc::JointRigid);

	/** A four-winged flier's body as its plan lays it, unit-mesh cm: its middle at the mesh's, its head ahead, its thorax,
	 *  its long abdomen behind; the hinges of its fore wings and its hind wings on its thorax, the hind pair's chord wider
	 *  at the root. */
	struct FFourWingBody
	{
		double Lb = 85.0, Ra = 3.0, Rt = 3.6, Rh = 4.0, Ab = 50.0, Th = 20.0, Hd = 8.5;
		double Rear = -60.0, Xt0 = -8.0, Xh0 = 12.0, Nose = 20.0;
		double ForeLead = 11.0, ForeChord = 12.0, HindLead = -1.0, HindChord = 15.0;
	};
	FFourWingBody FourWingBody(const FFaunaBodyPlan& P)
	{
		FFourWingBody F;
		F.Lb = 100.0 * FMath::Clamp((double)P.Length, 0.6, 1.0);
		F.Ra = 50.0 * FMath::Clamp((double)P.Girth, 0.04, 0.08);
		F.Rt = FMath::Min(1.5 * F.Ra, 3.6);
		F.Rh = FMath::Clamp(55.0 * (double)P.Neck, 3.0, 5.5);
		F.Ab = F.Lb * FMath::Clamp((double)P.Tail, 0.5, 0.72);
		F.Hd = 0.1 * F.Lb;
		F.Th = F.Lb - F.Ab - F.Hd;
		F.Rear = -0.5 * F.Lb;
		F.Xt0 = F.Rear + F.Ab;
		F.Xh0 = F.Xt0 + F.Th;
		F.Nose = F.Xh0 + F.Hd;
		F.ForeChord = 100.0 * FMath::Clamp((double)P.Chord, 0.08, 0.16);
		F.HindChord = 1.25 * F.ForeChord;
		F.ForeLead = F.Xh0 - 1.0;
		F.HindLead = F.ForeLead - F.ForeChord - 1.0;
		return F;
	}

	/**
	 * A four-winged flier of the plan, at a LOD: 92 to 96, 40 or 6 triangles. LOD 0: its body a four-sided tube of eight
	 * rings from its abdomen's tip to its head (its head big, a pinch at its neck), coned at both ends, its flanks upright;
	 * four narrow wings each of three panels and a rounded tip, the hind pair (U 2 to 3) a little shorter and wider at the
	 * root; a forked tail and antennae as its plan wears them. LOD 1: a four-sided body of three bands, a quad a wing. LOD
	 * 2: a triangle a wing, and its body's side a quad standing along it (its glow organs' row drawn over it).
	 */
	void BuildFourWing(FBuild& B, const FFaunaBodyPlan& Plan, int32 Lod)
	{
		const FFourWingBody F = FourWingBody(Plan);
		const double Root = Vc::WingRootCm;
		const double WingZ = 0.25 * F.Rt;
		auto Wing = [&](bool bHind, double S, int32 Detail)
		{
			const double Lead0 = bHind ? F.HindLead : F.ForeLead, C0 = bHind ? F.HindChord : F.ForeChord;
			const double Tip = bHind ? 47.0 : SlonkFauna::UnitCm * 0.5;
			const float U0 = bHind ? 2.f : 0.f;
			// The leading edge a little swept, the chord narrowing out to a rounded tip (the hind pair's more, from its wider root).
			auto Lead = [&](double Y) { return Lead0 - 0.04 * (Y - Root); };
			auto Chord = [&](double Y) { const double Fy = (Y - Root) / (Tip - Root); return C0 * (bHind ? 1.0 - 0.45 * Fy : 1.0 - 0.2 * Fy); };
			auto V = [&](double X, double Y, float Across) { return B.Vert(FVector(X, S * Y, WingZ), FVector::ZAxisVector, FVector2f(U0 + Across, (float)((Y - Root) / (Tip - Root))), WingCol(S * Y)); };
			if (Detail >= 2)
			{
				TriFacing(B, V(Lead(Root), Root, 0.f), V(Lead(Root) - C0, Root, 1.f), V(Lead(Tip) - 0.5 * Chord(Tip), Tip, 0.5f), FVector::ZAxisVector);
				return;
			}
			const TArray<double> Ys = Detail == 0 ? TArray<double>{ Root, 16.0, 30.0, Tip - 6.0 } : TArray<double>{ Root, Tip - 4.0 };
			TArray<int32> Le, Te;
			for (const double Y : Ys) { Le.Add(V(Lead(Y), Y, 0.f)); Te.Add(V(Lead(Y) - Chord(Y), Y, 1.f)); }
			for (int32 i = 0; i + 1 < Ys.Num(); ++i)
			{
				TriFacing(B, Le[i], Te[i], Te[i + 1], FVector::ZAxisVector);
				TriFacing(B, Le[i], Te[i + 1], Le[i + 1], FVector::ZAxisVector);
			}
			if (Detail == 0) { TriFacing(B, Le.Last(), Te.Last(), V(Lead(Tip) - 0.45 * Chord(Tip), Tip, 0.5f), FVector::ZAxisVector); }
		};
		if (Lod >= 2)
		{
			for (const double S : { -1.0, 1.0 }) { Wing(false, S, 2); Wing(true, S, 2); }
			// Its body's side, standing along it over its glow organs' row (V across it as round the finer bodies' flanks).
			const double H0 = 0.85 * F.Ra, H1 = 0.8 * F.Rh;
			const FVector A(F.Rear, 0.0, -H0), Bp(F.Rear, 0.0, H0), C(F.Nose, 0.0, H1), D(F.Nose, 0.0, -H1);
			const int32 Va = B.Vert(A, FVector::YAxisVector, FlierBodyUV(A, -1.0), FlierBodyCol), Vb = B.Vert(Bp, FVector::YAxisVector, FlierBodyUV(Bp, 1.0), FlierBodyCol);
			const int32 Vcv = B.Vert(C, FVector::YAxisVector, FlierBodyUV(C, 1.0), FlierBodyCol), Vd = B.Vert(D, FVector::YAxisVector, FlierBodyUV(D, -1.0), FlierBodyCol);
			TriFacing(B, Va, Vb, Vcv, FVector::YAxisVector);
			TriFacing(B, Va, Vcv, Vd, FVector::YAxisVector);
			return;
		}
		const bool bFine = Lod == 0;
		{
			// The body: its abdomen narrowing to its tip, its thorax, a pinch at its neck, its big head.
			TArray<double> Xs, R;
			if (bFine)
			{
				Xs = { F.Rear + 0.08 * F.Ab, F.Rear + 0.35 * F.Ab, F.Rear + 0.7 * F.Ab, F.Xt0, F.Xt0 + 0.5 * F.Th, F.Xh0, F.Xh0 + 0.45 * F.Hd, F.Xh0 + 0.8 * F.Hd };
				R = { 0.55 * F.Ra, 0.8 * F.Ra, F.Ra, 1.05 * F.Ra, F.Rt, 0.7 * F.Rt, F.Rh, 0.75 * F.Rh };
			}
			else
			{
				Xs = { F.Rear + 0.5 * F.Ab, F.Xt0, F.Xt0 + 0.5 * F.Th, F.Xh0 + 0.45 * F.Hd };
				R = { 0.8 * F.Ra, F.Ra, F.Rt, F.Rh };
			}
			TArray<double> Zc;
			Zc.Init(0.0, Xs.Num());
			LatheBody(B, Xs, R, R, Zc, 4, FVector(F.Rear, 0.0, 0.0), FVector(F.Nose, 0.0, 0.0), FlierBodyUV, [](const FVector&) { return FlierBodyCol; }, UE_DOUBLE_PI * 0.25);
		}
		for (const double S : { -1.0, 1.0 }) { Wing(false, S, bFine ? 0 : 1); Wing(true, S, bFine ? 0 : 1); }
		if (bFine && (Plan.Ornament & 1))
		{
			// A forked tail: two thin prongs from its abdomen's tip.
			for (const double S : { -1.0, 1.0 })
			{
				const FVector A(F.Rear + 1.0, S * 0.3, 0.0), Bp(F.Rear + 1.0, S * 1.2, 0.0), C(F.Rear - 6.0, S * 2.5, 0.0);
				TriFacing(B, FlatVert(B, A, FVector::ZAxisVector, FlierBodyUV(A, 0.0), FlierBodyCol), FlatVert(B, Bp, FVector::ZAxisVector, FlierBodyUV(Bp, 0.0), FlierBodyCol),
					FlatVert(B, C, FVector::ZAxisVector, FlierBodyUV(C, 0.0), FlierBodyCol), FVector::ZAxisVector);
			}
		}
		if (bFine && (Plan.Ornament & 2))
		{
			// Antennae, dark, from its head forward and up.
			const FVector4f Dark = VertexCol(Vc::SurfaceBeak, 0.f, Vc::SideMiddle, Vc::JointRigid);
			for (const double S : { -1.0, 1.0 })
			{
				const FVector A(F.Nose - 1.0, S * 0.4 * F.Rh, 0.6 * F.Rh), Bp(F.Nose - 1.5, S * 0.9 * F.Rh, 0.6 * F.Rh), C(F.Nose + 7.0, S * 3.0 * F.Rh, 1.4 * F.Rh);
				TriFacing(B, FlatVert(B, A, FVector::ZAxisVector, FlierBodyUV(A, 1.0), Dark), FlatVert(B, Bp, FVector::ZAxisVector, FlierBodyUV(Bp, 1.0), Dark),
					FlatVert(B, C, FVector::ZAxisVector, FlierBodyUV(C, 1.0), Dark), FVector::ZAxisVector);
			}
		}
	}

	/** A membrane-winged flier's body and wings as its plan lays them, unit-mesh cm: its body from Rear to Nose (Rb round),
	 *  its head on to its tip, its tail back from its rump, all of it laid about the mesh's middle; each wing's leading
	 *  edge from its shoulder (on its hinge) forward out to its wrist and swept far back to its tip, its trailing edge from
	 *  the root through each strut's end, scalloped deep between them. */
	struct FMembraneBody
	{
		double Lb = 36.0, Rb = 6.0, Rear = -20.0, Nose = 16.0, HeadTip = 28.0, TailEnd = -50.0, Shoulder = 5.0, Croot = 36.0;
		int32 Struts = 3;
	};
	FMembraneBody MembraneBody(const FFaunaBodyPlan& P)
	{
		FMembraneBody M;
		M.Lb = 100.0 * FMath::Clamp((double)P.Length, 0.25, 0.45);
		M.Rb = 50.0 * FMath::Clamp((double)P.Girth, 0.08, 0.16);
		M.Rear = -0.55 * M.Lb;
		M.Nose = 0.45 * M.Lb;
		M.HeadTip = M.Nose + 100.0 * FMath::Clamp((double)P.Neck, 0.06, 0.18);
		M.TailEnd = M.Rear - 100.0 * FMath::Clamp((double)P.Tail, 0.1, 0.5);
		M.Shoulder = 0.15 * M.Lb;
		M.Croot = 100.0 * FMath::Clamp((double)P.Chord, 0.25, 0.45);
		M.Struts = FMath::Clamp((int32)P.LimbPairs, 3, 4);
		// All of it about the mesh's middle, from its tail's vane to its head's tip.
		const double Shift = -0.5 * (M.TailEnd - 5.0 + M.HeadTip);
		M.Rear += Shift;
		M.Nose += Shift;
		M.HeadTip += Shift;
		M.TailEnd += Shift;
		M.Shoulder += Shift;
		return M;
	}
	/** A membrane wing's stations out along it, as its mesh is built: each's y, its leading and trailing edge's x, and its V
	 *  (along its struts: whole at each strut's end, a half between at the scallop's deepest). Every station is the root,
	 *  a strut's end or a scallop between two; at LOD 2 only the root, its middle scallop and its tip. */
	struct FMembraneStation { double Y, Lead, Trail; float V; };
	TArray<FMembraneStation> MembraneStations(const FMembraneBody& M, bool bSpeck)
	{
		const double Root = Vc::WingRootCm, Tip = SlonkFauna::UnitCm * 0.5, Wrist = Root + 0.35 * (Tip - Root);
		auto Lead = [&](double Y)
		{
			// Forward from the shoulder to the wrist, then swept far back along the longest finger to the tip.
			if (Y <= Wrist) { return M.Shoulder + 10.0 * (Y - Root) / (Wrist - Root); }
			return M.Shoulder + 10.0 - (10.0 + 0.5 * M.Croot) * (Y - Wrist) / (Tip - Wrist);
		};
		// The chord narrowing out to a point at the tip.
		auto Chord = [&](double Y) { return M.Croot * (1.0 - 0.9 * FMath::Pow(FMath::Clamp((Y - Root) / (Tip - Root), 0.0, 1.0), 1.3)) + 1.0; };
		const int32 N = M.Struts;
		auto StrutY = [&](int32 k) { return Root + (Tip - Root) * k / N; };
		auto StrutTrail = [&](int32 k) { return Lead(StrutY(k)) - Chord(StrutY(k)); };
		TArray<FMembraneStation> Out;
		Out.Add({ Root, Lead(Root), StrutTrail(0), 0.f });
		for (int32 k = 1; k <= N; ++k)
		{
			// The scallop between this strut's end and the one before's: the edge there pulled in toward the leading edge
			// by a third of the chord (the speck's only at its middle one).
			const double Ym = Root + (Tip - Root) * (k - 0.5) / N;
			if (!bSpeck || k == (N + 1) / 2) { Out.Add({ Ym, Lead(Ym), 0.5 * (StrutTrail(k - 1) + StrutTrail(k)) + 0.33 * Chord(Ym), (float)k - 0.5f }); }
			if (!bSpeck || k == N) { Out.Add({ StrutY(k), Lead(StrutY(k)), StrutTrail(k), (float)k }); }
		}
		return Out;
	}

	/**
	 * A membrane-winged flier of the plan, at a LOD: 82 to 101, 42 to 50, or 6 triangles. LOD 0: its body a four-sided
	 * spindle of four rings coned to its rump and on to its long head's tip, its flanks upright, a crest as its plan wears it, a whip of a tail
	 * (a vane at its end as it wears one), flexing from its root; each wing of skin from its hinge to its tip in two rows
	 * across, its stations out along it its root, each strut's end and the scallop between (its strut count of them,
	 * three or four), so its trailing edge is scalloped between the struts the material draws. LOD 1: a four-sided body,
	 * its tail, each wing one row across at every station. LOD 2: a wing two triangles (the root, a scallop and its tip),
	 * and its body's side a quad standing along it.
	 */
	void BuildMembrane(FBuild& B, const FFaunaBodyPlan& Plan, int32 Lod)
	{
		const FMembraneBody M = MembraneBody(Plan);
		const double WingZ = 0.3 * M.Rb;
		auto Wings = [&](bool bSpeck, int32 Rows)
		{
			const TArray<FMembraneStation> St = MembraneStations(M, bSpeck);
			for (const double S : { -1.0, 1.0 })
			{
				TArray<TArray<int32>> Cols;
				for (const FMembraneStation& Sn : St)
				{
					TArray<int32> Col;
					for (int32 r = 0; r <= Rows; ++r)
					{
						const float U = (float)r / Rows;
						const double X = FMath::Lerp(Sn.Lead, Sn.Trail, (double)U);
						Col.Add(B.Vert(FVector(X, S * Sn.Y, WingZ), FVector::ZAxisVector, FVector2f(U, Sn.V), WingCol(S * Sn.Y)));
					}
					Cols.Add(Col);
				}
				for (int32 c = 0; c + 1 < Cols.Num(); ++c)
				{
					for (int32 r = 0; r < Rows; ++r)
					{
						TriFacing(B, Cols[c][r], Cols[c][r + 1], Cols[c + 1][r + 1], FVector::ZAxisVector);
						TriFacing(B, Cols[c][r], Cols[c + 1][r + 1], Cols[c + 1][r], FVector::ZAxisVector);
					}
				}
			}
		};
		if (Lod >= 2)
		{
			// Each wing as its root, one scallop and its tip: two triangles.
			const TArray<FMembraneStation> St = MembraneStations(M, true);
			for (const double S : { -1.0, 1.0 })
			{
				auto V = [&](double X, double Y, float U, float Vv) { return B.Vert(FVector(X, S * Y, WingZ), FVector::ZAxisVector, FVector2f(U, Vv), WingCol(S * Y)); };
				const FMembraneStation& R0 = St[0];
				const FMembraneStation& Mid = St[FMath::Min(2, St.Num() - 2)];
				const FMembraneStation& T = St.Last();
				const int32 A = V(R0.Lead, R0.Y, 0.f, R0.V), Bt = V(R0.Trail, R0.Y, 1.f, R0.V), C = V(Mid.Trail, Mid.Y, 1.f, Mid.V), D = V(T.Lead, T.Y, 0.f, T.V);
				TriFacing(B, A, Bt, C, FVector::ZAxisVector);
				TriFacing(B, A, C, D, FVector::ZAxisVector);
			}
			const FVector A(M.Rear, 0.0, -0.85 * M.Rb), Bp(M.Rear, 0.0, 0.85 * M.Rb), C(M.HeadTip, 0.0, 0.3 * M.Rb), D(M.HeadTip, 0.0, -0.3 * M.Rb);
			const int32 Va = B.Vert(A, FVector::YAxisVector, FlierBodyUV(A, -1.0), FlierBodyCol), Vb = B.Vert(Bp, FVector::YAxisVector, FlierBodyUV(Bp, 1.0), FlierBodyCol);
			const int32 Vcv = B.Vert(C, FVector::YAxisVector, FlierBodyUV(C, 1.0), FlierBodyCol), Vd = B.Vert(D, FVector::YAxisVector, FlierBodyUV(D, -1.0), FlierBodyCol);
			TriFacing(B, Va, Vb, Vcv, FVector::YAxisVector);
			TriFacing(B, Va, Vcv, Vd, FVector::YAxisVector);
			return;
		}
		const bool bFine = Lod == 0;
		{
			// The body: a spindle from its rump to the back of its head, coned to its long head's tip.
			const TArray<double> Xs = bFine ? TArray<double>{ M.Rear + 0.12 * M.Lb, M.Rear + 0.4 * M.Lb, M.Rear + 0.7 * M.Lb, M.Nose } : TArray<double>{ M.Rear + 0.3 * M.Lb, M.Nose - 0.1 * M.Lb };
			const TArray<double> R = bFine ? TArray<double>{ 0.6 * M.Rb, M.Rb, 0.9 * M.Rb, 0.55 * M.Rb } : TArray<double>{ 0.9 * M.Rb, 0.75 * M.Rb };
			TArray<double> Rz, Zc;
			for (const double Rr : R) { Rz.Add(0.85 * Rr); Zc.Add(0.0); }
			LatheBody(B, Xs, R, Rz, Zc, 4, FVector(M.Rear, 0.0, 0.0), FVector(M.HeadTip, 0.0, -0.1 * M.Rb), FlierBodyUV, [](const FVector&) { return FlierBodyCol; }, UE_DOUBLE_PI * 0.25);
		}
		Wings(false, bFine ? 2 : 1);
		{
			// The tail: a whip from its rump, flexing from its root; a vane at its end as it wears one.
			const double Len = FMath::Max(M.Rear - M.TailEnd, 2.0);
			auto TailC = [&](double X) { return VertexCol(Vc::SurfaceTail, (float)FMath::Clamp((M.Rear - X) / Len, 0.0, 1.0), Vc::SideMiddle, Vc::JointTail); };
			auto Tv = [&](const FVector& P) { return FlatVert(B, P, FVector::ZAxisVector, FlierBodyUV(P, 0.0), TailC(P.X)); };
			const double W0 = 0.35 * M.Rb, W1 = 0.6;
			const int32 A = Tv(FVector(M.Rear + 1.0, -W0, 0.0)), Bv = Tv(FVector(M.Rear + 1.0, W0, 0.0)), C = Tv(FVector(M.TailEnd, W1, 0.0)), D = Tv(FVector(M.TailEnd, -W1, 0.0));
			TriFacing(B, A, Bv, C, FVector::ZAxisVector);
			TriFacing(B, A, C, D, FVector::ZAxisVector);
			if (bFine && (Plan.Ornament & 2))
			{
				const double Vw = 0.3 * M.Rb + 3.0;
				const int32 E = Tv(FVector(M.TailEnd + 4.0, 0.0, 0.0)), Fv = Tv(FVector(M.TailEnd, Vw, 0.0)), G = Tv(FVector(M.TailEnd - 5.0, 0.0, 0.0)), H = Tv(FVector(M.TailEnd, -Vw, 0.0));
				TriFacing(B, E, Fv, G, FVector::ZAxisVector);
				TriFacing(B, E, G, H, FVector::ZAxisVector);
			}
		}
		if (bFine && (Plan.Ornament & 1))
		{
			// A crest standing back from its head.
			const FVector4f Dark = VertexCol(Vc::SurfaceBeak, 0.f, Vc::SideMiddle, Vc::JointRigid);
			const FVector A(M.Nose + 0.3 * (M.HeadTip - M.Nose), 0.0, 0.5 * M.Rb), Bp(M.Nose - 2.0, 0.0, 0.6 * M.Rb), C(M.Nose - 0.25 * M.Lb, 0.0, 1.6 * M.Rb + 4.0);
			TriFacing(B, FlatVert(B, A, FVector::YAxisVector, FlierBodyUV(A, 1.0), Dark), FlatVert(B, Bp, FVector::YAxisVector, FlierBodyUV(Bp, 1.0), Dark),
				FlatVert(B, C, FVector::YAxisVector, FlierBodyUV(C, 1.0), Dark), FVector::YAxisVector);
		}
	}

	/** An alien fish's colour at a point: its surface, how far it bends there (an eel's from its nose, a spinefin's as an
	 *  Earth fish's), its side, its joint (it undulates). */
	FVector4f EelCol(float Surface, const FVector& P)
	{
		return FVector4f(Surface, (float)FMath::Clamp((50.0 - P.X) / 100.0, 0.0, 1.0), P.Y > 0.5 ? Vc::SideRight : (P.Y < -0.5 ? Vc::SideLeft : Vc::SideMiddle), Vc::JointUndulate);
	}
	/** A fish's UV: its place along it from its tail's tip (U) and up its body's depth (V, 0 at its belly, 1 at its back). */
	FVector2f FishUVAt(const FVector& P, double Rz) { return FVector2f((float)((P.X + 50.0) / 100.0), (float)FMath::Clamp((P.Z + Rz) / (2.0 * Rz), 0.0, 1.0)); }
	/** A fish's speck's side: a quad standing along it from Xb to Xf, Hz up and down (its glow organs' row drawn over it). */
	void FishSpeckSide(FBuild& B, double Xb, double Xf, double Hz, double Rz, TFunctionRef<FVector4f(float, const FVector&)> Col)
	{
		const FVector Y = FVector::YAxisVector;
		const FVector A(Xb, 0.0, -Hz), Bp(Xb, 0.0, Hz), C(Xf, 0.0, Hz), D(Xf, 0.0, -Hz);
		const int32 Va = B.Vert(A, Y, FishUVAt(A, Rz), Col(Vc::SurfaceBody, A)), Vb = B.Vert(Bp, Y, FishUVAt(Bp, Rz), Col(Vc::SurfaceBody, Bp));
		const int32 Vcv = B.Vert(C, Y, FishUVAt(C, Rz), Col(Vc::SurfaceBody, C)), Vd = B.Vert(D, Y, FishUVAt(D, Rz), Col(Vc::SurfaceBody, D));
		TriFacing(B, Va, Vb, Vcv, Y);
		TriFacing(B, Va, Vcv, Vd, Y);
	}

	/**
	 * An eel of the plan, at a LOD: 82 or 84, 38 or 3 triangles, a metre from its tail's tip to its nose. LOD 0: a
	 * four-sided body of eight rings, deeper than it is wide, its flanks upright, coned to its nose and to its tail's tip; a ribbon of fin
	 * along its back from its middle to its tail's tip (five panels) and along its belly (four), and barbels as its plan
	 * wears them. LOD 1: a four-sided body of four rings, its ribbons of two panels and one. LOD 2: its side a quad
	 * standing along it and its back a triangle lying along it. Its G its bend from its nose.
	 */
	void BuildEel(FBuild& B, const FFaunaBodyPlan& Plan, int32 Lod)
	{
		const double Gi = FMath::Clamp((double)Plan.Girth, 0.05, 0.14);
		const double Rz = 50.0 * Gi * 1.15, Ry = 50.0 * Gi * 0.85;
		const double Fin = FMath::Clamp((double)Plan.Tail, 0.2, 0.7) * 2.0 * Rz;
		auto Uv = [&](const FVector& P, double) { return FishUVAt(P, Rz); };
		auto Col = [](const FVector& P) { return EelCol(Vc::SurfaceBody, P); };
		const FVector YAx = FVector::YAxisVector, ZAx = FVector::ZAxisVector;
		if (Lod >= 2)
		{
			FishSpeckSide(B, -48.0, 48.0, 0.8 * Rz, Rz, [](float S, const FVector& P) { return EelCol(S, P); });
			const FVector T0(50.0, 0.0, 0.0), T1(-50.0, -0.8 * Ry, 0.0), T2(-50.0, 0.8 * Ry, 0.0);
			TriFacing(B, B.Vert(T0, ZAx, FishUVAt(T0, Rz), Col(T0)), B.Vert(T1, ZAx, FishUVAt(T1, Rz), Col(T1)), B.Vert(T2, ZAx, FishUVAt(T2, Rz), Col(T2)), ZAx);
			return;
		}
		const bool bFine = Lod == 0;
		const TArray<double> Xs = bFine ? TArray<double>{ -45.0, -36.0, -24.0, -10.0, 4.0, 18.0, 32.0, 44.0 } : TArray<double>{ -38.0, -14.0, 10.0, 34.0 };
		const TArray<double> K = bFine ? TArray<double>{ 0.35, 0.62, 0.82, 0.95, 1.0, 1.0, 0.9, 0.6 } : TArray<double>{ 0.55, 0.9, 1.0, 0.85 };
		TArray<double> R1, R2, Zc;
		for (const double k : K) { R1.Add(Ry * k); R2.Add(Rz * k); Zc.Add(0.0); }
		LatheBody(B, Xs, R1, R2, Zc, 4, FVector(-50.0, 0.0, 0.0), FVector(50.0, 0.0, -0.1 * Rz), Uv, Col, UE_DOUBLE_PI * 0.25);
		// The ribbons: along its back from its middle to its tail's tip, and along its belly, standing up and down from it,
		// as high as its plan's share of its depth and tapering into its tail.
		auto Ribbon = [&](const TArray<double>& At, double Sign)
		{
			TArray<int32> Prev;
			for (int32 i = 0; i < At.Num(); ++i)
			{
				const double X = At[i];
				const int32 Ki = Xs.IndexOfByPredicate([X](double V) { return FMath::IsNearlyEqual(V, X); });
				const double Base = (Ki >= 0 ? K[Ki] : 0.3) * Rz * 0.95;
				const double H = Fin * (X <= -48.0 ? 0.35 : FMath::Clamp((X + 50.0) / 40.0, 0.4, 1.0));
				const FVector Lo(X, 0.0, Sign * (X <= -48.0 ? 0.0 : Base)), Hi(X, 0.0, Sign * ((X <= -48.0 ? 0.0 : Base) + H));
				TArray<int32> Row = { B.Vert(Lo, YAx, FishUVAt(Lo, Rz), EelCol(Vc::SurfaceTail, Lo)), B.Vert(Hi, YAx, FishUVAt(Hi, Rz), EelCol(Vc::SurfaceTail, Hi)) };
				if (Prev.Num() > 0)
				{
					TriFacing(B, Prev[0], Prev[1], Row[1], YAx);
					TriFacing(B, Prev[0], Row[1], Row[0], YAx);
				}
				Prev = MoveTemp(Row);
			}
		};
		if (bFine)
		{
			Ribbon({ 4.0, -10.0, -24.0, -36.0, -45.0, -50.0 }, 1.0);
			Ribbon({ -10.0, -24.0, -36.0, -45.0, -50.0 }, -1.0);
		}
		else
		{
			Ribbon({ 10.0, -14.0, -50.0 }, 1.0);
			Ribbon({ -14.0, -50.0 }, -1.0);
		}
		if (bFine && Plan.Ornament)
		{
			// Barbels from its mouth, back and down.
			for (const double S : { -1.0, 1.0 })
			{
				const FVector A(48.0, S * 0.4 * Ry, -0.3 * Rz), Bp(46.0, S * 0.5 * Ry, -0.1 * Rz), C(38.0, S * (Ry + 3.0), -1.2 * Rz - 2.0);
				TriFacing(B, B.Vert(A, ZAx, FishUVAt(A, Rz), EelCol(Vc::SurfaceTail, A)), B.Vert(Bp, ZAx, FishUVAt(Bp, Rz), EelCol(Vc::SurfaceTail, Bp)),
					B.Vert(C, ZAx, FishUVAt(C, Rz), EelCol(Vc::SurfaceTail, C)), ZAx);
			}
		}
	}

	/**
	 * A spinefin of the plan, at a LOD: 63 or 66, 21 or 5 triangles, a metre from its tail's tip to its nose. LOD 0: a
	 * deep six-sided body of four rings coned to its nose and its tail's root; a deeply forked tail of three triangles;
	 * spines along its back (four, and three shorter between them as its plan wears them); its four broad fins (the
	 * pectoral pair of three triangles each, the pelvic of one), lying out from their hinges as it spreads them, the
	 * wing's surface, flexing from the hinge. LOD 1: a four-sided body of two rings, its tail's two lobes, its spines as
	 * one fin, a triangle a pectoral fin. LOD 2: its side a quad standing along it, its back a triangle lying along it,
	 * and a triangle a pectoral fin from its hinge.
	 */
	void BuildSpinefin(FBuild& B, const FFaunaBodyPlan& Plan, int32 Lod)
	{
		const double G = FMath::Clamp((double)Plan.Girth, 0.12, 0.35) / 0.2;
		const double Rz = 10.0 * G, Ry = 6.0 * G;
		const double Root = -30.0, Nose = 50.0, TailTip = -50.0;
		const double TailK = FMath::Clamp((double)Plan.Tail, 0.2, 0.4) / 0.25;
		const double Hinge = Ff::FinRootCm, Reach = 100.0 * FMath::Clamp((double)Plan.Chord, 0.2, 0.45);
		const double SpineH = 100.0 * FMath::Clamp((double)Plan.Neck, 0.08, 0.24);
		auto Uv = [&](const FVector& P, double) { return FishUVAt(P, Rz); };
		auto Col = [](const FVector& P) { return FishCol(Vc::SurfaceBody, P); };
		const FVector YAx = FVector::YAxisVector, ZAx = FVector::ZAxisVector;
		// A fin's colour: the wing's surface, its flex from the hinge out to its tip, its side.
		auto FinCol = [&](const FVector& P, double TipY) { return FVector4f(Vc::SurfaceWing, (float)FMath::Clamp((FMath::Abs(P.Y) - Hinge) / FMath::Max(TipY - Hinge, 1.0), 0.0, 1.0), P.Y > 0.0 ? Vc::SideRight : Vc::SideLeft, Vc::JointUndulate); };
		auto FinV = [&](const FVector& P, double TipY) { return B.Vert(P, ZAx, FishUVAt(P, Rz), FinCol(P, TipY)); };
		if (Lod >= 2)
		{
			FishSpeckSide(B, -42.0, 46.0, 0.85 * Rz, Rz, [](float S, const FVector& P) { return FishCol(S, P); });
			// Its back and its pectoral fins spread across it.
			const FVector T0(Nose, 0.0, 0.0), T1(-40.0, -1.1 * Ry, 0.0), T2(-40.0, 1.1 * Ry, 0.0);
			TriFacing(B, B.Vert(T0, ZAx, FishUVAt(T0, Rz), Col(T0)), B.Vert(T1, ZAx, FishUVAt(T1, Rz), Col(T1)), B.Vert(T2, ZAx, FishUVAt(T2, Rz), Col(T2)), ZAx);
			// A triangle a pectoral fin, from its hinge (so it folds and spreads as the finer LODs' do).
			const double Tip = Hinge + Reach;
			for (const double S : { -1.0, 1.0 })
			{
				TriFacing(B, FinV(FVector(22.0, S * Hinge, -0.3 * Rz), Tip), FinV(FVector(6.0, S * Hinge, -0.3 * Rz), Tip), FinV(FVector(10.0, S * Tip, -0.3 * Rz - 2.0), Tip), ZAx);
			}
			return;
		}
		const bool bFine = Lod == 0;
		{
			const TArray<double> Xs = bFine ? TArray<double>{ Root, -10.0, 12.0, 32.0 } : TArray<double>{ -24.0, 14.0 };
			const TArray<double> K = bFine ? TArray<double>{ 0.3, 0.8, 1.0, 0.75 } : TArray<double>{ 0.5, 1.0 };
			TArray<double> R1, R2, Zc;
			for (const double k : K) { R1.Add(Ry * k); R2.Add(Rz * k); Zc.Add(0.0); }
			LatheBody(B, Xs, R1, R2, Zc, bFine ? 6 : 4, FVector(Root - 4.0, 0.0, 0.0), FVector(Nose, 0.0, -0.1 * Rz), Uv, Col, bFine ? UE_DOUBLE_HALF_PI : UE_DOUBLE_PI * 0.25);
		}
		// Its tail: deeply forked, standing up from its root to its two tips (a notch between at LOD 0).
		{
			auto Tv = [&](const FVector& P) { return B.Vert(P, YAx, FishUVAt(P, Rz), FishCol(Vc::SurfaceTail, P)); };
			const FVector RT(Root - 1.0, 0.0, 0.3 * Rz), RB(Root - 1.0, 0.0, -0.3 * Rz), UT(TailTip, 0.0, 1.3 * Rz * TailK), LT(TailTip, 0.0, -1.3 * Rz * TailK);
			if (bFine)
			{
				const FVector Notch(-38.0, 0.0, 0.0);
				TriFacing(B, Tv(RT), Tv(UT), Tv(Notch), YAx);
				TriFacing(B, Tv(RT), Tv(Notch), Tv(RB), YAx);
				TriFacing(B, Tv(RB), Tv(Notch), Tv(LT), YAx);
			}
			else
			{
				const FVector Mid(Root - 1.0, 0.0, 0.0);
				TriFacing(B, Tv(Mid), Tv(UT), Tv(FVector(-40.0, 0.0, 0.0)), YAx);
				TriFacing(B, Tv(Mid), Tv(FVector(-40.0, 0.0, 0.0)), Tv(LT), YAx);
			}
		}
		// Its spines along its back, swept back, each a thin triangle (at LOD 1 one fin over them).
		{
			auto Sv = [&](const FVector& P) { return B.Vert(P, YAx, FishUVAt(P, Rz), FishCol(Vc::SurfaceTail, P)); };
			if (bFine)
			{
				for (int32 i = 0; i < 4; ++i)
				{
					const double X = 20.0 - 11.0 * i;
					const double Hh = SpineH * (1.0 - 0.15 * i);
					TriFacing(B, Sv(FVector(X, 0.0, 0.85 * Rz)), Sv(FVector(X - 3.0, 0.0, 0.85 * Rz)), Sv(FVector(X - 5.0, 0.0, 0.85 * Rz + Hh)), YAx);
					if ((Plan.Ornament & 1) && i < 3)
					{
						TriFacing(B, Sv(FVector(X - 5.0, 0.0, 0.85 * Rz)), Sv(FVector(X - 7.0, 0.0, 0.85 * Rz)), Sv(FVector(X - 8.5, 0.0, 0.85 * Rz + 0.5 * Hh)), YAx);
					}
				}
			}
			else
			{
				TriFacing(B, Sv(FVector(20.0, 0.0, 0.85 * Rz)), Sv(FVector(-16.0, 0.0, 0.85 * Rz)), Sv(FVector(15.0, 0.0, 0.85 * Rz + SpineH)), YAx);
			}
		}
		// Its fins, lying out from their hinges as it spreads them: the pectoral pair broad behind its head, the pelvic
		// smaller under its belly.
		for (const double S : { -1.0, 1.0 })
		{
			const double Tip = Hinge + Reach, Z = -0.3 * Rz;
			if (bFine)
			{
				const double Mid = Hinge + 0.55 * Reach;
				const int32 A = FinV(FVector(24.0, S * Hinge, Z), Tip), Bt = FinV(FVector(8.0, S * Hinge, Z), Tip);
				const int32 C = FinV(FVector(8.0, S * Mid, Z - 1.0), Tip), D = FinV(FVector(20.0, S * Mid, Z - 1.0), Tip);
				const int32 E = FinV(FVector(10.0, S * Tip, Z - 2.0), Tip);
				TriFacing(B, A, Bt, C, ZAx);
				TriFacing(B, A, C, D, ZAx);
				TriFacing(B, D, C, E, ZAx);
				const double Pt = Hinge + 0.55 * Reach, Pz = -0.6 * Rz;
				TriFacing(B, FinV(FVector(-6.0, S * Hinge, Pz), Pt), FinV(FVector(-18.0, S * Hinge, Pz), Pt), FinV(FVector(-16.0, S * Pt, Pz - 1.0), Pt), ZAx);
			}
			else
			{
				TriFacing(B, FinV(FVector(24.0, S * Hinge, Z), Tip), FinV(FVector(6.0, S * Hinge, Z), Tip), FinV(FVector(10.0, S * Tip, Z - 2.0), Tip), ZAx);
			}
		}
	}
}

SlonkFauna::GlowOrgan::FRow SlonkFaunaMeshes::GlowRowOf(const FFaunaBodyPlan& Plan)
{
	SlonkFauna::GlowOrgan::FRow Row;
	if (Plan.GlowOrgans == 0) { return Row; }
	Row.Spots = Plan.GlowOrgans;
	auto U = [](double X) { return (float)((X + 50.0) / 100.0); };
	// Each spot about RCm of the unit mesh round, its V's half-width the share of the depth or round the body that is.
	auto Set = [&](double X0, double X1, float V, double RCm, double VPerCm)
	{
		Row.U0 = U(X0);
		Row.U1 = U(X1);
		Row.V = V;
		Row.RU = (float)(RCm / 100.0);
		Row.RV = (float)(RCm * VPerCm);
	};
	if (Plan.Wings == EFaunaWings::FourWings)
	{
		// Along its abdomen's flanks (V round it: a flank's half-way, over twice its radius a centimetre).
		const FFourWingBody F = FourWingBody(Plan);
		Set(F.Rear + 0.14 * F.Ab, F.Xt0 - 0.08 * F.Ab, 0.5f, 1.4, 1.0 / (2.0 * 0.9 * F.Ra));
		return Row;
	}
	if (Plan.Wings == EFaunaWings::Membrane)
	{
		const FMembraneBody M = MembraneBody(Plan);
		Set(M.Rear + 0.15 * M.Lb, M.Nose - 0.12 * M.Lb, 0.5f, 2.0, 1.0 / (2.0 * 0.85 * M.Rb));
		return Row;
	}
	if (Plan.Limbs == EFaunaLimbs::Fins)
	{
		// A lateral line of them, half-way up its depth (V up it: its depth a centimetre's share).
		if (SlonkFauna::FishForm::FormOf(Plan) == SlonkFauna::FishForm::Eel)
		{
			const double Rz = 50.0 * FMath::Clamp((double)Plan.Girth, 0.05, 0.14) * 1.15;
			Set(-35.0, 28.0, 0.5f, 1.8, 1.0 / (2.0 * Rz));
		}
		else
		{
			const double Rz = 10.0 * FMath::Clamp((double)Plan.Girth, 0.12, 0.35) / 0.2;
			Set(-24.0, 28.0, 0.46f, 2.6, 1.0 / (2.0 * Rz));
		}
		return Row;
	}
	const SlonkFauna::Walker::FShape S = SlonkFauna::Walker::ShapeOf(Plan);
	if (S.Form == SlonkFauna::Walker::EForm::Hexapod)
	{
		// Along its barrel's flank, on its upright faces at every LOD (V up it, z over 60 cm), where its speck's side stands too.
		const FHexapodBody H = HexapodBody(Plan);
		const double Span = H.X1 - H.X0;
		Set(H.X0 + 0.2 * Span, H.X1 - 0.12 * Span, (float)((H.Zc - 0.1 * H.Rz) / 60.0), 3.4, 1.0 / 60.0);
	}
	else if (S.Form == SlonkFauna::Walker::EForm::Insectoid)
	{
		// Along its abdomen's flank, where its abdomen is at every LOD and its speck's side stands too.
		const FInsectBody I = InsectBody(Plan);
		Set(-50.0 + 0.35 * (I.Xa1 + 50.0), -50.0 + 0.75 * (I.Xa1 + 50.0), (float)(I.Za / 60.0), 2.6, 1.0 / 60.0);
	}
	else { Row.Spots = 0; }
	return Row;
}

bool SlonkFaunaMeshes::BuildLod(const FFaunaBodyPlan& Plan, int32 Lod, FDynamicMesh3& Out)
{
	const SlonkFauna::Walker::EForm Form = SlonkFauna::Walker::ShapeOf(Plan).Form;
	const bool bWalker = Form != SlonkFauna::Walker::EForm::None;
	const bool bFish = Plan.Wings == EFaunaWings::None && Plan.Limbs == EFaunaLimbs::Fins;
	const bool bJelly = Plan.Wings == EFaunaWings::None && Plan.Limbs == EFaunaLimbs::Tentacles && Plan.Symmetry >= 3 && Plan.LimbPairs >= 1;
	if (Plan.Wings == EFaunaWings::None && !bWalker && !bFish && !bJelly) { return false; }
	FBuild B;
	const int32 L = FMath::Clamp(Lod, 0, NumLods - 1);
	if (Form == SlonkFauna::Walker::EForm::Hexapod) { BuildHexapod(B, Plan, L); }
	else if (Form == SlonkFauna::Walker::EForm::Insectoid) { BuildInsectoid(B, Plan, L); }
	else if (bWalker) { BuildWalker(B, Plan, L); }
	else if (bFish && Ff::FormOf(Plan) == Ff::Eel) { BuildEel(B, Plan, L); }
	else if (bFish && Ff::FormOf(Plan) == Ff::Spinefin) { BuildSpinefin(B, Plan, L); }
	else if (bFish) { BuildFish(B, Plan, L); }
	else if (bJelly) { BuildJelly(B, Plan, L); }
	else if (Wn::FormOf(Plan) == Wn::Four) { BuildFourWing(B, Plan, L); }
	else if (Wn::FormOf(Plan) == Wn::Membrane) { BuildMembrane(B, Plan, L); }
	else { BuildBird(B, Plan, L); }
	Out = MoveTemp(B.Mesh);
	return true;
}

void SlonkFaunaMeshes::HoldPlans(const void* Holder, TConstArrayView<TPair<FFaunaBodyPlan, EFaunaKind>> Plans, bool bLetGoNow)
{
	if (Plans.Num() == 0) { GHeldPlans.Remove(Holder); }
	else
	{
		TSet<uint32>& Held = GHeldPlans.FindOrAdd(Holder);
		Held.Reset();
		for (const TPair<FFaunaBodyPlan, EFaunaKind>& P : Plans) { Held.Add(MeshKey(P.Key, P.Value)); }
	}
	if (bLetGoNow) { LetGoUnheld(); }
}

int32 SlonkFaunaMeshes::NumCachedMeshes()
{
	return GFaunaMeshes.Num();
}

bool SlonkFaunaMeshes::IsMeshCached(const FFaunaBodyPlan& Plan, EFaunaKind Kind)
{
	return GFaunaMeshes.Contains(MeshKey(Plan, Kind));
}

UStaticMesh* SlonkFaunaMeshes::GetMesh(const FFaunaBodyPlan& Plan, EFaunaKind Kind)
{
	const uint32 Key = MeshKey(Plan, Kind);
	if (UStaticMesh* const* Found = GFaunaMeshes.Find(Key))
	{
		if (*Found && IsValid(*Found)) { return *Found; }
	}
	FDynamicMesh3 Lods[NumLods];
	for (int32 L = 0; L < NumLods; ++L)
	{
		if (!BuildLod(Plan, L, Lods[L])) { return nullptr; }
	}
	// The material moves the vertices past the mesh's box by up to the kind's padding, which the instances' boxes take in
	// (UPlanetInstancesComponent::SetCullPadding): the mesh's own bounds stay its own, and its LOD screen sizes are the
	// kind's, set for the padded box the GPU picks them by. A walker's mesh and a fish's have a tangent basis, as the
	// material turns their normals the way they face in tangent space, and a jelly's (its normals its own, but lit through
	// its membrane as a leaf is); the Earth birds' normals are the mesh's own, whatever their tangents (a strange world's
	// fliers have a basis, as every mesh made since has).
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(Kind);
	const bool bTangents = Kind == EFaunaKind::Herd || Kind == EFaunaKind::Leaper || Kind == EFaunaKind::Floater || (Kind == EFaunaKind::Flier && !SlonkFauna::IsEarthPlan(Kind, Plan));
	UStaticMesh* Mesh = SlonkProcMesh::MakeStaticMesh(*FString::Printf(TEXT("SlonkFauna_%s_%08x"), SlonkFauna::KindName(Kind), Key), TEXT("Fauna"),
		TArrayView<const FDynamicMesh3>(Lods, NumLods), TArrayView<const float>(D.LodScreenSize, NumLods), FVector::ZeroVector, bTangents);
	GFaunaMeshes.Add(Key, Mesh);
	return Mesh;
}

void SlonkFaunaMeshes::BuildMoteCloudLod(int32 Lod, FDynamicMesh3& Out)
{
	namespace Mo = SlonkFauna::Mote;
	Lod = FMath::Clamp(Lod, 0, NumLods - 1);
	FBuild B;
	const int32 Cards = Mo::CardsAtLod[Lod];
	const int32 Gnats = Lod == 0 ? Mo::GnatsPerCloud : (Lod == 1 ? 2 : 0);
	const int32 Spread = Cards - Gnats;
	const int32 Free = Mo::MotesPerCloud - Mo::GnatsPerCloud;
	const double H = Mo::CardHalfCm;
	auto Card = [&](const FVector& C, float Stands, float Rnd)
	{
		const FVector4f Col(Vc::SurfaceMote, Stands / (float)Mo::MotesPerCloud, Rnd, Vc::JointRigid);
		int32 V[4];
		for (int32 k = 0; k < 4; ++k)
		{
			const float U = (k == 1 || k == 2) ? 1.f : 0.f, Vv = k >= 2 ? 1.f : 0.f;
			V[k] = B.Vert(C + FVector((2.0 * U - 1.0) * H, 0.0, (2.0 * Vv - 1.0) * H), FVector::ZAxisVector, FVector2f(U, Vv), Col);
		}
		// Wound as a leaf card is (FBuild::Card): the material lays a card's U to the camera's right and its V up, so the
		// camera sees its front, and the two-sided shading keeps its normal up rather than turning it into the ground.
		B.Quad(V[0], V[3], V[2], V[1]);
	};
	// The gnats' swarm: a ball 8 cm of the unit mesh across (about half a metre at a cloud's span), off the middle, about
	// a fifth of a span up; their randoms under GnatShare.
	const FVector Swarm(9.0, -7.0, 21.0);
	for (int32 i = 0; i < Gnats; ++i)
	{
		const double A = i * UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0)), Z = 1.0 - 2.0 * (i + 0.5) / Gnats, Rr = FMath::Sqrt(FMath::Max(1.0 - Z * Z, 0.0));
		Card(Swarm + FVector(Rr * FMath::Cos(A), Rr * FMath::Sin(A), Z) * 4.0, (float)Mo::GnatsPerCloud / Gnats, (0.5f + i) / Gnats * (Mo::GnatShare - 0.02f));
	}
	// The rest spread over a disc half a span round (a sunflower's spiral), at heights spread through 0.04 to 0.3 of a
	// span; their randoms through the rest of the range, each its own.
	for (int32 j = 0; j < Spread; ++j)
	{
		const double Rr = 45.0 * FMath::Sqrt((j + 0.5) / Spread), A = j * UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0)) + 0.7 * Lod;
		const double Z = 4.0 + 26.0 * FMath::Frac(j * 0.6180339887 + 0.37 + 0.11 * Lod);
		const float Rnd = Mo::GnatShare + 0.02f + (1.f - Mo::GnatShare - 0.04f) * (float)FMath::Frac((j + 0.5) / Spread * 0.999 + 0.29 * Lod);
		Card(FVector(Rr * FMath::Cos(A), Rr * FMath::Sin(A), Z), (float)Free / Spread, Rnd);
	}
	Out = MoveTemp(B.Mesh);
}

UStaticMesh* SlonkFaunaMeshes::GetMoteCloudMesh()
{
	static UStaticMesh* Cached = nullptr;
	if (Cached && IsValid(Cached)) { return Cached; }
	FDynamicMesh3 Lods[NumLods];
	for (int32 L = 0; L < NumLods; ++L) { BuildMoteCloudLod(L, Lods[L]); }
	Cached = SlonkProcMesh::MakeStaticMesh(TEXT("SlonkFauna_MoteCloud"), TEXT("Fauna"), TArrayView<const FDynamicMesh3>(Lods, NumLods),
		TArrayView<const float>(MoteLodScreenSize, NumLods), FVector(SlonkFauna::Mote::ReachCm));
	return Cached;
}
