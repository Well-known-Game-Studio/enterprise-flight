#include "LandmarkMeshes.h"

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "ProcMeshBuild.h"
#include "UObject/Package.h"

using namespace UE::Geometry;

namespace
{
	/** The meshes made, by kind. */
	TArray<UStaticMesh*> GLandmarkMeshes;

	/** Lumps a rock's or a growth's surface is pushed in and out by: a smooth field of the place itself, so every level
	 *  of detail is pushed alike and they agree in outline. About -1 to 1. */
	double LandmarkLumps(const FVector& P, double Scale)
	{
		const FVector Q = P * Scale;
		return 0.6 * FMath::Sin(Q.X * 1.31 + 1.3) * FMath::Sin(Q.Y * 1.07 + 2.1) * FMath::Cos(Q.Z * 0.93 + 0.7)
			+ 0.4 * FMath::Sin(Q.X * 2.9 + Q.Z * 1.7 + 4.2) * FMath::Cos(Q.Y * 3.3 - Q.Z * 2.1 + 0.4);
	}

	/** One closed solid being made of rings of points joined ring to ring. */
	struct FLandmarkSolid
	{
		FDynamicMesh3 Mesh;
		int32 Ring(const TArray<FVector>& Pts)
		{
			const int32 First = Mesh.MaxVertexID();
			for (const FVector& P : Pts) { Mesh.AppendVertex(P); }
			return First;
		}
		void Join(int32 A, int32 B, int32 N)
		{
			for (int32 s = 0; s < N; ++s)
			{
				const int32 s1 = (s + 1) % N;
				Mesh.AppendTriangle(A + s, A + s1, B + s1);
				Mesh.AppendTriangle(A + s, B + s1, B + s);
			}
		}
		/** A ring closed over by a fan to a point; bFlip for the cap at a solid's first ring. */
		void Cap(int32 R, int32 N, const FVector& Centre, bool bFlip)
		{
			const int32 C = Mesh.AppendVertex(Centre);
			for (int32 s = 0; s < N; ++s)
			{
				const int32 s1 = (s + 1) % N;
				if (bFlip) { Mesh.AppendTriangle(C, R + s1, R + s); }
				else { Mesh.AppendTriangle(C, R + s, R + s1); }
			}
		}
		/** Wound outward whichever way its rings ran: by its faces' own normals against their places, which sum to
		 *  three times its volume over a closed solid whose faces look out of it, and to less than nothing otherwise. */
		void FaceOutward()
		{
			double Sum = 0.0;
			for (const int32 Tid : Mesh.TriangleIndicesItr())
			{
				const FIndex3i T = Mesh.GetTriangle(Tid);
				const FVector A = Mesh.GetVertex(T.A), B = Mesh.GetVertex(T.B), C = Mesh.GetVertex(T.C);
				Sum += FVector::CrossProduct(B - A, C - A).Size() * 0.5 * FVector::DotProduct(FVector(Mesh.GetTriNormal(Tid)), (A + B + C) / 3.0);
			}
			if (Sum < 0.0) { Mesh.ReverseOrientation(false); }
		}
	};

	/** Solids gathered into one mesh, each faced outward first. */
	void LandmarkAppend(FDynamicMesh3& Out, FLandmarkSolid& Solid)
	{
		Solid.FaceOutward();
		TArray<int32> Map;
		Map.Init(-1, Solid.Mesh.MaxVertexID());
		for (const int32 Vid : Solid.Mesh.VertexIndicesItr()) { Map[Vid] = Out.AppendVertex(Solid.Mesh.GetVertex(Vid)); }
		for (const int32 Tid : Solid.Mesh.TriangleIndicesItr())
		{
			const FIndex3i T = Solid.Mesh.GetTriangle(Tid);
			Out.AppendTriangle(Map[T.A], Map[T.B], Map[T.C]);
		}
	}

