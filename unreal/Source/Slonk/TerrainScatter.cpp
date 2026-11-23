#include "TerrainScatter.h"
#include "FaunaTypes.h"
#include "LandmarkMeshes.h"
#include "PlanetCities.h"
#include "ScatterSpot.h"

namespace
{
	enum class EClass : uint8 { Boulder, Stone, Pebble, Tree, Grass };
	/** A size class: a lattice of candidates, the chunks it rides on, and how big its pieces are. */
	struct FClass
	{
		EClass Kind;
		/** Wanted spacing of the lattice, cm; it snaps to the nearest level of the quadtree on each planet. */
		double SpacingCm;
		/** Rides on chunks with edges this long or shorter, cm, all of it. */
		double MaxEdgeCm;
		/** A rock's size across its largest axis, a plant's height, cm. */
		double MinCm, MaxCm;
		uint64 Salt;
		/** And on this many coarser levels besides, thinned: Keep of the candidates on the first, Keep squared on the
		 *  next. The kept ones are picked by a hash of their own, apart from the draws that place them, so a piece
		 *  far off is one of those close by, in the same place, and the class thins out with distance rather than
		 *  ending at a ring. */
		int32 FarLevels;
		double Keep;
	};
	// Boulders on chunks to 150-odd metres (seen to about 600 m) and half of them to 300 m chunks (1.2 km); stones to
	// 40 m chunks (150 m) and pebbles on the last level (35 m), where they are a pixel or two across, so no further;
	// grass on the last level, half on the one before; trees on chunks to 300 m, two in five on the next level, one in
	// six on the next, one in sixteen and one in forty on the two after (chunks to 5 km), and on Horizon one in a hundred
	// and one in 244 on the two after that (chunks to 10 and 20 km), as many of those as the world needs (FarLevelsUsed).
	// The plants fade out at three edges of the coarsest chunk carrying them (FadeEdges), and a world's chunk edges halve
	// from its face's, so a level lands on edges between half and all of its limit: the trees reach about 0.5-0.9,
	// 0.9-1.9, 1.8-3.7, 3.7-7.4 and, on Horizon, 20-40 km by the world's size, where its chunks are their depth's share of
	// the face's edge (towards a face's edges they run up to 30% smaller, and a level lands a depth higher or lower: the
	// trees on Horizon end from about 15 to 60 km off there). Past the trees the terrain's own colours carry the forest. Turning with the planet costs an instance nothing on the CPU (UPlanetInstancesComponent), but each is still
	// drawn, in the velocity pass too as it moves: a tree past a couple of kilometres is its far crown, a handful of
	// triangles (SlonkPlants), so its far levels cost the GPU a fraction of what its cards would.
	constexpr FClass Classes[] = {
		{ EClass::Boulder, 750.0, 26000.0, 120.0, 420.0, 0x51ed270b2f1c7e3dULL, 1, 0.5 },
		{ EClass::Stone, 220.0, 6500.0, 26.0, 95.0, 0x2545f4914f6cdd1dULL, 0, 1.0 },
		{ EClass::Pebble, 60.0, 1700.0, 6.0, 22.0, 0x8c4b5bd7d9a1e5f3ULL, 0, 1.0 },
		{ EClass::Tree, 600.0, 31000.0, 700.0, 2200.0, 0x3c6ef372fe94f82bULL, 6, 0.4 },
		{ EClass::Grass, 45.0, 1700.0, 25.0, 60.0, 0xa54ff53a5f1d36f1ULL, 1, 0.5 },
	};
	/** Shrubs stand on the material's scrub dots, on chunks to 76 m (seen to about 250 m; the painted dots beyond),
	 *  and seven in ten on the next level (500 m). */
	constexpr double ShrubMaxEdgeCm = 8000.0;
	constexpr int32 ShrubFarLevels = 1;
	constexpr double ShrubKeep = 0.7;
	/** The salt of the hash that picks the pieces kept on the far levels. */
	constexpr uint64 KeepSalt = 0x7f4a7c159e3779b9ULL;
	/** And of the one that mixes a role's two species where their stands meet: a number of the plant's own, apart from
	 *  the draws that place it, so a world with one species to a role places every plant as it always did. */
	constexpr uint64 SpeciesSalt = 0x9d2c5680a3b1f4e7ULL;
	/** And the giants' lattice's own (SlonkScatter::GiantSpacingCm and the rest): on chunks to 2.5 km (seen to about
	 *  7 km) and half of them on each of three levels more (to 60 km), where one two hundred metres tall is still a few
	 *  pixels of far crown. */
	constexpr uint64 GiantSalt = 0x1f83d9abfb41bd6bULL;
	int32 GiantLevel(double FaceArcCm) { return FMath::Clamp(FMath::RoundToInt32(FMath::Log2(FaceArcCm / SlonkScatter::GiantSpacingCm)), 0, 40); }
	int32 GiantFarLevelsUsed(const FScatterSettings& S) { return FMath::Clamp(FMath::Min(SlonkScatter::GiantFarLevels, S.MaxFarLevels), 0, SlonkScatter::GiantFarLevels); }

	/** How many levels a chunk lies above the coarsest that carries all of a class: 0 at or below it. */
	int32 LevelsAbove(double EdgeCm, double MaxEdgeCm)
	{
		return EdgeCm <= MaxEdgeCm * 1.0001 ? 0 : FMath::CeilToInt32(FMath::Log2(EdgeCm / MaxEdgeCm) - 1e-6);
	}

	/**
	 * Where a plant fades out, cm from the camera: FadeEdges edges of the coarsest chunk that carries it. A chunk
	 * splits at SplitFactor of its own edges, so the finer ones carrying a plant arrive with it at least about
	 * 2 x SplitFactor of their edges away; it fades before that and never pops in. The level a piece is kept to is
	 * the last whose share its keep draw falls under.
	 */
	/** The largest of a world's chunk edges no longer than LimitCm, cm: they halve from RootEdgeCm. */
	double ChunkEdgeUnder(double RootEdgeCm, double LimitCm)
	{
		const int32 Depth = FMath::Max(0, FMath::CeilToInt32(FMath::Log2(RootEdgeCm / LimitCm) - 1e-6));
		return RootEdgeCm / FMath::Pow(2.0, (double)Depth);
	}

	double FadeFarCm(const SlonkScatter::FChunkGrid& G, double MaxEdgeCm, int32 FarLevels, double Keep, double KeepDraw, double FadeEdges)
	{
		int32 Top = 0;
		for (int32 k = FarLevels; k > 0; --k) { if (KeepDraw < FMath::Pow(Keep, (double)k)) { Top = k; break; } }
		// The coarsest chunk carrying it: the largest of the world's edges no longer than the class's at that level.
		return FadeEdges * ChunkEdgeUnder(G.EdgeCm * FMath::Pow(2.0, (double)G.Depth), MaxEdgeCm * FMath::Pow(2.0, (double)Top));
	}

	/**
	 * How many of its far levels a class uses on a world whose faces' chunk edge is FaceEdge (SlonkScatter::FaceEdgeCm,
	 * the same for every chunk of the world, so parent and child, and the chunks across a face, agree): as many as the
	 * settings allow; and on their last step (Horizon) the trees as many more as they need to reach HorizonTreeReachCm. A
	 * level lands on the world's chunk edges between half and all of its limit, so how far a number of levels reaches
	 * varies by twice with the world's size: Horizon takes the levels this world needs, two or three more than the step
	 * before's.
	 */
	int32 FarLevelsUsed(const FClass& Cl, const FScatterSettings& S, double FaceEdge)
	{
		int32 Far = FMath::Min(Cl.FarLevels, S.MaxFarLevels);
		if (Cl.Kind == EClass::Tree && S.MaxFarLevels >= SlonkScatter::HorizonStep && FaceEdge > 0.0)
		{
			while (Far < Cl.FarLevels && S.FadeEdges * ChunkEdgeUnder(FaceEdge, Cl.MaxEdgeCm * FMath::Pow(2.0, (double)Far)) < SlonkScatter::HorizonTreeReachCm) { ++Far; }
		}
		return Far;
	}

	/** Height to width of each rock shape, and the second horizontal axis to the first: ranges a rock is drawn from. */
	struct FProportion { double ZLo, ZHi, YLo, YHi, TiltDeg; };
	constexpr FProportion Proportions[SlonkGroundRocks::NumKinds] = {
		{ 0.50, 0.85, 0.65, 1.0, 10.0 },	// Angular
		{ 0.55, 0.95, 0.60, 1.0, 7.0 },		// Block
		{ 0.16, 0.32, 0.55, 1.0, 5.0 },		// Slab
		{ 0.45, 0.75, 0.65, 1.0, 10.0 },	// Rounded
	};

	int32 PickKind(const double (&W)[SlonkGroundRocks::NumKinds], double U)
	{
		double Sum = 0.0;
		for (double V : W) { Sum += FMath::Max(V, 0.0); }
		double Acc = 0.0;
		for (int32 K = 0; K < SlonkGroundRocks::NumKinds; ++K)
		{
			Acc += FMath::Max(W[K], 0.0);
			if (U * Sum < Acc) { return K; }
		}
		return 0;
	}
}

namespace SlonkScatter
{
	/** The settlements near a chunk, as circles the scatter keeps out of, and the ways the terrain paints round them
	 *  (the roads between them, a grown town's outer ring ways). */
	struct FKeepOut
	{
		TArray<TPair<FVector, double>> Towns;
		TArray<FTownSite> Sites;
		FPlanetTerrainParams Params;
		FCivilization Civ;
		void Build(const FPlanetTerrainParams& P, const FCivilization& People, const SlonkScatter::FChunkGrid& G) { BuildAround(P, People, G.Origin, G.EdgeCm * 0.75); }
		/** The settlements within a reach of a place (planet-local cm), the nearest 64. */
		void BuildAround(const FPlanetTerrainParams& P, const FCivilization& People, const FVector& CentreLocalCm, double ReachCm)
		{
			Params = P;
			Civ = People.bInhabited ? People : SlonkCivs::Colonists();
			SlonkCities::EnumerateTowns(P, Civ, CentreLocalCm, ReachCm + SlonkCities::CityRadiusCm(1.f) + 5000.0, Sites, 64);
			// A cliff town runs its radius along the wall and its profile 300 m across it, either way.
			for (const FTownSite& T : Sites) { Towns.Add({ T.LocalCm, FMath::Square((T.bCliff ? FMath::Sqrt(FMath::Square(T.RadiusCm) + FMath::Square(30000.0)) : T.RadiusCm) + 2500.0) }); }
		}
		/** In a town's ground; a plant (bPlant) not where it stands well inside one of the town's parks, which its
		 *  builder keeps every building out of (SlonkCities::ParkAt). */
		bool Contains(const FVector& Up, double R, bool bPlant = false) const
		{
			const FVector OnSphere = Up * R;
			for (int32 i = 0; i < Towns.Num(); ++i)
			{
				if (FVector::DistSquared(OnSphere, Towns[i].Key) >= Towns[i].Value) { continue; }
				if (bPlant && SlonkCities::ParkAt(Params, Sites[i], Up) >= 0.7f) { continue; }
				return true;
			}
			return false;
		}
		/** On a way the terrain paints, or within a couple of metres of one. */
		bool OnWay(const FVector& Up) const { return SlonkCities::PaintedWayCm(Params, Civ, Up) < 200.0; }
		/** Or within a distance of one: a giant's foot is metres across. */
		bool WayWithin(const FVector& Up, double Cm) const { return SlonkCities::PaintedWayCm(Params, Civ, Up) < Cm; }
	};
}

