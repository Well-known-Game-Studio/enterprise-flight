#include "PlantMeshes.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "ProcMeshBuild.h"

using namespace UE::Geometry;

namespace
{
	TArray<UStaticMesh*> GPlants;

	using SlonkProcMesh::FBuild;
	// Vertex colour alpha: the surface kind the material masks by.
	using SlonkProcMesh::SolidSurface;
	constexpr float LeafCard = 0.4f, NeedleCard = 1.f;
	/** The screen size at which a tree goes to its far crown: the engine's, the diameter over the screen's of the sphere
	 *  round the box the GPU bounds an instance by (UPlanetInstancesComponent's, padded for the wind: about 1.27 of the
	 *  tree's height across), so a 15 m tree about 2 km off, 6-7 pixels tall. slonk.Plants.FarCrownScreenSize overrides
	 *  it if set before the first world (the meshes are made once a process); it is kept between the screen sizes the
	 *  engine can tell apart from none (1/1024) and the LOD before's. */
	float FarCrownScreenSize = 0.016f;
	FAutoConsoleVariableRef CVarFarCrownScreenSize(TEXT("slonk.Plants.FarCrownScreenSize"), FarCrownScreenSize,
		TEXT("Screen size at which a tree's mesh goes to its solid far crown (0.002-0.06; read when the plant meshes are made, once a process)"));

	/** A random direction with a lean toward the sky: crowns are lit and seen from above. */
	FVector Upward(FRandomStream& Rng, double Lean)
	{
		FVector D = Rng.GetUnitVector();
		D.Z = FMath::Lerp(D.Z, FMath::Abs(D.Z), Lean);
		return D.GetSafeNormal();
	}

	/** Cards scattered through an ellipsoid of leaves, facing out of it. */
	void Crown(FBuild& B, FRandomStream& Rng, const FVector& Centre, const FVector& Radii, int32 Cards, double Size, const FVector& MassCentre, float Rand)
	{
		for (int32 i = 0; i < Cards; ++i)
		{
			const FVector D = Upward(Rng, 0.5);
			const double Depth = 0.55 + 0.45 * FMath::Sqrt(Rng.FRand());
			const FVector P = Centre + D * Radii * Depth;
			const FVector Out = (D + Rng.GetUnitVector() * 0.35).GetSafeNormal();
			const FVector Right = FVector::CrossProduct(Out, Rng.GetUnitVector()).GetSafeNormal();
			const FVector Bend = (P - MassCentre).GetSafeNormal();
			const double S = Size * Rng.FRandRange(0.8f, 1.2f);
			const float Sw = (float)FMath::Clamp(0.3 + 0.7 * P.Z / SlonkPlants::UnitCm, 0.0, 1.0);
			B.Card(P, Out, Right, S, S, Bend, Sw * 0.85f, Sw, FMath::Clamp(Rand + Rng.FRandRange(-0.1f, 0.1f), 0.f, 1.f), LeafCard);
		}
	}

	void BuildGrass(FBuild& B, int32 Lod)
	{
		FRandomStream Rng(911);
		const int32 Blades = Lod == 0 ? 16 : (Lod == 1 ? 7 : 3);
		const int32 Segs = Lod == 0 ? 3 : (Lod == 1 ? 2 : 1);
		for (int32 b = 0; b < Blades; ++b)
		{
			const double A = Rng.FRandRange(0.f, UE_TWO_PI), R = 12.0 * FMath::Sqrt(Rng.FRand());
			const FVector Base(R * FMath::Cos(A), R * FMath::Sin(A), 0.0);
			const double Out = A + Rng.FRandRange(-0.5f, 0.5f);
			const FVector O(FMath::Cos(Out), FMath::Sin(Out), 0.0), S(-O.Y, O.X, 0.0);
			const double Lean = FMath::DegreesToRadians(Rng.FRandRange(10.f, 40.f)), H = Rng.FRandRange(60.f, 100.f), W = Rng.FRandRange(3.0f, 4.5f);
			const float Rand = Rng.FRand();
			// Lit as if facing the sky and a little outward: a tuft reads as a soft mass, not a fan of flat strips.
			const FVector Nrm = FVector::ZAxisVector * 0.6 + O * 0.4;
			TArray<int32> L, Rt;
			for (int32 i = 0; i <= Segs; ++i)
			{
				const double T = (double)i / Segs;
				const FVector P = Base + O * (H * 0.55 * FMath::Sin(Lean) * FMath::Pow(T, 1.6)) + FVector::ZAxisVector * (H * T * FMath::Cos(Lean * 0.5 * T));
				const double Wd = W * FMath::Pow(1.0 - T, 0.9) * 0.5;
				const float Sw = (float)FMath::Pow(T, 1.5);
				if (i < Segs)
				{
					L.Add(B.Vert(P - S * Wd, Nrm, FVector2f(0.f, (float)T), FVector4f(1.f, Sw, Rand, SolidSurface)));
					Rt.Add(B.Vert(P + S * Wd, Nrm, FVector2f(1.f, (float)T), FVector4f(1.f, Sw, Rand, SolidSurface)));
				}
				else
				{
					const int32 Tip = B.Vert(P, Nrm, FVector2f(0.5f, 1.f), FVector4f(1.f, 1.f, Rand, SolidSurface));
					B.Tri(L.Last(), Rt.Last(), Tip);
				}
				// The blade's front is its side toward the sky (it leans outward, so that side faces up and in): seen from
				// above, as it mostly is, its face is lit by its sky-leaning normal rather than the reverse of it.
				if (i > 0 && i < Segs) { B.Quad(L[i - 1], Rt[i - 1], Rt[i], L[i]); }
			}
		}
	}

