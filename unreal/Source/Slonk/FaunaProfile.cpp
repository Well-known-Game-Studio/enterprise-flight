#include "FaunaProfile.h"
#include "FaunaHabitat.h"
#include "FaunaMeshes.h"
#include "ScatterSpot.h"
#include "TerrainScatter.h"

namespace
{
	using SlonkFauna::FFaunaKindDesc;

	/**
	 * The kind table. The fliers' figures: a 1.2 km cell holds at most one flock, which flies a loop of a few
	 * kilometres round it at 9 to 20 m/s; flocks are spawned within 3.7 km of the camera's ground point and kept to
	 * 4.6 km (their loops reach 0.85 km from their middle, so one that comes at its reach or goes past that is beyond the
	 * 2.5 km their members fade by), at most 12 of them and 1,000 birds. With the caps full, the farthest out of sight
	 * (within reach or past it) give way to a flock 200 m nearer; where the caps are held by flocks it cannot take the
	 * place of (more in sight than the caps allow, or as far off as it), it waits, and may come in sight before there is
	 * room. A craft in the air (not set down) lower than 250 m over the ground and within 250 m of a flock, or someone
	 * on foot within 40 m, startles it: it turns and flees 220 m, and drifts back. Their shadows within 300 m. The material moves a bird's
	 * vertices up to 4 unit-mesh metres past its box (its wings, the ripple through the flock, a startle's scatter of up
	 * to two spans and the speck floor's growth: fauna_material.py), which every instance's box is padded by; their LOD
	 * screen sizes are set with that box (about 7.8 spans across, where a bird is 1 span), so a bird goes to its 12
	 * triangles about 40 pixels across and to its 2 about 4. A strange world's four-winged fliers (92 to 96 triangles,
	 * their long bodies about the mesh's middle) and membrane-winged ones (82 to 101) keep inside the same padding, their
	 * speck floor growing them at most three times (their darts fading as it grows: the closest of any vertex 73 units
	 * inside, Slonk.Fauna.AlienFliers), and come down to their 40 to 50 triangles and their 6 where a bird does; 1,000 of
	 * the most are 101,000 triangles at LOD 0.
	 * The motes are not the field's: the terrain's chunks carry them (SlonkScatter::PlaceMotes), clouds of 24 over cells of
	 * about 9 m, all of them on the chunks to 160 m and a quarter of them on the next level up, so on an Earth they fade
	 * out by about 460 m from the camera and the share kept by about 920 m (MoteMaxEdgeCm, MoteKeep, the plants' fade), far
	 * enough for a ship's chase camera to see the meadows twinkle; none are built while the camera and the pawn are both
	 * more than about a kilometre over the ground, the chunks carrying them not being made. A cloud 6 to 9 m across is 8
	 * cards past about 110 to 165 m and 2 past about 545 to 820 m (16 and 4 triangles, on the GPU instances:
	 * SlonkFaunaMeshes::MoteLodScreenSize), so their row here is the budget of those drawn whole, at 24 cards: 400 clouds,
	 * which nothing caps but Slonk.Fauna.MotesNest holds them to (about 260 in the densest country; some 1,700 clouds in
	 * all about a camera on an Earth's land out to their fades, 5,000 in the densest country, about 75,000 triangles
	 * there; the debug line's count, every cloud on the chunks showing, runs higher, a chunk carrying its clouds on past
	 * where they fade).
	 * The herds: a 2 km cell holds at most one, grazing round a loop of about 2 km a lap an hour on open, level grass,
	 * however lush (on an Earth-like world one for every five cells whose middle is that grass, its wettest lowland as
	 * well, nine with towns; its loop may cross a short stretch of scrub, a strip of trees or a gentle rise, and none of
	 * the ground its walkers stand on is water, a cliff, steep or a dense wood; it keeps 90 m from water and 500 m past
	 * its edge from a town's ground); herds are spawned within 3 km of the camera's ground point and kept to 3.75 km
	 * (their loops reach about 560 m from their middles, so one comes and goes past the 2.2 km their members fade by), at
	 * most 16 of them and 960 walkers (16 herds of the largest, 60), none while the camera is more than 2.5 km over the
	 * ground (so one comes and goes, as the camera climbs and comes down, past its members' fade too). A craft in the air
	 * (not set down) lower than 150 m over the ground and within 150 m of a herd's middle startles it: its walkers turn to
	 * flee where they stand (half a second; on the move it stops first, and turns back and round where it stands), then
	 * it gallops off 250 m (across the way its path goes if that walks during its fright; turned up to a quarter of a turn
	 * either way, or along its loop, to keep out of the water), at its fastest (8 to 11.5 m/s, a herd of smaller walkers
	 * the slower: no more than nine of their strides a second) 8 to 11 s in, loops round (25 m to its side) and from 40 s
	 * comes back at a steady pace no faster than about 2.1 m/s (its slowest trot is 2.4), on its path again 200 s after
	 * (frightened again, later by as much as it has further to come back); galloping, a second fright turns it on the
	 * run, or from ahead sends it on. Someone on foot it keeps 50 m from its nearest member (never walking into the water
	 * for them), and one who runs in to 20 m of it startles it. Their shadows within 600 m, where a walker is at
	 * least 3 pixels long and its shadow reads from the air at a low sun. The material moves a walker's vertices up to
	 * about 3.7 unit-mesh metres past its box (its round of its grazing circle, up to 1.93 of its lengths from its
	 * instance; the speck floor's growth to four times its size; its legs' stride; the ground under it, up to 1.5 m off its
	 * herd's plane), which every instance's box is padded by (4 m); their LOD screen sizes are set with that box (about 7.9
	 * lengths round, where a walker is 1), so a walker goes to its 30 triangles about 30 pixels long and to its 2 about 6
	 * (Slonk.Fauna.Walker). A strange world's six-legged grazers (to 3.4 m long) and insectoids (to 3.5 m, their feet splayed
	 * out to their sides) keep inside the same padding: the longer a walker the fewer of its unit mesh's centimetres its
	 * circle and its ground are, and its grazing circle no wider than leaves its farthest reach (a six-legged grazer's
	 * horns' tips, an insectoid's antennae and splayed feet) grown by the speck floor and lined up with its circle inside its
	 * own box and that (Walker::FShape::CircleShare, a bound by arithmetic: 4 units to spare at the worst, and the closest
	 * of any vertex of the cases drawn 68 inside, Slonk.Fauna.AlienWalkers); they go to their 32 or 38 triangles about
	 * 30 pixels long and to their 5 or 4 about 6; of at most 179 triangles at LOD 0, 960 of them are 171,840.
	 * The leapers: fish in groups of 1 to 5 leaping out of the shoals the terrain material paints (SlonkFauna::ShoalAt), a
	 * group in the cell of the shoals' own lattice (about 200 m) holding the shoal it leaps from, in half the shoals over
	 * water at least 3 m deep and 3 m deeper than the shoal swims; groups are spawned within 1.5 km of the camera's ground
	 * point and kept to 1.9 km (their fish fade by 1.2 km, so one comes and goes unseen), at most 6 of them and 30 fish,
	 * none while the camera is more than 500 m over the sea. A craft in the air lower than 50 m over the sea and within
	 * 50 m of a group sends it diving: no fish of it leaves the water for a minute after (SlonkFauna::Leaper::DiveSec; the
	 * crew on a shore are nothing to it). No shadows (each fish is out of the water a second at a time). The material moves
	 * a fish's vertices up to about 7.6 unit-mesh metres past its box (its leap out from its home, up to 1.2 of its lengths
	 * either way and 4.5 on; up 1.8 of them; the sea's surface it leaves, up to 1.5 m off its level, 4.7 of the shortest
	 * fish's lengths; the speck floor's growth to twice its size at the top of its leap), which every instance's box is
	 * padded by (8 m); their LOD screen sizes are set with that box (about 29 lengths round, where a fish is 1), so a fish
	 * goes to its fewer triangles about 30 pixels long and to its 2 about 6 (Slonk.Fauna.Fish). A strange world's eels (a
	 * metre to 1.8 m, a tenth of that thick, so one reads from the shore and from a ship hovering over the sea) and
	 * spinefins (to 1 m) leap the same arcs inside the same padding, an eel's wave and a spinefin's spread fins the closest
	 * of their vertices 85 units inside it (Slonk.Fauna.AlienFish); their habitat read out as far as their longest leap.
	 * The floaters: sky-jellies 20 to 80 m across, only on a living world at least 0.3 strange whose air is thick
	 * (SlonkFauna::FloatersLive), one in a 3 km cell in the share of the cells its world rolls (5 to 60%), drifting round a
	 * loop of 3 to 6 km 300 m to 3 km over the ground; spawned within 12 km of the camera's ground point and kept to 15 km
	 * (their loops reach at most 925 m from their middles, so one comes and goes past the 10.8 km they fade by), at most 24
	 * of them, none spawned while the camera is more than 15 km over the ground and none kept over 18.75 km, the ground
	 * a jelly's own: the higher of the ground under the scan's middle and its loop's highest (over which it drifts at most
	 * 3 km, its bob 60 m more and its crown, drawn at twice its size, 200 m more), so one comes and goes at least 11.7 km
	 * off, past its fade, as the camera climbs and comes down, however high the ground under its loop stands
	 * (Slonk.Fauna.FloaterGate). A craft in the air within 150 m of one (at any height under 5 km over the ground: they
	 * drift that high) startles it: it squeezes itself hard and away from it and down, 100 m off its loop at its fastest
	 * 7.4 m/s five seconds in, holds there and drifts back onto its loop by 150 s after; someone on foot is nothing to it, so
	 * far under it. Their shadows while it or where its shadow falls is within 3.5 km (a jelly overhead throws its dappled
	 * shadow on the ground; a high one's falls kilometres off at a low sun). The material moves a jelly's vertices up to
	 * about 85 unit-mesh cm past its box across (its tentacles trailing and undulating, up to 0.5 of their length) and up
	 * to its half-height and 14 more along it (the speck floor's growth to twice its size, about its middle, and its pulse),
	 * which every instance's box is padded by (110: 0.15 of its half-height and 20 more for any mesh, so it holds a
	 * half-height of up to 120, the longest-tentacled plan's); their LOD screen sizes are set with that box (3.2 to 3.8
	 * spans round, where a jelly is 1), so a jelly goes to its 60-odd triangles about 35 to 45 pixels across and to its 16
	 * about 6 to 7 (Slonk.Fauna.Jelly), which cover about as much of the sky as its finer LODs, seen from above, below and
	 * the side, and glow about as much (Slonk.Fauna.JellySpeck).
	 * 2,414 instances and 58 group sets in all.
	 */
	FFaunaKindDesc MakeDesc(EFaunaKind K)
	{
		FFaunaKindDesc D;
		D.Kind = K;
		switch (K)
		{
		case EFaunaKind::Flier:
			D.bLive = true;
			D.CellCm = 1.2e5; D.ReachCm = 3.7e5; D.MaxAltitudeCm = 2.5e5;
			D.GroupMin = 3; D.GroupMax = 120; D.MaxGroups = 12; D.MaxMembers = 1000;
			D.ScareRadiusCm = 2.5e4; D.ScareAltitudeCm = 2.5e4; D.CrewScareCm = 4.0e3; D.FleeCm = 2.2e4;
			D.ShadowReachCm = 3.0e4; D.FadeCm = 2.5e5;
			D.CullPaddingUnits = 400.0;
			D.LodScreenSize[0] = 1.f; D.LodScreenSize[1] = 0.55f; D.LodScreenSize[2] = 0.055f;
			break;
		case EFaunaKind::Herd:
			D.bLive = true;
			D.CellCm = 2.0e5; D.ReachCm = 3.0e5; D.MaxAltitudeCm = 2.5e5;
			D.GroupMin = 15; D.GroupMax = 60; D.MaxGroups = 16; D.MaxMembers = 960;
			D.ScareRadiusCm = 1.5e4; D.ScareAltitudeCm = 1.5e4; D.CrewScareCm = 2.0e3; D.FleeCm = 2.5e4;
			D.FleeRiseSec = 8.0; D.bGallop = true; D.FleeUp = 0.0; D.FleeTurnSec = 0.5; D.FleeLoopCm = 2.5e3; D.KeepFromCrewCm = 5.0e3;
			D.FrightSec = 200.0; D.FleeBackSec = 40.0;
			D.ShadowReachCm = 6.0e4; D.FadeCm = 2.2e5;
			D.CullPaddingUnits = 400.0;
			D.LodScreenSize[0] = 1.f; D.LodScreenSize[1] = 0.44f; D.LodScreenSize[2] = 0.088f;
			break;
		case EFaunaKind::Mote:
			D.CellCm = SlonkScatter::MoteSpacingCm; D.ReachCm = 9.2e4; D.MaxAltitudeCm = 1.0e5;
			D.GroupMin = SlonkFauna::Mote::MotesPerCloud; D.GroupMax = SlonkFauna::Mote::MotesPerCloud; D.MaxGroups = 0; D.MaxMembers = 400;
			D.FadeCm = 4.6e4;
			D.CullPaddingUnits = SlonkFauna::Mote::ReachCm;
			for (int32 L = 0; L < 3; ++L) { D.LodScreenSize[L] = SlonkFaunaMeshes::MoteLodScreenSize[L]; }
			break;
		case EFaunaKind::Leaper:
			D.bLive = true;
			D.CellCm = SlonkScatter::NoisePeriodCm / SlonkFauna::ShoalCellsPerPeriod; D.ReachCm = 1.5e5; D.MaxAltitudeCm = 5.0e4;
			D.GroupMin = 1; D.GroupMax = SlonkFauna::Leaper::MaxPerGroup; D.MaxGroups = 6; D.MaxMembers = 30;
			D.ScareRadiusCm = 5.0e3; D.ScareAltitudeCm = 5.0e3; D.FadeCm = 1.2e5;
			D.FrightSec = SlonkFauna::Leaper::DiveSec + 6.0; D.FleeRiseSec = 1.0; D.FleeUp = 0.0;
			D.CullPaddingUnits = 800.0;
			D.LodScreenSize[0] = 1.f; D.LodScreenSize[1] = 0.8f; D.LodScreenSize[2] = 0.16f;
			break;
		case EFaunaKind::Floater:
			D.bLive = true;
			D.CellCm = 3.0e5; D.ReachCm = 1.2e6; D.MaxAltitudeCm = 1.5e6;
			D.GroupMin = 1; D.GroupMax = 1; D.MaxGroups = 24; D.MaxMembers = 24;
			D.ScareRadiusCm = 1.5e4; D.ScareAltitudeCm = 5.0e5; D.CrewScareCm = 0.0; D.FleeCm = 1.0e4;
			D.FleeRiseSec = 5.0; D.FleeUp = -0.55; D.FrightSec = 150.0;
			D.ShadowReachCm = 3.5e5; D.FadeCm = 1.08e6;
			D.CullPaddingUnits = 110.0;
			D.LodScreenSize[0] = 1.f; D.LodScreenSize[1] = 0.25f; D.LodScreenSize[2] = 0.04f;
			break;
		default: break;
		}
		return D;
	}

