// Slonk - tests for what lives on a world (FaunaProfile.h, FaunaHabitat.h, FaunaMeshes.h) and the habitat the scatter
// reads for it (SlonkScatter::HabitatAt). Under "Slonk.Fauna."; tools/run_tests.sh runs them headless.

#include "Misc/AutomationTest.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "HAL/PlatformTime.h"
#include "NiagaraComponent.h"
#include "StaticMeshResources.h"
#include "UObject/Package.h"
#include "../Civilization.h"
#include "../FaunaHabitat.h"
#include "../FaunaMeshes.h"
#include "../FaunaProfile.h"
#include "../HerdDust.h"
#include "../PlanetBody.h"
#include "../PlanetCities.h"
#include "../PlanetInstancesComponent.h"
#include "../PlanetTerrain.h"
#include "../PlanetTerrainComponent.h"
#include "../TerrainScatter.h"
#include "../WorldGenome.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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
	 * A hand copy: Slonk.Fauna.ShoalMirror ties it to the script by the CRC of the block it copies (MaterialShoalBlock).
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

	/** The block MaterialShoalShape and SlonkFauna::ShoalAt copy, as terrain_material.py has it now: from its
	 *  "if (ShoalDensity" line to its "if (se < 1.3)" line, each line trimmed. Empty if the script or the block is not
	 *  found. */
	FString MaterialShoalBlock()
	{
		FString Py;
		if (!FFileHelper::LoadFileToString(Py, *FPaths::Combine(FPaths::ProjectDir(), TEXT("../tools/planet_materials/terrain_material.py")))) { return FString(); }
		TArray<FString> Lines;
		Py.ParseIntoArrayLines(Lines, false);
		FString Block;
		for (FString& L : Lines)
		{
			L.TrimStartAndEndInline();
			if (Block.IsEmpty() && !L.StartsWith(TEXT("if (ShoalDensity > 0.0"))) { continue; }
			Block += L + TEXT("\n");
			if (L.StartsWith(TEXT("if (se < 1.3)"))) { return Block; }
		}
		return FString();
	}
	/** Its CRC (FCrc::StrCrc32) when MaterialShoalShape and SlonkFauna::ShoalAt were last brought in line with it. */
	constexpr uint32 MaterialShoalBlockCrc = 0x6b319d0au;

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaShipWakeTest, "Slonk.Fauna.ShipWake", SLONK_TEST_FLAGS)
bool FSlonkFaunaShipWakeTest::RunTest(const FString& Parameters)
{
	// A craft stirs the sea (APlanetBody::ShipStirOf, which UpdateShipWake gives the terrain material) by its height over
	// the sea's level, not over the ground under it, and only while it hovers: the shuttle set down on a beach, or a ship
	// hovering over a cliff top whose foot is in the sea far below, stirs nothing.
	const double R = 6.371e8;
	const FVector Up = FVector(0.3, -0.5, 0.8).GetSafeNormal();
	auto StirAt = [&](double OverSeaCm, bool bLanded) { return APlanetBody::ShipStirOf(Up * (R + OverSeaCm), R, bLanded); };
	// The shuttle hovering a metre and a half over the waves, the mothership held 30 m up by its ground clearance, a ship
	// 150 m up and one 250 m up.
	const float Low = StirAt(150.0, false), Held = StirAt(3000.0, false), High = StirAt(15000.0, false), Above = StirAt(25000.0, false);
	// The shuttle set down 1.5 m over a beach two metres above the sea; a ship 60 m over a cliff top 150 m above it.
	const float Landed = StirAt(350.0, true), OverCliff = StirAt(21000.0, false), Under = StirAt(-500.0, false);
	AddInfo(FString::Printf(TEXT("the sea stirred %.3f by the shuttle 1.5 m over it, %.3f by a ship 30 m up, %.3f at 150 m, %.3f at 250 m; %.3f by the shuttle set down on a beach, %.3f by a ship 60 m over a 150 m cliff top"),
		Low, Held, High, Above, Landed, OverCliff));
	TestTrue(TEXT("the lower a craft hovers over the sea, the harder it stirs it"), Low > Held && Held > High && High > 0.f);
	TestTrue(TEXT("a ship held 30 m up stirs it about 0.72"), FMath::IsNearlyEqual(Held, 0.7225f, 1e-3f));
	TestEqual(TEXT("none from 200 m up"), Above, 0.f);
	TestEqual(TEXT("none from a craft set down"), Landed, 0.f);
	TestEqual(TEXT("none from a ship over high ground, however low over it"), OverCliff, 0.f);
	TestEqual(TEXT("none below the sea's level"), Under, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaShoalMirrorTest, "Slonk.Fauna.ShoalMirror", SLONK_TEST_FLAGS)
bool FSlonkFaunaShoalMirrorTest::RunTest(const FString& Parameters)
{
	// SlonkFauna::ShoalAt mirrors the terrain material's shoals, as the float copy of the material's code above has them:
	// the same cells, the same share of them holding one, and the shoal where the material draws it, at any time; a shoal
	// never leaves its cell and swims the way it heads.
	const double R = 6.371e8;
	const float Density = SlonkFauna::ShoalDensityAtLife;
	const double Times[] = { 0.0, 37.25, 1234.5, 86400.0 };
	FRandomStream Rng(4242);
	const int32 N = 4000;
	int32 Held = 0, Inside = 0, Outside = 0, Disagree = 0, Strayed = 0, Followed = 0, Backward = 0, Unstable = 0, Lifted = 0;
	for (int32 i = 0; i < N; ++i)
	{
		const FVector Local = Rng.GetUnitVector() * R;
		const double T = Times[i % 4];
		SlonkFauna::FShoal S;
		const bool bHas = SlonkFauna::ShoalAt(Local, R, Density, T, S);
		// The same cell as the material's: whether it holds one, from the transliterated hash at the same point.
		const bool bMaterial = MaterialShoalShape(Local, Density, (float)T) >= 0.f;
		Disagree += bHas != bMaterial ? 1 : 0;
		// Ten metres over the sea, or thirty down in it, the shoal drawn on the sea there: the lattice lies on the sea.
		for (const double Lift : { 1000.0, -3000.0 })
		{
			SlonkFauna::FShoal L;
			const bool bL = SlonkFauna::ShoalAt(Local * ((R + Lift) / R), R, Density, T, L);
			Lifted += bL == bHas && (!bHas || (L.Axis == S.Axis && L.Key == S.Key && L.CentreCm.Equals(S.CentreCm, 1.0))) ? 0 : 1;
		}
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
	AddInfo(FString::Printf(TEXT("%d points well inside a mirrored shoal and %d well outside: %d disagree with the material's float copy; %d strayed from their cells, %d of %d swam other than they head, %d unstable, %d of %d off the sea found another shoal"),
		Inside, Outside, Disagree, Strayed, Backward, Followed, Unstable, Lifted, 2 * N));
	TestTrue(TEXT("the share of cells holding a shoal is the density asked"), FMath::Abs(Share - Density) < 0.03);
	TestTrue(TEXT("enough points tried in and round the shoals"), Inside > 2000 && Outside > 2000);
	TestEqual(TEXT("the mirror agrees with the material's float copy, cell for cell and shape for shape"), Disagree, 0);
	TestEqual(TEXT("a shoal never leaves its cell"), Strayed, 0);
	TestEqual(TEXT("a shoal swims the way it heads, at 0.6 to 1.5 m/s"), Backward, 0);
	TestEqual(TEXT("the same shoal every time it is asked for"), Unstable, 0);
	TestEqual(TEXT("over the sea or in it, the shoal drawn on the sea there"), Lifted, 0);
	// The float copy is of the material's code as it stands: a change to the block in terrain_material.py changes its CRC
	// and fails here, until the copy and SlonkFauna::ShoalAt are brought in line with it and the CRC is set anew. (What
	// reaches the block, the pixel's noise-space point and the projection's plane, is the material's and taken as given.)
	const FString Block = MaterialShoalBlock();
	const uint32 BlockCrc = FCrc::StrCrc32(*Block);
	AddInfo(FString::Printf(TEXT("terrain_material.py's shoal block: %d characters, CRC 0x%08x (0x%08x when the copy was made)"), Block.Len(), BlockCrc, MaterialShoalBlockCrc));
	TestFalse(TEXT("the shoal block is found in terrain_material.py"), Block.IsEmpty());
	TestTrue(TEXT("the float copy is of the material's shoal block as it stands"), BlockCrc == MaterialShoalBlockCrc);
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
	double MostOffCm = 0.0;
	const int32 N = 3000;
	for (int32 i = 0; i < N; ++i)
	{
		const double Z = 1.0 - 2.0 * (i + 0.5) / N, Rr = FMath::Sqrt(FMath::Max(1.0 - Z * Z, 0.0)), A = i * UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0));
		const FVector Dir(Rr * FMath::Cos(A), Rr * FMath::Sin(A), Z);
		const SlonkScatter::FHabitat H = SlonkScatter::HabitatAt(P, Dir);
		// Near the field at its finest: the detail under ten metres it leaves out moves the ground by under a metre.
		const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir.GetSafeNormal(), 0.0);
		MostOffCm = FMath::Max(MostOffCm, FMath::Abs(H.HeightCm - S.HeightCm));
		Mismatch += H.bWater == S.bWater && FMath::Abs(H.HeightCm - S.HeightCm) < 100.0 ? 0 : 1;
		auto Unit = [](double V) { return V >= 0.0 && V <= 1.0; };
		OutOfRange += Unit(H.Wet) && Unit(H.Flat) && Unit(H.Cliff) && Unit(H.WallFoot) && Unit(H.Forest) && Unit(H.Ice) && H.Alpine >= 0.0 && H.Alpine <= 1.5 && H.DepthCm >= 0.0 ? 0 : 1;
		if (H.bWater) { ++Sea; Deep += H.DepthCm > 1000.0 ? 1 : 0; OutOfRange += H.Forest == 0.0 ? 0 : 1; continue; }
		++Land;
		Wooded += H.Forest > 0.3 ? 1 : 0;
		Level += H.Flat > 0.9 ? 1 : 0;
		Steep += H.Cliff > 0.1 || H.Flat < 0.5 ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("%d directions: %d sea (%d deeper than 10 m), %d land (%d wooded, %d level, %d steep); %d out of range, %d off the field at its finest (%.2f m at most)"), N, Sea, Deep, Land, Wooded, Level, Steep, OutOfRange, Mismatch, MostOffCm / 100.0));
	TestEqual(TEXT("the habitat is the field's, to within a metre of its finest detail"), Mismatch, 0);
	TestEqual(TEXT("every habitat within its ranges, no forest at sea"), OutOfRange, 0);
	TestTrue(TEXT("seas with depth, and land"), Sea > N / 10 && Deep > Sea / 2 && Land > N / 10);
	TestTrue(TEXT("forest, level ground and steep ground on the land"), Wooded > 0 && Level > 0 && Steep > 0);
	return true;
}

namespace
{
	/** An Earth-like world of a given radius, as the terrain tests' EarthLike has it (APlanetBody::MakeTerrainParams's
	 *  temperate world on their rock world's seed). */
	FPlanetTerrainParams FaunaEarthParams(double RadiusKm)
	{
		FPlanetTerrainParams P;
		P.Seed = FVector(3.1, 7.7, 1.3);
		P.Canyons = 0.6f;
		P.Strata = 0.6f;
		P.Mountains = 0.6f;
		P.Moisture = 0.55f;
		P.RadiusCm = RadiusKm * 1.0e5;
		P.ReliefCm = SlonkTerrain::ReliefForRadius(0.0016, P.RadiusCm);
		P.OceanDepthCm = SlonkTerrain::ReliefForRadius(0.0008, P.RadiusCm);
		P.CanyonDepthCm = SlonkTerrain::ScaleWithWorld(1.6e5, P.RadiusCm);
		P.CanyonWidthCm = SlonkTerrain::ScaleWithWorld(1.6e6, P.RadiusCm);
		P.SnowlineCm = 5.0e5;
		return P;
	}