	void BuildShrub(FBuild& B, int32 Lod)
	{
		FRandomStream Rng(1201);
		const FVector Centre(0.0, 0.0, 58.0), Radii(55.0, 55.0, 42.0);
		if (Lod < 2)
		{
			const int32 Stems = Lod == 0 ? 5 : 3;
			for (int32 s = 0; s < Stems; ++s)
			{
				const FVector Top = Centre + FVector(Rng.FRandRange(-25.f, 25.f), Rng.FRandRange(-25.f, 25.f), Rng.FRandRange(-20.f, 5.f));
				const FVector Foot(Rng.FRandRange(-6.f, 6.f), Rng.FRandRange(-6.f, 6.f), -3.0);
				B.Tube({ Foot, FMath::Lerp(Foot, Top, 0.5) + FVector(0.0, 0.0, 4.0), Top }, { 1.6, 1.1, 0.5 }, Lod == 0 ? 4 : 3, 0.0, 0.3, 0.f);
			}
		}
		const int32 Cards = Lod == 0 ? 90 : (Lod == 1 ? 32 : 10);
		const double Size = Lod == 0 ? 24.0 : (Lod == 1 ? 38.0 : 62.0);
		Crown(B, Rng, Centre, Radii, Cards, Size, Centre - FVector(0.0, 0.0, 15.0), 0.5f);
	}

	void BuildBroadleaf(FBuild& B, int32 Lod)
	{
		FRandomStream Rng(2203);
		// The trunk leans and bends a little, then forks into the branches under the crown.
		const FVector Lean(Rng.FRandRange(-2.f, 2.f), Rng.FRandRange(-2.f, 2.f), 0.0);
		const TArray<FVector> Trunk = { FVector(0.0, 0.0, -4.0), FVector(0.0, 0.0, 15.0) + Lean * 0.4, FVector(0.0, 0.0, 30.0) + Lean, FVector(0.0, 0.0, 46.0) + Lean * 1.3 };
		B.Tube(Trunk, { 3.6, 3.0, 2.6, 2.1 }, Lod == 0 ? 7 : (Lod == 1 ? 5 : 4), 0.0, 0.15, 0.f);
		const FVector Mass(0.0, 0.0, 66.0);
		const int32 Clusters = 6;
		TArray<FVector> Tops;
		for (int32 c = 0; c < Clusters; ++c)
		{
			const double A = UE_DOUBLE_TWO_PI * (c + Rng.FRandRange(-0.25f, 0.25f)) / Clusters;
			const double Rr = c == 0 ? 0.0 : Rng.FRandRange(14.f, 22.f);
			Tops.Add(FVector(Rr * FMath::Cos(A), Rr * FMath::Sin(A), c == 0 ? 82.0 : Rng.FRandRange(58.f, 74.f)) + Lean * 1.3);
		}
		if (Lod < 2)
		{
			for (int32 c = 0; c < (Lod == 0 ? Clusters : 3); ++c)
			{
				const FVector From = Trunk.Last() - FVector(0.0, 0.0, Rng.FRandRange(0.f, 10.f));
				B.Tube({ From, FMath::Lerp(From, Tops[c], 0.55) + FVector(0.0, 0.0, 3.0), Tops[c] }, { 1.5, 1.0, 0.5 }, Lod == 0 ? 4 : 3, 0.15, 0.5, 0.f);
			}
		}
		const int32 Cards = Lod == 0 ? 34 : (Lod == 1 ? 12 : 4);
		const double Size = Lod == 0 ? 13.0 : (Lod == 1 ? 21.0 : 36.0);
		for (int32 c = 0; c < Clusters; ++c)
		{
			const double R = Rng.FRandRange(16.f, 22.f);
			Crown(B, Rng, Tops[c], FVector(R, R, R * 0.8), Cards, Size, Mass, (float)c / Clusters);
		}
	}