	/** How finely a level is made: 0 the finest. */
	int32 LandmarkPick(int32 Lod, int32 Fine, int32 Mid, int32 Coarse) { return Lod <= 0 ? Fine : (Lod == 1 ? Mid : Coarse); }

	// ---- the arch: a span of rock thick at its feet and thin over its crown, its legs run on under the ground ----

	/** How far the arch's legs run on under its origin, and how far its middle line wanders out of its plane. */
	constexpr double ArchBelow = 24.0;
	double ArchSway(double A) { return 3.5 * FMath::Sin(2.2 * A + 0.6); }

	/** The arch's middle line at T, 0 at one foot's bottom to 1 at the other's, and how thick it is there. */
	void ArchLine(double T, FVector& OutCentre, FVector& OutAlong, double& OutRadius)
	{
		// A leg under the ground, the arc (lopsided, as a real one is: it leans along its span, wanders a little out of
		// its own plane, and one abutment is the heavier), and the other leg.
		const double LegShare = 0.1;
		auto ArcAt = [](double A)
		{
			return FVector(-38.0 * FMath::Cos(A) + 5.0 * FMath::Square(FMath::Sin(A)), ArchSway(A), 46.0 * FMath::Sin(A) * (1.0 + 0.08 * FMath::Cos(A)));
		};
		auto Heavy = [](double A) { return 1.0 + 0.15 * FMath::Cos(A); };
		if (T < LegShare)
		{
			const double F = T / LegShare;
			OutCentre = FVector(-38.0, ArchSway(0.0), FMath::Lerp(-ArchBelow, 0.0, F));
			OutAlong = FVector::ZAxisVector;
			OutRadius = FMath::Lerp(12.5, 11.5, F) * Heavy(0.0);
			return;
		}
		if (T > 1.0 - LegShare)
		{
			const double F = (1.0 - T) / LegShare;
			OutCentre = FVector(38.0, ArchSway(UE_DOUBLE_PI), FMath::Lerp(-ArchBelow, 0.0, F));
			OutAlong = -FVector::ZAxisVector;
			OutRadius = FMath::Lerp(12.5, 11.5, F) * Heavy(UE_DOUBLE_PI);
			return;
		}
		const double A = UE_DOUBLE_PI * (T - LegShare) / (1.0 - 2.0 * LegShare);
		OutCentre = ArcAt(A);
		OutAlong = (ArcAt(A + 0.01) - ArcAt(A - 0.01)).GetSafeNormal();
		OutRadius = FMath::Lerp(11.5, 6.5, FMath::Pow(FMath::Max(FMath::Sin(A), 0.0), 0.8)) * Heavy(A);
	}

	void ArchRing(double T, int32 Sides, TArray<FVector>& Out)
	{
		FVector C, Along;
		double Rad;
		ArchLine(T, C, Along, Rad);
		// Across the span in its own plane, and through it. A slab of rock more than a tube: its section squared off (a
		// round whose corners stand out an eighth), narrower through than it is deep, and worn in and out along its
		// length by two sizes of lump.
		const FVector In(-Along.Z, 0.0, Along.X);
		Out.Reset();
		for (int32 s = 0; s < Sides; ++s)
		{
			const double A = UE_DOUBLE_TWO_PI * s / Sides;
			const double Ca = FMath::Cos(A), Sa = FMath::Sin(A);
			const double Square = FMath::Pow(FMath::Pow(FMath::Abs(Ca), 3.0) + FMath::Pow(FMath::Abs(Sa), 3.0), -1.0 / 3.0);
			const FVector D = (In * Ca + FVector::YAxisVector * (Sa * 0.8)) * Square;
			const FVector P = C + D * Rad;
			Out.Add(C + D * (Rad * (1.0 + 0.22 * LandmarkLumps(P, 0.07) + 0.09 * LandmarkLumps(P + FVector(31.0, 17.0, 5.0), 0.21))));
		}
	}

