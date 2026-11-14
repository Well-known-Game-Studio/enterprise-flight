#include "BuildingMeshes.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMeshToMeshDescription.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"

using namespace UE::Geometry;

namespace
{
	UStaticMesh* GMeshes[(int32)EBuildingKind::Count] = {};

	/** Triangles with a normal at each corner. Every triangle has vertices of its own, so a corner's normal can be
	 *  kept by vertex id through the turn of orientation at the end. Wound right-handed outward as given; the
	 *  winding is checked against the normals and corrected, so a shape can be written either way. */
	/** A part of a shape's collision: a box, or the convex hull of some points. */
	struct FPart
	{
		bool bBox = false;
		FVector3d Min, Max;
		TArray<FVector3d> Points;
	};

	struct FShape
	{
		FDynamicMesh3& M;
		TArray<FVector3f> VertexNormal;
		/** The collision, part by part, in the same metres. */
		TArray<FPart> Parts;
		explicit FShape(FDynamicMesh3& InM) : M(InM) {}
		void Tri(const FVector3d& A, const FVector3d& B, const FVector3d& C, const FVector3f& Na, const FVector3f& Nb, const FVector3f& Nc)
		{
			const FVector3d Geo = (B - A).Cross(C - A);
			if (Geo.SquaredLength() < 1e-18) { return; }
			const bool bFlip = Geo.Dot(FVector3d(Na + Nb + Nc)) < 0.0;
			const int32 Ia = M.AppendVertex(A);
			const int32 Ib = M.AppendVertex(bFlip ? C : B);
			const int32 Ic = M.AppendVertex(bFlip ? B : C);
			VertexNormal.SetNum(M.MaxVertexID());
			VertexNormal[Ia] = Na; VertexNormal[Ib] = bFlip ? Nc : Nb; VertexNormal[Ic] = bFlip ? Nb : Nc;
			M.AppendTriangle(Ia, Ib, Ic);
		}
		/** A flat triangle facing Out (any vector on its outer side). */
		void Flat(const FVector3d& A, const FVector3d& B, const FVector3d& C, const FVector3d& Out)
		{
			FVector3d N = (B - A).Cross(C - A);
			if (N.Dot(Out) < 0.0) { N = -N; }
			const FVector3f Nf = (FVector3f)Normalized(N);
			Tri(A, B, C, Nf, Nf, Nf);
		}
		void Quad(const FVector3d& A, const FVector3d& B, const FVector3d& C, const FVector3d& D, const FVector3d& Out)
		{
			Flat(A, B, C, Out);
			Flat(A, C, D, Out);
		}
		/** An axis-aligned box, flat-shaded. */
		void Box(const FVector3d& Min, const FVector3d& Max)
		{
			const FVector3d C = (Min + Max) * 0.5;
			const FVector3d P[8] = { { Min.X, Min.Y, Min.Z }, { Max.X, Min.Y, Min.Z }, { Max.X, Max.Y, Min.Z }, { Min.X, Max.Y, Min.Z },
				{ Min.X, Min.Y, Max.Z }, { Max.X, Min.Y, Max.Z }, { Max.X, Max.Y, Max.Z }, { Min.X, Max.Y, Max.Z } };
			const int32 F[6][4] = { { 0, 3, 2, 1 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 }, { 1, 2, 6, 5 }, { 2, 3, 7, 6 }, { 3, 0, 4, 7 } };
			for (int32 f = 0; f < 6; ++f)
			{
				const FVector3d Fc = (P[F[f][0]] + P[F[f][1]] + P[F[f][2]] + P[F[f][3]]) * 0.25;
				Quad(P[F[f][0]], P[F[f][1]], P[F[f][2]], P[F[f][3]], Fc - C);
			}
			FPart& Pt = Parts.AddDefaulted_GetRef(); Pt.bBox = true; Pt.Min = Min; Pt.Max = Max;
		}
		/** A roof prism over a box footprint: ridge along X at the top. */
		void Roof(const FVector3d& Min, const FVector3d& Max, double RidgeZ)
		{
			const double Ym = 0.5 * (Min.Y + Max.Y);
			const FVector3d A(Min.X, Min.Y, Max.Z), B(Max.X, Min.Y, Max.Z), Cc(Max.X, Max.Y, Max.Z), D(Min.X, Max.Y, Max.Z);
			const FVector3d R0(Min.X, Ym, RidgeZ), R1(Max.X, Ym, RidgeZ);
			Quad(A, B, R1, R0, FVector3d(0, -1, 1));
			Quad(Cc, D, R0, R1, FVector3d(0, 1, 1));
			Flat(A, R0, D, FVector3d(-1, 0, 0));
			Flat(B, Cc, R1, FVector3d(1, 0, 0));
			Parts.AddDefaulted_GetRef().Points = { A, B, Cc, D, R0, R1 };
		}
		/**
		 * A solid of revolution about Z from a profile of (radius, z) points, bottom to top, in Segments around, with
		 * Phase turning it. Smooth: normals from the profile's slope, averaged at each point (a dome, a pod) except where
		 * it turns sharply (a bulb's neck, a rim), which shades as a crease; not smooth: flat faces (a hut's wall, a
		 * spire's facets). Closed with flat caps where the profile does not reach the axis. Its collision is the hull of
		 * the whole, or of each band of HullBand profile steps (a shape that narrows and widens again).
		 */
		void Lathe(const TArray<FVector2d>& Prof, int32 Segments, bool bSmooth, double Phase = 0.0, int32 HullBand = 0)
		{
			const int32 N = Prof.Num();
			if (N < 2) { return; }
			// Each segment's outward normal, in (r, z): its tangent turned a quarter.
			TArray<FVector2d> Sn; Sn.SetNum(N - 1);
			for (int32 j = 0; j + 1 < N; ++j)
			{
				const FVector2d T = Prof[j + 1] - Prof[j];
				Sn[j] = Normalized(FVector2d(T.Y, -T.X));
			}
			// At a point, drawing segment j: the mean of the segments either side, or segment j's own where they turn
			// by more than about 37 degrees.
			auto PointNormal = [&](int32 i, int32 j)
			{
				const int32 A = FMath::Max(i - 1, 0), B = FMath::Min(i, N - 2);
				if (A == B || Sn[A].Dot(Sn[B]) < 0.8) { return Sn[j]; }
				return Normalized(Sn[A] + Sn[B]);
			};
			auto At = [&](int32 i, int32 s) { const double A = Phase + 2.0 * PI * s / Segments; return FVector3d(Prof[i].X * FMath::Cos(A), Prof[i].X * FMath::Sin(A), Prof[i].Y); };
			auto Nrm = [&](int32 i, int32 j, int32 s) { const FVector2d Pn = PointNormal(i, j); const double A = Phase + 2.0 * PI * s / Segments; return FVector3f((float)(Pn.X * FMath::Cos(A)), (float)(Pn.X * FMath::Sin(A)), (float)Pn.Y); };
			for (int32 i = 0; i + 1 < N; ++i)
			{
				for (int32 s = 0; s < Segments; ++s)
				{
					const FVector3d A = At(i, s), B = At(i, s + 1), C = At(i + 1, s + 1), D = At(i + 1, s);
					const FVector3d Mid = (A + B + C + D) * 0.25;
					const FVector3d Out(Mid.X, Mid.Y, 0.0);
					if (bSmooth)
					{
						Tri(A, B, C, Nrm(i, i, s), Nrm(i, i, s + 1), Nrm(i + 1, i, s + 1));
						Tri(A, C, D, Nrm(i, i, s), Nrm(i + 1, i, s + 1), Nrm(i + 1, i, s));
					}
					else
					{
						// A facet faces out along the profile's own slope there.
						const FVector3d O = Normalized(Out) * Sn[i].X + FVector3d(0, 0, Sn[i].Y);
						Quad(A, B, C, D, O);
					}
				}
			}
			const FVector3d Down(0, 0, -1), Up(0, 0, 1);
			if (Prof[0].X > 1e-6)
			{
				const FVector3d Cb(0, 0, Prof[0].Y);
				for (int32 s = 0; s < Segments; ++s) { Flat(Cb, At(0, s + 1), At(0, s), Down); }
			}
			if (Prof[N - 1].X > 1e-6)
			{
				const FVector3d Ct(0, 0, Prof[N - 1].Y);
				for (int32 s = 0; s < Segments; ++s) { Flat(Ct, At(N - 1, s), At(N - 1, s + 1), Up); }
			}
			// The collision: a hull of each band (sharing its end rings with the next), or of the whole.
			const int32 Band = HullBand > 0 ? HullBand : N - 1;
			for (int32 i0 = 0; i0 + 1 < N; i0 += Band)
			{
				FPart& Pt = Parts.AddDefaulted_GetRef();
				for (int32 i = i0; i <= FMath::Min(i0 + Band, N - 1); ++i)
				{
					if (Prof[i].X < 1e-6) { Pt.Points.Add(FVector3d(0, 0, Prof[i].Y)); continue; }
					for (int32 s = 0; s < Segments; ++s) { Pt.Points.Add(At(i, s)); }
				}
			}
		}
	};

