// Slonk - what grows on a world, as a descriptor: a life form is its body plan (how it is put together), the size
// class it is built for, and a handful of proportions, in place of a hard-coded kind of plant. The Earth's grass, bush,
// broadleaf and conifer are four such forms among many; a strange world rolls others in their places and beside them
// (FloraProfile.h), and the meshes are made, and cached, by the form (PlantMeshes.h). Plain data, no objects: the
// scatter reads a world's species off the game thread (TerrainScatter.h).

#pragma once

#include "CoreMinimal.h"

/** The light a world's plants grow under: its star's, by colour. A red dwarf's (M), an orange star's (K), a yellow
 *  one's (G: the Sun's), a white one's (F), and the blue-white of the hot stars and the white dwarfs (A). */
enum class EStarLight : uint8
{
	Red,
	Orange,
	Yellow,
	White,
	Blue,
	Count
};

/** How a life form is put together: the parts its mesh is built of. */
enum class EFloraPlan : uint8
{
	// The Earth's four, built as they always were.
	/** A tuft of blades: grass. */
	Tuft,
	/** Leaf cards on a few stems: a bush. */
	Bush,
	/** A trunk, a few branches and a crown of leaf clusters: a broadleaf. */
	Crown,
	/** Tiers of drooping needle cards on a single stem: a conifer. */
	Tiers,
	// The strange worlds'.
	/** A stalk under a cap turned on a lathe: a mushroom's dome, a parasol, a funnel open to the sky. */
	StalkCap,
	/** Fans spread from a stem, ribbed from their roots to their rims: a sea fan standing in air. */
	Fan,
	/** A cluster of open tubes of unequal heights, each with a lip: organ pipes. */
	Tubes,
	/** Bladders swollen on thin stalks: sacs of gas or water, drawn in at their necks. */
	Bulb,
	Count
};

/** The size class a life form is built for, which sets where the scatter places it and how much of a mesh it is given:
 *  a tuft among the grass, a bush on the scrub's dots, a tree in the forest's stands, or a giant of a hundred metres
 *  and more standing over them. */
enum class EFloraClass : uint8
{
	Grass,
	Shrub,
	Tree,
	Giant,
	Count
};

/** What a life form's skin is, which the foliage material draws it by (SlonkFlora::LookOf): a leaf's, as the Earth's;
 *  a fungus's matt flesh; a membrane the sun shines through; chitin's hard gloss; a crystal's faces. */
enum class EFloraSurface : uint8
{
	Leaf,
	Fungal,
	Membrane,
	Chitin,
	Crystal,
	Count
};

/** Where a life form glows at night, in its mesh's UV (SlonkFlora::Uv): nowhere, as the Earth's; in spots scattered
 *  over it; along its ribs; round its rims and lips; or all of it, faintly. */
enum class EFloraGlow : uint8
{
	None,
	Spots,
	Ribs,
	Rim,
	Body,
	Count
};

/**
 * A life form: what the mesh builder needs and nothing else (two species of one form share a mesh). The proportions run
 * 0 to 1 across each plan's own range; what each means is the plan's (PlantMeshes.h): Spread how wide it stands against
 * its height (a cap's reach, a fan's span, a cluster's foot, a bladder's girth), Stem how much of it is stalk, Arch how
 * its main part curves (a cap from dome through parasol to funnel, a fan from upright to drooping, a tube from straight
 * to flared, a bladder from round to drawn out), Lean how far it leans or twists from the upright. The Earth's four
 * take none of them: they are built as they always were, whatever these hold.
 */
struct FFloraForm
{
	EFloraPlan Plan = EFloraPlan::Crown;
	EFloraClass Class = EFloraClass::Tree;
	/** How many main parts it has (caps up a stalk, fans, tubes, bladders): 0 for the plan's own count. */
	uint8 Parts = 0;
	float Spread = 0.5f;
	float Stem = 0.5f;
	float Arch = 0.5f;
	float Lean = 0.f;
	/** The seed of its small random details (where each part sits, how each differs), so two forms alike in the rest
	 *  are still two plants. */
	uint16 Variant = 0;

