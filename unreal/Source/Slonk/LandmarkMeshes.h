// Slonk - the meshes of a strange world's landmarks (Landmarks.h), grown in code once a process like the rocks on the
// ground (GroundRocks.h) and drawn in the same material, the world's rock laid in the mesh's own frame. Each is made in
// its kind's own measure of 100 cm (an arch's span, a standing stone's height, a ring's width across, a reef tower's
// height over the sea, a mound's height), so the scatter sizes one by its instance scale alone, in metres; each reaches
// some way under its origin, so it can be sunk into sloping ground, or stand on a sea's floor.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Landmarks.h"

class UStaticMesh;

namespace SlonkLandmarks
{
	/** A mesh's own measure, cm: an instance scale of 60 is a landmark 60 m in its kind's measure. */
	constexpr double UnitCm = 100.0;
	constexpr int32 NumLods = 3;
	/** The most triangles a level of detail has. */
	SLONK_API int32 MaxTriangles(int32 Lod);
	/** How far under its origin a kind's mesh reaches, in its own cm: what there is of it to sink. */
	SLONK_API double BelowCm(ELandmark Kind);

	/** One level of a kind's mesh: closed solids wound outward, their normals creased as rock's are. A pure function of
	 *  the kind and the level, safe off the game thread. */
	SLONK_API void BuildLod(ELandmark Kind, int32 Lod, UE::Geometry::FDynamicMesh3& Out);
	/** The convex parts a kind's collision is made of, in the mesh's own cm: an arch's by lengths of its span, so the
	 *  way under it is open; a ring's by lengths of its round, so its middle is; a tower or a mound by each of its own. */
	SLONK_API void CollisionParts(ELandmark Kind, TArray<TArray<FVector>>& OutHulls);

	/** The mesh for a kind: three levels of detail and its convex collision. Transient and rooted, so it survives PIE
	 *  sessions; never reference it from anything that gets saved. Game thread. */
	SLONK_API UStaticMesh* GetMesh(ELandmark Kind);
}