	/** A profile sampled from a radius function over the unit height (u 0 at the base, 1 at the top). */
	TArray<FVector2d> Profile(int32 Steps, TFunctionRef<double(double)> Radius)
	{
		TArray<FVector2d> P;
		for (int32 i = 0; i <= Steps; ++i) { const double U = (double)i / Steps; P.Add(FVector2d(FMath::Max(Radius(U), 0.0), -0.5 + U)); }
		return P;
	}

	void Build(FDynamicMesh3& M, EBuildingKind Kind, TArray<FPart>* OutParts = nullptr)
	{
		M.EnableAttributes();
		FShape S(M);
		// In metres here, scaled to centimetres below.
		switch (Kind)
		{
		case EBuildingKind::Tower:
			S.Box(FVector3d(-0.5, -0.5, -0.5), FVector3d(0.5, 0.5, -0.05));
			S.Box(FVector3d(-0.38, -0.38, -0.05), FVector3d(0.38, 0.38, 0.3));
			S.Box(FVector3d(-0.26, -0.26, 0.3), FVector3d(0.26, 0.26, 0.5));
			break;
		case EBuildingKind::House:
			S.Box(FVector3d(-0.5, -0.5, -0.5), FVector3d(0.5, 0.5, 0.15));
			S.Roof(FVector3d(-0.5, -0.5, -0.5), FVector3d(0.5, 0.5, 0.15), 0.5);
			break;
		case EBuildingKind::Hut:
		{
			// A low round wall and a steep cone of thatch overhanging it a little (inside the unit box).
			S.Lathe({ FVector2d(0.44, -0.5), FVector2d(0.44, -0.08) }, 12, false);
			S.Lathe({ FVector2d(0.5, -0.14), FVector2d(0.0, 0.5) }, 12, false);
			break;
		}
		case EBuildingKind::Pueblo:
			// The ground storey, the second stepped back to the rear half, and a parapet round the lower roof.
			S.Box(FVector3d(-0.5, -0.5, -0.5), FVector3d(0.5, 0.5, -0.02));
			S.Box(FVector3d(-0.42, -0.05, -0.02), FVector3d(0.38, 0.5, 0.5));
			S.Box(FVector3d(-0.5, -0.5, -0.02), FVector3d(0.5, -0.44, 0.06));
			break;
		case EBuildingKind::RoundTower:
			S.Lathe({ FVector2d(0.42, -0.5), FVector2d(0.42, 0.38) }, 14, false);
			S.Lathe({ FVector2d(0.5, 0.38), FVector2d(0.5, 0.5) }, 14, false);
			break;
		case EBuildingKind::Works:
			// A hall half the height, a sawtooth of north lights over it, and a chimney at one end rising the full height.
			S.Box(FVector3d(-0.5, -0.5, -0.5), FVector3d(0.5, 0.5, -0.05));
			for (int32 k = 0; k < 3; ++k)
			{
				const double X0 = -0.5 + k / 3.0, X1 = X0 + 1.0 / 3.0;
				S.Quad(FVector3d(X0, -0.5, -0.05), FVector3d(X0, 0.5, -0.05), FVector3d(X1, 0.5, 0.12), FVector3d(X1, -0.5, 0.12), FVector3d(-0.6, 0, 1));
				S.Quad(FVector3d(X1, -0.5, 0.12), FVector3d(X1, 0.5, 0.12), FVector3d(X1, 0.5, -0.05), FVector3d(X1, -0.5, -0.05), FVector3d(1, 0, 0));
				S.Flat(FVector3d(X0, -0.5, -0.05), FVector3d(X1, -0.5, 0.12), FVector3d(X1, -0.5, -0.05), FVector3d(0, -1, 0));
				S.Flat(FVector3d(X0, 0.5, -0.05), FVector3d(X1, 0.5, -0.05), FVector3d(X1, 0.5, 0.12), FVector3d(0, 1, 0));
			}
			{
				// The sawtooth's collision: its envelope.
				FPart& Pt = S.Parts.AddDefaulted_GetRef(); Pt.bBox = true; Pt.Min = FVector3d(-0.5, -0.5, -0.05); Pt.Max = FVector3d(0.5, 0.5, 0.12);
			}
			S.Lathe({ FVector2d(0.07, -0.05), FVector2d(0.05, 0.5) }, 10, true, 0.0);
			break;
		case EBuildingKind::Spire:
			S.Lathe(Profile(6, [](double U) { return U < 0.85 ? 0.46 * (1.0 - U) + 0.04 : 0.115 * (1.0 - (U - 0.85) / 0.15); }), 6, false, PI / 6.0);
			break;
		case EBuildingKind::Dome:
			S.Lathe(Profile(16, [](double U) { return 0.5 * FMath::Sqrt(FMath::Max(1.0 - U * U, 0.0)); }), 24, true);
			break;
		case EBuildingKind::Pod:
			// An egg, broader below, its foot flattened where it grows from the ground.
			S.Lathe(Profile(18, [](double U) { const double V = 0.08 + 0.92 * U; return 0.5 * FMath::Pow(FMath::Sin(PI * V), 0.75) * (1.0 - 0.25 * V); }), 20, true);
			break;
		case EBuildingKind::BulbTower:
			// Three bulbs on a stem, shrinking upward, the stem showing between them: the widest at the foot, its base
			// broadened where it grows from the ground. The collision is a hull per band, bulb by bulb.
			S.Lathe(Profile(48, [](double U)
			{
				auto Bulb = [U](double C, double H, double Rad) { const double T = (U - C) / H; return FMath::Abs(T) < 1.0 ? Rad * FMath::Sqrt(1.0 - T * T) : 0.0; };
				const double Foot = U < 0.18 ? 0.5 * FMath::Sqrt(1.0 - FMath::Square((U - 0.18) / 0.18 * 0.8)) : 0.0;
				const double Stem = 0.07 * (1.0 - FMath::Max(U - 0.96, 0.0) / 0.04);
				return FMath::Max(FMath::Max(FMath::Max(Bulb(0.18, 0.18, 0.5), Foot), FMath::Max(Bulb(0.55, 0.14, 0.36), Bulb(0.83, 0.11, 0.24))), Stem);
			}), 18, true, 0.0, 4);
			break;
		case EBuildingKind::Fire:
			// A low ring of stones and a cone of flame in it.
			S.Lathe({ FVector2d(0.5, -0.5), FVector2d(0.5, -0.38), FVector2d(0.36, -0.38) }, 10, false);
			S.Lathe(Profile(6, [](double U) { return 0.36 * FMath::Pow(1.0 - U, 1.3); }), 10, true);
			// No collision: the crew walk round it, or through.
			S.Parts.Reset();
			break;
		default:
			S.Box(FVector3d(-0.5, -0.5, -0.5), FVector3d(0.5, 0.5, 0.5));
			break;
		}
		for (int32 V : M.VertexIndicesItr()) { M.SetVertex(V, M.GetVertex(V) * 100.0); }
		// The faces above are wound outward by the right-hand rule, but GeometryCore's triangle normal is
		// (C - A) x (B - A), the left-handed reading, and the renderer shows the side that normal faces: as
		// written every building drew its inside. Turn them so each face's normal leaves the building.
		M.ReverseOrientation(false);
		// The normals as given at each corner (by vertex: no vertex is shared, so the turn keeps them).
		FDynamicMeshNormalOverlay* N = M.Attributes()->PrimaryNormals();
		for (int32 T : M.TriangleIndicesItr())
		{
			const FIndex3i Tv = M.GetTriangle(T);
			const int32 E0 = N->AppendElement(S.VertexNormal[Tv.A]), E1 = N->AppendElement(S.VertexNormal[Tv.B]), E2 = N->AppendElement(S.VertexNormal[Tv.C]);
			N->SetTriangle(T, FIndex3i(E0, E1, E2));
		}
		if (OutParts) { *OutParts = MoveTemp(S.Parts); }
	}
}

