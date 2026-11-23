// Slonk - the meshes a world's animals are drawn with: built in code once a process and cached by body plan
// (FFaunaBodyPlan::Hash), as the plants' are (PlantMeshes.h). Each is the unit mesh's metre across (a flier's
// wingspan, a walker's length), nose along +x, back up +z, so an instance's scale is its span in metres; three LODs, the
// last a couple of triangles for a speck. The vertex colours follow FaunaTypes.h's contract (SlonkFauna::Vc): a wing's
// green rises from 0 on its hinge, which stands Vc::WingRootCm out from the body's axis, to 1 at its tip, and the material
// beats the wing about that hinge by it; a walker's legs, neck and tail flex from their roots, its legs' blue their place
// in the gait (SlonkFauna::Walker); a fish bends from its middle to its tail's tip as its tail flicks; a sky-jelly is its
// span across and its middle the origin, its bell pulsing from its crown down and its tentacles waving from their roots
// (SlonkFauna::Floater).

#pragma once

#include "CoreMinimal.h"
#include "FaunaTypes.h"

class UStaticMesh;
namespace UE::Geometry { class FDynamicMesh3; }

namespace SlonkFaunaMeshes
{
	constexpr int32 NumLods = 3;
	/** The mesh for a kind's body plan, its LOD screen sizes the kind's (SlonkFauna::FFaunaKindDesc::LodScreenSize):
	 *  nullptr for a plan nothing is built for yet (only the winged fliers', the four-legged walkers', the finned fish's and
	 *  the radial, tentacled jellies' so far: a bird of 60, 12 and 2 triangles, a walker of 150, 30 and 2, a fish of 55, 19
	 *  and 2, a jelly of 308 to 312, 56 to 68 and 16: JellyTriangles). Transient and rooted, so it survives PIE sessions;
	 *  never reference it from anything saved. */
	SLONK_API UStaticMesh* GetMesh(const FFaunaBodyPlan& Plan, EFaunaKind Kind);
	/** A plan's LOD as built, for the tests: false if nothing is built for the plan. */
	SLONK_API bool BuildLod(const FFaunaBodyPlan& Plan, int32 Lod, UE::Geometry::FDynamicMesh3& Out);
	/** How many triangles a jelly of the plan has at a LOD: its bell's (24 sides of a fan and three bands at LOD 0, 12 of a
	 *  fan and one at LOD 1) and its tentacles' (72 quads among them at LOD 0, 12 at LOD 1, at least one each); or at LOD 2
	 *  16 (two crossed planes of a pentagon's 3, its ring canal's strip's 2 and a quad's 2, and a level diamond's 2). */
	inline int32 JellyTriangles(const FFaunaBodyPlan& Plan, int32 Lod)
	{
		if (Lod >= 2) { return 16; }
		const int32 T = SlonkFauna::Floater::TentacleCount(Plan);
		const int32 Quads = FMath::Max(FMath::RoundToInt32((Lod == 0 ? 72.0 : 12.0) / T), 1);
		return (Lod == 0 ? 24 + 3 * 48 : 12 + 24) + 2 * T * Quads;
	}

	/**
	 * The LOD screen sizes of a mote cloud (the engine's: the diameter over the screen's of the sphere round the box an
	 * instance's LOD is picked by). On the terrain's GPU instances (UPlanetInstancesComponent) that box is the mesh's
	 * bounds (its motes' reach included) padded by 15% and 20 cm of the unit mesh (1.35 m at a 6.75 m cloud's scale), and
	 * the sphere round it is 22 m across for a cloud 6.75 m across, which through a 90-degree view at 16:9 keeps its 24
	 * cards out to about 123 m, 8 from there and 2 from about 615 m (a 6 m cloud's about 110 m and 545 m, a 9 m one's
	 * 165 m and 820 m). The engine's own instances (slonk.Scatter.GPUInstances 0) pick it by the bounds alone, about two
	 * thirds the size, and so come down to 8 cards from about 85 m and to 2 from about 425 m. Slonk.Fauna.MoteCloud holds
	 * both paths to these figures.
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