SlonkScatter::FChunkKeepOut::FChunkKeepOut() = default;
SlonkScatter::FChunkKeepOut::~FChunkKeepOut() = default;

const SlonkScatter::FKeepOut& SlonkScatter::FChunkKeepOut::Get(const FPlanetTerrainParams& P, const FCivilization& People, const FChunkGrid& G)
{
	if (!KeepOut)
	{
		KeepOut = MakeUnique<FKeepOut>();
		KeepOut->Build(P, People, G);
	}
	return *KeepOut;
}

namespace
{
	void AddPiece(SlonkScatter::FChunkScatter& Out, int32 Mesh, const FTransform& Xf, const float (&C)[SlonkScatter::NumCustomFloats], bool bSolid)
	{
		Out.Transforms[Mesh].Add(Xf);
		Out.Custom[Mesh].Append(C, SlonkScatter::NumCustomFloats);
		if (bSolid) { Out.Solids[Mesh].Add(Xf); }
	}
}

int32 SlonkScatter::FChunkScatter::Num() const
{
	int32 N = 0;
	for (const TArray<FTransform>& T : Transforms) { N += T.Num(); }
	return N;
}

int32 SlonkScatter::FChunkScatter::NumPlants() const
{
	int32 N = 0;
	for (int32 M = FirstPlant; M < FirstLandmark; ++M) { N += Transforms[M].Num(); }
	return N;
}

double SlonkScatter::MaxChunkEdgeCm(const FScatterSettings& S, double FaceEdge)
{
	double M = ShrubMaxEdgeCm * FMath::Pow(2.0, (double)FMath::Min(ShrubFarLevels, FMath::Max(S.MaxFarLevels, 0)));
	for (const FClass& C : Classes) { M = FMath::Max(M, C.MaxEdgeCm * FMath::Pow(2.0, (double)FarLevelsUsed(C, S, FaceEdge))); }
	// The giants' coarse chunks carry something only on a world that grows a giant.
	if (S.Flora.HasGiants()) { M = FMath::Max(M, GiantMaxEdgeCm * FMath::Pow(2.0, (double)GiantFarLevelsUsed(S))); }
	// And the landmarks' only on a world that has any.
	if (S.Landmarks.Any()) { M = FMath::Max(M, LandmarkMaxEdgeCm); }
	return M;
}

int32 SlonkScatter::TreeFarLevels(const FScatterSettings& S, double FaceEdge)
{
	for (const FClass& C : Classes) { if (C.Kind == EClass::Tree) { return FarLevelsUsed(C, S, FaceEdge); } }
	return 0;
}

double SlonkScatter::TreeReachCm(const FScatterSettings& S, double FaceEdge)
{
	for (const FClass& C : Classes)
	{
		if (C.Kind == EClass::Tree) { return S.FadeEdges * ChunkEdgeUnder(FaceEdge, C.MaxEdgeCm * FMath::Pow(2.0, (double)FarLevelsUsed(C, S, FaceEdge))); }
	}
	return 0.0;
}

FVector SlonkScatter::ToNoiseSpace(const FVector& PlanetLocalCm)
{
	auto Wrap = [](double V) { double M = FMath::Fmod(V, NoisePeriodCm); if (M < 0.0) { M += NoisePeriodCm; } return M; };
	return FVector(Wrap(PlanetLocalCm.X), Wrap(PlanetLocalCm.Y), Wrap(PlanetLocalCm.Z));
}

FIntVector SlonkScatter::ScrubCellKey(const FIntVector& Cell)
{
	auto Wrap = [](int32 I) { const int32 W = I % ScrubCellsPerPeriod; return W < 0 ? W + ScrubCellsPerPeriod : W; };
	return FIntVector(Wrap(Cell.X), Wrap(Cell.Y), Wrap(Cell.Z));
}

bool SlonkScatter::ScrubDotAt(const FVector& NoiseP, FVector& OutCentre, double& OutRadiusCm)
{
	// terrain_material.py, the first pass of the jittered dots: the same cells, the index wrapped to the period the
	// same way, the same hash. The centre stays in the caller's noise space (the cell unwrapped).
	const FIntVector Fl((int32)FMath::FloorToDouble(NoiseP.X / ScrubCellCm), (int32)FMath::FloorToDouble(NoiseP.Y / ScrubCellCm), (int32)FMath::FloorToDouble(NoiseP.Z / ScrubCellCm));
	const FIntVector C = ScrubCellKey(Fl);
	uint32 H = ((uint32)C.X * 73856093u) ^ ((uint32)C.Y * 19349663u) ^ ((uint32)C.Z * 83492791u) ^ (31u * 2654435761u);
	H = (H ^ (H >> 13)) * 0x5bd1e995u;
	H ^= H >> 15;
	if ((float)(H & 0xffu) / 255.f >= 0.35f) { return false; }
	const FVector Ctr = FVector((double)((H >> 8) & 0xffu), (double)((H >> 16) & 0xffu), (double)((H >> 24) & 0xffu)) / 255.0 * 0.5 + FVector(0.25);
	OutCentre = (FVector(Fl) + Ctr) * ScrubCellCm;
	OutRadiusCm = (0.12 + 0.13 * (double)((H >> 4) & 0xffu) / 255.0) * ScrubCellCm;
	return true;
}

namespace
{
	/** The salt of the motes' cells: where a cloud is and what it is (its keep draw takes KeepSalt as well). */
	constexpr uint64 MoteSalt = 0x2b7e151628aed2a6ULL;
	/** The best chance of a cloud over a cell: over the wettest level ground along a forest's edge. */
	constexpr double MotePeak = 0.45;

	/** The spot as a chunk would read it at a unit direction, on the field's own ground rather than a drawn triangle,
	 *  and the field's sample there. */
	SlonkScatter::FSpot FieldSpot(const FPlanetTerrainParams& P, const FVector& Up, FPlanetTerrainSample& OutT)
	{
		using SlonkScatter::SmoothStep;
		const FPlanetTerrainSample T = SlonkTerrain::Sample(P, Up, 1000.0);
		SlonkScatter::FSpot Sp;
		Sp.Slope = T.Slope; Sp.Rock = T.Rock; Sp.Wet = T.Wet; Sp.Ice = T.Ice; Sp.Canyon = T.Canyon; Sp.Wall = T.Wall;
		Sp.Crater = T.Crater; Sp.Table = T.Table; Sp.River = T.River; Sp.HeightCm = T.HeightCm; Sp.Stratum = T.Stratum;
		Sp.Geo = T.Geo;
		Sp.Up = Up; Sp.Nt = Up; Sp.Pt = Up * (P.RadiusCm + T.HeightCm);
		// The regimes from the slope, as SpotAt has them.
		Sp.Cliff = SmoothStep(0.7, 1.3, Sp.Slope);
		Sp.Talus = SmoothStep(0.18, 0.5, Sp.Slope) * (1.0 - Sp.Cliff);
		Sp.Flat = 1.0 - SmoothStep(0.12, 0.4, Sp.Slope);
		Sp.Rocky = SmoothStep(0.35, 0.8, Sp.Rock) * (1.0 - Sp.Cliff);
		Sp.WallFoot = SmoothStep(0.05, 0.3, Sp.Wall) * (1.0 - Sp.Cliff);
		Sp.Gorge = Sp.Canyon * (1.0 - SmoothStep(0.03, 0.2, Sp.Slope));
		OutT = T;
		return Sp;
	}

	/**
	 * The chance of a mote cloud over a spot, before the world's life and the density setting: the wet, level ground,
	 * thickest along a forest's edge (the stands' field at half, where the woods give onto the meadows) and thinner in
	 * open country or deep in the trees; none on a cliff, in a river, on lying snow or ice, near the treeline or above
	 * it, nor on a locked world's ground too hot for the grass or at freezing.
	 */
	double MoteDensityOf(const FPlanetTerrainParams& P, const SlonkScatter::FSpot& Sp)
	{
		using SlonkScatter::SmoothStep;
		// None over a strange world's own geology, where nothing grows for them to rise from.
		if (Sp.River > 0.15 || Sp.Cliff > 0.3 || Sp.Geo > 0.25) { return 0.0; }
		const double Base = SmoothStep(0.25, 0.6, Sp.Wet) * Sp.Flat * (1.0 - SmoothStep(0.05, 0.35, Sp.Ice));
		if (Base <= 0.0) { return 0.0; }
		const SlonkScatter::FClimate Clim = SlonkScatter::ClimateAt(P, Sp);
		double D = Base * (1.0 - SmoothStep(0.55, 0.85, Clim.Alpine));
		if (Clim.bClimate) { D *= (1.0 - SmoothStep(0.7, 0.95, Clim.Heat)) * SmoothStep(0.0, 0.08, Clim.Heat); }
		if (D <= 0.0) { return 0.0; }
		const double Forest = SlonkScatter::ForestAt(P, Sp);
		const double Edge = 4.0 * Forest * (1.0 - Forest);
		return D * (0.15 + 0.85 * Edge) * MotePeak;
	}
}

SlonkScatter::FHabitat SlonkScatter::HabitatAt(const FPlanetTerrainParams& P, const FVector& Dir)
{
	const FVector Up = Dir.GetSafeNormal();
	// The spot as a chunk would read it there, on the field's own ground rather than a drawn triangle.
	FPlanetTerrainSample T;
	const FSpot Sp = FieldSpot(P, Up, T);
	const FClimate Clim = ClimateAt(P, Sp);
	FHabitat H;
	H.Wet = Sp.Wet;
	H.Flat = Sp.Flat;
	H.Cliff = Sp.Cliff;
	H.WallFoot = Sp.WallFoot;
	// As the trees are placed and the ground's cover is painted: none in the water or the rivers.
	H.Forest = (T.bWater || Sp.River > 0.08) ? 0.0 : TreeDensity(Sp, Clim, ForestAt(P, Sp));
	H.Alpine = Clim.Alpine;
	H.Ice = Sp.Ice;
	H.bWater = T.bWater;
	H.DepthCm = T.bWater ? FMath::Max(T.WaterCm - T.GroundCm, 0.0) : 0.0;
	H.HeightCm = T.HeightCm;
	H.Geo = Sp.Geo;
	return H;
}

int32 SlonkScatter::SpeciesAt(const FPlanetTerrainParams& P, const FFloraProfile& Flora, int32 First, const FVector& Up, double Mix)
{
	const int32 Second = SlonkFlora::SecondOfRole(First);
	const FFloraSpecies& A = Flora.Species[First];
	const FFloraSpecies& B = Flora.Species[Second];
	if (!B.bLive) { return First; }
	if (!A.bLive) { return Second; }
	// Stands a few hundred metres across, each role's laid apart from the others', and the first species as much of
	// them as its share of the two.
	const double Field = SlonkTerrain::Fbm(Up * (P.RadiusCm / 60000.0) + P.Seed * 9.1 + FVector(3.0 * First, 5.0, 1.0), 2);
	const double U = FMath::Clamp(0.5 + 0.9 * Field + 0.3 * (Mix - 0.5), 0.0, 1.0);
	return U < (double)A.Share / FMath::Max((double)A.Share + (double)B.Share, 1e-6) ? First : Second;
}

