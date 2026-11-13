// Slonk - the rocks that lie on a world's ground, procedural meshes built once per process at runtime like the
// asteroids in RockMeshes.h, but broken into the shapes rock takes on a planet. Every mesh fills the same unit box
// (-50..50 cm on each axis, its base at -50), so the scatter sets a rock's size and proportions in its instance
// scale alone, in metres, and knows how deep it buries one.

#pragma once

#include "CoreMinimal.h"

class UStaticMesh;

/** How a ground rock broke and weathered. */
enum class EGroundRock : uint8
{
	/** Fractured: a dozen flat breaks at random angles, sharp edges. Talus, rockfall, crater ejecta. */
	Angular,
	/** Jointed: broken along three near-square planes, corners chipped. Blocks off a cliff of bedded rock. */
	Block,
	/** A bed's thickness: flat top and bottom, broken edges round the sides. Strata and mesa rims. */
	Slab,
	/** Weathered or water-worn: smooth and lumpy. River cobbles, old boulders, regolith on the flats. */
	Rounded,
	Count
};

namespace SlonkGroundRocks
{
	constexpr int32 NumKinds = (int32)EGroundRock::Count;
	/** Edge of the unit box each mesh fills, cm: an instance scale of 2 is a rock 2 m across that axis. */
	constexpr double UnitCm = 100.0;

	/** The mesh for a kind: three LODs and convex collision from the coarsest. Transient and rooted, so it survives
	 *  PIE sessions; never reference it from anything that gets saved. */
	SLONK_API UStaticMesh* GetMesh(EGroundRock Kind);
}
