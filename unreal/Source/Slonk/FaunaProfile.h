// Slonk - what lives on a world, beside what grows there: how its animals look, and what its seas carry. Rolled from
// the world's genome (WorldGenome.h), so a world's life is the same on every visit, and at strangeness 0 it is the
// Earth-like version's: the blue-green glow of plankton stirred at night, shoals of fish under the surface by day,
// and birds over the woods, the meadows, the coasts and the cliffs. The herds are rolled here too, and on a strange
// world whose air is thick, the sky-jellies drifting high over its ground.

#pragma once

#include "CoreMinimal.h"
#include "FaunaTypes.h"
#include "WorldGenome.h"

struct FScatterPalette;

namespace SlonkFauna
{
	/** How a world's animals look. The one place the biosphere phase replaces: everything drawn takes its colours from
	 *  here. */
	struct FFaunaLook
	{
		/** Blue-green on an Earth-like world, turned round the colour wheel with a strange world's sky. */
		FLinearColor Glow = FLinearColor(0.08f, 0.7f, 0.85f);
		/** The fireflies' light: an Earth's warm yellow-green, turned with the plankton's glow. */
		FLinearColor Firefly = FLinearColor(0.55f, 1.f, 0.08f);
		/** The pollen drifting in the sun by day: an Earth's gold, turned with the rest. */
		FLinearColor Pollen = FLinearColor(0.9f, 0.62f, 0.16f);
		/** A sky-jelly's bell by day: a pale blue-lilac, turned with the rest (there are none on an Earth-like world). */
		FLinearColor Jelly = FLinearColor(0.56f, 0.66f, 0.86f);
		/** How far round the colour wheel its animals' pigments turn from the Earth's, degrees: the sky's turn times the
		 *  strangeness, as the glow's. */
		float HueTurnDeg = 0.f;
		/** How vivid its animals are, 0..1: the Earth's browns and greys at strangeness 0, the living ground's own colour
		 *  at full saturation on the strangest worlds. */
		float Vivid = 0.f;
	};
	/** The look of a world's fauna from its genome: at strangeness 0 the Earth-like look, turned round the wheel by the
	 *  sky's turn times the strangeness as a world grows strange (its own colour, related to its sky's); its plankton's
	 *  glow (and the sky-jellies' and the glow organs' light with it) from its seas' solvent before it turns
	 *  (PlanktonBase), an alien sea's turned no brighter to the eye than its base (a water sea's turned as it always was). */
	SLONK_API FFaunaLook LookOf(const FWorldGenome& G);
	/** The plankton's glow in a sea of a solvent, before a strange world's sky turns it: water's blue-green (exactly the
	 *  Earth-like look's), an ammonia sea's amber, a methane sea's rose-magenta, an acid sea's violet, each at the water's
	 *  saturation and as bright to the eye; where the seas carry none (molten rock, none at all) the water's, which only
	 *  the jellies and the glow organs show. */
	SLONK_API FLinearColor PlanktonBase(ESolvent S);
	/** How bright a glow is to the eye (its Rec. 709 luma). */
	inline float PlanktonLuma(const FLinearColor& C) { return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B; }

