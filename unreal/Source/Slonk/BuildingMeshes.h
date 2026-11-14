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
	Fire,			// a hearth or campfire: a cone of flame the building material lights and flickers
	Footing,		// plain masonry under a building, down to the ground at its low side: a cliff house's, a plinth on a slope
	Count
};

namespace SlonkBuildings
{
	/** The transient static mesh for a kind, built once; unit size. Its simple collision is a convex hull per part of
	 *  the shape (a box, a roof, a band of a lathe), so the crew stand on a dome's curve and walk round a spire's foot,
	 *  not on an invisible box; a fire has none. (M_Building reads the kind's index from custom data 3: the order of
	 *  EBuildingKind is the material's too.) */
	SLONK_API UStaticMesh* GetMesh(EBuildingKind Kind);
	/** The kind's name, for logs and tests. */
	SLONK_API const TCHAR* KindName(EBuildingKind Kind);
	/** The kind's shape as built, for the tests: its enclosed volume (m^3 of the 1 m unit box; positive when its faces
	 *  are wound outward), the worst agreement of a face's winding with the normals at its corners (-1..1; above 0
	 *  when every face shows its outside and shades as it faces) and how many convex parts its collision has. */
	SLONK_API void ShapeStats(EBuildingKind Kind, double& OutVolume, double& OutWorstFacing, int32& OutCollisionParts);
}
