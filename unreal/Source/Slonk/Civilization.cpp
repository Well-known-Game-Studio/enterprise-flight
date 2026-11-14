#include "Civilization.h"
#include "PlanetBody.h"

namespace
{
	/** An age from weights over Stone..Starfaring. */
	ETechAge PickAge(FRandomStream& R, const float (&W)[(int32)ETechAge::Count])
	{
		float Sum = 0.f;
		for (float X : W) { Sum += X; }
		float U = R.FRand() * Sum;
		for (int32 i = 0; i < (int32)ETechAge::Count; ++i)
		{
			if (U < W[i]) { return (ETechAge)i; }
			U -= W[i];
		}
		return ETechAge::Modern;
	}
}

FString FCivilization::AgeName() const
{
	switch (Age)
	{
	case ETechAge::Stone: return TEXT("stone-age");
	case ETechAge::Iron: return TEXT("iron-age");
	case ETechAge::Industrial: return TEXT("industrial");
	case ETechAge::Modern: return TEXT("modern");
	default: return TEXT("starfaring");
	}
}

FString FCivilization::Describe() const
{
	if (!bInhabited) { return TEXT("No inhabitants"); }
	if (bColonists) { return TEXT("Federation colonists: modern towns on a street grid"); }
	FString Who;
	if (IsOrganic())
	{
		Who = Age == ETechAge::Starfaring ? TEXT("A starfaring people whose cities are grown, not built: living towers, shells and pods")
			: TEXT("A modern people who grow their towns: shells and pods along curving ways");
	}
	else
	{
		switch (Age)
		{
		case ETechAge::Stone: Who = TEXT("A stone-age people in hamlets of huts"); break;
		case ETechAge::Iron: Who = TEXT("An iron-age people in villages round a keep"); break;
		case ETechAge::Industrial: Who = TEXT("An industrial people: brick towns on a grid, works and chimneys"); break;
		case ETechAge::Modern: Who = TEXT("A modern people: blocks, towers and suburbs"); break;
		default: Who = TEXT("A starfaring people: spires and domes of white and glass"); break;
		}
	}
	if (CliffAffinity >= 0.5f) { Who += TEXT(", many of their towns built into the cliffs"); }
	else if (CliffAffinity >= 0.25f) { Who += TEXT(", some towns built into the cliffs"); }
	return Who;
}

FCivilization SlonkCivs::Make(ETechAge Age, float Organic, float CliffAffinity, bool bDesert, uint32 SeedHash)
{
	FCivilization C;
	C.bInhabited = true;
	C.Age = Age;
	// Only a people with power and materials science grows its buildings; earlier ones build with what they find.
	C.Organic = Age >= ETechAge::Modern ? FMath::Clamp(Organic, 0.f, 1.f) : 0.f;
	C.CliffAffinity = FMath::Clamp(CliffAffinity, 0.f, 1.f);
	C.WetMin = bDesert ? -1.f : 0.3f;
	switch (Age)
	{
	case ETechAge::Stone:
		C.Density = 0.35f; C.GroundLights = 0.f; C.StreetGrid = 0.f; C.WindowLit = 0.12f; C.WindowGlow = 5.f;
		C.LightColor = FLinearColor(1.0f, 0.42f, 0.1f);		// firelight
		C.ToneA = bDesert ? FLinearColor(0.58f, 0.40f, 0.27f) : FLinearColor(0.40f, 0.33f, 0.24f);
		C.ToneB = bDesert ? FLinearColor(0.70f, 0.52f, 0.36f) : FLinearColor(0.55f, 0.47f, 0.34f);
		break;
	case ETechAge::Iron:
		C.Density = 0.6f; C.GroundLights = 0.f; C.StreetGrid = 0.f; C.WindowLit = 0.25f; C.WindowGlow = 7.f;
		C.LightColor = FLinearColor(1.0f, 0.55f, 0.2f);		// oil lamps
		C.ToneA = bDesert ? FLinearColor(0.62f, 0.45f, 0.30f) : FLinearColor(0.48f, 0.46f, 0.42f);
		C.ToneB = bDesert ? FLinearColor(0.76f, 0.60f, 0.42f) : FLinearColor(0.66f, 0.62f, 0.55f);
		break;
	case ETechAge::Industrial:
		C.Density = 0.85f; C.GroundLights = 4.f; C.StreetGrid = 1.f; C.WindowLit = 0.45f; C.WindowGlow = 10.f;
		C.LightColor = FLinearColor(1.0f, 0.68f, 0.32f);		// sodium and gas
		C.ToneA = FLinearColor(0.42f, 0.20f, 0.14f); C.ToneB = FLinearColor(0.60f, 0.38f, 0.28f);		// brick
		break;
	case ETechAge::Modern:
		C.Density = 1.f; C.GroundLights = 6.f; C.StreetGrid = 1.f; C.WindowLit = 0.55f; C.WindowGlow = 12.f;
		C.LightColor = FLinearColor(1.0f, 0.8f, 0.5f);
		break;
	default:
		C.Density = 0.75f; C.GroundLights = 8.f; C.StreetGrid = 1.f; C.WindowLit = 0.65f; C.WindowGlow = 14.f;
		C.LightColor = FLinearColor(0.82f, 0.9f, 1.0f);
		C.ToneA = FLinearColor(0.78f, 0.80f, 0.84f); C.ToneB = FLinearColor(0.94f, 0.95f, 0.97f);		// white and glass
		break;
	}
	if (C.IsOrganic())
	{
		// Grown: no grid, a softer glow from orbit in a living light (teal, violet or green, from the seed), shells
		// of ivory and moss.
		static const FLinearColor Glows[3] = { FLinearColor(0.3f, 1.0f, 0.8f), FLinearColor(0.7f, 0.45f, 1.0f), FLinearColor(0.5f, 1.0f, 0.4f) };
		C.LightColor = Glows[SeedHash % 3];
		C.GroundLights = 3.f; C.StreetGrid = 0.f; C.WindowLit = 0.6f; C.WindowGlow = 10.f;
		C.Density *= 0.8f;
		C.ToneA = FLinearColor(0.82f, 0.78f, 0.66f); C.ToneB = FLinearColor(0.55f, 0.68f, 0.58f);
	}
	return C;
}