	void BuildConifer(FBuild& B, int32 Lod)
	{
		FRandomStream Rng(3307);
		B.Tube({ FVector(0.0, 0.0, -4.0), FVector(0.0, 0.0, 40.0), FVector(0.0, 0.0, 97.0) }, { 2.6, 1.8, 0.4 }, Lod == 0 ? 6 : (Lod == 1 ? 4 : 3), 0.0, 0.25, 0.f);
		const int32 Tiers = Lod == 0 ? 9 : (Lod == 1 ? 6 : 3);
		const int32 PerTier = Lod == 0 ? 9 : (Lod == 1 ? 6 : 4);
		const int32 Layers = Lod == 0 ? 2 : 1;
		for (int32 t = 0; t < Tiers; ++t)
		{
			const double F = (double)t / FMath::Max(Tiers - 1, 1);
			const double Z = FMath::Lerp(16.0, 94.0, F);
			// The skirt narrows to the leader in a slightly concave taper, as a spruce does.
			const double Rr = 31.0 * FMath::Pow(1.0 - F, 1.1) + 3.0;
			const float Rand = Rng.FRand();
			for (int32 L = 0; L < Layers; ++L)
			{
				const double Offset = Rng.FRandRange(0.f, UE_TWO_PI) + L * UE_DOUBLE_PI / PerTier;
				const double Zl = Z + L * 3.0, Rl = Rr * (1.0 - 0.15 * L);
				for (int32 m = 0; m < PerTier; ++m)
				{
					const double A = Offset + UE_DOUBLE_TWO_PI * (m + Rng.FRandRange(-0.15f, 0.15f)) / PerTier;
					const FVector O(FMath::Cos(A), FMath::Sin(A), 0.0), S(-O.Y, O.X, 0.0);
					const FVector In = FVector(0.0, 0.0, Zl) + O * 1.5;
					const FVector Tip = FVector(0.0, 0.0, Zl) + O * Rl - FVector::ZAxisVector * (Rl * 0.38);
					const double Wi = 2.0, Wo = UE_DOUBLE_TWO_PI * Rl / PerTier * 1.3;
					// A drooping fan of needles from the stem to the tier's edge, lit from above and outward.
					const FVector Nrm = FVector::ZAxisVector * 0.7 + O * 0.5;
					const float SwIn = (float)(0.1 + 0.4 * Zl / 100.0), SwOut = (float)FMath::Min(0.4 + 0.6 * Zl / 100.0, 1.0);
					const int32 V0 = B.Vert(In - S * Wi * 0.5, Nrm, FVector2f(0.f, 0.f), FVector4f(1.f, SwIn, Rand, NeedleCard));
					const int32 V1 = B.Vert(In + S * Wi * 0.5, Nrm, FVector2f(1.f, 0.f), FVector4f(1.f, SwIn, Rand, NeedleCard));
					const int32 V2 = B.Vert(Tip + S * Wo * 0.5, Nrm, FVector2f(1.f, 1.f), FVector4f(1.f, SwOut, Rand, NeedleCard));
					const int32 V3 = B.Vert(Tip - S * Wo * 0.5, Nrm, FVector2f(0.f, 1.f), FVector4f(1.f, SwOut, Rand, NeedleCard));
					B.Quad(V0, V1, V2, V3);
				}
			}
		}
	}

