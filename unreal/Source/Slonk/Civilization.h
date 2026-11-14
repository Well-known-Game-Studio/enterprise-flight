// Slonk - the people of a world: who lives there, how far along they are, what they build and how it shines at
// night. One per inhabited world, rolled from its seed like its style; the settlements (PlanetCities.h) take their
// layout and building shapes from it, the building material its colours and lights, the terrain its night glow.
#pragma once

#include "CoreMinimal.h"

enum class EPlanetArchetype : uint8;

/** How far a people has come: what they build with and what lights their nights. */
enum class ETechAge : uint8
{
	Stone,			// hamlets of huts; firelight
	Iron,			// villages of stone, timber or adobe round a keep; lamps
	Industrial,		// brick towns on a grid, works and chimneys; sodium light
	Modern,			// blocks, towers and suburbs; a landing pad in a city
	Starfaring,		// spires and domes; cool white light
	Count
};

struct FCivilization
{
	/** Anyone at all. A world with nobody of its own can still be settled by the Federation (Colonists()). */
	bool bInhabited = false;
	/** The Federation's settlers, not a native people. */
	bool bColonists = false;
	ETechAge Age = ETechAge::Modern;
	/** 0 built (masonry, concrete, glass, straight lines) .. 1 grown (living shells and pods, curves, no grid). */
	float Organic = 0.f;
	/** The share of their towns built into cliff faces where there are cliffs near: Mesa Verde's ledges for a stone-age
	 *  people, a facade of glass set into the rock for a starfaring one, pods grown on the face for a grown one. */
	float CliffAffinity = 0.f;
	/** The share of the settlement field's town points they live at. */
	float Density = 1.f;
	/** The driest ground (the sample's Wet) they settle; a desert people none at all. */
	float WetMin = 0.3f;
	/** The terrain's own night glow under their towns (its NightLightIntensity): seen from orbit. */
	float GroundLights = 0.f;
	/** 1 paves and lights a street grid under their towns (the terrain material), 0 lays none. */
	float StreetGrid = 0.f;
	/** Their night lights. */
	FLinearColor LightColor = FLinearColor(1.f, 0.8f, 0.5f);
	/** Wall colours: each building takes one between the two. */
	FLinearColor ToneA = FLinearColor(0.52f, 0.48f, 0.44f);
	FLinearColor ToneB = FLinearColor(0.78f, 0.76f, 0.72f);
	/** The share of windows lit at night, and how bright. */
	float WindowLit = 0.55f;
	float WindowGlow = 12.f;

	bool IsOrganic() const { return Organic > 0.5f; }
	/** "stone-age", "iron-age", "industrial", "modern", "starfaring". */
	FString AgeName() const;
	/** A sentence for the science scanner: who they are and how they build. */
	FString Describe() const;
};

namespace SlonkCivs
{
	/** A world's own people, or nobody, from its kind and its seed's hash. bLitByStyle: an Earth-like world the style
	 *  already rolled lit keeps its lights (an industrial people or later); one rolled dark may still be home to a
	 *  people without electric light. Deserts, ocean and ice worlds have their own odds and peoples. */
	SLONK_API FCivilization Natives(EPlanetArchetype Kind, uint32 SeedHash, bool bLitByStyle);
	/** The Federation's settlers on a world with nobody of its own: modern, built, on the grid. */
	SLONK_API FCivilization Colonists();
	/** A people of a given age and style, the rest following from them (for the debug command and the tests);
	 *  bDesert gives adobe walls and no need of wet ground. */
	SLONK_API FCivilization Make(ETechAge Age, float Organic, float CliffAffinity, bool bDesert, uint32 SeedHash = 0);
}