	void LandmarkArch(int32 Lod, FDynamicMesh3& Out)
	{
		const int32 Rings = LandmarkPick(Lod, 30, 16, 9), Sides = LandmarkPick(Lod, 14, 9, 6);
		FLandmarkSolid S;
		TArray<FVector> Pts;
		int32 Prev = -1;
		for (int32 r = 0; r < Rings; ++r)
		{
			ArchRing((double)r / (Rings - 1), Sides, Pts);
			const int32 Here = S.Ring(Pts);
			if (r == 0) { S.Cap(Here, Sides, FVector(-38.0, ArchSway(0.0), -ArchBelow), true); }
			else { S.Join(Prev, Here, Sides); }
			Prev = Here;
		}
		S.Cap(Prev, Sides, FVector(38.0, ArchSway(UE_DOUBLE_PI), -ArchBelow), false);
		LandmarkAppend(Out, S);
	}

	// ---- the standing stone: a worked slab, tapering, its top cut on a slant ----

	void MonolithRing(double Z, bool bChamfered, TArray<FVector>& Out)
	{
		// From its foot under the ground to its top: narrower as it rises, leaning a hair, turned a few degrees.
		const double F = FMath::Clamp((Z + 8.0) / 108.0, 0.0, 1.0);
		const double A = FMath::Lerp(11.5, 8.2, F), B = FMath::Lerp(6.5, 4.5, F);
		const double Turn = FMath::DegreesToRadians(4.0 * F), Lean = 1.5 * F;
		const double Ch = bChamfered ? 0.14 : 0.0;
		const FVector2D Corner[8] = { { A * (1 - Ch), -B }, { A, -B * (1 - Ch) }, { A, B * (1 - Ch) }, { A * (1 - Ch), B }, { -A * (1 - Ch), B }, { -A, B * (1 - Ch) }, { -A, -B * (1 - Ch) }, { -A * (1 - Ch), -B } };
		Out.Reset();
		for (int32 k = 0; k < 8; ++k)
		{
			if (!bChamfered && (k & 1)) { continue; }
			const double X = Corner[k].X * FMath::Cos(Turn) - Corner[k].Y * FMath::Sin(Turn) + Lean, Y = Corner[k].X * FMath::Sin(Turn) + Corner[k].Y * FMath::Cos(Turn);
			// The top's slant: one long edge 6 lower than the other.
			Out.Add(FVector(X, Y, Z >= 99.0 ? Z - 6.0 * (0.5 + 0.5 * Corner[k].X / A) : Z));
		}
	}

	void LandmarkMonolith(int32 Lod, FDynamicMesh3& Out)
	{
		const bool bChamfered = Lod == 0;
		const int32 Sides = bChamfered ? 8 : 4;
		const double Heights[] = { -8.0, 0.0, 34.0, 68.0, 100.0 };
		FLandmarkSolid S;
		TArray<FVector> Pts;
		int32 Prev = -1;
		const int32 NumHeights = UE_ARRAY_COUNT(Heights);
		for (int32 r = 0; r < NumHeights; ++r)
		{
			MonolithRing(Heights[r], bChamfered, Pts);
			const int32 Here = S.Ring(Pts);
			if (r == 0) { S.Cap(Here, Sides, FVector(0.0, 0.0, -8.0), true); }
			else { S.Join(Prev, Here, Sides); }
			Prev = Here;
		}
		S.Cap(Prev, Sides, FVector(1.5, 0.0, 97.0), false);
		LandmarkAppend(Out, S);
	}

	// ---- the ring: a worked band stood on edge, its lowest fifth under the ground ----

	constexpr double LandmarkRingCentreZ = 28.0, LandmarkRingRadius = 45.5;

