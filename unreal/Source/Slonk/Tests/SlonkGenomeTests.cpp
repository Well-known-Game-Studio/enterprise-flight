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
		// The archetypes' air, from a shallow ice world's to a deep Venus's.
		S.AtmosphereScaleHeightKm = 5.5f + R.FRand() * 14.f;
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
	// What the far shell and the near sky read off a style the genome left alone: the shell its sky colour as it is (no
	// brightness scale, no haze blended in), the sky the Rayleigh colour it always made of it; and the settings the
	// genome moves start at the ones that leave a world as it was (the swell the terrain material's own 120 cm).
	int32 ShellMoved = 0, RayleighMoved = 0;
	for (uint32 Hash = 1; Hash <= 64; ++Hash)
	{
		for (const FPlanetStyle& S : { FPlanetStyle(), Varied(Hash) })
		{
			ShellMoved += SlonkGenome::ShellSkyColor(S) == S.AtmosphereColor ? 0 : 1;
			const FLinearColor C0 = S.AtmosphereColor / FMath::Max(S.AtmosphereColor.GetMax(), KINDA_SMALL_NUMBER);
			const FLinearColor Was(FMath::Max(FMath::Pow(C0.R, 1.6f), 0.08f), FMath::Max(FMath::Pow(C0.G, 1.6f), 0.08f), FMath::Max(FMath::Pow(C0.B, 1.6f), 0.08f));
			RayleighMoved += SlonkGenome::RayleighOf(S.AtmosphereColor).Equals(Was, 1.0e-6f) ? 0 : 1;
		}
	}
	const FPlanetStyle D = FPlanetStyle();
	AddInfo(FString::Printf(TEXT("128 untouched styles: %d shell sky colours moved, %d Rayleigh colours moved"), ShellMoved, RayleighMoved));
	TestEqual(TEXT("the far shell takes an untouched style's sky colour as it is"), ShellMoved, 0);
	TestEqual(TEXT("the near sky scatters an untouched style's colour as it always did"), RayleighMoved, 0);
	TestTrue(TEXT("a style's genome settings start where they change nothing"), D.SkyScatterScale == 1.f && D.SkyShellScale == 1.f && D.HazeScaleHeightKm == 0.f
		&& D.HazeColor == FLinearColor::White && D.SwellCm == 120.f);
	AddInfo(FString::Printf(TEXT("%d styles through a strangeness-0 genome: %d changed, %d genomes off the archetype's own"), Tried, Changed, Shifted));
	TestEqual(TEXT("a strangeness-0 genome changes no style"), Changed, 0);
	TestEqual(TEXT("a strangeness-0 genome is the archetype's own"), Shifted, 0);

	// Nor has it a climate of its own, however hard its star's tides pull, even asked to lock: no lock, no warmth, and
	// the terrain's parameters left exactly as the archetype made them.
	int32 Climates = 0, Moved = 0, ClimateTried = 0;
	FPlanetTerrainParams Earth;
	Earth.Seed = FVector(3.1, 7.7, 1.3);
	Earth.Moisture = 0.55f;
	for (const EPlanetArchetype Kind : AllKinds)
	{
		for (const float Pull : { 0.f, 0.5f, 1.f })
		{
			for (uint32 Hash = 1; Hash <= 64; ++Hash)
			{
				FWorldGenome G = SlonkGenome::Roll(Kind, 0.f, Hash * 7919u, Pull);
				SlonkGenome::SettleLock(G, Kind, FVector(0.6, -0.8, 0.0), true);
				Climates += (G.bTidallyLocked || G.WarmthN != 0.f || G.DayHeat != 0.f || G.TermHeat != 0.f || G.NightCold != 0.f) ? 1 : 0;
				for (const FPlanetTerrainParams& P0 : { FPlanetTerrainParams(), Earth })
				{
					FPlanetTerrainParams P = P0;
					SlonkGenome::ApplyClimate(G, Kind, FVector(0.6, -0.8, 0.0), P);
					Moved += P == P0 && P.Climate == EClimateModel::Classic ? 0 : 1;
					++ClimateTried;
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d terrain parameter sets through a strangeness-0 genome's climate: %d moved, %d genomes with a climate of their own"), ClimateTried, Moved, Climates));
	TestEqual(TEXT("a strangeness-0 genome has no climate of its own"), Climates, 0);
	TestEqual(TEXT("and leaves the terrain's parameters as they were"), Moved, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkGenomeRollsTest, "Slonk.Worlds.GenomeRolls", SLONK_TEST_FLAGS)
bool FSlonkGenomeRollsTest::RunTest(const FString& Parameters)
{
	// The same world rolls the same genome; strange worlds spread over seas, skies and hazes, and stranger ones
	// depart further; what the genome makes is a colour the sky and the sea can use; giants and moons keep theirs.
	const int32 N = 400;
	TMap<ESolvent, int32> Seas;
	int32 BigTurns = 0, Hazy = 0, Bad = 0, Unstable = 0, FarAtLow = 0, Glare = 0, ShellGlare = 0, Unmatched = 0, Turned = 0, Capped = 0;
	double TurnLow = 0.0, TurnHigh = 0.0; int32 AlienLow = 0, AlienHigh = 0, HazyLow = 0, HazyHigh = 0;
	float ScatterLo = 1.f, ShellLo = 1.f;
	for (int32 i = 0; i < N; ++i)
	{
		const uint32 Hash = 2654435761u * (uint32)(i + 1);
		const FWorldGenome G = SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 1.f, Hash);
		Unstable += SameGenome(G, SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 1.f, Hash)) ? 0 : 1;
		Seas.FindOrAdd(G.Solvent)++;
		BigTurns += FMath::Abs(G.SkyHueShiftDeg) > 60.f ? 1 : 0;
		Hazy += G.LowHazeKm > 0.f ? 1 : 0;
		// What the genome makes, on every archetype it changes (the ice worlds' seas and the dry worlds' dust haze have
		// branches of their own): colours the sky and the sea can use, a lowland haze the sky draws as it is, and a sky
		// no brighter than the archetype's, near (the Sky Atmosphere's scattering) or from afar (the shell's glow).
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
			// The sky (SlonkGameMode) draws a lowland haze between 0.2 km and 0.8 of the air's haze height, a quarter of
			// its scale height: the style's is one it draws as it is, never deeper than the genome rolled.
			const float HazeTopKm = FMath::Min(K.LowHazeKm, S.AtmosphereScaleHeightKm * 0.25f * 0.8f);
			Bad += (S.SwellCm <= 0.f || S.Haze < 0.f || S.Haze > 1.f || S.CloudColor.A != Before.CloudColor.A || S.SkyScatterScale <= 0.f || S.SkyScatterScale > 1.5f
				|| S.SkyShellScale <= 0.f || S.SkyShellScale > 1.5f
				|| (K.LowHazeKm > 0.f && (S.HazeScaleHeightKm < 0.2f || S.HazeScaleHeightKm > HazeTopKm * 1.0001f))) ? 1 : 0;
			Capped += (K.LowHazeKm > 0.f && S.HazeScaleHeightKm < K.LowHazeKm) ? 1 : 0;
			// Every turned sky, the ones the brightening cap holds dimmer among them: no brighter than the archetype's,
			// near or from afar, and as bright wherever the cap leaves it be. The shell's sky is weighed without the
			// haze, which tints it whatever the sky's colour.
			if (K.SkyHueShiftDeg != 0.f)
			{
				FPlanetStyle Clear = S; Clear.HazeColor = FLinearColor::White;
				const float Was = Luma(SlonkGenome::RayleighOf(Before.AtmosphereColor)), Now = Luma(SlonkGenome::RayleighOf(S.AtmosphereColor)) * S.SkyScatterScale / Before.SkyScatterScale;
				const float ShellWas = Luma(SlonkGenome::ShellSkyColor(Before)), ShellNow = Luma(SlonkGenome::ShellSkyColor(Clear));
				Glare += Now > Was * 1.02f ? 1 : 0;
				ShellGlare += ShellNow > ShellWas * 1.02f ? 1 : 0;
				Unmatched += ((S.SkyScatterScale < 1.49f && FMath::Abs(Now / Was - 1.f) > 0.02f) || (S.SkyShellScale < 1.49f && FMath::Abs(ShellNow / ShellWas - 1.f) > 0.02f)) ? 1 : 0;
				ScatterLo = FMath::Min(ScatterLo, S.SkyScatterScale); ShellLo = FMath::Min(ShellLo, S.SkyShellScale);
				++Turned;
			}
		}
		const FWorldGenome Lo = SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 0.3f, Hash), Hi = SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 0.9f, Hash);
		FarAtLow += FMath::Abs(Lo.SkyHueShiftDeg) >= 30.f ? 1 : 0;
		TurnLow += FMath::Abs(Lo.SkyHueShiftDeg); TurnHigh += FMath::Abs(Hi.SkyHueShiftDeg);
		AlienLow += Lo.Solvent != ESolvent::Water ? 1 : 0; AlienHigh += Hi.Solvent != ESolvent::Water ? 1 : 0;
		HazyLow += Lo.LowHazeKm > 0.f ? 1 : 0; HazyHigh += Hi.LowHazeKm > 0.f ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("%d temperate worlds at strangeness 1: seas water %d, ammonia %d, acid %d, methane %d; %d skies turned past 60 deg; %d hazy"),
		N, Seas.FindRef(ESolvent::Water), Seas.FindRef(ESolvent::Ammonia), Seas.FindRef(ESolvent::Acid), Seas.FindRef(ESolvent::Methane), BigTurns, Hazy));
	AddInfo(FString::Printf(TEXT("Mean sky turn %.0f deg at strangeness 0.3, %.0f at 0.9; alien seas %d and %d; hazy %d and %d"), TurnLow / N, TurnHigh / N, AlienLow, AlienHigh, HazyLow, HazyHigh));
	AddInfo(FString::Printf(TEXT("%d turned skies: scattering scaled down to %.2f, the shell to %.2f; %d brighter near, %d from afar, %d off the archetype's brightness; %d lowland hazes kept under the air's"),
		Turned, ScatterLo, ShellLo, Glare, ShellGlare, Unmatched, Capped));
	TestEqual(TEXT("the same world rolls the same genome"), Unstable, 0);
	TestTrue(TEXT("strange worlds' seas are of several solvents, water among them"), Seas.Num() >= 4 && Seas.FindRef(ESolvent::Water) > 0);
	TestTrue(TEXT("most strange skies turn well away from blue"), BigTurns > N / 2);
	AddInfo(FString::Printf(TEXT("%d of %d skies at strangeness 0.3 turned far"), FarAtLow, N));
	TestTrue(TEXT("a sky turns far as often as the world is strange (0.3: about 30%)"), FarAtLow > N * 22 / 100 && FarAtLow < N * 38 / 100);
	TestEqual(TEXT("a turned sky scatters no more light than the archetype's"), Glare, 0);
	TestEqual(TEXT("and its far shell glows no brighter than the archetype's"), ShellGlare, 0);
	TestEqual(TEXT("and both are as bright as the archetype's unless that takes brightening by more than half"), Unmatched, 0);
	TestTrue(TEXT("some strange worlds are hazy and some clear"), Hazy > N / 4 && Hazy < N * 9 / 10);
	TestTrue(TEXT("stranger worlds' skies turn further"), TurnHigh > TurnLow * 1.5);
	TestTrue(TEXT("stranger worlds are more often hazy"), HazyHigh > HazyLow * 2);
	TestTrue(TEXT("stranger worlds' seas are more often not water"), AlienHigh > AlienLow);
	TestEqual(TEXT("every colour and setting the genome makes is one the sky and the sea can use"), Bad, 0);

	// The seas, skies and hazes every world rolled before the climate's draws were added after them stay as they were:
	// a CRC of those fields over every archetype, three strangenesses and many hashes, against one taken before.
	{
		uint32 Crc = 0;
		for (const EPlanetArchetype Kind : AllKinds)
		{
			for (const float S : { 0.3f, 0.75f, 1.f })
			{
				for (uint32 i = 1; i <= 200; ++i)
				{
					const FWorldGenome K = SlonkGenome::Roll(Kind, S, 2654435761u * i + 97u);
					const float F[9] = { K.Strangeness, (float)(uint8)K.Solvent, K.SkyHueShiftDeg, K.SkySaturation, K.LowHazeKm, K.LowHazeDensity, K.HazeTint.R, K.HazeTint.G, K.HazeTint.B };
					Crc = FCrc::MemCrc32(F, sizeof(F), Crc);
				}
			}
		}
		AddInfo(FString::Printf(TEXT("seas, skies and hazes rolled: 0x%08xu"), Crc));
		TestEqual(TEXT("the seas, skies and hazes roll as they did before the climate's draws"), Crc, 0x74e8ac52u);
	}

	// The tides: a world with a climate is locked as often as strangeness x (0.15 + 0.7 x pull), the same world always
	// the same way; a world without one (giants, moons, the lava worlds, Venus, Mars) never, even asked to; a world
	// with no star bearing never.
	{
		const int32 M = 4000;
		int32 RateOff = 0, LockUnstable = 0, NeverBroken = 0;
		for (const float S : { 0.4f, 1.f })
		{
			for (const float Pull : { 0.f, 0.5f, 1.f })
			{
				int32 Locked = 0;
				for (int32 i = 0; i < M; ++i)
				{
					const uint32 Hash = 2246822519u * (uint32)(i + 1) + 13u;
					const FWorldGenome K = SlonkGenome::Roll(EPlanetArchetype::Terrestrial, S, Hash, Pull);
					Locked += K.bTidallyLocked ? 1 : 0;
					LockUnstable += SameGenome(K, SlonkGenome::Roll(EPlanetArchetype::Terrestrial, S, Hash, Pull)) ? 0 : 1;
				}
				const float Want = S * (0.15f + 0.7f * Pull), Got = (float)Locked / M;
				AddInfo(FString::Printf(TEXT("strangeness %.1f, tidal pull %.1f: %.3f of temperate worlds locked (%.3f expected)"), S, Pull, Got, Want));
				RateOff += FMath::Abs(Got - Want) < 0.03f ? 0 : 1;
			}
		}
		for (const EPlanetArchetype Kind : { EPlanetArchetype::GasGiant, EPlanetArchetype::IceGiant, EPlanetArchetype::Moon, EPlanetArchetype::Lava, EPlanetArchetype::Venus, EPlanetArchetype::Mars })
		{
			for (uint32 Hash = 1; Hash <= 200; ++Hash)
			{
				FWorldGenome K = SlonkGenome::Roll(Kind, 1.f, Hash * 40503u, 1.f);
				SlonkGenome::SettleLock(K, Kind, FVector::XAxisVector, true);
				FPlanetTerrainParams P;
				SlonkGenome::ApplyClimate(K, Kind, FVector::XAxisVector, P);
				NeverBroken += K.bTidallyLocked || P.Climate != EClimateModel::Classic ? 1 : 0;
			}
		}
		// Forced, with no bearing: never locked; with one, locked.
		FWorldGenome Placed = SlonkGenome::Roll(EPlanetArchetype::Ocean, 0.8f, 99u, 1.f);
		SlonkGenome::SettleLock(Placed, EPlanetArchetype::Ocean, FVector::ZeroVector, true);
		FWorldGenome Forced = SlonkGenome::Roll(EPlanetArchetype::Ocean, 0.8f, 99u, 0.f);
		SlonkGenome::SettleLock(Forced, EPlanetArchetype::Ocean, FVector::YAxisVector, true);
		TestEqual(TEXT("the lock rate is strangeness x (0.15 + 0.7 x pull)"), RateOff, 0);
		TestEqual(TEXT("the same world locks the same way"), LockUnstable, 0);
		TestEqual(TEXT("giants, moons, lava worlds, Venus and Mars never lock or take a climate"), NeverBroken, 0);
		TestFalse(TEXT("a world placed by hand, with no star bearing, is never locked"), Placed.bTidallyLocked);
		TestTrue(TEXT("a forced lock holds a strange world with a bearing"), Forced.bTidallyLocked);
	}

	// The climate laid on a temperate world's terrain (an Earth's 5 km snowline): a locked one about the star's bearing,
	// under the star 0.73 to 0.91 of the liquid range, on the terminator 0.11 to 0.29, the far side 0.2 to 0.6 below
	// freezing; a spinning one warmer or colder by at most 0.12 of it at strangeness 1, about its poles still.
	{
		int32 LockedBad = 0, SpinBad = 0, Locks = 0, Spins = 0;
		for (uint32 i = 1; i <= 400; ++i)
		{
			FPlanetTerrainParams P;
			P.SnowlineCm = 5.0e5;
			const FWorldGenome K = SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 1.f, 2654435761u * i, 1.f);
			const FVector Bearing = FVector(1.0, 2.0, -0.5);
			SlonkGenome::ApplyClimate(K, EPlanetArchetype::Terrestrial, Bearing, P);
			if (K.bTidallyLocked)
			{
				++Locks;
				LockedBad += (P.Climate != EClimateModel::Locked || !P.ClimateAxis.Equals(Bearing.GetSafeNormal(), 1e-12) || P.DayN < 0.73f || P.DayN > 0.911f
					|| P.TermN < 0.113f || P.TermN > 0.293f || P.NightN > -0.19f || P.NightN < -0.61f || P.WarmthN != 0.f) ? 1 : 0;
			}
			else
			{
				++Spins;
				SpinBad += (P.Climate != EClimateModel::Spin || FMath::Abs(P.WarmthN) > 0.12f || P.WarmthN != K.WarmthN || P.ClimateAxis != FVector::ZAxisVector || P.DayN != 0.f) ? 1 : 0;
			}
		}
		AddInfo(FString::Printf(TEXT("400 temperate worlds at strangeness 1 close in: %d locked, %d spinning; %d and %d with a climate out of its ranges"), Locks, Spins, LockedBad, SpinBad));
		TestTrue(TEXT("close in, most strange worlds lock and some spin"), Locks > 300 && Spins > 20);
		TestEqual(TEXT("a locked temperate world's climate is laid about its star at an Earth's numbers"), LockedBad, 0);
		TestEqual(TEXT("a spinning one's is its warmth"), SpinBad, 0);
	}

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