FCivilization SlonkCivs::Colonists()
{
	FCivilization C = Make(ETechAge::Modern, 0.f, 0.1f, false);
	C.bColonists = true;
	return C;
}

FCivilization SlonkCivs::Natives(EPlanetArchetype Kind, uint32 SeedHash, bool bLitByStyle)
{
	// A stream of its own, so nothing the style already rolls moves.
	FRandomStream R((int32)(SeedHash * 53u + 11u));
	float Chance = 0.f;
	float W[(int32)ETechAge::Count] = { 0.f, 0.f, 0.f, 0.f, 0.f };
	float Cliff = 0.f;
	const bool bDesert = Kind == EPlanetArchetype::Desert;
	switch (Kind)
	{
	case EPlanetArchetype::Terrestrial:
		// A world the style lit has an electric people; one it left dark is home to a people before electricity
		// half the time.
		if (bLitByStyle) { Chance = 1.f; W[2] = 0.25f; W[3] = 0.45f; W[4] = 0.30f; }
		else { Chance = 0.5f; W[0] = 0.5f; W[1] = 0.5f; }
		Cliff = R.FRand() < 0.25f ? 0.6f : 0.1f;
		break;
	case EPlanetArchetype::Desert:
		Chance = 0.4f; W[0] = 0.3f; W[1] = 0.35f; W[2] = 0.1f; W[3] = 0.1f; W[4] = 0.15f;
		Cliff = 0.85f;
		break;
	case EPlanetArchetype::Ocean:
		Chance = 0.35f; W[0] = 0.15f; W[1] = 0.25f; W[2] = 0.15f; W[3] = 0.2f; W[4] = 0.25f;
		Cliff = 0.15f;
		break;
	case EPlanetArchetype::Ice:
		Chance = 0.15f; W[1] = 0.4f; W[2] = 0.3f; W[4] = 0.3f;
		Cliff = 0.4f;
		break;
	default:
		return FCivilization();
	}
	// Drawn in a fixed order so each comes out the same whatever the others do.
	const float UInhabited = R.FRand(), UOrganic = R.FRand(), UOrganicAmount = R.FRand();
	if (UInhabited >= Chance) { return FCivilization(); }
	const ETechAge Age = PickAge(R, W);
	const float Organic = Age >= ETechAge::Modern && UOrganic < 0.35f ? 0.75f + 0.25f * UOrganicAmount : 0.f;
	// The later peoples of a desert keep to the cliffs less; a grown one grows into them.
	if (bDesert && Age >= ETechAge::Industrial) { Cliff = Organic > 0.5f ? 0.7f : 0.45f; }
	return Make(Age, Organic, Cliff, bDesert, SeedHash);
}
