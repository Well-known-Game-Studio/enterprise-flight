#include "RockInstancesComponent.h"
#include "Engine/StaticMesh.h"

void URockInstancesComponent::SetRocks(const TArray<FTransform>& Transforms, TConstArrayView<float> CustomData)
{
	ClearRocks();
	if (Transforms.Num() == 0) { return; }
	const FBox Mesh = GetStaticMesh() ? GetStaticMesh()->GetBounds().GetBox() : FBox(FVector(-50.0), FVector(50.0));
	for (const FTransform& T : Transforms) { LocalBox += Mesh.TransformBy(T); }
	AddInstances(Transforms, false, false, false);
	if (NumCustomDataFloats > 0 && CustomData.Num() == Transforms.Num() * NumCustomDataFloats)
	{
		SetCustomData(0, Transforms.Num() - 1, CustomData);
	}
	UpdateBounds();
}

void URockInstancesComponent::ClearRocks()
{
	LocalBox.Init();
	if (GetInstanceCount() > 0) { ClearInstances(); }
}

FBoxSphereBounds URockInstancesComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (!LocalBox.IsValid) { return Super::CalcBounds(LocalToWorld); }
	FBoxSphereBounds B(LocalBox.TransformBy(LocalToWorld));
	B.BoxExtent *= BoundsScale;
	B.SphereRadius *= BoundsScale;
	return B;
}
