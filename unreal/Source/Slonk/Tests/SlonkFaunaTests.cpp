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
#include "../FishSplash.h"
#include "../GameClock.h"
#include "../HerdDust.h"
#include "../PlanetBody.h"
#include "../PlanetCities.h"
#include "../PlanetFaunaComponent.h"
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

	// The glow is blue-green at strangeness 0, and turns round the wheel with a strange world's sky from its seas' solvent's
	// own (water's that blue-green: Slonk.Fauna.PlanktonSolvent): a water sea's its brightest channel kept, as it always
	// was; an alien sea's no brighter to the eye than its own base, nor its brightest channel past the base's.
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
		const FLinearColor Base = SlonkFauna::PlanktonBase(G.Solvent);
		const bool bWater = Base == SlonkFauna::FFaunaLook().Glow;
		const bool bBright = bWater ? !FMath::IsNearlyEqual(Sea.PlanktonColor.GetMax(), Base.GetMax(), 1e-3f)
			: (SlonkFauna::PlanktonLuma(Sea.PlanktonColor) > SlonkFauna::PlanktonLuma(Base) * 1.001f || Sea.PlanktonColor.GetMax() > Base.GetMax() + 1e-3f);
		TurnWrong += (HueGap(HueOf(Sea.PlanktonColor), FMath::Fmod(HueOf(Base) + Want + 720.f, 360.f)) > 0.5f || bBright) ? 1 : 0;
		if (G.Solvent != ESolvent::Water) { ++AlienSeas; AlienDead += Sea.PlanktonGlow > 0.f ? 0 : 1; }
	}
	AddInfo(FString::Printf(TEXT("400 ocean worlds at strangeness 1: %d glows turned past 30 deg (%d off the sky's turn), %d alien seas (%d lifeless)"), Turned, TurnWrong, AlienSeas, AlienDead));
	TestTrue(TEXT("strange worlds' glows turn"), Turned > 100);
	TestEqual(TEXT("a glow turns from its solvent's by the sky's turn times the strangeness, a water sea's brightest channel kept, an alien sea's no brighter than its base"), TurnWrong, 0);
	TestTrue(TEXT("alien seas are rolled"), AlienSeas > 100);
	TestEqual(TEXT("ammonia, methane and acid seas live as water does"), AlienDead, 0);

	// The sea's milky glow at night seen from a wader's eye (1.7 m) and a ship hovering low (15 m), higher (30 m) and at
	// fauna goto's 40 m over a sea of an Earth's radius. The far sea that perspective squashes into the rows under the
	// horizon until its bloom's 400 m cells are under a pixel tall (at 800 pixels a radian, in the four rows under it, within
	// 0.005 rad), kilometres of it evened into a flat line, lets out a few hundredths of the glow at the most (the surface
	// mirroring the night sky), so it is no bright line over a dark near sea, a water world's or an alien one's. The surface
	// still dims the blooms a little while their cells are 1 to 3 pixels tall (the Fresnel weight in full under 1.26 px, gone
	// by 3.06), and every view in which they are well resolved (cells over about 3 px) lets out all of it, exactly as before:
	// those rows' nearer sea, the wader's sea 10 to 200 m off, the ships' 100 m to a kilometre off, and the sea straight under
	// the wader's eye and out to 45 degrees off it. The terrain material lets it out so (its milky glow only: the foam's
	// stirred glow as before).
	const double Rad = 6.371e8;
	const double CellCm = SlonkScatter::NoisePeriodCm / 26.0;
	// The glow let out at the point of the sea D cm along it from under an eye H cm over it, how many pixels tall the bloom's
	// cells look there, and how far under the horizon it is seen.
	auto SeenAt = [Rad, CellCm](double H, double D, double* OutCellPx = nullptr, double* OutBelow = nullptr)
	{
		const FVector Cam(0.0, 0.0, Rad + H);
		const FVector At(Rad * FMath::Sin(D / Rad), 0.0, Rad * FMath::Cos(D / Rad));
		const double Cos = FVector::DotProduct(At.GetSafeNormal(), (Cam - At).GetSafeNormal());
		const double Dist = FVector::Dist(Cam, At);
		if (OutCellPx) { *OutCellPx = CellCm * FMath::Max(Cos, 0.0) / Dist * 800.0; }
		if (OutBelow) { *OutBelow = -FMath::Asin(FVector::DotProduct((At - Cam).GetSafeNormal(), Cam.GetSafeNormal())) - FMath::Acos(Rad / (Rad + H)); }
		return SlonkFauna::MilkySeenThrough((float)Cos, (float)Dist);
	};
	FString Horizon;
	double FarMost = 0.0;
	int32 NoLine = 0, RampOff = 0, RampSeen = 0;
	for (const double H : { 170.0, 1500.0, 3000.0, 4000.0 })
	{
		const double HorizonCm = FMath::Sqrt(2.0 * Rad * H + H * H);
		double Sum = 0.0, Nearest = HorizonCm, Resolved = 0.0;
		int32 N = 0;
		for (int32 i = 0; i <= 4000; ++i)
		{
			const double D = FMath::Lerp(5.0e3, HorizonCm * 0.999, i / 4000.0);
			double CellPx = 0.0, Below = 0.0;
			const float Out = SeenAt(H, D, &CellPx, &Below);
			// Anywhere from 50 m out to the horizon, wherever the blooms' cells are well resolved (over about 3 px), all of it.
			if (CellPx >= 3.6 * 0.85) { RampOff += Out == 1.f ? 0 : 1; ++RampSeen; }
			if (Below > 0.005) { continue; }
			// Nearer, in those rows: the blooms' cells are a pixel tall or more, dimmed a little while under about 3 px.
			if (CellPx >= 1.0) { Resolved = FMath::Max(Resolved, D); continue; }
			Nearest = FMath::Min(Nearest, D);
			Sum += Out;
			++N;
			FarMost = FMath::Max(FarMost, (double)Out);
		}
		NoLine += N > 0 ? 0 : 1;
		Horizon += FString::Printf(TEXT("%s%.1f m up: the sea under the horizon with its cells under a pixel (%.2f to %.1f km off) lets out %.4f of it on average%s"), Horizon.IsEmpty() ? TEXT("") : TEXT("; "), H / 100.0,
			Nearest / 1e5, HorizonCm / 1e5, Sum / FMath::Max(N, 1), Resolved > 0.0 ? *FString::Printf(TEXT(" (the four rows' nearer sea, out to %.2f km, has cells a pixel tall or more, partly dimmed while under about 3 px)"), Resolved / 1e5) : TEXT(""));
	}
	double NearLeast = 1.0;
	int32 NearOff = 0, Near = 0;
	auto NearView = [&](double H, double D) { const float Out = SeenAt(H, D); NearLeast = FMath::Min(NearLeast, (double)Out); NearOff += Out == 1.f ? 0 : 1; ++Near; };
	for (double D = 1.0e3; D <= 2.0e4 + 1.0; D += 100.0) { NearView(170.0, D); }
	for (const double H : { 1500.0, 3000.0, 4000.0 }) { for (double D = 1.0e4; D <= 1.0e5 + 1.0; D += 1000.0) { NearView(H, D); } }
	for (const double Deg : { 0.0, 15.0, 30.0, 45.0 }) { NearView(170.0, 170.0 * FMath::Tan(FMath::DegreesToRadians(Deg))); }
	FString Terrain;
	FFileHelper::LoadFileToString(Terrain, *FPaths::Combine(FPaths::ProjectDir(), TEXT("../tools/planet_materials/terrain_material.py")));
	const bool bLetOut = Terrain.Contains(TEXT("float cosV = saturate(dot(Nn, normalize(Cam - WP)));"))
		&& Terrain.Contains(TEXT("float bloomSeen = saturate(2.0 * ((NoisePeriodCm / 26.0) * cosV / (dist * 0.0045) - 0.35));"))
		&& Terrain.Contains(TEXT("float milkyOut = 1.0 - (1.0 - bloomSeen) * pow(1.0 - cosV, 5.0);")) && Terrain.Contains(TEXT("plankton = PlanktonGlow * (stirred + milky * milkyOut) * water"));
	AddInfo(FString::Printf(TEXT("the milky glow seen through the sea's surface: %s; the most any of it lets out %.4f; %d points from 50 m to the horizon with cells over 3.06 px, %d letting out less than all of it; %d near views (a wader's sea 10 to 200 m off, the ships' 100 m to 1 km, straight under to 45 degrees off it), %d letting out less than all of it, the least %.4f; terrain_material.py lets it out so: %s"),
		*Horizon, FarMost, RampSeen, RampOff, Near, NearOff, NearLeast, bLetOut ? TEXT("yes") : TEXT("no")));
	TestTrue(TEXT("the far sea seen edge-on, its blooms under a pixel, lets out a few hundredths of its milky glow at the most: no bright line at the horizon"), FarMost < 0.05 && NoLine == 0);
	TestTrue(TEXT("every view in which the blooms are well resolved lets out all of it, as it always did"), NearOff == 0 && NearLeast >= 0.99 && RampOff == 0);
	TestTrue(TEXT("the terrain material lets the milky glow out so, the stirred glow as before"), bLetOut);
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
					for (int32 K = 0; K < SlonkFauna::NumKinds; ++K) { EarthOff += A.Species[K].Num == 1 && A.PlanOf((EFaunaKind)K) == SlonkFauna::EarthPlan((EFaunaKind)K) ? 0 : 1; }
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
	// room for nearer ones, and never more than the caps are about. The herds about with them (a kind of their own, with its
	// own reach, fade and caps) are none of this: only the flocks are counted.
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
		for (const SlonkFauna::FFaunaField::FGroupRef& C : F.GetCandidates()) { N += C->Kind == EFaunaKind::Flier && FVector::Dist(G.Centre, C->Centre) * R < D.ReachCm ? 1 : 0; }
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
			if (!G || (*G)->Kind != EFaunaKind::Flier || DistOf(**G) > D.ReachCm * SlonkFauna::SpawnHysteresis) { continue; }
			++MadeRoom;
			SeenGo += DistOf(**G) - (*G)->ExtentCm() > D.FadeCm ? 0 : 1;
		}
		bool bFarAbout = false;
		for (const TPair<uint64, SlonkFauna::FFaunaField::FGroupRef>& A : F.GetActive()) { bFarAbout |= A.Value->Kind == EFaunaKind::Flier && DistOf(*A.Value) > D.ReachCm; }
		for (const SlonkFauna::FFaunaField::FGroupRef& G : Spawned)
		{
			if (G->Kind != EFaunaKind::Flier) { continue; }
			++Spawns;
			const bool bInSight = DistOf(*G) - G->ExtentCm() < D.FadeCm;
			InSight += s > 0 && bInSight ? 1 : 0;
			if (bInSight && bFarAbout && Bad++ == 0) { FirstBad = s; }
		}
		int32 Waiting = 0;
		for (const SlonkFauna::FFaunaField::FGroupRef& C : F.GetCandidates()) { Waiting += C->Kind == EFaunaKind::Flier && !F.GetActive().Contains(C->Id) && DistOf(*C) < D.ReachCm ? 1 : 0; }
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
	 *  branches), its mote branch, its walker branch, its leaper branch and its floater branch; and of its CODE, its glow
	 *  organs' block. */
	enum class EMotionBlock : uint8 { Fliers, Motes, Walkers, Leapers, Floaters, Glow };
	/** fauna_material.py's WPO_CODE (or with bPixel its CODE), its lines trimmed, the blank ones and the comments left out:
	 *  the fliers' motion, which SlonkFauna::FlierMotion copies; its mote branch (from its test of Motes to the brace that
	 *  closes it), whose turn to the camera Slonk.Fauna.MoteCloud copies; its walker branch (from its test of Walkers to
	 *  the brace that closes it), which SlonkFauna::WalkerMotion copies; its leaper branch, which SlonkFauna::LeaperMotion
	 *  copies (and in CODE, LeaperShown and LeaperTurn); or its floater branch, which SlonkFauna::FloaterMotion copies (and
	 *  in CODE, FloaterCover and FloaterLight). Each leaves the others out. Empty if the script or the block is not found. */
	FString MaterialMotionBlock(EMotionBlock Which = EMotionBlock::Fliers, bool bPixel = false)
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
			if (!bIn) { bIn = L.StartsWith(bPixel ? TEXT("CODE = r\"\"\"") : TEXT("WPO_CODE = r\"\"\"")); continue; }
			if (L.StartsWith(TEXT("\"\"\""))) { return Block; }
			if (L.IsEmpty() || L.StartsWith(TEXT("//"))) { continue; }
			if (In == EMotionBlock::Fliers)
			{
				if (L.StartsWith(TEXT("[branch] if (Motes"))) { In = EMotionBlock::Motes; Depth = 0; }
				else if (L.StartsWith(TEXT("[branch] if (Walkers"))) { In = EMotionBlock::Walkers; Depth = 0; }
				else if (L.StartsWith(TEXT("[branch] if (Leapers"))) { In = EMotionBlock::Leapers; Depth = 0; }
				else if (L.StartsWith(TEXT("[branch] if (Floaters"))) { In = EMotionBlock::Floaters; Depth = 0; }
				else if (L.StartsWith(TEXT("[branch] if (GlowSpots"))) { In = EMotionBlock::Glow; Depth = 0; }
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
	/** The fliers' part's CRC (FCrc::StrCrc32) when SlonkFauna::FlierMotion was last brought in line with it (the alien
	 *  plans' wings taken in, a four-winged flock's darts its own, every member together; a bird's WingForm 0 the motion it
	 *  was: Slonk.Fauna.EarthGolden holds the copy's bird to the bit as it was). */
	constexpr uint32 MaterialMotionBlockCrc = 0x6423d356u;
	/** The mote branch's CRC when Slonk.Fauna.MoteCloud's copy of its turn to the camera was last brought in line with it. */
	constexpr uint32 MaterialMoteBlockCrc = 0xa89449edu;
	/** The walker branch's CRC when SlonkFauna::WalkerMotion was last brought in line with it (its plan's rig taken in, the
	 *  Earth grazer's the branch's defaults: Slonk.Fauna.EarthGolden holds the copy's Earth motion to the bit as it was). */
	constexpr uint32 MaterialWalkerBlockCrc = 0xbe9591e8u;
	/** The leaper branch's CRCs, in WPO_CODE and in CODE, when SlonkFauna::LeaperMotion, LeaperShown and LeaperTurn were
	 *  last brought in line with them (the alien fish's forms and the glow organs taken in, their light dimmed by the speck
	 *  floor's growth as SlonkFauna::GlowSpeckDim has it; an Earth fish's FishForm 0 the motion and the reading of its leap
	 *  they were: Slonk.Fauna.EarthGolden holds the copies to the bit as they were). */
	constexpr uint32 MaterialLeaperBlockCrc = 0x1ccf1728u;
	constexpr uint32 MaterialLeaperPixelBlockCrc = 0x8ab6e9bdu;
	/** The floater branch's CRCs, in WPO_CODE and in CODE, when SlonkFauna::FloaterMotion, FloaterCover and FloaterLight were
	 *  last brought in line with them. */
	constexpr uint32 MaterialFloaterBlockCrc = 0x35f5c6a5u;
	constexpr uint32 MaterialFloaterPixelBlockCrc = 0x0cc1e980u;
	/** The glow organs' block's CRC, in CODE, when SlonkFauna::GlowSpotAt and GlowBreath were last brought in line with it. */
	constexpr uint32 MaterialGlowBlockCrc = 0xa42f569du;
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
	TestFalse(TEXT("nothing built for a plan without wings, legs, fins or tentacles (a mote's)"), SlonkFaunaMeshes::BuildLod(SlonkFauna::EarthPlan(EFaunaKind::Mote), 0, Nothing));

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
	TestTrue(TEXT("no mesh for a plan with nothing built"), SlonkFaunaMeshes::GetMesh(SlonkFauna::EarthPlan(EFaunaKind::Mote), EFaunaKind::Mote) == nullptr);
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

	/**
	 * The Earth-like world "spawnplanet terrestrial seed 1" makes from the game's start (the ship's own place puts it at
	 * the location below; seed 1 moves the place its seed is drawn from on by 1e9 cm along x), as APlanetBody's
	 * ResolveStyleFromArchetype and MakeTerrainParams make it at strangeness 0: its seed from that place, its style's sea
	 * level and polar caps from the first and sixth draws of the stream that place's hash seeds, a temperate world's
	 * relief, ranges, canyons and moisture, and its style's light cratering. Its lowlands read wet, from 0.6 to the top
	 * of the range, nearly everywhere there is level land: the world the herds once found no grass on.
	 */
	FPlanetTerrainParams SpawnedEarthParams()
	{
		const FVector L = FVector(-2125199545.789, -1406367466.411, 0.0) + FVector(1.0e9, 0.0, 0.0);
		const uint32 Hash = HashCombine(HashCombine(GetTypeHash(FMath::RoundToInt64(L.X / 1000.0)), GetTypeHash(FMath::RoundToInt64(L.Y / 1000.0))), GetTypeHash(FMath::RoundToInt64(L.Z / 1000.0)));
		FRandomStream Rng(static_cast<int32>(Hash));
		const float SeaLevel = Rng.FRandRange(0.45f, 0.6f);
		for (int32 i = 0; i < 4; ++i) { Rng.FRandRange(0.f, 1.f); }
		const float IceCapStart = Rng.FRandRange(0.72f, 0.8f);
		FPlanetTerrainParams P = FaunaEarthParams(6371.0);
		P.Seed = FVector(FMath::Frac(L.X / 7.3e9) * 100.0, FMath::Frac(L.Y / 5.1e9) * 100.0, FMath::Frac(L.Z / 3.7e9) * 100.0);
		P.SeaLevel = SeaLevel;
		P.IceCapStart = IceCapStart;
		P.Craters = 0.1f;
		P.Canyons = 0.5f;
		return P;
	}

	/** That world's herds alone, full of life. */
	SlonkFauna::FFaunaWorld SpawnedEarthHerds()
	{
		SlonkFauna::FFaunaWorld W = EarthHerds();
		W.P = SpawnedEarthParams();
		return W;
	}

	/** A world's greenest lowland, as "terrain goto green" finds it with no sun asked for and nothing near: the wettest,
	 *  levelest, lowest land of 6000 places spread evenly over the globe, the field read as coarse as they are apart
	 *  (UPlanetTerrainComponent's coarse pass). */
	FVector GreenestLowland(const FPlanetTerrainParams& P)
	{
		const int32 N = 6000;
		const double Golden = UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0));
		const double CoarseWavelength = 2.0 * UE_DOUBLE_PI * P.RadiusCm / FMath::Sqrt((double)N * UE_DOUBLE_PI);
		double BestScore = -1e30;
		FVector Best = FVector::ZAxisVector;
		for (int32 i = 0; i < N; ++i)
		{
			const double Z = 1.0 - 2.0 * (i + 0.5) / N, Rr = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
			const FVector Dir(Rr * FMath::Cos(Golden * i), Rr * FMath::Sin(Golden * i), Z);
			const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir, CoarseWavelength);
			const double Score = S.bWater ? -1e9 : S.Wet * 6.0 - S.Slope * 4.0 - S.Rock * 2.0 - S.Ice * 6.0 - S.HeightCm / FMath::Max(P.ReliefCm, 1.0) * 6.0 - SlonkTerrain::ClimateColdness(P, Dir) * 2.0;
			if (Score > BestScore) { BestScore = Score; Best = Dir; }
		}
		return Best;
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

	/** A herd's loop at theta as its cell lays it (FFaunaGroup: in the plane tangent at its middle), a planet-local unit
	 *  direction. */
	FVector HerdLoopPoint(const SlonkFauna::FFaunaGroup& G, double Th)
	{
		const double Rr = G.RadiusCm * (1.0 + G.Lobe * FMath::Cos(G.Lobes * Th + G.LobePhase)) + G.MeanderCm * FMath::Sin(G.MeanderK * Th + 1.7 * G.LobePhase);
		return (G.East * FMath::Cos(Th) + G.North * FMath::Sin(Th)) * Rr;
	}
	FVector HerdLoopDir(const SlonkFauna::FFaunaGroup& G, double RadiusCm, double Th)
	{
		return (G.Centre * RadiusCm + HerdLoopPoint(G, Th)).GetSafeNormal();
	}
	/** Off across its loop at theta (along the loop's normal in its plane, outward positive), a planet-local unit
	 *  direction: where its walkers stand either side of the loop's line, out to its radius. */
	FVector HerdFootDir(const SlonkFauna::FFaunaGroup& G, double RadiusCm, double Th, double Off)
	{
		const FVector Across = FVector::CrossProduct(HerdLoopPoint(G, Th + 1.0e-3) - HerdLoopPoint(G, Th - 1.0e-3), G.Centre).GetSafeNormal();
		return (G.Centre * RadiusCm + HerdLoopPoint(G, Th) + Across * Off).GetSafeNormal();
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
	 *  planet-local: its instance unturned in its herd's frame, scaled by its length; its plan's rig as its herd's material
	 *  instance gives it (the Earth grazer's by default). */
	FVector WalkerVertexAt(const SlonkFauna::FHerdState& S, const FTransform& Xf, const float* C, const float* Gd, const FVector3f& LP, const FLinearColor& VC, float T, float DistCm = 3000.f,
		const SlonkFauna::Walker::FRig& Rig = SlonkFauna::Walker::FRig())
	{
		const float Size = C[SlonkFauna::Custom::SizeM];
		const FVector3f LPP = (FVector3f)Xf.GetTranslation() + LP * Size;
		const FVector3f Off = SlonkFauna::WalkerMotion(LP, LPP, VC, T, C, Gd, DistCm, 960.f, Rig);
		return S.FramePos + S.FrameRot.RotateVector(FVector(LPP + Off * Size));
	}

	/** The feet a test follows of a herd's walkers, in the unit mesh, and the colours their vertices carry: the Earth
	 *  grazer's four hooves (WalkerHooves, as the material is given them), else each of its form's feet as its mesh is built,
	 *  its colour the mesh's own at the foot (so a foot built off the colour contract shows). */
	void WalkerFeetOf(const FFaunaBodyPlan& Plan, TArray<FVector3f>& OutAt, TArray<FLinearColor>& OutColour)
	{
		OutAt.Reset();
		OutColour.Reset();
		const SlonkFauna::Walker::FShape Shape = SlonkFauna::Walker::ShapeOf(Plan);
		if (Shape.Form == SlonkFauna::Walker::EForm::Grazer)
		{
			for (int32 f = 0; f < 4; ++f) { OutAt.Add(WalkerHooves[f]); OutColour.Add(FLinearColor(SlonkFauna::Vc::SurfaceBeak, 1.f, WalkerHoofPhase[f], SlonkFauna::Vc::JointLeg)); }
			return;
		}
		UE::Geometry::FDynamicMesh3 M;
		SlonkFaunaMeshes::BuildLod(Plan, 0, M);
		const UE::Geometry::FDynamicMeshColorOverlay* C = M.Attributes()->PrimaryColors();
		for (int32 f = 0; f < Shape.NumLegs; ++f)
		{
			const FVector3f Foot = Shape.Legs[f].Foot;
			// The leg vertex nearest the foot's place: its cap's.
			double Best = TNumericLimits<double>::Max();
			FLinearColor Col(0.f, 0.f, 0.f, 0.f);
			for (const int32 El : C->ElementIndicesItr())
			{
				const FVector4f Cv = C->GetElement(El);
				if (!FMath::IsNearlyEqual(Cv.W, SlonkFauna::Vc::JointLeg)) { continue; }
				const double D = FVector::Dist((FVector)M.GetVertex(C->GetParentVertex(El)), (FVector)Foot);
				if (D < Best) { Best = D; Col = FLinearColor(Cv.X, Cv.Y, Cv.Z, Cv.W); }
			}
			OutAt.Add(Foot);
			OutColour.Add(Col);
		}
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
		double WorstSlip = 0.0, SumSlip = 0.0, WorstSlipAt = 0.0;
		int32 Slips = 0, Slides = 0;
		/** The worst slip of each foot followed (by its place in its walker's feet), the herd's share on the move and its
		 *  way's turning (radians a metre) at the worst slip of all. */
		double WorstByFoot[6] = {};
		double TravelAtWorst = 0.0, KapAtWorst = 0.0;
		/** The most any walker's yaw turned between two steps, radians, and the least and most of the herd on the move; how
		 *  sharply its way turned at most as it walked (radians a metre) and the most that changed between two steps. */
		double WorstYawStep = 0.0, MinTravel = 1.0, MaxTravel = 0.0, MaxKap = 0.0, MaxKapStep = 0.0;
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
		// Its walkers' feet (four hooves of an Earth grazer, six feet of a strange world's walker) and its plan's rig.
		TArray<FVector3f> FeetAt;
		TArray<FLinearColor> FeetColour;
		WalkerFeetOf(G.Plan, FeetAt, FeetColour);
		const int32 NumFeet = FeetAt.Num();
		const SlonkFauna::Walker::FRig Rig = SlonkFauna::Walker::ShapeOf(G.Plan).Rig;
		TArray<FVector> Last;
		TArray<uint8> WasPlanted;
		TArray<float> LastYaw;
		Last.SetNum(Xf.Num() * NumFeet);
		WasPlanted.Init(0, Xf.Num() * NumFeet);
		LastYaw.Init(0.f, Xf.Num());
		float Frozen = -1.f;
		double LastKap = 0.0;
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
			Out.MaxKap = FMath::Max(Out.MaxKap, FMath::Abs(S.WalkKap));
			if (k > 0) { Out.MaxKapStep = FMath::Max(Out.MaxKapStep, FMath::Abs(S.WalkKap - LastKap)); }
			LastKap = S.WalkKap;
			for (int32 m = 0; m < Xf.Num(); ++m)
			{
				const float* C = Cu.GetData() + m * SlonkFauna::Custom::Num;
				const float Yaw = SlonkFauna::WalkerYaw(T, C, Gd);
				if (k > 0) { Out.WorstYawStep = FMath::Max(Out.WorstYawStep, (double)FMath::Abs(FMath::UnwindRadians(Yaw - LastYaw[m]))); }
				LastYaw[m] = Yaw;
				if (m % FMath::Max(MemberStride, 1) != 0) { continue; }
				const float Strides = SlonkFauna::WalkerStrides(T, C, Gd);
				const float Speed = SlonkFauna::WalkerSpeedMS(T, C, Gd);
				for (int32 f = 0; f < NumFeet; ++f)
				{
					const FLinearColor& VC = FeetColour[f];
					const FVector P = WalkerVertexAt(S, Xf[m], C, Gd, FeetAt[f], VC, T, 3000.f, Rig);
					const FVector Up = P.GetSafeNormal();
					const double Off = FMath::Abs(P.Size() - R - HeightAt(Up));
					const bool bPlanted = FMath::Frac(Strides - VC.B) < SlonkFauna::Walker::Duty;
					++Out.Feet;
					Out.WorstFootCm = FMath::Max(Out.WorstFootCm, Off);
					if (bPlanted)
					{
						++Out.Planted;
						Out.WorstPlantedCm = FMath::Max(Out.WorstPlantedCm, Off);
						Out.SumPlantedCm += Off;
					}
					const int32 I = m * NumFeet + f;
					if (k > 0 && bPlanted && WasPlanted[I])
					{
						FVector D = P - Last[I];
						D -= Up * FVector::DotProduct(D, Up);
						const double Slip = D.Size() * Hz;
						++Out.Slips;
						Out.SumSlip += Slip;
						if (Slip > Out.WorstSlip) { Out.WorstSlip = Slip; Out.WorstSlipAt = Now - Start; Out.TravelAtWorst = S.Travel; Out.KapAtWorst = S.WalkKap; }
						Out.Slides += Slip > 10.0 ? 1 : 0;
						if (f < 6) { Out.WorstByFoot[f] = FMath::Max(Out.WorstByFoot[f], Slip); }
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
	// On an Earth-like world the herds graze open, level grass, a good share of it: one herd for every three to eight cells
	// whose middle is the grass a herd likes best, however wet (lush grass reads wet to the top of its range); and as many
	// on the greenest lowland of the world "spawnplanet terrestrial seed 1" makes, all of it level and wetter than 0.6,
	// where the herds once found no grass at all (its ground there as the debug's "fauna habitat" reads it, below). Along
	// each herd's loop (read here again at 480 places evenly round it, as its cell reads it, and at 240 through its hour)
	// it crosses ground off its grass (a little rougher than its cell's read allows: the ground of 240 places it never
	// read) at no more than about one place in seven, in stretches no longer than about a twelfth of its loop; and nowhere
	// its walkers stand, on its loop's line or across it out to its radius either side (a third, two thirds and all of it
	// at each of the 720 places), does it set foot on water, ice, a cliff, steep ground or a dense wood; no water within
	// 90 m of its loop either side (read here 30 m apart across it), the room it leaves the crew as the habitat read it; 15
	// to 60 walkers of 1.45 to 2.6 m. None on a world with no life or no air, at density 0, or with the herds off; and on a
	// world with towns, none on a town's ground or a road: at none of 480 places round its loop within its radius and
	// 490 m of a town's ground nor its radius and 49 m of a road (its cell's read keeps 500 m and 50 m at every fifth). The
	// search the debug's goto makes finds the nearest herd there is, reading every cell out to it.
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Herd);
	// What a world's herds stand on, read again here.
	struct FHerdsRead
	{
		int32 Wrong = 0, Hard = 0, Watery = 0, Wooded = 0, Crossing = 0, LongStretch = 0, Members = 0, BadMembers = 0, WaterNear = 0, NarrowRoom = 0, LongestRun = 0, Places = 0;
		double MinFlat = 1.0, MaxForest = 0.0, MinWet = 1.0, MaxWet = 0.0, MinLoop = 1e12, MaxLoop = 0.0, MostCrossed = 0.0, MinDry = 1e12, SumWet = 0.0;
	};
	auto ReadHerds = [&D](const SlonkFauna::FFaunaWorld& Wd, const TArray<SlonkFauna::FFaunaGroup>& Found)
	{
		FHerdsRead Out;
		const double Rad = Wd.P.RadiusCm;
		auto Tolerant = [](const SlonkScatter::FHabitat& H) { return !H.bWater && H.Ice <= 0.25 && H.Alpine <= 0.75 && H.Cliff <= 0.4 && H.Flat > 0.38 && H.Forest < 0.65; };
		auto Note = [&Out](const SlonkScatter::FHabitat& H)
		{
			++Out.Places;
			Out.Watery += H.bWater ? 1 : 0;
			Out.Wooded += H.Forest >= 0.65 ? 1 : 0;
			Out.MinFlat = FMath::Min(Out.MinFlat, H.Flat);
			Out.MaxForest = FMath::Max(Out.MaxForest, H.Forest);
			if (!H.bWater) { Out.MinWet = FMath::Min(Out.MinWet, H.Wet); Out.MaxWet = FMath::Max(Out.MaxWet, H.Wet); }
		};
		for (const SlonkFauna::FFaunaGroup& G : Found)
		{
			Out.Members += G.Members;
			Out.MinLoop = FMath::Min(Out.MinLoop, G.LengthCm);
			Out.MaxLoop = FMath::Max(Out.MaxLoop, G.LengthCm);
			bool bOk = true;
			// Where its walkers stand about a place on its loop's line: across it a sixth, a half, five sixths and all of its radius
			// either side.
			auto Across = [&](TFunctionRef<FVector(double)> At)
			{
				// Between the habitat read's own offsets (a third, two thirds, all of the radius), out to the full radius, so it
				// reads ground the cell never read.
				for (const double Share : { 1.0 / 6.0, -1.0 / 6.0, 0.5, -0.5, 5.0 / 6.0, -5.0 / 6.0, 1.0, -1.0 })
				{
					const SlonkScatter::FHabitat H = SlonkScatter::HabitatAt(Wd.P, At(Share * G.HerdRadiusCm));
					Note(H);
					bOk &= Tolerant(H);
				}
			};
			// Where it stands through its hour, across the way along its loop there.
			for (int32 k = 0; k < 240; ++k)
			{
				const SlonkFauna::FFaunaPose Pose = SlonkFauna::PathAt(G, Rad, 3600.0 * k / 240.0);
				const FVector Up = Pose.PosCm.GetSafeNormal();
				const SlonkScatter::FHabitat H = SlonkScatter::HabitatAt(Wd.P, Up);
				Note(H);
				bOk &= Tolerant(H);
				const FVector Side = FVector::CrossProduct(Pose.Ahead, Up).GetSafeNormal();
				Across([&](double Off) { return (Up * Rad + Side * Off).GetSafeNormal(); });
			}
			// Evenly round its loop: its grass, and its walkers either side.
			bool bGrass[480];
			int32 Off = 0;
			for (int32 k = 0; k < 480; ++k)
			{
				// Midway between the habitat read's own 480 points, which it has already gated.
				const double Th = UE_DOUBLE_TWO_PI * (k + 0.5) / 480.0;
				const SlonkScatter::FHabitat H = SlonkScatter::HabitatAt(Wd.P, HerdLoopDir(G, Rad, Th));
				Note(H);
				bOk &= Tolerant(H);
				Across([&](double Off2) { return HerdFootDir(G, Rad, Th, Off2); });
				bGrass[k] = !H.bWater && H.Ice <= 0.2 && H.Alpine <= 0.7 && H.Flat > 0.62 && H.Wet >= 0.12 && H.Forest < 0.5;
				Off += bGrass[k] ? 0 : 1;
			}
			int32 Longest = 0;
			for (int32 k = 0, Run = 0; k < 2 * 480; ++k)
			{
				Run = bGrass[k % 480] ? 0 : Run + 1;
				Longest = FMath::Max(Longest, FMath::Min(Run, 480));
			}
			Out.Hard += bOk ? 0 : 1;
			Out.MostCrossed = FMath::Max(Out.MostCrossed, Off / 480.0);
			Out.LongestRun = FMath::Max(Out.LongestRun, Longest);
			Out.Crossing += Off > 480 / 7 ? 1 : 0;
			Out.LongStretch += Longest > 480 / 12 ? 1 : 0;
			const SlonkFauna::FHerdHabitatRead Read = SlonkFauna::HerdHabitatAt(Wd, G);
			bOk &= Read.bSuits;
			Out.SumWet += Read.MeanWet;
			// Water within 90 m of its loop, either side, read 30 m apart across it at each of the 480 places.
			for (int32 k = 0; k < 480; ++k)
			{
				for (const double Off2 : { -9000.0, -6000.0, -3000.0, 3000.0, 6000.0, 9000.0 })
				{
					Out.WaterNear += SlonkScatter::HabitatAt(Wd.P, HerdFootDir(G, Rad, UE_DOUBLE_TWO_PI * k / 480.0, Off2)).bWater ? 1 : 0;
				}
			}
			Out.MinDry = FMath::Min(Out.MinDry, G.DryCm);
			Out.NarrowRoom += G.DryCm == Read.DryCm && G.DryCm >= SlonkFauna::HerdWaterMarginCm && SlonkFauna::CrewRoomCm(G) <= 3.0e4 ? 0 : 1;
			Out.Wrong += bOk ? 0 : 1;
			Out.BadMembers += G.Members >= D.GroupMin && G.Members <= D.GroupMax && G.SpanM >= 1.6f && G.SpanM <= 2.3f ? 0 : 1;
		}
		return Out;
	};
	// A world's herds against its grass, and what they stand on.
	auto Report = [this](const TCHAR* World, int32 NumCells, int32 Middles, const TArray<SlonkFauna::FFaunaGroup>& Found, const FHerdsRead& Rd)
	{
		const double Per = (double)Middles / FMath::Max(Found.Num(), 1);
		AddInfo(FString::Printf(TEXT("%d cells of %s, %d of them with the grass a herd likes best at their middles: %d herds (one for every %.1f of those cells; %d walkers), loops %.1f to %.1f km, their grass wet %.2f on average; where their walkers stand (%d places) flat from %.2f, wet %.2f to %.2f, wood at most %.2f; %d herds setting foot where they never should (%d places in water, %d in a dense wood), crossing ground off their grass at %.0f%% of their loops at most (%d over a seventh), in stretches of %d places of 480 at most (%d longer than a twelfth); %d points of water within 90 m of a loop, its loop %.0f m from the nearest at the least"),
			NumCells, World, Middles, Found.Num(), Per, Rd.Members, Rd.MinLoop / 1e5, Rd.MaxLoop / 1e5, Rd.SumWet / FMath::Max(Found.Num(), 1), Rd.Places, Rd.MinFlat, Rd.MinWet, Rd.MaxWet, Rd.MaxForest, Rd.Hard,
			Rd.Watery, Rd.Wooded, Rd.MostCrossed * 100.0, Rd.Crossing, Rd.LongestRun, Rd.LongStretch, Rd.WaterNear, Rd.MinDry / 100.0));
		TestTrue(FString::Printf(TEXT("herds on %s"), World), Found.Num() >= 60);
		TestTrue(FString::Printf(TEXT("one herd for every three to eight cells whose middle is its grass, on %s"), World), Per >= 3.0 && Per <= 8.0);
		TestEqual(TEXT("every herd's loop on its grass"), Rd.Wrong, 0);
		TestEqual(TEXT("none setting foot in the water, a wood, on a cliff or steep ground, anywhere its walkers stand"), Rd.Hard + Rd.Watery + Rd.Wooded, 0);
		TestEqual(TEXT("none crossing much ground off its grass"), Rd.Crossing, 0);
		TestEqual(TEXT("nor any long stretch of it"), Rd.LongStretch, 0);
		TestEqual(TEXT("no water within 90 m of a herd's loop"), Rd.WaterNear, 0);
		TestEqual(TEXT("the room it leaves the crew as its habitat read it"), Rd.NarrowRoom, 0);
		TestEqual(TEXT("every herd 15 to 60 grazers' length"), Rd.BadMembers, 0);
		TestTrue(TEXT("loops of about 2 km"), Rd.MinLoop > 1.0e5 && Rd.MaxLoop < 3.2e5);
	};
	const SlonkFauna::FFaunaWorld W = EarthHerds();
	const double R = W.P.RadiusCm;
	TArray<SlonkFauna::FFaunaGroup> Herds;
	int32 Cells = 0;
	HerdsOverGlobe(W, 30000, Herds, &Cells);
	// The cells whose middle is the grass a herd likes best.
	int32 GrassMiddles = 0;
	{
		TSet<uint64> Seen;
		const double Wd = 2.0 / (double)SlonkFauna::CellsAcross(W.P, EFaunaKind::Herd);
		for (int32 i = 0; i < 30000; ++i)
		{
			const SlonkFauna::FFaunaCell C = SlonkFauna::CellOf(W.P, EFaunaKind::Herd, Spread(i, 30000));
			bool bAlready = false;
			Seen.Add(C.Id(), &bAlready);
			if (bAlready) { continue; }
			const FVector Mid = SlonkTerrain::CubeDir(C.Face, -1.0 + Wd * (C.X + 0.5), -1.0 + Wd * (C.Y + 0.5)).GetSafeNormal();
			GrassMiddles += SlonkFauna::HerdGrass(SlonkScatter::HabitatAt(W.P, Mid), true) ? 1 : 0;
		}
	}
	Report(TEXT("an Earth-like world"), Cells, GrassMiddles, Herds, ReadHerds(W, Herds));

	// The world "spawnplanet terrestrial seed 1" makes, every cell within 60 km of its greenest lowland (as "terrain goto
	// green" finds it): its ground at their middles as the debug's "fauna habitat" reads it (how much is water; of the
	// land, how much is the herds' grass and their strict grass, what keeps the rest from it, and its level, wet and wooded
	// by fifths), how much of it the grass a herd likes best is and how much it was as the herds had it with their grass
	// no wetter than 0.6; and its herds.
	{
		const SlonkFauna::FFaunaWorld Lush = SpawnedEarthHerds();
		const FVector Green = GreenestLowland(Lush.P);
		TArray<SlonkFauna::FFaunaCell> Near;
		SlonkFauna::CellsNear(Lush.P, EFaunaKind::Herd, Green, 6.0e6, Near);
		const double Wd = 2.0 / (double)SlonkFauna::CellsAcross(Lush.P, EFaunaKind::Herd);
		TArray<SlonkFauna::FFaunaGroup> LushHerds;
		int32 Water = 0, Land = 0, Grass = 0, Strict = 0, NotFlat = 0, TooDry = 0, Wetter = 0, Wooded = 0, Iced = 0, Once = 0;
		int32 FlatBins[5] = {}, WetBins[5] = {}, WoodBins[5] = {};
		for (const SlonkFauna::FFaunaCell& C : Near)
		{
			SlonkFauna::FFaunaGroup G;
			if (SlonkFauna::GroupInCell(Lush, C, G)) { LushHerds.Add(MoveTemp(G)); }
			const FVector Mid = SlonkTerrain::CubeDir(C.Face, -1.0 + Wd * (C.X + 0.5), -1.0 + Wd * (C.Y + 0.5)).GetSafeNormal();
			const SlonkScatter::FHabitat H = SlonkScatter::HabitatAt(Lush.P, Mid);
			if (H.bWater) { ++Water; continue; }
			++Land;
			Grass += SlonkFauna::HerdGrass(H, false) ? 1 : 0;
			const bool bStrict = SlonkFauna::HerdGrass(H, true);
			Strict += bStrict ? 1 : 0;
			Once += bStrict && H.Wet <= 0.6 ? 1 : 0;
			NotFlat += H.Flat <= 0.85 ? 1 : 0;
			TooDry += H.Wet < 0.2 ? 1 : 0;
			Wetter += H.Wet > 0.6 ? 1 : 0;
			Wooded += H.Forest >= 0.3 ? 1 : 0;
			Iced += (H.Ice > 0.2 || H.Alpine > 0.7) ? 1 : 0;
			++FlatBins[FMath::Clamp((int32)(H.Flat * 5.0), 0, 4)];
			++WetBins[FMath::Clamp((int32)(H.Wet * 5.0), 0, 4)];
			++WoodBins[FMath::Clamp((int32)(H.Forest * 5.0), 0, 4)];
		}
		auto Pct = [Land](int32 V) { return Land > 0 ? 100.0 * V / Land : 0.0; };
		auto Bins = [&](const int32* B) { return FString::Printf(TEXT("%.0f/%.0f/%.0f/%.0f/%.0f%%"), Pct(B[0]), Pct(B[1]), Pct(B[2]), Pct(B[3]), Pct(B[4])); };
		AddInfo(FString::Printf(TEXT("the habitat within 60 km of the greenest lowland of the world spawnplanet terrestrial seed 1 makes (%s), at %d cells' middles: %.0f%% water; of the land, %.1f%% herd grass, %.1f%% strict (a herd's middle); not level %.0f%%, too dry %.0f%%, wetter than 0.6 %.0f%%, wooded %.0f%%, ice or alpine %.0f%%; level by fifths %s, wet %s, wooded %s; %d cells' middles the grass a herd likes best, %d of them with the herds' grass no wetter than 0.6"),
			*Green.ToString(), Near.Num(), 100.0 * Water / FMath::Max(Near.Num(), 1), Pct(Grass), Pct(Strict), Pct(NotFlat), Pct(TooDry), Pct(Wetter), Pct(Wooded), Pct(Iced), *Bins(FlatBins), *Bins(WetBins), *Bins(WoodBins),
			Strict, Once));
		TestTrue(TEXT("the spawned world's greenest lowland level and wetter than 0.6 nearly everywhere"), Land > 1000 && Wetter >= 0.95 * Land && Land - NotFlat >= 0.95 * Land);
		TestTrue(TEXT("where the herds' grass, no wetter than 0.6, was hardly any"), Once * 20 < Strict);
		Report(TEXT("the spawned world's greenest lowland"), Near.Num(), Strict, LushHerds, ReadHerds(Lush, LushHerds));
	}

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

	// With towns (the Federation's colonists'), no herd's loop within its radius and 500 m of a town's ground, nor its
	// radius and 50 m of a road; the herds that stand are the town-free world's.
	SlonkFauna::FFaunaWorld Towns = W;
	Towns.bTowns = true;
	Towns.People = SlonkCivs::Colonists();
	TArray<SlonkFauna::FFaunaGroup> Settled;
	HerdsOverGlobe(Towns, 30000, Settled);
	int32 InTown = 0, OnRoad = 0, NotBefore = 0;
	double NearestTown = 1e12;
	TSet<uint64> Before;
	for (const SlonkFauna::FFaunaGroup& G : Herds) { Before.Add(G.Id); }
	for (const SlonkFauna::FFaunaGroup& G : Settled)
	{
		NotBefore += Before.Contains(G.Id) ? 0 : 1;
		TArray<FTownSite> Sites;
		SlonkCities::EnumerateTowns(Towns.P, Towns.People, G.Centre * R, G.ExtentCm() + 5.0e5, Sites, 128);
		for (int32 k = 0; k < 480; ++k)
		{
			const FVector Dir = HerdLoopDir(G, R, UE_DOUBLE_TWO_PI * k / 480.0);
			for (const FTownSite& T : Sites)
			{
				const double Ground = T.bCliff ? FMath::Sqrt(FMath::Square(T.RadiusCm) + FMath::Square(30000.0)) : T.RadiusCm;
				const double Gap = FVector::Dist(T.LocalCm, Dir * R) - Ground - G.HerdRadiusCm;
				NearestTown = FMath::Min(NearestTown, Gap);
				InTown += Gap < 4.9e4 ? 1 : 0;
			}
			OnRoad += k % 2 == 0 && SlonkCities::PaintedWayCm(Towns.P, Towns.People, Dir) < G.HerdRadiusCm + 4900.0 ? 1 : 0;
		}
	}
	AddInfo(FString::Printf(TEXT("with the colonists' towns: %d herds of the %d (one for every %.1f cells whose middle is its grass; %d not among them), %d places of their loops within their radius and 490 m of a town's ground (the nearest %.0f m past their edge), %d within their radius and 49 m of a road"),
		Settled.Num(), Herds.Num(), (double)GrassMiddles / FMath::Max(Settled.Num(), 1), NotBefore, InTown, NearestTown / 100.0, OnRoad));
	TestEqual(TEXT("no herd near a town's ground"), InTown, 0);
	TestEqual(TEXT("nor on a road"), OnRoad, 0);
	TestEqual(TEXT("the herds a world with towns keeps are the town-free world's"), NotBefore, 0);
	TestTrue(TEXT("the towns keep some herds off their ground"), Settled.Num() < Herds.Num());

	// The debug's search, from 25 km off a herd (a way round its middle): the nearest herd there is, as every cell within
	// it holds them; and from the open sea, how far it looked.
	if (Herds.Num() > 0)
	{
		const SlonkFauna::FFaunaGroup& G0 = Herds[Herds.Num() / 2];
		const FVector From = AlongGround(G0.Centre, 2.5e6, R, 0.7);
		SlonkFauna::FFaunaGroup Found;
		SlonkFauna::FGroupSearch Search;
		const bool bFound = SlonkFauna::FindGroupNear(W, EFaunaKind::Herd, From, 4.0e7, 120000, Found, &Search);
		const double FoundAt = bFound ? FVector::Dist(Found.Centre, From) * R : -1.0;
		TArray<SlonkFauna::FFaunaCell> Near;
		SlonkFauna::CellsNear(W.P, EFaunaKind::Herd, From, FMath::Max(FoundAt, 0.0) + 4.0e5, Near);
		double Nearest = 1e12;
		for (const SlonkFauna::FFaunaCell& C : Near)
		{
			SlonkFauna::FFaunaGroup G;
			if (SlonkFauna::GroupInCell(W, C, G)) { Nearest = FMath::Min(Nearest, FVector::Dist(G.Centre, From) * R); }
		}
		SlonkFauna::FFaunaGroup Never;
		SlonkFauna::FGroupSearch Sea;
		const double SeaT0 = FPlatformTime::Seconds();
		const bool bAtSea = SlonkFauna::FindGroupNear(W, EFaunaKind::Herd, FVector(0.0, 0.0, -1.0), 4.0e6, 120000, Never, &Sea);
		const double SeaMs = (FPlatformTime::Seconds() - SeaT0) * 1000.0;
		AddInfo(FString::Printf(TEXT("the goto's search 25 km off a herd: one %.1f km off (the nearest of the %d cells round it %.1f km), %d cells read, every one within %.1f km; from the south pole %s, %d cells read out to %.1f km in %.0f ms"),
			FoundAt / 1e5, Near.Num(), Nearest / 1e5, Search.Cells, Search.ReachCm / 1e5, bAtSea ? TEXT("one found") : TEXT("none found"), Sea.Cells, Sea.ReachCm / 1e5, SeaMs));
		TestTrue(TEXT("the goto's search finds the nearest herd"), bFound && FMath::IsNearlyEqual(FoundAt, Nearest, 1.0) && FoundAt <= 2.6e6);
		TestTrue(TEXT("having read every cell out to it"), Search.ReachCm >= FoundAt && Sea.ReachCm >= 4.0e6 - 1.0);
	}
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
	// off, then turns about and walks back), someone gone as it walks away from them and someone come back ahead of it as
	// it walks back (it eases off on the way it was walking, then turns about), a craft passing low ahead of it as it
	// grazes (it turns, gallops off the way it came, loops round and walks back), a craft from its side and from ahead as
	// it walks (it stops and turns where it stands before it gallops), a second craft from ahead as it gallops (it flees
	// on): a planted hoof slides less than a centimetre a second on average, hardly one in ten thousand more than 10 cm/s
	// and none more than 25 cm/s. Round a sharp bend, and round one sharper than its way can turn, how sharply it turns
	// comes up to the bend's (or its most) and back down smoothly (by no more than Walker::MaxKapRate a second), its hooves
	// sliding as little on average and none more than 25 cm/s; walking and trotting up and across a slope, as little as on
	// the level; and no walker's yaw turns between two frames more than it can turn in one. All of it for the test's herd,
	// the first five herds found over the globe and the one of the smallest walkers (the shortest strides, the most of
	// them a second at its gallop).
	const SlonkFauna::FFaunaWorld W = EarthHerds();
	TArray<SlonkFauna::FFaunaGroup> Herds;
	{
		SlonkFauna::FFaunaGroup First;
		if (!TestHerd(W, First)) { AddError(TEXT("no herd found on the Earth-like world")); return false; }
		Herds.Add(First);
		TArray<SlonkFauna::FFaunaGroup> All;
		HerdsOverGlobe(W, 12000, All);
		auto Have = [&Herds](const SlonkFauna::FFaunaGroup& H) { return Herds.ContainsByPredicate([&H](const SlonkFauna::FFaunaGroup& O) { return O.Id == H.Id; }); };
		for (int32 i = 0; i < All.Num() && i < 5; ++i) { if (!Have(All[i])) { Herds.Add(All[i]); } }
		const SlonkFauna::FFaunaGroup* Smallest = nullptr;
		for (const SlonkFauna::FFaunaGroup& H : All) { if (!Smallest || H.SpanM < Smallest->SpanM) { Smallest = &H; } }
		if (Smallest && !Have(*Smallest)) { Herds.Add(*Smallest); }
	}
	const double R = W.P.RadiusCm;
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Herd);
	FDrawnGround Fine;
	Fine.P = W.P;
	Fine.Depth = 16;
	auto FineAt = [&Fine](const FVector& Dir) { return Fine.At(Dir); };
	for (int32 HerdIndex = 0; HerdIndex < Herds.Num(); ++HerdIndex)
	{
		const SlonkFauna::FFaunaGroup& G = Herds[HerdIndex];
		const bool bFull = HerdIndex == 0;
		const double C0 = 3.0 * G.CycleSec;
		const double WalkStart = C0 + G.TrotA - 25.0, GrazeStart = C0 + G.WalkB + 5.0;
		const FHerdRun Walk = RunHerd(G, R, WalkStart, 60.0, 30.0, 1, FineAt, 1);
		const FHerdRun Graze = RunHerd(G, R, GrazeStart, 90.0, 30.0, 1, FineAt, 1);
		const FHerdRun Frozen = RunHerd(G, R, WalkStart + 20.0, 15.0, 30.0, 1, FineAt, 2, true);
		if (bFull)
		{
			AddInfo(FString::Printf(TEXT("on the move (%d hoof-steps planted, the herd at %.2f m/s on average): planted hooves slide %.2f cm/s on average, %.2f at worst; grazing (%d, the walkers at %.1f cm/s): %.2f cm/s on average, %.2f at worst; with the walked distance left as it was, %.1f cm/s on average"),
				Walk.TravelSlips, Walk.TravelSlips > 0 ? Walk.SumTravelSpeed / Walk.TravelSlips / 100.0 : 0.0, Walk.MeanTravelSlip(), Walk.WorstTravelSlip, Graze.GrazeSlips,
				Graze.GrazeSlips > 0 ? Graze.SumGrazeSpeed / Graze.GrazeSlips : 0.0, Graze.MeanGrazeSlip(), Graze.WorstGrazeSlip, Frozen.MeanTravelSlip()));
		}
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
		auto Facing = [](const SlonkFauna::FHerdState& S) { return S.FrameRot.GetAxisX() * FMath::Cos(S.Heading) + S.FrameRot.GetAxisY() * FMath::Sin(S.Heading); };
		// The crew as the game moves a herd for them: startled if they run in close, kept clear of, and its offset off its path.
		auto CrewDrive = [&](SlonkFauna::FHerdState& S, SlonkFauna::FPanic& P, const TArray<FVector>& Crew, double Now, double Dt, int32& Startled)
		{
			FHerdDrive Dr;
			for (const FVector& C : Crew)
			{
				SlonkFauna::FFaunaThreat Foot;
				Foot.bCraft = false;
				Foot.PosCm = C;
				Startled += SlonkFauna::StartleHerd(P, S, G, D, R, Foot, Now) ? 1 : 0;
			}
			Dr.bMoving = SlonkFauna::KeepFromCrew(S, G, D, Crew, Dt);
			Dr.bShaken = P.IsLive(Now);
			Dr.Offset = SlonkFauna::HerdFrightOffset(G, D, P, R, Now) + S.Avoid;
			Dr.Ahead = SlonkFauna::HerdFrightOffset(G, D, P, R, Now + 1.0) + S.Avoid + (Dr.bMoving ? S.AvoidWay : FVector::ZeroVector);
			return Dr;
		};
		FVector Crew = FVector::ZeroVector, Toward = FVector::ZeroVector;
		int32 CrewStartles = 0;
		SlonkFauna::FPanic CrewPanic;
		const double CrewStart = GrazeStart + 10.0;
		const FHerdRun CrewRun = RunHerdWith(G, R, CrewStart, 150.0, 30.0, 1, FineAt, [&](SlonkFauna::FHerdState& S, double Now, double Dt)
		{
			if (!S.bStarted) { return FHerdDrive(); }
			if (Toward.IsZero()) { Crew = S.FramePos + Facing(S) * (G.HerdRadiusCm + 1.0e4); Toward = -Facing(S); }
			Crew += Toward * ((Now - CrewStart < 45.0 ? 140.0 : -140.0) * Dt);
			return CrewDrive(S, CrewPanic, TArray<FVector>{ Crew }, Now, Dt, CrewStartles);
		}, 1);
		// Someone walking at it from ahead as before, gone (beamed up) 3 s after it has come up to its pace walking away: it
		// eases off on the way it was walking, stops, turns about and walks back to its path. And again, but back once it has
		// walked back toward its path for 3 s, 40 m ahead of it: it eases off on the way back, stops, turns about and walks
		// off again.
		auto CrewComesAndGoes = [&](bool bComesBack, int32& Startled)
		{
			FVector At = FVector::ZeroVector, Way = FVector::ZeroVector;
			int32 Phase = 0;
			double Since = -1.0;
			SlonkFauna::FPanic P;
			return RunHerdWith(G, R, CrewStart, 110.0, 30.0, 1, FineAt, [&](SlonkFauna::FHerdState& S, double Now, double Dt)
			{
				if (!S.bStarted) { return FHerdDrive(); }
				if (Way.IsZero()) { At = S.FramePos + Facing(S) * (G.HerdRadiusCm + 1.0e4); Way = -Facing(S); }
				// Walking away from them at its pace, or (once they have gone) back to its path at its pace: how long it has.
				const bool bPaced = S.Travel >= 1.0 && S.AvoidPace >= 1.0 && (Phase == 0 ? FVector::DotProduct(S.AvoidWay, S.Avoid) > 0.0 : FVector::DotProduct(S.AvoidWay, S.Avoid) < 0.0);
				Since = bPaced ? (Since < 0.0 ? Now : Since) : -1.0;
				if (Phase == 0 && Since >= 0.0 && Now - Since > 3.0) { Phase = 1; Since = -1.0; }
				else if (Phase == 1 && bComesBack && Since >= 0.0 && Now - Since > 3.0) { Phase = 2; At = S.FramePos + Facing(S) * (G.HerdRadiusCm + 4000.0); }
				if (Phase == 0) { At += Way * (140.0 * Dt); }
				return CrewDrive(S, P, Phase == 1 ? TArray<FVector>() : TArray<FVector>{ At }, Now, Dt, Startled);
			}, 1);
		};
		int32 GoneStartles = 0, BackStartles = 0;
		const FHerdRun CrewGone = CrewComesAndGoes(false, GoneStartles);
		const FHerdRun CrewAhead = CrewComesAndGoes(true, BackStartles);
		// A craft passing 30 m up 50 m off it, coming at it, from a way relative to the way it faces (0 ahead).
		auto CraftBy = [&](const SlonkFauna::FHerdState& S, double Bearing)
		{
			const FVector Up = S.FramePos.GetSafeNormal();
			const FVector F = Facing(S);
			const FVector Way = F * FMath::Cos(Bearing) + FVector::CrossProduct(Up, F) * FMath::Sin(Bearing);
			SlonkFauna::FFaunaThreat Craft;
			Craft.PosCm = S.FramePos + Way * 5000.0 + Up * 3000.0;
			Craft.VelCmS = -Way * 8000.0;
			Craft.AltitudeCm = 3000.0;
			return Craft;
		};
		// A herd startled by craft so many seconds after Start, each from its way (CraftBy), stepped for Seconds.
		auto Frightened = [&](double Start, double Seconds, const TArray<TPair<double, double>>& Frights, int32& Startled)
		{
			SlonkFauna::FPanic P;
			int32 Next = 0;
			return RunHerdWith(G, R, Start, Seconds, 30.0, 1, FineAt, [&](SlonkFauna::FHerdState& S, double Now, double Dt)
			{
				FHerdDrive Dr;
				if (S.bStarted && Next < Frights.Num() && Now - Start >= Frights[Next].Key)
				{
					Startled += SlonkFauna::StartleHerd(P, S, G, D, R, CraftBy(S, Frights[Next].Value), Now) ? 1 : 0;
					++Next;
				}
				Dr.bShaken = P.IsLive(Now);
				Dr.Offset = SlonkFauna::HerdFrightOffset(G, D, P, R, Now);
				Dr.Ahead = SlonkFauna::HerdFrightOffset(G, D, P, R, Now + 1.0);
				return Dr;
			}, 1);
		};
		// A craft passing low ahead of it as it grazes, coming at it: it turns, gallops off (across the way its path goes,
		// its path setting off again during its fright), loops round and walks back to its path, its whole fright.
		int32 Startles = 0, SideStartles = 0, AheadStartles = 0, TwiceStartles = 0;
		const FHerdRun Fright = Frightened(GrazeStart + 10.0, D.FrightSec + 5.0, { { 0.0, 0.0 } }, Startles);
		// Walking its loop (or trotting it), a craft from its side and one from ahead: it stops, its walkers turn back and
		// round to the way it flees where they stand, and only then it gallops off. Galloping, a second craft from ahead: it
		// flees on the way it is going rather than turning about on the run.
		const double OnTheMove = C0 + G.WalkA + 12.0;
		const FHerdRun WalkSide = Frightened(OnTheMove, 40.0, { { 3.0, 0.5 * UE_DOUBLE_PI } }, SideStartles);
		const FHerdRun WalkAhead = Frightened(OnTheMove, 40.0, { { 3.0, 0.0 } }, AheadStartles);
		const FHerdRun GallopAhead = Frightened(GrazeStart + 10.0, 40.0, { { 0.0, UE_DOUBLE_PI }, { 10.0, 0.0 } }, TwiceStartles);
		// Walking along a line for the crew, then round a bend, then on: at 0.4 m/s round one of 0.4 radians a metre for a
		// sixth of a turn (sharp, but no sharper than its way can turn), and at 0.2 m/s round one sharper than that (0.55
		// radians a metre for an eighth of a turn): it turns as sharply as the bend (or as it can), coming up to that and back
		// down smoothly.
		auto RoundBend = [&](double SpeedCmS, double KapPerM, double Turn)
		{
			FVector Line = FVector::ZeroVector, Bend = FVector::ZeroVector;
			double OnAt = -1.0;
			return RunHerdWith(G, R, GrazeStart + 10.0, 40.0, 30.0, 1, FineAt, [&](SlonkFauna::FHerdState& S, double Now, double Dt)
			{
				FHerdDrive Dr;
				if (!S.bStarted) { return Dr; }
				const FVector Up = S.FramePos.GetSafeNormal();
				if (Line.IsZero()) { Line = Facing(S); Bend = FVector::CrossProduct(Up, Line); }
				Dr.bMoving = true;
				// Where the bend's walk has it, Tau seconds after it came up to its pace (come up to over two seconds): a metre
				// and a half on, round the bend, and on.
				auto At = [&](double Tau)
				{
					const double Kap = KapPerM / 100.0, Straight = 150.0;
					const double Along = Tau <= 0.0 ? 0.0 : SpeedCmS * (Tau < 2.0 ? Tau * Tau * Tau / 4.0 - Tau * Tau * Tau * Tau / 16.0 : Tau - 1.0);
					if (Along <= Straight) { return Line * Along; }
					const double Arc = FMath::Min(Along - Straight, Turn / Kap);
					const FVector Round = Line * Straight + Bend / Kap;
					FVector P = Round + (-Bend * FMath::Cos(Kap * Arc) + Line * FMath::Sin(Kap * Arc)) / Kap;
					if (Along - Straight > Turn / Kap) { P += (Line * FMath::Cos(Turn) + Bend * FMath::Sin(Turn)) * (Along - Straight - Turn / Kap); }
					return P;
				};
				if (OnAt < 0.0 && S.Travel >= 1.0) { OnAt = Now; }
				const double Tau = OnAt < 0.0 ? 0.0 : Now - OnAt;
				Dr.Offset = At(Tau);
				Dr.Ahead = OnAt < 0.0 ? Line * 200.0 : At(Tau + 1.0);
				return Dr;
			}, 1);
		};
		const FHerdRun Bent = RoundBend(40.0, 0.4, UE_DOUBLE_PI / 3.0);
		const FHerdRun Capped = RoundBend(20.0, 0.55, UE_DOUBLE_PI / 4.0);
		// Walking a bend for the crew at 0.8 m/s round 0.3 radians a metre, a craft from its side half way round: it stops and
		// turns where it stands, the way its turn begins from the way its walkers were drawn facing as that step came (the
		// material runs its way on by how sharply it turns for how far its legs walk), so none is snapped back.
		int32 BendStartles = 0, BendStartleStep = -1;
		double BendDrawn = 0.0, BendSnap = -1.0, BendWalkSnap = 0.0, BendKapThen = 0.0;
		const FHerdRun BendStop = [&]()
		{
			FVector Line = FVector::ZeroVector, Bend = FVector::ZeroVector;
			double OnAt = -1.0, TauStop = -1.0;
			SlonkFauna::FPanic P;
			int32 Step = -1;
			constexpr double SpeedCmS = 80.0, KapPerM = 0.3, Turn = 0.5 * UE_DOUBLE_PI;
			return RunHerdWith(G, R, GrazeStart + 10.0, 30.0, 30.0, 1, FineAt, [&](SlonkFauna::FHerdState& S, double Now, double Dt)
			{
				++Step;
				FHerdDrive Dr;
				if (!S.bStarted) { return Dr; }
				// The way the material drew it facing as this step comes (its walk run on from the last), against the way the
				// last step's turn began from, or (walking on) the way it faces now.
				if (BendStartleStep >= 0 && BendSnap < 0.0) { BendSnap = FMath::Abs(FMath::UnwindRadians(S.TurnHeading - BendDrawn)); }
				else if (BendStartleStep < 0 && S.Travel >= 1.0 && Step > 1 && BendDrawn != 0.0) { BendWalkSnap = FMath::Max(BendWalkSnap, FMath::Abs(FMath::UnwindRadians(S.Heading - BendDrawn))); }
				BendDrawn = S.Heading + S.WalkKap * S.GaitMS * Dt;
				const FVector Up = S.FramePos.GetSafeNormal();
				if (Line.IsZero()) { Line = Facing(S); Bend = FVector::CrossProduct(Up, Line); }
				auto At = [&](double Tau)
				{
					const double Kap = KapPerM / 100.0, Straight = 150.0;
					const double Along = Tau <= 0.0 ? 0.0 : SpeedCmS * (Tau < 2.0 ? Tau * Tau * Tau / 4.0 - Tau * Tau * Tau * Tau / 16.0 : Tau - 1.0);
					if (Along <= Straight) { return Line * Along; }
					const double Arc = FMath::Min(Along - Straight, Turn / Kap);
					FVector Pt = Line * Straight + Bend / Kap + (-Bend * FMath::Cos(Kap * Arc) + Line * FMath::Sin(Kap * Arc)) / Kap;
					if (Along - Straight > Turn / Kap) { Pt += (Line * FMath::Cos(Turn) + Bend * FMath::Sin(Turn)) * (Along - Straight - Turn / Kap); }
					return Pt;
				};
				if (OnAt < 0.0 && S.Travel >= 1.0) { OnAt = Now; }
				const double Tau = OnAt < 0.0 ? 0.0 : Now - OnAt;
				// Half way round the bend: 1.5 m on, then a quarter turn's arc at 0.3 rad/m.
				const double HalfWay = 1.0 + (150.0 + 0.5 * Turn / (KapPerM / 100.0)) / SpeedCmS;
				if (TauStop < 0.0 && Tau >= HalfWay)
				{
					TauStop = Tau;
					BendKapThen = S.WalkKap;
					BendStartles += SlonkFauna::StartleHerd(P, S, G, D, R, CraftBy(S, 0.5 * UE_DOUBLE_PI), Now) ? 1 : 0;
					BendStartleStep = Step;
				}
				Dr.bMoving = TauStop < 0.0;
				Dr.bShaken = P.IsLive(Now);
				const double TauHeld = TauStop < 0.0 ? Tau : TauStop;
				Dr.Offset = At(TauHeld) + SlonkFauna::HerdFrightOffset(G, D, P, R, Now);
				Dr.Ahead = (OnAt < 0.0 ? Line * 200.0 : At(TauStop < 0.0 ? Tau + 1.0 : TauStop)) + SlonkFauna::HerdFrightOffset(G, D, P, R, Now + 1.0);
				return Dr;
			}, 1);
		}();
		// Come about mid-fright on the move (its schedule walking, galloping off across its path's way 9 s after a craft came
		// from its side): it faces the way its fright takes it from its first frame, not its path's way.
		SlonkFauna::FPanic MidPanic;
		{
			SlonkFauna::FHerdState S;
			SlonkFauna::StartHerd(S, G, OnTheMove);
			SlonkFauna::StepHerd(S, G, R, OnTheMove, 0.0, FVector::ZeroVector, false, false, true, FineAt, 0.0, nullptr);
			SlonkFauna::StartleHerd(MidPanic, S, G, D, R, CraftBy(S, 0.5 * UE_DOUBLE_PI), OnTheMove);
		}
		const FHerdRun MidFright = RunHerdWith(G, R, OnTheMove + 9.0, 6.0, 30.0, 1, FineAt, [&](SlonkFauna::FHerdState& S, double Now, double Dt)
		{
			FHerdDrive Dr;
			Dr.bShaken = MidPanic.IsLive(Now);
			Dr.Offset = SlonkFauna::HerdFrightOffset(G, D, MidPanic, R, Now);
			Dr.Ahead = SlonkFauna::HerdFrightOffset(G, D, MidPanic, R, Now + 1.0);
			return Dr;
		}, 1);
		// Walking into its trot over ground tilted 10.7 degrees (as steep as its grass is) along its east, every way across.
		const FVector Mid = G.Centre * R;
		const double Rise = FMath::Tan(FMath::DegreesToRadians(10.7));
		const FHerdRun Slope = RunHerd(G, R, WalkStart, 60.0, 30.0, 1, [&](const FVector& Dir) { return 5000.0 + Rise * FVector::DotProduct(Dir * R - Mid, G.East); }, 2);
		auto Report = [](const TCHAR* What, const FHerdRun& Run)
		{
			return FString::Printf(TEXT("%s %.2f cm/s on average, %.1f at worst (%.0f s in; %.1f while its walkers turned; %d of %d over 10 cm/s), a walker's yaw %.3f rad a frame at most"), What, Run.MeanSlip(),
				Run.WorstSlip, Run.WorstSlipAt, Run.WorstTurnSlip, Run.Slides, Run.Slips, Run.WorstYawStep);
		};
		if (bFull)
		{
			AddInfo(FString::Printf(TEXT("planted hooves through its turns: %s; %s; %s (startled %d times); %s; %s"), *Report(TEXT("setting off"), SetOff), *Report(TEXT("stopping"), Stop),
				*Report(TEXT("someone walking at it from ahead and away"), CrewRun), CrewStartles, *Report(TEXT("a craft low ahead of it (its whole fright)"), Fright), *Report(TEXT("on a 10.7 degree slope"), Slope)));
			AddInfo(FString::Printf(TEXT("startled on the move: %s; %s; %s (startled %d, %d and %d times)"), *Report(TEXT("walking, a craft from its side"), WalkSide), *Report(TEXT("walking, a craft from ahead"), WalkAhead),
				*Report(TEXT("galloping, a second craft from ahead"), GallopAhead), SideStartles, AheadStartles, TwiceStartles));
			AddInfo(FString::Printf(TEXT("the crew on the move: %s; %s (startled %d and %d times)"), *Report(TEXT("gone as it walks away from them"), CrewGone), *Report(TEXT("back ahead of it as it walks back"), CrewAhead),
				GoneStartles, BackStartles));
			AddInfo(FString::Printf(TEXT("round a bend: %s, how sharply it turns up to %.2f rad/m, changing by %.4f rad/m a frame at most; %s, up to %.2f rad/m, changing by %.4f at most (%.4f allowed)"),
				*Report(TEXT("at 0.4 m/s round 0.4 rad/m"), Bent), Bent.MaxKap, Bent.MaxKapStep, *Report(TEXT("at 0.2 m/s round 0.55 rad/m, sharper than its way can turn"), Capped), Capped.MaxKap,
				Capped.MaxKapStep, SlonkFauna::Walker::MaxKapRate / 30.0));
			AddInfo(FString::Printf(TEXT("on the move and grazing, a walker's yaw turns %.3f and %.3f rad a frame at most"), Walk.WorstYawStep, Graze.WorstYawStep));
			AddInfo(FString::Printf(TEXT("startled walking a bend at 0.8 m/s, turning %.2f rad/m then (startled %d times): its turn begins %.5f rad from the way its walkers were drawn facing (%.5f at most at a step round the bend before); %s; come about mid-fright galloping across its path: %s"),
				BendKapThen, BendStartles, BendSnap, BendWalkSnap, *Report(TEXT("round the bend and its stop"), BendStop), *Report(TEXT("its first seconds"), MidFright)));
		}
		// Every herd's turns in a line: the worst of them on average and at all, how many slid past 10 cm/s, and how fast it
		// galloped (its shortest walkers' strides a second then).
		{
			double WorstMean = 0.0, WorstAt = 0.0, Fastest = 0.0;
			int32 Slides = 0, Slips = 0;
			for (const FHerdRun* Run : { &SetOff, &Stop, &CrewRun, &Fright, &WalkSide, &WalkAhead, &GallopAhead, &CrewGone, &CrewAhead, &Bent, &Capped, &MidFright })
			{
				WorstMean = FMath::Max(WorstMean, Run->MeanSlip());
				WorstAt = FMath::Max(WorstAt, Run->WorstSlip);
				Slides += Run->Slides;
				Slips += Run->Slips;
				Fastest = FMath::Max(Fastest, Run->MaxSpeedMS);
			}
			const double StrideM = SlonkFauna::Walker::StrideShare * FMath::Clamp(0.9 * (double)G.SpanM, SlonkFauna::Walker::MinSizeM, 2.6);
			AddInfo(FString::Printf(TEXT("herd %d of %d (%d walkers of %.2f m, strides from %.2f m; a loop of %.2f km): through its turns planted hooves slide %.2f cm/s on average at most, %.1f at worst, %d of %d over 10 cm/s; galloping at up to %.1f m/s (%.1f strides a second); on the move %.2f, grazing %.2f, on a slope %.2f cm/s"),
				HerdIndex + 1, Herds.Num(), G.Members, G.SpanM, StrideM, G.LengthCm / 1e5, WorstMean, WorstAt, Slides, Slips, Fastest, Fastest / StrideM, Walk.MeanTravelSlip(), Graze.MeanGrazeSlip(), Slope.MeanTravelSlip()));
		}
		TestTrue(TEXT("stopped on a bend, no walker is snapped back"), BendStartles == 1 && FMath::Abs(BendKapThen) > 0.2 && BendSnap >= 0.0 && BendSnap < 0.001 && BendWalkSnap < 0.001);
		TestTrue(TEXT("come about mid-fright, it faces the way it goes"), MidPanic.IsLive(OnTheMove + 9.0) && MidFright.MaxSpeedMS > 6.0);
		TestTrue(TEXT("it set off, stopped, walked off from someone and fled a craft, each a whole turn"), SetOff.MinTravel < 0.01 && SetOff.MaxTravel > 0.99 && Stop.MinTravel < 0.01 && Stop.MaxTravel > 0.99
			&& CrewRun.MinTravel < 0.01 && CrewRun.MaxTravel > 0.99 && Fright.MinTravel < 0.01 && Fright.MaxTravel > 0.99 && Startles == 1 && CrewStartles == 0);
		TestTrue(TEXT("startled on the move, it stood and turned where it stood before it fled"), SideStartles == 1 && AheadStartles == 1 && WalkSide.MinTravel < 0.01 && WalkAhead.MinTravel < 0.01
			&& WalkSide.MaxSpeedMS > 6.0 && WalkAhead.MaxSpeedMS > 6.0);
		TestTrue(TEXT("startled again galloping, it fled on"), TwiceStartles == 2 && GallopAhead.MaxSpeedMS > 6.0);
		TestTrue(TEXT("the crew went and came back unstartling"), GoneStartles == 0 && BackStartles == 0 && CrewGone.MinTravel < 0.01 && CrewAhead.MinTravel < 0.01);
		TestTrue(TEXT("round the bends, it turned as sharply as the bend, or as it can"), Bent.MaxKap > 0.35 && Capped.MaxKap > 0.45 && Bent.MaxTravel > 0.99 && Capped.MaxTravel > 0.99);
		TestTrue(TEXT("how sharply it turns changing smoothly, never at a jump"), FMath::Max(Bent.MaxKapStep, Capped.MaxKapStep) <= SlonkFauna::Walker::MaxKapRate / 30.0 + 1e-9);
		for (const FHerdRun* Run : { &SetOff, &Stop, &CrewRun, &Fright, &WalkSide, &WalkAhead, &GallopAhead, &CrewGone, &CrewAhead, &Bent, &Capped, &MidFright })
		{
			TestTrue(TEXT("through its turns, a planted hoof slides less than a centimetre a second on average"), Run->MeanSlip() < 1.0 && Run->Slips > 1000);
			TestTrue(TEXT("none more than 25 cm/s"), Run->WorstSlip < 25.0);
		}
		for (const FHerdRun* Run : { &SetOff, &Stop, &CrewRun, &Fright, &WalkSide, &WalkAhead, &GallopAhead, &CrewGone, &CrewAhead })
		{
			TestTrue(TEXT("hardly one in ten thousand more than 10 cm/s"), Run->Slides * 10000 <= Run->Slips);
		}
		TestTrue(TEXT("up and across a slope, a planted hoof slides less than a centimetre a second"), Slope.MeanTravelSlip() < 1.0 && Slope.WorstTravelSlip < 6.0);
		// A walker's yaw turns at most about 7.9 rad/s (a frightened herd's walkers turning right round in 0.4 s), a quarter of
		// a radian at 30 Hz: a turn the wrong way round its wrap would be a jump of up to a whole turn.
		for (const FHerdRun* Run : { &Walk, &Graze, &SetOff, &Stop, &CrewRun, &Fright, &WalkSide, &WalkAhead, &GallopAhead, &CrewGone, &CrewAhead, &Bent, &Capped, &Slope, &BendStop, &MidFright })
		{
			TestTrue(TEXT("no walker's yaw jumps between two frames"), Run->WorstYawStep < 0.3);
		}
	}
	TestTrue(TEXT("five herds or more to follow"), Herds.Num() >= 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaHerdStartleTest, "Slonk.Fauna.HerdStartle", SLONK_TEST_FLAGS)
bool FSlonkFaunaHerdStartleTest::RunTest(const FString& Parameters)
{
	// A craft passing 30 m over a herd startles it: it gallops off (at a grazer's gallop, from a stand), heads up, and
	// comes back onto its path by the end of its fright, at a steady pace no faster than its own trot; one passing 300 m
	// up does not. Someone run in close to it startles it; someone walking up to it sees it walk off, keeping about 50 m
	// from them, and come back once they leave. Startled again while it flees, it turns away from the new fright carrying
	// on its gallop: nothing snaps (its place, its speed, its walkers' heads).
	const SlonkFauna::FFaunaWorld W = EarthHerds();
	SlonkFauna::FFaunaGroup G;
	if (!TestHerd(W, G)) { AddError(TEXT("no herd found on the Earth-like world")); return false; }
	const double R = W.P.RadiusCm;
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Herd);
	auto Height = [&W](const FVector& Dir) { return FMath::Max(SlonkTerrain::Sample(W.P, Dir, 2000.0).HeightCm, 0.0); };
	const double T0 = G.CycleSec * 2.0 + G.WalkB + 2.0;
	auto Step = [&](SlonkFauna::FHerdState& S, const SlonkFauna::FPanic& P, double Now, double Dt, int32 k)
	{
		const FVector Ahead = SlonkFauna::HerdFrightOffset(G, D, P, R, Now + 1.0) + S.Avoid;
		SlonkFauna::StepHerd(S, G, R, Now, Dt, SlonkFauna::HerdFrightOffset(G, D, P, R, Now) + S.Avoid, P.IsLive(Now), false, k % 4 == 0, Height, 0.0, nullptr, &Ahead);
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
			Startles += SlonkFauna::StartleHerd(Panic, S, G, D, R, Craft, Now) ? 1 : 0;
			Step(S, Panic, Now, 1.0 / 20.0, k);
		}
		return Startles;
	};
	SlonkFauna::FPanic Low, High;
	SlonkFauna::FHerdState SLow, SHigh;
	const int32 LowStartles = Pass(3000.0, Low, SLow);
	const int32 HighStartles = Pass(30000.0, High, SHigh);
	// Its flight after the pass: how far off its path 30 s on, how fast at its fastest, how fast it comes back, and back
	// on its path after.
	double Fastest = 0.0, FastestAt = 0.0, BackFastest = 0.0;
	const int32 Last = FMath::CeilToInt32(20.0 * (Low.T0 - T0 + D.FrightSec + 10.0));
	for (int32 k = 301; k <= Last; ++k)
	{
		const double Now = T0 + k / 20.0;
		Step(SLow, Low, Now, 1.0 / 20.0, k);
		if (SLow.SpeedMS > Fastest) { Fastest = SLow.SpeedMS; FastestAt = Now - Low.T0; }
		if (Now - Low.T0 > D.FleeBackSec) { BackFastest = FMath::Max(BackFastest, (SlonkFauna::HerdFrightOffset(G, D, Low, R, Now) - SlonkFauna::HerdFrightOffset(G, D, Low, R, Now - 0.05)).Size() / 5.0); }
	}
	const double Fled = SlonkFauna::HerdFrightOffset(G, D, Low, R, Low.T0 + 30.0).Size();
	const double Back = SlonkFauna::HerdFrightOffset(G, D, Low, R, Low.T0 + D.FrightSec + 0.01).Size();
	// On its path again: where a herd never startled stands then.
	SlonkFauna::FHerdState Calm;
	SlonkFauna::FPanic NoPanic;
	SlonkFauna::StartHerd(Calm, G, Low.T0 + D.FrightSec + 5.0);
	Step(Calm, NoPanic, Low.T0 + D.FrightSec + 5.0, 0.0, 0);
	SlonkFauna::FHerdState Again = SLow;
	Step(Again, Low, Low.T0 + D.FrightSec + 5.0, 0.0, 0);
	const double OffPath = FVector::Dist(Again.FramePos, Calm.FramePos);
	AddInfo(FString::Printf(TEXT("a herd of %d: a pass 30 m up startles it %d times; it gallops off at up to %.1f m/s (%.0f s in), %.0f m off its path 30 s on, comes back from %.0f s at up to %.2f m/s (its trot %.1f m/s), %.2f m off it %.0f s after (%.2f cm from a herd never startled); a pass 300 m up startles it %d times"),
		G.Members, LowStartles, Fastest, FastestAt, Fled / 100.0, D.FleeBackSec, BackFastest, G.TrotCmS / 100.0, Back / 100.0, D.FrightSec, OffPath, HighStartles));
	TestTrue(TEXT("a craft passing low startles the herd"), LowStartles >= 1 && Fled > 1.5e4);
	TestEqual(TEXT("one passing high does not"), HighStartles, 0);
	TestEqual(TEXT("back on its path by the end of its fright"), Back, 0.0);
	TestTrue(TEXT("exactly where it would have been"), OffPath < 0.01);
	TestTrue(TEXT("at a grazer's gallop, from a stand"), Fastest > 6.0 && Fastest < 16.0 && FastestAt > 2.0);
	TestTrue(TEXT("coming back no faster than its own trot"), BackFastest > 0.5 && BackFastest * 100.0 < G.TrotCmS);

	// Frightened a second time, 8 s into its flight (galloping) and 30 s in (slowing), by a craft coming after it the way
	// it flees: what the first flight left it carrying comes back with the second at the same steady pace, over a fright
	// as much longer as its way back is, so it comes back no faster than after one, below the slowest trot of any herd
	// (2.4 m/s).
	auto Restartled = [&](double AfterSec, double& OutLive)
	{
		SlonkFauna::FPanic P;
		SlonkFauna::FHerdState S;
		SlonkFauna::StartHerd(S, G, T0);
		Step(S, P, T0, 0.0, 0);
		SlonkFauna::FFaunaThreat First;
		First.PosCm = S.FramePos + S.FrameRot.GetAxisX() * 5000.0 + S.FramePos.GetSafeNormal() * 3000.0;
		First.AltitudeCm = 3000.0;
		SlonkFauna::StartleHerd(P, S, G, D, R, First, T0);
		double Fastest = 0.0;
		const int32 AgainAt = FMath::RoundToInt32(AfterSec * 20.0);
		for (int32 k = 1; k <= 20 * 700; ++k)
		{
			const double Now = T0 + k / 20.0;
			if (k == AgainAt)
			{
				SlonkFauna::FFaunaThreat Second;
				const FVector Up = S.FramePos.GetSafeNormal();
				Second.PosCm = S.FramePos - (P.Dir - Up * FVector::DotProduct(P.Dir, Up)).GetSafeNormal() * 5000.0 + Up * 3000.0;
				Second.AltitudeCm = 3000.0;
				SlonkFauna::StartleHerd(P, S, G, D, R, Second, Now, true);
				OutLive = P.LiveSec;
			}
			Step(S, P, Now, 1.0 / 20.0, k);
			if (k > AgainAt && Now - P.T0 > D.FleeBackSec) { Fastest = FMath::Max(Fastest, (SlonkFauna::HerdFrightOffset(G, D, P, R, Now) - SlonkFauna::HerdFrightOffset(G, D, P, R, Now - 0.05)).Size() / 5.0); }
			if (k > AgainAt && !P.IsLive(Now)) { break; }
		}
		return Fastest;
	};
	double Live8 = 0.0, Live30 = 0.0;
	const double Back8 = Restartled(8.0, Live8), Back30 = Restartled(30.0, Live30);
	AddInfo(FString::Printf(TEXT("frightened again 8 s into its flight it comes back at up to %.2f m/s (its fright %.0f s), 30 s in at up to %.2f m/s (%.0f s); after one, %.2f m/s"),
		Back8, Live8, Back30, Live30, BackFastest));
	TestTrue(TEXT("frightened twice, it comes back no faster than after one, below any herd's trot"), Back8 < 2.4 && Back30 < 2.4 && Back8 * 100.0 < G.TrotCmS && Back30 * 100.0 < G.TrotCmS
		&& Back8 > 0.5 && Back30 > 0.5);
	TestTrue(TEXT("its fright the longer for the further way back"), Live8 > D.FrightSec && Live30 > Live8);

	// Someone on foot: run in to 15 m of its nearest member, it bolts; 80 m off, it does not.
	SlonkFauna::FHerdState Here;
	SlonkFauna::StartHerd(Here, G, T0);
	Step(Here, NoPanic, T0, 0.0, 0);
	const FVector Side = Here.FrameRot.GetAxisY();
	SlonkFauna::FFaunaThreat Walker;
	Walker.bCraft = false;
	Walker.PosCm = Here.FramePos + Side * (G.HerdRadiusCm + 1500.0);
	SlonkFauna::FPanic Near, Far;
	SlonkFauna::FHerdState HereNear = Here, HereFar = Here;
	TestTrue(TEXT("someone run in close startles it"), SlonkFauna::StartleHerd(Near, HereNear, G, D, R, Walker, T0));
	Walker.PosCm = Here.FramePos + Side * (G.HerdRadiusCm + 8000.0);
	TestFalse(TEXT("someone 80 m off does not"), SlonkFauna::StartleHerd(Far, HereFar, G, D, R, Walker, T0));
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
		CrewStartles += SlonkFauna::StartleHerd(KeptPanic, Kept, G, D, R, Foot, Now) ? 1 : 0;
		const bool bMoving = SlonkFauna::KeepFromCrew(Kept, G, D, TArray<FVector>{ Crew }, 1.0 / 20.0);
		const FVector Ahead = SlonkFauna::HerdFrightOffset(G, D, KeptPanic, R, Now + 1.0) + Kept.Avoid + (bMoving ? Kept.AvoidWay : FVector::ZeroVector);
		SlonkFauna::StepHerd(Kept, G, R, Now, 1.0 / 20.0, SlonkFauna::HerdFrightOffset(G, D, KeptPanic, R, Now) + Kept.Avoid, KeptPanic.IsLive(Now), bMoving, k % 4 == 0, Height, 0.0, nullptr, &Ahead);
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
	TestTrue(TEXT("startled by a craft 50 m off"), SlonkFauna::StartleHerd(Twice, STwice, G, D, R, First, T0));
	const double Ts = T0 + 12.0;
	for (int32 k = 1; k < 240; ++k) { Step(STwice, Twice, T0 + k / 20.0, 1.0 / 20.0, k); }
	const FVector Before = SlonkFauna::HerdFrightOffset(G, D, Twice, R, Ts), BeforeV = (Before - SlonkFauna::HerdFrightOffset(G, D, Twice, R, Ts - 0.05)) / 0.05;
	float GdBefore[SlonkFauna::HerdData::Num] = {};
	SlonkFauna::FHerdState SAt = STwice;
	SlonkFauna::StepHerd(SAt, G, R, Ts, 1.0 / 20.0, Before + SAt.Avoid, true, false, true, Height, 0.0, GdBefore);
	SlonkFauna::HerdFrightData(Twice, Ts, 0.0, GdBefore);
	SlonkFauna::FFaunaThreat Second;
	Second.PosCm = SAt.FramePos + SAt.FrameRot.GetAxisY() * 5000.0 + SAt.FramePos.GetSafeNormal() * 3000.0;
	Second.AltitudeCm = 3000.0;
	const FVector FirstDir = Twice.Dir;
	TestTrue(TEXT("startled again 12 s on"), SlonkFauna::StartleHerd(Twice, SAt, G, D, R, Second, Ts));
	const FVector After = SlonkFauna::HerdFrightOffset(G, D, Twice, R, Ts), AfterV = (SlonkFauna::HerdFrightOffset(G, D, Twice, R, Ts + 0.05) - After) / 0.05;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaHerdFleesDryTest, "Slonk.Fauna.HerdFleesDry", SLONK_TEST_FLAGS)
bool FSlonkFaunaHerdFleesDryTest::RunTest(const FString& Parameters)
{
	// A frightened herd's flight never takes it into the water nor across it: with a lake where a craft would have driven
	// it (from each of eight ways round it as it grazes, from its side as it walks, and a second lake where a second craft
	// 8 s into its flight would have), it flees a way that keeps every walker out of the water (its frame its radius from
	// the shore) from its startle to its fright's end, read ten times a second; with water all round it but along its
	// loop, it flees along its loop. The water read in another order gives the same flight. Someone walking it off its
	// path toward a lake never walks it in: its edge stops short of the shore; and where its loop's dry ground is narrow,
	// the crew walk it no further off its path than that leaves room for. Without the water read, each of these would
	// have taken it in.
	const SlonkFauna::FFaunaWorld W = EarthHerds();
	SlonkFauna::FFaunaGroup G;
	if (!TestHerd(W, G)) { AddError(TEXT("no herd found on the Earth-like world")); return false; }
	const double R = W.P.RadiusCm;
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Herd);
	auto Height = [&W](const FVector& Dir) { return FMath::Max(SlonkTerrain::Sample(W.P, Dir, 2000.0).HeightCm, 0.0); };
	auto Facing = [](const SlonkFauna::FHerdState& S) { return S.FrameRot.GetAxisX() * FMath::Cos(S.Heading) + S.FrameRot.GetAxisY() * FMath::Sin(S.Heading); };
	// Lakes: discs on the sea-level sphere (planet-local cm), how far a place is from the nearest one's shore (below 0 in it).
	struct FLake { FVector At = FVector::ZeroVector; double RadiusCm = 0.0; };
	auto LakeGap = [R](const TArray<FLake>& Lakes, const FVector& PosCm)
	{
		double Gap = 1.0e12;
		for (const FLake& L : Lakes) { Gap = FMath::Min(Gap, FVector::Dist(PosCm.GetSafeNormal() * R, L.At) - L.RadiusCm); }
		return Gap;
	};
	// A craft 30 m up 50 m off it along a way from the way it faces, coming at it.
	auto CraftAt = [&](const SlonkFauna::FHerdState& S, double Bearing)
	{
		const FVector Up = S.FramePos.GetSafeNormal();
		const FVector F = Facing(S);
		const FVector Way = F * FMath::Cos(Bearing) + FVector::CrossProduct(Up, F) * FMath::Sin(Bearing);
		SlonkFauna::FFaunaThreat Craft;
		Craft.PosCm = S.FramePos + Way * 5000.0 + Up * 3000.0;
		Craft.VelCmS = -Way * 8000.0;
		Craft.AltitudeCm = 3000.0;
		return Craft;
	};
	// A herd from Start, startled at its first step by a craft from Bearing and (Again past 0) that many seconds on by one
	// from AgainBearing, stepped at 10 Hz to its fright's end: how near its walkers came to the water (its frame's gap to
	// it less its radius), where it stood when startled each time, and its fright as each startle left it.
	struct FFled { double WorstCm = 1.0e12; FVector At0 = FVector::ZeroVector, At1 = FVector::ZeroVector; SlonkFauna::FPanic First, Second; };
	auto Flee = [&](double Start, double Bearing, double Again, double AgainBearing, TFunctionRef<double(const FVector&)> Gap, SlonkFauna::FHerdWater* Water)
	{
		FFled Out;
		SlonkFauna::FHerdState S;
		SlonkFauna::FPanic P;
		SlonkFauna::StartHerd(S, G, Start);
		const int32 Again10 = FMath::RoundToInt32(Again * 10.0);
		for (int32 k = 0; ; ++k)
		{
			const double Now = Start + k / 10.0;
			if (k == 1) { SlonkFauna::StartleHerd(P, S, G, D, R, CraftAt(S, Bearing), Now, true, Water); Out.First = P; Out.At0 = S.FramePos; }
			if (Again10 > 0 && k == 1 + Again10) { SlonkFauna::StartleHerd(P, S, G, D, R, CraftAt(S, AgainBearing), Now, true, Water); Out.Second = P; Out.At1 = S.FramePos; }
			const FVector Ahead = SlonkFauna::HerdFrightOffset(G, D, P, R, Now + 1.0);
			SlonkFauna::StepHerd(S, G, R, Now, k == 0 ? 0.0 : 0.1, SlonkFauna::HerdFrightOffset(G, D, P, R, Now), P.IsLive(Now), false, k % 4 == 0, Height, 0.0, nullptr, &Ahead);
			if (k > 0) { Out.WorstCm = FMath::Min(Out.WorstCm, Gap(S.FramePos) - G.HerdRadiusCm); }
			if (k > 2 && Now > P.T0 + P.LiveSec + 1.0) { break; }
		}
		return Out;
	};
	const double C0 = 3.0 * G.CycleSec;
	const double GrazeAt = C0 + G.WalkB + 5.0, WalkAt = C0 + G.WalkA + 12.0;
	// Each startle with a lake of 100 m set 220 m along the way it fled without one, its shore its radius and 50 m further
	// on: dry with the water read, in it without.
	int32 Runs = 0, Wet = 0, WetBlind = 0, Turned = 0;
	double Worst = 1.0e12, WorstBlind = 1.0e12;
	auto WithLake = [&](double Start, double Bearing, double Again, double AgainBearing)
	{
		const FFled Blind = Flee(Start, Bearing, Again, AgainBearing, [](const FVector&) { return 1.0e12; }, nullptr);
		const SlonkFauna::FPanic& Fled = Again > 0.0 ? Blind.Second : Blind.First;
		const FVector From = Again > 0.0 ? Blind.At1 : Blind.At0;
		const FVector Up = From.GetSafeNormal();
		const FVector Way = (Fled.Dir - Up * FVector::DotProduct(Fled.Dir, Up)).GetSafeNormal();
		TArray<FLake> Lakes;
		Lakes.Add({ (From + Way * 2.2e4).GetSafeNormal() * R, 1.0e4 });
		auto Gap = [&](const FVector& Pos) { return LakeGap(Lakes, Pos); };
		SlonkFauna::FHerdWater Water([&](const FVector& Dir) { return LakeGap(Lakes, Dir * R) < 0.0; }, R);
		const FFled Dry = Flee(Start, Bearing, Again, AgainBearing, Gap, &Water);
		const FFled Into = Flee(Start, Bearing, Again, AgainBearing, Gap, nullptr);
		const SlonkFauna::FPanic& Chose = Again > 0.0 ? Dry.Second : Dry.First;
		++Runs;
		Wet += Dry.WorstCm < 0.0 ? 1 : 0;
		WetBlind += Into.WorstCm < 0.0 ? 1 : 0;
		Turned += Chose.LoopWay != 0 || FVector::DotProduct(Chose.Dir, Fled.Dir) < 0.99 ? 1 : 0;
		Worst = FMath::Min(Worst, Dry.WorstCm);
		WorstBlind = FMath::Min(WorstBlind, Into.WorstCm);
	};
	for (int32 b = 0; b < 8; ++b) { WithLake(GrazeAt, UE_DOUBLE_TWO_PI * b / 8.0, 0.0, 0.0); }
	WithLake(WalkAt, 0.5 * UE_DOUBLE_PI, 0.0, 0.0);
	WithLake(GrazeAt, UE_DOUBLE_PI, 8.0, 0.5 * UE_DOUBLE_PI);
	AddInfo(FString::Printf(TEXT("%d startles with a lake of 100 m where each flight would go: %d flights turned from it, %d into it (its walkers %.0f m from its shore at the nearest); without the water read %d into it (%.0f m)"),
		Runs, Turned, Wet, Worst / 100.0, WetBlind, WorstBlind / 100.0));
	TestEqual(TEXT("no flight into a lake or across it"), Wet, 0);
	TestEqual(TEXT("each turned from it"), Turned, Runs);
	TestEqual(TEXT("as each would have gone into it without the water read"), WetBlind, Runs);

	// Water everywhere but within 70 m of its loop (its walkers spread about 45 m either side of it): it flees along its
	// loop, its walkers never in the water.
	TArray<FVector> Loop;
	for (int32 k = 0; k < 960; ++k) { Loop.Add(HerdLoopDir(G, R, UE_DOUBLE_TWO_PI * k / 960.0) * R); }
	auto BandGap = [&](const FVector& Pos)
	{
		const FVector P = Pos.GetSafeNormal() * R;
		double Near = 1.0e12;
		for (const FVector& L : Loop) { Near = FMath::Min(Near, FVector::DistSquared(P, L)); }
		return 7000.0 - FMath::Sqrt(Near);
	};
	SlonkFauna::FHerdWater Band([&](const FVector& Dir) { return BandGap(Dir * R) < 0.0; }, R);
	const FFled Along = Flee(GrazeAt, 0.0, 0.0, 0.0, BandGap, &Band);
	const FFled AlongBlind = Flee(GrazeAt, 0.0, 0.0, 0.0, BandGap, nullptr);
	AddInfo(FString::Printf(TEXT("with water all round but along its loop: it flees %s (its walkers %.0f m short of the water at the nearest; %.0f m into it without the water read), its fright %.0f s"),
		Along.First.LoopWay != 0 ? TEXT("along its loop") : TEXT("off it"), Along.WorstCm / 100.0, -AlongBlind.WorstCm / 100.0, Along.First.LiveSec));
	TestTrue(TEXT("with no dry way off its loop, it flees along it"), Along.First.LoopWay != 0 && Along.WorstCm >= 0.0 && AlongBlind.WorstCm < 0.0);

	// The same flight whatever order the water is read in: one water read fresh, one read first all about it at random.
	{
		TArray<FLake> Lakes;
		SlonkFauna::FHerdState S;
		SlonkFauna::StartHerd(S, G, GrazeAt);
		SlonkFauna::StepHerd(S, G, R, GrazeAt, 0.0, FVector::ZeroVector, false, false, true, Height, 0.0, nullptr);
		const FVector Up = S.FramePos.GetSafeNormal();
		Lakes.Add({ (S.FramePos - Facing(S) * 2.0e4).GetSafeNormal() * R, 1.2e4 });
		Lakes.Add({ (S.FramePos + FVector::CrossProduct(Up, Facing(S)) * 2.5e4).GetSafeNormal() * R, 8.0e3 });
		auto IsWet = [&](const FVector& Dir) { return LakeGap(Lakes, Dir * R) < 0.0; };
		SlonkFauna::FHerdWater Fresh(IsWet, R), Read(IsWet, R);
		FRandomStream Rng(77);
		for (int32 i = 0; i < 2000; ++i) { Read.WetAt(G, S.FramePos + FVector(Rng.FRandRange(-6e4f, 6e4f), Rng.FRandRange(-6e4f, 6e4f), Rng.FRandRange(-6e4f, 6e4f))); }
		SlonkFauna::FHerdState Sa = S, Sb = S;
		SlonkFauna::FPanic Pa, Pb;
		SlonkFauna::StartleHerd(Pa, Sa, G, D, R, CraftAt(S, 0.0), GrazeAt + 0.1, true, &Fresh);
		SlonkFauna::StartleHerd(Pb, Sb, G, D, R, CraftAt(S, 0.0), GrazeAt + 0.1, true, &Read);
		const bool bSame = Pa.Dir == Pb.Dir && Pa.LoopWay == Pb.LoopWay && Pa.LiveSec == Pb.LiveSec && Pa.FleeT0 == Pb.FleeT0 && Pa.Side == Pb.Side;
		// What a startle costs the game thread on the world's own water, read fresh each time (a herd's first fright).
		double Ms = 0.0;
		int32 WorldReads = 0;
		for (int32 b = 0; b < 8; ++b)
		{
			SlonkFauna::FHerdWater Field(W.P);
			SlonkFauna::FHerdState Sc = S;
			SlonkFauna::FPanic Pc;
			const double T0 = FPlatformTime::Seconds();
			SlonkFauna::StartleHerd(Pc, Sc, G, D, R, CraftAt(S, UE_DOUBLE_TWO_PI * b / 8.0), GrazeAt + 0.1, true, &Field);
			Ms += (FPlatformTime::Seconds() - T0) * 1000.0;
			WorldReads += Field.Reads();
		}
		AddInfo(FString::Printf(TEXT("the water read fresh (%d points) and after 2,000 reads at random about it: %s flight; a startle on the world's own water %.2f ms on average, reading %d points of it"),
			Fresh.Reads(), bSame ? TEXT("the same") : TEXT("another"), Ms / 8.0, WorldReads / 8));
		TestTrue(TEXT("the same flight whatever order the water is read in"), bSame);
	}

	// Someone walking at it from inside its loop, out across it toward a lake of 150 m whose shore is 120 m past its far
	// edge, at 1.4 m/s for two and a half minutes: it walks off toward the lake and stops short of it.
	auto OutWay = [&](const SlonkFauna::FHerdState& S)
	{
		const FVector Up = S.FramePos.GetSafeNormal();
		const FVector Out = S.FramePos - G.Centre * R;
		return (Out - Up * FVector::DotProduct(Out, Up)).GetSafeNormal();
	};
	auto Pushed = [&](SlonkFauna::FFaunaGroup& Herd, TFunctionRef<double(const FVector&)> Gap, SlonkFauna::FHerdWater* Water, double& MostOff, int32& Startles)
	{
		SlonkFauna::FHerdState S;
		SlonkFauna::FPanic P;
		SlonkFauna::StartHerd(S, Herd, GrazeAt);
		SlonkFauna::StepHerd(S, Herd, R, GrazeAt, 0.0, FVector::ZeroVector, false, false, true, Height, 0.0, nullptr);
		FVector Crew = FVector::ZeroVector, Toward = FVector::ZeroVector;
		double WorstCm = 1.0e12;
		MostOff = 0.0;
		for (int32 k = 1; k <= 30 * 150; ++k)
		{
			const double Now = GrazeAt + k / 30.0;
			if (Toward.IsZero()) { Toward = OutWay(S); Crew = S.FramePos - Toward * (Herd.HerdRadiusCm + 1.0e4); }
			Crew += Toward * (140.0 / 30.0);
			SlonkFauna::FFaunaThreat Foot;
			Foot.bCraft = false;
			Foot.PosCm = Crew;
			Startles += SlonkFauna::StartleHerd(P, S, Herd, D, R, Foot, Now, false, Water) ? 1 : 0;
			const bool bMoving = SlonkFauna::KeepFromCrew(S, Herd, D, TArray<FVector>{ Crew }, 1.0 / 30.0, Water);
			const FVector Ahead = SlonkFauna::HerdFrightOffset(Herd, D, P, R, Now + 1.0) + S.Avoid + (bMoving ? S.AvoidWay : FVector::ZeroVector);
			SlonkFauna::StepHerd(S, Herd, R, Now, 1.0 / 30.0, SlonkFauna::HerdFrightOffset(Herd, D, P, R, Now) + S.Avoid, P.IsLive(Now), bMoving, k % 4 == 0, Height, 0.0, nullptr, &Ahead);
			WorstCm = FMath::Min(WorstCm, Gap(S.FramePos) - Herd.HerdRadiusCm);
			MostOff = FMath::Max(MostOff, S.Avoid.Size());
		}
		return WorstCm;
	};
	{
		SlonkFauna::FHerdState S;
		SlonkFauna::StartHerd(S, G, GrazeAt);
		SlonkFauna::StepHerd(S, G, R, GrazeAt, 0.0, FVector::ZeroVector, false, false, true, Height, 0.0, nullptr);
		TArray<FLake> Lakes;
		Lakes.Add({ (S.FramePos + OutWay(S) * (G.HerdRadiusCm + 1.2e4 + 1.5e4)).GetSafeNormal() * R, 1.5e4 });
		auto Gap = [&](const FVector& Pos) { return LakeGap(Lakes, Pos); };
		SlonkFauna::FHerdWater Water([&](const FVector& Dir) { return LakeGap(Lakes, Dir * R) < 0.0; }, R);
		// Its loop's dry ground as the lake leaves it, as its cell would have read it.
		SlonkFauna::FFaunaGroup ByLake = G;
		ByLake.DryCm = SlonkFauna::HerdDryReadCm;
		for (int32 k = 0; k < 960; ++k) { ByLake.DryCm = FMath::Min(ByLake.DryCm, LakeGap(Lakes, HerdLoopDir(G, R, UE_DOUBLE_TWO_PI * k / 960.0) * R)); }
		double Off = 0.0, OffBlind = 0.0, OffNarrow = 0.0;
		int32 Startles = 0, StartlesBlind = 0, StartlesNarrow = 0;
		const double Kept = Pushed(ByLake, Gap, &Water, Off, Startles);
		const double Blind = Pushed(G, Gap, nullptr, OffBlind, StartlesBlind);
		// Its loop's dry ground 120 m wide either side: the crew walk it no further than that less its radius and 20 m.
		SlonkFauna::FFaunaGroup Narrow = G;
		Narrow.DryCm = 1.2e4;
		const double Room = SlonkFauna::CrewRoomCm(Narrow);
		Pushed(Narrow, [](const FVector&) { return 1.0e12; }, nullptr, OffNarrow, StartlesNarrow);
		AddInfo(FString::Printf(TEXT("someone walking it toward a lake: its walkers stop %.1f m short of the shore (walked %.0f m off its path, its loop's dry ground leaving %.0f m of room; startled %d times); without the water read %.1f m into it; its loop's dry ground 120 m wide, it is walked %.1f m off its path at most (%.1f m of room, 300 m with no water near)"),
			Kept / 100.0, Off / 100.0, SlonkFauna::CrewRoomCm(ByLake) / 100.0, Startles, -Blind / 100.0, OffNarrow / 100.0, Room / 100.0));
		TestTrue(TEXT("the crew never walk it into the water"), Kept >= 0.0 && Blind < 0.0 && Off > 5000.0);
		TestTrue(TEXT("nor further off its path than its loop's dry ground leaves room for"), Room > 0.0 && Room < 1.2e4 && OffNarrow <= Room + 1.0 && OffNarrow > Room - 1000.0
			&& SlonkFauna::CrewRoomCm(G) == 3.0e4);
	}
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

	// A walker turning to walk where it stands, its grazing way a hair short of opposite the herd's way, and the herd's
	// way as slot 4 packs it a hair past the half turn (so the body's turn and its feet's lie either side of it): its body
	// turns the same way round as its planted hooves are carried, so none of them slides.
	{
		float Cu[SlonkFauna::Custom::Num] = {};
		float Gd[SlonkFauna::HerdData::Num] = {};
		const float Size = 2.f;
		Cu[SlonkFauna::Custom::SizeM] = Size;
		Cu[SlonkFauna::Custom::Seed] = 0.37f;
		Cu[SlonkFauna::Custom::RateHz] = 1.24f;
		Cu[SlonkFauna::Custom::PerKind] = 1.2f;
		const float T0 = 1500.f;
		const double TurnStep = 2.0 * UE_DOUBLE_PI / SlonkFauna::HerdData::TurnSteps;
		const double Hd = 1234.0 * TurnStep - 0.0007;
		Gd[SlonkFauna::HerdData::Heading] = (float)Hd;
		Gd[SlonkFauna::HerdData::TurnHeading] = SlonkFauna::HerdData::PackTurns(0.3, Hd);
		Gd[SlonkFauna::HerdData::DistM] = 100.f;
		Gd[SlonkFauna::HerdData::GrazeSec] = 1000.f;
		Gd[SlonkFauna::HerdData::GrazeRef] = 700.f;
		Gd[SlonkFauna::HerdData::ClockT0] = T0;
		// Its grazing way at that clock, turned by its phase to a hair (0.4 mrad) short of opposite the herd's way.
		const float YawC0 = SlonkFauna::WalkerYaw(T0, Cu, Gd);
		Cu[SlonkFauna::Custom::Phase] = (float)((Hd - UE_DOUBLE_PI + 0.0004 - YawC0) / (2.0 * UE_DOUBLE_PI));
		const float YawC = SlonkFauna::WalkerYaw(T0, Cu, Gd);
		// Half way through its turn to walk, its legs stepping it round (a whole turn is PivotM of their walk).
		Gd[SlonkFauna::HerdData::Travel] = 0.5f;
		Gd[SlonkFauna::HerdData::SpeedMS] = 2.f;
		Gd[SlonkFauna::HerdData::PivotRef] = 100.f - (float)(0.5 * SlonkFauna::Walker::PivotM);
		double WorstCmS = 0.0;
		int32 Planted = 0;
		const float Dt = 1.f / 60.f;
		for (int32 f = 0; f < 4; ++f)
		{
			const FLinearColor VC(SlonkFauna::Vc::SurfaceBeak, 1.f, WalkerHoofPhase[f], SlonkFauna::Vc::JointLeg);
			const FVector3f LP = WalkerHooves[f];
			for (int32 k = 0; k < 12; ++k)
			{
				const float Ta = T0 + k * Dt, Tb = Ta + Dt;
				const bool bDown = FMath::Frac(SlonkFauna::WalkerStrides(Ta, Cu, Gd) - VC.B) < SlonkFauna::Walker::Duty - 0.02f && FMath::Frac(SlonkFauna::WalkerStrides(Tb, Cu, Gd) - VC.B) < SlonkFauna::Walker::Duty - 0.02f;
				if (!bDown) { continue; }
				++Planted;
				const FVector3f A = (LP + SlonkFauna::WalkerMotion(LP, LP * Size, VC, Ta, Cu, Gd, 2000.f, 960.f)) * Size;
				const FVector3f B = (LP + SlonkFauna::WalkerMotion(LP, LP * Size, VC, Tb, Cu, Gd, 2000.f, 960.f)) * Size;
				WorstCmS = FMath::Max(WorstCmS, (double)FVector3f::Dist2D(A, B) / Dt);
			}
		}
		AddInfo(FString::Printf(TEXT("a walker turning to walk from a hair short of opposite the herd's way (%.4f rad off the half turn, its way packed %.4f rad past it): %d planted hoof-steps, the worst sliding %.2f cm/s"),
			UE_DOUBLE_PI - FMath::Abs(FMath::UnwindRadians(Hd - YawC)), FMath::UnwindRadians(1234.0 * TurnStep - Hd), Planted, WorstCmS));
		TestTrue(TEXT("its body and its planted hooves turn the same way round through the half turn"), Planted > 4 && WorstCmS < 5.0);
	}

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
	TestFalse(TEXT("nothing built for a mote's plan"), SlonkFaunaMeshes::BuildLod(SlonkFauna::EarthPlan(EFaunaKind::Mote), 0, Nothing));
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
			SlonkFauna::StartleHerd(Panics[h], S, G, D, R, Craft, Now);
			SlonkFauna::StartleHerd(Panics[h], S, G, D, R, Walker, Now);
			const bool bMoving = SlonkFauna::KeepFromCrew(S, G, D, TArray<FVector>{ Walker.PosCm }, 1.0 / 60.0);
			const FVector Ahead = SlonkFauna::HerdFrightOffset(G, D, Panics[h], R, Now + 1.0) + S.Avoid + (bMoving ? S.AvoidWay : FVector::ZeroVector);
			float Gd[SlonkFauna::HerdData::Num] = {};
			SlonkFauna::StepHerd(S, G, R, Now, f == 0 ? 0.0 : 1.0 / 60.0, SlonkFauna::HerdFrightOffset(G, D, Panics[h], R, Now) + S.Avoid, Panics[h].IsLive(Now), bMoving,
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
	// their spacing), every second of the hour. And of every herd found, every walker's circle keeps to its half of the
	// room its nearest neighbour leaves it past the longest walker's length (so any two leave that length between them),
	// even its smallest circle of three strides: no middle is ever thrown closer than that allows.
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
	int32 AllWalkers = 0, Cramped = 0;
	double Room = TNumericLimits<double>::Max();
	for (const SlonkFauna::FFaunaGroup& G : Herds)
	{
		TArray<FTransform> Xf;
		TArray<float> Cu;
		SlonkFauna::MembersOf(G, Xf, Cu);
		const double BodyM = 1.1 * 1.15 * G.SpanM;
		for (int32 i = 0; i < Xf.Num(); ++i)
		{
			double GapM = 1.0e9;
			for (int32 j = 0; j < Xf.Num(); ++j) { if (j != i) { GapM = FMath::Min(GapM, FVector::Dist2D(Xf[i].GetTranslation(), Xf[j].GetTranslation()) / 100.0); } }
			const double Left = 0.5 * (GapM - BodyM) - FMath::Abs(Cu[i * SlonkFauna::Custom::Num + SlonkFauna::Custom::PerKind]);
			Room = FMath::Min(Room, Left);
			Cramped += Left < -1e-4 ? 1 : 0;
			++AllWalkers;
		}
	}
	AddInfo(FString::Printf(TEXT("the %d tightest herds (%d walkers, %d of them on circles of three strides) grazing through an hour: two walkers' middles at least %.2f of the longer one's length apart; %d times a pair closer than that"),
		Tried, Walkers, SmallCircles, Closest, Close));
	AddInfo(FString::Printf(TEXT("all %d herds (%d walkers): every circle within its room by %.3f m at the least, %d not"), Herds.Num(), AllWalkers, Room, Cramped));
	TestTrue(TEXT("herds to try"), Tried >= 4);
	TestEqual(TEXT("no two walkers ever closer than the longer one's length"), Close, 0);
	TestEqual(TEXT("every walker's circle within its room"), Cramped, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaHerdDustTest, "Slonk.Fauna.HerdDust", SLONK_TEST_FLAGS)
bool FSlonkFaunaHerdDustTest::RunTest(const FString& Parameters)
{
	// A herd raises dust going faster than a walk, by day, near the camera, on a living world: none as it grazes, stands
	// or walks, more the faster it goes and the drier its ground (two fifths as much on soaked ground), none by night, far
	// off or on a dead world; its
	// puffs the size its walkers are, drifting as it goes, paler than the ground's own colour. Its speed is smoothed and
	// switched with slack: a single fast frame (a hitch, its ground shifting under it) raises none, a trot does within half
	// a second, and once raised it is kept through a moment's slowing and let go once it walks again. Its puffs are born
	// moving with the ground as the world turns (User.Carry): none while the spin is held, and on a turning world the
	// ground's own velocity, omega cross r, as the planet's turn moves a point on it. The components it is drawn with are
	// lent out of a pool and come back to it, one given back resting as long as its last puffs live before it is lent
	// again. No Niagara asset is needed for any of it.
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
	TestTrue(TEXT("none on a dead world, by night or far off"), Dead == 0.f && Dark == 0.f && Far == 0.f);
	const float Driest = With([](SlonkFauna::FHerdDustInput& I) { I.Wet = 0.0; }).Rate;
	TestTrue(TEXT("some on soaked ground, two fifths of the driest's"), Soaked > 0.f && FMath::IsNearlyEqual(Soaked, 0.4f * Driest, 0.05f * Driest + 0.01f));
	TestTrue(TEXT("fewer at dusk, more on drier ground"), Dusk > 0.f && Dusk < Trot.Rate && Drier > Trot.Rate);
	TestTrue(TEXT("its puffs the walkers' size, its footprint the herd's, drifting as it goes"), Big.Size > Trot.Size && Trot.Radius == 2500.f && Trot.Drift.Equals(In.VelWorldCmS));
	TestTrue(TEXT("paler than the ground, fainter at night"), Luma(Trot.Color) > Luma(Ground) && Trot.Color.A > Night.A && Night.A > 0.f);

	// Its gate, frame by frame at 60 Hz: walking at 1.1 m/s with a hitch of a quarter of a second read as 1.6 m/s, and a
	// frame its ground shifted under it read as 1.3 m/s; then a trot at 2.4 m/s; a second's slowing to 1.1 m/s two seconds
	// into it; and walking again at 1.1 m/s.
	SlonkFauna::FDustGate Gate;
	double Clock = 0.0;
	auto GateFor = [&](double Speed, double Dt) { Clock += Dt; return SlonkFauna::StepDustGate(Gate, Speed, Dt, Clock); };
	bool bSpiked = false;
	for (int32 f = 0; f < 120; ++f) { bSpiked |= GateFor(1.1, 1.0 / 60.0); }
	bSpiked |= GateFor(1.6, 0.25);
	for (int32 f = 0; f < 60; ++f) { bSpiked |= GateFor(f == 30 ? 1.3 : 1.1, 1.0 / 60.0); }
	double OnAfter = -1.0;
	for (int32 f = 1; f <= 120; ++f) { if (GateFor(2.4, 1.0 / 60.0) && OnAfter < 0.0) { OnAfter = f / 60.0; } }
	bool bKept = true;
	for (int32 f = 0; f < 60; ++f) { bKept &= GateFor(1.1, 1.0 / 60.0); }
	double OffAfter = -1.0;
	for (int32 f = 1; f <= 600; ++f) { if (!GateFor(1.1, 1.0 / 60.0) && OffAfter < 0.0) { OffAfter = f / 60.0; } }
	AddInfo(FString::Printf(TEXT("its dust's gate: a hitch and a shift of its ground while it walks raise %s; a trot raises it after %.2f s; a second's slowing to a walk %s; walking again it is let go after %.2f s more"),
		bSpiked ? TEXT("some") : TEXT("none"), OnAfter, bKept ? TEXT("keeps it") : TEXT("lets it go"), OffAfter));
	TestFalse(TEXT("a single fast frame raises no dust"), bSpiked);
	TestTrue(TEXT("a trot raises it within half a second"), OnAfter > 0.0 && OnAfter < 0.5);
	TestTrue(TEXT("a moment's slowing keeps it"), bKept);
	TestTrue(TEXT("walking again lets it go"), OffAfter > 0.0 && OffAfter < 3.0);
	// A herd of the briskest walk (1.4 m/s) raises none at its walk, raises it at its trot, and lets it go once it walks again.
	SlonkFauna::FDustGate Brisk;
	double BriskClock = 0.0;
	auto BriskFor = [&](double Speed, double Dt) { BriskClock += Dt; return SlonkFauna::StepDustGate(Brisk, Speed, Dt, BriskClock); };
	bool bBriskWalk = false, bBriskTrot = false;
	for (int32 f = 0; f < 300; ++f) { bBriskWalk |= BriskFor(1.4, 1.0 / 60.0); }
	for (int32 f = 0; f < 120; ++f) { bBriskTrot |= BriskFor(2.4, 1.0 / 60.0); }
	double BriskOff = -1.0;
	for (int32 f = 1; f <= 600; ++f) { if (!BriskFor(1.4, 1.0 / 60.0) && BriskOff < 0.0) { BriskOff = f / 60.0; } }
	AddInfo(FString::Printf(TEXT("the briskest walker (1.4 m/s): %s at its walk; its trot %s; walking again it is let go after %.2f s"), bBriskWalk ? TEXT("dust") : TEXT("none"),
		bBriskTrot ? TEXT("raises it") : TEXT("raises none"), BriskOff));
	TestTrue(TEXT("the briskest walker lets its dust go once it walks again"), !bBriskWalk && bBriskTrot && BriskOff > 0.0 && BriskOff < 3.0);
	// Its speed read over the time that passed: walking at its walk, a frame of 0.4 s the game steps as its quarter-second
	// most (UPlanetFaunaComponent::UpdateView) still reads its walk, not 1.6 times it.
	{
		const SlonkFauna::FFaunaWorld W = EarthHerds();
		SlonkFauna::FFaunaGroup G;
		if (TestHerd(W, G))
		{
			const double R = W.P.RadiusCm;
			auto Height = [&W](const FVector& Dir) { return FMath::Max(SlonkTerrain::Sample(W.P, Dir, 2000.0).HeightCm, 0.0); };
			const double Start = 3.0 * G.CycleSec + G.WalkA + G.RampSec + 5.0;
			SlonkFauna::FHerdState S;
			SlonkFauna::StartHerd(S, G, Start);
			double Now = Start, Steady = 0.0;
			for (int32 f = 0; f <= 120; ++f)
			{
				Now = Start + f / 60.0;
				SlonkFauna::StepHerd(S, G, R, Now, f == 0 ? 0.0 : 1.0 / 60.0, FVector::ZeroVector, false, false, f % 6 == 0, Height, 0.0, nullptr);
				Steady = S.SpeedMS;
			}
			SlonkFauna::StepHerd(S, G, R, Now + 0.4, 0.25, FVector::ZeroVector, false, false, true, Height, 0.0, nullptr);
			AddInfo(FString::Printf(TEXT("walking at %.2f m/s (its walk %.2f), a frame of 0.4 s stepped as 0.25 s reads %.2f m/s"), Steady, G.WalkCmS / 100.0, S.SpeedMS));
			TestTrue(TEXT("a long frame reads the speed it walks"), FMath::Abs(S.SpeedMS - Steady) < 0.03 * Steady && FMath::Abs(Steady * 100.0 - G.WalkCmS) < 0.05 * G.WalkCmS);
		}
	}

	// The ground's own velocity: none while the world holds its spin or does not turn; on a world turning a day of 24
	// hours at the clock's x60, tilted, a point 6,371 km from its middle moves as the planet's turn (its rotation run on
	// as APlanetBody::Tick runs it, by a yaw of its spin in its own frame) moves it.
	const FQuat Tilt = FQuat(FVector(1.0, 0.3, 0.0).GetSafeNormal(), FMath::DegreesToRadians(23.4)) * FQuat(FVector::ZAxisVector, 0.7);
	const FVector Centre(1.0e8, -3.0e7, 5.0e6);
	const float Spin = 0.0042f;
	const double Scale = 60.0;
	const FVector Local = FVector(0.3, -0.8, 0.52).GetSafeNormal() * 6.371e8;
	const FVector At0 = Centre + Tilt.RotateVector(Local);
	const FVector Held = SlonkFauna::GroundCarryCmS(Tilt.GetUpVector(), Centre, Spin, Scale, true, At0);
	const FVector Still = SlonkFauna::GroundCarryCmS(Tilt.GetUpVector(), Centre, 0.0, Scale, false, At0);
	const FVector Carry = SlonkFauna::GroundCarryCmS(Tilt.GetUpVector(), Centre, Spin, Scale, false, At0);
	constexpr double Step = 1.0e-3;
	const FQuat Turned = Tilt * FQuat(FRotator(0.f, (float)(Spin * Scale * Step), 0.f));
	const FVector Moved = (Centre + Turned.RotateVector(Local) - At0) / Step;
	const double Want = FMath::DegreesToRadians(Spin * Scale) * FVector::CrossProduct(Tilt.GetUpVector(), At0 - Centre).Size();
	AddInfo(FString::Printf(TEXT("the ground's own velocity: %.3f cm/s with the spin held, %.3f on a world that does not turn; on a turning world %.1f m/s (omega r cos latitude %.1f m/s), %.4f of it off the way the planet's turn moves the point (%.1f m/s)"),
		Held.Size(), Still.Size(), Carry.Size() / 100.0, Want / 100.0, (Carry - Moved).Size() / FMath::Max(Moved.Size(), 1.0), Moved.Size() / 100.0));
	TestTrue(TEXT("no carry while the spin is held, nor on a world that does not turn"), Held.IsZero() && Still.IsZero());
	TestTrue(TEXT("the ground's velocity, omega cross r, the way and as fast as the turn moves it"), Carry.Size() > 1.0e4 && FMath::IsNearlyEqual(Carry.Size(), Want, Want * 1e-6)
		&& (Carry - Moved).Size() < 1e-3 * Moved.Size());
	In.CarryWorldCmS = Carry;
	TestTrue(TEXT("and handed to the asset as it is"), SlonkFauna::HerdDustFor(In).Carry.Equals(Carry));
	// Over a frame of a sixtieth of a second, all of it; over one of 0.2 s (the planet's turn held to 0.1 s of it, the
	// emitter's tick to Niagara's 0.125 s), a puff born with it goes as far over the tick as the ground turned in the frame.
	const FVector Short = SlonkFauna::GroundCarryCmS(Tilt.GetUpVector(), Centre, Spin, Scale, false, At0, 1.0 / 60.0, 0.125);
	const FVector Long = SlonkFauna::GroundCarryCmS(Tilt.GetUpVector(), Centre, Spin, Scale, false, At0, 0.2, 0.125);
	const FQuat TurnedLong = Tilt * FQuat(FRotator(0.f, (float)(Spin * Scale * UGameClockSubsystem::MaxFrameSeconds), 0.f));
	const FVector GroundWent = Centre + TurnedLong.RotateVector(Local) - At0;
	AddInfo(FString::Printf(TEXT("over a frame of 0.2 s the ground turns %.2f m, a puff born with the carry goes %.2f m over the emitter's tick (%.2f m with the spin's own rate)"),
		GroundWent.Size() / 100.0, Long.Size() * 0.125 / 100.0, Carry.Size() * 0.125 / 100.0));
	TestTrue(TEXT("over a long frame the puffs go no further than the ground"), Short.Equals(Carry, 1e-6 * Carry.Size()) && (Long * 0.125 - GroundWent).Size() < 1e-3 * GroundWent.Size());

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

namespace
{
	/** That world with its seas as the terrain material paints them on a living world (SeaLifeOf's shoals) and the style's
	 *  swell. */
	SlonkFauna::FFaunaWorld EarthSeas(float Life = 1.f)
	{
		SlonkFauna::FFaunaWorld W = EarthFauna(Life);
		W.Sea.ShoalDensity = SlonkFauna::SeaLifeOf(Life, SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 0.f, 7919u)).ShoalDensity;
		W.Sea.SwellCm = 120.f;
		return W;
	}

	/** The open sea's depth at a direction as a group of leapers reads its water (the field's at ten metres): -1 off it. */
	double SeaDepthAt(const FPlanetTerrainParams& P, const FVector& Dir)
	{
		const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir.GetSafeNormal(), 1000.0);
		return S.bWater && FMath::Abs(S.WaterCm) < 1.0 && S.River < 0.05f && S.Ice < 0.3f ? S.WaterCm - S.GroundCm : -1.0;
	}

	/** Directions over the open sea at least MinDepthCm deep, of N spread over the globe: at most Count of them, taken
	 *  evenly from all there are (so from every latitude). */
	TArray<FVector> SeaSpots(const FPlanetTerrainParams& P, int32 N, double MinDepthCm, int32 Count)
	{
		TArray<FVector> All;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector Dir = Spread(i, N);
			if (SeaDepthAt(P, Dir) > MinDepthCm) { All.Add(Dir); }
		}
		if (All.Num() <= Count) { return All; }
		TArray<FVector> Out;
		for (int32 k = 0; k < Count; ++k) { Out.Add(All[(int32)((int64)k * All.Num() / Count)]); }
		return Out;
	}

	/** A group of leapers to test with: the nearest to the first deep sea of the spread directions that has one near. */
	bool TestLeapers(const SlonkFauna::FFaunaWorld& W, SlonkFauna::FFaunaGroup& Out)
	{
		for (const FVector& Dir : SeaSpots(W.P, 400, 5.0e3, 8))
		{
			if (SlonkFauna::FindGroupNear(W, EFaunaKind::Leaper, Dir, 3.0e5, 6000, Out)) { return true; }
		}
		return false;
	}

	/** Groups of leapers in the cells round deep sea spread over the globe, at most Count. */
	void LeaperGroupsOverSeas(const SlonkFauna::FFaunaWorld& W, int32 Count, TArray<SlonkFauna::FFaunaGroup>& Out)
	{
		Out.Reset();
		TSet<uint64> Seen;
		for (const FVector& Dir : SeaSpots(W.P, 1200, 5.0e3, 40))
		{
			TArray<SlonkFauna::FFaunaCell> Cells;
			SlonkFauna::CellsNear(W.P, EFaunaKind::Leaper, Dir, 6.0e4, Cells);
			for (const SlonkFauna::FFaunaCell& C : Cells)
			{
				bool bAlready = false;
				Seen.Add(C.Id(), &bAlready);
				SlonkFauna::FFaunaGroup G;
				if (!bAlready && SlonkFauna::GroupInCell(W, C, G)) { Out.Add(MoveTemp(G)); }
				if (Out.Num() >= Count) { return; }
			}
		}
	}

	/** A mesh's vertex colours by vertex (its first element's). */
	TArray<FLinearColor> VertexColours(const UE::Geometry::FDynamicMesh3& M)
	{
		TArray<FLinearColor> Out;
		Out.Init(FLinearColor(0.f, 0.f, 0.5f, 0.f), M.MaxVertexID());
		TArray<bool> Done;
		Done.Init(false, M.MaxVertexID());
		const UE::Geometry::FDynamicMeshColorOverlay* C = M.Attributes()->PrimaryColors();
		for (const int32 El : C->ElementIndicesItr())
		{
			const int32 V = C->GetParentVertex(El);
			if (V >= 0 && !Done[V]) { const FVector4f Cv = C->GetElement(El); Out[V] = FLinearColor(Cv.X, Cv.Y, Cv.Z, Cv.W); Done[V] = true; }
		}
		return Out;
	}

	/** The material's clock at a moment of the world's. */
	float MaterialTime(double WorldSec) { return (float)FMath::Fmod(WorldSec, SlonkFauna::ClockPeriodSec); }

	/**
	 * The terrain material's swell at a planet-local point on its sea (its WPO_CODE's plane of the projection and
	 * SlonkGerstner), transliterated in floats as the GPU runs it from the vertex's noise-space point (the point less whole
	 * periods): the displacement along the plane's two axes and up, cm, at full strength. A hand copy: Slonk.Fauna.LeaperArcs
	 * ties it to the script by the CRC of the code it copies (TerrainSwellBlock).
	 */
	FVector3f MaterialSwell(const FVector& Local, float Time, float SwellCm, int32* OutIu = nullptr, int32* OutIv = nullptr)
	{
		const FVector Pn = SlonkScatter::ToNoiseSpace(Local);
		const FVector3f p((float)Pn.X, (float)Pn.Y, (float)Pn.Z);
		const FVector Ud = Local.GetSafeNormal();
		const FVector3f aU(FMath::Abs((float)Ud.X), FMath::Abs((float)Ud.Y), FMath::Abs((float)Ud.Z));
		FVector2f pu;
		int32 Iu, Iv;
		if (aU.X >= aU.Y && aU.X >= aU.Z) { pu = FVector2f(p.Y, p.Z); Iu = 1; Iv = 2; }
		else if (aU.Y >= aU.Z) { pu = FVector2f(p.X, p.Z); Iu = 0; Iv = 2; }
		else { pu = FVector2f(p.X, p.Y); Iu = 0; Iv = 1; }
		if (OutIu) { *OutIu = Iu; }
		if (OutIv) { *OutIv = Iv; }
		const FVector2f K[4] = { FVector2f(100.f, 70.f), FVector2f(180.f, -60.f), FVector2f(0.f, 256.f), FVector2f(-300.f, 400.f) };
		const float A[4] = { 0.55f, 0.3f, 0.25f, 0.12f };
		const float Q = 0.55f;
		FVector3f Disp(0.f);
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector2f k = K[i] * (6.2831853f / 1048576.f);
			const float kl = k.Size();
			const FVector2f d = k / kl;
			const float w = FMath::Sqrt(981.f * kl);
			const float ph = (k.X * pu.X + k.Y * pu.Y) - w * Time;
			const float a = A[i] * SwellCm;
			const float sn = FMath::Sin(ph), cs = FMath::Cos(ph);
			Disp.X += d.X * (Q * a * cs);
			Disp.Y += d.Y * (Q * a * cs);
			Disp.Z += a * sn;
		}
		return Disp;
	}

	/** Where the terrain material's WPO_CODE carries a vertex of its sea at a planet-local point on the sea-level sphere,
	 *  its swell faded by Fade: along its plane's two fixed axes (Tw and Bw) and up its own radial (U), from MaterialSwell. */
	FVector MaterialSeaVertex(const FVector& Rest, float Time, float SwellCm, float Fade)
	{
		int32 Iu = 0, Iv = 0;
		const FVector3f D = MaterialSwell(Rest, Time, SwellCm, &Iu, &Iv);
		FVector Off = Rest.GetSafeNormal() * (double)D.Z;
		Off[Iu] += D.X;
		Off[Iv] += D.Y;
		return Rest + Off * Fade;
	}

	/** The terrain material's drawn sea over the sea-level sphere along the radial through a planet-local point, cm: the
	 *  vertex its swell carries onto that radial (found to convergence) and how high over the sphere it is carried there.
	 *  Out the residual (how far off the radial that carried vertex still is, cm) and the swell's own lift up that vertex's
	 *  radial alone (its sway's left out), cm. */
	double DrawnSeaCm(const FVector& Local, double RadiusCm, float Time, float SwellCm, float Fade = 1.f, double* OutResidualCm = nullptr, double* OutLiftCm = nullptr)
	{
		const FVector U = Local.GetSafeNormal();
		FVector B = U;
		FVector V = MaterialSeaVertex(B * RadiusCm, Time, SwellCm, Fade);
		for (int32 It = 0; It < 16; ++It)
		{
			// Moved back by how far the vertex it carries lands off the radial.
			B = (B + (U - V.GetSafeNormal())).GetSafeNormal();
			V = MaterialSeaVertex(B * RadiusCm, Time, SwellCm, Fade);
		}
		if (OutResidualCm) { *OutResidualCm = (V.GetSafeNormal() - U).Size() * RadiusCm; }
		if (OutLiftCm) { *OutLiftCm = MaterialSwell(B * RadiusCm, Time, SwellCm).Z * Fade; }
		return V.Size() - RadiusCm;
	}

	/** The code MaterialSwell copies, as terrain_material.py has it now: its SlonkGerstner function and its WPO_CODE, each
	 *  line trimmed, the blank ones and the comments left out. Empty if the script or either is not found. */
	FString TerrainSwellBlock()
	{
		FString Py;
		if (!FFileHelper::LoadFileToString(Py, *FPaths::Combine(FPaths::ProjectDir(), TEXT("../tools/planet_materials/terrain_material.py")))) { return FString(); }
		TArray<FString> Lines;
		Py.ParseIntoArrayLines(Lines, false);
		FString Gerstner, Wpo;
		bool bGerstner = false, bWpo = false, bGerstnerDone = false, bWpoDone = false;
		for (FString& L : Lines)
		{
			L.TrimStartAndEndInline();
			if (!bGerstner && !bGerstnerDone && L.StartsWith(TEXT("void SlonkGerstner("))) { bGerstner = true; }
			if (bGerstner)
			{
				if (!L.IsEmpty() && !L.StartsWith(TEXT("//"))) { Gerstner += L + TEXT("\n"); }
				if (L == TEXT("}")) { bGerstner = false; bGerstnerDone = true; }
				continue;
			}
			if (!bWpo && !bWpoDone && L.StartsWith(TEXT("WPO_CODE = r\"\"\""))) { bWpo = true; continue; }
			if (bWpo)
			{
				if (L.StartsWith(TEXT("\"\"\""))) { bWpo = false; bWpoDone = true; continue; }
				if (!L.IsEmpty() && !L.StartsWith(TEXT("//"))) { Wpo += L + TEXT("\n"); }
			}
		}
		return bGerstnerDone && bWpoDone ? Gerstner + Wpo : FString();
	}
	/** Its CRC when MaterialSwell and SlonkFauna::SwellAt were last brought in line with it. */
	constexpr uint32 TerrainSwellBlockCrc = 0xd19fff79u;

	/** A group of leapers' fish as its set is given them: their custom floats and their homes in its frame. */
	struct FFish
	{
		TArray<FTransform> Xf;
		TArray<float> Custom;
		TArray<FVector> Home;
		int32 Num() const { return Xf.Num(); }
		const float* At(int32 i) const { return Custom.GetData() + i * SlonkFauna::Custom::Num; }
	};
	FFish FishOf(const SlonkFauna::FFaunaGroup& G)
	{
		FFish F;
		SlonkFauna::MembersOf(G, F.Xf, F.Custom);
		for (const FTransform& T : F.Xf) { F.Home.Add(T.GetLocation()); }
		return F;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaLeaperHabitatTest, "Slonk.Fauna.LeaperHabitat", SLONK_TEST_FLAGS)
bool FSlonkFaunaLeaperHabitatTest::RunTest(const FString& Parameters)
{
	// Leaping fish live only in the shoals the terrain material paints: a group's cell holds a shoal (the material's float
	// copy draws it where its group says it swims), the group in its own cell, one group to a shoal, in about half the
	// shoals; over the open sea all round its leaps, at least 3 m deep and 3 m deeper than its shoal swims (so never on
	// land, nor in the shallows: the coasts' shoals over shallow water hold none); and none on a lifeless, airless or
	// molten world, nor in a sea whose shoals are not painted.
	const SlonkFauna::FFaunaWorld W = EarthSeas();
	const double R = W.P.RadiusCm;
	// The shoals' cells round points of sea spread over the globe, the shallows by the coasts among them; and round points of
	// sea on the seams of the projection (where a direction's two largest axes are as large), where the material's shoals
	// and swell change their plane.
	TArray<SlonkFauna::FFaunaCell> Cells;
	int32 SeamSpots = 0;
	{
		TArray<FVector> Spots = SeaSpots(W.P, 4000, 0.0, 240);
		const int32 Spread0 = Spots.Num();
		for (int32 i = 0; i < 6000 && SeamSpots < 120; ++i)
		{
			FVector Dir = Spread(i, 6000);
			const FVector A = Dir.GetAbs();
			const int32 Big = A.X >= A.Y && A.X >= A.Z ? 0 : (A.Y >= A.Z ? 1 : 2);
			const int32 Next = Big == 0 ? (A.Y >= A.Z ? 1 : 2) : (Big == 1 ? (A.X >= A.Z ? 0 : 2) : (A.X >= A.Y ? 0 : 1));
			const double M = 0.5 * (A[Big] + A[Next]);
			Dir[Big] = FMath::Sign(Dir[Big]) * M;
			Dir[Next] = FMath::Sign(Dir[Next]) * M;
			Dir = Dir.GetSafeNormal();
			if (SeaDepthAt(W.P, Dir) > 1000.0 && i % 7 == 0) { Spots.Add(Dir); ++SeamSpots; }
		}
		TSet<uint64> Seen;
		for (int32 s = 0; s < Spots.Num(); ++s)
		{
			TArray<SlonkFauna::FFaunaCell> Near;
			SlonkFauna::CellsNear(W.P, EFaunaKind::Leaper, Spots[s], s < Spread0 ? 6.0e4 : 3.0e4, Near);
			for (const SlonkFauna::FFaunaCell& C : Near)
			{
				bool bAlready = false;
				Seen.Add(C.Id(), &bAlready);
				if (!bAlready) { Cells.Add(C); }
			}
		}
	}
	int32 OffPlane = 0;
	int32 Shoals = 0, OnePlane = 0, Gated = 0, TurnedAway = 0, OverLand = 0, OverShallows = 0, Groups = 0, NoShoal = 0, NotTheirs = 0, Unpainted = 0, Shallow = 0, Twice = 0;
	int32 Fish = 0, FishMin = 99, FishMax = 0;
	double LeastWater = TNumericLimits<double>::Max(), LeastClear = TNumericLimits<double>::Max();
	TSet<FIntVector> Claimed;
	for (const SlonkFauna::FFaunaCell& C : Cells)
	{
		const SlonkFauna::FLeaperHabitatRead H = SlonkFauna::LeaperHabitatAt(W, C);
		SlonkFauna::FFaunaGroup G;
		const bool bGroup = SlonkFauna::GroupInCell(W, C, G);
		Shoals += H.bShoal ? 1 : 0;
		OnePlane += H.bShoal && H.bOnePlane ? 1 : 0;
		Gated += H.bShoal && H.bOnePlane && H.bGate ? 1 : 0;
		if (H.bShoal && H.bOnePlane && H.bGate && !H.bSuits)
		{
			++TurnedAway;
			OverLand += H.NotSea > 0 ? 1 : 0;
			OverShallows += H.NotSea == 0 && H.Shallow > 0 ? 1 : 0;
		}
		if (!bGroup) { continue; }
		++Groups;
		Fish += G.Members;
		FishMin = FMath::Min(FishMin, G.Members);
		FishMax = FMath::Max(FishMax, G.Members);
		// Its shoal where the material paints it: ShoalAt at its middle, through a day, finds the loop it swims round, in its
		// own cell; and the material's float copy of the terrain's shoals draws it where it swims then.
		NotTheirs += SlonkFauna::CellOf(W.P, EFaunaKind::Leaper, G.Centre).Id() == C.Id() ? 0 : 1;
		for (const double T : { 0.0, 777.7, 4321.0, 86000.0 })
		{
			SlonkFauna::FShoal S;
			if (!SlonkFauna::ShoalAt(G.Centre * R, R, W.Sea.ShoalDensity, T, S) || !S.LoopCentreCm.Equals(G.Centre * R, 1.0)) { ++NoShoal; continue; }
			const float Shape = MaterialShoalShape(S.CentreCm, W.Sea.ShoalDensity, (float)T);
			Unpainted += Shape >= 0.f && Shape < 0.5f ? 0 : 1;
		}
		// The open sea all round its leaps, deep enough: 25 points from its loop's middle out to its loop and its fish's reach,
		// read afresh (not as its own read had them).
		const double Need = FMath::Max(SlonkFauna::Leaper::MinWaterCm, G.ShoalDepthCm + SlonkFauna::Leaper::ShoalClearCm);
		for (int32 Ring = 0; Ring <= 3; ++Ring)
		{
			const int32 Around = Ring == 0 ? 1 : 8;
			for (int32 k = 0; k < Around; ++k)
			{
				const FVector Dir = Ring == 0 ? G.Centre : AlongGround(G.Centre, G.ExtentCm() * Ring / 3.0, R, UE_DOUBLE_TWO_PI * (k + 0.5 * (Ring % 2)) / Around);
				const double Depth = SeaDepthAt(W.P, Dir);
				Shallow += Depth >= Need ? 0 : 1;
				// And all of it on its own cell's plane of the projection, on its side.
				OffPlane += SlonkFauna::ProjectionAxis(Dir) == C.Face / 2 && (Dir[C.Face / 2] < 0.0) == ((C.Face & 1) != 0) ? 0 : 1;
				LeastWater = FMath::Min(LeastWater, Depth);
				LeastClear = FMath::Min(LeastClear, Depth - G.ShoalDepthCm);
			}
		}
		// One group to a shoal.
		bool bAlready = false;
		Claimed.Add(FIntVector((int32)FMath::RoundToDouble(G.Centre.X * R / 100.0), (int32)FMath::RoundToDouble(G.Centre.Y * R / 100.0), (int32)FMath::RoundToDouble(G.Centre.Z * R / 100.0)), &bAlready);
		Twice += bAlready ? 1 : 0;
	}
	// Where the cell holds no shoal of its own, no group.
	int32 WithoutShoal = 0;
	for (const SlonkFauna::FFaunaCell& C : Cells)
	{
		FVector Mid;
		SlonkFauna::FShoal S;
		if (!SlonkFauna::CellMiddle(W.P, C, Mid) || !SlonkFauna::ShoalAt(Mid * R, R, W.Sea.ShoalDensity, 0.0, S))
		{
			SlonkFauna::FFaunaGroup G;
			WithoutShoal += SlonkFauna::GroupInCell(W, C, G) ? 1 : 0;
		}
	}
	AddInfo(FString::Printf(TEXT("%d cells of the shoals' lattice round the seas: %d hold a painted shoal (%d wholly on their plane), %d of those in the leapers' share; %d groups of %d fish (%d to %d a group), %d turned away (%d over land, ice or a river, %d over the shallows)"),
		Cells.Num(), Shoals, OnePlane, Gated, Groups, Fish, FishMin, FishMax, TurnedAway, OverLand, OverShallows));
	AddInfo(FString::Printf(TEXT("the groups: %d not in their own cell, %d times their shoal not found where they swim and %d not painted there by the material's copy, %d points of their water read afresh shallower than they need (the least %.1f m, %.1f m past their shoal's depth), %d off their plane of the projection (%d points of sea on its seams among those read round), %d shoals with two groups; %d groups in cells without a shoal"),
		NotTheirs, NoShoal, Unpainted, Shallow, LeastWater / 100.0, LeastClear / 100.0, OffPlane, SeamSpots, Twice, WithoutShoal));
	TestTrue(TEXT("shoals across a seam of the projection turned away"), SeamSpots > 50 && OnePlane < Shoals);
	TestEqual(TEXT("each group's leaps wholly on its own plane of the projection, where the material's swell is the same plane's"), OffPlane, 0);
	TestTrue(TEXT("enough shoals and groups to judge by"), Shoals > 500 && Groups > 100);
	TestTrue(TEXT("about half the shoals that may hold leapers do"), Gated > 0 && FMath::Abs((double)Gated / FMath::Max(OnePlane, 1) - SlonkFauna::Leaper::ShoalShare) < 0.08);
	TestTrue(TEXT("shoals over the shallows and the shore turned away"), OverShallows > 0 && OverLand > 0);
	TestEqual(TEXT("each group in its own cell"), NotTheirs, 0);
	TestEqual(TEXT("each group's shoal found where it swims"), NoShoal, 0);
	TestEqual(TEXT("and painted there by the terrain material"), Unpainted, 0);
	TestEqual(TEXT("the open sea all round their leaps, at least 3 m deep and 3 m deeper than their shoal swims"), Shallow, 0);
	TestTrue(TEXT("never on land, never in the shallows"), LeastWater >= SlonkFauna::Leaper::MinWaterCm && LeastClear >= SlonkFauna::Leaper::ShoalClearCm);
	TestEqual(TEXT("one group to a shoal"), Twice, 0);
	TestEqual(TEXT("none where no shoal is painted"), WithoutShoal, 0);
	TestTrue(TEXT("one to five fish a group"), FishMin >= 1 && FishMax <= SlonkFauna::Leaper::MaxPerGroup);

	// No leapers on a world where nothing lives, without air, with molten seas (none of whose shoals are painted), or with
	// the kind switched off.
	FWorldGenome Molten = SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 0.f, 7919u);
	Molten.Solvent = ESolvent::Magma;
	SlonkFauna::FFaunaWorld Dead = EarthSeas(0.f), Airless = EarthSeas(), Magma = EarthSeas(), Off = EarthSeas();
	Airless.P.bAirless = true;
	Magma.Sea.ShoalDensity = SlonkFauna::SeaLifeOf(1.f, Molten).ShoalDensity;
	Off.KindMask = SlonkFauna::AllKinds & ~SlonkFauna::KindBit(EFaunaKind::Leaper);
	int32 InDead = 0, InAirless = 0, InMagma = 0, InOff = 0;
	for (int32 i = 0; i < Cells.Num(); i += 7)
	{
		SlonkFauna::FFaunaGroup G;
		InDead += SlonkFauna::GroupInCell(Dead, Cells[i], G) ? 1 : 0;
		InAirless += SlonkFauna::GroupInCell(Airless, Cells[i], G) ? 1 : 0;
		InMagma += SlonkFauna::GroupInCell(Magma, Cells[i], G) ? 1 : 0;
		InOff += SlonkFauna::GroupInCell(Off, Cells[i], G) ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("of a seventh of those cells: %d groups on a lifeless world, %d without air, %d in molten seas (shoal density %.2f), %d with the kind off"), InDead, InAirless, InMagma, Magma.Sea.ShoalDensity, InOff));
	TestTrue(TEXT("no leapers on a lifeless, airless or molten world, nor with the kind off"), InDead == 0 && InAirless == 0 && InMagma == 0 && InOff == 0 && !Magma.Spawns(EFaunaKind::Leaper));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaLeaperArcsTest, "Slonk.Fauna.LeaperArcs", SLONK_TEST_FLAGS)
bool FSlonkFaunaLeaperArcsTest::RunTest(const FString& Parameters)
{
	// A fish's leaps, in closed form (SlonkFauna::LeapAt, LeapPoseAt) and as the material draws them (its float copy,
	// LeaperMotion and LeaperShown), agree: it is drawn only from just under the sea as it comes up to just under it as it
	// dives back in, leaving the water at the sea's drawn surface where and when it crosses it and diving back in at the
	// surface there and then, never drawn above the water outside a leap nor below the shallowest sea floor it may live
	// over; the splashes come where and when its leaps cross the surface; every cycle divides the hour and the hour's wrap
	// shows no seam; the fish of a group leap a few seconds apart, irregularly. The swell the crossings are read from is the
	// terrain material's own.
	const SlonkFauna::FFaunaWorld W = EarthSeas();
	const double R = W.P.RadiusCm;
	const double Swell = W.Sea.SwellCm;
	TArray<SlonkFauna::FFaunaGroup> Groups;
	LeaperGroupsOverSeas(W, 30, Groups);
	if (Groups.Num() < 10) { AddError(TEXT("too few groups of leapers found")); return false; }
	using namespace UE::Geometry;
	FDynamicMesh3 Lod0;
	SlonkFaunaMeshes::BuildLod(SlonkFauna::EarthPlan(EFaunaKind::Leaper), 0, Lod0);
	const TArray<FLinearColor> Vcs = VertexColours(Lod0);
	const FLinearColor Middle(0.f, 0.f, 0.5f, SlonkFauna::Vc::JointUndulate);
	int32 Tried = 0, ShownDisagree = 0, Off = 0, NotAtSurface = 0, CrossingOff = 0, AboveOutside = 0, PokesOut = 0, BelowFloor = 0, Leaps = 0, Crossings = 0, OffDrawnSea = 0;
	double WorstOffCm = 0.0, WorstSurfaceCm = 0.0, WorstCrossCm = 0.0, WorstDrawnSeaCm = 0.0, Lowest = 0.0, Highest = -1.0e9, HiddenHighest = -1.0e9;
	FRandomStream Rng(808);
	SlonkFauna::FPanic NoDive;
	for (const SlonkFauna::FFaunaGroup& G : Groups)
	{
		const FFish F = FishOf(G);
		SlonkFauna::FLeaperSurfaceCache Cache;
		for (int32 i = 0; i < F.Num(); ++i)
		{
			const float* C = F.At(i);
			const float S = C[SlonkFauna::Custom::SizeM];
			// Cycles of the first two hours (and so across the hour's wrap), and a few of a day later.
			for (int32 Pick = 0; Pick < 14; ++Pick)
			{
				const double Cyc = C[SlonkFauna::Custom::RateHz];
				const int64 N = Pick < 4 ? (int64)FMath::RoundToDouble(3600.0 / Cyc) - 2 + Pick : (Pick < 10 ? Rng.RandRange(0, 480) : 86400 / (int64)Cyc + Rng.RandRange(0, 100));
				const SlonkFauna::FLeap L = SlonkFauna::LeapOf(C, N);
				if (!L.bLeaps) { continue; }
				++Leaps;
				// Moments before it is drawn, through its leap (its two crossings among them) and after.
				TArray<double> Ts = { L.T0 - L.LeadSec - 0.02, L.T0 - L.LeadSec + 0.002, L.T0, L.T1(), L.T1() + L.LeadSec - 0.002, L.T1() + L.LeadSec + 0.02 };
				for (int32 k = 0; k < 6; ++k) { Ts.Add(L.T0 - L.LeadSec + (L.FlightSec + 2.0 * L.LeadSec) * Rng.FRand()); }
				for (const double T : Ts)
				{
					++Tried;
					float Gd[SlonkFauna::LeaperData::Num] = {};
					SlonkFauna::LeaperGroupData(G, F.Custom, F.Home, R, Swell, 1.0, T, NoDive, Cache, Gd);
					const float Tm = MaterialTime(T);
					const bool bShown = SlonkFauna::LeaperShown(Tm, C, Gd);
					const bool bDrawn = L.DrawnAt(T);
					const bool bNearEdge = FMath::Abs(T - (L.T0 - L.LeadSec)) < 0.005 || FMath::Abs(T - (L.T1() + L.LeadSec)) < 0.005;
					ShownDisagree += bShown == bDrawn || bNearEdge ? 0 : 1;
					const double H0 = Gd[SlonkFauna::LeaperData::Surface0 + 2 * i], H1 = Gd[SlonkFauna::LeaperData::Surface0 + 2 * i + 1];
					const SlonkFauna::FLeapPose P = SlonkFauna::LeapPoseAt(L, H0, H1, T);
					// The material's fish's middle (its mesh's origin) in its group's frame, cm from its home.
					const FVector Mid = (FVector)SlonkFauna::LeaperMotion(FVector3f::ZeroVector, Middle, Tm, C, Gd, 2000.f, 960.f) * S;
					// Every vertex of it, in its group's frame over its home (the sea's level at z 0).
					double Top = -1.0e9, Bottom = 1.0e9;
					for (const int32 V : Lod0.VertexIndicesItr())
					{
						const FVector3f LP = (FVector3f)Lod0.GetVertex(V);
						const FVector3f Moved = LP + SlonkFauna::LeaperMotion(LP, Vcs[V], Tm, C, Gd, 2000.f, 960.f);
						Top = FMath::Max(Top, (double)Moved.Z * S);
						Bottom = FMath::Min(Bottom, (double)Moved.Z * S);
					}
					Lowest = FMath::Min(Lowest, Bottom);
					BelowFloor += Bottom >= -SlonkFauna::Leaper::MinWaterCm ? 0 : 1;
					if (!bShown)
					{
						// Masked, and held under the sea however low its surface stands.
						HiddenHighest = FMath::Max(HiddenHighest, Top);
						AboveOutside += Top < -SlonkFauna::Leaper::MaxSurfaceCm ? 0 : 1;
						continue;
					}
					Highest = FMath::Max(Highest, Top);
					const double OffCm = (Mid - P.PosCm).Size();
					WorstOffCm = FMath::Max(WorstOffCm, OffCm);
					Off += OffCm < 0.5 ? 0 : 1;
					// At the very start and end of its drawing, all of it under the line between the surfaces it crosses.
					if (FMath::Abs(T - (L.T0 - L.LeadSec)) < 0.01 || FMath::Abs(T - (L.T1() + L.LeadSec)) < 0.01)
					{
						PokesOut += Top <= FMath::Lerp(H0, H1, P.Share) + 0.5 ? 0 : 1;
					}
					// At its crossings, at the surface there: and where LeapCrossing (the splash's place) says it crosses.
					for (const bool bEntry : { false, true })
					{
						if (T != (bEntry ? L.T1() : L.T0)) { continue; }
						const double Want = bEntry ? H1 : H0;
						WorstSurfaceCm = FMath::Max(WorstSurfaceCm, FMath::Abs(Mid.Z - Want));
						NotAtSurface += FMath::Abs(Mid.Z - Want) < 0.5 ? 0 : 1;
						const FVector2D Off2 = bEntry ? L.ToCm() : L.FromCm;
						NotAtSurface += FVector2D(Mid.X, Mid.Y).Equals(Off2, 0.5) ? 0 : 1;
						double Surf = 0.0;
						const FVector Splash = SlonkFauna::LeapCrossing(G, R, Swell, 1.0, F.Home[i], L, bEntry, &Surf);
						const FVector Drawn = SlonkFauna::LeaperFramePoint(G, R, T, F.Home[i] + FVector(Mid.X, Mid.Y, 0.0));
						const FVector DrawnOnSea = Drawn.GetSafeNormal() * (R + Mid.Z);
						const double Cross = (Splash - DrawnOnSea).Size();
						WorstCrossCm = FMath::Max(WorstCrossCm, Cross);
						CrossingOff += Cross < 1.0 && FMath::Abs(Surf - Want) < 0.01 ? 0 : 1;
						// And there the sea as the terrain draws it (its float copy's vertex carried onto the radial through the
						// fish's middle, held to the most the material takes): the fish and its splash on it.
						const double Sea = FMath::Clamp(DrawnSeaCm(Drawn, R, (float)T, (float)Swell), -(double)SlonkFauna::Leaper::MaxSurfaceCm, (double)SlonkFauna::Leaper::MaxSurfaceCm);
						WorstDrawnSeaCm = FMath::Max(WorstDrawnSeaCm, FMath::Abs(Mid.Z - Sea));
						OffDrawnSea += FMath::Abs(Mid.Z - Sea) < 2.0 ? 0 : 1;
						++Crossings;
					}
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d groups, %d leaps, %d moments: %d disagree on whether the fish is drawn, %d drawn off the closed form (%.3f cm at worst), %d off the surface where they cross it (%.3f cm), %d off the splash's place (%.3f cm)"),
		Groups.Num(), Leaps, Tried, ShownDisagree, Off, WorstOffCm, NotAtSurface, WorstSurfaceCm, CrossingOff, WorstCrossCm));
	AddInfo(FString::Printf(TEXT("%d crossings: %d off the sea as the terrain draws it there (%.2f cm at worst)"), Crossings, OffDrawnSea, WorstDrawnSeaCm));
	AddInfo(FString::Printf(TEXT("drawn up to %.0f cm over the sea's level; between leaps held %.0f cm under it at the highest (%d not under the lowest surface); %d poking out of the water as they come and go; the lowest any vertex goes %.0f cm (%d under 3 m)"),
		Highest, -HiddenHighest, AboveOutside, PokesOut, -Lowest, BelowFloor));
	TestTrue(TEXT("enough leaps tried"), Leaps > 400);
	TestEqual(TEXT("drawn when the closed form has it drawn, and only then"), ShownDisagree, 0);
	TestEqual(TEXT("the material's fish where the closed form has it"), Off, 0);
	TestEqual(TEXT("leaving and diving back in at the sea's surface there and then"), NotAtSurface, 0);
	TestEqual(TEXT("the splash where the arc crosses the surface"), CrossingOff, 0);
	TestTrue(TEXT("enough crossings tried"), Crossings > 800);
	TestEqual(TEXT("leaving and diving back in on the sea as the terrain draws it"), OffDrawnSea, 0);
	TestEqual(TEXT("never drawn above water outside a leap: held under the lowest surface"), AboveOutside, 0);
	TestEqual(TEXT("under the water whole as it comes and goes"), PokesOut, 0);
	TestEqual(TEXT("never below the shallowest sea floor it lives over"), BelowFloor, 0);

	// The splashes: over two hours, each leaping cycle of each fish leaves the water once and dives back in once, at its
	// crossings, in order (asked frame by frame); none for a cycle that skips its leap.
	int32 Events = 0, EventsOff = 0, Skipped = 0, Cycles = 0, OutOfOrder = 0;
	double Gaps = 0.0, GapSq = 0.0;
	int32 NumGaps = 0, Together = 0;
	for (int32 g = 0; g < FMath::Min(Groups.Num(), 16); ++g)
	{
		const FFish F = FishOf(Groups[g]);
		TArray<double> Leaves;
		for (int32 i = 0; i < F.Num(); ++i)
		{
			const float* C = F.At(i);
			TArray<SlonkFauna::FLeapEvent> Ev;
			for (double T = 0.0; T < 7200.0; T += 0.25) { SlonkFauna::LeapEventsBetween(C, i, T, T + 0.25, Ev); }
			const double Cyc = C[SlonkFauna::Custom::RateHz];
			int32 Want = 0;
			for (int64 N = -1; N <= (int64)(7200.0 / Cyc) + 1; ++N)
			{
				const SlonkFauna::FLeap L = SlonkFauna::LeapOf(C, N);
				++Cycles;
				Skipped += L.bLeaps ? 0 : 1;
				if (!L.bLeaps) { continue; }
				Want += (L.T0 > 0.0 && L.T0 <= 7200.0 ? 1 : 0) + (L.T1() > 0.0 && L.T1() <= 7200.0 ? 1 : 0);
				if (L.T0 > 0.0 && L.T0 <= 3600.0) { Leaves.Add(L.T0); }
			}
			Events += Ev.Num();
			EventsOff += Ev.Num() == Want ? 0 : 1;
			for (int32 e = 0; e < Ev.Num(); ++e)
			{
				const SlonkFauna::FLeapEvent& E = Ev[e];
				EventsOff += E.Member == i && E.TimeSec == (E.bEntry ? E.Leap.T1() : E.Leap.T0) && E.Leap.bLeaps ? 0 : 1;
				OutOfOrder += e == 0 || Ev[e - 1].TimeSec <= E.TimeSec ? 0 : 1;
			}
		}
		// The fish of a group of two or more leave the water a few seconds apart, irregularly.
		if (F.Num() < 2) { continue; }
		Leaves.Sort();
		for (int32 k = 1; k < Leaves.Num(); ++k)
		{
			const double Gap = Leaves[k] - Leaves[k - 1];
			Gaps += Gap;
			GapSq += Gap * Gap;
			++NumGaps;
			Together += Gap < 0.3 ? 1 : 0;
		}
	}
	const double MeanGap = Gaps / FMath::Max(NumGaps, 1);
	const double GapSd = FMath::Sqrt(FMath::Max(GapSq / FMath::Max(NumGaps, 1) - MeanGap * MeanGap, 0.0));
	AddInfo(FString::Printf(TEXT("splashes over two hours: %d crossings, %d fish with crossings off their leaps, %d out of order; %d of %d cycles skip their leap (%.2f); the fish of groups of two or more leave the water %.1f s apart on average (%.1f s either way), %d of %d within 0.3 s of another"),
		Events, EventsOff, OutOfOrder, Skipped, Cycles, (double)Skipped / FMath::Max(Cycles, 1), MeanGap, GapSd, Together, NumGaps));
	TestEqual(TEXT("a splash where and when each leap leaves the water and dives back in, and no other"), EventsOff, 0);
	TestEqual(TEXT("in order"), OutOfOrder, 0);
	TestTrue(TEXT("about a quarter of the cycles skip their leap"), FMath::Abs((double)Skipped / FMath::Max(Cycles, 1) - SlonkFauna::Leaper::SkipShare) < 0.06);
	TestTrue(TEXT("a group's fish leap a few seconds apart, irregularly"), MeanGap > 2.0 && MeanGap < 8.0 && GapSd > 0.3 * MeanGap);

	// Every cycle divides the hour and every tail beats a whole number of times a minute; the same leap an hour on; and the
	// material's wrap of its clock at the hour moves no fish (its motion two frames apart across the wrap as between any two
	// frames: within the closed form's).
	int32 NotDividing = 0, NotHourly = 0, Seams = 0;
	for (const SlonkFauna::FFaunaGroup& G : Groups)
	{
		const FFish F = FishOf(G);
		for (int32 i = 0; i < F.Num(); ++i)
		{
			const float* C = F.At(i);
			const double Cyc = C[SlonkFauna::Custom::RateHz];
			NotDividing += FMath::IsNearlyEqual(FMath::Fmod(3600.0, Cyc), 0.0, 1e-9) ? 0 : 1;
			const float Beat = FMath::RoundToFloat((6.f + 4.f * FMath::Frac(C[SlonkFauna::Custom::Seed] * 3.7f)) * 60.f);
			NotDividing += Beat == FMath::RoundToFloat(Beat) && Beat > 0.f ? 0 : 1;
			for (int32 k = 0; k < 6; ++k)
			{
				const double T = Rng.FRandRange(0.f, 3600.f);
				const SlonkFauna::FLeap A = SlonkFauna::LeapAt(C, T), B = SlonkFauna::LeapAt(C, T + 3600.0);
				NotHourly += A.bLeaps == B.bLeaps && FMath::IsNearlyEqual(A.T0 + 3600.0, B.T0, 1e-6) && A.FromCm == B.FromCm && A.Yaw == B.Yaw && A.ApexCm == B.ApexCm && A.RangeCm == B.RangeCm ? 0 : 1;
			}
		}
	}
	// Across the wrap: a fish drawn as the hour turns, its middle a frame either side, against the closed form's then.
	{
		for (const SlonkFauna::FFaunaGroup& G : Groups)
		{
			const FFish F = FishOf(G);
			SlonkFauna::FLeaperSurfaceCache Cache;
			for (int32 i = 0; i < F.Num(); ++i)
			{
				const float* C = F.At(i);
				for (int32 Hour = 1; Hour <= 2; ++Hour)
				{
					for (const double Dt : { -1.0 / 60.0, 1.0 / 60.0 })
					{
						const double T = 3600.0 * Hour + Dt;
						float Gd[SlonkFauna::LeaperData::Num] = {};
						SlonkFauna::LeaperGroupData(G, F.Custom, F.Home, R, Swell, 1.0, T, NoDive, Cache, Gd);
						const SlonkFauna::FLeap L = SlonkFauna::LeapAt(C, T);
						const float Tm = MaterialTime(T);
						if (SlonkFauna::LeaperShown(Tm, C, Gd) != L.DrawnAt(T)) { ++Seams; continue; }
						if (!L.DrawnAt(T)) { continue; }
						const FVector Mid = (FVector)SlonkFauna::LeaperMotion(FVector3f::ZeroVector, Middle, Tm, C, Gd, 2000.f, 960.f) * C[SlonkFauna::Custom::SizeM];
						const SlonkFauna::FLeapPose P = SlonkFauna::LeapPoseAt(L, Gd[SlonkFauna::LeaperData::Surface0 + 2 * i], Gd[SlonkFauna::LeaperData::Surface0 + 2 * i + 1], T);
						Seams += (Mid - P.PosCm).Size() < 0.5 ? 0 : 1;
					}
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d cycles or beats not dividing the hour, %d leaps not the same an hour on, %d seams at the hour's wrap"), NotDividing, NotHourly, Seams));
	TestEqual(TEXT("every cycle and beat divides the hour"), NotDividing, 0);
	TestEqual(TEXT("the same leap an hour on"), NotHourly, 0);
	TestEqual(TEXT("no seam at the hour's wrap"), Seams, 0);

	// The swell the crossings are read from is the terrain material's: SwellAt against its float copy, and the surface over
	// a point (SeaSurfaceCm) against the drawn sea there, the material's vertex carried onto the point's radial (found to
	// convergence) and how high over the sphere it is carried (its sway along the plane's fixed axes lifting it too), at
	// full strength and faded; points on the projection's seams and corners among them, where that lift is the most.
	int32 SwellOff = 0, SurfaceOff = 0, Unconverged = 0;
	double WorstSwell = 0.0, WorstSurface = 0.0, SurfaceSq = 0.0, WorstLift = 0.0;
	constexpr int32 NumSurfaces = 3000;
	for (int32 k = 0; k < NumSurfaces; ++k)
	{
		FVector P = Rng.GetUnitVector();
		if (k % 3 == 1)
		{
			// Near a seam: two of its axes nearly as large.
			const int32 A = Rng.RandRange(0, 2), B = (A + Rng.RandRange(1, 2)) % 3;
			P[B] = FMath::Sign(P[B] == 0.0 ? 1.0 : P[B]) * FMath::Abs(P[A]) * Rng.FRandRange(0.97f, 1.f);
		}
		else if (k % 3 == 2)
		{
			// Near a corner: all three nearly as large.
			P = FVector(FMath::Sign(P.X + 1e-9) * Rng.FRandRange(0.95f, 1.f), FMath::Sign(P.Y + 1e-9) * Rng.FRandRange(0.95f, 1.f), FMath::Sign(P.Z + 1e-9) * Rng.FRandRange(0.95f, 1.f));
		}
		P = P.GetSafeNormal() * R;
		const double T = Rng.FRandRange(0.f, 5000.f);
		const FVector D = SlonkFauna::SwellAt(P, T, Swell);
		const FVector3f M = MaterialSwell(P, (float)T, (float)Swell);
		const double Diff = (D - (FVector)M).Size();
		WorstSwell = FMath::Max(WorstSwell, Diff);
		SwellOff += Diff < 0.5 ? 0 : 1;
		const double Fade = k % 4 == 3 ? 0.4 : 1.0;
		double Residual = 0.0, Lift = 0.0;
		const double Want = DrawnSeaCm(P, R, (float)T, (float)Swell, (float)Fade, &Residual, &Lift);
		Unconverged += Residual < 0.01 ? 0 : 1;
		const double Got = SlonkFauna::SeaSurfaceCm(P, R, T, Swell, Fade);
		WorstSurface = FMath::Max(WorstSurface, FMath::Abs(Got - Want));
		SurfaceSq += FMath::Square(Got - Want);
		SurfaceOff += FMath::Abs(Got - Want) < 2.0 ? 0 : 1;
		// What the sway's own lift adds to the swell's.
		WorstLift = FMath::Max(WorstLift, FMath::Abs(Want - Lift));
	}
	const FString Block = TerrainSwellBlock();
	const uint32 BlockCrc = FCrc::StrCrc32(*Block);
	AddInfo(FString::Printf(TEXT("the swell at %d points: %d off the terrain material's float copy (%.3f cm at worst), %d surfaces off the drawn sea there (%.2f cm at worst, %.2f cm RMS; %d not converged), its sway's lift up to %.0f cm over the swell's own; terrain_material.py's swell code %d characters, CRC 0x%08x (0x%08x when the copy was made)"),
		NumSurfaces, SwellOff, WorstSwell, SurfaceOff, WorstSurface, FMath::Sqrt(SurfaceSq / NumSurfaces), Unconverged, WorstLift, Block.Len(), BlockCrc, TerrainSwellBlockCrc));
	TestEqual(TEXT("the swell mirrors the terrain material's"), SwellOff, 0);
	TestEqual(TEXT("the drawn sea found to convergence"), Unconverged, 0);
	TestEqual(TEXT("the surface over a point is the drawn sea's there, the sway's lift and all"), SurfaceOff, 0);
	TestTrue(TEXT("terrain_material.py's swell code found"), Block.Len() > 500 && Block.Contains(TEXT("void SlonkGerstner(")));
	TestEqual(TEXT("the copy is of the terrain material's swell as it is now"), BlockCrc, TerrainSwellBlockCrc);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaLeaperDiveTest, "Slonk.Fauna.LeaperDive", SLONK_TEST_FLAGS)
bool FSlonkFaunaLeaperDiveTest::RunTest(const FString& Parameters)
{
	// A craft passing low over a group of leapers (below 50 m within 50 m) sends it diving: none of its fish leaves the water
	// for a minute after the latest startle, a leap already out of the water comes down (and splashes back in), and once the
	// dive is over they leap again; as the material draws it, no fish shows but at the start of a leap, the dive's start and
	// end and its extension by a second pass among it. A craft higher, further off or set down, and anyone on foot (on a
	// shore or wading in), are nothing to it.
	const SlonkFauna::FFaunaWorld W = EarthSeas();
	const double R = W.P.RadiusCm;
	TArray<SlonkFauna::FFaunaGroup> Groups;
	LeaperGroupsOverSeas(W, 8, Groups);
	if (Groups.Num() < 4) { AddError(TEXT("too few groups of leapers found")); return false; }
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Leaper);
	int32 Scared = 0, NotScared = 0, Restarted = 0, LeftInDive = 0, CameDown = 0, Resumed = 0, Popped = 0, Disagree = 0, SplashesInDive = 0, EntriesInDive = 0, Frames = 0;
	for (const SlonkFauna::FFaunaGroup& G : Groups)
	{
		const FFish F = FishOf(G);
		// Startled while its first fish is out of the water, two fifths of the way through a leap of its after 1000 s.
		SlonkFauna::FLeap Out = SlonkFauna::LeapAt(F.At(0), 1000.0);
		while (!Out.bLeaps || Out.T0 < 1000.0) { Out = SlonkFauna::LeapOf(F.At(0), Out.Cycle + 1); }
		const double T0 = Out.T0 + 0.4 * Out.FlightSec;
		const SlonkFauna::FFaunaPose Pose = SlonkFauna::PathAt(G, R, T0);
		const FVector Side = Pose.Rot.GetAxisY();
		auto Craft = [&](double AltCm, double OffCm, bool bLanded)
		{
			SlonkFauna::FFaunaThreat T;
			T.PosCm = (Pose.PosCm + Side * OffCm).GetSafeNormal() * (R + AltCm);
			T.VelCmS = Pose.Rot.GetAxisX() * 3000.0;
			T.AltitudeCm = AltCm;
			T.bLanded = bLanded;
			return T;
		};
		SlonkFauna::FFaunaThreat Crew = Craft(170.0, 300.0, false);
		Crew.bCraft = false;
		SlonkFauna::FPanic Dive;
		// Not by a craft 100 m up, nor one 80 m off at 30 m, one set down beside it, nor someone wading three metres off.
		for (const SlonkFauna::FFaunaThreat& T : { Craft(10000.0, 0.0, false), Craft(3000.0, 8000.0, false), Craft(0.0, 1000.0, true), Crew })
		{
			NotScared += SlonkFauna::StartleLeapers(Dive, D, Pose.PosCm, T, T0) ? 1 : 0;
		}
		// By one passing 30 m up and 25 m off (its dive keeping under the leaps leaving from a moment on, past those already
		// on their way up); not again 3 s on, again 20 s on (its dive going on from its start).
		Scared += SlonkFauna::StartleLeapers(Dive, D, Pose.PosCm, Craft(3000.0, 2500.0, false), T0) ? 1 : 0;
		const double Began = Dive.ScatterT0;
		Restarted += Began >= T0 && Began <= T0 + SlonkFauna::Leaper::DiveLeadSec ? 0 : 1;
		Restarted += SlonkFauna::StartleLeapers(Dive, D, Pose.PosCm, Craft(3000.0, 2500.0, false), T0 + 3.0) ? 1 : 0;
		SlonkFauna::FPanic Extended = Dive;
		Restarted += SlonkFauna::StartleLeapers(Extended, D, Pose.PosCm, Craft(3000.0, 2500.0, false), T0 + 20.0) ? 0 : 1;
		Restarted += Extended.ScatterT0 == Began ? 0 : 1;
		const double DiveEnd = T0 + 20.0 + SlonkFauna::Leaper::DiveSec;
		// Frame by frame through the dive, its extension and on past it: the material's fish against the closed form and the
		// dive's rule, and none coming into sight but at the start of a leap.
		SlonkFauna::FLeaperSurfaceCache Cache;
		TArray<bool> WasShown;
		WasShown.Init(false, F.Num());
		const double Dt = 1.0 / 30.0;
		for (double T = T0 - 5.0; T < DiveEnd + 60.0; T += Dt)
		{
			++Frames;
			// Startled at T0 and again at T0 + 20, as the component would have it.
			const SlonkFauna::FPanic& Now = T < T0 ? SlonkFauna::FPanic() : (T < T0 + 20.0 ? Dive : Extended);
			float Gd[SlonkFauna::LeaperData::Num] = {};
			SlonkFauna::LeaperGroupData(G, F.Custom, F.Home, R, W.Sea.SwellCm, 1.0, T, Now, Cache, Gd);
			const float Tm = MaterialTime(T);
			for (int32 i = 0; i < F.Num(); ++i)
			{
				const float* C = F.At(i);
				const SlonkFauna::FLeap L = SlonkFauna::LeapAt(C, T);
				const bool bShown = SlonkFauna::LeaperShown(Tm, C, Gd);
				const bool bWant = L.DrawnAt(T) && !SlonkFauna::LeapDived(Now, L.T0);
				const bool bNearEdge = FMath::Abs(T - (L.T0 - L.LeadSec)) < 0.005 || FMath::Abs(T - (L.T1() + L.LeadSec)) < 0.005;
				Disagree += bShown == bWant || bNearEdge ? 0 : 1;
				if (T > T0 - 5.0 + 0.5 * Dt && bShown && !WasShown[i] && T - (L.T0 - L.LeadSec) > Dt + 1e-3) { ++Popped; }
				WasShown[i] = bShown;
				if (!bShown) { continue; }
				if (T >= T0 && L.T0 >= Began && L.T0 < DiveEnd) { ++LeftInDive; }
				if (T >= T0 && L.T0 < T0) { ++CameDown; }
				if (L.T0 >= DiveEnd) { ++Resumed; }
			}
			// The splashes this frame, of the leaps the dive does not keep in the water.
			for (int32 i = 0; i < F.Num(); ++i)
			{
				TArray<SlonkFauna::FLeapEvent> Ev;
				SlonkFauna::LeapEventsBetween(F.At(i), i, T - Dt, T, Ev);
				for (const SlonkFauna::FLeapEvent& E : Ev)
				{
					if (SlonkFauna::LeapDived(Now, E.Leap.T0) || E.TimeSec < Began || E.TimeSec >= DiveEnd) { continue; }
					SplashesInDive += E.bEntry ? 0 : 1;
					EntriesInDive += E.bEntry ? 1 : 0;
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d groups: %d sent diving by a craft passing 30 m up and 25 m off, %d by a craft 100 m up, 80 m off, set down or by someone wading in; startled again 3 s and 20 s on: %d wrong"),
		Groups.Num(), Scared, NotScared, Restarted));
	AddInfo(FString::Printf(TEXT("%d frames: %d fish frames out of the water from a leap begun in the dive, %d of leaps already out coming down, %d of leaps after it; %d fish coming into sight but at a leap's start, %d off the closed form and the dive; %d splashes of leaving the water in the dive, %d of diving back in"),
		Frames, LeftInDive, CameDown, Resumed, Popped, Disagree, SplashesInDive, EntriesInDive));
	TestEqual(TEXT("a craft passing low sends every group diving"), Scared, Groups.Num());
	TestEqual(TEXT("a craft higher, further off or set down, and the crew, do not"), NotScared, 0);
	TestEqual(TEXT("startled again within the re-startle's while it keeps its dive, after it its dive goes on from its start"), Restarted, 0);
	TestEqual(TEXT("no fish leaves the water while the group dives"), LeftInDive, 0);
	TestEqual(TEXT("no splash of a fish leaving the water while it dives"), SplashesInDive, 0);
	TestTrue(TEXT("a leap already out of the water comes down, and once the dive is over they leap again"), CameDown > 0 && Resumed > 0);
	TestEqual(TEXT("as the material draws it: drawn as the closed form and the dive have it"), Disagree, 0);
	TestEqual(TEXT("and no fish comes into sight but at the start of a leap"), Popped, 0);

	// The bounds the dive's rule leans on: no leap is drawn on its way up under the water for longer than DiveLeadSec
	// before it leaves it, nor for longer than HeldUnderSec after it leaves it (the longest fish's lowest and highest leaps,
	// and every leap of these groups' fish over two hours).
	double MostLead = 0.0, MostAfter = 0.0;
	{
		const double Len = SlonkFauna::Leaper::MaxSizeM * 100.0;
		for (const double Apex : { (double)SlonkFauna::Leaper::ApexLo, (double)(SlonkFauna::Leaper::ApexLo + SlonkFauna::Leaper::ApexSpan) })
		{
			const double Tf = 2.0 * FMath::Sqrt(2.0 * Apex * Len / SlonkFauna::Leaper::GravityCmS2);
			const double Lead = SlonkFauna::Leaper::LeadDepth * Len / (4.0 * Apex * Len / Tf);
			MostLead = FMath::Max(MostLead, Lead);
			MostAfter = FMath::Max(MostAfter, Tf + Lead);
		}
	}
	double SeenLead = 0.0, SeenAfter = 0.0;
	for (const SlonkFauna::FFaunaGroup& G : Groups)
	{
		const FFish F = FishOf(G);
		for (int32 i = 0; i < F.Num(); ++i)
		{
			const float* C = F.At(i);
			for (int64 N = 0; N <= (int64)(7200.0 / C[SlonkFauna::Custom::RateHz]); ++N)
			{
				const SlonkFauna::FLeap L = SlonkFauna::LeapOf(C, N);
				if (!L.bLeaps) { continue; }
				SeenLead = FMath::Max(SeenLead, L.LeadSec);
				SeenAfter = FMath::Max(SeenAfter, L.FlightSec + L.LeadSec);
			}
		}
	}
	AddInfo(FString::Printf(TEXT("a leap is drawn under the water on its way up for %.3f s at most (%.3f s seen; a dive keeps under those from %.2f s after its startle), and out and back under for %.3f s at most after it leaves (%.3f s seen; a new dive waits %.2f s past the last one's window)"),
		MostLead, SeenLead, SlonkFauna::Leaper::DiveLeadSec, MostAfter, SeenAfter, SlonkFauna::Leaper::HeldUnderSec));
	TestTrue(TEXT("a new dive starts past every leap already on its way up"), MostLead < SlonkFauna::Leaper::DiveLeadSec && SeenLead <= MostLead + 1e-6);
	TestTrue(TEXT("and only once every leap the last one kept under is past its time back in the water"), MostAfter < SlonkFauna::Leaper::HeldUnderSec && SeenAfter <= MostAfter + 1e-6);

	// Startled again at many moments round the end of its dive's window (just before it closes, as it closes, in the few
	// seconds its fright lingers past it, and after), by a craft passing low and by the command; and, calm, at many moments
	// round a leap's leaving the water (its nose out before its middle is): no fish in sight is ever hidden before its leap
	// is back under the water, none out of the water among them; every leap that leaves the water (its splash thrown) dives
	// back into it (its splash thrown); and a startle does nothing only in the moments round the window's end it must.
	int32 Vanished = 0, VanishedInAir = 0, NoEntry = 0, Entries = 0, SweepFrames = 0, Sweeps = 0, InAirAtStartle = 0, WrongRefusal = 0, Refusals = 0;
	auto Sweep = [&](const SlonkFauna::FFaunaGroup& G, const FFish& F, SlonkFauna::FPanic Dive, double At, bool bForce, double From, double To, bool& bOutStartled)
	{
		++Sweeps;
		bOutStartled = false;
		const double Dt = 1.0 / 60.0;
		TArray<bool> WasShown;
		WasShown.Init(false, F.Num());
		// Each fish's leap whose leaving the water threw its splash, until its diving back in.
		TArray<int64> Left;
		Left.Init(TNumericLimits<int64>::Lowest(), F.Num());
		bool bAsked = false;
		for (double T = From; T < To; T += Dt)
		{
			++SweepFrames;
			// As the component has it: a dive over is forgotten, then the craft or the command startles it.
			if (!Dive.IsLive(T)) { Dive = SlonkFauna::FPanic(); }
			if (!bAsked && T >= At)
			{
				bAsked = true;
				const SlonkFauna::FFaunaPose Pose = SlonkFauna::PathAt(G, R, T);
				SlonkFauna::FFaunaThreat Low;
				Low.PosCm = (Pose.PosCm + Pose.Rot.GetAxisY() * 2500.0).GetSafeNormal() * (R + 3000.0);
				Low.VelCmS = Pose.Rot.GetAxisX() * 3000.0;
				Low.AltitudeCm = 3000.0;
				bOutStartled = SlonkFauna::StartleLeapers(Dive, D, Pose.PosCm, Low, T, bForce);
				for (int32 i = 0; i < F.Num(); ++i)
				{
					const SlonkFauna::FLeap L = SlonkFauna::LeapAt(F.At(i), T);
					if (L.bLeaps && T >= L.T0 && T <= L.T1()) { ++InAirAtStartle; break; }
				}
			}
			float Gd[SlonkFauna::LeaperData::Num] = {};
			SlonkFauna::LeaperDiveData(Dive, T, Gd);
			const float Tm = MaterialTime(T);
			for (int32 i = 0; i < F.Num(); ++i)
			{
				const float* C = F.At(i);
				const SlonkFauna::FLeap L = SlonkFauna::LeapAt(C, T);
				const bool bShown = SlonkFauna::LeaperShown(Tm, C, Gd);
				// Shown the frame before and its leap still to be drawn now (not at its very end), yet hidden.
				const bool bNearEnd = FMath::Abs(T - (L.T1() + L.LeadSec)) < 0.005;
				if (T > From + 0.5 * Dt && WasShown[i] && !bShown && L.DrawnAt(T) && !bNearEnd)
				{
					++Vanished;
					VanishedInAir += T >= L.T0 && T <= L.T1() ? 1 : 0;
				}
				WasShown[i] = bShown;
				// The splashes this frame, as UpdateLeaper throws them (of the leaps the dive does not keep in the water).
				TArray<SlonkFauna::FLeapEvent> Ev;
				SlonkFauna::LeapEventsBetween(C, i, T - Dt, T, Ev);
				for (const SlonkFauna::FLeapEvent& E : Ev)
				{
					const bool bThrown = !SlonkFauna::LeapDived(Dive, E.Leap.T0);
					if (!E.bEntry)
					{
						if (bThrown) { Left[i] = E.Leap.Cycle; }
						continue;
					}
					if (Left[i] != E.Leap.Cycle) { continue; }
					++Entries;
					NoEntry += bThrown ? 0 : 1;
					Left[i] = TNumericLimits<int64>::Lowest();
				}
			}
		}
	};
	for (int32 g = 0; g < Groups.Num(); ++g)
	{
		const SlonkFauna::FFaunaGroup& G = Groups[g];
		const FFish F = FishOf(G);
		const double First = 2000.0 + 37.0 * g;
		const SlonkFauna::FFaunaPose Pose = SlonkFauna::PathAt(G, R, First);
		SlonkFauna::FFaunaThreat Low;
		Low.PosCm = Pose.PosCm.GetSafeNormal() * (R + 3000.0);
		Low.AltitudeCm = 3000.0;
		SlonkFauna::FPanic Calm, Dived;
		SlonkFauna::StartleLeapers(Dived, D, Pose.PosCm, Low, First);
		const double End = Dived.T0 + SlonkFauna::Leaper::DiveSec;
		// Round the end of its window: from a second before it to a second past its fright's lingering.
		for (int32 k = 0; k <= 80; ++k)
		{
			const double At = End - 1.0 + 0.1 * k + 0.003 * g;
			for (const bool bForce : { false, true })
			{
				bool bStartled = false;
				Sweep(G, F, Dived, At, bForce, At - 2.0, At + 3.0, bStartled);
				// Startled on the first frame at or after At: refused in the gap from DiveLeadSec short of the window's end to
				// HeldUnderSec past it, and only there (a frame either side of its edges may go either way).
				const double Frame = 1.0 / 60.0;
				const bool bMustRefuse = At >= End - SlonkFauna::Leaper::DiveLeadSec && At + Frame <= End + SlonkFauna::Leaper::HeldUnderSec;
				const bool bMustTake = At + Frame <= End - SlonkFauna::Leaper::DiveLeadSec || At >= End + SlonkFauna::Leaper::HeldUnderSec;
				Refusals += bStartled ? 0 : 1;
				WrongRefusal += (bMustRefuse && bStartled) || (bMustTake && !bStartled) ? 1 : 0;
			}
		}
		// Calm, round each fish's next leap's leaving the water, from just before it is drawn to a little after it leaves.
		for (int32 i = 0; i < F.Num(); ++i)
		{
			SlonkFauna::FLeap L = SlonkFauna::LeapAt(F.At(i), 3000.0 + 11.0 * g);
			while (!L.bLeaps || L.T0 < 3000.0 + 11.0 * g) { L = SlonkFauna::LeapOf(F.At(i), L.Cycle + 1); }
			for (int32 k = 0; k <= 12; ++k)
			{
				const double At = L.T0 - L.LeadSec - 0.02 + (L.LeadSec + 0.12) * k / 12.0;
				bool bStartled = false;
				Sweep(G, F, Calm, At, k % 2 == 1, At - 0.5, At + 2.0, bStartled);
				WrongRefusal += bStartled ? 0 : 1;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d startles round a dive's end and a leap's leaving the water (%d with a fish out of the water then; %d refused, %d wrongly so or not), %d frames: %d fish in sight hidden before their leap was back under (%d of them out of the water), %d of %d leaps that splashed out of the water with no splash diving back in"),
		Sweeps, InAirAtStartle, Refusals, WrongRefusal, SweepFrames, Vanished, VanishedInAir, NoEntry, Entries));
	TestTrue(TEXT("enough startles with a fish out of the water"), InAirAtStartle > 40 && Entries > 200);
	TestEqual(TEXT("a startle never hides a fish in sight"), Vanished, 0);
	TestEqual(TEXT("nor one out of the water"), VanishedInAir, 0);
	TestEqual(TEXT("every leap that leaves the water dives back in, and splashes as it does"), NoEntry, 0);
	TestEqual(TEXT("a startle does nothing only while the leaps the last dive kept under may still be in sight"), WrongRefusal, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaLeaperGotoTest, "Slonk.Fauna.LeaperGoto", SLONK_TEST_FLAGS)
bool FSlonkFaunaLeaperGotoTest::RunTest(const FString& Parameters)
{
	// `fauna goto leaper` parks the ship (SlonkFauna::LeaperWatchDir) where the shoal it watches, swimming round its loop,
	// never comes within reach of its fright: at every height the command may be given below the scare altitude, wherever
	// round its loop the shoal is when the ship comes, over the whole of its loop and on; off to the side of the shoal, out
	// from the loop's middle, no further off than it need be.
	const SlonkFauna::FFaunaWorld W = EarthSeas();
	const double R = W.P.RadiusCm;
	TArray<SlonkFauna::FFaunaGroup> Groups;
	LeaperGroupsOverSeas(W, 30, Groups);
	if (Groups.Num() < 10) { AddError(TEXT("too few groups of leapers found")); return false; }
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Leaper);
	int32 Parks = 0, ScaredParks = 0, OffSide = 0, Inward = 0;
	double Nearest = 1.0e30, MostOff = 0.0;
	for (const SlonkFauna::FFaunaGroup& G : Groups)
	{
		for (const double Alt : { 200.0, 1000.0, 3000.0, 4500.0 })
		{
			for (int32 k = 0; k < 4; ++k)
			{
				const double Now = 1000.0 + 77.0 * k;
				double SideCm = 0.0;
				const FVector Dir = SlonkFauna::LeaperWatchDir(G, R, Now, Alt, &SideCm);
				++Parks;
				SlonkFauna::FFaunaThreat Ship;
				Ship.PosCm = Dir * (R + Alt);
				Ship.AltitudeCm = Alt;
				Ship.bCraft = true;
				// Off to the side of the shoal now, at least SideCm off and no more than its loop across further, out from the
				// loop's middle.
				const FVector Here = SlonkFauna::PathAt(G, R, Now).PosCm;
				const double Off = FVector::Dist(Dir * R, Here);
				MostOff = FMath::Max(MostOff, Off - SideCm);
				OffSide += Off >= SideCm - 1.0 && Off <= SideCm + 4.0 * G.RadiusCm ? 0 : 1;
				const FVector Mid = G.Centre * R;
				Inward += FVector::DotProduct(Dir * R - Mid, Here - Mid) > 0.0 ? 0 : 1;
				// Round its loop and on (once round takes 67 to 320 s): never in reach of its fright.
				bool bScared = false;
				for (double T = Now; T < Now + 400.0; T += 1.0)
				{
					const FVector Pos = SlonkFauna::PathAt(G, R, T).PosCm;
					Nearest = FMath::Min(Nearest, FVector::Dist(Pos, Ship.PosCm));
					bScared |= SlonkFauna::Scares(D, Pos, Ship);
				}
				ScaredParks += bScared ? 1 : 0;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d groups, %d parkings from 2 m to 45 m up: %d where the shoal swims within reach of its fright (the nearest it comes %.1f m); %d not off its side as asked (up to %.1f m further than asked), %d not out from the loop's middle"),
		Groups.Num(), Parks, ScaredParks, Nearest / 100.0, OffSide, MostOff / 100.0, Inward));
	TestEqual(TEXT("the shoal never swims within reach of the parked ship's fright"), ScaredParks, 0);
	TestTrue(TEXT("never nearer than the scare radius"), Nearest > D.ScareRadiusCm);
	TestEqual(TEXT("off to the side of the shoal as asked"), OffSide, 0);
	TestEqual(TEXT("out from its loop's middle"), Inward, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaFishSplashTest, "Slonk.Fauna.FishSplash", SLONK_TEST_FLAGS)
bool FSlonkFaunaFishSplashTest::RunTest(const FString& Parameters)
{
	// A splash as the asset is given it: the fish's length, whether it dives back in, the night, the ground's carry, the foam
	// white in any sea; and the pool of one-shots: a component lent rests while its burst plays and is lent again after,
	// new ones only while none has finished, up to the most there may be, every one coming back.
	SlonkFauna::FFishSplashInput In;
	In.SizeCm = 60.0;
	In.CarryWorldCmS = FVector(1.2e4, -3.0e3, 50.0);
	const SlonkFauna::FFishSplash Leave = SlonkFauna::FishSplashFor(In);
	In.bEntry = true;
	In.Night = 1.7;
	const SlonkFauna::FFishSplash Entry = SlonkFauna::FishSplashFor(In);
	In.Shallow = FLinearColor(0.07f, 0.06f, 0.04f);
	In.Ocean = FLinearColor(0.018f, 0.016f, 0.014f);
	const SlonkFauna::FFishSplash Methane = SlonkFauna::FishSplashFor(In);
	auto Luma = [](const FLinearColor& C) { return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B; };
	AddInfo(FString::Printf(TEXT("a 60 cm fish's splash: size %.0f cm, entry %.0f then %.0f, night %.1f then %.1f; foam %.2f %.2f %.2f (opacity %.2f) on water, %.2f %.2f %.2f on methane; carry %.0f cm/s"),
		Leave.Size, Leave.Entry, Entry.Entry, Leave.Night, Entry.Night, Leave.Color.R, Leave.Color.G, Leave.Color.B, Leave.Color.A, Methane.Color.R, Methane.Color.G, Methane.Color.B, Leave.Carry.Size()));
	TestTrue(TEXT("the fish's length, whether it dives back in, the night held to 1"), Leave.Size == 60.f && Leave.Entry == 0.f && Entry.Entry == 1.f && Leave.Night == 0.f && Entry.Night == 1.f);
	TestTrue(TEXT("the ground's carry handed on as it is"), Leave.Carry.Equals(In.CarryWorldCmS));
	TestTrue(TEXT("foam white in the sun on any sea, nearly opaque"), Luma(Leave.Color) > 0.8f && Luma(Methane.Color) > 0.75f && Leave.Color.A > 0.6f);

	// None by night: thrown by day and at dusk (the sun on the horizon there), none from where the sun is far enough under
	// the horizon that the sky atmosphere's planet, its ground 0.05% of the radius under the sea (SlonkGameMode), shades a
	// splash on the sea wholly (it is lit by that sun alone, and would draw black), nor any deeper in the night.
	const double ShadedUp = -FMath::Sin(FMath::Acos(0.9995));
	int32 DarkThrown = 0, LitNotThrown = 0;
	for (int32 k = 0; k <= 200; ++k)
	{
		const double Up = 1.0 - 2.0 * k / 200.0;
		const float Night = SlonkFauna::LeaperNight(Up);
		DarkThrown += Up <= ShadedUp && SlonkFauna::SplashLitAt(Night) ? 1 : 0;
		LitNotThrown += Up >= 0.0 && !SlonkFauna::SplashLitAt(Night) ? 1 : 0;
	}
	DarkThrown += SlonkFauna::SplashLitAt(SlonkFauna::LeaperNight(ShadedUp)) ? 1 : 0;
	AddInfo(FString::Printf(TEXT("the night %.2f with the sun on the horizon, %.2f where the sky's planet shades a splash (the sun %.1f degrees down); splashes thrown to a night of %.2f: %d thrown in the dark, %d not thrown with the sun up"),
		SlonkFauna::LeaperNight(0.0), SlonkFauna::LeaperNight(ShadedUp), FMath::RadiansToDegrees(FMath::Asin(-ShadedUp)), SlonkFauna::SplashNightMax, DarkThrown, LitNotThrown));
	TestEqual(TEXT("no splash where the sun cannot light it"), DarkThrown, 0);
	TestEqual(TEXT("splashes by day and at dusk"), LitNotThrown, 0);

	// The pool: three bursts at once, a fourth a second later (four made); at 2.6 s the first three are over and lent again
	// (no more made); past their bursts every one comes back; and never more than the most, a burst past them let go.
	SlonkFauna::FSplashPool Pool;
	int32 Made = 0;
	auto Make = [&Made]() { ++Made; return NewObject<UNiagaraComponent>(GetTransientPackage()); };
	UNiagaraComponent* First[3];
	for (int32 i = 0; i < 3; ++i) { First[i] = Pool.Acquire(0.0, Make); }
	UNiagaraComponent* Fourth = Pool.Acquire(1.0, Make);
	const int32 MadeThen = Made;
	TSet<UNiagaraComponent*> Again;
	for (int32 i = 0; i < 3; ++i) { Again.Add(Pool.Acquire(SlonkFauna::SplashLifeSec + 0.1, Make)); }
	const bool bReused = Made == MadeThen && Again.Contains(First[0]) && Again.Contains(First[1]) && Again.Contains(First[2]) && !Again.Contains(Fourth);
	Pool.Tick(20.0);
	const bool bBack = Pool.NumLive() == 0 && Pool.NumResting() == Made;
	SlonkFauna::FSplashPool Full;
	int32 FullMade = 0, Lent = 0;
	auto MakeFull = [&FullMade]() { ++FullMade; return NewObject<UNiagaraComponent>(GetTransientPackage()); };
	for (int32 i = 0; i < SlonkFauna::MaxSplashComponents + 6; ++i) { Lent += Full.Acquire(0.5, MakeFull) ? 1 : 0; }
	AddInfo(FString::Printf(TEXT("the splashes' pool: %d made for 4 bursts, the first three lent again once over %s; %d in the air and %d resting after; %d bursts at once: %d lent, %d made, %d let go"),
		MadeThen, bReused ? TEXT("(no more made)") : TEXT("(more made)"), Pool.NumLive(), Pool.NumResting(), SlonkFauna::MaxSplashComponents + 6, Lent, FullMade, Full.NumDropped()));
	TestTrue(TEXT("a component for each burst while none is over"), MadeThen == 4 && First[0] && First[1] && First[2] && Fourth && First[0] != First[1]);
	TestTrue(TEXT("lent again once its burst is over, not before"), bReused);
	TestTrue(TEXT("every component comes back"), bBack);
	TestTrue(TEXT("never more than the most, a burst past them let go"), FullMade == SlonkFauna::MaxSplashComponents && Lent == SlonkFauna::MaxSplashComponents && Full.NumDropped() == 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaFishTest, "Slonk.Fauna.Fish", SLONK_TEST_FLAGS)
bool FSlonkFaunaFishTest::RunTest(const FString& Parameters)
{
	// An Earth fish: 55, 19 and 2 triangles (fewer than 80 at LOD 0), a metre from its tail's tip to its nose, its middle at
	// the origin; its vertex colours as the material reads them (its body and its fins, how far it bends from its middle
	// to its tail's tip, its side, a body that undulates), every face wound to show the side its normals point to, a
	// tangent basis at every vertex. The material's motion keeps every vertex inside the instance's padded box, whatever
	// its size, cycle, leap, surface, dive and speck size; its LODs come in where a fish is about 30 and 6 pixels long.
	using namespace UE::Geometry;
	namespace Vc = SlonkFauna::Vc;
	const FFaunaBodyPlan Plan = SlonkFauna::EarthPlan(EFaunaKind::Leaper);
	const int32 WantTris[SlonkFaunaMeshes::NumLods] = { 55, 19, 2 };
	int32 Tris[SlonkFaunaMeshes::NumLods] = {};
	int32 BadColour = 0, BadWinding = 0, Fins = 0, Bending = 0;
	FBox Box0(ForceInit);
	FDynamicMesh3 Lod0;
	for (int32 L = 0; L < SlonkFaunaMeshes::NumLods; ++L)
	{
		FDynamicMesh3 M;
		if (!SlonkFaunaMeshes::BuildLod(Plan, L, M)) { AddError(TEXT("no fish built")); return false; }
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
				const float WantBend = (float)FMath::Clamp((Vc::FishBendFromCm - P.X) / (Vc::FishBendFromCm + 50.0), 0.0, 1.0);
				const float WantSide = P.Y > 0.5 ? Vc::SideRight : (P.Y < -0.5 ? Vc::SideLeft : Vc::SideMiddle);
				BadColour += (Col.X == Vc::SurfaceBody || Col.X == Vc::SurfaceTail) && FMath::IsNearlyEqual(Col.Y, WantBend, 1e-3f) && Col.Z == WantSide && Col.W == Vc::JointUndulate ? 0 : 1;
				Fins += Col.X == Vc::SurfaceTail ? 1 : 0;
				Bending += Col.Y > 0.99f ? 1 : 0;
			}
		}
		if (L == 0) { Lod0 = MoveTemp(M); }
	}
	AddInfo(FString::Printf(TEXT("fish LODs %d, %d and %d triangles; LOD 0 %.0f x %.0f x %.0f cm of the unit mesh (z %.1f to %.1f); %d vertices off the colour contract, %d faces wound inward; %d of fins and tail, %d at the tail's tip"),
		Tris[0], Tris[1], Tris[2], Box0.GetSize().X, Box0.GetSize().Y, Box0.GetSize().Z, Box0.Min.Z, Box0.Max.Z, BadColour, BadWinding, Fins, Bending));
	for (int32 L = 0; L < SlonkFaunaMeshes::NumLods; ++L) { TestEqual(FString::Printf(TEXT("LOD %d's triangles"), L), Tris[L], WantTris[L]); }
	TestTrue(TEXT("fewer than 80 triangles at LOD 0"), Tris[0] < 80);
	TestEqual(TEXT("every vertex as the material reads it"), BadColour, 0);
	TestEqual(TEXT("every face shows the side its normals point to"), BadWinding, 0);
	TestTrue(TEXT("fins and a tail, bending to its tip"), Fins > 0 && Bending > 0);
	TestTrue(TEXT("a unit mesh a metre long, its middle at the origin"), FMath::IsNearlyEqual(Box0.Min.X, -50.0, 0.5) && FMath::IsNearlyEqual(Box0.Max.X, 50.0, 0.5));

	// The material's motion over LOD 0's vertices: every size, cycle, slot, leap, surface, dive and speck size, inside the box
	// each instance is culled by (UPlanetInstancesComponent::InstanceBox with the kind's padding).
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Leaper);
	const FBox Padded = Box0.ExpandBy(Box0.GetExtent() * 0.15 + FVector(20.0 + D.CullPaddingUnits));
	const TArray<FLinearColor> Vcs = VertexColours(Lod0);
	FRandomStream Rng(91);
	int32 Escaped = 0, Tried = 0, Shown = 0;
	double Margin = TNumericLimits<double>::Max();
	FVector Reach(0.0);
	for (int32 Case = 0; Case < 400; ++Case)
	{
		float Cu[SlonkFauna::Custom::Num] = {};
		float Gd[SlonkFauna::LeaperData::Num] = {};
		const float Size = Case % 5 == 0 ? (float)SlonkFauna::Leaper::MinSizeM : Rng.FRandRange((float)SlonkFauna::Leaper::MinSizeM, (float)SlonkFauna::Leaper::MaxSizeM);
		const int32 Count = Rng.RandRange(1, SlonkFauna::Leaper::MaxPerGroup), Idx = Rng.RandRange(0, Count - 1);
		Cu[SlonkFauna::Custom::SizeM] = Size;
		Cu[SlonkFauna::Custom::Seed] = Rng.FRand();
		Cu[SlonkFauna::Custom::Phase] = Rng.FRand();
		Cu[SlonkFauna::Custom::RateHz] = (float)SlonkFauna::Leaper::Cycles[Rng.RandRange(0, (int32)UE_ARRAY_COUNT(SlonkFauna::Leaper::Cycles) - 1)];
		Cu[SlonkFauna::Custom::PerKind] = (float)(8 * Idx + Count);
		// The surface where it leaves and dives back in anywhere within what is sent (the extremes often), the dive on or off.
		for (int32 k = 0; k < 2 * SlonkFauna::Leaper::MaxPerGroup; ++k)
		{
			Gd[SlonkFauna::LeaperData::Surface0 + k] = Case % 3 == 0 ? (Rng.FRand() < 0.5f ? -1.f : 1.f) * SlonkFauna::Leaper::MaxSurfaceCm : Rng.FRandRange(-1.f, 1.f) * SlonkFauna::Leaper::MaxSurfaceCm;
		}
		Gd[SlonkFauna::LeaperData::Diving] = Case % 4 == 0 ? 1.f : 0.f;
		Gd[SlonkFauna::LeaperData::DiveT0] = Rng.FRandRange(0.f, 3600.f);
		Gd[SlonkFauna::LeaperData::DiveLen] = Rng.FRandRange(0.f, 70.f);
		// Within a leap mostly, else anywhere in the hour.
		float T = Rng.FRandRange(0.f, 3600.f);
		if (Case % 4 != 3)
		{
			const SlonkFauna::FLeap L = SlonkFauna::LeapOf(Cu, Rng.RandRange(0, 200));
			T = MaterialTime(L.T0 - L.LeadSec + (L.FlightSec + 2.0 * L.LeadSec) * Rng.FRand());
		}
		Shown += SlonkFauna::LeaperShown(T, Cu, Gd) ? 1 : 0;
		// Near (no speck floor), a little past where it comes in, and far (twice its size).
		const float Dist = Case % 3 == 0 ? 2000.f : (Case % 3 == 1 ? Size * 100.f * 960.f / 1.5f : 4.0e5f);
		for (const int32 V : Lod0.VertexIndicesItr())
		{
			const FVector3f LP = (FVector3f)Lod0.GetVertex(V);
			const FVector3f Moved = LP + SlonkFauna::LeaperMotion(LP, Vcs[V], T, Cu, Gd, Dist, 960.f);
			++Tried;
			const FVector Mv((double)Moved.X, (double)Moved.Y, (double)Moved.Z);
			Escaped += Padded.IsInside(Mv) ? 0 : 1;
			Reach = FVector(FMath::Max(Reach.X, FMath::Abs(Mv.X)), FMath::Max(Reach.Y, FMath::Abs(Mv.Y)), FMath::Max(Reach.Z, FMath::Abs(Mv.Z)));
			Margin = FMath::Min(Margin, FMath::Min3(FMath::Min(Mv.X - Padded.Min.X, Padded.Max.X - Mv.X), FMath::Min(Mv.Y - Padded.Min.Y, Padded.Max.Y - Mv.Y), FMath::Min(Mv.Z - Padded.Min.Z, Padded.Max.Z - Mv.Z)));
		}
	}
	AddInfo(FString::Printf(TEXT("%d vertices moved by the material (%d of 400 cases in a leap): %d outside the box padded %.0f units (the closest %.0f units inside; reaching %.0f, %.0f and %.0f units from the origin)"),
		Tried, Shown, Escaped, D.CullPaddingUnits, Margin, Reach.X, Reach.Y, Reach.Z));
	TestTrue(TEXT("cases in a leap and out of one"), Shown > 150 && Shown < 400);
	TestEqual(TEXT("the material keeps every vertex in its instance's box"), Escaped, 0);
	// Those hold of the float copies only while the material's leaper branches are what was copied: an edit to either
	// branch of fauna_material.py alone fails here, until the copies are brought in line and the CRCs with them.
	const FString WpoBlock = MaterialMotionBlock(EMotionBlock::Leapers);
	const FString PixelBlock = MaterialMotionBlock(EMotionBlock::Leapers, true);
	const uint32 WpoCrc = FCrc::StrCrc32(*WpoBlock), PixelCrc = FCrc::StrCrc32(*PixelBlock);
	AddInfo(FString::Printf(TEXT("fauna_material.py's leaper branch: in WPO_CODE %d characters, CRC 0x%08x (0x%08x when the copy was made); in CODE %d characters, CRC 0x%08x (0x%08x)"),
		WpoBlock.Len(), WpoCrc, MaterialLeaperBlockCrc, PixelBlock.Len(), PixelCrc, MaterialLeaperPixelBlockCrc));
	TestTrue(TEXT("fauna_material.py's leaper branches found"), WpoBlock.Contains(TEXT("float lpitch = atan2(")) && PixelBlock.Contains(TEXT("bool lshown = ")));
	TestEqual(TEXT("LeaperMotion copies the material's leaper branch as it is now"), WpoCrc, MaterialLeaperBlockCrc);
	TestEqual(TEXT("LeaperShown and LeaperTurn copy its pixel shader's as it is now"), PixelCrc, MaterialLeaperPixelBlockCrc);
	// The fliers' part of WPO_CODE leaves the leaper branch out (Slonk.Fauna.Meshes holds it to its own CRC).
	TestFalse(TEXT("the fliers' part holds none of the leaper branch"), MaterialMotionBlock().Contains(TEXT("lpitch")));

	// The static mesh: made once and kept, three LODs at the kind's screen sizes; where they come in for a fish 60 cm long
	// through a 90-degree view at 16:9 (the GPU's radius, its padded box's), and how long it is there in pixels.
	UStaticMesh* Mesh = SlonkFaunaMeshes::GetMesh(Plan, EFaunaKind::Leaper);
	TestTrue(TEXT("the fish's mesh is made, and kept"), Mesh != nullptr && Mesh == SlonkFaunaMeshes::GetMesh(Plan, EFaunaKind::Leaper));
	if (Mesh)
	{
		TestEqual(TEXT("with three LODs"), Mesh->GetNumLODs(), SlonkFaunaMeshes::NumLods);
		const double LengthM = 0.6;
		UPlanetInstancesComponent* Set = NewObject<UPlanetInstancesComponent>();
		Set->SetStaticMesh(Mesh);
		Set->SetCullPadding(D.CullPaddingUnits);
		Set->SetPieces(TArray<FTransform>{ FTransform(FQuat::Identity, FVector::ZeroVector, FVector(LengthM)) }, TConstArrayView<float>());
		const double Proj = (16.0 / 9.0) / FMath::Tan(FMath::DegreesToRadians(45.0));
		const double GpuR = Set->GetMaxInstanceRadius();
		auto ComesInM = [&](int32 L) { return Proj * GpuR / D.LodScreenSize[L] / 100.0; };
		auto PxAt = [&](double M) { return LengthM * 960.0 / M; };
		AddInfo(FString::Printf(TEXT("a %.1f m fish (LOD radius %.0f cm): 19 triangles from %.0f m (%.1f pixels long), 2 from %.0f m (%.1f pixels)"), LengthM, GpuR, ComesInM(1), PxAt(ComesInM(1)), ComesInM(2), PxAt(ComesInM(2))));
		TestTrue(TEXT("its 19 triangles from about 30 pixels long"), PxAt(ComesInM(1)) > 24.0 && PxAt(ComesInM(1)) < 38.0);
		TestTrue(TEXT("and its 2 from about 6"), PxAt(ComesInM(2)) > 4.5 && PxAt(ComesInM(2)) < 8.0);
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
		AddInfo(FString::Printf(TEXT("the fish's mesh: %d vertices over its LODs, %d without a tangent basis at right angles to its normal"), Vertices, NoTangent));
		TestTrue(TEXT("every vertex has a tangent basis"), Vertices > 0 && NoTangent == 0);
	}

	// Its colour: an Earth fish's silver-blue at strangeness 0, a little of its sea's colour through it; turned round the
	// wheel with a strange world, as the other animals' are.
	const SlonkFauna::FFaunaLook Earth;
	const FLinearColor Water = SlonkFauna::LeaperPigment(Earth, FLinearColor(0.1f, 0.35f, 0.5f), 1.0);
	SlonkFauna::FFaunaLook Strange;
	Strange.HueTurnDeg = 120.f;
	Strange.Vivid = 1.f;
	const FLinearColor Turned = SlonkFauna::LeaperPigment(Strange, FLinearColor(0.1f, 0.35f, 0.5f), 1.0);
	const FLinearColor Ammonia = SlonkFauna::LeaperPigment(Earth, FLinearColor(0.22f, 0.17f, 0.40f), 1.0);
	AddInfo(FString::Printf(TEXT("a fish's pigment: %.2f %.2f %.2f on an Earth (hue %.0f deg), %.2f %.2f %.2f in an ammonia sea, %.2f %.2f %.2f on a strange world (hue %.0f deg)"),
		Water.R, Water.G, Water.B, HueOf(Water), Ammonia.R, Ammonia.G, Ammonia.B, Turned.R, Turned.G, Turned.B, HueOf(Turned)));
	TestTrue(TEXT("silver-blue at strangeness 0"), HueOf(Water) > 190.f && HueOf(Water) < 230.f && Water.B > Water.R && Water.GetMax() > 0.35f);
	TestTrue(TEXT("tinted by its sea's solvent"), !Ammonia.Equals(Water, 0.01f));
	TestTrue(TEXT("turned with a strange world"), HueGap(HueOf(Turned), HueOf(Water)) > 60.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaLeaperStaysPutTest, "Slonk.Fauna.LeaperStaysPut", SLONK_TEST_FLAGS)
bool FSlonkFaunaLeaperStaysPutTest::RunTest(const FString& Parameters)
{
	// The leapers are pure functions of the world and the place, as every kind is: a cell holds the same group whatever
	// order the cells are read in; a group's fish are the same every time; the groups about a camera are chosen by distance
	// alone (gone 30 km off and come back, the same groups, the same as about a camera that never left, their frames where
	// theirs are); one a little past its reach stays, a quarter past it goes; none about a camera too high over the sea.
	SlonkFauna::FFaunaWorld W = EarthSeas();
	W.KindMask = SlonkFauna::KindBit(EFaunaKind::Leaper);
	const double R = W.P.RadiusCm;
	SlonkFauna::FFaunaGroup G0;
	if (!TestLeapers(W, G0)) { AddError(TEXT("no group of leapers found on the Earth-like world")); return false; }
	TArray<SlonkFauna::FFaunaCell> Cells;
	SlonkFauna::CellsNear(W.P, EFaunaKind::Leaper, G0.Centre, 2.0e5, Cells);
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
	const FFish Fa = FishOf(G0), Fb = FishOf(G0);
	bool bFishSame = Fa.Num() == Fb.Num() && Fa.Custom == Fb.Custom && Fa.Num() == G0.Members;
	for (int32 i = 0; bFishSame && i < Fa.Num(); ++i) { bFishSame &= Fa.Xf[i].Equals(Fb.Xf[i], 0.0); }
	// Gone to B and back to A.
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
	for (const TPair<uint64, SlonkFauna::FFaunaField::FGroupRef>& A : F.GetActive())
	{
		const SlonkFauna::FFaunaGroup* Was = First.Find(A.Key);
		const SlonkFauna::FFaunaField::FGroupRef* Never = Stayed.GetActive().Find(A.Key);
		Changed += Was && *Was == *A.Value && Never && **Never == *A.Value ? 0 : 1;
		if (Never) { Moved += SlonkFauna::PathAt(*A.Value, R, 1234.5).PosCm.Equals(SlonkFauna::PathAt(**Never, R, 1234.5).PosCm, 0.0) ? 0 : 1; }
	}
	// The hysteresis, on a sparse sea (a few groups within reach, the caps leaving room).
	SlonkFauna::FFaunaWorld Sparse = W;
	Sparse.Density = 0.15f;
	SlonkFauna::FFaunaGroup G1;
	bool bSparse = TestLeapers(Sparse, G1);
	const double Reach = SlonkFauna::KindDesc(EFaunaKind::Leaper).ReachCm;
	TArray<uint64> AtG1, Near, Far, High;
	int32 NearGroups = 0;
	if (bSparse)
	{
		SlonkFauna::FFaunaField SparseField;
		SparseField.SetWorld(Sparse);
		ScanAndSelect(SparseField, CameraOver(Sparse, G1.Centre), AtG1);
		ScanAndSelect(SparseField, CameraOver(Sparse, AlongGround(G1.Centre, 1.1 * Reach, R, 0.7)), Near);
		NearGroups = SparseField.ActiveGroups(EFaunaKind::Leaper);
		ScanAndSelect(SparseField, CameraOver(Sparse, AlongGround(G1.Centre, 1.3 * Reach, R, 0.7)), Far);
	}
	ScanAndSelect(F, G0.Centre * (R + SlonkFauna::KindDesc(EFaunaKind::Leaper).MaxAltitudeCm * 1.4), High);
	AddInfo(FString::Printf(TEXT("%d cells round a group of %d: %d hold one, %d read otherwise backward; its fish the same made twice %s; about A %d groups, about B 30 km off %d (%d of A's), back at A %d, %d of them changed, %d off where the groups never left are; %d about a camera too high"),
		Cells.Num(), G0.Members, Held, Mismatch, bFishSame ? TEXT("yes") : TEXT("no"), A1.Num(), B1.Num(), Overlap, A2.Num(), Changed, Moved, High.Num()));
	TestEqual(TEXT("a cell holds the same group whatever order the cells are read in"), Mismatch, 0);
	TestTrue(TEXT("a group's fish are the same every time"), bFishSame);
	TestTrue(TEXT("groups about A, the test's group among them"), A1.Num() > 0 && A1.Contains(G0.Id));
	TestEqual(TEXT("none of A's about B"), Overlap, 0);
	TestTrue(TEXT("back at A, the same groups, as about a camera that never left"), A2 == A1 && Kept == A1);
	TestEqual(TEXT("each the same group as before and as one never left"), Changed, 0);
	TestEqual(TEXT("where one never left would be"), Moved, 0);
	TestTrue(TEXT("on the sparse sea, its group about its middle, and room under the caps past it"), bSparse && AtG1.Contains(G1.Id) && NearGroups < SlonkFauna::KindDesc(EFaunaKind::Leaper).MaxGroups);
	TestTrue(TEXT("a group just past its reach stays"), Near.Contains(G1.Id));
	TestFalse(TEXT("a group a quarter past its reach goes"), Far.Contains(G1.Id));
	TestEqual(TEXT("none under a camera too high over the sea"), High.Num(), 0);

	// Fauna switched off and on (the seas' shoals taken out of the terrain's material and put back, the world otherwise the
	// same): only the leapers' groups and cells go; every other kind's groups about and cells read are kept, so the scans
	// after read none of theirs again, and back on the same groups are about as before.
	{
		const SlonkFauna::FFaunaWorld All = EarthSeas();
		SlonkFauna::FFaunaWorld NoSea = All;
		NoSea.Sea = SlonkFauna::FFaunaSea();
		auto CellsOf = [](const SlonkFauna::FFaunaField& Fd, bool bLeapers)
		{
			int32 N = 0;
			if (Fd.GetCache().IsValid())
			{
				for (const TPair<uint64, SlonkFauna::FFaunaField::FGroupRef>& C : *Fd.GetCache()) { N += ((EFaunaKind)(C.Key >> 60) == EFaunaKind::Leaper) == bLeapers ? 1 : 0; }
			}
			return N;
		};
		auto OthersAbout = [](const SlonkFauna::FFaunaField& Fd)
		{
			TArray<uint64> Ids;
			for (const TPair<uint64, SlonkFauna::FFaunaField::FGroupRef>& A : Fd.GetActive()) { if (A.Value->Kind != EFaunaKind::Leaper) { Ids.Add(A.Key); } }
			Ids.Sort();
			return Ids;
		};
		// Over the sea near a coast, where groups of leapers and of other kinds are about together (the first such of points
		// of sea over the globe, the shallows by the coasts among them; else over the test's group).
		SlonkFauna::FFaunaField Fd;
		TArray<uint64> Before, During, After;
		FVector Cam = CamA;
		for (const FVector& Dir : SeaSpots(All.P, 3000, 300.0, 300))
		{
			SlonkFauna::FFaunaField Try;
			Try.SetWorld(All);
			TArray<uint64> Ids;
			const FVector At = CameraOver(All, Dir);
			ScanAndSelect(Try, At, Ids);
			if (OthersAbout(Try).Num() > 0 && Try.ActiveGroups(EFaunaKind::Leaper) > 0) { Cam = At; break; }
		}
		Fd.SetWorld(All);
		int32 EvalOff = -1, EvalOn = -1;
		ScanAndSelect(Fd, Cam, Before);
		const int32 OthersCells = CellsOf(Fd, false), LeaperCells = CellsOf(Fd, true);
		const TArray<uint64> OthersBefore = OthersAbout(Fd);
		Fd.SetWorld(NoSea);
		const int32 OthersCellsOff = CellsOf(Fd, false), LeaperCellsOff = CellsOf(Fd, true);
		const TArray<uint64> OthersOff = OthersAbout(Fd);
		const int32 LeapersOff = Fd.ActiveGroups(EFaunaKind::Leaper) + Fd.GetCandidates().FilterByPredicate([](const SlonkFauna::FFaunaField::FGroupRef& G) { return G->Kind == EFaunaKind::Leaper; }).Num();
		ScanAndSelect(Fd, Cam, During, nullptr, &EvalOff);
		Fd.SetWorld(All);
		ScanAndSelect(Fd, Cam, After, nullptr, &EvalOn);
		AddInfo(FString::Printf(TEXT("fauna off and on over the sea near a coast: %d groups about before (%d of other kinds), %d cells of other kinds and %d of the leapers read; off, %d and %d cells kept, %d of the other kinds' groups about, %d leaper groups left, its scan reading %d cells; on, %d groups about, its scan reading %d cells"),
			Before.Num(), OthersBefore.Num(), OthersCells, LeaperCells, OthersCellsOff, LeaperCellsOff, OthersOff.Num(), LeapersOff, EvalOff, After.Num(), EvalOn));
		TestTrue(TEXT("cells of other kinds and of the leapers read there, groups of both about"), OthersCells > 0 && LeaperCells > 0 && OthersBefore.Num() > 0 && Before.Num() > OthersBefore.Num());
		TestTrue(TEXT("off, the other kinds' cells and groups kept, the leapers' gone"), OthersCellsOff == OthersCells && LeaperCellsOff == 0 && OthersOff == OthersBefore && LeapersOff == 0);
		TestEqual(TEXT("off, its scan reads no cell again"), EvalOff, 0);
		TestEqual(TEXT("on, its scan reads the leapers' cells alone"), EvalOn, LeaperCells);
		TestTrue(TEXT("and the same groups are about as before"), After == Before);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaLeaperBudgetTest, "Slonk.Fauna.LeaperBudget", SLONK_TEST_FLAGS)
bool FSlonkFaunaLeaperBudgetTest::RunTest(const FString& Parameters)
{
	// About any camera over an Earth's seas the leapers keep to their caps (6 groups, 30 fish, under 1,700 triangles at their
	// nearest LOD), the caps full over the open sea; and a frame's math for them (each group's frame with its shoal, its dive
	// and the threats to it, the surface its fish's leaps cross and the crossings to splash for, as UpdateLeaper does it:
	// not the scene's writes, which want a world) is far under 0.15 ms of the game thread.
	SlonkFauna::FFaunaWorld W = EarthSeas();
	W.KindMask = SlonkFauna::KindBit(EFaunaKind::Leaper);
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Leaper);
	const double R = W.P.RadiusCm;
	int32 Places = 0, Over = 0, Full = 0, MaxGroups = 0, MaxFish = 0;
	SlonkFauna::FFaunaField Busiest;
	for (const FVector& Dir : SeaSpots(W.P, 2000, 5.0e3, 30))
	{
		SlonkFauna::FFaunaField F;
		F.SetWorld(W);
		TArray<uint64> Active;
		ScanAndSelect(F, CameraOver(W, Dir), Active);
		++Places;
		const int32 Groups = F.ActiveGroups(EFaunaKind::Leaper), Fish = F.ActiveMembers(EFaunaKind::Leaper);
		Over += Groups <= D.MaxGroups && Fish <= D.MaxMembers ? 0 : 1;
		Full += Groups == D.MaxGroups || Fish + SlonkFauna::Leaper::MaxPerGroup > D.MaxMembers ? 1 : 0;
		if (Groups > MaxGroups) { MaxGroups = Groups; Busiest = F; }
		MaxFish = FMath::Max(MaxFish, Fish);
	}
	// A frame's math at the busiest place, as UpdateLeaper does it, a ship passing low over the first group.
	struct FAbout { SlonkFauna::FFaunaField::FGroupRef G; FFish F; SlonkFauna::FLeaperSurfaceCache Cache; SlonkFauna::FPanic Dive; };
	TArray<FAbout> About;
	for (const TPair<uint64, SlonkFauna::FFaunaField::FGroupRef>& A : Busiest.GetActive()) { FAbout Ab; Ab.G = A.Value; Ab.F = FishOf(*A.Value); About.Add(MoveTemp(Ab)); }
	constexpr int32 Frames = 600;
	double Sink = 0.0;
	int32 Splashes = 0;
	const double Start = FPlatformTime::Seconds();
	for (int32 f = 0; f < Frames; ++f)
	{
		const double Now = 500.0 + f / 60.0, Was = Now - 1.0 / 60.0;
		for (int32 g = 0; g < About.Num(); ++g)
		{
			FAbout& Ab = About[g];
			const SlonkFauna::FFaunaPose Pose = SlonkFauna::PathAt(*Ab.G, R, Now);
			SlonkFauna::FFaunaThreat Craft;
			Craft.PosCm = Pose.PosCm.GetSafeNormal() * (R + (g == 0 ? 2000.0 : 1.0e4));
			Craft.AltitudeCm = g == 0 ? 2000.0 : 1.0e4;
			SlonkFauna::FFaunaThreat Walker;
			Walker.bCraft = false;
			Walker.PosCm = Pose.PosCm;
			SlonkFauna::StartleLeapers(Ab.Dive, D, Pose.PosCm, Craft, Now);
			SlonkFauna::StartleLeapers(Ab.Dive, D, Pose.PosCm, Walker, Now);
			float Gd[SlonkFauna::LeaperData::Num] = {};
			SlonkFauna::LeaperGroupData(*Ab.G, Ab.F.Custom, Ab.F.Home, R, W.Sea.SwellCm, SlonkFauna::SwellFade(5000.0), Now, Ab.Dive, Ab.Cache, Gd);
			TArray<SlonkFauna::FLeapEvent> Ev;
			for (int32 i = 0; i < Ab.F.Num(); ++i) { SlonkFauna::LeapEventsBetween(Ab.F.At(i), i, Was, Now, Ev); }
			for (const SlonkFauna::FLeapEvent& E : Ev)
			{
				if (SlonkFauna::LeapDived(Ab.Dive, E.Leap.T0)) { continue; }
				Sink += SlonkFauna::LeapCrossing(*Ab.G, R, W.Sea.SwellCm, 1.0, Ab.F.Home[E.Member], E.Leap, E.bEntry).X;
				++Splashes;
			}
			Sink += Pose.PosCm.X + Gd[SlonkFauna::LeaperData::Surface0];
		}
	}
	const double FrameMs = (FPlatformTime::Seconds() - Start) * 1000.0 / Frames;
	AddInfo(FString::Printf(TEXT("about %d cameras over the sea: at most %d groups and %d fish (%d triangles at LOD 0), the caps full at %d of them, %d over; a frame's math on %d groups %.4f ms (%d splashes over %d frames)"),
		Places, MaxGroups, MaxFish, MaxFish * 55, Full, Over, About.Num(), FrameMs, Splashes, Frames));
	TestTrue(TEXT("places over the sea to try"), Places >= 20 && MaxGroups > 0);
	TestEqual(TEXT("never over the leapers' caps"), Over, 0);
	TestTrue(TEXT("the caps full over the open sea"), Full > 0);
	TestTrue(TEXT("under 1,700 triangles of fish at LOD 0"), MaxFish * 55 <= 1700);
	TestTrue(TEXT("splashes thrown in the frames timed"), Splashes > 0 && FMath::IsFinite(Sink));
	TestTrue(TEXT("a frame's math far under 0.15 ms"), FrameMs < 0.15);
	return true;
}

namespace
{
	/** Where "spawnplanet terrestrial [strange s] seed N" puts a world from the game's start (the ship's own place puts seed
	 *  0's there; each seed moves the place its seed is drawn from on by 1e9 cm along x, so SpawnedEarthParams is seed 1's),
	 *  and the hash its style, its genome and its animals are rolled from (APlanetBody's, of that place). */
	FVector SpawnedPlace(int32 SeedPick) { return FVector(-2125199545.789, -1406367466.411, 0.0) + FVector(1.0e9 * SeedPick, 0.0, 0.0); }
	uint32 PlaceHash(const FVector& L)
	{
		return HashCombine(HashCombine(GetTypeHash(FMath::RoundToInt64(L.X / 1000.0)), GetTypeHash(FMath::RoundToInt64(L.Y / 1000.0))), GetTypeHash(FMath::RoundToInt64(L.Z / 1000.0)));
	}
	/** That world's field, as SpawnedEarthParams makes seed 1's. */
	FPlanetTerrainParams SpawnedParams(int32 SeedPick)
	{
		const FVector L = SpawnedPlace(SeedPick);
		FRandomStream Rng(static_cast<int32>(PlaceHash(L)));
		const float SeaLevel = Rng.FRandRange(0.45f, 0.6f);
		for (int32 i = 0; i < 4; ++i) { Rng.FRandRange(0.f, 1.f); }
		const float IceCapStart = Rng.FRandRange(0.72f, 0.8f);
		FPlanetTerrainParams P = FaunaEarthParams(6371.0);
		P.Seed = FVector(FMath::Frac(L.X / 7.3e9) * 100.0, FMath::Frac(L.Y / 5.1e9) * 100.0, FMath::Frac(L.Z / 3.7e9) * 100.0);
		P.SeaLevel = SeaLevel;
		P.IceCapStart = IceCapStart;
		P.Craters = 0.1f;
		P.Canyons = 0.5f;
		return P;
	}
	/** That world's jellies at strangeness S, full of life (its genome rolled as the spawn rolls it: no star's bearing, so it
	 *  spins). */
	SlonkFauna::FFaunaWorld SpawnedJellies(int32 SeedPick, float S)
	{
		const uint32 Hash = PlaceHash(SpawnedPlace(SeedPick));
		FWorldGenome G = SlonkGenome::Roll(EPlanetArchetype::Terrestrial, S, Hash);
		SlonkGenome::SettleLock(G, EPlanetArchetype::Terrestrial, FVector::ZeroVector, false);
		SlonkFauna::FFaunaWorld W;
		W.P = SpawnedParams(SeedPick);
		W.Profile = SlonkFauna::Roll(G, EPlanetArchetype::Terrestrial, Hash);
		W.Life = 1.f;
		W.KindMask = SlonkFauna::KindBit(EFaunaKind::Floater);
		return W;
	}
	/** The Earth-like world's jellies alone at strangeness S, its genome and its animals rolled from Hash, full of life. */
	SlonkFauna::FFaunaWorld StrangeJellies(float S, uint32 Hash)
	{
		SlonkFauna::FFaunaWorld W;
		W.P = FaunaEarthParams(6371.0);
		W.Profile = SlonkFauna::Roll(SlonkGenome::Roll(EPlanetArchetype::Terrestrial, S, Hash), EPlanetArchetype::Terrestrial, Hash);
		W.Life = 1.f;
		W.KindMask = SlonkFauna::KindBit(EFaunaKind::Floater);
		return W;
	}
	/** The first of those (hashes 7919 apart) whose jellies are in MinShare to MaxShare of its cells. */
	bool JellyWorld(float S, float MinShare, float MaxShare, SlonkFauna::FFaunaWorld& Out)
	{
		for (uint32 k = 1; k < 20000; ++k)
		{
			const FWorldGenome G = SlonkGenome::Roll(EPlanetArchetype::Terrestrial, S, k * 7919u);
			if (!SlonkFauna::FloatersLive(G)) { continue; }
			SlonkFauna::FFaunaWorld W = StrangeJellies(S, k * 7919u);
			if (W.Profile.FloaterShare < MinShare || W.Profile.FloaterShare > MaxShare) { continue; }
			Out = MoveTemp(W);
			return true;
		}
		return false;
	}
	/** A kind's groups in the cells under N directions spread evenly over the globe. */
	void KindOverGlobe(const SlonkFauna::FFaunaWorld& W, EFaunaKind Kind, int32 N, TArray<SlonkFauna::FFaunaGroup>& Out, int32* OutCells = nullptr)
	{
		Out.Reset();
		TSet<uint64> Seen;
		for (int32 i = 0; i < N; ++i)
		{
			const SlonkFauna::FFaunaCell C = SlonkFauna::CellOf(W.P, Kind, Spread(i, N));
			bool bAlready = false;
			Seen.Add(C.Id(), &bAlready);
			if (bAlready) { continue; }
			SlonkFauna::FFaunaGroup G;
			if (SlonkFauna::GroupInCell(W, C, G)) { Out.Add(MoveTemp(G)); }
		}
		if (OutCells) { *OutCells = Seen.Num(); }
	}
	/** A jelly to test with: the nearest to a fixed direction. */
	bool TestJelly(const SlonkFauna::FFaunaWorld& W, SlonkFauna::FFaunaGroup& Out)
	{
		return SlonkFauna::FindGroupNear(W, EFaunaKind::Floater, FVector(0.3, 0.8, 0.5).GetSafeNormal(), 4.0e7, 20000, Out);
	}
	/** A mesh's UV at each vertex. */
	TArray<FVector2f> VertexUVs(const UE::Geometry::FDynamicMesh3& M)
	{
		TArray<FVector2f> Out;
		Out.Init(FVector2f::ZeroVector, M.MaxVertexID());
		const UE::Geometry::FDynamicMeshUVOverlay* U = M.Attributes()->PrimaryUV();
		for (const int32 El : U->ElementIndicesItr())
		{
			const int32 V = U->GetParentVertex(El);
			if (V >= 0) { Out[V] = U->GetElement(El); }
		}
		return Out;
	}
	/** The ground under a jelly whose middle is at a planet-local place, cm over the sea-level sphere: the highest, from At
	 *  (a unit direction's ground, the sea's surface over water), under its middle and at six points round it as far out as
	 *  its rim and its trailing tentacles reach (half its span and its hang: its tentacles are at most twice its hang long,
	 *  and trail and wave at most half their length). */
	double JellyGroundCm(const SlonkFauna::FFaunaGroup& G, double RadiusCm, const FVector& PosCm, TFunctionRef<double(const FVector&)> At)
	{
		const FVector Up = PosCm.GetSafeNormal();
		const FVector E = FVector::CrossProduct(FMath::Abs(Up.Z) < 0.95 ? FVector::ZAxisVector : FVector::XAxisVector, Up).GetSafeNormal();
		const FVector N = FVector::CrossProduct(Up, E);
		const double Reach = 50.0 * G.SpanM + G.HangCm;
		double H = At(Up);
		for (int32 k = 0; k < 6; ++k)
		{
			const double A = UE_DOUBLE_TWO_PI * k / 6.0;
			H = FMath::Max(H, At((Up + (E * FMath::Cos(A) + N * FMath::Sin(A)) * (Reach / RadiusCm)).GetSafeNormal()));
		}
		return H;
	}
	/** How far a jelly's lowest point (its tentacles' tips, drawn at twice its size far off) clears the ground under it, cm,
	 *  with its middle at a planet-local place. */
	double JellyClearanceCm(const SlonkFauna::FFaunaGroup& G, double RadiusCm, const FVector& PosCm, TFunctionRef<double(const FVector&)> At)
	{
		return PosCm.Size() - RadiusCm - 2.0 * G.HangCm - JellyGroundCm(G, RadiusCm, PosCm, At);
	}
	/** Every plan of jelly a world may roll at the ends of its ranges: 6, 8 or 12 ribs, 6 to 12 tentacles, the lowest and the
	 *  tallest bell, the shortest and the longest tentacles, its rim cut deepest; and a few rolled. */
	TArray<FFaunaBodyPlan> JellyPlans()
	{
		namespace Fl = SlonkFauna::Floater;
		TArray<FFaunaBodyPlan> Plans;
		for (const uint8 Sym : { (uint8)6, (uint8)8, (uint8)12 })
		{
			for (uint8 Pairs = 3; Pairs <= 6; ++Pairs)
			{
				for (const float Bell : { Fl::MinBell, Fl::MaxBell })
				{
					for (const float Tent : { Fl::MinTentacles, Fl::MaxTentacles })
					{
						FFaunaBodyPlan P = SlonkFauna::EarthPlan(EFaunaKind::Floater);
						P.Symmetry = Sym;
						P.LimbPairs = Pairs;
						P.Length = Bell;
						P.Tail = Bell * Tent;
						P.Girth = 1.f;
						P.Chord = 0.1f;
						P.GlowOrgans = Sym;
						Plans.Add(P);
					}
				}
			}
		}
		for (uint32 Hash = 1; Hash <= 8; ++Hash) { Plans.Add(SlonkFauna::FloaterPlan(Hash * 7919u)); }
		return Plans;
	}
	/** A jelly's triangles at a LOD by its tentacles' count, as built: its bell's 168 or 36, its tentacles' 72 or 12 quads
	 *  among them (each at least one), or 16. */
	int32 WantJellyTris(int32 Tentacles, int32 Lod)
	{
		if (Lod >= 2) { return 16; }
		switch (Tentacles)
		{
		case 6: return Lod == 0 ? 312 : 60;
		case 8: return Lod == 0 ? 312 : 68;
		case 10: return Lod == 0 ? 308 : 56;
		case 12: return Lod == 0 ? 312 : 60;
		default: return -1;
		}
	}
	/** How a jelly's LOD covers the sky seen from far off straight along a view direction (its rest pose, the view square
	 *  to it): on a grid of samples Cell unit-mesh cm apart over the plane square to the view, the area of those a drawn
	 *  point of it covers (its membrane's pores left out: SlonkFauna::FloaterCover), and the glow at night of the nearest
	 *  drawn point at each (SlonkFauna::FloaterLight at the dimmest moment of its pulse, over the light it always has: 1
	 *  on a tentacle, its ribs and ring canal on its bell), all of it and its tentacles' alone; unit-mesh cm squared. */
	struct FJellySight
	{
		double Covered = 0.0, Glow = 0.0, TentacleGlow = 0.0;
	};
	FJellySight SeeJelly(const UE::Geometry::FDynamicMesh3& M, const FVector& View, const float* Cu, double Cell = 0.5)
	{
		const FVector Fwd = View.GetSafeNormal();
		const FVector Right = FVector::CrossProduct(FMath::Abs(Fwd.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector, Fwd).GetSafeNormal();
		const FVector Up = FVector::CrossProduct(Fwd, Right);
		constexpr double Half = 135.0;
		const int32 N = FMath::CeilToInt32(2.0 * Half / Cell);
		TArray<float> Depth, Light;
		TArray<uint8> Tentacle;
		Depth.Init(TNumericLimits<float>::Max(), N * N);
		Light.Init(0.f, N * N);
		Tentacle.Init(0, N * N);
		const TArray<FLinearColor> Vcs = VertexColours(M);
		const TArray<FVector2f> Uvs = VertexUVs(M);
		// The dimmest moment of its pulse: its flash runs from 0 to 0.3 of a pulse, 0.6 of one later at its tentacles' tips
		// than at its crown, so 0.95 of the way through one from its phase none of it is flashing.
		const float Gd[SlonkFauna::FloaterData::Num] = {};
		const float Dim = FMath::Max(FMath::Frac(0.95f - Cu[SlonkFauna::Custom::Phase]), 0.f) / FMath::Max(Cu[SlonkFauna::Custom::RateHz], 1e-3f);
		for (const int32 T : M.TriangleIndicesItr())
		{
			const UE::Geometry::FIndex3i Tri = M.GetTriangle(T);
			FVector2D S[3];
			double Z[3];
			for (int32 k = 0; k < 3; ++k)
			{
				const FVector P = (FVector)M.GetVertex(Tri[k]);
				S[k] = FVector2D(FVector::DotProduct(P, Right), FVector::DotProduct(P, Up));
				Z[k] = FVector::DotProduct(P, Fwd);
			}
			const double Area2 = (S[1].X - S[0].X) * (S[2].Y - S[0].Y) - (S[2].X - S[0].X) * (S[1].Y - S[0].Y);
			if (FMath::Abs(Area2) < 1e-9) { continue; }
			const int32 X0 = FMath::Clamp(FMath::FloorToInt32((FMath::Min3(S[0].X, S[1].X, S[2].X) + Half) / Cell), 0, N - 1);
			const int32 X1 = FMath::Clamp(FMath::FloorToInt32((FMath::Max3(S[0].X, S[1].X, S[2].X) + Half) / Cell), 0, N - 1);
			const int32 Y0 = FMath::Clamp(FMath::FloorToInt32((FMath::Min3(S[0].Y, S[1].Y, S[2].Y) + Half) / Cell), 0, N - 1);
			const int32 Y1 = FMath::Clamp(FMath::FloorToInt32((FMath::Max3(S[0].Y, S[1].Y, S[2].Y) + Half) / Cell), 0, N - 1);
			for (int32 y = Y0; y <= Y1; ++y)
			{
				for (int32 x = X0; x <= X1; ++x)
				{
					const FVector2D Q(-Half + (x + 0.5) * Cell, -Half + (y + 0.5) * Cell);
					const double W0 = ((S[1].X - Q.X) * (S[2].Y - Q.Y) - (S[2].X - Q.X) * (S[1].Y - Q.Y)) / Area2;
					const double W1 = ((S[2].X - Q.X) * (S[0].Y - Q.Y) - (S[0].X - Q.X) * (S[2].Y - Q.Y)) / Area2;
					const double W2 = 1.0 - W0 - W1;
					if (W0 < 0.0 || W1 < 0.0 || W2 < 0.0) { continue; }
					const int32 I = y * N + x;
					const float D = (float)(W0 * Z[0] + W1 * Z[1] + W2 * Z[2]);
					if (D >= Depth[I]) { continue; }
					const FVector2f Uv = Uvs[Tri.A] * (float)W0 + Uvs[Tri.B] * (float)W1 + Uvs[Tri.C] * (float)W2;
					const FLinearColor Vc = Vcs[Tri.A] * (float)W0 + Vcs[Tri.B] * (float)W1 + Vcs[Tri.C] * (float)W2;
					if (!SlonkFauna::FloaterCover(Uv, Vc, Cu)) { continue; }
					Depth[I] = D;
					Light[I] = SlonkFauna::FloaterLight(Uv, Vc, Dim, Cu, Gd) / 0.2f;
					Tentacle[I] = Vc.R > 0.25f ? 1 : 0;
				}
			}
		}
		FJellySight Out;
		const double A = Cell * Cell;
		for (int32 I = 0; I < N * N; ++I)
		{
			if (Depth[I] == TNumericLimits<float>::Max()) { continue; }
			Out.Covered += A;
			Out.Glow += A * Light[I];
			Out.TentacleGlow += Tentacle[I] ? A * Light[I] : 0.0;
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaFloaterWorldsTest, "Slonk.Fauna.FloaterWorlds", SLONK_TEST_FLAGS)
bool FSlonkFaunaFloaterWorldsTest::RunTest(const FString& Parameters)
{
	// The sky-jellies drift only on a living world at least 0.3 strange whose air is thick (a haze in its lowlands): none at
	// strangeness 0 on any archetype (so none on the Earth-like home worlds), none just short of 0.3 however hazy, none on a
	// strange world of thin air, none on a dead or airless one; on the thick-aired strange ones a share of their cells hold
	// one, as their genome rolls it (most with few, some teeming), and the cells bear it out. The worlds "spawnplanet
	// terrestrial strange s seed n" makes from the game's start with jellies at every strangeness from 0.6 to 0.9, for the
	// captures.
	using namespace SlonkFauna;
	int32 Tame = 0, TameWith = 0, TamePlansOff = 0;
	for (const EPlanetArchetype Kind : FaunaKinds)
	{
		for (uint32 Hash = 1; Hash <= 64; ++Hash)
		{
			const FFaunaProfile F = Roll(SlonkGenome::Roll(Kind, 0.f, Hash * 7919u), Kind, Hash * 7919u);
			FFaunaWorld W;
			W.P = FaunaEarthParams(6371.0);
			W.Profile = F;
			W.Life = 1.f;
			++Tame;
			TameWith += F.FloaterShare > 0.f || F.bThickAir || W.Spawns(EFaunaKind::Floater) ? 1 : 0;
			TamePlansOff += F.PlanOf(EFaunaKind::Floater) == EarthPlan(EFaunaKind::Floater) ? 0 : 1;
		}
	}
	// Just short of the strangeness (hazy or not), and from it up: thin-aired and thick-aired.
	int32 Short = 0, ShortHazed = 0, ShortWith = 0, Thin = 0, ThinWith = 0, Thick = 0, ThickWithout = 0, OffShare = 0, Teeming = 0, Few = 0;
	TArray<float> Shares;
	for (const EPlanetArchetype Kind : { EPlanetArchetype::Terrestrial, EPlanetArchetype::Ocean, EPlanetArchetype::Desert })
	{
		for (uint32 Hash = 1; Hash <= 400; ++Hash)
		{
			const FWorldGenome Gs = SlonkGenome::Roll(Kind, 0.29f, Hash * 7919u);
			++Short;
			ShortHazed += Gs.LowHazeDensity > 0.f ? 1 : 0;
			ShortWith += Roll(Gs, Kind, Hash * 7919u).FloaterShare > 0.f ? 1 : 0;
			const float S = 0.3f + 0.7f * (float)((Hash * 37u) % 100u) / 99.f;
			const FWorldGenome G = SlonkGenome::Roll(Kind, S, Hash * 7919u);
			const FFaunaProfile F = Roll(G, Kind, Hash * 7919u);
			if (G.LowHazeDensity <= 0.f)
			{
				++Thin;
				ThinWith += F.FloaterShare > 0.f || F.bThickAir ? 1 : 0;
				continue;
			}
			++Thick;
			ThickWithout += F.FloaterShare > 0.f && F.bThickAir ? 0 : 1;
			OffShare += F.FloaterShare >= 0.6f * Floater::MinShare && F.FloaterShare <= Floater::MinShare + Floater::ShareSpan + 1e-6f ? 0 : 1;
			Shares.Add(F.FloaterShare);
			Teeming += F.FloaterShare > 0.3f ? 1 : 0;
			Few += F.FloaterShare < 0.1f ? 1 : 0;
		}
	}
	Shares.Sort();
	const float Median = Shares.Num() > 0 ? Shares[Shares.Num() / 2] : 0.f;
	AddInfo(FString::Printf(TEXT("%d worlds at strangeness 0, every archetype: %d with jellies or thick air, %d with a floater plan off the Earth's"), Tame, TameWith, TamePlansOff));
	AddInfo(FString::Printf(TEXT("%d living worlds at 0.29: %d hazy, %d with jellies; %d at 0.3 to 1: %d thin-aired (%d with jellies), %d thick-aired (%d without); their shares %.3f to %.3f, median %.3f (%d off the range), %d teeming (over 30%% of their cells), %d with few (under 10%%)"),
		Short, ShortHazed, ShortWith, Thin + Thick, Thin, ThinWith, Thick, ThickWithout, Shares.Num() ? Shares[0] : 0.f, Shares.Num() ? Shares.Last() : 0.f, Median, OffShare, Teeming, Few));
	TestEqual(TEXT("no jellies at strangeness 0 on any world"), TameWith, 0);
	TestEqual(TEXT("and the Earth's plan for them, which they would start from"), TamePlansOff, 0);
	TestTrue(TEXT("hazy worlds just short of 0.3 to try"), ShortHazed > 20);
	TestEqual(TEXT("none just short of 0.3, however hazy"), ShortWith, 0);
	TestTrue(TEXT("thin-aired and thick-aired strange worlds to try"), Thin > 100 && Thick > 100);
	TestEqual(TEXT("none on a strange world of thin air"), ThinWith, 0);
	TestEqual(TEXT("jellies on every thick-aired strange one"), ThickWithout, 0);
	TestEqual(TEXT("in a share of its cells within the range"), OffShare, 0);
	TestTrue(TEXT("most with few, some teeming"), Few > Thick / 4 && Teeming > Thick / 20 && Median < 0.25f);

	// The cells: of those under 700 directions over a thick-aired world's globe, about its share hold a jelly (within three
	// of the binomial's standard deviations); of a thin-aired world's, a dead one's or an airless one's, none; on the
	// Earth-like world at strangeness 0, none to be found.
	FFaunaWorld Rich;
	TestTrue(TEXT("a thick-aired world with jellies in a quarter to a half of its cells"), JellyWorld(0.8f, 0.25f, 0.5f, Rich));
	TArray<FFaunaGroup> Jellies;
	int32 Cells = 0;
	KindOverGlobe(Rich, EFaunaKind::Floater, 700, Jellies, &Cells);
	const double Measured = Cells > 0 ? (double)Jellies.Num() / Cells : 0.0;
	const double Sigma = FMath::Sqrt(Rich.Profile.FloaterShare * (1.0 - Rich.Profile.FloaterShare) / FMath::Max(Cells, 1));
	FFaunaWorld ThinAir = Rich, Dead = Rich, Airless = Rich;
	ThinAir.Profile.FloaterShare = 0.f;
	for (uint32 k = 1; k < 2000; ++k)
	{
		const FWorldGenome G = SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 0.8f, k * 7919u);
		if (!ThickAir(G)) { ThinAir = StrangeJellies(0.8f, k * 7919u); break; }
	}
	Dead.Life = 0.f;
	Airless.P.bAirless = true;
	int32 ThinCells = 0, DeadCells = 0, AirlessCells = 0;
	TArray<FFaunaGroup> ThinJellies, DeadJellies, AirlessJellies;
	KindOverGlobe(ThinAir, EFaunaKind::Floater, 300, ThinJellies, &ThinCells);
	KindOverGlobe(Dead, EFaunaKind::Floater, 300, DeadJellies, &DeadCells);
	KindOverGlobe(Airless, EFaunaKind::Floater, 300, AirlessJellies, &AirlessCells);
	FFaunaGroup Found;
	FGroupSearch Search;
	const bool bEarthHas = FindGroupNear(EarthFauna(), EFaunaKind::Floater, FVector(0.3, 0.8, 0.5).GetSafeNormal(), 4.0e7, 20000, Found, &Search);
	AddInfo(FString::Printf(TEXT("a thick-aired world rolling jellies into %.1f%% of its cells: %d of %d cells hold one (%.1f%%, sigma %.1f%%); a thin-aired one %d of %d, a dead one %d of %d, an airless one %d of %d; the Earth-like world at strangeness 0: %s (%s)"),
		100.0 * Rich.Profile.FloaterShare, Jellies.Num(), Cells, 100.0 * Measured, 100.0 * Sigma, ThinJellies.Num(), ThinCells, DeadJellies.Num(), DeadCells, AirlessJellies.Num(), AirlessCells,
		bEarthHas ? TEXT("one found") : TEXT("none"), *FloaterAbsence(EarthFauna())));
	TestTrue(TEXT("about its share of a thick-aired world's cells hold one"), Cells > 500 && FMath::Abs(Measured - Rich.Profile.FloaterShare) < 3.0 * Sigma + 0.01);
	TestTrue(TEXT("none in a thin-aired world's cells, a dead one's or an airless one's"), ThinCells > 200 && ThinJellies.Num() == 0 && DeadJellies.Num() == 0 && AirlessJellies.Num() == 0);
	TestFalse(TEXT("none on the Earth-like world at strangeness 0"), bEarthHas);
	TestTrue(TEXT("and the debug readout says why"), FloaterAbsence(EarthFauna()).Contains(TEXT("strangeness")) && FloaterAbsence(ThinAir).Contains(TEXT("thin"))
		&& FloaterAbsence(Dead).Contains(TEXT("lives")) && FloaterAbsence(Rich).IsEmpty());

	// The captures' worlds: of the seeds "spawnplanet terrestrial strange s seed n" rolls from the game's start, those with
	// jellies at every strangeness from 0.6 to 0.9; and the first's nearest jelly to the middle of its day side's start.
	TArray<FString> Picks;
	int32 FirstSeed = -1;
	for (int32 Seed = 0; Seed <= 60 && Picks.Num() < 6; ++Seed)
	{
		float Lo = 1.f, Hi = 0.f;
		bool bAll = true;
		for (const float S : { 0.6f, 0.7f, 0.8f, 0.9f })
		{
			const float Share = SpawnedJellies(Seed, S).Profile.FloaterShare;
			bAll &= Share > 0.f;
			Lo = FMath::Min(Lo, Share);
			Hi = FMath::Max(Hi, Share);
		}
		if (!bAll) { continue; }
		if (FirstSeed < 0) { FirstSeed = Seed; }
		Picks.Add(FString::Printf(TEXT("seed %d (%.0f%% to %.0f%% of its cells)"), Seed, 100.f * Lo, 100.f * Hi));
	}
	bool bSpawnedJelly = false;
	if (FirstSeed >= 0)
	{
		const FFaunaWorld Spawned = SpawnedJellies(FirstSeed, 0.8f);
		FFaunaGroup J;
		FGroupSearch Js;
		bSpawnedJelly = FindGroupNear(Spawned, EFaunaKind::Floater, FVector(0.3, 0.8, 0.5).GetSafeNormal(), 4.0e7, 20000, J, &Js);
		AddInfo(FString::Printf(TEXT("spawnplanet terrestrial strange 0.8 seed %d: %s; its nearest jelly to (0.3, 0.8, 0.5) %.1f km off (%d cells read), %.0f m across, %.0f m over the ground near its loop"),
			FirstSeed, *SlonkGenome::Describe(SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 0.8f, PlaceHash(SpawnedPlace(FirstSeed)))), bSpawnedJelly ? FVector::Dist(J.Centre, FVector(0.3, 0.8, 0.5).GetSafeNormal()) * Spawned.P.RadiusCm / 1e5 : -1.0,
			Js.Cells, J.SpanM, J.CruiseCm / 100.0));
	}
	AddInfo(FString::Printf(TEXT("spawnplanet terrestrial seeds with jellies at strangeness 0.6 to 0.9 (from the game's start): %s"), *FString::Join(Picks, TEXT(", "))));
	TestTrue(TEXT("seeds for the captures"), Picks.Num() >= 3 && bSpawnedJelly);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaFloaterLoopsTest, "Slonk.Fauna.FloaterLoops", SLONK_TEST_FLAGS)
bool FSlonkFaunaFloaterLoopsTest::RunTest(const FString& Parameters)
{
	// Every jelly drifts a few metres a second round a closed loop whose period divides the hour (where it was an hour ago,
	// no seam at the clock's wrap), its middle 300 m to 3 km over the highest ground near its loop, rising and sinking
	// gently (under a metre and a half a second), its pulse a whole number of times an hour 3 to 6 s apart, 20 to 80 m
	// across, its loop within 1.2 km of its middle (so one comes and goes unseen past its fade). Its lowest point (its
	// tentacles' tips, drawn at twice its size far off) stays Floater::ClearCm over the ground under it all round its loop:
	// the field at its finest, and the drawn ground of chunks 1.2 km and 9.8 km across. The jellies are pure functions of
	// the world and the place: a cell holds the same whatever order the cells are read in; one revisited is where one never
	// left would be; one a little past its reach stays, a quarter past it goes; none under a camera too high.
	using namespace SlonkFauna;
	FFaunaWorld W;
	if (!JellyWorld(0.8f, 0.3f, 0.6f, W)) { AddError(TEXT("no teeming jelly world found")); return false; }
	const double R = W.P.RadiusCm;
	TArray<FFaunaGroup> Jellies;
	KindOverGlobe(W, EFaunaKind::Floater, 800, Jellies);
	int32 NotDividing = 0, Unclosed = 0, Seams = 0, Speeds = 0, Sizes = 0, Pulses = 0, Cruises = 0, Extents = 0, Climbs = 0;
	double MinSpeed = 1e9, MaxSpeed = 0.0, MaxClimb = 0.0, MinUp = 1e9, MaxUp = 0.0, MaxExtent = 0.0;
	for (const FFaunaGroup& G : Jellies)
	{
		const double Cycles = ClockPeriodSec / G.PeriodSec;
		NotDividing += FMath::Abs(Cycles - FMath::RoundToDouble(Cycles)) < 1e-9 ? 0 : 1;
		for (const double T : { 0.0, 777.7, 2345.6 })
		{
			Unclosed += FVector::Dist(PathAt(G, R, T).PosCm, PathAt(G, R, T + 3600.0).PosCm) < 1.0 && FVector::Dist(PathAt(G, R, T).PosCm, PathAt(G, R, T + G.PeriodSec).PosCm) < 1.0 ? 0 : 1;
		}
		Seams += FVector::Dist(PathAt(G, R, 3599.999).PosCm, PathAt(G, R, 3600.0).PosCm) < G.SpeedCmS * 0.002 + 1.0 ? 0 : 1;
		MinSpeed = FMath::Min(MinSpeed, G.SpeedCmS / 100.0);
		MaxSpeed = FMath::Max(MaxSpeed, G.SpeedCmS / 100.0);
		Speeds += G.SpeedCmS >= 100.0 && G.SpeedCmS <= 500.0 ? 0 : 1;
		Sizes += G.SpanM >= Floater::MinSpanM && G.SpanM <= Floater::MaxSpanM && G.Members == 1 ? 0 : 1;
		const double Pulse = 1.0 / FMath::Max((double)G.FlapHz, 1e-6), PerHour = (double)G.FlapHz * ClockPeriodSec;
		Pulses += FMath::Abs(PerHour - FMath::RoundToDouble(PerHour)) < 2e-3 && Pulse >= Floater::PulseLoSec - 0.01 && Pulse <= Floater::PulseHiSec + 0.01 ? 0 : 1;
		const double Least = 2.0 * G.HangCm + Floater::SinkAllowCm + Floater::ClearCm;
		Cruises += G.CruiseCm >= FMath::Max(Floater::MinCruiseCm, Least) - 1e-6 && G.CruiseCm <= FMath::Max(Floater::MaxCruiseCm, Least) + 1e-6 ? 0 : 1;
		MaxExtent = FMath::Max(MaxExtent, G.ExtentCm());
		Extents += KindDesc(EFaunaKind::Floater).ReachCm - G.ExtentCm() > KindDesc(EFaunaKind::Floater).FadeCm ? 0 : 1;
		// Its climb and sink over its loop, m/s, from its frame a second apart; its middle's height over the field under it.
		for (double T = 0.0; T < G.PeriodSec; T += G.PeriodSec / 120.0)
		{
			const FVector A = PathAt(G, R, T).PosCm, B = PathAt(G, R, T + 1.0).PosCm;
			const double Climb = FMath::Abs(B.Size() - A.Size()) / 100.0;
			MaxClimb = FMath::Max(MaxClimb, Climb);
			Climbs += Climb < 1.5 ? 0 : 1;
			const double Up = (A.Size() - R - FMath::Max(SlonkTerrain::SurfaceHeightCm(W.P, A.GetSafeNormal()), 0.0)) / 100.0;
			MinUp = FMath::Min(MinUp, Up);
			MaxUp = FMath::Max(MaxUp, Up);
		}
	}
	AddInfo(FString::Printf(TEXT("%d jellies over a world rolling them into %.0f%% of its cells: %d periods not dividing the hour, %d loops not closed, %d seams; drifting %.1f to %.1f m/s (%d off), %d off their sizes, %d pulses off, %d cruises off, loops within %.0f m of their middles (%d too wide to come unseen); climbing at most %.2f m/s (%d over 1.5); their middles %.0f to %.0f m over the ground under them"),
		Jellies.Num(), 100.0 * W.Profile.FloaterShare, NotDividing, Unclosed, Seams, MinSpeed, MaxSpeed, Speeds, Sizes, Pulses, Cruises, MaxExtent / 100.0, Extents, MaxClimb, Climbs, MinUp, MaxUp));
	TestTrue(TEXT("jellies to test"), Jellies.Num() >= 150);
	TestEqual(TEXT("every period divides the hour"), NotDividing, 0);
	TestEqual(TEXT("every loop closes, an hour and a period on"), Unclosed, 0);
	TestEqual(TEXT("no seam where the clock wraps"), Seams, 0);
	TestEqual(TEXT("drifting a few metres a second"), Speeds, 0);
	TestEqual(TEXT("20 to 80 m across, alone"), Sizes, 0);
	TestEqual(TEXT("pulsing a whole number of times an hour, 3 to 6 s apart"), Pulses, 0);
	TestEqual(TEXT("300 m to 3 km over the highest ground near, and never too low to clear it"), Cruises, 0);
	TestEqual(TEXT("each loop small enough to come and go past its fade"), Extents, 0);
	TestEqual(TEXT("rising and sinking gently"), Climbs, 0);
	TestTrue(TEXT("its middle about 300 m or more over the ground under it"), MinUp >= 280.0 && MaxUp > 1500.0);

	// Its lowest point over the ground all round its loop: the field at its finest under the first 40, and the drawn ground
	// (chunks 1.2 km and 9.8 km across) under the first 6.
	auto Field = [&W](const FVector& Dir) { return FMath::Max(SlonkTerrain::SurfaceHeightCm(W.P, Dir), 0.0); };
	FDrawnGround Fine, Coarse;
	Fine.P = W.P;
	Fine.Depth = 13;
	Coarse.P = W.P;
	Coarse.Depth = 10;
	auto FineAt = [&Fine](const FVector& Dir) { return FMath::Max(Fine.At(Dir), 0.0); };
	auto CoarseAt = [&Coarse](const FVector& Dir) { return FMath::Max(Coarse.At(Dir), 0.0); };
	double WorstField = 1e30, WorstFine = 1e30, WorstCoarse = 1e30;
	int32 Low = 0, Read = 0;
	for (int32 j = 0; j < FMath::Min(Jellies.Num(), 40); ++j)
	{
		const FFaunaGroup& G = Jellies[j];
		for (int32 k = 0; k < 240; ++k)
		{
			const FVector Pos = PathAt(G, R, G.PeriodSec * k / 240.0).PosCm;
			const double C = JellyClearanceCm(G, R, Pos, Field);
			WorstField = FMath::Min(WorstField, C);
			Low += C >= Floater::ClearCm ? 0 : 1;
			++Read;
			if (j < 6 && k % 4 == 0)
			{
				const double Cf = JellyClearanceCm(G, R, Pos, FineAt), Cc = JellyClearanceCm(G, R, Pos, CoarseAt);
				WorstFine = FMath::Min(WorstFine, Cf);
				WorstCoarse = FMath::Min(WorstCoarse, Cc);
				Low += Cf >= Floater::ClearCm && Cc >= Floater::ClearCm ? 0 : 1;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("round their loops (%d places), their lowest points drawn at twice their size clear the ground under them by %.0f m at the least over the field at its finest, %.0f m over chunks 1.2 km across and %.0f m over chunks 9.8 km across (%d under %.0f m); %d and %d chunks built"),
		Read, WorstField / 100.0, WorstFine / 100.0, WorstCoarse / 100.0, Low, Floater::ClearCm / 100.0, Fine.Chunks.Num(), Coarse.Chunks.Num()));
	TestEqual(TEXT("always clear of the ground under them by the margin"), Low, 0);

	// Pure functions of the world and the place.
	FFaunaGroup G0;
	if (!TestJelly(W, G0)) { AddError(TEXT("no jelly found")); return false; }
	TArray<FFaunaCell> Near;
	CellsNear(W.P, EFaunaKind::Floater, G0.Centre, 1.5e6, Near);
	TArray<TOptional<FFaunaGroup>> Forward, Backward;
	Forward.SetNum(Near.Num());
	Backward.SetNum(Near.Num());
	for (int32 i = 0; i < Near.Num(); ++i) { FFaunaGroup G; if (GroupInCell(W, Near[i], G)) { Forward[i] = G; } }
	for (int32 i = Near.Num() - 1; i >= 0; --i) { FFaunaGroup G; if (GroupInCell(W, Near[i], G)) { Backward[i] = G; } }
	int32 Held = 0, Mismatch = 0;
	for (int32 i = 0; i < Near.Num(); ++i)
	{
		Held += Forward[i].IsSet() ? 1 : 0;
		Mismatch += Forward[i].IsSet() == Backward[i].IsSet() && (!Forward[i].IsSet() || Forward[i].GetValue() == Backward[i].GetValue()) ? 0 : 1;
	}
	TArray<FTransform> Xa, Xb;
	TArray<float> Ca, Cb;
	MembersOf(G0, Xa, Ca);
	MembersOf(G0, Xb, Cb);
	const bool bMembersSame = Xa.Num() == 1 && Xb.Num() == 1 && Ca == Cb && Xa[0].Equals(Xb[0], 0.0) && Ca.Num() == Custom::Num && Ca[Custom::SizeM] == G0.SpanM
		&& Ca[Custom::PerKind] == (float)G0.Ribs && Ca[Custom::RateHz] == G0.FlapHz;
	const FVector CamA = CameraOver(W, G0.Centre);
	const FVector CamB = CameraOver(W, AlongGround(G0.Centre, 6.0e6, R, 2.0));
	FFaunaField F, Stayed;
	F.SetWorld(W);
	Stayed.SetWorld(W);
	TArray<uint64> A1, B1, A2, Kept;
	ScanAndSelect(F, CamA, A1);
	ScanAndSelect(Stayed, CamA, Kept);
	TMap<uint64, FFaunaGroup> First;
	for (const TPair<uint64, FFaunaField::FGroupRef>& A : F.GetActive()) { First.Add(A.Key, *A.Value); }
	ScanAndSelect(F, CamB, B1);
	int32 Overlap = 0;
	for (const uint64 Id : B1) { Overlap += A1.Contains(Id) ? 1 : 0; }
	ScanAndSelect(F, CamA, A2);
	ScanAndSelect(Stayed, CamA, Kept);
	int32 Changed = 0, Moved = 0;
	for (const TPair<uint64, FFaunaField::FGroupRef>& A : F.GetActive())
	{
		const FFaunaGroup* Was = First.Find(A.Key);
		const FFaunaField::FGroupRef* Never = Stayed.GetActive().Find(A.Key);
		Changed += Was && *Was == *A.Value && Never && **Never == *A.Value ? 0 : 1;
		if (Never) { Moved += PathAt(*A.Value, R, 1234.5).PosCm.Equals(PathAt(**Never, R, 1234.5).PosCm, 0.0) ? 0 : 1; }
	}
	// The hysteresis, on a world of few jellies (the caps leaving room).
	FFaunaWorld Sparse;
	const bool bSparse = JellyWorld(0.8f, 0.0f, 0.06f, Sparse);
	FFaunaGroup G1;
	const bool bG1 = bSparse && TestJelly(Sparse, G1);
	const double Reach = KindDesc(EFaunaKind::Floater).ReachCm;
	TArray<uint64> AtG1, NearIds, FarIds, Fresh, High;
	int32 NearGroups = 0;
	if (bG1)
	{
		FFaunaField SparseField;
		SparseField.SetWorld(Sparse);
		ScanAndSelect(SparseField, CameraOver(Sparse, G1.Centre), AtG1);
		ScanAndSelect(SparseField, CameraOver(Sparse, AlongGround(G1.Centre, 1.1 * Reach, R, 0.7)), NearIds);
		NearGroups = SparseField.ActiveGroups(EFaunaKind::Floater);
		ScanAndSelect(SparseField, CameraOver(Sparse, AlongGround(G1.Centre, 1.3 * Reach, R, 0.7)), FarIds);
		FFaunaField Come;
		Come.SetWorld(Sparse);
		ScanAndSelect(Come, CameraOver(Sparse, AlongGround(G1.Centre, 1.1 * Reach, R, 0.7)), Fresh);
	}
	ScanAndSelect(F, G0.Centre * (R + KindDesc(EFaunaKind::Floater).MaxAltitudeCm * 1.4 + FMath::Max(SlonkTerrain::SurfaceHeightCm(W.P, G0.Centre), 0.0)), High);
	AddInfo(FString::Printf(TEXT("%d cells round a jelly of %.0f m: %d hold one, %d read otherwise backward; its member the same made twice %s; about A %d jellies, about B 60 km off %d (%d of A's), back at A %d, %d of them changed, %d off where they would be had the camera never left; on a sparse world (%.1f%% of its cells) %d about a camera 1.1 reaches from one; %d about a camera too high"),
		Near.Num(), G0.SpanM, Held, Mismatch, bMembersSame ? TEXT("yes") : TEXT("no"), A1.Num(), B1.Num(), Overlap, A2.Num(), Changed, Moved, bSparse ? 100.0 * Sparse.Profile.FloaterShare : 0.0, NearGroups, High.Num()));
	TestEqual(TEXT("a cell holds the same jelly whatever order the cells are read in"), Mismatch, 0);
	TestTrue(TEXT("a jelly's member the same every time"), bMembersSame);
	TestTrue(TEXT("jellies about A, the test's among them"), A1.Num() > 0 && A1.Contains(G0.Id));
	TestEqual(TEXT("none of A's about B"), Overlap, 0);
	TestTrue(TEXT("back at A, the same jellies, as about a camera that never left"), A2 == A1 && Kept == A1);
	TestEqual(TEXT("each the same jelly as before and as one never left"), Changed, 0);
	TestEqual(TEXT("where one never left would be"), Moved, 0);
	TestTrue(TEXT("on the sparse world, its jelly about its middle, and room under the caps past it"), bG1 && AtG1.Contains(G1.Id) && NearGroups < KindDesc(EFaunaKind::Floater).MaxGroups);
	TestTrue(TEXT("a jelly just past its reach stays"), NearIds.Contains(G1.Id));
	TestFalse(TEXT("a jelly a quarter past its reach goes"), FarIds.Contains(G1.Id));
	TestFalse(TEXT("a jelly just past its reach does not come"), Fresh.Contains(G1.Id));
	TestEqual(TEXT("none under a camera too high"), High.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaFloaterStartleTest, "Slonk.Fauna.FloaterStartle", SLONK_TEST_FLAGS)
bool FSlonkFaunaFloaterStartleTest::RunTest(const FString& Parameters)
{
	// A craft passing within 150 m of a jelly's middle startles it, at any height it drifts at; one passing 400 m off does
	// not, nor one set down by it, nor someone on foot under it. Startled, it squeezes itself hard and away from the craft
	// and down, at a jelly's pace (never a jump: its frame's velocity is continuous at the startle and at every startle after
	// it), holds a while and is back exactly on its loop by its fright's end; the material squeezes its bell in the
	// seconds after, and a second fright moves no vertex more than a frame's motion does. Chased by a craft circling it for
	// as long as it can be startled again, it sinks no further than its loop allows for (Floater::SinkAllowCm), drifts off
	// its loop no further than the ground was read for, and its lowest point stays clear of the ground.
	using namespace SlonkFauna;
	FFaunaWorld W;
	if (!JellyWorld(0.8f, 0.2f, 0.6f, W)) { AddError(TEXT("no jelly world found")); return false; }
	FFaunaGroup G;
	if (!TestJelly(W, G)) { AddError(TEXT("no jelly found")); return false; }
	const double R = W.P.RadiusCm;
	const FFaunaKindDesc& D = KindDesc(EFaunaKind::Floater);
	auto Ground = [&W](const FVector& Dir) { return FMath::Max(SlonkTerrain::SurfaceHeightCm(W.P, Dir), 0.0); };
	auto Pass = [&](double MissCm, double Start, FPanic& Panic, double AltitudeOverride)
	{
		// Straight past the jelly's place at 60 m/s from 600 m before to 600 m after, MissCm to its side at its height.
		const FFaunaPose At = PathAt(G, R, Start + 10.0);
		const FVector Across = At.Rot.GetAxisX(), Side = At.Rot.GetAxisY();
		const FVector Mid = At.PosCm + Side * MissCm;
		int32 Startles = 0;
		for (double T = Start; T <= Start + 20.0; T += 0.1)
		{
			FFaunaThreat Craft;
			Craft.PosCm = Mid + Across * (6000.0 * (T - Start - 10.0));
			Craft.VelCmS = Across * 6000.0;
			Craft.AltitudeCm = AltitudeOverride >= 0.0 ? AltitudeOverride : Craft.PosCm.Size() - R - Ground(Craft.PosCm.GetSafeNormal());
			Startles += StartleGroup(Panic, D, FlightAt(G, D, Panic, R, T, FFaunaPose(), 0.0).PosCm, Craft, T) ? 1 : 0;
		}
		return Startles;
	};
	FPanic Close, Wide, High;
	const double T0 = 1000.0;
	const int32 CloseStartles = Pass(8000.0, T0, Close, -1.0);
	const int32 WideStartles = Pass(4.0e4, T0, Wide, -1.0);
	const int32 HighStartles = Pass(8000.0, T0, High, 6.0e5);
	// Someone on foot right under it, a craft set down by it.
	const FVector Here = PathAt(G, R, 2000.0).PosCm;
	FFaunaThreat Walker;
	Walker.bCraft = false;
	Walker.PosCm = Here - Here.GetSafeNormal() * 2.0e4;
	FFaunaThreat Parked;
	Parked.PosCm = Here + FVector::CrossProduct(Here.GetSafeNormal(), FVector::ZAxisVector).GetSafeNormal() * 5000.0;
	Parked.AltitudeCm = 200.0;
	Parked.bLanded = true;
	FPanic ByWalker, ByParked;
	const bool bWalker = StartleGroup(ByWalker, D, Here, Walker, 2000.0);
	const bool bParked = StartleGroup(ByParked, D, Here, Parked, 2000.0);
	// Its flight: away and down, at its pace, back exactly by its fright's end.
	const FVector Off20 = PanicOffset(Close, D, Close.T0 + 20.0);
	const FVector Up0 = PathAt(G, R, Close.T0).PosCm.GetSafeNormal();
	const double Sink20 = -FVector::DotProduct(Off20, Up0) / 100.0, Away20 = (Off20 - Up0 * FVector::DotProduct(Off20, Up0)).Size() / 100.0;
	double FleeSpeed = 0.0, MaxJump = 0.0;
	for (double T = Close.T0 - 0.5; T < Close.T0 + D.FrightSec; T += 0.05)
	{
		FleeSpeed = FMath::Max(FleeSpeed, FVector::Dist(PanicOffset(Close, D, T + 0.05), PanicOffset(Close, D, T)) / 0.05 / 100.0);
		MaxJump = FMath::Max(MaxJump, (FloaterVelocity(G, D, Close, R, T + 0.05) - FloaterVelocity(G, D, Close, R, T)).Size() / 100.0);
	}
	const double Back = PanicOffset(Close, D, Close.T0 + D.FrightSec + 0.01).Size();
	const bool bOnLoop = FlightAt(G, D, Close, R, Close.T0 + D.FrightSec + 0.5, FFaunaPose(), 0.0).PosCm.Equals(PathAt(G, R, Close.T0 + D.FrightSec + 0.5).PosCm, 0.0);
	AddInfo(FString::Printf(TEXT("a jelly of %.0f m %.0f m over the ground near its loop: a pass 80 m off startles it %d times, 400 m off %d, 80 m off but 6 km over the ground %d; someone on foot under it %d, a craft set down by it %d; 20 s on it is %.0f m away and %.0f m down, at most %.1f m/s off its loop, its velocity changing by at most %.2f m/s in 0.05 s; %.2f m off its loop at its fright's end"),
		G.SpanM, G.CruiseCm / 100.0, CloseStartles, WideStartles, HighStartles, bWalker ? 1 : 0, bParked ? 1 : 0, Away20, Sink20, FleeSpeed, MaxJump, Back / 100.0));
	TestTrue(TEXT("a craft passing 80 m off startles it"), CloseStartles >= 1);
	TestEqual(TEXT("one passing 400 m off does not"), WideStartles, 0);
	TestEqual(TEXT("nor one higher than any jelly drifts"), HighStartles, 0);
	TestFalse(TEXT("nor someone on foot under it"), bWalker);
	TestFalse(TEXT("nor a craft set down by it"), bParked);
	TestTrue(TEXT("it squeezes itself away and down"), Away20 > 40.0 && Sink20 > 20.0 && Sink20 < 0.7 * Away20);
	TestTrue(TEXT("at a jelly's pace"), FleeSpeed > 3.0 && FleeSpeed < 10.0);
	TestTrue(TEXT("its velocity never jumps"), MaxJump < 0.3);
	TestEqual(TEXT("back on its loop by its fright's end"), Back, 0.0);
	TestTrue(TEXT("exactly on it"), bOnLoop);

	// Startled again 10, 25 and 60 s on from the other side: the velocity runs on through each; the material's squeeze is the
	// first's while it is in its forty seconds, and a frame across each startle moves no vertex more than the frames before.
	FFaunaBodyPlan Plan = W.Profile.PlanOf(EFaunaKind::Floater);
	UE::Geometry::FDynamicMesh3 Lod0;
	SlonkFaunaMeshes::BuildLod(Plan, 0, Lod0);
	const TArray<FLinearColor> Vcs = VertexColours(Lod0);
	const TArray<FVector2f> Uvs = VertexUVs(Lod0);
	TArray<FTransform> Xf;
	TArray<float> Cu;
	MembersOf(G, Xf, Cu);
	FPanic Chased = Close;
	double WorstVelJump = 0.0, WorstStepRatio = 0.0, WorstTrailJump = 0.0, WorstRateJump = 0.0, WorstRunBack = 0.0, WorstTipGap = 0.0;
	int32 Again = 0;
	for (const double After : { 10.0, 25.0, 60.0 })
	{
		const double T = Close.T0 + After;
		const FFaunaPose Pose = FlightAt(G, D, Chased, R, T, FFaunaPose(), 0.0);
		FFaunaThreat Craft;
		Craft.PosCm = Pose.PosCm + Pose.Rot.GetAxisY() * ((Again % 2 == 0 ? -1.0 : 1.0) * 6000.0);
		Craft.VelCmS = Pose.Rot.GetAxisX() * 5000.0;
		Craft.AltitudeCm = 1.0e5;
		const FPanic Before = Chased;
		const FVector VelBefore = FloaterVelocity(G, D, Before, R, T);
		if (!StartleGroup(Chased, D, Pose.PosCm, Craft, T)) { continue; }
		++Again;
		WorstVelJump = FMath::Max(WorstVelJump, (FloaterVelocity(G, D, Chased, R, T) - VelBefore).Size() / 100.0);
		// The material a frame either side of it, against the frames before.
		auto DataAt = [&](const FPanic& P, double At, float* Gd)
		{
			FMemory::Memzero(Gd, sizeof(float) * FloaterData::Num);
			FloaterGroupData(G, D, P, R, At, FlightAt(G, D, P, R, At, FFaunaPose(), 0.0).Rot, 0.0, Gd);
		};
		float Ga[FloaterData::Num], Gb[FloaterData::Num], Gc[FloaterData::Num];
		const double Dt = 1.0 / 60.0;
		DataAt(Before, T - 2.0 * Dt, Ga);
		DataAt(Before, T - Dt, Gb);
		DataAt(Chased, T, Gc);
		double StepBefore = 0.0, StepAcross = 0.0;
		for (const int32 V : Lod0.VertexIndicesItr())
		{
			const FVector3f LP = (FVector3f)Lod0.GetVertex(V);
			const FVector3f Pa = LP + FloaterMotion(LP, Vcs[V], Uvs[V], MaterialTime(T - 2.0 * Dt), Cu.GetData(), Ga, 2.0e4f, 960.f);
			const FVector3f Pb = LP + FloaterMotion(LP, Vcs[V], Uvs[V], MaterialTime(T - Dt), Cu.GetData(), Gb, 2.0e4f, 960.f);
			const FVector3f Pc = LP + FloaterMotion(LP, Vcs[V], Uvs[V], MaterialTime(T), Cu.GetData(), Gc, 2.0e4f, 960.f);
			StepBefore = FMath::Max(StepBefore, (double)(Pb - Pa).Size());
			StepAcross = FMath::Max(StepAcross, (double)(Pc - Pb).Size());
		}
		WorstStepRatio = FMath::Max(WorstStepRatio, StepAcross / FMath::Max(StepBefore * 1.5 + 0.5, 1e-3));
		// What the material is sent a frame at a time through it (the frames before it of the flight before): the way it
		// drifts through the air runs on, and so does how fast that changes; and the velocity pass's frame before, run back
		// from each frame's data, trails its tentacles as that frame drew them.
		FVector2D PrevV(0.0), PrevA(0.0);
		float Prev[FloaterData::Num] = {};
		for (int32 k = -3; k <= 9; ++k)
		{
			const double At = T + k * Dt;
			float Gk[FloaterData::Num];
			DataAt(k < 0 ? Before : Chased, At, Gk);
			const FVector2D Vk(Gk[FloaterData::TrailX], Gk[FloaterData::TrailY]), Ak(Gk[FloaterData::TrailRateX], Gk[FloaterData::TrailRateY]);
			if (k > -3)
			{
				WorstTrailJump = FMath::Max(WorstTrailJump, (Vk - PrevV).Size());
				WorstRateJump = FMath::Max(WorstRateJump, (Ak - PrevA).Size());
				WorstRunBack = FMath::Max(WorstRunBack, (Vk - Ak * Dt - PrevV).Size());
				const float Was = MaterialTime(At - Dt);
				for (const int32 V : Lod0.VertexIndicesItr())
				{
					if (Vcs[V].R < 0.25f || Vcs[V].G < 0.99f) { continue; }
					const FVector3f LP = (FVector3f)Lod0.GetVertex(V);
					const FVector3f Rebuilt = FloaterMotion(LP, Vcs[V], Uvs[V], Was, Cu.GetData(), Gk, 2.0e4f, 960.f);
					const FVector3f Drawn = FloaterMotion(LP, Vcs[V], Uvs[V], Was, Cu.GetData(), Prev, 2.0e4f, 960.f);
					WorstTipGap = FMath::Max(WorstTipGap, (double)(Rebuilt - Drawn).Size());
				}
			}
			PrevV = Vk;
			PrevA = Ak;
			FMemory::Memcpy(Prev, Gk, sizeof(Prev));
		}
	}
	// The squeeze: its rim pulled in hard a couple of seconds after the first startle, its calm pulse again by ten.
	auto RimPull = [&](const FPanic& P, double At)
	{
		float Gd[FloaterData::Num] = {};
		FloaterGroupData(G, D, P, R, At, FlightAt(G, D, P, R, At, FFaunaPose(), 0.0).Rot, 0.0, Gd);
		double Most = 0.0;
		for (const int32 V : Lod0.VertexIndicesItr())
		{
			if (Vcs[V].R > 0.25f || Vcs[V].G < 0.99f) { continue; }
			const FVector3f LP = (FVector3f)Lod0.GetVertex(V);
			const FVector3f Off = FloaterMotion(LP, Vcs[V], Uvs[V], MaterialTime(At), Cu.GetData(), Gd, 2.0e4f, 960.f);
			Most = FMath::Max(Most, (double)-(FVector2f(Off.X, Off.Y) | FVector2f(LP.X, LP.Y).GetSafeNormal()) / FMath::Max((double)FVector2f(LP.X, LP.Y).Size(), 1.0));
		}
		return Most;
	};
	double Squeezed = 0.0, CalmMost = 0.0;
	for (double A = 1.0; A <= 3.0; A += 0.25) { Squeezed = FMath::Max(Squeezed, RimPull(Close, Close.T0 + A)); }
	for (double A = 0.0; A <= 6.0; A += 0.25) { CalmMost = FMath::Max(CalmMost, RimPull(FPanic(), Close.T0 + A)); }
	AddInfo(FString::Printf(TEXT("startled again %d times: its velocity jumping by %.3f m/s at the most; a frame across each moving a vertex at most %.2f times the frames before's (and half a unit); its rim pulled in by %.2f of its radius a second or two after the first, %.2f at the most calm"),
		Again, WorstVelJump, WorstStepRatio, Squeezed, CalmMost));
	AddInfo(FString::Printf(TEXT("sent a frame at a time through each: the way it drifts through the air jumping by %.3f m/s at the most, how fast that changes by %.2f m/s/s; the velocity pass's frame before, run back from each frame's data, %.3f m/s off the trail drawn then (its tentacles' tips %.2f units off)"),
		WorstTrailJump, WorstRateJump, WorstRunBack, WorstTipGap));
	TestTrue(TEXT("startled again"), Again >= 2);
	TestTrue(TEXT("its velocity runs on through a second fright"), WorstVelJump < 0.3);
	TestTrue(TEXT("and no vertex snaps"), WorstStepRatio <= 1.0);
	TestTrue(TEXT("the trail sent runs on through a second fright, and how fast it changes"), WorstTrailJump < 0.3 && WorstRateJump < 10.0);
	TestTrue(TEXT("so the velocity pass sees the tentacles trail as they were drawn"), WorstRunBack < 0.2);
	TestTrue(TEXT("squeezed hard after a startle, gently pulsing calm"), Squeezed > 0.28 && CalmMost > 0.1 && CalmMost < 0.2);

	// Chased by a craft circling it 80 m off for seven minutes, startling it whenever it may.
	double MostSink = 0.0, MostAway = 0.0, LeastClear = 1e30;
	int32 Chases = 0;
	for (int32 Run = 0; Run < 3; ++Run)
	{
		FFaunaGroup J;
		if (!FindGroupNear(W, EFaunaKind::Floater, AlongGround(FVector(0.3, 0.8, 0.5).GetSafeNormal(), 3.0e6 * Run, R, 1.0), 4.0e7, 20000, J)) { continue; }
		FPanic P;
		for (double T = 500.0; T < 920.0; T += 0.25)
		{
			const FFaunaPose Pose = FlightAt(J, D, P, R, T, FFaunaPose(), 0.0);
			const FVector Up = Pose.PosCm.GetSafeNormal();
			const double A = 0.15 * T * (Run == 1 ? -1.0 : 1.0);
			FFaunaThreat Craft;
			Craft.PosCm = Pose.PosCm + (Pose.Rot.GetAxisX() * FMath::Cos(A) + Pose.Rot.GetAxisY() * FMath::Sin(A)) * 8000.0;
			Craft.VelCmS = (Pose.Rot.GetAxisY() * FMath::Cos(A) - Pose.Rot.GetAxisX() * FMath::Sin(A)) * 1200.0;
			Craft.AltitudeCm = 1.0e5;
			Chases += StartleGroup(P, D, Pose.PosCm, Craft, T) ? 1 : 0;
			const FVector Off = PanicOffset(P, D, T);
			MostSink = FMath::Max(MostSink, -FVector::DotProduct(Off, Up));
			MostAway = FMath::Max(MostAway, (Off - Up * FVector::DotProduct(Off, Up)).Size());
			LeastClear = FMath::Min(LeastClear, JellyClearanceCm(J, R, FlightAt(J, D, P, R, T, FFaunaPose(), 0.0).PosCm, Ground));
		}
	}
	const double ReachLeft = Floater::GroundReachCm - (50.0 * Floater::MaxSpanM + 2.0 * 50.0 * (Floater::MaxBell + Floater::MaxBell * Floater::MaxTentacles) * Floater::MaxSpanM * 0.5);
	AddInfo(FString::Printf(TEXT("chased round by a craft 80 m off for seven minutes (%d startles): sunk %.0f m at the most (%.0f allowed), %.0f m off its loop across at the most (the ground read %.0f m round its loop, %.0f of it past the largest jelly's rim and trailing tentacles); its lowest point %.0f m clear of the ground at the least"),
		Chases, MostSink / 100.0, Floater::SinkAllowCm / 100.0, MostAway / 100.0, Floater::GroundReachCm / 100.0, ReachLeft / 100.0, LeastClear / 100.0));
	TestTrue(TEXT("chased and startled again and again"), Chases >= 10);
	TestTrue(TEXT("sinking no further than its loop allows for"), MostSink <= Floater::SinkAllowCm);
	TestTrue(TEXT("drifting off its loop no further than its ground was read"), MostAway <= ReachLeft);
	TestTrue(TEXT("its lowest point clear of the ground"), LeastClear >= Floater::ClearCm);

	// The debug command's startle (fauna startle), asked again and again of a jelly by a ship hovering 300 m off to its
	// side (each a threat on the ship's side of it, 75 m off, as UPlanetFaunaComponent::StartleAll makes it): every 30 s for
	// three minutes and every 2 s for one, it drifts off its loop no further than its ground was read, and sinks no further
	// than its loop allows for, as a craft chasing it would leave it.
	double CmdAway = 0.0, CmdSink = 0.0;
	int32 CmdAsked = 0, CmdStartles = 0;
	for (const int32 EverySteps : { 120, 8 })
	{
		FPanic P;
		const double Start = 3000.0;
		const FFaunaPose At0 = PathAt(G, R, Start);
		const FVector Ship = At0.PosCm + At0.Rot.GetAxisY() * 3.0e4;
		const int32 AskSteps = EverySteps > 100 ? 720 : 240;
		for (int32 s = 0; s < AskSteps + 4 * (int32)D.FrightSec; ++s)
		{
			const double T = Start + 0.25 * s;
			const FVector Path = PathAt(G, R, T).PosCm;
			const FVector Pos = Path + PanicOffset(P, D, T);
			const FVector Up = Path.GetSafeNormal();
			if (s < AskSteps && s % EverySteps == 0)
			{
				FVector Toward = Ship - Pos;
				Toward -= Up * FVector::DotProduct(Toward, Up);
				FFaunaThreat Asked;
				Asked.PosCm = Pos + Toward.GetClampedToMaxSize(0.5 * D.ScareRadiusCm);
				Asked.AltitudeCm = 0.0;
				++CmdAsked;
				CmdStartles += StartleByCommand(P, D, Pos, Asked, T) ? 1 : 0;
			}
			const FVector Off = PanicOffset(P, D, T);
			CmdSink = FMath::Max(CmdSink, -FVector::DotProduct(Off, Up));
			CmdAway = FMath::Max(CmdAway, (Off - Up * FVector::DotProduct(Off, Up)).Size());
		}
	}
	AddInfo(FString::Printf(TEXT("fauna startle asked %d times (every 30 s and every 2 s) by a ship hovering 300 m off: startled %d times, %.0f m off its loop across at the most (%.0f allowed), sunk %.0f m (%.0f allowed)"),
		CmdAsked, CmdStartles, CmdAway / 100.0, ReachLeft / 100.0, CmdSink / 100.0, Floater::SinkAllowCm / 100.0));
	TestTrue(TEXT("the command startles it"), CmdStartles >= 4);
	TestTrue(TEXT("asked again and again, it drifts off its loop no further than its ground was read, nor sinks too far"), CmdAway <= ReachLeft && CmdSink <= Floater::SinkAllowCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaJellyTest, "Slonk.Fauna.Jelly", SLONK_TEST_FLAGS)
bool FSlonkFaunaJellyTest::RunTest(const FString& Parameters)
{
	// A sky-jelly of every plan a world may roll: about 300, 60 and 16 triangles, its span across, its middle at the origin,
	// its crown up; its vertex colours and UVs as the material reads them; every face wound to show the side its normals
	// point to; a tangent basis at every vertex. The material's motion keeps every vertex of each LOD inside the instance's
	// padded box (UPlanetInstancesComponent::InstanceBox with the kind's padding), whatever its size, pulse, fright, drift
	// through the air and distance (the speck floor at twice its size among them); its tentacles keep their length as they
	// trail; its motion runs on across the hour's wrap. Its LODs come in where a jelly is about 40 and 6 pixels across.
	// Its membrane is pierced by pores the same at every moment, leaving about half of it; its crown, ribs, ring canal and
	// tentacles whole; its light at night pulses down its ribs. Pale, its hue turned with a strange world's sky.
	using namespace UE::Geometry;
	namespace Vc = SlonkFauna::Vc;
	namespace Fl = SlonkFauna::Floater;
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Floater);
	const TArray<FFaunaBodyPlan> Plans = JellyPlans();
	int32 BadTris = 0, BadColour = 0, BadWinding = 0, OffBox = 0, Escaped = 0, Tried = 0, Stretched = 0, Seams = 0;
	int32 MinTris[SlonkFaunaMeshes::NumLods] = { 1 << 20, 1 << 20, 1 << 20 }, MaxTris[SlonkFaunaMeshes::NumLods] = {};
	double Margin = TNumericLimits<double>::Max(), MostStretch = 0.0, MostSeam = 0.0;
	FVector Reach(0.0);
	FRandomStream Rng(57);
	for (const FFaunaBodyPlan& Plan : Plans)
	{
		const double H = Fl::HalfHeightCm(Plan), Lt = 100.0 * Fl::TentaclesOf(Plan);
		const double Zr = H - 100.0 * Fl::BellOf(Plan);
		const int32 Tents = Fl::TentacleCount(Plan);
		FBox Box0(ForceInit);
		FDynamicMesh3 Lods[SlonkFaunaMeshes::NumLods];
		for (int32 L = 0; L < SlonkFaunaMeshes::NumLods; ++L)
		{
			FDynamicMesh3& M = Lods[L];
			if (!SlonkFaunaMeshes::BuildLod(Plan, L, M)) { AddError(TEXT("no jelly built")); return false; }
			const int32 Tris = M.TriangleCount();
			MinTris[L] = FMath::Min(MinTris[L], Tris);
			MaxTris[L] = FMath::Max(MaxTris[L], Tris);
			BadTris += Tris == WantJellyTris(Tents, L) && Tris == SlonkFaunaMeshes::JellyTriangles(Plan, L) ? 0 : 1;
			const FDynamicMeshNormalOverlay* N = M.Attributes()->PrimaryNormals();
			const TArray<FLinearColor> Vcs = VertexColours(M);
			const TArray<FVector2f> Uvs = VertexUVs(M);
			for (const int32 T : M.TriangleIndicesItr())
			{
				FVector3d Fn, Centroid; double Area;
				M.GetTriInfo(T, Fn, Area, Centroid);
				const FIndex3i E = N->GetTriangle(T);
				BadWinding += Fn.Dot(FVector3d(N->GetElement(E.A) + N->GetElement(E.B) + N->GetElement(E.C))) > 0.0 ? 0 : 1;
			}
			for (const int32 V : M.VertexIndicesItr())
			{
				const FVector P = (FVector)M.GetVertex(V);
				const FLinearColor& C = Vcs[V];
				const FVector2f& U = Uvs[V];
				if (L == 0) { Box0 += P; }
				bool bOk = U.X >= 0.f && U.X <= 1.f && C.B == U.X;
				if (C.R == Vc::SurfaceBody)
				{
					// The bell: rigid, down it from its crown to its rim as its V says (its speck's drawn whole, its V from 0.1
					// to under the crown's, or on its ring canal's at its rim), above its rim.
					const bool bV = L < 2 ? C.G == U.Y : (U.Y >= 0.1f && U.Y < Fl::CrownV) || (FMath::Abs(U.Y - Fl::RingV) < 0.01f && P.Z <= Zr + 0.1 * 100.0 * Fl::BellOf(Plan) + 1e-3);
					bOk &= C.A == Vc::JointRigid && C.G >= 0.f && C.G <= 1.f && bV && P.Z >= Zr - 3.01 && P.Z <= H + 1e-3;
				}
				else if (C.R == Vc::SurfaceTail)
				{
					// A tentacle: undulating, along it from its root (at the rim's height) to its tip (at -H) as its V says, its place
					// round the bell its root's.
					const double Along = 100.0 * (U.Y - 1.0);
					bOk &= C.A == Vc::JointUndulate && C.G >= 0.f && C.G <= 1.f && U.Y >= 1.f && FMath::IsNearlyEqual(P.Z, Zr - Along, 1e-2) && FMath::IsNearlyEqual(Along, Lt * C.G, 1e-2);
					if (L < 2) { const double Round = FMath::Frac(FMath::Atan2(P.Y, P.X) / UE_DOUBLE_TWO_PI + 1.0); bOk &= FMath::Abs(FMath::Frac(Round - C.B + 0.5) - 0.5) < 0.03; }
				}
				else { bOk = false; }
				BadColour += bOk ? 0 : 1;
			}
		}
		// Its box: its span across, its middle the origin; the coarser LODs within it (but for their rims, round where LOD 0's
		// is scalloped: within a tenth of its radius of it).
		OffBox += FMath::IsNearlyEqual(Box0.Max.X, Fl::RimCm, 1e-3) && Box0.Min.X >= -Fl::RimCm - 1e-3 && Box0.GetExtent().Y <= Fl::RimCm + 1e-3 && FMath::IsNearlyEqual(Box0.Max.Z, H, 1e-3)
			&& FMath::IsNearlyEqual(Box0.Min.Z, -H, 1e-3) ? 0 : 1;
		for (int32 L = 1; L < SlonkFaunaMeshes::NumLods; ++L)
		{
			for (const int32 V : Lods[L].VertexIndicesItr()) { OffBox += Box0.ExpandBy(FVector(0.1 * Fl::RimCm, 0.1 * Fl::RimCm, 1e-3)).IsInside((FVector)Lods[L].GetVertex(V)) ? 0 : 1; }
		}
		// The material's motion over every LOD's vertices: 30 cases of each plan.
		const FBox Padded = Box0.ExpandBy(Box0.GetExtent() * 0.15 + FVector(20.0 + D.CullPaddingUnits));
		for (int32 Case = 0; Case < 30; ++Case)
		{
			float Cu[SlonkFauna::Custom::Num] = {};
			float Gd[SlonkFauna::FloaterData::Num] = {};
			const float Size = Case % 5 == 0 ? (float)Fl::MinSpanM : Rng.FRandRange((float)Fl::MinSpanM, (float)Fl::MaxSpanM);
			Cu[SlonkFauna::Custom::SizeM] = Size;
			Cu[SlonkFauna::Custom::Seed] = Rng.FRand();
			Cu[SlonkFauna::Custom::Phase] = Rng.FRand();
			Cu[SlonkFauna::Custom::RateHz] = (float)Rng.RandRange(600, 1200) / 3600.f;
			Cu[SlonkFauna::Custom::PerKind] = (float)Plan.Symmetry;
			const float T = Rng.FRandRange(0.f, 3600.f);
			// Drifting through the air any way, slow to far faster than a jelly goes (the extremes often), changing.
			const float Spd = Case % 3 == 0 ? 30.f : Rng.FRandRange(0.f, 12.f), Way = Rng.FRandRange(0.f, 6.2831853f);
			Gd[SlonkFauna::FloaterData::TrailX] = Spd * FMath::Cos(Way);
			Gd[SlonkFauna::FloaterData::TrailY] = Spd * FMath::Sin(Way);
			Gd[SlonkFauna::FloaterData::TrailRateX] = Rng.FRandRange(-6.f, 6.f);
			Gd[SlonkFauna::FloaterData::TrailRateY] = Rng.FRandRange(-6.f, 6.f);
			Gd[SlonkFauna::FloaterData::ClockT0] = FMath::Fmod(T - Rng.FRandRange(-0.5f, 2.f) + 3600.f, 3600.f);
			// Calm, or frightened anywhere in its fright: its squeeze, its fast pulse coming in or easing.
			if (Case % 2 == 1)
			{
				Gd[SlonkFauna::FloaterData::Startled] = 1.f;
				const float Age = Rng.FRandRange(0.f, 150.f);
				Gd[SlonkFauna::FloaterData::PanicT0] = FMath::Fmod(T - Age + 3600.f, 3600.f);
				Gd[SlonkFauna::FloaterData::OnsetT0] = FMath::Fmod(T - Age - Rng.FRandRange(0.f, 1.f) + 3600.f, 3600.f);
				Gd[SlonkFauna::FloaterData::SqueezeT0] = FMath::Fmod(T - Rng.FRandRange(0.f, 8.f) + 3600.f, 3600.f);
			}
			// Near (no speck floor), where it grows, and far (twice its size).
			const float Dist = Case % 3 == 0 ? 5000.f : (Case % 3 == 1 ? Size * 100.f * 960.f / 5.f : 5.0e6f);
			for (int32 L = 0; L < SlonkFaunaMeshes::NumLods; ++L)
			{
				const TArray<FLinearColor> Vcs = VertexColours(Lods[L]);
				const TArray<FVector2f> Uvs = VertexUVs(Lods[L]);
				for (const int32 V : Lods[L].VertexIndicesItr())
				{
					const FVector3f LP = (FVector3f)Lods[L].GetVertex(V);
					const FVector3f Moved = LP + SlonkFauna::FloaterMotion(LP, Vcs[V], Uvs[V], T, Cu, Gd, Dist, 960.f);
					++Tried;
					const FVector Mv((double)Moved.X, (double)Moved.Y, (double)Moved.Z);
					Escaped += Padded.IsInside(Mv) ? 0 : 1;
					Reach = FVector(FMath::Max(Reach.X, FMath::Abs(Mv.X)), FMath::Max(Reach.Y, FMath::Abs(Mv.Y)), FMath::Max(Reach.Z, FMath::Abs(Mv.Z) - H));
					Margin = FMath::Min(Margin, FMath::Min3(FMath::Min(Mv.X - Padded.Min.X, Padded.Max.X - Mv.X), FMath::Min(Mv.Y - Padded.Min.Y, Padded.Max.Y - Mv.Y), FMath::Min(Mv.Z - Padded.Min.Z, Padded.Max.Z - Mv.Z)));
					// Across the hour's wrap, near: no seam.
					if (L == 0 && Case % 3 == 0 && Case < 6)
					{
						float Gw[SlonkFauna::FloaterData::Num];
						FMemory::Memcpy(Gw, Gd, sizeof(Gw));
						Gw[SlonkFauna::FloaterData::ClockT0] = 3599.99f;
						const FVector3f Wa = SlonkFauna::FloaterMotion(LP, Vcs[V], Uvs[V], 3599.995f, Cu, Gw, Dist, 960.f);
						const FVector3f Wb = SlonkFauna::FloaterMotion(LP, Vcs[V], Uvs[V], 0.005f, Cu, Gw, Dist, 960.f);
						const FVector3f Wc = SlonkFauna::FloaterMotion(LP, Vcs[V], Uvs[V], 0.015f, Cu, Gw, Dist, 960.f);
						MostSeam = FMath::Max(MostSeam, (double)(Wa - Wb).Size());
						Seams += (Wa - Wb).Size() <= 1.5f * (Wb - Wc).Size() + 0.05f ? 0 : 1;
					}
				}
			}
		}
		// A tentacle trailing at full stretch (6 m/s and faster), near: its tip as far from its root as its length, within a
		// fifth.
		{
			float Cu[SlonkFauna::Custom::Num] = {};
			float Gd[SlonkFauna::FloaterData::Num] = {};
			Cu[SlonkFauna::Custom::SizeM] = 40.f;
			Cu[SlonkFauna::Custom::RateHz] = 900.f / 3600.f;
			Cu[SlonkFauna::Custom::PerKind] = (float)Plan.Symmetry;
			Gd[SlonkFauna::FloaterData::TrailX] = 8.f;
			const TArray<FLinearColor> Vcs = VertexColours(Lods[0]);
			const TArray<FVector2f> Uvs = VertexUVs(Lods[0]);
			TMap<int32, FVector3f> Root, Tip;
			for (const int32 V : Lods[0].VertexIndicesItr())
			{
				if (Vcs[V].R != Vc::SurfaceTail || (Vcs[V].G != 0.f && Vcs[V].G != 1.f)) { continue; }
				const FVector3f LP = (FVector3f)Lods[0].GetVertex(V);
				const FVector3f P = LP + SlonkFauna::FloaterMotion(LP, Vcs[V], Uvs[V], 100.f, Cu, Gd, 5000.f, 960.f);
				const int32 Key = FMath::RoundToInt32(Vcs[V].B * 1000.f) * 2 + (LP.Y * FMath::Cos(Vcs[V].B * 6.2831853f) - LP.X * FMath::Sin(Vcs[V].B * 6.2831853f) > 0.f ? 1 : 0);
				(Vcs[V].G == 0.f ? Root : Tip).Add(Key, P);
			}
			for (const TPair<int32, FVector3f>& Tp : Tip)
			{
				if (const FVector3f* Rt = Root.Find(Tp.Key))
				{
					const double S = (double)(Tp.Value - *Rt).Size() / Lt;
					MostStretch = FMath::Max(MostStretch, FMath::Abs(S - 1.0));
					Stretched += FMath::Abs(S - 1.0) < 0.2 ? 0 : 1;
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d plans of jelly: LODs of %d to %d, %d to %d and %d triangles (%d off), %d vertices off the colour contract, %d faces wound inward, %d off the box"),
		Plans.Num(), MinTris[0], MaxTris[0], MinTris[1], MaxTris[1], MinTris[2], BadTris, BadColour, BadWinding, OffBox));
	AddInfo(FString::Printf(TEXT("%d vertices moved by the material: %d outside the box padded %.0f units (the closest %.1f units inside; reaching %.0f and %.0f units across, %.0f past its half-height along); tentacles trailing in full off their length by %.3f at most (%d over a fifth); %.3f units moved across the hour's wrap at most (%d more than the hundredth of a second after)"),
		Tried, Escaped, D.CullPaddingUnits, Margin, Reach.X, Reach.Y, Reach.Z, MostStretch, Stretched, MostSeam, Seams));
	TestEqual(TEXT("its LODs' triangles"), BadTris, 0);
	TestTrue(TEXT("about 300, 60 and 16"), MinTris[0] >= 280 && MaxTris[0] <= 340 && MinTris[1] >= 50 && MaxTris[1] <= 70 && MinTris[2] == 16 && MaxTris[2] == 16);
	TestEqual(TEXT("every vertex as the material reads it"), BadColour, 0);
	TestEqual(TEXT("every face shows the side its normals point to"), BadWinding, 0);
	TestEqual(TEXT("its span across, its middle the origin, its coarser LODs inside it"), OffBox, 0);
	TestEqual(TEXT("the material keeps every vertex in its instance's box"), Escaped, 0);
	TestEqual(TEXT("its tentacles keep their length as they trail"), Stretched, 0);
	TestEqual(TEXT("its motion runs on across the hour's wrap"), Seams, 0);

	// Those hold of the float copies only while the material's floater branches are what was copied.
	const FString WpoBlock = MaterialMotionBlock(EMotionBlock::Floaters);
	const FString PixelBlock = MaterialMotionBlock(EMotionBlock::Floaters, true);
	const uint32 WpoCrc = FCrc::StrCrc32(*WpoBlock), PixelCrc = FCrc::StrCrc32(*PixelBlock);
	AddInfo(FString::Printf(TEXT("fauna_material.py's floater branch: in WPO_CODE %d characters, CRC 0x%08x (0x%08x when the copy was made); in CODE %d characters, CRC 0x%08x (0x%08x)"),
		WpoBlock.Len(), WpoCrc, MaterialFloaterBlockCrc, PixelBlock.Len(), PixelCrc, MaterialFloaterPixelBlockCrc));
	TestTrue(TEXT("fauna_material.py's floater branches found"), WpoBlock.Contains(TEXT("float2 jbend = ")) && PixelBlock.Contains(TEXT("bool jpore = ")));
	TestEqual(TEXT("FloaterMotion copies the material's floater branch as it is now"), WpoCrc, MaterialFloaterBlockCrc);
	TestEqual(TEXT("FloaterCover and FloaterLight copy its pixel shader's as it is now"), PixelCrc, MaterialFloaterPixelBlockCrc);
	TestFalse(TEXT("the fliers' part holds none of the floater branch"), MaterialMotionBlock().Contains(TEXT("jbend")) || MaterialMotionBlock(EMotionBlock::Fliers, true).Contains(TEXT("jpore")));

	// The static mesh: made once and kept, three LODs, a tangent basis everywhere; where its LODs come in for jellies of
	// 50 m of the shortest and the tallest plans, through a 90-degree view at 16:9 (the GPU's radius, its padded box's).
	for (const FFaunaBodyPlan& Plan : { Plans[0], Plans.Last(8) })
	{
		UStaticMesh* Mesh = SlonkFaunaMeshes::GetMesh(Plan, EFaunaKind::Floater);
		TestTrue(TEXT("the jelly's mesh is made, and kept"), Mesh != nullptr && Mesh == SlonkFaunaMeshes::GetMesh(Plan, EFaunaKind::Floater));
		if (!Mesh) { continue; }
		TestEqual(TEXT("with three LODs"), Mesh->GetNumLODs(), SlonkFaunaMeshes::NumLods);
		const double SpanM = 50.0;
		UPlanetInstancesComponent* Set = NewObject<UPlanetInstancesComponent>();
		Set->SetStaticMesh(Mesh);
		Set->SetCullPadding(D.CullPaddingUnits);
		Set->SetPieces(TArray<FTransform>{ FTransform(FQuat::Identity, FVector::ZeroVector, FVector(SpanM)) }, TConstArrayView<float>());
		const double Proj = (16.0 / 9.0) / FMath::Tan(FMath::DegreesToRadians(45.0));
		const double GpuR = Set->GetMaxInstanceRadius();
		auto ComesInM = [&](int32 L) { return Proj * GpuR / D.LodScreenSize[L] / 100.0; };
		auto PxAt = [&](double M) { return SpanM * 960.0 / M; };
		AddInfo(FString::Printf(TEXT("a %.0f m jelly of a plan %.0f units tall (LOD radius %.1f spans): its 60-odd triangles from %.0f m (%.1f pixels across), its 16 from %.0f m (%.1f pixels)"),
			SpanM, 2.0 * Fl::HalfHeightCm(Plan), GpuR / (100.0 * SpanM), ComesInM(1), PxAt(ComesInM(1)), ComesInM(2), PxAt(ComesInM(2))));
		TestTrue(TEXT("its 60-odd triangles from about 40 pixels across"), PxAt(ComesInM(1)) > 30.0 && PxAt(ComesInM(1)) < 50.0);
		TestTrue(TEXT("and its 16 from about 6"), PxAt(ComesInM(2)) > 4.5 && PxAt(ComesInM(2)) < 8.5);
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
		TestTrue(TEXT("every vertex has a tangent basis"), Vertices > 0 && NoTangent == 0);
	}

	// Its membrane: over a fine lattice of its bell's UV, the share left whole (its pores the same at every moment: they ask no
	// time), its crown, ribs and ring canal whole, its tentacles whole; its light at night on a rib, between them and on a
	// tentacle over a few pulses.
	float Cu[SlonkFauna::Custom::Num] = {};
	Cu[SlonkFauna::Custom::Seed] = 0.37f;
	Cu[SlonkFauna::Custom::PerKind] = 8.f;
	Cu[SlonkFauna::Custom::RateHz] = 900.f / 3600.f;
	int32 Panel = 0, PanelWhole = 0, CrownHoles = 0, RibHoles = 0, TentacleHoles = 0;
	for (int32 j = 0; j < 400; ++j)
	{
		const float V = (j + 0.5f) / 400.f;
		for (int32 i = 0; i < 1600; ++i)
		{
			const float U = (i + 0.5f) / 1600.f;
			const bool bWhole = SlonkFauna::FloaterCover(FVector2f(U, V), FLinearColor(Vc::SurfaceBody, V, U, Vc::JointRigid), Cu);
			const float RibD = FMath::Abs(FMath::Frac(U * 8.f + 0.5f) - 0.5f);
			if (V < Fl::CrownV) { CrownHoles += bWhole ? 0 : 1; continue; }
			if (RibD < 0.03f || FMath::Abs(V - Fl::RingV) < 0.01f) { RibHoles += bWhole ? 0 : 1; continue; }
			++Panel;
			PanelWhole += bWhole ? 1 : 0;
		}
		TentacleHoles += SlonkFauna::FloaterCover(FVector2f((j % 8 + 0.5f) / 8.f, 1.f + V), FLinearColor(Vc::SurfaceTail, V, 0.5f, Vc::JointUndulate), Cu) ? 0 : 1;
	}
	const double Whole = Panel > 0 ? (double)PanelWhole / Panel : 0.0;
	float Gd[SlonkFauna::FloaterData::Num] = {};
	float RibLo = 1e9f, RibHi = 0.f, BetweenHi = 0.f, TentHi = 0.f;
	for (float T = 100.f; T < 112.f; T += 0.02f)
	{
		const float Rib = SlonkFauna::FloaterLight(FVector2f(0.f, 0.5f), FLinearColor(Vc::SurfaceBody, 0.5f, 0.f, Vc::JointRigid), T, Cu, Gd);
		RibLo = FMath::Min(RibLo, Rib);
		RibHi = FMath::Max(RibHi, Rib);
		BetweenHi = FMath::Max(BetweenHi, SlonkFauna::FloaterLight(FVector2f(1.f / 16.f, 0.5f), FLinearColor(Vc::SurfaceBody, 0.5f, 1.f / 16.f, Vc::JointRigid), T, Cu, Gd));
		TentHi = FMath::Max(TentHi, SlonkFauna::FloaterLight(FVector2f(0.5f, 1.5f), FLinearColor(Vc::SurfaceTail, 0.5f, 0.5f, Vc::JointUndulate), T, Cu, Gd));
	}
	AddInfo(FString::Printf(TEXT("its bell's panels %.0f%% whole (%d points), %d holes in its crown, %d in its ribs and ring canal, %d in its tentacles; at night a rib lit %.2f to %.2f over its pulses, between them %.2f, a tentacle up to %.2f"),
		100.0 * Whole, Panel, CrownHoles, RibHoles, TentacleHoles, RibLo, RibHi, BetweenHi, TentHi));
	TestTrue(TEXT("its membrane about half whole, translucent"), Whole > 0.35 && Whole < 0.7);
	TestTrue(TEXT("its crown, ribs, ring canal and tentacles whole"), CrownHoles == 0 && RibHoles == 0 && TentacleHoles == 0);
	TestTrue(TEXT("a pulse of light down its ribs and tentacles, a little always"), RibLo > 0.15f && RibLo < 0.3f && RibHi > 1.5f && TentHi > 1.5f && BetweenHi == 0.f);

	// Pale, its hue turned with a strange world's sky.
	const SlonkFauna::FFaunaLook Earth;
	FWorldGenome Turned;
	Turned.Strangeness = 1.f;
	Turned.SkyHueShiftDeg = 150.f;
	const FLinearColor Jelly = SlonkFauna::FloaterPigment(Earth, 0.0, 1.0), Strange = SlonkFauna::FloaterPigment(SlonkFauna::LookOf(Turned), 0.0, 1.0);
	const FLinearColor Hsv = Jelly.LinearRGBToHSV();
	AddInfo(FString::Printf(TEXT("a jelly's pigment: %.2f %.2f %.2f (hue %.0f deg, saturation %.2f) before its world's turn, %.2f %.2f %.2f (hue %.0f deg) under a sky turned 150 deg"),
		Jelly.R, Jelly.G, Jelly.B, Hsv.R, Hsv.G, Strange.R, Strange.G, Strange.B, HueOf(Strange)));
	TestTrue(TEXT("pale"), Hsv.G < 0.5f && Hsv.B > 0.6f);
	TestTrue(TEXT("turned with a strange world's sky"), HueGap(HueOf(Strange), HueOf(Jelly)) > 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaFloaterBudgetTest, "Slonk.Fauna.FloaterBudget", SLONK_TEST_FLAGS)
bool FSlonkFaunaFloaterBudgetTest::RunTest(const FString& Parameters)
{
	// About any camera over a world teeming with jellies they keep to their caps (24, under 7,500 triangles at their nearest
	// LOD), the caps full; a frame's math for them (each jelly's frame on its loop or fleeing, the threats to it, what its
	// material reads: not the scene's writes, which want a world) is far under 0.15 ms of the game thread; a scan's first
	// reading of the jellies' cells takes a few tens of milliseconds on its task, and one a sixth of a cell on reads only the
	// cells new to it.
	using namespace SlonkFauna;
	FFaunaWorld W;
	if (!JellyWorld(0.9f, 0.45f, 0.6f, W)) { AddError(TEXT("no teeming jelly world found")); return false; }
	const FFaunaKindDesc& D = KindDesc(EFaunaKind::Floater);
	const double R = W.P.RadiusCm;
	int32 Places = 0, Over = 0, Full = 0, MaxGroups = 0, MaxTris = 0;
	double FirstMs = 0.0, MovedMs = 0.0;
	int32 FirstCells = 0, MovedCells = 0, MovedRead = 0;
	FFaunaField Busiest;
	for (int32 i = 0; i < 16; ++i)
	{
		FFaunaField F;
		F.SetWorld(W);
		TArray<uint64> Active;
		double Ms = 0.0;
		int32 Read = 0, Cells = 0;
		const FVector Dir = Spread(i * 37 + 5, 600);
		ScanAndSelect(F, CameraOver(W, Dir), Active, &Ms, &Read, &Cells);
		++Places;
		const int32 Groups = F.ActiveGroups(EFaunaKind::Floater);
		Over += Groups <= D.MaxGroups && F.ActiveMembers(EFaunaKind::Floater) <= D.MaxMembers ? 0 : 1;
		Full += Groups == D.MaxGroups ? 1 : 0;
		if (Groups > MaxGroups) { MaxGroups = Groups; Busiest = F; }
		MaxTris = FMath::Max(MaxTris, Groups * SlonkFaunaMeshes::JellyTriangles(W.Profile.PlanOf(EFaunaKind::Floater), 0));
		if (i == 0)
		{
			FirstMs = Ms;
			FirstCells = Read;
			ScanAndSelect(F, CameraOver(W, AlongGround(Dir, D.CellCm / 6.0 + 100.0, R, 0.3)), Active, &MovedMs, &MovedRead, &MovedCells);
		}
	}
	// A frame's math at the busiest place, as UpdateFloater does it, a craft passing by.
	TMap<uint64, FPanic> Panics;
	constexpr int32 Frames = 600;
	double Sink = 0.0;
	const double Start = FPlatformTime::Seconds();
	for (int32 f = 0; f < Frames; ++f)
	{
		const double Now = 500.0 + f / 60.0;
		for (const TPair<uint64, FFaunaField::FGroupRef>& A : Busiest.GetActive())
		{
			FPanic& P = Panics.FindOrAdd(A.Key);
			FFaunaPose Pose = FlightAt(*A.Value, D, P, R, Now, FFaunaPose(), 0.0);
			FFaunaThreat Craft;
			Craft.PosCm = Pose.PosCm + Pose.Rot.GetAxisY() * 1.0e4;
			Craft.AltitudeCm = 5.0e4;
			FFaunaThreat Walker;
			Walker.bCraft = false;
			Walker.PosCm = Pose.PosCm - Pose.PosCm.GetSafeNormal() * 5.0e4;
			if (StartleGroup(P, D, Pose.PosCm, Craft, Now) || StartleGroup(P, D, Pose.PosCm, Walker, Now)) { Pose = FlightAt(*A.Value, D, P, R, Now, FFaunaPose(), 0.0); }
			float Gd[FloaterData::Num] = {};
			FloaterGroupData(*A.Value, D, P, R, Now, Pose.Rot, 0.0, Gd);
			Sink += Pose.PosCm.X + Gd[FloaterData::TrailX];
		}
	}
	const double FrameMs = (FPlatformTime::Seconds() - Start) * 1000.0 / Frames;
	AddInfo(FString::Printf(TEXT("about %d cameras on a world rolling jellies into %.0f%% of its cells: at most %d jellies (%d triangles at LOD 0), the caps full at %d of them, %d over; a frame's math on %d jellies %.4f ms; a first scan %.1f ms reading %d cells anew; one a sixth of a cell on %.1f ms, %d of its %d cells read anew"),
		Places, 100.0 * W.Profile.FloaterShare, MaxGroups, MaxTris, Full, Over, Busiest.GetActive().Num(), FrameMs, FirstMs, FirstCells, MovedMs, MovedRead, MovedCells));
	TestTrue(TEXT("places to try, jellies about"), Places >= 16 && MaxGroups > 0 && FMath::IsFinite(Sink));
	TestEqual(TEXT("never over the jellies' caps"), Over, 0);
	TestTrue(TEXT("the caps full on a teeming world"), Full > 0);
	TestTrue(TEXT("under 7,500 triangles of jellies at LOD 0"), MaxTris <= 7500);
	TestTrue(TEXT("a frame's math far under 0.15 ms"), FrameMs < 0.15);
	TestTrue(TEXT("a scan a sixth of a cell on reads only the cells new to it"), MovedCells > 0 && MovedRead * 4 < MovedCells);

	// With every kind about (the fliers and the herds too): a camera near the ground has every kind's cells read, one 6 km
	// up the jellies' alone (no other kind is kept that high, and their habitat is the dearest to read), and coming up or
	// down past a kind's height asks for a scan where it stands.
	FFaunaWorld All = W;
	All.KindMask = AllKinds;
	FFaunaField Mixed;
	Mixed.SetWorld(All);
	const FVector Dir = Spread(5, 600);
	const uint32 Low = Mixed.ScanKinds(3000.0), High = Mixed.ScanKinds(6.0e5);
	const auto SLow = FFaunaField::Scan(All, Dir, Mixed.ScanRadiusCm(Low), nullptr, Low);
	Mixed.Accept(*SLow);
	const bool bUpWants = Mixed.WantsScan(Dir, 6.0e5), bStillWants = Mixed.WantsScan(Dir, 3000.0);
	const auto SHigh = FFaunaField::Scan(All, Dir, Mixed.ScanRadiusCm(High), Mixed.GetCache(), High);
	int32 LowKinds[NumKinds] = {}, HighOthers = 0;
	for (const uint64 Id : SLow->CellIds) { LowKinds[FMath::Clamp((int32)(Id >> 60), 0, NumKinds - 1)] += 1; }
	for (const uint64 Id : SHigh->CellIds) { HighOthers += (EFaunaKind)(Id >> 60) == EFaunaKind::Floater ? 0 : 1; }
	AddInfo(FString::Printf(TEXT("with the fliers and herds about too: a camera 30 m up has %d fliers', %d herds' and %d jellies' cells read (kinds 0x%x), one 6 km up %d cells, %d of other kinds (kinds 0x%x; %d read anew); coming up asks for a scan %s, staying %s"),
		LowKinds[(int32)EFaunaKind::Flier], LowKinds[(int32)EFaunaKind::Herd], LowKinds[(int32)EFaunaKind::Floater], SLow->Kinds, SHigh->CellIds.Num(), HighOthers, SHigh->Kinds, SHigh->Evaluated,
		bUpWants ? TEXT("yes") : TEXT("no"), bStillWants ? TEXT("yes") : TEXT("no")));
	TestTrue(TEXT("near the ground every kind's cells read"), LowKinds[(int32)EFaunaKind::Flier] > 0 && LowKinds[(int32)EFaunaKind::Herd] > 0 && LowKinds[(int32)EFaunaKind::Floater] > 0);
	TestTrue(TEXT("6 km up the jellies' alone"), SHigh->CellIds.Num() > 0 && HighOthers == 0 && SHigh->Kinds == KindBit(EFaunaKind::Floater) && SHigh->Evaluated == 0);
	TestTrue(TEXT("coming up past the others' height asks for a scan, staying put does not"), bUpWants && !bStillWants);

	// Flying about the fliers' and herds' height over rolling ground (4.6 km over it and 3.9 km, twenty times, over the
	// place read near the ground): no scan asked for, their cells kept. Up to 6 km and back down to the ground: a scan each
	// way, the way down reading no cell anew (the cells of the kinds the way up's scan did not read kept as they were).
	FFaunaField Rolling;
	Rolling.SetWorld(All);
	Rolling.Accept(*SLow);
	auto FlyAt = [&](double AltCm, int32& Scans, int32& Read)
	{
		if (!Rolling.WantsScan(Dir, AltCm)) { return; }
		const uint32 K = Rolling.ScanKinds(AltCm);
		const auto S = FFaunaField::Scan(All, Dir, Rolling.ScanRadiusCm(K), Rolling.GetCache(), K);
		Rolling.Accept(*S);
		++Scans;
		Read += S->Evaluated;
	};
	int32 RollScans = 0, RollRead = 0, UpScans = 0, UpRead = 0, DownScans = 0, DownRead = 0;
	for (int32 k = 0; k < 20; ++k)
	{
		FlyAt(4.6e5, RollScans, RollRead);
		FlyAt(3.9e5, RollScans, RollRead);
	}
	FlyAt(6.0e5, UpScans, UpRead);
	FlyAt(3000.0, DownScans, DownRead);
	AddInfo(FString::Printf(TEXT("flying between 3.9 and 4.6 km over the ground twenty times: %d scans asked for, %d cells read anew; up to 6 km %d scan (%d cells read anew), back down to 30 m %d (%d read anew)"),
		RollScans, RollRead, UpScans, UpRead, DownScans, DownRead));
	TestEqual(TEXT("flying about the fliers' height over rolling ground asks for no scan"), RollScans, 0);
	TestTrue(TEXT("up past it and back down, a scan each way"), UpScans == 1 && DownScans == 1);
	TestEqual(TEXT("and the way down reads no cell anew"), DownRead, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaFloaterGateTest, "Slonk.Fauna.FloaterGate", SLONK_TEST_FLAGS)
bool FSlonkFaunaFloaterGateTest::RunTest(const FString& Parameters)
{
	// A camera coming straight down from 30 km over a world teeming with jellies to 2 km, and climbing back, at eight
	// places over it: every jelly it spawns is past its fade (its nearest drawn point, wherever round its loop it is,
	// farther from the camera than the kind's FadeCm), and so is every one it lets go (but those let go to make room for
	// nearer ones, past their fade along the ground by their loops already), so none pops in or out. The same for one of
	// the highest-drifting, largest jellies over a loop whose ground stands 1, 3 and 6 km higher than the ground under the
	// scan's middle (a ridge, a range), under the camera and 4 and 9 km off to its side: spawned, and past its fade.
	using namespace SlonkFauna;
	FFaunaWorld W;
	if (!JellyWorld(0.9f, 0.45f, 0.6f, W)) { AddError(TEXT("no teeming jelly world found")); return false; }
	const FFaunaKindDesc& D = KindDesc(EFaunaKind::Floater);
	const double R = W.P.RadiusCm;
	const double H = Floater::HalfHeightCm(W.Profile.PlanOf(EFaunaKind::Floater));
	// How far from its middle a jelly is drawn at most: its instance's padded box (its mesh's box grown by 0.15 of it and
	// 20 units, and by the kind's padding) at its span.
	const double BoxAcross = 1.15 * Floater::RimCm + 20.0 + D.CullPaddingUnits, BoxAlong = 1.15 * H + 20.0 + D.CullPaddingUnits;
	auto Nearest = [&](const FFaunaGroup& G, const FVector& Cam)
	{
		const double Reach = (double)G.SpanM * FMath::Sqrt(2.0 * BoxAcross * BoxAcross + BoxAlong * BoxAlong);
		double Least = TNumericLimits<double>::Max();
		for (int32 k = 0; k < 96; ++k) { Least = FMath::Min(Least, FVector::Dist(PathAt(G, R, G.PeriodSec * k / 96.0).PosCm, Cam) - Reach); }
		return Least;
	};
	auto TopOf = [](const FFaunaGroup& G)
	{
		double Top = -1.0e30;
		for (const float Hg : G.Ground) { Top = FMath::Max(Top, (double)Hg); }
		return Top;
	};
	struct FFlown
	{
		int32 Spawned = 0, LetGo = 0, Near = 0;
		double Least = TNumericLimits<double>::Max(), LowestSpawn = TNumericLimits<double>::Max(), HighestSpawn = 0.0, MostRise = 0.0;
	};
	// Down from 30 km to 2 km over the ground under the scan's middle in 50 m steps, and back up.
	auto Fly = [&](FFaunaField& F, const FVector& Dir, FFlown& Out)
	{
		TArray<FFaunaField::FGroupRef> Spawned;
		TArray<uint64> LetGo;
		TMap<uint64, FFaunaField::FGroupRef> Known;
		const double Ground = F.GetGroundCm();
		auto Step = [&](double Alt)
		{
			const FVector Cam = Dir * (R + Ground + Alt);
			F.Select(Cam, Spawned, LetGo);
			for (const FFaunaField::FGroupRef& G : Spawned)
			{
				Known.Add(G->Id, G);
				const double Nr = Nearest(*G, Cam);
				++Out.Spawned;
				Out.Near += Nr > D.FadeCm ? 0 : 1;
				Out.Least = FMath::Min(Out.Least, Nr);
				Out.LowestSpawn = FMath::Min(Out.LowestSpawn, Alt);
				Out.HighestSpawn = FMath::Max(Out.HighestSpawn, Alt);
				Out.MostRise = FMath::Max(Out.MostRise, TopOf(*G) - Ground);
			}
			for (const uint64 Id : LetGo)
			{
				const FFaunaField::FGroupRef* G = Known.Find(Id);
				if (!G || FVector::Dist(Dir, (*G)->Centre) * R - (*G)->ExtentCm() > D.FadeCm) { continue; }
				const double Nr = Nearest(**G, Cam);
				++Out.LetGo;
				Out.Near += Nr > D.FadeCm ? 0 : 1;
				Out.Least = FMath::Min(Out.Least, Nr);
			}
		};
		for (double Alt = 3.0e6; Alt >= 2.0e5; Alt -= 5000.0) { Step(Alt); }
		for (double Alt = 2.0e5; Alt <= 3.0e6; Alt += 5000.0) { Step(Alt); }
	};
	FFlown Real;
	for (int32 i = 0; i < 8; ++i)
	{
		const FVector Dir = Spread(i * 71 + 13, 600);
		FFaunaField F;
		F.SetWorld(W);
		F.Accept(*FFaunaField::Scan(W, Dir, F.ScanRadiusCm(), nullptr));
		Fly(F, Dir, Real);
	}
	AddInfo(FString::Printf(TEXT("coming down from 30 km to 2 km at 8 places over a world rolling jellies into %.0f%% of its cells, and climbing back: %d spawned (the camera %.1f to %.1f km over the ground under the scan's middle; their loops' ground up to %.0f m higher than that), %d let go by its height; their nearest drawn points %.2f km from the camera at the least (they fade out by %.1f km)"),
		100.0 * W.Profile.FloaterShare, Real.Spawned, Real.LowestSpawn / 1e5, Real.HighestSpawn / 1e5, Real.MostRise / 100.0, Real.LetGo, Real.Least / 1e5, D.FadeCm / 1e5));
	TestTrue(TEXT("jellies spawned coming down and let go climbing"), Real.Spawned > 20 && Real.LetGo > 20);
	TestEqual(TEXT("every one past its fade as it comes and goes"), Real.Near, 0);

	// A ridge's jelly: one of the highest-drifting and largest, its loop's highest ground 1, 3 and 6 km over the ground
	// under the scan's middle, under the camera and off to its side.
	FFaunaGroup Base;
	if (!TestJelly(W, Base)) { AddError(TEXT("no jelly found")); return false; }
	const FVector Dir = Spread(5, 600);
	const TSharedRef<FFaunaField::FScan, ESPMode::ThreadSafe> Bare = FFaunaField::Scan(W, Dir, 0.0, nullptr, 0u);
	FFlown Ridge;
	int32 Cases = 0, CasesSpawned = 0;
	double RidgeLow = TNumericLimits<double>::Max(), RidgeHigh = 0.0;
	for (const double Rise : { 1.0e5, 3.0e5, 6.0e5 })
	{
		for (const double Off : { 0.0, 4.0e5, 9.0e5 })
		{
			FFaunaGroup J = Base;
			J.Centre = AlongGround(Dir, Off, R, 0.6);
			J.East = FVector::CrossProduct(FMath::Abs(J.Centre.Z) < 0.95 ? FVector::ZAxisVector : FVector::XAxisVector, J.Centre).GetSafeNormal();
			J.North = FVector::CrossProduct(J.Centre, J.East);
			const double Top = TopOf(Base);
			for (float& Hg : J.Ground) { Hg = (float)(Bare->GroundCm + Rise + ((double)Hg - Top)); }
			J.CruiseCm = Floater::MaxCruiseCm;
			J.BobCm = 6.0e3;
			J.SpanM = (float)Floater::MaxSpanM;
			FFaunaField::FScan S = *Bare;
			S.CellIds = { J.Id };
			S.CellGroups = { MakeShared<const FFaunaGroup, ESPMode::ThreadSafe>(J) };
			S.Kinds = KindBit(EFaunaKind::Floater);
			FFaunaField F;
			F.SetWorld(W);
			F.Accept(S);
			FFlown One;
			Fly(F, Dir, One);
			++Cases;
			CasesSpawned += One.Spawned > 0 && One.LetGo > 0 ? 1 : 0;
			Ridge.Spawned += One.Spawned;
			Ridge.LetGo += One.LetGo;
			Ridge.Near += One.Near;
			Ridge.Least = FMath::Min(Ridge.Least, One.Least);
			RidgeLow = FMath::Min(RidgeLow, One.LowestSpawn);
			RidgeHigh = FMath::Max(RidgeHigh, One.HighestSpawn);
		}
	}
	AddInfo(FString::Printf(TEXT("a jelly of %.0f m drifting %.0f m over a loop whose ground stands 1 to 6 km over the ground under the scan's middle, under the camera and off to its side: %d of %d cases spawned and let go (spawned with the camera %.1f to %.1f km over that ground), its nearest drawn point %.2f km from the camera at the least"),
		Floater::MaxSpanM, Floater::MaxCruiseCm / 100.0, CasesSpawned, Cases, RidgeLow / 1e5, RidgeHigh / 1e5, Ridge.Least / 1e5));
	TestEqual(TEXT("a ridge's jelly spawned and let go in every case"), CasesSpawned, Cases);
	TestEqual(TEXT("and past its fade as it comes and goes"), Ridge.Near, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaJellySpeckTest, "Slonk.Fauna.JellySpeck", SLONK_TEST_FLAGS)
bool FSlonkFaunaJellySpeckTest::RunTest(const FString& Parameters)
{
	// Each of a jelly's coarser LODs covers about as much of the sky as its finest does, seen from far off straight from
	// above, from below and from the side (at eight bearings round it), within half as much again either way: its speck
	// (LOD 2) drawn whole, as its finer LODs' pores (under a pixel by then) and their bell's two layers seen from the side
	// leave most of it, with a level diamond at its rim for a view from above or below, to which its crossed planes are
	// edge-on. Its tentacles glow about as much at night, within half as much again either way: its speck's sheet is as
	// wide as its ribbons laid side by side, shared between its two planes. From the side that is on average round it (its
	// ribbons hide one another, front and back, as the view comes round, by as much as half); from above and below its
	// tentacles are edge-on at every LOD and glow next to nothing. And all of it glows about as much, from above, below and
	// the side (round it): its speck's ribs lit over its whole bell, and a strip of its ring canal at its rim.
	using namespace UE::Geometry;
	const TArray<FFaunaBodyPlan> Plans = JellyPlans();
	struct FView { FVector Dir; int32 Class; };
	TArray<FView> Views = { { -FVector::ZAxisVector, 0 }, { FVector::ZAxisVector, 1 } };
	for (int32 k = 0; k < 8; ++k) { const double A = UE_DOUBLE_HALF_PI * k / 8.0; Views.Add({ FVector(FMath::Cos(A), FMath::Sin(A), 0.0), 2 }); }
	constexpr double Factor = 1.5;
	double CoverLo[3][3], CoverHi[3][3], TentLo[3], TentHi[3], EachLo[3], EachHi[3], GlowLo[3], GlowHi[3], EdgeOn[3], TopGlowLo[3], TopGlowHi[3];
	for (int32 L = 0; L < 3; ++L)
	{
		for (int32 C = 0; C < 3; ++C) { CoverLo[L][C] = 1e9; CoverHi[L][C] = 0.0; }
		TentLo[L] = EachLo[L] = GlowLo[L] = TopGlowLo[L] = 1e9;
		TentHi[L] = EachHi[L] = GlowHi[L] = EdgeOn[L] = TopGlowHi[L] = 0.0;
	}
	int32 CoverOff = 0, GlowOff = 0;
	FJellySight First[3][3];
	for (int32 p = 0; p < Plans.Num(); ++p)
	{
		const FFaunaBodyPlan& Plan = Plans[p];
		FDynamicMesh3 Lods[SlonkFaunaMeshes::NumLods];
		for (int32 L = 0; L < SlonkFaunaMeshes::NumLods; ++L)
		{
			if (!SlonkFaunaMeshes::BuildLod(Plan, L, Lods[L])) { AddError(TEXT("no jelly built")); return false; }
		}
		float Cu[SlonkFauna::Custom::Num] = {};
		Cu[SlonkFauna::Custom::Seed] = 0.37f;
		Cu[SlonkFauna::Custom::PerKind] = (float)Plan.Symmetry;
		Cu[SlonkFauna::Custom::RateHz] = 0.25f;
		TArray<FJellySight> Sights[SlonkFaunaMeshes::NumLods];
		for (int32 L = 0; L < SlonkFaunaMeshes::NumLods; ++L)
		{
			for (const FView& V : Views) { Sights[L].Add(SeeJelly(Lods[L], V.Dir, Cu)); }
			if (p == 0) { First[L][0] = Sights[L][0]; First[L][1] = Sights[L][1]; First[L][2] = Sights[L][2]; }
		}
		for (int32 L = 1; L < SlonkFaunaMeshes::NumLods; ++L)
		{
			double Tent0 = 0.0, TentL = 0.0, Glow0 = 0.0, GlowL = 0.0;
			for (int32 v = 0; v < Views.Num(); ++v)
			{
				const FJellySight& S0 = Sights[0][v];
				const FJellySight& S = Sights[L][v];
				const double Rc = S.Covered / FMath::Max(S0.Covered, 1.0);
				CoverLo[L][Views[v].Class] = FMath::Min(CoverLo[L][Views[v].Class], Rc);
				CoverHi[L][Views[v].Class] = FMath::Max(CoverHi[L][Views[v].Class], Rc);
				CoverOff += Rc >= 1.0 / Factor && Rc <= Factor ? 0 : 1;
				if (Views[v].Class == 2)
				{
					Tent0 += S0.TentacleGlow;
					TentL += S.TentacleGlow;
					Glow0 += S0.Glow;
					GlowL += S.Glow;
					const double Re = S.TentacleGlow / FMath::Max(S0.TentacleGlow, 1.0);
					EachLo[L] = FMath::Min(EachLo[L], Re);
					EachHi[L] = FMath::Max(EachHi[L], Re);
				}
				else
				{
					const double Most = FMath::Max(S0.TentacleGlow, S.TentacleGlow) / FMath::Max(S0.Covered, 1.0);
					EdgeOn[L] = FMath::Max(EdgeOn[L], Most);
					const double Rb = S.Glow / FMath::Max(S0.Glow, 1.0);
					TopGlowLo[L] = FMath::Min(TopGlowLo[L], Rb);
					TopGlowHi[L] = FMath::Max(TopGlowHi[L], Rb);
					GlowOff += Rb >= 1.0 / Factor && Rb <= Factor ? 0 : 1;
					GlowOff += Most < 0.03 ? 0 : 1;
				}
			}
			const double Rt = TentL / FMath::Max(Tent0, 1.0), Rg = GlowL / FMath::Max(Glow0, 1.0);
			TentLo[L] = FMath::Min(TentLo[L], Rt);
			TentHi[L] = FMath::Max(TentHi[L], Rt);
			GlowLo[L] = FMath::Min(GlowLo[L], Rg);
			GlowHi[L] = FMath::Max(GlowHi[L], Rg);
			GlowOff += Rt >= 1.0 / Factor && Rt <= Factor && Rg >= 1.0 / Factor && Rg <= Factor ? 0 : 1;
		}
	}
	AddInfo(FString::Printf(TEXT("the first plan (%d ribs, %d tentacles), unit-mesh cm squared covered from above, below and the side at bearing 0, LODs 0/1/2: %.0f/%.0f/%.0f, %.0f/%.0f/%.0f, %.0f/%.0f/%.0f; its tentacles glowing from the side %.0f/%.0f/%.0f"),
		Plans[0].Symmetry, SlonkFauna::Floater::TentacleCount(Plans[0]), First[0][0].Covered, First[1][0].Covered, First[2][0].Covered, First[0][1].Covered, First[1][1].Covered, First[2][1].Covered,
		First[0][2].Covered, First[1][2].Covered, First[2][2].Covered, First[0][2].TentacleGlow, First[1][2].TentacleGlow, First[2][2].TentacleGlow));
	for (int32 L = 1; L < SlonkFaunaMeshes::NumLods; ++L)
	{
		AddInfo(FString::Printf(TEXT("%d plans, LOD %d against LOD 0: covering %.2f to %.2f as much from above, %.2f to %.2f from below, %.2f to %.2f from the side; its tentacles glowing %.2f to %.2f as much from the side round it (%.2f to %.2f at a bearing), all of it %.2f to %.2f; from above and below its tentacles' glow at most %.3f of LOD 0's cover, all its glow %.2f to %.2f as much"),
			Plans.Num(), L, CoverLo[L][0], CoverHi[L][0], CoverLo[L][1], CoverHi[L][1], CoverLo[L][2], CoverHi[L][2], TentLo[L], TentHi[L], EachLo[L], EachHi[L], GlowLo[L], GlowHi[L], EdgeOn[L],
			TopGlowLo[L], TopGlowHi[L]));
	}
	TestEqual(TEXT("each LOD covers within half as much again of the finest's, from above, below and the side"), CoverOff, 0);
	TestEqual(TEXT("and its tentacles glow as much, within half as much again, from the side (next to nothing from above and below), and all of it from above, below and the side"), GlowOff, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaFloaterShadowTest, "Slonk.Fauna.FloaterShadow", SLONK_TEST_FLAGS)
bool FSlonkFaunaFloaterShadowTest::RunTest(const FString& Parameters)
{
	// A jelly's shadow falls away from the sun from its middle down to the ground under it: straight under it with the sun
	// overhead; 2 km up with the sun 30 degrees up, 3.46 km off along the ground, so crew standing in it, 4 km from the jelly
	// and past the kind's 3.5 km reach for shadows, have it drawn (the nearer of the jelly and where its shadow falls, as
	// UPlanetFaunaComponent::UpdateFloater switches it, is within it); with the sun all but on the horizon or under it,
	// none (its middle). The ground its loop has it over is the ground its path flies it over.
	using namespace SlonkFauna;
	FFaunaWorld W;
	if (!JellyWorld(0.8f, 0.2f, 0.6f, W)) { AddError(TEXT("no jelly world found")); return false; }
	FFaunaGroup G;
	if (!TestJelly(W, G)) { AddError(TEXT("no jelly found")); return false; }
	const double R = W.P.RadiusCm;
	const FFaunaKindDesc& D = KindDesc(EFaunaKind::Floater);
	double OffPath = 0.0;
	for (int32 k = 0; k < 96; ++k)
	{
		const double T = G.PeriodSec * k / 96.0;
		const double Over = PathAt(G, R, T).PosCm.Size() - R - FloaterGroundAt(G, T) - G.CruiseCm;
		OffPath = FMath::Max(OffPath, FMath::Max(-Over, Over - G.BobCm));
	}
	const FVector Up = G.Centre, E = G.East;
	const double Ground = 4.0e4, Over = 2.0e5;
	const FVector Pos = Up * (R + Ground + Over), Under = Up * (R + Ground);
	const FVector Overhead = FloaterShadowFall(Pos, R, Ground, Up);
	const FVector Sun30 = Up * FMath::Sin(UE_DOUBLE_PI / 6.0) + E * FMath::Cos(UE_DOUBLE_PI / 6.0);
	const FVector At30 = FloaterShadowFall(Pos, R, Ground, Sun30);
	const FVector AtDusk = FloaterShadowFall(Pos, R, Ground, (Up * 0.02 + E).GetSafeNormal());
	const FVector AtNight = FloaterShadowFall(Pos, R, Ground, (E - Up * 0.3).GetSafeNormal());
	const double Along30 = (At30 - Under).Size(), Level30 = FVector::DotProduct(At30 - Under, Up);
	const FVector Crew = At30 + Up * 180.0;
	const double JellyOff = FVector::Dist(Pos, Crew), Switched = FMath::Min(JellyOff, FVector::Dist(At30, Crew));
	AddInfo(FString::Printf(TEXT("its path %.2f cm off its loop's ground, cruise and bob at the most; a jelly 2 km up: its shadow %.1f cm from under it with the sun overhead, %.3f km along the ground (%.1f cm off it) with the sun 30 degrees up, crew there %.2f km from it (its shadow switched by %.2f km, its reach %.1f km); its middle with the sun at 1 degree %s, under the horizon %s"),
		OffPath, FVector::Dist(Overhead, Under), Along30 / 1e5, Level30, JellyOff / 1e5, Switched / 1e5, D.ShadowReachCm / 1e5,
		AtDusk.Equals(Pos, 1e-3) ? TEXT("yes") : TEXT("no"), AtNight.Equals(Pos, 1e-3) ? TEXT("yes") : TEXT("no")));
	TestTrue(TEXT("its loop's ground the ground its path flies over"), OffPath < 1.0);
	TestTrue(TEXT("its shadow straight under it with the sun overhead"), FVector::Dist(Overhead, Under) < 1.0);
	TestTrue(TEXT("and down the sun's rays to the ground with it 30 degrees up"), FMath::IsNearlyEqual(Along30, Over / FMath::Tan(UE_DOUBLE_PI / 6.0), 10.0) && FMath::Abs(Level30) < 1.0);
	TestTrue(TEXT("crew standing in it past the reach of the jelly itself have it drawn"), JellyOff > D.ShadowReachCm && Switched < D.ShadowReachCm);
	TestTrue(TEXT("none to speak of with the sun on the horizon or under it"), AtDusk.Equals(Pos, 1e-3) && AtNight.Equals(Pos, 1e-3));
	return true;
}

namespace
{
	/** Bytes appended field by field, for a CRC that hangs on the values alone (no padding between them). */
	struct FGoldenBytes
	{
		TArray<uint8> B;
		template <typename T> void Put(const T& V) { B.Append(reinterpret_cast<const uint8*>(&V), sizeof(T)); }
		void PutVec(const FVector& V) { Put(V.X); Put(V.Y); Put(V.Z); }
		uint32 Crc() const { return FCrc::MemCrc32(B.GetData(), B.Num()); }
	};
	/** A kind's plan as a profile has it (the first of its species). */
	const FFaunaBodyPlan& GoldenPlanOf(const SlonkFauna::FFaunaProfile& F, EFaunaKind K) { return F.PlanOf(K); }
	/** A body plan's fields as they were before the species came (the Earth's walk the same fields). */
	void PutPlan(FGoldenBytes& Out, const FFaunaBodyPlan& P)
	{
		Out.Put(P.Symmetry); Out.Put(P.Segments); Out.Put(P.LimbPairs); Out.Put((uint8)P.Limbs); Out.Put((uint8)P.Wings);
		Out.Put(P.Length); Out.Put(P.Girth); Out.Put(P.Tail); Out.Put(P.Chord); Out.Put(P.GlowOrgans);
	}
	/** A mesh's every vertex, normal, UV, colour and triangle, in order. */
	uint32 GoldenMeshCrc(const UE::Geometry::FDynamicMesh3& M)
	{
		FGoldenBytes Out;
		for (const int32 V : M.VertexIndicesItr()) { Out.Put(V); Out.PutVec((FVector)M.GetVertex(V)); }
		const UE::Geometry::FDynamicMeshNormalOverlay* N = M.Attributes()->PrimaryNormals();
		const UE::Geometry::FDynamicMeshUVOverlay* U = M.Attributes()->PrimaryUV();
		const UE::Geometry::FDynamicMeshColorOverlay* C = M.Attributes()->PrimaryColors();
		for (const int32 E : N->ElementIndicesItr()) { Out.Put(N->GetElement(E)); }
		for (const int32 E : U->ElementIndicesItr()) { Out.Put(U->GetElement(E)); }
		for (const int32 E : C->ElementIndicesItr()) { Out.Put(C->GetElement(E)); }
		for (const int32 T : M.TriangleIndicesItr()) { Out.Put(M.GetTriangle(T)); Out.Put(N->GetTriangle(T)); Out.Put(U->GetTriangle(T)); Out.Put(C->GetTriangle(T)); }
		return Out.Crc();
	}
	/** Every field a group had before the species came, and its members as MembersOf makes them. */
	void PutGroup(FGoldenBytes& Out, const SlonkFauna::FFaunaGroup& G)
	{
		Out.Put(G.Id); Out.Put((uint8)G.Kind); Out.Put((uint8)G.Habitat); Out.Put(G.Seed); Out.PutVec(G.Centre); Out.PutVec(G.East); Out.PutVec(G.North);
		Out.Put(G.RadiusCm); Out.Put(G.Lobe); Out.Put(G.LobePhase); Out.Put(G.MeanderCm); Out.Put(G.Lobes); Out.Put(G.MeanderK); Out.Put(G.BobK);
		Out.Put(G.PeriodSec); Out.Put(G.Phase0); Out.Put(G.Turn); Out.Put(G.CruiseCm); Out.Put(G.BobCm); Out.Put(G.LengthCm); Out.Put(G.SpeedCmS);
		for (const float H : G.Ground) { Out.Put(H); }
		Out.Put(G.Members); Out.Put(G.SpanM); Out.Put(G.SpacingM); Out.Put(G.FlapHz); Out.Put(G.Pigment); Out.Put(G.Glow);
		Out.Put(G.CycleSec); Out.Put(G.WalkCmS); Out.Put(G.TrotCmS); Out.Put(G.WalkA); Out.Put(G.WalkB); Out.Put(G.TrotA); Out.Put(G.TrotB); Out.Put(G.RampSec); Out.Put(G.StartCm);
		for (const float A : G.ArcCm) { Out.Put(A); }
		Out.Put(G.FormationCm); Out.Put(G.HerdRadiusCm); Out.Put(G.GroundHalfCm); Out.Put(G.DryCm); Out.Put(G.ShoalDensity); Out.Put(G.ShoalHalfLengthCm);
		Out.Put(G.ShoalHalfWidthCm); Out.Put(G.ShoalDepthCm); Out.Put(G.WaterCm); Out.Put(G.Ribs); Out.Put(G.HangCm);
		TArray<FTransform> Xf;
		TArray<float> Cu;
		SlonkFauna::MembersOf(G, Xf, Cu);
		for (const FTransform& T : Xf)
		{
			Out.PutVec(T.GetTranslation());
			Out.Put(T.GetRotation().X); Out.Put(T.GetRotation().Y); Out.Put(T.GetRotation().Z); Out.Put(T.GetRotation().W);
			Out.PutVec(T.GetScale3D());
		}
		for (const float F : Cu) { Out.Put(F); }
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaEarthGoldenTest, "Slonk.Fauna.EarthGolden", SLONK_TEST_FLAGS)
bool FSlonkFaunaEarthGoldenTest::RunTest(const FString& Parameters)
{
	// What the Earth's animals were before the alien body plans came, held to the bit: their meshes, the profiles every
	// world rolls at strangeness 0, the flocks, herds and leaping fish of the Earth-like world and their members, a herd
	// stepped through a minute of walking, a startle and its flight and someone walking at it, and the walker branch's
	// float copy over random cases; and the sky-jellies of a strange world, which keep their own plan.
	using namespace UE::Geometry;
	FString Lines;
	auto Note = [&Lines](const TCHAR* What, uint32 Crc) { Lines += FString::Printf(TEXT("%s 0x%08x; "), What, Crc); return Crc; };
	// The meshes, every LOD.
	uint32 MeshCrc[4] = {};
	{
		const FFaunaBodyPlan Plans[4] = { SlonkFauna::EarthPlan(EFaunaKind::Flier), SlonkFauna::EarthPlan(EFaunaKind::Herd), SlonkFauna::EarthPlan(EFaunaKind::Leaper), SlonkFauna::FloaterPlan(3u * 7919u) };
		for (int32 p = 0; p < 4; ++p)
		{
			FGoldenBytes Out;
			for (int32 L = 0; L < SlonkFaunaMeshes::NumLods; ++L)
			{
				FDynamicMesh3 M;
				Out.Put(SlonkFaunaMeshes::BuildLod(Plans[p], L, M));
				Out.Put(GoldenMeshCrc(M));
			}
			Out.Put(Plans[p].Hash());
			MeshCrc[p] = Out.Crc();
		}
	}
	Note(TEXT("bird mesh"), MeshCrc[0]);
	Note(TEXT("walker mesh"), MeshCrc[1]);
	Note(TEXT("fish mesh"), MeshCrc[2]);
	Note(TEXT("jelly mesh"), MeshCrc[3]);
	// The profiles at strangeness 0, every archetype, 24 worlds each: each kind's plan, the look, the air, the jellies' share.
	uint32 ProfileCrc = 0;
	{
		FGoldenBytes Out;
		for (const EPlanetArchetype Kind : FaunaKinds)
		{
			for (uint32 Hash = 1; Hash <= 24; ++Hash)
			{
				const SlonkFauna::FFaunaProfile F = SlonkFauna::Roll(SlonkGenome::Roll(Kind, 0.f, Hash * 7919u), Kind, Hash * 7919u);
				Out.Put(F.Strangeness); Out.Put((uint8)F.Type); Out.Put(F.Hash); Out.Put(F.Seed);
				for (int32 K = 0; K < SlonkFauna::NumKinds; ++K) { PutPlan(Out, GoldenPlanOf(F, (EFaunaKind)K)); }
				Out.Put(F.Look.Glow); Out.Put(F.Look.Firefly); Out.Put(F.Look.Pollen); Out.Put(F.Look.Jelly); Out.Put(F.Look.HueTurnDeg); Out.Put(F.Look.Vivid);
				Out.Put(F.bThickAir); Out.Put(F.FloaterShare);
			}
		}
		ProfileCrc = Note(TEXT("profiles at 0"), Out.Crc());
	}
	// The Earth-like world's flocks, herds and leapers, and a strange world's jellies.
	auto GroupsCrc = [](const TArray<SlonkFauna::FFaunaGroup>& Groups) { FGoldenBytes Out; for (const SlonkFauna::FFaunaGroup& G : Groups) { PutGroup(Out, G); } return Out.Crc(); };
	TArray<SlonkFauna::FFaunaGroup> Flocks, Herds, Leapers, Jellies;
	GroupsOverGlobe(EarthFauna(), 3000, Flocks);
	HerdsOverGlobe(EarthHerds(), 12000, Herds);
	LeaperGroupsOverSeas(EarthSeas(), 30, Leapers);
	SlonkFauna::FFaunaWorld JW;
	const bool bJellyWorld = JellyWorld(0.6f, 0.3f, 0.7f, JW);
	if (bJellyWorld) { KindOverGlobe(JW, EFaunaKind::Floater, 3000, Jellies); }
	const uint32 FlockCrc = Note(TEXT("flocks"), GroupsCrc(Flocks));
	const uint32 HerdCrc = Note(TEXT("herds"), GroupsCrc(Herds));
	const uint32 LeaperCrc = Note(TEXT("leapers"), GroupsCrc(Leapers));
	const uint32 JellyCrc = Note(TEXT("jellies at 0.6"), GroupsCrc(Jellies));
	// A herd stepped as the game steps it: a minute and a half from before its walk, a craft low over it, and someone
	// walking at it.
	uint32 StepCrc = 0;
	{
		FGoldenBytes Out;
		const SlonkFauna::FFaunaWorld W = EarthHerds();
		SlonkFauna::FFaunaGroup G;
		if (TestHerd(W, G))
		{
			const double R = W.P.RadiusCm;
			const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Herd);
			auto Height = [&W](const FVector& Dir) { return FMath::Max(SlonkTerrain::Sample(W.P, Dir, 2000.0).HeightCm, 0.0); };
			Out.Put(SlonkFauna::HerdRiseSec(G, D));
			Out.Put(SlonkFauna::HerdTurnRateOf(G, true));
			SlonkFauna::FHerdState S;
			SlonkFauna::FPanic P;
			const double Start = 3.0 * G.CycleSec + G.WalkA - 20.0;
			SlonkFauna::StartHerd(S, G, Start);
			for (int32 k = 0; k <= 2700; ++k)
			{
				const double Now = Start + k / 30.0;
				if (k == 900)
				{
					SlonkFauna::FFaunaThreat Craft;
					Craft.PosCm = S.FramePos + S.FrameRot.GetAxisY() * 5000.0 + S.FramePos.GetSafeNormal() * 3000.0;
					Craft.AltitudeCm = 3000.0;
					Out.Put(SlonkFauna::StartleHerd(P, S, G, D, R, Craft, Now));
				}
				TArray<FVector> Crew;
				if (k > 2000) { Crew.Add(S.FramePos + S.FrameRot.GetAxisX() * (G.HerdRadiusCm + 4000.0)); }
				const bool bMoving = SlonkFauna::KeepFromCrew(S, G, D, Crew, k == 0 ? 0.0 : 1.0 / 30.0);
				const FVector Ahead = SlonkFauna::HerdFrightOffset(G, D, P, R, Now + 1.0) + S.Avoid + (bMoving ? S.AvoidWay : FVector::ZeroVector);
				float Gd[SlonkFauna::HerdData::Num] = {};
				SlonkFauna::StepHerd(S, G, R, Now, k == 0 ? 0.0 : 1.0 / 30.0, SlonkFauna::HerdFrightOffset(G, D, P, R, Now) + S.Avoid, P.IsLive(Now), bMoving, k % 6 == 0, Height, 0.0, Gd, &Ahead);
				SlonkFauna::HerdFrightData(P, Now, 0.0, Gd);
				Out.B.Append(reinterpret_cast<const uint8*>(Gd), sizeof(Gd));
				Out.PutVec(S.FramePos);
				Out.Put(S.FrameRot.X); Out.Put(S.FrameRot.Y); Out.Put(S.FrameRot.Z); Out.Put(S.FrameRot.W);
			}
		}
		StepCrc = Note(TEXT("a herd stepped"), Out.Crc());
	}
	// The walker branch's float copy over the Earth walker's LOD 0 vertices, random cases as Slonk.Fauna.Walker makes them.
	uint32 MotionCrc = 0;
	{
		FGoldenBytes Out;
		FDynamicMesh3 Lod0;
		SlonkFaunaMeshes::BuildLod(SlonkFauna::EarthPlan(EFaunaKind::Herd), 0, Lod0);
		const TArray<FLinearColor> Vcs = VertexColours(Lod0);
		FRandomStream Rng(4242);
		for (int32 Case = 0; Case < 160; ++Case)
		{
			float Cu[SlonkFauna::Custom::Num] = {};
			float Gd[SlonkFauna::HerdData::Num] = {};
			const float Size = Rng.FRandRange((float)SlonkFauna::Walker::MinSizeM, 2.6f);
			const float Stride = (float)SlonkFauna::Walker::StrideShare * Size * Rng.FRandRange(0.97f, 1.03f);
			Cu[SlonkFauna::Custom::SizeM] = Size;
			Cu[SlonkFauna::Custom::Seed] = Rng.FRand();
			Cu[SlonkFauna::Custom::Phase] = Rng.FRand();
			Cu[SlonkFauna::Custom::RateHz] = Stride;
			Cu[SlonkFauna::Custom::PerKind] = (Rng.FRand() < 0.5f ? -1.f : 1.f) * Rng.FRandRange(0.5f, 2.8f);
			const float T = Rng.FRandRange(0.f, 3600.f);
			for (int32 s = 0; s < SlonkFauna::HerdData::Num; ++s) { Gd[s] = Rng.FRandRange(-1.f, 1.f); }
			Gd[SlonkFauna::HerdData::Heading] = Rng.FRandRange(-3.2f, 3.2f);
			Gd[SlonkFauna::HerdData::SpeedMS] = Case % 4 == 0 ? Rng.FRandRange(4.f, 13.f) : Rng.FRandRange(0.f, 3.4f);
			Gd[SlonkFauna::HerdData::DistM] = Rng.FRandRange(0.f, 2600.f);
			Gd[SlonkFauna::HerdData::GrazeSec] = Rng.FRandRange(0.f, 3600.f);
			Gd[SlonkFauna::HerdData::ClockT0] = FMath::Fmod(T - Rng.FRandRange(0.f, 0.1f) + 3600.f, 3600.f);
			Gd[SlonkFauna::HerdData::Travel] = Case % 3 == 0 ? 0.f : (Case % 3 == 1 ? 1.f : Rng.FRand());
			Gd[SlonkFauna::HerdData::PivotRef] = Gd[SlonkFauna::HerdData::DistM] - (Case % 3 == 0 ? 1000.f : Rng.FRandRange(0.f, 8.f)) + (Case % 5 < 2 ? (float)SlonkFauna::HerdData::PivotBack : 0.f);
			Gd[SlonkFauna::HerdData::GrazeRef] = Gd[SlonkFauna::HerdData::GrazeSec] - Rng.FRandRange(0.f, 60.f);
			Gd[SlonkFauna::HerdData::WalkKap] = Rng.FRandRange(-1.f, 1.f) * (float)SlonkFauna::Walker::MaxWalkKap;
			Gd[SlonkFauna::HerdData::TurnHeading] = SlonkFauna::HerdData::PackTurns(Rng.FRandRange(-3.2f, 3.2f), Rng.FRandRange(-3.2f, 3.2f));
			Gd[SlonkFauna::HerdData::Startled] = Case % 2 == 0 ? 1.f : 0.f;
			Gd[SlonkFauna::HerdData::PanicT0] = FMath::Fmod(T - Rng.FRandRange(0.f, 120.f) + 3600.f, 3600.f);
			Gd[SlonkFauna::HerdData::OnsetT0] = FMath::Fmod(T - Rng.FRandRange(0.f, 121.f) + 3600.f, 3600.f);
			for (int32 m = 0; m < SlonkFauna::HerdData::GroundNodes; ++m) { Gd[SlonkFauna::HerdData::Ground0 + m] = Rng.FRandRange(-1.f, 1.f) * (float)SlonkFauna::HerdData::MaxGroundCm; }
			Gd[SlonkFauna::HerdData::GroundHalfCm] = Rng.FRandRange(1500.f, 4500.f);
			const FVector3f Home(Rng.FRandRange(-3500.f, 3500.f), Rng.FRandRange(-3500.f, 3500.f), 0.f);
			const float Dist = Case % 3 == 0 ? 2000.f : (Case % 3 == 1 ? Size * 100.f * 960.f / 3.5f : 4.0e5f);
			Out.Put(SlonkFauna::WalkerYaw(T, Cu, Gd));
			Out.Put(SlonkFauna::WalkerStrides(T, Cu, Gd));
			Out.Put(SlonkFauna::WalkerSpeedMS(T, Cu, Gd));
			for (const int32 V : Lod0.VertexIndicesItr())
			{
				const FVector3f LP = (FVector3f)Lod0.GetVertex(V);
				Out.Put(SlonkFauna::WalkerMotion(LP, Home + LP * Size, Vcs[V], T, Cu, Gd, Dist, 960.f));
			}
		}
		MotionCrc = Note(TEXT("walker motion"), Out.Crc());
	}
	// The fliers' and the leapers' float copies over the Earth bird's and fish's LOD 0 vertices, random cases as
	// Slonk.Fauna.Meshes and Slonk.Fauna.Fish make them (the alien plans' wings and fish came into the same code).
	uint32 FlierMotionCrc = 0, LeaperMotionCrc = 0;
	{
		FGoldenBytes Out;
		FDynamicMesh3 Lod0;
		SlonkFaunaMeshes::BuildLod(SlonkFauna::EarthPlan(EFaunaKind::Flier), 0, Lod0);
		const TArray<FLinearColor> Vcs = VertexColours(Lod0);
		FRandomStream Rng(5151);
		for (int32 Case = 0; Case < 160; ++Case)
		{
			float Cu[SlonkFauna::Custom::Num] = {};
			float Gd[SlonkFauna::GroupData::Num] = {};
			const float Size = Rng.FRandRange(0.33f, 1.5f);
			Cu[SlonkFauna::Custom::SizeM] = Size;
			Cu[SlonkFauna::Custom::Seed] = Rng.FRand();
			Cu[SlonkFauna::Custom::Phase] = Rng.FRand();
			Cu[SlonkFauna::Custom::RateHz] = FMath::RoundToFloat(Rng.FRandRange(2.4f, 12.f) * 60.f) / 60.f;
			Cu[SlonkFauna::Custom::PerKind] = Rng.FRandRange(-3.2f, 3.2f);
			const FVector Fled = Rng.GetUnitVector();
			Gd[SlonkFauna::GroupData::ScatterDirX] = (float)Fled.X;
			Gd[SlonkFauna::GroupData::ScatterDirY] = (float)Fled.Y;
			Gd[SlonkFauna::GroupData::ScatterDirZ] = (float)Fled.Z;
			Gd[SlonkFauna::GroupData::Bank] = Rng.FRandRange(-0.8f, 0.8f);
			Gd[SlonkFauna::GroupData::Startled] = Case % 2 == 0 ? 1.f : 0.f;
			Gd[SlonkFauna::GroupData::SpreadM] = Rng.FRandRange(0.f, 10.f);
			const float T = Rng.FRandRange(0.f, 3600.f);
			Gd[SlonkFauna::GroupData::ScatterT0] = FMath::Fmod(T - Rng.FRandRange(0.f, 60.f) + 3600.f, 3600.f);
			const float PanicAge = Rng.FRandRange(0.f, 120.f);
			Gd[SlonkFauna::GroupData::PanicT0] = FMath::Fmod(T - PanicAge + 3600.f, 3600.f);
			Gd[SlonkFauna::GroupData::OnsetT0] = FMath::Fmod(T - PanicAge - Rng.FRandRange(0.f, 1.f) + 3600.f, 3600.f);
			const float Dist = Case % 3 == 0 ? 1000.f : (Case % 3 == 1 ? Size * 100.f * 960.f / 1.f : 5.0e5f);
			for (const int32 V : Lod0.VertexIndicesItr())
			{
				const FVector3f LP = (FVector3f)Lod0.GetVertex(V);
				Out.Put(SlonkFauna::FlierMotion(LP, Vcs[V], T, Cu, Gd, Dist, 960.f));
			}
		}
		FlierMotionCrc = Note(TEXT("flier motion"), Out.Crc());
	}
	{
		FGoldenBytes Out;
		FDynamicMesh3 Lod0;
		SlonkFaunaMeshes::BuildLod(SlonkFauna::EarthPlan(EFaunaKind::Leaper), 0, Lod0);
		const TArray<FLinearColor> Vcs = VertexColours(Lod0);
		FRandomStream Rng(6161);
		for (int32 Case = 0; Case < 400; ++Case)
		{
			float Cu[SlonkFauna::Custom::Num] = {};
			float Gd[SlonkFauna::LeaperData::Num] = {};
			const float Size = Case % 5 == 0 ? (float)SlonkFauna::Leaper::MinSizeM : Rng.FRandRange((float)SlonkFauna::Leaper::MinSizeM, (float)SlonkFauna::Leaper::MaxSizeM);
			const int32 Count = Rng.RandRange(1, SlonkFauna::Leaper::MaxPerGroup), Idx = Rng.RandRange(0, Count - 1);
			Cu[SlonkFauna::Custom::SizeM] = Size;
			Cu[SlonkFauna::Custom::Seed] = Rng.FRand();
			Cu[SlonkFauna::Custom::Phase] = Rng.FRand();
			Cu[SlonkFauna::Custom::RateHz] = (float)SlonkFauna::Leaper::Cycles[Rng.RandRange(0, (int32)UE_ARRAY_COUNT(SlonkFauna::Leaper::Cycles) - 1)];
			Cu[SlonkFauna::Custom::PerKind] = (float)(8 * Idx + Count);
			for (int32 k = 0; k < 2 * SlonkFauna::Leaper::MaxPerGroup; ++k) { Gd[SlonkFauna::LeaperData::Surface0 + k] = Rng.FRandRange(-1.f, 1.f) * SlonkFauna::Leaper::MaxSurfaceCm; }
			Gd[SlonkFauna::LeaperData::Diving] = Case % 4 == 0 ? 1.f : 0.f;
			Gd[SlonkFauna::LeaperData::DiveT0] = Rng.FRandRange(0.f, 3600.f);
			Gd[SlonkFauna::LeaperData::DiveLen] = Rng.FRandRange(0.f, 70.f);
			float T = Rng.FRandRange(0.f, 3600.f);
			if (Case % 4 != 3)
			{
				const SlonkFauna::FLeap L = SlonkFauna::LeapOf(Cu, Rng.RandRange(0, 200));
				T = MaterialTime(L.T0 - L.LeadSec + (L.FlightSec + 2.0 * L.LeadSec) * Rng.FRand());
			}
			Out.Put(SlonkFauna::LeaperShown(T, Cu, Gd));
			Out.Put(SlonkFauna::LeaperTurn(T, Cu, Gd));
			const float Dist = Case % 3 == 0 ? 2000.f : (Case % 3 == 1 ? Size * 100.f * 960.f / 1.5f : 4.0e5f);
			for (const int32 V : Lod0.VertexIndicesItr())
			{
				const FVector3f LP = (FVector3f)Lod0.GetVertex(V);
				Out.Put(SlonkFauna::LeaperMotion(LP, Vcs[V], T, Cu, Gd, Dist, 960.f));
			}
		}
		LeaperMotionCrc = Note(TEXT("leaper motion"), Out.Crc());
	}
	AddInfo(FString::Printf(TEXT("the Earth's animals (%d flocks, %d herds, %d leaper groups, %d jellies%s): %s"), Flocks.Num(), Herds.Num(), Leapers.Num(), Jellies.Num(),
		bJellyWorld ? TEXT("") : TEXT(", no jelly world"), *Lines));
	TestTrue(TEXT("groups of every kind to hold"), Flocks.Num() == 424 && Herds.Num() == 410 && Leapers.Num() == 30 && Jellies.Num() == 917);
	// As they were before the species and the alien plans came (this test, run on that code, gave these).
	TestEqual(TEXT("the Earth bird's mesh as it was"), MeshCrc[0], 0x2b5c9454u);
	TestEqual(TEXT("the Earth grazer's mesh as it was"), MeshCrc[1], 0x794bc941u);
	TestEqual(TEXT("the Earth fish's mesh as it was"), MeshCrc[2], 0xc1e84ec9u);
	TestEqual(TEXT("a jelly's mesh as it was"), MeshCrc[3], 0x0c84f68du);
	TestEqual(TEXT("every world's profile at strangeness 0 as it was"), ProfileCrc, 0xae201e53u);
	TestEqual(TEXT("the Earth-like world's flocks as they were"), FlockCrc, 0x987cd155u);
	TestEqual(TEXT("its herds as they were"), HerdCrc, 0x72d681cdu);
	TestEqual(TEXT("its leapers as they were"), LeaperCrc, 0xcd898b04u);
	TestEqual(TEXT("a strange world's jellies as they were"), JellyCrc, 0x2c603a68u);
	TestEqual(TEXT("a herd stepped as it was"), StepCrc, 0x00a33f86u);
	TestEqual(TEXT("the walker branch's copy as it was, for the Earth grazer's rig"), MotionCrc, 0x433b0e77u);
	// The alien wings and fish came into the fliers' part and the leaper branch: their copies for a bird and an Earth fish
	// (WingForm and FishForm 0) as they were before (this test, run on that code, gave these), so the CRC lines of those
	// blocks changed with the Earth's motion as it was.
	TestEqual(TEXT("the fliers' copy as it was, for a bird"), FlierMotionCrc, 0x85281adfu);
	TestEqual(TEXT("the leaper branch's copies as they were, for an Earth fish"), LeaperMotionCrc, 0x097f19fdu);
	return true;
}


namespace
{
	namespace Wk = SlonkFauna::Walker;

	/** An alien grazer's plan of a form (0 a six-legged grazer, 1 an insectoid), every proportion's draw At (0, the low end
	 *  of its range, to 1, the high), wearing what OrnamentDraw gives; and its size against an Earth grazer's. */
	FFaunaBodyPlan AlienHerdPlan(int32 Form, float At, float OrnamentDraw, float* OutScale = nullptr)
	{
		float Shape[SlonkFauna::SpeciesShapeDraws];
		for (int32 j = 0; j < SlonkFauna::SpeciesShapeDraws; ++j) { Shape[j] = At; }
		Shape[5] = OrnamentDraw;
		float Scale = 1.f;
		const FFaunaBodyPlan P = SlonkFauna::AlienPlan(EFaunaKind::Herd, Form, Shape, Scale);
		if (OutScale) { *OutScale = Scale; }
		return P;
	}
	/** Every alien grazer plan the tests run over: each form at the low end, the middle and the high end of its ranges,
	 *  each wearing every ornament it can; and each with its size against an Earth grazer's. */
	struct FAlienPlan { FFaunaBodyPlan Plan; float Scale = 1.f; };
	TArray<FAlienPlan> AlienHerdPlans()
	{
		TArray<FAlienPlan> Out;
		for (int32 Form = 0; Form < SlonkFauna::AlienForms(EFaunaKind::Herd); ++Form)
		{
			for (const float At : { 0.f, 0.5f, 1.f })
			{
				for (const float Orn : { 0.1f, 0.4f, 0.6f, 0.9f })
				{
					FAlienPlan A;
					A.Plan = AlienHerdPlan(Form, At, Orn, &A.Scale);
					if (!Out.ContainsByPredicate([&A](const FAlienPlan& O) { return O.Plan == A.Plan; })) { Out.Add(A); }
				}
			}
		}
		return Out;
	}
	/** The Earth-like world, its herds all of one species of a plan at a size against an Earth grazer's. */
	SlonkFauna::FFaunaWorld AlienHerdWorld(const FFaunaBodyPlan& Plan, float Scale)
	{
		SlonkFauna::FFaunaWorld W = EarthHerds();
		SlonkFauna::FFaunaKindSpecies& S = W.Profile.Species[(int32)EFaunaKind::Herd];
		S = SlonkFauna::FFaunaKindSpecies();
		S.List[0].Plan = Plan;
		S.List[0].SizeScale = Scale;
		return W;
	}
	/** A strange world's profile on the Earth-like world's ground, its herds alone: the first hash (7919 apart) whose herds
	 *  at strangeness S have at least MinSpecies species, at least one of each alien form among them when bBothForms. */
	bool StrangeHerds(float S, int32 MinSpecies, bool bBothForms, SlonkFauna::FFaunaWorld& Out, uint32* OutHash = nullptr)
	{
		for (uint32 k = 1; k < 5000; ++k)
		{
			const uint32 Hash = k * 7919u;
			const SlonkFauna::FFaunaProfile F = SlonkFauna::Roll(SlonkGenome::Roll(EPlanetArchetype::Terrestrial, S, Hash), EPlanetArchetype::Terrestrial, Hash);
			const SlonkFauna::FFaunaKindSpecies& Sp = F.Species[(int32)EFaunaKind::Herd];
			if (Sp.Num < MinSpecies) { continue; }
			bool bHexa = false, bInsect = false;
			for (int32 i = 0; i < Sp.Num; ++i)
			{
				const Wk::EForm Form = Wk::ShapeOf(Sp.List[i].Plan).Form;
				bHexa |= Form == Wk::EForm::Hexapod;
				bInsect |= Form == Wk::EForm::Insectoid;
			}
			if (bBothForms && !(bHexa && bInsect)) { continue; }
			Out = EarthHerds();
			Out.Profile = F;
			if (OutHash) { *OutHash = Hash; }
			return true;
		}
		return false;
	}
	/** An alien herd to test with on a world: of its herds over the globe, the first of at least 24 walkers (else the
	 *  first), or with bLargest the one of the longest walkers. */
	bool TestAlienHerd(const SlonkFauna::FFaunaWorld& W, bool bLargest, SlonkFauna::FFaunaGroup& Out)
	{
		TArray<SlonkFauna::FFaunaGroup> Herds;
		HerdsOverGlobe(W, 8000, Herds);
		if (Herds.Num() == 0) { return false; }
		if (bLargest)
		{
			const SlonkFauna::FFaunaGroup* Best = &Herds[0];
			for (const SlonkFauna::FFaunaGroup& G : Herds) { if (G.SpanM > Best->SpanM) { Best = &G; } }
			Out = *Best;
			return true;
		}
		for (const SlonkFauna::FFaunaGroup& G : Herds) { if (G.Members >= 24) { Out = G; return true; } }
		Out = Herds[0];
		return true;
	}
	const TCHAR* FormName(Wk::EForm F)
	{
		switch (F)
		{
		case Wk::EForm::Grazer: return TEXT("Earth grazer");
		case Wk::EForm::Hexapod: return TEXT("six-legged grazer");
		case Wk::EForm::Insectoid: return TEXT("insectoid");
		default: return TEXT("none");
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaEarthAtZeroTest, "Slonk.Fauna.EarthAtZero", SLONK_TEST_FLAGS)
bool FSlonkFaunaEarthAtZeroTest::RunTest(const FString& Parameters)
{
	// At strangeness 0 every world's every kind is one species, its Earth plan, holding all of its kind's cells at the
	// kind's own size: two feathered wings for its fliers, four legs for its herds, fins for its fish; and no jellies. So too
	// to 0.15, where no species departs yet. Every archetype, 200 worlds each. On the Earth-like world every group of every
	// kind is of that one species and its plan. At strangeness 1, of 300 seeds, most depart in each kind with alien forms:
	// their fliers, their herds and their fish. (Slonk.Fauna.EarthGolden holds the Earth's meshes and animals to the bit.)
	int32 Worlds = 0, Off = 0, Jellies = 0, NotTwoWings = 0, NotFourLegs = 0, NotFins = 0;
	for (const EPlanetArchetype Kind : FaunaKinds)
	{
		for (uint32 Hash = 1; Hash <= 200; ++Hash)
		{
			for (const float S : { 0.f, 0.1f, 0.15f })
			{
				const SlonkFauna::FFaunaProfile F = SlonkFauna::Roll(SlonkGenome::Roll(Kind, S, Hash * 7919u), Kind, Hash * 7919u);
				++Worlds;
				for (int32 K = 0; K < SlonkFauna::NumKinds; ++K)
				{
					const SlonkFauna::FFaunaKindSpecies& Sp = F.Species[K];
					Off += Sp.Num == 1 && Sp.List[0].Plan == SlonkFauna::EarthPlan((EFaunaKind)K) && Sp.List[0].Share == 1.f && Sp.List[0].SizeScale == 1.f ? 0 : 1;
				}
				Jellies += F.FloaterShare > 0.f ? 1 : 0;
				NotTwoWings += F.PlanOf(EFaunaKind::Flier).Wings == EFaunaWings::Feathered ? 0 : 1;
				const Wk::FShape Herd = Wk::ShapeOf(F.PlanOf(EFaunaKind::Herd));
				NotFourLegs += Herd.Form == Wk::EForm::Grazer && Herd.NumLegs == 4 ? 0 : 1;
				NotFins += F.PlanOf(EFaunaKind::Leaper).Limbs == EFaunaLimbs::Fins ? 0 : 1;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d worlds at strangeness 0, 0.1 and 0.15: %d kinds off their one Earth species, %d with jellies, %d fliers without two feathered wings, %d herds not on four legs, %d fish without fins; a species departs with chance %.3f, %.3f and %.3f there"),
		Worlds, Off, Jellies, NotTwoWings, NotFourLegs, NotFins, SlonkFauna::DepartChance(0.f), SlonkFauna::DepartChance(0.1f), SlonkFauna::DepartChance(0.15f)));
	TestEqual(TEXT("every kind one species, its Earth plan, all of its cells, its own size"), Off, 0);
	TestEqual(TEXT("no jellies"), Jellies, 0);
	TestEqual(TEXT("two feathered wings, four legs, fins"), NotTwoWings + NotFourLegs + NotFins, 0);
	TestTrue(TEXT("no species departs to 0.15"), SlonkFauna::DepartChance(0.f) == 0.f && SlonkFauna::DepartChance(0.15f) == 0.f && SlonkFauna::DepartChance(0.2f) > 0.f);
	// The Earth-like world's groups, every kind: species 0, its Earth plan, the Earth grazer's rig for its herds.
	TArray<SlonkFauna::FFaunaGroup> Flocks, Herds, Leapers;
	GroupsOverGlobe(EarthFauna(), 1500, Flocks);
	HerdsOverGlobe(EarthHerds(), 6000, Herds);
	LeaperGroupsOverSeas(EarthSeas(), 20, Leapers);
	int32 Groups = 0, GroupsOff = 0;
	for (const TArray<SlonkFauna::FFaunaGroup>* Set : { &Flocks, &Herds, &Leapers })
	{
		for (const SlonkFauna::FFaunaGroup& G : *Set)
		{
			++Groups;
			GroupsOff += G.Species == 0 && G.Plan == SlonkFauna::EarthPlan(G.Kind) && (G.Kind != EFaunaKind::Herd || Wk::ShapeOf(G.Plan).Rig == Wk::FRig()) ? 0 : 1;
		}
	}
	AddInfo(FString::Printf(TEXT("the Earth-like world: %d flocks, %d herds and %d leaper groups, %d off their Earth species"), Flocks.Num(), Herds.Num(), Leapers.Num(), GroupsOff));
	TestTrue(TEXT("groups of each kind"), Flocks.Num() > 20 && Herds.Num() > 20 && Leapers.Num() > 5);
	TestEqual(TEXT("every group its Earth species"), GroupsOff, 0);
	// And at strangeness 1 most seeds depart, in every kind with alien forms: their fliers, their herds and their fish.
	int32 Departed[SlonkFauna::NumKinds] = {}, Seeds = 0;
	for (uint32 Hash = 1; Hash <= 300; ++Hash)
	{
		const SlonkFauna::FFaunaProfile F = SlonkFauna::Roll(SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 1.f, Hash * 7919u), EPlanetArchetype::Terrestrial, Hash * 7919u);
		++Seeds;
		for (const EFaunaKind K : { EFaunaKind::Flier, EFaunaKind::Herd, EFaunaKind::Leaper })
		{
			bool bAlien = false;
			for (int32 i = 0; i < F.Species[(int32)K].Num; ++i) { bAlien |= !SlonkFauna::IsEarthPlan(K, F.Species[(int32)K].List[i].Plan); }
			Departed[(int32)K] += bAlien ? 1 : 0;
		}
	}
	AddInfo(FString::Printf(TEXT("%d seeds at strangeness 1: %d departed in their fliers, %d in their herds, %d in their fish"), Seeds, Departed[(int32)EFaunaKind::Flier], Departed[(int32)EFaunaKind::Herd], Departed[(int32)EFaunaKind::Leaper]));
	TestTrue(TEXT("most seeds depart at 1, in their fliers, their herds and their fish"), Departed[(int32)EFaunaKind::Flier] > Seeds * 9 / 10 && Departed[(int32)EFaunaKind::Herd] > Seeds * 9 / 10
		&& Departed[(int32)EFaunaKind::Leaper] > Seeds * 9 / 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaDepartureTest, "Slonk.Fauna.Departure", SLONK_TEST_FLAGS)
bool FSlonkFaunaDepartureTest::RunTest(const FString& Parameters)
{
	// A world's animals depart from the Earth's plans smoothly as it grows strange: of 600 worlds at each tenth of
	// strangeness, the share whose herds have a species of another plan rises with it (none to 0.1, few at 0.2 and 0.3,
	// nearly all at 1), and the share of the cells those species hold with it; a world that has departed has departed at
	// every strangeness past that, keeping every alien plan it had (the same proportions, the same form). Both forms come,
	// in about even numbers. At 1, most worlds have departed in every kind that has forms to depart to (the herds, the
	// fliers and the fish), each of their forms coming; their fliers and their fish, too, rise with it and keep every alien
	// plan they had. Of more than one species, each holds a fair share of its kind's cells. The profile's
	// other draws never move: its seed, its look and its jellies are those its stream gave before the species came, at
	// every strangeness.
	constexpr int32 N = 600;
	double WorldShare[11] = {}, CellShare[11] = {}, SpeciesMean[11] = {};
	int32 Lost = 0, Moved = 0, Hexa = 0, Insect = 0, EveryKind = 0, Skewed = 0, Several = 0;
	TArray<TArray<FFaunaBodyPlan>> Was;
	Was.SetNum(N);
	// The fliers' (0) and the fish's (1): each world's alien plans at the last tenth, the share of worlds departing in them
	// at each tenth, and their forms at 1.
	TArray<TArray<FFaunaBodyPlan>> WasOther[2];
	WasOther[0].SetNum(N);
	WasOther[1].SetNum(N);
	double OtherShare[2][11] = {};
	int32 OtherForms[2][3] = {};
	for (int32 t = 0; t <= 10; ++t)
	{
		const float S = t / 10.f;
		for (int32 h = 0; h < N; ++h)
		{
			const uint32 Hash = (uint32)(h + 1) * 7919u;
			const FWorldGenome G = SlonkGenome::Roll(EPlanetArchetype::Terrestrial, S, Hash);
			const SlonkFauna::FFaunaProfile F = SlonkFauna::Roll(G, EPlanetArchetype::Terrestrial, Hash);
			// The profile's own stream, drawn as it was: its seed, a draw a kind, the jellies' share from the floaters' draw.
			FRandomStream R((int32)(Hash * 0x9E3779B1u + 0x27d4eb2fu));
			const uint32 Seed = (uint32)R.GetUnsignedInt();
			float Draws[SlonkFauna::NumKinds];
			for (int32 K = 0; K < SlonkFauna::NumKinds; ++K) { Draws[K] = R.FRand(); }
			const SlonkFauna::FFaunaLook L = SlonkFauna::LookOf(G);
			Moved += F.Seed == Seed && F.FloaterShare == SlonkFauna::FloaterShareOf(G, Draws[(int32)EFaunaKind::Floater]) && F.Look.Glow == L.Glow && F.Look.HueTurnDeg == L.HueTurnDeg
				&& F.Look.Vivid == L.Vivid ? 0 : 1;
			const SlonkFauna::FFaunaKindSpecies& Hs = F.Species[(int32)EFaunaKind::Herd];
			TArray<FFaunaBodyPlan> Alien;
			double Share = 0.0;
			float Least = 1.f;
			for (int32 i = 0; i < Hs.Num; ++i)
			{
				Least = FMath::Min(Least, Hs.List[i].Share);
				if (SlonkFauna::IsEarthPlan(EFaunaKind::Herd, Hs.List[i].Plan)) { continue; }
				Alien.Add(Hs.List[i].Plan);
				Share += Hs.List[i].Share;
				if (t == 10)
				{
					Hexa += Wk::ShapeOf(Hs.List[i].Plan).Form == Wk::EForm::Hexapod ? 1 : 0;
					Insect += Wk::ShapeOf(Hs.List[i].Plan).Form == Wk::EForm::Insectoid ? 1 : 0;
				}
			}
			if (Hs.Num > 1)
			{
				++Several;
				Skewed += Least >= 0.4f / (0.4f + 2.f * 1.f) - 1e-4f ? 0 : 1;
			}
			WorldShare[t] += Alien.Num() > 0 ? 1.0 / N : 0.0;
			CellShare[t] += Share / N;
			SpeciesMean[t] += (double)Hs.Num / N;
			// Every alien plan it had at the last tenth it has still.
			for (const FFaunaBodyPlan& P : Was[h]) { Lost += Alien.Contains(P) ? 0 : 1; }
			Was[h] = Alien;
			for (int32 k = 0; k < 2; ++k)
			{
				const EFaunaKind Kk = k == 0 ? EFaunaKind::Flier : EFaunaKind::Leaper;
				const SlonkFauna::FFaunaKindSpecies& Ks = F.Species[(int32)Kk];
				TArray<FFaunaBodyPlan> Others;
				for (int32 i = 0; i < Ks.Num; ++i)
				{
					if (SlonkFauna::IsEarthPlan(Kk, Ks.List[i].Plan)) { continue; }
					Others.Add(Ks.List[i].Plan);
					if (t == 10) { ++OtherForms[k][FMath::Clamp(k == 0 ? SlonkFauna::Wings::FormOf(Ks.List[i].Plan) : SlonkFauna::FishForm::FormOf(Ks.List[i].Plan), 0, 2)]; }
				}
				for (const FFaunaBodyPlan& P : WasOther[k][h]) { Lost += Others.Contains(P) ? 0 : 1; }
				WasOther[k][h] = Others;
				OtherShare[k][t] += Others.Num() > 0 ? 1.0 / N : 0.0;
			}
			if (t == 10)
			{
				bool bEvery = true;
				for (int32 K = 0; K < SlonkFauna::NumKinds; ++K)
				{
					if (SlonkFauna::AlienForms((EFaunaKind)K) <= 0) { continue; }
					bool bAlien = false;
					for (int32 i = 0; i < F.Species[K].Num; ++i) { bAlien |= !SlonkFauna::IsEarthPlan((EFaunaKind)K, F.Species[K].List[i].Plan); }
					bEvery &= bAlien;
				}
				EveryKind += bEvery ? 1 : 0;
			}
		}
	}
	FString Table;
	for (int32 t = 0; t <= 10; ++t) { Table += FString::Printf(TEXT("%s%.1f: %.1f%% (%.1f%% of the cells, %.2f species)"), t == 0 ? TEXT("") : TEXT("; "), t / 10.0, 100.0 * WorldShare[t], 100.0 * CellShare[t], SpeciesMean[t]); }
	AddInfo(FString::Printf(TEXT("of %d worlds, those whose herds depart from the Earth's plan, by strangeness: %s"), N, *Table));
	AddInfo(FString::Printf(TEXT("at 1: %d six-legged grazer species and %d insectoid ones; %.1f%% of the worlds departed in every kind with forms to depart to; %d worlds lost an alien plan as they grew stranger; %d profiles' other draws moved; %d worlds of several herd species, %d with one holding less than its least share"),
		Hexa, Insect, 100.0 * EveryKind / N, Lost, Moved, Several, Skewed));
	FString Others;
	for (int32 t = 0; t <= 10; t += 2) { Others += FString::Printf(TEXT("%s%.1f: %.1f%% and %.1f%%"), t == 0 ? TEXT("") : TEXT("; "), t / 10.0, 100.0 * OtherShare[0][t], 100.0 * OtherShare[1][t]); }
	AddInfo(FString::Printf(TEXT("those departing in their fliers and in their fish, by strangeness: %s; at 1, %d four-winged and %d membrane-winged flier species, %d eel and %d spinefin species"), *Others,
		OtherForms[0][SlonkFauna::Wings::Four], OtherForms[0][SlonkFauna::Wings::Membrane], OtherForms[1][SlonkFauna::FishForm::Eel], OtherForms[1][SlonkFauna::FishForm::Spinefin]));
	bool bRising = true;
	for (int32 t = 1; t <= 10; ++t) { bRising &= WorldShare[t] >= WorldShare[t - 1] && CellShare[t] >= CellShare[t - 1] - 1e-9; }
	TestTrue(TEXT("none departs to 0.1"), WorldShare[0] == 0.0 && WorldShare[1] == 0.0);
	TestTrue(TEXT("few at 0.2 and 0.3"), WorldShare[2] > 0.0 && WorldShare[2] < 0.04 && WorldShare[3] < 0.15);
	TestTrue(TEXT("about half at 0.5, nearly all at 1"), WorldShare[5] > 0.35 && WorldShare[5] < 0.65 && WorldShare[10] > 0.95);
	TestTrue(TEXT("rising smoothly with strangeness"), bRising);
	TestEqual(TEXT("a world grown stranger keeps every alien plan it had"), Lost, 0);
	TestTrue(TEXT("both forms, about evenly"), Hexa > 200 && Insect > 200 && FMath::Abs(Hexa - Insect) < (Hexa + Insect) / 4);
	bool bOthersRising = true;
	for (int32 k = 0; k < 2; ++k) { for (int32 t = 1; t <= 10; ++t) { bOthersRising &= OtherShare[k][t] >= OtherShare[k][t - 1]; } }
	TestTrue(TEXT("the fliers and the fish depart as the herds do, none to 0.1, nearly all at 1, rising"), bOthersRising && OtherShare[0][1] == 0.0 && OtherShare[1][1] == 0.0 && OtherShare[0][10] > 0.95 && OtherShare[1][10] > 0.95);
	TestTrue(TEXT("both forms of each, about evenly"), OtherForms[0][1] > 200 && OtherForms[0][2] > 200 && OtherForms[1][1] > 200 && OtherForms[1][2] > 200
		&& FMath::Abs(OtherForms[0][1] - OtherForms[0][2]) < (OtherForms[0][1] + OtherForms[0][2]) / 4 && FMath::Abs(OtherForms[1][1] - OtherForms[1][2]) < (OtherForms[1][1] + OtherForms[1][2]) / 4);
	TestTrue(TEXT("most worlds at 1 departed in every kind with forms to depart to"), EveryKind > N * 9 / 10);
	TestTrue(TEXT("of several species, each a fair share"), Several > 100 && Skewed == 0);
	TestEqual(TEXT("the profile's other draws as its stream gave them"), Moved, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaSpeciesStayPutTest, "Slonk.Fauna.SpeciesStayPut", SLONK_TEST_FLAGS)
bool FSlonkFaunaSpeciesStayPutTest::RunTest(const FString& Parameters)
{
	// A cell's species is its own: on a strange world of three herd species of both forms, the same whichever order the
	// cells are read in, the group in it of that species (its plan the species'); over the world's cells each species holds
	// its share of them; gone 30 km off and come back, the same herds of the same species are about. The debug's search
	// for a species finds the nearest group of it. A world makes a mesh for each species, none for each herd: at most
	// MaxSpecies a kind. The jellies and the motes keep one species each, the jellies' a plan of their own (the fliers and
	// the fish have forms of their own).
	SlonkFauna::FFaunaWorld W;
	uint32 Hash = 0;
	if (!StrangeHerds(0.9f, 3, true, W, &Hash)) { AddError(TEXT("no world of three herd species of both forms")); return false; }
	const SlonkFauna::FFaunaKindSpecies& Sp = W.Profile.Species[(int32)EFaunaKind::Herd];
	const double R = W.P.RadiusCm;
	// The shares over the world's cells.
	int32 Counts[SlonkFauna::MaxSpecies] = {}, Cells = 0;
	{
		TSet<uint64> Seen;
		for (int32 i = 0; i < 40000; ++i)
		{
			const SlonkFauna::FFaunaCell C = SlonkFauna::CellOf(W.P, EFaunaKind::Herd, Spread(i, 40000));
			bool bAlready = false;
			Seen.Add(C.Id(), &bAlready);
			if (bAlready) { continue; }
			++Cells;
			++Counts[FMath::Clamp(SlonkFauna::SpeciesInCell(W, C), 0, SlonkFauna::MaxSpecies - 1)];
		}
	}
	double WorstShare = 0.0;
	FString Shares;
	for (int32 i = 0; i < Sp.Num; ++i)
	{
		const double Got = (double)Counts[i] / FMath::Max(Cells, 1);
		WorstShare = FMath::Max(WorstShare, FMath::Abs(Got - Sp.List[i].Share));
		Shares += FString::Printf(TEXT("%s#%d %s %.3f of the cells (its share %.3f)"), i == 0 ? TEXT("") : TEXT(", "), i, FormName(Wk::ShapeOf(Sp.List[i].Plan).Form), Got, Sp.List[i].Share);
	}
	// The herds of the world, each of its cell's species and plan; the meshes they make.
	TArray<SlonkFauna::FFaunaGroup> Herds;
	HerdsOverGlobe(W, 12000, Herds);
	int32 OffSpecies = 0, BySpecies[SlonkFauna::MaxSpecies] = {};
	TSet<uint32> PlanHashes;
	TSet<UStaticMesh*> Meshes;
	for (const SlonkFauna::FFaunaGroup& G : Herds)
	{
		const SlonkFauna::FFaunaCell C = SlonkFauna::CellOf(W.P, EFaunaKind::Herd, G.Centre);
		OffSpecies += G.Species == SlonkFauna::SpeciesInCell(W, C) && G.Plan == Sp[G.Species].Plan ? 0 : 1;
		++BySpecies[FMath::Clamp(G.Species, 0, SlonkFauna::MaxSpecies - 1)];
		PlanHashes.Add(G.Plan.Hash());
		Meshes.Add(SlonkFaunaMeshes::GetMesh(G.Plan, G.Kind));
	}
	// The cells round a herd read forward, backward and shuffled.
	TArray<SlonkFauna::FFaunaCell> Near;
	SlonkFauna::CellsNear(W.P, EFaunaKind::Herd, Herds.Num() > 0 ? Herds[0].Centre : FVector::ZAxisVector, 1.2e6, Near);
	TArray<int32> Order;
	for (int32 i = 0; i < Near.Num(); ++i) { Order.Add(i); }
	TArray<TOptional<SlonkFauna::FFaunaGroup>> Ahead, Back, Mixed;
	TArray<int32> SpAhead, SpBack, SpMixed;
	Ahead.SetNum(Near.Num()); Back.SetNum(Near.Num()); Mixed.SetNum(Near.Num());
	SpAhead.SetNum(Near.Num()); SpBack.SetNum(Near.Num()); SpMixed.SetNum(Near.Num());
	auto Read = [&](int32 i, TArray<TOptional<SlonkFauna::FFaunaGroup>>& G, TArray<int32>& Spc)
	{
		Spc[i] = SlonkFauna::SpeciesInCell(W, Near[i]);
		SlonkFauna::FFaunaGroup One;
		if (SlonkFauna::GroupInCell(W, Near[i], One)) { G[i] = One; }
	};
	for (int32 i = 0; i < Near.Num(); ++i) { Read(i, Ahead, SpAhead); }
	for (int32 i = Near.Num() - 1; i >= 0; --i) { Read(i, Back, SpBack); }
	FRandomStream Rng(91);
	for (int32 i = Order.Num() - 1; i > 0; --i) { Order.Swap(i, Rng.RandRange(0, i)); }
	for (const int32 i : Order) { Read(i, Mixed, SpMixed); }
	int32 Mismatch = 0, Held = 0;
	auto Same = [](const TOptional<SlonkFauna::FFaunaGroup>& A, const TOptional<SlonkFauna::FFaunaGroup>& B) { return A.IsSet() == B.IsSet() && (!A.IsSet() || A.GetValue() == B.GetValue()); };
	for (int32 i = 0; i < Near.Num(); ++i)
	{
		Held += Ahead[i].IsSet() ? 1 : 0;
		Mismatch += SpAhead[i] == SpBack[i] && SpAhead[i] == SpMixed[i] && Same(Ahead[i], Back[i]) && Same(Ahead[i], Mixed[i]) && (!Ahead[i].IsSet() || Ahead[i]->Species == SpAhead[i]) ? 0 : 1;
	}
	// About A, about B 30 km off, back at A: the same herds, each of the same species and where one never left would be.
	const FVector CamA = CameraOver(W, Herds.Num() > 0 ? Herds[0].Centre : FVector::ZAxisVector);
	const FVector CamB = CameraOver(W, AlongGround(CamA.GetSafeNormal(), 3.0e6, R, 2.0));
	SlonkFauna::FFaunaField F, Stayed;
	F.SetWorld(W);
	Stayed.SetWorld(W);
	TArray<uint64> A1, B1, A2, Kept;
	ScanAndSelect(F, CamA, A1);
	ScanAndSelect(Stayed, CamA, Kept);
	ScanAndSelect(F, CamB, B1);
	ScanAndSelect(F, CamA, A2);
	int32 Changed = 0;
	for (const TPair<uint64, SlonkFauna::FFaunaField::FGroupRef>& A : F.GetActive())
	{
		const SlonkFauna::FFaunaField::FGroupRef* Never = Stayed.GetActive().Find(A.Key);
		Changed += Never && **Never == *A.Value && (*Never)->Species == A.Value->Species ? 0 : 1;
	}
	// The debug's search for each species, from a herd's middle 25 km off: one of it, and the nearest there is.
	int32 SearchOff = 0;
	FString Searches;
	const FVector From = AlongGround(CamA.GetSafeNormal(), 2.5e6, R, 0.7);
	for (int32 s = 0; s < Sp.Num; ++s)
	{
		SlonkFauna::FFaunaGroup Found;
		SlonkFauna::FGroupSearch Search;
		const bool bFound = SlonkFauna::FindGroupNear(W, EFaunaKind::Herd, From, 4.0e7, 120000, Found, &Search, s);
		const double At = bFound ? FVector::Dist(Found.Centre, From) * R : -1.0;
		double Nearest = 1.0e12;
		if (bFound)
		{
			TArray<SlonkFauna::FFaunaCell> Round;
			SlonkFauna::CellsNear(W.P, EFaunaKind::Herd, From, At + 4.0e5, Round);
			for (const SlonkFauna::FFaunaCell& C : Round)
			{
				SlonkFauna::FFaunaGroup G;
				if (SlonkFauna::GroupInCell(W, C, G) && G.Species == s) { Nearest = FMath::Min(Nearest, FVector::Dist(G.Centre, From) * R); }
			}
		}
		SearchOff += bFound && Found.Species == s && FMath::IsNearlyEqual(At, Nearest, 1.0) ? 0 : 1;
		Searches += FString::Printf(TEXT("%s#%d %.1f km off (%d cells)"), s == 0 ? TEXT("") : TEXT(", "), s, At / 1e5, Search.Cells);
	}
	// The jellies' and the motes' one species on the strange world, the jellies' its own plan (the fliers and the fish have
	// forms of their own: Slonk.Fauna.AlienFlocks and Slonk.Fauna.AlienFish).
	int32 OthersOff = 0;
	for (const EFaunaKind K : { EFaunaKind::Floater, EFaunaKind::Mote })
	{
		OthersOff += W.Profile.Species[(int32)K].Num == 1 ? 0 : 1;
	}
	if (W.Profile.FloaterShare > 0.f) { OthersOff += W.Profile.PlanOf(EFaunaKind::Floater) == SlonkFauna::FloaterPlan(Hash) ? 0 : 1; }
	AddInfo(FString::Printf(TEXT("a world of hash %u at strangeness 0.9, %d herd species: over %d cells %s (%.3f off at worst); %d herds (%d, %d and %d of each), %d off their cell's species, %d plans and %d meshes among them"),
		Hash, Sp.Num, Cells, *Shares, WorstShare, Herds.Num(), BySpecies[0], BySpecies[1], BySpecies[2], OffSpecies, PlanHashes.Num(), Meshes.Num()));
	AddInfo(FString::Printf(TEXT("%d cells round a herd (%d holding one), %d read otherwise in another order; about A %d herds, back from B %d, %d changed; the search for each species: %s, %d off; %d other kinds of more than one species"),
		Near.Num(), Held, Mismatch, A1.Num(), A2.Num(), Changed, *Searches, SearchOff, OthersOff));
	TestTrue(TEXT("each species holds its share of the cells"), Cells > 20000 && WorstShare < 0.02);
	TestEqual(TEXT("every herd of its cell's species and that species' plan"), OffSpecies, 0);
	TestTrue(TEXT("herds of every species"), BySpecies[0] > 10 && BySpecies[1] > 10 && BySpecies[2] > 10);
	TestTrue(TEXT("a mesh for each species, none for each herd"), PlanHashes.Num() == Sp.Num && Meshes.Num() == Sp.Num && !Meshes.Contains(nullptr));
	TestEqual(TEXT("a cell's species and group the same whatever order the cells are read in"), Mismatch, 0);
	TestTrue(TEXT("cells holding herds"), Held > 5);
	TestTrue(TEXT("back at A, the same herds, each of the same species"), A1.Num() > 0 && A2 == A1 && Kept == A1 && Changed == 0);
	TestEqual(TEXT("the search for a species finds the nearest of it"), SearchOff, 0);
	TestEqual(TEXT("one species of every other kind, the jellies' their own plan"), OthersOff, 0);
	return true;
}

namespace
{
	/** Which of a walker's legs a vertex of its mesh belongs to: the one whose hip, knee and foot it lies nearest the line
	 *  through (and where along it: 0 the hip, 1 the foot, by the leg's length). */
	int32 LegOfVertex(const Wk::FShape& S, const FVector& P, double& OutAlong)
	{
		int32 Best = -1;
		double BestD = TNumericLimits<double>::Max();
		OutAlong = 0.0;
		for (int32 i = 0; i < S.NumLegs; ++i)
		{
			const FVector Pts[3] = { (FVector)S.Legs[i].Hip, (FVector)S.Legs[i].Knee, (FVector)S.Legs[i].Foot };
			const double L0 = FVector::Dist(Pts[0], Pts[1]), L1 = FVector::Dist(Pts[1], Pts[2]);
			for (int32 sgm = 0; sgm < 2; ++sgm)
			{
				const FVector A = Pts[sgm], B = Pts[sgm + 1];
				const FVector Ab = B - A;
				const double T = FMath::Clamp(FVector::DotProduct(P - A, Ab) / FMath::Max(Ab.SizeSquared(), 1e-9), 0.0, 1.0);
				const double D = FVector::Dist(P, A + Ab * T);
				if (D < BestD) { BestD = D; Best = i; OutAlong = (sgm == 0 ? T * L0 : L0 + T * L1) / FMath::Max(L0 + L1, 1e-6); }
			}
		}
		return Best;
	}
	/** A mesh's triangles laid on a plane and filled on a grid of cells Cell apart (the plane's two axes A and B, over the
	 *  ranges given): which cells it covers, for its silhouette seen along the plane's normal. */
	TArray<uint8> Silhouette(const UE::Geometry::FDynamicMesh3& M, const FVector& A, const FVector& B, double Lo, double Hi, double Cell, int32& OutN)
	{
		const int32 N = FMath::CeilToInt32((Hi - Lo) / Cell);
		OutN = N;
		TArray<uint8> Cover;
		Cover.Init(0, N * N);
		for (const int32 T : M.TriangleIndicesItr())
		{
			const UE::Geometry::FIndex3i Tri = M.GetTriangle(T);
			FVector2D S[3];
			for (int32 k = 0; k < 3; ++k) { const FVector P = (FVector)M.GetVertex(Tri[k]); S[k] = FVector2D(FVector::DotProduct(P, A), FVector::DotProduct(P, B)); }
			const double Area2 = (S[1].X - S[0].X) * (S[2].Y - S[0].Y) - (S[2].X - S[0].X) * (S[1].Y - S[0].Y);
			if (FMath::Abs(Area2) < 1e-9) { continue; }
			const int32 X0 = FMath::Clamp(FMath::FloorToInt32((FMath::Min3(S[0].X, S[1].X, S[2].X) - Lo) / Cell), 0, N - 1), X1 = FMath::Clamp(FMath::FloorToInt32((FMath::Max3(S[0].X, S[1].X, S[2].X) - Lo) / Cell), 0, N - 1);
			const int32 Y0 = FMath::Clamp(FMath::FloorToInt32((FMath::Min3(S[0].Y, S[1].Y, S[2].Y) - Lo) / Cell), 0, N - 1), Y1 = FMath::Clamp(FMath::FloorToInt32((FMath::Max3(S[0].Y, S[1].Y, S[2].Y) - Lo) / Cell), 0, N - 1);
			for (int32 y = Y0; y <= Y1; ++y)
			{
				for (int32 x = X0; x <= X1; ++x)
				{
					const FVector2D Q(Lo + (x + 0.5) * Cell, Lo + (y + 0.5) * Cell);
					const double W0 = ((S[1].X - Q.X) * (S[2].Y - Q.Y) - (S[2].X - Q.X) * (S[1].Y - Q.Y)) / Area2;
					const double W1 = ((S[2].X - Q.X) * (S[0].Y - Q.Y) - (S[0].X - Q.X) * (S[2].Y - Q.Y)) / Area2;
					if (W0 >= 0.0 && W1 >= 0.0 && 1.0 - W0 - W1 >= 0.0) { Cover[y * N + x] = 1; }
				}
			}
		}
		return Cover;
	}
	/** How much two silhouettes share of all either covers. */
	double SilhouetteOverlap(const TArray<uint8>& A, const TArray<uint8>& B)
	{
		int32 Both = 0, Either = 0;
		for (int32 i = 0; i < A.Num(); ++i) { Both += A[i] && B[i] ? 1 : 0; Either += A[i] || B[i] ? 1 : 0; }
		return Either > 0 ? (double)Both / Either : 1.0;
	}
	/** Of a silhouette seen from the side (x along it, z up), the runs of cells it covers along x in the row Z cm up: its
	 *  legs as they stand below its body. */
	int32 LegRuns(const TArray<uint8>& Side, int32 N, double Lo, double Cell, double Z)
	{
		const int32 Row = FMath::Clamp(FMath::FloorToInt32((Z - Lo) / Cell), 0, N - 1);
		int32 Runs = 0;
		for (int32 x = 0; x < N; ++x) { Runs += Side[Row * N + x] && (x == 0 || !Side[Row * N + x - 1]) ? 1 : 0; }
		return Runs;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaAlienWalkersTest, "Slonk.Fauna.AlienWalkers", SLONK_TEST_FLAGS)
bool FSlonkFaunaAlienWalkersTest::RunTest(const FString& Parameters)
{
	// The strange worlds' walkers, every plan at the ends and the middle of its ranges and wearing every ornament it can:
	// a six-legged grazer of 174 to 179, 32 and 5 triangles, an insectoid of 168 to 171, 38 and 4 (each speck's side its
	// flank whole, for its glow organs: Slonk.Fauna.GlowOrgans); a metre long, its feet
	// on z 0; its vertex colours as the material reads them (each leg's flex from 0 at the hip to 1 at its dark foot, an
	// insectoid's 0.45 at its knee, its blue its place in the tripod gait, the fore and hind of a side with the middle of the
	// other, half a stride from the other three; the neck and head, the antennae, mandibles and horns flexing as the neck;
	// the tail's from its root; the body rigid, its side by its sign); every face wound to show the side its normals point
	// to; a tangent basis at every vertex; the material's motion keeping every vertex in the instance's padded box at every
	// size its form comes in (an insectoid to 3.5 m), every gait, circle, ground and speck floor, with its plan's rig; its LODs
	// coming in where it is about 30 and 6 pixels long. From the side and from above they read as other animals than the
	// Earth's grazer and each other, at the LODs a herd is seen by from 30 to 300 m: three legs a side standing under them
	// (the grazer's two), a lower barrel and a heavier head, a wide, flat insectoid of legs splayed out.
	using namespace UE::Geometry;
	namespace Vc = SlonkFauna::Vc;
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Herd);
	const TArray<FAlienPlan> Plans = AlienHerdPlans();
	int32 BadTris = 0, BadColour = 0, BadWinding = 0, BadFeet = 0, BadGait = 0, Escaped = 0, Tried = 0, NoTangent = 0, Vertices = 0, BadLod = 0, BadReach = 0;
	double Margin = TNumericLimits<double>::Max(), WorstPx1Lo = 1e9, WorstPx1Hi = 0.0, WorstPx2Lo = 1e9, WorstPx2Hi = 0.0, MostTris = 0.0;
	FString Seen;
	int32 BoxOver = 0, LinedUp = 0, LinedUpOut = 0;
	double BoxBound = -TNumericLimits<double>::Max(), LinedUpMargin = TNumericLimits<double>::Max();
	FString BoundsSeen;
	TMap<FString, int32> InwardAt;
	for (int32 p = 0; p < Plans.Num(); ++p)
	{
		const FFaunaBodyPlan& Plan = Plans[p].Plan;
		const Wk::FShape S = Wk::ShapeOf(Plan);
		const bool bInsect = S.Form == Wk::EForm::Insectoid;
		const int32 Want[3] = { bInsect ? 168 + ((Plan.Ornament & 1) ? 2 : 0) + ((Plan.Ornament & 2) ? 1 : 0) : 174 + ((Plan.Ornament & 1) ? 2 : 0) + ((Plan.Ornament & 2) ? 3 : 0), bInsect ? 38 : 32, bInsect ? 4 : 5 };
		FBox Box0(ForceInit);
		FDynamicMesh3 Lod0;
		for (int32 L = 0; L < SlonkFaunaMeshes::NumLods; ++L)
		{
			FDynamicMesh3 M;
			if (!SlonkFaunaMeshes::BuildLod(Plan, L, M)) { AddError(TEXT("no alien walker built")); return false; }
			BadTris += M.TriangleCount() == Want[L] ? 0 : 1;
			MostTris = L == 0 ? FMath::Max(MostTris, (double)M.TriangleCount()) : MostTris;
			const FDynamicMeshColorOverlay* C = M.Attributes()->PrimaryColors();
			const FDynamicMeshNormalOverlay* Nm = M.Attributes()->PrimaryNormals();
			TSet<int32> PhasesSeen;
			for (const int32 T : M.TriangleIndicesItr())
			{
				FVector3d Fn, Centroid; double Area;
				M.GetTriInfo(T, Fn, Area, Centroid);
				const FIndex3i E = Nm->GetTriangle(T);
				const FIndex3i Ce = C->GetTriangle(T);
				const FIndex3i Tv = M.GetTriangle(T);
				if (Fn.Dot(FVector3d(Nm->GetElement(E.A) + Nm->GetElement(E.B) + Nm->GetElement(E.C))) <= 0.0)
				{
					++BadWinding;
					InwardAt.FindOrAdd(FString::Printf(TEXT("%s LOD %d joint %.3f"), FormName(S.Form), L, C->GetElement(Ce.A).W))++;
				}
				for (int32 k = 0; k < 3; ++k)
				{
					const FVector4f Col = C->GetElement(Ce[k]);
					const FVector P = (FVector)M.GetVertex(Tv[k]);
					if (L == 0) { Box0 += P; }
					bool bOk = true;
					if (FMath::IsNearlyEqual(Col.W, Vc::JointLeg))
					{
						// Its leg, by where it lies, its blue that leg's place in the gait and its green how far down it it is.
						double Along = 0.0;
						const int32 Leg = LegOfVertex(S, P, Along);
						const float WantFlex = bInsect ? (Along < 0.25 ? 0.f : (Along < 0.75 ? Wk::InsectKneeFlex : 1.f)) : (float)FMath::Clamp((S.Legs[FMath::Max(Leg, 0)].Hip.Z - P.Z) / S.Legs[FMath::Max(Leg, 0)].Hip.Z, 0.0, 1.0);
						bOk = Leg >= 0 && Col.Z == S.Legs[Leg].Phase && FMath::IsNearlyEqual(Col.Y, WantFlex, bInsect ? 1e-3f : 0.02f) && (Col.X == (Col.Y > 0.99f ? Vc::SurfaceBeak : Vc::SurfaceBody));
						PhasesSeen.Add(FMath::RoundToInt32(Col.Z * 1000.f));
						if (Col.Y > 0.99f && L == 0 && Leg >= 0) { BadFeet += FMath::Abs(P.Z) < 1e-3 && FVector2D::Distance(FVector2D(P.X, P.Y), FVector2D(S.Legs[Leg].Foot.X, S.Legs[Leg].Foot.Y)) < 3.0 ? 0 : 1; }
					}
					else if (FMath::IsNearlyEqual(Col.W, Vc::JointNeck)) { bOk = (Col.X == Vc::SurfaceBody || Col.X == Vc::SurfaceBeak) && Col.Y >= 0.f && Col.Y <= 1.f && P.X > 5.0; }
					else if (FMath::IsNearlyEqual(Col.W, Vc::JointTail)) { bOk = Col.X == Vc::SurfaceTail && Col.Y >= 0.f && Col.Y <= 1.f && Col.Z == Vc::SideMiddle && P.X < -40.0; }
					else if (Col.W == Vc::JointRigid) { bOk = (Col.X == Vc::SurfaceBody || (Col.X == Vc::SurfaceBeak && Col.Z == Vc::SideMiddle)) && Col.Y == 0.f && Col.Z == (P.Y > 0.5 ? Vc::SideRight : (P.Y < -0.5 ? Vc::SideLeft : Vc::SideMiddle)); }
					else { bOk = false; }
					BadColour += bOk ? 0 : 1;
				}
			}
			// Six legs of six places in the gait at LODs 0 and 1: the tripods half a stride apart, each leg of one a ripple after
			// the one behind it.
			if (L < 2) { BadGait += PhasesSeen.Num() == 6 ? 0 : 1; }
			if (L == 0) { Lod0 = MoveTemp(M); }
		}
		const float* Ph = Wk::TripodPhase;
		BadGait += S.NumLegs == 6 && FMath::IsNearlyEqual(Ph[3] - Ph[0], 0.5f) && FMath::IsNearlyEqual(Ph[1] - Ph[4], 0.5f) && FMath::IsNearlyEqual(Ph[5] - Ph[2], 0.5f)
			&& FMath::IsNearlyEqual(Ph[4] - Ph[0], Wk::Ripple) && FMath::IsNearlyEqual(Ph[2] - Ph[4], Wk::Ripple) ? 0 : 1;
		BadFeet += FMath::IsNearlyEqual(Box0.Min.Z, 0.0, 1e-3) && FMath::IsNearlyEqual(Box0.Min.X, -50.0, 0.6) ? 0 : 1;
		// Its footprint's reach is its feet's (Walker::FShape::Reach), which its herd keeps its walkers apart by.
		double Far = 50.0;
		for (int32 i = 0; i < S.NumLegs; ++i) { Far = FMath::Max(Far, (double)FVector2f(S.Legs[i].Foot.X, S.Legs[i].Foot.Y).Size() + 3.0); }
		BadReach += FMath::IsNearlyEqual(S.Reach, Far / 50.0, 1e-9) && S.Reach >= 1.0 ? 0 : 1;
		// The material's motion over LOD 0's vertices with its plan's rig, inside the box each instance is culled by.
		const FBox Padded = Box0.ExpandBy(Box0.GetExtent() * 0.15 + FVector(20.0 + D.CullPaddingUnits));
		const TArray<FLinearColor> Vcs = VertexColours(Lod0);
		FRandomStream Rng(57 + p);
		for (int32 Case = 0; Case < 160; ++Case)
		{
			float Cu[SlonkFauna::Custom::Num] = {};
			float Gd[SlonkFauna::HerdData::Num] = {};
			const float Size = Case % 5 == 0 ? (float)S.MinSizeM : (Case % 5 == 1 ? (float)S.MaxSizeM : Rng.FRandRange((float)S.MinSizeM, (float)S.MaxSizeM));
			const float Stride = (float)S.StrideShare * Size * Rng.FRandRange(0.97f, 1.03f);
			const int32 K = FMath::Max(3, FMath::FloorToInt32(UE_TWO_PI * (float)Wk::MaxCircleM / Stride * Rng.FRand()));
			Cu[SlonkFauna::Custom::SizeM] = Size;
			Cu[SlonkFauna::Custom::Seed] = Rng.FRand();
			Cu[SlonkFauna::Custom::Phase] = Rng.FRand();
			Cu[SlonkFauna::Custom::RateHz] = Stride;
			Cu[SlonkFauna::Custom::PerKind] = (Rng.FRand() < 0.5f ? -1.f : 1.f) * FMath::Min(FMath::Min(K * Stride / UE_TWO_PI, (float)Wk::MaxCircleM), Size * (float)FMath::Min(S.CircleShare, 1.0e6));
			const float T = Rng.FRandRange(0.f, 3600.f);
			Gd[SlonkFauna::HerdData::Heading] = Rng.FRandRange(-3.2f, 3.2f);
			Gd[SlonkFauna::HerdData::SpeedMS] = Case % 4 == 0 ? Rng.FRandRange(4.f, 13.f) : Rng.FRandRange(0.f, 3.4f);
			Gd[SlonkFauna::HerdData::DistM] = Rng.FRandRange(0.f, 2600.f);
			Gd[SlonkFauna::HerdData::GrazeSec] = Rng.FRandRange(0.f, 3600.f);
			Gd[SlonkFauna::HerdData::ClockT0] = FMath::Fmod(T - Rng.FRandRange(0.f, 0.1f) + 3600.f, 3600.f);
			Gd[SlonkFauna::HerdData::Travel] = Rng.FRand();
			Gd[SlonkFauna::HerdData::PivotRef] = Gd[SlonkFauna::HerdData::DistM] - (Case % 3 == 0 ? 1000.f : Rng.FRandRange(0.f, 8.f)) + (Case % 5 < 2 ? (float)SlonkFauna::HerdData::PivotBack : 0.f);
			Gd[SlonkFauna::HerdData::GrazeRef] = Gd[SlonkFauna::HerdData::GrazeSec] - Rng.FRandRange(0.f, 60.f);
			Gd[SlonkFauna::HerdData::WalkKap] = Rng.FRandRange(-1.f, 1.f) * (float)Wk::MaxWalkKap;
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
				const FVector3f Moved = LP + SlonkFauna::WalkerMotion(LP, Home + LP * Size, Vcs[V], T, Cu, Gd, Dist, 960.f, S.Rig);
				++Tried;
				const FVector Mv((double)Moved.X, (double)Moved.Y, (double)Moved.Z);
				Escaped += Padded.IsInside(Mv) ? 0 : 1;
				Margin = FMath::Min(Margin, FMath::Min3(FMath::Min(Mv.X - Padded.Min.X, Padded.Max.X - Mv.X), FMath::Min(Mv.Y - Padded.Min.Y, Padded.Max.Y - Mv.Y), FMath::Min(Mv.Z - Padded.Min.Z, Padded.Max.Z - Mv.Z)));
			}
		}
		// The box by arithmetic rather than by chance (the cases above seldom line the worst up): a walker going its herd's way
		// (or turning to it) faces any way at all against where it stands on its grazing circle, so the farthest any vertex of
		// it reaches from its middle across the ground (its head at every swing, its legs through every stride and turn, the
		// smallest of its form far off, grown SpeckMax times by the speck floor), and its circle as wide as its herd gives one
		// that size, may line up: together they stay within the box's nearest side to its middle. Then the worst of them so
		// lined up, run through the material's copy (its circle turned to that side, the walker turned to put the vertex
		// there), against each side of the box.
		{
			const float SizeLo = (float)S.MinSizeM;
			const float CircleLo = (float)FMath::Min(Wk::MaxCircleM, S.MinSizeM * S.CircleShare);
			const double Lim = FMath::Min(FMath::Min(-Padded.Min.X, Padded.Max.X), FMath::Min(-Padded.Min.Y, Padded.Max.Y));
			const FVector3f Origin = FVector3f::ZeroVector;
			const FLinearColor RigidMiddle(0.f, 0.f, 0.5f, 0.f);
			double Farthest = 0.0, FarWalking = 0.0;
			float WorstCu[SlonkFauna::Custom::Num] = {}, WorstGd[SlonkFauna::HerdData::Num] = {};
			float WorstT = 0.f;
			int32 WorstV = -1;
			FRandomStream Wr(919 + p);
			for (int32 Case = 0; Case < 240; ++Case)
			{
				float Cu[SlonkFauna::Custom::Num] = {};
				float Gd[SlonkFauna::HerdData::Num] = {};
				const float Stride = (float)S.StrideShare * SizeLo;
				Cu[SlonkFauna::Custom::SizeM] = SizeLo;
				Cu[SlonkFauna::Custom::Seed] = Wr.FRand();
				Cu[SlonkFauna::Custom::Phase] = Wr.FRand();
				Cu[SlonkFauna::Custom::RateHz] = Stride;
				const int32 K = FMath::Max(3, FMath::FloorToInt32(UE_TWO_PI * CircleLo / Stride));
				Cu[SlonkFauna::Custom::PerKind] = (Wr.FRand() < 0.5f ? -1.f : 1.f) * FMath::Min(K * Stride / UE_TWO_PI, CircleLo);
				const float T = Wr.FRandRange(0.f, 3600.f);
				// Walking with its herd (half the cases, its way straight on), turning to it or back, grazing.
				const int32 State = Case % 4;
				Gd[SlonkFauna::HerdData::Heading] = Wr.FRandRange(-3.2f, 3.2f);
				Gd[SlonkFauna::HerdData::SpeedMS] = State == 3 ? 0.f : Wr.FRandRange(0.3f, 9.f);
				Gd[SlonkFauna::HerdData::DistM] = Wr.FRandRange(0.f, 2600.f);
				Gd[SlonkFauna::HerdData::GrazeSec] = Wr.FRandRange(0.f, 3600.f);
				Gd[SlonkFauna::HerdData::ClockT0] = FMath::Fmod(T - Wr.FRandRange(0.f, 0.1f) + 3600.f, 3600.f);
				Gd[SlonkFauna::HerdData::Travel] = State < 2 ? 1.f : (State == 2 ? Wr.FRandRange(0.05f, 0.95f) : 0.f);
				Gd[SlonkFauna::HerdData::PivotRef] = Gd[SlonkFauna::HerdData::DistM] - Wr.FRandRange(0.f, 8.f) + (State == 3 ? (float)SlonkFauna::HerdData::PivotBack : 0.f);
				Gd[SlonkFauna::HerdData::GrazeRef] = Gd[SlonkFauna::HerdData::GrazeSec] - Wr.FRandRange(0.f, 60.f);
				Gd[SlonkFauna::HerdData::WalkKap] = State < 2 ? 0.f : Wr.FRandRange(-1.f, 1.f) * (float)Wk::MaxWalkKap;
				Gd[SlonkFauna::HerdData::TurnHeading] = Wr.FRandRange(-3.2f, 3.2f);
				Gd[SlonkFauna::HerdData::Startled] = Case % 3 == 0 ? 1.f : 0.f;
				const float PanicAge = Wr.FRandRange(0.f, 120.f);
				Gd[SlonkFauna::HerdData::PanicT0] = FMath::Fmod(T - PanicAge + 3600.f, 3600.f);
				Gd[SlonkFauna::HerdData::OnsetT0] = FMath::Fmod(T - PanicAge - Wr.FRandRange(0.f, 1.f) + 3600.f, 3600.f);
				Gd[SlonkFauna::HerdData::GroundHalfCm] = 3000.f;
				const FVector3f Circle = SlonkFauna::WalkerMotion(Origin, Origin, RigidMiddle, T, Cu, Gd, 4.0e5f, 960.f, S.Rig);
				for (const int32 V : Lod0.VertexIndicesItr())
				{
					const FVector3f LP = (FVector3f)Lod0.GetVertex(V);
					const FVector3f Moved = LP + SlonkFauna::WalkerMotion(LP, LP * SizeLo, Vcs[V], T, Cu, Gd, 4.0e5f, 960.f, S.Rig);
					const double R = FVector2D(Moved.X - Circle.X, Moved.Y - Circle.Y).Size();
					if (R > FarWalking && State < 2)
					{
						FarWalking = R;
						FMemory::Memcpy(WorstCu, Cu, sizeof(Cu));
						FMemory::Memcpy(WorstGd, Gd, sizeof(Gd));
						WorstT = T;
						WorstV = V;
					}
					Farthest = FMath::Max(Farthest, R);
				}
			}
			const double Worst = Farthest + 100.0 * CircleLo / SizeLo;
			BoxBound = FMath::Max(BoxBound, Worst - Lim);
			BoxOver += Worst <= Lim ? 0 : 1;
			// The worst walking vertex lined up with its circle, against each side.
			if (WorstV >= 0)
			{
				const FVector3f LP = (FVector3f)Lod0.GetVertex(WorstV);
				for (const double Side : { 0.0, 0.5 * UE_DOUBLE_PI, UE_DOUBLE_PI, 1.5 * UE_DOUBLE_PI })
				{
					float Cu[SlonkFauna::Custom::Num], Gd[SlonkFauna::HerdData::Num];
					FMemory::Memcpy(Cu, WorstCu, sizeof(Cu));
					FMemory::Memcpy(Gd, WorstGd, sizeof(Gd));
					// Its circle turned to the side (where it stands on it, by its place on the round: its phase), then the walker
					// turned so the vertex points that way too (going its herd's way, it faces its herd's).
					FVector3f Circle = SlonkFauna::WalkerMotion(Origin, Origin, RigidMiddle, WorstT, Cu, Gd, 4.0e5f, 960.f, S.Rig);
					Cu[SlonkFauna::Custom::Phase] += (float)((Side - FMath::Atan2((double)Circle.Y, (double)Circle.X)) / UE_DOUBLE_TWO_PI);
					Circle = SlonkFauna::WalkerMotion(Origin, Origin, RigidMiddle, WorstT, Cu, Gd, 4.0e5f, 960.f, S.Rig);
					const FVector3f Was = LP + SlonkFauna::WalkerMotion(LP, LP * SizeLo, Vcs[WorstV], WorstT, Cu, Gd, 4.0e5f, 960.f, S.Rig) - FVector3f(Circle.X, Circle.Y, 0.f);
					Gd[SlonkFauna::HerdData::Heading] += (float)(Side - FMath::Atan2((double)Was.Y, (double)Was.X));
					const FVector3f Moved = LP + SlonkFauna::WalkerMotion(LP, LP * SizeLo, Vcs[WorstV], WorstT, Cu, Gd, 4.0e5f, 960.f, S.Rig);
					const FVector Mv((double)Moved.X, (double)Moved.Y, (double)Moved.Z);
					LinedUp += 1;
					LinedUpOut += Padded.IsInside(Mv) ? 0 : 1;
					LinedUpMargin = FMath::Min(LinedUpMargin, FMath::Min(FMath::Min(Mv.X - Padded.Min.X, Padded.Max.X - Mv.X), FMath::Min(Mv.Y - Padded.Min.Y, Padded.Max.Y - Mv.Y)));
				}
			}
			if (S.Form == Wk::EForm::Insectoid && BoundsSeen.Len() < 600)
			{
				BoundsSeen += FString::Printf(TEXT("%s%.0f + %.0f against %.0f (circle %.2f m at %.2f m)"), BoundsSeen.IsEmpty() ? TEXT("") : TEXT(", "), Farthest, 100.0 * CircleLo / SizeLo, Lim, CircleLo, SizeLo);
			}
		}
		// The static mesh: made once and kept, three LODs, a tangent basis everywhere; where its LODs come in for one 2.5 m
		// long through a 90-degree view at 16:9, in pixels (960 a radian).
		UStaticMesh* Mesh = SlonkFaunaMeshes::GetMesh(Plan, EFaunaKind::Herd);
		if (!Mesh || Mesh != SlonkFaunaMeshes::GetMesh(Plan, EFaunaKind::Herd) || Mesh->GetNumLODs() != SlonkFaunaMeshes::NumLods) { ++BadLod; continue; }
		if (const FStaticMeshRenderData* RD = Mesh->GetRenderData())
		{
			for (const FStaticMeshLODResources& LR : RD->LODResources)
			{
				const FStaticMeshVertexBuffer& VB = LR.VertexBuffers.StaticMeshVertexBuffer;
				for (uint32 i = 0; i < VB.GetNumVertices(); ++i)
				{
					const FVector3f Tx(VB.VertexTangentX(i)), Tz(VB.VertexTangentZ(i)), Ty(VB.VertexTangentY(i));
					++Vertices;
					NoTangent += Tx.Size() > 0.5f && Ty.Size() > 0.5f && FMath::Abs(FVector3f::DotProduct(Tx.GetSafeNormal(), Tz.GetSafeNormal())) < 0.1f ? 0 : 1;
				}
			}
		}
		const double LengthM = 2.5;
		UPlanetInstancesComponent* Set = NewObject<UPlanetInstancesComponent>();
		Set->SetStaticMesh(Mesh);
		Set->SetCullPadding(D.CullPaddingUnits);
		Set->SetPieces(TArray<FTransform>{ FTransform(FQuat::Identity, FVector::ZeroVector, FVector(LengthM)) }, TConstArrayView<float>());
		const double Proj = (16.0 / 9.0) / FMath::Tan(FMath::DegreesToRadians(45.0));
		const double GpuR = Set->GetMaxInstanceRadius();
		const double Px1 = LengthM * 960.0 / (Proj * GpuR / D.LodScreenSize[1] / 100.0), Px2 = LengthM * 960.0 / (Proj * GpuR / D.LodScreenSize[2] / 100.0);
		WorstPx1Lo = FMath::Min(WorstPx1Lo, Px1); WorstPx1Hi = FMath::Max(WorstPx1Hi, Px1);
		WorstPx2Lo = FMath::Min(WorstPx2Lo, Px2); WorstPx2Hi = FMath::Max(WorstPx2Hi, Px2);
		if (p == 0 || p == Plans.Num() - 1)
		{
			Seen += FString::Printf(TEXT("%s%s (%s): LOD 0 %.0f x %.0f x %.0f cm, reach %.2f, legs %s, its rig's neck at (%.1f, %.1f), lift %.0f, swing %.2f, carapace %.0f"), Seen.IsEmpty() ? TEXT("") : TEXT("; "), FormName(S.Form),
				*SlonkFauna::DescribePlan(EFaunaKind::Herd, Plan), Box0.GetSize().X, Box0.GetSize().Y, Box0.GetSize().Z, S.Reach, *FString::Printf(TEXT("%d"), S.NumLegs), S.Rig.NeckX, S.Rig.NeckZ, S.Rig.LiftCm, S.Rig.PitchScale, S.Rig.Carapace);
		}
	}
	AddInfo(FString::Printf(TEXT("%d alien walker plans: %d LODs off their triangles (%.0f at LOD 0 at the most), %d vertices off the colour contract, %d faces wound inward, %d feet off the ground or their legs, %d gaits off the tripod, %d reaches off their feet; %d vertices moved by the material, %d outside the box padded %.0f units (the closest %.0f inside); %d vertices over their meshes, %d without a tangent basis, %d meshes not made; LOD 1 from %.1f to %.1f pixels long, LOD 2 from %.1f to %.1f"),
		Plans.Num(), BadTris, MostTris, BadColour, BadWinding, BadFeet, BadGait, BadReach, Tried, Escaped, D.CullPaddingUnits, Margin, Vertices, NoTangent, BadLod, WorstPx1Lo, WorstPx1Hi, WorstPx2Lo, WorstPx2Hi));
	AddInfo(Seen);
	for (const TPair<FString, int32>& In : InwardAt) { AddInfo(FString::Printf(TEXT("wound inward: %d faces of the %s"), In.Value, *In.Key)); }
	AddInfo(FString::Printf(TEXT("the box by arithmetic: %d plans whose farthest reach grown by the speck floor and widest circle at their smallest, lined up, pass the box's nearest side (by %.1f units at the worst, under 0 inside); the insectoids' farthest and circle against the side, unit-mesh cm: %s; %d runs of the worst walking vertex lined up with its circle against each side, %d outside the box (the closest %.1f inside)"),
		BoxOver, BoxBound, *BoundsSeen, LinedUp, LinedUpOut, LinedUpMargin));
	TestTrue(TEXT("plans of both forms"), Plans.Num() >= 16);
	TestEqual(TEXT("every LOD its triangles"), BadTris, 0);
	TestTrue(TEXT("no more than 180 triangles at LOD 0"), MostTris <= 180.0);
	TestEqual(TEXT("every vertex as the material reads it"), BadColour, 0);
	TestEqual(TEXT("every face shows the side its normals point to"), BadWinding, 0);
	TestEqual(TEXT("every foot on the ground at its leg's foot"), BadFeet, 0);
	TestEqual(TEXT("six legs in a tripod gait"), BadGait, 0);
	TestEqual(TEXT("its footprint's reach its feet's"), BadReach, 0);
	TestEqual(TEXT("the material keeps every vertex in its instance's box, speck floor and all"), Escaped, 0);
	TestEqual(TEXT("by arithmetic: the farthest reach grown by the speck floor and the widest circle, lined up, inside the box"), BoxOver, 0);
	TestTrue(TEXT("and the worst of them lined up with its circle, through the material's copy, inside it"), LinedUp > 0 && LinedUpOut == 0);
	TestTrue(TEXT("every mesh made and kept, a tangent basis at every vertex"), BadLod == 0 && Vertices > 0 && NoTangent == 0);
	TestTrue(TEXT("its 32 or 38 triangles from about 30 pixels long, its 5 or 4 from about 6"), WorstPx1Lo > 24.0 && WorstPx1Hi < 38.0 && WorstPx2Lo > 4.5 && WorstPx2Hi < 8.0);

	// Their silhouettes against the Earth grazer's and each other's, from the side, from above and from the air (from 35
	// degrees up, off its quarter, as a ship's camera at 30 to 300 m sees a herd), at LOD 1 (the LOD a herd is seen by from
	// about 40 m to 200 m off) and LOD 0: how much of each they share (of all either covers), and the legs standing under
	// each, seen from the side 4 cm over the ground.
	const FVector Air = FVector(FMath::Cos(FMath::DegreesToRadians(35.0)) * FVector(0.7071, 0.7071, 0.0) - FVector(0.0, 0.0, FMath::Sin(FMath::DegreesToRadians(35.0)))).GetSafeNormal();
	const FVector AirRight = FVector::CrossProduct(Air, FVector::ZAxisVector).GetSafeNormal(), AirUp = FVector::CrossProduct(AirRight, Air).GetSafeNormal();
	auto Look = [&](const FFaunaBodyPlan& P, int32 Lod, TArray<uint8>& Side, TArray<uint8>& Top, TArray<uint8>& Oblique, int32& N)
	{
		FDynamicMesh3 M;
		SlonkFaunaMeshes::BuildLod(P, Lod, M);
		Side = Silhouette(M, FVector::XAxisVector, FVector::ZAxisVector, -80.0, 90.0, 0.25, N);
		Top = Silhouette(M, FVector::XAxisVector, FVector::YAxisVector, -80.0, 90.0, 0.25, N);
		Oblique = Silhouette(M, AirRight, AirUp, -80.0, 90.0, 0.25, N);
	};
	const FFaunaBodyPlan Earth = SlonkFauna::EarthPlan(EFaunaKind::Herd), Hexa = AlienHerdPlan(0, 0.5f, 0.6f), Insect = AlienHerdPlan(1, 0.5f, 0.6f);
	FString Looks;
	double MostAlike = 0.0, MostAlikeAnyView = 0.0;
	int32 LegsOff = 0;
	for (const int32 Lod : { 0, 1 })
	{
		TArray<uint8> Es, Et, Eo, Hs, Ht, Ho, Is, It, Io;
		int32 N = 0;
		Look(Earth, Lod, Es, Et, Eo, N);
		Look(Hexa, Lod, Hs, Ht, Ho, N);
		Look(Insect, Lod, Is, It, Io, N);
		const double EH = SilhouetteOverlap(Es, Hs), EI = SilhouetteOverlap(Es, Is), HI = SilhouetteOverlap(Hs, Is);
		const double EHt = SilhouetteOverlap(Et, Ht), EIt = SilhouetteOverlap(Et, It), HIt = SilhouetteOverlap(Ht, It);
		const double EHo = SilhouetteOverlap(Eo, Ho), EIo = SilhouetteOverlap(Eo, Io), HIo = SilhouetteOverlap(Ho, Io);
		const int32 Le = LegRuns(Es, N, -80.0, 0.25, 4.0), Lh = LegRuns(Hs, N, -80.0, 0.25, 4.0), Li = LegRuns(Is, N, -80.0, 0.25, 4.0);
		LegsOff += Le == 2 && Lh == 3 && Li == 3 ? 0 : 1;
		// From the air, every pair apart; and each pair far apart in one view at least.
		MostAlike = FMath::Max(MostAlike, FMath::Max3(EHo, EIo, HIo));
		MostAlikeAnyView = FMath::Max(MostAlikeAnyView, FMath::Max3(FMath::Min3(EH, EHt, EHo), FMath::Min3(EI, EIt, EIo), FMath::Min3(HI, HIt, HIo)));
		Looks += FString::Printf(TEXT("%sLOD %d: the Earth grazer and the six-legged grazer share %.2f of their side view, %.2f of their top and %.2f from the air; the grazer and the insectoid %.2f, %.2f and %.2f; the two alien forms %.2f, %.2f and %.2f; legs standing seen from the side %d, %d and %d"),
			Looks.IsEmpty() ? TEXT("") : TEXT("; "), Lod, EH, EHt, EHo, EI, EIt, EIo, HI, HIt, HIo, Le, Lh, Li);
	}
	AddInfo(Looks);
	TestEqual(TEXT("two legs standing under the Earth grazer from the side, three under each alien form"), LegsOff, 0);

	// Every alien plan's head down in the grass as it grazes, up as it looks up now and then, and high when it is alarmed,
	// standing on level ground, at LOD 0 and LOD 1: no vertex of it under the ground (a six-legged grazer's head swung its
	// own share of the Earth grazer's, so its muzzle and its horns' tips stay over the grass). The plans above and 300 of each
	// form drawn at random.
	TArray<FFaunaBodyPlan> PosePlans;
	for (const FAlienPlan& A : Plans) { PosePlans.Add(A.Plan); }
	{
		FRandomStream Rng(4242);
		for (int32 Form = 0; Form < SlonkFauna::AlienForms(EFaunaKind::Herd); ++Form)
		{
			for (int32 i = 0; i < 300; ++i)
			{
				float Shape[SlonkFauna::SpeciesShapeDraws];
				for (float& U : Shape) { U = Rng.FRand(); }
				float Scale = 1.f;
				PosePlans.Add(SlonkFauna::AlienPlan(EFaunaKind::Herd, Form, Shape, Scale));
			}
		}
	}
	const TCHAR* PoseName[3] = { TEXT("grazing"), TEXT("looking up"), TEXT("alarmed") };
	double Lowest[3] = { 1e9, 1e9, 1e9 };
	int32 Under[3] = {}, PoseTried = 0;
	FString LowestAt;
	double HexaSwingLo = 1.0, HexaSwingHi = 0.0;
	for (const FFaunaBodyPlan& Plan : PosePlans)
	{
		const Wk::FShape S = Wk::ShapeOf(Plan);
		if (S.Form == Wk::EForm::Hexapod) { HexaSwingLo = FMath::Min(HexaSwingLo, (double)S.Rig.PitchScale); HexaSwingHi = FMath::Max(HexaSwingHi, (double)S.Rig.PitchScale); }
		for (int32 L = 0; L < 2; ++L)
		{
			FDynamicMesh3 M;
			SlonkFaunaMeshes::BuildLod(Plan, L, M);
			const TArray<FLinearColor> Vcs = VertexColours(M);
			for (int32 Pose = 0; Pose < 3; ++Pose)
			{
				float Cu[SlonkFauna::Custom::Num] = {};
				float Gd[SlonkFauna::HerdData::Num] = {};
				const float Size = 2.f;
				Cu[SlonkFauna::Custom::SizeM] = Size;
				// Its seed 0: it looks up 60 times an hour, at the turn of each minute, for 0.12 of it.
				Cu[SlonkFauna::Custom::Seed] = 0.f;
				Cu[SlonkFauna::Custom::Phase] = 0.3f;
				Cu[SlonkFauna::Custom::RateHz] = (float)S.StrideShare * Size;
				Cu[SlonkFauna::Custom::PerKind] = 1.5f;
				const float T = Pose == 1 ? 3.6f : 30.f;
				Gd[SlonkFauna::HerdData::GrazeSec] = 100.f;
				Gd[SlonkFauna::HerdData::ClockT0] = T;
				Gd[SlonkFauna::HerdData::PivotRef] = -1000.f + (float)SlonkFauna::HerdData::PivotBack;
				Gd[SlonkFauna::HerdData::GrazeRef] = 50.f;
				Gd[SlonkFauna::HerdData::GroundHalfCm] = 3000.f;
				if (Pose == 2) { Gd[SlonkFauna::HerdData::Startled] = 1.f; Gd[SlonkFauna::HerdData::PanicT0] = T - 5.f; Gd[SlonkFauna::HerdData::OnsetT0] = T - 5.f; }
				for (const int32 V : M.VertexIndicesItr())
				{
					const FVector3f LP = (FVector3f)M.GetVertex(V);
					const FVector3f Moved = LP + SlonkFauna::WalkerMotion(LP, LP * Size, Vcs[V], T, Cu, Gd, 2000.f, 960.f, S.Rig);
					++PoseTried;
					Under[Pose] += Moved.Z < -1e-3f ? 1 : 0;
					if (Moved.Z < Lowest[Pose])
					{
						Lowest[Pose] = Moved.Z;
						if (Pose == 0) { LowestAt = FString::Printf(TEXT("%s at LOD %d, its swing %.2f"), *SlonkFauna::DescribePlan(EFaunaKind::Herd, Plan), L, S.Rig.PitchScale); }
					}
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d alien plans (%d drawn at random), %d vertices posed at LOD 0 and 1: under the ground %s %d, %s %d, %s %d; the lowest over it %.2f, %.2f and %.2f unit-mesh cm (grazing: %s); the six-legged grazers' heads swung %.2f to %.2f of the Earth grazer's"),
		PosePlans.Num(), PosePlans.Num() - Plans.Num(), PoseTried, PoseName[0], Under[0], PoseName[1], Under[1], PoseName[2], Under[2], Lowest[0], Lowest[1], Lowest[2], *LowestAt, HexaSwingLo, HexaSwingHi));
	TestEqual(TEXT("no vertex under the ground as it grazes, looks up or stands alarmed"), Under[0] + Under[1] + Under[2], 0);
	TestTrue(TEXT("each a silhouette of its own from the air"), MostAlike < 0.75);
	TestTrue(TEXT("and far apart from the side, from above or from the air"), MostAlikeAnyView < 0.6);
	return true;
}

namespace
{
	/** The worlds and herds the alien herds' tests run over: of each form, a herd of a plan at the middle of its ranges and
	 *  one of the largest walkers of a plan at their high ends (an insectoid's to 3.4 m long). */
	struct FAlienHerdCase
	{
		SlonkFauna::FFaunaWorld W;
		SlonkFauna::FFaunaGroup G;
		FString Name;
		/** The Earth grazers' herd of the same cell (on the same ground, on the same path), to set its slides against. */
		SlonkFauna::FFaunaGroup Earth;
		bool bEarth = false;
	};
	TArray<FAlienHerdCase> AlienHerdCases()
	{
		TArray<FAlienHerdCase> Out;
		for (int32 Form = 0; Form < SlonkFauna::AlienForms(EFaunaKind::Herd); ++Form)
		{
			for (const bool bLargest : { false, true })
			{
				float Scale = 1.f;
				const FFaunaBodyPlan Plan = AlienHerdPlan(Form, bLargest ? 1.f : 0.5f, 0.9f, &Scale);
				FAlienHerdCase C;
				C.W = AlienHerdWorld(Plan, Scale);
				if (!TestAlienHerd(C.W, bLargest, C.G)) { continue; }
				C.Name = FString::Printf(TEXT("%s herd of %d of %.2f m (%s)"), FormName(Wk::ShapeOf(Plan).Form), C.G.Members, C.G.SpanM, bLargest ? TEXT("the largest") : TEXT("a middling plan"));
				C.bEarth = SlonkFauna::GroupInCell(EarthHerds(), SlonkFauna::CellOf(C.W.P, EFaunaKind::Herd, C.G.Centre), C.Earth);
				Out.Add(MoveTemp(C));
			}
		}
		return Out;
	}
}

namespace
{
	/** What following a herd's feet through every scenario of Slonk.Fauna.AlienHerdNoSlide found. */
	struct FSlideSet
	{
		FHerdRun Walk, Graze, Frozen, WalkFar, WalkCoarse, SetOff, Stop, CrewRun, Fright, WalkSide, WalkAhead, GallopAhead;
		int32 CrewStartles = 0, Startles = 0, SideStartles = 0, AheadStartles = 0, TwiceStartles = 0;
		/** The runs through its turns and flights. */
		TArray<const FHerdRun*> Turns() const { return { &SetOff, &Stop, &CrewRun, &Fright, &WalkSide, &WalkAhead, &GallopAhead }; }
	};
	/** A herd stepped through every scenario: walking into its trot and grazing (its ground read each frame, every sixth, and
	 *  on coarser chunks), its walk not run on, setting off, stopping, someone walking at it from ahead and away, a craft low
	 *  ahead of it as it grazes (its whole fright), from its side and from ahead as it walks, and a second from ahead as it
	 *  gallops. */
	FSlideSet SlideRuns(const SlonkFauna::FFaunaGroup& G, const SlonkFauna::FFaunaWorld& W)
	{
		FSlideSet Out;
		const double R = W.P.RadiusCm;
		const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Herd);
		FDrawnGround Fine;
		Fine.P = W.P;
		Fine.Depth = 16;
		FDrawnGround Coarse;
		Coarse.P = W.P;
		Coarse.Depth = 14;
		auto FineAt = [&Fine](const FVector& Dir) { return Fine.At(Dir); };
		auto CoarseAt = [&Coarse](const FVector& Dir) { return Coarse.At(Dir); };
		const double C0 = 3.0 * G.CycleSec;
		const double WalkStart = C0 + G.TrotA - 25.0, GrazeStart = C0 + G.WalkB + 5.0;
		Out.Walk = RunHerd(G, R, WalkStart, 60.0, 30.0, 1, FineAt, 2);
		Out.Graze = RunHerd(G, R, GrazeStart, 90.0, 30.0, 1, FineAt, 2);
		Out.Frozen = RunHerd(G, R, WalkStart + 20.0, 15.0, 30.0, 1, FineAt, 3, true);
		Out.WalkFar = RunHerd(G, R, WalkStart, 60.0, 30.0, 6, FineAt, 3);
		Out.WalkCoarse = RunHerd(G, R, WalkStart, 60.0, 10.0, 6, CoarseAt, 3);
		Out.SetOff = RunHerd(G, R, C0 + G.WalkA - SlonkFauna::HerdLeadSec - 4.0, 16.0, 30.0, 1, FineAt, 1);
		Out.Stop = RunHerd(G, R, C0 + G.WalkB - 6.0, 16.0, 30.0, 1, FineAt, 1);
		auto Facing = [](const SlonkFauna::FHerdState& S) { return S.FrameRot.GetAxisX() * FMath::Cos(S.Heading) + S.FrameRot.GetAxisY() * FMath::Sin(S.Heading); };
		// Someone walking at it from ahead as it grazes, from 100 m off its edge at 1.4 m/s for 45 s, then away again.
		FVector Crew = FVector::ZeroVector, Toward = FVector::ZeroVector;
		SlonkFauna::FPanic CrewPanic;
		const double CrewStart = GrazeStart + 10.0;
		Out.CrewRun = RunHerdWith(G, R, CrewStart, 150.0, 30.0, 1, FineAt, [&](SlonkFauna::FHerdState& S, double Now, double Dt)
		{
			FHerdDrive Dr;
			if (!S.bStarted) { return Dr; }
			if (Toward.IsZero()) { Crew = S.FramePos + Facing(S) * (G.HerdRadiusCm + 1.0e4); Toward = -Facing(S); }
			Crew += Toward * ((Now - CrewStart < 45.0 ? 140.0 : -140.0) * Dt);
			SlonkFauna::FFaunaThreat Foot;
			Foot.bCraft = false;
			Foot.PosCm = Crew;
			Out.CrewStartles += SlonkFauna::StartleHerd(CrewPanic, S, G, D, R, Foot, Now) ? 1 : 0;
			Dr.bMoving = SlonkFauna::KeepFromCrew(S, G, D, TArray<FVector>{ Crew }, Dt);
			Dr.bShaken = CrewPanic.IsLive(Now);
			Dr.Offset = SlonkFauna::HerdFrightOffset(G, D, CrewPanic, R, Now) + S.Avoid;
			Dr.Ahead = SlonkFauna::HerdFrightOffset(G, D, CrewPanic, R, Now + 1.0) + S.Avoid + (Dr.bMoving ? S.AvoidWay : FVector::ZeroVector);
			return Dr;
		}, 2);
		// A craft passing 30 m up 50 m off it, coming at it, from a way relative to the way it faces.
		auto CraftBy = [&](const SlonkFauna::FHerdState& S, double Bearing)
		{
			const FVector Up = S.FramePos.GetSafeNormal();
			const FVector F = Facing(S);
			const FVector Way = F * FMath::Cos(Bearing) + FVector::CrossProduct(Up, F) * FMath::Sin(Bearing);
			SlonkFauna::FFaunaThreat Craft;
			Craft.PosCm = S.FramePos + Way * 5000.0 + Up * 3000.0;
			Craft.VelCmS = -Way * 8000.0;
			Craft.AltitudeCm = 3000.0;
			return Craft;
		};
		auto Frightened = [&](double Start, double Seconds, const TArray<TPair<double, double>>& Frights, int32& Startled)
		{
			SlonkFauna::FPanic P;
			int32 Next = 0;
			return RunHerdWith(G, R, Start, Seconds, 30.0, 1, FineAt, [&](SlonkFauna::FHerdState& S, double Now, double Dt)
			{
				FHerdDrive Dr;
				if (S.bStarted && Next < Frights.Num() && Now - Start >= Frights[Next].Key)
				{
					Startled += SlonkFauna::StartleHerd(P, S, G, D, R, CraftBy(S, Frights[Next].Value), Now) ? 1 : 0;
					++Next;
				}
				Dr.bShaken = P.IsLive(Now);
				Dr.Offset = SlonkFauna::HerdFrightOffset(G, D, P, R, Now);
				Dr.Ahead = SlonkFauna::HerdFrightOffset(G, D, P, R, Now + 1.0);
				return Dr;
			}, 2);
		};
		Out.Fright = Frightened(GrazeStart + 10.0, D.FrightSec + 5.0, { { 0.0, 0.0 } }, Out.Startles);
		const double OnTheMove = C0 + G.WalkA + 12.0;
		Out.WalkSide = Frightened(OnTheMove, 40.0, { { 3.0, 0.5 * UE_DOUBLE_PI } }, Out.SideStartles);
		Out.WalkAhead = Frightened(OnTheMove, 40.0, { { 3.0, 0.0 } }, Out.AheadStartles);
		Out.GallopAhead = Frightened(GrazeStart + 10.0, 40.0, { { 0.0, UE_DOUBLE_PI }, { 10.0, 0.0 } }, Out.TwiceStartles);
		return Out;
	}
	FString SlideLine(const TCHAR* What, const FHerdRun& Run)
	{
		return FString::Printf(TEXT("%s %.2f cm/s on average, %.1f at worst (%.0f s in, its share on the move %.2f, turning %.3f rad/m; by foot %.1f/%.1f/%.1f/%.1f/%.1f/%.1f; %.1f while its walkers turned; %d of %d over 10 cm/s)"),
			What, Run.MeanSlip(), Run.WorstSlip, Run.WorstSlipAt, Run.TravelAtWorst, Run.KapAtWorst, Run.WorstByFoot[0], Run.WorstByFoot[1], Run.WorstByFoot[2], Run.WorstByFoot[3], Run.WorstByFoot[4], Run.WorstByFoot[5],
			Run.WorstTurnSlip, Run.Slides, Run.Slips);
	}
	FString SlideReport(const FString& Name, const FSlideSet& S)
	{
		return FString::Printf(TEXT("%s: on the move (%d foot-steps planted, at up to %.1f m/s) planted feet slide %.2f cm/s on average, %.2f at worst; grazing %.2f and %.2f; with its walk not run on %.1f cm/s; %s; %s; %s (startled %d); %s (startled %d times); %s; %s; %s; a walker's yaw %.3f rad a frame at most; planted feet off the drawn ground by %.1f cm at most (%.1f on average) walking, read each frame; %.1f (%.1f) every sixth; grazing %.1f (%.1f); on chunks 610 m across %.1f (%.1f)"),
			*Name, S.Walk.TravelSlips, S.Walk.MaxSpeedMS, S.Walk.MeanTravelSlip(), S.Walk.WorstTravelSlip, S.Graze.MeanGrazeSlip(), S.Graze.WorstGrazeSlip, S.Frozen.MeanTravelSlip(), *SlideLine(TEXT("setting off"), S.SetOff),
			*SlideLine(TEXT("stopping"), S.Stop), *SlideLine(TEXT("someone at it from ahead and away"), S.CrewRun), S.CrewStartles, *SlideLine(TEXT("a craft low ahead of it (its whole fright)"), S.Fright), S.Startles,
			*SlideLine(TEXT("walking, a craft from its side"), S.WalkSide), *SlideLine(TEXT("walking, from ahead"), S.WalkAhead), *SlideLine(TEXT("galloping, a second from ahead"), S.GallopAhead),
			FMath::Max3(S.Walk.WorstYawStep, S.Graze.WorstYawStep, FMath::Max3(S.SetOff.WorstYawStep, S.Stop.WorstYawStep, S.Fright.WorstYawStep)),
			S.Walk.WorstPlantedCm, S.Walk.MeanPlantedCm(), S.WalkFar.WorstPlantedCm, S.WalkFar.MeanPlantedCm(), S.Graze.WorstPlantedCm, S.Graze.MeanPlantedCm(), S.WalkCoarse.WorstPlantedCm, S.WalkCoarse.MeanPlantedCm());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaAlienHerdNoSlideTest, "Slonk.Fauna.AlienHerdNoSlide", SLONK_TEST_FLAGS)
bool FSlonkFaunaAlienHerdNoSlideTest::RunTest(const FString& Parameters)
{
	// The alien herds' feet stay where they are put down and on the ground that is drawn, as an Earth herd's do: of each
	// form, a middling herd and one of its largest walkers, every planted foot of six followed (its colour its mesh's own at
	// the foot, so a foot built off the contract slides) walking into its trot, grazing round its circles, setting off and
	// stopping (each a whole turn where it stands), someone walking at it from ahead and away, a craft low ahead of it as it
	// grazes (its whole fright: it turns, gallops off, loops round and walks back), a craft from its side and from ahead as
	// it walks (it stops and turns before it gallops) and a second from ahead as it gallops (it flees on). Each against the
	// Earth grazers' herd of the same cell, on the same ground and the same path, run the same way: a planted foot slides
	// less than a centimetre a second on average and none more than 25 cm/s through its turns and flights (6 cm/s walking and
	// grazing), hardly one in a thousand more than 10 cm/s, or no more than the Earth grazers' feet do there (a tenth more
	// and half a centimetre a second); with its walk not run on, they would slide as fast as it walks. No walker's yaw jumps
	// between two frames. On chunks 150 m across and 610 m across, every planted foot within half a metre of the ground
	// drawn under it, a few centimetres on the whole.
	const TArray<FAlienHerdCase> Cases = AlienHerdCases();
	TestTrue(TEXT("a herd of each form, middling and largest, and the Earth grazers of their cells"), Cases.Num() == 4 && !Cases.ContainsByPredicate([](const FAlienHerdCase& C) { return !C.bEarth; }));
	// Within a bound, or no worse than the Earth grazers' feet on the same ground.
	auto AsLittle = [](double Got, double Bound, double Earth) { return Got < Bound || Got <= 1.1 * Earth + 0.5; };
	for (const FAlienHerdCase& Case : Cases)
	{
		const FSlideSet A = SlideRuns(Case.G, Case.W);
		const FSlideSet E = SlideRuns(Case.Earth, Case.W);
		AddInfo(SlideReport(Case.Name, A));
		AddInfo(SlideReport(FString::Printf(TEXT("the Earth grazers of its cell (%d of %.2f m)"), Case.Earth.Members, Case.Earth.SpanM), E));
		TestTrue(TEXT("feet followed on the move and grazing"), A.Walk.TravelSlips > 1000 && A.Graze.GrazeSlips > 100 && A.Walk.MaxSpeedMS > 2.0);
		TestTrue(TEXT("on the move, a planted foot slides less than a centimetre a second"), AsLittle(A.Walk.MeanTravelSlip(), 1.0, E.Walk.MeanTravelSlip()) && AsLittle(A.Walk.WorstTravelSlip, 6.0, E.Walk.WorstTravelSlip));
		TestTrue(TEXT("grazing, as little"), AsLittle(A.Graze.MeanGrazeSlip(), 1.0, E.Graze.MeanGrazeSlip()) && AsLittle(A.Graze.WorstGrazeSlip, 6.0, E.Graze.WorstGrazeSlip));
		TestTrue(TEXT("with the herd's walk not run on, they would slide as fast as it walks"), A.Frozen.MeanTravelSlip() > 50.0);
		TestTrue(TEXT("it set off, stopped and fled, each a whole turn"), A.SetOff.MinTravel < 0.01 && A.SetOff.MaxTravel > 0.99 && A.Stop.MinTravel < 0.01 && A.Stop.MaxTravel > 0.99
			&& A.Fright.MinTravel < 0.01 && A.Fright.MaxTravel > 0.99 && A.Startles == 1 && A.CrewStartles == 0);
		TestTrue(TEXT("startled on the move and again galloping"), A.SideStartles == 1 && A.AheadStartles == 1 && A.TwiceStartles == 2 && A.WalkSide.MinTravel < 0.01 && A.GallopAhead.MaxSpeedMS > 4.0);
		const TArray<const FHerdRun*> At = A.Turns(), Et = E.Turns();
		for (int32 i = 0; i < At.Num(); ++i)
		{
			TestTrue(TEXT("through its turns and flights, a planted foot slides less than a centimetre a second on average"), AsLittle(At[i]->MeanSlip(), 1.0, Et[i]->MeanSlip()) && At[i]->Slips > 500);
			TestTrue(TEXT("none more than 25 cm/s"), AsLittle(At[i]->WorstSlip, 25.0, Et[i]->WorstSlip));
			TestTrue(TEXT("hardly one in a thousand more than 10 cm/s"), AsLittle((double)At[i]->Slides / FMath::Max(At[i]->Slips, 1), 1e-3, (double)Et[i]->Slides / FMath::Max(Et[i]->Slips, 1)));
		}
		for (const FHerdRun* Run : { &A.Walk, &A.Graze, &A.SetOff, &A.Stop, &A.CrewRun, &A.Fright, &A.WalkSide, &A.WalkAhead, &A.GallopAhead })
		{
			TestTrue(TEXT("no walker's yaw jumps between two frames"), Run->WorstYawStep < 0.3);
		}
		TestTrue(TEXT("every planted foot within half a metre of the drawn ground"), A.Walk.WorstPlantedCm <= 50.0 && A.Graze.WorstPlantedCm <= 50.0 && A.WalkFar.WorstPlantedCm <= 50.0 && A.WalkCoarse.WorstPlantedCm <= 50.0);
		TestTrue(TEXT("on it, on the whole, within a few centimetres"), A.Walk.MeanPlantedCm() < 10.0 && A.Graze.MeanPlantedCm() < 10.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaAlienHerdFrightTest, "Slonk.Fauna.AlienHerdFright", SLONK_TEST_FLAGS)
bool FSlonkFaunaAlienHerdFrightTest::RunTest(const FString& Parameters)
{
	// The alien herds take fright, flee and come back as an Earth herd does, of each form, a middling herd and the largest:
	// a craft passing 30 m over it startles it (one 300 m up does not), it gallops off (no faster than nine of its shortest
	// walkers' strides a second, from a stand) and comes back onto its path by the end of its fright, at a steady pace no
	// faster than its trot, exactly where it would have been. Its flight never takes it into the water: a lake where a craft
	// from each of eight ways would have driven it turns it from it. Someone walking at it sees it keep 40 to 60 m from them.
	// It raises dust at its trot by day near the camera and none as it grazes, its puffs its walkers' size.
	const TArray<FAlienHerdCase> Cases = AlienHerdCases();
	TestTrue(TEXT("a herd of each form, middling and largest"), Cases.Num() == 4);
	for (const FAlienHerdCase& Case : Cases)
	{
		const SlonkFauna::FFaunaGroup& G = Case.G;
		const SlonkFauna::FFaunaWorld& W = Case.W;
		const double R = W.P.RadiusCm;
		const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Herd);
		auto Height = [&W](const FVector& Dir) { return FMath::Max(SlonkTerrain::Sample(W.P, Dir, 2000.0).HeightCm, 0.0); };
		const double T0 = G.CycleSec * 2.0 + G.WalkB + 2.0;
		auto Step = [&](SlonkFauna::FHerdState& S, const SlonkFauna::FPanic& P, double Now, double Dt, int32 k)
		{
			const FVector Ahead = SlonkFauna::HerdFrightOffset(G, D, P, R, Now + 1.0) + S.Avoid;
			SlonkFauna::StepHerd(S, G, R, Now, Dt, SlonkFauna::HerdFrightOffset(G, D, P, R, Now) + S.Avoid, P.IsLive(Now), false, k % 4 == 0, Height, 0.0, nullptr, &Ahead);
		};
		// A pass straight across it at 80 m/s, low and high.
		auto Pass = [&](double AltitudeCm, SlonkFauna::FPanic& Panic, SlonkFauna::FHerdState& S)
		{
			SlonkFauna::StartHerd(S, G, T0);
			Step(S, Panic, T0, 0.0, 0);
			const FVector Up = S.FramePos.GetSafeNormal();
			const FVector Across = S.FrameRot.GetAxisY();
			const FVector Mid = S.FramePos + Up * AltitudeCm;
			int32 Startled = 0;
			for (int32 k = 1; k <= 300; ++k)
			{
				const double Now = T0 + k / 20.0;
				SlonkFauna::FFaunaThreat Craft;
				Craft.PosCm = Mid + Across * (8000.0 * (k / 20.0 - 7.5));
				Craft.VelCmS = Across * 8000.0;
				Craft.AltitudeCm = AltitudeCm;
				Startled += SlonkFauna::StartleHerd(Panic, S, G, D, R, Craft, Now) ? 1 : 0;
				Step(S, Panic, Now, 1.0 / 20.0, k);
			}
			return Startled;
		};
		SlonkFauna::FPanic Low, High;
		SlonkFauna::FHerdState SLow, SHigh;
		const int32 LowStartles = Pass(3000.0, Low, SLow), HighStartles = Pass(30000.0, High, SHigh);
		double Fastest = 0.0, BackFastest = 0.0;
		const int32 Last = FMath::CeilToInt32(20.0 * (Low.T0 - T0 + D.FrightSec + 10.0));
		for (int32 k = 301; k <= Last; ++k)
		{
			const double Now = T0 + k / 20.0;
			Step(SLow, Low, Now, 1.0 / 20.0, k);
			Fastest = FMath::Max(Fastest, SLow.SpeedMS);
			if (Now - Low.T0 > D.FleeBackSec) { BackFastest = FMath::Max(BackFastest, (SlonkFauna::HerdFrightOffset(G, D, Low, R, Now) - SlonkFauna::HerdFrightOffset(G, D, Low, R, Now - 0.05)).Size() / 5.0); }
		}
		const double Fled = SlonkFauna::HerdFrightOffset(G, D, Low, R, Low.T0 + 30.0).Size();
		const double Back = SlonkFauna::HerdFrightOffset(G, D, Low, R, Low.T0 + D.FrightSec + 0.01).Size();
		SlonkFauna::FHerdState Calm, Again = SLow;
		SlonkFauna::FPanic NoPanic;
		SlonkFauna::StartHerd(Calm, G, Low.T0 + D.FrightSec + 5.0);
		Step(Calm, NoPanic, Low.T0 + D.FrightSec + 5.0, 0.0, 0);
		Step(Again, Low, Low.T0 + D.FrightSec + 5.0, 0.0, 0);
		const double OffPath = FVector::Dist(Again.FramePos, Calm.FramePos);
		// Its gallop's cap: nine of its shortest walkers' strides a second.
		const Wk::FShape Shape = Wk::ShapeOf(G.Plan);
		const double ShortStrideM = Shape.StrideShare * FMath::Clamp(0.9 * (double)G.SpanM, Shape.MinSizeM, Shape.MaxSizeM);
		const double CapMS = SlonkFauna::HerdGallopStridesHz * ShortStrideM;
		// A lake of 100 m 220 m along each of eight ways it flees without one: it turns from it, dry.
		int32 Runs = 0, Wet = 0, WetBlind = 0;
		double Worst = 1.0e12;
		{
			struct FLake { FVector At = FVector::ZeroVector; double RadiusCm = 0.0; };
			auto Facing = [](const SlonkFauna::FHerdState& S) { return S.FrameRot.GetAxisX() * FMath::Cos(S.Heading) + S.FrameRot.GetAxisY() * FMath::Sin(S.Heading); };
			auto CraftAt = [&](const SlonkFauna::FHerdState& S, double Bearing)
			{
				const FVector Up = S.FramePos.GetSafeNormal();
				const FVector F = Facing(S);
				const FVector Way = F * FMath::Cos(Bearing) + FVector::CrossProduct(Up, F) * FMath::Sin(Bearing);
				SlonkFauna::FFaunaThreat Craft;
				Craft.PosCm = S.FramePos + Way * 5000.0 + Up * 3000.0;
				Craft.VelCmS = -Way * 8000.0;
				Craft.AltitudeCm = 3000.0;
				return Craft;
			};
			const double GrazeAt = 3.0 * G.CycleSec + G.WalkB + 5.0;
			auto Flee = [&](double Bearing, const TArray<FLake>& Lakes, SlonkFauna::FHerdWater* Water, SlonkFauna::FPanic& OutP, FVector& OutAt)
			{
				SlonkFauna::FHerdState S;
				SlonkFauna::FPanic P;
				SlonkFauna::StartHerd(S, G, GrazeAt);
				double Gap = 1.0e12;
				for (int32 k = 0; ; ++k)
				{
					const double Now = GrazeAt + k / 10.0;
					if (k == 1) { SlonkFauna::StartleHerd(P, S, G, D, R, CraftAt(S, Bearing), Now, true, Water); OutP = P; OutAt = S.FramePos; }
					const FVector Ahead = SlonkFauna::HerdFrightOffset(G, D, P, R, Now + 1.0);
					SlonkFauna::StepHerd(S, G, R, Now, k == 0 ? 0.0 : 0.1, SlonkFauna::HerdFrightOffset(G, D, P, R, Now), P.IsLive(Now), false, k % 4 == 0, Height, 0.0, nullptr, &Ahead);
					if (k > 0)
					{
						for (const FLake& L : Lakes) { Gap = FMath::Min(Gap, FVector::Dist(S.FramePos.GetSafeNormal() * R, L.At) - L.RadiusCm - G.HerdRadiusCm); }
					}
					if (k > 2 && Now > P.T0 + P.LiveSec + 1.0) { break; }
				}
				return Gap;
			};
			for (int32 b = 0; b < 8; ++b)
			{
				const double Bearing = UE_DOUBLE_TWO_PI * b / 8.0;
				SlonkFauna::FPanic Blind;
				FVector From;
				Flee(Bearing, TArray<FLake>(), nullptr, Blind, From);
				const FVector Up = From.GetSafeNormal();
				const FVector Way = (Blind.Dir - Up * FVector::DotProduct(Blind.Dir, Up)).GetSafeNormal();
				TArray<FLake> Lakes;
				Lakes.Add({ (From + Way * 2.2e4).GetSafeNormal() * R, 1.0e4 });
				SlonkFauna::FHerdWater Water([&](const FVector& Dir) { for (const FLake& L : Lakes) { if (FVector::Dist(Dir * R, L.At) < L.RadiusCm) { return true; } } return false; }, R);
				SlonkFauna::FPanic Chose, Into;
				FVector Ignore;
				const double Dry = Flee(Bearing, Lakes, &Water, Chose, Ignore);
				const double Blinded = Flee(Bearing, Lakes, nullptr, Into, Ignore);
				++Runs;
				Wet += Dry < 0.0 ? 1 : 0;
				WetBlind += Blinded < 0.0 ? 1 : 0;
				Worst = FMath::Min(Worst, Dry);
			}
		}
		// Someone walking straight at it from 120 m off its edge at 1.4 m/s for a minute and a half.
		double MinGap = TNumericLimits<double>::Max();
		int32 CrewStartles = 0;
		{
			SlonkFauna::FHerdState Kept;
			SlonkFauna::FPanic KeptPanic;
			SlonkFauna::StartHerd(Kept, G, T0);
			Step(Kept, KeptPanic, T0, 0.0, 0);
			const FVector Side = Kept.FrameRot.GetAxisY();
			FVector Crew = Kept.FramePos + Side * (G.HerdRadiusCm + 12000.0);
			for (int32 k = 1; k <= 20 * 90; ++k)
			{
				const double Now = T0 + k / 20.0;
				Crew -= Side * (140.0 / 20.0);
				SlonkFauna::FFaunaThreat Foot;
				Foot.bCraft = false;
				Foot.PosCm = Crew;
				CrewStartles += SlonkFauna::StartleHerd(KeptPanic, Kept, G, D, R, Foot, Now) ? 1 : 0;
				const bool bMoving = SlonkFauna::KeepFromCrew(Kept, G, D, TArray<FVector>{ Crew }, 1.0 / 20.0);
				const FVector Ahead = SlonkFauna::HerdFrightOffset(G, D, KeptPanic, R, Now + 1.0) + Kept.Avoid + (bMoving ? Kept.AvoidWay : FVector::ZeroVector);
				SlonkFauna::StepHerd(Kept, G, R, Now, 1.0 / 20.0, SlonkFauna::HerdFrightOffset(G, D, KeptPanic, R, Now) + Kept.Avoid, KeptPanic.IsLive(Now), bMoving, k % 4 == 0, Height, 0.0, nullptr, &Ahead);
				if (k > 20 * 30) { MinGap = FMath::Min(MinGap, SlonkFauna::CrewGapCm(G, Kept.FramePos, Crew)); }
			}
		}
		// Its dust: stepped from grazing through its walk into its trot, by day, 300 m off.
		float GrazeDust = 0.f, TrotDust = 0.f, PuffSize = 0.f;
		{
			SlonkFauna::FHerdState S;
			SlonkFauna::FDustGate Gate;
			const double Start = 3.0 * G.CycleSec + G.WalkA - 30.0;
			SlonkFauna::StartHerd(S, G, Start);
			for (int32 f = 0; f <= 60 * FMath::CeilToInt32(G.TrotB - G.WalkA + 30.0); f += 2)
			{
				const double Now = Start + f / 60.0;
				SlonkFauna::StepHerd(S, G, R, Now, f == 0 ? 0.0 : 2.0 / 60.0, FVector::ZeroVector, false, false, f % 12 == 0, Height, 0.0, nullptr);
				SlonkFauna::StepDustGate(Gate, S.SpeedMS, f == 0 ? 0.0 : 2.0 / 60.0, Now);
				SlonkFauna::FHerdDustInput In;
				In.SpeedMS = Gate.SpeedMS;
				In.Wet = 0.4;
				In.Life = 1.0;
				In.Night = 0.0;
				In.DistCm = 3.0e4;
				In.Members = G.Members;
				In.SpanM = G.SpanM;
				In.RadiusCm = G.HerdRadiusCm;
				const SlonkFauna::FHerdDust Dust = SlonkFauna::HerdDustFor(In);
				if (Now - Start < 25.0) { GrazeDust = FMath::Max(GrazeDust, Dust.Rate); }
				if (Now > 3.0 * G.CycleSec + G.TrotA + G.RampSec + 1.0 && Now < 3.0 * G.CycleSec + G.TrotB - G.RampSec) { TrotDust = FMath::Max(TrotDust, Dust.Rate); PuffSize = FMath::Max(PuffSize, Dust.Size); }
			}
		}
		SlonkFauna::FHerdDustInput EarthIn;
		EarthIn.SpeedMS = 3.0;
		EarthIn.Members = G.Members;
		EarthIn.SpanM = 1.95;
		EarthIn.DistCm = 3.0e4;
		const float EarthPuff = SlonkFauna::HerdDustFor(EarthIn).Size;
		AddInfo(FString::Printf(TEXT("%s: a pass 30 m up startles it %d times, 300 m up %d; it gallops off at up to %.1f m/s (nine of its shortest walkers' strides a second %.1f m/s), %.0f m off its path 30 s on, comes back at up to %.2f m/s (its trot %.1f), %.2f m off it at its fright's end (%.2f cm from a herd never startled); with a lake each of 8 ways it fled, %d into it (its walkers %.0f m from the shore at the nearest), %d without the water read; someone walking at it kept %.0f m off (startled %d times); dust grazing %.1f, at its trot %.1f puffs a second, %.0f cm across (an Earth herd's %.0f)"),
			*Case.Name, LowStartles, HighStartles, Fastest, CapMS, Fled / 100.0, BackFastest, G.TrotCmS / 100.0, Back / 100.0, OffPath, Wet, Worst / 100.0, WetBlind, MinGap / 100.0, CrewStartles, GrazeDust, TrotDust,
			PuffSize, EarthPuff));
		TestTrue(TEXT("a craft passing low startles the herd, one high does not"), LowStartles >= 1 && HighStartles == 0 && Fled > 1.5e4);
		TestTrue(TEXT("back on its path by the end of its fright, exactly where it would have been"), Back == 0.0 && OffPath < 0.01);
		TestTrue(TEXT("at its gallop, no faster than nine of its shortest strides a second"), Fastest > 4.0 && Fastest <= CapMS * 1.15 + 0.5 && Fastest < 16.0);
		TestTrue(TEXT("coming back no faster than its own trot"), BackFastest > 0.3 && BackFastest * 100.0 < G.TrotCmS);
		TestTrue(TEXT("no flight into a lake, as each would have gone in without the water read"), Runs == 8 && Wet == 0 && WetBlind == Runs);
		TestTrue(TEXT("it keeps 40 to 60 m from someone walking at it, unstartled"), MinGap > 3500.0 && MinGap < 6500.0 && CrewStartles == 0);
		TestTrue(TEXT("dust at its trot, none grazing, its puffs its walkers' size"), GrazeDust == 0.f && TrotDust > 0.f && (G.SpanM > 1.95f ? PuffSize >= EarthPuff : PuffSize > 0.f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaAlienHerdSpacingTest, "Slonk.Fauna.AlienHerdSpacing", SLONK_TEST_FLAGS)
bool FSlonkFaunaAlienHerdSpacingTest::RunTest(const FString& Parameters)
{
	// No two alien walkers of a herd ever meet: grazing round their circles a stride a step through a whole hour (and so
	// walking with the herd, their circles held), the middles of any two stay at least the longer one's footprint apart
	// (its length times its form's reach: an insectoid's feet splay out past its length). Of each form, its tightest herds.
	// And the herds of every species keep to their budget: about the densest places of a strange world of three species, at
	// most 16 herds and 960 walkers, at most 180 triangles a walker at LOD 0 (172,800 in all, inside the quarter of a
	// million the fauna's worst case allows); a frame's work on 16 alien herds well inside 0.15 ms of the game thread; and
	// the world builds a mesh for each species, never one for each herd.
	int32 Close = 0, Walkers = 0, Tried = 0;
	double Closest = TNumericLimits<double>::Max();
	FString Forms;
	const FLinearColor Rigid(SlonkFauna::Vc::SurfaceBody, 0.f, SlonkFauna::Vc::SideMiddle, SlonkFauna::Vc::JointRigid);
	for (int32 Form = 0; Form < SlonkFauna::AlienForms(EFaunaKind::Herd); ++Form)
	{
		for (const float At : { 0.f, 1.f })
		{
			float Scale = 1.f;
			const FFaunaBodyPlan Plan = AlienHerdPlan(Form, At, 0.5f, &Scale);
			const Wk::FShape Shape = Wk::ShapeOf(Plan);
			const SlonkFauna::FFaunaWorld W = AlienHerdWorld(Plan, Scale);
			TArray<SlonkFauna::FFaunaGroup> Herds;
			HerdsOverGlobe(W, 8000, Herds);
			Herds.Sort([](const SlonkFauna::FFaunaGroup& A, const SlonkFauna::FFaunaGroup& B) { return A.Members / FMath::Square(A.SpacingM) > B.Members / FMath::Square(B.SpacingM); });
			double FormClosest = TNumericLimits<double>::Max();
			for (int32 h = 0; h < FMath::Min(Herds.Num(), 3); ++h)
			{
				const SlonkFauna::FFaunaGroup& G = Herds[h];
				++Tried;
				TArray<FTransform> Xf;
				TArray<float> Cu;
				SlonkFauna::MembersOf(G, Xf, Cu);
				const int32 N = Xf.Num();
				Walkers += N;
				TArray<FVector3f> At3;
				At3.SetNum(N);
				for (int32 s = 0; s < 3600; s += 2)
				{
					float Gd[SlonkFauna::HerdData::Num] = {};
					Gd[SlonkFauna::HerdData::GrazeSec] = (float)s;
					Gd[SlonkFauna::HerdData::GrazeRef] = (float)s - 900.f;
					Gd[SlonkFauna::HerdData::PivotRef] = -1000.f;
					for (int32 m = 0; m < N; ++m)
					{
						const float* C = Cu.GetData() + m * SlonkFauna::Custom::Num;
						const FVector3f Home = (FVector3f)Xf[m].GetTranslation();
						At3[m] = Home + SlonkFauna::WalkerMotion(FVector3f::ZeroVector, Home, Rigid, 0.f, C, Gd, 2000.f, 960.f, Shape.Rig) * C[SlonkFauna::Custom::SizeM];
					}
					for (int32 i = 0; i < N; ++i)
					{
						for (int32 j = i + 1; j < N; ++j)
						{
							const double Longer = 100.0 * Shape.Reach * FMath::Max(Cu[i * SlonkFauna::Custom::Num + SlonkFauna::Custom::SizeM], Cu[j * SlonkFauna::Custom::Num + SlonkFauna::Custom::SizeM]);
							const double Ratio = FVector3f::Dist2D(At3[i], At3[j]) / Longer;
							FormClosest = FMath::Min(FormClosest, Ratio);
							Close += Ratio < 1.0 ? 1 : 0;
						}
					}
				}
			}
			Closest = FMath::Min(Closest, FormClosest);
			Forms += FString::Printf(TEXT("%s%s (reach %.2f, %d herds, the tightest %d walkers of %.2f m): %.2f"), Forms.IsEmpty() ? TEXT("") : TEXT("; "), FormName(Shape.Form), Shape.Reach, Herds.Num(),
				Herds.Num() > 0 ? Herds[0].Members : 0, Herds.Num() > 0 ? Herds[0].SpanM : 0.f, FormClosest);
		}
	}
	AddInfo(FString::Printf(TEXT("the tightest alien herds (%d, %d walkers) grazing through an hour, two walkers' middles at least so many of the longer one's footprints apart: %s; %d times a pair closer"), Tried, Walkers, *Forms, Close));
	TestTrue(TEXT("herds to try"), Tried >= 8);
	TestEqual(TEXT("no two walkers ever closer than the longer one's footprint"), Close, 0);

	// The budget, about the densest places of a strange world of three herd species of both forms.
	SlonkFauna::FFaunaWorld W;
	if (!StrangeHerds(0.9f, 3, true, W)) { AddError(TEXT("no world of three herd species of both forms")); return false; }
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Herd);
	TArray<SlonkFauna::FFaunaGroup> Herds;
	HerdsOverGlobe(W, 12000, Herds);
	if (Herds.Num() == 0) { AddError(TEXT("no herds on the strange world")); return false; }
	SlonkFauna::FFaunaWorld Dense = W;
	Dense.Density = 2.f;
	int32 MaxGroups = 0, MaxMembers = 0, Over = 0, Places = 0, MaxTris = 0;
	TSet<UStaticMesh*> Meshes;
	for (int32 i = 0; i < Herds.Num() && Places < 10; i += FMath::Max(Herds.Num() / 10, 1), ++Places)
	{
		SlonkFauna::FFaunaField F;
		F.SetWorld(Dense);
		TArray<uint64> Active;
		ScanAndSelect(F, CameraOver(Dense, Herds[i].Centre), Active);
		MaxGroups = FMath::Max(MaxGroups, F.ActiveGroups(EFaunaKind::Herd));
		MaxMembers = FMath::Max(MaxMembers, F.ActiveMembers(EFaunaKind::Herd));
		Over += F.ActiveGroups(EFaunaKind::Herd) <= D.MaxGroups && F.ActiveMembers(EFaunaKind::Herd) <= D.MaxMembers ? 0 : 1;
		int32 Tris = 0;
		for (const TPair<uint64, SlonkFauna::FFaunaField::FGroupRef>& A : F.GetActive())
		{
			UE::Geometry::FDynamicMesh3 M;
			SlonkFaunaMeshes::BuildLod(A.Value->Plan, 0, M);
			Tris += A.Value->Members * M.TriangleCount();
			Meshes.Add(SlonkFaunaMeshes::GetMesh(A.Value->Plan, EFaunaKind::Herd));
		}
		MaxTris = FMath::Max(MaxTris, Tris);
	}
	// A frame's work on 16 alien herds (of the world's insectoids' or six-legged grazers', the first found).
	const SlonkFauna::FFaunaGroup* Alien = Herds.FindByPredicate([](const SlonkFauna::FFaunaGroup& G) { return !SlonkFauna::IsEarthPlan(EFaunaKind::Herd, G.Plan); });
	double FrameMs = 1.0e9;
	if (Alien)
	{
		const SlonkFauna::FFaunaGroup& G = *Alien;
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
				SlonkFauna::StartleHerd(Panics[h], S, G, D, R, Craft, Now);
				SlonkFauna::StartleHerd(Panics[h], S, G, D, R, Walker, Now);
				const bool bMoving = SlonkFauna::KeepFromCrew(S, G, D, TArray<FVector>{ Walker.PosCm }, 1.0 / 60.0);
				const FVector Ahead = SlonkFauna::HerdFrightOffset(G, D, Panics[h], R, Now + 1.0) + S.Avoid + (bMoving ? S.AvoidWay : FVector::ZeroVector);
				float Gd[SlonkFauna::HerdData::Num] = {};
				SlonkFauna::StepHerd(S, G, R, Now, f == 0 ? 0.0 : 1.0 / 60.0, SlonkFauna::HerdFrightOffset(G, D, Panics[h], R, Now) + S.Avoid, Panics[h].IsLive(Now), bMoving,
					h < 2 || (f + h) % 6 == 0, At, 0.0, Gd, &Ahead);
				SlonkFauna::HerdFrightData(Panics[h], Now, 0.0, Gd);
				Sink += Gd[SlonkFauna::HerdData::DistM] + S.FramePos.X;
			}
		};
		double Sink = 0.0;
		for (int32 f = 0; f < 120; ++f) { Frame(f, Sink); }
		const double Start = FPlatformTime::Seconds();
		for (int32 f = 120; f < 720; ++f) { Frame(f, Sink); }
		FrameMs = (FPlatformTime::Seconds() - Start) * 1000.0 / 600.0;
		TestTrue(TEXT("the frames' places all found"), FMath::IsFinite(Sink));
	}
	// The worst the caps allow: every walker about of the plan of the most triangles at LOD 0.
	int32 MostPlanTris = 0;
	for (const FAlienPlan& A : AlienHerdPlans())
	{
		UE::Geometry::FDynamicMesh3 M;
		SlonkFaunaMeshes::BuildLod(A.Plan, 0, M);
		MostPlanTris = FMath::Max(MostPlanTris, M.TriangleCount());
	}
	const int32 WorstCase = D.MaxMembers * MostPlanTris;
	AddInfo(FString::Printf(TEXT("about %d of the densest places of a strange world of %d herd species: at most %d herds and %d walkers (%d triangles at LOD 0), %d over the caps, %d meshes among them; at the caps (%d walkers) of the plan of the most triangles (%d) %d triangles; a frame's work on 16 alien herds %.4f ms"),
		Places, W.Profile.Species[(int32)EFaunaKind::Herd].Num, MaxGroups, MaxMembers, MaxTris, Over, Meshes.Num(), D.MaxMembers, MostPlanTris, WorstCase, FrameMs));
	TestTrue(TEXT("places with herds to try"), Places >= 6 && MaxGroups > 0);
	TestEqual(TEXT("never over the herds' caps"), Over, 0);
	TestTrue(TEXT("at most 172,800 triangles of walkers at LOD 0, at the caps"), MaxTris <= WorstCase && MaxTris > 0 && WorstCase <= 172800);
	TestTrue(TEXT("a mesh for each species at the most"), Meshes.Num() <= W.Profile.Species[(int32)EFaunaKind::Herd].Num && !Meshes.Contains(nullptr));
	TestTrue(TEXT("a frame's work on 16 alien herds well inside 0.15 ms"), FrameMs < 0.1);
	return true;
}

namespace
{
	/** The world "spawnplanet terrestrial strange s seed n" makes from the game's start, every kind (its genome settled as
	 *  the spawn settles it: no star's bearing, so it spins), full of life. */
	SlonkFauna::FFaunaWorld SpawnedStrange(int32 SeedPick, float S)
	{
		const uint32 Hash = PlaceHash(SpawnedPlace(SeedPick));
		FWorldGenome G = SlonkGenome::Roll(EPlanetArchetype::Terrestrial, S, Hash);
		SlonkGenome::SettleLock(G, EPlanetArchetype::Terrestrial, FVector::ZeroVector, false);
		SlonkFauna::FFaunaWorld W;
		W.P = SpawnedParams(SeedPick);
		W.Profile = SlonkFauna::Roll(G, EPlanetArchetype::Terrestrial, Hash);
		W.Life = 1.f;
		W.KindMask = SlonkFauna::KindBit(EFaunaKind::Herd);
		return W;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaAlienHerdSeedsTest, "Slonk.Fauna.AlienHerdSeeds", SLONK_TEST_FLAGS)
bool FSlonkFaunaAlienHerdSeedsTest::RunTest(const FString& Parameters)
{
	// The captures' worlds: of the seeds "spawnplanet terrestrial strange s seed n" rolls from the game's start, those whose
	// herds have a six-legged grazer species and an insectoid one at a strangeness from 0.6 to 0.9, and how far from the
	// start's place (the middle of its day side as the spawn faces it, (0.3, 0.8, 0.5) of the world) the nearest herd of each
	// is ("fauna goto herd 120 species n" finds it).
	TArray<FString> Picks;
	for (int32 Seed = 0; Seed <= 80 && Picks.Num() < 4; ++Seed)
	{
		for (const float S : { 0.7f, 0.8f, 0.9f, 0.6f })
		{
			const SlonkFauna::FFaunaWorld W = SpawnedStrange(Seed, S);
			const SlonkFauna::FFaunaKindSpecies& Sp = W.Profile.Species[(int32)EFaunaKind::Herd];
			int32 Hexa = -1, Insect = -1;
			for (int32 i = 0; i < Sp.Num; ++i)
			{
				const Wk::EForm F = Wk::ShapeOf(Sp.List[i].Plan).Form;
				if (F == Wk::EForm::Hexapod && Hexa < 0) { Hexa = i; }
				if (F == Wk::EForm::Insectoid && Insect < 0) { Insect = i; }
			}
			if (Hexa < 0 || Insect < 0) { continue; }
			const FVector Start = FVector(0.3, 0.8, 0.5).GetSafeNormal();
			SlonkFauna::FFaunaGroup Gh, Gi;
			SlonkFauna::FGroupSearch Sh, Si;
			const bool bH = SlonkFauna::FindGroupNear(W, EFaunaKind::Herd, Start, 4.0e7, 60000, Gh, &Sh, Hexa);
			const bool bI = SlonkFauna::FindGroupNear(W, EFaunaKind::Herd, Start, 4.0e7, 60000, Gi, &Si, Insect);
			if (!bH || !bI) { continue; }
			const double Rkm = W.P.RadiusCm / 1e5;
			const double Dh = FVector::Dist(Gh.Centre, Start) * Rkm, Di = FVector::Dist(Gi.Centre, Start) * Rkm;
			if (FMath::Max(Dh, Di) > 600.0) { continue; }
			Picks.Add(FString::Printf(TEXT("seed %d at strangeness %.1f: %d herd species, six-legged grazers #%d (%.0f%% of the cells; the nearest herd %.0f km off, %d of %.1f m) and insectoids #%d (%.0f%%; %.0f km off, %d of %.1f m)"),
				Seed, S, Sp.Num, Hexa, 100.f * Sp.List[Hexa].Share, Dh, Gh.Members, Gh.SpanM, Insect, 100.f * Sp.List[Insect].Share, Di, Gi.Members, Gi.SpanM));
			break;
		}
	}
	AddInfo(FString::Printf(TEXT("spawnplanet terrestrial worlds with both alien grazers near the start: %s"), *FString::Join(Picks, TEXT("; "))));
	TestTrue(TEXT("seeds for the captures"), Picks.Num() >= 2);
	return true;
}

namespace
{
	namespace Wn = SlonkFauna::Wings;
	namespace Ff = SlonkFauna::FishForm;

	/** An alien plan of a kind's form, every proportion's draw At (0, the low end of its range, to 1, the high), its
	 *  ornaments' (and a membrane's struts') OrnDraw, its glow organs' GlowDraw (under 0.4 it carries them); and its size
	 *  against the kind's own. */
	FFaunaBodyPlan AlienKindPlan(EFaunaKind K, int32 Form, float At, float OrnDraw, float GlowDraw, float* OutScale = nullptr)
	{
		float Shape[SlonkFauna::SpeciesShapeDraws];
		for (int32 j = 0; j < SlonkFauna::SpeciesShapeDraws; ++j) { Shape[j] = At; }
		Shape[5] = OrnDraw;
		Shape[6] = GlowDraw;
		float Scale = 1.f;
		const FFaunaBodyPlan P = SlonkFauna::AlienPlan(K, Form, Shape, Scale);
		if (OutScale) { *OutScale = Scale; }
		return P;
	}
	/** Every alien plan of a kind the tests run over: each form at the ends and the middle of its ranges, wearing each
	 *  ornament (and a membrane each count of struts), with glow organs and without. */
	TArray<FAlienPlan> AlienKindPlans(EFaunaKind K)
	{
		TArray<FAlienPlan> Out;
		for (int32 Form = 0; Form < SlonkFauna::AlienForms(K); ++Form)
		{
			for (const float At : { 0.f, 0.5f, 1.f })
			{
				for (const float Orn : { 0.1f, 0.2f, 0.3f, 0.45f, 0.6f, 0.7f, 0.9f })
				{
					for (const float Glow : { 0.9f, 0.2f })
					{
						FAlienPlan A;
						A.Plan = AlienKindPlan(K, Form, At, Orn, Glow, &A.Scale);
						if (!Out.ContainsByPredicate([&A](const FAlienPlan& O) { return O.Plan == A.Plan; })) { Out.Add(A); }
					}
				}
			}
		}
		return Out;
	}
	/** A world, its species of a kind one plan alone at a size against the kind's own. */
	SlonkFauna::FFaunaWorld OnePlanWorld(SlonkFauna::FFaunaWorld W, EFaunaKind K, const FFaunaBodyPlan& Plan, float Scale)
	{
		SlonkFauna::FFaunaKindSpecies& S = W.Profile.Species[(int32)K];
		S = SlonkFauna::FFaunaKindSpecies();
		S.List[0].Plan = Plan;
		S.List[0].SizeScale = Scale;
		return W;
	}
	const TCHAR* WingName(int32 Form) { return Form == Wn::Four ? TEXT("four-winged") : (Form == Wn::Membrane ? TEXT("membrane-winged") : TEXT("bird")); }
	const TCHAR* FishName(int32 Form) { return Form == Ff::Eel ? TEXT("eel") : (Form == Ff::Spinefin ? TEXT("spinefin") : TEXT("Earth fish")); }

	/** How many triangles an alien flier's plan has at a LOD (FaunaMeshes.h's counts). */
	int32 AlienFlierTris(const FFaunaBodyPlan& P, int32 Lod)
	{
		if (Lod >= 2) { return 6; }
		if (Wn::FormOf(P) == Wn::Four) { return Lod == 0 ? 92 + ((P.Ornament & 1) ? 2 : 0) + ((P.Ornament & 2) ? 2 : 0) : 40; }
		const int32 N = FMath::Clamp((int32)P.LimbPairs, 3, 4);
		return Lod == 0 ? 32 + 16 * N + 2 + ((P.Ornament & 2) ? 2 : 0) + ((P.Ornament & 1) ? 1 : 0) : 16 + 8 * N + 2;
	}
	/** How many an alien fish's has. */
	int32 AlienFishTris(const FFaunaBodyPlan& P, int32 Lod)
	{
		if (Ff::FormOf(P) == Ff::Eel) { return Lod == 0 ? 82 + (P.Ornament ? 2 : 0) : (Lod == 1 ? 38 : 3); }
		return Lod == 0 ? 63 + (P.Ornament ? 3 : 0) : (Lod == 1 ? 21 : 5);
	}

	/** Every vertex of a static mesh's LODs with a tangent basis at right angles to its normal, of how many. */
	void TangentBasis(UStaticMesh* Mesh, int32& OutVertices, int32& OutWithout)
	{
		if (!Mesh) { return; }
		if (const FStaticMeshRenderData* RD = Mesh->GetRenderData())
		{
			for (const FStaticMeshLODResources& LR : RD->LODResources)
			{
				const FStaticMeshVertexBuffer& VB = LR.VertexBuffers.StaticMeshVertexBuffer;
				for (uint32 i = 0; i < VB.GetNumVertices(); ++i)
				{
					const FVector3f Tx(VB.VertexTangentX(i)), Tz(VB.VertexTangentZ(i)), Ty(VB.VertexTangentY(i));
					++OutVertices;
					OutWithout += Tx.Size() > 0.5f && Ty.Size() > 0.5f && FMath::Abs(FVector3f::DotProduct(Tx.GetSafeNormal(), Tz.GetSafeNormal())) < 0.1f ? 0 : 1;
				}
			}
		}
	}
	/** How many faces of a mesh are wound to show the side away from where their normals point. */
	int32 InwardFaces(const UE::Geometry::FDynamicMesh3& M)
	{
		const UE::Geometry::FDynamicMeshNormalOverlay* N = M.Attributes()->PrimaryNormals();
		int32 Bad = 0;
		for (const int32 T : M.TriangleIndicesItr())
		{
			FVector3d Fn, Centroid; double Area;
			M.GetTriInfo(T, Fn, Area, Centroid);
			const UE::Geometry::FIndex3i E = N->GetTriangle(T);
			Bad += Fn.Dot(FVector3d(N->GetElement(E.A) + N->GetElement(E.B) + N->GetElement(E.C))) > 0.0 ? 0 : 1;
		}
		return Bad;
	}
	/** Where a mesh's LOD 1 and LOD 2 come in, in pixels across for a span or length of SizeM through a 90-degree view at
	 *  16:9, 960 pixels a radian (the GPU's radius, its padded box's). */
	FVector2D LodPixels(UStaticMesh* Mesh, const SlonkFauna::FFaunaKindDesc& D, double SizeM)
	{
		UPlanetInstancesComponent* Set = NewObject<UPlanetInstancesComponent>();
		Set->SetStaticMesh(Mesh);
		Set->SetCullPadding(D.CullPaddingUnits);
		Set->SetPieces(TArray<FTransform>{ FTransform(FQuat::Identity, FVector::ZeroVector, FVector(SizeM)) }, TConstArrayView<float>());
		const double Proj = (16.0 / 9.0) / FMath::Tan(FMath::DegreesToRadians(45.0));
		const double GpuR = Set->GetMaxInstanceRadius();
		return FVector2D(SizeM * 960.0 / (Proj * GpuR / D.LodScreenSize[1] / 100.0), SizeM * 960.0 / (Proj * GpuR / D.LodScreenSize[2] / 100.0));
	}

	/** How much of a mesh's glow organs are seen from far off straight along a view: on a grid of samples Cell unit-mesh cm
	 *  apart over the plane square to the view, at the nearest drawn point of each, the share of a spot covering it
	 *  (SlonkFauna::GlowSpotAt); unit-mesh cm squared. And how much of the sky it covers. */
	FVector2D SeeGlow(const UE::Geometry::FDynamicMesh3& M, const FVector& View, const SlonkFauna::GlowOrgan::FRow& Row, double Cell = 0.4)
	{
		const FVector Fwd = View.GetSafeNormal();
		const FVector Right = FVector::CrossProduct(FMath::Abs(Fwd.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector, Fwd).GetSafeNormal();
		const FVector Up = FVector::CrossProduct(Fwd, Right);
		constexpr double Half = 90.0;
		const int32 N = FMath::CeilToInt32(2.0 * Half / Cell);
		TArray<float> Depth, Light;
		Depth.Init(TNumericLimits<float>::Max(), N * N);
		Light.Init(0.f, N * N);
		const TArray<FLinearColor> Vcs = VertexColours(M);
		const TArray<FVector2f> Uvs = VertexUVs(M);
		for (const int32 T : M.TriangleIndicesItr())
		{
			const UE::Geometry::FIndex3i Tri = M.GetTriangle(T);
			FVector2D S[3];
			double Z[3];
			for (int32 k = 0; k < 3; ++k)
			{
				const FVector P = (FVector)M.GetVertex(Tri[k]);
				S[k] = FVector2D(FVector::DotProduct(P, Right), FVector::DotProduct(P, Up));
				Z[k] = FVector::DotProduct(P, Fwd);
			}
			const double Area2 = (S[1].X - S[0].X) * (S[2].Y - S[0].Y) - (S[2].X - S[0].X) * (S[1].Y - S[0].Y);
			if (FMath::Abs(Area2) < 1e-9) { continue; }
			const int32 X0 = FMath::Clamp(FMath::FloorToInt32((FMath::Min3(S[0].X, S[1].X, S[2].X) + Half) / Cell), 0, N - 1);
			const int32 X1 = FMath::Clamp(FMath::FloorToInt32((FMath::Max3(S[0].X, S[1].X, S[2].X) + Half) / Cell), 0, N - 1);
			const int32 Y0 = FMath::Clamp(FMath::FloorToInt32((FMath::Min3(S[0].Y, S[1].Y, S[2].Y) + Half) / Cell), 0, N - 1);
			const int32 Y1 = FMath::Clamp(FMath::FloorToInt32((FMath::Max3(S[0].Y, S[1].Y, S[2].Y) + Half) / Cell), 0, N - 1);
			for (int32 y = Y0; y <= Y1; ++y)
			{
				for (int32 x = X0; x <= X1; ++x)
				{
					const FVector2D Q(-Half + (x + 0.5) * Cell, -Half + (y + 0.5) * Cell);
					const double W0 = ((S[1].X - Q.X) * (S[2].Y - Q.Y) - (S[2].X - Q.X) * (S[1].Y - Q.Y)) / Area2;
					const double W1 = ((S[2].X - Q.X) * (S[0].Y - Q.Y) - (S[0].X - Q.X) * (S[2].Y - Q.Y)) / Area2;
					const double W2 = 1.0 - W0 - W1;
					if (W0 < 0.0 || W1 < 0.0 || W2 < 0.0) { continue; }
					const int32 I = y * N + x;
					const float D = (float)(W0 * Z[0] + W1 * Z[1] + W2 * Z[2]);
					if (D >= Depth[I]) { continue; }
					const FVector2f Uv = Uvs[Tri.A] * (float)W0 + Uvs[Tri.B] * (float)W1 + Uvs[Tri.C] * (float)W2;
					const FLinearColor Vc = Vcs[Tri.A] * (float)W0 + Vcs[Tri.B] * (float)W1 + Vcs[Tri.C] * (float)W2;
					Depth[I] = D;
					Light[I] = SlonkFauna::GlowSpotAt(Uv, Vc, Row);
				}
			}
		}
		FVector2D Out(0.0, 0.0);
		for (int32 I = 0; I < N * N; ++I)
		{
			if (Depth[I] == TNumericLimits<float>::Max()) { continue; }
			Out.X += Cell * Cell * Light[I];
			Out.Y += Cell * Cell;
		}
		return Out;
	}
	/** The view from the air a ship's camera at 30 to 300 m has of an animal: 35 degrees down, off its quarter. */
	FVector AirView() { return FVector(FMath::Cos(FMath::DegreesToRadians(35.0)) * FVector(0.7071, 0.7071, 0.0) - FVector(0.0, 0.0, FMath::Sin(FMath::DegreesToRadians(35.0)))).GetSafeNormal(); }

	/** A flier's moved vertex as the material moves it, for a plan's form, in a random case of Slonk.Fauna.Meshes' kind. */
	struct FFlierCase
	{
		float Cu[SlonkFauna::Custom::Num] = {};
		float Gd[SlonkFauna::GroupData::Num] = {};
		float T = 0.f, Dist = 1000.f;
	};
	FFlierCase RandomFlierCase(FRandomStream& Rng, int32 Case, float SizeLo, float SizeHi)
	{
		FFlierCase M;
		const float Size = Rng.FRandRange(SizeLo, SizeHi);
		M.Cu[SlonkFauna::Custom::SizeM] = Size;
		M.Cu[SlonkFauna::Custom::Seed] = Rng.FRand();
		M.Cu[SlonkFauna::Custom::Phase] = Rng.FRand();
		M.Cu[SlonkFauna::Custom::RateHz] = FMath::RoundToFloat(Rng.FRandRange(1.5f, 14.f) * 60.f) / 60.f;
		M.Cu[SlonkFauna::Custom::PerKind] = Rng.FRandRange(-3.2f, 3.2f);
		const FVector Fled = Rng.GetUnitVector();
		M.Gd[SlonkFauna::GroupData::ScatterDirX] = (float)Fled.X;
		M.Gd[SlonkFauna::GroupData::ScatterDirY] = (float)Fled.Y;
		M.Gd[SlonkFauna::GroupData::ScatterDirZ] = (float)Fled.Z;
		M.Gd[SlonkFauna::GroupData::Bank] = Rng.FRandRange(-0.8f, 0.8f);
		M.Gd[SlonkFauna::GroupData::Startled] = Case % 2 == 0 ? 1.f : 0.f;
		M.Gd[SlonkFauna::GroupData::SpreadM] = Rng.FRandRange(0.f, 12.f);
		M.T = Rng.FRandRange(0.f, 3600.f);
		M.Gd[SlonkFauna::GroupData::ScatterT0] = FMath::Fmod(M.T - Rng.FRandRange(0.f, 60.f) + 3600.f, 3600.f);
		const float PanicAge = Rng.FRandRange(0.f, 120.f);
		M.Gd[SlonkFauna::GroupData::PanicT0] = FMath::Fmod(M.T - PanicAge + 3600.f, 3600.f);
		M.Gd[SlonkFauna::GroupData::OnsetT0] = FMath::Fmod(M.T - PanicAge - Rng.FRandRange(0.f, 1.f) + 3600.f, 3600.f);
		// Its flock's number for its darts, and its span: a twelfth over its own, as its smallest member of a flock's is.
		M.Gd[SlonkFauna::GroupData::DartSeed] = M.Cu[SlonkFauna::Custom::Seed];
		M.Gd[SlonkFauna::GroupData::DartSpanM] = Size / 0.92f;
		// Near (no speck floor), at a few hundred metres, and far (four times its size), and between.
		M.Dist = Case % 4 == 0 ? 1000.f : (Case % 4 == 1 ? Size * 100.f * 960.f / 1.f : (Case % 4 == 2 ? 5.0e5f : Size * 100.f * 960.f / Rng.FRandRange(0.5f, 2.f)));
		return M;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaAlienFliersTest, "Slonk.Fauna.AlienFliers", SLONK_TEST_FLAGS)
bool FSlonkFaunaAlienFliersTest::RunTest(const FString& Parameters)
{
	// The strange worlds' fliers, every plan at the ends and the middle of its ranges, wearing every ornament (a membrane
	// each count of struts), with glow organs and without: a four-winged flier of 92 to 96, 40 and 6 triangles, a
	// membrane-winged one of 82 to 101, 42 to 50 and 6; a metre across its wings; its vertex colours as the material reads
	// them (each wing's flex from 0 on its hinge 4 cm out to 1 at its tip, its side by its sign, a four-winged one's hind
	// wings' U from 2, a membrane's V out along its struts; the body rigid; a membrane's tail flexing from its root); every
	// face wound to show the side its normals point to; a tangent basis at every vertex; the material's motion (its plan's
	// wings, darts and waves) keeping every vertex in the instance's padded box at every size, beat, bank, startle and speck
	// floor, a wing's root on its hinge, its two sides mirrored; its LODs coming in about 40 and 4 pixels across, as a
	// bird's. A four-winged one's hind wings beat 0.3 of a beat behind its fore; it holds its place in its flock and darts
	// over the last fifth of each of its own cycles, a whole number an hour; a membrane's wave runs out along it, none at
	// its hinge, while it beats and as it glides. From below, from the side and from the air they read as other animals
	// than the Earth's bird and each other: two pairs of wings over a long body, broad wings scalloped between their struts.
	using namespace UE::Geometry;
	namespace Vc = SlonkFauna::Vc;
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Flier);
	const TArray<FAlienPlan> Plans = AlienKindPlans(EFaunaKind::Flier);
	int32 BadTris = 0, BadColour = 0, BadWinding = 0, Hinges = 0, Tips = 0, HindWings = 0, Escaped = 0, Tried = 0, Detached = 0, Unmirrored = 0, NoTangent = 0, Vertices = 0, BadLod = 0;
	int32 Forms[3] = {}, Struts[5] = {};
	double Margin = TNumericLimits<double>::Max(), WorstMirror = 0.0, MostTris = 0.0, Px1Lo = 1e9, Px1Hi = 0.0, Px2Lo = 1e9, Px2Hi = 0.0;
	FString Seen;
	for (int32 p = 0; p < Plans.Num(); ++p)
	{
		const FFaunaBodyPlan& Plan = Plans[p].Plan;
		const int32 Form = Wn::FormOf(Plan);
		++Forms[Form];
		if (Form == Wn::Membrane) { ++Struts[FMath::Clamp((int32)Plan.LimbPairs, 0, 4)]; }
		FBox Box0(ForceInit);
		FDynamicMesh3 Lod0;
		for (int32 L = 0; L < SlonkFaunaMeshes::NumLods; ++L)
		{
			FDynamicMesh3 M;
			if (!SlonkFaunaMeshes::BuildLod(Plan, L, M)) { AddError(TEXT("no alien flier built")); return false; }
			BadTris += M.TriangleCount() == AlienFlierTris(Plan, L) ? 0 : 1;
			MostTris = L == 0 ? FMath::Max(MostTris, (double)M.TriangleCount()) : MostTris;
			BadWinding += InwardFaces(M);
			const FDynamicMeshColorOverlay* C = M.Attributes()->PrimaryColors();
			const FDynamicMeshUVOverlay* U = M.Attributes()->PrimaryUV();
			for (const int32 T : M.TriangleIndicesItr())
			{
				const FIndex3i Ce = C->GetTriangle(T), Ue = U->GetTriangle(T), Tv = M.GetTriangle(T);
				for (int32 k = 0; k < 3; ++k)
				{
					const FVector4f Col = C->GetElement(Ce[k]);
					const FVector2f Uv = U->GetElement(Ue[k]);
					const FVector3d P = M.GetVertex(Tv[k]);
					if (L == 0) { Box0 += (FVector)P; }
					bool bOk = true;
					if (FMath::IsNearlyEqual(Col.W, Vc::JointWing))
					{
						const double Want = FMath::Clamp((FMath::Abs(P.Y) - Vc::WingRootCm) / (50.0 - Vc::WingRootCm), 0.0, 1.0);
						const bool bHind = Uv.X > 1.5f;
						bOk = Col.X == Vc::SurfaceWing && FMath::IsNearlyEqual((double)Col.Y, Want, 1e-3) && Col.Z == (P.Y > 0.0 ? Vc::SideRight : Vc::SideLeft)
							&& FMath::Abs(P.Y) >= Vc::WingRootCm - 1e-3 && Uv.Y >= -1e-4f
							&& (Form == Wn::Four ? (Uv.X >= (bHind ? 2.f : 0.f) - 1e-4f && Uv.X <= (bHind ? 3.f : 1.f) + 1e-4f && Uv.Y <= 1.f + 1e-4f)
								: (Uv.X >= -1e-4f && Uv.X <= 1.f + 1e-4f && Uv.Y <= (float)Plan.LimbPairs + 1e-4f));
						Hinges += FMath::IsNearlyEqual(FMath::Abs(P.Y), Vc::WingRootCm, 1e-3) && Col.Y == 0.f ? 1 : 0;
						Tips += Col.Y == 1.f ? 1 : 0;
						HindWings += bHind && L == 0 ? 1 : 0;
					}
					else if (FMath::IsNearlyEqual(Col.W, Vc::JointTail)) { bOk = Form == Wn::Membrane && Col.X == Vc::SurfaceTail && Col.Y >= 0.f && Col.Y <= 1.f && Col.Z == Vc::SideMiddle; }
					else if (Col.W == Vc::JointRigid) { bOk = (Col.X == Vc::SurfaceBody || Col.X == Vc::SurfaceBeak) && Col.Y == 0.f && Col.Z == Vc::SideMiddle; }
					else { bOk = false; }
					BadColour += bOk ? 0 : 1;
				}
			}
			if (L == 0) { Lod0 = MoveTemp(M); }
		}
		// The material's motion over LOD 0's vertices with its plan's wings, inside the box each instance is culled by; a
		// wing's root on its hinge; and, unbanked and unstartled, its left and right mirrored about its body's middle.
		const FBox Padded = Box0.ExpandBy(Box0.GetExtent() * 0.15 + FVector(20.0 + D.CullPaddingUnits));
		const TArray<FLinearColor> Vcs = VertexColours(Lod0);
		const TArray<FVector2f> Uvs = VertexUVs(Lod0);
		FRandomStream Rng(71 + p);
		const float SizeLo = Form == Wn::Four ? 0.25f : 0.38f, SizeHi = Form == Wn::Four ? 1.3f : 2.6f;
		for (int32 Case = 0; Case < 120; ++Case)
		{
			FFlierCase M = RandomFlierCase(Rng, Case, SizeLo, SizeHi);
			for (const int32 V : Lod0.VertexIndicesItr())
			{
				const FVector3f LP = (FVector3f)Lod0.GetVertex(V);
				const FVector3f Moved = LP + SlonkFauna::FlierMotion(LP, Vcs[V], M.T, M.Cu, M.Gd, M.Dist, 960.f, Uvs[V], Form);
				++Tried;
				const FVector Mv((double)Moved.X, (double)Moved.Y, (double)Moved.Z);
				Escaped += Padded.IsInside(Mv) ? 0 : 1;
				Margin = FMath::Min(Margin, FMath::Min3(FMath::Min(Mv.X - Padded.Min.X, Padded.Max.X - Mv.X), FMath::Min(Mv.Y - Padded.Min.Y, Padded.Max.Y - Mv.Y), FMath::Min(Mv.Z - Padded.Min.Z, Padded.Max.Z - Mv.Z)));
			}
			FFlierCase Level = M;
			Level.Gd[SlonkFauna::GroupData::Bank] = 0.f;
			Level.Gd[SlonkFauna::GroupData::Startled] = 0.f;
			const FVector3f Root(0.f, 4.f, 1.f);
			const FVector3f Wing = SlonkFauna::FlierMotion(Root, FLinearColor(1.f, 0.f, 1.f, 0.5f), Level.T, Level.Cu, Level.Gd, 1000.f, 960.f, FVector2f(0.f, 0.f), Form);
			const FVector3f Rigid = SlonkFauna::FlierMotion(Root, FLinearColor(0.f, 0.f, 0.5f, 0.f), Level.T, Level.Cu, Level.Gd, 1000.f, 960.f, FVector2f(0.f, 0.f), Form);
			Detached += (Wing - Rigid).Size() < 1e-3f ? 0 : 1;
			// Its middle as the material carries it (every vertex goes with it: the ripple, the dart and the scatter).
			const FVector3f Middle = SlonkFauna::FlierMotion(FVector3f::ZeroVector, FLinearColor(0.f, 0.f, 0.5f, 0.f), Level.T, Level.Cu, Level.Gd, Level.Dist, 960.f, FVector2f(0.f, 0.f), Form);
			for (const int32 V : Lod0.VertexIndicesItr())
			{
				const FVector3f LP = (FVector3f)Lod0.GetVertex(V);
				if (LP.Y <= 4.f || !FMath::IsNearlyEqual(Vcs[V].A, Vc::JointWing)) { continue; }
				const FVector3f LM(LP.X, -LP.Y, LP.Z);
				FLinearColor Cm = Vcs[V];
				Cm.B = Vc::SideLeft;
				const FVector3f A = LP + SlonkFauna::FlierMotion(LP, Vcs[V], Level.T, Level.Cu, Level.Gd, Level.Dist, 960.f, Uvs[V], Form) - Middle;
				const FVector3f B = LM + SlonkFauna::FlierMotion(LM, Cm, Level.T, Level.Cu, Level.Gd, Level.Dist, 960.f, Uvs[V], Form) - Middle;
				const float Off = FVector3f(A.X - B.X, A.Y + B.Y, A.Z - B.Z).Size();
				WorstMirror = FMath::Max(WorstMirror, (double)Off);
				Unmirrored += Off < 0.01f ? 0 : 1;
			}
		}
		UStaticMesh* Mesh = SlonkFaunaMeshes::GetMesh(Plan, EFaunaKind::Flier);
		if (!Mesh || Mesh != SlonkFaunaMeshes::GetMesh(Plan, EFaunaKind::Flier) || Mesh->GetNumLODs() != SlonkFaunaMeshes::NumLods) { ++BadLod; continue; }
		TangentBasis(Mesh, Vertices, NoTangent);
		const FVector2D Px = LodPixels(Mesh, D, Form == Wn::Four ? 0.6 : 1.2);
		Px1Lo = FMath::Min(Px1Lo, Px.X); Px1Hi = FMath::Max(Px1Hi, Px.X);
		Px2Lo = FMath::Min(Px2Lo, Px.Y); Px2Hi = FMath::Max(Px2Hi, Px.Y);
		if (p == 0 || p == Plans.Num() - 1) { Seen += FString::Printf(TEXT("%s%s: LOD 0 %.0f x %.0f x %.0f cm"), Seen.IsEmpty() ? TEXT("") : TEXT("; "), *SlonkFauna::DescribePlan(EFaunaKind::Flier, Plan), Box0.GetSize().X, Box0.GetSize().Y, Box0.GetSize().Z); }
	}
	AddInfo(FString::Printf(TEXT("%d alien flier plans (%d four-winged, %d membrane-winged: %d of 3 struts, %d of 4): %d LODs off their triangles (%.0f at LOD 0 at the most), %d vertices off the colour contract (%d on a hinge, %d at a tip, %d of hind wings at LOD 0), %d faces wound inward; %d vertices moved by the material, %d outside the box padded %.0f units (the closest %.0f inside), %d wing roots off their hinge, %d wing vertices not mirrored (%.4f at worst); %d vertices over their meshes, %d without a tangent basis, %d meshes not made; LOD 1 from %.1f to %.1f pixels across, LOD 2 from %.1f to %.1f"),
		Plans.Num(), Forms[Wn::Four], Forms[Wn::Membrane], Struts[3], Struts[4], BadTris, MostTris, BadColour, Hinges, Tips, HindWings, BadWinding, Tried, Escaped, D.CullPaddingUnits, Margin, Detached, Unmirrored, WorstMirror,
		Vertices, NoTangent, BadLod, Px1Lo, Px1Hi, Px2Lo, Px2Hi));
	AddInfo(Seen);
	TestTrue(TEXT("plans of both forms, membranes of three and four struts"), Forms[Wn::Four] >= 6 && Forms[Wn::Membrane] >= 6 && Struts[3] > 0 && Struts[4] > 0);
	TestEqual(TEXT("every LOD its triangles"), BadTris, 0);
	TestTrue(TEXT("no more than 110 triangles at LOD 0"), MostTris <= 110.0);
	TestEqual(TEXT("every vertex as the material reads it"), BadColour, 0);
	TestTrue(TEXT("wings hinged at the root, flexing to the tip, a four-winged one's hind pair told apart"), Hinges > 0 && Tips > 0 && HindWings > 0);
	TestEqual(TEXT("every face shows the side its normals point to"), BadWinding, 0);
	TestEqual(TEXT("the material keeps every vertex in its instance's box, its darts, waves and speck floor and all"), Escaped, 0);
	TestEqual(TEXT("a wing's root stays on its hinge"), Detached, 0);
	TestEqual(TEXT("its two sides mirrored"), Unmirrored, 0);
	TestTrue(TEXT("every mesh made and kept, a tangent basis at every vertex"), BadLod == 0 && Vertices > 0 && NoTangent == 0);
	TestTrue(TEXT("its LOD 1 from about 40 pixels across, its LOD 2 from about 4"), Px1Lo > 28.0 && Px1Hi < 60.0 && Px2Lo > 2.8 && Px2Hi < 6.0);

	// The four-winged flier's hind wings 0.3 of a beat behind its fore (its wings' angle about their hinge, over its flex's
	// share); its darts; and a membrane's wave.
	const FFaunaBodyPlan Four = AlienKindPlan(EFaunaKind::Flier, 0, 0.5f, 0.5f, 0.9f), Memb = AlienKindPlan(EFaunaKind::Flier, 1, 0.5f, 0.5f, 0.9f);
	double WorstLag = 0.0;
	{
		float Cu[SlonkFauna::Custom::Num] = {};
		float Gd[SlonkFauna::GroupData::Num] = {};
		Cu[SlonkFauna::Custom::SizeM] = 0.6f;
		Cu[SlonkFauna::Custom::Seed] = 0.37f;
		Cu[SlonkFauna::Custom::RateHz] = 9.f;
		auto Angle = [&](float T, float U) -> float
		{
			const FVector3f Tip(0.f, 30.f, 1.f);
			const FLinearColor Vcw(1.f, 26.f / 46.f, 1.f, 0.5f);
			const FVector3f Mid = SlonkFauna::FlierMotion(FVector3f::ZeroVector, FLinearColor(0.f, 0.f, 0.5f, 0.f), T, Cu, Gd, 1000.f, 960.f, FVector2f(U, 0.5f), Wn::Four);
			const FVector3f P = Tip + SlonkFauna::FlierMotion(Tip, Vcw, T, Cu, Gd, 1000.f, 960.f, FVector2f(U, 0.5f), Wn::Four) - Mid;
			return FMath::Atan2(P.Z - Tip.Z, P.Y - 4.f) / (0.8f + 0.4f * Vcw.G);
		};
		for (int32 k = 0; k < 200; ++k)
		{
			const float T = 100.f + 0.0071f * k;
			WorstLag = FMath::Max(WorstLag, (double)FMath::Abs(Angle(T, 2.5f) - Angle(T - 0.3f / Cu[SlonkFauna::Custom::RateHz], 0.5f)));
		}
	}
	// Its darts: still over the first four fifths of each cycle, moving over the last, never past its reach either way, the
	// same across the hour's wrap.
	int32 DartMoved = 0, DartStill = 0, DartFar = 0, DartSeam = 0;
	double MostDart = 0.0;
	{
		FRandomStream Rng(808);
		for (int32 c = 0; c < 60; ++c)
		{
			float Gd[SlonkFauna::GroupData::Num] = {};
			Gd[SlonkFauna::GroupData::DartSeed] = Rng.FRand();
			const float Seed = Gd[SlonkFauna::GroupData::DartSeed];
			const float Dn = Wn::DartLo + FMath::FloorToFloat(FMath::Frac(Seed * 5.93f) * Wn::DartSpan);
			const float Len = 3600.f / Dn, Off = FMath::Frac(Seed * 2.37f);
			const int32 Cycle = Rng.RandRange(1, (int32)Dn - 3);
			const float Start = ((float)Cycle - Off) * Len;
			const FVector3f A = SlonkFauna::FlierDart(Start + 0.05f * Len, Gd), B = SlonkFauna::FlierDart(Start + 0.75f * Len, Gd), C = SlonkFauna::FlierDart(Start + 0.99f * Len, Gd);
			DartStill += (A - B).Size() < 1e-3f ? 0 : 1;
			DartMoved += (C - B).Size() > 5.f ? 1 : 0;
			for (int32 j = 0; j < 3; ++j) { DartFar += FMath::Abs(C[j]) <= Wn::DashCm[j] + 1e-3f ? 0 : 1; MostDart = FMath::Max(MostDart, (double)FMath::Abs(C[j]) / Wn::DashCm[j]); }
			// The hour's wrap: a frame's thousandth of a second across it moves it no more than one between any two moments (a
			// dash is at most about 2,000 cm of the unit mesh a second), and where it is just before is where it is a moment
			// before the hour's turn (its cycles wrapped to the hour).
			const FVector3f Before = SlonkFauna::FlierDart(3600.f - 0.001f, Gd);
			DartSeam += (Before - SlonkFauna::FlierDart(0.f, Gd)).Size() < 5.f && (Before - SlonkFauna::FlierDart(-0.001f, Gd)).Size() < 1.f ? 0 : 1;
		}
	}
	// A membrane's wave: how far its tip's trailing edge stands off where an unwaved wing's would, over its beat and over a
	// glide; none at its hinge.
	double WaveFlap = 0.0, WaveGlide = 0.0, WaveHinge = 0.0;
	{
		float Cu[SlonkFauna::Custom::Num] = {};
		float Gd[SlonkFauna::GroupData::Num] = {};
		Cu[SlonkFauna::Custom::SizeM] = 1.2f;
		Cu[SlonkFauna::Custom::Seed] = 0.21f;
		Cu[SlonkFauna::Custom::RateHz] = 2.f;
		// Startled (beating all the time) and calm (its glide between bursts: its cycle's last share).
		const float Seed = Cu[SlonkFauna::Custom::Seed];
		const float Nb = Wn::GlideLo + FMath::FloorToFloat(FMath::Frac(Seed * 7.31f) * Wn::GlideSpan);
		for (int32 k = 0; k < 400; ++k)
		{
			const float T = 200.f + 0.013f * k;
			for (const bool bFlap : { true, false })
			{
				float G2[SlonkFauna::GroupData::Num] = {};
				if (bFlap) { G2[SlonkFauna::GroupData::Startled] = 1.f; G2[SlonkFauna::GroupData::PanicT0] = T - 5.f; G2[SlonkFauna::GroupData::OnsetT0] = T - 5.f; G2[SlonkFauna::GroupData::ScatterT0] = T - 100.f; }
				// The glide: three quarters of the way round the cycle.
				const float Tg = bFlap ? T : (FMath::FloorToFloat(T * Nb / 3600.f) + 0.9f - Seed) * 3600.f / Nb + 0.002f * k;
				for (const float Y : { 50.f, 4.f })
				{
					// The same place of its wing, turned the same about its hinge, its trailing edge (U 1) against its leading (U 0):
					// how far the wave across it lifts the one off the other.
					const FVector3f P(-10.f, Y, 1.f);
					const FLinearColor Vcw(1.f, (Y - 4.f) / 46.f, 1.f, 0.5f);
					const FVector3f Trail = SlonkFauna::FlierMotion(P, Vcw, Tg, Cu, G2, 1000.f, 960.f, FVector2f(1.f, 3.f), Wn::Membrane);
					const FVector3f Lead = SlonkFauna::FlierMotion(P, Vcw, Tg, Cu, G2, 1000.f, 960.f, FVector2f(0.f, 3.f), Wn::Membrane);
					const double Off = (Trail - Lead).Size();
					if (Y < 5.f) { WaveHinge = FMath::Max(WaveHinge, Off); }
					else if (bFlap) { WaveFlap = FMath::Max(WaveFlap, Off); }
					else { WaveGlide = FMath::Max(WaveGlide, Off); }
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("a four-winged flier's hind wings %.4f rad off its fore's 0.3 of a beat before; its darts: %d of 60 moved over a cycle's last fifth, %d not still before it, %d past their reach (%.2f of it at the most), %d seams at the hour's wrap; a membrane's wave lifting its tip's trailing edge off its leading up to %.2f cm of the unit mesh beating, %.2f gliding, at its hinge %.4f"),
		WorstLag, DartMoved, DartStill, DartFar, MostDart, DartSeam, WaveFlap, WaveGlide, WaveHinge));
	TestTrue(TEXT("its hind wings 0.3 of a beat behind its fore"), WorstLag < 2e-3);
	TestTrue(TEXT("it holds its place and darts over the last fifth of its cycle"), DartMoved >= 50 && DartStill == 0 && DartFar == 0);
	TestEqual(TEXT("no seam in its darts at the hour's wrap"), DartSeam, 0);
	TestTrue(TEXT("a membrane's wave running across it as it beats and as it glides, none at its hinge"), WaveFlap > 3.0 && WaveGlide > 0.6 && WaveGlide < WaveFlap && WaveHinge < 1e-3);

	// A membrane's struts as its pixel shader shades them (MembraneLinesAt), seen nearer and further off, from a strut ten
	// pixels wide to a twentieth of one: under the temporal upscaler's jitter (half a pixel either way, frame to frame) no
	// pixel flickers between skin and strut more than a little once a strut is narrower than a pixel (such a line is drawn
	// a pixel and a half wide and the fainter for it), and a row of pixels across the wing is as dark in all as its struts
	// are; its skin solid (little light let through it, so the sky behind does not wash it out), and nothing in the alien
	// fliers' branch dithered from frame to frame (its fade a flier at a time).
	double WorstFlicker = 0.0, LineShareLo = 1e9, LineShareHi = 0.0;
	int32 Flickers = 0;
	FString LineRows;
	for (const float VPerPx : { 0.01f, 0.03f, 0.1f, 0.3f, 1.f, 2.f })
	{
		const int32 Pixels = FMath::Max(64, FMath::CeilToInt32(6.f / VPerPx));
		double Sum = 0.0, RowFlicker = 0.0;
		for (int32 x = 0; x < Pixels; ++x)
		{
			const float V0 = 0.37f + (float)x * VPerPx;
			double M = 0.0, M2 = 0.0;
			for (int32 j = 0; j < 32; ++j)
			{
				const float Jit = ((float)j + 0.5f) / 32.f - 0.5f;
				const double Dk = SlonkFauna::MembraneLinesAt(FVector2f(0.5f, V0 + Jit * VPerPx), FVector2f(0.002f, VPerPx));
				M += Dk / 32.0;
				M2 += Dk * Dk / 32.0;
			}
			const double Sd = FMath::Sqrt(FMath::Max(M2 - M * M, 0.0));
			if (0.1f / VPerPx < 1.f) { RowFlicker = FMath::Max(RowFlicker, Sd); Flickers += Sd > 0.12 ? 1 : 0; }
			Sum += M;
		}
		const double Share = Sum / Pixels;
		WorstFlicker = FMath::Max(WorstFlicker, RowFlicker);
		LineShareLo = FMath::Min(LineShareLo, Share);
		LineShareHi = FMath::Max(LineShareHi, Share);
		LineRows += FString::Printf(TEXT("%s%.2f px wide: %.3f of the row dark, its pixels flickering %.3f at the most"), LineRows.IsEmpty() ? TEXT("") : TEXT("; "), 0.1f / VPerPx, Share, RowFlicker);
	}
	const FString FlierPixel = MaterialMotionBlock(EMotionBlock::Fliers, true);
	const int32 BranchAt = FlierPixel.Find(TEXT("[branch] if (WingForm > 0.5)")), BranchEnd = FlierPixel.Find(TEXT("float surf = VC.r;"));
	const FString Branch = BranchAt >= 0 && BranchEnd > BranchAt ? FlierPixel.Mid(BranchAt, BranchEnd - BranchAt) : FString();
	const bool bLinesCopied = Branch.Contains(TEXT("float astrut = max((1.0 - smoothstep(asw - 0.5 * avw, asw + 0.5 * avw, abs(frac(UV.y + 0.5) - 0.5))) * min(0.05 / min(asw, 0.5), 1.0),"))
		&& Branch.Contains(TEXT("(1.0 - smoothstep(alw - 0.5 * auw, alw + 0.5 * auw, au)) * min(0.07 / min(alw, 1.0), 1.0));")) && Branch.Contains(TEXT("float asw = max(0.05, 0.75 * avw);"))
		&& Branch.Contains(TEXT("float alw = max(0.07, 0.75 * auw);"));
	const bool bSolid = Branch.Contains(TEXT("Sub = askin * 0.2 * awing;"));
	const bool bNoDither = !Branch.IsEmpty() && !Branch.Contains(TEXT("SvPosition")) && !Branch.Contains(TEXT("StateFrameIndex")) && Branch.Contains(TEXT("Mask = afade > frac(Seed * 91.37 + 0.13) ? 1.0 : 0.0;"));
	AddInfo(FString::Printf(TEXT("a membrane's struts: %s; the material's lines as MembraneLinesAt has them: %s, its skin solid: %s, nothing dithered from frame to frame: %s"), *LineRows,
		bLinesCopied ? TEXT("yes") : TEXT("no"), bSolid ? TEXT("yes") : TEXT("no"), bNoDither ? TEXT("yes") : TEXT("no")));
	TestEqual(TEXT("no pixel of a membrane flickering between skin and strut under the jitter, its struts under a pixel wide"), Flickers, 0);
	TestTrue(TEXT("a row across the wing as dark in all as its struts are, near and far"), LineShareLo > 0.07 && LineShareHi < 0.14);
	TestTrue(TEXT("the alien fliers' branch draws a membrane's lines, its skin and its fade so"), bLinesCopied && bSolid && bNoDither);

	// Their silhouettes against the Earth bird's and each other's, from below, from the side and from the air, at LOD 1 (the
	// LOD a flock is seen by from about 12 m to 120 m off) and LOD 0; and how many wings a line across them meets 30 cm out.
	auto Look = [&](const FFaunaBodyPlan& P, int32 Lod, TArray<uint8>& Below, TArray<uint8>& Side, TArray<uint8>& Air, int32& N)
	{
		FDynamicMesh3 M;
		SlonkFaunaMeshes::BuildLod(P, Lod, M);
		const FVector Av = AirView();
		const FVector AirRight = FVector::CrossProduct(Av, FVector::ZAxisVector).GetSafeNormal(), AirUp = FVector::CrossProduct(AirRight, Av).GetSafeNormal();
		Below = Silhouette(M, FVector::XAxisVector, FVector::YAxisVector, -80.0, 80.0, 0.25, N);
		Side = Silhouette(M, FVector::XAxisVector, FVector::ZAxisVector, -80.0, 80.0, 0.25, N);
		Air = Silhouette(M, AirRight, AirUp, -80.0, 80.0, 0.25, N);
	};
	// The runs of cover along x in the row y cm out of a silhouette from below (x along it, y across).
	auto WingRuns = [](const TArray<uint8>& Below, int32 N, double Y) { return LegRuns(Below, N, -80.0, 0.25, Y); };
	const FFaunaBodyPlan Bird = SlonkFauna::EarthPlan(EFaunaKind::Flier);
	FString Looks;
	double MostAlike = 0.0, MostAlikeAlien = 0.0;
	int32 RunsOff = 0;
	for (const int32 Lod : { 0, 1 })
	{
		TArray<uint8> Bb, Bs, Ba, Fb, Fs, Fa, Mb, Ms, Ma;
		int32 N = 0;
		Look(Bird, Lod, Bb, Bs, Ba, N);
		Look(Four, Lod, Fb, Fs, Fa, N);
		Look(Memb, Lod, Mb, Ms, Ma, N);
		const double BF = SilhouetteOverlap(Bb, Fb), BM = SilhouetteOverlap(Bb, Mb), FM = SilhouetteOverlap(Fb, Mb);
		const double BFs = SilhouetteOverlap(Bs, Fs), BMs = SilhouetteOverlap(Bs, Ms), FMs = SilhouetteOverlap(Fs, Ms);
		const double BFa = SilhouetteOverlap(Ba, Fa), BMa = SilhouetteOverlap(Ba, Ma), FMa = SilhouetteOverlap(Fa, Ma);
		const int32 Rb = WingRuns(Bb, N, 30.0), Rf = WingRuns(Fb, N, 30.0), Rm = WingRuns(Mb, N, 30.0);
		RunsOff += Rb == 1 && Rf == 2 && Rm == 1 ? 0 : 1;
		MostAlike = FMath::Max(MostAlike, FMath::Max(FMath::Max(BF, BFa), FMath::Max(BM, BMa)));
		MostAlikeAlien = FMath::Max(MostAlikeAlien, FMath::Max(FM, FMa));
		Looks += FString::Printf(TEXT("%sLOD %d: the bird and the four-winged flier share %.2f of their view from below, %.2f from the side, %.2f from the air; the bird and the membrane-winged %.2f, %.2f and %.2f; the two alien forms %.2f, %.2f and %.2f; wings met 30 cm out %d, %d and %d"),
			Looks.IsEmpty() ? TEXT("") : TEXT("; "), Lod, BF, BFs, BFa, BM, BMs, BMa, FM, FMs, FMa, Rb, Rf, Rm);
	}
	AddInfo(Looks);
	TestEqual(TEXT("one wing a side met across the bird and the membrane, two across the four-winged flier"), RunsOff, 0);
	TestTrue(TEXT("each a silhouette of its own from below and from the air, apart from the bird's"), MostAlike < 0.6);
	// The four-winged flier's narrow wings lie mostly within the membrane's broad ones: they share less, its wings counted.
	TestTrue(TEXT("and from each other's"), MostAlikeAlien < 0.7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaAlienFlocksTest, "Slonk.Fauna.AlienFlocks", SLONK_TEST_FLAGS)
bool FSlonkFaunaAlienFlocksTest::RunTest(const FString& Parameters)
{
	// The strange worlds' flocks: on the Earth-like world, its fliers all of one alien plan, each flock in the cell an Earth
	// one is in, of the same habitat, members and loop, but of its species' plan and size (its span its habitat bird's
	// times its species' scale), a four-winged flock's wings beating twice as fast (at most 14 a second) and flying its
	// loop three quarters as fast, a membrane-winged one's beating 0.6 as fast and flying 0.9 as fast; every beat a whole
	// number a minute. A craft low by one startles it as it does a bird's flock; one high over it does not. Those of a plan
	// with glow organs glow at night (1.5 to 3), the others not at all. A frame's math for them is a bird's, and at the caps
	// (1,000 fliers of the plan of the most triangles) they keep within 110,000 triangles at LOD 0.
	const SlonkFauna::FFaunaWorld Earth = EarthFauna();
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Flier);
	TArray<SlonkFauna::FFaunaGroup> EarthFlocks;
	GroupsOverGlobe(Earth, 1200, EarthFlocks);
	TMap<uint64, SlonkFauna::FFaunaGroup> ById;
	for (const SlonkFauna::FFaunaGroup& G : EarthFlocks) { ById.Add(G.Id, G); }
	FString Lines;
	int32 Off = 0, NotWhole = 0, BadGlow = 0, Startled = 0, StartledHigh = 0, Tried = 0, Flocks = 0;
	for (int32 Form = 0; Form < 2; ++Form)
	{
		for (const float GlowDraw : { 0.9f, 0.2f })
		{
			float Scale = 1.f;
			const FFaunaBodyPlan Plan = AlienKindPlan(EFaunaKind::Flier, Form, 0.5f, 0.5f, GlowDraw, &Scale);
			const SlonkFauna::FFaunaWorld W = OnePlanWorld(Earth, EFaunaKind::Flier, Plan, Scale);
			TArray<SlonkFauna::FFaunaGroup> Groups;
			GroupsOverGlobe(W, 1200, Groups);
			double SpeedRatio = 0.0, FlapRatio = 0.0, SpanRatio = 0.0;
			int32 N = 0;
			for (const SlonkFauna::FFaunaGroup& G : Groups)
			{
				++Flocks;
				const SlonkFauna::FFaunaGroup* E = ById.Find(G.Id);
				if (!E) { ++Off; continue; }
				const int32 F = Wn::FormOf(G.Plan);
				// The same cell's flock in all but its plan's own.
				Off += G.Plan == Plan && F == (Form == 0 ? Wn::Four : Wn::Membrane) && G.Habitat == E->Habitat && G.Members == E->Members && G.Centre == E->Centre && G.RadiusCm == E->RadiusCm
					&& G.CruiseCm == E->CruiseCm && G.Pigment == E->Pigment ? 0 : 1;
				const float WantSpan = FMath::Clamp(E->SpanM * Scale, 0.25f, 2.6f);
				const float WantFlap = F == Wn::Four ? FMath::Min(E->FlapHz * Wn::FourFlapK, Wn::FourFlapMax) : E->FlapHz * Wn::MembraneFlapK;
				Off += FMath::IsNearlyEqual(G.SpanM, WantSpan, 1e-5f) && FMath::IsNearlyEqual(G.FlapHz, WantFlap, 1e-5f) && FMath::IsNearlyEqual(G.SpacingM, E->SpacingM * Scale, 1e-4f) ? 0 : 1;
				SpeedRatio += G.SpeedCmS / FMath::Max(E->SpeedCmS, 1.0);
				FlapRatio += G.FlapHz / FMath::Max(E->FlapHz, 0.01f);
				SpanRatio += G.SpanM / FMath::Max(E->SpanM, 0.01f);
				++N;
				BadGlow += Plan.GlowOrgans > 0 ? (G.Glow >= SlonkFauna::GlowOrgan::GlowLo && G.Glow <= SlonkFauna::GlowOrgan::GlowHi ? 0 : 1) : (G.Glow == 0.f ? 0 : 1);
				TArray<FTransform> Xf;
				TArray<float> Cu;
				SlonkFauna::MembersOf(G, Xf, Cu);
				for (int32 i = 0; i < Xf.Num(); ++i)
				{
					const float Bpm = Cu[i * SlonkFauna::Custom::Num + SlonkFauna::Custom::RateHz] * 60.f;
					NotWhole += FMath::IsNearlyEqual(Bpm, FMath::RoundToFloat(Bpm), 1e-3f) ? 0 : 1;
					BadGlow += Cu[i * SlonkFauna::Custom::Num + SlonkFauna::Custom::Glow] == G.Glow ? 0 : 1;
				}
				// A craft 30 m up passing 50 m off it startles it; one 300 m up over it does not (the first 30 of each).
				if (N <= 30)
				{
					++Tried;
					const SlonkFauna::FFaunaPose P = SlonkFauna::PathAt(G, W.P.RadiusCm, 500.0);
					SlonkFauna::FPanic Pa, Pb;
					SlonkFauna::FFaunaThreat Low, High;
					Low.PosCm = P.PosCm + P.Rot.GetAxisY() * 5000.0;
					Low.AltitudeCm = 3000.0;
					High.PosCm = P.PosCm + P.PosCm.GetSafeNormal() * 3.0e4;
					High.AltitudeCm = 3.0e4 + G.CruiseCm;
					Startled += SlonkFauna::StartleGroup(Pa, D, P.PosCm, Low, 500.0) ? 1 : 0;
					StartledHigh += SlonkFauna::StartleGroup(Pb, D, P.PosCm, High, 500.0) ? 1 : 0;
				}
			}
			Lines += FString::Printf(TEXT("%s%s%s: %d flocks, their span %.2f, beat %.2f and speed %.2f of the same cells' birds' on average"), Lines.IsEmpty() ? TEXT("") : TEXT("; "), WingName(Form == 0 ? Wn::Four : Wn::Membrane),
				Plan.GlowOrgans > 0 ? *FString::Printf(TEXT(" (%d glow organs a flank)"), Plan.GlowOrgans) : TEXT(""), N, SpanRatio / FMath::Max(N, 1), FlapRatio / FMath::Max(N, 1), SpeedRatio / FMath::Max(N, 1));
			if (Form == 0) { TestTrue(TEXT("a four-winged flock beats faster and flies its loop slower than a bird's"), N > 50 && FlapRatio / N > 1.5 && SpeedRatio / N < 0.85); }
			else { TestTrue(TEXT("a membrane-winged flock beats slower and flies its loop a little slower than a bird's"), N > 50 && FlapRatio / N < 0.7 && SpeedRatio / N < 0.97 && SpeedRatio / N > 0.8); }
		}
	}
	// At the caps: 1,000 of the plan of the most triangles at LOD 0.
	int32 MostTris = 0;
	for (const FAlienPlan& A : AlienKindPlans(EFaunaKind::Flier))
	{
		UE::Geometry::FDynamicMesh3 M;
		SlonkFaunaMeshes::BuildLod(A.Plan, 0, M);
		MostTris = FMath::Max(MostTris, M.TriangleCount());
	}
	const int32 WorstCase = D.MaxMembers * MostTris;
	AddInfo(FString::Printf(TEXT("%s; %d flocks, %d off the same cell's bird's flock but in their plan's own, %d beats not a whole number a minute, %d glows off; startled by a craft low by %d of %d, by one high over %d; at the caps (%d fliers) of the plan of the most triangles (%d) %d triangles at LOD 0"),
		*Lines, Flocks, Off, NotWhole, BadGlow, Startled, Tried, StartledHigh, D.MaxMembers, MostTris, WorstCase));
	TestTrue(TEXT("alien flocks to try"), Flocks > 200);
	TestEqual(TEXT("each the same cell's flock but in its plan's own"), Off, 0);
	TestEqual(TEXT("every beat a whole number a minute"), NotWhole, 0);
	TestEqual(TEXT("those of a plan with glow organs glow, the others not"), BadGlow, 0);
	TestTrue(TEXT("a craft low by one startles it, one high over it does not"), Startled == Tried && StartledHigh == 0 && Tried > 0);
	TestTrue(TEXT("at most 110,000 triangles of fliers at LOD 0, at the caps"), WorstCase <= 110000);

	// The densest four-winged flocks (the most members at the least spacing for their span), every member's middle as the
	// material moves it (its ripple through the flock and its darts) every tenth of a second through a whole hour: no two of
	// them nearer than they come without the darts (the flock darts together, every member at once, so the gaps the
	// formation gave them hold).
	{
		float Scale = 1.f;
		const FFaunaBodyPlan Plan = AlienKindPlan(EFaunaKind::Flier, 0, 0.5f, 0.5f, 0.2f, &Scale);
		const SlonkFauna::FFaunaWorld W = OnePlanWorld(Earth, EFaunaKind::Flier, Plan, Scale);
		TArray<SlonkFauna::FFaunaGroup> Groups;
		GroupsOverGlobe(W, 1200, Groups);
		Groups.Sort([](const SlonkFauna::FFaunaGroup& A, const SlonkFauna::FFaunaGroup& B) { return A.Members / FMath::Max(A.SpacingM / A.SpanM, 0.1f) > B.Members / FMath::Max(B.SpacingM / B.SpanM, 0.1f); });
		double ClosestDarting = 1e9, ClosestStill = 1e9;
		int32 DashedInto = 0, PairsSeen = 0, Moments = 0, DartSeeds = 0;
		FString Dense;
		for (int32 g = 0; g < FMath::Min(2, Groups.Num()); ++g)
		{
			const SlonkFauna::FFaunaGroup& G = Groups[g];
			TArray<FTransform> Xf;
			TArray<float> Cu;
			SlonkFauna::MembersOf(G, Xf, Cu);
			const int32 N = Xf.Num();
			float Gd[SlonkFauna::GroupData::Num] = {};
			Gd[SlonkFauna::GroupData::SpreadM] = G.SpacingM;
			Gd[SlonkFauna::GroupData::DartSeed] = SlonkFauna::DartSeedOf(G);
			Gd[SlonkFauna::GroupData::DartSpanM] = G.SpanM;
			DartSeeds += Gd[SlonkFauna::GroupData::DartSeed] > 0.f ? 1 : 0;
			// The pairs whose homes lie within six spans (a pair further apart never meets: the ripple and a dart are each
			// under two spans either way).
			TArray<FIntPoint> Pairs;
			for (int32 i = 0; i < N; ++i)
			{
				for (int32 j = i + 1; j < N; ++j) { if (FVector::Dist(Xf[i].GetLocation(), Xf[j].GetLocation()) < 600.0 * G.SpanM) { Pairs.Add(FIntPoint(i, j)); } }
			}
			PairsSeen += Pairs.Num();
			TArray<FVector> At, AtStill;
			At.SetNum(N);
			AtStill.SetNum(N);
			double FlockDarting = 1e9, FlockStill = 1e9;
			for (int32 Step = 0; Step < 36000; ++Step)
			{
				const float T = 0.1f * (float)Step;
				++Moments;
				for (int32 i = 0; i < N; ++i)
				{
					const float* C = &Cu[i * SlonkFauna::Custom::Num];
					const FVector3f Mo = SlonkFauna::FlierMotion(FVector3f::ZeroVector, FLinearColor(0.f, 0.f, 0.5f, 0.f), T, C, Gd, 1000.f, 960.f, FVector2f(0.f, 0.f), Wn::Four);
					// Without its darts: its middle as a bird's moves (the ripple alone: no wing moves a body's middle).
					const FVector3f Still = SlonkFauna::FlierMotion(FVector3f::ZeroVector, FLinearColor(0.f, 0.f, 0.5f, 0.f), T, C, Gd, 1000.f, 960.f, FVector2f(0.f, 0.f), Wn::Bird);
					const double S = C[SlonkFauna::Custom::SizeM];
					At[i] = Xf[i].GetLocation() + Xf[i].GetRotation().RotateVector(FVector(Mo) * S);
					AtStill[i] = Xf[i].GetLocation() + Xf[i].GetRotation().RotateVector(FVector(Still) * S);
				}
				for (const FIntPoint& Pr : Pairs)
				{
					const double Span = 0.5 * (Cu[Pr.X * SlonkFauna::Custom::Num + SlonkFauna::Custom::SizeM] + Cu[Pr.Y * SlonkFauna::Custom::Num + SlonkFauna::Custom::SizeM]) * 100.0;
					const double Dd = FVector::Dist(At[Pr.X], At[Pr.Y]) / Span, Ds = FVector::Dist(AtStill[Pr.X], AtStill[Pr.Y]) / Span;
					FlockDarting = FMath::Min(FlockDarting, Dd);
					FlockStill = FMath::Min(FlockStill, Ds);
					DashedInto += Dd < Ds - 0.01 && Dd < 1.0 ? 1 : 0;
				}
			}
			ClosestDarting = FMath::Min(ClosestDarting, FlockDarting);
			ClosestStill = FMath::Min(ClosestStill, FlockStill);
			Dense += FString::Printf(TEXT("%s%d of %.2f m, %.1f spans apart: closest %.2f spans darting, %.2f without its darts"), Dense.IsEmpty() ? TEXT("") : TEXT("; "), N, G.SpanM, G.SpacingM / G.SpanM, FlockDarting, FlockStill);
		}
		AddInfo(FString::Printf(TEXT("the densest four-winged flocks through an hour, every tenth of a second (%d moments, %d pairs within six spans): %s; %d moments a pair under a span apart came nearer for the darts"),
			Moments, PairsSeen, *Dense, DashedInto));
		TestTrue(TEXT("dense four-winged flocks to fly"), Groups.Num() >= 2 && PairsSeen > 20 && DartSeeds == FMath::Min(2, Groups.Num()));
		TestTrue(TEXT("no two members of a four-winged flock nearer for its darts: it darts together"), DashedInto == 0 && ClosestDarting >= ClosestStill - 1e-3);
	}
	return true;
}

namespace
{
	/** A fish's vertex colour at its mesh's middle (its origin): an eel's G its bend from its nose there (a half), every
	 *  other's as Slonk.Fauna.LeaperArcs reads it (0). */
	FLinearColor FishMiddle(int32 Form) { return FLinearColor(0.f, Form == Ff::Eel ? 0.5f : 0.f, 0.5f, SlonkFauna::Vc::JointUndulate); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaAlienFishTest, "Slonk.Fauna.AlienFish", SLONK_TEST_FLAGS)
bool FSlonkFaunaAlienFishTest::RunTest(const FString& Parameters)
{
	// The strange worlds' leaping fish, every plan at the ends and the middle of its ranges, with its ornaments and without,
	// with glow organs and without: an eel of 82 or 84, 38 and 3 triangles, a spinefin of 63 or 66, 21 and 5; a metre long,
	// its middle at the origin; its vertex colours as the material reads them (an eel's bend from its nose, a spinefin's
	// fins the wing's surface flexing from their hinge 4 cm out); every face wound to show the side its normals point to; a
	// tangent basis everywhere; the material's motion of its form keeping every vertex in the instance's padded box; its
	// LODs coming in about 30 and 6 pixels long. On the Earth-like world's seas, its fish all of the plan, each leaps the
	// Earth fish's arcs: drawn when the closed form has it drawn and only then, its middle where the closed form has it
	// (an eel's wave held off it), at the sea's surface where and when it leaves and dives back in, and its splash there;
	// under the water whole as it comes and goes, held under the lowest surface between leaps, never below the shallowest
	// floor it lives over. An eel's wave runs back along it, more in the air; a spinefin's fins are folded down under the
	// water and spread at its leap's top. A group's fish no longer than its plan's longest, the habitat read out as far as
	// they leap; their splashes scaled to them.
	using namespace UE::Geometry;
	namespace Vc = SlonkFauna::Vc;
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Leaper);
	const TArray<FAlienPlan> Plans = AlienKindPlans(EFaunaKind::Leaper);
	int32 BadTris = 0, BadColour = 0, BadWinding = 0, Escaped = 0, Tried = 0, NoTangent = 0, Vertices = 0, BadLod = 0, Fins = 0;
	int32 Forms[3] = {};
	double Margin = TNumericLimits<double>::Max(), Px1Lo = 1e9, Px1Hi = 0.0, Px2Lo = 1e9, Px2Hi = 0.0;
	for (int32 p = 0; p < Plans.Num(); ++p)
	{
		const FFaunaBodyPlan& Plan = Plans[p].Plan;
		const int32 Form = Ff::FormOf(Plan);
		++Forms[Form];
		FBox Box0(ForceInit);
		FDynamicMesh3 Lod0;
		const double FinTip = Ff::FinRootCm + 100.0 * FMath::Clamp((double)Plan.Chord, 0.2, 0.45);
		for (int32 L = 0; L < SlonkFaunaMeshes::NumLods; ++L)
		{
			FDynamicMesh3 M;
			if (!SlonkFaunaMeshes::BuildLod(Plan, L, M)) { AddError(TEXT("no alien fish built")); return false; }
			BadTris += M.TriangleCount() == AlienFishTris(Plan, L) ? 0 : 1;
			BadWinding += InwardFaces(M);
			const FDynamicMeshColorOverlay* C = M.Attributes()->PrimaryColors();
			for (const int32 T : M.TriangleIndicesItr())
			{
				const FIndex3i Ce = C->GetTriangle(T), Tv = M.GetTriangle(T);
				for (int32 k = 0; k < 3; ++k)
				{
					const FVector4f Col = C->GetElement(Ce[k]);
					const FVector3d P = M.GetVertex(Tv[k]);
					if (L == 0) { Box0 += (FVector)P; }
					const float WantSide = P.Y > 0.5 ? Vc::SideRight : (P.Y < -0.5 ? Vc::SideLeft : Vc::SideMiddle);
					bool bOk = Col.W == Vc::JointUndulate;
					if (Col.X == Vc::SurfaceWing)
					{
						// A spinefin's fin: flexing from its hinge.
						const float Pt = P.X < -2.0 ? (float)(Ff::FinRootCm + 0.55 * (FinTip - Ff::FinRootCm)) : (float)FinTip;
						bOk &= Form == Ff::Spinefin && FMath::Abs(P.Y) >= Ff::FinRootCm - 1e-3 && FMath::IsNearlyEqual(Col.Y, FMath::Clamp((float)(FMath::Abs(P.Y) - Ff::FinRootCm) / FMath::Max(Pt - Ff::FinRootCm, 1.f), 0.f, 1.f), 1e-3f)
							&& Col.Z == (P.Y > 0.0 ? Vc::SideRight : Vc::SideLeft);
						Fins += L == 0 ? 1 : 0;
					}
					else
					{
						const float WantBend = Form == Ff::Eel ? (float)FMath::Clamp((50.0 - P.X) / 100.0, 0.0, 1.0) : (float)FMath::Clamp((Vc::FishBendFromCm - P.X) / (Vc::FishBendFromCm + 50.0), 0.0, 1.0);
						bOk &= (Col.X == Vc::SurfaceBody || Col.X == Vc::SurfaceTail) && FMath::IsNearlyEqual(Col.Y, WantBend, 1e-3f) && Col.Z == WantSide;
					}
					BadColour += bOk ? 0 : 1;
				}
			}
			if (L == 0) { Lod0 = MoveTemp(M); }
		}
		const FBox Padded = Box0.ExpandBy(Box0.GetExtent() * 0.15 + FVector(20.0 + D.CullPaddingUnits));
		const TArray<FLinearColor> Vcs = VertexColours(Lod0);
		FRandomStream Rng(91 + p);
		const float Longest = (float)SlonkFauna::LeaperMaxSizeM(Plan);
		for (int32 Case = 0; Case < 160; ++Case)
		{
			float Cu[SlonkFauna::Custom::Num] = {};
			float Gd[SlonkFauna::LeaperData::Num] = {};
			const float Size = Case % 5 == 0 ? (float)SlonkFauna::Leaper::MinSizeM : (Case % 5 == 1 ? Longest : Rng.FRandRange((float)SlonkFauna::Leaper::MinSizeM, Longest));
			const int32 Count = Rng.RandRange(1, SlonkFauna::Leaper::MaxPerGroup), Idx = Rng.RandRange(0, Count - 1);
			Cu[SlonkFauna::Custom::SizeM] = Size;
			Cu[SlonkFauna::Custom::Seed] = Rng.FRand();
			Cu[SlonkFauna::Custom::Phase] = Rng.FRand();
			Cu[SlonkFauna::Custom::RateHz] = (float)SlonkFauna::Leaper::Cycles[Rng.RandRange(0, (int32)UE_ARRAY_COUNT(SlonkFauna::Leaper::Cycles) - 1)];
			Cu[SlonkFauna::Custom::PerKind] = (float)(8 * Idx + Count);
			for (int32 k = 0; k < 2 * SlonkFauna::Leaper::MaxPerGroup; ++k)
			{
				Gd[SlonkFauna::LeaperData::Surface0 + k] = Case % 3 == 0 ? (Rng.FRand() < 0.5f ? -1.f : 1.f) * SlonkFauna::Leaper::MaxSurfaceCm : Rng.FRandRange(-1.f, 1.f) * SlonkFauna::Leaper::MaxSurfaceCm;
			}
			Gd[SlonkFauna::LeaperData::Diving] = Case % 4 == 0 ? 1.f : 0.f;
			Gd[SlonkFauna::LeaperData::DiveT0] = Rng.FRandRange(0.f, 3600.f);
			Gd[SlonkFauna::LeaperData::DiveLen] = Rng.FRandRange(0.f, 70.f);
			float T = Rng.FRandRange(0.f, 3600.f);
			if (Case % 4 != 3)
			{
				const SlonkFauna::FLeap L = SlonkFauna::LeapOf(Cu, Rng.RandRange(0, 200));
				T = MaterialTime(L.T0 - L.LeadSec + (L.FlightSec + 2.0 * L.LeadSec) * Rng.FRand());
			}
			const float Dist = Case % 3 == 0 ? 2000.f : (Case % 3 == 1 ? Size * 100.f * 960.f / 1.5f : 4.0e5f);
			for (const int32 V : Lod0.VertexIndicesItr())
			{
				const FVector3f LP = (FVector3f)Lod0.GetVertex(V);
				const FVector3f Moved = LP + SlonkFauna::LeaperMotion(LP, Vcs[V], T, Cu, Gd, Dist, 960.f, Form);
				++Tried;
				const FVector Mv((double)Moved.X, (double)Moved.Y, (double)Moved.Z);
				Escaped += Padded.IsInside(Mv) ? 0 : 1;
				Margin = FMath::Min(Margin, FMath::Min3(FMath::Min(Mv.X - Padded.Min.X, Padded.Max.X - Mv.X), FMath::Min(Mv.Y - Padded.Min.Y, Padded.Max.Y - Mv.Y), FMath::Min(Mv.Z - Padded.Min.Z, Padded.Max.Z - Mv.Z)));
			}
		}
		UStaticMesh* Mesh = SlonkFaunaMeshes::GetMesh(Plan, EFaunaKind::Leaper);
		if (!Mesh || Mesh != SlonkFaunaMeshes::GetMesh(Plan, EFaunaKind::Leaper) || Mesh->GetNumLODs() != SlonkFaunaMeshes::NumLods) { ++BadLod; continue; }
		TangentBasis(Mesh, Vertices, NoTangent);
		const FVector2D Px = LodPixels(Mesh, D, Form == Ff::Eel ? 0.9 : 0.6);
		Px1Lo = FMath::Min(Px1Lo, Px.X); Px1Hi = FMath::Max(Px1Hi, Px.X);
		Px2Lo = FMath::Min(Px2Lo, Px.Y); Px2Hi = FMath::Max(Px2Hi, Px.Y);
	}
	AddInfo(FString::Printf(TEXT("%d alien fish plans (%d eels, %d spinefins): %d LODs off their triangles, %d vertices off the colour contract (%d of a spinefin's fins at LOD 0), %d faces wound inward; %d vertices moved by the material, %d outside the box padded %.0f units (the closest %.0f inside); %d vertices over their meshes, %d without a tangent basis, %d meshes not made; LOD 1 from %.1f to %.1f pixels long, LOD 2 from %.1f to %.1f"),
		Plans.Num(), Forms[Ff::Eel], Forms[Ff::Spinefin], BadTris, BadColour, Fins, BadWinding, Tried, Escaped, D.CullPaddingUnits, Margin, Vertices, NoTangent, BadLod, Px1Lo, Px1Hi, Px2Lo, Px2Hi));
	TestTrue(TEXT("plans of both forms"), Forms[Ff::Eel] >= 4 && Forms[Ff::Spinefin] >= 4);
	TestEqual(TEXT("every LOD its triangles"), BadTris, 0);
	TestEqual(TEXT("every vertex as the material reads it"), BadColour, 0);
	TestTrue(TEXT("a spinefin's fins"), Fins > 0);
	TestEqual(TEXT("every face shows the side its normals point to"), BadWinding, 0);
	TestEqual(TEXT("the material keeps every vertex in its instance's box"), Escaped, 0);
	TestTrue(TEXT("every mesh made and kept, a tangent basis at every vertex"), BadLod == 0 && Vertices > 0 && NoTangent == 0);
	TestTrue(TEXT("its LOD 1 from about 30 pixels long, its LOD 2 from about 6"), Px1Lo > 20.0 && Px1Hi < 42.0 && Px2Lo > 3.5 && Px2Hi < 9.0);

	// Their leaps on the Earth-like world's seas, as Slonk.Fauna.LeaperArcs follows an Earth fish's.
	const SlonkFauna::FFaunaWorld Seas = EarthSeas();
	const double R = Seas.P.RadiusCm;
	const double Swell = Seas.Sea.SwellCm;
	FString Lines;
	for (int32 Form = 0; Form < 2; ++Form)
	{
		float Scale = 1.f;
		const FFaunaBodyPlan Plan = AlienKindPlan(EFaunaKind::Leaper, Form, 0.5f, 0.7f, 0.9f, &Scale);
		const int32 F = Ff::FormOf(Plan);
		const SlonkFauna::FFaunaWorld W = OnePlanWorld(Seas, EFaunaKind::Leaper, Plan, Scale);
		TArray<SlonkFauna::FFaunaGroup> Groups;
		LeaperGroupsOverSeas(W, 16, Groups);
		FDynamicMesh3 Lod0;
		SlonkFaunaMeshes::BuildLod(Plan, 0, Lod0);
		const TArray<FLinearColor> Vcs = VertexColours(Lod0);
		const double Longest = SlonkFauna::LeaperMaxSizeM(Plan);
		int32 Moments = 0, ShownDisagree = 0, OffArc = 0, NotAtSurface = 0, CrossingOff = 0, AboveOutside = 0, PokesOut = 0, BelowFloor = 0, Leaps = 0, TooLong = 0, ReachOff = 0, Folded = 0, NotFolded = 0, Spread = 0, NotSpread = 0;
		double WorstOff = 0.0, WorstSurface = 0.0, Lowest = 0.0, MostWave = 0.0, MostWaveMid = 0.0;
		double ShortestFish = 1e9, LongestFish = 0.0, AllLength = 0.0;
		int32 FishCount = 0;
		FRandomStream Rng(303 + Form);
		SlonkFauna::FPanic NoDive;
		for (const SlonkFauna::FFaunaGroup& G : Groups)
		{
			const FFish Fi = FishOf(G);
			SlonkFauna::FLeaperSurfaceCache Cache;
			SlonkFauna::FShoal S;
			if (SlonkFauna::ShoalAt(G.Centre * R, R, G.ShoalDensity, 0.0, S)) { ReachOff += FMath::IsNearlyEqual(G.MeanderCm, SlonkFauna::LeaperReachCm(S, Longest), 1e-6) ? 0 : 1; }
			for (int32 i = 0; i < Fi.Num(); ++i)
			{
				const float* C = Fi.At(i);
				const float Sz = C[SlonkFauna::Custom::SizeM];
				TooLong += Sz <= Longest + 1e-4 && Sz >= SlonkFauna::Leaper::MinSizeM - 1e-4 ? 0 : 1;
				ShortestFish = FMath::Min(ShortestFish, (double)Sz);
				LongestFish = FMath::Max(LongestFish, (double)Sz);
				AllLength += Sz;
				++FishCount;
				for (int32 Pick = 0; Pick < 8; ++Pick)
				{
					const SlonkFauna::FLeap L = SlonkFauna::LeapOf(C, Rng.RandRange(0, 300));
					if (!L.bLeaps) { continue; }
					++Leaps;
					TArray<double> Ts = { L.T0 - L.LeadSec - 0.02, L.T0 - L.LeadSec + 0.002, L.T0, L.T0 + 0.5 * L.FlightSec, L.T1(), L.T1() + L.LeadSec - 0.002, L.T1() + L.LeadSec + 0.02 };
					for (int32 k = 0; k < 5; ++k) { Ts.Add(L.T0 - L.LeadSec + (L.FlightSec + 2.0 * L.LeadSec) * Rng.FRand()); }
					for (const double T : Ts)
					{
						++Moments;
						float Gd[SlonkFauna::LeaperData::Num] = {};
						SlonkFauna::LeaperGroupData(G, Fi.Custom, Fi.Home, R, Swell, 1.0, T, NoDive, Cache, Gd);
						const float Tm = MaterialTime(T);
						const bool bShown = SlonkFauna::LeaperShown(Tm, C, Gd);
						const bool bNearEdge = FMath::Abs(T - (L.T0 - L.LeadSec)) < 0.005 || FMath::Abs(T - (L.T1() + L.LeadSec)) < 0.005;
						ShownDisagree += bShown == L.DrawnAt(T) || bNearEdge ? 0 : 1;
						const double H0 = Gd[SlonkFauna::LeaperData::Surface0 + 2 * i], H1 = Gd[SlonkFauna::LeaperData::Surface0 + 2 * i + 1];
						const SlonkFauna::FLeapPose P = SlonkFauna::LeapPoseAt(L, H0, H1, T);
						const FVector Mid = (FVector)SlonkFauna::LeaperMotion(FVector3f::ZeroVector, FishMiddle(F), Tm, C, Gd, 2000.f, 960.f, F) * Sz;
						double Top = -1.0e9, Bottom = 1.0e9, TailY = 0.0;
						const FVector2f Turn = SlonkFauna::LeaperTurn(Tm, C, Gd);
						// A point moved by the material, in the fish's own frame about its middle (unturned, unpitched), unit-mesh cm.
						auto Own = [&](const FVector3f& LP, const FLinearColor& Col)
						{
							FVector3f Dv = LP + SlonkFauna::LeaperMotion(LP, Col, Tm, C, Gd, 2000.f, 960.f, F) - SlonkFauna::LeaperMotion(FVector3f::ZeroVector, FishMiddle(F), Tm, C, Gd, 2000.f, 960.f, F);
							const float Cy = FMath::Cos(-Turn.Y), Sy = FMath::Sin(-Turn.Y);
							Dv = FVector3f(Dv.X * Cy - Dv.Y * Sy, Dv.X * Sy + Dv.Y * Cy, Dv.Z);
							const float Cp = FMath::Cos(-Turn.X), Sp = FMath::Sin(-Turn.X);
							return FVector3f(Dv.X * Cp - Dv.Z * Sp, Dv.Y, Dv.X * Sp + Dv.Z * Cp);
						};
						for (const int32 V : Lod0.VertexIndicesItr())
						{
							const FVector3f LP = (FVector3f)Lod0.GetVertex(V);
							const FVector3f Moved = LP + SlonkFauna::LeaperMotion(LP, Vcs[V], Tm, C, Gd, 2000.f, 960.f, F);
							Top = FMath::Max(Top, (double)Moved.Z * Sz);
							Bottom = FMath::Min(Bottom, (double)Moved.Z * Sz);
							if (LP.X < -49.f && bShown) { TailY = FMath::Max(TailY, (double)FMath::Abs(Own(LP, Vcs[V]).Y)); }
						}
						Lowest = FMath::Min(Lowest, Bottom);
						BelowFloor += Bottom >= -SlonkFauna::Leaper::MinWaterCm ? 0 : 1;
						if (!bShown) { AboveOutside += Top < -SlonkFauna::Leaper::MaxSurfaceCm ? 0 : 1; continue; }
						const double OffCm = (Mid - P.PosCm).Size();
						WorstOff = FMath::Max(WorstOff, OffCm);
						OffArc += OffCm < 0.5 ? 0 : 1;
						if (FMath::Abs(T - (L.T0 - L.LeadSec)) < 0.01 || FMath::Abs(T - (L.T1() + L.LeadSec)) < 0.01) { PokesOut += Top <= FMath::Lerp(H0, H1, P.Share) + 0.5 ? 0 : 1; }
						for (const bool bEntry : { false, true })
						{
							if (T != (bEntry ? L.T1() : L.T0)) { continue; }
							const double Want = bEntry ? H1 : H0;
							WorstSurface = FMath::Max(WorstSurface, FMath::Abs(Mid.Z - Want));
							NotAtSurface += FMath::Abs(Mid.Z - Want) < 0.5 && FVector2D(Mid.X, Mid.Y).Equals(bEntry ? L.ToCm() : L.FromCm, 0.5) ? 0 : 1;
							double Surf = 0.0;
							const FVector Splash = SlonkFauna::LeapCrossing(G, R, Swell, 1.0, Fi.Home[i], L, bEntry, &Surf);
							const FVector Drawn = SlonkFauna::LeaperFramePoint(G, R, T, Fi.Home[i] + FVector(Mid.X, Mid.Y, 0.0));
							CrossingOff += (Splash - Drawn.GetSafeNormal() * (R + Mid.Z)).Size() < 1.0 && FMath::Abs(Surf - Want) < 0.01 ? 0 : 1;
						}
						// In its own frame, unpitched: an eel's tail swinging, its middle still; a spinefin's fins folded down under the water
						// and spread at its top.
						const float Sh = (float)((T - L.T0) / L.FlightSec);
						if (F == Ff::Eel && Sh > 0.3f && Sh < 0.7f) { MostWave = FMath::Max(MostWave, TailY); }
						if (F == Ff::Eel && Sh < 0.f) { MostWaveMid = FMath::Max(MostWaveMid, TailY); }
						if (F == Ff::Spinefin)
						{
							float Lo = 1.0e9f, Wide = 0.f;
							for (const int32 V : Lod0.VertexIndicesItr())
							{
								if (Vcs[V].R < 0.75f || Vcs[V].G < 0.99f) { continue; }
								// The fin's own swing about its hinge: its tip's height and reach off its hinge's, in the fish's own frame.
								const FVector3f LP = (FVector3f)Lod0.GetVertex(V);
								const FLinearColor Tip = Vcs[V];
								const FVector3f Hinge(LP.X, LP.Y > 0.f ? Ff::FinRootCm : -Ff::FinRootCm, LP.Z);
								const FVector3f Dv = Own(LP, Tip) - Own(Hinge, FLinearColor(1.f, 0.f, Tip.B, Tip.A));
								Lo = FMath::Min(Lo, Dv.Z / FMath::Max(FMath::Abs(LP.Y) - Ff::FinRootCm, 1.f));
								Wide = FMath::Max(Wide, FMath::Abs(Dv.Y) / FMath::Max(FMath::Abs(LP.Y) - Ff::FinRootCm, 1.f));
							}
							if (Sh < 0.f) { (Lo < -0.9f ? Folded : NotFolded) += 1; }
							if (FMath::Abs(Sh - 0.5f) < 0.02f) { (Wide > 0.97f ? Spread : NotSpread) += 1; }
						}
					}
				}
			}
		}
		Lines += FString::Printf(TEXT("%s%s (%s): %d groups, %d leaps, %d moments: %d disagree on whether it is drawn, %d off the closed form (%.3f cm at worst), %d off the surface where it crosses (%.3f cm), %d off the splash's place; %d poking out as it comes and goes, %d not held under between leaps, the lowest %.0f cm (%d under 3 m); %d fish past their plan's sizes, %d reaches off its longest's"),
			Lines.IsEmpty() ? TEXT("") : TEXT("; "), FishName(F), *SlonkFauna::DescribePlan(EFaunaKind::Leaper, Plan), Groups.Num(), Leaps, Moments, ShownDisagree, OffArc, WorstOff, NotAtSurface, WorstSurface, CrossingOff, PokesOut, AboveOutside, -Lowest, BelowFloor, TooLong, ReachOff);
		Lines += FString::Printf(TEXT("; %d fish %.2f to %.2f m long (%.2f on average), %.0f to %.0f cm thick"), FishCount, ShortestFish, LongestFish, AllLength / FMath::Max(FishCount, 1),
			100.0 * Plan.Girth * ShortestFish, 100.0 * Plan.Girth * LongestFish);
		// fauna plans reads its fish's lengths out as they are drawn: the range it prints (to a centimetre) holds every one.
		double ReadLo = 0.0, ReadHi = 0.0;
		{
			UPlanetFaunaComponent* Reader = NewObject<UPlanetFaunaComponent>();
			Reader->SetProfile(W.Profile);
			TArray<FString> Read;
			Reader->DescribePlans(Read);
			for (const FString& Line : Read)
			{
				FString Head, Range, Lo, Hi;
				if (!Line.StartsWith(TEXT("leaper #0:")) || !Line.Split(TEXT(" m long"), &Head, nullptr, ESearchCase::CaseSensitive, ESearchDir::FromEnd)) { continue; }
				Head.Split(TEXT("; "), nullptr, &Range, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
				if (Range.Split(TEXT(" to "), &Lo, &Hi)) { ReadLo = FCString::Atod(*Lo); ReadHi = FCString::Atod(*Hi); }
			}
			Reader->DestroyComponent();
		}
		Lines += FString::Printf(TEXT("; fauna plans reads them out %.2f to %.2f m long"), ReadLo, ReadHi);
		TestTrue(TEXT("fauna plans reads out the lengths its fish are drawn at"), ReadHi > 0.0 && ReadLo <= ShortestFish + 0.005 && ReadHi >= LongestFish - 0.005);
		if (F == Ff::Eel) { Lines += FString::Printf(TEXT("; its tail's tip swinging up to %.1f cm of the unit mesh either way of its middle in the air, %.1f as it comes up under the water"), MostWave, MostWaveMid); }
		else { Lines += FString::Printf(TEXT("; its fins folded under the water %d times (%d not), spread at its top %d (%d not)"), Folded, NotFolded, Spread, NotSpread); }
		TestTrue(TEXT("groups of it, leaping"), Groups.Num() >= 6 && Leaps > 100);
		TestEqual(TEXT("drawn when the closed form has it drawn, and only then"), ShownDisagree, 0);
		TestEqual(TEXT("its middle where the closed form has it"), OffArc, 0);
		TestEqual(TEXT("leaving and diving back in at the sea's surface there and then"), NotAtSurface, 0);
		TestEqual(TEXT("its splash where its arc crosses the surface"), CrossingOff, 0);
		TestEqual(TEXT("under the water whole as it comes and goes, held under between leaps"), PokesOut + AboveOutside, 0);
		TestEqual(TEXT("never below the shallowest sea floor it lives over"), BelowFloor, 0);
		TestEqual(TEXT("its fish of its plan's sizes, its reach its longest's"), TooLong + ReachOff, 0);
		if (F == Ff::Eel)
		{
			TestTrue(TEXT("an eel's wave running back along it, more in the air than under the water"), MostWave > 8.0 && MostWaveMid > 1.0 && MostWaveMid < MostWave);
			// Long and thick enough to read from the shore and from a ship hovering over the sea: a metre to 1.8 m, a tenth of
			// its length thick or about.
			TestTrue(TEXT("its eels a metre to 1.8 m long, most of them well over a metre, a hand's breadth thick"), FishCount > 20 && ShortestFish >= 0.9 - 1e-4 && LongestFish <= 1.8 + 1e-4
				&& AllLength / FishCount > 1.1 && Plan.Girth >= 0.09f && 100.0 * Plan.Girth * ShortestFish >= 8.0);
		}
		else { TestTrue(TEXT("a spinefin's fins folded under the water, spread at its top"), Folded > 20 && NotFolded == 0 && Spread > 5 && NotSpread == 0); }
	}
	AddInfo(Lines);
	// Every eel plan a tenth of its length thick or about, every eel group's fish a metre long or more on its plan's sizes.
	int32 ThinEels = 0;
	for (const FAlienPlan& A : AlienKindPlans(EFaunaKind::Leaper)) { ThinEels += Ff::FormOf(A.Plan) != Ff::Eel || (A.Plan.Girth >= 0.09f - 1e-4f && A.Plan.Girth <= 0.12f + 1e-4f) ? 0 : 1; }
	TestEqual(TEXT("every eel plan's girth 0.09 to 0.12 of its length"), ThinEels, 0);
	// The splashes: an eel's three fifths of its length's (slim, it throws less than a fish of its length; its longest's
	// no bigger than a spinefin's longest), a spinefin's a third again; an Earth fish's its own.
	const FFaunaBodyPlan EelPlan = AlienKindPlan(EFaunaKind::Leaper, 0, 0.5f, 0.5f, 0.9f), FinPlan = AlienKindPlan(EFaunaKind::Leaper, 1, 0.5f, 0.5f, 0.9f);
	TestTrue(TEXT("splashes scaled to the fish"), SlonkFauna::SplashScaleOf(SlonkFauna::EarthPlan(EFaunaKind::Leaper)) == 1.0 && SlonkFauna::SplashScaleOf(EelPlan) < 0.9
		&& SlonkFauna::SplashScaleOf(FinPlan) > 1.2
		&& SlonkFauna::LeaperMaxSizeM(EelPlan) * SlonkFauna::SplashScaleOf(EelPlan) <= SlonkFauna::LeaperMaxSizeM(FinPlan) * SlonkFauna::SplashScaleOf(FinPlan));
	TestTrue(TEXT("an Earth fish's reach as it was"), SlonkFauna::LeaperMaxSizeM(SlonkFauna::EarthPlan(EFaunaKind::Leaper)) == SlonkFauna::Leaper::MaxSizeM);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaGlowOrgansTest, "Slonk.Fauna.GlowOrgans", SLONK_TEST_FLAGS)
bool FSlonkFaunaGlowOrgansTest::RunTest(const FString& Parameters)
{
	// Glow organs: none at strangeness 0 to 0.15 on any world's species of any kind, nor on any group of the Earth-like
	// world; at strangeness 1 about two in five alien species carry them, of every kind with alien forms. Every glowing
	// plan of every alien form (fliers, walkers, fish) has its row along its flanks on its body (its finest LOD glowing from
	// the side), and each of its coarser LODs glows about as much as its finest, within half as much again either way, from
	// either side and from where a player sees it from (a flier from under it, a herd and a fish from the air), its flanks
	// upright at every LOD and its speck's side standing over the row; its breath a whole number of cycles an hour, 0.7 to
	// 1; its row's material copy the material's as it stands.
	int32 Earthly = 0, Worlds = 0;
	for (const EPlanetArchetype Kind : FaunaKinds)
	{
		for (uint32 Hash = 1; Hash <= 120; ++Hash)
		{
			for (const float S : { 0.f, 0.1f, 0.15f })
			{
				const SlonkFauna::FFaunaProfile F = SlonkFauna::Roll(SlonkGenome::Roll(Kind, S, Hash * 7919u), Kind, Hash * 7919u);
				++Worlds;
				for (int32 K = 0; K < SlonkFauna::NumKinds; ++K)
				{
					for (int32 i = 0; i < F.Species[K].Num; ++i) { Earthly += F.Species[K].List[i].Plan.GlowOrgans == 0 ? 0 : 1; }
				}
			}
		}
	}
	int32 Glowing = 0, GroupsGlow = 0;
	{
		TArray<SlonkFauna::FFaunaGroup> Flocks, Herds, Leapers;
		GroupsOverGlobe(EarthFauna(), 1000, Flocks);
		HerdsOverGlobe(EarthHerds(), 4000, Herds);
		LeaperGroupsOverSeas(EarthSeas(), 20, Leapers);
		for (const TArray<SlonkFauna::FFaunaGroup>* Set : { &Flocks, &Herds, &Leapers }) { for (const SlonkFauna::FFaunaGroup& G : *Set) { GroupsGlow += G.Glow == 0.f && G.Plan.GlowOrgans == 0 ? 0 : 1; } }
	}
	// At strangeness 1.
	int32 Alien = 0, AlienGlowing = 0;
	int32 ByKind[SlonkFauna::NumKinds] = {};
	for (uint32 Hash = 1; Hash <= 600; ++Hash)
	{
		const SlonkFauna::FFaunaProfile F = SlonkFauna::Roll(SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 1.f, Hash * 7919u), EPlanetArchetype::Terrestrial, Hash * 7919u);
		for (int32 K = 0; K < SlonkFauna::NumKinds; ++K)
		{
			for (int32 i = 0; i < F.Species[K].Num; ++i)
			{
				const FFaunaBodyPlan& P = F.Species[K].List[i].Plan;
				if (SlonkFauna::IsEarthPlan((EFaunaKind)K, P) || (EFaunaKind)K == EFaunaKind::Floater) { continue; }
				++Alien;
				AlienGlowing += P.GlowOrgans > 0 ? 1 : 0;
				ByKind[K] += P.GlowOrgans > 0 ? 1 : 0;
			}
		}
	}
	Glowing = AlienGlowing;
	// Every glowing plan of every alien form, at the ends and the middle of its ranges.
	struct FGlowPlan { FFaunaBodyPlan Plan; EFaunaKind Kind; };
	TArray<FGlowPlan> Plans;
	for (const EFaunaKind K : { EFaunaKind::Flier, EFaunaKind::Herd, EFaunaKind::Leaper })
	{
		for (int32 Form = 0; Form < SlonkFauna::AlienForms(K); ++Form)
		{
			for (const float At : { 0.f, 0.5f, 1.f })
			{
				for (const float GlowDraw : { 0.05f, 0.35f })
				{
					const FFaunaBodyPlan P = AlienKindPlan(K, Form, At, 0.6f, GlowDraw);
					if (P.GlowOrgans > 0) { Plans.Add({ P, K }); }
				}
			}
		}
	}
	// The views a player has of each: a flier from either side and from under it (35 degrees up, off its quarter, as crew
	// on the ground see a flock: from over it its wings hide its flanks), a herd and a fish from either side and from the
	// air.
	const FVector Under = FVector(AirView().X, AirView().Y, -AirView().Z);
	int32 RowOff = 0, Unlit = 0, GlowOff = 0, CoverOff = 0;
	double RatioLo[3] = { 1e9, 1e9, 1e9 }, RatioHi[3] = {}, CoverLo = 1e9, CoverHi = 0.0;
	FString Firsts, OffAt;
	for (const FGlowPlan& G : Plans)
	{
		const SlonkFauna::GlowOrgan::FRow Row = SlonkFaunaMeshes::GlowRowOf(G.Plan);
		RowOff += Row.Spots == G.Plan.GlowOrgans && Row.U1 > Row.U0 && Row.RU > 0.f && Row.RV > 0.f ? 0 : 1;
		UE::Geometry::FDynamicMesh3 Lods[SlonkFaunaMeshes::NumLods];
		for (int32 L = 0; L < SlonkFaunaMeshes::NumLods; ++L) { SlonkFaunaMeshes::BuildLod(G.Plan, L, Lods[L]); }
		const TArray<FVector> Views = { FVector::YAxisVector, -FVector::YAxisVector, G.Kind == EFaunaKind::Flier ? Under : AirView() };
		for (int32 v = 0; v < Views.Num(); ++v)
		{
			const FVector2D G0 = SeeGlow(Lods[0], Views[v], Row);
			Unlit += v < 2 && G0.X <= 1.0 ? 1 : 0;
			for (int32 L = 1; L < SlonkFaunaMeshes::NumLods; ++L)
			{
				const FVector2D Gl = SeeGlow(Lods[L], Views[v], Row);
				const double Rg = Gl.X / FMath::Max(G0.X, 1e-3);
				RatioLo[L] = FMath::Min(RatioLo[L], Rg);
				RatioHi[L] = FMath::Max(RatioHi[L], Rg);
				const bool bOff = !(Rg >= 1.0 / 1.5 && Rg <= 1.5);
				GlowOff += bOff ? 1 : 0;
				if (bOff && OffAt.Len() < 2400)
				{
					OffAt += FString::Printf(TEXT("%s%s form %d view %d LOD %d: %.1f against %.1f cm2"), OffAt.IsEmpty() ? TEXT("") : TEXT("; "), SlonkFauna::KindName(G.Kind),
						G.Kind == EFaunaKind::Flier ? SlonkFauna::Wings::FormOf(G.Plan) : (G.Kind == EFaunaKind::Leaper ? SlonkFauna::FishForm::FormOf(G.Plan) : (int32)SlonkFauna::Walker::ShapeOf(G.Plan).Form), v, L, Gl.X, G0.X);
				}
				if (v < 2)
				{
					const double Rc = Gl.Y / FMath::Max(G0.Y, 1e-3);
					CoverLo = FMath::Min(CoverLo, Rc);
					CoverHi = FMath::Max(CoverHi, Rc);
				}
			}
			if (v == 0 && Firsts.Len() < 900)
			{
				Firsts += FString::Printf(TEXT("%s%s (%d a flank): %.0f cm2 of its LOD 0 lit from the side"), Firsts.IsEmpty() ? TEXT("") : TEXT("; "), SlonkFauna::KindName(G.Kind), G.Plan.GlowOrgans, G0.X);
			}
		}
	}
	// Its breath.
	int32 BreathOff = 0;
	{
		FRandomStream Rng(11);
		for (int32 c = 0; c < 200; ++c)
		{
			float Cu[SlonkFauna::Custom::Num] = {};
			Cu[SlonkFauna::Custom::Seed] = Rng.FRand();
			const float A = SlonkFauna::GlowBreath(0.f, Cu), B = SlonkFauna::GlowBreath(3600.f, Cu), M = SlonkFauna::GlowBreath(Rng.FRandRange(0.f, 3600.f), Cu);
			BreathOff += FMath::Abs(A - B) < 2e-3f && M >= SlonkFauna::GlowOrgan::Breath - 1e-4f && M <= 1.f + 1e-4f ? 0 : 1;
		}
	}
	// None on any Earth plan's row.
	int32 EarthRows = 0;
	for (int32 K = 0; K < SlonkFauna::NumKinds; ++K) { EarthRows += SlonkFaunaMeshes::GlowRowOf(SlonkFauna::EarthPlan((EFaunaKind)K)).Spots; }
	const FString Block = MaterialMotionBlock(EMotionBlock::Glow, true);
	const uint32 BlockCrc = FCrc::StrCrc32(*Block);
	AddInfo(FString::Printf(TEXT("%d worlds at strangeness 0, 0.1 and 0.15: %d species with glow organs; the Earth-like world's groups: %d glowing; at 1, %d of %d alien species carry them (%.0f%%: %d fliers', %d herds', %d fish's); the Earth plans' rows %d spots"),
		Worlds, Earthly, GroupsGlow, Glowing, Alien, 100.0 * Glowing / FMath::Max(Alien, 1), ByKind[(int32)EFaunaKind::Flier], ByKind[(int32)EFaunaKind::Herd], ByKind[(int32)EFaunaKind::Leaper], EarthRows));
	AddInfo(FString::Printf(TEXT("%d glowing plans: %d rows off their plan, %d dark from the side at LOD 0; LOD 1 glowing %.2f to %.2f as much as LOD 0 from either side and the air, LOD 2 %.2f to %.2f; %d views off; their side covering %.2f to %.2f; %s"),
		Plans.Num(), RowOff, Unlit, RatioLo[1], RatioHi[1], RatioLo[2], RatioHi[2], GlowOff, CoverLo, CoverHi, *Firsts));
	AddInfo(FString::Printf(TEXT("%d breaths off; fauna_material.py's glow organs' block %d characters, CRC 0x%08x (0x%08x when the copy was made)"), BreathOff, Block.Len(), BlockCrc, MaterialGlowBlockCrc));
	if (!OffAt.IsEmpty()) { AddInfo(FString::Printf(TEXT("off: %s"), *OffAt)); }
	TestEqual(TEXT("no glow organs to strangeness 0.15, none on the Earth-like world's groups or the Earth's plans"), Earthly + GroupsGlow + EarthRows, 0);
	TestTrue(TEXT("about two in five alien species carry them, of every kind with alien forms"), Alien > 1000 && (double)Glowing / Alien > 0.3 && (double)Glowing / Alien < 0.5
		&& ByKind[(int32)EFaunaKind::Flier] > 0 && ByKind[(int32)EFaunaKind::Herd] > 0 && ByKind[(int32)EFaunaKind::Leaper] > 0);
	TestTrue(TEXT("glowing plans of every alien form"), Plans.Num() >= 12);
	TestEqual(TEXT("each plan's row its own, lit on its body from the side"), RowOff + Unlit, 0);
	TestEqual(TEXT("the speck LODs glowing as much as the finest, within half as much again either way"), GlowOff, 0);
	TestEqual(TEXT("its breath 0.7 to 1, a whole number of cycles an hour"), BreathOff, 0);
	TestTrue(TEXT("fauna_material.py's glow organs' block found"), Block.Contains(TEXT("float gbody = ")));
	TestEqual(TEXT("GlowSpotAt and GlowBreath copy the material's glow organs as they are now"), BlockCrc, MaterialGlowBlockCrc);

	// Far off, the speck floor draws a glowing animal bigger than it is (its spots with it): their light is the fainter by
	// the square of that growth (GlowSpeckDim), so all the light an animal gives (its spots' drawn area, the square of its
	// drawn size over its distance, times their light) keeps falling as the square of its distance past the floor's onset,
	// as it does before it. The growth read off the vertex shader's copies (a point 10 cm of the unit mesh along its body,
	// against its middle): a four-winged and a membrane-winged flier, a walker; a fish's from its line, its full growth at
	// the top of its leap and its own size where it crosses the surface.
	int32 DimOff = 0, NotFalling = 0, PastOnset = 0;
	double MostDimOff = 0.0, FarthestDim = 1.0;
	{
		const FVector3f Along(10.f, 0.f, 0.f);
		const FLinearColor Rigid(0.f, 0.f, 0.5f, 0.f);
		for (const float SizeM : { 0.4f, 1.f, 2.5f })
		{
			for (int32 What = 0; What < 5; ++What)
			{
				double Before = TNumericLimits<double>::Max();
				for (int32 Step = 0; Step < 120; ++Step)
				{
					const float DistCm = 2.0e3f * FMath::Pow(1.06f, (float)Step);
					float Growth = 1.f, Dim = 1.f;
					if (What < 2)
					{
						float Cu[SlonkFauna::Custom::Num] = {};
						float Gd[SlonkFauna::GroupData::Num] = {};
						Cu[SlonkFauna::Custom::SizeM] = SizeM;
						const int32 Form = What == 0 ? SlonkFauna::Wings::Four : SlonkFauna::Wings::Membrane;
						Growth = (SlonkFauna::FlierMotion(Along, Rigid, 50.f, Cu, Gd, DistCm, 960.f, FVector2f(0.f, 0.f), Form) + Along - SlonkFauna::FlierMotion(FVector3f::ZeroVector, Rigid, 50.f, Cu, Gd, DistCm, 960.f, FVector2f(0.f, 0.f), Form)).Size() / 10.f;
						Dim = SlonkFauna::GlowSpeckDim(EFaunaKind::Flier, SizeM, DistCm, 960.f, Form);
					}
					else if (What == 2)
					{
						float Cu[SlonkFauna::Custom::Num] = {};
						float Gd[SlonkFauna::HerdData::Num] = {};
						Cu[SlonkFauna::Custom::SizeM] = SizeM;
						Cu[SlonkFauna::Custom::RateHz] = 0.6f * SizeM;
						Cu[SlonkFauna::Custom::PerKind] = 1.5f;
						Gd[SlonkFauna::HerdData::GroundHalfCm] = 3000.f;
						Growth = (SlonkFauna::WalkerMotion(Along, Along * SizeM, Rigid, 50.f, Cu, Gd, DistCm, 960.f) + Along - SlonkFauna::WalkerMotion(FVector3f::ZeroVector, FVector3f::ZeroVector, Rigid, 50.f, Cu, Gd, DistCm, 960.f)).Size() / 10.f;
						Dim = SlonkFauna::GlowSpeckDim(EFaunaKind::Herd, SizeM, DistCm, 960.f);
					}
					else
					{
						const float Share = What == 3 ? 0.5f : 0.f;
						const float Full = FMath::Clamp(2.f / FMath::Max(FMath::Max(SizeM, 0.1f) * 100.f * 960.f / DistCm, 0.001f), 1.f, SlonkFauna::Leaper::SpeckMax);
						Growth = 1.f + (Full - 1.f) * FMath::Sin(3.14159265f * Share);
						Dim = SlonkFauna::GlowSpeckDim(EFaunaKind::Leaper, SizeM, DistCm, 960.f, 0, Share);
					}
					const double Off = FMath::Abs((double)Dim * Growth * Growth - 1.0);
					MostDimOff = FMath::Max(MostDimOff, Off);
					DimOff += Off < 2e-3 ? 0 : 1;
					PastOnset += Growth > 1.01f ? 1 : 0;
					FarthestDim = FMath::Min(FarthestDim, (double)Dim);
					// All of its light: its drawn size's square over its distance's, times its spots' light.
					const double All = FMath::Square(Growth * SizeM * 100.0 / DistCm) * Dim;
					// Each step out is 6% further: a tenth fainter or more, not held where it was.
					NotFalling += All < Before * 0.99 ? 0 : 1;
					Before = All;
				}
			}
		}
	}
	const FString Pixel = MaterialMotionBlock(EMotionBlock::Fliers, true) + MaterialMotionBlock(EMotionBlock::Walkers, true) + MaterialMotionBlock(EMotionBlock::Leapers, true);
	const bool bDimmed = Pixel.Contains(TEXT("Emis = GlowColor * (Glow * Night * glit / (agk * agk));")) && Pixel.Contains(TEXT("Emis = GlowColor * (Glow * Night * glit / (wgk * wgk));"))
		&& Pixel.Contains(TEXT("Emis = GlowColor * (Glow * Night * glit / (lgk * lgk));")) && Pixel.Contains(TEXT("float wgk = clamp(3.0 / max(max(Size, 0.1) * 100.0 * PxPerRad / max(length(WP - ViewPos), 1.0), 0.001), 1.0, 4.0);"))
		&& Pixel.Contains(TEXT("float agk = clamp(2.0 / max(max(Size, 0.01) * 100.0 * PxPerRad / max(length(WP - ViewPos), 1.0), 0.001), 1.0, 3.0);"))
		&& Pixel.Contains(TEXT("float lgk = 1.0 + (lgr - 1.0) * sin(3.14159265 * saturate(lsh));"));
	AddInfo(FString::Printf(TEXT("glow organs past the speck floor's onset: %d moments grown past their size (their light down to %.3f of its own at the farthest); its dimming off the square of the vertex shader's growth by %.5f at the most (%d off); %d steps out where all the light an animal gives did not fall; the pixel shader's branches dim it so: %s"),
		PastOnset, FarthestDim, MostDimOff, DimOff, NotFalling, bDimmed ? TEXT("yes") : TEXT("no")));
	TestTrue(TEXT("animals far enough off that the speck floor grows them"), PastOnset > 100);
	TestEqual(TEXT("its spots' light over the square of the speck floor's growth of it"), DimOff, 0);
	TestEqual(TEXT("all the light an animal gives falls with distance past the speck floor's onset"), NotFalling, 0);
	TestTrue(TEXT("the material's branches light the glow organs so (fliers, walkers, fish)"), bDimmed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaPlanktonSolventTest, "Slonk.Fauna.PlanktonSolvent", SLONK_TEST_FLAGS)
bool FSlonkFaunaPlanktonSolventTest::RunTest(const FString& Parameters)
{
	// The plankton's glow starts from its sea's solvent: water's exactly the Earth-like look's blue-green (every world at
	// strangeness 0 has it, its look as it was), an ammonia sea's, a methane sea's and an acid sea's each its own, at least 60
	// degrees round the wheel from water's and from one another, at the water's saturation and as bright to the eye; where
	// there is no plankton (molten rock, no seas) the water's base and no glow. A strange world's then turns with its sky
	// (Slonk.Fauna.PlanktonParams), and its sky-jellies' light and its glow organs' are that glow: the look's, the sea's.
	auto Luma = [](const FLinearColor& C) { return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B; };
	const FLinearColor Water = SlonkFauna::PlanktonBase(ESolvent::Water);
	const ESolvent Alien[3] = { ESolvent::Ammonia, ESolvent::Methane, ESolvent::Acid };
	FString Bases;
	int32 TooNear = 0, Dimmer = 0, Paler = 0;
	for (int32 a = 0; a < 3; ++a)
	{
		const FLinearColor C = SlonkFauna::PlanktonBase(Alien[a]);
		Bases += FString::Printf(TEXT("%s %.2f %.2f %.2f (hue %.0f deg, luma %.3f); "), SlonkGenome::SolventName(Alien[a]), C.R, C.G, C.B, HueOf(C), Luma(C));
		TooNear += HueGap(HueOf(C), HueOf(Water)) >= 60.f ? 0 : 1;
		for (int32 b = a + 1; b < 3; ++b) { TooNear += HueGap(HueOf(C), HueOf(SlonkFauna::PlanktonBase(Alien[b]))) >= 60.f ? 0 : 1; }
		Dimmer += FMath::IsNearlyEqual(Luma(C), Luma(Water), 0.01f * Luma(Water)) ? 0 : 1;
		Paler += FMath::IsNearlyEqual(C.LinearRGBToHSV().G, Water.LinearRGBToHSV().G, 1e-3f) ? 0 : 1;
	}
	// The worlds: every archetype at strangeness 0 has water's look; at 1, each solvent's world its own, the jellies' and
	// the glow organs' light the sea's glow; none in molten or no seas.
	int32 ZeroOff = 0, Worlds = 0, LookOff = 0, DeadGlow = 0, DeadBase = 0;
	int32 BySolvent[6] = {};
	for (const EPlanetArchetype Kind : FaunaKinds)
	{
		for (uint32 Hash = 1; Hash <= 40; ++Hash)
		{
			const FWorldGenome G0 = SlonkGenome::Roll(Kind, 0.f, Hash * 7919u);
			ZeroOff += SlonkFauna::LookOf(G0).Glow == SlonkFauna::FFaunaLook().Glow ? 0 : 1;
			for (const float S : { 0.6f, 1.f })
			{
				const FWorldGenome G = SlonkGenome::Roll(Kind, S, Hash * 7919u);
				++Worlds;
				++BySolvent[FMath::Clamp((int32)G.Solvent, 0, 5)];
				const SlonkFauna::FFaunaProfile F = SlonkFauna::Roll(G, Kind, Hash * 7919u);
				const SlonkFauna::FSeaLife Sea = SlonkFauna::SeaLifeOf(1.f, G);
				LookOff += F.Look.Glow == Sea.PlanktonColor ? 0 : 1;
				if (G.Solvent == ESolvent::Magma || G.Solvent == ESolvent::None)
				{
					DeadGlow += Sea.PlanktonGlow == 0.f ? 0 : 1;
					DeadBase += SlonkFauna::PlanktonBase(G.Solvent) == Water ? 0 : 1;
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("water's base %.2f %.2f %.2f (hue %.0f deg, luma %.3f); %s%d pairs under 60 degrees apart, %d off water's brightness, %d off its saturation"),
		Water.R, Water.G, Water.B, HueOf(Water), Luma(Water), *Bases, TooNear, Dimmer, Paler));
	AddInfo(FString::Printf(TEXT("%d worlds at 0.6 and 1 (%d water, %d ammonia, %d methane, %d acid, %d molten, %d dry): %d looks off their sea's glow, %d molten or dry seas glowing, %d of them off water's base; %d worlds at 0 off the Earth's look"),
		Worlds, BySolvent[0], BySolvent[1], BySolvent[2], BySolvent[3], BySolvent[4], BySolvent[5], LookOff, DeadGlow, DeadBase, ZeroOff));
	TestTrue(TEXT("water's base exactly the Earth-like look's blue-green"), Water == SlonkFauna::FFaunaLook().Glow && SlonkFauna::FSeaLife().PlanktonColor == Water);
	TestEqual(TEXT("every world at strangeness 0 the Earth's look"), ZeroOff, 0);
	TestEqual(TEXT("each alien sea's its own hue, far round the wheel from the others"), TooNear, 0);
	TestEqual(TEXT("as bright to the eye and as saturated as water's"), Dimmer + Paler, 0);
	TestTrue(TEXT("seas of every solvent rolled"), BySolvent[1] > 0 && BySolvent[2] > 0 && BySolvent[3] > 0 && BySolvent[4] > 0);
	TestEqual(TEXT("the jellies' and the glow organs' light the sea's glow"), LookOff, 0);
	TestEqual(TEXT("none where there is no plankton, and no solvent's hue there"), DeadGlow + DeadBase, 0);

	// Turned round the wheel by every sky's turn (a degree at a time), an alien sea's glow is no brighter to the eye than its
	// own base, which is water's (so no brighter than water's own glow before it turns, under the locked exposure the night
	// seas, the jellies and the glow organs are lit by), nor its brightest channel past the base's; its hue the base's turned.
	// A water sea's turns as it always has: its brightest channel kept (so the Earth-like look and every water world are as
	// they were).
	int32 Brighter = 0, HueOff = 0, WaterOff = 0, Turns = 0;
	float MostOverBase = 0.f, WaterMost = 0.f;
	FString Brightest;
	for (const ESolvent Sv : { ESolvent::Water, ESolvent::Ammonia, ESolvent::Methane, ESolvent::Acid })
	{
		const FLinearColor Base = SlonkFauna::PlanktonBase(Sv);
		float Most = 0.f;
		for (int32 Deg = -180; Deg <= 180; ++Deg)
		{
			if (Deg == 0) { continue; }
			FWorldGenome G;
			G.Solvent = Sv;
			G.Strangeness = 1.f;
			G.SkyHueShiftDeg = (float)Deg;
			const FLinearColor Glow = SlonkFauna::LookOf(G).Glow;
			++Turns;
			HueOff += HueGap(HueOf(Glow), FMath::Fmod(HueOf(Base) + (float)Deg + 720.f, 360.f)) > 0.5f ? 1 : 0;
			const float Over = SlonkFauna::PlanktonLuma(Glow) / SlonkFauna::PlanktonLuma(Base);
			if (Sv == ESolvent::Water)
			{
				WaterOff += FMath::IsNearlyEqual(Glow.GetMax(), Base.GetMax(), 1e-4f) ? 0 : 1;
				WaterMost = FMath::Max(WaterMost, Over);
				continue;
			}
			Most = FMath::Max(Most, Over);
			Brighter += Over > 1.0001f || Glow.GetMax() > Base.GetMax() + 1e-4f ? 1 : 0;
		}
		if (Sv != ESolvent::Water)
		{
			MostOverBase = FMath::Max(MostOverBase, Most);
			Brightest += FString::Printf(TEXT("%s%s %.2f"), Brightest.IsEmpty() ? TEXT("") : TEXT(", "), SlonkGenome::SolventName(Sv), Most);
		}
	}
	AddInfo(FString::Printf(TEXT("%d turns of each solvent's glow: the brightest to the eye over its base %s (water's own up to %.2f of its base, its brightest channel kept, %d off it); %d alien glows brighter than their base, %d off their hue"),
		Turns / 4, *Brightest, WaterMost, WaterOff, Brighter, HueOff));
	TestEqual(TEXT("an alien sea's glow turned no brighter to the eye than its base, nor any channel past the base's"), Brighter, 0);
	TestEqual(TEXT("every glow its base's hue turned by the sky"), HueOff, 0);
	TestEqual(TEXT("a water sea's glow turned as it always was, its brightest channel kept"), WaterOff, 0);
	return true;
}

namespace
{
	/** The world "spawnplanet terrestrial strange s seed n" makes from the game's start, its fliers and its fish (its seas'
	 *  shoals as the terrain material paints them), full of life. */
	SlonkFauna::FFaunaWorld SpawnedSkiesAndSeas(int32 SeedPick, float S)
	{
		const uint32 Hash = PlaceHash(SpawnedPlace(SeedPick));
		FWorldGenome G = SlonkGenome::Roll(EPlanetArchetype::Terrestrial, S, Hash);
		SlonkGenome::SettleLock(G, EPlanetArchetype::Terrestrial, FVector::ZeroVector, false);
		SlonkFauna::FFaunaWorld W;
		W.P = SpawnedParams(SeedPick);
		W.Profile = SlonkFauna::Roll(G, EPlanetArchetype::Terrestrial, Hash);
		W.Life = 1.f;
		W.KindMask = SlonkFauna::KindBit(EFaunaKind::Flier) | SlonkFauna::KindBit(EFaunaKind::Leaper);
		W.Sea.ShoalDensity = SlonkFauna::SeaLifeOf(1.f, G).ShoalDensity;
		W.Sea.SwellCm = 120.f;
		return W;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaAlienSkiesSeedsTest, "Slonk.Fauna.AlienSkiesSeeds", SLONK_TEST_FLAGS)
bool FSlonkFaunaAlienSkiesSeedsTest::RunTest(const FString& Parameters)
{
	// The captures' worlds: of the seeds "spawnplanet terrestrial strange s seed n" rolls from the game's start, those with
	// a four-winged species, a membrane-winged one, an eel and a spinefin at a strangeness from 0.6 to 0.9, and how far from
	// the start's place ((0.3, 0.8, 0.5) of the world) the nearest flock or group of each is ("fauna goto flier 60 species
	// n", "fauna goto leaper 30 species n").
	TArray<FString> Picks[4];
	const FVector Start = FVector(0.3, 0.8, 0.5).GetSafeNormal();
	for (int32 Seed = 0; Seed <= 40; ++Seed)
	{
		if (Picks[0].Num() >= 2 && Picks[1].Num() >= 2 && Picks[2].Num() >= 2 && Picks[3].Num() >= 2) { break; }
		for (const float S : { 0.7f, 0.8f, 0.9f, 0.6f })
		{
			const SlonkFauna::FFaunaWorld W = SpawnedSkiesAndSeas(Seed, S);
			bool bAny = false;
			for (int32 Want = 0; Want < 4; ++Want)
			{
				if (Picks[Want].Num() >= 2) { continue; }
				const EFaunaKind K = Want < 2 ? EFaunaKind::Flier : EFaunaKind::Leaper;
				const SlonkFauna::FFaunaKindSpecies& Sp = W.Profile.Species[(int32)K];
				int32 Species = -1;
				for (int32 i = 0; i < Sp.Num && Species < 0; ++i)
				{
					const int32 F = K == EFaunaKind::Flier ? Wn::FormOf(Sp.List[i].Plan) : Ff::FormOf(Sp.List[i].Plan);
					if (F == (Want == 0 ? Wn::Four : (Want == 1 ? Wn::Membrane : (Want == 2 ? Ff::Eel : Ff::Spinefin)))) { Species = i; }
				}
				if (Species < 0) { continue; }
				SlonkFauna::FFaunaGroup G;
				SlonkFauna::FGroupSearch Search;
				if (!SlonkFauna::FindGroupNear(W, K, Start, 4.0e7, K == EFaunaKind::Flier ? 6000 : 60000, G, &Search, Species)) { continue; }
				const double Km = FVector::Dist(G.Centre, Start) * W.P.RadiusCm / 1e5;
				Picks[Want].Add(FString::Printf(TEXT("seed %d at %.1f: species #%d of %d (%.0f%% of the cells), the nearest %s %.0f km off (%d of %.2f m%s)"), Seed, S, Species, Sp.Num, 100.f * Sp.List[Species].Share,
					K == EFaunaKind::Flier ? SlonkFauna::HabitatName(G.Habitat) : TEXT("group"), Km, G.Members, G.SpanM, G.Plan.GlowOrgans > 0 ? TEXT(", glowing") : TEXT("")));
				bAny = true;
			}
			if (bAny) { break; }
		}
	}
	const TCHAR* Names[4] = { TEXT("four-winged fliers"), TEXT("membrane-winged fliers"), TEXT("eels"), TEXT("spinefins") };
	for (int32 Want = 0; Want < 4; ++Want) { AddInfo(FString::Printf(TEXT("spawnplanet terrestrial worlds with %s near the start: %s"), Names[Want], *FString::Join(Picks[Want], TEXT("; ")))); }
	TestTrue(TEXT("seeds for the captures of every alien flier and fish"), Picks[0].Num() > 0 && Picks[1].Num() > 0 && Picks[2].Num() > 0 && Picks[3].Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaStrangeWorldsLetGoTest, "Slonk.Fauna.StrangeWorldsLetGo", SLONK_TEST_FLAGS)
bool FSlonkFaunaStrangeWorldsLetGoTest::RunTest(const FString& Parameters)
{
	// A trip through a dozen strange worlds' profiles, one after another, by one world's fauna (as fauna strange gives it
	// a new one; a world left behind is suspended instead: Slonk.Fauna.SuspendedLetGo), each drawing a group of every
	// species it has (a mesh and a material instance for each of its alien plans), while a second world's fauna holds its
	// own the whole time: the meshes kept are only the Earth's and the two worlds' species' now (every profile left behind
	// lets go of its own, and comes back to them made anew), and the fauna's material instances only its world's alien
	// plans' (none of a profile left behind, so none is written to each frame); a world's fauna gone lets go of its meshes
	// too.
	UPlanetFaunaComponent* Trip = NewObject<UPlanetFaunaComponent>();
	UPlanetFaunaComponent* Stays = NewObject<UPlanetFaunaComponent>();
	auto ProfileAt = [](uint32 Hash) { return SlonkFauna::Roll(SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 1.f, Hash), EPlanetArchetype::Terrestrial, Hash); };
	auto Alien = [](const TPair<FFaunaBodyPlan, EFaunaKind>& P) { return !SlonkFauna::IsEarthPlan(P.Value, P.Key); };
	const SlonkFauna::FFaunaProfile Home = ProfileAt(7u * 7919u);
	Stays->SetProfile(Home);
	for (const TPair<FFaunaBodyPlan, EFaunaKind>& P : SlonkFauna::PlansOf(Home)) { SlonkFaunaMeshes::GetMesh(P.Key, P.Value); }
	const SlonkFauna::FFaunaProfile Earth = SlonkFauna::Roll(SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 0.f, 1u), EPlanetArchetype::Terrestrial, 1u);
	for (const TPair<FFaunaBodyPlan, EFaunaKind>& P : SlonkFauna::PlansOf(Earth)) { SlonkFaunaMeshes::GetMesh(P.Key, P.Value); }
	int32 MostMeshes = 0, MostMaterials = 0, Worlds = 0, AlienPlans = 0, NotKept = 0, LeftKept = 0, HomeLost = 0, EarthLost = 0, MaterialsOff = 0, MadeAgain = 0;
	TArray<TPair<FFaunaBodyPlan, EFaunaKind>> Before;
	TSet<uint32> HomeKeys;
	for (const TPair<FFaunaBodyPlan, EFaunaKind>& P : SlonkFauna::PlansOf(Home)) { HomeKeys.Add(P.Key.Hash()); }
	UStaticMesh* FirstMesh = nullptr;
	FFaunaBodyPlan FirstPlan;
	EFaunaKind FirstKind = EFaunaKind::Flier;
	for (uint32 w = 1; w <= 12; ++w)
	{
		const SlonkFauna::FFaunaProfile P = ProfileAt(w * 104729u);
		Trip->SetProfile(P);
		const TArray<TPair<FFaunaBodyPlan, EFaunaKind>> Plans = SlonkFauna::PlansOf(P);
		int32 Wanted = 0;
		for (const TPair<FFaunaBodyPlan, EFaunaKind>& Pl : Plans)
		{
			UStaticMesh* M = SlonkFaunaMeshes::GetMesh(Pl.Key, Pl.Value);
			if (Alien(Pl) && !FirstMesh && M) { FirstMesh = M; FirstPlan = Pl.Key; FirstKind = Pl.Value; }
			Trip->MaterialForPlan(Pl.Value, Pl.Key);
			const bool bOwn = Alien(Pl) && Pl.Value != EFaunaKind::Floater;
			AlienPlans += Alien(Pl) ? 1 : 0;
			Wanted += bOwn ? 1 : 0;
		}
		++Worlds;
		MostMeshes = FMath::Max(MostMeshes, SlonkFaunaMeshes::NumCachedMeshes());
		MostMaterials = FMath::Max(MostMaterials, Trip->NumPlanMaterials());
		// Its own instances: one for each alien plan (the herds' only for a plan whose rig is not the Earth grazer's, which
		// every alien walker's is not), none of the world before.
		MaterialsOff += Trip->NumPlanMaterials() == Wanted ? 0 : 1;
		for (const TPair<FFaunaBodyPlan, EFaunaKind>& Pl : Plans) { NotKept += SlonkFaunaMeshes::IsMeshCached(Pl.Key, Pl.Value) ? 0 : 1; }
		for (const TPair<FFaunaBodyPlan, EFaunaKind>& Pl : Before)
		{
			const bool bStill = Plans.ContainsByPredicate([&Pl](const TPair<FFaunaBodyPlan, EFaunaKind>& Q) { return Q.Key == Pl.Key && Q.Value == Pl.Value; }) || HomeKeys.Contains(Pl.Key.Hash());
			LeftKept += Alien(Pl) && !bStill && SlonkFaunaMeshes::IsMeshCached(Pl.Key, Pl.Value) ? 1 : 0;
		}
		for (const TPair<FFaunaBodyPlan, EFaunaKind>& Pl : SlonkFauna::PlansOf(Home)) { HomeLost += SlonkFaunaMeshes::IsMeshCached(Pl.Key, Pl.Value) ? 0 : 1; }
		for (const TPair<FFaunaBodyPlan, EFaunaKind>& Pl : SlonkFauna::PlansOf(Earth)) { EarthLost += SlonkFaunaMeshes::IsMeshCached(Pl.Key, Pl.Value) ? 0 : 1; }
		Before = Plans;
	}
	// Back to the first world: its plan's mesh made anew (the one let go is another object, freed once nothing draws it).
	Trip->SetProfile(ProfileAt(1u * 104729u));
	UStaticMesh* Again = FirstMesh ? SlonkFaunaMeshes::GetMesh(FirstPlan, FirstKind) : nullptr;
	MadeAgain = Again && Again != FirstMesh && IsValid(Again) ? 1 : 0;
	// An Earth-like world for it, then the second world's fauna gone: nothing alien kept.
	Trip->SetProfile(Earth);
	const int32 AfterEarth = Trip->NumPlanMaterials();
	Stays->DestroyComponent();
	int32 AlienLeft = 0;
	for (uint32 w = 1; w <= 12; ++w) { for (const TPair<FFaunaBodyPlan, EFaunaKind>& Pl : SlonkFauna::PlansOf(ProfileAt(w * 104729u))) { AlienLeft += Alien(Pl) && SlonkFaunaMeshes::IsMeshCached(Pl.Key, Pl.Value) ? 1 : 0; } }
	for (const TPair<FFaunaBodyPlan, EFaunaKind>& Pl : SlonkFauna::PlansOf(Home)) { AlienLeft += Alien(Pl) && SlonkFaunaMeshes::IsMeshCached(Pl.Key, Pl.Value) ? 1 : 0; }
	AddInfo(FString::Printf(TEXT("%d strange worlds of %d alien plans in all, one after another, while a second world's %d plans were held: at most %d meshes cached and %d material instances of plans; %d of a world's plans not kept while it was drawn, %d of the world before kept after it, %d of the second world's lost, %d of the Earth's; %d worlds with their instances off their alien plans; its first plan's mesh made anew on coming back: %s; an Earth-like world after: %d instances; the second world gone, %d alien meshes still cached"),
		Worlds, AlienPlans, SlonkFauna::PlansOf(Home).Num(), MostMeshes, MostMaterials, NotKept, LeftKept, HomeLost, EarthLost, MaterialsOff, MadeAgain ? TEXT("yes") : TEXT("no"), AfterEarth, AlienLeft));
	TestTrue(TEXT("strange worlds of many alien plans"), Worlds == 12 && AlienPlans > 40);
	TestEqual(TEXT("every plan of the world drawn now kept, its second world's and the Earth's too"), NotKept + HomeLost + EarthLost, 0);
	TestEqual(TEXT("a world left behind lets go of its meshes"), LeftKept, 0);
	TestTrue(TEXT("the meshes kept no more than the Earth's and two worlds' species'"), MostMeshes <= SlonkFauna::NumKinds + 2 * 3 * 4);
	TestTrue(TEXT("its material instances its world's alien plans' alone"), MaterialsOff == 0 && MostMaterials > 0 && MostMaterials <= 3 * 3 && AfterEarth == 0);
	TestTrue(TEXT("a world come back to makes its meshes anew"), MadeAgain == 1);
	TestEqual(TEXT("a world's fauna gone, nothing alien kept"), AlienLeft, 0);
	Trip->DestroyComponent();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaSuspendedLetGoTest, "Slonk.Fauna.SuspendedLetGo", SLONK_TEST_FLAGS)
bool FSlonkFaunaSuspendedLetGoTest::RunTest(const FString& Parameters)
{
	// Two strange worlds' fauna, each drawing a group of every species it has (a mesh and a material instance for each of
	// its alien plans). The one suspended (its terrain put away, beyond the horizon, or seen from too high for any of its
	// animals: a strange world left behind, or each one a spawnplanet run leaves about) lets go of the meshes of its alien
	// plans the other does not share, and of its material instances, while the other's and the Earth's are kept; suspended
	// again every frame, nothing more goes and nothing of the other's is made again. Drawn again, it holds its plans once
	// more, each mesh made anew once and kept so however often its groups are drawn, while the other, suspended in turn,
	// lets its own go. A suspension for the debug setting keeps them; both suspended, nothing alien of theirs is kept.
	using FPlan = TPair<FFaunaBodyPlan, EFaunaKind>;
	auto ProfileAt = [](uint32 Hash) { return SlonkFauna::Roll(SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 1.f, Hash), EPlanetArchetype::Terrestrial, Hash); };
	auto Alien = [](const FPlan& P) { return !SlonkFauna::IsEarthPlan(P.Value, P.Key); };
	auto Has = [](const TArray<FPlan>& Plans, const FPlan& P) { return Plans.ContainsByPredicate([&P](const FPlan& Q) { return Q.Key == P.Key && Q.Value == P.Value; }); };
	// How many of a world's alien plans that the other does not share have their meshes cached; how many of some plans do not.
	auto CachedOwn = [&](const TArray<FPlan>& Mine, const TArray<FPlan>& Other)
	{
		int32 N = 0;
		for (const FPlan& P : Mine) { N += Alien(P) && !Has(Other, P) && SlonkFaunaMeshes::IsMeshCached(P.Key, P.Value) ? 1 : 0; }
		return N;
	};
	auto NotCached = [](const TArray<FPlan>& Plans) { int32 N = 0; for (const FPlan& P : Plans) { N += SlonkFaunaMeshes::IsMeshCached(P.Key, P.Value) ? 0 : 1; } return N; };
	// A group of every species drawn: its material asked for (as SpawnSet does, its world's plans held first), its mesh got.
	auto Draw = [](UPlanetFaunaComponent* C, const TArray<FPlan>& Plans) { for (const FPlan& P : Plans) { C->MaterialForPlan(P.Value, P.Key); SlonkFaunaMeshes::GetMesh(P.Key, P.Value); } };
	auto Meshes = [](const TArray<FPlan>& Plans) { TArray<UStaticMesh*> Out; for (const FPlan& P : Plans) { Out.Add(SlonkFaunaMeshes::GetMesh(P.Key, P.Value)); } return Out; };
	const SlonkFauna::FFaunaProfile ProfA = ProfileAt(23u * 104729u), ProfB = ProfileAt(29u * 104729u);
	const TArray<FPlan> PlansA = SlonkFauna::PlansOf(ProfA), PlansB = SlonkFauna::PlansOf(ProfB);
	const TArray<FPlan> EarthPlans = SlonkFauna::PlansOf(SlonkFauna::Roll(SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 0.f, 1u), EPlanetArchetype::Terrestrial, 1u));
	for (const FPlan& P : EarthPlans) { SlonkFaunaMeshes::GetMesh(P.Key, P.Value); }
	UPlanetFaunaComponent* A = NewObject<UPlanetFaunaComponent>();
	UPlanetFaunaComponent* B = NewObject<UPlanetFaunaComponent>();
	A->SetProfile(ProfA);
	B->SetProfile(ProfB);
	Draw(A, PlansA);
	Draw(B, PlansB);
	int32 OwnA = 0, OwnB = 0;
	for (const FPlan& P : PlansA) { OwnA += Alien(P) && !Has(PlansB, P) ? 1 : 0; }
	for (const FPlan& P : PlansB) { OwnB += Alien(P) && !Has(PlansA, P) ? 1 : 0; }
	const int32 DrawnA = CachedOwn(PlansA, PlansB), MatsA0 = A->NumPlanMaterials(), MatsB = B->NumPlanMaterials();
	const TArray<UStaticMesh*> MeshesB = Meshes(PlansB);
	// The first world suspended, then again every frame of a hundred.
	A->Suspend();
	const int32 LeftA = CachedOwn(PlansA, PlansB), MatsA = A->NumPlanMaterials();
	for (int32 i = 0; i < 100; ++i) { A->Suspend(); }
	const int32 LostB = NotCached(PlansB) + NotCached(EarthPlans);
	const bool bSameB = Meshes(PlansB) == MeshesB && B->NumPlanMaterials() == MatsB;
	// Drawn again, and the other suspended.
	Draw(A, PlansA);
	const TArray<UStaticMesh*> MeshesA = Meshes(PlansA);
	const int32 BackA = CachedOwn(PlansA, PlansB), MatsBackA = A->NumPlanMaterials();
	B->Suspend();
	const int32 LeftB = CachedOwn(PlansB, PlansA), MatsLeftB = B->NumPlanMaterials();
	const int32 LostA = NotCached(PlansA) + NotCached(EarthPlans);
	for (int32 i = 0; i < 100; ++i) { Draw(A, PlansA); B->Suspend(); }
	const bool bSameA = Meshes(PlansA) == MeshesA;
	// A suspension for the debug setting, then both suspended.
	A->Suspend(true);
	const int32 KeptDebug = CachedOwn(PlansA, PlansB);
	A->Suspend();
	const int32 AlienLeft = CachedOwn(PlansA, {}) + CachedOwn(PlansB, {});
	const int32 EarthLost = NotCached(EarthPlans);
	AddInfo(FString::Printf(TEXT("two strange worlds of %d and %d alien plans their own: drawn, %d of the first's meshes cached and %d material instances; suspended, %d of them and %d instances kept, %d of the second world's and the Earth's lost, the second's made again: %s; drawn again, %d cached and %d instances, kept over a hundred more draws: %s; the second suspended, %d of its own and %d instances kept, %d of the first's and the Earth's lost; the debug's suspension kept %d; both suspended, %d alien meshes of theirs cached, %d of the Earth's lost"),
		OwnA, OwnB, DrawnA, MatsA0, LeftA, MatsA, LostB, bSameB ? TEXT("no") : TEXT("yes"), BackA, MatsBackA, bSameA ? TEXT("yes") : TEXT("no"), LeftB, MatsLeftB, LostA, KeptDebug, AlienLeft, EarthLost));
	TestTrue(TEXT("two strange worlds, each drawing alien plans of its own"), OwnA > 0 && OwnB > 0 && DrawnA == OwnA && MatsA0 > 0 && MatsB > 0);
	TestTrue(TEXT("a world suspended lets go of its own alien meshes and its material instances"), LeftA == 0 && MatsA == 0 && LeftB == 0 && MatsLeftB == 0);
	TestTrue(TEXT("the other world's and the Earth's kept, none made again"), LostB == 0 && LostA == 0 && bSameB);
	TestTrue(TEXT("drawn again, its meshes held once more and made once"), BackA == OwnA && MatsBackA == MatsA0 && bSameA);
	TestEqual(TEXT("the debug setting's suspension keeps them"), KeptDebug, OwnA);
	TestTrue(TEXT("both suspended, nothing alien of theirs kept, the Earth's kept"), AlienLeft == 0 && EarthLost == 0);
	A->DestroyComponent();
	B->DestroyComponent();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFaunaWatchGotoTest, "Slonk.Fauna.WatchGoto", SLONK_TEST_FLAGS)
bool FSlonkFaunaWatchGotoTest::RunTest(const FString& Parameters)
{
	// What fauna watch follows: the group the last fauna goto found while it is drawn and of the kind asked for (or any),
	// though another be nearer the view (the flock watched before the goto, or one the ship parked over); another kind asked
	// for, the nearest of it; the goto's group not drawn, or none found, the nearest as before. (The goto itself drops any
	// watch and gives the view back to the craft it moves: StarshipPawn's fauna goto.)
	using SlonkFauna::FDrawnGroupAt;
	const TArray<FDrawnGroupAt> Drawn = { { 1, EFaunaKind::Flier, FVector(100.0, 0.0, 0.0) }, { 2, EFaunaKind::Flier, FVector(5.0e4, 0.0, 0.0) }, { 3, EFaunaKind::Herd, FVector(50.0, 0.0, 0.0) },
		{ 4, EFaunaKind::Leaper, FVector(9.0e4, 0.0, 0.0) }, { 5, EFaunaKind::Floater, FVector(0.0, 3.0e5, 0.0) } };
	const FVector View = FVector::ZeroVector;
	int32 Off = 0;
	auto Expect = [&](int32 Kind, uint64 Goto, uint64 Want, const TCHAR* What)
	{
		const uint64 Got = SlonkFauna::PickWatched(Drawn, View, Kind, Goto);
		if (Got != Want) { ++Off; AddInfo(FString::Printf(TEXT("%s: followed %llu, not %llu"), What, Got, Want)); }
	};
	Expect(-1, 2, 2, TEXT("any kind after a goto: the goto's flock, though another is nearer"));
	Expect((int32)EFaunaKind::Flier, 2, 2, TEXT("fliers after a goto flier: the goto's flock"));
	Expect((int32)EFaunaKind::Leaper, 4, 4, TEXT("leapers after a goto leaper: the goto's shoal"));
	Expect((int32)EFaunaKind::Floater, 5, 5, TEXT("a sky-jelly after a goto floater, far off: that jelly"));
	Expect((int32)EFaunaKind::Herd, 2, 3, TEXT("herds asked for after a goto flier: the nearest herd"));
	Expect(-1, 99, 3, TEXT("the goto's group not drawn: the nearest of any kind"));
	Expect(-1, 0, 3, TEXT("no goto: the nearest of any kind"));
	Expect((int32)EFaunaKind::Flier, 0, 1, TEXT("no goto, fliers: the nearest flock"));
	TestEqual(TEXT("PickWatched of nothing drawn"), (int64)SlonkFauna::PickWatched({}, View, -1, 2), (int64)0);
	TestEqual(TEXT("a watch after a goto follows the group it found, while it is drawn and of the kind asked for"), Off, 0);

	// One watch after another (SlonkFauna::TakeWatched, as fauna watch picks): the watch after a goto takes its group and
	// forgets it, so every later watch, of any kind or of the goto's, follows the nearest again, not the goto's group
	// however far off it is; a watch that has not taken it (another kind asked for, or the group not drawn yet) leaves it
	// for the next. Fauna watch off forgets it too (StarshipPawn's own, read from its source).
	int32 TakeOff = 0;
	auto Take = [&](int32 Kind, uint64& Goto, uint64 Want, uint64 WantLeft, const TCHAR* What)
	{
		const uint64 Got = SlonkFauna::TakeWatched(Drawn, View, Kind, Goto);
		if (Got != Want || Goto != WantLeft) { ++TakeOff; AddInfo(FString::Printf(TEXT("%s: followed %llu, not %llu, and kept the goto's %llu, not %llu"), What, Got, Want, Goto, WantLeft)); }
	};
	uint64 Goto = 2;
	Take(-1, Goto, 2, 0, TEXT("the watch after a goto flier: its flock, forgotten then"));
	Take(-1, Goto, 3, 0, TEXT("a later watch of any kind: the nearest, not the goto's flock"));
	Take((int32)EFaunaKind::Flier, Goto, 1, 0, TEXT("a later watch of fliers: the nearest flock, not the goto's"));
	Goto = 4;
	Take((int32)EFaunaKind::Herd, Goto, 3, 4, TEXT("herds asked for after a goto leaper: the nearest herd, the goto's shoal kept for the next"));
	Take(-1, Goto, 4, 0, TEXT("then any: the goto's shoal, forgotten then"));
	Take(-1, Goto, 3, 0, TEXT("and after it the nearest"));
	Goto = 99;
	Take(-1, Goto, 3, 99, TEXT("the goto's group not drawn yet: the nearest, the goto's kept"));
	Goto = 0;
	Take(-1, Goto, 3, 0, TEXT("no goto: the nearest"));
	FString Pawn;
	FFileHelper::LoadFileToString(Pawn, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/Slonk/StarshipPawn.cpp")));
	const int32 OffAt = Pawn.Find(TEXT("if (ArgStr(2) == TEXT(\"off\"))"), ESearchCase::CaseSensitive);
	const int32 OffEnd = OffAt >= 0 ? Pawn.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, OffAt) : INDEX_NONE;
	const bool bOffForgets = OffEnd > OffAt && Pawn.Mid(OffAt, OffEnd - OffAt).Contains(TEXT("FaunaGotoId = 0;"), ESearchCase::CaseSensitive);
	const bool bWatchTakes = Pawn.Contains(TEXT("FaunaWatchId = F->NearestDrawnGroup(View, FaunaWatchKind, &FaunaGotoId);"), ESearchCase::CaseSensitive);
	AddInfo(FString::Printf(TEXT("%d watches one after another off what they should follow; fauna watch takes the goto's group so: %s; fauna watch off forgets it: %s"), TakeOff,
		bWatchTakes ? TEXT("yes") : TEXT("no"), bOffForgets ? TEXT("yes") : TEXT("no")));
	TestEqual(TEXT("only the watch after a goto follows its group first: once taken it is forgotten"), TakeOff, 0);
	TestTrue(TEXT("fauna watch takes it so, and fauna watch off forgets it"), bWatchTakes && bOffForgets);
	return true;
}

#endif