	void LandmarkRingSection(double Angle, bool bChamfered, TArray<FVector>& Out)
	{
		// Round the ring in its own plane (x and z), its band 9 through from inside to out and 7 across.
		const FVector Outward(FMath::Cos(Angle), 0.0, FMath::Sin(Angle));
		const FVector Centre = FVector(0.0, 0.0, LandmarkRingCentreZ) + Outward * LandmarkRingRadius;
		const double A = 4.5, B = 3.5, Ch = bChamfered ? 0.25 : 0.0;
		const FVector2D Corner[8] = { { A * (1 - Ch), -B }, { A, -B * (1 - Ch) }, { A, B * (1 - Ch) }, { A * (1 - Ch), B }, { -A * (1 - Ch), B }, { -A, B * (1 - Ch) }, { -A, -B * (1 - Ch) }, { -A * (1 - Ch), -B } };
		Out.Reset();
		for (int32 k = 0; k < 8; ++k)
		{
			if (!bChamfered && (k & 1)) { continue; }
			Out.Add(Centre + Outward * Corner[k].X + FVector::YAxisVector * Corner[k].Y);
		}
	}

	void LandmarkRing(int32 Lod, FDynamicMesh3& Out)
	{
		const bool bChamfered = Lod < 2;
		const int32 Sides = bChamfered ? 8 : 4, Round = LandmarkPick(Lod, 48, 28, 14);
		FLandmarkSolid S;
		TArray<FVector> Pts;
		int32 First = -1, Prev = -1;
		for (int32 r = 0; r < Round; ++r)
		{
			LandmarkRingSection(UE_DOUBLE_TWO_PI * r / Round, bChamfered, Pts);
			const int32 Here = S.Ring(Pts);
			if (r == 0) { First = Here; }
			else { S.Join(Prev, Here, Sides); }
			Prev = Here;
		}
		S.Join(Prev, First, Sides);
		LandmarkAppend(Out, S);
	}

	// ---- turned shapes: a reef's fluted towers, a mound city's spires ----

	/** One turned solid about a vertical through (Cx, Cy): rings at the heights given, each of a radius the caller gives
	 *  for the height and the way round; closed under its first ring and over its last (to TopZ at its middle). */
	template <typename RadiusAt>
	void LandmarkTurned(FDynamicMesh3& Out, double Cx, double Cy, const TArray<double>& Heights, int32 Sides, double TopZ, RadiusAt&& Radius)
	{
		FLandmarkSolid S;
		TArray<FVector> Pts;
		int32 Prev = -1;
		for (int32 r = 0; r < Heights.Num(); ++r)
		{
			Pts.Reset();
			for (int32 s = 0; s < Sides; ++s)
			{
				const double A = UE_DOUBLE_TWO_PI * s / Sides;
				const double Rad = Radius(Heights[r], A);
				Pts.Add(FVector(Cx + Rad * FMath::Cos(A), Cy + Rad * FMath::Sin(A), Heights[r]));
			}
			const int32 Here = S.Ring(Pts);
			if (r == 0) { S.Cap(Here, Sides, FVector(Cx, Cy, Heights[0]), true); }
			else { S.Join(Prev, Here, Sides); }
			Prev = Here;
		}
		S.Cap(Prev, Sides, FVector(Cx, Cy, TopZ), false);
		LandmarkAppend(Out, S);
	}

	/** A reef's towers: where each stands from the cluster's middle, how tall over the sea, how thick, and how many flutes. */
	struct FReefTower { double X, Y, Top, Radius; int32 Flutes; };
	constexpr FReefTower ReefTowers[] = { { 0.0, 0.0, 100.0, 10.0, 7 }, { 17.0, 6.0, 74.0, 8.0, 6 }, { -9.0, 15.0, 58.0, 7.0, 5 }, { 4.0, -16.0, 41.0, 6.0, 6 } };
	constexpr double ReefFootZ = -45.0;