	/** That world's animals at strangeness 0, full of life. */
	SlonkFauna::FFaunaWorld EarthFauna(float Life = 1.f)
	{
		SlonkFauna::FFaunaWorld W;
		W.P = FaunaEarthParams(6371.0);
		W.Profile = SlonkFauna::Roll(SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 0.f, 7919u), EPlanetArchetype::Terrestrial, 7919u);
		W.Life = Life;
		return W;
	}

	FVector Spread(int32 i, int32 N)
	{
		const double Z = 1.0 - 2.0 * (i + 0.5) / N, Rr = FMath::Sqrt(FMath::Max(1.0 - Z * Z, 0.0)), A = i * UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0));
		return FVector(Rr * FMath::Cos(A), Rr * FMath::Sin(A), Z);
	}

	/** The fliers' groups in the cells under N directions spread evenly over the globe. */
	void GroupsOverGlobe(const SlonkFauna::FFaunaWorld& W, int32 N, TArray<SlonkFauna::FFaunaGroup>& Out, int32* OutCells = nullptr)
	{
		Out.Reset();
		TSet<uint64> Seen;
		for (int32 i = 0; i < N; ++i)
		{
			const SlonkFauna::FFaunaCell C = SlonkFauna::CellOf(W.P, EFaunaKind::Flier, Spread(i, N));
			bool bAlready = false;
			Seen.Add(C.Id(), &bAlready);
			if (bAlready) { continue; }
			SlonkFauna::FFaunaGroup G;
			if (SlonkFauna::GroupInCell(W, C, G)) { Out.Add(MoveTemp(G)); }
		}
		if (OutCells) { *OutCells = Seen.Num(); }
	}

	/** A direction a ground distance from another along a bearing. */
	FVector AlongGround(const FVector& From, double Cm, double RadiusCm, double Bearing)
	{
		const FVector Up = From.GetSafeNormal();
		const FVector E = FVector::CrossProduct(FMath::Abs(Up.Z) < 0.95 ? FVector::ZAxisVector : FVector::XAxisVector, Up).GetSafeNormal();
		const FVector N = FVector::CrossProduct(Up, E);
		return (Up + (E * FMath::Cos(Bearing) + N * FMath::Sin(Bearing)) * (Cm / RadiusCm)).GetSafeNormal();
	}

	/** A camera 30 m over the ground at a direction, planet-local. */
	FVector CameraOver(const SlonkFauna::FFaunaWorld& W, const FVector& Dir)
	{
		return Dir.GetSafeNormal() * (W.P.RadiusCm + FMath::Max(SlonkTerrain::SurfaceHeightCm(W.P, Dir.GetSafeNormal()), 0.0) + 3000.0);
	}

	/** The field's scan (made at once, as its task would make it) and selection for a camera: the ids about after it. */
	void ScanAndSelect(SlonkFauna::FFaunaField& F, const FVector& CamLocal, TArray<uint64>& OutActive, double* OutScanMs = nullptr, int32* OutEvaluated = nullptr, int32* OutCells = nullptr)
	{
		if (F.WantsScan(CamLocal.GetSafeNormal()))
		{
			const TSharedRef<SlonkFauna::FFaunaField::FScan, ESPMode::ThreadSafe> S = SlonkFauna::FFaunaField::Scan(F.GetWorld(), CamLocal.GetSafeNormal(), F.ScanRadiusCm(), F.GetCache());
			if (OutScanMs) { *OutScanMs = S->Ms; }
			if (OutEvaluated) { *OutEvaluated = S->Evaluated; }
			if (OutCells) { *OutCells = S->CellIds.Num(); }
			F.Accept(*S);
		}
		TArray<SlonkFauna::FFaunaField::FGroupRef> Spawned;
		TArray<uint64> LetGo;
		F.Select(CamLocal, Spawned, LetGo);
		OutActive.Reset();
		F.GetActive().GetKeys(OutActive);
		OutActive.Sort();
	}

	/** A flock to test with: the nearest to a fixed direction on the Earth-like world. */
	bool TestFlock(const SlonkFauna::FFaunaWorld& W, SlonkFauna::FFaunaGroup& Out)
	{
		const FVector Start = FVector(0.3, 0.8, 0.5).GetSafeNormal();
		return SlonkFauna::FindGroupNear(W, EFaunaKind::Flier, Start, 2.0e6, 1200, Out) || SlonkFauna::FindGroupNear(W, EFaunaKind::Flier, Start, 4.0e7, 4000, Out);
	}

	/** A member's custom floats and a group's custom primitive data, as the game gives the material them. */
	struct FMotionCase
	{
		float Custom[SlonkFauna::Custom::Num] = {};
		float Group[SlonkFauna::GroupData::Num] = {};
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaDeterministicTest, "Slonk.Fauna.Deterministic", SLONK_TEST_FLAGS)
bool FSlonkFaunaDeterministicTest::RunTest(const FString& Parameters)
{
	// A world's animals are pure functions of the world and the place: its profile rolled again is the same (the Earth's
	// at strangeness 0), a cell's group is the same whichever order the cells are read in and from whichever scan, and
	// a group's members are the same every time they are made.
	int32 Rolled = 0, Differ = 0, EarthOff = 0;
	TSet<uint32> Seeds;
	for (const EPlanetArchetype Kind : FaunaKinds)
	{
		for (uint32 Hash = 1; Hash <= 24; ++Hash)
		{
			for (const float S : { 0.f, 0.6f })
			{
				const FWorldGenome G = SlonkGenome::Roll(Kind, S, Hash * 7919u);
				const SlonkFauna::FFaunaProfile A = SlonkFauna::Roll(G, Kind, Hash * 7919u), B = SlonkFauna::Roll(G, Kind, Hash * 7919u);
				Differ += SlonkFauna::SameProfile(A, B) ? 0 : 1;
				Seeds.Add(A.Seed);
				++Rolled;
				if (S == 0.f)
				{
					for (int32 K = 0; K < SlonkFauna::NumKinds; ++K) { EarthOff += A.Plans[K] == SlonkFauna::EarthPlan((EFaunaKind)K) ? 0 : 1; }
					EarthOff += A.Look.HueTurnDeg == 0.f && A.Look.Vivid == 0.f && A.Look.Glow == SlonkFauna::FFaunaLook().Glow ? 0 : 1;
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d profiles rolled twice: %d differ, %d off the Earth's at strangeness 0, %d seeds"), Rolled, Differ, EarthOff, Seeds.Num()));
	TestEqual(TEXT("a profile rolled again is the same"), Differ, 0);
	TestEqual(TEXT("at strangeness 0 every kind is the Earth's, as it looks on the Earth"), EarthOff, 0);
	TestEqual(TEXT("each world its own seed (24 worlds' hashes)"), Seeds.Num(), 24);

	const SlonkFauna::FFaunaWorld W = EarthFauna();
	SlonkFauna::FFaunaGroup G0;
	if (!TestFlock(W, G0)) { AddError(TEXT("no flock found on the Earth-like world")); return false; }
	// The cells round it read forward, backward and shuffled.
	TArray<SlonkFauna::FFaunaCell> Cells;
	SlonkFauna::CellsNear(W.P, EFaunaKind::Flier, G0.Centre, 8.0e5, Cells);
	TArray<TOptional<SlonkFauna::FFaunaGroup>> Forward, Backward, Shuffled;
	Forward.SetNum(Cells.Num());
	Backward.SetNum(Cells.Num());
	Shuffled.SetNum(Cells.Num());
	auto Read = [&](int32 i, TArray<TOptional<SlonkFauna::FFaunaGroup>>& Into)
	{
		SlonkFauna::FFaunaGroup G;
		if (SlonkFauna::GroupInCell(W, Cells[i], G)) { Into[i] = G; }
	};
	for (int32 i = 0; i < Cells.Num(); ++i) { Read(i, Forward); }
	for (int32 i = Cells.Num() - 1; i >= 0; --i) { Read(i, Backward); }
	TArray<int32> Order;
	for (int32 i = 0; i < Cells.Num(); ++i) { Order.Add(i); }
	FRandomStream Rng(77);
	for (int32 i = Order.Num() - 1; i > 0; --i) { Order.Swap(i, Rng.RandRange(0, i)); }
	for (const int32 i : Order) { Read(i, Shuffled); }
	int32 Held = 0, Mismatch = 0;
	auto Same = [](const TOptional<SlonkFauna::FFaunaGroup>& A, const TOptional<SlonkFauna::FFaunaGroup>& B) { return A.IsSet() == B.IsSet() && (!A.IsSet() || A.GetValue() == B.GetValue()); };
	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		Held += Forward[i].IsSet() ? 1 : 0;
		Mismatch += Same(Forward[i], Backward[i]) && Same(Forward[i], Shuffled[i]) ? 0 : 1;
	}
	// Two scans a kilometre and a half apart, one of them from the other's cells: the cells they share hold the same.
	SlonkFauna::FFaunaField F;
	F.SetWorld(W);
	const double Radius = F.ScanRadiusCm();
	const auto S1 = SlonkFauna::FFaunaField::Scan(W, G0.Centre, Radius, nullptr);
	const FVector Moved = AlongGround(G0.Centre, 1.5e5, W.P.RadiusCm, 1.0);
	const auto S2 = SlonkFauna::FFaunaField::Scan(W, Moved, Radius, nullptr);
	TSharedPtr<SlonkFauna::FFaunaField::FCellCache, ESPMode::ThreadSafe> Cache1 = MakeShared<SlonkFauna::FFaunaField::FCellCache, ESPMode::ThreadSafe>();
	for (int32 i = 0; i < S1->CellIds.Num(); ++i) { Cache1->Add(S1->CellIds[i], S1->CellGroups[i]); }
	const auto S3 = SlonkFauna::FFaunaField::Scan(W, Moved, Radius, Cache1);
	int32 Shared = 0, ScanMismatch = 0;
	for (int32 i = 0; i < S2->CellIds.Num(); ++i)
	{
		const SlonkFauna::FFaunaField::FGroupRef* In1 = Cache1->Find(S2->CellIds[i]);
		const SlonkFauna::FFaunaField::FGroupRef& A = S2->CellGroups[i];
		const SlonkFauna::FFaunaField::FGroupRef& C = S3->CellGroups[i];
		ScanMismatch += S3->CellIds[i] == S2->CellIds[i] && A.IsValid() == C.IsValid() && (!A.IsValid() || *A == *C) ? 0 : 1;
		if (!In1) { continue; }
		++Shared;
		ScanMismatch += A.IsValid() == In1->IsValid() && (!A.IsValid() || *A == **In1) ? 0 : 1;
	}
	// Its members, made twice.
	TArray<FTransform> Xa, Xb;
	TArray<float> Ca, Cb;
	SlonkFauna::MembersOf(G0, Xa, Ca);
	SlonkFauna::MembersOf(G0, Xb, Cb);
	bool bMembersSame = Xa.Num() == Xb.Num() && Ca == Cb && Xa.Num() == G0.Members && Ca.Num() == G0.Members * SlonkFauna::Custom::Num;
	for (int32 i = 0; bMembersSame && i < Xa.Num(); ++i) { bMembersSame &= Xa[i].Equals(Xb[i], 0.0); }
	AddInfo(FString::Printf(TEXT("%d cells round a %s flock of %d: %d hold a group, %d read otherwise in another order; scans %.1f km apart share %d of %d cells (%d read anew with the first's cache), %d disagree"),
		Cells.Num(), SlonkFauna::HabitatName(G0.Habitat), G0.Members, Held, Mismatch, 1.5, Shared, S2->CellIds.Num(), S3->Evaluated, ScanMismatch));
	TestEqual(TEXT("a cell holds the same group whatever order the cells are read in"), Mismatch, 0);
	TestEqual(TEXT("scans agree on the cells they share, with or without a cache"), ScanMismatch, 0);
	TestTrue(TEXT("the scans overlap, and the cached one reads only the new cells"), Shared > S2->CellIds.Num() / 2 && S3->Evaluated == S2->CellIds.Num() - Shared);
	TestTrue(TEXT("a group's members are the same every time"), bMembersSame);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaStaysPutTest, "Slonk.Fauna.StaysPut", SLONK_TEST_FLAGS)
bool FSlonkFaunaStaysPutTest::RunTest(const FString& Parameters)
{
	// The groups about a camera are chosen by distance alone, with hysteresis: gone to B and come back to A, the same
	// groups (the same ids, on the same paths) are about A as before, a group revisited is where one never left would be,
	// and one a little past its reach stays until it is a quarter past it while the caps leave room.
	const SlonkFauna::FFaunaWorld W = EarthFauna();
	SlonkFauna::FFaunaGroup G0;
	if (!TestFlock(W, G0)) { AddError(TEXT("no flock found on the Earth-like world")); return false; }
	const double R = W.P.RadiusCm;
	const double Reach = SlonkFauna::KindDesc(EFaunaKind::Flier).ReachCm;
	const FVector CamA = CameraOver(W, G0.Centre);
	const FVector CamB = CameraOver(W, AlongGround(G0.Centre, 3.0e6, R, 2.0));
	SlonkFauna::FFaunaField F, Stayed;
	F.SetWorld(W);
	Stayed.SetWorld(W);
	TArray<uint64> A1, B1, A2, Kept;
	ScanAndSelect(F, CamA, A1);
	ScanAndSelect(Stayed, CamA, Kept);
	TMap<uint64, SlonkFauna::FFaunaGroup> First;
	for (const TPair<uint64, SlonkFauna::FFaunaField::FGroupRef>& A : F.GetActive()) { First.Add(A.Key, *A.Value); }
	ScanAndSelect(F, CamB, B1);
	int32 Overlap = 0;
	for (const uint64 Id : B1) { Overlap += A1.Contains(Id) ? 1 : 0; }
	ScanAndSelect(F, CamA, A2);
	ScanAndSelect(Stayed, CamA, Kept);
	int32 Changed = 0, Moved = 0;
	const double Later = 1234.5;
	for (const TPair<uint64, SlonkFauna::FFaunaField::FGroupRef>& A : F.GetActive())
	{
		const SlonkFauna::FFaunaGroup* Was = First.Find(A.Key);
		const SlonkFauna::FFaunaField::FGroupRef* Never = Stayed.GetActive().Find(A.Key);
		Changed += Was && *Was == *A.Value && Never && **Never == *A.Value ? 0 : 1;
		if (Never) { Moved += SlonkFauna::PathAt(*A.Value, R, Later).PosCm.Equals(SlonkFauna::PathAt(**Never, R, Later).PosCm, 0.0) ? 0 : 1; }
	}
	AddInfo(FString::Printf(TEXT("about A: %d groups; about B, 30 km off: %d (%d of A's); back at A: %d, %d of them changed, %d off where the groups never left are"), A1.Num(), B1.Num(), Overlap, A2.Num(), Changed, Moved));
	TestTrue(TEXT("groups about A, the test's flock among them"), A1.Num() > 0 && A1.Contains(G0.Id));
	TestEqual(TEXT("none of A's about B"), Overlap, 0);
	TestTrue(TEXT("back at A, the same groups"), A2 == A1);
	TestEqual(TEXT("each the same group as before and as one never left"), Changed, 0);
	TestEqual(TEXT("where one never left would be"), Moved, 0);
	TestTrue(TEXT("the same as about a camera that never left"), Kept == A1);

	// The hysteresis, where the caps leave room (a sparse world, a few flocks within reach of any camera): from a flock's
	// middle, a camera 1.1 reaches from it keeps it, 1.3 lets it go; a camera come fresh to 1.1 reaches does not bring
	// it. With the caps full, one past its reach makes room for a nearer one (Slonk.Fauna.Select).
	SlonkFauna::FFaunaWorld Sparse = W;
	Sparse.Density = 0.25f;
	SlonkFauna::FFaunaGroup G1;
	if (!TestFlock(Sparse, G1)) { AddError(TEXT("no flock found on the sparse world")); return false; }
	SlonkFauna::FFaunaField SparseField;
	SparseField.SetWorld(Sparse);
	TArray<uint64> AtG1, Near, Far, Fresh;
	ScanAndSelect(SparseField, CameraOver(Sparse, G1.Centre), AtG1);
	ScanAndSelect(SparseField, CameraOver(Sparse, AlongGround(G1.Centre, 1.1 * Reach, R, 0.7)), Near);
	const int32 NearGroups = SparseField.ActiveGroups(EFaunaKind::Flier);
	ScanAndSelect(SparseField, CameraOver(Sparse, AlongGround(G1.Centre, 1.3 * Reach, R, 0.7)), Far);
	SlonkFauna::FFaunaField Come;
	Come.SetWorld(Sparse);
	ScanAndSelect(Come, CameraOver(Sparse, AlongGround(G1.Centre, 1.1 * Reach, R, 0.7)), Fresh);
	TestTrue(TEXT("on the sparse world, its flock about its middle, and room under the caps past it"), AtG1.Contains(G1.Id) && NearGroups < SlonkFauna::KindDesc(EFaunaKind::Flier).MaxGroups);
	TestTrue(TEXT("a group just past its reach stays"), Near.Contains(G1.Id));
	TestFalse(TEXT("a group a quarter past its reach goes"), Far.Contains(G1.Id));
	TestFalse(TEXT("a group just past its reach does not come"), Fresh.Contains(G1.Id));
	// Too high a camera keeps none.
	TArray<uint64> High;
	ScanAndSelect(F, G0.Centre * (R + SlonkFauna::KindDesc(EFaunaKind::Flier).MaxAltitudeCm * 1.4 + FMath::Max(SlonkTerrain::SurfaceHeightCm(W.P, G0.Centre), 0.0)), High);
	TestEqual(TEXT("none under a camera too high"), High.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaSelectTest, "Slonk.Fauna.Select", SLONK_TEST_FLAGS)
bool FSlonkFaunaSelectTest::RunTest(const FString& Parameters)
{
	// With the caps full, which flocks are about follows from where the camera is, not the way it came: a camera flown
	// in a straight line over a dense wood, 100 m a frame, never has a flock come in where it would be seen (nearer than
	// the fade by its loop) while one past its reach is still about, holding the place it needs. Flocks out of sight make
	// room for nearer ones, and never more than the caps are about.
	SlonkFauna::FFaunaWorld W = EarthFauna();
	// Every cell where the habitat suits fully holds a flock.
	W.Density = 2.f;
	const double R = W.P.RadiusCm;
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Flier);
	// A wood with more flocks within reach of its middle than the caps allow.
	TArray<SlonkFauna::FFaunaGroup> Groups;
	GroupsOverGlobe(W, 400, Groups);
	FVector Start = FVector::ZeroVector;
	int32 Tried = 0, Within = 0;
	for (const SlonkFauna::FFaunaGroup& G : Groups)
	{
		if (Tried >= 12 || Within >= D.MaxGroups + 6) { break; }
		if (G.Habitat != EFlierHabitat::Woodland) { continue; }
		++Tried;
		SlonkFauna::FFaunaField F;
		F.SetWorld(W);
		TArray<uint64> Active;
		ScanAndSelect(F, CameraOver(W, G.Centre), Active);
		int32 N = 0;
		for (const SlonkFauna::FFaunaField::FGroupRef& C : F.GetCandidates()) { N += FVector::Dist(G.Centre, C->Centre) * R < D.ReachCm ? 1 : 0; }
		if (N > Within) { Within = N; Start = G.Centre; }
	}
	if (Start.IsZero()) { AddError(TEXT("no wood found on the Earth-like world")); return false; }
	// Across it and on, 30 km.
	SlonkFauna::FFaunaField F;
	F.SetWorld(W);
	constexpr int32 Steps = 300;
	int32 Spawns = 0, InSight = 0, Bad = 0, FirstBad = -1, MadeRoom = 0, SeenGo = 0, Full = 0, Over = 0;
	for (int32 s = 0; s <= Steps; ++s)
	{
		const FVector Cam = CameraOver(W, AlongGround(Start, 1.0e4 * s, R, 0.9));
		const FVector CamDir = Cam.GetSafeNormal();
		if (F.WantsScan(CamDir)) { F.Accept(*SlonkFauna::FFaunaField::Scan(W, CamDir, F.ScanRadiusCm(), F.GetCache())); }
		const TMap<uint64, SlonkFauna::FFaunaField::FGroupRef> Was = F.GetActive();
		TArray<SlonkFauna::FFaunaField::FGroupRef> Spawned;
		TArray<uint64> LetGo;
		F.Select(Cam, Spawned, LetGo);
		auto DistOf = [&](const SlonkFauna::FFaunaGroup& G) { return FVector::Dist(CamDir, G.Centre) * R; };
		// Let go within the hysteresis: to make room, out of sight (past the fade by more than its loop) as it went.
		for (const uint64 Id : LetGo)
		{
			const SlonkFauna::FFaunaField::FGroupRef* G = Was.Find(Id);
			if (!G || DistOf(**G) > D.ReachCm * SlonkFauna::SpawnHysteresis) { continue; }
			++MadeRoom;
			SeenGo += DistOf(**G) - (*G)->ExtentCm() > D.FadeCm ? 0 : 1;
		}
		bool bFarAbout = false;
		for (const TPair<uint64, SlonkFauna::FFaunaField::FGroupRef>& A : F.GetActive()) { bFarAbout |= DistOf(*A.Value) > D.ReachCm; }
		for (const SlonkFauna::FFaunaField::FGroupRef& G : Spawned)
		{
			++Spawns;
			const bool bInSight = DistOf(*G) - G->ExtentCm() < D.FadeCm;
			InSight += s > 0 && bInSight ? 1 : 0;
			if (bInSight && bFarAbout && Bad++ == 0) { FirstBad = s; }
		}
		int32 Waiting = 0;
		for (const SlonkFauna::FFaunaField::FGroupRef& C : F.GetCandidates()) { Waiting += !F.GetActive().Contains(C->Id) && DistOf(*C) < D.ReachCm ? 1 : 0; }
		Full += Waiting > 0 ? 1 : 0;
		Over += F.ActiveGroups(EFaunaKind::Flier) <= D.MaxGroups && F.ActiveMembers(EFaunaKind::Flier) <= D.MaxMembers ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("%d woods tried, the densest with %d flocks within reach of its middle (caps %d); %d steps of 100 m from it: %d flocks came (%d in sight after the first step, %d of them while one past its reach was still about, first at step %d), %d let go to make room (%d of them in sight as they went), the caps full on %d steps, over them on %d"),
		Tried, Within, D.MaxGroups, Steps, Spawns, InSight, Bad, FirstBad, MadeRoom, SeenGo, Full, Over));
	TestTrue(TEXT("a wood with more flocks within reach than the caps allow"), Within > D.MaxGroups);
	TestTrue(TEXT("the caps full along the way"), Full > 10);
	TestTrue(TEXT("flocks out of sight make room for nearer ones"), MadeRoom > 0);
	TestEqual(TEXT("every flock let go to make room was out of sight as it went"), SeenGo, 0);
	TestEqual(TEXT("no flock comes in where it would be seen while one past its reach holds its place"), Bad, 0);
	TestEqual(TEXT("never over the caps"), Over, 0);

	// The margin: with the caps full, two flocks out of sight about as far off (3 km), one about and one waiting, swap
	// for neither while the camera drifts to and fro between them (each nearer by turns, by up to 100 m: less than a
	// sixth of a cell); one nearer by more than that (300 m) takes the other's place, out of sight as it goes, and
	// drifting back does not swap them again. The rest of the caps are held by flocks in sight 1 km off.
	SlonkFauna::FFaunaField Margin;
	Margin.SetWorld(W);
	SlonkFauna::FFaunaField::FScan Cells;
	Cells.CentreDir = Start;
	auto Flock = [&](uint64 Id, const FVector& Dir)
	{
		SlonkFauna::FFaunaGroup G;
		G.Id = Id;
		G.Kind = EFaunaKind::Flier;
		G.Centre = Dir.GetSafeNormal();
		G.Members = 10;
		G.RadiusCm = 1000.0;
		const SlonkFauna::FFaunaField::FGroupRef Ref = MakeShared<const SlonkFauna::FFaunaGroup, ESPMode::ThreadSafe>(MoveTemp(G));
		Cells.CellIds.Add(Id);
		Cells.CellGroups.Add(Ref);
		return Ref;
	};
	for (int32 i = 0; i < D.MaxGroups - 1; ++i) { Flock(100 + i, AlongGround(Start, 1.0e5, R, i * 0.5)); }
	const SlonkFauna::FFaunaField::FGroupRef FlockA = Flock(1, AlongGround(Start, 3.0e5, R, 0.0)), FlockB = Flock(2, AlongGround(Start, 3.0e5, R, UE_DOUBLE_PI));
	Margin.Accept(Cells);
	// The camera 30 m over the sea-level sphere, X along the ground toward A (away from B).
	auto Drift = [&](double X, int32& Swaps, int32& BNearer, int32& Seen)
	{
		const FVector Cam = AlongGround(Start, X, R, 0.0) * (R + 3000.0);
		TArray<SlonkFauna::FFaunaField::FGroupRef> Spawned;
		TArray<uint64> LetGo;
		const TMap<uint64, SlonkFauna::FFaunaField::FGroupRef> Was = Margin.GetActive();
		Margin.Select(Cam, Spawned, LetGo);
		Swaps += Spawned.Num() + LetGo.Num();
		const FVector CamDir = Cam.GetSafeNormal();
		BNearer += FVector::Dist(CamDir, FlockB->Centre) < FVector::Dist(CamDir, FlockA->Centre) ? 1 : 0;
		for (const uint64 Id : LetGo)
		{
			if (const SlonkFauna::FFaunaField::FGroupRef* G = Was.Find(Id)) { Seen += FVector::Dist(CamDir, (*G)->Centre) * R - (*G)->ExtentCm() > D.FadeCm ? 0 : 1; }
		}
		return LetGo;
	};
	int32 Unused = 0, FirstSwaps = 0, Nearer = 0, BackSwaps = 0, BackNearer = 0, MarginSeen = 0;
	Drift(3000.0, Unused, Unused, MarginSeen);
	const bool bFilled = Margin.ActiveGroups(EFaunaKind::Flier) == D.MaxGroups && Margin.GetActive().Contains(FlockA->Id) && !Margin.GetActive().Contains(FlockB->Id);
	for (int32 k = 0; k < 60; ++k) { Drift(5000.0 * FMath::Sin(k * 0.7), FirstSwaps, Nearer, MarginSeen); }
	int32 Swapped = 0;
	const TArray<uint64> Gone = Drift(-15000.0, Swapped, Unused, MarginSeen);
	const bool bTaken = Gone.Num() == 1 && Gone[0] == FlockA->Id && Margin.GetActive().Contains(FlockB->Id) && Margin.ActiveGroups(EFaunaKind::Flier) == D.MaxGroups;
	for (int32 k = 0; k < 60; ++k) { Drift(5000.0 * FMath::Sin(k * 0.7), BackSwaps, BackNearer, MarginSeen); }
	AddInfo(FString::Printf(TEXT("the margin: two flocks out of sight 3 km off, the caps full %s; drifting 60 frames within 50 m of halfway, the waiting one the nearer on %d of them: %d swaps; 150 m toward it, it %s; drifting back, the other the nearer on %d frames: %d swaps; %d let go in sight"),
		bFilled ? TEXT("with the first about") : TEXT("(not as meant)"), Nearer, FirstSwaps, bTaken ? TEXT("takes the other's place") : TEXT("does not come"), 60 - BackNearer, BackSwaps, MarginSeen));
	TestTrue(TEXT("the caps full, the nearer of the two about"), bFilled);
	TestTrue(TEXT("the waiting flock the nearer on some frames"), Nearer > 10);
	TestEqual(TEXT("two flocks about as far off do not churn"), FirstSwaps, 0);
	TestTrue(TEXT("one nearer by more than the margin takes the other's place"), bTaken);
	TestEqual(TEXT("nor do they churn swapped"), BackSwaps, 0);
	TestEqual(TEXT("none let go in sight"), MarginSeen, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaHabitatFliersTest, "Slonk.Fauna.FlierHabitat", SLONK_TEST_FLAGS)
bool FSlonkFaunaHabitatFliersTest::RunTest(const FString& Parameters)
{
	// On an Earth-like world the fliers live by the woods, the cliffs, the shores and over the wet meadows, each flock
	// where the ground round its loop's middle offers its habitat; none over open water or ice; none at all on a world
	// with no life or no air, or at density 0.
	const SlonkFauna::FFaunaWorld W = EarthFauna();
	TArray<SlonkFauna::FFaunaGroup> Groups;
	int32 Cells = 0;
	GroupsOverGlobe(W, 1500, Groups, &Cells);
	int32 ByHabitat[(int32)EFlierHabitat::Count] = {};
	int32 Wrong = 0, Members = 0;
	// Each flock's habitat read afresh round its middle, not by the reading that placed it: the scatter's habitat at 49
	// points out to 450 m, and whether there is water out to a kilometre. Wooded ground by a woodland flock, steep ground
	// by a cliff's, water within a kilometre of a shore's, level land with little water about a meadow's; land by every
	// flock, most of it free of ice.
	const double R = W.P.RadiusCm;
	double LeastWood = 1.0, LeastSteep = 1.0, LeastLevel = 1.0;
	for (const SlonkFauna::FFaunaGroup& G : Groups)
	{
		++ByHabitat[(int32)G.Habitat];
		Members += G.Members;
		double MaxForest = 0.0, MaxSteep = 0.0, FlatSum = 0.0;
		int32 Points = 0, Land = 0, Icy = 0, Wet = 0;
		for (int32 Ring = 0; Ring <= 4; ++Ring)
		{
			const int32 Around = Ring == 0 ? 1 : 12;
			for (int32 k = 0; k < Around; ++k)
			{
				const FVector Dir = Ring == 0 ? G.Centre : AlongGround(G.Centre, 11250.0 * Ring, R, UE_DOUBLE_TWO_PI * (k + 0.5 * (Ring % 2)) / Around);
				const SlonkScatter::FHabitat H = SlonkScatter::HabitatAt(W.P, Dir);
				++Points;
				if (H.bWater) { ++Wet; continue; }
				++Land;
				MaxForest = FMath::Max(MaxForest, H.Forest);
				MaxSteep = FMath::Max(MaxSteep, FMath::Max(H.Cliff, 0.6 * H.WallFoot));
				FlatSum += H.Flat;
				Icy += H.Ice > 0.35 ? 1 : 0;
			}
		}
		bool bWaterWithinKm = Wet > 0;
		for (const double Out : { 6.0e4, 8.0e4, 1.0e5 })
		{
			for (int32 k = 0; k < 12 && !bWaterWithinKm; ++k) { bWaterWithinKm = SlonkScatter::HabitatAt(W.P, AlongGround(G.Centre, Out, R, UE_DOUBLE_TWO_PI * k / 12.0)).bWater; }
		}
		const double Level = FlatSum / FMath::Max(Land, 1);
		bool bOk = Land > 0 && Icy * 2 <= Land;
		switch (G.Habitat)
		{
		case EFlierHabitat::Woodland: bOk &= MaxForest > 0.2; LeastWood = FMath::Min(LeastWood, MaxForest); break;
		case EFlierHabitat::Cliff: bOk &= MaxSteep > 0.1; LeastSteep = FMath::Min(LeastSteep, MaxSteep); break;
		case EFlierHabitat::Coast: bOk &= bWaterWithinKm && Land >= 3; break;
		case EFlierHabitat::Meadow: bOk &= Level > 0.6 && Wet * 10 <= Points; LeastLevel = FMath::Min(LeastLevel, Level); break;
		default: bOk = false; break;
		}
		const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Flier);
		bOk &= G.Members >= D.GroupMin && G.Members <= D.GroupMax && G.SpanM > 0.3f && G.SpanM < 1.5f;
		Wrong += bOk ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("%d cells of an Earth-like world: %d flocks (%d woodland, %d meadow, %d coast, %d cliff; %d birds), %d off their habitat read afresh (the least wooded wood %.2f, the least steep cliff %.2f, the least level meadow %.2f)"),
		Cells, Groups.Num(), ByHabitat[0], ByHabitat[1], ByHabitat[2], ByHabitat[3], Members, Wrong, LeastWood, LeastSteep, LeastLevel));
	TestTrue(TEXT("flocks on an Earth-like world"), Groups.Num() > 50);
	TestTrue(TEXT("by the woods, the cliffs and the shores"), ByHabitat[(int32)EFlierHabitat::Woodland] > 0 && ByHabitat[(int32)EFlierHabitat::Cliff] > 0 && ByHabitat[(int32)EFlierHabitat::Coast] > 0);
	TestEqual(TEXT("every flock where its habitat is"), Wrong, 0);
	// None without life, without air, at density 0, or with the fliers not allowed.
	TArray<SlonkFauna::FFaunaGroup> None;
	int32 Leaked = 0;
	GroupsOverGlobe(EarthFauna(0.f), 600, None);
	Leaked += None.Num();
	SlonkFauna::FFaunaWorld Airless = W;
	Airless.P.bAirless = true;
	GroupsOverGlobe(Airless, 600, None);
	Leaked += None.Num();
	SlonkFauna::FFaunaWorld Sparse = W;
	Sparse.Density = 0.f;
	GroupsOverGlobe(Sparse, 600, None);
	Leaked += None.Num();
	SlonkFauna::FFaunaWorld Others = W;
	Others.KindMask = SlonkFauna::AllKinds & ~SlonkFauna::KindBit(EFaunaKind::Flier);
	GroupsOverGlobe(Others, 600, None);
	Leaked += None.Num();
	TestEqual(TEXT("no flocks without life or air, at density 0, or with the fliers off"), Leaked, 0);
	TestEqual(TEXT("a world with no life spawns nothing"), APlanetBody::GroundLifeOf(EPlanetArchetype::Terrestrial, false) > 0.f ? 1 : 0, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaPeriodsDivideTest, "Slonk.Fauna.PeriodsDivide", SLONK_TEST_FLAGS)
bool FSlonkFaunaPeriodsDivideTest::RunTest(const FString& Parameters)
{
	// Every path's period divides the material's hour, so a flock is where it was a period ago and an hour ago, the
	// hour's wrap of the clock moves nothing, and the material's own cycles show no seam there either. Its loops stay
	// small enough to come and go unseen (their reach less the fade), fly at a bird's speed and keep over the ground.
	const SlonkFauna::FFaunaWorld W = EarthFauna();
	TArray<SlonkFauna::FFaunaGroup> Groups;
	GroupsOverGlobe(W, 600, Groups);
	const double R = W.P.RadiusCm;
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Flier);
	int32 NotDividing = 0, Unclosed = 0, Seams = 0, Slow = 0, Wide = 0, Under = 0, MotionSeams = 0;
	double WorstLoop = 0.0, MinClearance = TNumericLimits<double>::Max(), MinSpeed = 1e9, MaxSpeed = 0.0, MaxExtent = 0.0;
	for (const SlonkFauna::FFaunaGroup& G : Groups)
	{
		const double Turns = SlonkFauna::ClockPeriodSec / G.PeriodSec;
		NotDividing += G.PeriodSec >= 60.0 && FMath::Abs(Turns - FMath::RoundToDouble(Turns)) < 1e-9 ? 0 : 1;
		for (const double T : { 0.0, 123.4, 1777.7, 3599.0 })
		{
			const FVector P0 = SlonkFauna::PathAt(G, R, T).PosCm;
			const double Loop = FMath::Max3(FVector::Dist(P0, SlonkFauna::PathAt(G, R, T + G.PeriodSec).PosCm), FVector::Dist(P0, SlonkFauna::PathAt(G, R, T + 3600.0).PosCm),
				FVector::Dist(P0, SlonkFauna::PathAt(G, R, T + 7.0 * 3600.0).PosCm));
			WorstLoop = FMath::Max(WorstLoop, Loop);
			Unclosed += Loop < 1.0 ? 0 : 1;
		}
		// Across the wrap: a millisecond's flight.
		Seams += FVector::Dist(SlonkFauna::PathAt(G, R, 3599.999).PosCm, SlonkFauna::PathAt(G, R, 3600.0).PosCm) < G.SpeedCmS * 0.002 + 1.0 ? 0 : 1;
		MinSpeed = FMath::Min(MinSpeed, G.SpeedCmS / 100.0);
		MaxSpeed = FMath::Max(MaxSpeed, G.SpeedCmS / 100.0);
		Slow += G.SpeedCmS > 600.0 && G.SpeedCmS < 2400.0 ? 0 : 1;
		MaxExtent = FMath::Max(MaxExtent, G.ExtentCm());
		Wide += G.ExtentCm() + D.FadeCm < D.ReachCm ? 0 : 1;
		for (int32 k = 0; k < 24; ++k)
		{
			const FVector P = SlonkFauna::PathAt(G, R, G.PeriodSec * k / 24.0).PosCm;
			const double Clear = P.Size() - R - FMath::Max(SlonkTerrain::SurfaceHeightCm(W.P, P.GetSafeNormal()), 0.0);
			MinClearance = FMath::Min(MinClearance, Clear);
			Under += Clear > 0.0 ? 0 : 1;
		}
		// The material's motion across its own wrap, at a wing's tip: no more than its motion over the same quarter
		// millisecond anywhere else.
		TArray<FTransform> Xf;
		TArray<float> Custom;
		SlonkFauna::MembersOf(G, Xf, Custom);
		FMotionCase M;
		FMemory::Memcpy(M.Custom, Custom.GetData(), sizeof(M.Custom));
		const FVector3f Tip(-5.f, 50.f, 1.f);
		const FLinearColor TipC(1.f, 1.f, 1.f, 0.5f);
		const FVector3f Before = SlonkFauna::FlierMotion(Tip, TipC, 3599.9998f, M.Custom, M.Group, 5000.f, 960.f);
		const FVector3f After = SlonkFauna::FlierMotion(Tip, TipC, 0.f, M.Custom, M.Group, 5000.f, 960.f);
		MotionSeams += (Before - After).Size() < 2.f ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("%d flocks: %d periods not dividing the hour, %d paths not closed (worst %.3f cm), %d seams at the hour's wrap, %d in the material's motion"), Groups.Num(), NotDividing, Unclosed, WorstLoop, Seams, MotionSeams));
	AddInfo(FString::Printf(TEXT("speeds %.1f to %.1f m/s (%d outside 6-24), loops reach %.0f m from their middles (%d past %.0f m), lowest %.1f m over the ground (%d under it)"),
		MinSpeed, MaxSpeed, Slow, MaxExtent / 100.0, Wide, (D.ReachCm - D.FadeCm) / 100.0, MinClearance / 100.0, Under));
	TestTrue(TEXT("flocks to test"), Groups.Num() > 20);
	TestEqual(TEXT("every period divides the hour"), NotDividing, 0);
	TestEqual(TEXT("every path closes after its period and after the hour"), Unclosed, 0);
	TestEqual(TEXT("no seam where the clock wraps"), Seams, 0);
	TestEqual(TEXT("no seam in the material's motion where its clock wraps"), MotionSeams, 0);
	TestEqual(TEXT("at a bird's speed"), Slow, 0);
	TestEqual(TEXT("every loop small enough that its flock comes and goes unseen"), Wide, 0);
	TestEqual(TEXT("over the ground"), Under, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaStartleTest, "Slonk.Fauna.Startle", SLONK_TEST_FLAGS)
bool FSlonkFaunaStartleTest::RunTest(const FString& Parameters)
{
	// A craft passing 30 m over the ground through a flock scatters it; one passing 300 m up does not; the flock is back
	// on its path two minutes after its last fright. Someone walking just under a flock startles it too, one far off
	// does not; a craft set down by it does not, the same craft in the air does; and the material's scatter is open at
	// once and closed again by its fortieth second.
	const SlonkFauna::FFaunaWorld W = EarthFauna();
	SlonkFauna::FFaunaGroup G;
	if (!TestFlock(W, G)) { AddError(TEXT("no flock found on the Earth-like world")); return false; }
	const double R = W.P.RadiusCm;
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Flier);
	auto Pass = [&](double AltitudeCm, double Start, SlonkFauna::FPanic& Panic, double& OutMaxStep)
	{
		// Straight across the flock's place at 80 m/s, from 600 m before it to 600 m after.
		const SlonkFauna::FFaunaPose At = SlonkFauna::PathAt(G, R, Start + 7.5);
		const FVector Up = At.PosCm.GetSafeNormal();
		const FVector Across = At.Rot.GetRightVector();
		const double Ground = FMath::Max(SlonkTerrain::SurfaceHeightCm(W.P, Up), 0.0);
		const FVector Mid = Up * (R + Ground + AltitudeCm);
		int32 Startles = 0;
		FVector Last = FVector::ZeroVector;
		OutMaxStep = 0.0;
		for (double T = Start; T <= Start + 15.0; T += 0.1)
		{
			SlonkFauna::FFaunaThreat Craft;
			Craft.PosCm = Mid + Across * (8000.0 * (T - Start - 7.5));
			Craft.VelCmS = Across * 8000.0;
			Craft.AltitudeCm = AltitudeCm;
			const FVector Pos = SlonkFauna::PathAt(G, R, T).PosCm + SlonkFauna::PanicOffset(Panic, D, T);
			Startles += SlonkFauna::StartleGroup(Panic, D, Pos, Craft, T) ? 1 : 0;
			const FVector Off = SlonkFauna::PanicOffset(Panic, D, T);
			OutMaxStep = FMath::Max(OutMaxStep, FVector::Dist(Off, Last));
			Last = Off;
		}
		return Startles;
	};
	SlonkFauna::FPanic Low, High;
	double LowStep = 0.0, HighStep = 0.0;
	const double T0 = 1000.0;
	const int32 LowStartles = Pass(3000.0, T0, Low, LowStep);
	const int32 HighStartles = Pass(30000.0, T0, High, HighStep);
	const double Fled = SlonkFauna::PanicOffset(Low, D, T0 + 30.0).Size();
	const double Back = SlonkFauna::PanicOffset(Low, D, Low.T0 + SlonkFauna::PanicSec + 0.01).Size();
	const bool bOnPath = (SlonkFauna::PathAt(G, R, Low.T0 + 125.0).PosCm + SlonkFauna::PanicOffset(Low, D, Low.T0 + 125.0)).Equals(SlonkFauna::PathAt(G, R, Low.T0 + 125.0).PosCm, 0.0);
	// The flight's speed off its path at its fastest, m/s, from its offsets a tenth of a second apart.
	double FleeSpeed = 0.0;
	for (double T = Low.T0; T < Low.T0 + SlonkFauna::PanicSec; T += 0.1)
	{
		FleeSpeed = FMath::Max(FleeSpeed, FVector::Dist(SlonkFauna::PanicOffset(Low, D, T + 0.1), SlonkFauna::PanicOffset(Low, D, T)) / 0.1 / 100.0);
	}
	AddInfo(FString::Printf(TEXT("a %s flock %.0f m over the ground: a pass 30 m up startles it %d times, %.0f m off its path 30 s on (at most %.1f m/s off it), %.2f m off it two minutes after; a pass 300 m up startles it %d times"),
		SlonkFauna::HabitatName(G.Habitat), G.CruiseCm / 100.0, LowStartles, Fled / 100.0, FleeSpeed, Back / 100.0, HighStartles));
	TestTrue(TEXT("a craft passing low scatters the flock"), LowStartles >= 1 && Fled > 5000.0);
	TestEqual(TEXT("one passing high does not"), HighStartles, 0);
	TestEqual(TEXT("unscattered by it"), SlonkFauna::PanicOffset(High, D, T0 + 30.0).Size(), 0.0);
	TestEqual(TEXT("back on its path two minutes after its fright"), Back, 0.0);
	TestTrue(TEXT("exactly on it"), bOnPath);
	TestTrue(TEXT("fleeing at a bird's speed, not a jump"), FleeSpeed > 5.0 && FleeSpeed < 40.0);
	// Someone on foot under it, and far from it.
	const FVector Here = SlonkFauna::PathAt(G, R, 2000.0).PosCm;
	SlonkFauna::FFaunaThreat Walker;
	Walker.bCraft = false;
	Walker.PosCm = Here - Here.GetSafeNormal() * 2000.0;
	SlonkFauna::FPanic Under, Away;
	TestTrue(TEXT("someone walking under a flock startles it"), SlonkFauna::StartleGroup(Under, D, Here, Walker, 2000.0));
	Walker.PosCm = Here + FVector::CrossProduct(Here.GetSafeNormal(), FVector::ZAxisVector).GetSafeNormal() * 2.0e5;
	TestFalse(TEXT("someone 2 km off does not"), SlonkFauna::StartleGroup(Away, D, Here, Walker, 2000.0));
	// A craft set down 20 m under a flock and 30 m to one side, as a ship parked there is, scares nothing; lifted off
	// there, it does.
	SlonkFauna::FFaunaThreat Parked;
	Parked.PosCm = Here - Here.GetSafeNormal() * 2000.0 + FVector::CrossProduct(Here.GetSafeNormal(), FVector::ZAxisVector).GetSafeNormal() * 3000.0;
	Parked.AltitudeCm = 150.0;
	Parked.bLanded = true;
	SlonkFauna::FPanic ByParked, ByLifting;
	TestFalse(TEXT("a craft set down by a flock does not startle it"), SlonkFauna::StartleGroup(ByParked, D, Here, Parked, 2000.0));
	Parked.bLanded = false;
	TestTrue(TEXT("the same craft in the air there does"), SlonkFauna::StartleGroup(ByLifting, D, Here, Parked, 2000.0));
	// The material's scatter: within two spans, open a few seconds in, closed by the fortieth.
	FMotionCase M;
	M.Custom[SlonkFauna::Custom::SizeM] = 0.4f;
	M.Custom[SlonkFauna::Custom::Seed] = 0.37f;
	M.Custom[SlonkFauna::Custom::RateHz] = 10.f;
	M.Custom[SlonkFauna::Custom::PerKind] = 0.8f;
	M.Group[SlonkFauna::GroupData::ScatterT0] = 100.f;
	M.Group[SlonkFauna::GroupData::ScatterDirX] = 1.f;
	M.Group[SlonkFauna::GroupData::Startled] = 1.f;
	M.Group[SlonkFauna::GroupData::SpreadM] = 3.f;
	M.Group[SlonkFauna::GroupData::PanicT0] = 100.f;
	M.Group[SlonkFauna::GroupData::OnsetT0] = 100.f;
	const FVector3f Body(0.f, 0.f, 0.f);
	const FLinearColor Rigid(0.f, 0.f, 0.5f, 0.f);
	auto Scatter = [&](float Age, float Startled)
	{
		FMotionCase C = M;
		C.Group[SlonkFauna::GroupData::Startled] = Startled;
		FMotionCase Calm = M;
		Calm.Group[SlonkFauna::GroupData::Startled] = 0.f;
		return (SlonkFauna::FlierMotion(Body, Rigid, 100.f + Age, C.Custom, C.Group, 1000.f, 960.f) - SlonkFauna::FlierMotion(Body, Rigid, 100.f + Age, Calm.Custom, Calm.Group, 1000.f, 960.f)).Size();
	};
	AddInfo(FString::Printf(TEXT("the material's scatter of a 0.4 m bird: %.0f units at 0 s, %.0f at 3 s, %.0f at 20 s, %.0f at 41 s (two spans: 200)"), Scatter(0.f, 1.f), Scatter(3.f, 1.f), Scatter(20.f, 1.f), Scatter(41.f, 1.f)));
	TestTrue(TEXT("the scatter opens within seconds, to at most two spans"), Scatter(3.f, 1.f) > 150.f && Scatter(3.f, 1.f) <= 200.5f);
	TestTrue(TEXT("and is closed by its fortieth second"), Scatter(41.f, 1.f) < 0.01f && Scatter(0.f, 1.f) < 1.f);

	// Startled again 8 to 40 s on, while its scatter is open, the flock flees the new way but its birds' scatter is left
	// as it was: the material is given the same moment and way it opened, so not a vertex moves when the startle comes.
	// The first startle's fast wingbeat comes in rather than snapping on.
	auto GroupDataOf = [&](const SlonkFauna::FPanic& P, double Now, const FQuat& Rot, bool bAsBefore)
	{
		// As UPlanetFaunaComponent::UpdateView gives the material it (SlonkFauna::FrightData, the clock's offset 0);
		// bAsBefore with the scatter's moment and way the last startle's, as it was given before the scatter was kept.
		FMotionCase C = M;
		SlonkFauna::FrightData(P, Now, Rot, 0.0, C.Group);
		if (bAsBefore && P.IsLive(Now))
		{
			const FVector Fled = Rot.UnrotateVector(P.Dir).GetSafeNormal();
			C.Group[SlonkFauna::GroupData::ScatterT0] = C.Group[SlonkFauna::GroupData::PanicT0];
			C.Group[SlonkFauna::GroupData::ScatterDirX] = (float)Fled.X;
			C.Group[SlonkFauna::GroupData::ScatterDirY] = (float)Fled.Y;
			C.Group[SlonkFauna::GroupData::ScatterDirZ] = (float)Fled.Z;
		}
		return C;
	};
	// A wing's tip, the body and the tail's tip.
	const TPair<FVector3f, FLinearColor> Verts[] = { { FVector3f(-5.f, 50.f, 1.f), FLinearColor(1.f, 1.f, 1.f, 0.5f) }, { Body, Rigid }, { FVector3f(-30.f, 0.f, 0.f), FLinearColor(0.5f, 1.f, 0.5f, 0.25f) } };
	auto Jump = [&](const FMotionCase& A, const FMotionCase& B, double Now)
	{
		float Worst = 0.f;
		const float T = (float)FMath::Fmod(Now, SlonkFauna::ClockPeriodSec);
		for (const TPair<FVector3f, FLinearColor>& V : Verts)
		{
			Worst = FMath::Max(Worst, (SlonkFauna::FlierMotion(V.Key, V.Value, T, A.Custom, A.Group, 1000.f, 960.f) - SlonkFauna::FlierMotion(V.Key, V.Value, T, B.Custom, B.Group, 1000.f, 960.f)).Size());
		}
		return Worst;
	};
	// A craft 30 m up and 50 m off the flock along the ground a planet-local way, at a time.
	auto CraftBy = [&](double Now, const SlonkFauna::FPanic& P, const FVector& Way, FVector& OutPos, FQuat& OutRot)
	{
		const SlonkFauna::FFaunaPose At = SlonkFauna::PathAt(G, R, Now);
		OutPos = At.PosCm + SlonkFauna::PanicOffset(P, D, Now);
		OutRot = At.Rot;
		const FVector Up = OutPos.GetSafeNormal();
		SlonkFauna::FFaunaThreat T;
		T.PosCm = OutPos + (Way - Up * FVector::DotProduct(Way, Up)).GetSafeNormal() * 5000.0;
		T.AltitudeCm = 3000.0;
		return T;
	};
	const double Ts = 1500.0;
	SlonkFauna::FPanic First;
	FVector Pos0;
	FQuat Rot0;
	const SlonkFauna::FFaunaThreat Ahead = CraftBy(Ts, First, SlonkFauna::PathAt(G, R, Ts).Rot.GetForwardVector(), Pos0, Rot0);
	const FMotionCase Calm = GroupDataOf(First, Ts, Rot0, false);
	TestTrue(TEXT("a craft just ahead of a flock startles it"), SlonkFauna::StartleGroup(First, D, Pos0, Ahead, Ts));
	const float FirstJump = Jump(Calm, GroupDataOf(First, Ts, Rot0, false), Ts);
	// Its panic eases from its latest startle: once both scatters have closed, its birds move as those of a flock first
	// startled then (and not as they would with the panic run from when its scatter opened, as it was run before).
	int32 Follows = 0;
	float OffOpening = 0.f;
	auto CheckFollows = [&](const SlonkFauna::FPanic& P, const FVector& Pos, const SlonkFauna::FFaunaThreat& T, const FQuat& Rot, double At)
	{
		SlonkFauna::FPanic Fresh;
		if (!SlonkFauna::StartleGroup(Fresh, D, Pos, T, At)) { return; }
		float Off = 0.f, FromOpening = 0.f;
		for (const double After : { 45.0, 70.0, 95.0 })
		{
			const FMotionCase Mine = GroupDataOf(P, At + After, Rot, false);
			Off = FMath::Max(Off, Jump(Mine, GroupDataOf(Fresh, At + After, Rot, false), At + After));
			FMotionCase Opening = Mine;
			Opening.Group[SlonkFauna::GroupData::PanicT0] = Mine.Group[SlonkFauna::GroupData::ScatterT0];
			FromOpening = FMath::Max(FromOpening, Jump(Mine, Opening, At + After));
		}
		Follows += Off == 0.f ? 1 : 0;
		OffOpening = FMath::Max(OffOpening, FromOpening);
	};
	// Again from the way it fled, so it turns back.
	int32 Restartled = 0, Kept = 0, Turned = 0;
	float RestartJump = 0.f, WouldHaveJumped = 0.f;
	for (const double Dt : { 8.5, 15.0, 25.0, 39.0 })
	{
		SlonkFauna::FPanic Again = First;
		FVector Pos;
		FQuat Rot;
		const SlonkFauna::FFaunaThreat Behind = CraftBy(Ts + Dt, Again, First.Dir, Pos, Rot);
		const FMotionCase Before = GroupDataOf(Again, Ts + Dt, Rot, false);
		if (!SlonkFauna::StartleGroup(Again, D, Pos, Behind, Ts + Dt)) { continue; }
		++Restartled;
		Kept += Again.ScatterT0 == First.ScatterT0 && Again.ScatterDir == First.ScatterDir && Again.T0 == Ts + Dt ? 1 : 0;
		Turned += FVector::DotProduct(Again.Dir, First.Dir) < 0.5 ? 1 : 0;
		RestartJump = FMath::Max(RestartJump, Jump(Before, GroupDataOf(Again, Ts + Dt, Rot, false), Ts + Dt));
		WouldHaveJumped = FMath::Max(WouldHaveJumped, Jump(Before, GroupDataOf(Again, Ts + Dt, Rot, true), Ts + Dt));
		CheckFollows(Again, Pos, Behind, Rot, Ts + Dt);
	}
	const float WithinOffOpening = OffOpening;
	// Startled again 40 to 100 s on, its scatter closed and its fright still live (as a craft hovering by a flock opens a
	// new scatter about every 40 s), it scatters again and its panic runs from this startle, while its wings and tail go
	// on as they were: its fast beat is not begun again (as it was when one clock ran the scatter, the panic and the
	// beat's coming-in). Past about 80 s, its beat eased as its fright wears on, the beat comes back up from where it had
	// eased to (not straight back in full), as near as the material's clock tells.
	int32 LateStartled = 0, Reopened = 0;
	float LateJump = 0.f, EasedJump = 0.f, OneClockJump = 0.f, StraightBackJump = 0.f;
	for (const double Dt : { 45.0, 60.0, 75.0, 90.0, 100.0 })
	{
		SlonkFauna::FPanic Late = First;
		FVector PosL;
		FQuat RotL;
		const double Tl = Ts + Dt;
		const SlonkFauna::FFaunaThreat LateCraft = CraftBy(Tl, Late, First.Dir, PosL, RotL);
		const FMotionCase Before = GroupDataOf(Late, Tl, RotL, false);
		if (!SlonkFauna::StartleGroup(Late, D, PosL, LateCraft, Tl)) { continue; }
		++LateStartled;
		Reopened += Late.ScatterT0 == Tl && Late.ScatterDir == Late.Dir && Late.T0 == Tl ? 1 : 0;
		const FMotionCase After = GroupDataOf(Late, Tl, RotL, false);
		float& Worst = Dt < 80.0 ? LateJump : EasedJump;
		Worst = FMath::Max(Worst, Jump(Before, After, Tl));
		FMotionCase OneClock = After;
		OneClock.Group[SlonkFauna::GroupData::PanicT0] = After.Group[SlonkFauna::GroupData::ScatterT0];
		OneClock.Group[SlonkFauna::GroupData::OnsetT0] = After.Group[SlonkFauna::GroupData::ScatterT0];
		OneClockJump = FMath::Max(OneClockJump, Jump(Before, OneClock, Tl));
		if (Dt >= 80.0)
		{
			FMotionCase Straight = After;
			Straight.Group[SlonkFauna::GroupData::OnsetT0] = Before.Group[SlonkFauna::GroupData::OnsetT0];
			StraightBackJump = FMath::Max(StraightBackJump, Jump(Before, Straight, Tl));
		}
		CheckFollows(Late, PosL, LateCraft, RotL, Tl);
	}
	AddInfo(FString::Printf(TEXT("startled again 8 to 40 s on: %d of 4 times, %d keeping the scatter as it opened (%d fleeing a new way), the birds moving %.4f units as it comes (up to %.0f had the scatter started again); the first startle moves them %.4f"),
		Restartled, Kept, Turned, RestartJump, WouldHaveJumped, FirstJump));
	AddInfo(FString::Printf(TEXT("startled again 40 to 100 s on: %d of 5 times, %d scattering again; the birds moving %.4f units as it comes up to 80 s on, %.4f after (its beat eased; up to %.0f had it come straight back in full), up to %.0f had one clock run the scatter, the panic and the beat; %d of 9 startles again easing the panic from the latest (up to %.0f units off had it eased from the scatter's opening)"),
		LateStartled, Reopened, LateJump, EasedJump, StraightBackJump, OneClockJump, Follows, WithinOffOpening));
	TestEqual(TEXT("startled again within the scatter, every time"), Restartled, 4);
	TestEqual(TEXT("its scatter kept as it opened"), Kept, 4);
	TestEqual(TEXT("its flight turned to the new fright"), Turned, 4);
	TestTrue(TEXT("not a vertex moves as the new startle comes"), RestartJump < 1e-3f);
	TestTrue(TEXT("where a scatter started again would have snapped shut"), WouldHaveJumped > 20.f);
	TestTrue(TEXT("the first startle's fast wingbeat comes in, not on at once"), FirstJump < 1e-3f);
	TestEqual(TEXT("startled again 40 to 100 s on, every time"), LateStartled, 5);
	TestEqual(TEXT("startled after its scatter has closed, it scatters again"), Reopened, 5);
	TestTrue(TEXT("its wings and tail go on as they were when it scatters again"), LateJump == 0.f);
	TestTrue(TEXT("where one clock ran the scatter, the panic and the beat, they would have snapped"), OneClockJump > 5.f);
	TestTrue(TEXT("its eased beat comes back up from where it was, to the material clock's precision"), EasedJump < 0.5f);
	TestTrue(TEXT("where the beat came straight back in full, the wings would have snapped"), StraightBackJump > 5.f);
	TestEqual(TEXT("its panic eases from the latest startle"), Follows, 9);
	TestTrue(TEXT("where the panic eased from the scatter's opening, the birds would move otherwise"), WithinOffOpening > 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaFlightTest, "Slonk.Fauna.Flight", SLONK_TEST_FLAGS)
bool FSlonkFaunaFlightTest::RunTest(const FString& Parameters)
{
	// A group's frame as the game draws it, a frame at a time from the one before (SlonkFauna::FlightAt): a calm flock's
	// is its path's, exactly. Startled, a flock faces the way it really flies within a second and a half and keeps to it
	// wherever it flies at a bird's pace, where its path's frame had it fly sideways (a craft beside it) or tail first (a
	// craft just ahead, its flight back the way it came outrunning its path's at first); its frame never turns faster
	// than FlightTurnRadS, it banks hard into the wheel, its bank never changes faster than FlightBankRadS, and its frame
	// is its path's again once its fright is over. The flock is the Earth-like world's slowest, whose flight outruns its
	// path's the longest.
	const SlonkFauna::FFaunaWorld W = EarthFauna();
	TArray<SlonkFauna::FFaunaGroup> Groups;
	GroupsOverGlobe(W, 600, Groups);
	if (Groups.Num() == 0) { AddError(TEXT("no flock found on the Earth-like world")); return false; }
	const SlonkFauna::FFaunaGroup* Slowest = &Groups[0];
	for (const SlonkFauna::FFaunaGroup& F : Groups) { if (F.SpeedCmS < Slowest->SpeedCmS) { Slowest = &F; } }
	const SlonkFauna::FFaunaGroup& G = *Slowest;
	const double R = W.P.RadiusCm;
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Flier);
	// The way a flock flies, level, from where its path and its flight put it a tenth of a second apart, and how fast.
	auto FlownWay = [&](const SlonkFauna::FPanic& P, double T, double& OutSpeed)
	{
		const FVector A = SlonkFauna::PathAt(G, R, T).PosCm + SlonkFauna::PanicOffset(P, D, T);
		const FVector B = SlonkFauna::PathAt(G, R, T + 0.1).PosCm + SlonkFauna::PanicOffset(P, D, T + 0.1);
		const FVector Up = A.GetSafeNormal();
		FVector V = (B - A) / 0.1;
		V -= Up * FVector::DotProduct(V, Up);
		OutSpeed = V.Size();
		return V.GetSafeNormal();
	};
	auto Between = [](const FVector& A, const FVector& B) { return FMath::Acos(FMath::Clamp(FVector::DotProduct(A.GetSafeNormal(), B.GetSafeNormal()), -1.0, 1.0)); };
	struct FRun
	{
		bool bStartled = false;
		int32 OffPath = 0, CalmFrames = 0;
		double Faced = -1.0, Worst = 0.0, PathWorst = 0.0, MaxTurn = 0.0, MaxBankStep = 0.0, MaxBank = 0.0, WheelBank = 0.0;
	};
	const double Ts = 1500.0, Dt = 1.0 / 60.0;
	// Startled at Ts by a craft 30 m up and 50 m off at a bearing from its heading (0 ahead, a quarter turn to its right).
	auto Run = [&](double Bearing)
	{
		FRun Out;
		SlonkFauna::FPanic Panic;
		SlonkFauna::FFaunaPose Was = SlonkFauna::PathAt(G, R, Ts - 1.0 - Dt);
		const int32 Frames = FMath::CeilToInt32((SlonkFauna::PanicSec + 6.0) / Dt);
		for (int32 f = 0; f < Frames; ++f)
		{
			const double T = Ts - 1.0 + f * Dt;
			SlonkFauna::FFaunaPose Pose = SlonkFauna::FlightAt(G, D, Panic, R, T, Was, Dt);
			if (!Out.bStartled && T >= Ts)
			{
				SlonkFauna::FFaunaThreat Craft;
				Craft.PosCm = Pose.PosCm + (Pose.Rot.GetForwardVector() * FMath::Cos(Bearing) + Pose.Rot.GetRightVector() * FMath::Sin(Bearing)) * 5000.0;
				Craft.AltitudeCm = 3000.0;
				Out.bStartled = SlonkFauna::StartleGroup(Panic, D, Pose.PosCm, Craft, T);
				if (Out.bStartled) { Pose = SlonkFauna::FlightAt(G, D, Panic, R, T, Was, Dt); }
			}
			const FVector Fwd = Pose.Rot.GetForwardVector();
			if (!Panic.IsLive(T))
			{
				const SlonkFauna::FFaunaPose Path = SlonkFauna::PathAt(G, R, T);
				++Out.CalmFrames;
				Out.OffPath += Pose.PosCm == Path.PosCm && Pose.Rot.Equals(Path.Rot, 0.0) && Pose.Bank == Path.Bank ? 0 : 1;
			}
			else
			{
				double Speed = 0.0;
				const FVector Way = FlownWay(Panic, T, Speed);
				const double Age = T - Panic.T0;
				if (Out.Faced < 0.0 && Between(Fwd, Way) < 0.1) { Out.Faced = Age; }
				// Where it flies at a bird's pace (not as it slows to turn back to its path), a second and a half on.
				if (Speed > 500.0 && Age >= 1.5)
				{
					Out.Worst = FMath::Max(Out.Worst, Between(Fwd, Way));
					Out.PathWorst = FMath::Max(Out.PathWorst, Between(SlonkFauna::PathAt(G, R, T).Rot.GetForwardVector(), Way));
				}
				if (Age < 1.5) { Out.WheelBank = FMath::Max(Out.WheelBank, FMath::Abs(Pose.Bank)); }
			}
			Out.MaxTurn = FMath::Max(Out.MaxTurn, Between(Fwd, Was.Rot.GetForwardVector()));
			Out.MaxBankStep = FMath::Max(Out.MaxBankStep, FMath::Abs(Pose.Bank - Was.Bank));
			Out.MaxBank = FMath::Max(Out.MaxBank, FMath::Abs(Pose.Bank));
			Was = Pose;
		}
		return Out;
	};
	const FRun Ahead = Run(0.0), Beside = Run(UE_DOUBLE_HALF_PI);
	for (const TPair<const TCHAR*, const FRun*>& Case : { TPair<const TCHAR*, const FRun*>(TEXT("just ahead"), &Ahead), TPair<const TCHAR*, const FRun*>(TEXT("beside it"), &Beside) })
	{
		const FRun& U = *Case.Value;
		AddInfo(FString::Printf(TEXT("a %s flock at %.1f m/s startled by a craft %s: it faces the way it flies %.2f s on and keeps within %.1f deg of it from 1.5 s on (its path's frame up to %.0f deg off); turning %.2f deg a frame at most (%.2f allowed), banked %.2f rad into the wheel, its bank changing %.4f rad a frame at most (%.4f allowed), %.2f at most; %d of %d calm frames off its path's"),
			SlonkFauna::HabitatName(G.Habitat), G.SpeedCmS / 100.0, Case.Key, U.Faced, FMath::RadiansToDegrees(U.Worst), FMath::RadiansToDegrees(U.PathWorst), FMath::RadiansToDegrees(U.MaxTurn),
			FMath::RadiansToDegrees(SlonkFauna::FlightTurnRadS * Dt), U.WheelBank, U.MaxBankStep, SlonkFauna::FlightBankRadS * Dt, U.MaxBank, U.OffPath, U.CalmFrames));
		TestTrue(TEXT("startled by the craft"), U.bStartled);
		TestTrue(TEXT("calm before and after"), U.CalmFrames > 300);
		TestEqual(TEXT("a calm flock's frame is its path's, exactly"), U.OffPath, 0);
		TestTrue(TEXT("a startled flock faces the way it flies within a second and a half"), U.Faced >= 0.0 && U.Faced <= 1.5);
		TestTrue(TEXT("and keeps to it"), U.Worst < 0.15);
		TestTrue(TEXT("its frame never turns faster than FlightTurnRadS"), U.MaxTurn <= SlonkFauna::FlightTurnRadS * Dt + 1e-3);
		TestTrue(TEXT("it banks hard into the wheel"), U.WheelBank > 0.5);
		TestTrue(TEXT("its bank never changes faster than FlightBankRadS"), U.MaxBankStep <= SlonkFauna::FlightBankRadS * Dt + 5e-3);
		TestTrue(TEXT("and never past 0.7 rad"), U.MaxBank <= 0.7 + 1e-9);
	}
	TestTrue(TEXT("where its path's frame had it fly tail first from a craft ahead"), Ahead.PathWorst > 2.5);
	TestTrue(TEXT("and sideways from one beside it"), Beside.PathWorst > 0.6);
	return true;
}