	const FFaunaKindDesc GKinds[SlonkFauna::NumKinds] = { MakeDesc(EFaunaKind::Flier), MakeDesc(EFaunaKind::Herd), MakeDesc(EFaunaKind::Mote), MakeDesc(EFaunaKind::Leaper), MakeDesc(EFaunaKind::Floater) };

	/** A colour turned round the colour wheel, its saturation and brightness kept. */
	FLinearColor TurnPigment(const FLinearColor& C, float Deg)
	{
		if (Deg == 0.f) { return C; }
		FLinearColor Hsv = C.LinearRGBToHSV();
		Hsv.R = FMath::Fmod(Hsv.R + Deg + 720.f, 360.f);
		FLinearColor Out = Hsv.HSVToLinearRGB();
		Out.A = 1.f;
		return Out;
	}
}

const SlonkFauna::FFaunaKindDesc& SlonkFauna::KindDesc(EFaunaKind K)
{
	return GKinds[FMath::Clamp((int32)K, 0, NumKinds - 1)];
}

const TCHAR* SlonkFauna::KindName(EFaunaKind K)
{
	switch (K)
	{
	case EFaunaKind::Flier: return TEXT("flier");
	case EFaunaKind::Herd: return TEXT("herd");
	case EFaunaKind::Mote: return TEXT("mote");
	case EFaunaKind::Leaper: return TEXT("leaper");
	case EFaunaKind::Floater: return TEXT("floater");
	default: return TEXT("?");
	}
}