	/** A tower's radius at a height and a way round: flared at its foot on the sea's floor, drawn in up its length,
	 *  swelling a little as it goes, flared again at its lip; fluted all the way, the flutes turning as they rise. */
	double ReefRadius(const FReefTower& T, double Z, double Angle, bool bFluted)
	{
		const double F = FMath::Clamp((Z - ReefFootZ) / (T.Top - ReefFootZ), 0.0, 1.0);
		const double Sm = F * F * (3.0 - 2.0 * F);
		double R = T.Radius * (FMath::Lerp(1.5, 0.72, Sm) + 0.07 * FMath::Sin(F * 5.0 * UE_DOUBLE_PI) + 0.22 * FMath::Square(FMath::Max(F - 0.9, 0.0) / 0.1));
		if (bFluted) { R *= 1.0 + 0.16 * FMath::Cos(T.Flutes * Angle + F * 2.4); }
		return R;
	}

	void LandmarkReef(int32 Lod, FDynamicMesh3& Out)
	{
		const int32 Rings = LandmarkPick(Lod, 14, 8, 5);
		for (const FReefTower& T : ReefTowers)
		{
			const bool bFluted = Lod < 2;
			const int32 Sides = Lod == 0 ? 3 * T.Flutes : (Lod == 1 ? 2 * T.Flutes : 6);
			TArray<double> Heights;
			for (int32 r = 0; r < Rings; ++r) { Heights.Add(FMath::Lerp(ReefFootZ, T.Top, (double)r / (Rings - 1))); }
			// Its top a cup: the middle of its lid a little under its lip.
			LandmarkTurned(Out, T.X, T.Y, Heights, Sides, T.Top - 0.5 * T.Radius, [&T, bFluted](double Z, double A) { return ReefRadius(T, Z, A, bFluted); });
		}
	}

	/** A mound city's spires, and the low swell of earth they stand on. */
	struct FMoundSpire { double X, Y, Top, Radius; };
	constexpr FMoundSpire MoundSpires[] = { { 0.0, 0.0, 100.0, 17.0 }, { 20.0, 8.0, 62.0, 11.0 }, { -16.0, 14.0, 48.0, 10.0 }, { 6.0, -21.0, 40.0, 9.0 }, { -12.0, -13.0, 30.0, 8.0 } };
	constexpr double MoundFootZ = -8.0;

	/** A spire's radius at a height and a way round: broad at its foot, drawn up in tiers to a blunt tip, lumpy. */
	double MoundRadius(const FMoundSpire& M, double Z, double Angle)
	{
		const double F = FMath::Clamp((Z - MoundFootZ) / (M.Top - MoundFootZ), 0.0, 1.0);
		const double R = M.Radius * (FMath::Pow(1.0 - F, 0.75) * (1.0 + 0.12 * FMath::Sin(F * 7.0 * UE_DOUBLE_PI)) + 0.06);
		const FVector P(M.X + R * FMath::Cos(Angle), M.Y + R * FMath::Sin(Angle), Z);
		return R * (1.0 + 0.13 * LandmarkLumps(P, 0.14));
	}

	void LandmarkMound(int32 Lod, FDynamicMesh3& Out)
	{
		const int32 Rings = LandmarkPick(Lod, 10, 6, 4), Sides = LandmarkPick(Lod, 14, 9, 6);
		for (const FMoundSpire& M : MoundSpires)
		{
			TArray<double> Heights;
			for (int32 r = 0; r < Rings; ++r) { Heights.Add(FMath::Lerp(MoundFootZ, M.Top - 1.5, FMath::Pow((double)r / (Rings - 1), 0.9))); }
			LandmarkTurned(Out, M.X, M.Y, Heights, Sides, M.Top, [&M](double Z, double A) { return MoundRadius(M, Z, A); });
		}
		// The swell they stand on: a low dome 34 across its half, 9 high.
		TArray<double> Heights;
		const int32 DomeRings = LandmarkPick(Lod, 5, 4, 3);
		for (int32 r = 0; r < DomeRings; ++r) { Heights.Add(FMath::Lerp(MoundFootZ, 8.0, (double)r / (DomeRings - 1))); }
		LandmarkTurned(Out, 0.0, 0.0, Heights, LandmarkPick(Lod, 18, 12, 8), 9.0, [](double Z, double A)
		{
			const double F = FMath::Clamp((Z - MoundFootZ) / (9.0 - MoundFootZ), 0.0, 1.0);
			const double R = 34.0 * FMath::Sqrt(FMath::Max(1.0 - F * F, 0.0)) + 1.0;
			return R * (1.0 + 0.08 * LandmarkLumps(FVector(R * FMath::Cos(A), R * FMath::Sin(A), Z), 0.09));
		});
	}

