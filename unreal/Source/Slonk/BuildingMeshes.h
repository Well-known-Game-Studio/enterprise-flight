// Slonk - the building shapes: unit meshes (a 1 m box about the origin, the base at -0.5) that the town builder scales
// to each lot. Which a town uses comes from its people (Civilization.h): huts and adobe for the early ages, brick works
// for the industrial, blocks and towers for the modern, spires and domes for the starfaring, pods and bulb towers for
// a people who grow their buildings.
#pragma once

#include "CoreMinimal.h"

class UStaticMesh;

enum class EBuildingKind : uint8
{
	Block = 0,		// a flat-roofed box (and, under a metre tall, a city's landing pad)
	Tower,			// three tiers with setbacks
	House,			// a box with a pitched roof
	Hut,			// a round wall under a cone of thatch
	Pueblo,			// flat-roofed adobe, a second storey stepped back: the cliff dwellers' house
	RoundTower,		// a round tower with a wider crown: a keep, a kiva tower on a ledge
	Works,			// a low hall with a tall chimney
	Spire,			// a six-sided taper to a needle
	Dome,			// a half ellipsoid on the ground
	Pod,			// an egg, its foot flattened: grown, not built
	BulbTower,		// three bulbs on a stem, shrinking upward
	Count
};

namespace SlonkBuildings
{
	/** The transient static mesh for a kind, built once; unit size, simple box collision. */
	SLONK_API UStaticMesh* GetMesh(EBuildingKind Kind);
	/** The kind's name, for logs and tests. */
	SLONK_API const TCHAR* KindName(EBuildingKind Kind);
}