bool SlonkFauna::KindFromName(const FString& Name, EFaunaKind& Out)
{
	const FString N = Name.ToLower();
	for (int32 K = 0; K < NumKinds; ++K)
	{
		const FString Kn = KindName((EFaunaKind)K);
		if (N == Kn || N == Kn + TEXT("s")) { Out = (EFaunaKind)K; return true; }
	}
	if (N == TEXT("bird") || N == TEXT("birds")) { Out = EFaunaKind::Flier; return true; }
	if (N == TEXT("jelly") || N == TEXT("jellies")) { Out = EFaunaKind::Floater; return true; }
	return false;
}

const TCHAR* SlonkFauna::HabitatName(EFlierHabitat H)
{
	switch (H)
	{
	case EFlierHabitat::Woodland: return TEXT("woodland");
	case EFlierHabitat::Meadow: return TEXT("meadow");
	case EFlierHabitat::Coast: return TEXT("coast");
	case EFlierHabitat::Cliff: return TEXT("cliff");
	default: return TEXT("?");
	}
}

FFaunaBodyPlan SlonkFauna::EarthPlan(EFaunaKind K)
{
	FFaunaBodyPlan P;
	switch (K)
	{
	case EFaunaKind::Flier:
		// A bird: two feathered wings, a pair of legs tucked under it, a fanned tail.
		P.LimbPairs = 1; P.Limbs = EFaunaLimbs::Legs; P.Wings = EFaunaWings::Feathered;
		P.Length = 0.45f; P.Girth = 0.09f; P.Tail = 0.14f; P.Chord = 0.24f;
		break;
	case EFaunaKind::Herd:
		// Four legs, a long body.
		P.LimbPairs = 2; P.Limbs = EFaunaLimbs::Legs; P.Length = 1.f; P.Girth = 0.35f; P.Tail = 0.2f; P.Chord = 0.f;
		break;
	case EFaunaKind::Leaper:
		// A fish: fins, a tail, no legs.
		P.LimbPairs = 1; P.Limbs = EFaunaLimbs::Fins; P.Length = 1.f; P.Girth = 0.2f; P.Tail = 0.25f; P.Chord = 0.f;
		break;
	case EFaunaKind::Floater:
		// Radial: a bell and its trailing arms (none on the Earth; the shape a strange world's would start from).
		P.Symmetry = 8; P.LimbPairs = 4; P.Limbs = EFaunaLimbs::Tentacles; P.Length = 1.f; P.Girth = 1.f; P.Tail = 0.f; P.Chord = 0.f;
		break;
	default:
		// A mote is a speck.
		P.Symmetry = 1; P.Length = 1.f; P.Girth = 1.f; P.Tail = 0.f; P.Chord = 0.f;
		break;
	}
	return P;
}

