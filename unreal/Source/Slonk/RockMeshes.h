// Slonk - procedurally shaped asteroid meshes, built once per process at runtime (no content assets needed).

#pragma once

#include "CoreMinimal.h"

class UStaticMesh;

namespace SlonkRocks
{
	/** Number of distinct rock shapes available. */
	constexpr int32 NumVariants = 4;

	/** Nominal radius (cm) of the rock meshes: the same 50 cm as the engine sphere, so Radius / 50 is the scale. */
	constexpr float UnitRadius = 50.f;

	/**
	 * A lumpy, dented rock mesh with two LODs and simple sphere collision. Transient and rooted, so it survives
	 * PIE sessions; never reference it from anything that gets saved.
	 */
	SLONK_API UStaticMesh* GetRockMesh(int32 Variant);
}
