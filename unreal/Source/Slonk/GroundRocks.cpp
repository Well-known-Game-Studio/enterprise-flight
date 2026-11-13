#include "GroundRocks.h"
#include "PlanetTerrain.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
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
	TArray<UStaticMesh*> GGroundRocks;

	/** A break: everything beyond D along the unit normal N is planed off onto the plane. */
	struct FCut { FVector N; double D; };

	struct FShape
	{
		TArray<FCut> Cuts;
		/** Low-frequency swell of the radius before the breaks, and fine unevenness across the faces after them. */
		double Lump = 0.1, Grain = 0.0;
		FVector Offset = FVector::ZeroVector;
		/** Faces meeting at more than this keep separate normals, so a break reads as an edge; 180 is smooth. */
		double CreaseDeg = 34.0;
	};

	FShape MakeShape(EGroundRock Kind)
	{
		FRandomStream Rng(4021 + (int32)Kind * 7717);
		FShape S;
		S.Offset = FVector(Rng.FRandRange(0.f, 50.f), Rng.FRandRange(0.f, 50.f), Rng.FRandRange(0.f, 50.f));
		switch (Kind)
		{
		case EGroundRock::Angular:
			S.Lump = 0.12; S.Grain = 0.012;
			for (int32 i = 0; i < 11; ++i) { S.Cuts.Add({ Rng.GetUnitVector(), Rng.FRandRange(0.58f, 0.86f) }); }
			// The face it came to rest on.
			S.Cuts.Add({ FVector(0.0, 0.0, -1.0), 0.55 });
			break;
		case EGroundRock::Block:
		{
			S.Lump = 0.06; S.Grain = 0.01;
			// Three joint sets near square to each other, tipped a few degrees so no face is true to the box.
			const FQuat Tip(Rng.GetUnitVector(), FMath::DegreesToRadians(Rng.FRandRange(3.f, 9.f)));
			const FVector Ax[3] = { Tip.RotateVector(FVector::XAxisVector), Tip.RotateVector(FVector::YAxisVector), Tip.RotateVector(FVector::ZAxisVector) };
			for (int32 a = 0; a < 3; ++a)
			{
				S.Cuts.Add({ Ax[a], Rng.FRandRange(0.6f, 0.72f) });
				S.Cuts.Add({ -Ax[a], Rng.FRandRange(0.6f, 0.72f) });
			}
			// Chipped corners and edges.
			for (int32 i = 0; i < 6; ++i) { S.Cuts.Add({ Rng.GetUnitVector(), Rng.FRandRange(0.8f, 0.92f) }); }
			break;
		}
		case EGroundRock::Slab:
			S.Lump = 0.08; S.Grain = 0.01;
			S.Cuts.Add({ FVector(0.0, 0.0, 1.0), 0.4 });
			S.Cuts.Add({ FVector(0.0, 0.0, -1.0), 0.4 });
			// Broken round the sides, the breaks leaning a little in and out.
			for (int32 i = 0; i < 7; ++i)
			{
				const double A = UE_DOUBLE_TWO_PI * (i + Rng.FRandRange(-0.25f, 0.25f)) / 7.0;
				S.Cuts.Add({ FVector(FMath::Cos(A), FMath::Sin(A), Rng.FRandRange(-0.3f, 0.3f)).GetSafeNormal(), Rng.FRandRange(0.7f, 0.9f) });
			}
			break;
		default:
			S.Lump = 0.14; S.Grain = 0.0; S.CreaseDeg = 180.0;
			break;
		}
		return S;
	}

	FVector ShapePoint(const FShape& S, const FVector& Dir)
	{
		const double R = 1.0 + S.Lump * SlonkTerrain::Fbm(Dir * 1.3 + S.Offset, 3) + S.Lump * 0.35 * SlonkTerrain::Fbm(Dir * 3.1 + S.Offset * 1.7, 2);
		FVector P = Dir * R;
		// Twice through, since planing onto one break can lift a point back past one that met it at a sharp angle.
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			for (const FCut& C : S.Cuts)
			{
				const double Over = FVector::DotProduct(P, C.N) - C.D;
				if (Over > 0.0) { P -= C.N * Over; }
			}
		}
		if (S.Grain > 0.0) { P += Dir * (S.Grain * SlonkTerrain::Fbm(Dir * 9.0 + S.Offset * 2.3, 2)); }
		return P;
	}

	/** Unit icosphere with welded vertices, Level times subdivided (1: 42 vertices, 2: 162, 3: 642). */
	void Icosphere(int32 Level, TArray<FVector>& V, TArray<FIntVector>& T)
	{
		const double G = (1.0 + FMath::Sqrt(5.0)) * 0.5;
		V = { {-1, G, 0}, {1, G, 0}, {-1, -G, 0}, {1, -G, 0}, {0, -1, G}, {0, 1, G}, {0, -1, -G}, {0, 1, -G}, {G, 0, -1}, {G, 0, 1}, {-G, 0, -1}, {-G, 0, 1} };
		for (FVector& P : V) { P.Normalize(); }
		T = { {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
			{3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1} };
		for (int32 L = 0; L < Level; ++L)
		{
			TMap<uint64, int32> Mid;
			auto MidPoint = [&](int32 A, int32 B)
			{
				const uint64 K = A < B ? ((uint64)A << 32) | (uint32)B : ((uint64)B << 32) | (uint32)A;
				if (const int32* Found = Mid.Find(K)) { return *Found; }
				const int32 I = V.Add(((V[A] + V[B]) * 0.5).GetSafeNormal());
				Mid.Add(K, I);
				return I;
			};
			TArray<FIntVector> Next;
			Next.Reserve(T.Num() * 4);
			for (const FIntVector& F : T)
			{
				const int32 A = MidPoint(F.X, F.Y), B = MidPoint(F.Y, F.Z), C = MidPoint(F.Z, F.X);
				Next.Add({ F.X, A, C }); Next.Add({ F.Y, B, A }); Next.Add({ F.Z, C, B }); Next.Add({ A, B, C });
			}
			T = MoveTemp(Next);
		}
	}

	/** One LOD: the icosphere at Level pushed onto the shape, then mapped into the unit box Box was measured for. */
	void BuildLOD(FDynamicMesh3& Mesh, int32 Level, const FShape& S, const FBox& Box, TArray<FVector>* OutPoints = nullptr)
	{
		TArray<FVector> Dirs; TArray<FIntVector> Tris;
		Icosphere(Level, Dirs, Tris);
		const FVector Centre = Box.GetCenter(), Half = Box.GetExtent();
		Mesh.EnableAttributes();
		FDynamicMeshUVOverlay* UV = Mesh.Attributes()->PrimaryUV();
		for (const FVector& D : Dirs)
		{
			const FVector P = (ShapePoint(S, D) - Centre) / Half * (SlonkGroundRocks::UnitCm * 0.5);
			Mesh.AppendVertex(P);
			// The material projects its textures in the rock's own frame; the UVs only satisfy the mesh build.
			UV->AppendElement(FVector2f(0.5f + (float)(FMath::Atan2(D.Y, D.X) / UE_DOUBLE_TWO_PI), 0.5f - (float)(FMath::Asin(FMath::Clamp(D.Z, -1.0, 1.0)) / UE_DOUBLE_PI)));
			if (OutPoints) { OutPoints->Add(P); }
		}
		for (const FIntVector& F : Tris)
		{
			const int32 Tid = Mesh.AppendTriangle(FIndex3i(F.X, F.Y, F.Z));
			if (Tid >= 0) { UV->SetTriangle(Tid, FIndex3i(F.X, F.Y, F.Z)); }
		}
		// Outward winding whatever the handedness, judged on a face that no break has planed flat to the centre.
		if (Mesh.TriangleCount() > 0)
		{
			const FIndex3i T0 = Mesh.GetTriangle(0);
			const FVector C0 = (Mesh.GetVertex(T0.A) + Mesh.GetVertex(T0.B) + Mesh.GetVertex(T0.C)) / 3.0;
			if (FVector::DotProduct(Mesh.GetTriNormal(0), C0) < 0.0) { Mesh.ReverseOrientation(false); }
		}
		FMeshNormals::InitializeOverlayTopologyFromOpeningAngle(&Mesh, Mesh.Attributes()->PrimaryNormals(), S.CreaseDeg);
		FMeshNormals::QuickRecomputeOverlayNormals(Mesh);
	}
}

