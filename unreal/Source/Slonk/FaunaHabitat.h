// Slonk - where a world's animals are: pure functions of the planet and the time, safe off the game thread, so the
// game finds the same animal wherever and whenever it looks. The shoals of the sea are mirrored from the terrain
// material's own lattice (terrain_material.py, its water block), so whatever leaps from the sea leaps out of a shoal
// that is drawn there. The fliers live in cells laid on the cube's faces, at most one flock to a cell where the ground
// round it offers a wood, a wet meadow, a shore or a cliff; each flock flies a closed, lobed loop round its cell whose
// period divides the material's hour, so where a flock is follows from the time alone, and only a startle (a craft
// passing low, someone walking under it) moves it off its loop for a couple of minutes. The herds live in cells of their
// own where a loop of about 2 km round the cell's point lies all on open, level grass, away from the water, the woods
// and the towns; each grazes most of the time and walks and trots now and then, on a schedule of the clock whose
// distance walked is known in closed form, and stands on the ground that is drawn (SlonkFauna::StepHerd). FFaunaField
// keeps the groups about a camera, by its distance from them alone.

#pragma once

#include "Civilization.h"
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
		/** Whether the world has towns (APlanetBody::HasCities), and whose: the herds keep their loops out of them. */
		bool bTowns = false;
		FCivilization People;
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
		/** Its members: how many, their span (m; a walker's length), how far apart (m), their wingbeat (Hz), pigment and
		 *  glow. */
		int32 Members = 0;
		float SpanM = 0.4f;
		float SpacingM = 1.f;
		float FlapHz = 4.f;
		FLinearColor Pigment = FLinearColor::Gray;
		float Glow = 0.f;
		/**
		 * A herd's: it does not fly its loop at a constant rate but walks it on a schedule repeating each CycleSec (a
		 * divisor of its period, which is the hour): grazing, standing, the most of the cycle, then walking at WalkCmS from
		 * WalkA to WalkB (s into the cycle), trotting at TrotCmS from TrotA to TrotB within that, each speed coming in and
		 * going over RampSec; the loop is as long as the hour's schedule walks (LengthCm), from StartCm along it at the
		 * hour's turn. ArcCm is the loop's length from theta 0 at NumArc + 1 even steps of theta, the way along it read
		 * back. Its members' circles' middles lie within FormationCm of its frame's middle and every foot within
		 * HerdRadiusCm; the ground under it is read over a grid GroundHalfCm either way.
		 */
		double CycleSec = 300.0;
		double WalkCmS = 0.0, TrotCmS = 0.0;
		double WalkA = 0.0, WalkB = 0.0, TrotA = 0.0, TrotB = 0.0, RampSec = 4.0;
		double StartCm = 0.0;
		TArray<float> ArcCm;
		double FormationCm = 0.0, HerdRadiusCm = 0.0, GroundHalfCm = 0.0;
		/** The farthest any point of its loop lies from its middle, cm. */
		double ExtentCm() const { return RadiusCm * (1.0 + Lobe) + MeanderCm; }
		bool operator==(const FFaunaGroup& O) const;
	};
	constexpr int32 NumGround = 48;
	constexpr int32 NumArc = 192;
	/** A herd starts to turn to walk this long before its walk comes in, s (it stands and turns first), and is turned back
	 *  to graze once it has stopped. */
	constexpr double HerdLeadSec = 2.5;

	/** What a herd's cell offers along a loop: open, level grass (the ground read at points along it, each as a chunk
	 *  would read it there), none of it under water or ice, nor in a wood; no water within its flight of it; no town
	 *  near it. */
	struct FHerdHabitatRead
	{
		bool bSuits = false;
		/** Points read along the loop (with its middle), those off the grass the herd lives on at all and those not
		 *  quite as it likes (a little steep, wet, dry or wooded: at most six of them), and the water within its flight. */
		int32 Points = 0, Off = 0, Rough = 0, WaterNear = 0;
		double MinFlat = 1.0, MaxForest = 0.0, MinWet = 1.0, MaxWet = 0.0, MeanWet = 0.0;
		bool bTown = false;
	};
	/** Whether a spot is the open grass a herd lives on: bStrict as it likes it best (level, Flat over 0.85; wet from 0.2
	 *  to 0.6; no more than 0.3 of a wood), else as it will cross (a little rougher). Never water, ice, near the snowline. */
	SLONK_API bool HerdGrass(const SlonkScatter::FHabitat& H, bool bStrict);
	/** The group a cell holds, if it holds one (false if not, or if its kind does not live here). */
	SLONK_API bool GroupInCell(const FFaunaWorld& W, const FFaunaCell& C, FFaunaGroup& Out);
	/** The nearest group of a kind to a direction, searched out to MaxCm over at most MaxCells cells: a debug search,
	 *  synchronous and coarse (the cells under a spiral of points, nearest first). */
	SLONK_API bool FindGroupNear(const FFaunaWorld& W, EFaunaKind K, const FVector& Dir, double MaxCm, int32 MaxCells, FFaunaGroup& Out);
	/** What a herd's loop offers (its cell's group, or one being made), read along it. */
	SLONK_API FHerdHabitatRead HerdHabitatAt(const FFaunaWorld& W, const FFaunaGroup& G);

	/** Where a group's frame is on its path at a time: planet-local, its frame (a flock's: x along its heading, level, z up
	 *  from the planet; a herd's: its own east and north laid level, which never turn, z up), its velocity, cm/s, and the
	 *  bank its members lean into the turn with, radians (positive lifting the right wing, so negative in a turn to the
	 *  right). A herd's frame stands on the field's ground (the game sets it on the drawn ground, SlonkFauna::StepHerd); its
	 *  heading is the way it walks in its frame, radians from x toward y; Intent 1 from HerdLeadSec before it sets off to
	 *  when it has stopped; DistCm how far along its loop it is (from theta 0, the loop's way). */
	struct FFaunaPose
	{
		FVector PosCm = FVector::ZeroVector;
		FQuat Rot = FQuat::Identity;
		FVector VelCmS = FVector::ZeroVector;
		double Bank = 0.0;
		double Heading = 0.0;
		double Intent = 0.0;
		double DistCm = 0.0;
		/** A herd's way along its loop there, planet-local unit (level): its heading, in whatever frame it stands in. */
		FVector Ahead = FVector::ZeroVector;
	};
	/** The group's frame on its path at a time (seconds on the world's clock, wrapped to ClockPeriodSec first). */
	SLONK_API FFaunaPose PathAt(const FFaunaGroup& G, double RadiusCm, double Seconds);
	/** A herd's schedule, in closed form: how far it has walked along its loop since the hour's turn, cm, and how fast it
	 *  walks, cm/s, at a time (the world's seconds, wrapped to the hour); and whether it is set to walk (1 from
	 *  HerdLeadSec before its walk comes in until it has stopped, else 0). */
	SLONK_API double HerdWalkedCm(const FFaunaGroup& G, double Seconds);
	SLONK_API double HerdSpeedCmS(const FFaunaGroup& G, double Seconds);
	SLONK_API double HerdIntent(const FFaunaGroup& G, double Seconds);
	/** Its members in its frame (cm; scaled by their span in metres, the unit mesh being 1 m across: a walker's length),
	 *  and their custom floats (Custom::Num each). */
	SLONK_API void MembersOf(const FFaunaGroup& G, TArray<FTransform>& OutXf, TArray<float>& OutCustom);

	/**
	 * The drawn ground under a herd, as its frame stands on it: the plane fitted to it over a grid of points either way of
	 * the frame's middle (its rise over run along the herd's east and north there), and how far the ground stands off that
	 * plane at the grid's nodes, cm along its normal (HerdData: the grid's corners not kept), each held to
	 * HerdData::MaxGroundCm. The herd's frame lies on the plane: x its east, z the plane's normal (SlonkFauna::HerdFrameRot).
	 */
	struct FHerdGround
	{
		double SlopeE = 0.0, SlopeN = 0.0;
		float Node[HerdData::GroundNodes] = {};
		double HalfCm = 0.0;
		bool bValid = false;
	};
	/** The ground under a herd's frame standing at FramePos (planet-local, on the drawn ground), the ground's height over
	 *  the sea-level sphere at a planet-local unit direction from HeightAt (the drawn ground's, the field's where none is
	 *  drawn). */
	SLONK_API void SampleHerdGround(const FFaunaGroup& G, double RadiusCm, const FVector& FramePos, TFunctionRef<double(const FVector&)> HeightAt, FHerdGround& Out);
	/** A herd's frame at a place on the ground: its east and north laid on the plane (the herd's own axes, never turned). */
	SLONK_API FQuat HerdFrameRot(const FFaunaGroup& G, const FVector& FramePos, const FHerdGround& Ground);
	/** The ground over a herd's plane at a point of its frame's xy, cm, as the material reads it from its custom primitive
	 *  data (HerdData::Num floats): bilinear over the grid, each corner made from the three nodes beside it, held at its
	 *  edge beyond it. */
	SLONK_API float HerdGroundAt(const float* Group, float X, float Y);

	/**
	 * A herd as the game keeps it while it is about: its clocks (how far its legs have walked, m, and how long it has
	 * grazed, s, which the material's walking integrates: SlonkFauna::HerdData), the share of it on the move (Travel,
	 * turning to walk before its walk comes in and back to graze once stopped, its walkers stepping round where they
	 * stand), the way it walks (Heading, unwound: it turns only while it walks or while it grazes), how fast its frame goes
	 * over the ground (SpeedMS) and how fast its legs walk (GaitMS), the ground under it, where its frame last stood, and
	 * how far it has moved off its path out of the crew's way (Avoid, planet-local cm). Its latest turn to walk or back to
	 * graze: how far its legs had walked when it began (PivotStartM, carried back through DistM's wraps, so DistM less it
	 * is the legs' walk since), which way it went (PivotSign: 1 to walk, -1 back to graze), the grazing clock it stood at
	 * (GrazeRef) and its way through it (TurnHeading); the turn before it (the other way: LastTurnHeading its way) and what
	 * came between (SinceLastTurn: before a turn to walk, the grazing clock the stop before left it at; before one back to
	 * graze, how far its legs walked, m), through which a walker's feet still down are carried; how sharply its way turns
	 * as it walks (WalkKap, radians a metre, changing smoothly, kept from its last walk). A turn once begun runs on to its
	 * end. A herd that has to turn more than TurnAroundRad off its way while on the move stops, turns where it stands and
	 * sets off again (bTurnAround). Out of the crew's way: the way it wants to go and how fast (AvoidWay, planet-local
	 * cm/s), the way it is walking (AvoidGoing, which it eases off along when it may not go the way it wants) and how much
	 * of that pace it has come up to (AvoidPace).
	 */
	struct FHerdState
	{
		double DistM = 0.0, GrazeSec = 0.0, Travel = 0.0, Heading = 0.0, SpeedMS = 0.0, GaitMS = 0.0;
		double PivotStartM = -1000.0, PivotSign = -1.0, GrazeRef = -900.0, TurnHeading = 0.0, LastTurnHeading = 0.0, SinceLastTurn = 1000.0, WalkKap = 0.0;
		bool bTurnAround = false;
		FVector Avoid = FVector::ZeroVector;
		FVector AvoidWay = FVector::ZeroVector;
		FVector AvoidGoing = FVector::ZeroVector;
		double AvoidPace = 0.0;
		FVector FramePos = FVector::ZeroVector;
		FQuat FrameRot = FQuat::Identity;
		/** Its frame's velocity along its plane over the last step, planet-local cm/s, and its offset off its path then. */
		FVector VelCmS = FVector::ZeroVector;
		FVector LastOffset = FVector::ZeroVector;
		/** The way it was steering for at the last step (its frame's way on the move), which WalkKap follows as it turns. */
		double TargetWas = 0.0;
		bool bTargetWas = false;
		bool bStarted = false;
		FHerdGround Ground;
	};
	/** How far off its way a herd on the move has to turn to stop and turn where it stands, radians (its frame all but
	 *  standing, slower than 10 cm/s, TurnStandingRad: at a stand it turns where it stands rather than on the move). How
	 *  fast its walkers
	 *  turn to walk or back to graze, a share of the whole turn a second, calm and frightened. How fast a herd's frame goes
	 *  over the ground, m/s, past which a fright cannot stop it dead (a gallop: its walk and trot are no faster than about
	 *  3.4 m/s). */
	constexpr double TurnAroundRad = 1.75, TurnStandingRad = 0.35;
	constexpr double HerdTurnRate = 0.5, HerdShakenTurnRate = 2.5;
	constexpr double HerdGallopMS = 4.0;
	/** A herd come about at a time: its clocks from its schedule (as far along its loop as the hour has walked it, as
	 *  long grazed as the hour has left it standing), turned as its schedule has it. */
	SLONK_API void StartHerd(FHerdState& S, const FFaunaGroup& G, double Seconds);
	/** How far someone on foot is from a herd's nearest member (its frame at FramePos), cm, along the ground. */
	SLONK_API double CrewGapCm(const FFaunaGroup& G, const FVector& FramePos, const FVector& CrewPosCm);
	/**
	 * Moves a herd off its path out of the way of the crew on foot (planet-local positions): away from anyone nearer its
	 * nearest member than the kind's KeepFromCrewCm, at a walk (a trot for one close in), and back to its path at a stroll
	 * once nobody is near, never back toward anyone. It turns to go before it goes: it moves only once all of it is on the
	 * move (Travel 1, not turning about) and the way it wants to go (AvoidWay) is within TurnAroundRad of the way it faces,
	 * coming up to its pace over a second; when it may not, it eases off over half a second on along the way it was
	 * walking (AvoidGoing), so it comes to a stop going forward and only then turns about. True while it wants to move
	 * for them.
	 */
	SLONK_API bool KeepFromCrew(FHerdState& S, const FFaunaGroup& G, const FFaunaKindDesc& D, const TArray<FVector>& CrewPosCm, double Dt);
	/**
	 * A herd's frame for this frame, and what the material reads of it (HerdData::Num floats; the night and the fright's
	 * slots left as they are): its place on its path at Now plus OffsetCm (its fright's and the crew's), set on the ground
	 * HeightAt gives (the drawn ground) and tilted to the plane fitted to it there (read again with bRefreshGround, else
	 * the last kept); its legs' walk run on by how far its frame went ahead along that plane since the last step and by
	 * its walkers' steps as they turn, its grazing clock by the time it stood grazing; its travel share turned toward what
	 * its schedule, its fright (bShaken) or the crew (bMoving) ask, and its heading toward the way it is going or about to
	 * go: its path and OffsetAheadCm (its offset a second on, the way its fright or the crew will take it; OffsetCm if
	 * null). The heading is set at once while it grazes, turned as it walks (by WalkKap a metre walked, which steers
	 * toward the way it goes as sharply as it can still straighten out by the time it faces it, at most MaxWalkKap a
	 * metre and a few radians a second, changing by no more than Walker::MaxKapRate a second) and held while its walkers
	 * turn; a herd on the move asked to go more than TurnAroundRad off its way stops, turns and sets off again. Its legs
	 * walk by its frame's way along its plane from where it stood, laid level first, so the drawn ground rising or falling
	 * under it (its chunks splitting) walks no legs on. ClockOffsetSec is the paths' clock's offset from the material's
	 * (UPlanetFaunaComponent::SetTimeOffset).
	 */
	SLONK_API void StepHerd(FHerdState& S, const FFaunaGroup& G, double RadiusCm, double Now, double Dt, const FVector& OffsetCm, bool bShaken, bool bMoving, bool bRefreshGround,
		TFunctionRef<double(const FVector&)> HeightAt, double ClockOffsetSec, float* OutData, const FVector* OffsetAheadCm = nullptr);

	/** Something that may scare a group: a craft (bCraft: its height over the ground, cm, and whether it is set down) or
	 *  someone on foot, planet-local. */
	struct FFaunaThreat
	{
		FVector PosCm = FVector::ZeroVector;
		FVector VelCmS = FVector::ZeroVector;
		double AltitudeCm = -1.0;
		bool bCraft = true;
		bool bLanded = false;
	};
	/** A group's fright: when it was last startled (the path clock's seconds), which its panic eases from; the way it
	 *  fled (planet-local unit, away from what scared it and up), and how far off its path it was at that moment, which
	 *  fades as the new flight runs (and a galloping kind's speed then, cm/s, which it carries on with as it turns);
	 *  when its members' scatter opened and the way it opened, which a startle while it is
	 *  still open leaves as they are; and when its wingbeat's urge began to come in. The material runs the scatter's
	 *  shape, the panic and the urge's coming-in each from its own moment (GroupData), so a startle that restarts the
	 *  panic and turns the flight leaves the scatter as it is and the wings beating as they were. */
	struct FPanic
	{
		double T0 = -1.0e30;
		/** How long it stays shaken from T0, s (its kind's FFaunaKindDesc::FrightSec). */
		double LiveSec = PanicSec;
		/** A galloping kind's: when its gallop comes on (the paths' clock: its walkers have turned to flee by then), and
		 *  whether the startle stopped it where it stood (its path's own walk held back until the gallop has come on:
		 *  HerdFrightOffset). */
		double FleeT0 = -1.0e30;
		bool bHold = false;
		FVector Dir = FVector::ZeroVector;
		/** The side a galloping kind loops round to come back to its path (planet-local unit, level). */
		FVector Side = FVector::ZeroVector;
		FVector Carry = FVector::ZeroVector;
		FVector CarryVel = FVector::ZeroVector;
		double ScatterT0 = -1.0e30;
		FVector ScatterDir = FVector::ZeroVector;
		/** When its wingbeat's urge began to come in (over 0.3 s, in the material): the moment a startle found it calm.
		 *  A startle while it is shaken leaves the urge where it stands: in full it stays in full (this set a second
		 *  back, so the material's clock never wraps round to it however long the fright is kept up), and eased as the
		 *  fright wears on it comes back from where it had eased to (this set back as far as the coming-in has got
		 *  there). */
		double OnsetT0 = -1.0e30;
		bool IsLive(double Now) const { return Now >= T0 && Now - T0 < LiveSec; }
		/** Whether its members' scatter is still open (ScatterSec from when it opened, while the fright lasts). */
		bool IsScattered(double Now) const { return IsLive(Now) && Now - ScatterT0 < ScatterSec; }
	};
	/** How far a group's frame is off its path for its fright at a time, planet-local cm: away fast, held a while, back
	 *  by its LiveSec (exactly zero from then). A galloping kind's gallop comes on at its FleeT0, and it comes back from
	 *  its kind's FleeBackSec at a steady pace eased in and out. */
	SLONK_API FVector PanicOffset(const FPanic& P, const FFaunaKindDesc& D, double Now);
	/** A herd's offset off its path for its fright at a time, planet-local cm: its flight (PanicOffset) and, while a
	 *  startle holds it where it stood (FPanic::bHold), its path's own walk since the startle held back, let go as its
	 *  gallop comes on and as it comes back (exactly zero by its LiveSec). */
	SLONK_API FVector HerdFrightOffset(const FFaunaGroup& G, const FFaunaKindDesc& D, const FPanic& P, double RadiusCm, double Now);
	/** How fast a shaken group's frame wheels round to the way it flies, and how fast its members' bank follows the turn,
	 *  radians a second. */
	constexpr double FlightTurnRadS = 2.5;
	constexpr double FlightBankRadS = 2.0;
	/**
	 * A group's frame as it flies at a time (the paths' clock), on its path and off it for its fright: a calm group's is
	 * its path's (PathAt), exactly. A shaken one is where its path and its flight (PanicOffset) put it, at their velocity
	 * together, facing the way it flies, level: it wheels round to that from Was (its frame Dt seconds before) at
	 * FlightTurnRadS at most, so a startle turns the formation rather than flipping it, and leans into the turn it makes
	 * (hard over while it wheels), its bank following at FlightBankRadS at most. A group just come has no frame before:
	 * any long Dt faces it its flight at once.
	 */
	SLONK_API FFaunaPose FlightAt(const FFaunaGroup& G, const FFaunaKindDesc& D, const FPanic& P, double RadiusCm, double Now, const FFaunaPose& Was, double Dt);
	/** Whether a threat scares a group at a place: a craft in the air (not set down) below the kind's ScareAltitudeCm
	 *  and within its ScareRadiusCm, someone on foot within CrewScareCm (of its members, a group GroupRadiusCm across
	 *  either way). */
	SLONK_API bool Scares(const FFaunaKindDesc& D, const FVector& GroupPosCm, const FFaunaThreat& T, double GroupRadiusCm = 0.0);
	/** Startles a group at its place now if the threat scares it (unless it was startled in the last RestartleSec, or is
	 *  already half again its flight off its path): true if it did, as StartleNow. */
	SLONK_API bool StartleGroup(FPanic& P, const FFaunaKindDesc& D, const FVector& GroupPosCm, const FFaunaThreat& T, double Now, double GroupRadiusCm = 0.0);
	/**
	 * Startles a herd where its frame stands (S, as last stepped) if the threat scares it, as StartleGroup does a flock
	 * (bForce: however lately it was startled, as StartleNow), its flight running on from where its fright (its hold of its
	 * path among it) has it now. A herd going no faster than HerdGallopMS stops where it stands: its walkers turn back and
	 * round to the way it flees (S.bTurnAround, if any of it was on the move), its path held where it was (FPanic::bHold)
	 * and its gallop coming on only once they have turned (FPanic::FleeT0), so its frame never moves while its walkers
	 * turn. One galloping turns away on the run, carrying its speed; asked to flee more than TurnAroundRad off the way it
	 * faces (the fright ahead of it), it flees on the way it is going.
	 */
	SLONK_API bool StartleHerd(FPanic& P, FHerdState& S, const FFaunaGroup& G, const FFaunaKindDesc& D, double RadiusCm, const FFaunaThreat& T, double Now, bool bForce = false);
	/** Startles a group at its place now, however lately it was: it flees away from the threat (or off its track, the
	 *  threat overhead), what is left of its last flight carried; its panic runs from now; its members' scatter, if still
	 *  open, is kept, and its wingbeat's urge comes in from where it stands (FPanic::OnsetT0). */
	SLONK_API void StartleNow(FPanic& P, const FFaunaKindDesc& D, const FVector& GroupPosCm, const FFaunaThreat& T, double Now);
	/** A group's fright as the material reads it, into its custom primitive data (GroupData::Num floats): when its scatter
	 *  opened, the way it opened in the group's frame (Rot, its frame's rotation now), whether it is live, when it was
	 *  last startled and when its urge began to come in, each moment on the material's clock (the paths' clock less
	 *  ClockOffsetSec, wrapped to ClockPeriodSec); all 0 once the fright is over. The other slots are left as they are. */
	SLONK_API void FrightData(const FPanic& P, double Now, const FQuat& Rot, double ClockOffsetSec, float* Group);
	/** A herd's fright as its walkers read it (HerdData): whether it is live, when it was last startled and when its
	 *  alarm began to come in, as FrightData has them. The herd's own slots are left as they are. */
	SLONK_API void HerdFrightData(const FPanic& P, double Now, double ClockOffsetSec, float* Group);

	/**
	 * The material's motion of a flier's vertex (fauna_material.py, WPO_CODE), transliterated in floats as the GPU runs
	 * it: the offset of a vertex at LP (the unit mesh's cm, in its instance's frame) with its vertex colour, at the
	 * material's time T, for a member's custom floats (Custom::Num) in a group's custom primitive data (GroupData::Num),
	 * DistCm from the camera, which sees PxPerRad pixels a radian. In the instance's frame, unit-mesh cm. A hand copy:
	 * Slonk.Fauna.Meshes ties it to the script by the CRC of WPO_CODE's lines, so an edit to the material's motion alone
	 * fails it.
	 */
	SLONK_API FVector3f FlierMotion(const FVector3f& LP, const FLinearColor& VC, float T, const float* Custom, const float* Group, float DistCm, float PxPerRad);

	/**
	 * The material's motion of a walker's vertex (fauna_material.py's walker branch), transliterated in floats as the GPU
	 * runs it: the offset of a vertex at LP (its unit mesh's cm, in its instance's frame) and LPP (where that is in its
	 * herd's frame, cm), with its vertex colour, at the material's time T, for a member's custom floats (Custom::Num) in a
	 * herd's custom primitive data (HerdData::Num), DistCm from the camera, which sees PxPerRad pixels a radian. In the
	 * instance's frame, unit-mesh cm. WalkerYaw is the way it faces then, radians in its herd's frame (the material turns
	 * its normals by it; it never jumps, its herd's way being held while its walkers turn); WalkerStrides how many strides
	 * it has walked (its gait's phase: walking, grazing and the steps of its turns), and WalkerSpeedMS how fast it walks,
	 * m/s. A planted foot is carried back by every way its body has gone since it was put down, across its herd's latest
	 * turn to walk or back to graze, so it stays where it was put down.
	 */
	SLONK_API FVector3f WalkerMotion(const FVector3f& LP, const FVector3f& LPP, const FLinearColor& VC, float T, const float* Custom, const float* Group, float DistCm, float PxPerRad);
	SLONK_API float WalkerYaw(float T, const float* Custom, const float* Group);
	SLONK_API float WalkerStrides(float T, const float* Custom, const float* Group);
	SLONK_API float WalkerSpeedMS(float T, const float* Custom, const float* Group);

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
