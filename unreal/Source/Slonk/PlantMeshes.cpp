#include "PlantMeshes.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMeshToMeshDescription.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "UObject/Package.h"

using namespace UE::Geometry;

namespace
{
	TArray<UStaticMesh*> GPlants;

	// Vertex colour alpha: the surface kind the material masks by.
	constexpr float SolidSurface = 0.f, LeafCard = 0.4f, NeedleCard = 1.f;

	/** A mesh of unshared vertices, each with its own normal, UV and colour, so a blade or a card is lit as itself.
	 *  Elements are appended in step with the vertices, so an element's id is its vertex's. */
	struct FBuild
	{
		FDynamicMesh3 Mesh;
		FDynamicMeshNormalOverlay* N = nullptr;
		FDynamicMeshUVOverlay* UV = nullptr;
		FDynamicMeshColorOverlay* C = nullptr;
		FBuild()
		{
			Mesh.EnableAttributes();
			Mesh.Attributes()->EnablePrimaryColors();
			N = Mesh.Attributes()->PrimaryNormals();
			UV = Mesh.Attributes()->PrimaryUV();
			C = Mesh.Attributes()->PrimaryColors();
		}
		int32 Vert(const FVector& P, const FVector& Nrm, const FVector2f& T, const FVector4f& Col)
		{
			const int32 V = Mesh.AppendVertex(P);
			N->AppendElement(FVector3f(Nrm.GetSafeNormal()));
			UV->AppendElement(T);
			C->AppendElement(Col);
			return V;
		}
		void Tri(int32 A, int32 B, int32 D)
		{
			const FIndex3i T(A, B, D);
			const int32 Tid = Mesh.AppendTriangle(T);
			if (Tid >= 0) { N->SetTriangle(Tid, T); UV->SetTriangle(Tid, T); C->SetTriangle(Tid, T); }
		}
		void Quad(int32 A, int32 B, int32 D, int32 E) { Tri(A, B, D); Tri(A, D, E); }

		/** A tapering tube along a path (a trunk, a branch, a stem): rings of Sides, normals out from the axis. */
		void Tube(const TArray<FVector>& Path, const TArray<double>& Radii, int32 Sides, double Sway0, double Sway1, float Rand)
		{
			if (Path.Num() < 2 || Sides < 3) { return; }
			FVector Ax = FVector::ZeroVector;
			TArray<int32> Prev;
			for (int32 r = 0; r < Path.Num(); ++r)
			{
				const FVector T = (Path[FMath::Min(r + 1, Path.Num() - 1)] - Path[FMath::Max(r - 1, 0)]).GetSafeNormal();
				// The ring's frame is carried along the path, so the tube does not twist.
				if (Ax.IsZero()) { Ax = FVector::CrossProduct(T, FMath::Abs(T.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector).GetSafeNormal(); }
				Ax = (Ax - T * FVector::DotProduct(Ax, T)).GetSafeNormal();
				const FVector Ay = FVector::CrossProduct(T, Ax);
				const double F = (double)r / (Path.Num() - 1);
				const float Sw = (float)FMath::Lerp(Sway0, Sway1, F);
				TArray<int32> Ring;
				for (int32 s = 0; s <= Sides; ++s)
				{
					const double A = UE_DOUBLE_TWO_PI * s / Sides;
					const FVector D = Ax * FMath::Cos(A) + Ay * FMath::Sin(A);
					Ring.Add(Vert(Path[r] + D * Radii[r], D, FVector2f((float)s / Sides, (float)F), FVector4f(0.f, Sw, Rand, SolidSurface)));
				}
				if (Prev.Num() > 0)
				{
					for (int32 s = 0; s < Sides; ++s) { Quad(Prev[s], Prev[s + 1], Ring[s + 1], Ring[s]); }
				}
				Prev = MoveTemp(Ring);
			}
		}

		/** A leaf card: a quad facing Out, its normals bent toward Bend (out from the crown) so a crown is lit as a
		 *  mass rather than as a hundred flat planes. */
		void Card(const FVector& Centre, const FVector& Out, const FVector& Right, double W, double H, const FVector& Bend, float SwayLo, float SwayHi, float Rand, float Kind)
		{
			const FVector Up = FVector::CrossProduct(Out, Right).GetSafeNormal();
			const FVector R = Right * (W * 0.5), U = Up * (H * 0.5);
			const FVector Corner[4] = { Centre - R - U, Centre + R - U, Centre + R + U, Centre - R + U };
			const FVector2f T[4] = { FVector2f(0.f, 0.f), FVector2f(1.f, 0.f), FVector2f(1.f, 1.f), FVector2f(0.f, 1.f) };
			int32 V[4];
			for (int32 i = 0; i < 4; ++i)
			{
				const FVector Nrm = Bend * 0.65 + Out * 0.35 + (Corner[i] - Centre).GetSafeNormal() * 0.2;
				V[i] = Vert(Corner[i], Nrm, T[i], FVector4f(1.f, i < 2 ? SwayLo : SwayHi, Rand, Kind));
			}
			Quad(V[0], V[1], V[2], V[3]);
		}
	};

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