namespace
{
	/** The parts of fauna_material.py's WPO_CODE a float copy is held to: the fliers' motion (all of it but its
	 *  branches), its mote branch and its walker branch. */
	enum class EMotionBlock : uint8 { Fliers, Motes, Walkers };
	/** fauna_material.py's WPO_CODE, its lines trimmed, the blank ones and the comments left out: the fliers' motion, which
	 *  SlonkFauna::FlierMotion copies; its mote branch (from its test of Motes to the brace that closes it), whose turn to
	 *  the camera Slonk.Fauna.MoteCloud copies; or its walker branch (from its test of Walkers to the brace that closes
	 *  it), which SlonkFauna::WalkerMotion copies. Each leaves the others out. Empty if the script or the block is not
	 *  found. */
	FString MaterialMotionBlock(EMotionBlock Which = EMotionBlock::Fliers)
	{
		FString Py;
		if (!FFileHelper::LoadFileToString(Py, *FPaths::Combine(FPaths::ProjectDir(), TEXT("../tools/planet_materials/fauna_material.py")))) { return FString(); }
		TArray<FString> Lines;
		Py.ParseIntoArrayLines(Lines, false);
		FString Block;
		bool bIn = false;
		EMotionBlock In = EMotionBlock::Fliers;
		int32 Depth = 0;
		for (FString& L : Lines)
		{
			L.TrimStartAndEndInline();
			if (!bIn) { bIn = L.StartsWith(TEXT("WPO_CODE = r\"\"\"")); continue; }
			if (L.StartsWith(TEXT("\"\"\""))) { return Block; }
			if (L.IsEmpty() || L.StartsWith(TEXT("//"))) { continue; }
			if (In == EMotionBlock::Fliers)
			{
				if (L.StartsWith(TEXT("[branch] if (Motes"))) { In = EMotionBlock::Motes; Depth = 0; }
				else if (L.StartsWith(TEXT("[branch] if (Walkers"))) { In = EMotionBlock::Walkers; Depth = 0; }
			}
			const EMotionBlock Line = In;
			if (In != EMotionBlock::Fliers)
			{
				for (const TCHAR C : L) { Depth += C == TEXT('{') ? 1 : (C == TEXT('}') ? -1 : 0); }
				if (Depth <= 0 && L.Contains(TEXT("}"))) { In = EMotionBlock::Fliers; }
			}
			if (Line == Which) { Block += L + TEXT("\n"); }
		}
		return FString();
	}
	/** The fliers' part's CRC (FCrc::StrCrc32) when SlonkFauna::FlierMotion was last brought in line with it. */
	constexpr uint32 MaterialMotionBlockCrc = 0x0a6c86cdu;
	/** The mote branch's CRC when Slonk.Fauna.MoteCloud's copy of its turn to the camera was last brought in line with it. */
	constexpr uint32 MaterialMoteBlockCrc = 0xa89449edu;
	/** The walker branch's CRC when SlonkFauna::WalkerMotion was last brought in line with it. */
	constexpr uint32 MaterialWalkerBlockCrc = 0x2e26a11cu;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaMeshesTest, "Slonk.Fauna.Meshes", SLONK_TEST_FLAGS)
bool FSlonkFaunaMeshesTest::RunTest(const FString& Parameters)
{
	// An Earth bird: 60, 12 and 2 triangles, its vertex colours as the material reads them (a wing's flex 0 on its hinge,
	// 4 cm out from the body's axis, rising to 1 at its tip half a span out; its side by its sign; the body and bill rigid,
	// the tail flexing from its root), every face wound to show the side its normals point to. The material's motion
	// keeps every vertex inside the instance's padded box, and a wing's root on its hinge.
	using namespace UE::Geometry;
	const FFaunaBodyPlan Plan = SlonkFauna::EarthPlan(EFaunaKind::Flier);
	const int32 WantTris[SlonkFaunaMeshes::NumLods] = { 60, 12, 2 };
	int32 Tris[SlonkFaunaMeshes::NumLods] = {};
	int32 BadColour = 0, BadWinding = 0, Hinges = 0, Tips = 0;
	FBox Box0(ForceInit);
	FDynamicMesh3 Lod0;
	for (int32 L = 0; L < SlonkFaunaMeshes::NumLods; ++L)
	{
		FDynamicMesh3 M;
		if (!SlonkFaunaMeshes::BuildLod(Plan, L, M)) { AddError(TEXT("no bird built")); return false; }
		Tris[L] = M.TriangleCount();
		const FDynamicMeshColorOverlay* C = M.Attributes()->PrimaryColors();
		const FDynamicMeshNormalOverlay* N = M.Attributes()->PrimaryNormals();
		for (const int32 T : M.TriangleIndicesItr())
		{
			FVector3d Fn, Centroid; double Area;
			M.GetTriInfo(T, Fn, Area, Centroid);
			const FIndex3i E = N->GetTriangle(T);
			BadWinding += Fn.Dot(FVector3d(N->GetElement(E.A) + N->GetElement(E.B) + N->GetElement(E.C))) > 0.0 ? 0 : 1;
			const FIndex3i Ce = C->GetTriangle(T);
			const FIndex3i Tv = M.GetTriangle(T);
			for (int32 k = 0; k < 3; ++k)
			{
				const FVector4f Col = C->GetElement(Ce[k]);
				const FVector3d P = M.GetVertex(Tv[k]);
				if (L == 0) { Box0 += (FVector)P; }
				bool bOk = true;
				if (FMath::IsNearlyEqual(Col.W, SlonkFauna::Vc::JointWing))
				{
					const double Want = FMath::Clamp((FMath::Abs(P.Y) - SlonkFauna::Vc::WingRootCm) / (50.0 - SlonkFauna::Vc::WingRootCm), 0.0, 1.0);
					bOk = Col.X == SlonkFauna::Vc::SurfaceWing && FMath::IsNearlyEqual((double)Col.Y, Want, 1e-3) && Col.Z == (P.Y > 0.0 ? SlonkFauna::Vc::SideRight : SlonkFauna::Vc::SideLeft)
						&& FMath::Abs(P.Y) >= SlonkFauna::Vc::WingRootCm - 1e-3;
					Hinges += FMath::IsNearlyEqual(FMath::Abs(P.Y), SlonkFauna::Vc::WingRootCm, 1e-3) && Col.Y == 0.f ? 1 : 0;
					Tips += Col.Y == 1.f ? 1 : 0;
				}
				else if (FMath::IsNearlyEqual(Col.W, SlonkFauna::Vc::JointTail)) { bOk = Col.X == SlonkFauna::Vc::SurfaceTail && Col.Y >= 0.f && Col.Y <= 1.f && Col.Z == SlonkFauna::Vc::SideMiddle; }
				else if (Col.W == SlonkFauna::Vc::JointRigid) { bOk = (Col.X == SlonkFauna::Vc::SurfaceBody || Col.X == SlonkFauna::Vc::SurfaceBeak) && Col.Y == 0.f && Col.Z == SlonkFauna::Vc::SideMiddle; }
				else { bOk = false; }
				BadColour += bOk ? 0 : 1;
			}
		}
		if (L == 0) { Lod0 = MoveTemp(M); }
	}
	AddInfo(FString::Printf(TEXT("bird LODs %d, %d and %d triangles; LOD 0 %.0f x %.0f x %.0f cm of the unit mesh; %d vertices off the colour contract, %d faces wound inward; %d on a hinge, %d at a tip"),
		Tris[0], Tris[1], Tris[2], Box0.GetSize().X, Box0.GetSize().Y, Box0.GetSize().Z, BadColour, BadWinding, Hinges, Tips));
	for (int32 L = 0; L < SlonkFaunaMeshes::NumLods; ++L) { TestEqual(FString::Printf(TEXT("LOD %d's triangles"), L), Tris[L], WantTris[L]); }
	TestEqual(TEXT("every vertex as the material reads it"), BadColour, 0);
	TestEqual(TEXT("every face shows the side its normals point to"), BadWinding, 0);
	TestTrue(TEXT("wings hinged at the root, flexing to the tip"), Hinges > 0 && Tips > 0);
	TestTrue(TEXT("a unit mesh a metre across"), FMath::IsNearlyEqual(Box0.Min.Y, -50.0, 0.5) && FMath::IsNearlyEqual(Box0.Max.Y, 50.0, 0.5));
	FDynamicMesh3 Nothing;
	TestFalse(TEXT("nothing built yet for a plan without wings or legs (a fish)"), SlonkFaunaMeshes::BuildLod(SlonkFauna::EarthPlan(EFaunaKind::Leaper), 0, Nothing));

	// The material's motion over LOD 0's vertices, every seed, time, bank, startle and speck size, inside the box each
	// instance is culled by (UPlanetInstancesComponent::InstanceBox with the kind's padding).
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Flier);
	const FBox Padded = Box0.ExpandBy(Box0.GetExtent() * 0.15 + FVector(20.0 + D.CullPaddingUnits));
	const FDynamicMeshColorOverlay* C0 = Lod0.Attributes()->PrimaryColors();
	FRandomStream Rng(31);
	int32 Escaped = 0, Tried = 0, Detached = 0;
	double Margin = TNumericLimits<double>::Max();
	for (int32 Case = 0; Case < 160; ++Case)
	{
		FMotionCase M;
		const float Size = Rng.FRandRange(0.33f, 1.5f);
		M.Custom[SlonkFauna::Custom::SizeM] = Size;
		M.Custom[SlonkFauna::Custom::Seed] = Rng.FRand();
		M.Custom[SlonkFauna::Custom::Phase] = Rng.FRand();
		M.Custom[SlonkFauna::Custom::RateHz] = FMath::RoundToFloat(Rng.FRandRange(2.4f, 12.f) * 60.f) / 60.f;
		M.Custom[SlonkFauna::Custom::PerKind] = Rng.FRandRange(-3.2f, 3.2f);
		const FVector Fled = Rng.GetUnitVector();
		M.Group[SlonkFauna::GroupData::ScatterDirX] = (float)Fled.X;
		M.Group[SlonkFauna::GroupData::ScatterDirY] = (float)Fled.Y;
		M.Group[SlonkFauna::GroupData::ScatterDirZ] = (float)Fled.Z;
		M.Group[SlonkFauna::GroupData::Bank] = Rng.FRandRange(-0.8f, 0.8f);
		M.Group[SlonkFauna::GroupData::Startled] = Case % 2 == 0 ? 1.f : 0.f;
		M.Group[SlonkFauna::GroupData::SpreadM] = Rng.FRandRange(0.f, 10.f);
		const float T = Rng.FRandRange(0.f, 3600.f);
		M.Group[SlonkFauna::GroupData::ScatterT0] = FMath::Fmod(T - Rng.FRandRange(0.f, 60.f) + 3600.f, 3600.f);
		// Its panic anywhere in its two minutes, its beat anywhere in its coming-in or past it.
		const float PanicAge = Rng.FRandRange(0.f, 120.f);
		M.Group[SlonkFauna::GroupData::PanicT0] = FMath::Fmod(T - PanicAge + 3600.f, 3600.f);
		M.Group[SlonkFauna::GroupData::OnsetT0] = FMath::Fmod(T - PanicAge - Rng.FRandRange(0.f, 1.f) + 3600.f, 3600.f);
		// Near (no speck floor), at a few hundred metres, and far (four times its size).
		const float Dist = Case % 3 == 0 ? 1000.f : (Case % 3 == 1 ? Size * 100.f * 960.f / 1.f : 5.0e5f);
		for (const int32 V : Lod0.VertexIndicesItr())
		{
			const FVector3f LP = (FVector3f)Lod0.GetVertex(V);
			FVector4f Cv = FVector4f(0.f, 0.f, 0.5f, 0.f);
			for (const int32 El : C0->ElementIndicesItr()) { if (C0->GetParentVertex(El) == V) { Cv = C0->GetElement(El); break; } }
			const FLinearColor Col(Cv.X, Cv.Y, Cv.Z, Cv.W);
			const FVector3f Moved = LP + SlonkFauna::FlierMotion(LP, Col, T, M.Custom, M.Group, Dist, 960.f);
			++Tried;
			const FVector Mv((double)Moved.X, (double)Moved.Y, (double)Moved.Z);
			Escaped += Padded.IsInside(Mv) ? 0 : 1;
			Margin = FMath::Min(Margin, FMath::Min3(FMath::Min(Mv.X - Padded.Min.X, Padded.Max.X - Mv.X), FMath::Min(Mv.Y - Padded.Min.Y, Padded.Max.Y - Mv.Y), FMath::Min(Mv.Z - Padded.Min.Z, Padded.Max.Z - Mv.Z)));
		}
		// A wing's root stays on its hinge: it moves as the body does there, unbanked and at its own size.
		FMotionCase Level = M;
		Level.Group[SlonkFauna::GroupData::Bank] = 0.f;
		const FVector3f Root(0.f, 4.f, 1.f);
		const FVector3f Wing = SlonkFauna::FlierMotion(Root, FLinearColor(1.f, 0.f, 1.f, 0.5f), T, Level.Custom, Level.Group, 1000.f, 960.f);
		const FVector3f Rigid = SlonkFauna::FlierMotion(Root, FLinearColor(0.f, 0.f, 0.5f, 0.f), T, Level.Custom, Level.Group, 1000.f, 960.f);
		Detached += (Wing - Rigid).Size() < 1e-3f ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("%d vertices moved by the material: %d outside the box padded %.0f units (the closest %.0f units inside); %d wing roots off their hinge"), Tried, Escaped, D.CullPaddingUnits, Margin, Detached));
	TestEqual(TEXT("the material keeps every vertex in its instance's box"), Escaped, 0);
	TestEqual(TEXT("a wing's root stays on its hinge"), Detached, 0);
	// Those hold of the float copy (SlonkFauna::FlierMotion) only while the material's motion is what was copied: an edit
	// to the fliers' part of fauna_material.py's WPO_CODE alone fails here, until the copy is brought in line and the CRC
	// with it (the mote branch is Slonk.Fauna.MoteCloud's to hold).
	const FString Block = MaterialMotionBlock();
	const uint32 BlockCrc = FCrc::StrCrc32(*Block);
	AddInfo(FString::Printf(TEXT("fauna_material.py's WPO_CODE, the fliers' part: %d characters of code, CRC 0x%08x (0x%08x when the copy was made)"), Block.Len(), BlockCrc, MaterialMotionBlockCrc));
	TestTrue(TEXT("fauna_material.py's WPO_CODE found"), Block.Len() > 1000);
	TestEqual(TEXT("FlierMotion copies the material's motion as it is now"), BlockCrc, MaterialMotionBlockCrc);

	// The static mesh: made once and kept, three LODs at the kind's screen sizes; none for a plan with nothing built.
	UStaticMesh* Mesh = SlonkFaunaMeshes::GetMesh(Plan, EFaunaKind::Flier);
	TestTrue(TEXT("the bird's mesh is made"), Mesh != nullptr);
	TestTrue(TEXT("and kept"), Mesh == SlonkFaunaMeshes::GetMesh(Plan, EFaunaKind::Flier));
	if (Mesh) { TestEqual(TEXT("with three LODs"), Mesh->GetNumLODs(), SlonkFaunaMeshes::NumLods); }
	TestTrue(TEXT("no mesh for a plan with nothing built"), SlonkFaunaMeshes::GetMesh(SlonkFauna::EarthPlan(EFaunaKind::Leaper), EFaunaKind::Leaper) == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaBudgetTest, "Slonk.Fauna.Budget", SLONK_TEST_FLAGS)
