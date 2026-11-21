// Slonk - what lives on a world, beside what grows there: how its animals look, and what its seas carry. Rolled from
// the world's genome (WorldGenome.h), so a world's life is the same on every visit, and at strangeness 0 it is the
// Earth-like version's: the blue-green glow of plankton stirred at night, shoals of fish under the surface by day,
// and birds over the woods, the meadows, the coasts and the cliffs. The herds and the drifting things of the later
// phases are rolled here too.

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
		/** How far round the colour wheel its animals' pigments turn from the Earth's, degrees: the sky's turn times the
		 *  strangeness, as the glow's. */
		float HueTurnDeg = 0.f;
		/** How vivid its animals are, 0..1: the Earth's browns and greys at strangeness 0, the living ground's own colour
		 *  at full saturation on the strangest worlds. */
		float Vivid = 0.f;
	};
	/** The look of a world's fauna from its genome: at strangeness 0 the Earth-like look, turned round the wheel by the
	 *  sky's turn times the strangeness as a world grows strange (its own colour, related to its sky's). */
	SLONK_API FFaunaLook LookOf(const FWorldGenome& G);

	/** A world's animals: how each kind is put together, and how they look. */
	struct FFaunaProfile
	{
		float Strangeness = 0.f;
		/** The world it was rolled for (its archetype and hash), so it can be rolled again at another strangeness. */
		EPlanetArchetype Type = (EPlanetArchetype)0;
		uint32 Hash = 0;
		/** A seed of the profile's own, which every habitat cell's hash is salted with (with the terrain's seed). */
		uint32 Seed = 0;
		FFaunaBodyPlan Plans[NumKinds];
		FFaunaLook Look;
	};
	/**
	 * A world's fauna from its genome and its hash: the same world always rolls the same, and at strangeness 0 it is the
	 * Earth's (each kind's EarthPlan, the Earth-like look). Draws from a stream of its own, so nothing else a world rolls
	 * moves; every draw is taken whatever the earlier ones gave, so the biosphere phase's plans can follow from them
	 * without moving the seed.
	 */
	SLONK_API FFaunaProfile Roll(const FWorldGenome& G, EPlanetArchetype Type, uint32 Hash);
	/** Whether two profiles are the same in everything they draw. */
	SLONK_API bool SameProfile(const FFaunaProfile& A, const FFaunaProfile& B);

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
}
