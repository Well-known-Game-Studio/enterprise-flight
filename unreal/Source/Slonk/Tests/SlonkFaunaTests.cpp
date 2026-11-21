// Slonk - tests for what lives on a world (FaunaProfile.h, FaunaHabitat.h) and the habitat the scatter reads for it
// (SlonkScatter::HabitatAt). Under "Slonk.Fauna."; tools/run_tests.sh runs them headless.

#include "Misc/AutomationTest.h"
#include "../FaunaHabitat.h"
#include "../FaunaProfile.h"
#include "../PlanetBody.h"
#include "../PlanetTerrain.h"
#include "../TerrainScatter.h"
#include "../WorldGenome.h"

#if WITH_DEV_AUTOMATION_TESTS

#ifndef SLONK_TEST_FLAGS
#define SLONK_TEST_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#endif

namespace
{
	const EPlanetArchetype FaunaKinds[] = { EPlanetArchetype::Terrestrial, EPlanetArchetype::Ocean, EPlanetArchetype::Desert, EPlanetArchetype::Ice,
		EPlanetArchetype::Lava, EPlanetArchetype::GasGiant, EPlanetArchetype::Moon, EPlanetArchetype::Venus, EPlanetArchetype::Mars, EPlanetArchetype::IceGiant };

	float HueOf(const FLinearColor& C) { return C.LinearRGBToHSV().R; }
	float HueGap(float A, float B) { const float D = FMath::Abs(FMath::Fmod(A - B + 540.f, 360.f) - 180.f); return D; }