	/** The most species a kind has on a world. */
	constexpr int32 MaxSpecies = 3;
	/** One species of a kind on a world: its body plan, the share of the kind's cells it holds (its kind's species' shares
	 *  sum to 1), and its size against the kind's own (a group's span or length drawn from the kind's range times this). */
	struct FFaunaSpecies
	{
		FFaunaBodyPlan Plan;
		float Share = 1.f;
		float SizeScale = 1.f;
		bool operator==(const FFaunaSpecies& O) const { return Plan == O.Plan && Share == O.Share && SizeScale == O.SizeScale; }
		bool operator!=(const FFaunaSpecies& O) const { return !(*this == O); }
	};
	/**
	 * A kind's species on a world (1 to MaxSpecies of them, the first the one most like the Earth's). Each habitat cell picks
	 * one by a number of its own (SlonkFauna::SpeciesInCell), the shares laid end to end: a strange world can have six-legged
	 * grazers in one valley and insectoids in the next. At strangeness 0 every kind has one, its EarthPlan.
	 */
	struct FFaunaKindSpecies
	{
		int32 Num = 1;
		FFaunaSpecies List[MaxSpecies];
		/** The species a number 0..1 picks: the one whose stretch of the shares laid end to end holds it (the last for 1). */
		int32 Pick(double U) const
		{
			double Run = 0.0;
			for (int32 i = 0; i + 1 < Num; ++i)
			{
				Run += (double)List[i].Share;
				if (U < Run) { return i; }
			}
			return FMath::Max(Num - 1, 0);
		}
		const FFaunaSpecies& operator[](int32 i) const { return List[FMath::Clamp(i, 0, FMath::Max(Num - 1, 0))]; }
		bool operator==(const FFaunaKindSpecies& O) const
		{
			if (Num != O.Num) { return false; }
			for (int32 i = 0; i < Num; ++i) { if (List[i] != O.List[i]) { return false; } }
			return true;
		}
		bool operator!=(const FFaunaKindSpecies& O) const { return !(*this == O); }
	};

	/** A world's animals: how each kind is put together, and how they look. */
	struct FFaunaProfile
	{
		float Strangeness = 0.f;
		/** The world it was rolled for (its archetype and hash), so it can be rolled again at another strangeness. */
		EPlanetArchetype Type = (EPlanetArchetype)0;
		uint32 Hash = 0;
		/** A seed of the profile's own, which every habitat cell's hash is salted with (with the terrain's seed). */
		uint32 Seed = 0;
		/** Each kind's species (one, the Earth's plan, at strangeness 0). */
		FFaunaKindSpecies Species[NumKinds];
		/** A kind's species' plan (its first's: the one most like the Earth's). */
		const FFaunaBodyPlan& PlanOf(EFaunaKind K, int32 S = 0) const { return Species[(int32)K][S].Plan; }
		FFaunaLook Look;
		/** Whether its air is thick (SlonkFauna::ThickAir), and the share of its floaters' cells holding a jelly
		 *  (0: there are none, as at strangeness 0, under thin air or below Floater::MinStrangeness). */
		bool bThickAir = false;
		float FloaterShare = 0.f;
	};
	/**
	 * A world's fauna from its genome and its hash: the same world always rolls the same, and at strangeness 0 it is the
	 * Earth's (each kind's EarthPlan, the Earth-like look). Draws from a stream of its own, so nothing else a world rolls
	 * moves; every draw is taken whatever the earlier ones gave, so the biosphere phase's plans can follow from them
	 * without moving the seed.
	 */
	SLONK_API FFaunaProfile Roll(const FWorldGenome& G, EPlanetArchetype Type, uint32 Hash);
	/** Whether two profiles are the same in everything they draw, each kind's species among it. */
	SLONK_API bool SameProfile(const FFaunaProfile& A, const FFaunaProfile& B);

