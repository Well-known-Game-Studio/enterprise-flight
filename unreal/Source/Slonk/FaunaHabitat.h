// Slonk - where a world's animals are: pure functions of the planet and the time, safe off the game thread, so the
// game finds the same animal wherever and whenever it looks. The shoals of the sea are mirrored from the terrain
// material's own lattice (terrain_material.py, its water block), so whatever leaps from the sea leaps out of a shoal
// that is drawn there. The fliers live in cells laid on the cube's faces, at most one flock to a cell where the ground
// round it offers a wood, a wet meadow, a shore or a cliff; each flock flies a closed, lobed loop round its cell whose
// period divides the material's hour, so where a flock is follows from the time alone, and only a startle (a craft
// passing low, someone walking under it) moves it off its loop for a couple of minutes. FFaunaField keeps the groups
// about a camera, by its distance from them alone.

#pragma once

#include "CoreMinimal.h"
#include "FaunaProfile.h"
#include "FaunaTypes.h"
#include "PlanetTerrain.h"
#include "TerrainScatter.h"

namespace SlonkFauna
{
	/** The terrain material's shoals: at most one in each cell of a lattice laid on the sea's cube-face projection (the
	 *  plane across the largest axis of the planet-local direction, as the material lays its waves), a whole number of
	 *  cells to the noise period along each axis (about 200 m a cell), so the lattice wraps with the chunk origins. A
	 *  shoal swims a loop 16 to 30 m round a point near the middle of its cell, at 0.6 to 1.5 m/s, and never leaves it. */
	constexpr int32 ShoalCellsPerPeriod = 52;

	struct FShoal
	{
		/** The projection's plane (0 across x, 1 across y, 2 across z) and the cell on the periodic lattice: an id. */
		int32 Axis = 0;
		FIntPoint Key = FIntPoint::ZeroValue;
		/** Its middle at the time asked, planet-local cm, on the sea-level sphere, and the way it swims there (unit,
		 *  along the sea). */
		FVector CentreCm = FVector::ZeroVector;
		FVector Heading = FVector::XAxisVector;
		/** Across it as the material lays it (unit, along the sea): square to Heading on the plane it is drawn on, and so
		 *  a little off square on the sea toward the corners of the cube's faces. */
		FVector Side = FVector::YAxisVector;
		/** Half its length along Heading and half its width along Side, cm (its painted shape's edge, which the material
		 *  frays a tenth either way), and how deep it swims, 3 to 40 m: the material shows it only where the sea is
		 *  deeper than that. */
		double HalfLengthCm = 0.0, HalfWidthCm = 0.0, DepthCm = 0.0;
	};

	/** The shoal of the cell holding a planet-local point (on the sea-level sphere of RadiusCm, or near it) at a time
	 *  (the terrain material's Time: the world's time, UWorld::GetTimeSeconds), exactly as the material hashes it, in
	 *  Density of the cells (its ShoalDensity, SlonkFauna::SeaLifeOf): false if the cell has none. */
	SLONK_API bool ShoalAt(const FVector& PlanetLocalCm, double RadiusCm, float Density, double TimeSec, FShoal& Out);
	/** Where a planet-local point near a shoal lies in its painted shape: 0 at its middle, 1 at its edge. */
	SLONK_API double ShoalShapeAt(const FShoal& S, const FVector& PlanetLocalCm);