const TCHAR* SlonkBuildings::KindName(EBuildingKind Kind)
{
	static const TCHAR* Names[(int32)EBuildingKind::Count] = { TEXT("Block"), TEXT("Tower"), TEXT("House"), TEXT("Hut"), TEXT("Pueblo"), TEXT("RoundTower"),
		TEXT("Works"), TEXT("Spire"), TEXT("Dome"), TEXT("Pod"), TEXT("BulbTower"), TEXT("Fire"), TEXT("Footing") };
	return (int32)Kind < (int32)EBuildingKind::Count ? Names[(int32)Kind] : TEXT("?");
}

UStaticMesh* SlonkBuildings::GetMesh(EBuildingKind Kind)
{
	const int32 K = (int32)Kind;
	if (K < 0 || K >= (int32)EBuildingKind::Count) { return nullptr; }
	if (GMeshes[K]) { return GMeshes[K]; }
	FDynamicMesh3 Dm;
	TArray<FPart> Parts;
	Build(Dm, Kind, &Parts);
	FMeshDescription MD;
	FStaticMeshAttributes(MD).Register();
	FDynamicMeshToMeshDescription Converter;
	Converter.Convert(&Dm, MD, false);
	UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(), *FString::Printf(TEXT("SlonkBuilding%d"), K), RF_Transient);
	Mesh->AddToRoot();
	Mesh->bAllowCPUAccess = true;
	FStaticMaterial Mat(nullptr, TEXT("Walls"));