double SlonkScatter::GiantDensityAt(const FPlanetTerrainParams& P, const FVector& Dir)
{
	const FVector Up = Dir.GetSafeNormal();
	FPlanetTerrainSample T;
	const FSpot Sp = FieldSpot(P, Up, T);
	if (T.bWater || Sp.River > 0.08 || Sp.Cliff > 0.3 || P.bAirless) { return 0.0; }
	return GiantDensity(P, Sp, ClimateAt(P, Sp));
}

namespace
{
	/** A giant standing in a cell of the giants' lattice. */
	struct FGiantStand
	{
		/** Its place on its face, and its direction there. */
		double U = 0.0, V = 0.0;
		FVector Up = FVector::ZeroVector;
		/** Its species' slot, its height, and how far round its foot nothing else grows, cm. */
		int32 Slot = SlonkFlora::Slot::Giant;
		double HeightCm = 0.0, FootCm = 0.0;
	};

	/**
	 * The giant in a cell of the giants' lattice, if one stands there. Whether one does, where, of which species and
	 * how tall are read from the planet's field and the cell's own draws, not from any chunk's drawn mesh: every chunk
	 * at every level finds the same giant in the same place, and so does whatever else is placed near it, which keeps
	 * out of its foot. D is the cell's draws, left where its own chunk takes up the rest (its seed, its girth, how it
	 * stands).
	 */
	bool GiantInCell(const FPlanetTerrainParams& P, const FScatterSettings& S, int32 Face, int32 L, int64 GX, int64 GY, FGiantStand& Out, SlonkScatter::FDraw& D)
	{
		using namespace SlonkScatter;
		const double Delta = 2.0 / (double)((int64)1 << L);
		Out.U = -1.0 + ((double)GX + D.Next()) * Delta;
		Out.V = -1.0 + ((double)GY + D.Next()) * Delta;
		const double Draw = D.Next(), Tall = D.Next();
		Out.Up = SlonkTerrain::CubeDir(Face, Out.U, Out.V);
		if (Draw >= FMath::Min(GiantDensityAt(P, Out.Up) * S.Life * S.Density, 0.9)) { return false; }
		Out.Slot = SlonkFlora::Slot::Giant;
		if (S.Flora.Species[SlonkFlora::Slot::Giant2].bLive || !S.Flora.Species[SlonkFlora::Slot::Giant].bLive)
		{
			Out.Slot = SpeciesAt(P, S.Flora, SlonkFlora::Slot::Giant, Out.Up, FDraw(CellHash(Face, L, GX, GY, GiantSalt ^ SpeciesSalt)).Next());
		}
		const FFloraSpecies& Kind = S.Flora.Species[Out.Slot];
		if (!Kind.bLive) { return false; }
		Out.HeightCm = GiantMinCm * FMath::Pow(GiantMaxCm / GiantMinCm, FMath::Pow(Tall, 1.3)) * Kind.Height;
		Out.FootCm = Kind.UnitFootCm * Out.HeightCm / SlonkPlants::UnitCm;
		return true;
	}
}

namespace
{
	/** The landmarks' lattices' salt: each kind's is its own of it, and each of a site's members' draws theirs. */
	constexpr uint64 LandmarkSalt = 0x6a09e667f3bcc908ULL;
	/** A world's own: the cells of two worlds of a size are the same cells, and without its hash the same ones would be
	 *  sites on both, for stones of the same height turned the same way. */
	uint64 LandmarkSaltOf(const FLandmarkProfile& World, ELandmark Kind, int32 Member = -1)
	{
		return SlonkScatter::Mix64(LandmarkSalt + 0x9E3779B97F4A7C15ULL * (uint64)((int32)Kind + 1) + 0xD1B54A32D192ED03ULL * (uint64)(Member + 1) + 0xC2B2AE3D27D4EB4FULL * (uint64)World.Hash);
	}
	int32 LandmarkLevel(double FaceArcCm) { return FMath::Clamp(FMath::RoundToInt32(FMath::Log2(FaceArcCm / SlonkScatter::LandmarkSpacingCm)), 0, 40); }

	/** How well a spot of the field suits a landmark of a kind and a size, 0 (none stands there) to 1. */
	double LandmarkGround(const FPlanetTerrainParams& P, ELandmark Kind, const SlonkScatter::FSpot& Sp, const FPlanetTerrainSample& T, double SizeCm, float Life)
	{
		using namespace SlonkScatter;
		if (SlonkLandmarks::SpecOf(Kind).bInSea)
		{
			// The sea's own shallows (no river's water, no sea of lava), free of ice: from a couple of metres deep to two
			// fifths of the tower's height, which is what its mesh has under its origin to stand on the floor with.
			if (!T.bWater || T.HeightCm >= 0.0 || P.bLava) { return 0.0; }
			const double Depth = -T.HeightCm;
			return SmoothStep(150.0, 400.0, Depth) * (1.0 - SmoothStep(0.3 * SizeCm, 0.42 * SizeCm, Depth)) * (1.0 - SmoothStep(0.15, 0.4, Sp.Ice));
		}
		// On land: never in water, on a cliff, or on the world's own geology.
		if (T.bWater || Sp.River > 0.08 || Sp.Cliff > 0.3 || Sp.Geo > 0.1) { return 0.0; }
		const double SnowLying = SmoothStep(0.2, 0.6, Sp.Ice);
		const FClimate Clim = ClimateAt(P, Sp);
		const double Woods = Life > 0.f ? TreeDensity(Sp, Clim, ForestAt(P, Sp)) : 0.0;
		switch (Kind)
		{
		case ELandmark::Arch:
		{
			// Rock to stand in: tableland, rocky ground, or ground plainly dry; none through lying snow, and none on a
			// slope past one in two, where the ground falls away from under the downhill leg faster than the leg runs on.
			const double Dry = 1.0 - SmoothStep(0.25, 0.55, Sp.Wet);
			return SmoothStep(0.15, 0.5, FMath::Max3(Sp.Table, Sp.Rocky, 0.7 * Dry)) * (0.3 + 0.7 * Sp.Flat) * (1.0 - SmoothStep(0.3, 0.5, Sp.Slope)) * (1.0 - SnowLying);
		}
		case ELandmark::Monolith:
			// Open, level ground with hardly a tree on it: a people raised them to be seen.
			return Sp.Flat * (1.0 - SmoothStep(0.04, 0.18, Woods)) * (1.0 - 0.5 * SnowLying);
		case ELandmark::Ring:
			// The same, and flatter: its foot is a hundred metres and more.
			return Sp.Flat * (1.0 - SmoothStep(0.05, 0.2, Sp.Slope)) * (1.0 - SmoothStep(0.04, 0.18, Woods)) * (1.0 - 0.5 * SnowLying);
		case ELandmark::Mound:
			// A mound city: open grassland, neither parched nor sodden, under the treeline.
			return Sp.Flat * SmoothStep(0.15, 0.35, Sp.Wet) * (1.0 - SmoothStep(0.6, 0.85, Sp.Wet)) * (1.0 - SmoothStep(0.04, 0.2, Woods)) * (1.0 - SnowLying)
				* (1.0 - SmoothStep(0.5, 0.8, Clim.Alpine));
		case ELandmark::Shell:
			// Where the sea is, or was: open, level ground within a couple of hundred metres of its level.
			return Sp.Flat * (1.0 - SmoothStep(6000.0, 20000.0, T.HeightCm)) * (1.0 - SmoothStep(0.05, 0.25, Woods)) * (1.0 - 0.5 * SnowLying);
		default:
			// Rock hangs over the rock it was torn from: tableland and rocky ground the most, any ground a little, none
			// over a slope that would stand into it.
			return (1.0 - SmoothStep(0.45, 0.65, Sp.Slope)) * (0.35 + 0.65 * FMath::Max(Sp.Rocky, Sp.Table));
		}
	}

	/**
	 * A place in a lattice cell where the sea is as deep as a tower of SizeCm stands in, for a kind that stands in the
	 * shallows: they are a narrow band round a coast, which a place drawn at random in the cell mostly misses. Nine
	 * places spread over the cell are read; between one where the sea is shallower than that (or there is none) and a
	 * neighbour where it is deeper, the line is halved until the depth is found. False if no coast crosses the cell.
	 */
	bool LandmarkShallows(const FPlanetTerrainParams& P, int32 Face, int64 GX, int64 GY, double Delta, double SizeCm, SlonkScatter::FDraw& D, double& U, double& V)
	{
		// Deep enough to stand in and shallow enough that the least of a site's towers reaches the floor.
		const double Want = FMath::Max(SizeCm * FMath::Lerp(0.08, 0.17, D.Next()), 350.0);
		const int32 Start = FMath::Min((int32)(D.Next() * 12.0), 11);
		auto PlaceAt = [&](int32 i, int32 j, double& OutU, double& OutV)
		{
			OutU = -1.0 + ((double)GX + 0.1 + 0.4 * i) * Delta;
			OutV = -1.0 + ((double)GY + 0.1 + 0.4 * j) * Delta;
		};
		// The sea's depth at a place, cm; under nothing where there is no sea (a river's water is none).
		auto DepthAt = [&](double Au, double Av)
		{
			const FPlanetTerrainSample T = SlonkTerrain::Sample(P, SlonkTerrain::CubeDir(Face, Au, Av), 1000.0);
			return T.bWater && T.HeightCm < 0.0 ? -T.HeightCm : -1.0;
		};
		double Depth[3][3];
		for (int32 j = 0; j < 3; ++j)
		for (int32 i = 0; i < 3; ++i)
		{
			double Au, Av;
			PlaceAt(i, j, Au, Av);
			Depth[j][i] = DepthAt(Au, Av);
		}
		// The twelve pairs of neighbours, six along u and six along v, from one the cell draws.
		for (int32 k = 0; k < 12; ++k)
		{
			const int32 e = (Start + k) % 12;
			const int32 i0 = e < 6 ? e % 2 : (e - 6) % 3, j0 = e < 6 ? e / 2 : (e - 6) / 3;
			const int32 i1 = e < 6 ? i0 + 1 : i0, j1 = e < 6 ? j0 : j0 + 1;
			const bool bFirstShallow = Depth[j0][i0] < Want;
			if (bFirstShallow == (Depth[j1][i1] < Want)) { continue; }
			double Su, Sv, Du, Dv;
			PlaceAt(bFirstShallow ? i0 : i1, bFirstShallow ? j0 : j1, Su, Sv);
			PlaceAt(bFirstShallow ? i1 : i0, bFirstShallow ? j1 : j0, Du, Dv);
			for (int32 Step = 0; Step < 7; ++Step)
			{
				const double Mu = 0.5 * (Su + Du), Mv = 0.5 * (Sv + Dv);
				if (DepthAt(Mu, Mv) < Want) { Su = Mu; Sv = Mv; }
				else { Du = Mu; Dv = Mv; }
			}
			U = Du; V = Dv;
			return true;
		}
		return false;
	}