	/** A world as its animals see it: its field, its fauna, how much lives there, the living ground's colours, and the
	 *  settings (the density, the kinds allowed). */
	struct FFaunaWorld
	{
		FPlanetTerrainParams P;
		FFaunaProfile Profile;
		FScatterPalette Palette;
		/** How much lives there, 0..1 (APlanetBody::GroundLife: 0 on a dead or airless world). */
		float Life = 0.f;
		/** slonk.Fauna.Density: scales the share of the habitat's cells that hold a group. */
		float Density = 1.f;
		uint32 KindMask = AllKinds;
		/** Whether any group of the kind lives here: life, air, the kind allowed and spawned in this build. */
		bool Spawns(EFaunaKind K) const { return Life > 0.f && Density > 0.f && !P.bAirless && (KindMask & KindBit(K)) != 0u && KindDesc(K).bLive; }
		bool SpawnsAny() const { for (int32 K = 0; K < NumKinds; ++K) { if (Spawns((EFaunaKind)K)) { return true; } } return false; }
		/** The salt every cell's hash takes: the profile's seed and the terrain's. */
		uint64 Salt() const;
		bool operator==(const FFaunaWorld& O) const;
		bool operator!=(const FFaunaWorld& O) const { return !(*this == O); }
	};

	/** A habitat cell of a kind on the cube's faces (the terrain's quadtree's, SlonkTerrain::CubeDir). */
	struct FFaunaCell
	{
		EFaunaKind Kind = EFaunaKind::Flier;
		int32 Face = 0;
		int64 X = 0, Y = 0;
		uint64 Id() const { return ((uint64)Kind << 60) ^ ((uint64)(Face & 7) << 56) ^ ((uint64)(X & 0xFFFFFFF) << 28) ^ (uint64)(Y & 0xFFFFFFF); }
	};
	/** How many of a kind's cells lie along a face's edge on this world (each about the kind's CellCm across). */
	SLONK_API int64 CellsAcross(const FPlanetTerrainParams& P, EFaunaKind K);
	/** The cell holding a planet-local unit direction. */
	SLONK_API FFaunaCell CellOf(const FPlanetTerrainParams& P, EFaunaKind K, const FVector& Dir);
	/** Every cell of the kind within RadiusCm of a direction along the ground (the cells under a disc RadiusCm plus a
	 *  cell across, so any group placed within RadiusCm is in one), once each, nearest first. */
	SLONK_API void CellsNear(const FPlanetTerrainParams& P, EFaunaKind K, const FVector& Dir, double RadiusCm, TArray<FFaunaCell>& Out);

	/** What a flier's habitat cell offers round a point: its ground read at the point and at six points on rings of
	 *  150 m and 400 m round it (a shore and a cliff are things near a place, not at it), and whether there is water on
	 *  a ring of six at 900 m too (a shore's birds range along it); each habitat's suitability 0..1, and the best of
	 *  them. None (all 0) on open water, ice, near the snowline, or on a locked world's ground too hot or frozen for
	 *  life. */
	struct FFlierHabitatRead
	{
		float Score[(int32)EFlierHabitat::Count] = {};
		EFlierHabitat Best = EFlierHabitat::Woodland;
		float BestScore = 0.f;
		/** Of the thirteen points read, those on water and on land; and on water out to 900 m. */
		int32 WaterPoints = 0, LandPoints = 0, ShoreWaterPoints = 0;
		double MaxForest = 0.0, MaxCliff = 0.0, MeanWet = 0.0, MeanFlat = 0.0, CentreHeightCm = 0.0;
	};
	SLONK_API FFlierHabitatRead FlierHabitatAt(const FPlanetTerrainParams& P, const FVector& Dir);