	/**
	 * The terrain material's shoal at a pixel (terrain_material.py, its water block), transliterated in floats as the GPU
	 * runs it, from the pixel's planet-local position on the sea: how far into the shoal's shape it lies before its edge
	 * is frayed (under 1 inside, 0 at its middle), or -1 if the pixel's cell has none. The material's noise-space point is
	 * the chunk origin modulo the period plus the vertex's place, the planet-local point less whole periods, as here.
	 */
	float MaterialShoalShape(const FVector& Local, float Density, float Time)
	{
		const FVector Pn = SlonkScatter::ToNoiseSpace(Local);
		const FVector3f p((float)Pn.X, (float)Pn.Y, (float)Pn.Z);
		const FVector Ud = Local.GetSafeNormal();
		const FVector3f aU(FMath::Abs((float)Ud.X), FMath::Abs((float)Ud.Y), FMath::Abs((float)Ud.Z));
		FVector2f pu;
		if (aU.X >= aU.Y && aU.X >= aU.Z) { pu = FVector2f(p.Y, p.Z); }
		else if (aU.Y >= aU.Z) { pu = FVector2f(p.X, p.Z); }
		else { pu = FVector2f(p.X, p.Y); }
		const float scs = 1048576.f / 52.f;
		const FVector2f sq = pu / scs;
		const FVector2f sfl(FMath::FloorToFloat(sq.X), FMath::FloorToFloat(sq.Y));
		const FVector2f sfr = sq - sfl;
		const int32 scx = ((int32)sfl.X % 52 + 52) % 52, scy = ((int32)sfl.Y % 52 + 52) % 52;
		const uint32 sax = (aU.X >= aU.Y && aU.X >= aU.Z) ? 0u : (aU.Y >= aU.Z ? 1u : 2u);
		uint32 sh = ((uint32)scx * 73856093u) ^ ((uint32)scy * 19349663u) ^ ((41u + sax) * 2654435761u);
		sh = (sh ^ (sh >> 13)) * 0x5bd1e995u; sh ^= sh >> 15;
		if (!((float)(sh & 0xffu) / 255.f < Density)) { return -1.f; }
		uint32 sh2 = (sh ^ 0x9e3779b9u) * 0x85ebca6bu; sh2 ^= sh2 >> 13; sh2 *= 0xc2b2ae35u; sh2 ^= sh2 >> 16;
		const FVector2f sctr = FVector2f(0.4f, 0.4f) + FVector2f((float)((sh >> 8) & 0xffu), (float)((sh >> 16) & 0xffu)) * (0.2f / 255.f);
		const float loopR = 0.08f + 0.07f * (float)((sh >> 24) & 0xffu) / 255.f;
		const float sphase = 6.2831853f * (float)((sh2 >> 8) & 0xffu) / 255.f;
		const float sspeed = 60.f + 90.f * (float)((sh2 >> 16) & 0xffu) / 255.f;
		const float shalf = 0.06f + 0.09f * (float)((sh2 >> 24) & 0x7fu) / 127.f;
		const float sturn = (sh2 >> 31) != 0u ? -1.f : 1.f;
		float ssn, scn;
		FMath::SinCos(&ssn, &scn, sphase + sturn * sspeed / (loopR * scs) * Time);
		const FVector2f sdir = FVector2f(-ssn, scn) * sturn;
		const FVector2f sd = sfr - (sctr + FVector2f(scn, ssn) * loopR);
		const float along = sd.X * sdir.X + sd.Y * sdir.Y, across = sd.Y * sdir.X - sd.X * sdir.Y;
		return FMath::Sqrt(along * along + (across / 0.45f) * (across / 0.45f)) / shalf;
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaPlanktonParamsTest, "Slonk.Fauna.PlanktonParams", SLONK_TEST_FLAGS)
bool FSlonkFaunaPlanktonParamsTest::RunTest(const FString& Parameters)
{
	// What a world's seas carry, as APlanetBody::ApplyTerrainMaterialParams gives the terrain material: life in the seas
	// of the living kinds alone, none on a dead, airless, dry or molten world, which the material leaves as it was.
	int32 Living = 0, Dead = 0, Wrong = 0, Tried = 0;
	for (const EPlanetArchetype Kind : FaunaKinds)
	{
		for (const bool bAir : { true, false })
		{
			for (const float Strange : { 0.f, 0.4f, 1.f })
			{
				for (uint32 Hash = 1; Hash <= 32; ++Hash)
				{
					const float Life = APlanetBody::GroundLifeOf(Kind, bAir);
					const FWorldGenome G = SlonkGenome::Roll(Kind, SlonkGenome::AppliesTo(Kind, bAir) ? Strange : 0.f, Hash * 7919u);
					const SlonkFauna::FSeaLife Sea = SlonkFauna::SeaLifeOf(Life, G);
					const bool bShouldLive = bAir && (Kind == EPlanetArchetype::Terrestrial || Kind == EPlanetArchetype::Ocean);
					const bool bLives = Sea.PlanktonGlow > 0.f && Sea.ShoalDensity > 0.f;
					const bool bNone = Sea.PlanktonGlow == 0.f && Sea.ShoalDensity == 0.f;
					Wrong += bShouldLive ? (bLives ? 0 : 1) : (bNone ? 0 : 1);
					Living += bLives ? 1 : 0;
					Dead += bNone ? 1 : 0;
					++Tried;
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d worlds: %d with living seas, %d without, %d of them wrong"), Tried, Living, Dead, Wrong));
	TestEqual(TEXT("life in the seas of living worlds alone (none dead, airless, dry or molten)"), Wrong, 0);

	// One by one, the kinds the plan names: an ocean lives; a dead, an airless and a lava world do not.
	auto SeaOf = [](EPlanetArchetype Kind, bool bAir) { return SlonkFauna::SeaLifeOf(APlanetBody::GroundLifeOf(Kind, bAir), SlonkGenome::Roll(Kind, 0.f, 12345u)); };
	TestTrue(TEXT("an ocean world's seas glow"), SeaOf(EPlanetArchetype::Ocean, true).PlanktonGlow > 0.f);
	TestTrue(TEXT("an ocean world's seas hold shoals"), SeaOf(EPlanetArchetype::Ocean, true).ShoalDensity > 0.f);
	TestEqual(TEXT("a dead world's seas (an ice world) glow not at all"), SeaOf(EPlanetArchetype::Ice, true).PlanktonGlow, 0.f);
	TestEqual(TEXT("an airless world's seas glow not at all"), SeaOf(EPlanetArchetype::Terrestrial, false).PlanktonGlow, 0.f);
	TestEqual(TEXT("an airless world's seas hold no shoals"), SeaOf(EPlanetArchetype::Terrestrial, false).ShoalDensity, 0.f);
	TestEqual(TEXT("a lava world's seas glow not at all"), SeaOf(EPlanetArchetype::Lava, true).PlanktonGlow, 0.f);
	TestEqual(TEXT("a lava world's seas hold no shoals"), SeaOf(EPlanetArchetype::Lava, true).ShoalDensity, 0.f);
	// Molten seas never live, however much grows round them; nor does any sea of a world where nothing grows.
	FWorldGenome Molten;
	Molten.Solvent = ESolvent::Magma;
	TestEqual(TEXT("seas of molten rock carry nothing"), SlonkFauna::SeaLifeOf(1.f, Molten).PlanktonGlow, 0.f);
	TestEqual(TEXT("a world with no life carries nothing in its seas"), SlonkFauna::SeaLifeOf(0.f, FWorldGenome()).ShoalDensity, 0.f);

	// The glow is blue-green at strangeness 0, and turns round the wheel with a strange world's sky, as bright as ever.
	const FLinearColor Earth = SeaOf(EPlanetArchetype::Terrestrial, true).PlanktonColor;
	AddInfo(FString::Printf(TEXT("strangeness 0: plankton glow %.2f in %.2f %.2f %.2f (hue %.0f deg), shoals in %.2f of the cells"),
		SeaOf(EPlanetArchetype::Terrestrial, true).PlanktonGlow, Earth.R, Earth.G, Earth.B, HueOf(Earth), SeaOf(EPlanetArchetype::Terrestrial, true).ShoalDensity));
	TestTrue(TEXT("blue-green at strangeness 0 (hue 160 to 200 deg)"), HueOf(Earth) >= 160.f && HueOf(Earth) <= 200.f && Earth.R < 0.2f * FMath::Min(Earth.G, Earth.B));
	int32 Turned = 0, TurnWrong = 0, AlienSeas = 0, AlienDead = 0;
	for (uint32 Hash = 1; Hash <= 400; ++Hash)
	{
		const FWorldGenome G = SlonkGenome::Roll(EPlanetArchetype::Ocean, 1.f, Hash * 104729u);
		const SlonkFauna::FSeaLife Sea = SlonkFauna::SeaLifeOf(1.f, G);
		const float Want = G.SkyHueShiftDeg * G.Strangeness;
		if (FMath::Abs(Want) > 30.f) { ++Turned; }
		TurnWrong += (HueGap(HueOf(Sea.PlanktonColor), FMath::Fmod(HueOf(Earth) + Want + 720.f, 360.f)) > 0.5f
			|| !FMath::IsNearlyEqual(Sea.PlanktonColor.GetMax(), Earth.GetMax(), 1e-3f)) ? 1 : 0;
		if (G.Solvent != ESolvent::Water) { ++AlienSeas; AlienDead += Sea.PlanktonGlow > 0.f ? 0 : 1; }
	}
	AddInfo(FString::Printf(TEXT("400 ocean worlds at strangeness 1: %d glows turned past 30 deg (%d off the sky's turn), %d alien seas (%d lifeless)"), Turned, TurnWrong, AlienSeas, AlienDead));
	TestTrue(TEXT("strange worlds' glows turn"), Turned > 100);
	TestEqual(TEXT("a glow turns by the sky's turn times the strangeness, its brightness kept"), TurnWrong, 0);
	TestTrue(TEXT("alien seas are rolled"), AlienSeas > 100);
	TestEqual(TEXT("ammonia, methane and acid seas live as water does"), AlienDead, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaShoalMirrorTest, "Slonk.Fauna.ShoalMirror", SLONK_TEST_FLAGS)
bool FSlonkFaunaShoalMirrorTest::RunTest(const FString& Parameters)
{
	// SlonkFauna::ShoalAt mirrors the terrain material's shoals: the same cells, the same share of them holding one, and
	// the shoal where the material draws it, at any time; a shoal never leaves its cell and swims the way it heads.
	const double R = 6.371e8;
	const float Density = SlonkFauna::ShoalDensityAtLife;
	const double Times[] = { 0.0, 37.25, 1234.5, 86400.0 };
	FRandomStream Rng(4242);
	const int32 N = 4000;
	int32 Held = 0, Inside = 0, Outside = 0, Disagree = 0, Strayed = 0, Followed = 0, Backward = 0, Unstable = 0;
	for (int32 i = 0; i < N; ++i)
	{
		const FVector Local = Rng.GetUnitVector() * R;
		const double T = Times[i % 4];
		SlonkFauna::FShoal S;
		const bool bHas = SlonkFauna::ShoalAt(Local, R, Density, T, S);
		// The same cell as the material's: whether it holds one, from the transliterated hash at the same point.
		const bool bMaterial = MaterialShoalShape(Local, Density, (float)T) >= 0.f;
		Disagree += bHas != bMaterial ? 1 : 0;
		if (!bHas) { continue; }
		++Held;
		SlonkFauna::FShoal Again;
		Unstable += SlonkFauna::ShoalAt(Local, R, Density, T, Again) && Again.CentreCm == S.CentreCm && Again.Heading == S.Heading ? 0 : 1;
		// Through a day and a night its middle stays in its own cell.
		for (double Later = T; Later < T + 86400.0; Later += 4321.0)
		{
			SlonkFauna::FShoal At, There;
			if (!SlonkFauna::ShoalAt(Local, R, Density, Later, At)) { ++Strayed; continue; }
			Strayed += SlonkFauna::ShoalAt(At.CentreCm, R, Density, Later, There) && There.Axis == S.Axis && There.Key == S.Key && There.CentreCm.Equals(At.CentreCm, 1.0) ? 0 : 1;
		}
		// A second later it has swum a metre or so the way it heads.
		SlonkFauna::FShoal Next;
		if (SlonkFauna::ShoalAt(Local, R, Density, T + 1.0, Next))
		{
			const FVector Moved = Next.CentreCm - S.CentreCm;
			++Followed;
			// On the sea a little faster than on the plane it is drawn on, toward the corners of the cube's faces.
			Backward += Moved.Size() > 50.0 && Moved.Size() < 300.0 && FVector::DotProduct(Moved.GetSafeNormal(), S.Heading) > 0.95 ? 0 : 1;
		}
		// Round its middle, the material draws it where the mirror has it: points well inside its shape are inside the
		// material's, points well outside outside (its frayed edge between is left out).
		for (int32 k = 0; k < 24; ++k)
		{
			const FVector Off = S.Heading * (Rng.FRandRange(-2.f, 2.f) * S.HalfLengthCm) + S.Side * (Rng.FRandRange(-2.f, 2.f) * S.HalfWidthCm);
			const FVector Pt = (S.CentreCm + Off).GetSafeNormal() * R;
			const double Mirror = SlonkFauna::ShoalShapeAt(S, Pt);
			const float Material = MaterialShoalShape(Pt, Density, (float)T);
			if (Mirror < 0.8) { ++Inside; Disagree += Material >= 0.f && Material < 1.f ? 0 : 1; }
			else if (Mirror > 1.25) { ++Outside; Disagree += Material < 0.f || Material > 1.f ? 0 : 1; }
		}
	}
	const double Share = (double)Held / N;
	AddInfo(FString::Printf(TEXT("%d points of an Earth's sea: %.3f of them in a cell holding a shoal (%.2f asked)"), N, Share, Density));
	AddInfo(FString::Printf(TEXT("%d points well inside a mirrored shoal and %d well outside: %d disagree with the material; %d strayed from their cells, %d of %d swam other than they head, %d unstable"),
		Inside, Outside, Disagree, Strayed, Backward, Followed, Unstable));
	TestTrue(TEXT("the share of cells holding a shoal is the density asked"), FMath::Abs(Share - Density) < 0.03);
	TestTrue(TEXT("enough points tried in and round the shoals"), Inside > 2000 && Outside > 2000);
	TestEqual(TEXT("the mirror agrees with the material, cell for cell and shape for shape"), Disagree, 0);
	TestEqual(TEXT("a shoal never leaves its cell"), Strayed, 0);
	TestEqual(TEXT("a shoal swims the way it heads, at 0.6 to 1.5 m/s"), Backward, 0);
	TestEqual(TEXT("the same shoal every time it is asked for"), Unstable, 0);
	// No shoals at all where the material is given none.
	SlonkFauna::FShoal None;
	TestFalse(TEXT("no shoals at density 0"), SlonkFauna::ShoalAt(FVector(R, 0.0, 0.0), R, 0.f, 0.0, None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaHabitatTest, "Slonk.Fauna.Habitat", SLONK_TEST_FLAGS)
bool FSlonkFaunaHabitatTest::RunTest(const FString& Parameters)
{
	// SlonkScatter::HabitatAt reads the field as the chunks do: water where the field has it, with its depth, and the
	// ground's regimes, forest and height within their ranges; and on an Earth-like world some of each.
	FPlanetTerrainParams P;
	P.Seed = FVector(3.1, 7.7, 1.3);
	P.Moisture = 0.55f;
	int32 Sea = 0, Deep = 0, Land = 0, Wooded = 0, Level = 0, Steep = 0, OutOfRange = 0, Mismatch = 0;
	const int32 N = 3000;
	for (int32 i = 0; i < N; ++i)
	{
		const double Z = 1.0 - 2.0 * (i + 0.5) / N, Rr = FMath::Sqrt(FMath::Max(1.0 - Z * Z, 0.0)), A = i * UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0));
		const FVector Dir(Rr * FMath::Cos(A), Rr * FMath::Sin(A), Z);
		const SlonkScatter::FHabitat H = SlonkScatter::HabitatAt(P, Dir);
		const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir.GetSafeNormal(), 1000.0);
		Mismatch += H.bWater == S.bWater && H.HeightCm == S.HeightCm ? 0 : 1;
		auto Unit = [](double V) { return V >= 0.0 && V <= 1.0; };
		OutOfRange += Unit(H.Wet) && Unit(H.Flat) && Unit(H.Cliff) && Unit(H.WallFoot) && Unit(H.Forest) && Unit(H.Ice) && H.Alpine >= 0.0 && H.Alpine <= 1.5 && H.DepthCm >= 0.0 ? 0 : 1;
		if (H.bWater) { ++Sea; Deep += H.DepthCm > 1000.0 ? 1 : 0; OutOfRange += H.Forest == 0.0 ? 0 : 1; continue; }
		++Land;
		Wooded += H.Forest > 0.3 ? 1 : 0;
		Level += H.Flat > 0.9 ? 1 : 0;
		Steep += H.Cliff > 0.1 || H.Flat < 0.5 ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("%d directions: %d sea (%d deeper than 10 m), %d land (%d wooded, %d level, %d steep); %d out of range, %d off the field"), N, Sea, Deep, Land, Wooded, Level, Steep, OutOfRange, Mismatch));
	TestEqual(TEXT("the habitat is the field's"), Mismatch, 0);
	TestEqual(TEXT("every habitat within its ranges, no forest at sea"), OutOfRange, 0);
	TestTrue(TEXT("seas with depth, and land"), Sea > N / 10 && Deep > Sea / 2 && Land > N / 10);
	TestTrue(TEXT("forest, level ground and steep ground on the land"), Wooded > 0 && Level > 0 && Steep > 0);
	return true;
}

#endif
