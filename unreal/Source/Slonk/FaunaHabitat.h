// Slonk - where a world's animals are: pure functions of the planet and the time, safe off the game thread, so the
// game finds the same animal wherever and whenever it looks. The shoals of the sea are mirrored from the terrain
// material's own lattice (terrain_material.py, its water block), so whatever leaps from the sea leaps out of a shoal
// that is drawn there. The fliers live in cells laid on the cube's faces, at most one flock to a cell where the ground
// round it offers a wood, a wet meadow, a shore or a cliff; each flock flies a closed, lobed loop round its cell whose
// period divides the material's hour, so where a flock is follows from the time alone, and only a startle (a craft
// passing low, someone walking under it) moves it off its loop for a couple of minutes. The herds live in cells of their
// own where a loop of about 2 km round the cell's point lies on open, level grass, however lush (crossing a short stretch
// of scrub, a strip of trees or a gentle rise here and there, and nowhere its walkers stand water, a cliff or a dense
// wood), keeping clear of the water and off the towns and their roads; each grazes most of the time and walks and trots
// now and then, on a schedule of the clock whose distance walked is known in closed form, and stands on the ground that
// is drawn (SlonkFauna::StepHerd); frightened, it flees a way that keeps it dry. The leapers live in the painted shoals
// themselves, a group in half the shoals over water deep enough, drawn in the shoal's frame as it swims: each fish leaps
// out of the sea and back on a schedule of the material's clock in closed form (SlonkFauna::LeapOf), from the swell's
// surface where it leaves to the surface where it dives back in (SlonkFauna::SwellAt mirrors the terrain material's
// swell), and a craft passing low sends the group diving for a minute. FFaunaField keeps the groups about a camera, by
// its distance from them alone.

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
		/** The middle of the loop it swims round, planet-local cm on the sea-level sphere (where it is at no time, the same
		 *  at every time), and the loop's radius on the plane it is drawn on, cm. */
		FVector LoopCentreCm = FVector::ZeroVector;
		double LoopRadiusCm = 0.0;
	};

	/** The shoal of the cell holding a planet-local point (on the sea-level sphere of RadiusCm, or near it) at a time
	 *  (the terrain material's Time: the world's time, UWorld::GetTimeSeconds), exactly as the material hashes it, in
	 *  Density of the cells (its ShoalDensity, SlonkFauna::SeaLifeOf): false if the cell has none. */
	SLONK_API bool ShoalAt(const FVector& PlanetLocalCm, double RadiusCm, float Density, double TimeSec, FShoal& Out);
	/** Where a planet-local point near a shoal lies in its painted shape: 0 at its middle, 1 at its edge. */
	SLONK_API double ShoalShapeAt(const FShoal& S, const FVector& PlanetLocalCm);

	/** The projection's plane for a planet-local direction, as the terrain material lays its waves and its shoals: 0 across
	 *  x, 1 across y, 2 across z, by the direction's largest axis (x before y before z where they tie). */
	SLONK_API int32 ProjectionAxis(const FVector& Dir);

	/**
	 * The terrain material's swell (terrain_material.py's SlonkGerstner, which moves the sea's vertices): four Gerstner
	 * waves of whole wave numbers across the noise period, on the projection's plane at a planet-local point (on the
	 * sea-level sphere, or near it), at the material's time (the world's seconds, never wrapped): the displacement along
	 * the plane's two axes and up the radial, cm, at full strength (the material fades it out from 800 m to 2.5 km from
	 * the camera: SwellFade).
	 */
	SLONK_API FVector SwellAt(const FVector& PlanetLocalCm, double TimeSec, double SwellCm);
	SLONK_API double SwellFade(double DistFromCameraCm);
	/** The drawn sea's surface over the sea-level sphere along the radial through a planet-local point, cm: where the swell
	 *  (faded by Fade) has carried the surface that stands over it (the vertex it came from found by one step back along the
	 *  swell's sway across the plane), its lift up the radial and its sway's own lift (the sway runs along the plane's fixed
	 *  axes, not along the sphere). */
	SLONK_API double SeaSurfaceCm(const FVector& PlanetLocalCm, double RadiusCm, double TimeSec, double SwellCm, double Fade);

	/** What a world's seas carry as its animals see them: the share of the shoal lattice's cells holding a shoal (the
	 *  terrain material's ShoalDensity, SlonkFauna::SeaLifeOf: 0 on a sea with no life, and then there are no leapers), the
	 *  swell's height (the material's SwellCm), and the sea's colours (its solvent's). */
	struct FFaunaSea
	{
		float ShoalDensity = 0.f;
		float SwellCm = 0.f;
		FLinearColor Shallow = FLinearColor(0.1f, 0.35f, 0.5f);
		FLinearColor Ocean = FLinearColor(0.05f, 0.17f, 0.35f);
		bool operator==(const FFaunaSea& O) const { return ShoalDensity == O.ShoalDensity && SwellCm == O.SwellCm && Shallow == O.Shallow && Ocean == O.Ocean; }
		bool operator!=(const FFaunaSea& O) const { return !(*this == O); }
	};

	/** A world as its animals see it: its field, its fauna, how much lives there, the living ground's colours, its seas,
	 *  and the settings (the density, the kinds allowed). */
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
		FFaunaSea Sea;
		/** Whether any group of the kind lives here: life, air, the kind allowed and spawned in this build (the leapers only in
		 *  a sea whose shoals are painted). */
		bool Spawns(EFaunaKind K) const
		{
			return Life > 0.f && Density > 0.f && !P.bAirless && (KindMask & KindBit(K)) != 0u && KindDesc(K).bLive && (K != EFaunaKind::Leaper || Sea.ShoalDensity > 0.f);
		}
		bool SpawnsAny() const { for (int32 K = 0; K < NumKinds; ++K) { if (Spawns((EFaunaKind)K)) { return true; } } return false; }
		/** The salt every cell's hash takes: the profile's seed and the terrain's. */
		uint64 Salt() const;
		bool operator==(const FFaunaWorld& O) const;
		bool operator!=(const FFaunaWorld& O) const { return !(*this == O); }
	};

	/** A habitat cell of a kind on the cube's faces (the terrain's quadtree's, SlonkTerrain::CubeDir). A leaper's cell is
	 *  instead a cell of the shoals' own lattice (ShoalAt's, unwrapped so each is one place on the planet): its Face the
	 *  projection's plane times two, and one more on the plane's negative side; X and Y its cell across the plane's two
	 *  axes, from the planet's middle (either sign). */
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
	/** A cell's middle, a planet-local unit direction (a leaper's cell's off the sphere: none, false). */
	SLONK_API bool CellMiddle(const FPlanetTerrainParams& P, const FFaunaCell& C, FVector& OutDir);
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
		/** How far its loop's ground reads dry toward the nearest water, cm (the last dry read short of it, the shore up to a
		 *  read's step beyond), read out to HerdDryReadCm (that much if none is nearer; unread, as far as can be): the crew
		 *  walk it off its path no further than this leaves its edge HerdCrewDryCm short of the water. */
		double DryCm = 1.0e30;
		/**
		 * A group of leapers': the shoal it leaps from (ShoalAt at Centre on the sea, with the terrain material's
		 * ShoalDensity), whose frame it is drawn in, swimming its loop (RadiusCm round Centre) on the world's own seconds as
		 * the material paints it; its painted half-length and half-width and how deep it swims, cm; the shallowest water
		 * read under its leaps, cm; and its leap cycle, s (CycleSec, a divisor of the hour; PeriodSec the same).
		 */
		float ShoalDensity = 0.f;
		double ShoalHalfLengthCm = 0.0, ShoalHalfWidthCm = 0.0, ShoalDepthCm = 0.0, WaterCm = 0.0;
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
	 *  would read it there), a little of it rougher ground the herd crosses (HerdCrossMax of its points at most, no more
	 *  than HerdCrossRun of them in a row: a short stretch of scrub, a strip of trees, a gentle rise), none of it water,
	 *  ice, a cliff or a dense wood, nor anywhere its walkers stand (read across its footprint, out to its radius either
	 *  side of each point); no water within HerdWaterMarginCm of the loop, either side of it (read every
	 *  HerdWaterMarginCm / 2 across it at every fifth point), and how far the nearest is out to HerdDryReadCm; off any
	 *  town's ground and its roads by its radius and HerdTownMarginCm (a road HerdRoadMarginCm). */
	struct FHerdHabitatRead
	{
		bool bSuits = false;
		/** Points read along the loop (with its middle) and across its footprint: those it never sets foot on, those of the
		 *  loop's line off its grass it crosses and the longest stretch of them in a row, those not quite as it likes
		 *  (rougher than it likes best, crossed ground among them: at most HerdRoughMax); the points of water read within
		 *  HerdWaterMarginCm of the loop. */
		int32 Points = 0, Foot = 0, Off = 0, Crossed = 0, LongestCrossed = 0, Rough = 0, WaterNear = 0;
		double MinFlat = 1.0, MaxForest = 0.0, MinWet = 1.0, MaxWet = 0.0, MeanWet = 0.0;
		/** How far the loop's ground reads dry toward the nearest water, cm: the last dry read short of it (HerdDryReadCm if
		 *  none is wet). */
		double DryCm = 0.0;
		bool bTown = false, bRoad = false;
	};
	/** The share of a herd's cells that may hold one, on a world full of life at density 1 (the rest hold none whatever
	 *  their ground). A loop laid on grass suits about two times in five, on broken country and on the widest, wettest
	 *  lowland alike (the woods, the banks and the strips of scrub its loop meets keep the rest off it), so this sets how
	 *  many herds the grass carries: on an Earth-like world about one for every five cells whose middle is its grass. */
	constexpr double HerdCellShare = 0.5;
	/** How many points round its loop a herd's habitat is read at (about 4 m apart round a 2 km loop, as fine as the
	 *  field's own ground: no steep bank nor strip of dense wood between them), and how many across its walkers'
	 *  footprint either side of each (its radius over that apart, out to its radius). */
	constexpr int32 HerdLoopReads = 480, HerdFootReads = 3;
	/** Of a herd's HerdLoopReads points along its loop and its middle, how many may be ground it crosses rather than its
	 *  grass (an eighth), and how many of them in a row (a sixteenth of its loop); how many may be rougher than it likes
	 *  best (its crossed ground among them: half). */
	constexpr int32 HerdCrossMax = 60, HerdCrossRun = 30, HerdRoughMax = 240;
	/** How far its loop keeps from water, cm (its walkers spread up to about 45 m either side of it), and how far out the
	 *  water is read for how much room the crew may walk it into (300 m, the most they walk it off its path, past its
	 *  radius), and how far short of the water they leave its edge. How far its loop keeps from a town's ground past its
	 *  own radius, and from a road. */
	constexpr double HerdWaterMarginCm = 9.0e3, HerdDryReadCm = 3.7e4, HerdCrewDryCm = 2.0e3;
	constexpr double HerdTownMarginCm = 5.0e4, HerdRoadMarginCm = 5.0e3;
	/** Whether a spot is the open grass a herd lives on: bStrict as it likes it best (level, Flat over 0.85; wet 0.2 or
	 *  more, so no desert, however lush; no more than 0.3 of a wood), else as it will cross (a little rougher). Never water,
	 *  ice, near the snowline. */
	SLONK_API bool HerdGrass(const SlonkScatter::FHabitat& H, bool bStrict);
	/** Whether a herd will set foot on a spot at all: its grass, or rougher ground off it that it crosses on its way
	 *  (scrub too dry or too wet for it, a strip of trees, a gentle rise); never water, ice, near the snowline, a cliff
	 *  (Cliff over 0.3), ground steeper than a gentle rise (Flat 0.45 or less, about 15 degrees) nor a dense wood (Forest
	 *  0.6 or more). */
	SLONK_API bool HerdCrosses(const SlonkScatter::FHabitat& H);
	/** The group a cell holds, if it holds one (false if not, or if its kind does not live here). */
	SLONK_API bool GroupInCell(const FFaunaWorld& W, const FFaunaCell& C, FFaunaGroup& Out);
	/** How far a search for a group looked: how many cells it read and out to how far, cm (every cell within that). */
	struct FGroupSearch
	{
		int32 Cells = 0;
		double ReachCm = 0.0;
	};
	/** The nearest group of a kind to a direction, searched out to MaxCm over at most MaxCells cells: a debug search,
	 *  synchronous, reading every cell ring by ring outward (each ring a cell wide, its cells nearest first) until no cell
	 *  further out could hold a nearer group than one found, MaxCm is reached or MaxCells are read. */
	SLONK_API bool FindGroupNear(const FFaunaWorld& W, EFaunaKind K, const FVector& Dir, double MaxCm, int32 MaxCells, FFaunaGroup& Out, FGroupSearch* OutSearch = nullptr);
	/** What a herd's loop offers (its cell's group, or one being made), read along it. */
	SLONK_API FHerdHabitatRead HerdHabitatAt(const FFaunaWorld& W, const FFaunaGroup& G);

	/**
	 * What a leaper's cell offers (its group's own reading of it): whether the cell holds a painted shoal (ShoalAt at the
	 * cell's middle finds this cell's), lying wholly on its own plane of the projection out to its reach (so the swell
	 * the material draws about it is the same plane's all round, and the sea a fish leaves is the sea drawn); whether the
	 * cell is among the share of the shoals holding leapers; and the water under its leaps, read at its loop's middle and
	 * eight points round it out to its loop and reach: the open sea everywhere (no land, no river or lake, no ice; on a
	 * locked world the air neither baked nor frozen), at least NeedDepthCm deep (Leaper::MinWaterCm, and the shoal's own
	 * depth and Leaper::ShoalClearCm, so the material paints it whole where its fish leap).
	 */
	struct FLeaperHabitatRead
	{
		bool bShoal = false, bOnePlane = false, bGate = false, bSuits = false;
		/** The points of water read, how many of them were not the open sea or too shallow, and the shallowest water read. */
		int32 Points = 0, NotSea = 0, Shallow = 0;
		double MinDepthCm = 0.0, NeedDepthCm = 0.0;
		FShoal Shoal;
	};
	SLONK_API FLeaperHabitatRead LeaperHabitatAt(const FFaunaWorld& W, const FFaunaCell& C);
	/** How far a group of leapers' fish may come from its shoal's loop's path, cm along the sea: their homes within its
	 *  painted shape, and their leaps out from them. */
	SLONK_API double LeaperReachCm(const FShoal& S);

	/**
	 * A fish's leap (SlonkFauna::Leaper), in closed form from its custom floats (Custom::Num) as the material has it: its
	 * cycle (on the clock asked of: whole cycles of its group's from that clock's zero, its slot's start a share in) and
	 * whether the cycle holds a leap; when it leaves the water (T0, on the same clock) and how long it is out of it, and how
	 * long before and after that it is drawn under the surface; where it leaves the water in its group's frame (cm from its
	 * home), the way it goes (radians from the frame's x toward its y), how high it rises over the line between the sea's
	 * surface where it leaves and where it dives back in, and how far on it comes down, cm.
	 */
	struct FLeap
	{
		int64 Cycle = 0;
		bool bLeaps = false;
		double T0 = 0.0, FlightSec = 0.0, LeadSec = 0.0;
		FVector2D FromCm = FVector2D::ZeroVector;
		double Yaw = 0.0, ApexCm = 0.0, RangeCm = 0.0;
		double T1() const { return T0 + FlightSec; }
		FVector2D ToCm() const { return FromCm + FVector2D(FMath::Cos(Yaw), FMath::Sin(Yaw)) * RangeCm; }
		/** Whether it is drawn at a moment: from LeadSec before it leaves to LeadSec after it dives back in. */
		bool DrawnAt(double T) const { return bLeaps && T >= T0 - LeadSec && T <= T1() + LeadSec; }
	};
	/** A fish's leap of a cycle (any whole number: its hash is its place in the hour), and of the cycle holding a moment,
	 *  on any clock whose hours are the material's (the world's seconds, or the material's own). */
	SLONK_API FLeap LeapOf(const float* Custom, int64 Cycle);
	SLONK_API FLeap LeapAt(const float* Custom, double Seconds);
	/** A fish's place on its leap at a moment, in its group's frame, cm from its home (z up from the sea's level), with the
	 *  sea's surface H0 over its level where it leaves the water and H1 where it dives back in (cm, each held to
	 *  Leaper::MaxSurfaceCm): its middle, its pitch (radians, nose up) and its share of the way through its flight (0
	 *  leaving, 1 diving back in; beyond either it is under the surface). */
	struct FLeapPose
	{
		FVector PosCm = FVector::ZeroVector;
		double Pitch = 0.0;
		double Share = 0.0;
	};
	SLONK_API FLeapPose LeapPoseAt(const FLeap& L, double H0, double H1, double T);
	/** A fish's leaving the water (bEntry false) or diving back into it, at a moment (the world's seconds). */
	struct FLeapEvent
	{
		int32 Member = 0;
		bool bEntry = false;
		double TimeSec = 0.0;
		FLeap Leap;
	};
	/** Every leaving and diving back in of a fish's leaps after FromSec up to ToSec (the world's seconds), oldest first: none
	 *  of a cycle without a leap. */
	SLONK_API void LeapEventsBetween(const float* Custom, int32 Member, double FromSec, double ToSec, TArray<FLeapEvent>& Out);
	/** A point of a group of leapers' frame (cm in its frame), planet-local, at a moment (the world's seconds). */
	SLONK_API FVector LeaperFramePoint(const FFaunaGroup& G, double RadiusCm, double WorldSec, const FVector& FrameCm);
	/** Where a fish's leap leaves the water (bEntry false) or dives back into it: planet-local, on the sea's drawn surface
	 *  there at that moment (SeaSurfaceCm, its swell faded by Fade), its home HomeCm in its group's frame and the leap on the
	 *  world's seconds; and the surface's height over the sea's level there, cm. */
	SLONK_API FVector LeapCrossing(const FFaunaGroup& G, double RadiusCm, double SwellCm, double Fade, const FVector& HomeCm, const FLeap& L, bool bEntry, double* OutSurfaceCm = nullptr);
	/** Where a ship parks to watch a group of leapers from AltCm over the sea at a moment (the world's seconds; `fauna goto
	 *  leaper`): off to the side of its shoal then, out from its loop's middle, SideCm out past the furthest its shoal
	 *  swims from that middle, so that wherever round its loop the shoal goes it never comes nearer than SideCm. SideCm is
	 *  1.6 times the kind's scare radius when lower than 1.2 times its scare altitude (out of reach of its dive), else 0.6
	 *  of its height (at least 30 m). A planet-local unit direction; and SideCm. */
	SLONK_API FVector LeaperWatchDir(const FFaunaGroup& G, double RadiusCm, double WorldSec, double AltCm, double* OutSideCm = nullptr);

	/** Where a group's frame is on its path at a time: planet-local, its frame (a flock's: x along its heading, level, z up
	 *  from the planet; a herd's: its own east and north laid level, which never turn, z up), its velocity, cm/s, and the
	 *  bank its members lean into the turn with, radians (positive lifting the right wing, so negative in a turn to the
	 *  right). A herd's frame stands on the field's ground (the game sets it on the plane fitted to the drawn ground,
	 *  SlonkFauna::StepHerd); its heading is the way it walks in its frame, radians from x toward y; Intent 1 from
	 *  HerdLeadSec before it sets off to when it has stopped; DistCm how far along its loop it is (from theta 0, the
	 *  loop's way). */
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
	/** The group's frame on its path at a time (seconds on the world's clock, wrapped to ClockPeriodSec first). A group of
	 *  leapers' is its shoal's, at the world's own seconds (never wrapped, nor moved by the paths' clock's offset: the
	 *  terrain material swims the shoals on them): on the sea-level sphere at the shoal's middle, x the way it swims. */
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
	 * the frame's middle (its rise over run along the herd's east and north there, and where it stands: a point of it,
	 * planet-local, over the ground at its middle), and how far the ground stands off that plane at the grid's nodes, cm
	 * along its normal (HerdData: the grid's corners not kept), each held to HerdData::MaxGroundCm. The herd's frame lies
	 * on the plane, where the radial through its place meets it: x its east, z the plane's normal
	 * (SlonkFauna::HerdFrameRot). Its walkers stand on the ground through the nodes; the frame itself rides the plane,
	 * which goes as smoothly as the ground under the whole herd does, so a bump under its middle moves none of its
	 * walkers.
	 */
	struct FHerdGround
	{
		double SlopeE = 0.0, SlopeN = 0.0;
		float Node[HerdData::GroundNodes] = {};
		double HalfCm = 0.0;
		FVector PlanePt = FVector::ZeroVector;
		bool bValid = false;
	};
	/** The ground under a herd whose place is over GroundPos (planet-local, on the drawn ground), the ground's height over
	 *  the sea-level sphere at a planet-local unit direction from HeightAt (the drawn ground's, the field's where none is
	 *  drawn): read twice, its nodes laid first on the plane tangent there and then on the plane that first read fits, so
	 *  the fit is the ground's alone, whatever was read before. */
	SLONK_API void SampleHerdGround(const FFaunaGroup& G, double RadiusCm, const FVector& GroundPos, TFunctionRef<double(const FVector&)> HeightAt, FHerdGround& Out);
	/** Where a herd's frame stands for a place on its way (a planet-local unit direction): where the radial there meets the
	 *  plane its ground was last fitted to (SlonkFauna::SampleHerdGround). */
	SLONK_API FVector HerdFramePos(const FFaunaGroup& G, double RadiusCm, const FVector& Dir, const FHerdGround& Ground);
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
		/** When a frightened herd may next be startled again, after a fright that found no way it could take (the paths'
		 *  clock, s). */
		double NextStartleSec = -1.0e9;
		FVector Avoid = FVector::ZeroVector;
		FVector AvoidWay = FVector::ZeroVector;
		FVector AvoidGoing = FVector::ZeroVector;
		double AvoidPace = 0.0;
		FVector FramePos = FVector::ZeroVector;
		FQuat FrameRot = FQuat::Identity;
		/** Its frame's velocity along its plane over the last step, planet-local cm/s, and its offset off its path then; when
		 *  that step was (the paths' clock), the time since which its speeds are read over. */
		FVector VelCmS = FVector::ZeroVector;
		FVector LastOffset = FVector::ZeroVector;
		double LastStepSec = -1.0e30;
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
	/** How fast a herd's walkers turn to walk or back to graze, a share of the whole turn a second: HerdTurnRate calm;
	 *  frightened HerdShakenTurnRate, or slower for a herd of smaller walkers, so their steps round where they stand
	 *  (Walker::PivotM of their legs' walk a whole turn) are no more than HerdGallopStridesHz of its shortest walkers'
	 *  strides a second (about two turns a second for the smallest) and no hoof is lifted and put down a stride on
	 *  between two frames. */
	SLONK_API double HerdTurnRateOf(const FFaunaGroup& G, bool bShaken);
	/** A herd come about at a time: its clocks from its schedule (as far along its loop as the hour has walked it, as
	 *  long grazed as the hour has left it standing), turned as its schedule has it. */
	SLONK_API void StartHerd(FHerdState& S, const FFaunaGroup& G, double Seconds);
	/** How far someone on foot is from a herd's nearest member (its frame at FramePos), cm, along the ground. */
	SLONK_API double CrewGapCm(const FFaunaGroup& G, const FVector& FramePos, const FVector& CrewPosCm);

	/**
	 * The water about a herd as it flees or walks out of the crew's way: whether the field is water (as SlonkTerrain's
	 * structure has it, the water the loop's own read finds), or ground it never sets foot on (SlonkFauna::HerdCrosses: a
	 * dense wood, a cliff, steep ground, ice), at the point of a lattice HerdWaterStepCm apart on its loop's
	 * tangent plane nearest a place, each point read once and kept, so what is found never hangs on the order or the
	 * frame it is asked in. Made for a world (or, for the tests, from any reading of the water at a planet-local unit
	 * direction).
	 */
	class SLONK_API FHerdWater
	{
	public:
		FHerdWater() = default;
		explicit FHerdWater(const FPlanetTerrainParams& P);
		explicit FHerdWater(TFunction<bool(const FVector&)> InIsWater, double InRadiusCm) : IsWater(MoveTemp(InIsWater)), RadiusCm(InRadiusCm) {}
		/** Whether there is water at the lattice point nearest a planet-local place near a herd (none read without a world). */
		bool WetAt(const FFaunaGroup& G, const FVector& PosCm);
		/** Whether a herd standing at a place, going a way (planet-local, level; none if it stands) keeps dry: no water at
		 *  its middle, nor its radius and MarginCm out to either side of the way it goes (all round it, standing). */
		bool DryAround(const FFaunaGroup& G, const FVector& PosCm, const FVector& Way, double MarginCm);
		/** How many of its lattice's points have been read. */
		int32 Reads() const { return Wet.Num(); }
	private:
		TFunction<bool(const FVector&)> IsWater;
		double RadiusCm = 0.0;
		TMap<uint64, bool> Wet;
	};
	/** How far apart FHerdWater's lattice points are, cm. */
	constexpr double HerdWaterStepCm = 1000.0;
	/** How far apart a herd's flight is read for water as it is chosen, cm along it, and how far past its radius either
	 *  side (half the crew's HerdCrewDryCm, so a herd the crew have walked to the shore may still flee along it); how long
	 *  a gallop carries it on once frightened again (FPanic's carry coasting, s). */
	constexpr double HerdFlightReadCm = 4000.0, HerdFlightDryCm = 1000.0, HerdCoastSec = 3.0;

	/**
	 * Moves a herd off its path out of the way of the crew on foot (planet-local positions): away from anyone nearer its
	 * nearest member than the kind's KeepFromCrewCm, at a walk (a trot for one close in), and back to its path at a stroll
	 * once nobody is near, never back toward anyone. It turns to go before it goes: it moves only once all of it is on the
	 * move (Travel 1, not turning about) and the way it wants to go (AvoidWay) is within TurnAroundRad of the way it faces,
	 * coming up to its pace over a second; when it may not, it eases off over half a second on along the way it was
	 * walking (AvoidGoing), so it comes to a stop going forward and only then turns about. Never toward water: off its
	 * path no further than its loop's dry ground allows (FFaunaGroup::DryCm), slowing as it comes to that, and (Water
	 * given) it may not walk off a way whose ground its edge would walk onto (ahead and to either side) is water. True
	 * while it wants to move for them.
	 */
	SLONK_API bool KeepFromCrew(FHerdState& S, const FFaunaGroup& G, const FFaunaKindDesc& D, const TArray<FVector>& CrewPosCm, double Dt, FHerdWater* Water = nullptr);
	/** How far off its path the crew may walk a herd, cm: 300 m, or less where its loop's dry ground is narrower (its edge
	 *  kept HerdCrewDryCm short of the water). */
	SLONK_API double CrewRoomCm(const FFaunaGroup& G);
	/**
	 * A herd's frame for this frame, and what the material reads of it (HerdData::Num floats; the night and the fright's
	 * slots left as they are): its place on its path at Now plus OffsetCm (its fright's and the crew's), set where the
	 * radial there meets the plane fitted to the ground HeightAt gives (the drawn ground) under it, and tilted to that plane
	 * (read again with bRefreshGround, else the last kept); its legs' walk run on by how far its frame went ahead along that
	 * plane since the last step (none partway through a turn, when its walkers step round where they stand) and by
	 * its walkers' steps as they turn, its grazing clock by the time it stood grazing; its travel share turned toward what
	 * its schedule, its fright (bShaken) or the crew (bMoving) ask, and its heading toward the way it is going or about to
	 * go: its path and OffsetAheadCm (its offset a second on, the way its fright or the crew will take it; OffsetCm if
	 * null). The heading is set at once while it grazes, turned as it walks (by WalkKap a metre walked, which steers
	 * toward the way it goes as sharply as it can still straighten out by the time it faces it, at most MaxWalkKap a
	 * metre and a few radians a second, changing by no more than Walker::MaxKapRate a second) and held while its walkers
	 * turn; a herd on the move asked to go more than TurnAroundRad off its way stops, turns and sets off again. Its legs
	 * walk by its frame's way along its plane from where it stood, laid level first, so the drawn ground rising or falling
	 * under it (its chunks splitting) walks no legs on. The step spans Dt, or the time since its last step where the
	 * caller holds a long frame's Dt shorter (up to a second: its path has run on the whole of it, so its speeds are read
	 * over that). Come about on the move along its path alone it faces its path's way; taken off it by its fright or the
	 * crew as it comes, the way it goes. ClockOffsetSec is the paths' clock's offset from the material's
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
		/** A galloping kind's: how long its gallop takes to come on from a stand, s (at its fastest that long in): its
		 *  kind's FleeRiseSec if 0, a herd's own (SlonkFauna::HerdRiseSec) once it is startled. */
		double RiseSec = 0.0;
		/** A herd fleeing along its own loop rather than off it (no dry way off it): its flight FleeCm along the loop, the
		 *  loop's own way (1) or against it (-1); 0 for a flight straight off along Dir. */
		int32 LoopWay = 0;
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
	 *  its kind's FleeBackSec at a steady pace eased in and out, what a fright before left it carrying (FPanic::Carry,
	 *  and the gallop it coasts on) coming back with it at the same pace. */
	SLONK_API FVector PanicOffset(const FPanic& P, const FFaunaKindDesc& D, double Now);
	/** A herd's offset off its path for its fright at a time, planet-local cm: its flight (PanicOffset; or, fleeing along
	 *  its loop, FPanic::LoopWay, the way along it the flight has taken it from where its path has it) and, while a
	 *  startle holds it where it stood (FPanic::bHold), its path's own walk since the startle held back, let go as its
	 *  gallop comes on and as it comes back (exactly zero by its LiveSec). */
	SLONK_API FVector HerdFrightOffset(const FFaunaGroup& G, const FFaunaKindDesc& D, const FPanic& P, double RadiusCm, double Now);
	/** How many strides a second a herd's shortest walkers take at most at its fastest gallop: their stride is a share of
	 *  their length at every gait, so a herd of smaller walkers gallops the slower (from about 8 m/s for the smallest to
	 *  its kind's 11.5 for the largest), and a hoof's swing (the last 0.4 of its stride) always outlasts a frame at 30
	 *  frames a second, a second fright's gallop come on over the coasting of the first's (a twelfth faster) among them:
	 *  no hoof is lifted and put down a stride on between two frames. */
	constexpr double HerdGallopStridesHz = 9.0;
	/** How long a herd's gallop takes to come on from a stand, s (FPanic::RiseSec): its kind's FleeRiseSec, or longer, so
	 *  that at its fastest (its kind's FleeCm over e times this) its shortest walkers take no more than
	 *  HerdGallopStridesHz strides a second. */
	SLONK_API double HerdRiseSec(const FFaunaGroup& G, const FFaunaKindDesc& D);
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
	 * faces (the fright ahead of it), it flees on the way it is going. Its fright lasts the longer the further it has to
	 * come back (what an earlier one left it carrying besides its own flight), so it comes back no faster than after one.
	 * Water given, its flight keeps dry: of that way and the ways turned from it by a twelfth, a sixth and a quarter of a
	 * turn either side (the nearer first), the first whose whole flight and walk back (its frame read every
	 * HerdFlightReadCm along them, its radius and HerdFlightDryCm either side, and as far on as its gallop would coast
	 * frightened again there) finds no water; with none, it flees along its own loop (whose ground keeps clear of water
	 * by more than the crew may walk it off it).
	 */
	SLONK_API bool StartleHerd(FPanic& P, FHerdState& S, const FFaunaGroup& G, const FFaunaKindDesc& D, double RadiusCm, const FFaunaThreat& T, double Now, bool bForce = false,
		FHerdWater* Water = nullptr);
	/** Whether a herd's fright (P, from Now to its end, the crew's offset Avoid besides) keeps it dry, as StartleHerd reads
	 *  a flight. */
	SLONK_API bool HerdFlightDry(const FFaunaGroup& G, const FFaunaKindDesc& D, const FPanic& P, double RadiusCm, double Now, const FVector& Avoid, FHerdWater& Water);
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
	/** Sends a group of leapers diving if a craft scares it (Scares: in the air lower than its kind's ScareAltitudeCm over
	 *  the sea, within its ScareRadiusCm; someone on foot never does, on a shore or wading), unless it was startled in the
	 *  last RestartleSec; with bForce, whatever comes and however lately. None of its fish leaves the water from then
	 *  (Leaper::DiveLeadSec on, past the leaps already on their way up) to Leaper::DiveSec after its latest startle; a leap
	 *  already in sight comes down. Dive.T0 is its latest startle and Dive.ScatterT0 when its dive began: a startle while
	 *  the dive lasts carries it on from there (moved up to ten seconds back after half an hour of it). Once its window has
	 *  closed (or is all but closed, DiveLeadSec short of it) its fish leap again, though still shaken a few seconds: a
	 *  startle then, forced or not, does nothing until every leap the old dive kept under is past its time back in the
	 *  water (Leaper::HeldUnderSec), and after it begins a new dive; so no startle ever hides a fish in sight, nor shows
	 *  one the dive kept under. On the world's own seconds, as the leaps are. True if it did. */
	SLONK_API bool StartleLeapers(FPanic& Dive, const FFaunaKindDesc& D, const FVector& GroupPosCm, const FFaunaThreat& T, double Now, bool bForce = false);
	/** Whether a group's dive keeps in the water a leap leaving at T0 (the world's seconds): one leaving from when its dive
	 *  began (Dive.ScatterT0) to Leaper::DiveSec after its latest startle (Dive.T0), as the material reads it. */
	SLONK_API bool LeapDived(const FPanic& Dive, double T0);
	/** A group of leapers' dive as the material reads it (LeaperData's slots 1, 6 and 7), Now the world's seconds: the
	 *  other slots are left as they are. */
	SLONK_API void LeaperDiveData(const FPanic& Dive, double Now, float* Group);
	/** The sea's surface each of a group's fish's leaps crosses, as last read: the leap's cycle (none read yet: the lowest),
	 *  and the surface where it leaves the water and where it dives back in, at the swell's full strength, cm. */
	struct FLeaperSurfaceCache
	{
		int64 Cycle[Leaper::MaxPerGroup];
		double Surface[Leaper::MaxPerGroup][2];
		FLeaperSurfaceCache()
		{
			for (int32 i = 0; i < Leaper::MaxPerGroup; ++i) { Cycle[i] = TNumericLimits<int64>::Lowest(); Surface[i][0] = Surface[i][1] = 0.0; }
		}
	};
	/** The leap of a fish the material is given the sea's surface for at a moment (the world's seconds): its current one
	 *  until a second after it is back under the water, then its next (many seconds off, so the material never reads one
	 *  leap's surface for another's). */
	SLONK_API FLeap LeapToRead(const float* Custom, double WorldSec);
	/**
	 * What the material reads of a group of leapers at a moment (the world's seconds), into its custom primitive data
	 * (LeaperData::Num floats; the night left as it is): its dive (LeaperDiveData), and the sea's surface where and when
	 * each fish's leap to read (LeapToRead) leaves the water and dives back in, faded by Fade (SwellFade at the camera's
	 * distance: the surface the terrain draws there): read afresh (LeapCrossing, at full strength) for a fish whose leap to
	 * read has changed since Cache's, else taken from it. Its fish's custom floats (Custom::Num each) and homes (in its
	 * frame) as its set was given them.
	 */
	SLONK_API void LeaperGroupData(const FFaunaGroup& G, TConstArrayView<float> Custom, TConstArrayView<FVector> Homes, double RadiusCm, double SwellCm, double Fade, double WorldSec,
		const FPanic& Dive, FLeaperSurfaceCache& Cache, float* Group);

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
	 * The material's motion of a fish's vertex (fauna_material.py's leaper branch), transliterated in floats as the GPU
	 * runs it: the offset of a vertex at LP (its unit mesh's cm, in its instance's frame, which is its group's unturned
	 * and scaled by its length) with its vertex colour, at the material's time T, for a fish's custom floats (Custom::Num)
	 * in its group's custom primitive data (LeaperData::Num), DistCm from the camera, which sees PxPerRad pixels a radian.
	 * In the instance's frame, unit-mesh cm. LeaperShown is the pixel shader's own reading of the same leap: whether the
	 * fish is drawn at all then (its fade's dither aside); and LeaperTurn the pitch and the way it turns the fish then
	 * (radians), by which the pixel shader turns its normals.
	 */
	SLONK_API FVector3f LeaperMotion(const FVector3f& LP, const FLinearColor& VC, float T, const float* Custom, const float* Group, float DistCm, float PxPerRad);
	SLONK_API bool LeaperShown(float T, const float* Custom, const float* Group);
	SLONK_API FVector2f LeaperTurn(float T, const float* Custom, const float* Group);

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

		/** A changed world (another density, a kind switched off, a new profile) clears everything: false. One whose seas
		 *  alone changed (fauna switched off and on takes the shoals out of the terrain's material and puts them back) lets
		 *  only the leapers' groups and cells go, which alone live by them, and asks for a scan; the other kinds' groups about
		 *  and cells read are kept: true (as for the same world). */
		bool SetWorld(const FFaunaWorld& W);
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