	/**
	 * The landmarks of a kind at one cell of its lattice, added to Out, if the cell is a site and the ground there takes
	 * them. Whether it is, what stands there, where and how big are read from the planet's field and the cell's own
	 * draws, not from any chunk's drawn mesh: every chunk at every level finds the same ones in the same places, and so
	 * does whatever else is placed near them, which keeps out of their feet.
	 */
	bool LandmarkSite(const FPlanetTerrainParams& P, const FScatterSettings& S, ELandmark Kind, int32 Face, int32 L, int64 GX, int64 GY, TArray<SlonkScatter::FLandmarkStand>& Out)
	{
		using namespace SlonkScatter;
		const FLandmarkKind& Have = S.Landmarks.Kinds[(int32)Kind];
		const SlonkLandmarks::FKindSpec& Spec = SlonkLandmarks::SpecOf(Kind);
		FDraw D(CellHash(Face, L, GX, GY, LandmarkSaltOf(S.Landmarks, Kind)));
		// The cheap draw first: most cells are no site, and nothing of the field is read for them.
		if (!Have.bLive || D.Next() >= Spec.Sites) { return false; }
		const double Tall = D.Next(), Accept = D.Next(), Turn = D.Next(), Many = D.Next();
		const double Delta = 2.0 / (double)((int64)1 << L);
		const double SizeCm = Spec.MinCm * FMath::Pow(Spec.MaxCm / Spec.MinCm, FMath::Pow(Tall, 1.3)) * (double)Have.Size;
		// Where in the cell, a tenth in from its edges; a kind that stands in the sea, where the cell's shallows are.
		double U = -1.0 + ((double)GX + 0.1 + 0.8 * D.Next()) * Delta, V = -1.0 + ((double)GY + 0.1 + 0.8 * D.Next()) * Delta;
		if (Spec.bInSea && !LandmarkShallows(P, Face, GX, GY, Delta, SizeCm, D, U, V)) { return false; }
		const FVector Up = SlonkTerrain::CubeDir(Face, U, V);
		FPlanetTerrainSample T;
		const FSpot SiteSpot = FieldSpot(P, Up, T);
		const double Ground = LandmarkGround(P, Kind, SiteSpot, T, SizeCm, S.Life);
		if (Ground <= (Spec.bInSea ? 0.3 : 0.0) || Accept >= Ground * (double)Have.Share) { return false; }

		// What stands there: one, a row of stones along the site's own bearing, or a cluster round the first.
		FVector Ta, Tb;
		LandmarkTangents(Up, Ta, Tb);
		const double Yaw = UE_DOUBLE_TWO_PI * Turn;
		const FVector Along = Ta * FMath::Cos(Yaw) + Tb * FMath::Sin(Yaw), Across = FVector::CrossProduct(Up, Along);
		const int32 Count = FMath::Clamp(Spec.MinCount + (int32)(Many * (double)(Spec.MaxCount - Spec.MinCount + 1)), Spec.MinCount, Spec.MaxCount);
		const int32 First = Out.Num();
		// None in a town's ground or across a way, on a world with towns: asked of the towns about the site itself, not
		// of a chunk's list of them (a chunk twenty kilometres across keeps the nearest 64), so every level agrees.
		FKeepOut Towns;
		if (S.bCities) { Towns.BuildAround(P, S.People, Up * P.RadiusCm, (Spec.Apart * (double)Spec.MaxCount + Spec.Foot) * SizeCm + 40000.0); }
		for (int32 m = 0; m < Count; ++m)
		{
			FDraw Dm(CellHash(Face, L, GX, GY, LandmarkSaltOf(S.Landmarks, Kind, m)));
			const double Small = Dm.Next(), Off = Dm.Next(), TurnM = Dm.Next(), Rise = Dm.Next();
			FLandmarkStand St;
			St.Kind = Kind; St.Face = Face; St.GX = GX; St.GY = GY; St.Member = m;
			St.SizeCm = SizeCm; St.Yaw = Yaw; St.U = U; St.V = V; St.Up = Up;
			St.BaseCm = Spec.bInSea ? 0.0 : T.HeightCm;
			if (m > 0)
			{
				double Dx = 0.0, Dy = 0.0;
				if (Kind == ELandmark::Monolith)
				{
					// A row: the next a little under the first's height along the line each way, the outer ones the lower.
					const int32 Step = (m + 1) / 2;
					Dx = ((m & 1) ? 1.0 : -1.0) * Step * Spec.Apart * SizeCm;
					St.SizeCm = SizeCm * (1.0 - 0.13 * Step) * (0.92 + 0.16 * Small);
				}
				else
				{
					// A cluster: round the first, further out as they go, each the smaller, each turned its own way.
					const double Ang = 2.39996 * m, Rad = Spec.Apart * SizeCm * FMath::Sqrt((double)m) * (0.8 + 0.4 * Off);
					Dx = Rad * FMath::Cos(Ang);
					Dy = Rad * FMath::Sin(Ang);
					St.SizeCm = SizeCm * (0.45 + 0.4 * Small);
					St.Yaw = Yaw + UE_DOUBLE_TWO_PI * TurnM;
				}
				St.Up = (Up * P.RadiusCm + Along * Dx + Across * Dy).GetSafeNormal();
				// On the site's own face of the world (the lattice is a face's), on ground or in shallows that take it.
				int32 MemberFace = 0;
				if (!SlonkTerrain::FaceUV(St.Up, MemberFace, St.U, St.V) || MemberFace != Face) { continue; }
				FPlanetTerrainSample Tm;
				const FSpot Sm = FieldSpot(P, St.Up, Tm);
				if (LandmarkGround(P, Kind, Sm, Tm, St.SizeCm, S.Life) <= 0.0) { continue; }
				St.BaseCm = Spec.bInSea ? 0.0 : Tm.HeightCm;
			}
			St.FootCm = Spec.Foot * St.SizeCm;
			// One that hangs in the air, by the first's width over the ground at its own place: each at a height of its own.
			St.LiftCm = SizeCm * FMath::Lerp(Spec.LiftLo, Spec.LiftHi, Rise);
			if (S.bCities && (Towns.Contains(St.Up, P.RadiusCm, false) || Towns.WayWithin(St.Up, 200.0 + St.FootCm))) { continue; }
			Out.Add(St);
		}
		return Out.Num() > First;
	}
}