	/** How sharp an edge has to be for a kind's faces to be lit apart across it: worked stone keeps its edges. */
	float LandmarkCreaseDeg(ELandmark Kind) { return Kind == ELandmark::Monolith || Kind == ELandmark::Ring ? 25.f : 42.f; }
}

int32 SlonkLandmarks::MaxTriangles(int32 Lod)
{
	return Lod <= 0 ? 2600 : (Lod == 1 ? 1100 : 320);
}

double SlonkLandmarks::BelowCm(ELandmark Kind)
{
	switch (Kind)
	{
	case ELandmark::Arch: return ArchBelow;
	case ELandmark::Monolith: return 8.0;
	case ELandmark::Ring: return LandmarkRingRadius + 4.5 - LandmarkRingCentreZ;
	case ELandmark::ReefTower: return -ReefFootZ;
	default: return -MoundFootZ;
	}
}

void SlonkLandmarks::BuildLod(ELandmark Kind, int32 Lod, FDynamicMesh3& Out)
{
	Out.Clear();
	const int32 L = FMath::Clamp(Lod, 0, NumLods - 1);
	switch (Kind)
	{
	case ELandmark::Arch: LandmarkArch(L, Out); break;
	case ELandmark::Monolith: LandmarkMonolith(L, Out); break;
	case ELandmark::Ring: LandmarkRing(L, Out); break;
	case ELandmark::ReefTower: LandmarkReef(L, Out); break;
	default: LandmarkMound(L, Out); break;
	}
	// The material lays its rock in the mesh's own frame; the UVs only satisfy the mesh build.
	Out.EnableAttributes();
	FDynamicMeshUVOverlay* UV = Out.Attributes()->PrimaryUV();
	for (const int32 Vid : Out.VertexIndicesItr()) { UV->AppendElement(FVector2f(0.f, 0.f)); }
	for (const int32 Tid : Out.TriangleIndicesItr()) { UV->SetTriangle(Tid, Out.GetTriangle(Tid)); }
	FMeshNormals::InitializeOverlayTopologyFromOpeningAngle(&Out, Out.Attributes()->PrimaryNormals(), LandmarkCreaseDeg(Kind));
	FMeshNormals::QuickRecomputeOverlayNormals(Out);
}