	/**
	 * A tree a kilometre and more off, a few pixels tall: its crown alone, one closed solid shape of foliage (no cards
	 * to mask, no trunk), Sides round from Bottom up to Top through a ring of Radius at Waist (Bottom at the waist makes
	 * a cone). The far levels of the forest are thousands of these, so a handful of triangles apiece. Wound outward (as
	 * the mesh reads it, the side the renderer shows), normals out from the crown's middle so it is lit as a mass,
	 * brighter toward the top as the cards are.
	 */
	void FarCrown(FBuild& B, double Bottom, double Waist, double Top, double Radius, int32 Sides, double Twist)
	{
		const FVector Mid(0.0, 0.0, (Bottom + Top) * 0.5);
		const double Half = FMath::Max((Top - Bottom) * 0.5, 1.0);
		auto Normal = [&](const FVector& P) { const FVector D = P - Mid; return FVector(D.X / (Radius * Radius), D.Y / (Radius * Radius), D.Z / (Half * Half)); };
		auto Sway = [](double Z) { return (float)FMath::Clamp(0.3 + 0.7 * Z / SlonkPlants::UnitCm, 0.0, 1.0); };
		const FVector Up(0.0, 0.0, Top), Down(0.0, 0.0, Bottom);
		const bool bCone = Bottom >= Waist - 1e-3;
		for (int32 s = 0; s < Sides; ++s)
		{
			const double A0 = Twist + UE_DOUBLE_TWO_PI * s / Sides, A1 = Twist + UE_DOUBLE_TWO_PI * (s + 1) / Sides;
			const FVector E0(Radius * FMath::Cos(A0), Radius * FMath::Sin(A0), Waist), E1(Radius * FMath::Cos(A1), Radius * FMath::Sin(A1), Waist);
			// A cone's sides lean in to the top, and the conifer's needle cards it stands for are lit as facing 54 degrees
			// above the horizon (their normals, up 0.7 and out 0.5), steeper than the slope: its normals take theirs, so
			// it is as bright, and holds snow as well, where a tree goes to it.
			const FVector N0 = bCone ? FVector(FMath::Cos(A0), FMath::Sin(A0), 1.4) : Normal(E0);
			const FVector N1 = bCone ? FVector(FMath::Cos(A1), FMath::Sin(A1), 1.4) : Normal(E1);
			const FVector4f CW(1.f, Sway(Waist), 0.5f, SolidSurface), CT(1.f, Sway(Top), 0.5f, SolidSurface), CB(1.f, Sway(Bottom), 0.5f, SolidSurface);
			B.Tri(B.Vert(E1, N1, FVector2f(1.f, 0.f), CW), B.Vert(E0, N0, FVector2f(0.f, 0.f), CW), B.Vert(Up, FVector::ZAxisVector, FVector2f(0.5f, 1.f), CT));
			if (!bCone) { B.Tri(B.Vert(Down, -FVector::ZAxisVector, FVector2f(0.5f, 0.f), CB), B.Vert(E0, N0, FVector2f(0.f, 0.5f), CW), B.Vert(E1, N1, FVector2f(1.f, 0.5f), CW)); }
		}
	}

	/** Each kind's levels of detail: three for all, and the trees a fourth (FarCrown) for their far levels. */
	int32 NumLods(EPlant Kind) { return Kind == EPlant::Broadleaf || Kind == EPlant::Conifer ? 4 : 3; }

	void BuildLod(EPlant Kind, int32 Lod, FDynamicMesh3& Out)
	{
		FBuild B;
		switch (Kind)
		{
		case EPlant::Grass: BuildGrass(B, Lod); break;
		case EPlant::Shrub: BuildShrub(B, Lod); break;
		// The broadleaf's six clusters (tops 14-22 out at 58-82 up, 16-22 across) make a crown about 80 wide from 45 to
		// 100; the conifer's tiers a cone from its lowest skirt, drooping to about 8, to its leader at 97.
		case EPlant::Broadleaf: if (Lod >= 3) { FarCrown(B, 42.0, 68.0, 100.0, 37.0, 4, 0.4); } else { BuildBroadleaf(B, Lod); } break;
		default: if (Lod >= 3) { FarCrown(B, 8.0, 8.0, 99.0, 30.0, 5, 0.0); } else { BuildConifer(B, Lod); } break;
		}
		Out = MoveTemp(B.Mesh);
	}
}

bool SlonkPlants::FarCrownStats(EPlant Kind, int32& OutTriangles, double& OutVolume, double& OutWorstFacing, FBox& OutBox, FBox& OutNearBox)
{
	if (NumLods(Kind) < 4) { return false; }
	FDynamicMesh3 Far, Near;
	BuildLod(Kind, 3, Far);
	BuildLod(Kind, 0, Near);
	OutBox = FBox(ForceInit);
	for (int32 V : Far.VertexIndicesItr()) { OutBox += (FVector)Far.GetVertex(V); }
	OutNearBox = FBox(ForceInit);
	for (int32 V : Near.VertexIndicesItr()) { OutNearBox += (FVector)Near.GetVertex(V); }
	OutTriangles = Far.TriangleCount();
	OutVolume = 0.0;
	OutWorstFacing = 1.0;
	const FVector3d Ref(0.0, 0.0, OutBox.Min.Z);
	const FDynamicMeshNormalOverlay* N = Far.Attributes()->PrimaryNormals();
	for (int32 T : Far.TriangleIndicesItr())
	{
		// The face's normal as the mesh reads its winding (the side the renderer shows), against its corners' normals.
		FVector3d Fn, Centroid; double Area;
		Far.GetTriInfo(T, Fn, Area, Centroid);
		OutVolume += Area * Fn.Dot(Centroid - Ref) / 3.0 / FMath::Pow(UnitCm, 3.0);
		const FIndex3i E = N->GetTriangle(T);
		const FVector3d Corner = Normalized(FVector3d(N->GetElement(E.A) + N->GetElement(E.B) + N->GetElement(E.C)));
		OutWorstFacing = FMath::Min(OutWorstFacing, Fn.Dot(Corner));
	}
	return true;
}

