// Slonk - automation tests for a world's flora: what it rolls (FloraProfile.h), the strange life forms as meshes
// (PlantMeshes.h), where each species stands (TerrainScatter.h) and what the terrain draws it in. Under "Slonk.Flora.";
// tools/run_tests.sh runs them headless.

#include "Misc/AutomationTest.h"
#include "../FloraProfile.h"
#include "../PlantMeshes.h"
#include "../PlanetBody.h"
#include "../PlanetTerrain.h"
#include "../PlanetTerrainComponent.h"
#include "../TerrainScatter.h"
#include "../WorldGenome.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/BodySetup.h"

#if WITH_DEV_AUTOMATION_TESTS

#ifndef SLONK_TEST_FLAGS
#define SLONK_TEST_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#endif

namespace SlonkFloraTestHelpers
{
	const EPlanetArchetype AllArchetypes[] = { EPlanetArchetype::Terrestrial, EPlanetArchetype::Ocean, EPlanetArchetype::Desert, EPlanetArchetype::Ice,
		EPlanetArchetype::Lava, EPlanetArchetype::GasGiant, EPlanetArchetype::Moon, EPlanetArchetype::Venus, EPlanetArchetype::Mars, EPlanetArchetype::IceGiant };
	const EStarLight AllLights[] = { EStarLight::Red, EStarLight::Orange, EStarLight::Yellow, EStarLight::White, EStarLight::Blue };
	const EFloraPlan AllStrangePlans[] = { EFloraPlan::StalkCap, EFloraPlan::Fan, EFloraPlan::Tubes, EFloraPlan::Bulb };
	const EFloraClass AllClasses[] = { EFloraClass::Grass, EFloraClass::Shrub, EFloraClass::Tree, EFloraClass::Giant };

	float LumaOf(const FLinearColor& C) { return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B; }

	/** A strange form of a plan for a class, every proportion at one place in its range. */
	FFloraForm FormAt(EFloraPlan Plan, EFloraClass Class, float At, int32 Parts, uint16 Variant)
	{
		FFloraForm F;
		F.Plan = Plan; F.Class = Class;
		F.Spread = At; F.Stem = At; F.Arch = At; F.Lean = At * 0.6f;
		F.Parts = (uint8)Parts; F.Variant = Variant;
		return F;
	}
	/** A species of a strange form, as much of its role as the Earth's. */
	FFloraSpecies SpeciesOf(const FFloraForm& Form)
	{
		FFloraSpecies Sp;
		Sp.bLive = true;
		Sp.Form = Form;
		Sp.UnitWideCm = SlonkPlants::CrownWideCm(Form);
		Sp.UnitFootCm = SlonkPlants::FootRadiusCm(Form);
		return Sp;
	}
	/** A mesh's vertices, colours and triangles as one number. */
	uint32 MeshCrcOf(const UE::Geometry::FDynamicMesh3& M)
	{
		TArray<uint8> B;
		auto Put = [&B](const void* P, int32 N) { B.Append((const uint8*)P, N); };
		const UE::Geometry::FDynamicMeshColorOverlay* C = M.Attributes()->PrimaryColors();
		for (const int32 V : M.VertexIndicesItr()) { const FVector3d P = M.GetVertex(V); Put(&P, sizeof(P)); const FVector4f Col = C->GetElement(V); Put(&Col, sizeof(Col)); }
		for (const int32 T : M.TriangleIndicesItr()) { const UE::Geometry::FIndex3i I = M.GetTriangle(T); Put(&I, sizeof(I)); }
		return FCrc::MemCrc32(B.GetData(), B.Num());
	}

	/** An Earth-like world, as APlanetBody::MakeTerrainParams gives one (SlonkTerrainTests.cpp's). */
	FPlanetTerrainParams FloraWorld()
	{
		FPlanetTerrainParams P;
		P.Seed = FVector(3.1, 7.7, 1.3);
		P.Canyons = 0.6f;
		P.Strata = 0.6f;
		P.RadiusCm = 6371.0 * 1.0e5;
		P.ReliefCm = SlonkTerrain::ReliefForRadius(0.0016, P.RadiusCm);
		P.OceanDepthCm = SlonkTerrain::ReliefForRadius(0.0008, P.RadiusCm);
		P.Mountains = 0.6f;
		P.Moisture = 0.55f;
		P.CanyonDepthCm = SlonkTerrain::ScaleWithWorld(1.6e5, P.RadiusCm);
		P.CanyonWidthCm = SlonkTerrain::ScaleWithWorld(1.6e6, P.RadiusCm);
		P.SnowlineCm = 5.0e5;
		return P;
	}
	FVector GlobeDir(int32 i, int32 N)
	{
		const double Z = 1.0 - 2.0 * (i + 0.5) / N;
		const double Rr = FMath::Sqrt(FMath::Max(1.0 - Z * Z, 0.0));
		const double A = i * UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0));
		return FVector(Rr * FMath::Cos(A), Rr * FMath::Sin(A), Z);
	}
	/** Directions over the globe, the best first by a score of the place (those it gives nothing left out). */
	TArray<FVector> BestPlaces(int32 Count, TFunctionRef<double(const FVector&)> Score)
	{
		struct FPlace { FVector Dir; double Score; };
		TArray<FPlace> Places;
		const int32 N = 3000;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector Dir = GlobeDir(i, N);
			const double Sc = Score(Dir);
			if (Sc > 0.0) { Places.Add({ Dir, Sc }); }
		}
		Places.Sort([](const FPlace& A, const FPlace& B) { return A.Score > B.Score; });
		TArray<FVector> Out;
		for (int32 i = 0; i < Places.Num() && Out.Num() < Count; ++i) { Out.Add(Places[i].Dir); }
		return Out;
	}
	/** Wooded lowlands, the thickest forest first. */
	TArray<FVector> Woods(const FPlanetTerrainParams& P, int32 Count)
	{
		return BestPlaces(Count, [&P](const FVector& Dir) { return FMath::Abs(Dir.Z) > 0.6 ? 0.0 : SlonkScatter::HabitatAt(P, Dir).Forest; });
	}
	int32 DepthOf(const FPlanetTerrainParams& P, double EdgeCm) { return FMath::CeilToInt32(FMath::Log2(P.RadiusCm * UE_DOUBLE_HALF_PI / EdgeCm)); }

	struct FPlants
	{
		TArray<FTransform> Xf[SlonkScatter::NumMeshes];
		TArray<double> Drawn[SlonkScatter::NumMeshes];
		int32 In(int32 Slot) const { return Xf[SlonkScatter::FloraMesh(Slot)].Num(); }
	};
	void BuildPlants(const FPlanetTerrainParams& P, const FScatterSettings& S, int32 Face, int32 Depth, int64 X, int64 Y, FPlants& Out)
	{
		UPlanetTerrainComponent::BuildChunkScatterForTest(P, S, Face, Depth, X, Y, 64, Out.Xf, Out.Drawn);
	}
	FScatterSettings LivingWith(const FFloraProfile& Flora)
	{
		FScatterSettings S;
		S.Life = 1.f;
		S.Flora = Flora;
		return S;
	}
	/** A flora with a second, strange species in every role. */
	FFloraProfile TwoToARole()
	{
		FFloraProfile F;
		F.Species[SlonkFlora::Slot::Warm2] = SpeciesOf(FormAt(EFloraPlan::StalkCap, EFloraClass::Tree, 0.5f, 1, 11));
		F.Species[SlonkFlora::Slot::Cold2] = SpeciesOf(FormAt(EFloraPlan::Fan, EFloraClass::Tree, 0.5f, 2, 12));
		F.Species[SlonkFlora::Slot::Cover2] = SpeciesOf(FormAt(EFloraPlan::Tubes, EFloraClass::Grass, 0.5f, 0, 13));
		F.Species[SlonkFlora::Slot::Bush2] = SpeciesOf(FormAt(EFloraPlan::Bulb, EFloraClass::Shrub, 0.5f, 0, 14));
		return F;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFloraEarthAtZeroTest, "Slonk.Flora.EarthAtZero", SLONK_TEST_FLAGS)