	/** Whether it is one of the Earth's four plans, built as they always were. */
	bool IsEarth() const { return Plan == EFloraPlan::Tuft || Plan == EFloraPlan::Bush || Plan == EFloraPlan::Crown || Plan == EFloraPlan::Tiers; }
	/** What its mesh is cached under. An Earth plan's hangs on the plan alone (its proportions build nothing). */
	uint32 Hash() const
	{
		uint32 H = HashCombine(GetTypeHash((uint8)Plan), GetTypeHash((uint8)Class));
		if (IsEarth()) { return H; }
		H = HashCombine(H, GetTypeHash(Parts));
		H = HashCombine(H, GetTypeHash(Spread)); H = HashCombine(H, GetTypeHash(Stem));
		H = HashCombine(H, GetTypeHash(Arch)); H = HashCombine(H, GetTypeHash(Lean));
		return HashCombine(H, GetTypeHash(Variant));
	}
	bool operator==(const FFloraForm& O) const
	{
		if (Plan != O.Plan || Class != O.Class) { return false; }
		return IsEarth() || (Parts == O.Parts && Spread == O.Spread && Stem == O.Stem && Arch == O.Arch && Lean == O.Lean && Variant == O.Variant);
	}
	bool operator!=(const FFloraForm& O) const { return !(*this == O); }
};

/**
 * One species of a world's flora: its form, and what the scatter and the material need of it beside. A slot of a
 * world's flora with nothing in it has bLive false, and nothing reads the rest.
 */
struct FFloraSpecies
{
	bool bLive = false;
	FFloraForm Form;
	/** Its share against the other species of its role (SlonkFlora::Slot: at most two a role), which the scatter picks
	 *  between by stands, so a wood is of one and the next valley's of the other. */
	float Share = 1.f;
	/** Its height against its class's own. A double, so the conifer's 1.15 scales a height exactly as it always did. */
	double Height = 1.0;
	/** A bush's width across its crown at unit scale, cm: the scatter makes it as wide as the scrub's dot it stands on.
	 *  The mesh's (SlonkPlants::CrownWideCm); only a bush's is read. */
	double UnitWideCm = 110.0;
	/** How far out from its middle a giant's foot reaches at unit scale, cm (SlonkPlants::FootRadiusCm): the scatter
	 *  keeps everything else it places out of it. Only a giant's is read. */
	double UnitFootCm = 0.0;
	/** Its foliage's colour as a multiple of the living ground's where it stands (SlonkScatter::FoliageAt), and how
	 *  much of a colour of its own it wears instead (0: the ground's alone, as the Earth's). */
	FLinearColor Tint = FLinearColor::White;
	FLinearColor Own = FLinearColor::Black;
	float OwnShare = 0.f;
	/** Its stems' and trunks' colour: the Earth's bark by default. */
	FLinearColor StemColor = FLinearColor(0.24f, 0.18f, 0.13f);
	EFloraSurface Surface = EFloraSurface::Leaf;
	/** Its glow at night: where, how bright (0: none, as the Earth's) and of what colour. */
	EFloraGlow Glow = EFloraGlow::None;
	float GlowStrength = 0.f;
	FLinearColor GlowColor = FLinearColor::Black;

	/** Whether the foliage material draws it exactly as it draws the Earth's plants: an Earth form with the Earth's
	 *  bark, a leaf's skin and no glow. Such a species needs no material of its own. */
	bool HasEarthLook() const { return Surface == EFloraSurface::Leaf && GlowStrength <= 0.f && StemColor == FLinearColor(0.24f, 0.18f, 0.13f); }
	bool operator==(const FFloraSpecies& O) const
	{
		if (bLive != O.bLive) { return false; }
		if (!bLive) { return true; }
		return Form == O.Form && Share == O.Share && Height == O.Height && UnitWideCm == O.UnitWideCm && UnitFootCm == O.UnitFootCm && Tint == O.Tint && Own == O.Own && OwnShare == O.OwnShare
			&& StemColor == O.StemColor && Surface == O.Surface && Glow == O.Glow && GlowStrength == O.GlowStrength && GlowColor == O.GlowColor;
	}
	bool operator!=(const FFloraSpecies& O) const { return !(*this == O); }
};