bool FSlonkFaunaBudgetTest::RunTest(const FString& Parameters)
{
	// The kind table's caps hold the plan's budget (about 3,500 instances, at most 60 group sets); about any camera on an
	// Earth-like world the fliers keep to theirs (12 flocks, 1,000 birds, 60,000 triangles at their nearest LOD); a frame's
	// math for them (where each flies and how it is turned, its fright and the threats to it: not the component's writes
	// to the scene, which want a world to time) is far under 0.15 ms of the game thread; and a scan a sixth of a cell on
	// reads only the cells new to it.
	int32 Instances = 0, GroupSets = 0;
	for (int32 K = 0; K < SlonkFauna::NumKinds; ++K)
	{
		const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc((EFaunaKind)K);
		// The motes ride on the terrain's chunks, in no group sets of their own, and nothing caps them there: their row is
		// the clouds drawn whole (24 cards), which Slonk.Fauna.MotesNest holds them to about a camera in the densest country.
		Instances += D.MaxMembers;
		GroupSets += (EFaunaKind)K == EFaunaKind::Mote ? 0 : D.MaxGroups;
	}
	TestTrue(TEXT("about 3,500 instances at most"), Instances <= 3500);
	TestTrue(TEXT("at most 60 group sets"), GroupSets <= 60);

	const SlonkFauna::FFaunaWorld W = EarthFauna();
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Flier);
	TArray<SlonkFauna::FFaunaGroup> Groups;
	GroupsOverGlobe(W, 240, Groups);
	int32 MaxGroups = 0, MaxMembers = 0, Over = 0, Places = 0;
	double FirstMs = 0.0, MovedMs = 0.0, FrameMs = 0.0;
	int32 FirstCells = 0, MovedCells = 0, MovedRead = 0;
	SlonkFauna::FFaunaField Busiest;
	for (int32 i = 0; i < Groups.Num() && Places < 24; i += FMath::Max(Groups.Num() / 24, 1), ++Places)
	{
		SlonkFauna::FFaunaField F;
		F.SetWorld(W);
		TArray<uint64> Active;
		double Ms = 0.0;
		int32 Read = 0, Cells = 0;
		ScanAndSelect(F, CameraOver(W, Groups[i].Centre), Active, &Ms, &Read, &Cells);
		const int32 Members = F.ActiveMembers(EFaunaKind::Flier);
		Over += F.ActiveGroups(EFaunaKind::Flier) <= D.MaxGroups && Members <= D.MaxMembers ? 0 : 1;
		if (F.ActiveGroups(EFaunaKind::Flier) > MaxGroups) { MaxGroups = F.ActiveGroups(EFaunaKind::Flier); Busiest = F; }
		MaxMembers = FMath::Max(MaxMembers, Members);
		if (Places == 0)
		{
			FirstMs = Ms;
			FirstCells = Cells;
			// A sixth of a cell on (a scan's step), from this one's cells.
			ScanAndSelect(F, CameraOver(W, AlongGround(Groups[i].Centre, D.CellCm / 6.0 + 100.0, W.P.RadiusCm, 0.3)), Active, &MovedMs, &MovedRead, &MovedCells);
		}
	}
	// A frame's math for the busiest place, as UpdateView does it: every group's frame as it flies from the one before
	// (a third of them shaken, off their paths, turning and reading the ground under them), its fright and two threats
	// checked.
	const double R = W.P.RadiusCm;
	TMap<uint64, SlonkFauna::FPanic> Panics;
	TMap<uint64, SlonkFauna::FFaunaPose> Poses;
	int32 Shaken = 0, Nth = 0;
	for (const TPair<uint64, SlonkFauna::FFaunaField::FGroupRef>& A : Busiest.GetActive())
	{
		Poses.Add(A.Key, SlonkFauna::PathAt(*A.Value, R, 500.0));
		if (Nth++ % 3 != 0) { continue; }
		SlonkFauna::FPanic Panic;
		SlonkFauna::FFaunaThreat By;
		By.PosCm = SlonkFauna::PathAt(*A.Value, R, 495.0).PosCm + FVector(3000.0, 0.0, 0.0);
		By.AltitudeCm = 3000.0;
		if (SlonkFauna::StartleGroup(Panic, D, SlonkFauna::PathAt(*A.Value, R, 495.0).PosCm, By, 495.0)) { Panics.Add(A.Key, Panic); ++Shaken; }
	}
	SlonkFauna::FFaunaThreat Craft, Walker;
	Craft.AltitudeCm = 3000.0;
	Walker.bCraft = false;
	const double Start = FPlatformTime::Seconds();
	constexpr int32 Frames = 600;
	double Sink = 0.0;
	for (int32 f = 0; f < Frames; ++f)
	{
		const double Now = 500.0 + f / 60.0;
		for (const TPair<uint64, SlonkFauna::FFaunaField::FGroupRef>& A : Busiest.GetActive())
		{
			SlonkFauna::FPanic Panic = Panics.FindRef(A.Key);
			SlonkFauna::FFaunaPose& Was = Poses.FindChecked(A.Key);
			const SlonkFauna::FFaunaPose P = SlonkFauna::FlightAt(*A.Value, D, Panic, R, Now, Was, 1.0 / 60.0);
			Was = P;
			const FVector Pos = P.PosCm;
			Craft.PosCm = Pos + FVector(0.0, 0.0, 1.0e5);
			Walker.PosCm = Pos + FVector(1.0e5, 0.0, 0.0);
			if (SlonkFauna::StartleGroup(Panic, D, Pos, Craft, Now) || SlonkFauna::StartleGroup(Panic, D, Pos, Walker, Now)) { Panics.Add(A.Key, Panic); }
			Sink += Pos.X + P.Bank;
		}
	}
	FrameMs = (FPlatformTime::Seconds() - Start) * 1000.0 / Frames;
	const int32 Triangles = MaxMembers * 60;
	AddInfo(FString::Printf(TEXT("kind table: %d instances, %d group sets at most; about %d places on an Earth-like world, at most %d flocks and %d birds (%d triangles at LOD 0), %d over the caps"),
		Instances, GroupSets, Places, MaxGroups, MaxMembers, Triangles, Over));
	AddInfo(FString::Printf(TEXT("a frame's math on %d flocks (%d of them shaken) %.4f ms; a first scan %.1f ms over %d cells; one a sixth of a cell on %.1f ms, %d of its %d cells read anew"),
		Busiest.GetActive().Num(), Shaken, FrameMs, FirstMs, FirstCells, MovedMs, MovedRead, MovedCells));
	TestTrue(TEXT("the frames' places all found"), FMath::IsFinite(Sink));
	TestTrue(TEXT("places with flocks to try"), Places >= 8 && MaxGroups > 0);
	TestTrue(TEXT("shaken flocks in the frames timed"), Shaken > 0);
	TestEqual(TEXT("never over the fliers' caps"), Over, 0);
	TestTrue(TEXT("at most 60,000 triangles of birds at LOD 0"), Triangles <= 60000);
	TestTrue(TEXT("a frame's math far under 0.15 ms"), FrameMs < 0.15);
	TestTrue(TEXT("a scan a sixth of a cell on reads only the cells new to it"), MovedCells > 0 && MovedRead * 4 < MovedCells);
	return true;
}

namespace
{
	/** The coarsest depth whose chunks carry every mote cloud (their near level), on a world of that radius. */
	int32 MoteDepth(const FPlanetTerrainParams& P)
	{
		return FMath::CeilToInt32(FMath::Log2(P.RadiusCm * UE_DOUBLE_HALF_PI / SlonkScatter::MoteMaxEdgeCm));
	}

	/** How far off a mote cloud of a span comes down to a far LOD (1, 8 cards; 2, two), m, through a 90-degree view at 16:9
	 *  (the engine's screen size is the view's projection scale times the radius over the distance): on the terrain's GPU
	 *  instances by the radius they pick an instance's LOD by (their padded box's), else on the engine's own by the mesh's
	 *  bounds alone. */
	double MoteLodFromM(double SpanM, int32 Lod, bool bGpuInstances)
	{
		UStaticMesh* Mesh = SlonkFaunaMeshes::GetMoteCloudMesh();
		if (!Mesh) { return 0.0; }
		const double Scale = SpanM * 100.0 / SlonkFauna::UnitCm;
		double RadiusCm = Mesh->GetBounds().BoxExtent.Length() * Scale;
		if (bGpuInstances)
		{
			UPlanetInstancesComponent* Set = NewObject<UPlanetInstancesComponent>();
			Set->SetStaticMesh(Mesh);
			Set->SetPieces(TArray<FTransform>{ FTransform(FQuat::Identity, FVector::ZeroVector, FVector(Scale)) }, TConstArrayView<float>());
			RadiusCm = Set->GetMaxInstanceRadius();
		}
		const double Proj = (16.0 / 9.0) / FMath::Tan(FMath::DegreesToRadians(45.0));
		return Proj * RadiusCm / SlonkFaunaMeshes::MoteLodScreenSize[FMath::Clamp(Lod, 1, SlonkFaunaMeshes::NumLods - 1)] / 100.0;
	}

	/** Motes on, at a world's life. */
	SlonkScatter::FMoteSettings MotesOn(float Life = 1.f)
	{
		SlonkScatter::FMoteSettings M;
		M.bEnabled = true;
		M.Life = Life;
		return M;
	}

	/** The densest mote country over the globe (wet, level ground along a forest's edge), densest first, at most Count of
	 *  them and none within 30 km of another. */
	TArray<FVector> MoteSpots(const FPlanetTerrainParams& P, int32 Count)
	{
		struct FSpot { FVector Dir; double Score; };
		TArray<FSpot> Spots;
		const int32 N = 4000;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector Dir = Spread(i, N);
			const double D = SlonkScatter::MoteDensityAt(P, Dir);
			if (D > 0.05) { Spots.Add({ Dir, D }); }
		}
		Spots.Sort([](const FSpot& A, const FSpot& B) { return A.Score > B.Score; });
		TArray<FVector> Out;
		for (const FSpot& S : Spots)
		{
			if (Out.Num() >= Count) { break; }
			bool bNear = false;
			for (const FVector& O : Out) { bNear |= FVector::Dist(O, S.Dir) * P.RadiusCm < 3.0e6; }
			if (!bNear) { Out.Add(S.Dir); }
		}
		return Out;
	}

	struct FMotes
	{
		TArray<FTransform> Xf;
		TArray<float> Custom;
		TArray<double> Drawn;
		FVector Origin = FVector::ZeroVector;
		int32 Num() const { return Xf.Num(); }
		const float* At(int32 i) const { return Custom.GetData() + i * SlonkFauna::Custom::Num; }
	};
	FMotes BuildMotes(const FPlanetTerrainParams& P, const SlonkScatter::FMoteSettings& M, int32 Face, int32 Depth, int64 X, int64 Y, const FScatterSettings& S = FScatterSettings())
	{
		FMotes R;
		UPlanetTerrainComponent::BuildChunkMotesForTest(P, S, M, Face, Depth, X, Y, 64, R.Xf, R.Custom, R.Drawn, &R.Origin);
		return R;
	}

	/** What MotesFoundAgain makes of the clouds it finds again, summed over its calls. */
	struct FFoundAgain
	{
		/** The largest ratio of a cloud's two fades. */
		double WorstFade = 1.0;
		/** How many the child turns more than 10 degrees from the parent about the ground's up (their X axes laid flat
		 *  across the radial up), and the largest such turn, degrees. */
		int32 Turned = 0;
		double WorstTurnDeg = 0.0;
		/** How many stand on drawn triangles whose normals lie either side of 0.9 along the planet's z on the two levels. */
		int32 Straddling = 0;
	};

	/** How many of a coarse chunk's clouds its four children carry again: in the same place (within half a metre along
	 *  the ground), of the same span and seed, fading out at the same distance within a level's factor of two (the fade is
	 *  reckoned, as the plants' is, from the edge each chunk's build measures, which a level's chunks measure a little
	 *  differently); and what InOut makes of them. */
	int32 MotesFoundAgain(const FPlanetTerrainParams& P, const SlonkScatter::FMoteSettings& M, const FMotes& Parent, int32 Face, int32 Depth, int64 X, int64 Y, int32& OutKids,
		FFoundAgain* InOut = nullptr)
	{
		FMotes Kids[4];
		OutKids = 0;
		for (int32 c = 0; c < 4; ++c) { Kids[c] = BuildMotes(P, M, Face, Depth + 1, X * 2 + (c & 1), Y * 2 + (c >> 1)); OutKids += Kids[c].Num(); }
		int32 Matched = 0;
		for (int32 i = 0; i < Parent.Num(); ++i)
		{
			const FVector Ua = Parent.Xf[i].GetLocation().GetSafeNormal();
			const float* A = Parent.At(i);
			bool bFound = false;
			for (const FMotes& Kid : Kids)
			{
				for (int32 j = 0; j < Kid.Num() && !bFound; ++j)
				{
					const double Apart = FMath::Acos(FMath::Clamp(FVector::DotProduct(Ua, Kid.Xf[j].GetLocation().GetSafeNormal()), -1.0, 1.0)) * P.RadiusCm;
					const float* B = Kid.At(j);
					const float Fa = A[SlonkFauna::Custom::FadeCm], Fb = B[SlonkFauna::Custom::FadeCm];
					bFound = Apart < 50.0 && A[SlonkFauna::Custom::SizeM] == B[SlonkFauna::Custom::SizeM] && A[SlonkFauna::Custom::Seed] == B[SlonkFauna::Custom::Seed]
						&& Fa > 0.f && Fb > 0.f && FMath::Max(Fa, Fb) <= 2.01f * FMath::Min(Fa, Fb);
					if (bFound && InOut)
					{
						InOut->WorstFade = FMath::Max(InOut->WorstFade, (double)FMath::Max(Fa, Fb) / FMath::Min(Fa, Fb));
						auto Flat = [&Ua](const FVector& Ax) { return (Ax - Ua * FVector::DotProduct(Ax, Ua)).GetSafeNormal(); };
						const FQuat Ra = Parent.Xf[i].GetRotation(), Rb = Kid.Xf[j].GetRotation();
						const double TurnDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Flat(Ra.GetAxisX()), Flat(Rb.GetAxisX())), -1.0, 1.0)));
						InOut->Turned += TurnDeg > 10.0 ? 1 : 0;
						InOut->WorstTurnDeg = FMath::Max(InOut->WorstTurnDeg, TurnDeg);
						InOut->Straddling += (FMath::Abs(Ra.GetAxisZ().Z) < 0.9) != (FMath::Abs(Rb.GetAxisZ().Z) < 0.9) ? 1 : 0;
					}
				}
				if (bFound) { break; }
			}
			Matched += bFound ? 1 : 0;
		}
		return Matched;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaMotesNestTest, "Slonk.Fauna.MotesNest", SLONK_TEST_FLAGS)
bool FSlonkFaunaMotesNestTest::RunTest(const FString& Parameters)
{
	// A mote cloud a chunk carries is carried again by its four children, in the same place, of the same span and seed,
	// fading out at the same distance (within the level's factor of two the plants' fade allows): on the motes' own
	// levels, where a chunk carries all of them, and from their far level, where it carries a share, down to the first
	// of their own. The far level keeps about MoteKeep of them, and those fade twice as far off. The children turn every
	// cloud as the parent does about the ground's up; on a world without ice caps, on the lines where the ground's up runs
	// 0.9 along the planet's z, too, where a turn reckoned from the drawn triangles' normals would swing from one level to
	// the next as the normals under a cloud fall either side of it. About a camera in the densest country the clouds
	// drawn whole keep to the motes' row of the budget (FFaunaKindDesc::MaxMembers).
	const FPlanetTerrainParams P = FaunaEarthParams(6371.0);
	const SlonkScatter::FMoteSettings M = MotesOn();
	const int32 D = MoteDepth(P);
	int32 Tried = 0, Clouds = 0, Matched = 0, FarClouds = 0, FarMatched = 0, FarKids = 0, FarFadesFurther = 0;
	FFoundAgain Near, Again;
	double AreaKm2 = 0.0;
	for (const FVector& Dir : MoteSpots(P, 12))
	{
		int32 Face; int64 X, Y;
		if (!UPlanetTerrainComponent::ChunkAt(Dir, D, Face, X, Y)) { continue; }
		const FMotes Parent = BuildMotes(P, M, Face, D, X, Y);
		if (Parent.Num() < 10) { continue; }
		++Tried;
		int32 Kids = 0;
		Clouds += Parent.Num();
		Matched += MotesFoundAgain(P, M, Parent, Face, D, X, Y, Kids, &Near);
		AreaKm2 += FMath::Square(P.RadiusCm * UE_DOUBLE_HALF_PI / (double)((int64)1 << D) / 1.0e5);
		// The far level's chunk over this one, and its children (this one among them).
		int32 FF; int64 FX, FY;
		UPlanetTerrainComponent::ChunkAt(Dir, D - 1, FF, FX, FY);
		const FMotes Far = BuildMotes(P, M, FF, D - 1, FX, FY);
		FarClouds += Far.Num();
		FarMatched += MotesFoundAgain(P, M, Far, FF, D - 1, FX, FY, Kids, &Again);
		FarKids += Kids;
		// The near level's fade: the nearest any cloud of this chunk fades by (most are the near level's alone).
		float NearFade = TNumericLimits<float>::Max();
		for (int32 i = 0; i < Parent.Num(); ++i) { NearFade = FMath::Min(NearFade, Parent.At(i)[SlonkFauna::Custom::FadeCm]); }
		for (int32 i = 0; i < Far.Num(); ++i) { FarFadesFurther += Far.At(i)[SlonkFauna::Custom::FadeCm] > NearFade * 1.5f ? 1 : 0; }
		if (Tried >= 4) { break; }
	}
	// The densest mote country on the lines where the ground's up runs 0.9 along the planet's z, north and south, of the
	// world as it would be without its ice caps and with its snowline three times as high (an Earth's caps reach down past
	// those lines): the chunk there and its children.
	FPlanetTerrainParams Thawed = P;
	Thawed.IceCapStart = 2.f;
	Thawed.SnowlineCm = P.SnowlineCm * 3.0;
	int32 LineChunks = 0, LineClouds = 0, LineMatched = 0;
	FFoundAgain Line;
	for (const double Zs : { 0.9, -0.9 })
	{
		const int32 Around = 8000;
		const double Rxy = FMath::Sqrt(1.0 - Zs * Zs);
		FVector Best = FVector::ZeroVector;
		double BestDensity = 0.0;
		for (int32 a = 0; a < Around; ++a)
		{
			const double A = UE_DOUBLE_TWO_PI * (a + 0.5) / Around;
			const FVector Dir(Rxy * FMath::Cos(A), Rxy * FMath::Sin(A), Zs);
			const double Dn = SlonkScatter::MoteDensityAt(Thawed, Dir);
			if (Dn > BestDensity) { BestDensity = Dn; Best = Dir; }
		}
		int32 Face; int64 X, Y;
		if (BestDensity <= 0.0 || !UPlanetTerrainComponent::ChunkAt(Best, D, Face, X, Y)) { continue; }
		const FMotes Parent = BuildMotes(Thawed, M, Face, D, X, Y);
		if (Parent.Num() == 0) { continue; }
		++LineChunks;
		int32 Kids = 0;
		LineClouds += Parent.Num();
		LineMatched += MotesFoundAgain(Thawed, M, Parent, Face, D, X, Y, Kids, &Line);
	}
	const double PerKm2 = AreaKm2 > 0.0 ? Clouds / AreaKm2 : 0.0;
	// And the land's mean: the share of its cells holding a cloud, over the whole globe's land.
	double LandSum = 0.0;
	int32 LandPoints = 0;
	for (int32 i = 0; i < 4000; ++i)
	{
		const FVector Dir = Spread(i, 4000);
		if (SlonkTerrain::Sample(P, Dir, 1.0e4).bWater) { continue; }
		++LandPoints;
		LandSum += FMath::Min(SlonkScatter::MoteDensityAt(P, Dir), 0.9);
	}
	const double CellKm = P.RadiusCm * UE_DOUBLE_HALF_PI / FMath::Pow(2.0, FMath::RoundToDouble(FMath::Log2(P.RadiusCm * UE_DOUBLE_HALF_PI / SlonkScatter::MoteSpacingCm))) / 1.0e5;
	const double LandPerKm2 = LandPoints > 0 ? LandSum / LandPoints / (CellKm * CellKm) : 0.0;
	// About a camera in country as dense as this: every cloud out to the near level's fade, the far level's share past it.
	const double NearKm = 3.0 * P.RadiusCm * UE_DOUBLE_HALF_PI / (double)((int64)1 << D) / 1.0e5, FarKm = NearKm * 2.0;
	const double Reach = UE_DOUBLE_PI * (NearKm * NearKm + SlonkScatter::MoteKeep * (FarKm * FarKm - NearKm * NearKm));
	// And how they are drawn there, on the GPU instances, for spans spread evenly over a cloud's 6 to 9 m: whole (24 cards)
	// nearer than their LOD 1 comes in, 8 cards to their LOD 2, 2 past it (MoteLodFromM).
	auto CloudsWithin = [&](double Km)
	{
		const double Nk = FMath::Min(Km, NearKm), Fk = FMath::Clamp(Km, NearKm, FarKm);
		return PerKm2 * UE_DOUBLE_PI * (Nk * Nk + SlonkScatter::MoteKeep * (Fk * Fk - NearKm * NearKm));
	};
	double Whole = 0.0, Triangles = 0.0, LodNearKm = 0.0;
	const int32 Spans = 12;
	for (int32 s = 0; s < Spans; ++s)
	{
		const double SpanM = 6.0 + 3.0 * (s + 0.5) / Spans;
		const double L1 = MoteLodFromM(SpanM, 1, true) / 1000.0, L2 = MoteLodFromM(SpanM, 2, true) / 1000.0;
		const double N0 = CloudsWithin(L1), N1 = CloudsWithin(L2) - N0, N2 = CloudsWithin(FarKm) - N0 - N1;
		Whole += N0 / Spans;
		LodNearKm += L1 / Spans;
		Triangles += 2.0 * (SlonkFauna::Mote::CardsAtLod[0] * N0 + SlonkFauna::Mote::CardsAtLod[1] * N1 + SlonkFauna::Mote::CardsAtLod[2] * N2) / Spans;
	}
	const int32 WholeBudget = SlonkFauna::KindDesc(EFaunaKind::Mote).MaxMembers;
	AddInfo(FString::Printf(TEXT("%d clouds on %d chunks of depth %d (%.0f a square km in the densest country, %.0f on the land's mean): %d found again on the children (their fades at most %.3f times apart); the far level %d clouds of %d under it (%.0f%%), %d found again, %d fading further"),
		Clouds, Tried, D, PerKm2, LandPerKm2, Matched, Near.WorstFade, FarClouds, FarKids, FarKids > 0 ? 100.0 * FarClouds / FarKids : 0.0, FarMatched, FarFadesFurther));
	AddInfo(FString::Printf(TEXT("fading out by %.0f m, the far level's by %.0f m: about a camera %.0f clouds in the densest country (%.0f of them drawn whole, within about %.0f m, of a budget of %d; %.0f triangles in all), %.0f on the land's mean"),
		NearKm * 1000.0, FarKm * 1000.0, PerKm2 * Reach, Whole, LodNearKm * 1000.0, WholeBudget, Triangles, LandPerKm2 * Reach));
	AddInfo(FString::Printf(TEXT("turned more than 10 deg on the children: %d of those found again (at most %.2f deg); thawed, on the lines at z 0.9, %d clouds on %d chunks, %d found again (%d of them on ground whose normal lies either side of z 0.9 on the two levels), %d turned (at most %.2f deg)"),
		Near.Turned + Again.Turned, FMath::Max(Near.WorstTurnDeg, Again.WorstTurnDeg), LineClouds, LineChunks, LineMatched, Line.Straddling, Line.Turned, Line.WorstTurnDeg));
	TestTrue(TEXT("found mote country"), Tried > 0);
	TestEqual(TEXT("the children turn every cloud as the parent does"), Near.Turned + Again.Turned, 0);
	TestTrue(TEXT("mote country on the lines at z 0.9 of the thawed world"), LineChunks > 0 && LineClouds >= 10 && LineMatched >= LineClouds * 0.85);
	TestTrue(TEXT("some of it on ground whose normal crosses z 0.9 between the levels"), Line.Straddling > 0);
	TestEqual(TEXT("and the clouds there turned alike"), Line.Turned, 0);
	TestTrue(TEXT("nearly every cloud is found again on the finer chunks"), Matched >= Clouds * 0.85);
	TestTrue(TEXT("and every far-level cloud on the motes' own"), FarClouds > 0 && FarMatched >= FarClouds * 0.85);
	TestTrue(TEXT("the far level keeps about a quarter"), FarKids > 0 && FarClouds > FarKids * (SlonkScatter::MoteKeep - 0.1) && FarClouds < FarKids * (SlonkScatter::MoteKeep + 0.1));
	TestEqual(TEXT("the far level's fade further off"), FarFadesFurther, FarClouds);
	TestTrue(TEXT("the clouds drawn whole about a camera in the densest country keep to the motes' row of the budget"), Whole > 0.0 && Whole <= WholeBudget);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaMotesOnLandTest, "Slonk.Fauna.MotesOnLand", SLONK_TEST_FLAGS)