UStaticMesh* SlonkGroundRocks::GetMesh(EGroundRock Kind)
{
	const int32 K = FMath::Clamp((int32)Kind, 0, NumKinds - 1);
	if (GGroundRocks.Num() < NumKinds) { GGroundRocks.SetNumZeroed(NumKinds); }
	if (GGroundRocks[K] && IsValid(GGroundRocks[K])) { return GGroundRocks[K]; }

	const FShape Shape = MakeShape((EGroundRock)K);
	// The box comes from the finest sampling of the shape, so all three LODs fill it alike.
	TArray<FVector> D0; TArray<FIntVector> T0;
	Icosphere(3, D0, T0);
	FBox Box(ForceInit);
	for (const FVector& D : D0) { Box += ShapePoint(Shape, D); }
	FDynamicMesh3 Lod0, Lod1, Lod2;
	TArray<FVector> HullPoints;
	BuildLOD(Lod0, 3, Shape, Box);
	BuildLOD(Lod1, 2, Shape, Box);
	BuildLOD(Lod2, 1, Shape, Box, &HullPoints);

	FMeshDescription MD0, MD1, MD2;
	FStaticMeshAttributes(MD0).Register();
	FStaticMeshAttributes(MD1).Register();
	FStaticMeshAttributes(MD2).Register();
	FDynamicMeshToMeshDescription Converter;
	Converter.Convert(&Lod0, MD0, false);
	Converter.Convert(&Lod1, MD1, false);
	Converter.Convert(&Lod2, MD2, false);

	UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(), *FString::Printf(TEXT("SlonkGroundRock%d"), K), RF_Transient);
	Mesh->AddToRoot();
	FStaticMaterial RockMaterial(nullptr, TEXT("Rock"));
