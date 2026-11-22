#include "ProcMeshBuild.h"

#include "DynamicMeshToMeshDescription.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "UObject/Package.h"

UStaticMesh* SlonkProcMesh::MakeStaticMesh(FName Name, FName SlotName, TArrayView<const UE::Geometry::FDynamicMesh3> Lods, TArrayView<const float> ScreenSizes, const FVector& BoundsExtension,
	bool bTangents)
{
	TArray<FMeshDescription> MD;
	MD.SetNum(Lods.Num());
	TArray<const FMeshDescription*> Descs;
	FDynamicMeshToMeshDescription Converter;
	for (int32 L = 0; L < Lods.Num(); ++L)
	{
		FStaticMeshAttributes Attributes(MD[L]);
		Attributes.Register();
		Converter.Convert(&Lods[L], MD[L], false);
		if (bTangents)
		{
			// A tangent at right angles to each normal: the mesh's +x laid on the surface (its +y where the normal runs
			// along x), and the bitangent the normal's cross with it, as the engine builds it.
			TVertexInstanceAttributesConstRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
			TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
			TVertexInstanceAttributesRef<float> Signs = Attributes.GetVertexInstanceBinormalSigns();
			for (const FVertexInstanceID V : MD[L].VertexInstances().GetElementIDs())
			{
				const FVector3f N = Normals[V];
				const FVector3f Ax = FMath::Abs(N.X) < 0.9f ? FVector3f::XAxisVector : FVector3f::YAxisVector;
				Tangents[V] = (Ax - N * FVector3f::DotProduct(N, Ax)).GetSafeNormal();
				Signs[V] = 1.f;
			}
		}
		Descs.Add(&MD[L]);
	}
	UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(), Name, RF_Transient);
	Mesh->AddToRoot();
	FStaticMaterial Slot(nullptr, SlotName);
#if WITH_EDITORONLY_DATA
	Slot.ImportedMaterialSlotName = SlotName;
#endif
	Mesh->GetStaticMaterials().Add(Slot);
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bFastBuild = true;
	Params.bMarkPackageDirty = false;
	Params.bCommitMeshDescription = false;
	Params.bBuildSimpleCollision = false;
	Mesh->BuildFromMeshDescriptions(Descs, Params);
	if (FStaticMeshRenderData* RD = Mesh->GetRenderData())
	{
		for (int32 L = 0; L < FMath::Min(Lods.Num(), ScreenSizes.Num()); ++L) { RD->ScreenSize[L].Default = ScreenSizes[L]; }
	}
	if (!BoundsExtension.IsZero())
	{
		Mesh->SetPositiveBoundsExtension(BoundsExtension);
		Mesh->SetNegativeBoundsExtension(BoundsExtension);
		Mesh->CalculateExtendedBounds();
	}
	return Mesh;
}