	/**
	 * A group living in a habitat cell: everything about it that does not change, a pure function of the world and the
	 * cell. Its path is a loop in the plane tangent at its middle, r(theta) = Radius (1 + Lobe cos(Lobes theta + phi))
	 * + Meander sin(MeanderK theta + 1.7 phi), theta turning once a PeriodSec at a constant rate, laid onto the ground
	 * and flown CruiseCm over it (the ground along the loop sampled once, taken at its highest a few samples either side
	 * so a flock rises ahead of a ridge) and rising and falling BobCm over it. The period divides ClockPeriodSec.
	 */
	struct FFaunaGroup
	{
		uint64 Id = 0;
		EFaunaKind Kind = EFaunaKind::Flier;
		EFlierHabitat Habitat = EFlierHabitat::Woodland;
		uint32 Seed = 0;
		/** Its loop's middle (unit, planet-local) and the plane's axes there. */
		FVector Centre = FVector::ZAxisVector;
		FVector East = FVector::XAxisVector;
		FVector North = FVector::YAxisVector;
		double RadiusCm = 0.0, Lobe = 0.0, LobePhase = 0.0, MeanderCm = 0.0;
		int32 Lobes = 2, MeanderK = 4, BobK = 2;
		double PeriodSec = 300.0, Phase0 = 0.0, Turn = 1.0;
		double CruiseCm = 0.0, BobCm = 0.0;
		/** The loop's length and the speed round it, cm and cm/s. */
		double LengthCm = 0.0, SpeedCmS = 0.0;
		/** The ground along the loop, cm over the sea-level sphere (the water's surface over water), NumGround evenly in
		 *  theta from 0. */
		TArray<float> Ground;
		/** Its members: how many, their span (m), how far apart (m), their wingbeat (Hz), pigment and glow. */
		int32 Members = 0;
		float SpanM = 0.4f;
		float SpacingM = 1.f;
		float FlapHz = 4.f;
		FLinearColor Pigment = FLinearColor::Gray;
		float Glow = 0.f;
		/** The farthest any point of its loop lies from its middle, cm. */
		double ExtentCm() const { return RadiusCm * (1.0 + Lobe) + MeanderCm; }
		bool operator==(const FFaunaGroup& O) const;
	};
	constexpr int32 NumGround = 48;
	/** The group a cell holds, if it holds one (false if not, or if its kind does not live here). */
	SLONK_API bool GroupInCell(const FFaunaWorld& W, const FFaunaCell& C, FFaunaGroup& Out);
	/** The nearest group of a kind to a direction, searched out to MaxCm over at most MaxCells cells: a debug search,
	 *  synchronous and coarse (the cells under a spiral of points, nearest first). */
	SLONK_API bool FindGroupNear(const FFaunaWorld& W, EFaunaKind K, const FVector& Dir, double MaxCm, int32 MaxCells, FFaunaGroup& Out);

	/** Where a group's frame is on its path at a time: planet-local, its frame (x along its heading, level, z up from the
	 *  planet), its velocity, cm/s, and the bank its members lean into the turn with, radians (positive lifting the
	 *  right wing, so negative in a turn to the right). */
	struct FFaunaPose
	{
		FVector PosCm = FVector::ZeroVector;
		FQuat Rot = FQuat::Identity;
		FVector VelCmS = FVector::ZeroVector;
		double Bank = 0.0;
	};
	/** The group's frame on its path at a time (seconds on the world's clock, wrapped to ClockPeriodSec first). */
	SLONK_API FFaunaPose PathAt(const FFaunaGroup& G, double RadiusCm, double Seconds);
	/** Its members in its frame (cm; scaled by their span in metres, the unit mesh being 1 m across), and their custom
	 *  floats (Custom::Num each). */
	SLONK_API void MembersOf(const FFaunaGroup& G, TArray<FTransform>& OutXf, TArray<float>& OutCustom);