bool FSlonkFloraEarthAtZeroTest::RunTest(const FString& Parameters)
{
	// Every world near home, and every world only faintly strange (to 0.15), grows the Earth's four plants and nothing
	// else, whatever star it stands under: its flora is the one a flora is made as, its pigment changes no style, and
	// each of its species is drawn with the foliage material's own defaults.
	using namespace SlonkFloraTestHelpers;
	int32 Rolled = 0, Off = 0, StyleMoved = 0, LookMoved = 0;
	const FFloraProfile Earth;
	for (const float S : { 0.f, 0.1f, 0.15f })
	{
		for (const EStarLight Light : AllLights)
		{
			for (const EPlanetArchetype Kind : AllArchetypes)
			{
				for (uint32 k = 0; k < 40; ++k)
				{
					const uint32 Hash = 7919u * k + 13u;
					const FFloraProfile F = SlonkFlora::Roll(SlonkGenome::Roll(Kind, S, Hash), Kind, Hash, Light);
					++Rolled;
					Off += F.IsEarth() && F.SameSpecies(Earth) && F.Num() == 4 ? 0 : 1;
					FPlanetStyle Was, Now;
					SlonkFlora::ApplyPigment(F, 1.f, Now);
					StyleMoved += FPlanetStyle::StaticStruct()->CompareScriptStruct(&Was, &Now, PPF_None) ? 0 : 1;
					for (int32 Slot = 0; Slot < SlonkFlora::MaxSpecies; ++Slot)
					{
						const SlonkFlora::FFloraLook L = SlonkFlora::LookOf(F.Species[Slot]);
						const bool bDefault = L.Stem == FLinearColor(0.24f, 0.18f, 0.13f) && L.Gloss == 0.f && L.Translucence == 1.f && L.Glow == 0.f && L.GlowColor == FLinearColor::Black
							&& L.GlowForm == FLinearColor(0.f, 0.f, 0.f, 0.f);
						LookMoved += bDefault ? 0 : 1;
					}
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d floras rolled to strangeness 0.15: %d not the Earth's, %d styles moved, %d looks off the defaults"), Rolled, Off, StyleMoved, LookMoved));
	TestEqual(TEXT("every flora to strangeness 0.15 is the Earth's four"), Off, 0);
	TestEqual(TEXT("and its pigment changes no style"), StyleMoved, 0);
	TestEqual(TEXT("and each of its species draws by the material's defaults"), LookMoved, 0);
	TestTrue(TEXT("no species departs to 0.15"), SlonkFlora::DepartChance(0.f) == 0.f && SlonkFlora::DepartChance(0.15f) == 0.f && SlonkFlora::DepartChance(0.2f) > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFloraDepartureTest, "Slonk.Flora.Departure", SLONK_TEST_FLAGS)
bool FSlonkFloraDepartureTest::RunTest(const FString& Parameters)
{
	// As a world grows strange its plants depart from the Earth's: more worlds with a strange species at every tenth of
	// strangeness, nearly all of them at 1, and a world grown stranger keeps every strange species it had, in the same
	// form (its draws do not move). Its four roles are always filled; a role's two species are never of one plan; a
	// slot's species is of the slot's own class; none grows a giant under the strangeness giants begin at; and it has
	// between four species and the most a world may. Another star's light changes its pigment and moves no species.
	using namespace SlonkFloraTestHelpers;
	const int32 Worlds = 300;
	int32 Lost = 0, RoleEmpty = 0, Twins = 0, EarlyGiants = 0, WrongClass = 0, BadCount = 0, Unstable = 0, StarMoved = 0;
	int32 Strange[11] = {}, GiantsAtOne = 0, GlowingAtOne = 0, MostSpecies = 0;
	for (int32 w = 0; w < Worlds; ++w)
	{
		const uint32 Hash = 104729u * (uint32)w + 7u;
		const EStarLight Light = AllLights[w % 5];
		FFloraProfile Before;
		for (int32 t = 0; t <= 10; ++t)
		{
			const float S = t / 10.f;
			const FWorldGenome G = SlonkGenome::Roll(EPlanetArchetype::Terrestrial, S, Hash);
			const FFloraProfile F = SlonkFlora::Roll(G, EPlanetArchetype::Terrestrial, Hash, Light);
			Unstable += F.SameSpecies(SlonkFlora::Roll(G, EPlanetArchetype::Terrestrial, Hash, Light)) ? 0 : 1;
			const FFloraProfile Other = SlonkFlora::Roll(G, EPlanetArchetype::Terrestrial, Hash, AllLights[(w + 2) % 5]);
			int32 NumStrange = 0, Glowing = 0;
			for (int32 Slot = 0; Slot < SlonkFlora::MaxSpecies; ++Slot)
			{
				const FFloraSpecies& Sp = F.Species[Slot];
				StarMoved += Sp.bLive == Other.Species[Slot].bLive && (!Sp.bLive || Sp.Form == Other.Species[Slot].Form) ? 0 : 1;
				if (Slot < 4 && !Sp.bLive) { ++RoleEmpty; }
				if (Sp.bLive && Sp.Form.Class != SlonkFlora::ClassOf(Slot)) { ++WrongClass; }
				if (Sp.bLive && !Sp.Form.IsEarth()) { ++NumStrange; }
				if (Sp.bLive && Sp.GlowStrength > 0.f) { ++Glowing; }
				if (t > 0 && Before.Species[Slot].bLive && !Before.Species[Slot].Form.IsEarth() && (!Sp.bLive || Sp.Form != Before.Species[Slot].Form)) { ++Lost; }
				const int32 First = SlonkFlora::FirstOfRole(Slot);
				if (Slot != First && Sp.bLive && F.Species[First].bLive && F.Species[First].Form.Plan == Sp.Form.Plan) { ++Twins; }
			}
			if (S < SlonkFlora::GiantMinStrangeness && F.HasGiants()) { ++EarlyGiants; }
			if (F.Num() < 4 || F.Num() > SlonkFlora::MaxSpecies) { ++BadCount; }
			MostSpecies = FMath::Max(MostSpecies, F.Num());
			Strange[t] += NumStrange > 0 ? 1 : 0;
			if (t == 10) { GiantsAtOne += F.HasGiants() ? 1 : 0; GlowingAtOne += Glowing > 0 ? 1 : 0; }
			Before = F;
		}
	}
	bool bRises = true;
	for (int32 t = 1; t <= 10; ++t) { bRises &= Strange[t] >= Strange[t - 1]; }
	AddInfo(FString::Printf(TEXT("worlds of %d with a strange species by tenths of strangeness: %d %d %d %d %d %d %d %d %d %d %d; at 1, %d grow giants and %d glow; the most species on one world %d"), Worlds,
		Strange[0], Strange[1], Strange[2], Strange[3], Strange[4], Strange[5], Strange[6], Strange[7], Strange[8], Strange[9], Strange[10], GiantsAtOne, GlowingAtOne, MostSpecies));
	TestEqual(TEXT("a world grown stranger keeps every strange species it had"), Lost, 0);
	TestEqual(TEXT("the same world rolls the same flora"), Unstable, 0);
	TestEqual(TEXT("another star's light moves no species"), StarMoved, 0);
	TestEqual(TEXT("every role is filled"), RoleEmpty, 0);
	TestEqual(TEXT("a role's two species are never of one plan"), Twins, 0);
	TestEqual(TEXT("each species is of its slot's class"), WrongClass, 0);
	TestEqual(TEXT("no giant under the strangeness they begin at"), EarlyGiants, 0);
	TestEqual(TEXT("between four species and the most a world may grow"), BadCount, 0);
	TestTrue(TEXT("none strange to a tenth, more at every tenth after, nearly all at 1"), Strange[0] == 0 && Strange[1] == 0 && bRises && Strange[10] > Worlds * 9 / 10);
	TestTrue(TEXT("most of the strangest worlds grow giants, and some glow"), GiantsAtOne > Worlds / 2 && GlowingAtOne > Worlds / 10);
	TestTrue(TEXT("some world fills more than its four roles"), MostSpecies > 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFloraPigmentTest, "Slonk.Flora.Pigment", SLONK_TEST_FLAGS)
bool FSlonkFloraPigmentTest::RunTest(const FString& Parameters)
{
	// What grows under a star takes the pigment that suits its light: dark under a red dwarf (it takes all the light it
	// can), orange and red under the hot blue-white stars (it throws back the blue it has too much of). A star's light
	// is told from its colour as the star systems give it. The living ground wears the pigment as far as the flora
	// takes it: all the way on a full living world, half on a desert's sparse life, not at all on a dead world; the dry
	// ground a third as far; and nothing else of the style moves.
	using namespace SlonkFloraTestHelpers;
	int32 NotDark = 0, NotWarm = 0;
	for (int32 d = 0; d < 3; ++d)
	{
		NotDark += LumaOf(SlonkFlora::StarPigment(EStarLight::Red, (d + 0.5f) / 3.f)) < 0.12f ? 0 : 1;
		const FLinearColor B = SlonkFlora::StarPigment(EStarLight::Blue, (d + 0.5f) / 3.f);
		NotWarm += B.R > B.G && B.G > B.B ? 0 : 1;
	}
	TestEqual(TEXT("every pigment under a red dwarf is dark"), NotDark, 0);
	TestEqual(TEXT("every pigment under a blue-white star is orange or red"), NotWarm, 0);
	TestTrue(TEXT("a star's light by its colour: red dwarf, orange, the Sun's yellow, white, blue-white"),
		SlonkFlora::StarLightOf(FLinearColor(1.f, 0.48f, 0.30f)) == EStarLight::Red && SlonkFlora::StarLightOf(FLinearColor(1.f, 0.72f, 0.45f)) == EStarLight::Orange
		&& SlonkFlora::StarLightOf(FLinearColor(1.f, 0.86f, 0.62f)) == EStarLight::Yellow && SlonkFlora::StarLightOf(FLinearColor(1.f, 0.95f, 0.85f)) == EStarLight::White
		&& SlonkFlora::StarLightOf(FLinearColor(0.85f, 0.9f, 1.f)) == EStarLight::Blue && SlonkFlora::StarLightOf(FLinearColor(0.8f, 0.88f, 1.f)) == EStarLight::Blue);

	FFloraProfile F;
	F.Vivid = 1.f;
	F.Pigment = FLinearColor(0.1f, 0.05f, 0.2f);
	const FPlanetStyle Was;
	FPlanetStyle Full = Was, Half = Was, Dead = Was;
	SlonkFlora::ApplyPigment(F, 1.f, Full);
	SlonkFlora::ApplyPigment(F, 0.25f, Half);
	SlonkFlora::ApplyPigment(F, 0.f, Dead);
	auto Near = [](const FLinearColor& A, const FLinearColor& B) { return FMath::Abs(A.R - B.R) < 1e-5f && FMath::Abs(A.G - B.G) < 1e-5f && FMath::Abs(A.B - B.B) < 1e-5f; };
	TestTrue(TEXT("a full living world's ground takes the whole pigment"), Near(Full.LandColor, F.Pigment));
	TestTrue(TEXT("its dry ground a third of it"), Near(Full.LandColor2, FMath::Lerp(Was.LandColor2, F.Pigment, 1.f / 3.f)));
	TestTrue(TEXT("a desert's sparse life half of it"), Near(Half.LandColor, FMath::Lerp(Was.LandColor, F.Pigment, 0.5f)));
	TestTrue(TEXT("a dead world none"), FPlanetStyle::StaticStruct()->CompareScriptStruct(&Was, &Dead, PPF_None));
	Full.LandColor = Was.LandColor;
	Full.LandColor2 = Was.LandColor2;
	TestTrue(TEXT("nothing else of the style moves"), FPlanetStyle::StaticStruct()->CompareScriptStruct(&Was, &Full, PPF_None));
	const uint32 Hash = 7919u * 31u;
	const FFloraProfile Faint = SlonkFlora::Roll(SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 0.15f, Hash), EPlanetArchetype::Terrestrial, Hash, EStarLight::Red);
	const FFloraProfile Deep = SlonkFlora::Roll(SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 0.6f, Hash), EPlanetArchetype::Terrestrial, Hash, EStarLight::Red);
	TestTrue(TEXT("no pigment taken to strangeness 0.15, all of it from 0.6, and a red dwarf's is dark"), Faint.Vivid == 0.f && Deep.Vivid == 1.f && LumaOf(Deep.Pigment) < 0.14f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFloraFormsTest, "Slonk.Flora.Forms", SLONK_TEST_FLAGS)
bool FSlonkFloraFormsTest::RunTest(const FString& Parameters)
{
	// The strange worlds' plans as meshes, for every size class, at the ends and the middle of their proportions and
	// with every count of parts: each stands to the unit height on its foot at the origin and is as wide as it says it
	// is, between the narrowest and the widest a plant may be; each level of detail is within its class's triangles;
	// every vertex is a stem's or foliage's, solid, swaying no more than a tree's tips (and a giant's a fifth of that:
	// the wind moves a tip by a share of the plant's height); its foliage's UV lies in the unit square the glow is laid
	// in; every face shows the side its normals point to; the same form builds the same mesh every time, and another
	// seed another. A tree and a giant have a far crown, a closed shape of a few triangles wound outward, within the
	// plant and a good part of its width; and a capsule round the stalk, within the plant; a tuft and a bush have neither.
	using namespace SlonkFloraTestHelpers;
	int32 Forms = 0, BadLods = 0, OverBudget = 0, BadHeight = 0, BadWidth = 0, BadColour = 0, BadUv = 0, BadFacing = 0, Unsteady = 0, Alike = 0, BadCrown = 0, BadTrunk = 0;
	int32 MostTris[4][4] = {};
	double Narrowest = 1e9, Widest = 0.0;
	for (const EFloraPlan Plan : AllStrangePlans)
	{
		for (const EFloraClass Class : AllClasses)
		{
			const bool bTall = Class == EFloraClass::Tree || Class == EFloraClass::Giant;
			for (const float At : { 0.f, 0.5f, 1.f })
			{
				for (int32 Parts = 0; Parts <= 4; ++Parts)
				{
					const FFloraForm Form = FormAt(Plan, Class, At, Parts, 1);
					++Forms;
					if (SlonkPlants::NumLods(Form) != (bTall ? 4 : 3)) { ++BadLods; }
					FBox Box0(ForceInit);
					for (int32 Lod = 0; Lod < 3; ++Lod)
					{
						UE::Geometry::FDynamicMesh3 M;
						if (!SlonkPlants::BuildLod(Form, Lod, M) || M.TriangleCount() == 0) { ++BadLods; continue; }
						MostTris[(int32)Class][Lod] = FMath::Max(MostTris[(int32)Class][Lod], M.TriangleCount());
						if (M.TriangleCount() > SlonkPlants::MaxTriangles(Class, Lod)) { ++OverBudget; }
						const UE::Geometry::FDynamicMeshColorOverlay* C = M.Attributes()->PrimaryColors();
						const UE::Geometry::FDynamicMeshUVOverlay* U = M.Attributes()->PrimaryUV();
						FBox Box(ForceInit);
						double Far = 0.0;
						bool bColour = true, bUv = true;
						for (const int32 V : M.VertexIndicesItr())
						{
							const FVector P = (FVector)M.GetVertex(V);
							Box += P;
							Far = FMath::Max(Far, P.Size2D());
							const FVector4f Col = C->GetElement(V);
							bColour &= (Col.X == 0.f || Col.X == 1.f) && Col.W == 0.f && Col.Y >= 0.f && Col.Y <= (Class == EFloraClass::Giant ? 0.2001f : 1.f) && Col.Z >= 0.f && Col.Z <= 1.f;
							const FVector2f Uv = U->GetElement(V);
							if (Col.X == 1.f) { bUv &= Uv.X >= -1e-4f && Uv.X <= 1.0001f && Uv.Y >= -1e-4f && Uv.Y <= 1.0001f; }
						}
						BadColour += bColour ? 0 : 1;
						BadUv += bUv ? 0 : 1;
						double Bark = 0.0, Leaf = 0.0, Sky = 0.0;
						SlonkPlants::FacingShares(Form, Lod, Bark, Leaf, Sky);
						if (Bark < 0.99 || Leaf < 0.9) { ++BadFacing; }
						if (Lod == 0)
						{
							Box0 = Box;
							const double Wide = SlonkPlants::CrownWideCm(Form);
							Narrowest = FMath::Min(Narrowest, Wide);
							Widest = FMath::Max(Widest, Wide);
							if (Box.Max.Z < 92.0 || Box.Max.Z > 100.5 || Box.Min.Z < -7.0 || Box.Min.Z > 1.0) { ++BadHeight; }
							if (FMath::Abs(2.0 * Far - Wide) > 1.0 || Wide < 29.9 || Wide > 150.1) { ++BadWidth; }
							UE::Geometry::FDynamicMesh3 Again, Sibling;
							SlonkPlants::BuildLod(Form, 0, Again);
							SlonkPlants::BuildLod(FormAt(Plan, Class, At, Parts, 977), 0, Sibling);
							const uint32 Crc = MeshCrcOf(M);
							Unsteady += Crc == MeshCrcOf(Again) ? 0 : 1;
							Alike += Crc == MeshCrcOf(Sibling) ? 1 : 0;
						}
						else if (Box.Max.Z < 80.0 || Box.Max.Z > 100.5) { ++BadHeight; }
					}
					int32 Tris = 0; double Volume = 0.0, Facing = 0.0; FBox Crown, Near;
					const bool bHasCrown = SlonkPlants::FarCrownStats(Form, Tris, Volume, Facing, Crown, Near);
					SlonkPlants::FTrunk Trunk;
					const bool bHasTrunk = SlonkPlants::TrunkOf(Form, Trunk);
					if (bHasCrown != bTall || bHasTrunk != bTall) { ++BadCrown; continue; }
					if (!bTall) { continue; }
					MostTris[(int32)Class][3] = FMath::Max(MostTris[(int32)Class][3], Tris);
					if (Tris <= 0 || Tris > SlonkPlants::MaxTriangles(Class, 3) || Volume <= 0.0 || !Near.ExpandBy(3.0).IsInside(Crown) || Crown.GetSize().X < 0.3 * Near.GetSize().X
						|| Crown.GetSize().Z < 4.0) { ++BadCrown; }
					const double Reach = Trunk.Centre.Z + 0.5 * Trunk.Length + Trunk.Radius;
					if (Trunk.Radius <= 0.f || Trunk.Length <= 0.f || Reach > Box0.Max.Z + 1.0 || Trunk.Centre.Z - 0.5 * Trunk.Length - Trunk.Radius < Box0.Min.Z - Trunk.Radius - 1.0
						|| Trunk.Radius > 0.35 * FMath::Max(Box0.GetSize().X, Box0.GetSize().Y) + 1.0)
					{
						if (++BadTrunk <= 6)
						{
							AddInfo(FString::Printf(TEXT("trunk off: %s %s at %.1f with %d parts: radius %.1f, length %.1f, middle %.1f, in a box %.1f to %.1f tall and %.1f wide"), SlonkFlora::PlanName(Plan),
								SlonkFlora::ClassName(Class), At, Parts, Trunk.Radius, Trunk.Length, Trunk.Centre.Z, Box0.Min.Z, Box0.Max.Z, Box0.GetSize().X));
						}
					}
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d forms, %.0f to %.0f cm wide; the most triangles by level of detail: tuft %d %d %d, bush %d %d %d, tree %d %d %d and %d, giant %d %d %d and %d"), Forms, Narrowest, Widest,
		MostTris[0][0], MostTris[0][1], MostTris[0][2], MostTris[1][0], MostTris[1][1], MostTris[1][2], MostTris[2][0], MostTris[2][1], MostTris[2][2], MostTris[2][3],
		MostTris[3][0], MostTris[3][1], MostTris[3][2], MostTris[3][3]));
	AddInfo(FString::Printf(TEXT("off: lods %d, budget %d, height %d, width %d, colour %d, uv %d, facing %d, unsteady %d, alike %d, crown %d, trunk %d"), BadLods, OverBudget, BadHeight, BadWidth, BadColour,
		BadUv, BadFacing, Unsteady, Alike, BadCrown, BadTrunk));
	TestEqual(TEXT("three levels of detail, and a tree's and a giant's far crown"), BadLods, 0);
	TestEqual(TEXT("each level within its class's triangles"), OverBudget, 0);
	TestEqual(TEXT("each stands to the unit height on its foot"), BadHeight, 0);
	TestEqual(TEXT("each is as wide as it says, between the narrowest and the widest"), BadWidth, 0);
	TestEqual(TEXT("every vertex a stem's or foliage's, solid, swaying no more than it may"), BadColour, 0);
	TestEqual(TEXT("the foliage's UV in the unit square"), BadUv, 0);
	TestEqual(TEXT("every face shows the side its normals point to"), BadFacing, 0);
	TestEqual(TEXT("the same form builds the same mesh"), Unsteady, 0);
	TestEqual(TEXT("and another seed another"), Alike, 0);
	TestEqual(TEXT("a far crown for a tree and a giant, closed and within the plant, none for a tuft or a bush"), BadCrown, 0);
	TestEqual(TEXT("a capsule round a tree's and a giant's stalk, within the plant"), BadTrunk, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFloraSpeciesTest, "Slonk.Flora.Species", SLONK_TEST_FLAGS)
bool FSlonkFloraSpeciesTest::RunTest(const FString& Parameters)
{
	// Where each species stands. A world with the Earth's flora fills its four roles' slots and no other. Given a
	// second species in every role, the same plants stand in the same places, each role's shared between its two
	// species: both grow, in stands (a tree's nearest neighbour of its role is mostly its own species), and a tree is
	// of the same species on the finer chunks as on the coarser. A species' own height scales its plants' and moves
	// none; a colour of its own changes what is built; and the same chunk builds the same every time.
	using namespace SlonkFloraTestHelpers;
	namespace Slot = SlonkFlora::Slot;
	const FPlanetTerrainParams P = FloraWorld();
	const int32 D = DepthOf(P, 31000.0);
	const FFloraProfile Two = TwoToARole();
	FFloraProfile Tall;
	Tall.Species[Slot::Warm].Height = 1.5;
	FFloraProfile Coloured = Two;
	Coloured.Species[Slot::Warm2].Own = FLinearColor(0.6f, 0.1f, 0.5f);
	Coloured.Species[Slot::Warm2].OwnShare = 1.f;
	int32 Chunks = 0, Beyond = 0, Moved = 0, First = 0, Second = 0, SameNear = 0, Neighbours = 0, Kept = 0, Looked = 0, NotScaled = 0, Warm = 0, CrcMoved = 0, CrcSame = 0, CrcTried = 0;
	for (const FVector& Dir : Woods(P, 14))
	{
		int32 Face; int64 X, Y;
		if (!UPlanetTerrainComponent::ChunkAt(Dir, D, Face, X, Y)) { continue; }
		FPlants E, T, H;
		BuildPlants(P, LivingWith(FFloraProfile()), Face, D, X, Y, E);
		if (E.In(Slot::Warm) + E.In(Slot::Cold) < 40) { continue; }
		++Chunks;
		for (int32 S = 4; S < SlonkFlora::MaxSpecies; ++S) { Beyond += E.In(S); }
		BuildPlants(P, LivingWith(Two), Face, D, X, Y, T);
		Moved += T.In(Slot::Warm) + T.In(Slot::Warm2) == E.In(Slot::Warm) && T.In(Slot::Cold) + T.In(Slot::Cold2) == E.In(Slot::Cold) ? 0 : 1;
		First += T.In(Slot::Warm);
		Second += T.In(Slot::Warm2);
		// Stands: each lowland tree's nearest lowland neighbour, and whether it is of its own species.
		struct FTree { FVector At; int32 Slot; };
		TArray<FTree> Trees;
		for (const int32 S : { (int32)Slot::Warm, (int32)Slot::Warm2 }) { for (const FTransform& A : T.Xf[SlonkScatter::FloraMesh(S)]) { Trees.Add({ A.GetLocation(), S }); } }
		for (int32 i = 0; i < Trees.Num() && i < 600; ++i)
		{
			int32 Best = -1; double BestD = 1e30;
			for (int32 j = 0; j < Trees.Num(); ++j)
			{
				const double Dd = FVector::DistSquared(Trees[i].At, Trees[j].At);
				if (j != i && Dd < BestD) { BestD = Dd; Best = j; }
			}
			if (Best >= 0) { ++Neighbours; SameNear += Trees[Best].Slot == Trees[i].Slot ? 1 : 0; }
		}
		// The finer chunks: each tree found again in the same slot.
		if (Chunks <= 3)
		{
			FPlants Kids[4];
			for (int32 c = 0; c < 4; ++c) { BuildPlants(P, LivingWith(Two), Face, D + 1, X * 2 + (c & 1), Y * 2 + (c >> 1), Kids[c]); }
			for (const int32 S : { (int32)Slot::Warm, (int32)Slot::Warm2, (int32)Slot::Cold, (int32)Slot::Cold2 })
			{
				const int32 Mesh = SlonkScatter::FloraMesh(S);
				for (const FTransform& A : T.Xf[Mesh])
				{
					++Looked;
					bool bFound = false;
					for (const FPlants& Kid : Kids)
					{
						for (const FTransform& B : Kid.Xf[Mesh])
						{
							if (FVector::DistSquared(A.GetLocation(), B.GetLocation()) < FMath::Square(60.0)) { bFound = true; break; }
						}
						if (bFound) { break; }
					}
					Kept += bFound ? 1 : 0;
				}
			}
		}
		// A species half as tall again: the same trees in the same places, each half as tall again.
		BuildPlants(P, LivingWith(Tall), Face, D, X, Y, H);
		const TArray<FTransform>& Ew = E.Xf[SlonkScatter::FloraMesh(Slot::Warm)];
		const TArray<FTransform>& Hw = H.Xf[SlonkScatter::FloraMesh(Slot::Warm)];
		if (Ew.Num() != Hw.Num()) { ++NotScaled; }
		else
		{
			for (int32 i = 0; i < Ew.Num(); ++i)
			{
				++Warm;
				// The same place across the ground, to a few centimetres: a taller tree only sinks further along its own axis.
				const FVector Up = Ew[i].GetLocation().GetSafeNormal();
				const FVector Apart = Hw[i].GetLocation() - Ew[i].GetLocation();
				const bool bSamePlace = (Apart - Up * FVector::DotProduct(Apart, Up)).Size() < 5.0;
				NotScaled += bSamePlace && FMath::Abs(Hw[i].GetScale3D().Z / Ew[i].GetScale3D().Z - 1.5) < 1e-9 ? 0 : 1;
			}
		}
		if (Chunks <= 2)
		{
			++CrcTried;
			const uint32 A = UPlanetTerrainComponent::ChunkBuildCrcForTest(P, LivingWith(Two), Face, D, X, Y);
			CrcSame += A == UPlanetTerrainComponent::ChunkBuildCrcForTest(P, LivingWith(Two), Face, D, X, Y) ? 1 : 0;
			CrcMoved += A != UPlanetTerrainComponent::ChunkBuildCrcForTest(P, LivingWith(Coloured), Face, D, X, Y) ? 1 : 0;
		}
		if (Chunks >= 8) { break; }
	}
	AddInfo(FString::Printf(TEXT("%d wooded chunks: %d lowland trees of the first species and %d of the second; %d of %d nearest neighbours of a tree's own species; %d of %d trees found again on the finer chunks in their slots; %d trees half as tall again"),
		Chunks, First, Second, SameNear, Neighbours, Kept, Looked, Warm));
	if (!TestTrue(TEXT("found wooded chunks"), Chunks >= 4)) { return true; }
	TestEqual(TEXT("the Earth's flora fills no slot beyond its four roles"), Beyond, 0);
	TestEqual(TEXT("a second species moves no plant: each role's trees are shared between its two"), Moved, 0);
	TestTrue(TEXT("both of a role's species grow"), First > (First + Second) / 8 && Second > (First + Second) / 8);
	TestTrue(TEXT("in stands: a tree's nearest neighbour is mostly its own species"), Neighbours > 100 && SameNear > Neighbours * 7 / 10);
	TestTrue(TEXT("a tree is of the same species on the finer chunks"), Looked > 50 && Kept >= Looked * 85 / 100);
	TestTrue(TEXT("a species' own height scales its trees and moves none"), Warm > 20 && NotScaled == 0);
	TestEqual(TEXT("the same chunk builds the same every time"), CrcSame, CrcTried);
	TestEqual(TEXT("a species' own colour is in what is built"), CrcMoved, CrcTried);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFloraGiantsTest, "Slonk.Flora.Giants", SLONK_TEST_FLAGS)
bool FSlonkFloraGiantsTest::RunTest(const FString& Parameters)
{
	// A strange world's giants stand in groves on a lattice of their own: on a world whose flora has one, and on no
	// other (whose chunks are no coarser for them). Each is between the least and the most a giant may be tall,
	// upright, its foot in the drawn ground; the chunks a level finer carry the same giants in the same places, and
	// the far level above a share of them; a dead world and an airless one grow none, and none stands in the sea.
	// Nothing else the scatter places stands in a giant's foot: the trees and bushes that would grow there on a world
	// without the giant are left out.
	using namespace SlonkFloraTestHelpers;
	namespace Slot = SlonkFlora::Slot;
	const FPlanetTerrainParams P = FloraWorld();
	FFloraProfile WithGiant;
	WithGiant.Species[Slot::Giant] = SpeciesOf(FormAt(EFloraPlan::StalkCap, EFloraClass::Giant, 0.5f, 1, 21));
	const FScatterSettings Earth = LivingWith(FFloraProfile()), Giants = LivingWith(WithGiant);
	const double FaceEdge = SlonkScatter::FaceEdgeCm(P.RadiusCm);
	TestTrue(TEXT("a world without giants has chunks no coarser for them, one with them has"),
		SlonkScatter::MaxChunkEdgeCm(Earth, FaceEdge) == SlonkScatter::MaxChunkEdgeCm(LivingWith(TwoToARole()), FaceEdge) && SlonkScatter::MaxChunkEdgeCm(Giants, FaceEdge) > SlonkScatter::MaxChunkEdgeCm(Earth, FaceEdge));
	const int32 D = DepthOf(P, SlonkScatter::GiantMaxEdgeCm);
	const int32 Mesh = SlonkScatter::FloraMesh(Slot::Giant);
	int32 Chunks = 0, OnEarth = 0, Stood = 0, BadHeight = 0, Leaning = 0, Floating = 0, Sunk = 0, Kept = 0, Looked = 0, Above = 0, Below = 0, OnDead = 0, Wet = 0;
	int32 Feet = 0, InFoot = 0, WouldBeInFoot = 0;
	double Shortest = 1e9, Tallest = 0.0;
	const int32 TreeDepth = DepthOf(P, 31000.0);
	const double UnitFoot = WithGiant.Species[Slot::Giant].UnitFootCm;
	for (const FVector& Dir : BestPlaces(10, [&P](const FVector& At) { return FMath::Abs(At.Z) > 0.6 ? 0.0 : SlonkScatter::GiantDensityAt(P, At); }))
	{
		int32 Face; int64 X, Y;
		if (!UPlanetTerrainComponent::ChunkAt(Dir, D, Face, X, Y)) { continue; }
		FPlants G, E;
		BuildPlants(P, Giants, Face, D, X, Y, G);
		if (G.Xf[Mesh].Num() < 5) { continue; }
		++Chunks;
		BuildPlants(P, Earth, Face, D, X, Y, E);
		OnEarth += E.Xf[Mesh].Num();
		for (int32 i = 0; i < G.Xf[Mesh].Num(); ++i)
		{
			const FTransform& A = G.Xf[Mesh][i];
			++Stood;
			const double Tall = A.GetScale3D().Z * SlonkPlants::UnitCm;
			Shortest = FMath::Min(Shortest, Tall);
			Tallest = FMath::Max(Tallest, Tall);
			if (Tall < SlonkScatter::GiantMinCm * 0.54 || Tall > SlonkScatter::GiantMaxCm * 1.0001) { ++BadHeight; }
			if (FVector::DotProduct(A.GetRotation().GetAxisZ(), A.GetLocation().GetSafeNormal()) < FMath::Cos(FMath::DegreesToRadians(5.0))) { ++Leaning; }
			const double Foot = A.GetLocation().Size() - P.RadiusCm - G.Drawn[Mesh][i];
			if (Foot > 0.0) { ++Floating; }
			if (Foot < -(0.05 * Tall + 400.0)) { ++Sunk; }
			if (SlonkTerrain::Sample(P, A.GetLocation().GetSafeNormal(), 200.0).bWater) { ++Wet; }
			// Its foot, on the chunk the trees ride that holds it: clear of every tree and bush with the giant there,
			// and what would stand in it without.
			if (Feet < 24 && SlonkScatter::HabitatAt(P, A.GetLocation().GetSafeNormal()).Forest > 0.3)
			{
				int32 Tf; int64 Tx, Ty;
				if (UPlanetTerrainComponent::ChunkAt(A.GetLocation().GetSafeNormal(), TreeDepth, Tf, Tx, Ty))
				{
					++Feet;
					const double Clear = UnitFoot * A.GetScale3D().Z;
					FPlants With, Without;
					BuildPlants(P, Giants, Tf, TreeDepth, Tx, Ty, With);
					BuildPlants(P, Earth, Tf, TreeDepth, Tx, Ty, Without);
					const FVector At = A.GetLocation().GetSafeNormal() * P.RadiusCm;
					for (const int32 S : { (int32)Slot::Bush, (int32)Slot::Warm, (int32)Slot::Cold })
					{
						for (const FTransform& B : With.Xf[SlonkScatter::FloraMesh(S)]) { InFoot += FVector::Dist(B.GetLocation().GetSafeNormal() * P.RadiusCm, At) < Clear ? 1 : 0; }
						for (const FTransform& B : Without.Xf[SlonkScatter::FloraMesh(S)]) { WouldBeInFoot += FVector::Dist(B.GetLocation().GetSafeNormal() * P.RadiusCm, At) < Clear ? 1 : 0; }
					}
				}
			}
		}
		// The four chunks a level finer carry them all; the chunk a level coarser a share of them.
		FPlants Kids[4];
		for (int32 c = 0; c < 4; ++c) { BuildPlants(P, Giants, Face, D + 1, X * 2 + (c & 1), Y * 2 + (c >> 1), Kids[c]); }
		for (const FTransform& A : G.Xf[Mesh])
		{
			++Looked;
			bool bFound = false;
			for (const FPlants& Kid : Kids)
			{
				for (const FTransform& B : Kid.Xf[Mesh]) { if (FVector::DistSquared(A.GetLocation(), B.GetLocation()) < FMath::Square(200.0)) { bFound = true; break; } }
				if (bFound) { break; }
			}
			Kept += bFound ? 1 : 0;
		}
		FPlants Far;
		BuildPlants(P, Giants, Face, D - 1, X / 2, Y / 2, Far);
		Above += Far.Xf[Mesh].Num();
		for (int32 c = 0; c < 4; ++c)
		{
			FPlants Sib;
			BuildPlants(P, Giants, Face, D, (X / 2) * 2 + (c & 1), (Y / 2) * 2 + (c >> 1), Sib);
			Below += Sib.Xf[Mesh].Num();
		}
		FScatterSettings DeadWorld = Giants;
		DeadWorld.Life = 0.f;
		FPlants Dd;
		BuildPlants(P, DeadWorld, Face, D, X, Y, Dd);
		FPlanetTerrainParams Airless = P;
		Airless.bAirless = true;
		FPlants Aa;
		BuildPlants(Airless, Giants, Face, D, X, Y, Aa);
		OnDead += Dd.Xf[Mesh].Num() + Aa.Xf[Mesh].Num();
		if (Chunks >= 4) { break; }
	}
	int32 Seas = 0, InSea = 0;
	for (int32 i = 0; i < 400; ++i)
	{
		const FVector Dir = GlobeDir(i, 400);
		if (!SlonkTerrain::Sample(P, Dir, 200.0).bWater) { continue; }
		++Seas;
		InSea += SlonkScatter::GiantDensityAt(P, Dir) > 0.0 ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("%d giants on %d chunks, %.0f to %.0f m tall; %d of %d found again a level finer; a level coarser %d of the %d under it; in the feet of %d of them in the woods (%.1f cm a metre of height) %d trees and bushes stand, where %d would without the giant"),
		Stood, Chunks, Shortest / 100.0, Tallest / 100.0, Kept, Looked, Above, Below, Feet, UnitFoot, InFoot, WouldBeInFoot));
	if (!TestTrue(TEXT("found the giants' groves"), Chunks >= 2 && Stood >= 20)) { return true; }
	TestEqual(TEXT("none on a world whose flora has no giant"), OnEarth, 0);
	TestEqual(TEXT("each between the least and the most a giant may be tall"), BadHeight, 0);
	TestEqual(TEXT("each upright"), Leaning, 0);
	TestEqual(TEXT("no giant's foot stands clear of the drawn ground"), Floating, 0);
	TestEqual(TEXT("none sunk past its foot"), Sunk, 0);
	TestTrue(TEXT("nearly every giant is found again a level finer"), Kept >= Looked * 95 / 100);
	TestTrue(TEXT("a level coarser carries a share of them, thinned"), Above > Below / 5 && Above < Below * 4 / 5);
	TestEqual(TEXT("none on a dead world or an airless one"), OnDead, 0);
	TestTrue(TEXT("none stands in the sea"), Seas > 20 && InSea == 0 && Wet == 0);
	TestTrue(TEXT("a giant's foot is its own: nothing else stands in it, where trees would without the giant"), Feet > 3 && InFoot == 0 && WouldBeInFoot > 0 && UnitFoot > 0.5);
	TestTrue(TEXT("every giant is found again a level finer, in the same place"), Kept == Looked);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFloraMeshesTest, "Slonk.Flora.Meshes", SLONK_TEST_FLAGS)
bool FSlonkFloraMeshesTest::RunTest(const FString& Parameters)
{
	// The meshes the game draws a world's species with are made once a form and shared: the same form the same mesh,
	// another form another. A tree's has its far crown as a fourth level of detail and a capsule round its stalk; a
	// bush's three levels and none. A strange form's mesh is kept while a world holds the form and let go when none
	// does; the Earth's four are kept for good.
	using namespace SlonkFloraTestHelpers;
	const FFloraForm Tree = FormAt(EFloraPlan::Fan, EFloraClass::Tree, 0.3f, 2, 4242), Bush = FormAt(EFloraPlan::Bulb, EFloraClass::Shrub, 0.7f, 3, 77);
	UStaticMesh* A = SlonkPlants::GetMesh(Tree);
	UStaticMesh* B = SlonkPlants::GetMesh(Bush);
	if (!TestTrue(TEXT("a mesh for each form"), A != nullptr && B != nullptr)) { return true; }
	TestTrue(TEXT("the same form the same mesh, another form another"), SlonkPlants::GetMesh(Tree) == A && A != B);
	TestTrue(TEXT("a tree's four levels of detail and a capsule, a bush's three and none"), A->GetNumLODs() == 4 && B->GetNumLODs() == 3
		&& A->GetBodySetup() && A->GetBodySetup()->AggGeom.SphylElems.Num() == 1 && (!B->GetBodySetup() || B->GetBodySetup()->AggGeom.SphylElems.Num() == 0));
	for (const EPlant Kind : { EPlant::Grass, EPlant::Shrub, EPlant::Broadleaf, EPlant::Conifer }) { SlonkPlants::GetMesh(Kind); }
	int32 HolderA = 0, HolderB = 0;
	const FFloraForm OfA[] = { Tree }, OfB[] = { Bush };
	SlonkPlants::HoldForms(&HolderA, OfA, false);
	SlonkPlants::HoldForms(&HolderB, OfB);
	TestTrue(TEXT("held forms' meshes are kept"), SlonkPlants::IsMeshCached(Tree) && SlonkPlants::IsMeshCached(Bush));
	SlonkPlants::HoldForms(&HolderB, {});
	TestTrue(TEXT("a form no world holds is let go, one still held is kept"), SlonkPlants::IsMeshCached(Tree) && !SlonkPlants::IsMeshCached(Bush));
	SlonkPlants::HoldForms(&HolderA, {});
	TestTrue(TEXT("and let go in its turn"), !SlonkPlants::IsMeshCached(Tree));
	bool bEarthKept = true;
	for (const EPlant Kind : { EPlant::Grass, EPlant::Shrub, EPlant::Broadleaf, EPlant::Conifer }) { bEarthKept &= SlonkPlants::IsMeshCached(SlonkPlants::EarthForm(Kind)); }
	TestTrue(TEXT("the Earth's four are kept for good"), bEarthKept);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFloraMaterialsTest, "Slonk.Flora.Materials", SLONK_TEST_FLAGS)
bool FSlonkFloraMaterialsTest::RunTest(const FString& Parameters)
{
	// A world's terrain draws each species in the foliage material: the Earth's plants in the one all share, a strange
	// species in an instance of its own carrying its look, every parameter of which the material has (one it lacks
	// would be set and never read). A material built before the species' parameters has none of them: then every
	// species shares the one material, drawn as leaves. The terrain holds its flora's forms while it grows them, finds
	// a plant of a slot near a place, and lets the forms go when given the Earth's again.
	using namespace SlonkFloraTestHelpers;
	namespace Slot = SlonkFlora::Slot;
	const FPlanetTerrainParams P = FloraWorld();
	UPlanetTerrainComponent* T = NewObject<UPlanetTerrainComponent>();
	T->Configure(P, nullptr);
	T->SetScatterOptions(true, 1.f, false, 1.f, FCivilization());
	FFloraProfile F;
	FFloraSpecies Strange = SpeciesOf(FormAt(EFloraPlan::StalkCap, EFloraClass::Tree, 0.4f, 1, 501));
	Strange.Surface = EFloraSurface::Fungal;
	Strange.StemColor = FLinearColor(0.6f, 0.55f, 0.45f);
	Strange.Glow = EFloraGlow::Spots;
	Strange.GlowStrength = 2.5f;
	Strange.GlowColor = FLinearColor(0.1f, 0.8f, 0.7f);
	F.Species[Slot::Warm] = Strange;
	T->SetFlora(F);
	UMaterialInterface* Shared = T->GetFloraMaterial(Slot::Cover);
	UMaterialInterface* Own = T->GetFloraMaterial(Slot::Warm);
	if (!TestTrue(TEXT("the foliage material loads"), Shared != nullptr && Own != nullptr)) { return true; }
	if (!T->HasFloraMaterial())
	{
		AddWarning(TEXT("M_Foliage has no species parameters yet (tools/planet_materials/foliage_material.py builds them): strange plants are drawn as leaves"));
		TestTrue(TEXT("with no species parameters every species shares the foliage material"), Own == Shared);
	}
	else
	{
		const UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(Own);
		if (TestTrue(TEXT("a strange species has a material of its own, the Earth's share one"), M != nullptr && Own != Shared && T->GetFloraMaterial(Slot::Cold) == Shared))
		{
			int32 Unknown = 0;
			for (const FScalarParameterValue& V : M->ScalarParameterValues) { float X = 0.f; Unknown += M->Parent->GetScalarParameterValue(V.ParameterInfo, X) ? 0 : 1; }
			for (const FVectorParameterValue& V : M->VectorParameterValues) { FLinearColor X; Unknown += M->Parent->GetVectorParameterValue(V.ParameterInfo, X) ? 0 : 1; }
			AddInfo(FString::Printf(TEXT("%d scalar and %d vector parameters on a species' material; %d the foliage material does not have"), M->ScalarParameterValues.Num(), M->VectorParameterValues.Num(), Unknown));
			TestEqual(TEXT("the foliage material has every parameter a species sets"), Unknown, 0);
			const SlonkFlora::FFloraLook Look = SlonkFlora::LookOf(Strange);
			float Glow = 0.f, Gloss = -1.f; FLinearColor Stem, GlowColor;
			M->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Glow")), Glow);
			M->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Gloss")), Gloss);
			M->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("StemColor")), Stem);
			M->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("GlowColor")), GlowColor);
			TestTrue(TEXT("and it carries the species' look"), Glow == Look.Glow && Gloss == Look.Gloss && Stem == Look.Stem && GlowColor == Look.GlowColor && Look.Glow == 2.5f);
		}
	}
	// Its mesh made, as a set drawing it makes it: kept through another world's letting go while this one holds it.
	SlonkPlants::GetMesh(Strange.Form);
	int32 Elsewhere = 0;
	SlonkPlants::HoldForms(&Elsewhere, {});
	TestTrue(TEXT("the terrain holds its flora's forms"), SlonkPlants::IsMeshCached(Strange.Form));
	TArray<FVector> Wooded = Woods(P, 3);
	FVector Foot = FVector::ZeroVector; double Tall = 0.0;
	const bool bFound = Wooded.Num() > 0 && T->FindPlant(Slot::Warm, Wooded[0], 2.0e5, Foot, Tall);
	TestTrue(TEXT("and finds a tree of a slot in the woods, within the reach asked"),
		bFound && FMath::Acos(FMath::Clamp(FVector::DotProduct(Foot.GetSafeNormal(), Wooded[0]), -1.0, 1.0)) * P.RadiusCm < 2.0e5 && Tall > 300.0 && Tall < 2600.0);
	T->SetFlora(FFloraProfile());
	TestTrue(TEXT("given the Earth's flora again it lets the strange forms go"), !SlonkPlants::IsMeshCached(Strange.Form) && T->GetFloraMaterial(Slot::Warm) == T->GetFloraMaterial(Slot::Cover));
	return true;
}

#endif
