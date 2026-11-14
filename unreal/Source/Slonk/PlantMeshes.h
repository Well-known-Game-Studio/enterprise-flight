// Slonk - the plants on a living world's ground: procedural meshes built once per process, like the rocks in
// GroundRocks.h. Each stands on its base at the origin and fills a unit height (100 cm), so an instance's scale is
// its size in metres. The vertex colour carries what the foliage material needs: R the part (0 bark or stem,
// 1 foliage), G how far it sways in the wind (0 at the root, 1 at the tips), B a random value per blade, tier or
// leaf cluster for the colour's variation, and A the kind of surface (0 solid, 0.4 a card of broad leaves, 1 a card
// of needles) for the material's leaf masks.

#pragma once

#include "CoreMinimal.h"

class UStaticMesh;

/** What grows on the ground. */
enum class EPlant : uint8
{
	/** A tuft of blades, knee-high on the meadows and along the water. */
	Grass,
	/** A bush of leaf cards on a few stems: the dark dots the terrain material paints, close to. */
	Shrub,
	/** A tree of the warm and temperate lowlands: a trunk, a few branches, a crown of leaf clusters. */
	Broadleaf,
	/** A tree of the cold, the heights and the poor ground: tiers of drooping needle cards on a single stem. */
	Conifer,
	Count
};

namespace SlonkPlants
{
	constexpr int32 NumKinds = (int32)EPlant::Count;
	/** Height each mesh stands to, cm: an instance scale of 12 is a twelve-metre tree. */
	constexpr double UnitCm = 100.0;

	/** The mesh for a kind: three LODs; the trees have a capsule round the trunk for collision. Transient and
	 *  rooted, so it survives PIE sessions; never reference it from anything that gets saved. */
	SLONK_API UStaticMesh* GetMesh(EPlant Kind);
}