SlonkFauna::FFaunaLook SlonkFauna::LookOf(const FWorldGenome& G)
{
	FFaunaLook L;
	const float Turn = G.SkyHueShiftDeg * G.Strangeness;
	L.HueTurnDeg = Turn;
	L.Vivid = FMath::Clamp(G.Strangeness, 0.f, 1.f);
	// The plankton's glow starts from its sea's solvent (water's blue-green at strangeness 0, where every sea is water),
	// and the sky-jellies' light and the glow organs' with it.
	const FLinearColor Base = PlanktonBase(G.Solvent);
	L.Glow = Base;
	if (Turn != 0.f)
	{
		// Round the colour wheel, its saturation and brightness kept: the plankton's glow, the fireflies', the pollen's and
		// the sky-jellies' pale bells.
		L.Glow = TurnPigment(Base, Turn);
		// An alien sea's glow is its own hue made as bright to the eye as water's, some of its channels past 1, and the turn
		// keeps its brightest channel: turned round the wheel it is no brighter to the eye than its own base (a water sea's
		// glow turns as it always has).
		if (Base != FFaunaLook().Glow)
		{
			const float Lb = PlanktonLuma(Base), Lt = PlanktonLuma(L.Glow);
			if (Lt > Lb) { L.Glow *= Lb / Lt; L.Glow.A = 1.f; }
		}
		L.Firefly = TurnPigment(L.Firefly, Turn);
		L.Pollen = TurnPigment(L.Pollen, Turn);
		L.Jelly = TurnPigment(L.Jelly, Turn);
	}
	return L;
}

bool SlonkFauna::ThickAir(const FWorldGenome& G)
{
	return G.LowHazeDensity > 0.f;
}

bool SlonkFauna::FloatersLive(const FWorldGenome& G)
{
	return G.Strangeness >= Floater::MinStrangeness && ThickAir(G);
}

float SlonkFauna::FloaterShareOf(const FWorldGenome& G, float Draw)
{
	if (!FloatersLive(G)) { return 0.f; }
	const float D = FMath::Clamp(Draw, 0.f, 1.f);
	const float Haze = FMath::Clamp(G.LowHazeDensity / Floater::HazeFull, 0.f, 1.f);
	return (Floater::MinShare + Floater::ShareSpan * D * D) * (0.6f + 0.4f * Haze);
}

FFaunaBodyPlan SlonkFauna::FloaterPlan(uint32 Hash)
{
	// A stream of its own: the profile's draws, and every other roll of the world, stay where they were.
	FRandomStream R((int32)(Hash * 0x2C1B3C6Du + 0x297A2D39u));
	FFaunaBodyPlan P = EarthPlan(EFaunaKind::Floater);
	const uint8 Ribs[3] = { 6, 8, 12 };
	P.Symmetry = Ribs[FMath::Min((int32)(R.FRand() * 3.f), 2)];
	P.LimbPairs = (uint8)(3 + FMath::Min((int32)(R.FRand() * 4.f), 3));
	P.Length = FMath::Lerp(Floater::MinBell, Floater::MaxBell, R.FRand());
	P.Tail = P.Length * FMath::Lerp(Floater::MinTentacles, Floater::MaxTentacles, R.FRand());
	P.Girth = 1.f;
	P.Chord = FMath::Lerp(0.04f, 0.1f, R.FRand());
	P.GlowOrgans = P.Symmetry;
	return P;
}

FLinearColor SlonkFauna::FloaterPigment(const FFaunaLook& L, double Turn, double Vary)
{
	FLinearColor C = TurnPigment(L.Jelly, (float)(12.0 * FMath::Clamp(Turn, -1.0, 1.0))) * (float)Vary;
	C.A = 1.f;
	return C;
}

float SlonkFauna::DepartChance(float Strangeness)
{
	const float S = FMath::Clamp(Strangeness, 0.f, 1.f);
	return S <= 0.15f ? 0.f : 0.95f * (float)SlonkScatter::SmoothStep(0.15, 0.95, (double)S);
}

int32 SlonkFauna::AlienForms(EFaunaKind K)
{
	// The herds: six-legged grazers and insectoids; the fliers: four-winged and membrane-winged; the fish: eels and
	// spinefins. The jellies keep a plan of their own, and the motes are specks.
	return K == EFaunaKind::Herd || K == EFaunaKind::Flier || K == EFaunaKind::Leaper ? 2 : 0;
}

FLinearColor SlonkFauna::PlanktonBase(ESolvent S)
{
	const FLinearColor Water = FFaunaLook().Glow;
	float Hue = 0.f;
	switch (S)
	{
	// An ammonia sea's an amber over its ink-blue, a methane sea's a rose-magenta over its black, an acid sea's a violet
	// over its yellow-green: each at least 70 degrees round the wheel from the water's and the others'.
	case ESolvent::Ammonia: Hue = 40.f; break;
	case ESolvent::Methane: Hue = 330.f; break;
	case ESolvent::Acid: Hue = 260.f; break;
	// Water's blue-green, and where there are no plankton (molten rock, no seas at all) the same.
	default: return Water;
	}
	// The water's saturation, and as bright to the eye (its luma the water's).
	FLinearColor C = FLinearColor(Hue, Water.LinearRGBToHSV().G, 1.f, 1.f).HSVToLinearRGB();
	C = C * (PlanktonLuma(Water) / FMath::Max(PlanktonLuma(C), 1e-3f));
	C.A = 1.f;
	return C;
}

bool SlonkFauna::IsEarthPlan(EFaunaKind K, const FFaunaBodyPlan& P)
{
	return P == EarthPlan(K);
}