void SlonkScatter::LandmarkTangents(const FVector& Up, FVector& OutA, FVector& OutB)
{
	OutA = FVector::CrossProduct(FMath::Abs(Up.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector, Up).GetSafeNormal();
	OutB = FVector::CrossProduct(Up, OutA);
}

double SlonkScatter::LandmarkGroundAt(const FPlanetTerrainParams& P, ELandmark Kind, const FVector& Dir, double SizeCm, float Life)
{
	const FVector Up = Dir.GetSafeNormal();
	FPlanetTerrainSample T;
	const FSpot Sp = FieldSpot(P, Up, T);
	return LandmarkGround(P, Kind, Sp, T, SizeCm, Life);
}

void SlonkScatter::LandmarksNear(const FPlanetTerrainParams& P, const FScatterSettings& S, ELandmark Kind, const FVector& FromDir, double MaxCm, TArray<FLandmarkStand>& Out)
{
	Out.Reset();
	const FVector From = FromDir.GetSafeNormal();
	int32 Face = 0;
	double U = 0.0, V = 0.0;
	if (!S.Landmarks.Any() || MaxCm <= 0.0 || !SlonkTerrain::FaceUV(From, Face, U, V)) { return; }
	const double R = P.RadiusCm;
	const int32 L = LandmarkLevel(FaceEdgeCm(R));
	const int64 Cells = (int64)1 << L;
	const double Delta = 2.0 / (double)Cells;
	// The cells within reach: one is the face's edge over their number across at the face's middle, and down to seven
	// tenths of that towards its edges.
	const int64 Reach = FMath::Min((int64)FMath::CeilToDouble(MaxCm / (FaceEdgeCm(R) / (double)Cells) * 1.5) + 1, Cells);
	const int64 CX = (int64)FMath::FloorToDouble((U + 1.0) / Delta), CY = (int64)FMath::FloorToDouble((V + 1.0) / Delta);
	TArray<FLandmarkStand> Found;
	for (int32 K = 0; K < SlonkLandmarks::NumKinds; ++K)
	{
		if ((Kind != ELandmark::Count && Kind != (ELandmark)K) || !S.Landmarks.Kinds[K].bLive) { continue; }
		for (int64 GY = FMath::Max(CY - Reach, (int64)0); GY <= FMath::Min(CY + Reach, Cells - 1); ++GY)
		for (int64 GX = FMath::Max(CX - Reach, (int64)0); GX <= FMath::Min(CX + Reach, Cells - 1); ++GX)
		{
			LandmarkSite(P, S, (ELandmark)K, Face, L, GX, GY, Found);
		}
	}
	const double MaxCos = FMath::Cos(FMath::Min(MaxCm / R, UE_DOUBLE_PI));
	for (const FLandmarkStand& St : Found) { if (FVector::DotProduct(St.Up, From) >= MaxCos) { Out.Add(St); } }
	Out.Sort([&From](const FLandmarkStand& A, const FLandmarkStand& B) { return FVector::DotProduct(A.Up, From) > FVector::DotProduct(B.Up, From); });
}

void SlonkScatter::TreeCover(const FPlanetTerrainParams& P, const FChunkGrid& G, TArray<float>& Out)
{
	const int32 GW = G.Steps + 1;
	Out.Init(0.f, GW * GW);
	if (P.bAirless || !G.Samples || !G.Dirs || !G.Pos || G.Steps <= 0) { return; }
	const FChunkFrame F(G);
	// The forest's alone: the settlements' clearings are the material's, cut per pixel from the same settlement points
	// at any height (SlonkCities::ClearingAt, which thins the trees, mirrors them). Cut here, a town could be looked for
	// only on the chunks a few of its size across (a coarse chunk's search would span a continent), and its clearing
	// came and went with height.
	for (int32 j = 0; j <= G.Steps; ++j)
	{
		for (int32 i = 0; i <= G.Steps; ++i)
		{
			const double U = F.U0 + F.Wd * (double)i / G.Steps, V = F.V0 + F.Wd * (double)j / G.Steps;
			FSpot Sp;
			if (!F.SpotAt(U, V, Sp) || Sp.River > 0.08 || Sp.Cliff > 0.3) { continue; }
			Out[j * GW + i] = (float)TreeDensity(Sp, ClimateAt(P, Sp), ForestAt(P, Sp));
		}
	}
}

void SlonkScatter::ScatterChunk(const FPlanetTerrainParams& P, const FScatterSettings& S, const FChunkGrid& G, FChunkScatter& Out, FChunkKeepOut* Shared)
{
	const double R = P.RadiusCm;
	const double FaceArcCm = FaceEdgeCm(R);
	if (!S.bEnabled || S.Density <= 0.f || G.EdgeCm > MaxChunkEdgeCm(S, FaceArcCm) || !G.Samples || !G.Dirs || !G.Pos || G.Steps <= 0) { return; }
	const FChunkFrame F(G);
	const bool bLiving = S.Life > 0.f && !P.bAirless;
	// The settlements, the build's own if it shares one with the motes.
	FChunkKeepOut Own;
	const FKeepOut NoTowns{};
	const FKeepOut& KeepOut = S.bCities ? (Shared ? Shared : &Own)->Get(P, S.People, G) : NoTowns;
	const FCivilization ClearCiv = S.People.bInhabited ? S.People : SlonkCivs::Colonists();

	// A strange world's giants standing on this chunk and just beyond it, as far as anything placed here might stand in
	// one's foot: on the chunks the trees' own levels ride and finer (a far level's trees are specks, and its cells
	// thousands).
	TArray<TPair<FVector, double>> Feet;
	const bool bGiants = bLiving && S.Flora.HasGiants();
	if (bGiants && G.EdgeCm <= 62000.0)
	{
		const int32 L = GiantLevel(FaceArcCm);
		const int64 Cells = (int64)1 << L;
		const double Delta = 2.0 / (double)Cells;
		const int64 GX0 = FMath::Max((int64)FMath::FloorToDouble((F.U0 + 1.0) / Delta) - 1, (int64)0), GX1 = FMath::Min((int64)FMath::CeilToDouble((F.U1 + 1.0) / Delta), Cells - 1);
		const int64 GY0 = FMath::Max((int64)FMath::FloorToDouble((F.V0 + 1.0) / Delta) - 1, (int64)0), GY1 = FMath::Min((int64)FMath::CeilToDouble((F.V1 + 1.0) / Delta), Cells - 1);
		for (int64 GY = GY0; GY <= GY1; ++GY)
		for (int64 GX = GX0; GX <= GX1; ++GX)
		{
			FDraw D(CellHash(G.Face, L, GX, GY, GiantSalt));
			FGiantStand St;
			if (GiantInCell(P, S, G.Face, L, GX, GY, St, D)) { Feet.Add({ St.Up * R, FMath::Square(St.FootCm + 150.0) }); }
		}
	}
	// A strange world's landmarks standing on this chunk and just beyond it: placed below, and nothing else stands in
	// one's foot (kept out with the giants', on the chunks that carry what would).
	TArray<FLandmarkStand> Marks;
	if (S.Landmarks.Any() && G.EdgeCm <= LandmarkMaxEdgeCm * 1.0001)
	{
		const int32 L = LandmarkLevel(FaceArcCm);
		const int64 Cells = (int64)1 << L;
		const double Delta = 2.0 / (double)Cells;
		const int64 GX0 = FMath::Max((int64)FMath::FloorToDouble((F.U0 + 1.0) / Delta) - 1, (int64)0), GX1 = FMath::Min((int64)FMath::CeilToDouble((F.U1 + 1.0) / Delta), Cells - 1);
		const int64 GY0 = FMath::Max((int64)FMath::FloorToDouble((F.V0 + 1.0) / Delta) - 1, (int64)0), GY1 = FMath::Min((int64)FMath::CeilToDouble((F.V1 + 1.0) / Delta), Cells - 1);
		for (int32 K = 0; K < SlonkLandmarks::NumKinds; ++K)
		{
			if (!S.Landmarks.Kinds[K].bLive) { continue; }
			for (int64 GY = GY0; GY <= GY1; ++GY)
			for (int64 GX = GX0; GX <= GX1; ++GX) { LandmarkSite(P, S, (ELandmark)K, G.Face, L, GX, GY, Marks); }
		}
		// At every level that carries them: a far level's trees stand out of a landmark's foot as the near one's do.
		// (One that hangs in the air has none.)
		for (const FLandmarkStand& St : Marks) { if (St.FootCm > 0.0) { Feet.Add({ St.Up * R, FMath::Square(St.FootCm + 150.0) }); } }
	}
	auto InGiantFoot = [&Feet, R](const FVector& Up)
	{
		for (const TPair<FVector, double>& Ft : Feet) { if (FVector::DistSquared(Up * R, Ft.Key) < Ft.Value) { return true; } }
		return false;
	};

	int32 Rocks = 0, Plants = 0;
	for (const FClass& Cl : Classes)
	{
		// On the class's own levels all of it; on its far levels the share its keep draw allows.
		const int32 FarLevels = FarLevelsUsed(Cl, S, FaceArcCm);
		const int32 Above = LevelsAbove(G.EdgeCm, Cl.MaxEdgeCm);
		if (Above > FarLevels) { continue; }
		const double KeepHere = FMath::Pow(Cl.Keep, (double)Above);
		const bool bRock = Cl.Kind == EClass::Boulder || Cl.Kind == EClass::Stone || Cl.Kind == EClass::Pebble;
		if (!bRock && !bLiving) { continue; }
		// The class's lattice level: cells of 2 / 2^L in face coordinates, nested in the chunks of the quadtree.
		const int32 L = FMath::Clamp(FMath::RoundToInt32(FMath::Log2(FaceArcCm / Cl.SpacingCm)), 0, 40);
		const double Delta = 2.0 / (double)((int64)1 << L);
		const int64 GX0 = (int64)FMath::FloorToDouble((F.U0 + 1.0) / Delta), GX1 = (int64)FMath::CeilToDouble((F.U1 + 1.0) / Delta) - 1;
		const int64 GY0 = (int64)FMath::FloorToDouble((F.V0 + 1.0) / Delta), GY1 = (int64)FMath::CeilToDouble((F.V1 + 1.0) / Delta) - 1;
		// Rocks and plants are capped apart; a class stops where its count reaches the cap.
		const int32& Count = bRock ? Rocks : Plants;
		const int32 Cap = bRock ? MaxPerChunk : MaxPlantsPerChunk;
		for (int64 GY = GY0; GY <= GY1 && Count < Cap; ++GY)
		for (int64 GX = GX0; GX <= GX1 && Count < Cap; ++GX)
		{
			// Kept on this level? A draw of its own, so the candidate's place and its other draws are the same on every level.
			const double KeepDraw = FDraw(CellHash(G.Face, L, GX, GY, Cl.Salt ^ KeepSalt)).Next();
			if (KeepDraw >= KeepHere) { continue; }
			FDraw D(CellHash(G.Face, L, GX, GY, Cl.Salt));
			// The candidate: anywhere in its cell. Half-open windows put it in exactly one chunk of each level.
			const double U = -1.0 + ((double)GX + D.Next()) * Delta, V = -1.0 + ((double)GY + D.Next()) * Delta;
			if (!F.Inside(U, V)) { continue; }
			FSpot Sp;
			if (!F.SpotAt(U, V, Sp) || Sp.River > (bRock ? 0.15 : 0.08)) { continue; }
			if (Sp.Cliff > (bRock ? 0.6 : 0.3)) { continue; }
			const double Air = P.bAirless ? 1.0 : 0.0;
			const double Dry = 1.0 - Sp.Wet;
			// For a plant: the climate, which sets how much grows and what (the treeline, the conifers).
			const FClimate Clim = bRock ? FClimate() : ClimateAt(P, Sp);
			double Density = 0.0;
			if (bRock)
			{
				// Snow and ice bury all but the biggest.
				const double Cover = 1.0 - 0.9 * SmoothStep(0.3, 0.8, Sp.Ice);
				if (Cl.Kind != EClass::Pebble)
				{
					// The patches where the refinement raises its own boulders, so the meshes lie among them.
					const FVector Q = Sp.Up * (R / SlonkTerrain::RefineStartCm) + P.Seed * 7.3;
					const double Field = SmoothStep(-0.05, 0.35, SlonkTerrain::Fbm(Q * 2.0 + FVector(1.0, 9.0, 4.0), 2));
					Density = Cl.Kind == EClass::Boulder
						? (0.55 * Field * Sp.Talus + 0.12 * Sp.Rocky + 0.3 * Sp.WallFoot + 0.08 * Sp.Gorge + 0.35 * Sp.Crater * (1.0 - Sp.Cliff) + 0.006 + 0.03 * Air * Sp.Flat) * Cover
						: (0.45 * Sp.Talus * (0.5 + Field) + 0.3 * Sp.Rocky + 0.4 * Sp.WallFoot + 0.5 * Sp.Gorge * Sp.Wet + 0.12 * Sp.Flat * Dry * Dry + 0.25 * Air + 0.3 * Sp.Crater) * Cover;
				}
				else
				{
					Density = (0.5 * Sp.Flat * Dry * Dry + 0.45 * Sp.Talus + 0.3 * Sp.Rocky + 0.6 * Sp.Gorge + 0.45 * Air + 0.25 * Sp.WallFoot) * (1.0 - Sp.Ice);
				}
			}
			else
			{
				const double Forest = ForestAt(P, Sp);
				const double SnowLying = SmoothStep(0.2, 0.6, Sp.Ice);
				if (Cl.Kind == EClass::Tree)
				{
					Density = TreeDensity(Sp, Clim, Forest);
				}
				else
				{
					Density = SmoothStep(0.15, 0.45, Sp.Wet) * (1.0 - SmoothStep(0.3, 0.7, Sp.Slope)) * (1.0 - Sp.Rocky) * (1.0 - SnowLying)
						* (1.0 - SmoothStep(0.75, 1.0, Clim.Alpine)) * (1.0 - 0.5 * Forest * Sp.Wet) * 0.9 * Sp.Grows();
					// The grass holds on into more heat than the trees: it thins out past seven tenths of the liquid range.
					if (Clim.bClimate) { Density *= 1.0 - SmoothStep(0.7, 0.95, Clim.Heat); }
				}
				Density *= S.Life;
			}
			Density = FMath::Min(Density * S.Density, 0.9);
			const double Draw = D.Next();
			if (Draw >= Density) { continue; }
			// In a settlement's clearing a tree thins out as the material clears the canopy's colour there, so the two part
			// at the same edge (the same draw, so a tree outside every clearing stands where it did).
			if (Cl.Kind == EClass::Tree && S.bCities && Draw >= Density * (1.0 - SlonkCities::ClearingAt(P, ClearCiv, Sp.Up))) { continue; }
			if (KeepOut.Towns.Num() > 0 && (KeepOut.Contains(Sp.Up, R, !bRock) || KeepOut.OnWay(Sp.Up))) { continue; }
			// Nothing grows or lies in a giant's foot.
			if (Feet.Num() > 0 && InGiantFoot(Sp.Up)) { continue; }
			// No loose rock lies on a strange world's own geology (a boulder stood out of a spire's side as readily as
			// off its floor), but for the pebbles among its spires.
			if (bRock && Sp.Geo > 0.25 && (P.Geology != EGeology::Spires || Cl.Kind != EClass::Pebble)) { continue; }
			const double Seed = D.Next();
			const double Snow = FMath::Clamp(Sp.Ice * 1.2, 0.0, 1.0) * (1.0 - FMath::Clamp(Sp.Slope * 2.0, 0.0, 1.0));

			if (bRock)
			{
				// What it is.
				const bool bBoulder = Cl.Kind == EClass::Boulder, bPebble = Cl.Kind == EClass::Pebble;
				const double StrataWall = Sp.WallFoot * P.Strata;
				double W[SlonkGroundRocks::NumKinds];
				if (bBoulder)
				{
					W[0] = 0.55 + 0.3 * Air + 0.2 * Sp.Crater;
					W[1] = 0.18 + 0.45 * StrataWall + 0.2 * Sp.Table;
					W[2] = 0.08 + 0.3 * Sp.Table + 0.25 * StrataWall;
					W[3] = 0.15 + 0.5 * Sp.Gorge * Sp.Wet + 0.25 * Sp.Flat * Sp.Wet * (1.0 - Air);
				}
				else if (!bPebble)
				{
					W[0] = 0.45 + 0.3 * Air;
					W[1] = 0.1 + 0.2 * StrataWall;
					W[2] = 0.12 + 0.2 * Sp.Table + 0.15 * StrataWall;
					W[3] = 0.3 + 0.8 * Sp.Gorge * Sp.Wet + 0.3 * Sp.Flat * Sp.Wet * (1.0 - Air);
				}
				else
				{
					W[0] = 0.45 + 0.3 * Air;
					W[1] = 0.0;
					W[2] = 0.1;
					W[3] = 0.55 + Sp.Gorge * Sp.Wet + 0.3 * Sp.Wet;
				}
				const int32 Kind = PickKind(W, D.Next());
				const FProportion& Pr = Proportions[Kind];
				// Small ones are many: the size leans to the low end of the class. Mountains break off bigger blocks.
				double Across = Cl.MinCm * FMath::Pow(Cl.MaxCm / Cl.MinCm, FMath::Pow(D.Next(), 1.7));
				if (bBoulder) { Across *= 1.0 + 0.5 * Sp.Rocky + 0.3 * Air * Sp.Crater; }
				const double Sx = Across, Sy = Across * FMath::Lerp(Pr.YLo, Pr.YHi, D.Next()), Sz = Across * FMath::Lerp(Pr.ZLo, Pr.ZHi, D.Next());
				// Lying on the ground: mostly along the drawn face, a little toward the vertical, tipped and turned.
				const FQuat Rot = StandingFrame(Sp, 0.65, Pr.TiltDeg, D);
				const FVector Z = Rot.GetAxisZ();
				// Buried a fifth of its height on the flat, more on a slope, where the side that turned less than the
				// ground would otherwise stand clear of it.
				const double CosG = FMath::Clamp(FVector::DotProduct(Sp.Nt, Sp.Up), 0.2, 1.0);
				const double TanG = FMath::Sqrt(1.0 - CosG * CosG) / CosG;
				const double Bury = FMath::Min(Sz * 0.22 + Sx * 0.5 * FMath::Tan(FMath::Atan(TanG) * 0.35), Sz * 0.6);
				const FTransform Xf(Rot, Sp.Pt + Z * (Sz * 0.5 - Bury), FVector(Sx, Sy, Sz) / SlonkGroundRocks::UnitCm);
				// Its colour: the shader's rock colour, warmed in a canyon; a rock at a wall's foot is a piece of a bed
				// above it, and the rock material colours the bed by its index as the terrain material does.
				FLinearColor Tint = S.Palette.Mountain * 0.85f;
				const float Warm = (float)(FMath::Clamp(Sp.Canyon * 1.5, 0.0, 1.0) * 0.7);
				Tint = FMath::Lerp(Tint, Tint * FLinearColor(1.25f, 0.9f, 0.72f), Warm);
				Tint *= (float)((0.82 + 0.3 * D.Next()) * (1.0 - 0.3 * Sp.Wet * Sp.Gorge) * 0.8);
				const bool bBed = P.Strata > 0.f && Sp.Wall > 0.02 && Sp.Stratum > 0.0;
				const double Layer = bBed ? (double)FMath::Min(FMath::FloorToInt32(Sp.Stratum * 8.0) + FMath::FloorToInt32(D.Next() * 2.99), 7) : -1.0;
				const double OnWall = bBed ? FMath::Clamp(Sp.Wall * 1.5, 0.0, 1.0) : 0.0;
				const float C[NumCustomFloats] = { Tint.R, Tint.G, Tint.B, (float)Snow, (float)Seed, (float)(Sx / 100.0), (float)(Sy / 100.0), (float)(Sz / 100.0), (float)Layer, (float)OnWall };
				AddPiece(Out, Kind, Xf, C, Across >= SolidMinCm && G.EdgeCm <= SolidMaxEdgeCm);
				++Rocks;
			}
			else
			{
				const double Dryness = DrynessAt(Sp);
				const bool bTree = Cl.Kind == EClass::Tree;
				// The cold, the heights and the poor ground grow conifers; the warm, wet lowlands broadleaves.
				const bool bConifer = bTree && D.Next() < SmoothStep(0.25, 0.6, Clim.Cold) + 0.3 * Sp.Rocky;
				// The role, and which of the role's species stands here: the world's one, or by stands where it has two
				// (a number of the plant's own mixes their edges, the same on every level of the quadtree).
				const int32 Role = bTree ? (bConifer ? SlonkFlora::Slot::Cold : SlonkFlora::Slot::Warm) : SlonkFlora::Slot::Cover;
				int32 Slot = Role;
				if (S.Flora.Species[SlonkFlora::SecondOfRole(Role)].bLive || !S.Flora.Species[Role].bLive)
				{
					Slot = SpeciesAt(P, S.Flora, Role, Sp.Up, FDraw(CellHash(G.Face, L, GX, GY, Cl.Salt ^ SpeciesSalt)).Next());
				}
				const FFloraSpecies& Kind = S.Flora.Species[Slot];
				if (!Kind.bLive) { continue; }
				// A species' own height against its class's: the conifer stands a little taller than the broadleaf.
				double H = Cl.MinCm * FMath::Pow(Cl.MaxCm / Cl.MinCm, FMath::Pow(D.Next(), 1.3));
				H *= Kind.Height;
				// Near the treeline what grows is stunted.
				if (bTree) { H *= 1.0 - 0.45 * SmoothStep(0.45, 0.85, Clim.Alpine); }
				const double Wx = FMath::Lerp(0.85, 1.15, D.Next()), Wy = Wx * FMath::Lerp(0.9, 1.1, D.Next());
				// Upright: trees grow to the vertical whatever the slope; a tuft of grass follows the ground a little.
				const FQuat Rot = StandingFrame(Sp, bTree ? 0.05 : 0.35, bTree ? 3.0 : 8.0, D);
				const FVector Z = Rot.GetAxisZ();
				// The base sunk into the ground, deeper for a tree on a slope so its downhill side meets it.
				const double SlopeTan = FMath::Sqrt(FMath::Max(1.0 - FMath::Square(FVector::DotProduct(Sp.Nt, Sp.Up)), 0.0));
				const double Sink = bTree ? 30.0 + H * 0.02 * SlopeTan * 3.0 : 2.0;
				const FTransform Xf(Rot, Sp.Pt - Z * Sink, FVector(H * Wx, H * Wy, H) / SlonkPlants::UnitCm);
				const double Vary = 0.78 + 0.34 * D.Next();
				FLinearColor Tint = FoliageAt(S.Palette, Dryness, Vary);
				// The grass is the ground's own colour so the two meet; a species' own shade over that (needles darker and
				// bluer than leaves), and for a strange one a share of a colour of its own.
				if (Cl.Kind == EClass::Grass) { Tint = FMath::Lerp(S.Palette.Land2, S.Palette.Land, (float)FMath::Clamp(Sp.Wet * 1.2 - Sp.Rocky * 0.3, 0.0, 1.0)) * (float)(0.8 * Vary); }
				Tint *= Kind.Tint;
				if (Kind.OwnShare > 0.f) { Tint = FMath::Lerp(Tint, Kind.Own * (float)Vary, Kind.OwnShare); }
				const double FadeCm = FadeFarCm(G, Cl.MaxEdgeCm, FarLevels, Cl.Keep, KeepDraw, S.FadeEdges);
				const float C[NumCustomFloats] = { Tint.R, Tint.G, Tint.B, (float)Snow, (float)Seed, (float)(H * Wx / 100.0), (float)(H * Wy / 100.0), (float)(H / 100.0), (float)Dryness, (float)FadeCm };
				AddPiece(Out, FloraMesh(Slot), Xf, C, bTree && H >= SolidTreeCm && G.EdgeCm <= SolidMaxEdgeCm);
				++Plants;
			}
		}
	}

	// The giants: each where the field says one stands (GiantInCell), on the drawn ground there; none over the water as
	// the chunk draws it, in a town or across a way.
	const int32 GiantFar = GiantFarLevelsUsed(S);
	const int32 GiantAbove = LevelsAbove(G.EdgeCm, GiantMaxEdgeCm);
	if (bGiants && GiantAbove <= GiantFar)
	{
		const double KeepHere = FMath::Pow(GiantKeep, (double)GiantAbove);
		const int32 L = GiantLevel(FaceArcCm);
		const double Delta = 2.0 / (double)((int64)1 << L);
		const int64 GX0 = (int64)FMath::FloorToDouble((F.U0 + 1.0) / Delta), GX1 = (int64)FMath::CeilToDouble((F.U1 + 1.0) / Delta) - 1;
		const int64 GY0 = (int64)FMath::FloorToDouble((F.V0 + 1.0) / Delta), GY1 = (int64)FMath::CeilToDouble((F.V1 + 1.0) / Delta) - 1;
		for (int64 GY = GY0; GY <= GY1 && Plants < MaxPlantsPerChunk; ++GY)
		for (int64 GX = GX0; GX <= GX1 && Plants < MaxPlantsPerChunk; ++GX)
		{
			const double KeepDraw = FDraw(CellHash(G.Face, L, GX, GY, GiantSalt ^ KeepSalt)).Next();
			if (KeepDraw >= KeepHere) { continue; }
			FDraw D(CellHash(G.Face, L, GX, GY, GiantSalt));
			FGiantStand St;
			if (!GiantInCell(P, S, G.Face, L, GX, GY, St, D) || !F.Inside(St.U, St.V)) { continue; }
			// No giant in a landmark's foot, nor one's stalk through it.
			bool bInMark = false;
			for (const FLandmarkStand& Mark : Marks) { bInMark |= Mark.FootCm > 0.0 && FVector::DistSquared(St.Up * R, Mark.Up * R) < FMath::Square(Mark.FootCm + St.FootCm); }
			if (bInMark) { continue; }
			FSpot Sp;
			if (!F.SpotAt(St.U, St.V, Sp)) { continue; }
			if (KeepOut.Towns.Num() > 0 && (KeepOut.Contains(Sp.Up, R, false) || KeepOut.WayWithin(Sp.Up, 200.0 + St.FootCm))) { continue; }
			const FFloraSpecies& Kind = S.Flora.Species[St.Slot];
			const double Seed = D.Next();
			const double H = St.HeightCm;
			const double Wx = FMath::Lerp(0.85, 1.15, D.Next()), Wy = Wx * FMath::Lerp(0.9, 1.1, D.Next());
			// More nearly upright than a tree, and sunk by a hundredth of its height besides: its foot is metres across.
			const FQuat Rot = StandingFrame(Sp, 0.02, 2.0, D);
			const double SlopeTan = FMath::Sqrt(FMath::Max(1.0 - FMath::Square(FVector::DotProduct(Sp.Nt, Sp.Up)), 0.0));
			const double Sink = 120.0 + H * 0.012 + H * 0.02 * SlopeTan * 3.0;
			const FTransform Xf(Rot, Sp.Pt - Rot.GetAxisZ() * Sink, FVector(H * Wx, H * Wy, H) / SlonkPlants::UnitCm);
			const double Dryness = DrynessAt(Sp);
			const double Vary = 0.78 + 0.34 * D.Next();
			FLinearColor Tint = FoliageAt(S.Palette, Dryness, Vary) * Kind.Tint;
			if (Kind.OwnShare > 0.f) { Tint = FMath::Lerp(Tint, Kind.Own * (float)Vary, Kind.OwnShare); }
			const double Snow = FMath::Clamp(Sp.Ice * 1.2, 0.0, 1.0) * (1.0 - FMath::Clamp(Sp.Slope * 2.0, 0.0, 1.0));
			const double FadeCm = FadeFarCm(G, GiantMaxEdgeCm, GiantFar, GiantKeep, KeepDraw, S.FadeEdges);
			const float C[NumCustomFloats] = { Tint.R, Tint.G, Tint.B, (float)Snow, (float)Seed, (float)(H * Wx / 100.0), (float)(H * Wy / 100.0), (float)(H / 100.0), (float)Dryness, (float)FadeCm };
			AddPiece(Out, FloraMesh(St.Slot), Xf, C, H >= SolidTreeCm && G.EdgeCm <= SolidMaxEdgeCm);
			++Plants;
		}
	}

	// The landmarks: each where the field says one stands (LandmarkSite, which keeps them out of the towns and off
	// the ways), on the drawn ground there or, for one that stands in the sea, at the sea's level.
	for (const FLandmarkStand& St : Marks)
	{
		if (!F.Inside(St.U, St.V)) { continue; }
		const SlonkLandmarks::FKindSpec& Spec = SlonkLandmarks::SpecOf(St.Kind);
		FVector Pt = St.Up * (R + St.BaseCm) - G.Origin;
		double Snow = 0.0, SlopeTan = 0.0;
		if (!Spec.bInSea)
		{
			// On the chunk's drawn ground; where its grid cannot read the place (a coarse chunk's cells are hundreds of
			// metres, and one with water at a corner reads as none), on the field's, which put it on dry land: every
			// level carries it, and it does not come and go as the chunks split.
			FSpot Sp;
			if (F.SpotAt(St.U, St.V, Sp))
			{
				Pt = Sp.Pt;
				Snow = FMath::Clamp(Sp.Ice * 1.2, 0.0, 1.0) * (1.0 - FMath::Clamp(Sp.Slope * 2.0, 0.0, 1.0));
				SlopeTan = FMath::Sqrt(FMath::Max(1.0 - FMath::Square(FVector::DotProduct(Sp.Nt, Sp.Up)), 0.0));
			}
			else { SlopeTan = 0.2; }
		}
		// One that hangs in the air: its lift over the field's ground at its place, not the chunk's drawn ground, so it
		// hangs at one height whatever level draws it.
		if (Spec.LiftHi > 0.0)
		{
			Pt = St.Up * (R + St.BaseCm + St.LiftCm) - G.Origin;
			SlopeTan = 0.0;
		}
		// Its own draws, apart from the ones that put it there: how it leans, its proportions, its shade.
		FDraw D(CellHash(St.Face, LandmarkLevel(FaceArcCm), St.GX, St.GY, LandmarkSaltOf(S.Landmarks, St.Kind, 100 + St.Member)));
		FVector Ta, Tb;
		LandmarkTangents(St.Up, Ta, Tb);
		const double TiltAng = FMath::DegreesToRadians(Spec.TiltDeg * D.Next()), TiltDir = UE_DOUBLE_TWO_PI * D.Next();
		const FVector Z = (St.Up * FMath::Cos(TiltAng) + (Ta * FMath::Cos(TiltDir) + Tb * FMath::Sin(TiltDir)) * FMath::Sin(TiltAng)).GetSafeNormal();
		const FQuat Rot = FRotationMatrix::MakeFromZX(Z, Ta * FMath::Cos(St.Yaw) + Tb * FMath::Sin(St.Yaw)).ToQuat();
		// A metre of the mesh's own to each metre of its measure; broader, thicker or squatter by its own draws, and sunk
		// a little, the more the steeper its ground (its mesh runs on under its origin: SlonkLandmarks::BelowCm).
		const double Size = St.SizeCm / SlonkLandmarks::UnitCm;
		const double A = D.Next(), B = D.Next();
		double Wx = 1.0, Wy = 1.0, Wz = 1.0, Sink = 0.0;
		FLinearColor Tint = S.Landmarks.Kinds[(int32)St.Kind].Color;
		switch (St.Kind)
		{
		case ELandmark::Arch:
			Wy = FMath::Lerp(0.8, 1.35, A); Wz = FMath::Lerp(0.75, 1.15, B);
			Sink = (0.03 + FMath::Min(SlopeTan * 0.4, 0.08)) * St.SizeCm;
			Tint = S.Palette.Mountain;
			break;
		case ELandmark::Monolith:
			Wx = Wy = FMath::Lerp(0.85, 1.25, A);
			Sink = (0.03 + FMath::Min(SlopeTan * 0.2, 0.04)) * St.SizeCm;
			break;
		case ELandmark::Ring:
			Wy = FMath::Lerp(0.9, 1.5, A);
			Sink = 0.05 * B * St.SizeCm;
			break;
		case ELandmark::ReefTower:
			Wx = Wy = FMath::Lerp(0.85, 1.2, A);
			break;
		case ELandmark::Mound:
			Wx = Wy = FMath::Lerp(0.9, 1.3, A); Wz = FMath::Lerp(0.85, 1.1, B);
			Sink = (0.02 + FMath::Min(SlopeTan * 0.5, 0.05)) * St.SizeCm;
			// The earth it was raised from: the dry ground's colour, a little of the rock's.
			Tint = FMath::Lerp(S.Palette.Land2, S.Palette.Mountain, 0.35f);
			break;
		case ELandmark::Shell:
			// Thicker or thinner through; three tenths of it buried as its mesh is made, and up to a sixth more.
			Wy = FMath::Lerp(0.85, 1.3, A);
			Sink = (0.02 + 0.08 * B + FMath::Min(SlopeTan * 0.3, 0.06)) * St.SizeCm;
			break;
		default:
			// A floating rock: broader one way than the other, deeper or shallower, of the world's own rock.
			Wx = FMath::Lerp(0.85, 1.2, A); Wy = FMath::Lerp(0.85, 1.2, B); Wz = FMath::Lerp(0.8, 1.2, D.Next());
			Tint = S.Palette.Mountain;
			break;
		}
		const FTransform Xf(Rot, Pt - Rot.GetAxisZ() * Sink, FVector(Size * Wx, Size * Wy, Size * Wz));
		Tint *= (float)(0.85 + 0.3 * D.Next());
		// As a rock's (M_GroundRock): its tint, its snow, its seed, its size in metres along its axes, and no canyon bed.
		const float C[NumCustomFloats] = { Tint.R, Tint.G, Tint.B, (float)Snow, (float)D.Next(), (float)(Size * Wx), (float)(Size * Wy), (float)(Size * Wz), -1.f, 0.f };
		AddPiece(Out, LandmarkMesh(St.Kind), Xf, C, G.EdgeCm <= SolidMaxEdgeCm);
	}

	// Shrubs: on the scrub dots the terrain material paints, the same cells, hashed the same way, so the bush close to
	// stands where the dark dot was from the air. The material adds each dot through the cell its pixel falls in, so
	// a dot is drawn about its centre only if the ground there is in the dot's own cell; that is where a bush goes.
	const int32 ShrubFar = FMath::Min(ShrubFarLevels, S.MaxFarLevels);
	const int32 ShrubAbove = LevelsAbove(G.EdgeCm, ShrubMaxEdgeCm);
	if (bLiving && ShrubAbove <= ShrubFar)
	{
		const double ShrubKeepHere = FMath::Pow(ShrubKeep, (double)ShrubAbove);
		const FVector OriginNoise = ToNoiseSpace(G.Origin);
		auto CellOf = [](const FVector& Pn) { return FIntVector((int32)FMath::FloorToDouble(Pn.X / ScrubCellCm), (int32)FMath::FloorToDouble(Pn.Y / ScrubCellCm), (int32)FMath::FloorToDouble(Pn.Z / ScrubCellCm)); };
		// Every cell the drawn ground passes through, from each grid cell's box (its two triangles lie inside it):
		// on a coarse grid the vertices are metres further apart than the dots' cells, and most of those cells hold
		// none. A cell with water at a corner carries nothing (SpotAt), so it adds none.
		TSet<FIntVector> Cells;
		for (int32 j = 0; j < G.Steps; ++j)
		{
			for (int32 i = 0; i < G.Steps; ++i)
			{
				const int32 Corner[4] = { F.Index(i, j), F.Index(i + 1, j), F.Index(i, j + 1), F.Index(i + 1, j + 1) };
				FBox Box(ForceInit);
				bool bWet = false;
				for (const int32 k : Corner) { bWet |= G.Samples[k].bWater; Box += OriginNoise + G.Pos[k]; }
				if (bWet) { continue; }
				const FIntVector Lo = CellOf(Box.Min), Hi = CellOf(Box.Max);
				for (int32 z = Lo.Z; z <= Hi.Z; ++z)
				for (int32 y = Lo.Y; y <= Hi.Y; ++y)
				for (int32 x = Lo.X; x <= Hi.X; ++x) { Cells.Add(FIntVector(x, y, z)); }
			}
		}
		for (const FIntVector& Cell : Cells)
		{
			if (Plants >= MaxPlantsPerChunk) { break; }
			FVector CentreN; double DotR;
			if (!ScrubDotAt((FVector(Cell) + FVector(0.5)) * ScrubCellCm, CentreN, DotR)) { continue; }
			const FVector Planet = G.Origin + (CentreN - OriginNoise);
			int32 Face; double U, V;
			if (!SlonkTerrain::FaceUV(Planet.GetSafeNormal(), Face, U, V) || Face != G.Face || !F.Inside(U, V)) { continue; }
			FSpot Sp;
			if (!F.SpotAt(U, V, Sp) || Sp.River > 0.08 || Sp.Cliff > 0.3 || Sp.Ice > 0.3 || Sp.Geo > 0.25) { continue; }
			if (CellOf(OriginNoise + Sp.Pt) != Cell) { continue; }
			const double Zone = F.ScrubZoneAt(P, Sp);
			// The bush's own draws from the dot's place on the periodic lattice, so it is the same bush whichever side
			// of a wrap the chunk's origin lies.
			const FIntVector Key = ScrubCellKey(Cell);
			const uint64 KeyHash = ((uint64)(uint32)Key.X << 42) ^ ((uint64)(uint32)Key.Y << 21) ^ (uint64)(uint32)Key.Z;
			const double KeepDraw = FDraw(Mix64(KeyHash ^ KeepSalt)).Next();
			if (KeepDraw >= ShrubKeepHere) { continue; }
			FDraw D(Mix64(KeyHash ^ 0x6a09e667f3bcc909ULL));
			if (D.Next() >= S.Life * S.Density * SmoothStep(0.25, 0.55, Zone)) { continue; }
			if (KeepOut.Towns.Num() > 0 && (KeepOut.Contains(Sp.Up, R, true) || KeepOut.OnWay(Sp.Up))) { continue; }
			if (Feet.Num() > 0 && InGiantFoot(Sp.Up)) { continue; }
			// Which of the world's bushes: its one, or by stands where it has two.
			int32 Slot = SlonkFlora::Slot::Bush;
			if (S.Flora.Species[SlonkFlora::Slot::Bush2].bLive || !S.Flora.Species[SlonkFlora::Slot::Bush].bLive)
			{
				Slot = SpeciesAt(P, S.Flora, SlonkFlora::Slot::Bush, Sp.Up, FDraw(Mix64(KeyHash ^ SpeciesSalt)).Next());
			}
			const FFloraSpecies& Kind = S.Flora.Species[Slot];
			if (!Kind.bLive) { continue; }
			// As wide as the painted dot, a little over half as tall by its species' own height; the Earth's bush's crown
			// is 110 cm across at unit scale, and every species' mesh says its own.
			const double Wide = DotR * 2.0 * FMath::Lerp(0.95, 1.15, D.Next());
			const double H = Wide * FMath::Lerp(0.55, 0.85, D.Next()) * Kind.Height;
			const FQuat Rot = StandingFrame(Sp, 0.25, 6.0, D);
			// The Earth's bush by the very division it always was (a divisor the compiler knows is not one it is handed).
			const FVector Scale = Kind.Form.Plan == EFloraPlan::Bush ? FVector(Wide / 110.0, Wide / 110.0 * FMath::Lerp(0.85, 1.1, D.Next()), H / SlonkPlants::UnitCm)
				: FVector(Wide / Kind.UnitWideCm, Wide / Kind.UnitWideCm * FMath::Lerp(0.85, 1.1, D.Next()), H / SlonkPlants::UnitCm);
			const FTransform Xf(Rot, Sp.Pt - Rot.GetAxisZ() * (H * 0.04), Scale);
			const double Dryness = DrynessAt(Sp);
			// The dot's own darkening of the ground, which the bush now stands over.
			const double Vary = 0.8 + 0.3 * D.Next();
			FLinearColor Tint = FoliageAt(S.Palette, Dryness, Vary) * FLinearColor(0.55f, 0.62f, 0.45f) * 1.5f;
			Tint *= Kind.Tint;
			if (Kind.OwnShare > 0.f) { Tint = FMath::Lerp(Tint, Kind.Own * (float)Vary, Kind.OwnShare); }
			const double Snow = FMath::Clamp(Sp.Ice * 1.2, 0.0, 1.0);
			const double FadeCm = FadeFarCm(G, ShrubMaxEdgeCm, ShrubFar, ShrubKeep, KeepDraw, S.FadeEdges);
			const float C[NumCustomFloats] = { Tint.R, Tint.G, Tint.B, (float)Snow, (float)D.Next(), (float)(Wide / 100.0), (float)(Wide / 100.0), (float)(H / 100.0), (float)Dryness, (float)FadeCm };
			AddPiece(Out, FloraMesh(Slot), Xf, C, false);
			++Plants;
		}
	}
}

int32 SlonkScatter::MoteFarLevelsUsed(const FScatterSettings& S)
{
	return FMath::Clamp(FMath::Min(MoteFarLevels, S.MaxFarLevels), 0, MoteFarLevels);
}

double SlonkScatter::MoteMaxChunkEdgeCm(const FScatterSettings& S, const FMoteSettings& M)
{
	if (!M.bEnabled || M.Life <= 0.f || M.Density <= 0.f) { return 0.0; }
	return MoteMaxEdgeCm * FMath::Pow(2.0, (double)MoteFarLevelsUsed(S));
}

double SlonkScatter::MoteDensityAt(const FPlanetTerrainParams& P, const FVector& Dir)
{
	if (P.bAirless) { return 0.0; }
	FPlanetTerrainSample T;
	const FSpot Sp = FieldSpot(P, Dir.GetSafeNormal(), T);
	return T.bWater ? 0.0 : MoteDensityOf(P, Sp);
}

bool SlonkScatter::FindMoteSpot(const FPlanetTerrainParams& P, const FVector& From, double MaxCm, FVector& OutDir, double& OutDensity)
{
	const FVector Up = From.GetSafeNormal();
	if (Up.IsNearlyZero() || P.bAirless) { return false; }
	const FVector Ref = FMath::Abs(Up.Z) < 0.95 ? FVector::ZAxisVector : FVector::XAxisVector;
	const FVector E = FVector::CrossProduct(Ref, Up).GetSafeNormal();
	const FVector N = FVector::CrossProduct(Up, E);
	// Points spread evenly over a disc round a centre (a sunflower's), the nearest a little ahead of an equal one further.
	auto Search = [&](const FVector& Centre, const FVector& Ea, const FVector& Na, double RadiusCm, int32 Count, double& InOutBest, FVector& InOutDir)
	{
		bool bFound = false;
		for (int32 i = 0; i < Count; ++i)
		{
			const double Rr = RadiusCm * FMath::Sqrt((i + 0.5) / Count), A = i * UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0));
			const FVector Dir = (Centre + (Ea * FMath::Cos(A) + Na * FMath::Sin(A)) * (Rr / P.RadiusCm)).GetSafeNormal();
			const double D = MoteDensityAt(P, Dir) * (1.0 - 0.1 * Rr / FMath::Max(MaxCm, 1.0));
			if (D > InOutBest) { InOutBest = D; InOutDir = Dir; bFound = true; }
		}
		return bFound;
	};
	double Best = 0.0;
	FVector BestDir = Up;
	Search(Up, E, N, MaxCm, 3000, Best, BestDir);
	if (Best <= 0.0) { return false; }
	// Round the best, finer: a forest's edge is a few hundred metres wide.
	const FVector E2 = FVector::CrossProduct(FMath::Abs(BestDir.Z) < 0.95 ? FVector::ZAxisVector : FVector::XAxisVector, BestDir).GetSafeNormal();
	Search(BestDir, E2, FVector::CrossProduct(BestDir, E2), FMath::Min(MaxCm * 0.05, 1.5e5), 600, Best, BestDir);
	OutDir = BestDir;
	OutDensity = Best;
	return true;
}