namespace SlonkFlora
{
	/** The most species a world grows: each is an instanced set on every chunk it stands on, so the count is capped. */
	constexpr int32 MaxSpecies = 10;
	/**
	 * A world's species by slot. The first four are the roles every living world fills, in the order the Earth's four
	 * plants always had (EPlant: grass, shrub, broadleaf, conifer): its ground cover, its bush, the tree of its warm
	 * lowlands and the tree of its cold, its heights and its poor ground. A strange world may grow another form in a
	 * role, and fill the slots after them: a second species of each role, and one or two giants. A slot's class and
	 * role are the slot's own, whatever is rolled into it.
	 */
	namespace Slot
	{
		enum : int32 { Cover = 0, Bush = 1, Warm = 2, Cold = 3, Warm2 = 4, Cold2 = 5, Giant = 6, Giant2 = 7, Cover2 = 8, Bush2 = 9 };
	}
	/** The first species of a slot's role, and the second (itself, for a first). */
	constexpr int32 FirstOfRole(int32 S) { return S == Slot::Warm2 ? Slot::Warm : S == Slot::Cold2 ? Slot::Cold : S == Slot::Giant2 ? Slot::Giant : S == Slot::Cover2 ? Slot::Cover : S == Slot::Bush2 ? Slot::Bush : S; }
	constexpr int32 SecondOfRole(int32 S)
	{
		const int32 F = FirstOfRole(S);
		return F == Slot::Warm ? Slot::Warm2 : F == Slot::Cold ? Slot::Cold2 : F == Slot::Giant ? Slot::Giant2 : F == Slot::Cover ? Slot::Cover2 : Slot::Bush2;
	}
	/** The size class of a slot's species. */
	constexpr EFloraClass ClassOf(int32 S)
	{
		const int32 F = FirstOfRole(S);
		return F == Slot::Cover ? EFloraClass::Grass : F == Slot::Bush ? EFloraClass::Shrub : F == Slot::Giant ? EFloraClass::Giant : EFloraClass::Tree;
	}

	/**
	 * A life form's mesh in the terms the foliage material reads (tools/planet_materials/foliage_material.py), which the
	 * strange plans' parts all follow so one material draws them and lights their glow. The vertex colour is the
	 * plants' own (PlantMeshes.h): R the part (0 a stem or a stalk, in the species' stem colour; 1 foliage, in its
	 * tint), G how far it sways (kept small on a giant: the wind moves a tip by 2.5% of the plant's height times its
	 * square), B a random value per part, A the surface (0 solid, as every part of a strange plan is: no cards to
	 * mask). The UV of a strange plan's foliage part is its place on that part: x round it or across it (0 to 1, one
	 * turn or one side to the other), y from its root or its middle (0) to its rim, its lip or its tip (1). The glow is
	 * laid in that UV: spots on a lattice of it, ribs along lines of x, the rim where y nears 1.
	 */
	namespace Uv
	{
		/** The glow's spots: so many across x and so many along y, each a disc of this share of its cell. */
		constexpr float SpotsX = 6.f, SpotsY = 4.f, SpotRadius = 0.28f;
		/** Its ribs: so many lines of x, each this share of the gap between two wide. */
		constexpr float Ribs = 8.f, RibWidth = 0.22f;
		/** Its rim: the last of y, from here to 1. */
		constexpr float RimFrom = 0.82f;
	}
	/** The clock the glow breathes by wraps each hour (the material's Time node, period 3600 s), so every breath is a
	 *  whole number of cycles an hour: between BreathLo and BreathLo + BreathSpan of them, by the plant's seed. */
	constexpr float ClockPeriodSec = 3600.f;
	constexpr float BreathLo = 300.f, BreathSpan = 420.f;
	/** How far a breath dims the glow: from 1 down to this. */
	constexpr float BreathDim = 0.45f;

