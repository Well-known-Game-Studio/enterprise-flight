// Slonk - the building shapes: unit meshes (a 1 m box about the origin) that the town builder scales to each lot.
#pragma once

#include "CoreMinimal.h"

class UStaticMesh;

enum class EBuildingKind : uint8
{
	Block = 0,		// a flat-roofed box
	Tower,			// three tiers with setbacks
	House,			// a box with a pitched roof
	Count
};

namespace SlonkBuildings
{
	/** The transient static mesh for a kind, built once; unit size, simple box collision. */
	SLONK_API UStaticMesh* GetMesh(EBuildingKind Kind);
}
