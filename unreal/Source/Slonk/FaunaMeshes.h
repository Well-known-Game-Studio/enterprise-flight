// Slonk - the meshes a world's animals are drawn with: built in code once a process and cached by body plan
// (FFaunaBodyPlan::Hash), as the plants' are (PlantMeshes.h). Each is the unit mesh's metre across (a flier's
// wingspan), nose along +x, back up +z, so an instance's scale is its span in metres; three LODs, the last a couple of
// triangles for a speck. The vertex colours follow FaunaTypes.h's contract (SlonkFauna::Vc): a wing's green rises from
// 0 on its hinge, which stands Vc::WingRootCm out from the body's axis, to 1 at its tip, and the material beats the
// wing about that hinge by it.

#pragma once

#include "CoreMinimal.h"
#include "FaunaTypes.h"

class UStaticMesh;
namespace UE::Geometry { class FDynamicMesh3; }

namespace SlonkFaunaMeshes
{
	constexpr int32 NumLods = 3;
	/** The mesh for a kind's body plan, its LOD screen sizes the kind's (SlonkFauna::FFaunaKindDesc::LodScreenSize):
	 *  nullptr for a plan nothing is built for yet (only the winged fliers' so far). Transient and rooted, so it
	 *  survives PIE sessions; never reference it from anything saved. */
	SLONK_API UStaticMesh* GetMesh(const FFaunaBodyPlan& Plan, EFaunaKind Kind);
	/** A plan's LOD as built, for the tests: false if nothing is built for the plan. */
	SLONK_API bool BuildLod(const FFaunaBodyPlan& Plan, int32 Lod, UE::Geometry::FDynamicMesh3& Out);
}
