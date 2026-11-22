// Slonk - the kinds of animal a world's fauna is made of, and the contracts between the code that places them and the
// material that draws them (M_Fauna, built by tools/planet_materials/fauna_material.py). A kind's animals live in
// groups: a flock, a herd, a shoal. Each group is one instanced set whose frame the game moves along the group's path
// every frame, its members drawn in that frame and moved within it by the material (their wings, a ripple through the
// flock, the scatter of a startle), so the velocity pass sees the group's flight in its transform and the members'
// motion in the material's own clock. The kind table says how a kind's habitat cells are laid, how far from the camera
// its groups are kept, how many may be about at once, how fast its paths run and what scares it.

#pragma once

#include "CoreMinimal.h"

/** The kinds of animal, by how they move and how they are drawn. */
enum class EFaunaKind : uint8
{
	/** Birds and whatever else flies in flocks along a path, low over the woods, the coasts and the cliffs. */
	Flier,
	/** Grazers walking a loop over the open grass. */
	Herd,
	/** Pollen, midges and fireflies: clouds of specks carried on the terrain's chunks. */
	Mote,
	/** Fish that jump out of the painted shoals. */
	Leaper,
	/** Sky-jellies drifting through a strange world's thick air, high over its ground. */
	Floater,
	Count
};

/** What a body's wings are, if it has any. */
enum class EFaunaWings : uint8 { None, Feathered, Membrane, FourWings };
/** What it walks or swims on, if anything. */
enum class EFaunaLimbs : uint8 { None, Legs, Fins, Tentacles };

/** Where a flock of fliers lives, which says what kind of bird it is on an Earth-like world. */
enum class EFlierHabitat : uint8
{
	/** Crows, jays and pigeons over the forest's edges: a few to a dozen, ranging over the canopy. */
	Woodland,
	/** Starlings over wet, open grass: a cloud of a hundred, fast and low. */
	Meadow,
	/** Gulls along the shore: a loose few dozen, gliding more than they flap. */
	Coast,
	/** Swifts and rock doves round the cliffs and the canyon walls. */
	Cliff,
	Count
};

/** How an animal's body is put together. A world's genome turns these from the Earth's as it grows strange (the
 *  biosphere phase); the meshes are made, and cached, by the plan (FaunaMeshes.h). */
struct FFaunaBodyPlan
{
	/** 2 for a body with a left and a right side; more for a radial one. */
	uint8 Symmetry = 2;
	/** Body segments along its length (an insectoid's three: its head, its thorax and its abdomen). */
	uint8 Segments = 1;
	/** Pairs of limbs, and what they are (a membrane-winged flier's: the finger struts in each wing, 3 or 4). */
	uint8 LimbPairs = 0;
	EFaunaLimbs Limbs = EFaunaLimbs::None;
	EFaunaWings Wings = EFaunaWings::None;
	/** Its proportions, as shares of its span (a flier's wingspan, a walker's length): the body's length and girth, the
	 *  tail's length and the wing's chord at its root. A walker of another form reads them its own way (Walker::ShapeOf):
	 *  a six-legged grazer's barrel, its girth, its tail and its legs' height; an insectoid's abdomen, its girth, its
	 *  antennae and how far out to its sides its feet stand. A four-winged flier's Tail is its abdomen's share of its body,
	 *  a membrane-winged one's its whip of a tail; a fish's alien forms read them as an Earth fish's. */
	float Length = 0.45f;
	float Girth = 0.1f;
	float Tail = 0.2f;
	float Chord = 0.24f;
	/** Spots that glow at night: how many in the row along each of its flanks (0: none, as on the Earth's; the material
	 *  draws them, SlonkFauna::Glow). */
	uint8 GlowOrgans = 0;
	/** What else a walker's form takes from its plan, as a share of its length (0 for the Earth's grazer, which has its
	 *  own): a six-legged grazer's neck and head's heft, an insectoid's head; a four-winged flier's head, a membrane-winged
	 *  one's head and crest, a spinefin's spines' height, as shares of its span or length. */
	float Neck = 0.f;
	/** What it wears (0, nothing past its form's own), a bit each: a six-legged grazer's horns (1) and crest (2), an
	 *  insectoid's mandibles (1) and horn (2); a four-winged flier's forked tail (1) and antennae (2), a membrane-winged
	 *  one's crest (1) and tail vane (2); an eel's barbels (1), a spinefin's second dorsal spines (1). */
	uint8 Ornament = 0;

	/** Every field, so two plans that differ in anything have different meshes. The fields that came with the alien plans
	 *  are taken in only when they are set, so every plan that came before keeps its hash (and its mesh its name). */
	uint32 Hash() const
	{
		uint32 H = HashCombine(GetTypeHash(Symmetry), GetTypeHash(Segments));
		H = HashCombine(H, GetTypeHash(LimbPairs));
		H = HashCombine(H, GetTypeHash((uint8)Limbs));
		H = HashCombine(H, GetTypeHash((uint8)Wings));
		H = HashCombine(H, GetTypeHash(Length));
		H = HashCombine(H, GetTypeHash(Girth));
		H = HashCombine(H, GetTypeHash(Tail));
		H = HashCombine(H, GetTypeHash(Chord));
		H = HashCombine(H, GetTypeHash(GlowOrgans));
		if (Neck != 0.f || Ornament != 0) { H = HashCombine(HashCombine(H, GetTypeHash(Neck)), GetTypeHash(Ornament)); }
		return H;
	}
	bool operator==(const FFaunaBodyPlan& O) const
	{
		return Symmetry == O.Symmetry && Segments == O.Segments && LimbPairs == O.LimbPairs && Limbs == O.Limbs && Wings == O.Wings
			&& Length == O.Length && Girth == O.Girth && Tail == O.Tail && Chord == O.Chord && GlowOrgans == O.GlowOrgans && Neck == O.Neck && Ornament == O.Ornament;
	}
	bool operator!=(const FFaunaBodyPlan& O) const { return !(*this == O); }
};

namespace SlonkFauna
{
	constexpr int32 NumKinds = (int32)EFaunaKind::Count;
	/** Every kind, for a mask of the kinds allowed (FFaunaWorld::KindMask). */
	constexpr uint32 AllKinds = (1u << NumKinds) - 1u;
	constexpr uint32 KindBit(EFaunaKind K) { return 1u << (uint32)K; }