	/**
	 * The species of a kind a world rolls, from the kind's own draw of its fauna's stream (its reserved draw, which seeds a
	 * stream of the kind's own with the world's hash: nothing else the world rolls moves) and its strangeness. Each of up to
	 * MaxSpecies species takes the same draws whatever the others gave (whether it is there, whether it departs, its form,
	 * its share, its size and SpeciesShapeDraws for its proportions), so a kind given new forms keeps every other kind's
	 * species where they were. The first species departs from the Earth's plan with DepartChance's chance; a second and a
	 * third are there, each departing, with 0.75 and 0.45 of its square; a departing species takes one of the kind's AlienForms (the
	 * first by its draw, each after it the next of them round), its proportions and its size rolled within the form's
	 * ranges, and each species' share of the cells is its weight (0.4 to 1 by its draw) over all of theirs. The same world
	 * departs at every strangeness past the one it first departs at, in the same forms. At strangeness 0 (and to 0.15), and
	 * for a kind with no alien forms yet, one species: the Earth's plan.
	 */
	SLONK_API FFaunaKindSpecies RollSpecies(EFaunaKind K, float Strangeness, uint32 Hash, float Draw);
	/** The chance a species departs from the Earth's plan on a world this strange: none to about 0.15, a few in a hundred at
	 *  0.2 to 0.3, rising smoothly to 0.95 at 1. */
	SLONK_API float DepartChance(float Strangeness);
	/** How many alien forms a kind can roll: the herds two (six-legged grazers and insectoids), the fliers two (four-winged
	 *  and membrane-winged), the fish two (eels and spinefins); the jellies and the motes none. */
	SLONK_API int32 AlienForms(EFaunaKind K);
	/** The draws each species takes for its proportions. */
	constexpr int32 SpeciesShapeDraws = 8;
	/** A kind's alien plan of one of its forms (0 to AlienForms less 1), its proportions from Shape (SpeciesShapeDraws
	 *  numbers 0..1), its glow organs from the seventh (two in five alien plans of any kind carry them), and its size
	 *  against the kind's own (from the last of them). */
	SLONK_API FFaunaBodyPlan AlienPlan(EFaunaKind K, int32 Form, const float* Shape, float& OutSizeScale);
	/** Whether a kind's plan is its Earth's. */
	SLONK_API bool IsEarthPlan(EFaunaKind K, const FFaunaBodyPlan& P);
	/** A plan in words, for the debug readout ("an Earth grazer on four legs", "an insectoid: ...", ...). */
	SLONK_API FString DescribePlan(EFaunaKind K, const FFaunaBodyPlan& P);

	/**
	 * Whether a world's air is thick: its genome lays a haze in its lowlands (FWorldGenome::LowHazeDensity over 0, which on
	 * a living world is just when its style's Haze stands over its archetype's own). The sky-jellies drift only in thick
	 * air, and only on a world at least Floater::MinStrangeness strange (FloatersLive): never at strangeness 0, so none on
	 * the home worlds or the charted neighbours.
	 */
	SLONK_API bool ThickAir(const FWorldGenome& G);
	SLONK_API bool FloatersLive(const FWorldGenome& G);
	/** The share of a world's floater cells holding a jelly (at full life and density), from its genome and the floaters'
	 *  own draw of its fauna's stream (0..1): Floater::MinShare to MinShare + ShareSpan by the draw squared, three fifths of
	 *  that under the thinnest haze and all of it under one of Floater::HazeFull; 0 where they do not live. */
	SLONK_API float FloaterShareOf(const FWorldGenome& G, float Draw);
	/** A world's jellies' body plan, from its hash on a stream of its own: 6, 8 or 12 ribs, 6 to 12 tentacles, its bell
	 *  Floater::MinBell to MaxBell of its span high, its tentacles 1 to 3 times that long, its rim's scallops cut 4 to 10% of
	 *  its radius deep, its ribs glowing at night. */
	SLONK_API FFaunaBodyPlan FloaterPlan(uint32 Hash);
	/** A jelly's pigment: the look's pale bell colour, turned up to 12 degrees either way by Turn (-1..1) and of its own
	 *  shade Vary (about 0.9 to 1.1). */
	SLONK_API FLinearColor FloaterPigment(const FFaunaLook& L, double Turn, double Vary);

