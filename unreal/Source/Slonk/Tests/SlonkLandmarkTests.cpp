// Slonk - automation tests for a strange world's landmarks (Landmarks.h): what a world rolls, the meshes they are
// drawn with (LandmarkMeshes.h), where the scatter stands them (TerrainScatter.h) and that every level of the quadtree
// agrees, and that nothing else stands in one's foot. Under "Slonk.Landmarks."; tools/run_tests.sh runs them headless.

#include "Misc/AutomationTest.h"
#include "../FloraProfile.h"
#include "../LandmarkMeshes.h"
#include "../Landmarks.h"
#include "../PlanetBody.h"
#include "../PlanetCities.h"
#include "../PlanetTerrain.h"
#include "../PlanetTerrainComponent.h"
#include "../TerrainScatter.h"
#include "../WorldGenome.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "MeshQueries.h"
#include "PhysicsEngine/BodySetup.h"

#if WITH_DEV_AUTOMATION_TESTS

#ifndef SLONK_TEST_FLAGS
#define SLONK_TEST_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#endif

namespace SlonkLandmarkTestHelpers
{
	const ELandmark MarkKinds[] = { ELandmark::Arch, ELandmark::Monolith, ELandmark::Ring, ELandmark::ReefTower, ELandmark::Mound };

	/** An Earth-like world, as APlanetBody::MakeTerrainParams gives one (SlonkTerrainTests.cpp's). */
	FPlanetTerrainParams MarkWorld()
	{
		FPlanetTerrainParams P;
		P.Seed = FVector(3.1, 7.7, 1.3);
		P.Canyons = 0.6f;
		P.Strata = 0.6f;
		P.RadiusCm = 6371.0 * 1.0e5;
		P.ReliefCm = SlonkTerrain::ReliefForRadius(0.0016, P.RadiusCm);
		P.OceanDepthCm = SlonkTerrain::ReliefForRadius(0.0008, P.RadiusCm);
		P.Mountains = 0.6f;
		P.Moisture = 0.55f;
		P.CanyonDepthCm = SlonkTerrain::ScaleWithWorld(1.6e5, P.RadiusCm);
		P.CanyonWidthCm = SlonkTerrain::ScaleWithWorld(1.6e6, P.RadiusCm);
		P.SnowlineCm = 5.0e5;
		return P;
	}
	FVector MarkGlobeDir(int32 i, int32 N)
	{
		const double Z = 1.0 - 2.0 * (i + 0.5) / N;
		const double Rr = FMath::Sqrt(FMath::Max(1.0 - Z * Z, 0.0));
		const double A = i * UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0));
		return FVector(Rr * FMath::Cos(A), Rr * FMath::Sin(A), Z);
	}
	/** Every kind, as readily as a world can have it and at its kind's own size. */
	FLandmarkProfile EveryKind()
	{
		FLandmarkProfile L;
		L.Strangeness = 1.f;
		L.Hash = 77;
		for (FLandmarkKind& K : L.Kinds) { K.bLive = true; K.Share = 1.f; K.Size = 1.f; }
		return L;
	}
	FScatterSettings MarkSettings(const FLandmarkProfile& L)
	{
		FScatterSettings S;
		S.Life = 1.f;
		S.Landmarks = L;
		return S;
	}
	/** The coarsest depth whose chunks are no more than EdgeCm across. */
	int32 MarkDepth(const FPlanetTerrainParams& P, double EdgeCm)
	{
		return FMath::CeilToInt32(FMath::Log2(P.RadiusCm * UE_DOUBLE_HALF_PI / EdgeCm));
	}
	struct FMarkChunk
	{
		TArray<FTransform> Xf[SlonkScatter::NumMeshes];
		TArray<double> Drawn[SlonkScatter::NumMeshes];
	};
	/** The chunk at a depth under a direction, built once and kept. */
	struct FMarkChunks
	{
		const FPlanetTerrainParams& P;
		const FScatterSettings& S;
		TMap<FString, TSharedPtr<FMarkChunk>> Built;
		FMarkChunks(const FPlanetTerrainParams& InP, const FScatterSettings& InS) : P(InP), S(InS) {}
		const FMarkChunk* At(const FVector& Dir, int32 Depth)
		{
			int32 Face; int64 X, Y;
			if (!UPlanetTerrainComponent::ChunkAt(Dir, Depth, Face, X, Y)) { return nullptr; }
			const FString Key = FString::Printf(TEXT("%d/%d/%lld/%lld"), Face, Depth, X, Y);
			if (const TSharedPtr<FMarkChunk>* Have = Built.Find(Key)) { return Have->Get(); }
			TSharedPtr<FMarkChunk> C = MakeShared<FMarkChunk>();
			UPlanetTerrainComponent::BuildChunkScatterForTest(P, S, Face, Depth, X, Y, 64, C->Xf, C->Drawn);
			Built.Add(Key, C);
			return C.Get();
		}
	};
	/** The piece of a kind's standing at a stand's place on a chunk, or -1: within four metres over the ground (one sunk
	 *  and leaning is a metre or so off its place). */
	int32 PieceAt(const FMarkChunk& C, const SlonkScatter::FLandmarkStand& St, double RadiusCm)
	{
		const TArray<FTransform>& Xf = C.Xf[SlonkScatter::LandmarkMesh(St.Kind)];
		int32 Best = -1;
		double BestD = FMath::Square(400.0);
		for (int32 i = 0; i < Xf.Num(); ++i)
		{
			const double D = FVector::DistSquared(Xf[i].GetLocation().GetSafeNormal() * RadiusCm, St.Up * RadiusCm);
			if (D < BestD) { BestD = D; Best = i; }
		}
		return Best;
	}
	/** Stands of every kind from places spread over the globe, up to PerKind of each (no site's twice). */
	TArray<SlonkScatter::FLandmarkStand> StandsOver(const FPlanetTerrainParams& P, const FScatterSettings& S, int32 PerKind, int32 Places, double ReachCm)
	{
		TArray<SlonkScatter::FLandmarkStand> Out;
		int32 Have[SlonkLandmarks::NumKinds] = {};
		TSet<FString> Seen;
		for (int32 i = 0; i < Places; ++i)
		{
			bool bWant = false;
			for (const int32 N : Have) { bWant |= N < PerKind; }
			if (!bWant) { break; }
			TArray<SlonkScatter::FLandmarkStand> Near;
			SlonkScatter::LandmarksNear(P, S, ELandmark::Count, MarkGlobeDir(i, Places), ReachCm, Near);
			for (const SlonkScatter::FLandmarkStand& St : Near)
			{
				const FString Key = FString::Printf(TEXT("%d/%d/%lld/%lld/%d"), (int32)St.Kind, St.Face, St.GX, St.GY, St.Member);
				if (Have[(int32)St.Kind] >= PerKind || Seen.Contains(Key)) { continue; }
				Seen.Add(Key);
				++Have[(int32)St.Kind];
				Out.Add(St);
			}
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkLandmarkRollsTest, "Slonk.Landmarks.Rolls", SLONK_TEST_FLAGS)
bool FSlonkLandmarkRollsTest::RunTest(const FString& Parameters)
{
	// What a world rolls: nothing to strangeness 0.15 on any kind of world, and nothing ever on a world with no ground.
	// The same world rolls the same; grown stranger it keeps every landmark it had, as it was. At full strangeness,
	// arches on about three worlds in five, a vanished people's stones on two in five (monoliths or rings, never both),
	// reef towers and mound cities on about half of those with life, and neither on a dead one; no reef where the sea
	// is not liquid. A kind asked for by name stands whatever the roll, and moves nothing the roll gave.
	using namespace SlonkLandmarkTestHelpers;
	const EPlanetArchetype Types[] = { EPlanetArchetype::Terrestrial, EPlanetArchetype::Ocean, EPlanetArchetype::Desert, EPlanetArchetype::Ice, EPlanetArchetype::Lava,
		EPlanetArchetype::GasGiant, EPlanetArchetype::Moon, EPlanetArchetype::Venus, EPlanetArchetype::Mars, EPlanetArchetype::IceGiant };
	int32 NearHome = 0, NoGround = 0, Changed = 0, Lost = 0, BadRange = 0, Both = 0;
	for (const EPlanetArchetype Type : Types)
	{
		for (uint32 H = 1; H <= 200; ++H)
		{
			const uint32 Hash = H * 2654435761u;
			for (const float S : { 0.f, 0.08f, 0.15f })
			{
				NearHome += SlonkLandmarks::Roll(SlonkGenome::Roll(Type, S, Hash), Type, Hash, 1.f, true).Any() ? 1 : 0;
			}
			FLandmarkProfile Before;
			for (const float S : { 0.3f, 0.5f, 0.75f, 1.f })
			{
				const FWorldGenome G = SlonkGenome::Roll(Type, S, Hash);
				const FLandmarkProfile L = SlonkLandmarks::Roll(G, Type, Hash, 1.f, true);
				Changed += L != SlonkLandmarks::Roll(G, Type, Hash, 1.f, true) ? 1 : 0;
				if (Type == EPlanetArchetype::GasGiant || Type == EPlanetArchetype::IceGiant || Type == EPlanetArchetype::Moon) { NoGround += L.Any() ? 1 : 0; }
				for (int32 K = 0; K < SlonkLandmarks::NumKinds; ++K)
				{
					// (Roll gives a world its genome's strangeness, which the genome may have withheld: only a kept kind is held to.)
					if (Before.Kinds[K].bLive && G.Strangeness >= Before.Strangeness && !(L.Kinds[K] == Before.Kinds[K])) { ++Lost; }
					if (L.Kinds[K].bLive && (L.Kinds[K].Share < 0.35f || L.Kinds[K].Share > 1.f || L.Kinds[K].Size < 0.8f || L.Kinds[K].Size > 1.25f)) { ++BadRange; }
				}
				Both += L.Has(ELandmark::Monolith) && L.Has(ELandmark::Ring) ? 1 : 0;
				Before = L;
			}
		}
	}
	TestEqual(TEXT("no world to strangeness 0.15 has a landmark"), NearHome, 0);
	TestEqual(TEXT("no world without ground has one"), NoGround, 0);
	TestEqual(TEXT("the same world rolls the same"), Changed, 0);
	TestEqual(TEXT("a world grown stranger keeps every landmark it had, as it was"), Lost, 0);
	TestEqual(TEXT("each kind's share and size in their ranges"), BadRange, 0);
	TestEqual(TEXT("one people, one kind of stone: never monoliths and rings both"), Both, 0);

	// The shares at full strangeness, on a living world with liquid seas, a dead one, and a living one whose seas are not.
	FWorldGenome Full;
	Full.Strangeness = 1.f;
	const int32 N = 4000;
	int32 Count[SlonkLandmarks::NumKinds] = {}, Ruins = 0, DeadLife = 0, DrySeaReefs = 0, ForcedMissing = 0, ForcedMoved = 0, ForcedNearHome = 0;
	for (int32 i = 0; i < N; ++i)
	{
		const uint32 Hash = (uint32)(i + 1) * 2246822519u;
		const FLandmarkProfile L = SlonkLandmarks::Roll(Full, EPlanetArchetype::Terrestrial, Hash, 1.f, true);
		for (int32 K = 0; K < SlonkLandmarks::NumKinds; ++K) { Count[K] += L.Kinds[K].bLive ? 1 : 0; }
		Ruins += L.Has(ELandmark::Monolith) || L.Has(ELandmark::Ring) ? 1 : 0;
		const FLandmarkProfile Dead = SlonkLandmarks::Roll(Full, EPlanetArchetype::Mars, Hash, 0.f, true);
		DeadLife += Dead.Has(ELandmark::ReefTower) || Dead.Has(ELandmark::Mound) ? 1 : 0;
		// What needs no life is the same on a dead world as on a living one.
		for (const ELandmark K : { ELandmark::Arch, ELandmark::Monolith, ELandmark::Ring }) { ForcedMoved += !(Dead.Kinds[(int32)K] == L.Kinds[(int32)K]) ? 1 : 0; }
		DrySeaReefs += SlonkLandmarks::Roll(Full, EPlanetArchetype::Terrestrial, Hash, 1.f, false).Has(ELandmark::ReefTower) ? 1 : 0;
		// Every kind asked for: all stand, and those the roll gave are as it gave them.
		const FLandmarkProfile All = SlonkLandmarks::Roll(Full, EPlanetArchetype::Terrestrial, Hash, 1.f, true, 0x1f);
		for (int32 K = 0; K < SlonkLandmarks::NumKinds; ++K)
		{
			ForcedMissing += All.Kinds[K].bLive ? 0 : 1;
			ForcedMoved += L.Kinds[K].bLive && !(All.Kinds[K] == L.Kinds[K]) ? 1 : 0;
		}
		// One kind asked for on a world near home: that kind and no other; on a giant, still none.
		const FLandmarkProfile Home = SlonkLandmarks::Roll(FWorldGenome(), EPlanetArchetype::Terrestrial, Hash, 1.f, true, 1 << (int32)ELandmark::Ring);
		for (int32 K = 0; K < SlonkLandmarks::NumKinds; ++K) { ForcedNearHome += Home.Kinds[K].bLive != (K == (int32)ELandmark::Ring) ? 1 : 0; }
		ForcedNearHome += SlonkLandmarks::Roll(Full, EPlanetArchetype::GasGiant, Hash, 1.f, true, 0x1f).Any() ? 1 : 0;
	}
	const float Chance = SlonkFlora::DepartChance(1.f);
	AddInfo(FString::Printf(TEXT("of %d living worlds at full strangeness (the chance things depart by %.2f): arches on %d, monoliths %d, rings %d, reef towers %d, mound cities %d"), N, Chance,
		Count[0], Count[1], Count[2], Count[3], Count[4]));
	auto Near = [N](int32 Got, float Share) { return FMath::Abs((float)Got / N - Share) < 0.035f; };
	TestTrue(TEXT("arches on 0.65 of the chance"), Near(Count[(int32)ELandmark::Arch], 0.65f * Chance));
	TestTrue(TEXT("a vanished people's stones on 0.4 of it, a little more often monoliths than rings"), Near(Ruins, 0.4f * Chance) && Count[(int32)ELandmark::Monolith] > Count[(int32)ELandmark::Ring]
		&& Count[(int32)ELandmark::Ring] > N / 10);
	TestTrue(TEXT("reef towers on 0.6 of it, mound cities on 0.5"), Near(Count[(int32)ELandmark::ReefTower], 0.6f * Chance) && Near(Count[(int32)ELandmark::Mound], 0.5f * Chance));
	TestEqual(TEXT("a dead world has nothing its life would have built"), DeadLife, 0);
	TestEqual(TEXT("no reef where the sea is not liquid"), DrySeaReefs, 0);
	TestEqual(TEXT("every kind asked for by name stands"), ForcedMissing, 0);
	TestEqual(TEXT("asking for one moves nothing the roll gave, and what needs no life is the same on a dead world"), ForcedMoved, 0);
	TestEqual(TEXT("a kind asked for near home stands alone there, and none on a giant"), ForcedNearHome, 0);
	// As the world's ground can carry them: none on a world all sea, no reef on one with no sea, and what is kept as rolled.
	const FLandmarkProfile Every = SlonkLandmarks::Roll(Full, EPlanetArchetype::Terrestrial, 12345u, 1.f, true, 0x1f);
	FLandmarkProfile AllSea = Every, NoSea = Every, Shores = Every;
	SlonkLandmarks::FitToGround(AllSea, 0.01f);
	SlonkLandmarks::FitToGround(NoSea, 1.f);
	SlonkLandmarks::FitToGround(Shores, 0.3f);
	bool bDryKept = !NoSea.Has(ELandmark::ReefTower);
	for (const ELandmark K : { ELandmark::Arch, ELandmark::Monolith, ELandmark::Ring, ELandmark::Mound }) { bDryKept &= NoSea.Kinds[(int32)K] == Every.Kinds[(int32)K]; }
	TestTrue(TEXT("a world all sea has no landmark, one with no sea no reef and the rest as rolled, one with shores all it rolled"), Every.Any() && !AllSea.Any() && bDryKept && Shores == Every);
	// The names the debug commands know them by.
	bool bNames = true;
	for (const ELandmark K : MarkKinds) { bNames &= SlonkLandmarks::KindNamed(SlonkLandmarks::KindKey(K)) == K; }
	TestTrue(TEXT("each kind is found by its key, and nothing else is one (a locked world's temperate \"ring\" least of all)"), bNames && SlonkLandmarks::KindNamed(TEXT("ring")) == ELandmark::Count
		&& SlonkLandmarks::KindNamed(TEXT("mountain")) == ELandmark::Count && SlonkLandmarks::KindNamed(TEXT("")) == ELandmark::Count);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkLandmarkMeshesTest, "Slonk.Landmarks.Meshes", SLONK_TEST_FLAGS)
bool FSlonkLandmarkMeshesTest::RunTest(const FString& Parameters)
{
	// Each kind's mesh at each level of detail: closed solids wound outward, within the level's triangles, a hundred
	// of its own centimetres in its kind's measure (an arch's span, a stone's height, a ring's width across, a tower's
	// or a mound's height over its origin), reaching as far under its origin as BelowCm says; each coarser level is
	// lighter and fills the same space. Its collision is convex parts within it that leave the way under an arch and
	// through a ring open. The game's mesh for a kind has the three levels and those parts, and is made once.
	using namespace SlonkLandmarkTestHelpers;
	using namespace UE::Geometry;
	for (const ELandmark Kind : MarkKinds)
	{
		const TCHAR* Name = SlonkLandmarks::KindName(Kind);
		FAxisAlignedBox3d Fine;
		int32 Before = MAX_int32;
		for (int32 Lod = 0; Lod < SlonkLandmarks::NumLods; ++Lod)
		{
			FDynamicMesh3 M;
			SlonkLandmarks::BuildLod(Kind, Lod, M);
			const FAxisAlignedBox3d B = M.GetBounds();
			const FVector2d VolumeArea = TMeshQueries<FDynamicMesh3>::GetVolumeArea(M);
			const bool bSpan = Kind == ELandmark::Arch || Kind == ELandmark::Ring;
			const double Measure = bSpan ? B.Max.X - B.Min.X : B.Max.Z;
			AddInfo(FString::Printf(TEXT("%s, level %d: %d triangles, %.0f x %.0f x %.0f from %.0f under its origin, volume %.0f"), Name, Lod, M.TriangleCount(), B.Max.X - B.Min.X, B.Max.Y - B.Min.Y,
				B.Max.Z - B.Min.Z, -B.Min.Z, VolumeArea.X));
			// (A standing stone is forty triangles from its second level on: there is nothing left to take.)
			TestTrue(FString::Printf(TEXT("%s, level %d: a mesh within the level's triangles, lighter than the level before"), Name, Lod),
				M.TriangleCount() >= 40 && M.TriangleCount() <= SlonkLandmarks::MaxTriangles(Lod) && (M.TriangleCount() < Before || Before <= 48));
			TestTrue(FString::Printf(TEXT("%s, level %d: closed, and wound outward"), Name, Lod), M.IsClosed() && VolumeArea.X > 1000.0);
			TestTrue(FString::Printf(TEXT("%s, level %d: a hundred of its own cm in its kind's measure"), Name, Lod), Measure > 88.0 && Measure < 114.0);
			TestTrue(FString::Printf(TEXT("%s, level %d: reaches as far under its origin as BelowCm says"), Name, Lod), FMath::Abs(-B.Min.Z - SlonkLandmarks::BelowCm(Kind)) < 3.0);
			TestTrue(FString::Printf(TEXT("%s, level %d: normals and the dummy UVs the material wants"), Name, Lod), M.HasAttributes() && M.Attributes()->PrimaryNormals() != nullptr
				&& M.Attributes()->PrimaryNormals()->ElementCount() >= M.VertexCount() && M.Attributes()->PrimaryUV() != nullptr);
			if (Lod == 0) { Fine = B; }
			else
			{
				TestTrue(FString::Printf(TEXT("%s, level %d: fills the space the finest does"), Name, Lod), (B.Min - Fine.Min).GetAbsMax() < 9.0 && (B.Max - Fine.Max).GetAbsMax() < 9.0);
			}
			Before = M.TriangleCount();
		}
		TArray<TArray<FVector>> Hulls;
		SlonkLandmarks::CollisionParts(Kind, Hulls);
		int32 Thin = 0, Outside = 0, Blocking = 0;
		// The way through: under an arch's crown, and the middle of a ring.
		const FVector Way = Kind == ELandmark::Arch ? FVector(0.0, 0.0, 12.0) : FVector(0.0, 0.0, 40.0);
		for (const TArray<FVector>& Hull : Hulls)
		{
			Thin += Hull.Num() < 4 ? 1 : 0;
			FBox Box(ForceInit);
			for (const FVector& V : Hull)
			{
				Box += V;
				Outside += V.X < Fine.Min.X - 6.0 || V.Y < Fine.Min.Y - 6.0 || V.Z < Fine.Min.Z - 6.0 || V.X > Fine.Max.X + 6.0 || V.Y > Fine.Max.Y + 6.0 || V.Z > Fine.Max.Z + 6.0 ? 1 : 0;
			}
			// (A hull lies inside its points' box: a way no box holds, no hull does.)
			Blocking += Box.IsInsideOrOn(Way) ? 1 : 0;
		}
		TestTrue(FString::Printf(TEXT("%s: convex parts, each of four points or more, within the mesh"), Name), Hulls.Num() >= 1 && Thin == 0 && Outside == 0);
		if (Kind == ELandmark::Arch || Kind == ELandmark::Ring)
		{
			TestTrue(FString::Printf(TEXT("%s: of several parts, none across the way through it"), Name), Hulls.Num() >= 6 && Blocking == 0);
		}
		UStaticMesh* Mesh = SlonkLandmarks::GetMesh(Kind);
		if (TestTrue(FString::Printf(TEXT("%s: the game's mesh"), Name), Mesh != nullptr))
		{
			TestTrue(FString::Printf(TEXT("%s: made once, with its three levels and its parts to walk into"), Name), SlonkLandmarks::GetMesh(Kind) == Mesh && Mesh->GetNumLODs() == SlonkLandmarks::NumLods
				&& Mesh->GetBodySetup() && Mesh->GetBodySetup()->AggGeom.ConvexElems.Num() == Hulls.Num());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkLandmarkStandsTest, "Slonk.Landmarks.Stands", SLONK_TEST_FLAGS)
bool FSlonkLandmarkStandsTest::RunTest(const FString& Parameters)
{
	// Where they stand. A world with none carries none, and its chunks are no coarser for them. On a world with every
	// kind, the field's stands (LandmarksNear) are the pieces the chunks carry: each found on the chunk the trees ride,
	// of its kind's size, no further from upright than its kind leans; one on land with its origin in the drawn ground
	// and its mesh's foot under it, on dry ground; one in the sea at the sea's level over shallows its mesh reaches the
	// floor of. The coarsest chunks that carry landmarks (twenty kilometres across) have the same pieces in the same
	// places, at the same size; a chunk coarser still has none.
	using namespace SlonkLandmarkTestHelpers;
	const FPlanetTerrainParams P = MarkWorld();
	const double R = P.RadiusCm;
	const FScatterSettings With = MarkSettings(EveryKind()), Plain = MarkSettings(FLandmarkProfile());
	FScatterSettings Bare = Plain, BareWith = With;
	Bare.Life = 0.f;
	BareWith.Life = 0.f;
	const double FaceEdge = SlonkScatter::FaceEdgeCm(R);
	TestTrue(TEXT("a world without landmarks has chunks no coarser for them, one with them has"),
		SlonkScatter::MaxChunkEdgeCm(Bare, FaceEdge) < SlonkScatter::LandmarkMaxEdgeCm && SlonkScatter::MaxChunkEdgeCm(BareWith, FaceEdge) >= SlonkScatter::LandmarkMaxEdgeCm
		&& SlonkScatter::MaxChunkEdgeCm(With, FaceEdge) >= SlonkScatter::MaxChunkEdgeCm(Plain, FaceEdge));

	const int32 Fine = MarkDepth(P, 31000.0), Far = MarkDepth(P, SlonkScatter::LandmarkMaxEdgeCm);
	const TArray<SlonkScatter::FLandmarkStand> Stands = StandsOver(P, With, 12, 240, 2.5e6);
	FMarkChunks Near(P, With), Coarse(P, With), Without(P, Plain);
	int32 Of[SlonkLandmarks::NumKinds] = {}, Found[SlonkLandmarks::NumKinds] = {}, OnFar[SlonkLandmarks::NumKinds] = {};
	int32 BadSize = 0, Leaning = 0, Floating = 0, Buried = 0, Wet = 0, OffSea = 0, NoFloor = 0, FarMoved = 0, OnPlain = 0, Beyond = 0, BeyondChunks = 0, LegsOut = 0, Legs = 0, Steep = 0;
	// The most each kind is sunk, as a share of its size (ScatterChunk's own), and a hundredth for the lean of one sunk.
	const double MostSunk[SlonkLandmarks::NumKinds] = { 0.11, 0.07, 0.05, 0.0, 0.07 };
	double Least[SlonkLandmarks::NumKinds], Most[SlonkLandmarks::NumKinds];
	for (int32 K = 0; K < SlonkLandmarks::NumKinds; ++K) { Least[K] = 1.0e12; Most[K] = 0.0; }
	for (const SlonkScatter::FLandmarkStand& St : Stands)
	{
		const int32 K = (int32)St.Kind, Mesh = SlonkScatter::LandmarkMesh(St.Kind);
		const SlonkLandmarks::FKindSpec& Spec = SlonkLandmarks::SpecOf(St.Kind);
		++Of[K];
		const FMarkChunk* C = Near.At(St.Up, Fine);
		const int32 i = C ? PieceAt(*C, St, R) : -1;
		if (const FMarkChunk* None = Without.At(St.Up, Fine)) { for (const ELandmark Kind : MarkKinds) { OnPlain += None->Xf[SlonkScatter::LandmarkMesh(Kind)].Num(); } }
		if (i < 0) { continue; }
		++Found[K];
		const FTransform& T = C->Xf[Mesh][i];
		const FVector Up = T.GetLocation().GetSafeNormal(), Scale = T.GetScale3D();
		// Its kind's measure: the span along its x for an arch or a ring, its height for the rest.
		const double Size = (St.Kind == ELandmark::Arch || St.Kind == ELandmark::Ring ? Scale.X : Scale.Z) * SlonkLandmarks::UnitCm;
		Least[K] = FMath::Min(Least[K], Size);
		Most[K] = FMath::Max(Most[K], Size);
		// (A site's others are down to 0.45 of its first; a mound may be drawn 0.85 as tall as its measure.)
		if (Size < Spec.MinCm * 0.45 * 0.84 || Size > Spec.MaxCm * 1.16 || FMath::Abs(Size / St.SizeCm - 1.0) > 0.26) { ++BadSize; }
		if (FVector::DotProduct(T.GetRotation().GetAxisZ(), Up) < FMath::Cos(FMath::DegreesToRadians(Spec.TiltDeg + 0.2))) { ++Leaning; }
		const double Over = T.GetLocation().Size() - R;
		const FPlanetTerrainSample Ground = SlonkTerrain::Sample(P, Up, 200.0);
		if (Spec.bInSea)
		{
			OffSea += FMath::Abs(Over) > 2.0 ? 1 : 0;
			// Shallows, and its mesh stands on their floor.
			NoFloor += !Ground.bWater || -Ground.HeightCm > SlonkLandmarks::BelowCm(St.Kind) * Scale.Z ? 1 : 0;
		}
		else
		{
			const double Foot = Over - C->Drawn[Mesh][i];
			Floating += Foot > 1.0 ? 1 : 0;
			Buried += Foot < -(MostSunk[K] + 0.01) * St.SizeCm - 5.0 ? 1 : 0;
			Wet += Ground.bWater ? 1 : 0;
		}
		if (St.Kind == ELandmark::Arch)
		{
			// None on a slope past one in two; each leg's foot, under the ground where it comes down.
			Steep += SlonkTerrain::Sample(P, St.Up, 1000.0).Slope > 0.5f ? 1 : 0;
			for (const double Side : { -1.0, 1.0 })
			{
				const FVector FootAt = T.TransformPosition(FVector(38.0 * Side, 0.0, -SlonkLandmarks::BelowCm(St.Kind)));
				++Legs;
				LegsOut += FootAt.Size() - R > SlonkTerrain::Sample(P, FootAt.GetSafeNormal(), 200.0).HeightCm ? 1 : 0;
			}
		}
		// The chunk twenty kilometres across carries it too, in the same place and at the same size.
		if (const FMarkChunk* F = Coarse.At(St.Up, Far))
		{
			const int32 j = PieceAt(*F, St, R);
			if (j >= 0)
			{
				++OnFar[K];
				FarMoved += !F->Xf[Mesh][j].GetScale3D().Equals(Scale, 1.0e-6) || !F->Xf[Mesh][j].GetRotation().Equals(T.GetRotation(), 1.0e-6) ? 1 : 0;
			}
		}
	}
	// A chunk coarser than any that carries them.
	for (int32 i = 0; i < 6; ++i)
	{
		int32 Face; int64 X, Y;
		if (Stands.Num() == 0 || !UPlanetTerrainComponent::ChunkAt(Stands[i * Stands.Num() / 6].Up, Far - 1, Face, X, Y)) { continue; }
		FMarkChunk Big;
		++BeyondChunks;
		UPlanetTerrainComponent::BuildChunkScatterForTest(P, BareWith, Face, Far - 1, X, Y, 64, Big.Xf, Big.Drawn);
		for (const ELandmark Kind : MarkKinds) { Beyond += Big.Xf[SlonkScatter::LandmarkMesh(Kind)].Num(); }
	}
	int32 All = 0, AllFound = 0, AllFar = 0;
	for (const ELandmark Kind : MarkKinds)
	{
		const int32 K = (int32)Kind;
		All += Of[K]; AllFound += Found[K]; AllFar += OnFar[K];
		AddInfo(FString::Printf(TEXT("%s: %d stands, %d found on the chunks to 310 m and %d on those to 20 km, %.0f to %.0f m"), SlonkLandmarks::KindName(Kind), Of[K], Found[K], OnFar[K], Least[K] / 100.0, Most[K] / 100.0));
		TestTrue(FString::Printf(TEXT("%s: the world has them standing"), SlonkLandmarks::KindName(Kind)), Of[K] >= 6);
	}
	AddInfo(FString::Printf(TEXT("%d arch legs, %d with their foot clear of the ground"), Legs, LegsOut));
	if (!TestTrue(TEXT("found the landmarks"), All >= 40)) { return true; }
	TestEqual(TEXT("every stand the field gives is a piece on its chunk"), AllFound, All);
	TestEqual(TEXT("and on the chunk twenty kilometres across, every one"), AllFar, All);
	TestEqual(TEXT("the same size and the same way up at both"), FarMoved, 0);
	TestEqual(TEXT("each of its kind's size"), BadSize, 0);
	TestEqual(TEXT("none leans further than its kind does"), Leaning, 0);
	TestEqual(TEXT("none on land stands clear of the drawn ground"), Floating, 0);
	TestEqual(TEXT("none sunk past what its mesh has under its origin"), Buried, 0);
	TestEqual(TEXT("none of the land's stands in water"), Wet, 0);
	TestEqual(TEXT("a reef's towers stand at the sea's level"), OffSea, 0);
	TestEqual(TEXT("in shallows their mesh reaches the floor of"), NoFloor, 0);
	TestTrue(TEXT("an arch's legs come down into the ground, and none stands on a slope past one in two"), Legs >= 12 && LegsOut == 0 && Steep == 0);
	TestEqual(TEXT("a world without landmarks carries none"), OnPlain, 0);
	TestTrue(TEXT("a chunk coarser than twenty kilometres carries none"), BeyondChunks >= 4 && Beyond == 0);

	// A world's sites are its own: another world of the same ground (another hash) has its landmarks elsewhere.
	FLandmarkProfile Other = EveryKind();
	Other.Hash = 78;
	const FScatterSettings Elsewhere = MarkSettings(Other);
	int32 Shared = 0, Theirs = 0;
	for (int32 i = 0; i < 4; ++i)
	{
		TArray<SlonkScatter::FLandmarkStand> Here, There;
		SlonkScatter::LandmarksNear(P, With, ELandmark::Count, Stands[i * Stands.Num() / 4].Up, 2.0e6, Here);
		SlonkScatter::LandmarksNear(P, Elsewhere, ELandmark::Count, Stands[i * Stands.Num() / 4].Up, 2.0e6, There);
		Theirs += There.Num();
		for (const SlonkScatter::FLandmarkStand& A : Here)
		{
			Shared += There.ContainsByPredicate([&A](const SlonkScatter::FLandmarkStand& B) { return B.Kind == A.Kind && FVector::DistSquared(A.Up, B.Up) < FMath::Square(1.0e-7); }) ? 1 : 0;
		}
	}
	TestTrue(TEXT("another world of the same ground has its landmarks elsewhere"), Theirs > 20 && Shared == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkLandmarkClearingTest, "Slonk.Landmarks.Clearing", SLONK_TEST_FLAGS)
bool FSlonkLandmarkClearingTest::RunTest(const FString& Parameters)
{
	// A landmark's foot is its own: no rock, bush, tree or tuft the scatter places stands in it, where on the same
	// world without the landmark some would. Looked for on the chunks that carry the grass (eighty metres across),
	// those the trees and boulders ride (310 m), and the far levels that carry the trees thinned (1.2 and 5 km), from
	// which the clearing must show as it will close to.
	using namespace SlonkLandmarkTestHelpers;
	const FPlanetTerrainParams P = MarkWorld();
	const double R = P.RadiusCm;
	const FScatterSettings With = MarkSettings(EveryKind()), Plain = MarkSettings(FLandmarkProfile());
	const TArray<SlonkScatter::FLandmarkStand> Stands = StandsOver(P, With, 8, 240, 2.5e6);
	FMarkChunks Marked(P, With), Unmarked(P, Plain);
	int32 Feet = 0, InFoot = 0, WouldBe = 0, WouldBeFar = 0;
	for (const SlonkScatter::FLandmarkStand& St : Stands)
	{
		// (A reef's towers stand in the sea, where nothing else does.)
		if (SlonkLandmarks::SpecOf(St.Kind).bInSea) { continue; }
		++Feet;
		for (const int32 Depth : { MarkDepth(P, 31000.0), MarkDepth(P, 8000.0), MarkDepth(P, 124000.0), MarkDepth(P, 500000.0) })
		{
			const FMarkChunk* A = Marked.At(St.Up, Depth);
			const FMarkChunk* B = Unmarked.At(St.Up, Depth);
			if (!A || !B) { continue; }
			for (int32 M = 0; M < SlonkScatter::FirstLandmark; ++M)
			{
				for (const FTransform& T : A->Xf[M]) { InFoot += FVector::Dist(T.GetLocation().GetSafeNormal() * R, St.Up * R) < St.FootCm ? 1 : 0; }
				for (const FTransform& T : B->Xf[M])
				{
					const bool bIn = FVector::Dist(T.GetLocation().GetSafeNormal() * R, St.Up * R) < St.FootCm;
					WouldBe += bIn ? 1 : 0;
					WouldBeFar += bIn && Depth < MarkDepth(P, 31000.0) ? 1 : 0;
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("in the feet of %d landmarks on land: %d rocks and plants, where %d would stand without them (%d of those on the far levels)"), Feet, InFoot, WouldBe, WouldBeFar));
	TestTrue(TEXT("found landmarks on ground where things grow and lie"), Feet >= 20 && WouldBe > 100);
	TestEqual(TEXT("nothing else stands in a landmark's foot"), InFoot, 0);

	// On a world with towns none stands in a town's ground: the stands the field gives leave out those a town holds,
	// and keep every other as it was.
	FScatterSettings Settled = With;
	Settled.bCities = true;
	int32 InTowns = 0, WouldBeInTowns = 0, Dropped = 0, Added = 0, TownsSeen = 0;
	for (int32 i = 0; i < 60 && WouldBeInTowns < 3; ++i)
	{
		const FVector From = MarkGlobeDir(i, 60);
		if (FMath::Max3(FMath::Abs(From.X), FMath::Abs(From.Y), FMath::Abs(From.Z)) < 0.75 || SlonkTerrain::SampleStructure(P, From).bWater) { continue; }
		TArray<FTownSite> Towns;
		SlonkCities::EnumerateTowns(P, SlonkCivs::Colonists(), From * R, 6.0e6, Towns, 300);
		if (Towns.Num() == 0) { continue; }
		TownsSeen += Towns.Num();
		auto InATown = [&Towns, R](const SlonkScatter::FLandmarkStand& St)
		{
			for (const FTownSite& T : Towns) { if (!T.bCliff && FVector::Dist(St.Up * R, T.LocalCm) < T.RadiusCm) { return true; } }
			return false;
		};
		for (const ELandmark Kind : { ELandmark::Monolith, ELandmark::Mound, ELandmark::Arch })
		{
			TArray<SlonkScatter::FLandmarkStand> Wild, Kept;
			SlonkScatter::LandmarksNear(P, With, Kind, From, 5.0e6, Wild);
			SlonkScatter::LandmarksNear(P, Settled, Kind, From, 5.0e6, Kept);
			for (const SlonkScatter::FLandmarkStand& St : Wild) { WouldBeInTowns += InATown(St) ? 1 : 0; }
			for (const SlonkScatter::FLandmarkStand& St : Kept)
			{
				InTowns += InATown(St) ? 1 : 0;
				Added += Wild.ContainsByPredicate([&St](const SlonkScatter::FLandmarkStand& W) { return W.Up.Equals(St.Up, 0.0) && W.SizeCm == St.SizeCm; }) ? 0 : 1;
			}
			Dropped += Wild.Num() - Kept.Num();
		}
	}
	AddInfo(FString::Printf(TEXT("round %d towns: %d landmarks stand in a town's ground without the rule and %d with it; the rule takes %d away in all (a way's among them) and adds %d"), TownsSeen, WouldBeInTowns, InTowns,
		Dropped, Added));
	if (WouldBeInTowns == 0) { AddInfo(TEXT("no landmark's site fell in a town on the test world; the towns' rule went unchecked")); return true; }
	TestEqual(TEXT("none stands in a town's ground"), InTowns, 0);
	TestTrue(TEXT("the towns only take landmarks away, never move or add one"), Dropped >= WouldBeInTowns && Added == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkLandmarkNearTest, "Slonk.Landmarks.Near", SLONK_TEST_FLAGS)
bool FSlonkLandmarkNearTest::RunTest(const FString& Parameters)
{
	// Finding them from a place (what `ship terrain goto <landmark>` flies to): the nearest first, none further off than
	// asked, the same every time; a wider reach finds every one a narrower did; a kind asked for alone is that kind's
	// share of them all, and the ground under each suits its kind (LandmarkGroundAt). A world with none finds none.
	using namespace SlonkLandmarkTestHelpers;
	const FPlanetTerrainParams P = MarkWorld();
	const double R = P.RadiusCm;
	const FScatterSettings With = MarkSettings(EveryKind()), Plain = MarkSettings(FLandmarkProfile());
	int32 Places = 0, OutOfOrder = 0, TooFar = 0, Lost = 0, Unlike = 0, WrongKind = 0, Unsuited = 0, Total = 0, OnPlain = 0;
	for (int32 i = 0; i < 40 && Places < 6; ++i)
	{
		const FVector From = MarkGlobeDir(i, 40);
		if (FMath::Max3(FMath::Abs(From.X), FMath::Abs(From.Y), FMath::Abs(From.Z)) < 0.8) { continue; }	// well inside a face
		TArray<SlonkScatter::FLandmarkStand> Close, Wide, Again, None;
		SlonkScatter::LandmarksNear(P, With, ELandmark::Count, From, 1.5e6, Close);
		SlonkScatter::LandmarksNear(P, With, ELandmark::Count, From, 4.0e6, Wide);
		SlonkScatter::LandmarksNear(P, With, ELandmark::Count, From, 4.0e6, Again);
		SlonkScatter::LandmarksNear(P, Plain, ELandmark::Count, From, 4.0e6, None);
		OnPlain += None.Num();
		if (Wide.Num() < 4) { continue; }
		++Places;
		Total += Wide.Num();
		Unlike += Again.Num() != Wide.Num() ? 1 : 0;
		for (int32 k = 0; k < Wide.Num(); ++k)
		{
			const double Away = FMath::Acos(FMath::Clamp(FVector::DotProduct(Wide[k].Up, From), -1.0, 1.0)) * R;
			TooFar += Away > 4.0e6 * 1.0001 ? 1 : 0;
			OutOfOrder += k > 0 && FVector::DotProduct(Wide[k].Up, From) > FVector::DotProduct(Wide[k - 1].Up, From) + 1.0e-15 ? 1 : 0;
			Unlike += Again.IsValidIndex(k) && (!Again[k].Up.Equals(Wide[k].Up, 0.0) || Again[k].SizeCm != Wide[k].SizeCm) ? 1 : 0;
			Unsuited += SlonkScatter::LandmarkGroundAt(P, Wide[k].Kind, Wide[k].Up, Wide[k].SizeCm, With.Life) <= 0.0 ? 1 : 0;
		}
		for (const SlonkScatter::FLandmarkStand& St : Close)
		{
			Lost += Wide.ContainsByPredicate([&St](const SlonkScatter::FLandmarkStand& W) { return W.Kind == St.Kind && W.Up.Equals(St.Up, 0.0); }) ? 0 : 1;
		}
		int32 ByKind = 0;
		for (const ELandmark Kind : MarkKinds)
		{
			TArray<SlonkScatter::FLandmarkStand> One;
			SlonkScatter::LandmarksNear(P, With, Kind, From, 4.0e6, One);
			ByKind += One.Num();
			for (const SlonkScatter::FLandmarkStand& St : One) { WrongKind += St.Kind != Kind ? 1 : 0; }
		}
		WrongKind += ByKind != Wide.Num() ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("%d landmarks within 40 km of %d places"), Total, Places));
	if (!TestTrue(TEXT("found places with landmarks about them"), Places >= 4 && Total >= 40)) { return true; }
	TestEqual(TEXT("the nearest first"), OutOfOrder, 0);
	TestEqual(TEXT("none further off than asked"), TooFar, 0);
	TestEqual(TEXT("the same every time"), Unlike, 0);
	TestEqual(TEXT("a wider reach finds every one a narrower did"), Lost, 0);
	TestEqual(TEXT("a kind asked for alone is its share of them all"), WrongKind, 0);
	TestEqual(TEXT("the ground under each suits its kind"), Unsuited, 0);
	TestEqual(TEXT("a world with none finds none"), OnPlain, 0);
	return true;
}

#endif