void SlonkPlants::FacingShares(EPlant Kind, int32 Lod, double& OutBark, double& OutFoliage, double& OutSkyward)
{
	FDynamicMesh3 M;
	BuildLod(Kind, Lod, M);
	const FDynamicMeshNormalOverlay* N = M.Attributes()->PrimaryNormals();
	const FDynamicMeshColorOverlay* C = M.Attributes()->PrimaryColors();
	int32 Bark = 0, BarkOut = 0, Leaf = 0, LeafOut = 0, LeafUp = 0;
	for (int32 T : M.TriangleIndicesItr())
	{
		FVector3d Fn, Centroid; double Area;
		M.GetTriInfo(T, Fn, Area, Centroid);
		const FIndex3i E = N->GetTriangle(T);
		const FVector3d Corner = FVector3d(N->GetElement(E.A) + N->GetElement(E.B) + N->GetElement(E.C));
		const bool bOut = Fn.Dot(Corner) > 0.0;
		if (C->GetElement(C->GetTriangle(T).A).X < 0.5f) { ++Bark; BarkOut += bOut; } else { ++Leaf; LeafOut += bOut; LeafUp += Fn.Z > 0.0; }
	}
	OutBark = Bark ? (double)BarkOut / Bark : 1.0;
	OutFoliage = Leaf ? (double)LeafOut / Leaf : 1.0;
	OutSkyward = Leaf ? (double)LeafUp / Leaf : 1.0;
}

UStaticMesh* SlonkPlants::GetMesh(EPlant Kind)
{
	const int32 K = FMath::Clamp((int32)Kind, 0, NumKinds - 1);
	if (GPlants.Num() < NumKinds) { GPlants.SetNumZeroed(NumKinds); }
	if (GPlants[K] && IsValid(GPlants[K])) { return GPlants[K]; }

	const int32 Lods = NumLods((EPlant)K);
	FDynamicMesh3 Built[4];
	for (int32 L = 0; L < Lods; ++L) { BuildLod((EPlant)K, L, Built[L]); }
	// Trees hold their detail further than a tuft of grass does, and go to their solid crown (FarCrown) a few pixels
	// tall: a 15 m tree about 2 km off.
	const bool bTree = (EPlant)K == EPlant::Broadleaf || (EPlant)K == EPlant::Conifer;
	const float ScreenSizes[4] = { 1.f, bTree ? 0.25f : 0.12f, bTree ? 0.07f : 0.035f, FMath::Clamp(FarCrownScreenSize, 0.002f, 0.06f) };
	// The wind (M_Foliage) bends a plant by up to 3% of its height and flutters its leaves a centimetre on top, past the
	// mesh's own box: the instances are culled by these bounds, so they take the sway in, in the unit mesh's own
	// terms (a percent of the height is a unit; a centimetre is several on a tuft of grass a quarter of a metre tall).
	const FVector Sway = (EPlant)K == EPlant::Grass ? FVector(6.0) : FVector(4.0, 4.0, 2.0);
	UStaticMesh* Mesh = SlonkProcMesh::MakeStaticMesh(*FString::Printf(TEXT("SlonkPlant%d"), K), TEXT("Plant"), TArrayView<const FDynamicMesh3>(Built, Lods), ScreenSizes, Sway);
	// A capsule round a tree's trunk, so the crew walk round it; grass and bushes are walked through.
	if ((EPlant)K == EPlant::Broadleaf || (EPlant)K == EPlant::Conifer)
	{
		Mesh->CreateBodySetup();
		if (UBodySetup* BS = Mesh->GetBodySetup())
		{
			const bool bBroad = (EPlant)K == EPlant::Broadleaf;
			FKSphylElem Trunk(bBroad ? 3.0f : 2.5f, bBroad ? 40.f : 60.f);
			Trunk.Center = FVector(0.0, 0.0, bBroad ? 22.0 : 32.0);
			BS->AggGeom.SphylElems.Add(Trunk);
			BS->CollisionTraceFlag = CTF_UseSimpleAsComplex;
			BS->CreatePhysicsMeshes();
		}
	}
	GPlants[K] = Mesh;
	return Mesh;
}