bool FSlonkFaunaMotesOnLandTest::RunTest(const FString& Parameters)
{
	// A cloud hovers over land, never water, standing on the drawn ground, up along it, its custom floats as the
	// material reads them; on a coast the clouds keep to the land. The golden CRCs never see the motes: the build's CRC
	// hashes the ground and the scatter, never the chunk's fauna, and the golden tests build with the motes off. A chunk
	// with its motes on hashes as it does with them off, which the build's order makes so today (the scatter is placed,
	// and the settlements it keeps out of looked for, before the motes are): the check stands guard, so a change that lets
	// the motes touch the ground or the scatter fails here.
	const FPlanetTerrainParams P = FaunaEarthParams(6371.0);
	const SlonkScatter::FMoteSettings M = MotesOn();
	const int32 D = MoteDepth(P);
	// Coasts: land with the sea 300 m off.
	TArray<FVector> Coasts;
	for (int32 i = 0; i < 6000 && Coasts.Num() < 6; ++i)
	{
		const FVector Dir = Spread(i, 6000);
		if (SlonkScatter::MoteDensityAt(P, Dir) < 0.1) { continue; }
		const FVector Off = AlongGround(Dir, 3.0e4, P.RadiusCm, 0.0);
		if (SlonkTerrain::Sample(P, Off, 1000.0).bWater) { Coasts.Add(Dir); }
	}
	TArray<FVector> Spots = MoteSpots(P, 4);
	const int32 Inland = Spots.Num();
	Spots.Append(Coasts);
	int32 Clouds = 0, Wet = 0, Floating = 0, Tilted = 0, BadFloats = 0, CoastClouds = 0, CoastChunks = 0, CrcSame = 0, CrcSettledSame = 0, CrcTried = 0;
	for (int32 s = 0; s < Spots.Num(); ++s)
	{
		int32 Face; int64 X, Y;
		if (!UPlanetTerrainComponent::ChunkAt(Spots[s], D, Face, X, Y)) { continue; }
		const FMotes R = BuildMotes(P, M, Face, D, X, Y);
		const double CellCm = P.RadiusCm * UE_DOUBLE_HALF_PI / (double)((int64)1 << D) / 64.0;
		if (s >= Inland && R.Num() > 0) { ++CoastChunks; CoastClouds += R.Num(); }
		for (int32 i = 0; i < R.Num(); ++i)
		{
			++Clouds;
			const FVector Pos = R.Xf[i].GetLocation();
			const FVector Up = Pos.GetSafeNormal();
			Wet += SlonkTerrain::Sample(P, Up, CellCm * 2.0).bWater ? 1 : 0;
			Floating += FMath::Abs(Pos.Size() - P.RadiusCm - R.Drawn[i]) > 10.0 ? 1 : 0;
			Tilted += FVector::DotProduct(R.Xf[i].GetRotation().GetAxisZ(), Up) < 0.5 ? 1 : 0;
			const float* C = R.At(i);
			const float Span = C[SlonkFauna::Custom::SizeM], Rate = C[SlonkFauna::Custom::RateHz] * 60.f;
			const bool bOk = Span >= 6.f && Span <= 9.f && FMath::IsNearlyEqual((double)R.Xf[i].GetScale3D().X, (double)Span, 1e-4)
				&& C[SlonkFauna::Custom::Glow] >= 0.75f && C[SlonkFauna::Custom::Glow] <= 1.25f
				&& FMath::IsNearlyEqual(Rate, FMath::RoundToFloat(Rate), 1e-3f) && Rate >= 3.f - 1e-3f && Rate <= 7.f + 1e-3f
				&& C[SlonkFauna::Custom::PerKind] >= 0.35f && C[SlonkFauna::Custom::PerKind] <= 0.9f && C[SlonkFauna::Custom::FadeCm] > 1.0e4f
				&& C[SlonkFauna::Custom::Seed] >= 0.f && C[SlonkFauna::Custom::Seed] < 1.f && C[SlonkFauna::Custom::Phase] >= 0.f && C[SlonkFauna::Custom::Phase] < 1.f
				&& C[SlonkFauna::Custom::PigmentR] >= M.Pollen.R * 0.8f - 1e-4f && C[SlonkFauna::Custom::PigmentR] <= M.Pollen.R * 1.2f + 1e-4f;
			BadFloats += bOk ? 0 : 1;
		}
		// The build's CRC (the drawn ground, its attributes, the rocks and plants) with the motes on, the build carrying the
		// clouds just counted, and off; and on a world with settlements, whose keep-out the motes share with the scatter.
		if (R.Num() > 0 && CrcTried < 4)
		{
			FScatterSettings Living;
			Living.Life = 1.f;
			FScatterSettings Settled = Living;
			Settled.bCities = true;
			++CrcTried;
			CrcSame += UPlanetTerrainComponent::ChunkBuildCrcForTest(P, Living, Face, D, X, Y, 64, &M) == UPlanetTerrainComponent::ChunkBuildCrcForTest(P, Living, Face, D, X, Y) ? 1 : 0;
			CrcSettledSame += UPlanetTerrainComponent::ChunkBuildCrcForTest(P, Settled, Face, D, X, Y, 64, &M) == UPlanetTerrainComponent::ChunkBuildCrcForTest(P, Settled, Face, D, X, Y) ? 1 : 0;
		}
	}
	AddInfo(FString::Printf(TEXT("%d clouds (%d on %d coast chunks): %d over water, %d off the drawn ground, %d not up along it, %d with custom floats off the contract; the build's CRC the same with motes on on %d of %d chunks (%d with settlements)"),
		Clouds, CoastClouds, CoastChunks, Wet, Floating, Tilted, BadFloats, CrcSame, CrcTried, CrcSettledSame));
	TestTrue(TEXT("clouds over the land"), Clouds > 0);
	TestTrue(TEXT("and on the coasts"), CoastChunks > 0);
	TestTrue(TEXT("none over water"), Wet <= Clouds / 200);
	TestEqual(TEXT("every cloud on the drawn ground"), Floating, 0);
	TestEqual(TEXT("every cloud up along the ground"), Tilted, 0);
	TestEqual(TEXT("every cloud's custom floats as the material reads them"), BadFloats, 0);
	TestTrue(TEXT("the build is the same with motes on"), CrcTried > 0 && CrcSame == CrcTried);
	TestEqual(TEXT("and with settlements"), CrcSettledSame, CrcTried);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaMotesTownsTest, "Slonk.Fauna.MotesTowns", SLONK_TEST_FLAGS)