	/** Something that may scare a group: a craft (bCraft: its height over the ground, cm) or someone on foot,
	 *  planet-local. */
	struct FFaunaThreat
	{
		FVector PosCm = FVector::ZeroVector;
		FVector VelCmS = FVector::ZeroVector;
		double AltitudeCm = -1.0;
		bool bCraft = true;
	};
	/** A group's fright: when it was last startled (the path clock's seconds), which its panic eases from; the way it
	 *  fled (planet-local unit, away from what scared it and up), and how far off its path it was at that moment, which
	 *  fades as the new flight runs; when its members' scatter opened and the way it opened, which a startle while it is
	 *  still open leaves as they are; and when its wingbeat's urge began to come in. The material runs the scatter's
	 *  shape, the panic and the urge's coming-in each from its own moment (GroupData), so a startle that restarts the
	 *  panic and turns the flight leaves the scatter as it is and the wings beating as they were. */
	struct FPanic
	{
		double T0 = -1.0e30;
		FVector Dir = FVector::ZeroVector;
		FVector Carry = FVector::ZeroVector;
		double ScatterT0 = -1.0e30;
		FVector ScatterDir = FVector::ZeroVector;
		/** When its wingbeat's urge began to come in (over 0.3 s, in the material): the moment a startle found it calm.
		 *  A startle while it is shaken leaves the urge where it stands: in full it stays in full (this set a second
		 *  back, so the material's clock never wraps round to it however long the fright is kept up), and eased as the
		 *  fright wears on it comes back from where it had eased to (this set back as far as the coming-in has got
		 *  there). */
		double OnsetT0 = -1.0e30;
		bool IsLive(double Now) const { return Now >= T0 && Now - T0 < PanicSec; }
		/** Whether its members' scatter is still open (ScatterSec from when it opened, while the fright lasts). */
		bool IsScattered(double Now) const { return IsLive(Now) && Now - ScatterT0 < ScatterSec; }
	};
	/** How far a group's frame is off its path for its fright at a time, planet-local cm: away fast, held a while, back
	 *  by PanicSec (exactly zero from then). */
	SLONK_API FVector PanicOffset(const FPanic& P, const FFaunaKindDesc& D, double Now);
	/** Whether a threat scares a group at a place: a craft below the kind's ScareAltitudeCm and within its
	 *  ScareRadiusCm, someone on foot within CrewScareCm. */
	SLONK_API bool Scares(const FFaunaKindDesc& D, const FVector& GroupPosCm, const FFaunaThreat& T);
	/** Startles a group at its place now if the threat scares it (unless it was startled in the last RestartleSec, or is
	 *  already half again its flight off its path): true if it did, as StartleNow. */
	SLONK_API bool StartleGroup(FPanic& P, const FFaunaKindDesc& D, const FVector& GroupPosCm, const FFaunaThreat& T, double Now);
	/** Startles a group at its place now, however lately it was: it flees away from the threat (or off its track, the
	 *  threat overhead), what is left of its last flight carried; its panic runs from now; its members' scatter, if still
	 *  open, is kept, and its wingbeat's urge comes in from where it stands (FPanic::OnsetT0). */
	SLONK_API void StartleNow(FPanic& P, const FFaunaKindDesc& D, const FVector& GroupPosCm, const FFaunaThreat& T, double Now);
	/** A group's fright as the material reads it, into its custom primitive data (GroupData::Num floats): when its scatter
	 *  opened, the way it opened in the group's frame (Rot, its frame's rotation now), whether it is live, when it was
	 *  last startled and when its urge began to come in, each moment on the material's clock (the paths' clock less
	 *  ClockOffsetSec, wrapped to ClockPeriodSec); all 0 once the fright is over. The other slots are left as they are. */
	SLONK_API void FrightData(const FPanic& P, double Now, const FQuat& Rot, double ClockOffsetSec, float* Group);

	/**
	 * The material's motion of a flier's vertex (fauna_material.py, WPO_CODE), transliterated in floats as the GPU runs
	 * it: the offset of a vertex at LP (the unit mesh's cm, in its instance's frame) with its vertex colour, at the
	 * material's time T, for a member's custom floats (Custom::Num) in a group's custom primitive data (GroupData::Num),
	 * DistCm from the camera, which sees PxPerRad pixels a radian. In the instance's frame, unit-mesh cm.
	 */
	SLONK_API FVector3f FlierMotion(const FVector3f& LP, const FLinearColor& VC, float T, const float* Custom, const float* Group, float DistCm, float PxPerRad);

