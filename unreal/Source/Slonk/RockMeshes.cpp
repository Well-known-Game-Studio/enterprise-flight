#include "RockMeshes.h"

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
	TArray<UStaticMesh*> GRockMeshes;

	struct FRockShape
	{
		FVector Axes = FVector::OneVector;
		FVector Offset = FVector::ZeroVector;
		float Lump = 0.18f;
		float Grain = 0.07f;
		struct FDent { FVector Dir; float Angle; float Depth; };
		TArray<FDent> Dents;
	};

	FRockShape MakeShape(int32 Variant)
	{
		FRandomStream Rng(1234 + Variant * 7919);
		FRockShape S;
		S.Axes = FVector(1.f, Rng.FRandRange(0.72f, 0.95f), Rng.FRandRange(0.55f, 0.85f));
		S.Offset = FVector(Rng.FRandRange(0.f, 100.f), Rng.FRandRange(0.f, 100.f), Rng.FRandRange(0.f, 100.f));
		S.Lump = Rng.FRandRange(0.12f, 0.24f);
		S.Grain = Rng.FRandRange(0.04f, 0.09f);
		const int32 NumDents = Rng.RandRange(4, 9);
		for (int32 i = 0; i < NumDents; ++i)
		{
			S.Dents.Add({ Rng.GetUnitVector(), (float)Rng.FRandRange(0.25f, 0.7f), (float)Rng.FRandRange(0.05f, 0.16f) });
		}
		return S;
	}

	float Fbm(const FVector& P, int32 Octaves)
	{
		float Sum = 0.f, Amp = 0.5f, Freq = 1.f;
		for (int32 i = 0; i < Octaves; ++i)
		{
			Sum += Amp * FMath::PerlinNoise3D(P * Freq);
			Amp *= 0.5f;
			Freq *= 2.1f;
		}
		return Sum;
	}

	/** Radius multiplier of the rock surface in direction Dir. */
	float RockRadius(const FRockShape& S, const FVector& Dir)
	{
		float R = 1.f + S.Lump * Fbm(Dir * 1.6f + S.Offset, 3) + S.Grain * Fbm(Dir * 5.f + S.Offset * 1.7f, 2);
		for (const FRockShape::FDent& D : S.Dents)
		{
			const float Ang = FMath::Acos(FMath::Clamp((float)FVector::DotProduct(Dir, D.Dir), -1.f, 1.f));
			if (Ang < D.Angle * 1.25f)
			{
				const float T = Ang / D.Angle;
				if (T < 1.f)
				{
					// Bowl: deepest at the centre, flattening toward the rim.
					R -= D.Depth * FMath::Pow(1.f - T * T, 1.5f);
				}
				else
				{
					// Slight raised rim just outside the bowl.
					const float U = (T - 1.f) / 0.25f;
					R += D.Depth * 0.2f * (1.f - U) * U * 4.f;
				}
			}
		}
		return R;
	}

	/** Cube-sphere with welded edges, displaced into a rock. */
	void BuildRockLOD(FDynamicMesh3& Mesh, int32 Steps, const FRockShape& S)
	{
		Mesh.EnableAttributes();
		FDynamicMeshNormalOverlay* NO = Mesh.Attributes()->PrimaryNormals();
		FDynamicMeshUVOverlay* UV = Mesh.Attributes()->PrimaryUV();
		TMap<FIntVector, int32> Lattice;
		TArray<FVector> Dirs;

		auto VertexFor = [&](const FIntVector& Key) -> int32
		{
			if (const int32* Found = Lattice.Find(Key))
			{
				return *Found;
			}
			const FVector Dir = FVector(Key).GetSafeNormal();
			const FVector Pos = Dir * RockRadius(S, Dir) * S.Axes * SlonkRocks::UnitRadius;
			const int32 Vid = Mesh.AppendVertex(Pos);
			NO->AppendElement(FVector3f(Dir));
			UV->AppendElement(FVector2f(0.5f + FMath::Atan2(Dir.Y, Dir.X) / (2.f * PI), 0.5f - FMath::Asin(Dir.Z) / PI));
			Lattice.Add(Key, Vid);
			Dirs.Add(Dir);
			return Vid;
		};
		auto AddTri = [&](int32 A, int32 B, int32 C)
		{
			const int32 Tid = Mesh.AppendTriangle(FIndex3i(A, B, C));
			if (Tid >= 0)
			{
				NO->SetTriangle(Tid, FIndex3i(A, B, C));
				UV->SetTriangle(Tid, FIndex3i(A, B, C));
			}
		};
		for (int32 Face = 0; Face < 6; ++Face)
		{
			const int32 Axis = Face / 2;
			const int32 Sign = (Face % 2) ? -1 : 1;
			const int32 U = (Axis + 1) % 3, V = (Axis + 2) % 3;
			auto Key = [&](int32 i, int32 j)
			{
				FIntVector K;
				K[Axis] = Sign * Steps;
				K[U] = 2 * i - Steps;
				K[V] = 2 * j - Steps;
				return K;
			};
			for (int32 j = 0; j < Steps; ++j)
			{
				for (int32 i = 0; i < Steps; ++i)
				{
					const int32 A = VertexFor(Key(i, j)), B = VertexFor(Key(i + 1, j)), C = VertexFor(Key(i, j + 1)), D = VertexFor(Key(i + 1, j + 1));
					if (Sign > 0)
					{
						AddTri(A, C, B); AddTri(B, C, D);
					}
					else
					{
						AddTri(A, B, C); AddTri(B, D, C);
					}
				}
			}
		}
		// Make the winding face outward, then set smooth normals from the displaced faces.
		if (Mesh.TriangleCount() > 0)
		{
			const FIndex3i T0 = Mesh.GetTriangle(0);
			const FVector Centroid = (Mesh.GetVertex(T0.A) + Mesh.GetVertex(T0.B) + Mesh.GetVertex(T0.C)) / 3.0;
			if (FVector::DotProduct(Mesh.GetTriNormal(0), Centroid) < 0.0)
			{
				Mesh.ReverseOrientation(false);
			}
		}
		TArray<FVector> Accum;
		Accum.SetNumZeroed(Mesh.MaxVertexID());
		for (int32 Tid : Mesh.TriangleIndicesItr())
		{
			const FIndex3i T = Mesh.GetTriangle(Tid);
			const FVector N = Mesh.GetTriArea(Tid) * Mesh.GetTriNormal(Tid);
			Accum[T.A] += N; Accum[T.B] += N; Accum[T.C] += N;
		}
		for (int32 Vid : Mesh.VertexIndicesItr())
		{
			FVector N = Accum[Vid].GetSafeNormal();
			if (N.IsNearlyZero() || FVector::DotProduct(N, Dirs[Vid]) < 0.0)
			{
				N = Dirs[Vid];
			}
			NO->SetElement(Vid, FVector3f(N));
		}
	}
}