bool FSlonkFaunaMotesTownsTest::RunTest(const FString& Parameters)
{
	// On a world with settlements the motes keep out of the towns, as the plants do (but for the parks), and off the
	// painted ways; the same ground without the rule carries them.
	const FPlanetTerrainParams P = FaunaEarthParams(6371.0);
	const SlonkScatter::FMoteSettings M = MotesOn();
	const FCivilization Civ = SlonkCivs::Colonists();
	const int32 D = MoteDepth(P);
	TArray<FTownSite> Found;
	for (int32 i = 0; i < 3000 && Found.Num() < 3; ++i)
	{
		const FVector Dir = Spread(i, 3000);
		if (SlonkTerrain::SampleStructure(P, Dir).bWater) { continue; }
		TArray<FTownSite> Towns;
		SlonkCities::EnumerateTowns(P, Civ, Dir * P.RadiusCm, 3.0e6, Towns, 8);
		for (const FTownSite& T : Towns)
		{
			if (T.bCity || T.bCliff || SlonkScatter::MoteDensityAt(P, T.Dir) < 0.1) { continue; }
			if (Found.ContainsByPredicate([&](const FTownSite& F) { return F.Seed == T.Seed; })) { continue; }
			Found.Add(T);
			break;
		}
	}
	if (!TestTrue(TEXT("a town in mote country on the test world"), Found.Num() > 0)) { return true; }
	FScatterSettings Settled;
	Settled.bCities = true;
	const FScatterSettings Wild;
	int32 InTown = 0, InTownWild = 0, OnWay = 0, OnWayWild = 0, RoadOn = 0, RoadOnWild = 0, RoadChunks = 0;
	auto Count = [&](const FMotes& R, const FTownSite& T, int32& OutInTown, int32& OutOnWay)
	{
		for (const FTransform& Xf : R.Xf)
		{
			const FVector Up = Xf.GetLocation().GetSafeNormal();
			OutInTown += FVector::Dist(Up * P.RadiusCm, T.LocalCm) < T.RadiusCm && SlonkCities::ParkAt(P, T, Up) < 0.7f ? 1 : 0;
			OutOnWay += SlonkCities::PaintedWayCm(P, Civ, Up) < 150.0 ? 1 : 0;
		}
	};
	for (const FTownSite& T : Found)
	{
		int32 Face; int64 X, Y;
		if (!TestTrue(TEXT("the town's chunk"), UPlanetTerrainComponent::ChunkAt(T.Dir, D, Face, X, Y))) { continue; }
		Count(BuildMotes(P, M, Face, D, X, Y, Settled), T, InTown, OnWay);
		Count(BuildMotes(P, M, Face, D, X, Y, Wild), T, InTownWild, OnWayWild);
	}
	// The roads out of them, past their ground (so only the way, not the town, keeps the motes off): chunks on a painted
	// road in mote country, a few for each town, searched for on rings round it every 3 m (a road is a few metres wide).
	TSet<FIntVector> Seen;
	for (const FTownSite& T : Found)
	{
		int32 Here = 0;
		for (double Cm = T.RadiusCm + 4.0e4; Cm <= T.RadiusCm + 1.6e5 && Here < 3; Cm += 4.0e4)
		{
			const int32 Steps = FMath::CeilToInt32(UE_DOUBLE_TWO_PI * Cm / 300.0);
			for (int32 b = 0; b < Steps && Here < 3; ++b)
			{
				const FVector Dir = AlongGround(T.Dir, Cm, P.RadiusCm, b * UE_DOUBLE_TWO_PI / Steps);
				if (SlonkCities::PaintedWayCm(P, Civ, Dir) >= 0.0 || SlonkScatter::MoteDensityAt(P, Dir) < 0.05) { continue; }
				TArray<FTownSite> Near;
				SlonkCities::EnumerateTowns(P, Civ, Dir * P.RadiusCm, 2.0e5, Near, 16);
				if (Near.ContainsByPredicate([&](const FTownSite& N) { return FVector::Dist(N.LocalCm, Dir * P.RadiusCm) < N.RadiusCm + 2500.0 + 3.0e4; })) { continue; }
				int32 Face; int64 X, Y;
				if (!UPlanetTerrainComponent::ChunkAt(Dir, D, Face, X, Y)) { continue; }
				const FIntVector Key(Face, (int32)X, (int32)Y);
				if (Seen.Contains(Key)) { continue; }
				Seen.Add(Key);
				++Here; ++RoadChunks;
				auto OnRoad = [&](const FMotes& R)
				{
					int32 N = 0;
					for (const FTransform& Xf : R.Xf) { N += SlonkCities::PaintedWayCm(P, Civ, Xf.GetLocation().GetSafeNormal()) < 150.0 ? 1 : 0; }
					return N;
				};
				RoadOn += OnRoad(BuildMotes(P, M, Face, D, X, Y, Settled));
				RoadOnWild += OnRoad(BuildMotes(P, M, Face, D, X, Y, Wild));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d towns in mote country: %d clouds in their streets and lots and %d on the ways, %d and %d without the rule; on %d chunks of the roads out of them, %d clouds on the road, %d without the rule"),
		Found.Num(), InTown, OnWay, InTownWild, OnWayWild, RoadChunks, RoadOn, RoadOnWild));
	TestTrue(TEXT("the same ground would carry motes without the rule"), InTownWild > 0);
	TestEqual(TEXT("no motes in a town's streets and lots"), InTown, 0);
	TestEqual(TEXT("none on the ways"), OnWay, 0);
	TestTrue(TEXT("road chunks in mote country found"), RoadChunks > 0);
	TestTrue(TEXT("the same roads would carry motes without the rule"), RoadOnWild > 0);
	TestEqual(TEXT("none on the roads"), RoadOn, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaMotesLifeTest, "Slonk.Fauna.MotesLife", SLONK_TEST_FLAGS)
bool FSlonkFaunaMotesLifeTest::RunTest(const FString& Parameters)
{
	// Motes where there is life and air, the more the more life: none on a dead world, none without air, none with the
	// motes off or their density 0; on a desert's quarter of life a fraction of a living world's. On a world the tides
	// hold, none on the frozen far side, some round the terminator.
	const FPlanetTerrainParams P = FaunaEarthParams(6371.0);
	FPlanetTerrainParams Airless = P;
	Airless.bAirless = true;
	SlonkScatter::FMoteSettings Off = MotesOn();
	Off.bEnabled = false;
	SlonkScatter::FMoteSettings None = MotesOn();
	None.Density = 0.f;
	const int32 D = MoteDepth(P);
	int32 Living = 0, Quarter = 0, Dead = 0, NoAir = 0, Disabled = 0, Thin = 0;
	for (const FVector& Dir : MoteSpots(P, 4))
	{
		int32 Face; int64 X, Y;
		if (!UPlanetTerrainComponent::ChunkAt(Dir, D, Face, X, Y)) { continue; }
		Living += BuildMotes(P, MotesOn(1.f), Face, D, X, Y).Num();
		Quarter += BuildMotes(P, MotesOn(0.25f), Face, D, X, Y).Num();
		Dead += BuildMotes(P, MotesOn(0.f), Face, D, X, Y).Num();
		NoAir += BuildMotes(Airless, MotesOn(1.f), Face, D, X, Y).Num();
		Disabled += BuildMotes(P, Off, Face, D, X, Y).Num();
		Thin += BuildMotes(P, None, Face, D, X, Y).Num();
	}
	TestEqual(TEXT("the field's mote density is 0 without air"), SlonkScatter::MoteDensityAt(Airless, FVector(0.3, 0.8, 0.5)), 0.0);
	TestEqual(TEXT("no chunk carries motes switched off"), SlonkScatter::MoteMaxChunkEdgeCm(FScatterSettings(), Off), 0.0);
	// Held to its star along +X: its far side frozen, its terminator temperate.
	FPlanetTerrainParams Locked = P;
	const double T0 = SlonkTerrain::LapseNPerCm * Locked.SnowlineCm;
	Locked.Climate = EClimateModel::Locked;
	Locked.ClimateAxis = FVector::XAxisVector;
	Locked.DayN = (float)(T0 * 2.8);
	Locked.TermN = (float)(T0 * 0.6);
	Locked.NightN = -0.4f;
	Locked.IceCapStart = 2.f;
	int32 FarSide = 0, FarSideLand = 0, Ring = 0, RingLand = 0;
	for (int32 i = 0; i < 6000; ++i)
	{
		const FVector Dir = Spread(i, 6000);
		const double Mu = Dir.X;
		if (Mu > -0.6 && FMath::Abs(Mu) > 0.15) { continue; }
		if (SlonkTerrain::Sample(Locked, Dir, 1.0e4).bWater) { continue; }
		const bool bMotes = SlonkScatter::MoteDensityAt(Locked, Dir) > 0.0;
		if (Mu <= -0.6) { ++FarSideLand; FarSide += bMotes ? 1 : 0; }
		else { ++RingLand; Ring += bMotes ? 1 : 0; }
	}
	AddInfo(FString::Printf(TEXT("clouds on 4 chunks: %d at full life, %d at a quarter, %d at none, %d without air, %d switched off, %d at density 0; a locked world: %d of %d far-side points with motes, %d of %d round the terminator"),
		Living, Quarter, Dead, NoAir, Disabled, Thin, FarSide, FarSideLand, Ring, RingLand));
	TestTrue(TEXT("motes on a living world"), Living > 0);
	TestTrue(TEXT("fewer at a quarter of the life"), Quarter > 0 && Quarter < Living * 0.5);
	TestEqual(TEXT("none on a dead world"), Dead, 0);
	TestEqual(TEXT("none without air"), NoAir, 0);
	TestEqual(TEXT("none switched off"), Disabled, 0);
	TestEqual(TEXT("none at density 0"), Thin, 0);
	TestTrue(TEXT("none on a locked world's frozen far side"), FarSideLand > 0 && FarSide == 0);
	TestTrue(TEXT("some round its terminator"), Ring > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaMoteCloudTest, "Slonk.Fauna.MoteCloud", SLONK_TEST_FLAGS)
bool FSlonkFaunaMoteCloudTest::RunTest(const FString& Parameters)
{
	// The mote cloud: 24, 8 and 2 cards, each a quad whose middle the material finds from any of its corners by its UV,
	// over a disc half a span round from 0.03 to 0.31 of a span up, each card's colour as the material reads it (a mote's
	// surface, how many of the 24 it stands for, its own random, a gnat's under the share), the gnats' six on the near
	// LODs and none on the far one, the other eighteen on every LOD; every card, turned to a camera as the material turns
	// it (a copy of the material's turn, held to the script by its mote branch's CRC), showing the camera its front; its
	// mesh made once, three LODs, its bounds grown by the motes' reach, and its LODs coming in where FaunaMeshes.h says on
	// both the terrain's GPU instances and the engine's own.
	namespace Mo = SlonkFauna::Mote;
	using namespace UE::Geometry;
	int32 BadCard = 0, BadColour = 0, Repeated = 0, Tris[SlonkFaunaMeshes::NumLods] = {}, Free[SlonkFaunaMeshes::NumLods] = {}, Gnats[SlonkFaunaMeshes::NumLods] = {};
	int32 Faced = 0, BackToCamera = 0;
	// The ways to a camera, in the cloud's frame: across it, from above and below to one side, and straight down onto it.
	const FVector Views[] = { FVector(1.0, 0.0, 0.0), FVector(0.3, -0.8, 0.5).GetSafeNormal(), FVector(-0.6, 0.2, -0.3).GetSafeNormal(), FVector(0.0, 0.0, 1.0) };
	FBox Box0(ForceInit);
	for (int32 L = 0; L < SlonkFaunaMeshes::NumLods; ++L)
	{
		FDynamicMesh3 M;
		SlonkFaunaMeshes::BuildMoteCloudLod(L, M);
		Tris[L] = M.TriangleCount();
		const FDynamicMeshUVOverlay* UV = M.Attributes()->PrimaryUV();
		const FDynamicMeshColorOverlay* C = M.Attributes()->PrimaryColors();
		const FDynamicMeshNormalOverlay* N = M.Attributes()->PrimaryNormals();
		TSet<int32> Randoms;
		// The vertices come four to a card, their elements with them.
		for (int32 V0 = 0; V0 + 3 < M.MaxVertexID(); V0 += 4)
		{
			FVector Middle[4];
			const FVector4f Col = C->GetElement(V0);
			for (int32 k = 0; k < 4; ++k)
			{
				const FVector P = (FVector)M.GetVertex(V0 + k);
				const FVector2f T = UV->GetElement(V0 + k);
				if (L == 0) { Box0 += P; }
				Middle[k] = P - FVector(2.0 * T.X - 1.0, 0.0, 2.0 * T.Y - 1.0) * Mo::CardHalfCm;
				BadColour += C->GetElement(V0 + k) == Col && FVector(N->GetElement(V0 + k)).Equals(FVector::ZAxisVector, 1e-4) ? 0 : 1;
			}
			bool bCard = true;
			for (int32 k = 1; k < 4; ++k) { bCard &= Middle[k].Equals(Middle[0], 1e-4); }
			const double Rr = FVector2D(Middle[0].X, Middle[0].Y).Size();
			bCard &= Rr <= 46.0 && Middle[0].Z >= 3.0 && Middle[0].Z <= 31.0;
			BadCard += bCard ? 0 : 1;
			const int32 Stands = FMath::RoundToInt32(Col.Y * Mo::MotesPerCloud);
			const bool bGnat = Col.Z < Mo::GnatShare;
			(bGnat ? Gnats[L] : Free[L]) += Stands;
			BadColour += Col.X == SlonkFauna::Vc::SurfaceMote && Col.W == SlonkFauna::Vc::JointRigid && Stands >= 1 && Stands <= 12 && Col.Z >= 0.f && Col.Z < 1.f
				&& FMath::Abs(Col.Z - Mo::GnatShare) > 0.01f ? 0 : 1;
			bool bAlready = false;
			Randoms.Add(FMath::RoundToInt32(Col.Z * 255.f), &bAlready);
			Repeated += bAlready ? 1 : 0;
		}
		// Each card turned as the material turns it to face the camera (fauna_material.py, WPO_CODE's mote branch, tied to
		// this copy by its CRC below): a corner at its middle
		// plus (rt (2u - 1) + upv (2v - 1)) times its half-width, rt = z x v (x where v is straight up) and upv = v x rt,
		// v the way to the camera. The front the engine draws a triangle (A, B, C) by, (C - A) x (B - A), toward the camera.
		for (const int32 T : M.TriangleIndicesItr())
		{
			const FIndex3i Tv = M.GetTriangle(T), Tu = UV->GetTriangle(T);
			for (const FVector& V : Views)
			{
				FVector Rt = FVector::CrossProduct(FVector::ZAxisVector, V);
				Rt = Rt.SizeSquared() > 1e-6 ? Rt.GetSafeNormal() : FVector::XAxisVector;
				const FVector Upv = FVector::CrossProduct(V, Rt);
				FVector Q[3];
				for (int32 k = 0; k < 3; ++k)
				{
					const FVector2f Uv = UV->GetElement(Tu[k]);
					const FVector Cn(2.0 * Uv.X - 1.0, 0.0, 2.0 * Uv.Y - 1.0);
					Q[k] = (FVector)M.GetVertex(Tv[k]) - Cn * Mo::CardHalfCm + (Rt * Cn.X + Upv * Cn.Z) * 2.0;
				}
				++Faced;
				BackToCamera += FVector::DotProduct(FVector::CrossProduct(Q[2] - Q[0], Q[1] - Q[0]), V) > 0.0 ? 0 : 1;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("mote cloud LODs %d, %d and %d triangles; motes stood for %d+%d, %d+%d, %d+%d (free+gnats); LOD 0 %.0f x %.0f x %.0f cm of the unit mesh; %d cards off their middle or the cloud, %d vertices off the colour contract, %d randoms repeated"),
		Tris[0], Tris[1], Tris[2], Free[0], Gnats[0], Free[1], Gnats[1], Free[2], Gnats[2], Box0.GetSize().X, Box0.GetSize().Y, Box0.GetSize().Z, BadCard, BadColour, Repeated));
	for (int32 L = 0; L < SlonkFaunaMeshes::NumLods; ++L)
	{
		TestEqual(FString::Printf(TEXT("LOD %d's triangles, two a card"), L), Tris[L], 2 * Mo::CardsAtLod[L]);
		TestEqual(FString::Printf(TEXT("LOD %d stands for the cloud's eighteen free motes"), L), Free[L], Mo::MotesPerCloud - Mo::GnatsPerCloud);
		TestEqual(FString::Printf(TEXT("LOD %d's gnats"), L), Gnats[L], L < 2 ? Mo::GnatsPerCloud : 0);
	}
	TestEqual(TEXT("every card a quad round its middle, in the cloud"), BadCard, 0);
	TestEqual(TEXT("every vertex as the material reads it"), BadColour, 0);
	TestEqual(TEXT("each card its own random"), Repeated, 0);
	const int32 NumViews = (int32)UE_ARRAY_COUNT(Views);
	AddInfo(FString::Printf(TEXT("%d triangles turned to %d cameras as the material turns them: %d showing the camera their backs"), Faced / NumViews, NumViews, BackToCamera));
	TestTrue(TEXT("every card turned to the camera shows it its front"), Faced > 0 && BackToCamera == 0);
	// That holds of the copy of the turn above only while the material turns its cards as it did when the copy was made:
	// an edit to the mote branch of fauna_material.py's WPO_CODE fails here, until the copy is brought in line and the CRC
	// with it.
	const FString MoteBlock = MaterialMotionBlock(EMotionBlock::Motes);
	const uint32 MoteBlockCrc = FCrc::StrCrc32(*MoteBlock);
	AddInfo(FString::Printf(TEXT("fauna_material.py's WPO_CODE, the mote branch: %d characters of code, CRC 0x%08x (0x%08x when the copy was made)"), MoteBlock.Len(), MoteBlockCrc, MaterialMoteBlockCrc));
	TestTrue(TEXT("fauna_material.py's mote branch found"), MoteBlock.Len() > 500 && MoteBlock.Contains(TEXT("float3 upv = cross(v, rt);")));
	TestEqual(TEXT("the turn copied here is the material's as it is now"), MoteBlockCrc, MaterialMoteBlockCrc);
	UStaticMesh* Mesh = SlonkFaunaMeshes::GetMoteCloudMesh();
	TestTrue(TEXT("the cloud's mesh is made, and kept"), Mesh != nullptr && Mesh == SlonkFaunaMeshes::GetMoteCloudMesh());
	if (Mesh)
	{
		TestEqual(TEXT("with three LODs"), Mesh->GetNumLODs(), SlonkFaunaMeshes::NumLods);
		const FBox B = Mesh->GetBounds().GetBox();
		TestTrue(TEXT("its bounds grown by the motes' reach"), B.Min.X <= Box0.Min.X - Mo::ReachCm + 0.5 && B.Max.Z >= Box0.Max.Z + Mo::ReachCm - 0.5);
		// Where its LODs come in for a cloud of a middling span, on the terrain's GPU instances and on the engine's own
		// (MoteLodFromM), as FaunaMeshes.h has them (MoteLodScreenSize): each within a twentieth of its figure.
		const double SpanM = 6.75;
		const double Gpu1 = MoteLodFromM(SpanM, 1, true), Gpu2 = MoteLodFromM(SpanM, 2, true), Engine1 = MoteLodFromM(SpanM, 1, false), Engine2 = MoteLodFromM(SpanM, 2, false);
		AddInfo(FString::Printf(TEXT("a %.2f m cloud: on the GPU instances 8 cards from %.0f m, 2 from %.0f m; on the engine's from %.0f m and %.0f m (on the GPU instances a 6 m cloud's %.0f m and %.0f m, a 9 m one's %.0f m and %.0f m)"),
			SpanM, Gpu1, Gpu2, Engine1, Engine2, MoteLodFromM(6.0, 1, true), MoteLodFromM(6.0, 2, true), MoteLodFromM(9.0, 1, true), MoteLodFromM(9.0, 2, true)));
		auto Near = [](double M, double Figure) { return FMath::Abs(M - Figure) < Figure * 0.05; };
		TestTrue(TEXT("on the GPU instances 8 cards from about 123 m"), Near(Gpu1, 123.0));
		TestTrue(TEXT("and 2 from about 615 m"), Near(Gpu2, 615.0));
		TestTrue(TEXT("on the engine's own 8 cards from about 85 m"), Near(Engine1, 85.0));
		TestTrue(TEXT("and 2 from about 425 m"), Near(Engine2, 425.0));
	}
	return true;
}

namespace
{
	/** That world's herds alone, full of life. */
	SlonkFauna::FFaunaWorld EarthHerds(float Life = 1.f)
	{
		SlonkFauna::FFaunaWorld W = EarthFauna(Life);
		W.KindMask = SlonkFauna::KindBit(EFaunaKind::Herd);
		return W;
	}

	/** The herds in the cells under N directions spread evenly over the globe. */
	void HerdsOverGlobe(const SlonkFauna::FFaunaWorld& W, int32 N, TArray<SlonkFauna::FFaunaGroup>& Out, int32* OutCells = nullptr)
	{
		Out.Reset();
		TSet<uint64> Seen;
		for (int32 i = 0; i < N; ++i)
		{
			const SlonkFauna::FFaunaCell C = SlonkFauna::CellOf(W.P, EFaunaKind::Herd, Spread(i, N));
			bool bAlready = false;
			Seen.Add(C.Id(), &bAlready);
			if (bAlready) { continue; }
			SlonkFauna::FFaunaGroup G;
			if (SlonkFauna::GroupInCell(W, C, G)) { Out.Add(MoveTemp(G)); }
		}
		if (OutCells) { *OutCells = Seen.Num(); }
	}

	/** A herd to test with: of the herds over the globe the first of at least 24 walkers (else the first). */
	bool TestHerd(const SlonkFauna::FFaunaWorld& W, SlonkFauna::FFaunaGroup& Out)
	{
		TArray<SlonkFauna::FFaunaGroup> Herds;
		HerdsOverGlobe(W, 12000, Herds);
		for (const SlonkFauna::FFaunaGroup& G : Herds) { if (G.Members >= 24) { Out = G; return true; } }
		if (Herds.Num() > 0) { Out = Herds[0]; return true; }
		return false;
	}

	/** The drawn ground at a depth of the quadtree: each chunk's height grid built as a chunk build makes it, its
	 *  triangles read as UPlanetTerrainComponent::DrawnHeightAtLocalDir reads a chunk on screen. */
	struct FDrawnGround
	{
		FPlanetTerrainParams P;
		int32 Depth = 16;
		TMap<uint64, TArray<float>> Chunks;
		double At(const FVector& Dir)
		{
			int32 F = 0;
			int64 X = 0, Y = 0;
			UPlanetTerrainComponent::ChunkAt(Dir, Depth, F, X, Y);
			const uint64 Key = ((uint64)F << 58) ^ ((uint64)X << 29) ^ (uint64)Y;
			TArray<float>* H = Chunks.Find(Key);
			if (!H)
			{
				TArray<float> Grid;
				UPlanetTerrainComponent::BuildChunkHeightsForTest(P, F, Depth, X, Y, 64, Grid);
				H = &Chunks.Add(Key, MoveTemp(Grid));
			}
			return UPlanetTerrainComponent::ChunkHeightAtForTest(*H, 64, Depth, X, Y, Dir);
		}
	};

	/** A walker's four hooves in its unit mesh (SlonkFaunaMeshes' walker), and their places in the gait. */
	const FVector3f WalkerHooves[4] = { FVector3f(-33.f, -8.f, 0.f), FVector3f(10.f, -8.f, 0.f), FVector3f(-33.f, 8.f, 0.f), FVector3f(10.f, 8.f, 0.f) };
	const float WalkerHoofPhase[4] = { 0.f, 0.25f, 0.5f, 0.75f };

	/** Where the material draws a walker's vertex (at LP in its unit mesh, with its vertex colour) for a herd as stepped,
	 *  planet-local: its instance unturned in its herd's frame, scaled by its length. */
	FVector WalkerVertexAt(const SlonkFauna::FHerdState& S, const FTransform& Xf, const float* C, const float* Gd, const FVector3f& LP, const FLinearColor& VC, float T, float DistCm = 3000.f)
	{
		const float Size = C[SlonkFauna::Custom::SizeM];
		const FVector3f LPP = (FVector3f)Xf.GetTranslation() + LP * Size;
		const FVector3f Off = SlonkFauna::WalkerMotion(LP, LPP, VC, T, C, Gd, DistCm, 960.f);
		return S.FramePos + S.FrameRot.RotateVector(FVector(LPP + Off * Size));
	}

	/** What following a herd's hooves found. */
	struct FHerdRun
	{
		int32 Steps = 0, Feet = 0, Planted = 0, Moving = 0, Grazing = 0, Turning = 0;
		double WorstPlantedCm = 0.0, SumPlantedCm = 0.0, WorstFootCm = 0.0, MaxSpeedMS = 0.0;
		/** Planted hooves' slides along the ground between two steps, cm/s: on the move with the herd, grazing (with how
		 *  fast the walkers walked then, cm/s), turning to walk or back to graze, and in all; and how many slid faster
		 *  than 10 cm/s. */
		double WorstTravelSlip = 0.0, SumTravelSlip = 0.0, SumTravelSpeed = 0.0;
		int32 TravelSlips = 0;
		double WorstGrazeSlip = 0.0, SumGrazeSlip = 0.0, SumGrazeSpeed = 0.0;
		int32 GrazeSlips = 0;
		double WorstTurnSlip = 0.0, SumTurnSlip = 0.0;
		int32 TurnSlips = 0;
		double WorstSlip = 0.0, SumSlip = 0.0;
		int32 Slips = 0, Slides = 0;
		/** The most any walker's yaw turned between two steps, radians, and the least and most of the herd on the move. */
		double WorstYawStep = 0.0, MinTravel = 1.0, MaxTravel = 0.0;
		double MeanPlantedCm() const { return Planted > 0 ? SumPlantedCm / Planted : 0.0; }
		double MeanTravelSlip() const { return TravelSlips > 0 ? SumTravelSlip / TravelSlips : 0.0; }
		double MeanGrazeSlip() const { return GrazeSlips > 0 ? SumGrazeSlip / GrazeSlips : 0.0; }
		double MeanTurnSlip() const { return TurnSlips > 0 ? SumTurnSlip / TurnSlips : 0.0; }
		double MeanSlip() const { return Slips > 0 ? SumSlip / Slips : 0.0; }
	};

	/** What moves a herd off its schedule at a step: its offset off its path (its fright's and the crew's) and a second
	 *  on, whether it is shaken and whether it is moving for the crew (as UPlanetFaunaComponent::UpdateHerd gives them). */
	struct FHerdDrive
	{
		FVector Offset = FVector::ZeroVector;
		FVector Ahead = FVector::ZeroVector;
		bool bShaken = false, bMoving = false;
	};

	/**
	 * A herd stepped as the game steps it (SlonkFauna::StepHerd) from Start for Seconds at Hz on HeightAt's ground, read
	 * again every RefreshEvery steps, what Drive gives it each step (called before the step, with the herd as the last
	 * step left it and the step's time) moving it off its schedule; every MemberStride-th member's four hooves followed as
	 * the material moves them: how far a planted hoof (down for the stride's Duty) stands off the ground under it, and how
	 * fast one planted at two steps slid along the ground between them; and how far every walker's yaw turned a step.
	 * bFrozen leaves the herd's walked distance as it first was, as if the game never ran it on: its feet would then
	 * slide as fast as it walks.
	 */
	FHerdRun RunHerdWith(const SlonkFauna::FFaunaGroup& G, double R, double Start, double Seconds, double Hz, int32 RefreshEvery, TFunctionRef<double(const FVector&)> HeightAt,
		TFunctionRef<FHerdDrive(SlonkFauna::FHerdState&, double, double)> Drive, int32 MemberStride = 3, bool bFrozen = false)
	{
		FHerdRun Out;
		TArray<FTransform> Xf;
		TArray<float> Cu;
		SlonkFauna::MembersOf(G, Xf, Cu);
		SlonkFauna::FHerdState S;
		SlonkFauna::StartHerd(S, G, Start);
		TArray<FVector> Last;
		TArray<uint8> WasPlanted;
		TArray<float> LastYaw;
		Last.SetNum(Xf.Num() * 4);
		WasPlanted.Init(0, Xf.Num() * 4);
		LastYaw.Init(0.f, Xf.Num());
		float Frozen = -1.f;
		const int32 N = FMath::CeilToInt32(Seconds * Hz);
		for (int32 k = 0; k <= N; ++k)
		{
			const double Now = Start + k / Hz;
			const double Dt = k == 0 ? 0.0 : 1.0 / Hz;
			const FHerdDrive Dr = Drive(S, Now, Dt);
			float Gd[SlonkFauna::HerdData::Num] = {};
			SlonkFauna::StepHerd(S, G, R, Now, Dt, Dr.Offset, Dr.bShaken, Dr.bMoving, k % FMath::Max(RefreshEvery, 1) == 0, HeightAt, 0.0, Gd, &Dr.Ahead);
			if (bFrozen)
			{
				if (Frozen < 0.f) { Frozen = Gd[SlonkFauna::HerdData::DistM]; }
				Gd[SlonkFauna::HerdData::DistM] = Frozen;
			}
			const float T = (float)FMath::Fmod(Now, SlonkFauna::ClockPeriodSec);
			++Out.Steps;
			const bool bMoving = S.Travel > 0.99, bGrazing = S.Travel < 0.01;
			Out.Moving += bMoving ? 1 : 0;
			Out.Grazing += bGrazing ? 1 : 0;
			Out.Turning += !bMoving && !bGrazing ? 1 : 0;
			Out.MaxSpeedMS = FMath::Max(Out.MaxSpeedMS, S.SpeedMS);
			Out.MinTravel = FMath::Min(Out.MinTravel, S.Travel);
			Out.MaxTravel = FMath::Max(Out.MaxTravel, S.Travel);
			for (int32 m = 0; m < Xf.Num(); ++m)
			{
				const float* C = Cu.GetData() + m * SlonkFauna::Custom::Num;
				const float Yaw = SlonkFauna::WalkerYaw(T, C, Gd);
				if (k > 0) { Out.WorstYawStep = FMath::Max(Out.WorstYawStep, (double)FMath::Abs(FMath::UnwindRadians(Yaw - LastYaw[m]))); }
				LastYaw[m] = Yaw;
				if (m % FMath::Max(MemberStride, 1) != 0) { continue; }
				const float Strides = SlonkFauna::WalkerStrides(T, C, Gd);
				const float Speed = SlonkFauna::WalkerSpeedMS(T, C, Gd);
				for (int32 f = 0; f < 4; ++f)
				{
					const FLinearColor VC(SlonkFauna::Vc::SurfaceBeak, 1.f, WalkerHoofPhase[f], SlonkFauna::Vc::JointLeg);
					const FVector P = WalkerVertexAt(S, Xf[m], C, Gd, WalkerHooves[f], VC, T);
					const FVector Up = P.GetSafeNormal();
					const double Off = FMath::Abs(P.Size() - R - HeightAt(Up));
					const bool bPlanted = FMath::Frac(Strides - WalkerHoofPhase[f]) < SlonkFauna::Walker::Duty;
					++Out.Feet;
					Out.WorstFootCm = FMath::Max(Out.WorstFootCm, Off);
					if (bPlanted)
					{
						++Out.Planted;
						Out.WorstPlantedCm = FMath::Max(Out.WorstPlantedCm, Off);
						Out.SumPlantedCm += Off;
					}
					const int32 I = m * 4 + f;
					if (k > 0 && bPlanted && WasPlanted[I])
					{
						FVector D = P - Last[I];
						D -= Up * FVector::DotProduct(D, Up);
						const double Slip = D.Size() * Hz;
						++Out.Slips;
						Out.SumSlip += Slip;
						Out.WorstSlip = FMath::Max(Out.WorstSlip, Slip);
						Out.Slides += Slip > 10.0 ? 1 : 0;
						if (bMoving)
						{
							++Out.TravelSlips;
							Out.SumTravelSlip += Slip;
							Out.SumTravelSpeed += S.SpeedMS * 100.0;
							Out.WorstTravelSlip = FMath::Max(Out.WorstTravelSlip, Slip);
						}
						else if (bGrazing && Speed > 0.05f)
						{
							++Out.GrazeSlips;
							Out.SumGrazeSlip += Slip;
							Out.SumGrazeSpeed += Speed * 100.0;
							Out.WorstGrazeSlip = FMath::Max(Out.WorstGrazeSlip, Slip);
						}
						else if (!bGrazing)
						{
							++Out.TurnSlips;
							Out.SumTurnSlip += Slip;
							Out.WorstTurnSlip = FMath::Max(Out.WorstTurnSlip, Slip);
						}
					}
					Last[I] = P;
					WasPlanted[I] = bPlanted ? 1 : 0;
				}
			}
		}
		return Out;
	}

	/** A herd stepped on its schedule alone (RunHerdWith, nothing moving it off it). */
	FHerdRun RunHerd(const SlonkFauna::FFaunaGroup& G, double R, double Start, double Seconds, double Hz, int32 RefreshEvery, TFunctionRef<double(const FVector&)> HeightAt,
		int32 MemberStride = 3, bool bFrozen = false)
	{
		return RunHerdWith(G, R, Start, Seconds, Hz, RefreshEvery, HeightAt, [](SlonkFauna::FHerdState&, double, double) { return FHerdDrive(); }, MemberStride, bFrozen);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaHerdHabitatTest, "Slonk.Fauna.HerdHabitat", SLONK_TEST_FLAGS)
bool FSlonkFaunaHerdHabitatTest::RunTest(const FString& Parameters)
{
	// On an Earth-like world the herds graze open, level grass: every point along each herd's loop (read here again at
	// 240 places through its hour, between the 97 its cell read) is ground a herd walks over (a little rougher than its
	// cell's read allows at its own points: the ground between them), none of it water, ice or wood, no water within
	// its flight of it; 15 to 60 walkers of 1.45 to 2.6 m; none on a world with no life or no air, at density 0, or with
	// the herds off; and on a world with towns, none within reach of one.
	const SlonkFauna::FFaunaWorld W = EarthHerds();
	TArray<SlonkFauna::FFaunaGroup> Herds;
	int32 Cells = 0;
	HerdsOverGlobe(W, 30000, Herds, &Cells);
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Herd);
	int32 Wrong = 0, OffGrass = 0, Wooded = 0, Watery = 0, Rough = 0, Members = 0, BadMembers = 0, WaterNear = 0;
	double MinFlat = 1.0, MaxForest = 0.0, MinWet = 1.0, MaxWet = 0.0, MinLoop = 1e12, MaxLoop = 0.0;
	for (const SlonkFauna::FFaunaGroup& G : Herds)
	{
		Members += G.Members;
		MinLoop = FMath::Min(MinLoop, G.LengthCm);
		MaxLoop = FMath::Max(MaxLoop, G.LengthCm);
		int32 Strict = 0;
		bool bOk = true;
		for (int32 k = 0; k < 240; ++k)
		{
			const FVector Dir = SlonkFauna::PathAt(G, W.P.RadiusCm, 3600.0 * k / 240.0).PosCm.GetSafeNormal();
			const SlonkScatter::FHabitat H = SlonkScatter::HabitatAt(W.P, Dir);
			Watery += H.bWater ? 1 : 0;
			Wooded += H.Forest >= 0.45 ? 1 : 0;
			const bool bGrass = !H.bWater && H.Ice <= 0.2 && H.Alpine <= 0.7 && H.Flat > 0.62 && H.Wet >= 0.12 && H.Wet <= 0.72 && H.Forest < 0.45;
			OffGrass += bGrass ? 0 : 1;
			Strict += SlonkFauna::HerdGrass(H, true) ? 0 : 1;
			bOk &= bGrass;
			MinFlat = FMath::Min(MinFlat, H.Flat);
			MaxForest = FMath::Max(MaxForest, H.Forest);
			MinWet = FMath::Min(MinWet, H.Wet);
			MaxWet = FMath::Max(MaxWet, H.Wet);
		}
		// Not quite as it likes at most a tenth of the 240 places through its hour (its cell's read allows six of 97).
		Rough += Strict > 24 ? 1 : 0;
		const SlonkFauna::FHerdHabitatRead Read = SlonkFauna::HerdHabitatAt(W, G);
		bOk &= Read.bSuits;
		// Water anywhere it could be taken (its loop's farthest, a fright's flight half again and its loop back, and the
		// crew's 300 m), read here on rings 100 m apart, a point every 100 m round each.
		const double Reach = G.ExtentCm() + 1.5 * D.FleeCm + D.FleeLoopCm + 3.0e4;
		for (double Rr = 1.0e4; Rr <= Reach + 1.0; Rr += 1.0e4)
		{
			const int32 Round = FMath::Max(FMath::CeilToInt32(UE_DOUBLE_TWO_PI * Rr / 1.0e4), 6);
			for (int32 k = 0; k < Round; ++k)
			{
				const double A = UE_DOUBLE_TWO_PI * (k + 0.37) / Round;
				const FVector Dir = (G.Centre + (G.East * FMath::Cos(A) + G.North * FMath::Sin(A)) * (Rr / W.P.RadiusCm)).GetSafeNormal();
				WaterNear += SlonkScatter::HabitatAt(W.P, Dir).bWater ? 1 : 0;
			}
		}
		Wrong += bOk ? 0 : 1;
		BadMembers += G.Members >= D.GroupMin && G.Members <= D.GroupMax && G.SpanM >= 1.6f && G.SpanM <= 2.3f ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("%d cells of an Earth-like world: %d herds (%d walkers), loops %.1f to %.1f km; along their loops flat from %.2f, wet %.2f to %.2f, wood at most %.2f; %d herds off their grass, %d points off it (%d in water, %d in a wood), %d rougher than it likes, %d points of water within their flight"),
		Cells, Herds.Num(), Members, MinLoop / 1e5, MaxLoop / 1e5, MinFlat, MinWet, MaxWet, MaxForest, Wrong, OffGrass, Watery, Wooded, Rough, WaterNear));
	TestTrue(TEXT("herds on an Earth-like world"), Herds.Num() >= 10);
	TestEqual(TEXT("every herd's loop on its grass"), Wrong, 0);
	TestEqual(TEXT("no point of any loop in the water or a wood"), Watery + Wooded, 0);
	TestEqual(TEXT("no herd rougher than it likes"), Rough, 0);
	TestEqual(TEXT("no water within a herd's flight"), WaterNear, 0);
	TestEqual(TEXT("every herd 15 to 60 grazers' length"), BadMembers, 0);
	TestTrue(TEXT("loops of about 2 km"), MinLoop > 1.0e5 && MaxLoop < 3.2e5);

	// None without life, without air, at density 0, or with the herds off.
	TArray<SlonkFauna::FFaunaGroup> None;
	int32 Leaked = 0;
	HerdsOverGlobe(EarthHerds(0.f), 1500, None);
	Leaked += None.Num();
	SlonkFauna::FFaunaWorld Airless = W;
	Airless.P.bAirless = true;
	HerdsOverGlobe(Airless, 1500, None);
	Leaked += None.Num();
	SlonkFauna::FFaunaWorld Sparse = W;
	Sparse.Density = 0.f;
	HerdsOverGlobe(Sparse, 1500, None);
	Leaked += None.Num();
	SlonkFauna::FFaunaWorld Others = W;
	Others.KindMask = SlonkFauna::AllKinds & ~SlonkFauna::KindBit(EFaunaKind::Herd);
	HerdsOverGlobe(Others, 1500, None);
	Leaked += None.Num();
	TestEqual(TEXT("no herds without life or air, at density 0, or with the herds off"), Leaked, 0);

	// With towns (the Federation's colonists'), no herd's loop within reach of one; the herds that stand are the
	// town-free world's.
	SlonkFauna::FFaunaWorld Towns = W;
	Towns.bTowns = true;
	Towns.People = SlonkCivs::Colonists();
	TArray<SlonkFauna::FFaunaGroup> Settled;
	HerdsOverGlobe(Towns, 30000, Settled);
	int32 InTown = 0, NotBefore = 0;
	TSet<uint64> Before;
	for (const SlonkFauna::FFaunaGroup& G : Herds) { Before.Add(G.Id); }
	for (const SlonkFauna::FFaunaGroup& G : Settled)
	{
		NotBefore += Before.Contains(G.Id) ? 0 : 1;
		TArray<FTownSite> Sites;
		SlonkCities::EnumerateTowns(Towns.P, Towns.People, G.Centre * Towns.P.RadiusCm, G.ExtentCm() + 3.0e5, Sites, 32);
		for (const FTownSite& T : Sites) { InTown += FVector::Dist(T.LocalCm, G.Centre * Towns.P.RadiusCm) < G.ExtentCm() + G.HerdRadiusCm + T.RadiusCm ? 1 : 0; }
	}
	AddInfo(FString::Printf(TEXT("with the colonists' towns: %d herds of the %d (%d not among them), %d within reach of a town"), Settled.Num(), Herds.Num(), NotBefore, InTown));
	TestEqual(TEXT("no herd within reach of a town"), InTown, 0);
	TestEqual(TEXT("the herds a world with towns keeps are the town-free world's"), NotBefore, 0);
	TestTrue(TEXT("the towns keep some herds off their ground"), Settled.Num() < Herds.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaHerdLoopsCloseTest, "Slonk.Fauna.HerdLoopsClose", SLONK_TEST_FLAGS)
bool FSlonkFaunaHerdLoopsCloseTest::RunTest(const FString& Parameters)
{
	// A herd's schedule walks its loop exactly once an hour, in closed form: the distance walked at the hour's end is the
	// loop's length, it agrees with the integral of its speed, and a herd is where it was an hour ago with no seam at the
	// clock's wrap. Its cycle divides the hour; it grazes most of it, walks at a walk and trots now and then. Every
	// walker's stride goes a whole number of times round its herd's loop and round its own grazing circle, so the
	// material's clocks wrap without a seam; and the material's walking runs on across the hour's wrap without a jump.
	const SlonkFauna::FFaunaWorld W = EarthHerds();
	TArray<SlonkFauna::FFaunaGroup> Herds;
	HerdsOverGlobe(W, 20000, Herds);
	const double R = W.P.RadiusCm;
	int32 Unclosed = 0, Seams = 0, NotDividing = 0, Integral = 0, Shares = 0, Speeds = 0, Strides = 0, Circles = 0;
	double WorstLoop = 0.0, WorstIntegral = 0.0, MinGraze = 1.0, MaxTrotShare = 0.0;
	for (const SlonkFauna::FFaunaGroup& G : Herds)
	{
		const double Cycles = SlonkFauna::ClockPeriodSec / G.CycleSec;
		NotDividing += FMath::Abs(Cycles - FMath::RoundToDouble(Cycles)) < 1e-9 ? 0 : 1;
		Unclosed += FMath::Abs(SlonkFauna::HerdWalkedCm(G, 3599.9999) - G.LengthCm) < 1.0 && SlonkFauna::HerdWalkedCm(G, 0.0) == 0.0 ? 0 : 1;
		for (const double T : { 0.0, 123.4, 1777.7, 3599.0 })
		{
			const FVector P0 = SlonkFauna::PathAt(G, R, T).PosCm;
			const double Loop = FMath::Max(FVector::Dist(P0, SlonkFauna::PathAt(G, R, T + 3600.0).PosCm), FVector::Dist(P0, SlonkFauna::PathAt(G, R, T + 7.0 * 3600.0).PosCm));
			WorstLoop = FMath::Max(WorstLoop, Loop);
			Unclosed += Loop < 1.0 ? 0 : 1;
		}
		Seams += FVector::Dist(SlonkFauna::PathAt(G, R, 3599.999).PosCm, SlonkFauna::PathAt(G, R, 3600.0).PosCm) < G.TrotCmS * 0.002 + 1.0 ? 0 : 1;
		// The closed form against its speed's integral, by the trapezium over the hour at 20 Hz.
		double Sum = 0.0, Prev = SlonkFauna::HerdSpeedCmS(G, 0.0);
		for (int32 k = 1; k <= 72000; ++k)
		{
			const double V = SlonkFauna::HerdSpeedCmS(G, k * 0.05);
			Sum += 0.5 * (V + Prev) * 0.05;
			Prev = V;
		}
		const double Err = FMath::Abs(Sum - G.LengthCm) / G.LengthCm;
		WorstIntegral = FMath::Max(WorstIntegral, Err);
		Integral += Err < 1e-3 ? 0 : 1;
		const double Graze = 1.0 - (G.WalkB - G.WalkA) / G.CycleSec;
		MinGraze = FMath::Min(MinGraze, Graze);
		MaxTrotShare = FMath::Max(MaxTrotShare, (G.TrotB - G.TrotA) / G.CycleSec);
		Shares += Graze >= 0.6 && G.TrotB - G.TrotA > 2.0 * G.RampSec && G.TrotA >= G.WalkA + G.RampSec && G.TrotB <= G.WalkB - G.RampSec ? 0 : 1;
		Speeds += SlonkFauna::HerdSpeedCmS(G, 0.5 * (G.TrotA + G.TrotB)) >= 240.0 && SlonkFauna::HerdSpeedCmS(G, 0.5 * (G.TrotA + G.TrotB)) <= 340.0
			&& SlonkFauna::HerdSpeedCmS(G, G.WalkA + G.RampSec + 0.5) >= 90.0 && SlonkFauna::HerdSpeedCmS(G, G.WalkA * 0.5) == 0.0 ? 0 : 1;
		TArray<FTransform> Xf;
		TArray<float> Cu;
		SlonkFauna::MembersOf(G, Xf, Cu);
		for (int32 m = 0; m < Xf.Num(); ++m)
		{
			const float* C = Cu.GetData() + m * SlonkFauna::Custom::Num;
			const double Ls = C[SlonkFauna::Custom::RateHz], Rho = FMath::Abs(C[SlonkFauna::Custom::PerKind]);
			const double Laps = G.LengthCm / 100.0 / Ls, Round = UE_DOUBLE_TWO_PI * Rho / Ls;
			Strides += FMath::Abs(Laps - FMath::RoundToDouble(Laps)) < 2e-3 ? 0 : 1;
			Circles += FMath::Abs(Round - FMath::RoundToDouble(Round)) < 1e-3 && Rho <= SlonkFauna::Walker::MaxCircleM + 1e-3 ? 0 : 1;
		}
	}
	// A herd stepped across the hour's wrap: its hooves move no more as the clock wraps than they do a frame either side.
	int32 WrapJumps = 0;
	double WrapWorst = 0.0, StepWorst = 0.0;
	if (Herds.Num() > 0)
	{
		const SlonkFauna::FFaunaGroup& G = Herds[0];
		auto Height = [&W](const FVector& Dir) { return FMath::Max(SlonkTerrain::Sample(W.P, Dir, 2000.0).HeightCm, 0.0); };
		TArray<FTransform> Xf;
		TArray<float> Cu;
		SlonkFauna::MembersOf(G, Xf, Cu);
		// The wrap of the fifth hour, the herd on the move then if its schedule has it so: started a while before.
		const double Wrap = 5.0 * 3600.0;
		SlonkFauna::FHerdState S;
		SlonkFauna::StartHerd(S, G, Wrap - 3.0);
		FVector Last[4];
		for (int32 k = 0; k <= 180; ++k)
		{
			const double Now = Wrap - 3.0 + k / 30.0;
			float Gd[SlonkFauna::HerdData::Num] = {};
			SlonkFauna::StepHerd(S, G, R, Now, k == 0 ? 0.0 : 1.0 / 30.0, FVector::ZeroVector, false, false, true, Height, 0.0, Gd);
			const float T = (float)FMath::Fmod(Now, SlonkFauna::ClockPeriodSec);
			for (int32 f = 0; f < 4; ++f)
			{
				const FVector P = WalkerVertexAt(S, Xf[0], Cu.GetData(), Gd, WalkerHooves[f], FLinearColor(SlonkFauna::Vc::SurfaceBeak, 1.f, WalkerHoofPhase[f], SlonkFauna::Vc::JointLeg), T);
				if (k > 0)
				{
					const double Step = FVector::Dist(P, Last[f]);
					const bool bAtWrap = Now >= Wrap && Now - 1.0 / 30.0 < Wrap;
					if (bAtWrap) { WrapWorst = FMath::Max(WrapWorst, Step); } else { StepWorst = FMath::Max(StepWorst, Step); }
				}
				Last[f] = P;
			}
		}
		WrapJumps += WrapWorst <= StepWorst * 1.5 + 0.5 ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("%d herds: %d cycles not dividing the hour, %d schedules not walking their loop in the hour or paths not closed (worst %.3f cm), %d seams at the wrap; the closed form off its speed's integral by %.5f at worst (%d over 0.1%%); grazing at least %.0f%% of each cycle, trotting at most %.0f%% (%d off), %d off a walker's speeds; %d strides not whole round their loops, %d not round their circles"),
		Herds.Num(), NotDividing, Unclosed, WorstLoop, Seams, WorstIntegral, Integral, MinGraze * 100.0, MaxTrotShare * 100.0, Shares, Speeds, Strides, Circles));
	AddInfo(FString::Printf(TEXT("across the hour's wrap a walker's hooves move %.2f cm in the frame it wraps, %.2f cm at most in any other"), WrapWorst, StepWorst));
	TestTrue(TEXT("herds to test"), Herds.Num() >= 10);
	TestEqual(TEXT("every cycle divides the hour"), NotDividing, 0);
	TestEqual(TEXT("every schedule walks its loop once an hour, every path closes"), Unclosed, 0);
	TestEqual(TEXT("no seam where the clock wraps"), Seams, 0);
	TestEqual(TEXT("the closed form is its speed's integral"), Integral, 0);
	TestEqual(TEXT("grazing most of the time, trotting now and then"), Shares, 0);
	TestEqual(TEXT("at a grazer's walk and trot, still while grazing"), Speeds, 0);
	TestEqual(TEXT("every stride whole round its herd's loop"), Strides, 0);
	TestEqual(TEXT("and round its grazing circle"), Circles, 0);
	TestEqual(TEXT("the material's walking runs on across the hour's wrap"), WrapJumps, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaHerdOnGroundTest, "Slonk.Fauna.HerdOnGround", SLONK_TEST_FLAGS)
bool FSlonkFaunaHerdOnGroundTest::RunTest(const FString& Parameters)
{
	// Every walker stands on the ground that is drawn: a herd stepped as the game steps it for a minute walking into its
	// trot and a minute grazing, on the drawn ground of chunks 150 m across (2.4 m cells, as round a camera a few hundred
	// metres off), its ground read again every frame (a herd near the camera) and every sixth (one further off): every
	// planted hoof within half a metre of the ground drawn under it. And on chunks 610 m across (9.5 m cells, a herd a
	// kilometre or two off), the same.
	const SlonkFauna::FFaunaWorld W = EarthHerds();
	SlonkFauna::FFaunaGroup G;
	if (!TestHerd(W, G)) { AddError(TEXT("no herd found on the Earth-like world")); return false; }
	const double R = W.P.RadiusCm;
	FDrawnGround Fine;
	Fine.P = W.P;
	Fine.Depth = 16;
	FDrawnGround Coarse;
	Coarse.P = W.P;
	Coarse.Depth = 14;
	auto FineAt = [&Fine](const FVector& Dir) { return Fine.At(Dir); };
	auto CoarseAt = [&Coarse](const FVector& Dir) { return Coarse.At(Dir); };
	// Its schedule's fourth cycle: walking into its trot, then grazing after its walk.
	const double C0 = 3.0 * G.CycleSec;
	const double WalkStart = C0 + G.TrotA - 25.0, GrazeStart = C0 + G.WalkB + 5.0;
	const FHerdRun WalkNear = RunHerd(G, R, WalkStart, 60.0, 30.0, 1, FineAt);
	const FHerdRun WalkFar = RunHerd(G, R, WalkStart, 60.0, 30.0, 6, FineAt);
	const FHerdRun GrazeNear = RunHerd(G, R, GrazeStart, 60.0, 30.0, 1, FineAt);
	const FHerdRun GrazeFar = RunHerd(G, R, GrazeStart, 60.0, 30.0, 6, FineAt);
	const FHerdRun WalkCoarse = RunHerd(G, R, WalkStart, 60.0, 10.0, 6, CoarseAt);
	AddInfo(FString::Printf(TEXT("a herd of %d of %.1f m within %.0f m (its ground grid %.0f m either way): walking into its trot (to %.1f m/s) planted hooves off the drawn ground by %.1f cm at most (%.1f on average) read each frame, %.1f (%.1f) every sixth; grazing %.1f (%.1f) and %.1f (%.1f); on chunks 610 m across %.1f (%.1f); %d and %d chunks built; any hoof, lifted or not, at most %.1f cm"),
		G.Members, G.SpanM, G.HerdRadiusCm / 100.0, G.GroundHalfCm / 100.0, WalkNear.MaxSpeedMS, WalkNear.WorstPlantedCm, WalkNear.MeanPlantedCm(), WalkFar.WorstPlantedCm, WalkFar.MeanPlantedCm(),
		GrazeNear.WorstPlantedCm, GrazeNear.MeanPlantedCm(), GrazeFar.WorstPlantedCm, GrazeFar.MeanPlantedCm(), WalkCoarse.WorstPlantedCm, WalkCoarse.MeanPlantedCm(),
		Fine.Chunks.Num(), Coarse.Chunks.Num(), FMath::Max(WalkNear.WorstFootCm, GrazeNear.WorstFootCm)));
	TestTrue(TEXT("the herd walked and trotted"), WalkNear.Moving > 300 && WalkNear.MaxSpeedMS > 2.0);
	TestTrue(TEXT("and grazed"), GrazeNear.Grazing > 300 && GrazeNear.Planted > 0);
	TestTrue(TEXT("every planted hoof within half a metre of the drawn ground, read each frame"), WalkNear.WorstPlantedCm <= 50.0 && GrazeNear.WorstPlantedCm <= 50.0);
	TestTrue(TEXT("and read every sixth frame"), WalkFar.WorstPlantedCm <= 50.0 && GrazeFar.WorstPlantedCm <= 50.0);
	TestTrue(TEXT("and on a coarser drawn ground"), WalkCoarse.WorstPlantedCm <= 50.0);
	TestTrue(TEXT("on it, on the whole, within a few centimetres"), WalkNear.MeanPlantedCm() < 10.0 && GrazeNear.MeanPlantedCm() < 10.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaHerdNoSlideTest, "Slonk.Fauna.HerdNoSlide", SLONK_TEST_FLAGS)
bool FSlonkFaunaHerdNoSlideTest::RunTest(const FString& Parameters)
{
	// A planted hoof stays where it was put down: on the move with its herd, walking and trotting, its stride's phase the
	// exact integral of how far the herd walked, a planted hoof slides along the ground less than a centimetre a second on
	// average; grazing round its circle, the body turning over its planted feet, as little. Were the herd's walked
	// distance not run on (the material's walking left at what it first was), the hooves would slide as fast as it walks.
	// And through every turn its walkers make where they stand: setting off (from their grazing to the herd's way) and
	// stopping (back to their grazing), someone walking at it from ahead as it grazes and away again (it turns and walks
	// off, then turns about and walks back), a craft passing low ahead of it (it turns, gallops off the way it came, loops
	// round and walks back): a planted hoof slides less than a centimetre a second on average, hardly one in ten thousand
	// more than 10 cm/s and none more than 25 cm/s; walking and trotting up and across a slope, as little as on the level;
	// and no walker's yaw turns between two frames more than it can turn in one.
	const SlonkFauna::FFaunaWorld W = EarthHerds();
	SlonkFauna::FFaunaGroup G;
	if (!TestHerd(W, G)) { AddError(TEXT("no herd found on the Earth-like world")); return false; }
	const double R = W.P.RadiusCm;
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Herd);
	FDrawnGround Fine;
	Fine.P = W.P;
	Fine.Depth = 16;
	auto FineAt = [&Fine](const FVector& Dir) { return Fine.At(Dir); };
	const double C0 = 3.0 * G.CycleSec;
	const double WalkStart = C0 + G.TrotA - 25.0, GrazeStart = C0 + G.WalkB + 5.0;
	const FHerdRun Walk = RunHerd(G, R, WalkStart, 60.0, 30.0, 1, FineAt, 1);
	const FHerdRun Graze = RunHerd(G, R, GrazeStart, 90.0, 30.0, 1, FineAt, 1);
	const FHerdRun Frozen = RunHerd(G, R, WalkStart + 20.0, 15.0, 30.0, 1, FineAt, 2, true);
	AddInfo(FString::Printf(TEXT("on the move (%d hoof-steps planted, the herd at %.2f m/s on average): planted hooves slide %.2f cm/s on average, %.2f at worst; grazing (%d, the walkers at %.1f cm/s): %.2f cm/s on average, %.2f at worst; with the walked distance left as it was, %.1f cm/s on average"),
		Walk.TravelSlips, Walk.TravelSlips > 0 ? Walk.SumTravelSpeed / Walk.TravelSlips / 100.0 : 0.0, Walk.MeanTravelSlip(), Walk.WorstTravelSlip, Graze.GrazeSlips,
		Graze.GrazeSlips > 0 ? Graze.SumGrazeSpeed / Graze.GrazeSlips : 0.0, Graze.MeanGrazeSlip(), Graze.WorstGrazeSlip, Frozen.MeanTravelSlip()));
	TestTrue(TEXT("hooves followed on the move and grazing"), Walk.TravelSlips > 1000 && Graze.GrazeSlips > 100);
	TestTrue(TEXT("on the move, a planted hoof slides less than a centimetre a second"), Walk.MeanTravelSlip() < 1.0 && Walk.WorstTravelSlip < 6.0);
	TestTrue(TEXT("grazing, as little"), Graze.MeanGrazeSlip() < 1.0 && Graze.WorstGrazeSlip < 6.0);
	TestTrue(TEXT("with the herd's walk not run on, they would slide as fast as it walks"), Frozen.MeanTravelSlip() > 50.0);

	// Setting off from grazing (its walkers turning to the herd's way where they stand, then walking on) and stopping
	// (turning back to their grazing once it has stopped), every member followed.
	const FHerdRun SetOff = RunHerd(G, R, C0 + G.WalkA - SlonkFauna::HerdLeadSec - 4.0, 16.0, 30.0, 1, FineAt, 1);
	const FHerdRun Stop = RunHerd(G, R, C0 + G.WalkB - 6.0, 16.0, 30.0, 1, FineAt, 1);
	// Someone walking at it as it grazes from ahead (the way it last walked, which it still faces), from 100 m off its
	// edge at 1.4 m/s for 45 s, then away again: it turns and walks off, keeps away, then stops, turns about and walks
	// back to its path.
	FVector Crew = FVector::ZeroVector, Toward = FVector::ZeroVector;
	auto Facing = [](const SlonkFauna::FHerdState& S) { return S.FrameRot.GetAxisX() * FMath::Cos(S.Heading) + S.FrameRot.GetAxisY() * FMath::Sin(S.Heading); };
	int32 CrewStartles = 0;
	SlonkFauna::FPanic CrewPanic;
	const double CrewStart = GrazeStart + 10.0;
	const FHerdRun CrewRun = RunHerdWith(G, R, CrewStart, 150.0, 30.0, 1, FineAt, [&](SlonkFauna::FHerdState& S, double Now, double Dt)
	{
		FHerdDrive Dr;
		if (!S.bStarted) { return Dr; }
		if (Toward.IsZero()) { Crew = S.FramePos + Facing(S) * (G.HerdRadiusCm + 1.0e4); Toward = -Facing(S); }
		Crew += Toward * ((Now - CrewStart < 45.0 ? 140.0 : -140.0) * Dt);
		SlonkFauna::FFaunaThreat Foot;
		Foot.bCraft = false;
		Foot.PosCm = Crew;
		CrewStartles += SlonkFauna::StartleGroup(CrewPanic, D, S.FramePos, Foot, Now, G.HerdRadiusCm) ? 1 : 0;
		Dr.bMoving = SlonkFauna::KeepFromCrew(S, G, D, TArray<FVector>{ Crew }, Dt);
		Dr.bShaken = CrewPanic.IsLive(Now);
		Dr.Offset = SlonkFauna::PanicOffset(CrewPanic, D, Now) + S.Avoid;
		Dr.Ahead = SlonkFauna::PanicOffset(CrewPanic, D, Now + 1.0) + S.Avoid + (Dr.bMoving ? S.AvoidWay : FVector::ZeroVector);
		return Dr;
	}, 1);
	// A craft passing 30 m up 50 m ahead of it as it grazes, coming at it: it turns and gallops off the way it came,
	// loops round and walks back to its path.
	SlonkFauna::FPanic Panic;
	int32 Startles = 0;
	const FHerdRun Fright = RunHerdWith(G, R, GrazeStart + 10.0, 130.0, 30.0, 1, FineAt, [&](SlonkFauna::FHerdState& S, double Now, double Dt)
	{
		FHerdDrive Dr;
		if (S.bStarted && Startles == 0)
		{
			SlonkFauna::FFaunaThreat Craft;
			Craft.PosCm = S.FramePos + Facing(S) * 5000.0 + S.FramePos.GetSafeNormal() * 3000.0;
			Craft.VelCmS = -Facing(S) * 8000.0;
			Craft.AltitudeCm = 3000.0;
			Startles += SlonkFauna::StartleGroup(Panic, D, S.FramePos, Craft, Now) ? 1 : 0;
		}
		Dr.bShaken = Panic.IsLive(Now);
		Dr.Offset = SlonkFauna::PanicOffset(Panic, D, Now);
		Dr.Ahead = SlonkFauna::PanicOffset(Panic, D, Now + 1.0);
		return Dr;
	}, 1);
	// Walking into its trot over ground tilted 10.7 degrees (as steep as its grass is) along its east, every way across.
	const FVector Mid = G.Centre * R;
	const double Rise = FMath::Tan(FMath::DegreesToRadians(10.7));
	const FHerdRun Slope = RunHerd(G, R, WalkStart, 60.0, 30.0, 1, [&](const FVector& Dir) { return 5000.0 + Rise * FVector::DotProduct(Dir * R - Mid, G.East); }, 2);
	auto Line = [](const TCHAR* What, const FHerdRun& Run)
	{
		return FString::Printf(TEXT("%s %.2f cm/s on average, %.1f at worst (%.1f while its walkers turned; %d of %d over 10 cm/s), a walker's yaw %.3f rad a frame at most"), What, Run.MeanSlip(),
			Run.WorstSlip, Run.WorstTurnSlip, Run.Slides, Run.Slips, Run.WorstYawStep);
	};
	AddInfo(FString::Printf(TEXT("planted hooves through its turns: %s; %s; %s (startled %d times); %s; %s"), *Line(TEXT("setting off"), SetOff), *Line(TEXT("stopping"), Stop),
		*Line(TEXT("someone walking at it from ahead and away"), CrewRun), CrewStartles, *Line(TEXT("a craft low ahead of it (its whole flight)"), Fright), *Line(TEXT("on a 10.7 degree slope"), Slope)));
	AddInfo(FString::Printf(TEXT("on the move and grazing, a walker's yaw turns %.3f and %.3f rad a frame at most"), Walk.WorstYawStep, Graze.WorstYawStep));
	TestTrue(TEXT("it set off, stopped, walked off from someone and fled a craft, each a whole turn"), SetOff.MinTravel < 0.01 && SetOff.MaxTravel > 0.99 && Stop.MinTravel < 0.01 && Stop.MaxTravel > 0.99
		&& CrewRun.MinTravel < 0.01 && CrewRun.MaxTravel > 0.99 && Fright.MinTravel < 0.01 && Fright.MaxTravel > 0.99 && Startles == 1 && CrewStartles == 0);
	for (const FHerdRun* Run : { &SetOff, &Stop, &CrewRun, &Fright })
	{
		TestTrue(TEXT("through its turns, a planted hoof slides less than a centimetre a second on average"), Run->MeanSlip() < 1.0 && Run->TurnSlips > 100);
		TestTrue(TEXT("hardly one in ten thousand more than 10 cm/s, and none more than 25 cm/s"), Run->Slides * 10000 <= Run->Slips && Run->WorstSlip < 25.0);
	}
	TestTrue(TEXT("up and across a slope, a planted hoof slides less than a centimetre a second"), Slope.MeanTravelSlip() < 1.0 && Slope.WorstTravelSlip < 6.0);
	// A walker's yaw turns at most about 7.9 rad/s (a frightened herd's walkers turning right round in 0.4 s), a quarter of
	// a radian at 30 Hz: a turn the wrong way round its wrap would be a jump of up to a whole turn.
	for (const FHerdRun* Run : { &Walk, &Graze, &SetOff, &Stop, &CrewRun, &Fright, &Slope })
	{
		TestTrue(TEXT("no walker's yaw jumps between two frames"), Run->WorstYawStep < 0.3);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaHerdStartleTest, "Slonk.Fauna.HerdStartle", SLONK_TEST_FLAGS)
bool FSlonkFaunaHerdStartleTest::RunTest(const FString& Parameters)
{
	// A craft passing 30 m over a herd startles it: it gallops off (at a grazer's gallop, from a stand), heads up, and
	// walks back onto its path two minutes after its fright; one passing 300 m up does not. Someone run in close to it
	// startles it; someone walking up to it sees it walk off, keeping about 50 m from them, and come back once they leave.
	// Startled again while it flees, it turns away from the new fright carrying on its gallop: nothing snaps (its place,
	// its speed, its walkers' heads).
	const SlonkFauna::FFaunaWorld W = EarthHerds();
	SlonkFauna::FFaunaGroup G;
	if (!TestHerd(W, G)) { AddError(TEXT("no herd found on the Earth-like world")); return false; }
	const double R = W.P.RadiusCm;
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Herd);
	auto Height = [&W](const FVector& Dir) { return FMath::Max(SlonkTerrain::Sample(W.P, Dir, 2000.0).HeightCm, 0.0); };
	const double T0 = G.CycleSec * 2.0 + G.WalkB + 2.0;
	auto Step = [&](SlonkFauna::FHerdState& S, const SlonkFauna::FPanic& P, double Now, double Dt, int32 k)
	{
		const FVector Ahead = SlonkFauna::PanicOffset(P, D, Now + 1.0) + S.Avoid;
		SlonkFauna::StepHerd(S, G, R, Now, Dt, SlonkFauna::PanicOffset(P, D, Now) + S.Avoid, P.IsLive(Now), false, k % 4 == 0, Height, 0.0, nullptr, &Ahead);
	};
	// A pass straight across it at 80 m/s, from 600 m before it to 600 m after, the herd stepped at 20 Hz.
	auto Pass = [&](double AltitudeCm, SlonkFauna::FPanic& Panic, SlonkFauna::FHerdState& S)
	{
		SlonkFauna::StartHerd(S, G, T0);
		Step(S, Panic, T0, 0.0, 0);
		const FVector Up = S.FramePos.GetSafeNormal();
		const FVector Across = S.FrameRot.GetAxisY();
		const FVector Mid = S.FramePos + Up * AltitudeCm;
		int32 Startles = 0;
		for (int32 k = 1; k <= 300; ++k)
		{
			const double Now = T0 + k / 20.0;
			SlonkFauna::FFaunaThreat Craft;
			Craft.PosCm = Mid + Across * (8000.0 * (k / 20.0 - 7.5));
			Craft.VelCmS = Across * 8000.0;
			Craft.AltitudeCm = AltitudeCm;
			Startles += SlonkFauna::StartleGroup(Panic, D, S.FramePos, Craft, Now) ? 1 : 0;
			Step(S, Panic, Now, 1.0 / 20.0, k);
		}
		return Startles;
	};
	SlonkFauna::FPanic Low, High;
	SlonkFauna::FHerdState SLow, SHigh;
	const int32 LowStartles = Pass(3000.0, Low, SLow);
	const int32 HighStartles = Pass(30000.0, High, SHigh);
	// Its flight after the pass: how far off its path 30 s on, how fast at its fastest, and back on its path after.
	double Fastest = 0.0, FastestAt = 0.0;
	for (int32 k = 301; k <= 20 * 130; ++k)
	{
		const double Now = T0 + k / 20.0;
		Step(SLow, Low, Now, 1.0 / 20.0, k);
		if (SLow.SpeedMS > Fastest) { Fastest = SLow.SpeedMS; FastestAt = Now - Low.T0; }
	}
	const double Fled = SlonkFauna::PanicOffset(Low, D, Low.T0 + 30.0).Size();
	const double Back = SlonkFauna::PanicOffset(Low, D, Low.T0 + SlonkFauna::PanicSec + 0.01).Size();
	// On its path again: where a herd never startled stands then.
	SlonkFauna::FHerdState Calm;
	SlonkFauna::FPanic NoPanic;
	SlonkFauna::StartHerd(Calm, G, Low.T0 + 125.0);
	Step(Calm, NoPanic, Low.T0 + 125.0, 0.0, 0);
	SlonkFauna::FHerdState Again = SLow;
	Step(Again, Low, Low.T0 + 125.0, 0.0, 0);
	const double OffPath = FVector::Dist(Again.FramePos, Calm.FramePos);
	AddInfo(FString::Printf(TEXT("a herd of %d: a pass 30 m up startles it %d times; it gallops off at up to %.1f m/s (%.0f s in), %.0f m off its path 30 s on, %.2f m off it two minutes after (%.2f cm from a herd never startled); a pass 300 m up startles it %d times"),
		G.Members, LowStartles, Fastest, FastestAt, Fled / 100.0, Back / 100.0, OffPath, HighStartles));
	TestTrue(TEXT("a craft passing low startles the herd"), LowStartles >= 1 && Fled > 1.5e4);
	TestEqual(TEXT("one passing high does not"), HighStartles, 0);
	TestEqual(TEXT("back on its path two minutes after"), Back, 0.0);
	TestTrue(TEXT("exactly where it would have been"), OffPath < 0.01);
	TestTrue(TEXT("at a grazer's gallop, from a stand"), Fastest > 6.0 && Fastest < 16.0 && FastestAt > 2.0);

	// Someone on foot: run in to 15 m of its nearest member, it bolts; 80 m off, it does not.
	SlonkFauna::FHerdState Here;
	SlonkFauna::StartHerd(Here, G, T0);
	Step(Here, NoPanic, T0, 0.0, 0);
	const FVector Side = Here.FrameRot.GetAxisY();
	SlonkFauna::FFaunaThreat Walker;
	Walker.bCraft = false;
	Walker.PosCm = Here.FramePos + Side * (G.HerdRadiusCm + 1500.0);
	SlonkFauna::FPanic Near, Far;
	TestTrue(TEXT("someone run in close startles it"), SlonkFauna::StartleGroup(Near, D, Here.FramePos, Walker, T0, G.HerdRadiusCm));
	Walker.PosCm = Here.FramePos + Side * (G.HerdRadiusCm + 8000.0);
	TestFalse(TEXT("someone 80 m off does not"), SlonkFauna::StartleGroup(Far, D, Here.FramePos, Walker, T0, G.HerdRadiusCm));
	// Someone walking straight at it from 120 m off its edge at 1.4 m/s for a minute and a half, then away for five and a
	// half: it keeps clear of them unstartled, and comes back to its path once they have gone.
	SlonkFauna::FHerdState Kept = Here;
	SlonkFauna::FPanic KeptPanic;
	FVector Crew = Kept.FramePos + Side * (G.HerdRadiusCm + 12000.0);
	const FVector Toward = -Side;
	double MinGap = TNumericLimits<double>::Max(), MaxAvoid = 0.0;
	int32 CrewStartles = 0;
	for (int32 k = 1; k <= 20 * 420; ++k)
	{
		const double Now = T0 + k / 20.0;
		const bool bComing = k <= 20 * 90;
		Crew += (bComing ? Toward : -Toward) * (140.0 / 20.0);
		SlonkFauna::FFaunaThreat Foot;
		Foot.bCraft = false;
		Foot.PosCm = Crew;
		CrewStartles += SlonkFauna::StartleGroup(KeptPanic, D, Kept.FramePos, Foot, Now, G.HerdRadiusCm) ? 1 : 0;
		const bool bMoving = SlonkFauna::KeepFromCrew(Kept, G, D, TArray<FVector>{ Crew }, 1.0 / 20.0);
		const FVector Ahead = SlonkFauna::PanicOffset(KeptPanic, D, Now + 1.0) + Kept.Avoid + (bMoving ? Kept.AvoidWay : FVector::ZeroVector);
		SlonkFauna::StepHerd(Kept, G, R, Now, 1.0 / 20.0, SlonkFauna::PanicOffset(KeptPanic, D, Now) + Kept.Avoid, KeptPanic.IsLive(Now), bMoving, k % 4 == 0, Height, 0.0, nullptr, &Ahead);
		if (bComing && k > 20 * 30) { MinGap = FMath::Min(MinGap, SlonkFauna::CrewGapCm(G, Kept.FramePos, Crew)); }
		MaxAvoid = FMath::Max(MaxAvoid, Kept.Avoid.Size());
	}
	AddInfo(FString::Printf(TEXT("someone walking at it for 90 s: it keeps %.0f m from them at the nearest (walked %.0f m off its path), startled %d times; %.1f m off its path 5.5 minutes after they turned away"),
		MinGap / 100.0, MaxAvoid / 100.0, CrewStartles, Kept.Avoid.Size() / 100.0));
	TestTrue(TEXT("it keeps 40 to 60 m from someone walking at it"), MinGap > 3500.0 && MinGap < 6500.0 && MaxAvoid > 3000.0);
	TestEqual(TEXT("unstartled"), CrewStartles, 0);
	TestTrue(TEXT("and comes back to its path once they leave"), Kept.Avoid.Size() < 100.0);

	// Startled again 12 s into its flight from the other side: it turns away from the new fright with the speed it had
	// (its place and its speed run on with no jump), and its walkers' heads, up in alarm, stay as they were.
	SlonkFauna::FPanic Twice;
	SlonkFauna::FHerdState STwice;
	SlonkFauna::StartHerd(STwice, G, T0);
	Step(STwice, Twice, T0, 0.0, 0);
	SlonkFauna::FFaunaThreat First;
	First.PosCm = STwice.FramePos + STwice.FrameRot.GetAxisX() * 5000.0 + STwice.FramePos.GetSafeNormal() * 3000.0;
	First.AltitudeCm = 3000.0;
	TestTrue(TEXT("startled by a craft 50 m off"), SlonkFauna::StartleGroup(Twice, D, STwice.FramePos, First, T0));
	const double Ts = T0 + 12.0;
	const FVector Before = SlonkFauna::PanicOffset(Twice, D, Ts), BeforeV = (Before - SlonkFauna::PanicOffset(Twice, D, Ts - 0.05)) / 0.05;
	float GdBefore[SlonkFauna::HerdData::Num] = {};
	SlonkFauna::FHerdState SAt = STwice;
	SlonkFauna::StepHerd(SAt, G, R, Ts, 0.0, Before, true, false, true, Height, 0.0, GdBefore);
	SlonkFauna::HerdFrightData(Twice, Ts, 0.0, GdBefore);
	SlonkFauna::FFaunaThreat Second;
	Second.PosCm = SAt.FramePos + SAt.FrameRot.GetAxisY() * 5000.0 + SAt.FramePos.GetSafeNormal() * 3000.0;
	Second.AltitudeCm = 3000.0;
	const FVector FirstDir = Twice.Dir;
	TestTrue(TEXT("startled again 12 s on"), SlonkFauna::StartleGroup(Twice, D, SAt.FramePos, Second, Ts));
	const FVector After = SlonkFauna::PanicOffset(Twice, D, Ts), AfterV = (SlonkFauna::PanicOffset(Twice, D, Ts + 0.05) - After) / 0.05;
	float GdAfter[SlonkFauna::HerdData::Num] = {};
	FMemory::Memcpy(GdAfter, GdBefore, sizeof(GdAfter));
	SlonkFauna::HerdFrightData(Twice, Ts, 0.0, GdAfter);
	TArray<FTransform> Xf;
	TArray<float> Cu;
	SlonkFauna::MembersOf(G, Xf, Cu);
	const float T = (float)FMath::Fmod(Ts, SlonkFauna::ClockPeriodSec);
	const FVector3f Muzzle(50.f, 0.f, 41.f), Hoof(10.f, 8.f, 0.f);
	float Moved = 0.f;
	for (int32 m = 0; m < Xf.Num(); ++m)
	{
		const float* C = Cu.GetData() + m * SlonkFauna::Custom::Num;
		const FVector3f Home = (FVector3f)Xf[m].GetTranslation();
		const float Size = C[SlonkFauna::Custom::SizeM];
		for (const TPair<FVector3f, FLinearColor>& V : { TPair<FVector3f, FLinearColor>(Muzzle, FLinearColor(0.25f, 1.f, 0.5f, 0.375f)), TPair<FVector3f, FLinearColor>(Hoof, FLinearColor(0.25f, 1.f, 0.75f, 0.75f)) })
		{
			const FVector3f LPP = Home + V.Key * Size;
			Moved = FMath::Max(Moved, (SlonkFauna::WalkerMotion(V.Key, LPP, V.Value, T, C, GdBefore, 3000.f, 960.f) - SlonkFauna::WalkerMotion(V.Key, LPP, V.Value, T, C, GdAfter, 3000.f, 960.f)).Size());
		}
	}
	AddInfo(FString::Printf(TEXT("startled again 12 s into its flight: it turns %.0f deg away, its place moving %.3f cm and its speed %.2f m/s as it comes (it was galloping at %.1f m/s); its walkers' heads and hooves move %.4f units"),
		FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(FirstDir, Twice.Dir), -1.0, 1.0))), FVector::Dist(Before, After), FVector::Dist(BeforeV, AfterV) / 100.0, BeforeV.Size() / 100.0, Moved));
	TestTrue(TEXT("its place does not jump"), FVector::Dist(Before, After) < 0.5);
	TestTrue(TEXT("nor its speed"), FVector::Dist(BeforeV, AfterV) < 50.0 && BeforeV.Size() > 300.0);
	TestTrue(TEXT("nor its walkers"), Moved < 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaHerdStaysPutTest, "Slonk.Fauna.HerdStaysPut", SLONK_TEST_FLAGS)
bool FSlonkFaunaHerdStaysPutTest::RunTest(const FString& Parameters)
{
	// The herds are pure functions of the world and the place: a cell's herd is the same whichever order the cells are
	// read in, its members the same every time, and a herd stepped twice alike steps alike. Gone to a place 30 km off and
	// come back, the same herds are about as before, each where one never left would be.
	const SlonkFauna::FFaunaWorld W = EarthHerds();
	SlonkFauna::FFaunaGroup G0;
	if (!TestHerd(W, G0)) { AddError(TEXT("no herd found on the Earth-like world")); return false; }
	const double R = W.P.RadiusCm;
	TArray<SlonkFauna::FFaunaCell> Cells;
	SlonkFauna::CellsNear(W.P, EFaunaKind::Herd, G0.Centre, 8.0e5, Cells);
	TArray<TOptional<SlonkFauna::FFaunaGroup>> Forward, Backward;
	Forward.SetNum(Cells.Num());
	Backward.SetNum(Cells.Num());
	for (int32 i = 0; i < Cells.Num(); ++i) { SlonkFauna::FFaunaGroup G; if (SlonkFauna::GroupInCell(W, Cells[i], G)) { Forward[i] = G; } }
	for (int32 i = Cells.Num() - 1; i >= 0; --i) { SlonkFauna::FFaunaGroup G; if (SlonkFauna::GroupInCell(W, Cells[i], G)) { Backward[i] = G; } }
	int32 Held = 0, Mismatch = 0;
	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		Held += Forward[i].IsSet() ? 1 : 0;
		Mismatch += Forward[i].IsSet() == Backward[i].IsSet() && (!Forward[i].IsSet() || Forward[i].GetValue() == Backward[i].GetValue()) ? 0 : 1;
	}
	TArray<FTransform> Xa, Xb;
	TArray<float> Ca, Cb;
	SlonkFauna::MembersOf(G0, Xa, Ca);
	SlonkFauna::MembersOf(G0, Xb, Cb);
	bool bMembersSame = Xa.Num() == Xb.Num() && Ca == Cb && Xa.Num() == G0.Members;
	for (int32 i = 0; bMembersSame && i < Xa.Num(); ++i) { bMembersSame &= Xa[i].Equals(Xb[i], 0.0); }
	// Stepped twice alike.
	auto Height = [&W](const FVector& Dir) { return FMath::Max(SlonkTerrain::Sample(W.P, Dir, 2000.0).HeightCm, 0.0); };
	SlonkFauna::FHerdState Sa, Sb;
	SlonkFauna::StartHerd(Sa, G0, 1000.0);
	SlonkFauna::StartHerd(Sb, G0, 1000.0);
	bool bStepsSame = true;
	for (int32 k = 0; k < 60; ++k)
	{
		float Ga[SlonkFauna::HerdData::Num] = {}, Gb[SlonkFauna::HerdData::Num] = {};
		SlonkFauna::StepHerd(Sa, G0, R, 1000.0 + k * 0.5, k == 0 ? 0.0 : 0.5, FVector::ZeroVector, false, false, k % 3 == 0, Height, 0.0, Ga);
		SlonkFauna::StepHerd(Sb, G0, R, 1000.0 + k * 0.5, k == 0 ? 0.0 : 0.5, FVector::ZeroVector, false, false, k % 3 == 0, Height, 0.0, Gb);
		bStepsSame &= FMemory::Memcmp(Ga, Gb, sizeof(Ga)) == 0 && Sa.FramePos == Sb.FramePos && Sa.FrameRot == Sb.FrameRot;
	}
	// About A, about B 30 km off, back at A.
	const FVector CamA = CameraOver(W, G0.Centre);
	const FVector CamB = CameraOver(W, AlongGround(G0.Centre, 3.0e6, R, 2.0));
	SlonkFauna::FFaunaField F, Stayed;
	F.SetWorld(W);
	Stayed.SetWorld(W);
	TArray<uint64> A1, B1, A2, Kept;
	ScanAndSelect(F, CamA, A1);
	ScanAndSelect(Stayed, CamA, Kept);
	ScanAndSelect(F, CamB, B1);
	int32 Overlap = 0;
	for (const uint64 Id : B1) { Overlap += A1.Contains(Id) ? 1 : 0; }
	ScanAndSelect(F, CamA, A2);
	ScanAndSelect(Stayed, CamA, Kept);
	int32 Moved = 0;
	for (const TPair<uint64, SlonkFauna::FFaunaField::FGroupRef>& A : F.GetActive())
	{
		const SlonkFauna::FFaunaField::FGroupRef* Never = Stayed.GetActive().Find(A.Key);
		Moved += Never && **Never == *A.Value && SlonkFauna::PathAt(*A.Value, R, 2345.6).PosCm.Equals(SlonkFauna::PathAt(**Never, R, 2345.6).PosCm, 0.0) ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("%d cells round a herd of %d: %d hold a herd, %d read otherwise backward; about A %d herds, about B 30 km off %d (%d of A's), back at A %d, %d not where the herds never left are"),
		Cells.Num(), G0.Members, Held, Mismatch, A1.Num(), B1.Num(), Overlap, A2.Num(), Moved));
	TestEqual(TEXT("a cell holds the same herd whatever order the cells are read in"), Mismatch, 0);
	TestTrue(TEXT("its members the same every time"), bMembersSame);
	TestTrue(TEXT("a herd stepped twice alike steps alike"), bStepsSame);
	TestTrue(TEXT("herds about A, the test's herd among them"), A1.Contains(G0.Id));
	TestEqual(TEXT("none of A's about B"), Overlap, 0);
	TestTrue(TEXT("back at A, the same herds"), A2 == A1 && Kept == A1);
	TestEqual(TEXT("each where one never left would be"), Moved, 0);
	// Too high a camera keeps none: over 1.5 km (and its hysteresis) above the ground.
	TArray<uint64> High;
	ScanAndSelect(F, G0.Centre * (R + SlonkFauna::KindDesc(EFaunaKind::Herd).MaxAltitudeCm * 1.4 + FMath::Max(SlonkTerrain::SurfaceHeightCm(W.P, G0.Centre), 0.0)), High);
	TestEqual(TEXT("none under a camera too high"), High.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaWalkerTest, "Slonk.Fauna.Walker", SLONK_TEST_FLAGS)
bool FSlonkFaunaWalkerTest::RunTest(const FString& Parameters)
{
	// An Earth grazer: 150, 30 and 2 triangles, a metre from its tail's tip to its muzzle, its hooves on z 0; its vertex
	// colours as the material reads them (a leg's flex from 0 at the hip straight down to 1 at the hoof, its blue its place
	// in the gait by which leg it is, its hoof dark; the neck's and head's flex from its root to the muzzle; the tail's
	// from its root; the body rigid, its side by its sign); every face wound to show the side its normals point to, as
	// FBuild::Card's are. The material's motion keeps every vertex inside the instance's padded box, whatever its size,
	// circle, gait, head, ground and speck floor; its LODs come in where a walker is about 30 and 6 pixels long.
	using namespace UE::Geometry;
	namespace Vc = SlonkFauna::Vc;
	const FFaunaBodyPlan Plan = SlonkFauna::EarthPlan(EFaunaKind::Herd);
	const int32 WantTris[SlonkFaunaMeshes::NumLods] = { 150, 30, 2 };
	int32 Tris[SlonkFaunaMeshes::NumLods] = {};
	int32 BadColour = 0, BadWinding = 0, Legs = 0, Necks = 0, Hooves = 0;
	FBox Box0(ForceInit);
	FDynamicMesh3 Lod0;
	for (int32 L = 0; L < SlonkFaunaMeshes::NumLods; ++L)
	{
		FDynamicMesh3 M;
		if (!SlonkFaunaMeshes::BuildLod(Plan, L, M)) { AddError(TEXT("no walker built")); return false; }
		Tris[L] = M.TriangleCount();
		const FDynamicMeshColorOverlay* C = M.Attributes()->PrimaryColors();
		const FDynamicMeshNormalOverlay* N = M.Attributes()->PrimaryNormals();
		for (const int32 T : M.TriangleIndicesItr())
		{
			FVector3d Fn, Centroid; double Area;
			M.GetTriInfo(T, Fn, Area, Centroid);
			const FIndex3i E = N->GetTriangle(T);
			BadWinding += Fn.Dot(FVector3d(N->GetElement(E.A) + N->GetElement(E.B) + N->GetElement(E.C))) > 0.0 ? 0 : 1;
			const FIndex3i Ce = C->GetTriangle(T);
			const FIndex3i Tv = M.GetTriangle(T);
			for (int32 k = 0; k < 3; ++k)
			{
				const FVector4f Col = C->GetElement(Ce[k]);
				const FVector3d P = M.GetVertex(Tv[k]);
				if (L == 0) { Box0 += (FVector)P; }
				bool bOk = true;
				if (FMath::IsNearlyEqual(Col.W, Vc::JointLeg))
				{
					// Straight down from the hip, 38 cm up: its place in the gait by its leg (hind at x -33, left at -y).
					const float WantFlex = (float)FMath::Clamp((38.0 - P.Z) / 38.0, 0.0, 1.0);
					const float WantPhase = (P.Y < 0.0 ? 0.f : 0.5f) + (P.X > -10.0 ? 0.25f : 0.f);
					bOk = FMath::IsNearlyEqual(Col.Y, WantFlex, 1e-3f) && Col.Z == WantPhase && (Col.X == (WantFlex > 0.99f ? Vc::SurfaceBeak : Vc::SurfaceBody));
					++Legs;
					Hooves += Col.Y > 0.99f ? 1 : 0;
				}
				else if (FMath::IsNearlyEqual(Col.W, Vc::JointNeck))
				{
					bOk = (Col.X == Vc::SurfaceBody || Col.X == Vc::SurfaceBeak) && Col.Y >= 0.f && Col.Y <= 1.f && P.X > 5.0;
					++Necks;
				}
				else if (FMath::IsNearlyEqual(Col.W, Vc::JointTail)) { bOk = Col.X == Vc::SurfaceTail && Col.Y >= 0.f && Col.Y <= 1.f && Col.Z == Vc::SideMiddle && P.X < -40.0; }
				else if (Col.W == Vc::JointRigid) { bOk = Col.X == Vc::SurfaceBody && Col.Y == 0.f && Col.Z == (P.Y > 0.5 ? Vc::SideRight : (P.Y < -0.5 ? Vc::SideLeft : Vc::SideMiddle)); }
				else { bOk = false; }
				BadColour += bOk ? 0 : 1;
			}
		}
		if (L == 0) { Lod0 = MoveTemp(M); }
	}
	AddInfo(FString::Printf(TEXT("walker LODs %d, %d and %d triangles; LOD 0 %.0f x %.0f x %.0f cm of the unit mesh (z from %.1f); %d vertices off the colour contract, %d faces wound inward; %d leg vertices (%d at a hoof), %d of the neck and head"),
		Tris[0], Tris[1], Tris[2], Box0.GetSize().X, Box0.GetSize().Y, Box0.GetSize().Z, Box0.Min.Z, BadColour, BadWinding, Legs, Hooves, Necks));
	for (int32 L = 0; L < SlonkFaunaMeshes::NumLods; ++L) { TestEqual(FString::Printf(TEXT("LOD %d's triangles"), L), Tris[L], WantTris[L]); }
	TestEqual(TEXT("every vertex as the material reads it"), BadColour, 0);
	TestEqual(TEXT("every face shows the side its normals point to"), BadWinding, 0);
	TestTrue(TEXT("four legs to the hooves, a neck and head"), Hooves > 0 && Necks > 0);
	TestTrue(TEXT("a unit mesh a metre long"), FMath::IsNearlyEqual(Box0.Min.X, -50.0, 0.5) && FMath::IsNearlyEqual(Box0.Max.X, 50.0, 0.5));
	TestTrue(TEXT("its hooves on z 0"), FMath::IsNearlyEqual(Box0.Min.Z, 0.0, 1e-3));

	// The material's motion over LOD 0's vertices: every size, stride, circle, phase, clock, speed, travel share, fright,
	// ground and speck size, inside the box each instance is culled by (UPlanetInstancesComponent::InstanceBox, the kind's
	// padding); and the hooves' places in the gait as they were.
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Herd);
	const FBox Padded = Box0.ExpandBy(Box0.GetExtent() * 0.15 + FVector(20.0 + D.CullPaddingUnits));
	const FDynamicMeshColorOverlay* C0 = Lod0.Attributes()->PrimaryColors();
	FRandomStream Rng(57);
	int32 Escaped = 0, Tried = 0;
	double Margin = TNumericLimits<double>::Max();
	for (int32 Case = 0; Case < 240; ++Case)
	{
		float Cu[SlonkFauna::Custom::Num] = {};
		float Gd[SlonkFauna::HerdData::Num] = {};
		const float Size = Rng.FRandRange((float)SlonkFauna::Walker::MinSizeM, 2.6f);
		const float Stride = (float)SlonkFauna::Walker::StrideShare * Size * Rng.FRandRange(0.97f, 1.03f);
		const int32 K = FMath::Max(3, FMath::FloorToInt32(UE_TWO_PI * (float)SlonkFauna::Walker::MaxCircleM / Stride * Rng.FRand()));
		Cu[SlonkFauna::Custom::SizeM] = Size;
		Cu[SlonkFauna::Custom::Seed] = Rng.FRand();
		Cu[SlonkFauna::Custom::Phase] = Rng.FRand();
		Cu[SlonkFauna::Custom::RateHz] = Stride;
		Cu[SlonkFauna::Custom::PerKind] = (Rng.FRand() < 0.5f ? -1.f : 1.f) * FMath::Min(K * Stride / UE_TWO_PI, (float)SlonkFauna::Walker::MaxCircleM);
		const float T = Rng.FRandRange(0.f, 3600.f);
		Gd[SlonkFauna::HerdData::Heading] = Rng.FRandRange(-3.2f, 3.2f);
		Gd[SlonkFauna::HerdData::SpeedMS] = Case % 4 == 0 ? Rng.FRandRange(4.f, 13.f) : Rng.FRandRange(0.f, 3.4f);
		Gd[SlonkFauna::HerdData::DistM] = Rng.FRandRange(0.f, 2600.f);
		Gd[SlonkFauna::HerdData::GrazeSec] = Rng.FRandRange(0.f, 3600.f);
		Gd[SlonkFauna::HerdData::ClockT0] = FMath::Fmod(T - Rng.FRandRange(0.f, 0.1f) + 3600.f, 3600.f);
		Gd[SlonkFauna::HerdData::Travel] = Rng.FRand();
		// Its latest turn, lately or long ago, to walk or back to graze, from anywhere on its circle to any way.
		Gd[SlonkFauna::HerdData::PivotRef] = Gd[SlonkFauna::HerdData::DistM] - (Case % 3 == 0 ? 1000.f : Rng.FRandRange(0.f, 8.f)) + (Case % 5 < 2 ? (float)SlonkFauna::HerdData::PivotBack : 0.f);
		Gd[SlonkFauna::HerdData::GrazeRef] = Gd[SlonkFauna::HerdData::GrazeSec] - Rng.FRandRange(0.f, 60.f);
		Gd[SlonkFauna::HerdData::WalkKap] = Rng.FRandRange(-1.f, 1.f) * (float)SlonkFauna::Walker::MaxWalkKap;
		Gd[SlonkFauna::HerdData::TurnHeading] = Rng.FRandRange(-3.2f, 3.2f);
		Gd[SlonkFauna::HerdData::Startled] = Case % 2 == 0 ? 1.f : 0.f;
		const float PanicAge = Rng.FRandRange(0.f, 120.f);
		Gd[SlonkFauna::HerdData::PanicT0] = FMath::Fmod(T - PanicAge + 3600.f, 3600.f);
		Gd[SlonkFauna::HerdData::OnsetT0] = FMath::Fmod(T - PanicAge - Rng.FRandRange(0.f, 1.f) + 3600.f, 3600.f);
		for (int32 m = 0; m < SlonkFauna::HerdData::GroundNodes; ++m) { Gd[SlonkFauna::HerdData::Ground0 + m] = Rng.FRandRange(-1.f, 1.f) * (float)SlonkFauna::HerdData::MaxGroundCm; }
		Gd[SlonkFauna::HerdData::GroundHalfCm] = Rng.FRandRange(1500.f, 4500.f);
		const FVector3f Home(Rng.FRandRange(-3500.f, 3500.f), Rng.FRandRange(-3500.f, 3500.f), 0.f);
		// Near (no speck floor), a few hundred metres off, and far (four times its size).
		const float Dist = Case % 3 == 0 ? 2000.f : (Case % 3 == 1 ? Size * 100.f * 960.f / 3.5f : 4.0e5f);
		for (const int32 V : Lod0.VertexIndicesItr())
		{
			const FVector3f LP = (FVector3f)Lod0.GetVertex(V);
			FVector4f Cv = FVector4f(0.f, 0.f, 0.5f, 0.f);
			for (const int32 El : C0->ElementIndicesItr()) { if (C0->GetParentVertex(El) == V) { Cv = C0->GetElement(El); break; } }
			const FVector3f Moved = LP + SlonkFauna::WalkerMotion(LP, Home + LP * Size, FLinearColor(Cv.X, Cv.Y, Cv.Z, Cv.W), T, Cu, Gd, Dist, 960.f);
			++Tried;
			const FVector Mv((double)Moved.X, (double)Moved.Y, (double)Moved.Z);
			Escaped += Padded.IsInside(Mv) ? 0 : 1;
			Margin = FMath::Min(Margin, FMath::Min3(FMath::Min(Mv.X - Padded.Min.X, Padded.Max.X - Mv.X), FMath::Min(Mv.Y - Padded.Min.Y, Padded.Max.Y - Mv.Y), FMath::Min(Mv.Z - Padded.Min.Z, Padded.Max.Z - Mv.Z)));
		}
	}
	AddInfo(FString::Printf(TEXT("%d vertices moved by the material: %d outside the box padded %.0f units (the closest %.0f units inside)"), Tried, Escaped, D.CullPaddingUnits, Margin));
	TestEqual(TEXT("the material keeps every vertex in its instance's box"), Escaped, 0);
	// That, and every test of the walkers' motion, holds of the float copy (SlonkFauna::WalkerMotion) only while the
	// material's walker branch is what was copied: an edit to the walker branch of fauna_material.py's WPO_CODE alone
	// fails here, until the copy is brought in line and the CRC with it.
	const FString WalkerBlock = MaterialMotionBlock(EMotionBlock::Walkers);
	const uint32 WalkerBlockCrc = FCrc::StrCrc32(*WalkerBlock);
	AddInfo(FString::Printf(TEXT("fauna_material.py's WPO_CODE, the walker branch: %d characters of code, CRC 0x%08x (0x%08x when the copy was made)"), WalkerBlock.Len(), WalkerBlockCrc, MaterialWalkerBlockCrc));
	TestTrue(TEXT("fauna_material.py's walker branch found"), WalkerBlock.Len() > 2000 && WalkerBlock.Contains(TEXT("float wyaw = wyc + wtr * wdy;")));
	TestEqual(TEXT("WalkerMotion copies the material's walker branch as it is now"), WalkerBlockCrc, MaterialWalkerBlockCrc);

	// The static mesh: made once and kept, three LODs at the kind's screen sizes; where its LODs come in for a walker 2 m
	// long through a 90-degree view at 16:9 (the engine's screen size: the projection's scale times the radius over the
	// distance; the GPU's radius, its padded box's), and how long it is there in pixels (960 a radian).
	UStaticMesh* Mesh = SlonkFaunaMeshes::GetMesh(Plan, EFaunaKind::Herd);
	TestTrue(TEXT("the walker's mesh is made, and kept"), Mesh != nullptr && Mesh == SlonkFaunaMeshes::GetMesh(Plan, EFaunaKind::Herd));
	if (Mesh)
	{
		TestEqual(TEXT("with three LODs"), Mesh->GetNumLODs(), SlonkFaunaMeshes::NumLods);
		const double LengthM = 2.0;
		UPlanetInstancesComponent* Set = NewObject<UPlanetInstancesComponent>();
		Set->SetStaticMesh(Mesh);
		Set->SetCullPadding(D.CullPaddingUnits);
		Set->SetPieces(TArray<FTransform>{ FTransform(FQuat::Identity, FVector::ZeroVector, FVector(LengthM)) }, TConstArrayView<float>());
		const double Proj = (16.0 / 9.0) / FMath::Tan(FMath::DegreesToRadians(45.0));
		const double GpuR = Set->GetMaxInstanceRadius();
		auto ComesInM = [&](int32 L) { return Proj * GpuR / D.LodScreenSize[L] / 100.0; };
		auto PxAt = [&](double M) { return LengthM * 960.0 / M; };
		AddInfo(FString::Printf(TEXT("a %.1f m walker (LOD radius %.0f cm): 30 triangles from %.0f m (%.1f pixels long), 2 from %.0f m (%.1f pixels)"), LengthM, GpuR, ComesInM(1), PxAt(ComesInM(1)), ComesInM(2), PxAt(ComesInM(2))));
		TestTrue(TEXT("its 30 triangles from about 30 pixels long"), PxAt(ComesInM(1)) > 24.0 && PxAt(ComesInM(1)) < 38.0);
		TestTrue(TEXT("and its 2 from about 6"), PxAt(ComesInM(2)) > 4.5 && PxAt(ComesInM(2)) < 8.0);
		// A tangent basis at every vertex of every LOD (the material turns a walker's normals the way it faces in tangent
		// space: with none, every walker would be lit as if it faced its herd's east).
		int32 Vertices = 0, NoTangent = 0;
		if (const FStaticMeshRenderData* RD = Mesh->GetRenderData())
		{
			for (const FStaticMeshLODResources& L : RD->LODResources)
			{
				const FStaticMeshVertexBuffer& VB = L.VertexBuffers.StaticMeshVertexBuffer;
				for (uint32 i = 0; i < VB.GetNumVertices(); ++i)
				{
					const FVector3f Tx(VB.VertexTangentX(i)), Tz(VB.VertexTangentZ(i)), Ty(VB.VertexTangentY(i));
					++Vertices;
					NoTangent += Tx.Size() > 0.5f && Ty.Size() > 0.5f && FMath::Abs(FVector3f::DotProduct(Tx.GetSafeNormal(), Tz.GetSafeNormal())) < 0.1f ? 0 : 1;
				}
			}
		}
		AddInfo(FString::Printf(TEXT("the walker's mesh: %d vertices over its LODs, %d without a tangent basis at right angles to its normal"), Vertices, NoTangent));
		TestTrue(TEXT("every vertex has a tangent basis"), Vertices > 0 && NoTangent == 0);
	}
	FDynamicMesh3 Nothing;
	TestFalse(TEXT("nothing built yet for a fish"), SlonkFaunaMeshes::BuildLod(SlonkFauna::EarthPlan(EFaunaKind::Leaper), 0, Nothing));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaHerdBudgetTest, "Slonk.Fauna.HerdBudget", SLONK_TEST_FLAGS)