	void BuildLod(EPlant Kind, int32 Lod, FDynamicMesh3& Out)
	{
		FBuild B;
		switch (Kind)
		{
		case EPlant::Grass: BuildGrass(B, Lod); break;
		case EPlant::Shrub: BuildShrub(B, Lod); break;
		case EPlant::Broadleaf: BuildBroadleaf(B, Lod); break;
		default: BuildConifer(B, Lod); break;
		}
		Out = MoveTemp(B.Mesh);
	}
}

UStaticMesh* SlonkPlants::GetMesh(EPlant Kind)
{
	const int32 K = FMath::Clamp((int32)Kind, 0, NumKinds - 1);
	if (GPlants.Num() < NumKinds) { GPlants.SetNumZeroed(NumKinds); }
	if (GPlants[K] && IsValid(GPlants[K])) { return GPlants[K]; }

	FDynamicMesh3 Lods[3];
	FMeshDescription MD[3];
	FDynamicMeshToMeshDescription Converter;
	for (int32 L = 0; L < 3; ++L)
	{
		BuildLod((EPlant)K, L, Lods[L]);
		FStaticMeshAttributes(MD[L]).Register();
		Converter.Convert(&Lods[L], MD[L], false);
	}
	UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(), *FString::Printf(TEXT("SlonkPlant%d"), K), RF_Transient);
	Mesh->AddToRoot();
	FStaticMaterial PlantMaterial(nullptr, TEXT("Plant"));
#if WITH_EDITORONLY_DATA
	PlantMaterial.ImportedMaterialSlotName = TEXT("Plant");
#endif
	Mesh->GetStaticMaterials().Add(PlantMaterial);
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bFastBuild = true;
	Params.bMarkPackageDirty = false;
	Params.bCommitMeshDescription = false;
	Params.bBuildSimpleCollision = false;
	Mesh->BuildFromMeshDescriptions({ &MD[0], &MD[1], &MD[2] }, Params);
	if (FStaticMeshRenderData* RD = Mesh->GetRenderData())
	{
		// Trees hold their detail further than a tuft of grass does.
		const bool bTree = (EPlant)K == EPlant::Broadleaf || (EPlant)K == EPlant::Conifer;
		RD->ScreenSize[0].Default = 1.f;
		RD->ScreenSize[1].Default = bTree ? 0.25f : 0.12f;
		RD->ScreenSize[2].Default = bTree ? 0.07f : 0.035f;
	}
	// The wind (M_Foliage) bends a plant by up to 3% of its height and flutters its leaves a centimetre on top, past the
	// mesh's own box: the instances are culled by these bounds, so they take the sway in, in the unit mesh's own
	// terms (a percent of the height is a unit; a centimetre is several on a tuft of grass a quarter of a metre tall).
	{
		const FVector Sway = (EPlant)K == EPlant::Grass ? FVector(6.0) : FVector(4.0, 4.0, 2.0);
		Mesh->SetPositiveBoundsExtension(Sway);
		Mesh->SetNegativeBoundsExtension(Sway);
		Mesh->CalculateExtendedBounds();
	}
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
