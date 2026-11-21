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

	/**
	 * The LOD screen sizes of a mote cloud (the engine's: the diameter over the screen's of the sphere round the box an
	 * instance's LOD is picked by). On the terrain's GPU instances (UPlanetInstancesComponent) that box is the mesh's
	 * bounds (its motes' reach included) padded by 15% and 20 cm, and the sphere round it is 20 m across for a cloud 6.75 m
	 * across, which through a 90-degree view at 16:9 keeps its 24 cards out to about 110 m, 8 from there and 2 from about
	 * 550 m (a 6 m cloud's about 100 m and 490 m, a 7.5 m one's 125 m and 620 m). The engine's own instances
	 * (slonk.Scatter.GPUInstances 0) pick it by the bounds alone, two thirds the size, and so come down to 8 cards from
	 * about 75 m and to 2 from about 370 m.
	 */
	inline constexpr float MoteLodScreenSize[NumLods] = { 1.f, 0.16f, 0.032f };
	/**
	 * A cloud of motes (SlonkFauna::Mote): the unit mesh's metre across, its ground at z 0 and up +z. LOD 0 is 24 cards,
	 * 18 spread over a disc half a span round and from 0.04 to 0.3 of a span up (0.3 to 2.5 m at a cloud's span), and 6 in
	 * a gnats' swarm about 0.2 of a span up; LOD 1 is 8 cards each standing for three motes, two of them the swarm's; LOD 2
	 * two cards for nine motes each, the swarm left out. Each card is a quad in the xz plane, its corners in its UV, its
	 * front toward -y (U to the right and V up, as the material lays it out turned to the camera, so the camera sees its
	 * front) and its normal up, so the sun lights a speck from above whichever way the material turns it. Its bounds are
	 * grown by Mote::ReachCm each way, so every instance's box holds its motes as they move.
	 */
	SLONK_API void BuildMoteCloudLod(int32 Lod, UE::Geometry::FDynamicMesh3& Out);
	/** The mote cloud's mesh, built once a process (transient and rooted, as GetMesh's). */
	SLONK_API UStaticMesh* GetMoteCloudMesh();
}