#if WITH_EDITORONLY_DATA
	RockMaterial.ImportedMaterialSlotName = TEXT("Rock");
#endif
	Mesh->GetStaticMaterials().Add(RockMaterial);
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bFastBuild = true;
	Params.bMarkPackageDirty = false;
	Params.bCommitMeshDescription = false;
	Params.bBuildSimpleCollision = false;
	Mesh->BuildFromMeshDescriptions({ &MD0, &MD1, &MD2 }, Params);
	if (FStaticMeshRenderData* RD = Mesh->GetRenderData())
	{
		// The GPU picks each instance's LOD by its screen size: a 2 m boulder drops to 320 triangles past about
		// 50 m and to 80 past 200 m.
		RD->ScreenSize[0].Default = 1.f;
		RD->ScreenSize[1].Default = 0.12f;
		RD->ScreenSize[2].Default = 0.035f;
	}
	// Convex collision from the coarsest LOD: the crew stand on the rocks and walk into the big ones.
	Mesh->CreateBodySetup();
	if (UBodySetup* BS = Mesh->GetBodySetup())
	{
		FKConvexElem Hull;
		Hull.VertexData = HullPoints;
		Hull.UpdateElemBox();
		BS->AggGeom.ConvexElems.Add(Hull);
		BS->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		BS->CreatePhysicsMeshes();
	}
	GGroundRocks[K] = Mesh;
	return Mesh;
}
