// Slonk - the plants on a living world's ground: procedural meshes built in code from a life form (FloraTypes.h) and
// cached by it, like the rocks in GroundRocks.h. Each stands on its base at the origin and fills a unit height (100 cm),
// so an instance's scale is its size in metres. The vertex colour carries what the foliage material needs: R the part
// (0 bark or stem, 1 foliage), G how far it sways in the wind (0 at the root, 1 at the tips), B a random value per
// blade, tier, leaf cluster or part for the colour's variation, and A the kind of surface (0 solid, 0.4 a card of broad
// leaves, 1 a card of needles) for the material's leaf masks. The Earth's four plans (a tuft of grass, a bush, a
// broadleaf's crown, a conifer's tiers) are built as they always were; the strange worlds' plans (a stalk under a cap,
// fans, a cluster of tubes, bladders on stalks) are built of solid parts alone, their UV laid as SlonkFlora::Uv says so
// the material can light their glow, for whichever size class the form names: a tuft's handful of triangles among the
// grass, a bush's, a tree's, or a giant's.

#pragma once

#include "CoreMinimal.h"
#include "FloraTypes.h"

class UStaticMesh;
namespace UE::Geometry { class FDynamicMesh3; }

/** The Earth's four plants, and with them the four roles every living world fills (SlonkFlora::Slot, in this order). */
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

	/** The Earth's form of a kind. */
	inline FFloraForm EarthForm(EPlant Kind) { return SlonkFlora::EarthSpecies(FMath::Clamp((int32)Kind, 0, NumKinds - 1)).Form; }

	/** The most triangles a strange form's level of detail may have, by its class: a tuft among the grass is drawn tens
	 *  of thousands of times a chunk, a giant a few times a world. The Earth's four come under their classes' too. The
	 *  last of a tree's or a giant's is its far crown. */
	constexpr int32 MaxTriangles(EFloraClass C, int32 Lod)
	{
		return C == EFloraClass::Grass ? (Lod == 0 ? 96 : Lod == 1 ? 32 : 6)
			: C == EFloraClass::Shrub ? (Lod == 0 ? 280 : Lod == 1 ? 110 : 24)
			: C == EFloraClass::Tree ? (Lod == 0 ? 600 : Lod == 1 ? 230 : Lod == 2 ? 80 : 12)
			: (Lod == 0 ? 1400 : Lod == 1 ? 500 : Lod == 2 ? 140 : 16);
	}

	/**
	 * The mesh for a life form: three LODs, and for a tree or a giant a fourth past a couple of kilometres, its far
	 * crown: its top alone as one solid shape of a handful of triangles, for the far levels of the forest; a tree or a
	 * giant has a capsule round its trunk or its stalk for collision (TrunkOf). Cached by the form (FFloraForm::Hash), so
	 * a world makes one for each of its species and two worlds growing the same form share it. Transient and rooted, so
	 * it survives PIE sessions; never reference it from anything saved. A strange form's is let go once no world holds it
	 * (HoldForms); the Earth's four are kept for good.
	 */
	SLONK_API UStaticMesh* GetMesh(const FFloraForm& Form);
	inline UStaticMesh* GetMesh(EPlant Kind) { return GetMesh(EarthForm(Kind)); }
	/**
	 * The forms a holder (a world's terrain: UPlanetTerrainComponent, its flora's species) draws now, in place of those
	 * it held before. Every cached mesh of a form no holder holds, but the Earth's four, is let go: out of the cache and
	 * the root set, under a name of its own (so a world that comes back makes its form's mesh again under the form's),
	 * and freed by the garbage collector once nothing draws it. With bLetGoNow false (from a holder's own destruction,
	 * in the garbage collector) it only forgets the holder's forms, and the meshes are let go at the next call.
	 */
	SLONK_API void HoldForms(const void* Holder, TConstArrayView<FFloraForm> Forms, bool bLetGoNow = true);
	/** How many meshes are cached now, and whether a form's is. */
	SLONK_API int32 NumCachedMeshes();
	SLONK_API bool IsMeshCached(const FFloraForm& Form);

	/** How many levels of detail a form has: three, and a tree's or a giant's far crown. */
	SLONK_API int32 NumLods(const FFloraForm& Form);
	/** A form's level of detail as built, before the static mesh is made of them: false past its last. Safe off the
	 *  game thread; the same form builds the same mesh every time, on every machine. */
	SLONK_API bool BuildLod(const FFloraForm& Form, int32 Lod, UE::Geometry::FDynamicMesh3& Out);
	/** How wide a form stands at unit scale, cm, across its crown, its cap or its cluster: the Earth's bush 110, which
	 *  the scatter makes as wide as the scrub's dot it stands on (FFloraSpecies::UnitWideCm). */
	SLONK_API double CrownWideCm(const FFloraForm& Form);
	/** How far out from its middle a form's foot reaches at unit scale, cm: its stalk's, or its cluster's or its side
	 *  stalks' where it stands on several (what of it lies within an eighth of its height of the ground). A giant's
	 *  keeps everything else the scatter places out of it (FFloraSpecies::UnitFootCm). */
	SLONK_API double FootRadiusCm(const FFloraForm& Form);
	/** The capsule round a form's trunk or stalk, in the unit mesh's centimetres (an instance's scale makes it the
	 *  plant's): its radius, the length of its straight part and its middle. False for a form the crew walk through
	 *  (a tuft, a bush). */
	struct FTrunk
	{
		float Radius = 0.f;
		float Length = 0.f;
		FVector Centre = FVector::ZeroVector;
	};
	SLONK_API bool TrunkOf(const FFloraForm& Form, FTrunk& Out);

	/** For the tests: a form's far crown (none for a tuft or a bush: false) as built, its triangles, its volume in unit
	 *  meshes (from its lowest point, so an open-bottomed cone counts as closed by its base: positive if wound outward),
	 *  the worst agreement of a face's normal with its corners', and its box against the nearest LOD's. */
	SLONK_API bool FarCrownStats(const FFloraForm& Form, int32& OutTriangles, double& OutVolume, double& OutWorstFacing, FBox& OutBox, FBox& OutNearBox);
	inline bool FarCrownStats(EPlant Kind, int32& OutTriangles, double& OutVolume, double& OutWorstFacing, FBox& OutBox, FBox& OutNearBox)
	{
		return FarCrownStats(EarthForm(Kind), OutTriangles, OutVolume, OutWorstFacing, OutBox, OutNearBox);
	}
	/** For the tests: of a form's LOD, the share of its bark faces (stems, trunks, stalks) and of its foliage faces whose
	 *  winding shows the side its normals point to, and the share of its foliage faces whose front looks up. */
	SLONK_API void FacingShares(const FFloraForm& Form, int32 Lod, double& OutBark, double& OutFoliage, double& OutSkyward);
	inline void FacingShares(EPlant Kind, int32 Lod, double& OutBark, double& OutFoliage, double& OutSkyward) { FacingShares(EarthForm(Kind), Lod, OutBark, OutFoliage, OutSkyward); }
}
