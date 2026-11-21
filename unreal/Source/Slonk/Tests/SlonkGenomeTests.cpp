// Slonk - tests for the world genome, the alien layer over a world's archetype (WorldGenome.h).

#include "Misc/AutomationTest.h"
#include "../WorldGenome.h"
#include "../PlanetBody.h"

#if WITH_DEV_AUTOMATION_TESTS

#ifndef SLONK_TEST_FLAGS
#define SLONK_TEST_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#endif

namespace
{
	const EPlanetArchetype AllKinds[] = { EPlanetArchetype::Terrestrial, EPlanetArchetype::Ocean, EPlanetArchetype::Desert, EPlanetArchetype::Ice,
		EPlanetArchetype::Lava, EPlanetArchetype::GasGiant, EPlanetArchetype::Moon, EPlanetArchetype::Venus, EPlanetArchetype::Mars, EPlanetArchetype::IceGiant };

	bool SameStyle(const FPlanetStyle& A, const FPlanetStyle& B) { return FPlanetStyle::StaticStruct()->CompareScriptStruct(&A, &B, PPF_None); }
	float Luma(const FLinearColor& C) { return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B; }
	bool SameGenome(const FWorldGenome& A, const FWorldGenome& B) { return FWorldGenome::StaticStruct()->CompareScriptStruct(&A, &B, PPF_None); }