	/**
	 * The groups kept about a camera. A scan reads the cells round the camera's ground point (on a task: Scan is pure),
	 * reusing those the last scan read, once the ground point has moved a sixth of a cell; every frame, Select spawns
	 * the scan's groups whose middles have come within their kind's reach of the ground point, nearest first and within
	 * the kind's caps (with the caps full, the farthest of the kind's groups out of sight and farther off by a sixth of a
	 * cell make room for a nearer one), and lets go of those that have gone beyond SpawnHysteresis times it (or under a
	 * camera too high for them). Distance alone decides, so a group looked away from and back at is still there, and a
	 * group is the same group (the same id, on the same path) whenever it is found again.
	 */
	class SLONK_API FFaunaField
	{
	public:
		using FGroupRef = TSharedPtr<const FFaunaGroup, ESPMode::ThreadSafe>;
		using FCellCache = TMap<uint64, FGroupRef>;
		/** A scan's cells (a null group for a cell holding none) and the groups among them; the ground's height under its
		 *  middle; and how long it took. */
		struct FScan
		{
			FVector CentreDir = FVector::ZAxisVector;
			double GroundCm = 0.0;
			TArray<uint64> CellIds;
			TArray<FGroupRef> CellGroups;
			int32 Evaluated = 0;
			double Ms = 0.0;
		};

		/** A changed world (another density, a kind switched off, a new profile) clears everything. */
		void SetWorld(const FFaunaWorld& W);
		const FFaunaWorld& GetWorld() const { return World; }
		bool HasWorld() const { return bHasWorld; }
		/** Whether the camera's ground point (planet-local unit) has moved far enough from the last scan's for another. */
		bool WantsScan(const FVector& GroundDir) const;
		/** How far round its middle a scan reads, cm: every live kind's reach, its hysteresis, a sixth of a cell for the
		 *  camera's drift between scans, and a cell. */
		double ScanRadiusCm() const;
		/** The cells round a ground direction, those in Cache taken from it: pure, safe on a task. */
		static TSharedRef<FScan, ESPMode::ThreadSafe> Scan(const FFaunaWorld& W, const FVector& GroundDir, double RadiusCm, TSharedPtr<const FCellCache, ESPMode::ThreadSafe> Cache);
		/** Takes a finished scan: its cells become the cache, its groups the candidates. */
		void Accept(const FScan& S);
		TSharedPtr<const FCellCache, ESPMode::ThreadSafe> GetCache() const { return Cache; }
		/** Spawns and lets go of groups for a camera at a planet-local position: the ids spawned and let go this time. */
		void Select(const FVector& CamLocal, TArray<FGroupRef>& OutSpawned, TArray<uint64>& OutLetGo);
		const TMap<uint64, FGroupRef>& GetActive() const { return Active; }
		const TArray<FGroupRef>& GetCandidates() const { return Candidates; }
		int32 ActiveMembers(EFaunaKind K) const { return Members[(int32)K]; }
		int32 ActiveGroups(EFaunaKind K) const { return Groups[(int32)K]; }
		/** The ground's height under the last scan's middle, cm. */
		double GetGroundCm() const { return GroundCm; }
		/** Lets every group go and forgets the scans (the cache is kept unless bForgetCells). */
		void Clear(bool bForgetCells);
		/** The camera's height over the ground a group's kind is kept under, from a planet-local position. */
		double CameraAltitudeCm(const FVector& CamLocal) const { return CamLocal.Size() - World.P.RadiusCm - GroundCm; }

	private:
		void LetGo(uint64 Id);
		FFaunaWorld World;
		bool bHasWorld = false;
		TSharedPtr<const FCellCache, ESPMode::ThreadSafe> Cache;
		TArray<FGroupRef> Candidates;
		TMap<uint64, FGroupRef> Active;
		FVector LastScanDir = FVector::ZeroVector;
		bool bScanned = false;
		double GroundCm = 0.0;
		int32 Members[NumKinds] = {};
		int32 Groups[NumKinds] = {};
	};
}
