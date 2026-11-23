// Slonk - the strange worlds' life forms as meshes: a stalk under a cap, fans, a cluster of tubes, bladders on stalks,
// each for whichever size class a form names (FloraTypes.h). PlantMeshes.h hands them out with the Earth's four and
// caches them; this builds them. Each is laid out at its own proportions and then fitted: stood to the unit height and
// kept between a narrowest and a widest, so every level of detail of a form is the same plant.

#pragma once

#include "CoreMinimal.h"
#include "FloraTypes.h"
#include "PlantMeshes.h"

namespace UE::Geometry { class FDynamicMesh3; }

namespace SlonkStrangePlants
{
	/** A strange form's level of detail as built (0 to 2, and a tree's or a giant's far crown at 3): false for an Earth
	 *  plan, which is not built here. A pure function of the form: safe off the game thread. */
	bool BuildLod(const FFloraForm& Form, int32 Lod, UE::Geometry::FDynamicMesh3& Out);
	/** How wide the form stands at unit scale, cm (SlonkPlants::CrownWideCm). */
	double WideCm(const FFloraForm& Form);
	/** How far out its foot reaches at unit scale, cm (SlonkPlants::FootRadiusCm). */
	double FootCm(const FFloraForm& Form);
	/** The capsule round a tree's or a giant's stalk (SlonkPlants::TrunkOf): false for a tuft or a bush. */
	bool TrunkOf(const FFloraForm& Form, SlonkPlants::FTrunk& Out);
}