	/** A style that is not the defaults, so a change to any colour shows. */
	FPlanetStyle Varied(uint32 Hash)
	{
		FRandomStream R((int32)Hash);
		FPlanetStyle S;
		S.OceanColor = FLinearColor(R.FRand(), R.FRand(), R.FRand());
		S.AtmosphereColor = FLinearColor(R.FRand() * 0.5f, R.FRand() * 0.7f, 1.f);
		S.Haze = R.FRand() * 0.2f;
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkGenomeNeutralTest, "Slonk.Worlds.GenomeNeutral", SLONK_TEST_FLAGS)
bool FSlonkGenomeNeutralTest::RunTest(const FString& Parameters)
{
	// At strangeness 0 a world is exactly what its archetype makes it: the home system and the charted neighbours
	// look as they always have. Every archetype, many seeds.
	int32 Changed = 0, Shifted = 0, Tried = 0;
	for (const EPlanetArchetype Kind : AllKinds)
	{
		for (uint32 Hash = 1; Hash <= 64; ++Hash)
		{
			const FWorldGenome G = SlonkGenome::Roll(Kind, 0.f, Hash * 7919u);
			Shifted += (G.SkyHueShiftDeg != 0.f || G.LowHazeKm > 0.f || G.Solvent != SlonkGenome::NaturalSolvent(Kind)) ? 1 : 0;
			for (const FPlanetStyle& Before : { FPlanetStyle(), Varied(Hash) })
			{
				FPlanetStyle After = Before;
				SlonkGenome::Apply(G, Kind, After);
				Changed += SameStyle(Before, After) ? 0 : 1;
				++Tried;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d styles through a strangeness-0 genome: %d changed, %d genomes off the archetype's own"), Tried, Changed, Shifted));
	TestEqual(TEXT("a strangeness-0 genome changes no style"), Changed, 0);
	TestEqual(TEXT("a strangeness-0 genome is the archetype's own"), Shifted, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkGenomeRollsTest, "Slonk.Worlds.GenomeRolls", SLONK_TEST_FLAGS)
bool FSlonkGenomeRollsTest::RunTest(const FString& Parameters)
{
	// The same world rolls the same genome; strange worlds spread over seas, skies and hazes, and stranger ones
	// depart further; what the genome makes is a colour the sky and the sea can use; giants and moons keep theirs.
	const int32 N = 400;
	TMap<ESolvent, int32> Seas;
	int32 BigTurns = 0, Hazy = 0, Bad = 0, Unstable = 0, FarAtLow = 0, Glare = 0;
	double TurnLow = 0.0, TurnHigh = 0.0; int32 AlienLow = 0, AlienHigh = 0;
	for (int32 i = 0; i < N; ++i)
	{
		const uint32 Hash = 2654435761u * (uint32)(i + 1);
		const FWorldGenome G = SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 1.f, Hash);
		Unstable += SameGenome(G, SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 1.f, Hash)) ? 0 : 1;
		Seas.FindOrAdd(G.Solvent)++;
		BigTurns += FMath::Abs(G.SkyHueShiftDeg) > 60.f ? 1 : 0;
		Hazy += G.LowHazeKm > 0.f ? 1 : 0;
		// What the genome makes, on every archetype it changes (the ice worlds' seas and the dry worlds' dust haze have
		// branches of their own): colours the sky and the sea can use, and a sky no brighter than the archetype's.
		for (const EPlanetArchetype Kind : { EPlanetArchetype::Terrestrial, EPlanetArchetype::Ocean, EPlanetArchetype::Ice, EPlanetArchetype::Desert, EPlanetArchetype::Mars, EPlanetArchetype::Venus, EPlanetArchetype::Lava })
		{
			const FWorldGenome K = SlonkGenome::Roll(Kind, 1.f, Hash);
			const FPlanetStyle Before = Varied(Hash);
			FPlanetStyle S = Before;
			SlonkGenome::Apply(K, Kind, S);
			for (const FLinearColor& C : { S.AtmosphereColor, S.SunsetColor, S.OceanColor, S.ShallowColor, S.HazeColor, S.CloudColor, S.IceColor })
			{
				Bad += (C.R < 0.f || C.G < 0.f || C.B < 0.f || FMath::Max3(C.R, C.G, C.B) <= 0.f || !FMath::IsFinite(C.R + C.G + C.B)) ? 1 : 0;
			}
			Bad += (S.SwellCm <= 0.f || S.Haze < 0.f || S.Haze > 1.f || S.CloudColor.A != Before.CloudColor.A || S.SkyScatterScale < 0.4f || S.SkyScatterScale > 1.5f
				|| (K.LowHazeKm > 0.f && (S.HazeScaleHeightKm < 0.4f || S.HazeScaleHeightKm > 2.f))) ? 1 : 0;
			const float Was = Luma(SlonkGenome::RayleighOf(Before.AtmosphereColor)), Now = Luma(SlonkGenome::RayleighOf(S.AtmosphereColor)) * S.SkyScatterScale / Before.SkyScatterScale;
			Glare += (S.SkyScatterScale > 0.41f && S.SkyScatterScale < 1.49f && FMath::Abs(Now / Was - 1.f) > 0.02f) ? 1 : 0;
		}
		const FWorldGenome Lo = SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 0.3f, Hash), Hi = SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 0.9f, Hash);
		FarAtLow += FMath::Abs(Lo.SkyHueShiftDeg) >= 30.f ? 1 : 0;
		TurnLow += FMath::Abs(Lo.SkyHueShiftDeg); TurnHigh += FMath::Abs(Hi.SkyHueShiftDeg);
		AlienLow += Lo.Solvent != ESolvent::Water ? 1 : 0; AlienHigh += Hi.Solvent != ESolvent::Water ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("%d temperate worlds at strangeness 1: seas water %d, ammonia %d, acid %d, methane %d; %d skies turned past 60 deg; %d hazy"),
		N, Seas.FindRef(ESolvent::Water), Seas.FindRef(ESolvent::Ammonia), Seas.FindRef(ESolvent::Acid), Seas.FindRef(ESolvent::Methane), BigTurns, Hazy));
	AddInfo(FString::Printf(TEXT("Mean sky turn %.0f deg at strangeness 0.3, %.0f at 0.9; alien seas %d and %d"), TurnLow / N, TurnHigh / N, AlienLow, AlienHigh));
	TestEqual(TEXT("the same world rolls the same genome"), Unstable, 0);
	TestTrue(TEXT("strange worlds' seas are of several solvents, water among them"), Seas.Num() >= 4 && Seas.FindRef(ESolvent::Water) > 0);
	TestTrue(TEXT("most strange skies turn well away from blue"), BigTurns > N / 2);
	AddInfo(FString::Printf(TEXT("%d of %d skies at strangeness 0.3 turned far"), FarAtLow, N));
	TestTrue(TEXT("a sky turns far as often as the world is strange (0.3: about 30%)"), FarAtLow > N * 22 / 100 && FarAtLow < N * 38 / 100);
	TestEqual(TEXT("a turned sky scatters no more light than the archetype's"), Glare, 0);
	TestTrue(TEXT("some strange worlds are hazy and some clear"), Hazy > N / 4 && Hazy < N * 9 / 10);
	TestTrue(TEXT("stranger worlds' skies turn further"), TurnHigh > TurnLow * 1.5);
	TestTrue(TEXT("stranger worlds' seas are more often not water"), AlienHigh > AlienLow);
	TestEqual(TEXT("every colour and setting the genome makes is one the sky and the sea can use"), Bad, 0);