FFaunaBodyPlan SlonkFauna::AlienPlan(EFaunaKind K, int32 Form, const float* Shape, float& OutSizeScale)
{
	FFaunaBodyPlan P = EarthPlan(K);
	OutSizeScale = 1.f;
	if (K != EFaunaKind::Herd && K != EFaunaKind::Flier && K != EFaunaKind::Leaper) { return P; }
	auto At = [Shape](int32 i, float Lo, float Hi) { return FMath::Lerp(Lo, Hi, FMath::Clamp(Shape[i], 0.f, 1.f)); };
	// Its glow organs, from a draw of their own: two in five alien plans carry a row of Lo to Lo + Span spots along each
	// flank.
	auto Glows = [Shape](int32 Lo, int32 Span) { const float G = FMath::Clamp(Shape[6], 0.f, 1.f); return G < 0.4f ? (uint8)(Lo + FMath::Min((int32)(G / 0.4f * (Span + 1)), Span)) : (uint8)0; };
	if (K == EFaunaKind::Flier)
	{
		P.Symmetry = 2;
		P.Limbs = EFaunaLimbs::None;
		if (Form == 0)
		{
			// A four-winged flier: a long, slim body 0.72 to 0.95 of its span (its abdomen 0.55 to 0.7 of it), two pairs of
			// narrow wings 0.1 to 0.15 of its span in chord, a big head; a forked tail, antennae, both or neither; a bird's size
			// from three quarters to a third again.
			P.Wings = EFaunaWings::FourWings;
			P.Segments = 3;
			P.LimbPairs = 0;
			P.Length = At(0, 0.72f, 0.95f);
			P.Girth = At(1, 0.045f, 0.075f);
			P.Tail = At(2, 0.55f, 0.7f);
			P.Chord = At(3, 0.1f, 0.15f);
			P.Neck = At(4, 0.06f, 0.09f);
			P.Ornament = (uint8)FMath::Min((int32)(Shape[5] * 4.f), 3);
			P.GlowOrgans = Glows(3, 3);
			OutSizeScale = At(SpeciesShapeDraws - 1, 0.75f, 1.3f);
		}
		else
		{
			// A membrane-winged flier: wings of skin over three or four finger struts, 0.3 to 0.42 of its span in chord at
			// their root, a short body 0.3 to 0.42 of its span, a whip of a tail 0.15 to 0.45 of it, a long head; a crest, a
			// vane on its tail, both or neither; from a tenth again to seven tenths again a bird's size.
			P.Wings = EFaunaWings::Membrane;
			P.Segments = 1;
			const float U5 = FMath::Clamp(Shape[5], 0.f, 0.9999f) * 4.f;
			P.Ornament = (uint8)FMath::FloorToInt32(U5);
			P.LimbPairs = U5 - FMath::FloorToFloat(U5) < 0.5f ? 3 : 4;
			P.Length = At(0, 0.3f, 0.42f);
			P.Girth = At(1, 0.1f, 0.15f);
			P.Tail = At(2, 0.15f, 0.45f);
			P.Chord = At(3, 0.3f, 0.42f);
			P.Neck = At(4, 0.08f, 0.16f);
			P.GlowOrgans = Glows(3, 3);
			OutSizeScale = At(SpeciesShapeDraws - 1, 1.1f, 1.7f);
		}
		return P;
	}
	if (K == EFaunaKind::Leaper)
	{
		P.Symmetry = 2;
		P.Limbs = EFaunaLimbs::Fins;
		P.Wings = EFaunaWings::None;
		P.Length = 1.f;
		if (Form == 0)
		{
			// An eel: slim (its girth 0.09 to 0.12 of its length, a hand's breadth thick on one of a metre and a half), a
			// ribbon of fin 0.3 to 0.6 of its depth high along its back and belly to its tail, its head 0.08 to 0.14 of it;
			// barbels or none; a metre to 1.8 m long, well longer than an Earth fish (its own lengths: LeaperInCell), so it
			// reads from the shore and from a ship hovering over the sea.
			P.Segments = 2;
			P.LimbPairs = 1;
			P.Girth = At(1, 0.09f, 0.12f);
			P.Tail = At(2, 0.3f, 0.6f);
			P.Chord = 0.f;
			P.Neck = At(4, 0.08f, 0.14f);
			P.Ornament = Shape[5] < 0.5f ? 0 : 1;
			P.GlowOrgans = Glows(5, 4);
			OutSizeScale = At(SpeciesShapeDraws - 1, 0.9f, 1.1f);
		}
		else
		{
			// A spinefin: deep-bodied (its girth 0.22 to 0.3), its tail 0.25 to 0.35 of its length, four broad fins, the
			// pectoral pair 0.28 to 0.4 of its length out, spines along its back 0.12 to 0.2 of it high; a second row of
			// spines or none; an Earth fish's size to a quarter again.
			P.Segments = 3;
			P.LimbPairs = 2;
			P.Girth = At(1, 0.22f, 0.3f);
			P.Tail = At(2, 0.25f, 0.35f);
			P.Chord = At(3, 0.28f, 0.4f);
			P.Neck = At(4, 0.12f, 0.2f);
			P.Ornament = Shape[5] < 0.5f ? 0 : 1;
			P.GlowOrgans = Glows(4, 3);
			OutSizeScale = At(SpeciesShapeDraws - 1, 0.9f, 1.25f);
		}
		return P;
	}
	P.Symmetry = 2;
	P.LimbPairs = 3;
	P.Limbs = EFaunaLimbs::Legs;
	P.Wings = EFaunaWings::None;
	if (Form == 0)
	{
		// A six-legged grazer: its barrel 0.62 to 0.7 of its length, its girth, its tail, its legs 0.28 to 0.34 of its
		// length high (lower than an Earth grazer's 0.38), its neck and head's heft; its horns, its crest or both; a
		// grazer's size to a fifth again of it.
		P.Segments = 1;
		P.Length = At(0, 0.62f, 0.7f);
		P.Girth = At(1, 0.31f, 0.42f);
		P.Tail = At(2, 0.08f, 0.2f);
		P.Chord = At(3, 0.28f, 0.34f);
		P.Neck = At(4, 0.6f, 1.f);
		P.Ornament = (uint8)(1 + FMath::Min((int32)(Shape[5] * 3.f), 2));
		P.GlowOrgans = Glows(4, 4);
		OutSizeScale = At(SpeciesShapeDraws - 1, 1.05f, 1.32f);
	}
	else
	{
		// An insectoid: its abdomen 0.44 to 0.56 of its length, its girth, its antennae, its feet 0.36 to 0.48 of its
		// length out to its sides, its head 0.13 to 0.19 of it; its mandibles, a horn, both or neither; from a little
		// smaller than a grazer to half again its size (3.4 m long at the most).
		P.Segments = 3;
		P.Length = At(0, 0.44f, 0.56f);
		P.Girth = At(1, 0.22f, 0.32f);
		P.Tail = At(2, 0.22f, 0.42f);
		P.Chord = At(3, 0.36f, 0.48f);
		P.Neck = At(4, 0.13f, 0.19f);
		P.Ornament = (uint8)FMath::Min((int32)(Shape[5] * 4.f), 3);
		P.GlowOrgans = Glows(3, 3);
		OutSizeScale = At(SpeciesShapeDraws - 1, 0.85f, 1.38f);
	}
	return P;
}

