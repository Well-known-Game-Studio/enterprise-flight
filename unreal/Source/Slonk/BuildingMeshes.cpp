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
	UStaticMesh* GMeshes[(int32)EBuildingKind::Count] = { nullptr, nullptr, nullptr };

	// An axis-aligned box with flat-shaded faces (split normals) added to a mesh.
	void AddBox(FDynamicMesh3& M, const FVector3d& Min, const FVector3d& Max)
	{
		const FVector3d C[8] = {
			{ Min.X, Min.Y, Min.Z }, { Max.X, Min.Y, Min.Z }, { Max.X, Max.Y, Min.Z }, { Min.X, Max.Y, Min.Z },
			{ Min.X, Min.Y, Max.Z }, { Max.X, Min.Y, Max.Z }, { Max.X, Max.Y, Max.Z }, { Min.X, Max.Y, Max.Z } };
		// faces as quads, outward winding
		const int32 F[6][4] = { { 0, 3, 2, 1 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 }, { 1, 2, 6, 5 }, { 2, 3, 7, 6 }, { 3, 0, 4, 7 } };
		for (int32 f = 0; f < 6; ++f)
		{
			const int32 V0 = M.AppendVertex(C[F[f][0]]), V1 = M.AppendVertex(C[F[f][1]]), V2 = M.AppendVertex(C[F[f][2]]), V3 = M.AppendVertex(C[F[f][3]]);
			M.AppendTriangle(V0, V1, V2);
			M.AppendTriangle(V0, V2, V3);
		}
	}
	// A roof prism over a box footprint: ridge along X at the top.
	void AddRoof(FDynamicMesh3& M, const FVector3d& Min, const FVector3d& Max, double RidgeZ)
	{
		const double Ym = 0.5 * (Min.Y + Max.Y);
		const int32 A = M.AppendVertex(FVector3d(Min.X, Min.Y, Max.Z)), B = M.AppendVertex(FVector3d(Max.X, Min.Y, Max.Z));
		const int32 Cc = M.AppendVertex(FVector3d(Max.X, Max.Y, Max.Z)), D = M.AppendVertex(FVector3d(Min.X, Max.Y, Max.Z));
		const int32 R0 = M.AppendVertex(FVector3d(Min.X, Ym, RidgeZ)), R1 = M.AppendVertex(FVector3d(Max.X, Ym, RidgeZ));
		M.AppendTriangle(A, B, R1); M.AppendTriangle(A, R1, R0);		// front slope
		M.AppendTriangle(Cc, D, R0); M.AppendTriangle(Cc, R0, R1);	// back slope
		M.AppendTriangle(A, R0, D);										// gable ends
		M.AppendTriangle(B, Cc, R1);
	}
	void Build(FDynamicMesh3& M, EBuildingKind Kind)
	{
		M.EnableAttributes();
		// in metres here, scaled to centimetres below
		switch (Kind)
		{
		case EBuildingKind::Tower:
			AddBox(M, FVector3d(-0.5, -0.5, -0.5), FVector3d(0.5, 0.5, -0.05));
			AddBox(M, FVector3d(-0.38, -0.38, -0.05), FVector3d(0.38, 0.38, 0.3));
			AddBox(M, FVector3d(-0.26, -0.26, 0.3), FVector3d(0.26, 0.26, 0.5));
			break;
		case EBuildingKind::House:
			AddBox(M, FVector3d(-0.5, -0.5, -0.5), FVector3d(0.5, 0.5, 0.15));
			AddRoof(M, FVector3d(-0.5, -0.5, -0.5), FVector3d(0.5, 0.5, 0.15), 0.5);
			break;
		default:
			AddBox(M, FVector3d(-0.5, -0.5, -0.5), FVector3d(0.5, 0.5, 0.5));
			break;
		}
		for (int32 V : M.VertexIndicesItr()) { M.SetVertex(V, M.GetVertex(V) * 100.0); }
		// flat normals per triangle
		FDynamicMeshNormalOverlay* N = M.Attributes()->PrimaryNormals();
		for (int32 T : M.TriangleIndicesItr())
		{
			const FVector3f Nf((FVector3f)M.GetTriNormal(T));
			const int32 E0 = N->AppendElement(Nf), E1 = N->AppendElement(Nf), E2 = N->AppendElement(Nf);
			N->SetTriangle(T, FIndex3i(E0, E1, E2));
		}
	}
}

UStaticMesh* SlonkBuildings::GetMesh(EBuildingKind Kind)
{
	const int32 K = (int32)Kind;
	if (GMeshes[K]) { return GMeshes[K]; }
	FDynamicMesh3 Dm;
	Build(Dm, Kind);
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
		BS->AggGeom.BoxElems.Add(FKBoxElem(100.f, 100.f, 100.f));		// the 1 m box, in cm
		BS->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		BS->CreatePhysicsMeshes();
	}
	GMeshes[K] = Mesh;
	return Mesh;
}
