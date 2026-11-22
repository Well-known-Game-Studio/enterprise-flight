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
class UPlanetInstancesComponent;
class UStaticMeshComponent;
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
	void SetScatterOptions(bool bEnabled, float Density, bool bCities, float Life, const FCivilization& People);
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
	/** The instances and instance sets on the chunks showing, by scatter mesh, as of the last view update. */
	FString DescribeVisibleScatter() const;
	/** How many of each class's far levels to use (FScatterSettings::MaxFarLevels); a change rebuilds the scatter. */
	void SetScatterFarLevels(int32 Levels);

	/** The motes over the ground, carried on the chunks with the scatter (SlonkScatter::FMoteSettings, from the world's
	 *  fauna): a change that moves them builds again every chunk fine enough to carry any (RebuildChunksUpTo, as a change
	 *  of the scatter's settings does), the builds in flight waited for first; until they land the near ground shows the
	 *  coarser chunks above, with the little their level carries (no grass, stones or shrubs) and nothing solid round the
	 *  pawn. Their glow goes to the material alone.
	 *  Drawn in M_Fauna's mote branch; while M_Fauna has none (built before it) there are none, unless slonk.Fauna.Debug
	 *  draws them in the engine's basic material. */
	void SetMoteOptions(const SlonkScatter::FMoteSettings& InMotes);
	const SlonkScatter::FMoteSettings& GetMoteSettings() const { return Motes; }
	/** The mote settings the chunk builds take: none while nothing can draw them. */
	SlonkScatter::FMoteSettings MoteBuildSettings() const;
	/** Whether M_Fauna has its mote branch (else nothing draws the motes but slonk.Fauna.Debug's basic material). */
	bool HasMoteMaterial() const { return bMoteMaterial; }
	/** The mote clouds on the chunks showing, as of the last view update, and a line for the debug readout. */
	int32 GetVisibleMoteCount() const { return VisibleMotes; }
	FString DescribeMotes() const;
	/** How far from the camera the trees reach on this world under the settings now, cm, and how many far levels that is. */
	double GetTreeReachCm(int32* OutFarLevels = nullptr) const;
	/** The scatter settings the chunk builds take: the settings, with the fade set from the split factor. */
	FScatterSettings BuildScatterSettings() const;
	/** Traces the boots' channel down through the solid rocks nearest the pawn, up to MaxRocks of them: how many
	 *  were tried, and in OutHits how many the trace found solid. A check that what is drawn is what is stood on. */
	int32 CheckSolidRocks(int32 MaxRocks, int32& OutHits) const;

	/** The face and the chunk (X, Y) at a depth under a planet-local direction. */
	static bool ChunkAt(const FVector& Dir, int32 Depth, int32& OutFace, int64& OutX, int64& OutY);
	/** The rocks and plants a chunk carries, built as a chunk build builds them, as planet-local transforms by mesh;
	 *  the drawn height (cm above sea level) at each one's direction, from the chunk's own triangles; and the chunk's
	 *  origin, planet-local; and, if asked, the forest's cover at each grid vertex as the material reads it (UV3.y,
	 *  SlonkScatter::TreeCover). For tests. */
	static void BuildChunkScatterForTest(const FPlanetTerrainParams& P, const FScatterSettings& S, int32 Face, int32 Depth, int64 X, int64 Y, int32 Steps,
		TArray<FTransform> (&OutPlanetLocal)[SlonkScatter::NumMeshes], TArray<double> (&OutDrawnHeightCm)[SlonkScatter::NumMeshes], FVector* OutOrigin = nullptr, TArray<float>* OutTreeCover = nullptr);
	/** Everything a chunk build makes, as one CRC: the drawn vertices, their normals and every attribute the material
	 *  reads (the forest's cover among them), the grid's heights, the triangles, and the rocks' and plants' transforms
	 *  and custom data. Two builds agree bit for bit when their CRCs do. For the tests that hold a world's look fixed. */
	static uint32 ChunkBuildCrcForTest(const FPlanetTerrainParams& P, const FScatterSettings& S, int32 Face, int32 Depth, int64 X, int64 Y, int32 Steps = 64,
		const SlonkScatter::FMoteSettings* Motes = nullptr);
	/** The mote clouds a chunk carries, built as a chunk build builds them: their planet-local transforms and custom floats,
	 *  the drawn height (cm above sea level) at each one's direction from the chunk's own triangles, and the chunk's
	 *  origin. For tests. */
	static void BuildChunkMotesForTest(const FPlanetTerrainParams& P, const FScatterSettings& S, const SlonkScatter::FMoteSettings& M, int32 Face, int32 Depth, int64 X, int64 Y,
		int32 Steps, TArray<FTransform>& OutPlanetLocal, TArray<float>& OutCustom, TArray<double>& OutDrawnHeightCm, FVector* OutOrigin = nullptr);
	/** A chunk's height grid as a chunk build makes it (the heights of its drawn triangles' corners, cm above sea level,
	 *  (Steps + 1)^2), and the drawn height in such a grid at a planet-local direction, from its triangles, as
	 *  DrawnHeightAtLocalDir reads a chunk on screen. For tests. */
	static void BuildChunkHeightsForTest(const FPlanetTerrainParams& P, int32 Face, int32 Depth, int64 X, int64 Y, int32 Steps, TArray<float>& OutHeights);
	static double ChunkHeightAtForTest(const TArray<float>& Heights, int32 Steps, int32 Depth, int64 X, int64 Y, const FVector& Dir);

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
	 * ground above sea near the given direction), "coast"; and on a world the tides hold, "substellar" (the dry ground
	 * nearest the point under the star), "ring" (the temperate ring's sunlit edge along the terminator) and "nightcap"
	 * (the middle of the far side's ice). Returns the planet-local unit direction and the ground
	 * height there; false if nothing qualifies. Coarse (a few thousand samples) and synchronous: debug use.
	 */
	bool FindFeature(const FString& Kind, const FVector& NearDir, FVector& OutDir, double& OutHeightCm, const FVector& SunDirLocal = FVector::ZeroVector) const;
	int32 GetVisibleChunkCount() const { return VisibleChunks; }
	int32 GetBuiltChunkCount() const { return BuiltChunks; }
	int32 GetPendingBuildCount() const { return PendingBuilds; }
	int32 GetDeepestVisibleDepth() const { return DeepestVisible; }
	int32 GetParkedChunkCount() const { return ParkedChunks; }

	/** Has every planet instance in the world's scene written into the GPU scene this frame (UPlanetInstancesComponent::
	 *  QueueWriteAll), through this terrain's anchor, made if there is none: for planet instances that are not the
	 *  scatter's (the animals, UPlanetFaunaComponent), which need it whatever slonk.Scatter.GPUInstances says. Once a
	 *  frame at most, with the view update's own. */
	void QueuePlanetInstanceWrites();

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
		/** The chunk's edge as the build measures it, cm: the one its scatter is placed by (SlonkScatter::FChunkGrid). */
		double EdgeCm = 0.0;
		SlonkScatter::FChunkScatter Scatter;
		/** Its fauna, apart from the scatter (whose every piece the build CRCs hash): the mote clouds. */
		SlonkScatter::FChunkFauna Fauna;
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
		/** The edge its mesh's build measured, cm (FBuildResult::EdgeCm), which says whether that mesh carries scatter:
		 *  across a face it runs from 0.7 to 1 of EdgeCm, the one the quadtree splits by. */
		double BuiltEdgeCm = 0.0;
		/** The chunk's rocks and plants by mesh, attached to its mesh and shown and hidden with it: GPU-only planet instances,
		 *  or (slonk.Scatter.GPUInstances 0) the engine's instanced meshes. */
		UStaticMeshComponent* Sets[SlonkScatter::NumMeshes] = {};
		int32 RockCount = 0;
		int32 PlantCount = 0;
		/** The rocks and trees of them big enough to stand on or walk into, chunk-relative, for the collision round
		 *  the pawn. */
		TArray<FTransform> Solids[SlonkScatter::NumMeshes];
		/** Its mote clouds: one set in M_Fauna, attached to its mesh as the scatter's sets are, shown and hidden with it. */
		UStaticMeshComponent* MoteSet = nullptr;
		int32 MoteCount = 0;
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
	/** The chunk's mote clouds onto its set (made, or pooled, as they come and go). */
	void ApplyMotes(FNode& N, SlonkScatter::FChunkFauna& Fauna);
	UStaticMeshComponent* AcquireMoteSet();
	/** The motes' material: M_Fauna with its mote branch switched on, or slonk.Fauna.Debug's basic material; made again
	 *  when that setting changes. The basic material is tried once for each setting; an M_Fauna that does not load, or
	 *  loads without its mote branch, is looked at again every 5 s (FaunaProbedAt), not every frame. It builds nothing
	 *  again itself: SetMoteOptions does, when what it makes changes where the motes are. */
	void EnsureFaunaMaterial();
	/** An instanced set for one scatter mesh (SlonkScatter's index: the rock shapes, then the plants). */
	UStaticMeshComponent* AcquireSet(int32 Mesh);
	/** Makes WriterAnchor if there is none and a scene to put it in. */
	void EnsureWriterAnchor();
	/** Collision for the rocks round the pawn: rebuilt when it has walked a few metres or the chunks there changed. */
	void UpdateSolidRocks();
	void ClearSolidRocks();
	/** Builds again the chunks fine enough to carry scatter, at the far levels used before the change (FarLevelsBefore)
	 *  or after it, whichever reach further, so a chunk losing its scatter lets it go too. */
	void RebuildScatterChunks(int32 FarLevelsBefore);
	/** Builds again every chunk whose build measured an edge no longer than MaxEdgeCm, the builds in flight landed first. */
	void RebuildChunksUpTo(double MaxEdgeCm);
	/** The scatter settings the ground's material paints by: the forest's colour (TreeLife) and the settlements'
	 *  clearings (Clearings). */
	void ApplyScatterToMaterial();
	void MakeRoots();
	void ChunkFrame(int32 Face, int32 Depth, int64 X, int64 Y, FVector& OutCentre, double& OutEdge) const;
	static FVector CubeDir(int32 Face, double U, double V);
	/** The inverse of CubeDir: which face a direction falls on and its (U, V) there. */
	static bool FaceUV(const FVector& Dir, int32& OutFace, double& OutU, double& OutV);
	/** The drawn height at (U, V) from a chunk's grid of heights, on its triangles. */
	static double GridHeightAt(const TArray<float>& Heights, int32 Steps, int32 Depth, int64 X, int64 Y, double U, double V);
	static void BuildChunk(const FPlanetTerrainParams& P, const FScatterSettings& Sc, const SlonkScatter::FMoteSettings& Mo, int32 Face, int32 Depth, int64 X, int64 Y, int32 Steps,
		FBuildResult& Out);

	FPlanetTerrainParams Params;
	FScatterSettings Scatter;
	UPROPERTY() TObjectPtr<UMaterialInterface> Material;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MID;
	UPROPERTY() TArray<TObjectPtr<UDynamicMeshComponent>> Pool;
	TArray<UDynamicMeshComponent*> FreeMeshes;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RockMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FoliageMID;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> SetPool;
	TArray<UStaticMeshComponent*> FreeSets[SlonkScatter::NumMeshes];
	SlonkScatter::FMoteSettings Motes;
	/** The motes' material (EnsureFaunaMaterial), whether it is M_Fauna with its mote branch, and the slonk.Fauna.Debug it
	 *  was made under (-1: none made yet). */
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FaunaMID;
	bool bMoteMaterial = false;
	int32 FaunaMIDDebug = -1;
	/** When M_Fauna was last looked at for its mote branch, game seconds: one without it is looked at again now and then,
	 *  so a material built again while the world runs (the editor's) brings its motes in. */
	double FaunaProbedAt = -1.0e9;
	/** What it was last given, so a material made again has it too and nothing unchanged is sent again. */
	FLinearColor FaunaSunDirection = FLinearColor(0.f, 0.f, 1.f, 0.f);
	FLinearColor FaunaGlow = FLinearColor::Black;
	float FaunaPxPerRad = -1.f;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> MoteSetPool;
	TArray<UStaticMeshComponent*> FreeMoteSets;
	int32 VisibleMotes = 0;
	/** Always in the scene while the terrain is: carries each frame's write of every planet instance into the GPU scene
	 *  (UPlanetInstancesComponent::QueueWriteAll), one empty instance of its own. */
	UPROPERTY() TObjectPtr<UPlanetInstancesComponent> WriterAnchor;
	/** The frame the anchor's write was last queued in (GFrameCounter). */
	uint64 WriterQueuedFrame = MAX_uint64;
	/** Collision-only copies of the rocks round the pawn, one per kind, planet-local. */
	UPROPERTY() TArray<TObjectPtr<UScatterInstancesComponent>> SolidSets;
	uint64 SolidSignature = 0;
	FVector SolidCentre = FVector(1.0e30, 0.0, 0.0);
	FVector SolidLastPawn = FVector::ZeroVector;
	/** Planet-local; the rocks round it stay solid until SolidPinUntil (world seconds). */
	FVector SolidPinLocal = FVector::ZeroVector;
	double SolidPinUntil = -1.0;
	int32 VisibleRocks = 0;
	int32 VisibleByMesh[SlonkScatter::NumMeshes] = {};
	int32 VisibleSetsByMesh[SlonkScatter::NumMeshes] = {};
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
