// Slonk - what lives on a world, beside what grows there: how its animals look, and what its seas carry. Rolled from
// the world's genome (WorldGenome.h), so a world's life is the same on every visit, and at strangeness 0 it is the
// Earth-like version's: the blue-green glow of plankton stirred at night, shoals of fish under the surface by day.
// For now the seas alone; the fliers, the herds and the drifting things of the later phases are rolled here too.

#pragma once

#include "CoreMinimal.h"
#include "WorldGenome.h"

namespace SlonkFauna
{
	/** How a world's animals look. A stub for now: the colour its plankton glow in when the sea is stirred. */
	struct FFaunaLook
	{
		/** Blue-green on an Earth-like world, turned round the colour wheel with a strange world's sky. */
		FLinearColor Glow = FLinearColor(0.08f, 0.7f, 0.85f);
	};
	/** The look of a world's fauna from its genome: at strangeness 0 the Earth-like look, turned round the wheel by the
	 *  sky's turn times the strangeness as a world grows strange (its own colour, related to its sky's). */
	SLONK_API FFaunaLook LookOf(const FWorldGenome& G);

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
