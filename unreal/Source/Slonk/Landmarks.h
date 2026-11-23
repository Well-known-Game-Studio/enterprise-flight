// Slonk - the landmarks of a strange world: the great things that stand on it, to be found from the air and landed
// beside. Rock the ground's height field cannot make (it cannot overhang): natural arches. What a vanished people left:
// standing monoliths, or great rings half sunk in the ground. What its life has built: reef towers standing out of
// the shallows, mound cities on the open plains; and what it has left: the coiled shells of sea animals a hundred metres
// across, half buried where the sea was. And, over a world far gone in strangeness, rock that hangs in the air. A
// world's are rolled from its genome (WorldGenome.h), so they are the
// same on every visit, and none stands on a world to strangeness 0.15: the home system and the charted worlds have none.
// They are placed by the scatter on a lattice of their own (TerrainScatter.h), each a mesh grown in code
// (LandmarkMeshes.h) and drawn as the world's rock is.

#pragma once

#include "CoreMinimal.h"
#include "WorldGenome.h"

enum class EPlanetArchetype : uint8;

/** What a landmark is. */
enum class ELandmark : uint8
{
	/** A natural arch of the world's own rock, on its drier, rockier ground. */
	Arch,
	/** A standing stone of a vanished people's, alone or in a row of them, on open level ground. */
	Monolith,
	/** A great ring of theirs, stood on edge and half sunk in the ground. */
	Ring,
	/** Fluted towers grown by a sea's life, standing out of the shallows off a coast. */
	ReefTower,
	/** A city of spired mounds raised by the land's, on open plains. */
	Mound,
	/** The coiled shell of a sea animal long dead, stood on its edge and part buried, on low level ground by the sea. */
	Shell,
	/** Rocks hanging in the air over the ground they were torn from, flat on top, a root hanging under each. */
	FloatingRock,
	Count
};

/** One kind of landmark as a world has it. */
struct FLandmarkKind
{
	bool bLive = false;
	/** How readily a site of its lattice holds one where the ground suits it, 0.35 to 1. */
	float Share = 0.f;
	/** Its size against its kind's own range, 0.8 to 1.25. */
	float Size = 1.f;
	/** Its stone's own colour, linear, for the kinds that do not take the world's rock: a people's dark stone, a reef's. */
	FLinearColor Color = FLinearColor(0.08f, 0.08f, 0.08f);

	bool operator==(const FLandmarkKind& O) const { return bLive == O.bLive && Share == O.Share && Size == O.Size && Color == O.Color; }
};

/** A world's landmarks: none by default, as on every world near home. */
struct FLandmarkProfile
{
	float Strangeness = 0.f;
	uint32 Hash = 0;
	FLandmarkKind Kinds[(int32)ELandmark::Count];

	bool Any() const { for (const FLandmarkKind& K : Kinds) { if (K.bLive) { return true; } } return false; }
	bool Has(ELandmark Kind) const { return Kinds[(int32)Kind].bLive; }
	bool operator==(const FLandmarkProfile& O) const
	{
		for (int32 K = 0; K < (int32)ELandmark::Count; ++K) { if (!(Kinds[K] == O.Kinds[K])) { return false; } }
		return Hash == O.Hash;
	}
	bool operator!=(const FLandmarkProfile& O) const { return !(*this == O); }
};

namespace SlonkLandmarks
{
	constexpr int32 NumKinds = (int32)ELandmark::Count;

	/** How a kind stands, in its own measure: how big one is (the span of an arch, the height of a stone, a ring's
	 *  width across, a tower's height over the sea, a mound's height), cm, before the world's own size for it; how many
	 *  stand at one site, and how far apart as a share of their size; how far round each nothing else stands, as a share
	 *  of its size (none for one that hangs clear of the ground); how far it may lean from upright, degrees; whether it
	 *  stands in the sea; the share of its lattice's cells that are sites for one, where the ground suits it (the reefs'
	 *  is the most: the shallows they need are a narrow band round a coast); and, for a kind that hangs in the air, how
	 *  far over the ground its origin does, from and to, as a share of the size of the first at its site. */
	struct FKindSpec
	{
		double MinCm, MaxCm;
		int32 MinCount, MaxCount;
		double Apart;
		double Foot;
		double TiltDeg;
		bool bInSea;
		double Sites;
		double LiftLo, LiftHi;
	};
	SLONK_API const FKindSpec& SpecOf(ELandmark Kind);

	/**
	 * A world's landmarks from its genome and its hash: the same world always rolls the same, and to strangeness 0.15 it
	 * has none. Draws from a stream of its own, so nothing else a world rolls moves, and every draw is taken whatever the
	 * earlier ones gave, each kind held against the chance the plants and the animals depart by (SlonkFlora::DepartChance):
	 * a world grown stranger keeps every landmark it had, as it was. Arches on two strange worlds in three with ground to
	 * stand on; a vanished people's stones on two in five, monoliths or rings (one people, one kind); and only where
	 * something lives (Life over 0): reef towers on three in five with a sea that is liquid (bLivingSeas), mound cities
	 * on one in two. Giant shells on two strange worlds in five with ground; and rock hangs in the air over none under
	 * strangeness 0.4, over two in five at 1. ForcedKinds, a bit to each ELandmark, is for debug: those kinds stand on the world whatever it rolled
	 * and however near home it is, as readily as any does (a world with no ground still has none).
	 */
	SLONK_API FLandmarkProfile Roll(const FWorldGenome& G, EPlanetArchetype Type, uint32 Hash, float Life, bool bLivingSeas, uint8 ForcedKinds = 0);

	/** A world's landmarks as its ground can carry them, by the share of it that is land (0 to 1, read from its height
	 *  field) and the share that is land within two hundred metres of the sea's level: a world all sea, under a fiftieth
	 *  of it land, has nowhere to stand any and no coast to speak of; one with no sea, no shallows for a reef; one with
	 *  no low ground, none of the ground the giant shells lie on. What it keeps is as it rolled it. */
	SLONK_API void FitToGround(FLandmarkProfile& L, float LandShare, float LowlandShare = 1.f);

	/** A kind in a word or two, plural ("arches", "reef towers"), and as the debug commands name it ("arch", "reef",
	 *  "shell", "skyrock"; a great ring is "greatring": "ring" is a locked world's temperate ring). */
	SLONK_API const TCHAR* KindName(ELandmark Kind);
	SLONK_API const TCHAR* KindKey(ELandmark Kind);
	/** The kind a debug command names, or Count for none ("any" and "all" are the caller's). */
	SLONK_API ELandmark KindNamed(const FString& Key);
	/** A world's landmarks in a line, for the log; "no landmarks" for none. */
	SLONK_API FString Describe(const FLandmarkProfile& L);
}