SlonkFauna::FFaunaKindSpecies SlonkFauna::RollSpecies(EFaunaKind K, float Strangeness, uint32 Hash, float Draw)
{
	FFaunaKindSpecies Out;
	Out.Num = 1;
	Out.List[0].Plan = EarthPlan(K);
	const int32 Forms = AlienForms(K);
	const float Chance = DepartChance(Strangeness);
	if (Forms <= 0 || Chance <= 0.f) { return Out; }
	// The kind's own stream, from its reserved draw and the world's hash: the profile's stream and every other kind's stay
	// where they were.
	const uint32 DrawBits = (uint32)FMath::Clamp((double)Draw * 16777216.0, 0.0, 16777215.0);
	FRandomStream R((int32)(HashCombine(Hash * 0x85EBCA77u + 0x165667B1u * ((uint32)K + 1u), DrawBits)));
	constexpr float ExtraChance[MaxSpecies] = { 1.f, 0.75f, 0.45f };
	TArray<FFaunaSpecies, TInlineAllocator<MaxSpecies>> Kept;
	int32 FirstForm = 0;
	for (int32 i = 0; i < MaxSpecies; ++i)
	{
		// Every draw taken, whatever comes of them.
		const float UThere = R.FRand(), UDepart = R.FRand(), UForm = R.FRand(), UShare = R.FRand();
		float Shape[SpeciesShapeDraws];
		for (int32 j = 0; j < SpeciesShapeDraws; ++j) { Shape[j] = R.FRand(); }
		// The first species is always there, the Earth's plan unless it departs; the others only as they depart, the more
		// often the stranger the world (their chance the square of the first's, so a world of several is a very strange one). Each
		// species' form is its own whatever the others do (the first's by its draw, each after it the next of the kind's
		// forms), so a world grown stranger keeps the forms it had.
		if (i == 0) { FirstForm = FMath::Min((int32)(UForm * Forms), Forms - 1); }
		const bool bDeparts = i == 0 ? UDepart < Chance : UThere < Chance * Chance * ExtraChance[i];
		FFaunaSpecies S;
		S.Share = 0.4f + 0.6f * UShare;
		if (bDeparts) { S.Plan = AlienPlan(K, (FirstForm + i) % Forms, Shape, S.SizeScale); }
		else if (i == 0) { S.Plan = Out.List[0].Plan; }
		else { continue; }
		Kept.Add(S);
	}
	// Each species' share of the cells: its weight over all of theirs (the first's, the Earth's or not, by its own draw).
	float Total = 0.f;
	for (int32 i = 0; i < Kept.Num(); ++i) { Total += Kept[i].Share; }
	Out.Num = Kept.Num();
	for (int32 i = 0; i < Kept.Num(); ++i)
	{
		Out.List[i] = Kept[i];
		Out.List[i].Share = Kept.Num() == 1 ? 1.f : Kept[i].Share / FMath::Max(Total, 1e-6f);
	}
	return Out;
}

SlonkFauna::FFaunaProfile SlonkFauna::Roll(const FWorldGenome& G, EPlanetArchetype Type, uint32 Hash)
{
	FFaunaProfile F;
	F.Strangeness = FMath::Clamp(G.Strangeness, 0.f, 1.f);
	F.Type = Type;
	F.Hash = Hash;
	// A stream of its own: none of the world's other rolls (its style, its genome, its people) moves. Its first draw is
	// the seed the animals' places are made from, the same at every strangeness.
	FRandomStream R((int32)(Hash * 0x9E3779B1u + 0x27d4eb2fu));
	F.Seed = (uint32)R.GetUnsignedInt();
	// One draw per kind: each seeds the kind's species (RollSpecies, a stream of the kind's own; at strangeness 0 every
	// kind is the Earth's), and the floaters' is the share of their cells holding a jelly. They come from a second
	// stream, seeded with the hash mixed and not stepped: the first is seeded a fixed step from the genome's (the hash
	// times the same odd number, each plus a constant of its own), and a stream so seeded draws a fixed step from the
	// other's, draw for draw, on every world: the jellies' share followed the hue of the sky. The seed above stays where
	// it was, so nothing moves on a world near home; what departs on a strange one is drawn anew.
	FRandomStream Kinds((int32)HashCombine(Hash, 0x27d4eb2fu));
	float FloaterDraw = 0.f;
	for (int32 K = 0; K < NumKinds; ++K)
	{
		const float Draw = Kinds.FRand();
		F.Species[K] = RollSpecies((EFaunaKind)K, F.Strangeness, Hash, Draw);
		if ((EFaunaKind)K == EFaunaKind::Floater) { FloaterDraw = Draw; }
	}
	F.Look = LookOf(G);
	// The sky-jellies, on a strange world whose air is thick: how many there are, and their own body plan, one species of
	// it (an Earth-like world has none, and keeps the plan they would start from).
	F.bThickAir = ThickAir(G);
	F.FloaterShare = FloaterShareOf(G, FloaterDraw);
	if (F.FloaterShare > 0.f)
	{
		FFaunaKindSpecies& J = F.Species[(int32)EFaunaKind::Floater];
		J = FFaunaKindSpecies();
		J.List[0].Plan = FloaterPlan(Hash);
	}
	return F;
}