	/** The period of the material's clock (M_Fauna's Time node), s, which every path and every cycle of the material
	 *  divides: a flock is where it was an hour ago, and the hour's wrap of the clock shows no seam. */
	constexpr double ClockPeriodSec = 3600.0;
	/** A group is spawned once the camera's ground point comes within its kind's reach of the middle of its path, and
	 *  kept until it is this many times that far (looked away from and back at, a flock is still there), unless its
	 *  kind's caps are full and a group nearer by a sixth of a cell needs its place: then one out of sight is let go
	 *  sooner, within its reach or past it (FFaunaField::Select). */
	constexpr double SpawnHysteresis = 1.25;
	/** How long a startled group stays shaken, s: it flees, scatters, regroups and is back on its path by then. */
	constexpr double PanicSec = 120.0;
	/** A group startled again within this of the last startle keeps that one's flight, s: a ship hovering by a flock
	 *  does not restart its flight every frame. */
	constexpr double RestartleSec = 8.0;
	/** How long a startle's scatter of a group's members lasts, s: it opens in a second and is closed by then
	 *  (fauna_material.py). A startle while it is open keeps it, so the birds already spread never snap back. */
	constexpr double ScatterSec = 40.0;
	/** The unit mesh's span, cm: every fauna mesh is this across (a flier's wingspan), so an instance's scale is its
	 *  span in metres (FaunaMeshes.h). */
	constexpr double UnitCm = 100.0;

	/** What one kind is, and how many of it the game keeps about. */
	struct FFaunaKindDesc
	{
		EFaunaKind Kind = EFaunaKind::Flier;
		/** Whether this build spawns it (every kind but the motes, which the terrain's chunks carry). */
		bool bLive = false;
		/** The side of its habitat cells on the cube's faces, cm: at most one group to a cell. */
		double CellCm = 1.0e5;
		/** Its groups are spawned within this of the camera's ground point (the middle of their path), cm. */
		double ReachCm = 3.0e5;
		/** None are spawned while the camera stands higher than this above the ground, cm, nor kept while it stands higher
		 *  than SpawnHysteresis times this (a jelly's ground the higher of the ground under the scan's middle and its loop's
		 *  highest: FFaunaField::Select). */
		double MaxAltitudeCm = 2.5e5;
		/** Members of a group, fewest and most. */
		int32 GroupMin = 1;
		int32 GroupMax = 1;
		/** Groups and members about at once, at most. */
		int32 MaxGroups = 0;
		int32 MaxMembers = 0;
		/** A craft in the air lower than ScareAltitudeCm over the ground and within ScareRadiusCm of a group startles
		 *  it; so does someone on foot within CrewScareCm (0: nobody on foot ever does). A startled group flees this far,
		 *  cm, before it drifts back. */
		double ScareRadiusCm = 0.0;
		double ScareAltitudeCm = 0.0;
		double CrewScareCm = 0.0;
		double FleeCm = 0.0;
		/** How its flight off its path comes on, s: a bird is away at once, its flight closing on FleeCm by this time
		 *  constant from its fastest at the start; a walker (bGallop) breaks into a gallop from a stand, at its fastest this
		 *  long in, and a second fright carries the gallop it was in rather than stopping it dead; a floater squeezes itself
		 *  away from a drift, at its fastest this long in, a second fright carrying on the drift it had. How far up off the
		 *  ground it flees, as a share of the way away (a bird climbs, a walker keeps to the ground, a jelly sinks: below 0).
		 *  A walker's gallop comes in
		 *  FleeTurnSec after its members have stopped and turned to flee where they stand (at once from grazing; on the move
		 *  they first turn back to their grazing: SlonkFauna::StartleHerd). */
		double FleeRiseSec = 8.0;
		bool bGallop = false;
		double FleeUp = 0.35;
		double FleeTurnSec = 0.0;
		/** How far to its side a walker's flight loops as it slows, turns and comes back, cm (it never turns about on the
		 *  spot: its walkers turn as they go). */
		double FleeLoopCm = 0.0;
		/** How long a startled group of it stays shaken, s (PanicSec unless its flight back takes longer), and when a
		 *  walker's flight turns back, s after the startle: it comes back over the rest at a steady pace, eased in and out
		 *  (a herd's no faster than its slowest trot). */
		double FrightSec = PanicSec;
		double FleeBackSec = 0.3 * PanicSec;
		/** Someone on foot nearer than this to a group's nearest member, cm, sees it walk off out of their way (0: it never
		 *  does): a herd keeps about this far from the crew, and only one run in on to CrewScareCm of its middle bolts. */
		double KeepFromCrewCm = 0.0;
		/** Its groups cast shadows within this of the camera, cm (0: never). */
		double ShadowReachCm = 0.0;
		/** The members dither away by this distance from the camera, cm, so a group spawned at its reach (beyond this
		 *  by more than its path's own size) is invisible when it comes and goes. */
		double FadeCm = 0.0;
		/** How far the material moves a member's vertices past its mesh's box, in the unit mesh's centimetres: each
		 *  instance's box is padded by it (UPlanetInstancesComponent::SetCullPadding) so nothing is culled that is
		 *  drawn. */
		double CullPaddingUnits = 0.0;
		/** The meshes' LOD screen sizes, with that padding in the box the GPU picks them by. */
		float LodScreenSize[3] = { 1.f, 0.5f, 0.05f };
	};
	SLONK_API const FFaunaKindDesc& KindDesc(EFaunaKind K);
	SLONK_API const TCHAR* KindName(EFaunaKind K);
	/** "flier", "herd", "mote", "leaper", "floater" (or their plurals, or "jelly"): false for anything else. */
	SLONK_API bool KindFromName(const FString& Name, EFaunaKind& Out);
	SLONK_API const TCHAR* HabitatName(EFlierHabitat H);
	/** The Earth's body plan for a kind: what a world at strangeness 0 has. */
	SLONK_API FFaunaBodyPlan EarthPlan(EFaunaKind K);