	/**
	 * A flock's pigment where it lives: the living ground's colour there (SlonkScatter::FoliageAt, as the plants are
	 * tinted), as it would read on an Earth's birds at strangeness 0 (the woods' and the meadows' birds a dark brown or
	 * near black of it, the gulls a pale grey, the cliffs' a slate), turned round the wheel and made vivid as the world
	 * grows strange. Vary (about 0.8 to 1.2) is the flock's own shade.
	 */
	SLONK_API FLinearColor FlierPigment(const FFaunaLook& L, const FScatterPalette& Pal, EFlierHabitat H, double Dry, double Vary);
	/** A herd's coat where it grazes: on an Earth at strangeness 0 a grazer's brown, a bison's dark one to a deer's tan by
	 *  Tone (0..1) and paler on drier grass; turned round the wheel and made vivid (the grass's own colour) as the world
	 *  grows strange, as the fliers' are. Vary (about 0.85 to 1.15) is the herd's own shade. */
	SLONK_API FLinearColor HerdPigment(const FFaunaLook& L, const FScatterPalette& Pal, double Dry, double Tone, double Vary);
	/** An insectoid's carapace where it grazes: a beetle's near black to a chitin's amber by Tone (0..1), deeper on wetter
	 *  grass; turned round the wheel and made vivid (the grass's own colour, a little deeper) as the coats are. Vary is the
	 *  herd's own shade. */
	SLONK_API FLinearColor CarapacePigment(const FFaunaLook& L, const FScatterPalette& Pal, double Dry, double Tone, double Vary);
	/** A group of leapers' colour: on an Earth at strangeness 0 a fish's silver-blue, a quarter of it tinted by the sea's
	 *  own colour (SeaColor, its solvent's: the shoals under the surface are painted in its shade); turned round the wheel
	 *  and made vivid (the sea's own colour, as bright as the Earth's) as the world grows strange, as the other animals
	 *  are. Vary (about 0.85 to 1.15) is the group's own shade. */
	SLONK_API FLinearColor LeaperPigment(const FFaunaLook& L, const FLinearColor& SeaColor, double Vary);

	/** What a world's seas carry, as the terrain material paints them: how brightly the plankton glow where the sea is
	 *  stirred (PlanktonGlow: emissive at full stir, at night) and in what colour, and the share of the shoal lattice's
	 *  cells holding a shoal (ShoalDensity, SlonkFauna::ShoalAt). Both 0 on a world whose seas carry no life, which the
	 *  material then leaves exactly as it was. */
	struct FSeaLife
	{
		float PlanktonGlow = 0.f;
		float ShoalDensity = 0.f;
		FLinearColor PlanktonColor = FFaunaLook().Glow;
	};
	/** At full life (an Earth-like or ocean world: APlanetBody::GroundLife 1) the plankton glow this strongly at full
	 *  stir, under the locked exposure that puts sunlit white near 40 (a city's lights are a few), and this share of the
	 *  shoal lattice's cells holds a shoal (about one in every 370 m square of sea). */
	constexpr float PlanktonGlowAtLife = 2.f;
	constexpr float ShoalDensityAtLife = 0.3f;
	/** The seas of a world with this much life on its ground (APlanetBody::GroundLife: 0 dead, airless or molten) and
	 *  this genome: life in any liquid sea, water, ammonia, methane or acid, as much as on the ground; none in seas of
	 *  molten rock or on a world with none. */
	SLONK_API FSeaLife SeaLifeOf(float GroundLife, const FWorldGenome& G);
	/** How much of the sea's milky glow (the plankton's own, from within the water) its surface lets out toward an eye that
	 *  sees it at CosView (the cosine of the way to the eye off the sea's up) from DistCm off, as the terrain material lets
	 *  it out. Where the bloom's 400 m cells, foreshortened by the slant, are squashed under a pixel or so (under 0.35 of the
	 *  footprint the material weighs its bloom by, a distance's 0.0045), it falls as Schlick's Fresnel for water does the
	 *  flatter the sea is seen (the sea mirroring the night sky instead), so the far sea seen edge-on does not even its
	 *  blooms into a bright line at the horizon; it comes back to 1 by 0.85 of the footprint, so every view in which the
	 *  blooms still show lets all of it out, exactly as before. The foam's stirred glow, at the surface, is not dimmed so. */
	SLONK_API float MilkySeenThrough(float CosView, float DistCm);
}