#if WITH_EDITORONLY_DATA
	Mat.ImportedMaterialSlotName = TEXT("Walls");
#endif
	Mesh->GetStaticMaterials().Add(Mat);
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bFastBuild = true;
	Params.bAllowCpuAccess = true;
	Params.bMarkPackageDirty = false;
	Params.bCommitMeshDescription = false;
	Params.bBuildSimpleCollision = false;
	Mesh->BuildFromMeshDescriptions({ &MD }, Params);
	Mesh->CreateBodySetup();
	if (UBodySetup* BS = Mesh->GetBodySetup())
	{
		// A box or a convex hull per part, in cm.
		for (const FPart& Pt : Parts)
		{
			if (Pt.bBox)
			{
				const FVector3d Size = (Pt.Max - Pt.Min) * 100.0;
				FKBoxElem B((float)Size.X, (float)Size.Y, (float)Size.Z);
				B.Center = FVector((Pt.Min + Pt.Max) * 50.0);
				BS->AggGeom.BoxElems.Add(B);
				continue;
			}
			FKConvexElem C;
			for (const FVector3d& V : Pt.Points) { C.VertexData.Add(FVector(V * 100.0)); }
			C.UpdateElemBox();
			BS->AggGeom.ConvexElems.Add(MoveTemp(C));
		}
		BS->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		BS->CreatePhysicsMeshes();
	}
	GMeshes[K] = Mesh;
	return Mesh;
}

void SlonkBuildings::ShapeStats(EBuildingKind Kind, double& OutVolume, double& OutWorstFacing, int32& OutCollisionParts)
{
	FDynamicMesh3 Dm;
	TArray<FPart> Parts;
	Build(Dm, Kind, &Parts);
	OutCollisionParts = Parts.Num();
	OutVolume = 0.0;
	OutWorstFacing = 1.0;
	const FDynamicMeshNormalOverlay* N = Dm.Attributes()->PrimaryNormals();
	for (int32 T : Dm.TriangleIndicesItr())
	{
		// The face's normal as the mesh reads its winding (the side the renderer shows), against its corners' normals.
		FVector3d Fn, Centroid; double Area;
		Dm.GetTriInfo(T, Fn, Area, Centroid);
		OutVolume += Area * Fn.Dot(Centroid) / 3.0 / 1.0e6;
		const FIndex3i E = N->GetTriangle(T);
		const FVector3d Corner = Normalized(FVector3d(N->GetElement(E.A) + N->GetElement(E.B) + N->GetElement(E.C)));
		OutWorstFacing = FMath::Min(OutWorstFacing, Fn.Dot(Corner));
	}
}
