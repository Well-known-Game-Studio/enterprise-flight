// Slonk - chunked quadtree terrain on a cube-sphere. Each planet with relief gets one of these: six root chunks
// cover the globe from space, and the quadtree splits toward the camera down to metre-sized cells, so the same
// surface serves the view from orbit and a flight along a canyon floor. Chunk meshes are built off the game
// thread from the height field in PlanetTerrain.h, in doubles, and stored relative to the chunk's own origin, so
// float vertex positions stay exact to the centimetre on a 6,000 km body.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "PlanetTerrain.h"
#include "TerrainScatter.h"
#include "Tasks/Task.h"
#include "PlanetTerrainComponent.generated.h"

class UDynamicMeshComponent;
class UScatterInstancesComponent;
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

	/** Chunks with edges shorter than this cast their rocks' and bushes' shadows; trees cast them from chunks twice
	 *  this size, and grass never does. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Scatter")
	float ScatterShadowEdgeCm = 8000.f;

	/** Rocks and tree trunks within this of the pawn are solid to the crew's boots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Scatter")
	float ScatterSolidReachCm = 4000.f;

	/** Plants stop swaying in the wind beyond this, cm: their shadows and motion cost nothing further out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Scatter")
	float PlantWindDistanceCm = 30000.f;

	/** Set the field, material and radius; clears any built chunks. */
	void Configure(const FPlanetTerrainParams& InParams, UMaterialInterface* InMaterial);

	/** The rocks and plants on the ground (TerrainScatter.h): on or off, how many, whether there are towns to keep
	 *  clear, and how much grows. A change rebuilds the chunks fine enough to carry any, in place. */
	void SetScatterOptions(bool bEnabled, float Density, bool bCities, float Life);
	/** Makes the rocks round a world position solid for a while, wherever the pawn is: a transporter's destination,
	 *  so the arrival trace stands the person on a boulder rather than inside it. */
	void PinSolidRocks(const FVector& WorldPos, double Seconds);
	const FScatterSettings& GetScatterSettings() const { return Scatter; }
	/** Rocks and plants on the chunks showing, the rocks and trunks with collision round the pawn, and the instance
	 *  components made. */
	int32 GetVisibleRockCount() const { return VisibleRocks; }
	int32 GetVisiblePlantCount() const { return VisiblePlants; }
	int32 GetSolidRockCount() const { return SolidRocks; }
	int32 GetScatterSetCount() const { return SetPool.Num(); }
	/** Traces the boots' channel down through the solid rocks nearest the pawn, up to MaxRocks of them: how many
	 *  were tried, and in OutHits how many the trace found solid. A check that what is drawn is what is stood on. */
	int32 CheckSolidRocks(int32 MaxRocks, int32& OutHits) const;

	/** The face and the chunk (X, Y) at a depth under a planet-local direction. */
	static bool ChunkAt(const FVector& Dir, int32 Depth, int32& OutFace, int64& OutX, int64& OutY);
	/** The rocks and plants a chunk carries, built as a chunk build builds them, as planet-local transforms by mesh;
	 *  the drawn height (cm above sea level) at each one's direction, from the chunk's own triangles; and the chunk's
	 *  origin, planet-local. For tests. */
	static void BuildChunkScatterForTest(const FPlanetTerrainParams& P, const FScatterSettings& S, int32 Face, int32 Depth, int64 X, int64 Y, int32 Steps,
		TArray<FTransform> (&OutPlanetLocal)[SlonkScatter::NumMeshes], TArray<double> (&OutDrawnHeightCm)[SlonkScatter::NumMeshes], FVector* OutOrigin = nullptr);

	/** Pick LOD for this view (world space), launch builds, swap finished meshes in. Call a few times a second. */
	/** Refine for the camera and for the pawn (the ground it stands on matters a chase camera's length away). */
	void UpdateView(const FVector& ViewLocationWorld, const FVector& PawnLocationWorld);

	void SetTerrainVisible(bool bShow);
	bool IsTerrainVisible() const { return bTerrainVisible; }

	/**
	 * Ground height (cm above the sea-level sphere; the sea is 0) under a world position: the drawn mesh where a
	 * chunk is on screen there (the deepest one showing, its triangles interpolated), else the field at full detail.
	 * The two differ by metres on rough ground (a chunk refines from the slope of its own cells, the field from a
	 * metre's), and feet, skids and hover heights have to agree with what is drawn.
	 */
	double SurfaceHeightAtWorld(const FVector& WorldPos) const;
	/** The drawn mesh's height above sea level in a planet-local direction; false where no chunk is showing. */
	bool DrawnHeightAtLocalDir(const FVector& DirLocal, double& OutHeightCm) const;
	/** The full field sample under a world position. */
	FPlanetTerrainSample SampleAtWorld(const FVector& WorldPos, double MinWavelengthCm = 0.0) const;

	const FPlanetTerrainParams& GetParams() const { return Params; }

	/**
	 * Searches the globe for a landmark: "canyon" (deepest canyon floor), "rim", "river", "mountain" (highest ground), "land" (any
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
		/** Height above sea level (water surface over water) at each grid vertex, row-major (Steps + 1)^2. */
		TArray<float> Heights;
		int32 Steps = 0;
		SlonkScatter::FChunkScatter Scatter;
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
		/** The grid heights of the mesh this node shows (see FBuildResult), for ground queries. */
		TArray<float> Heights;
		int32 HeightSteps = 0;
		/** The chunk's rocks and plants by mesh, attached to its mesh and shown and hidden with it. */
		UScatterInstancesComponent* Sets[SlonkScatter::NumMeshes] = {};
		int32 RockCount = 0;
		int32 PlantCount = 0;
		/** The rocks and trees of them big enough to stand on or walk into, chunk-relative, for the collision round
		 *  the pawn. */
		TArray<FTransform> Solids[SlonkScatter::NumMeshes];
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
	/** The chunk's mesh and its rocks together. */
	void SetChunkVisible(FNode& N, bool bShow);
	UDynamicMeshComponent* AcquireMesh();
	void ApplyScatter(FNode& N, SlonkScatter::FChunkScatter& Sc);
	void ReleaseScatter(FNode& N);
	/** An instanced set for one scatter mesh (SlonkScatter's index: the rock shapes, then the plants). */
	UScatterInstancesComponent* AcquireSet(int32 Mesh);
	/** Collision for the rocks round the pawn: rebuilt when it has walked a few metres or the chunks there changed. */
	void UpdateSolidRocks();
	void ClearSolidRocks();
	void RebuildScatterChunks();
	void MakeRoots();
	void ChunkFrame(int32 Face, int32 Depth, int64 X, int64 Y, FVector& OutCentre, double& OutEdge) const;
	static FVector CubeDir(int32 Face, double U, double V);
	/** The inverse of CubeDir: which face a direction falls on and its (U, V) there. */
	static bool FaceUV(const FVector& Dir, int32& OutFace, double& OutU, double& OutV);
	/** The drawn height at (U, V) from a chunk's grid of heights, on its triangles. */
	static double GridHeightAt(const TArray<float>& Heights, int32 Steps, int32 Depth, int64 X, int64 Y, double U, double V);
	static void BuildChunk(const FPlanetTerrainParams& P, const FScatterSettings& Sc, int32 Face, int32 Depth, int64 X, int64 Y, int32 Steps, FBuildResult& Out);

	FPlanetTerrainParams Params;
	FScatterSettings Scatter;
	UPROPERTY() TObjectPtr<UMaterialInterface> Material;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MID;
	UPROPERTY() TArray<TObjectPtr<UDynamicMeshComponent>> Pool;
	TArray<UDynamicMeshComponent*> FreeMeshes;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RockMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FoliageMID;
	UPROPERTY() TArray<TObjectPtr<UScatterInstancesComponent>> SetPool;
	TArray<UScatterInstancesComponent*> FreeSets[SlonkScatter::NumMeshes];
	/** Collision-only copies of the rocks round the pawn, one per kind, planet-local. */
	UPROPERTY() TArray<TObjectPtr<UScatterInstancesComponent>> SolidSets;
	uint64 SolidSignature = 0;
	FVector SolidCentre = FVector(1.0e30, 0.0, 0.0);
	FVector SolidLastPawn = FVector::ZeroVector;
	/** Planet-local; the rocks round it stay solid until SolidPinUntil (world seconds). */
	FVector SolidPinLocal = FVector::ZeroVector;
	double SolidPinUntil = -1.0;
	int32 VisibleRocks = 0;
	int32 VisiblePlants = 0;
	int32 SolidRocks = 0;
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
	FVector PawnLocal = FVector::ZeroVector;
};