bool SlonkFauna::SameProfile(const FFaunaProfile& A, const FFaunaProfile& B)
{
	if (A.Strangeness != B.Strangeness || A.Type != B.Type || A.Hash != B.Hash || A.Seed != B.Seed) { return false; }
	for (int32 K = 0; K < NumKinds; ++K) { if (A.Species[K] != B.Species[K]) { return false; } }
	return A.Look.Glow == B.Look.Glow && A.Look.Firefly == B.Look.Firefly && A.Look.Pollen == B.Look.Pollen && A.Look.Jelly == B.Look.Jelly
		&& A.Look.HueTurnDeg == B.Look.HueTurnDeg && A.Look.Vivid == B.Look.Vivid && A.bThickAir == B.bThickAir && A.FloaterShare == B.FloaterShare;
}

FLinearColor SlonkFauna::FlierPigment(const FFaunaLook& L, const FScatterPalette& Pal, EFlierHabitat H, double Dry, double Vary)
{
	// The living ground's colour where the flock lives, the plants' own.
	const FLinearColor Ground = SlonkScatter::FoliageAt(Pal, Dry, 1.0);
	const float Luma = FMath::Max(0.2126f * Ground.R + 0.7152f * Ground.G + 0.0722f * Ground.B, 1e-3f);
	// As an Earth's birds wear it: its brightness alone, browned, and set to the habitat's tone (the woods' birds dark,
	// the meadows' near black, the gulls pale, the cliffs' a slate).
	FLinearColor Earth;
	switch (H)
	{
	case EFlierHabitat::Meadow: Earth = FLinearColor(0.045f, 0.043f, 0.05f); break;
	case EFlierHabitat::Coast: Earth = FLinearColor(0.62f, 0.64f, 0.66f); break;
	case EFlierHabitat::Cliff: Earth = FLinearColor(0.2f, 0.21f, 0.24f); break;
	default: Earth = FLinearColor(0.16f, 0.12f, 0.085f) * FMath::Clamp(Luma / 0.3f, 0.6f, 1.4f); break;
	}
	// A strange world's: the ground's colour itself, as bright as the Earth's tone, at full strength on the strangest.
	FLinearColor Own = Ground * ((0.2126f * Earth.R + 0.7152f * Earth.G + 0.0722f * Earth.B) / Luma);
	FLinearColor C = FMath::Lerp(Earth, Own, L.Vivid);
	C = TurnPigment(C, L.HueTurnDeg) * (float)Vary;
	C.A = 1.f;
	return C;
}

FLinearColor SlonkFauna::HerdPigment(const FFaunaLook& L, const FScatterPalette& Pal, double Dry, double Tone, double Vary)
{
	// The grass the herd grazes, the plants' own colour there.
	const FLinearColor Ground = SlonkScatter::FoliageAt(Pal, Dry, 1.0);
	const float Luma = FMath::Max(0.2126f * Ground.R + 0.7152f * Ground.G + 0.0722f * Ground.B, 1e-3f);
	// An Earth's grazers: a bison's dark brown to a deer's tan, the drier the country the paler.
	const FLinearColor Dark(0.10f, 0.065f, 0.042f), Tan(0.42f, 0.30f, 0.17f);
	FLinearColor Earth = FMath::Lerp(Dark, Tan, (float)FMath::Clamp(Tone * 0.8 + Dry * 0.3, 0.0, 1.0));
	// A strange world's: the ground's colour itself, as bright as the Earth's coat, at full strength on the strangest.
	const FLinearColor Own = Ground * ((0.2126f * Earth.R + 0.7152f * Earth.G + 0.0722f * Earth.B) / Luma);
	FLinearColor C = FMath::Lerp(Earth, Own, L.Vivid);
	C = TurnPigment(C, L.HueTurnDeg) * (float)Vary;
	C.A = 1.f;
	return C;
}

FLinearColor SlonkFauna::CarapacePigment(const FFaunaLook& L, const FScatterPalette& Pal, double Dry, double Tone, double Vary)
{
	// The grass it grazes, the plants' own colour there.
	const FLinearColor Ground = SlonkScatter::FoliageAt(Pal, Dry, 1.0);
	const float Luma = FMath::Max(0.2126f * Ground.R + 0.7152f * Ground.G + 0.0722f * Ground.B, 1e-3f);
	// A beetle's near black to a chitin's amber, the wetter the grass the deeper.
	const FLinearColor Dark(0.035f, 0.03f, 0.028f), Amber(0.34f, 0.18f, 0.05f);
	const FLinearColor Earth = FMath::Lerp(Dark, Amber, (float)FMath::Clamp(Tone * 0.85 + Dry * 0.25 - 0.1, 0.0, 1.0));
	// A strange world's: the ground's own colour, as bright as that chitin and a little deeper (its sheen is the material's),
	// at full strength on the strangest.
	const FLinearColor Own = Ground * (0.85f * (0.2126f * Earth.R + 0.7152f * Earth.G + 0.0722f * Earth.B) / Luma);
	FLinearColor C = FMath::Lerp(Earth, Own, L.Vivid);
	C = TurnPigment(C, L.HueTurnDeg) * (float)Vary;
	C.A = 1.f;
	return C;
}