void SlonkLandmarks::CollisionParts(ELandmark Kind, TArray<TArray<FVector>>& OutHulls)
{
	OutHulls.Reset();
	TArray<FVector> A, B;
	switch (Kind)
	{
	case ELandmark::Arch:
	{
		// Eight lengths of the span, each the hull of the rings at its ends: the way under the crown stays open.
		const int32 Rings = 9, Sides = 6;
		for (int32 r = 0; r + 1 < Rings; ++r)
		{
			ArchRing((double)r / (Rings - 1), Sides, A);
			ArchRing((double)(r + 1) / (Rings - 1), Sides, B);
			A.Append(B);
			OutHulls.Add(A);
		}
		break;
	}
	case ELandmark::Monolith:
	{
		TArray<FVector> Hull;
		for (const double Z : { -8.0, 34.0, 100.0 }) { MonolithRing(Z, false, A); Hull.Append(A); }
		OutHulls.Add(MoveTemp(Hull));
		break;
	}
	case ELandmark::Ring:
	{
		// Fourteen lengths of the round: its middle stays open, to walk or fly through.
		const int32 Round = 14;
		for (int32 r = 0; r < Round; ++r)
		{
			LandmarkRingSection(UE_DOUBLE_TWO_PI * r / Round, false, A);
			LandmarkRingSection(UE_DOUBLE_TWO_PI * (r + 1) / Round, false, B);
			A.Append(B);
			OutHulls.Add(A);
		}
		break;
	}
	case ELandmark::ReefTower:
		for (const FReefTower& T : ReefTowers)
		{
			TArray<FVector> Hull;
			for (int32 r = 0; r < 4; ++r)
			{
				const double Z = FMath::Lerp(ReefFootZ, T.Top, r / 3.0);
				for (int32 s = 0; s < 6; ++s) { const double Ang = UE_DOUBLE_TWO_PI * s / 6; const double R = ReefRadius(T, Z, Ang, false); Hull.Add(FVector(T.X + R * FMath::Cos(Ang), T.Y + R * FMath::Sin(Ang), Z)); }
			}
			OutHulls.Add(MoveTemp(Hull));
		}
		break;
	default:
		for (const FMoundSpire& M : MoundSpires)
		{
			TArray<FVector> Hull;
			for (int32 r = 0; r < 3; ++r)
			{
				const double Z = FMath::Lerp(MoundFootZ, M.Top - 1.5, r / 2.0);
				for (int32 s = 0; s < 6; ++s) { const double Ang = UE_DOUBLE_TWO_PI * s / 6; const double R = MoundRadius(M, Z, Ang); Hull.Add(FVector(M.X + R * FMath::Cos(Ang), M.Y + R * FMath::Sin(Ang), Z)); }
			}
			Hull.Add(FVector(M.X, M.Y, M.Top));
			OutHulls.Add(MoveTemp(Hull));
		}
		break;
	}
}

UStaticMesh* SlonkLandmarks::GetMesh(ELandmark Kind)
{
	check(IsInGameThread());
	const int32 K = FMath::Clamp((int32)Kind, 0, NumKinds - 1);
	if (GLandmarkMeshes.Num() < NumKinds) { GLandmarkMeshes.SetNumZeroed(NumKinds); }
	if (GLandmarkMeshes[K] && IsValid(GLandmarkMeshes[K])) { return GLandmarkMeshes[K]; }

	FDynamicMesh3 Built[NumLods];
	for (int32 L = 0; L < NumLods; ++L) { BuildLod((ELandmark)K, L, Built[L]); }
	// The GPU picks each instance's level by its size on the screen: a hundred-metre arch keeps its finest to about a
	// kilometre and a half, and is down to its coarsest by five.
	const float ScreenSizes[NumLods] = { 1.f, 0.2f, 0.05f };
	UStaticMesh* Mesh = SlonkProcMesh::MakeStaticMesh(*FString::Printf(TEXT("SlonkLandmark%d"), K), TEXT("Rock"), TArrayView<const FDynamicMesh3>(Built, NumLods), ScreenSizes, FVector::ZeroVector);
	// Convex collision by its parts: the crew walk into it, stand on it, and pass under an arch and through a ring.
	Mesh->CreateBodySetup();
	if (UBodySetup* BS = Mesh->GetBodySetup())
	{
		TArray<TArray<FVector>> Hulls;
		CollisionParts((ELandmark)K, Hulls);
		for (TArray<FVector>& Points : Hulls)
		{
			FKConvexElem Hull;
			Hull.VertexData = MoveTemp(Points);
			Hull.UpdateElemBox();
			BS->AggGeom.ConvexElems.Add(Hull);
		}
		BS->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		BS->CreatePhysicsMeshes();
	}
	GLandmarkMeshes[K] = Mesh;
	return Mesh;
}