	/**
	 * Each member's custom floats (as many as the scatter's, SlonkScatter::NumCustomFloats, so every planet instance is
	 * written in one pass): its pigment RGB; how brightly it glows at night; a seed; its span in metres (its scale);
	 * a phase for the ripple through its group (rolling across the formation); its wingbeat or stride, Hz (a whole
	 * number of beats each minute, so the clock's wrap shows no seam); one value of its kind's own (a flier: its bearing
	 * from the group's middle, radians, which a startle scatters it along); and the distance from the camera it fades
	 * out by, cm.
	 */
	namespace Custom
	{
		enum : int32 { PigmentR, PigmentG, PigmentB, Glow, Seed, SizeM, Phase, RateHz, PerKind, FadeCm, Num };
	}
	/**
	 * A mote cloud's custom floats, in the same slots (SlonkScatter::PlaceMotes writes them): its colour by day (the
	 * pollen's gold, its gnats a dark shade of it); how brightly its fireflies glow at night; a seed; its span in metres
	 * (its scale); a phase for its motes' cycles; how fast they wander, Hz (a whole number of cycles each minute); the
	 * share of its motes that are fireflies at night (PerKind); and the distance from the camera it fades out by, cm.
	 */
	namespace Mote
	{
		/** The motes of a cloud, and the cards that draw them at each LOD (the far LODs' cards each stand for several). */
		constexpr int32 MotesPerCloud = 24;
		constexpr int32 CardsAtLod[3] = { 24, 8, 2 };
		/** Of a cloud's motes, how many are gnats in their swarm by day (they are none at night): the cards whose random
		 *  (VC.B) is under GnatShare. The far LOD has none of them, too small to see that far. */
		constexpr int32 GnatsPerCloud = 6;
		constexpr float GnatShare = 0.25f;
		/** A card's half-width in the unit mesh, cm: every card stands in the mesh's xz plane, its corner (2u-1, 0, 2v-1)
		 *  times this from its middle, so the material finds the middle from the corner's UV and turns the card about it. */
		constexpr double CardHalfCm = 0.6;
		/** How far the material moves a card's vertices from where the mesh has them, unit-mesh cm, at most (their wander,
		 *  the turn to the camera, the speck floor's growth): the mesh's bounds are grown by it, so the box an instance is
		 *  culled by holds every mote however it moves (at most about 32: 17 of wander and 14 of a firefly's corner at forty
		 *  times its size on a 6 m cloud, and its place). */
		constexpr double ReachCm = 33.0;
	}
	/**
	 * A walker's custom floats, in the same slots (SlonkFauna::MembersOf writes them for a herd): its coat RGB; how
	 * brightly it glows at night (0 on an Earth); a seed; its length in metres (its scale); where on its grazing circle it
	 * starts, a share of a round (Phase); its stride, m (RateHz's slot: its herd's loop a whole number of them, and its
	 * circle too); the radius of the circle it ambles round as it grazes, m, negative for one it walks clockwise
	 * (PerKind); and the distance from the camera it fades out by, cm. Its instance stands at its circle's middle, unturned
	 * in its herd's frame, which never turns: the material turns it the way it walks.
	 */
	/**
	 * Each group's custom primitive data, set on its component each frame (FrightData writes its fright's part), by slot:
	 * 0 how deep in the night it is (0 day, 1 night); 1 when its members' scatter last opened, on the material's clock
	 * (seconds, the world's time wrapped to ClockPeriodSec; a startle while it is open keeps it), and 2 to 4 the way it
	 * opened in its own frame (unit); 5 the bank its members lean into the turn with (radians, positive lifting the right
	 * wing); 6 whether a startle is live (1 until PanicSec after the last); 7 how far its members scatter, m; 8 when it was
	 * last startled, on the same clock, which its panic eases from; and 9 when its wingbeat's urge began to come in, on
	 * the same clock (FPanic::OnsetT0). The scatter's shape, the panic and the urge's coming-in each run from their own
	 * moment, so a startle that restarts one moves nothing the others drive.
	 */
	namespace GroupData
	{
		/** And a four-winged flock's darts (0 for every other group): 10 its own number for them and 11 its span, m, so every
		 *  member of it darts together, the same way and as far in the flock's frame, and none dashes into its neighbour. */
		enum : int32 { Night = 0, ScatterT0 = 1, ScatterDirX = 2, ScatterDirY = 3, ScatterDirZ = 4, Bank = 5, Startled = 6, SpreadM = 7, PanicT0 = 8, OnsetT0 = 9, DartSeed = 10,
			DartSpanM = 11, Num = 12 };
	}
	/**
	 * A strange world's fliers' wings as the material beats them (the fliers' part of its motion, given WingForm by the
	 * material instance of each alien plan's flocks: UPlanetFaunaComponent; the Earth bird's 0). A four-winged flier (1): a
	 * long, slim body and two pairs of narrow wings, the hind pair HindLag of a beat behind the fore, beating all the time,
	 * faster than a bird (its habitat's beat FourFlapK times, at most FourFlapMax), FourSwing either way; it hovers in its
	 * flock and darts with it: once a cycle of the flock's own (DartLo to DartLo + DartSpan a hour, a whole number, from
	 * its GroupData::DartSeed) every member dashes together, over the last DashShare of the cycle, to a new place up to
	 * DashCm of a unit mesh of the flock's span (GroupData::DartSpanM) either way of its own, its members standing unturned
	 * in the flock, so the flock shifts as one and no member dashes into the next (they keep the gaps the formation gave
	 * them), the dash fading as the speck floor grows it. A membrane-winged flier (2):
	 * wings of skin over finger struts (its plan's
	 * LimbPairs, 3 or 4), their trailing edge scalloped between them, beating slower and deeper (MembraneFlapK of its
	 * habitat's beat, MembraneSwing either way) in bursts over a long glide (GlideLo to GlideLo + GlideSpan cycles an hour,
	 * MembraneShare to MembraneShare + 0.15 of each beating), its wings raised MembraneRaise in the glide, a wave running out
	 * along them and back across them at its beat (WaveRate of it), WaveCm of the unit mesh at the tip's trailing edge at
	 * full flap and GlideWave of that while it glides. Each is its habitat's flock, flying its loop SpeedK of a bird's
	 * speed; its mesh's wings hinged as a bird's, on Vc::WingRootCm, their flex from the hinge to the tip, its body (longer
	 * than a bird's) laid about the mesh's middle, and its speck floor growing it at most SpeckMax times (a bird's four), so
	 * the box a bird's instance is padded by holds it.
	 */
	namespace Wings
	{
		enum : int32 { Bird = 0, Four = 1, Membrane = 2 };
		constexpr float HindLag = 0.3f;
		constexpr float FourFlapK = 2.f, FourFlapMax = 14.f, FourSwing = 0.6f;
		constexpr float DartLo = 1000.f, DartSpan = 800.f, DashShare = 0.2f;
		constexpr float DashCm[3] = { 150.f, 110.f, 50.f };
		constexpr float MembraneFlapK = 0.6f, MembraneSwing = 1.15f, MembraneRaise = 0.06f;
		constexpr float GlideLo = 240.f, GlideSpan = 240.f, MembraneShare = 0.2f;
		constexpr float WaveRate = 0.6f, WaveCm = 7.f, GlideWave = 0.3f;
		constexpr double FourSpeedK = 0.75, MembraneSpeedK = 0.9;
		constexpr float SpeckMax = 3.f;
		/** The form of a flier's plan (Bird for the Earth's, and for any plan of feathered wings). */
		inline int32 FormOf(const FFaunaBodyPlan& P) { return P.Wings == EFaunaWings::FourWings ? Four : (P.Wings == EFaunaWings::Membrane ? Membrane : Bird); }
	}
	/**
	 * A herd's custom primitive data, every slot the engine has (36), set each frame (SlonkFauna::StepHerd). The fright's
	 * slots are a flock's (HerdFrightData writes them: 6 whether a startle is live, 8 when it was last startled and 9 when
	 * its alarm began to come in, each on the material's clock), and 0 the night. The rest are the herd's own: 5 the way it
	 * walks, radians in its frame (from its x toward its y), turned only while it walks (by 3 a metre walked) or while it
	 * grazes (when no walker faces it), never while its walkers turn to walk or back to graze; 7 how fast its walkers' legs
	 * walk, m/s (its frame's way along the ground ahead of it, and a turn's own steps); 10 how far its legs have walked, m
	 * (wrapped to its loop's length, a whole number of every member's strides; each whole turn to walk or back to graze
	 * adds Walker::PivotM, its walkers stepping round where they stand) and 11 how long it has grazed, s (wrapped to the
	 * hour, a whole number of every member's rounds of its grazing circle; it grazes only while none of it is on the move),
	 * each as of 12, a moment on the material's clock, and running on from it at 7 and while 13 is 0; 13 the share of it
	 * that is on the move (0 grazing, 1 walking with the herd), run on through a turn at 7 over PivotM, so the material's
	 * walking is the exact integral of the speeds given and the velocity pass sees the legs move. Its latest turn to walk
	 * or back to graze and the one before it, so the material knows how each foot still down has turned and walked since it
	 * was put down: 1 how far its legs had walked when the latest began (as 10 counts it, carried back through 10's wraps),
	 * PivotBack more for a turn back to graze; 2 what came between the two: before a turn to walk, the grazing clock the
	 * stop before left it at (as 11 counts it; its grazing clock through the latest is 11, which stands still till it
	 * grazes again), before a turn back to graze how far its legs walked, m, and once it grazes again its grazing clock
	 * through that turn; 3 how fast the herd's way turns, radians a metre walked (signed as 5 turns, changing smoothly as
	 * it walks: Walker::MaxKapRate; kept from its last walk); 4 its ways through the turn before and the latest (5 is held
	 * while its walkers turn), packed by PackTurns. 14 to 34 the drawn ground's height over the herd's frame, cm along its
	 * z, at the nodes of a 5 by 5 grid in its xy (the corners left out: the material makes each of them from the three
	 * nodes beside it), row by row from the most negative y; and 35 how far the grid reaches either way of the frame's
	 * middle, cm (its nodes half that apart).
	 */
	namespace HerdData
	{
		enum : int32 { Night = 0, PivotRef = 1, GrazeRef = 2, WalkKap = 3, TurnHeading = 4, Heading = 5, Startled = 6, SpeedMS = 7, PanicT0 = 8, OnsetT0 = 9, DistM = 10, GrazeSec = 11,
			ClockT0 = 12, Travel = 13, Ground0 = 14, GroundHalfCm = 35, Num = 36 };
		/** What slot 1 adds for a turn back to graze. */
		constexpr double PivotBack = 1.0e4;
		/** Slot 4: the way through the turn before the latest and through the latest, each to a TurnSteps-th of a round, as
		 *  the one times TurnSteps plus the other (a whole number a float holds exactly). */
		constexpr int32 TurnSteps = 3142;
		inline float PackTurns(double Before, double Latest)
		{
			auto Step = [](double A) { const double F = A / (2.0 * UE_DOUBLE_PI); return (int32)FMath::RoundToDouble((F - FMath::FloorToDouble(F)) * TurnSteps) % TurnSteps; };
			return (float)(Step(Before) * TurnSteps + Step(Latest));
		}
		/** The grid's nodes sent (a 5 by 5 grid less its corners), and the most a node stands off the herd's plane, cm, as
		 *  sent and as the material takes it (the box each walker is culled by holds that much). */
		constexpr int32 GroundSide = 5;
		constexpr int32 GroundNodes = 21;
		constexpr double MaxGroundCm = 150.0;
	}
	/**
	 * A walker (a herd's member) as the material moves it, its unit mesh's metre its length from rump to muzzle, its feet
	 * on its z 0 and its nose along +x: the gait (every leg on the same walk at every speed, the hind left, the fore left,
	 * the hind right and the fore right a quarter of a stride apart, each foot down for Duty of it); the neck's pivot,
	 * which the material turns the neck and head about to graze (GrazePitch, the muzzle to the grass) and to walk, trot
	 * and stand alarmed (radians, negative nose down); and the speck floor (a walker far off drawn no shorter than
	 * SpeckPx, at most SpeckMax times its size).
	 */
	namespace Walker
	{
		constexpr float Duty = 0.6f;
		constexpr float LiftCm = 6.f;
		constexpr float NeckRootX = 14.f, NeckRootZ = 40.f;
		constexpr float GrazePitch = -1.25f, WalkPitch = -0.1f, TrotPitch = 0.05f, AlarmPitch = 0.25f;
		constexpr float SpeckPx = 3.f, SpeckMax = 4.f;
		/** A walker's stride is about this share of its length (each member's set so its herd's loop is a whole number of
		 *  them), and the grazing circle it ambles round no wider than MaxCircleM (every member at least MinSizeM long). */
		constexpr double StrideShare = 0.62;
		constexpr double MaxCircleM = 2.8;
		constexpr double MinSizeM = 1.45;
		/** A whole turn from grazing to walking with the herd (or back) is as many steps as this many metres walked, each
		 *  walker stepping round where it stands (two and a half to four and a half strides, so a walker turning right
		 *  round turns at most about 1.3 radians a stride), and the herd turns as it walks no more sharply than MaxWalkKap
		 *  radians a metre, how sharply changing by no more than MaxKapRate radians a metre each second (a planted hoof is
		 *  carried round by how sharply its body turns now, so a sudden change would move every hoof that is down; and a
		 *  way that came round more slowly would lag the bend it walks, and slide its hooves sideways the more for it,
		 *  the longest-legged herds' a centimetre a second on the whole round a sharp one). */
		constexpr double PivotM = 4.0;
		constexpr double MaxWalkKap = 0.5;
		constexpr double MaxKapRate = 0.3;
		/** A herd on the move closes the gap between the way it faces and the way it goes over about this many metres. */
		constexpr double SteerM = 0.5;