	/** The Earth's species of a slot: its grass, its bush, its broadleaf (leaves a shade darker than the living ground's
	 *  own colour) and its conifer (needles darker and bluer than leaves, on a tree a little taller); nothing in the slots
	 *  only a strange world fills. */
	inline FFloraSpecies EarthSpecies(int32 S)
	{
		FFloraSpecies Sp;
		switch (S)
		{
		case Slot::Cover: Sp.bLive = true; Sp.Form.Plan = EFloraPlan::Tuft; Sp.Form.Class = EFloraClass::Grass; break;
		case Slot::Bush: Sp.bLive = true; Sp.Form.Plan = EFloraPlan::Bush; Sp.Form.Class = EFloraClass::Shrub; break;
		case Slot::Warm: Sp.bLive = true; Sp.Form.Plan = EFloraPlan::Crown; Sp.Form.Class = EFloraClass::Tree; Sp.Tint = FLinearColor(0.85f, 0.85f, 0.85f); break;
		case Slot::Cold: Sp.bLive = true; Sp.Form.Plan = EFloraPlan::Tiers; Sp.Form.Class = EFloraClass::Tree; Sp.Tint = FLinearColor(0.62f, 0.72f, 0.7f); Sp.Height = 1.15; break;
		default: break;
		}
		return Sp;
	}

	SLONK_API const TCHAR* PlanName(EFloraPlan P);
	SLONK_API const TCHAR* ClassName(EFloraClass C);
	SLONK_API const TCHAR* SurfaceName(EFloraSurface S);
	SLONK_API const TCHAR* GlowName(EFloraGlow G);
	SLONK_API const TCHAR* StarLightName(EStarLight L);
}

/**
 * What grows on a world: its species by slot (SlonkFlora::Slot), and the pigment its vegetation takes. Made as the
 * Earth's (its four plants in the four roles, no pigment of its own), which is what every world near home grows;
 * SlonkFlora::Roll (FloraProfile.h) gives a strange world its own. Plain data: the scatter is handed a copy with each
 * chunk it builds.
 */
struct FFloraProfile
{
	float Strangeness = 0.f;
	/** The world it was rolled for and the light it grows under, so it can be rolled again at another strangeness. */
	uint32 Hash = 0;
	EStarLight Star = EStarLight::Yellow;
	FFloraSpecies Species[SlonkFlora::MaxSpecies];
	/** The colour its vegetation is in place of the archetype's own green, which the living ground, the forest's far
	 *  paint and every plant take together (SlonkFlora::ApplyPigment), and how far it takes it: 0 the archetype's own,
	 *  as the Earth's, 1 the pigment's alone. */
	FLinearColor Pigment = FLinearColor(0.30f, 0.36f, 0.18f);
	float Vivid = 0.f;

	FFloraProfile()
	{
		for (int32 S = 0; S < SlonkFlora::MaxSpecies; ++S) { Species[S] = SlonkFlora::EarthSpecies(S); }
	}
	/** How many species it has. */
	int32 Num() const
	{
		int32 N = 0;
		for (const FFloraSpecies& Sp : Species) { N += Sp.bLive ? 1 : 0; }
		return N;
	}
	/** Whether it has a species of a class beyond the Earth's four roles' (a giant). */
	bool HasGiants() const { return Species[SlonkFlora::Slot::Giant].bLive || Species[SlonkFlora::Slot::Giant2].bLive; }
	/** Whether it grows exactly what the Earth does, in the Earth's own colours. */
	bool IsEarth() const
	{
		if (Vivid != 0.f) { return false; }
		for (int32 S = 0; S < SlonkFlora::MaxSpecies; ++S) { if (Species[S] != SlonkFlora::EarthSpecies(S)) { return false; } }
		return true;
	}
	/** Whether two floras place the same plants with the same data: the same species in the same slots. (The pigment
	 *  reaches the plants through the ground's palette, apart.) */
	bool SameSpecies(const FFloraProfile& O) const
	{
		for (int32 S = 0; S < SlonkFlora::MaxSpecies; ++S) { if (Species[S] != O.Species[S]) { return false; } }
		return true;
	}
};