	// Giants and moons keep their own looks; a world without air keeps its sky.
	int32 Moved = 0;
	for (const EPlanetArchetype Kind : { EPlanetArchetype::GasGiant, EPlanetArchetype::IceGiant, EPlanetArchetype::Moon })
	{
		for (uint32 Hash = 1; Hash <= 32; ++Hash)
		{
			const FPlanetStyle Before = Varied(Hash);
			FPlanetStyle After = Before;
			SlonkGenome::Apply(SlonkGenome::Roll(Kind, 1.f, Hash), Kind, After);
			Moved += SameStyle(Before, After) ? 0 : 1;
		}
	}
	{
		FPlanetStyle Airless = Varied(5); Airless.bAtmosphere = false;
		FPlanetStyle After = Airless;
		SlonkGenome::Apply(SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 1.f, 5), EPlanetArchetype::Terrestrial, After);
		Moved += SameStyle(Airless, After) ? 0 : 1;
	}
	TestEqual(TEXT("giants, moons and airless worlds keep their looks"), Moved, 0);
	TestFalse(TEXT("the genome does not apply to giants, moons or airless worlds"), SlonkGenome::AppliesTo(EPlanetArchetype::GasGiant, true) || SlonkGenome::AppliesTo(EPlanetArchetype::IceGiant, true)
		|| SlonkGenome::AppliesTo(EPlanetArchetype::Moon, true) || SlonkGenome::AppliesTo(EPlanetArchetype::Terrestrial, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkGenomeStrangenessTest, "Slonk.Worlds.Strangeness", SLONK_TEST_FLAGS)
bool FSlonkGenomeStrangenessTest::RunTest(const FString& Parameters)
{
	// An uncharted star's worlds are stranger the further out it lies, every one between 0 and 1, and about one in
	// five nearly Earth-like however far.
	const int32 N = 1000;
	double Near = 0.0, Far = 0.0; int32 Out = 0, Tame = 0;
	for (int32 i = 0; i < N; ++i)
	{
		const uint32 Hash = 40503u * (uint32)(i + 1) + 17u;
		const float A = SlonkGenome::WorldStrangeness(7.f, Hash), B = SlonkGenome::WorldStrangeness(28.f, Hash);
		Out += (A < 0.f || A > 1.f || B < 0.f || B > 1.f) ? 1 : 0;
		Near += A; Far += B;
		Tame += B < 0.1f ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("Mean strangeness %.2f at 7 ly, %.2f at 28 ly; %d of %d nearly Earth-like at 28 ly"), Near / N, Far / N, Tame, N));
	TestEqual(TEXT("every world's strangeness is between 0 and 1"), Out, 0);
	TestTrue(TEXT("the further stars' worlds are stranger"), Far > Near * 1.8);
	TestTrue(TEXT("about one in five stays nearly Earth-like"), Tame > N / 8 && Tame < N * 3 / 10);
	return true;
}

#endif