		/**
		 * The forms a walker's body plan builds (ShapeOf): the Earth's grazer on four legs (two pairs); a six-legged grazer, a
		 * longer, lower barrel on three pairs of legs, its neck and head heavier and carried low, horns or a crest of its
		 * plan's own (three pairs, one segment); an insectoid, its head, thorax and big abdomen slung low between six legs
		 * splayed out to its sides, each knee high over its body and each foot well out from it, antennae forward of its head
		 * (three pairs, three segments).
		 */
		enum class EForm : uint8 { None, Grazer, Hexapod, Insectoid };
		/** The gait of six legs: a tripod, the fore and hind legs of one side and the middle one of the other down together,
		 *  the two tripods half a stride apart, each leg of a tripod a little after the one behind it (Ripple of a stride,
		 *  hind to fore). The places in the gait, by leg: the hind, middle and fore left, then the hind, middle and fore
		 *  right. */
		constexpr float Ripple = 0.04f;
		constexpr float TripodPhase[6] = { 0.f, 0.5f + Ripple, 2.f * Ripple, 0.5f, Ripple, 0.5f + 2.f * Ripple };
		/** An insectoid's knee flexes this share of the way its foot does (its leg's G there). */
		constexpr float InsectKneeFlex = 0.45f;
		/**
		 * What the material's walker branch takes of a walker's body plan, from its herd's material instance (one to each
		 * plan: UPlanetFaunaComponent): the pivot its neck and head turn about, unit-mesh cm along x and up z; how high a
		 * foot lifts as it swings, unit-mesh cm; its head's swing as a share of the Earth grazer's (its pitches times this);
		 * and whether it wears a carapace (1, the pixel shader's banded, glossy look) or a coat (0). The defaults are the
		 * Earth grazer's, the material's own defaults (WalkerNeckX, WalkerNeckZ, WalkerLift, WalkerPitch, WalkerCarapace).
		 */
		struct FRig
		{
			float NeckX = NeckRootX, NeckZ = NeckRootZ, LiftCm = Walker::LiftCm, PitchScale = 1.f, Carapace = 0.f;
			bool operator==(const FRig& O) const { return NeckX == O.NeckX && NeckZ == O.NeckZ && LiftCm == O.LiftCm && PitchScale == O.PitchScale && Carapace == O.Carapace; }
		};
		/** A leg as its walker's mesh is built: its hip (inside the body), its knee and its foot (on z 0), unit-mesh cm, and
		 *  its place in the gait (the blue its vertices carry). */
		struct FLeg
		{
			FVector3f Hip = FVector3f::ZeroVector, Knee = FVector3f::ZeroVector, Foot = FVector3f::ZeroVector;
			float Phase = 0.f;
		};
		/**
		 * What a walker's body plan is to the code that moves it and the material that draws it: its form, its legs (four or
		 * six), its rig (the material's); its stride, a share of its length (each member's a whole number of them round its
		 * herd's loop); how far its footprint reaches from its middle, over half its length (1 for the Earth grazer, its muzzle
		 * and its rump half a length out; more for legs splayed out and antennae: its herd keeps its members that much further
		 * apart, and reads the ground that much further out); and how long its members are at the least and the most, m.
		 */
		struct FShape
		{
			EForm Form = EForm::None;
			int32 NumLegs = 0;
			FLeg Legs[6];
			FRig Rig;
			double StrideShare = Walker::StrideShare;
			double Reach = 1.0;
			double MinSizeM = Walker::MinSizeM, MaxSizeM = 2.6;
			/** How wide its grazing circle may be, in its own lengths: so the speck floor's growth of its farthest-reaching
			 *  part (an insectoid's antennae and splayed feet, out to SpeckMax times) and its circle, lined up the same way,
			 *  stay inside the box its instance is culled by (a bound by arithmetic: Walker::ShapeOf). No bound for the
			 *  Earth's grazer and the six-legged one, which the box holds at any circle (MaxCircleM bounds them). */
			double CircleShare = 1.0e9;
		};
		/** The shape of a walker of a plan (FaunaMeshes.cpp builds its mesh from it): the Earth grazer's for its own plan, a
		 *  form of None for a plan that is no walker. */
		SLONK_API FShape ShapeOf(const FFaunaBodyPlan& P);
	}
	/**
	 * A leaping fish's custom floats, in the same slots (SlonkFauna::MembersOf writes them for a group of leapers): its
	 * pigment RGB; how brightly it glows at night (0 on an Earth); a seed; its length in metres (its scale); where its slot
	 * in its group's leap cycle begins, a share of the cycle (Phase); the group's leap cycle, s (RateHz's slot: a divisor
	 * of the hour); its place in its group and the group's count, as 8 times the one plus the other (PerKind); and the
	 * distance from the camera it fades out by, cm. Its instance stands at its home in the shoal, unturned in its group's
	 * frame (the shoal's: x the way it swims, y to its left, z up from the sea's level; on the sea-level sphere at the
	 * shoal's middle): the material moves it out on each leap and turns it the way the leap goes.
	 *
	 * Its leaps, in closed form on the material's clock (SlonkFauna::LeapOf mirrors it): its cycle n (whole cycles from the
	 * hour's turn, its slot's start Phase of a cycle in) holds one leap unless the cycle's hash skips it (SkipShare of
	 * them), leaving the water a share SlotLo to SlotLo + SlotSpan of its slot (a cycle over the group's count) in, a
	 * way along the shoal turned up to YawSpan / 2 either side, from a point up to Jitter of its lengths either way of its
	 * home, rising ApexLo to ApexLo + ApexSpan of its lengths and coming down RangeLo to RangeLo + RangeSpan of them on,
	 * under gravity (GravityCmS2): a parabola from the sea's surface at the point it leaves to the surface at the point it
	 * dives back in (each read where and when it crosses it: LeaperData), its body pitched along it. It is drawn from
	 * LeadDepth of its lengths under the surface as it comes up to as deep as it dives again (so it comes out of the sea
	 * whole and goes back into it whole), and nowhere between leaps (masked, and kept HiddenCm and more under the sea).
	 */
	namespace Leaper
	{
		constexpr int32 MaxPerGroup = 5;
		constexpr double GravityCmS2 = 981.0;
		constexpr float SkipShare = 0.25f;
		constexpr float SlotLo = 0.15f, SlotSpan = 0.45f;
		constexpr float YawSpan = 1.4f;
		constexpr float Jitter = 1.2f;
		constexpr float ApexLo = 0.7f, ApexSpan = 1.1f;
		constexpr float RangeLo = 2.0f, RangeSpan = 2.5f;
		constexpr float LeadDepth = 0.6f;
		/** Between leaps the material holds it this far under its home on the sea's level, cm, and 0.8 of its length more. */
		constexpr float HiddenCm = 150.f;
		/** The most the sea's surface stands off its level where a fish crosses it, cm, as sent and as the material takes it
		 *  (the swell: its lift up to 1.22 times APlanetBody's SwellCm of 120, its sway's lift besides toward the
		 *  projection's seams bringing it to about 1.3 times there, 160 cm, only with all four waves in step). */
		constexpr float MaxSurfaceCm = 150.f;
		/** The speck floor: a fish far off is drawn no shorter than SpeckPx, at most SpeckMax times its size at the top of its
		 *  leap, coming in from its size at the surface (so it never pokes out of the water before it leaves it). */
		constexpr float SpeckPx = 2.f, SpeckMax = 2.f;
		/** How far its tail's tip flicks either side as it leaps, unit-mesh cm. */
		constexpr float FlickCm = 12.f;
		/** The shortest and longest fish, m (the box each is culled by holds the sea's surface over the shortest's length). */
		constexpr double MinSizeM = 0.32, MaxSizeM = 0.9;
		/** The leap cycles a group may take, s, each a divisor of the hour: a group's is the one nearest SlotSec to each of its
		 *  fish and up to SlotSpread more (a lone fish's and a pair's 10 s, five fish's 20 or 24), so its fish leave the water
		 *  a few seconds apart (SlotSec over the share of the cycles that leap, on average) and each leaps about every 13 to
		 *  32 s. Every fish's slot is at least 3 s, so each leap is back under the water long before its fish's next comes up. */
		constexpr double Cycles[] = { 10.0, 12.0, 15.0, 16.0, 18.0, 20.0, 24.0, 25.0 };
		constexpr double SlotSec = 4.0, SlotSpread = 2.0;
		/** A startled group's leaps stop for DiveSec from its latest startle (they dive): none leaves the water then, a leap
		 *  already in the air comes down. */
		constexpr double DiveSec = 60.0;
		/** A dive keeps under the leaps leaving from this long after the startle that begins it, s: past every leap already
		 *  drawn on its way up under the water (LeadSec, at most 0.15 s, the longest fish's lowest leap's), so none in sight
		 *  is hidden. */
		constexpr double DiveLeadSec = 0.2;
		/** How long after a dive's window closes every leap it kept under is past where it would have been back under the
		 *  water, s (FlightSec and LeadSec after it leaves, at most 1.25 s, the longest fish's highest leap's): a new dive
		 *  begins only after it, so moving the window off them shows none of them in the air. */
		constexpr double HeldUnderSec = 1.5;
		/** Of the shoals the terrain material paints, the share holding leapers on a world full of life at density 1. */
		constexpr double ShoalShare = 0.5;
		/** The water under a group's leaps is at least this deep, cm, and deeper than its shoal swims by ShoalClearCm (where
		 *  the material paints the shoal whole). */
		constexpr double MinWaterCm = 300.0, ShoalClearCm = 300.0;
	}
	/**
	 * A group of leapers' custom primitive data, set each frame (UPlanetFaunaComponent): 0 the night; 1 when its dive began
	 * (DiveLeadSec after the startle that began it, on the material's clock), 6 whether it is diving (1 until a little past the end
	 * of its dive), 7 how long its dive lasts from slot 1, s (to DiveSec past its latest startle): no fish leaves the water
	 * from 1 to 1 plus 7 while 6 is 1; and 14 to 23 the sea's surface over its level, cm (each held to MaxSurfaceCm), where
	 * and when each fish's current or next leap leaves it and dives back into it, two to a fish in its order. The flock's
	 * pins read them in the material (ScatterT0, Startled, SpreadM, HerdGround0 to 9).
	 */
	namespace LeaperData
	{
		enum : int32 { Night = 0, DiveT0 = 1, Diving = 6, DiveLen = 7, Surface0 = 14, Num = 24 };
	}
	/**
	 * A strange world's leaping fish as the material moves them (its leaper branch, given FishForm by the material instance
	 * of each alien plan's groups: UPlanetFaunaComponent; the Earth fish's 0): every form leaps the Earth fish's arcs, its
	 * middle on them, and only its body moves otherwise. An eel (1): long and slim, a ribbon of fin along its back and belly
	 * to its tail, a wave running back along all of it (EelWaveLen of the unit mesh long, at EelWaveK times its tail's beat),
	 * EelSwim of its swing as it comes up and goes back under the water and all of it in the air, up to EelWaveCm either way
	 * at its tail's tip and EelHead of that at its nose, its middle held on its arc (it pivots about it as the Earth fish
	 * does); EelMaxSizeM long at the most. A spinefin (2): deep-bodied and spined along its back, four broad fins (two
	 * pectoral, two pelvic, Vc::SurfaceWing, their G their flex from a hinge FinRootCm out from its axis) folded FinFold
	 * radians down against its flanks in the water and spread as it leaves it (by FinOpen of the way to its leap's top),
	 * fluttering FinFlutter either way in the air; its tail flicking as the Earth fish's. A fish's G is how far it bends,
	 * an eel's from its nose (0) to its tail's tip (1).
	 */
	namespace FishForm
	{
		enum : int32 { Earth = 0, Eel = 1, Spinefin = 2 };
		constexpr float EelWaveLen = 60.f, EelWaveK = 1.4f, EelWaveCm = 16.f, EelHead = 0.25f, EelSwim = 0.35f;
		constexpr double EelMaxSizeM = 1.8;
		/** An eel group's span, m, from the cell's draw (before its species' tenth either way of it). */
		constexpr float EelSpanLoM = 1.f, EelSpanHiM = 1.5f;
		constexpr float FinRootCm = 4.f, FinFold = 1.25f, FinOpen = 0.35f, FinFlutter = 0.12f;
		/** The form of a fish's plan (Earth for any of fins and the Earth's shape; its alien forms are told by their segments:
		 *  an eel's 2, a spinefin's 3). */
		inline int32 FormOf(const FFaunaBodyPlan& P) { return P.Limbs != EFaunaLimbs::Fins ? Earth : (P.Segments == 2 ? Eel : (P.Segments == 3 ? Spinefin : Earth)); }
	}
	/**
	 * A sky-jelly (a floater): a translucent bell 20 to 80 m across, a dome lathed round its axis with a scalloped rim, its
	 * ribs (its body plan's Symmetry: 6, 8 or 12) running from its crown to its rim, and 6 to 12 tentacles (twice its
	 * LimbPairs) hanging from just inside its rim 1 to 3 times its bell's height (its plan's Length, the bell's height, and
	 * Tail, the tentacles' length, as shares of its span). Only on a living world at least MinStrangeness strange whose air
	 * is thick (SlonkFauna::FloatersLive): one jelly in a 3 km cell, in the share of the cells its world rolls
	 * (FFaunaProfile::FloaterShare), drifting a few metres a second round a closed loop its middle MinCruiseCm to
	 * MaxCruiseCm over the highest ground within GroundReachCm of it (the sea's surface over water), rising and sinking a
	 * few dozen metres round it; however it is startled and drawn its lowest point stays ClearCm and more over that ground.
	 *
	 * Its unit mesh is its span across (the rim's lobes RimCm out from its axis), its middle the origin: its crown at z H
	 * and its tentacles' tips at -H, H = 50 (Length + Tail); its rim at H - 100 Length; its tentacles' roots RootCm out
	 * from the axis. Its custom floats (SlonkFauna::MembersOf writes them): its pigment RGB (pale, the world's own hue:
	 * FloaterPigment); how brightly its ribs and tentacles glow at night; a seed; its span, m (its scale); its pulse's phase,
	 * a share of a pulse; its pulse, Hz (a whole number of pulses an hour, PulseLoSec to PulseHiSec apart); its ribs' count
	 * (PerKind); and the distance from the camera it fades out by, cm.
	 *
	 * The material moves it in closed form on its clock (SlonkFauna::FloaterMotion mirrors it): its bell contracts quickly
	 * and relaxes slowly once a pulse, its margin pulled in by up to PulseCalm of its radius (twice as fast and PulseFast
	 * while frightened, squeezed hard to Squeeze by a startle) and down PullDownCm at full contraction, the whole of it
	 * rising SurgeCm with each, felt PulseLag of a pulse later down at the rim and on along the tentacles; its tentacles
	 * undulate (waves running down them, WaveAcross and WaveOut of their length at their tips) and trail back from the way
	 * it drifts through the air (TrailMax of their length at TrailFullMS and faster, kept their length); a speck floor draws
	 * one far off no smaller than SpeckPx across, at most SpeckMax times its size (its motion fading as it grows). At night
	 * a pulse of light runs down its ribs, its ring canal and its tentacles at each contraction, LightLag of a pulse from
	 * its crown to its rim.
	 */
	namespace Floater
	{
		constexpr float MinStrangeness = 0.3f;
		constexpr double MinSpanM = 20.0, MaxSpanM = 80.0;
		/** Its bell's height, a share of its span, and its tentacles' length, as many times its bell's height. */
		constexpr float MinBell = 0.4f, MaxBell = 0.6f, MinTentacles = 1.f, MaxTentacles = 3.f;
		constexpr double MinCruiseCm = 3.0e4, MaxCruiseCm = 3.0e5;
		constexpr double ClearCm = 5.0e3;
		/** How far round its loop the ground is read for it: its flight off its loop (a startle, and others after it), its
		 *  trailing tentacles and its bell. The ground along its loop is laid under an envelope rising and falling no steeper
		 *  than GroundSlope along it, so it climbs ahead of a ridge and sinks after it at under a metre a second. */
		constexpr double GroundReachCm = 4.5e4;
		constexpr double GroundSlope = 0.25;
		/** How far a startled jelly sinks at most, cm, however often it is startled again (Slonk.Fauna.FloaterStartle holds
		 *  it to that): its loop's height over the ground allows for it. */
		constexpr double SinkAllowCm = 1.6e4;
		/** How long the drift a second fright carries on with takes to die away, s. */
		constexpr double CoastSec = 3.0;
		constexpr double PulseLoSec = 3.0, PulseHiSec = 6.0;
		constexpr float PulseCalm = 0.16f, PulseFast = 0.24f, Squeeze = 0.34f;
		constexpr float PulseLag = 0.12f, LightLag = 0.3f;
		constexpr float PullDownCm = 20.f, SurgeCm = 4.f;
		constexpr float WaveAcross = 0.07f, WaveOut = 0.035f;
		constexpr float TrailMax = 0.4f, TrailFullMS = 6.f;
		constexpr float SpeckPx = 4.f, SpeckMax = 2.f;
		constexpr double RimCm = 50.0, RootCm = 44.0;
		/** The share of a world's floater cells holding a jelly: MinShare to MinShare + ShareSpan by its genome (the
		 *  floaters' own draw, squared, so most strange worlds have few and some teem with them), at full life and density,
		 *  three fifths of that under the thinnest haze and all of it under one of HazeFull. */
		constexpr float MinShare = 0.05f, ShareSpan = 0.55f, HazeFull = 0.2f;
		/** The bell's pores: rows of cells down it from its crown, each pierced by a pore (none on its ribs, its ring canal or
		 *  its crown, above CrownV) leaving it CoverLo at its rim to CoverHi below its crown: the material's fixed lattice. */
		constexpr int32 PoreRows = 16;
		constexpr float CrownV = 0.28f, CoverHi = 0.6f, CoverLo = 0.38f, RingV = 0.93f;
		/** A plan's bell's height and its tentacles' length, shares of its span, as its mesh is built (held to their
		 *  ranges), and the mesh's half-height H, unit-mesh cm. */
		inline float BellOf(const FFaunaBodyPlan& P) { return FMath::Clamp(P.Length, MinBell, MaxBell); }
		inline float TentaclesOf(const FFaunaBodyPlan& P) { const float B = BellOf(P); return FMath::Clamp(P.Tail, B * MinTentacles, B * MaxTentacles); }
		inline double HalfHeightCm(const FFaunaBodyPlan& P) { return 50.0 * ((double)BellOf(P) + (double)TentaclesOf(P)); }
		/** A plan's tentacles (twice its pairs, 2 to 16). */
		inline int32 TentacleCount(const FFaunaBodyPlan& P) { return FMath::Clamp(2 * (int32)P.LimbPairs, 2, 16); }
	}
	/**
	 * A jelly's custom primitive data, set each frame (UPlanetFaunaComponent): 0 the night; its fright as a flock's
	 * (SlonkFauna::FrightData): 1 when a startle last squeezed it (a startle while that is still in its forty seconds keeps
	 * it), 6 whether a startle is live, 8 when it was last startled and 9 when its frightened pulse began to come in, each on
	 * the material's clock; 2 and 3 the way it drifts through the air in its own frame, m/s, as of 12 (a moment on the
	 * material's clock), and 10 and 11 how fast that changes, m/s/s, so the material runs the way its tentacles trail on
	 * from it in closed form and the velocity pass sees them trail.
	 */
	namespace FloaterData
	{
		enum : int32 { Night = 0, SqueezeT0 = 1, TrailX = 2, TrailY = 3, Startled = 6, PanicT0 = 8, OnsetT0 = 9, TrailRateX = 10, TrailRateY = 11, ClockT0 = 12, Num = 13 };
	}
	/**
	 * A strange plan's glow organs (FFaunaBodyPlan::GlowOrgans; none on any Earth plan): a row of round spots along each of
	 * its flanks, fixed in its mesh's UV (FRow, SlonkFaunaMeshes::GlowRowOf: the same on both flanks, its UV its place along
	 * it and up it at every LOD, its speck's side standing over the row so it glows about as much as its finer LODs), on
	 * its body's own surface alone (rigid, or a fish's undulating body: none on a wing, a leg, a neck or a tail). The
	 * material lights them at night in the world's glow colour (FFaunaLook::Glow) by the group's glow (Custom::Glow, GlowLo
	 * to GlowHi) times SpotBright, breathing from Breath to 1 on a cycle of each animal's own (BreathLo to BreathLo +
	 * BreathSpan a hour, a whole number); by day each is a pale spot. Far off, where the speck floor draws the animal
	 * bigger than it is, its spots are drawn bigger with it and lit the fainter by the square of that growth
	 * (SlonkFauna::GlowSpeckDim), so all the light an animal gives falls with distance as it would without the floor. The
	 * plan's material instance gives the row (its GlowSpots, GlowRow and GlowSize: UPlanetFaunaComponent).
	 */
	namespace GlowOrgan
	{
		constexpr float SpotBright = 3.f, Breath = 0.7f, BreathLo = 600.f, BreathSpan = 600.f;
		constexpr float GlowLo = 1.5f, GlowHi = 3.f;
		/** A row: along the mesh's U from U0 to U1 at its V, each spot RU across either way in U and RV in V, Spots of them. */
		struct FRow
		{
			float U0 = 0.f, U1 = 0.f, V = 0.f, RU = 0.f, RV = 0.f;
			int32 Spots = 0;
		};
	}
	/**
	 * A fauna mesh's vertex colour: R the surface (0 body, 0.25 a bill or a beak, 0.5 a tail, 0.75 a mote's card, 1 a
	 * wing), G how far it flexes (0 at its joint, rising to 1 at the tip), B its side (0 left, 0.5 the middle, 1 right) and
	 * A the kind of joint the material moves it by (0 rigid, 0.25 a tail, 0.375 a neck, 0.5 a wing hinged along the body,
	 * 0.75 a leg, 1 a fin or a body that undulates). A mote's card is drawn by the material's mote branch alone (its Motes
	 * parameter, which the terrain's chunks set), and reads them otherwise: G is how many of its cloud's motes the card
	 * stands for over Mote::MotesPerCloud, B its own random (a gnat's under Mote::GnatShare), A rigid. A walker is drawn by
	 * its walker branch alone (its Walkers parameter, which a herd's sets set): its horns, hooves and muzzle are the beak's
	 * surface (dark), a leg's G is 0 at the hip and 1 at the hoof and its B the leg's place in the gait (Walker: 0 the hind
	 * left, 0.25 the fore left, 0.5 the hind right, 0.75 the fore right), and the neck's and head's G is 0 at its root and 1
	 * at the muzzle; its UV is its place along the body (U 0 at the rump, 1 at the muzzle) and up it (V, z over 60 cm).
	 * A strange world's walker keeps that contract on six legs (Walker::ShapeOf): each leg's B its place in the tripod gait
	 * (Walker::TripodPhase), its G 0 at the hip to 1 at the foot (dark), an insectoid's InsectKneeFlex at its knee (each
	 * foot's ring level on z 0); an insectoid's head flexes as a neck (its back ring at G 0.3, the rest 1), its antennae,
	 * mandibles and horn dark and flexing with it; a six-legged grazer's horns flex with its head and its crest's plates are
	 * rigid and dark (the beak's surface, its B the middle). The material reads no count of legs.
	 * A fish is drawn by its leaper branch alone (its Leapers parameter, which the leapers' sets set): R its surface (0 its
	 * body, 0.5 its fins and tail), G how far it bends as its tail flicks (0 from its nose to FishBendFromCm, rising to 1
	 * at its tail's tip), B its side, A JointUndulate; its UV its place along it (U 0 at the tail's tip, 1 at its nose) and
	 * up it (V 0 at its belly, 1 at its back), for its countershading. Its nose is along +x at 50, its middle the mesh's
	 * origin. A strange world's fish keeps that contract (FishForm): an eel's G how far it bends from its nose (0) to its
	 * tail's tip (1), its ribbon fins the fins' surface; a spinefin's four broad fins the wing's surface, their G their flex
	 * from their hinge FishForm::FinRootCm out from its axis (0) to their tips (1), their B its side; its spines the fins'.
	 * A strange world's flier keeps the bird's (its wings' G their flex from their hinge on WingRootCm to their tips, B its
	 * side, A JointWing; its body rigid; a membrane-winged flier's whip of a tail JointTail): a four-winged flier's wings'
	 * UV their place across them (U 0 at the leading edge to 1 at the trailing; its hind wings' 2 to 3, which the material
	 * beats behind the fore) and out along them (V 0 at the hinge to 1 at the tip); a membrane-winged one's across them (U)
	 * and out along its struts (V 0 at its root, 1 at its first strut, its strut count at its tip, so each strut is drawn
	 * where V is whole); its body's UV its place along it (U 0 at the unit mesh's x of -50 to 1 at +50) and round it (V 0
	 * under it to 1 over it, 0.5 along its flanks), as the glow organs' rows read it (GlowOrgan).
	 * A jelly is drawn by its floater branch alone (its Floaters parameter, which the jellies' sets set): R its surface (0
	 * its bell, 0.5 a tentacle), G how far down it a vertex is (the bell's from 0 at its crown to 1 at its rim, a
	 * tentacle's from 0 at its root to 1 at its tip), B its place round the bell, a share of a turn (a tentacle's root's),
	 * A rigid on the bell and JointUndulate on a tentacle; its UV its place round (U, a share of a turn: the bell's seam is
	 * drawn at 0 and again at 1) and down it (V: the bell's G; a tentacle's 1 and how far along it from its root, in the
	 * unit mesh's metres). Its speck (LOD 2) has its bell drawn whole: its bell's V runs from 0.1 at its crown to 0.27 over
	 * the strip of its ring canal at its rim (under Floater::CrownV, so no pore, and its ribs lit), the strip's within 0.01
	 * of Floater::RingV (whole, and lit as its ring canal is), its G still how far down it a vertex is.
	 */
	namespace Vc
	{
		constexpr float SurfaceBody = 0.f, SurfaceBeak = 0.25f, SurfaceTail = 0.5f, SurfaceMote = 0.75f, SurfaceWing = 1.f;
		constexpr float SideLeft = 0.f, SideMiddle = 0.5f, SideRight = 1.f;
		constexpr float JointRigid = 0.f, JointTail = 0.25f, JointNeck = 0.375f, JointWing = 0.5f, JointLeg = 0.75f, JointUndulate = 1.f;
		/** A wing's hinge stands this far out from the body's axis, unit-mesh cm: its root vertices (G 0) lie on it. */
		constexpr double WingRootCm = 4.0;
		/** A fish bends from this far along it, unit-mesh cm (its tail's tip is at -50). */
		constexpr double FishBendFromCm = 10.0;
	}
}
