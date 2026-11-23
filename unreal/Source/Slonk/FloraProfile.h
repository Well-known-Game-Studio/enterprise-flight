// Slonk - what grows on a world: its species (FloraTypes.h), rolled from the world's genome (WorldGenome.h) and the
// light of its star, so a world's plants are the same on every visit. At strangeness 0, and while a world is only
// faintly strange, it is the Earth's: grass, bushes, broadleaves in the warm lowlands and conifers in the cold. As a
// world grows strange its roles are taken by other life forms (stalks under caps, fans, clusters of tubes, bladders on
// stalks), more species come to stand beside them, giants of a hundred metres and more rise over its forests, its
// vegetation takes the pigment that suits its star's light, and some of it glows at night.

#pragma once

#include "CoreMinimal.h"
#include "FloraTypes.h"
#include "WorldGenome.h"

struct FPlanetStyle;

namespace SlonkFlora
{
	/** The strange plans a role can depart to: SlonkFlora::AlienPlan(0..NumAlienPlans-1). */
	constexpr int32 NumAlienPlans = 4;
	SLONK_API EFloraPlan AlienPlan(int32 Index);
	/** No world under this strangeness grows a giant. */
	constexpr float GiantMinStrangeness = 0.3f;
	/** The draws each slot takes for its form's proportions and its look, whatever comes of them. */
	constexpr int32 SlotShapeDraws = 16;

	/** The chance a role's species departs from the Earth's on a world this strange: none to 0.15, a few in a hundred at
	 *  0.2 to 0.3, rising smoothly to 0.95 at 1. The law the animals depart by (SlonkFauna::DepartChance), so a world's
	 *  plants and its animals grow strange together. */
	SLONK_API float DepartChance(float Strangeness);

	/**
	 * A world's flora from its genome, its hash and its star's light: the same world always rolls the same, and at
	 * strangeness 0 (and to 0.15) it is the Earth's, whatever its star. Draws from a stream of its own, so nothing else a
	 * world rolls moves, and every draw is taken whatever the earlier ones gave: each slot takes the same draws (whether it
	 * is there, whether it departs, its plan, its share, its height, and SlotShapeDraws for its proportions and its look)
	 * whatever the others came to, so a world grown stranger keeps every species it had, in the same form. Each of the
	 * four roles departs from the Earth's plant with DepartChance's chance, to one of the strange plans (the first by the
	 * world's own draw, each role after it the next of them round, so a world's species differ); a second species of a
	 * role is there with a share of its square (always a strange one); a giant with 0.8 of it from GiantMinStrangeness up,
	 * and a second giant with half its square. The pigment is its star's (StarPigment), taken as far as Vivid says:
	 * nothing to 0.15, all of it from 0.6. A strange species' surface, its stem's colour and its glow at night are rolled
	 * with it; a world's glow is of one family of colours, its plankton's (SlonkFauna::LookOf), so its nights are of a
	 * piece. An Earth plant in a strange world's role keeps the Earth's look and wears the world's pigment. The two
	 * trees that make a world's forests (the first of each tree role) stay near the living ground's colour, since the
	 * forest far off is painted in it; the giants, the bushes, the ground cover and a role's second species may wear a
	 * colour of their own.
	 */
	SLONK_API FFloraProfile Roll(const FWorldGenome& G, EPlanetArchetype Type, uint32 Hash, EStarLight Star);

	/**
	 * The pigment vegetation takes under a star's light, by a draw 0..1: it absorbs what its star sends most of, so under
	 * a red dwarf's dim red light it is near-black or a deep purple, taking everything it can; under an orange star dark
	 * olive, bronze or wine; under a yellow one green, or the purple and the gold an Earth might have had; under a white
	 * one blue-green and teal; and under the hot blue-white stars orange and red, throwing back the blue it has too much
	 * of. Each star has three, picked by the draw.
	 */
	SLONK_API FLinearColor StarPigment(EStarLight Star, float Draw);
	/** The light of a star of a colour (AStarBody::StarColor): by how much redder it is than it is blue. */
	SLONK_API EStarLight StarLightOf(const FLinearColor& StarColor);

	/**
	 * Applies a flora's pigment to a style the archetype resolved, after the genome (SlonkGenome::Apply): the living
	 * ground's colour (LandColor), which the ground, the forest's far paint and every plant's tint are taken from, goes
	 * Vivid of the way to the pigment, and the dry ground's (LandColor2) a third as far, so what grows there dries
	 * toward the soil as it always did. Life is how much grows (APlanetBody::GroundLifeOf): a full living world takes all
	 * of it, a desert's sparse scrub half, a dead world none. A flora with no pigment of its own (Vivid 0: every world
	 * near home) changes nothing.
	 */
	SLONK_API void ApplyPigment(const FFloraProfile& F, float Life, FPlanetStyle& S);

	/** How the foliage material draws a species, as the parameters it is given (tools/planet_materials/foliage_material.py;
	 *  the defaults are the Earth's, and draw as the material always drew). */
	struct FFloraLook
	{
		/** The stems' colour (StemColor). */
		FLinearColor Stem = FLinearColor(0.24f, 0.18f, 0.13f);
		/** How glossy the skin is, 0 a leaf's or a fungus's to 1 a crystal's (Gloss), and how much light comes through
		 *  it against a leaf's (Translucence: 1 a leaf, under it flesh and chitin, over it a membrane). */
		float Gloss = 0.f;
		float Translucence = 1.f;
		/** The glow's colour and brightness (GlowColor, Glow: 0 none), and how much of each pattern it is laid in
		 *  (GlowForm: spots, ribs, rim, body). */
		FLinearColor GlowColor = FLinearColor::Black;
		float Glow = 0.f;
		FLinearColor GlowForm = FLinearColor(0.f, 0.f, 0.f, 0.f);
	};
	SLONK_API FFloraLook LookOf(const FFloraSpecies& Sp);

	/** One line for the logs and the debug readout, and one for each species. */
	SLONK_API FString Describe(const FFloraProfile& F);
	SLONK_API FString DescribeSpecies(const FFloraProfile& F, int32 Slot);
}
