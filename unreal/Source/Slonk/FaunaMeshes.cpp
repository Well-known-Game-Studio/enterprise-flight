#include "FaunaMeshes.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "ProcMeshBuild.h"

using namespace UE::Geometry;

namespace
{
	using SlonkProcMesh::FBuild;
	namespace Vc = SlonkFauna::Vc;

	TMap<uint32, UStaticMesh*> GFaunaMeshes;

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

bool SlonkFaunaMeshes::BuildLod(const FFaunaBodyPlan& Plan, int32 Lod, FDynamicMesh3& Out)
{
	const bool bWalker = Plan.Wings == EFaunaWings::None && Plan.Limbs == EFaunaLimbs::Legs && Plan.LimbPairs == 2;
	if (Plan.Wings == EFaunaWings::None && !bWalker) { return false; }
	FBuild B;
	if (bWalker) { BuildWalker(B, Plan, FMath::Clamp(Lod, 0, NumLods - 1)); }
	else { BuildBird(B, Plan, FMath::Clamp(Lod, 0, NumLods - 1)); }
	Out = MoveTemp(B.Mesh);
	return true;
}

UStaticMesh* SlonkFaunaMeshes::GetMesh(const FFaunaBodyPlan& Plan, EFaunaKind Kind)
{
	const uint32 Key = HashCombine(Plan.Hash(), GetTypeHash((uint8)Kind));
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
	// kind's, set for the padded box the GPU picks them by. A walker's mesh has a tangent basis, as the material turns its
	// normals the way it faces in tangent space; the birds' normals are the mesh's own, whatever their tangents.
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(Kind);
	UStaticMesh* Mesh = SlonkProcMesh::MakeStaticMesh(*FString::Printf(TEXT("SlonkFauna_%s_%08x"), SlonkFauna::KindName(Kind), Key), TEXT("Fauna"),
		TArrayView<const FDynamicMesh3>(Lods, NumLods), TArrayView<const float>(D.LodScreenSize, NumLods), FVector::ZeroVector, Kind == EFaunaKind::Herd);
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