bool FSlonkFaunaHerdBudgetTest::RunTest(const FString& Parameters)
{
	// The herds keep to their budget: about any camera on an Earth-like world at most 16 herds and 960 walkers (144,000
	// triangles at their nearest LOD); a frame's work on 16 of them (each set on the drawn ground, its fright and the
	// crew checked, its ground read again every sixth frame and every frame for two near the camera) well inside the
	// plan's 0.15 ms of the game thread with the fliers'.
	const SlonkFauna::FFaunaWorld W = EarthHerds();
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Herd);
	TArray<SlonkFauna::FFaunaGroup> Herds;
	HerdsOverGlobe(W, 20000, Herds);
	if (Herds.Num() == 0) { AddError(TEXT("no herd found on the Earth-like world")); return false; }
	// Over the busiest places: twice the density, so every cell of grass holds a herd.
	SlonkFauna::FFaunaWorld Dense = W;
	Dense.Density = 2.f;
	int32 MaxGroups = 0, MaxMembers = 0, Over = 0, Places = 0;
	for (int32 i = 0; i < Herds.Num() && Places < 12; i += FMath::Max(Herds.Num() / 12, 1), ++Places)
	{
		SlonkFauna::FFaunaField F;
		F.SetWorld(Dense);
		TArray<uint64> Active;
		ScanAndSelect(F, CameraOver(Dense, Herds[i].Centre), Active);
		MaxGroups = FMath::Max(MaxGroups, F.ActiveGroups(EFaunaKind::Herd));
		MaxMembers = FMath::Max(MaxMembers, F.ActiveMembers(EFaunaKind::Herd));
		Over += F.ActiveGroups(EFaunaKind::Herd) <= D.MaxGroups && F.ActiveMembers(EFaunaKind::Herd) <= D.MaxMembers ? 0 : 1;
	}
	// A frame's work on 16 herds: one herd's loop walked by 16 at times a minute apart (on the drawn ground of chunks 610 m
	// across, built before the clock starts), a craft and someone on foot about each.
	const SlonkFauna::FFaunaGroup& G = Herds[0];
	const double R = W.P.RadiusCm;
	FDrawnGround Ground;
	Ground.P = W.P;
	Ground.Depth = 14;
	auto At = [&Ground](const FVector& Dir) { return Ground.At(Dir); };
	TArray<SlonkFauna::FHerdState> States;
	TArray<SlonkFauna::FPanic> Panics;
	States.SetNum(16);
	Panics.SetNum(16);
	for (int32 h = 0; h < 16; ++h) { SlonkFauna::StartHerd(States[h], G, 500.0 + 60.0 * h); }
	auto Frame = [&](int32 f, double& Sink)
	{
		for (int32 h = 0; h < 16; ++h)
		{
			const double Now = 500.0 + 60.0 * h + f / 60.0;
			SlonkFauna::FHerdState& S = States[h];
			SlonkFauna::FFaunaThreat Craft, Walker;
			Craft.PosCm = S.FramePos + S.FramePos.GetSafeNormal() * 1.0e5;
			Craft.AltitudeCm = 1.0e5;
			Walker.bCraft = false;
			Walker.PosCm = S.FramePos + S.FrameRot.GetAxisX() * 3.0e4;
			SlonkFauna::StartleGroup(Panics[h], D, S.FramePos, Craft, Now);
			SlonkFauna::StartleGroup(Panics[h], D, S.FramePos, Walker, Now, G.HerdRadiusCm);
			const bool bMoving = SlonkFauna::KeepFromCrew(S, G, D, TArray<FVector>{ Walker.PosCm }, 1.0 / 60.0);
			const FVector Ahead = SlonkFauna::PanicOffset(Panics[h], D, Now + 1.0) + S.Avoid + (bMoving ? S.AvoidWay : FVector::ZeroVector);
			float Gd[SlonkFauna::HerdData::Num] = {};
			SlonkFauna::StepHerd(S, G, R, Now, f == 0 ? 0.0 : 1.0 / 60.0, SlonkFauna::PanicOffset(Panics[h], D, Now) + S.Avoid, Panics[h].IsLive(Now), bMoving,
				h < 2 || (f + h) % 6 == 0, At, 0.0, Gd, &Ahead);
			SlonkFauna::HerdFrightData(Panics[h], Now, 0.0, Gd);
			Sink += Gd[SlonkFauna::HerdData::DistM] + S.FramePos.X;
		}
	};
	double Sink = 0.0;
	for (int32 f = 0; f < 120; ++f) { Frame(f, Sink); }
	const double Start = FPlatformTime::Seconds();
	constexpr int32 Frames = 600;
	for (int32 f = 120; f < 120 + Frames; ++f) { Frame(f, Sink); }
	const double FrameMs = (FPlatformTime::Seconds() - Start) * 1000.0 / Frames;
	const int32 Triangles = MaxMembers * 150;
	AddInfo(FString::Printf(TEXT("about %d of the densest places on an Earth-like world: at most %d herds and %d walkers (%d triangles at LOD 0), %d over the caps; a frame's work on 16 herds %.4f ms (the drawn ground read %d chunks)"),
		Places, MaxGroups, MaxMembers, Triangles, Over, FrameMs, Ground.Chunks.Num()));
	TestTrue(TEXT("the frames' places all found"), FMath::IsFinite(Sink));
	TestTrue(TEXT("places with herds to try"), Places >= 6 && MaxGroups > 0);
	TestEqual(TEXT("never over the herds' caps"), Over, 0);
	TestTrue(TEXT("at most 144,000 triangles of walkers at LOD 0"), Triangles <= 144000);
	TestTrue(TEXT("a frame's work on 16 herds well inside 0.15 ms"), FrameMs < 0.1);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaHerdSpacingTest, "Slonk.Fauna.HerdSpacing", SLONK_TEST_FLAGS)