UStaticMesh* SlonkRocks::GetRockMesh(int32 Variant)
{
	Variant = ((Variant % NumVariants) + NumVariants) % NumVariants;
	if (GRockMeshes.Num() < NumVariants)
	{
		GRockMeshes.SetNumZeroed(NumVariants);
	}
	if (GRockMeshes[Variant] && IsValid(GRockMeshes[Variant]))
	{
		return GRockMeshes[Variant];
	}

	const FRockShape Shape = MakeShape(Variant);
	FDynamicMesh3 Lod0, Lod1;
	BuildRockLOD(Lod0, 14, Shape);
	BuildRockLOD(Lod1, 5, Shape);

	FMeshDescription MD0, MD1;
	FStaticMeshAttributes(MD0).Register();
	FStaticMeshAttributes(MD1).Register();
	FDynamicMeshToMeshDescription Converter;
	Converter.Convert(&Lod0, MD0, false);
	Converter.Convert(&Lod1, MD1, false);

	UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(), *FString::Printf(TEXT("SlonkRock%d"), Variant), RF_Transient);
	Mesh->AddToRoot();
	Mesh->bAllowCPUAccess = true;
	FStaticMaterial RockMaterial(nullptr, TEXT("Rock"));
#if WITH_EDITORONLY_DATA
	RockMaterial.ImportedMaterialSlotName = TEXT("Rock");
#endif
	Mesh->GetStaticMaterials().Add(RockMaterial);
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bFastBuild = true;
	Params.bAllowCpuAccess = true;
	Params.bMarkPackageDirty = false;
	Params.bCommitMeshDescription = false;
	Params.bBuildSimpleCollision = false;
	Mesh->BuildFromMeshDescriptions({ &MD0, &MD1 }, Params);
	if (FStaticMeshRenderData* RD = Mesh->GetRenderData())
	{
		RD->ScreenSize[0].Default = 1.f;
		RD->ScreenSize[1].Default = 0.08f;
	}
	// Simple sphere collision so weapon traces and the ship can hit rocks cheaply.
	Mesh->CreateBodySetup();
	if (UBodySetup* BS = Mesh->GetBodySetup())
	{
		BS->AggGeom.SphereElems.Add(FKSphereElem(UnitRadius * 0.82f));
		BS->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		BS->CreatePhysicsMeshes();
	}
	GRockMeshes[Variant] = Mesh;
	return Mesh;
}
