// Slonk - automation tests for what grows on the worlds: the life forms a world's plants are built from and the meshes
// made of them (PlantMeshes.h). Under "Slonk.Flora."; tools/run_tests.sh runs them headless.

#include "Misc/AutomationTest.h"
#include "../PlantMeshes.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshResources.h"

#if WITH_DEV_AUTOMATION_TESTS

#ifndef SLONK_TEST_FLAGS
#define SLONK_TEST_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#endif

namespace
{
	/** Bytes appended field by field, for a CRC that hangs on the values alone (no padding between them). */
	struct FFloraBytes
	{
		TArray<uint8> B;
		template <typename T> void Put(const T& V) { B.Append(reinterpret_cast<const uint8*>(&V), sizeof(T)); }
		void PutVec(const FVector& V) { Put(V.X); Put(V.Y); Put(V.Z); }
		uint32 Crc() const { return FCrc::MemCrc32(B.GetData(), B.Num()); }
	};
	/** A mesh's every vertex, normal, UV, colour and triangle, in order. */
	void PutMesh(FFloraBytes& Out, const UE::Geometry::FDynamicMesh3& M)
	{
		for (const int32 V : M.VertexIndicesItr()) { Out.Put(V); Out.PutVec((FVector)M.GetVertex(V)); }
		const UE::Geometry::FDynamicMeshNormalOverlay* N = M.Attributes()->PrimaryNormals();
		const UE::Geometry::FDynamicMeshUVOverlay* U = M.Attributes()->PrimaryUV();
		const UE::Geometry::FDynamicMeshColorOverlay* C = M.Attributes()->PrimaryColors();
		for (const int32 E : N->ElementIndicesItr()) { Out.Put(N->GetElement(E)); }
		for (const int32 E : U->ElementIndicesItr()) { Out.Put(U->GetElement(E)); }
		for (const int32 E : C->ElementIndicesItr()) { Out.Put(C->GetElement(E)); }
		for (const int32 T : M.TriangleIndicesItr()) { Out.Put(M.GetTriangle(T)); Out.Put(N->GetTriangle(T)); Out.Put(U->GetTriangle(T)); Out.Put(C->GetTriangle(T)); }
	}
	/** The static mesh the game draws a plant with, as far as its instances hang on it: the screen sizes its levels of
	 *  detail change at, the box they are culled by (the wind's sway taken in), and the capsule round a tree's trunk. */
	void PutMade(FFloraBytes& Out, UStaticMesh* M)
	{
		const FStaticMeshRenderData* RD = M ? M->GetRenderData() : nullptr;
		const int32 Lods = RD ? RD->LODResources.Num() : -1;
		Out.Put(Lods);
		for (int32 L = 0; L < Lods; ++L) { Out.Put(RD->ScreenSize[L].Default); Out.Put((int32)RD->LODResources[L].GetNumTriangles()); }
		if (!M) { return; }
		const FBoxSphereBounds Box = M->GetBounds();
		Out.PutVec(Box.Origin); Out.PutVec(Box.BoxExtent); Out.Put(Box.SphereRadius);
		const UBodySetup* BS = M->GetBodySetup();
		const int32 Capsules = BS ? BS->AggGeom.SphylElems.Num() : 0;
		Out.Put(Capsules);
		for (int32 i = 0; i < Capsules; ++i)
		{
			const FKSphylElem& E = BS->AggGeom.SphylElems[i];
			Out.Put(E.Radius); Out.Put(E.Length); Out.PutVec(E.Center);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFloraEarthGoldenTest, "Slonk.Flora.EarthGolden", SLONK_TEST_FLAGS)
bool FSlonkFloraEarthGoldenTest::RunTest(const FString& Parameters)
{
	// What the Earth's plants were before a life form became a descriptor, held to the bit: the grass, the bush, the
	// broadleaf and the conifer at every level of detail (each vertex, normal, UV, colour and triangle, in the order
	// they are built), and the static mesh made of each (the screen sizes its levels change at, the box its instances
	// are culled by, the capsule the crew walk round a trunk by). The worlds near home grow these and nothing else, so
	// a forest there is the forest it was, tree for tree: where each stands is held by Slonk.Terrain.ClimateNeutral's
	// chunks, and what stands there by this.
	const EPlant Kinds[] = { EPlant::Grass, EPlant::Shrub, EPlant::Broadleaf, EPlant::Conifer };
	const TCHAR* Names[] = { TEXT("grass"), TEXT("bush"), TEXT("broadleaf"), TEXT("conifer") };
	// As they were before the descriptors came (this test, run on that code, gave these).
	const uint32 GoldenBuilt[] = { 0x8529d368u, 0xd8c538edu, 0x9557521au, 0x37963309u };
	const uint32 GoldenMade[] = { 0x82b37b28u, 0x6ea088e7u, 0x03b2c16fu, 0x382172c4u };
	const int32 GoldenLods[] = { 3, 3, 4, 4 };
	FString Built, Made;
	for (int32 k = 0; k < UE_ARRAY_COUNT(Kinds); ++k)
	{
		FFloraBytes B;
		int32 Lods = 0;
		for (;; ++Lods)
		{
			UE::Geometry::FDynamicMesh3 M;
			if (!SlonkPlants::BuildLod(SlonkPlants::EarthForm(Kinds[k]), Lods, M)) { break; }
			B.Put(Lods);
			PutMesh(B, M);
		}
		FFloraBytes S;
		PutMade(S, SlonkPlants::GetMesh(Kinds[k]));
		Built += FString::Printf(TEXT("%s0x%08xu"), k ? TEXT(", ") : TEXT(""), B.Crc());
		Made += FString::Printf(TEXT("%s0x%08xu"), k ? TEXT(", ") : TEXT(""), S.Crc());
		TestEqual(FString::Printf(TEXT("the %s's levels of detail"), Names[k]), Lods, GoldenLods[k]);
		TestEqual(FString::Printf(TEXT("the %s is built as it was"), Names[k]), B.Crc(), GoldenBuilt[k]);
		TestEqual(FString::Printf(TEXT("the %s's mesh is made as it was"), Names[k]), S.Crc(), GoldenMade[k]);
	}
	AddInfo(FString::Printf(TEXT("built { %s }, made { %s }"), *Built, *Made));
	return true;
}

#endif