bool FSlonkFaunaHerdSpacingTest::RunTest(const FString& Parameters)
{
	// No two walkers of a herd ever meet: grazing round their circles a stride a step through a whole hour of grazing
	// (and so walking with the herd too, their circles held where they stood when it set off), the middles of any two of
	// them stay at least the longer one's length apart. The tightest herds of an Earth-like world (the most walkers to
	// their spacing), every second of the hour.
	const SlonkFauna::FFaunaWorld W = EarthHerds();
	TArray<SlonkFauna::FFaunaGroup> Herds;
	HerdsOverGlobe(W, 20000, Herds);
	if (Herds.Num() == 0) { AddError(TEXT("no herd found on the Earth-like world")); return false; }
	Herds.Sort([](const SlonkFauna::FFaunaGroup& A, const SlonkFauna::FFaunaGroup& B) { return A.Members / FMath::Square(A.SpacingM) > B.Members / FMath::Square(B.SpacingM); });
	const int32 Tried = FMath::Min(Herds.Num(), 6);
	double Closest = TNumericLimits<double>::Max();
	int32 Close = 0, Walkers = 0, SmallCircles = 0;
	const FLinearColor Rigid(SlonkFauna::Vc::SurfaceBody, 0.f, SlonkFauna::Vc::SideMiddle, SlonkFauna::Vc::JointRigid);
	for (int32 h = 0; h < Tried; ++h)
	{
		const SlonkFauna::FFaunaGroup& G = Herds[h];
		TArray<FTransform> Xf;
		TArray<float> Cu;
		SlonkFauna::MembersOf(G, Xf, Cu);
		const int32 N = Xf.Num();
		Walkers += N;
		TArray<FVector3f> At;
		At.SetNum(N);
		for (int32 m = 0; m < N; ++m) { SmallCircles += FMath::Abs(Cu[m * SlonkFauna::Custom::Num + SlonkFauna::Custom::PerKind]) < 3.5f * Cu[m * SlonkFauna::Custom::Num + SlonkFauna::Custom::RateHz] / UE_TWO_PI ? 1 : 0; }
		for (int32 s = 0; s < 3600; ++s)
		{
			float Gd[SlonkFauna::HerdData::Num] = {};
			Gd[SlonkFauna::HerdData::GrazeSec] = (float)s;
			Gd[SlonkFauna::HerdData::GrazeRef] = (float)s - 900.f;
			Gd[SlonkFauna::HerdData::PivotRef] = -1000.f;
			for (int32 m = 0; m < N; ++m)
			{
				const float* C = Cu.GetData() + m * SlonkFauna::Custom::Num;
				const FVector3f Home = (FVector3f)Xf[m].GetTranslation();
				// Its body's middle: its instance's place and where on its circle it stands.
				At[m] = Home + SlonkFauna::WalkerMotion(FVector3f::ZeroVector, Home, Rigid, 0.f, C, Gd, 2000.f, 960.f) * C[SlonkFauna::Custom::SizeM];
			}
			for (int32 i = 0; i < N; ++i)
			{
				for (int32 j = i + 1; j < N; ++j)
				{
					const double Longer = 100.0 * FMath::Max(Cu[i * SlonkFauna::Custom::Num + SlonkFauna::Custom::SizeM], Cu[j * SlonkFauna::Custom::Num + SlonkFauna::Custom::SizeM]);
					const double Ratio = FVector3f::Dist2D(At[i], At[j]) / Longer;
					Closest = FMath::Min(Closest, Ratio);
					Close += Ratio < 1.0 ? 1 : 0;
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("the %d tightest herds (%d walkers, %d of them on circles of three strides) grazing through an hour: two walkers' middles at least %.2f of the longer one's length apart; %d times a pair closer than that"),
		Tried, Walkers, SmallCircles, Closest, Close));
	TestTrue(TEXT("herds to try"), Tried >= 4);
	TestEqual(TEXT("no two walkers ever closer than the longer one's length"), Close, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaHerdDustTest, "Slonk.Fauna.HerdDust", SLONK_TEST_FLAGS)
bool FSlonkFaunaHerdDustTest::RunTest(const FString& Parameters)
{
	// A herd raises dust off dry ground going faster than a walk, by day, near the camera, on a living world: none as it
	// grazes, stands or walks, more the faster it goes, none on soaked ground, by night, far off or on a dead world; its
	// puffs the size its walkers are, drifting as it goes, paler than the ground's own colour. The components it is drawn
	// with are lent out of a pool and come back to it, one given back resting as long as its last puffs live before it is
	// lent again. No Niagara asset is needed for any of it.
	FScatterPalette Pal;
	SlonkFauna::FHerdDustInput In;
	In.Members = 40;
	In.SpanM = 2.0;
	In.RadiusCm = 2500.0;
	In.Wet = 0.45;
	In.Life = 1.0;
	In.Night = 0.0;
	In.DistCm = 3.0e4;
	In.VelWorldCmS = FVector(300.0, 0.0, 0.0);
	In.Color = SlonkFauna::HerdDustColor(Pal, In.Wet, In.Night);
	auto RateAt = [&](double Speed) { SlonkFauna::FHerdDustInput I = In; I.SpeedMS = Speed; return SlonkFauna::HerdDustFor(I).Rate; };
	const double Speeds[] = { 0.0, 0.5, 1.0, 1.2, 1.5, 2.4, 3.4, 6.0, 11.0 };
	int32 SlowWithDust = 0, NotRising = 0;
	FString Rates;
	float Prev = 0.f;
	for (const double V : Speeds)
	{
		const float Rt = RateAt(V);
		Rates += FString::Printf(TEXT("%s%.1f"), Rates.IsEmpty() ? TEXT("") : TEXT(", "), Rt);
		if (V <= SlonkFauna::DustWalkMS) { SlowWithDust += Rt > 0.f ? 1 : 0; }
		else { NotRising += Rt > Prev ? 0 : 1; }
		Prev = Rt;
	}
	auto With = [&](TFunctionRef<void(SlonkFauna::FHerdDustInput&)> Change) { SlonkFauna::FHerdDustInput I = In; I.SpeedMS = 3.4; Change(I); return SlonkFauna::HerdDustFor(I); };
	const float Soaked = With([](SlonkFauna::FHerdDustInput& I) { I.Wet = 1.0; }).Rate;
	const float Dead = With([](SlonkFauna::FHerdDustInput& I) { I.Life = 0.0; }).Rate;
	const float Dark = With([](SlonkFauna::FHerdDustInput& I) { I.Night = 1.0; }).Rate;
	const float Dusk = With([](SlonkFauna::FHerdDustInput& I) { I.Night = 0.5; }).Rate;
	const float Far = With([](SlonkFauna::FHerdDustInput& I) { I.DistCm = 1.6e5; }).Rate;
	const float Drier = With([](SlonkFauna::FHerdDustInput& I) { I.Wet = 0.2; }).Rate;
	const SlonkFauna::FHerdDust Trot = With([](SlonkFauna::FHerdDustInput&) {});
	const SlonkFauna::FHerdDust Big = With([](SlonkFauna::FHerdDustInput& I) { I.SpanM = 2.4; });
	auto Luma = [](const FLinearColor& C) { return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B; };
	const FLinearColor Ground = FMath::Lerp(Pal.Land2, Pal.Land, (float)(In.Wet * 0.4));
	const FLinearColor Night = SlonkFauna::HerdDustColor(Pal, In.Wet, 0.8);
	AddInfo(FString::Printf(TEXT("a herd of %d raising dust at %s m/s: %s puffs a second; at a trot on soaked ground %.1f, on a dead world %.1f, at night %.1f (at dusk %.1f), %.0f m off %.1f, on drier ground %.1f; puffs %.0f cm (%.0f for walkers a fifth longer), its colour %.2f bright to the ground's %.2f, opacity %.2f by day and %.2f at night"),
		In.Members, TEXT("0, 0.5, 1, 1.2, 1.5, 2.4, 3.4, 6 and 11"), *Rates, Soaked, Dead, Dark, Dusk, 1.6e5 / 100.0, Far, Drier, Trot.Size, Big.Size, Luma(Trot.Color), Luma(Ground), Trot.Color.A, Night.A));
	TestEqual(TEXT("no dust grazing, standing or walking"), SlowWithDust, 0);
	TestEqual(TEXT("more the faster it goes past a walk"), NotRising, 0);
	TestTrue(TEXT("none on soaked ground, on a dead world, by night or far off"), Soaked == 0.f && Dead == 0.f && Dark == 0.f && Far == 0.f);
	TestTrue(TEXT("fewer at dusk, more on drier ground"), Dusk > 0.f && Dusk < Trot.Rate && Drier > Trot.Rate);
	TestTrue(TEXT("its puffs the walkers' size, its footprint the herd's, drifting as it goes"), Big.Size > Trot.Size && Trot.Radius == 2500.f && Trot.Drift.Equals(In.VelWorldCmS));
	TestTrue(TEXT("paler than the ground, fainter at night"), Luma(Trot.Color) > Luma(Ground) && Trot.Color.A > Night.A && Night.A > 0.f);

	// The pool: two herds raise dust, one stops (its component rests), a third starts before it has rested (a new one),
	// a fourth after (the rested one again); the components all come back.
	SlonkFauna::FDustPool Pool;
	int32 Made = 0;
	auto Make = [&Made]() { ++Made; return NewObject<UNiagaraComponent>(GetTransientPackage()); };
	UNiagaraComponent* A = Pool.Acquire(1, 0.0, Make);
	UNiagaraComponent* B = Pool.Acquire(2, 0.0, Make);
	const bool bSame = Pool.Acquire(1, 0.1, Make) == A && Pool.Find(2) == B;
	UNiagaraComponent* Back = Pool.Release(1, 0.5);
	UNiagaraComponent* C = Pool.Acquire(3, 1.0, Make);
	UNiagaraComponent* Again = Pool.Acquire(4, 0.5 + SlonkFauna::DustPuffLifeSec + 0.1, Make);
	const int32 MadeThen = Made;
	int32 Returned = 0;
	for (const uint64 Id : { 2ull, 3ull, 4ull }) { Returned += Pool.Release(Id, 20.0) ? 1 : 0; }
	AddInfo(FString::Printf(TEXT("the dust's pool: %d components made for 4 herds, %d lent again after resting; %d raising dust after all gave theirs back, %d resting, %d given back by a herd with none"),
		MadeThen, Again == A ? 1 : 0, Pool.NumLive(), Pool.NumResting(), Pool.Release(99, 20.0) ? 1 : 0));
	TestTrue(TEXT("a herd keeps its own"), bSame && A && B && A != B);
	TestTrue(TEXT("one given back is not lent while its puffs fade"), Back == A && C && C != A && C != B);
	TestTrue(TEXT("and is lent again once they have"), Again == A && MadeThen == 3);
	TestTrue(TEXT("every component comes back"), Returned == 3 && Pool.NumLive() == 0 && Pool.NumResting() == MadeThen);
	return true;
}

#endif
