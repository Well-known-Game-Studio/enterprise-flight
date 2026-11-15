// Slonk - instanced meshes that turn with a world for nearly nothing a frame: the rocks and plants on its terrain.
//
// The engine keeps an instance's transform in the GPU scene with its primitive's already in it, so every instance of an
// instanced component that moves is copied, multiplied and sent to the GPU again, every frame: with everything on a
// world turning with it, that was ten-odd milliseconds of each of the game and render threads for its hundreds of
// thousands of rocks and plants. These instances never reach the CPU's side of the GPU scene at all (its GPU-only
// instance data): each component keeps its instances in its own frame and hands them to FPlanetInstanceStore when its
// primitive enters the scene, and every frame one compute pass (FPlanetInstanceWriterCS) multiplies every planet
// instance by its primitive's transform of the frame, on the GPU. Moving one of these costs what moving a single mesh
// does.
//
// What they give up, as the engine's own GPU-only instances (PCG's, Niagara's) do: the Lumen scene and the distance
// fields, which need the instances on the CPU. They are lit, shadowed and culled per instance as before.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "PlanetInstancesComponent.generated.h"

UCLASS(ClassGroup = (Slonk))
class SLONK_API UPlanetInstancesComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UPlanetInstancesComponent(const FObjectInitializer& ObjectInitializer);

	/** Replaces every instance (transforms relative to the component) and their custom data (NumCustomDataFloats a
	 *  piece, or none), and boxes them; the primitive is made again with them. */
	void SetPieces(const TArray<FTransform>& Transforms, TConstArrayView<float> CustomData);
	/** Removes every instance. */
	void ClearPieces();
	int32 GetInstanceCount() const { return NumInstances; }
	/** The instances as the store gets them (see Packed), for the tests. */
	TConstArrayView<FVector4f> GetPacked() const { return Packed; }
	void SetNumCustomDataFloats(int32 InNumCustomDataFloats);
	int32 GetNumCustomDataFloats() const { return NumCustomFloats; }

	/**
	 * Has every planet instance in this component's scene written into the GPU scene this frame, through this
	 * component's primitive (call it on one that stays in the scene: the terrain keeps one for it). Game thread, once
	 * a frame; a second call in the same frame does nothing more.
	 */
	void QueueWriteAll();

	//~ UActorComponent / UPrimitiveComponent
	virtual bool ShouldCreatePhysicsState() const override { return false; }
	virtual bool IsNavigationRelevant() const override { return false; }
	virtual bool IsHLODRelevant() const override { return false; }
	virtual bool SupportsStaticLighting() const override { return false; }
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FMatrix GetRenderMatrix() const override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ UStaticMeshComponent
	virtual FPrimitiveSceneProxy* CreateStaticMeshSceneProxy(Nanite::FMaterialAudit& NaniteMaterials, bool bCreateNanite) override;
	virtual bool GetMaterialStreamingData(int32 MaterialIndex, FPrimitiveMaterialInfo& MaterialData) const override;

private:
	/** Per instance: three float4 rows of its 3x4 transform in this component's frame (translation in w), then its custom
	 *  data a float4 at a time; the store's Source layout. */
	TArray<FVector4f> Packed;
	int32 NumInstances = 0;
	int32 NumCustomFloats = 0;
	FBox LocalBox = FBox(ForceInit);
};