FString SlonkFauna::DescribePlan(EFaunaKind K, const FFaunaBodyPlan& P)
{
	if (IsEarthPlan(K, P))
	{
		switch (K)
		{
		case EFaunaKind::Flier: return TEXT("an Earth bird, two feathered wings");
		case EFaunaKind::Herd: return TEXT("an Earth grazer on four legs");
		case EFaunaKind::Leaper: return TEXT("an Earth fish, its fins and forked tail");
		case EFaunaKind::Mote: return TEXT("specks");
		default: return TEXT("the Earth's own");
		}
	}
	if (K == EFaunaKind::Floater)
	{
		return FString::Printf(TEXT("a sky-jelly, %d ribs and %d tentacles, its bell %.2f of its span high, its tentacles %.1f times that"), P.Symmetry, Floater::TentacleCount(P), Floater::BellOf(P),
			Floater::TentaclesOf(P) / FMath::Max(Floater::BellOf(P), 1e-3f));
	}
	// Its glow organs, on any alien plan that carries them.
	const FString Glows = P.GlowOrgans > 0 ? FString::Printf(TEXT("; %d glow organs along each flank, lit at night"), P.GlowOrgans) : FString();
	if (K == EFaunaKind::Flier && Wings::FormOf(P) == Wings::Four)
	{
		const TCHAR* Wears = P.Ornament == 3 ? TEXT(", a forked tail and antennae") : (P.Ornament == 2 ? TEXT(", antennae") : (P.Ornament == 1 ? TEXT(", a forked tail") : TEXT("")));
		return FString::Printf(TEXT("a four-winged flier: a slim body %.2f of its span long (its abdomen %.2f of it, girth %.3f), two pairs of narrow wings %.2f of it in chord beating fast, the hind behind the fore; it hovers and darts%s%s"),
			P.Length, P.Tail, P.Girth, P.Chord, Wears, *Glows);
	}
	if (K == EFaunaKind::Flier && Wings::FormOf(P) == Wings::Membrane)
	{
		const TCHAR* Wears = P.Ornament == 3 ? TEXT(", a crest and a tail vane") : (P.Ornament == 2 ? TEXT(", a tail vane") : (P.Ornament == 1 ? TEXT(", a crest") : TEXT("")));
		return FString::Printf(TEXT("a membrane-winged flier: wings of skin over %d finger struts, their trailing edge scalloped, %.2f of its span in chord, beating slow and deep over long glides, a wave running along them; its body %.2f of its span, its tail %.2f%s%s"),
			P.LimbPairs, P.Chord, P.Length, P.Tail, Wears, *Glows);
	}
	if (K == EFaunaKind::Leaper && FishForm::FormOf(P) == FishForm::Eel)
	{
		return FString::Printf(TEXT("an eel-like leaper: long and slim (girth %.2f), a ribbon of fin along its back and belly to its tail, a wave running back along all of it as it leaps%s%s"), P.Girth,
			P.Ornament ? TEXT(", barbels") : TEXT(""), *Glows);
	}
	if (K == EFaunaKind::Leaper && FishForm::FormOf(P) == FishForm::Spinefin)
	{
		return FString::Printf(TEXT("a spinefin: deep-bodied (girth %.2f), spined along its back (%.2f of its length high), four broad fins %.2f of it out that spread as it leaves the water%s%s"), P.Girth, P.Neck,
			P.Chord, P.Ornament ? TEXT(", a second row of spines") : TEXT(""), *Glows);
	}
	const Walker::FShape S = Walker::ShapeOf(P);
	if (S.Form == Walker::EForm::Hexapod)
	{
		const TCHAR* Wears = P.Ornament == 3 ? TEXT("horns and a crest") : (P.Ornament == 2 ? TEXT("a crest down its back") : TEXT("horns"));
		return FString::Printf(TEXT("a six-legged grazer: its barrel %.2f of its length, girth %.2f, on legs %.2f of it high in a tripod gait, its neck and head heavy (%.2f), %s%s"), P.Length, P.Girth, P.Chord,
			P.Neck, Wears, *Glows);
	}
	if (S.Form == Walker::EForm::Insectoid)
	{
		const TCHAR* Wears = P.Ornament == 3 ? TEXT(", mandibles and a horn") : (P.Ornament == 2 ? TEXT(", a horn") : (P.Ornament == 1 ? TEXT(", mandibles") : TEXT("")));
		return FString::Printf(TEXT("an insectoid: head (%.2f of its length), thorax and abdomen (%.2f), girth %.2f, six legs splayed %.2f of it out to each side in a tripod gait, antennae %.2f of it long, a banded carapace%s%s"),
			P.Neck, P.Length, P.Girth, P.Chord, P.Tail, Wears, *Glows);
	}
	return FString::Printf(TEXT("a plan of %d pairs of limbs, %d segments"), P.LimbPairs, P.Segments);
}

FLinearColor SlonkFauna::LeaperPigment(const FFaunaLook& L, const FLinearColor& SeaColor, double Vary)
{
	// An Earth fish's silver-blue, a quarter of it in the sea's own shade (its brightest channel at 1).
	const FLinearColor Earth(0.36f, 0.44f, 0.52f);
	const float SeaMax = FMath::Max3(SeaColor.R, SeaColor.G, SeaColor.B);
	const FLinearColor Shade = SeaMax > 1e-4f ? FLinearColor(SeaColor.R / SeaMax, SeaColor.G / SeaMax, SeaColor.B / SeaMax) : FLinearColor::White;
	const FLinearColor Tinted = FMath::Lerp(Earth, Earth * Shade, 0.25f);
	const float EarthLuma = 0.2126f * Earth.R + 0.7152f * Earth.G + 0.0722f * Earth.B;
	const float ShadeLuma = FMath::Max(0.2126f * Shade.R + 0.7152f * Shade.G + 0.0722f * Shade.B, 1e-3f);
	// A strange world's: the sea's own colour, as bright as the Earth's fish, at full strength on the strangest.
	const FLinearColor Own = Shade * (EarthLuma / ShadeLuma);
	FLinearColor C = FMath::Lerp(Tinted, Own, L.Vivid);
	C = TurnPigment(C, L.HueTurnDeg) * (float)Vary;
	C.A = 1.f;
	return C;
}

SlonkFauna::FSeaLife SlonkFauna::SeaLifeOf(float GroundLife, const FWorldGenome& G)
{
	FSeaLife Sea;
	Sea.PlanktonColor = LookOf(G).Glow;
	const float Life = FMath::Clamp(GroundLife, 0.f, 1.f);
	if (Life <= 0.f || G.Solvent == ESolvent::Magma || G.Solvent == ESolvent::None) { return Sea; }
	Sea.PlanktonGlow = PlanktonGlowAtLife * Life;
	Sea.ShoalDensity = ShoalDensityAtLife * Life;
	return Sea;
}

float SlonkFauna::MilkySeenThrough(float CosView, float DistCm)
{
	// The terrain material's milkyOut, in its float arithmetic: the bloom's cell (the noise period over its 26 cells) against
	// the footprint, the slant taken in.
	const float Cos = FMath::Clamp(CosView, 0.f, 1.f);
	const float Seen = FMath::Clamp(2.f * (((float)SlonkScatter::NoisePeriodCm / 26.f) * Cos / (FMath::Max(DistCm, 1.f) * 0.0045f) - 0.35f), 0.f, 1.f);
	return 1.f - (1.f - Seen) * FMath::Pow(1.f - Cos, 5.f);
}
