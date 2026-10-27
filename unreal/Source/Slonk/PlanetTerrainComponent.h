// Slonk - chunked quadtree terrain on a cube-sphere. Each planet with relief gets one of these: six root chunks
// cover the globe from space, and the quadtree splits toward the camera down to metre-sized cells, so the same
// surface serves the view from orbit and a flight along a canyon floor. Chunk meshes are built off the game
// thread from the height field in PlanetTerrain.h, in doubles, and stored relative to the chunk's own origin, so
// float vertex positions stay exact to the centimetre on a 6,000 km body.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "PlanetTerrain.h"
#include "Tasks/Task.h"
#include "PlanetTerrainComponent.generated.h"

class UDynamicMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
namespace UE::Geometry { class FDynamicMesh3; }

UCLASS(ClassGroup = (Slonk), meta = (BlueprintSpawnableComponent))
class SLONK_API UPlanetTerrainComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPlanetTerrainComponent();

	/** Vertices per chunk edge (cells = steps, vertices = steps + 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "8", ClampMax = "128"))
	int32 GridSteps = 64;

	/** A chunk splits while the camera is closer than this many chunk edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	float SplitFactor = 1.6f;

	/** Finest cell the quadtree will go to, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	float MinCellCm = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "1", ClampMax = "24"))
	int32 MaxDepth = 22;

	/** Built chunks that have scrolled out of use are kept hidden, up to this many, so coming back is instant. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0", ClampMax = "4000"))
	int32 ParkedBudget = 320;

	/** Chunks with edges shorter than this cast shadows (the far ones only churn the shadow maps). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	float ShadowCastEdgeCm = 6.0e6f;

	/** Builds in flight at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxConcurrentBuilds = 8;

	/** Meshes handed to the renderer per update. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxUploadsPerUpdate = 6;

	/** Set the field, material and radius; clears any built chunks. */
	void Configure(const FPlanetTerrainParams& InParams, UMaterialInterface* InMaterial);

	/** Pick LOD for this view (world space), launch builds, swap finished meshes in. Call a few times a second. */
	void UpdateView(const FVector& ViewLocationWorld);

	void SetTerrainVisible(bool bVisible);
	bool IsTerrainVisible() const { return bTerrainVisible; }

	/** Ground height (cm above the sea-level sphere; the sea is 0) under a world position, at full detail. */
	double SurfaceHeightAtWorld(const FVector& WorldPos) const;
	/** The full field sample under a world position. */
	FPlanetTerrainSample SampleAtWorld(const FVector& WorldPos, double MinWavelengthCm = 0.0) const;

	const FPlanetTerrainParams& GetParams() const { return Params; }

	/**
	 * Searches the globe for a landmark: "canyon" (deepest canyon floor), "mountain" (highest ground), "land" (any
	 * ground above sea near the given direction), "coast". Returns the planet-local unit direction and the ground
	 * height there; false if nothing qualifies. Coarse (a few thousand samples) and synchronous: debug use.
	 */
	bool FindFeature(const FString& Kind, const FVector& NearDir, FVector& OutDir, double& OutHeightCm, const FVector& SunDirLocal = FVector::ZeroVector) const;
	int32 GetVisibleChunkCount() const { return VisibleChunks; }
	int32 GetBuiltChunkCount() const { return BuiltChunks; }
	int32 GetPendingBuildCount() const { return PendingBuilds; }
	int32 GetDeepestVisibleDepth() const { return DeepestVisible; }
	int32 GetParkedChunkCount() const { return ParkedChunks; }

	/** Every surface material parameter set so far is also applied to new chunk instances. */
	void SetScalarParameter(FName Name, float Value);
	void SetVectorParameter(FName Name, const FLinearColor& Value);

	virtual void BeginDestroy() override;

private:
	struct FBuildResult
	{
		TUniquePtr<UE::Geometry::FDynamicMesh3> Mesh;
		FVector Origin = FVector::ZeroVector;   // planet-local cm
		FBox LocalBounds = FBox(ForceInit);
	};
	struct FNode
	{
		int32 Face = 0;
		int32 Depth = 0;
		int64 X = 0, Y = 0;
		/** Unit direction of the chunk centre and its edge length on the sea-level sphere, cm. */
		FVector Centre = FVector::ZAxisVector;
		double EdgeCm = 0.0;
		/** Planet-local position of the chunk origin (centre, sea level). */
		FVector Origin = FVector::ZeroVector;
		/** Ground height at the centre, cm, sampled coarsely when the node is made: LOD distance is measured to it. */
		double CentreHeightCm = 0.0;
		TUniquePtr<FNode> Children[4];
		UDynamicMeshComponent* Mesh = nullptr;
		bool bReady = false;
		bool bBuilding = false;
		TSharedPtr<FBuildResult, ESPMode::ThreadSafe> Result;
		UE::Tasks::TTask<void> Task;
		uint32 LastWanted = 0;
	};

	void ResetTree();
	void FreeSubtree(FNode& N);
	void ReleaseSubtree(FNode& N);
	void EvictParked();
	void ReleaseMesh(FNode& N);
	bool AllChildrenReady(const FNode& N) const;
	void MakeChildren(FNode& N);
	void Select(FNode& N, const FVector& CamLocal, double CamDist, const FVector& CamDir, double HorizonCos);
	void RequestBuild(FNode& N, double Priority);
	void LaunchBuilds();
	void CollectFinished();
	void ApplyResult(FNode& N);
	void ShowNode(FNode& N, bool bShow);
	UDynamicMeshComponent* AcquireMesh();
	void ChunkFrame(int32 Face, int32 Depth, int64 X, int64 Y, FVector& OutCentre, double& OutEdge) const;
	FVector CubeDir(int32 Face, double U, double V) const;
	static void BuildChunk(const FPlanetTerrainParams& P, int32 Face, int32 Depth, int64 X, int64 Y, int32 Steps, FBuildResult& Out);

	FPlanetTerrainParams Params;
	UPROPERTY() TObjectPtr<UMaterialInterface> Material;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MID;
	UPROPERTY() TArray<TObjectPtr<UDynamicMeshComponent>> Pool;
	TArray<UDynamicMeshComponent*> FreeMeshes;
	TUniquePtr<FNode> Roots[6];
	struct FRequest { FNode* Node; double Priority; };
	TArray<FRequest> Requests;
	TArray<FNode*> InFlight;
	uint32 Stamp = 0;
	bool bTerrainVisible = true;
	bool bConfigured = false;
	int32 VisibleChunks = 0;
	int32 BuiltChunks = 0;
	int32 PendingBuilds = 0;
	int32 DeepestVisible = 0;
	int32 ParkedChunks = 0;
	FVector LastCamLocal = FVector::ZeroVector;
};