void SlonkScatter::PlaceMotes(const FPlanetTerrainParams& P, const FScatterSettings& S, const FMoteSettings& M, const FChunkGrid& G, FChunkFauna& Out,
	FChunkKeepOut* Shared)
{
	const double R = P.RadiusCm;
	const double FaceArcCm = FaceEdgeCm(R);
	if (!M.bEnabled || M.Life <= 0.f || M.Density <= 0.f || P.bAirless || !G.Samples || !G.Dirs || !G.Pos || G.Steps <= 0) { return; }
	// On the motes' own levels all of them; on their far level the share their keep draw allows, as the plants'.
	const int32 FarLevels = MoteFarLevelsUsed(S);
	const int32 Above = LevelsAbove(G.EdgeCm, MoteMaxEdgeCm);
	if (Above > FarLevels) { return; }
	const double KeepHere = FMath::Pow(MoteKeep, (double)Above);
	const FChunkFrame F(G);
	// The lattice: cells of 2 / 2^L in face coordinates, nested in the chunks of the quadtree; half-open windows put each
	// cell's cloud in exactly one chunk of each level.
	const int32 L = FMath::Clamp(FMath::RoundToInt32(FMath::Log2(FaceArcCm / MoteSpacingCm)), 0, 40);
	const double Delta = 2.0 / (double)((int64)1 << L);
	const int64 GX0 = (int64)FMath::FloorToDouble((F.U0 + 1.0) / Delta), GX1 = (int64)FMath::CeilToDouble((F.U1 + 1.0) / Delta) - 1;
	const int64 GY0 = (int64)FMath::FloorToDouble((F.V0 + 1.0) / Delta), GY1 = (int64)FMath::CeilToDouble((F.V1 + 1.0) / Delta) - 1;
	// The settlements are looked for only once a cloud would stand here (most chunks carry none), unless the build's
	// scatter has looked for them already.
	FChunkKeepOut Own;
	FChunkKeepOut& KeepOutHere = Shared ? *Shared : Own;
	for (int64 GY = GY0; GY <= GY1 && Out.Num() < MaxMotesPerChunk; ++GY)
	for (int64 GX = GX0; GX <= GX1 && Out.Num() < MaxMotesPerChunk; ++GX)
	{
		const double KeepDraw = FDraw(CellHash(G.Face, L, GX, GY, MoteSalt ^ KeepSalt)).Next();
		if (KeepDraw >= KeepHere) { continue; }
		FDraw D(CellHash(G.Face, L, GX, GY, MoteSalt));
		const double U = -1.0 + ((double)GX + D.Next()) * Delta, V = -1.0 + ((double)GY + D.Next()) * Delta;
		if (!F.Inside(U, V)) { continue; }
		// None over water: SpotAt finds nothing in a cell with water at a corner.
		FSpot Sp;
		if (!F.SpotAt(U, V, Sp)) { continue; }
		const double Density = FMath::Min(MoteDensityOf(P, Sp) * M.Life * M.Density, 0.9);
		if (D.Next() >= Density) { continue; }
		if (S.bCities)
		{
			const FKeepOut& KeepOut = KeepOutHere.Get(P, S.People, G);
			if (KeepOut.Towns.Num() > 0 && (KeepOut.Contains(Sp.Up, R, true) || KeepOut.OnWay(Sp.Up))) { continue; }
		}
		const double Seed = D.Next();
		// A cloud 6 to 9 m across over its cell, standing on the drawn ground and level with it, turned. The turn is
		// reckoned about the radial up, the same on every level of the quadtree, so a chunk and its children turn a cloud
		// alike; the drawn triangle under it differs between them, and a turn reckoned about its normal could swing half
		// round from one level to the next. Two draws are passed over where a standing frame's tilt takes them, so the
		// cloud's other draws (its colour, glow and cycles) stay as they were.
		const double SpanM = FMath::Lerp(6.0, 9.0, D.Next());
		D.Next(); D.Next();
		const FVector YawRef = FMath::Abs(Sp.Up.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector;
		const FVector Ta = FVector::CrossProduct(YawRef, Sp.Up).GetSafeNormal();
		const FVector Tb = FVector::CrossProduct(Sp.Up, Ta);
		const double Yaw = UE_DOUBLE_TWO_PI * D.Next();
		const FQuat Rot = FRotationMatrix::MakeFromZX(Sp.Nt, Ta * FMath::Cos(Yaw) + Tb * FMath::Sin(Yaw)).ToQuat();
		const FTransform Xf(Rot, Sp.Pt, FVector(SpanM * 100.0 / SlonkFauna::UnitCm));
		const FLinearColor Day = M.Pollen * (float)(0.8 + 0.4 * D.Next());
		const double Glow = 0.75 + 0.5 * D.Next();
		const double Phase = D.Next();
		// Its motes wander on 3 to 7 cycles a minute, so the clock's hour holds a whole number of them.
		const double RateHz = (3.0 + FMath::FloorToDouble(D.Next() * 4.999)) / 60.0;
		// The wetter the ground, the more of its motes are fireflies at night.
		const double Fireflies = 0.35 + 0.55 * SmoothStep(0.3, 0.8, Sp.Wet);
		const double FadeCm = FadeFarCm(G, MoteMaxEdgeCm, FarLevels, MoteKeep, KeepDraw, S.FadeEdges);
		const float C[NumCustomFloats] = { Day.R, Day.G, Day.B, (float)Glow, (float)Seed, (float)SpanM, (float)Phase, (float)RateHz, (float)Fireflies, (float)FadeCm };
		Out.Motes.Add(Xf);
		Out.MoteCustom.Append(C, NumCustomFloats);
	}
}
