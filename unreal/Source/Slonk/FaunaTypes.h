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
	/** Things that drift through a dense sky. */
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
	/** Body segments along its length. */
	uint8 Segments = 1;
	/** Pairs of limbs, and what they are. */
	uint8 LimbPairs = 0;
	EFaunaLimbs Limbs = EFaunaLimbs::None;
	EFaunaWings Wings = EFaunaWings::None;
	/** Its proportions, as shares of its span (a flier's wingspan, a walker's length): the body's length and girth, the
	 *  tail's length and the wing's chord at its root. */
	float Length = 0.45f;
	float Girth = 0.1f;
	float Tail = 0.2f;
	float Chord = 0.24f;
	/** Spots that glow at night (0: none, as on the Earth's). */
	uint8 GlowOrgans = 0;

	/** Every field, so two plans that differ in anything have different meshes. */
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
		return HashCombine(H, GetTypeHash(GlowOrgans));
	}
	bool operator==(const FFaunaBodyPlan& O) const
	{
		return Symmetry == O.Symmetry && Segments == O.Segments && LimbPairs == O.LimbPairs && Limbs == O.Limbs && Wings == O.Wings
			&& Length == O.Length && Girth == O.Girth && Tail == O.Tail && Chord == O.Chord && GlowOrgans == O.GlowOrgans;
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
		/** Whether this build spawns it (the fliers and the herds so far; the others are the later phases'). */
		bool bLive = false;
		/** The side of its habitat cells on the cube's faces, cm: at most one group to a cell. */
		double CellCm = 1.0e5;
		/** Its groups are spawned within this of the camera's ground point (the middle of their path), cm. */
		double ReachCm = 3.0e5;
		/** None are kept while the camera stands higher than this above the ground, cm. */
		double MaxAltitudeCm = 2.5e5;
		/** Members of a group, fewest and most. */
		int32 GroupMin = 1;
		int32 GroupMax = 1;
		/** Groups and members about at once, at most. */
		int32 MaxGroups = 0;
		int32 MaxMembers = 0;
		/** A craft in the air lower than ScareAltitudeCm over the ground and within ScareRadiusCm of a group startles
		 *  it; so does someone on foot within CrewScareCm. A startled group flees this far, cm, before it drifts back. */
		double ScareRadiusCm = 0.0;
		double ScareAltitudeCm = 0.0;
		double CrewScareCm = 0.0;
		double FleeCm = 0.0;
		/** How its flight off its path comes on, s: a bird is away at once, its flight closing on FleeCm by this time
		 *  constant from its fastest at the start; a walker (bGallop) breaks into a gallop from a stand, at its fastest this
		 *  long in, and a second fright carries the gallop it was in rather than stopping it dead. How far up off the ground
		 *  it flees, as a share of the way away (a bird climbs, a walker keeps to the ground). A walker's gallop comes in
		 *  FleeTurnSec after the fright, the time its members take to turn to flee where they stand. */
		double FleeRiseSec = 8.0;
		bool bGallop = false;
		double FleeUp = 0.35;
		double FleeTurnSec = 0.0;
		/** How far to its side a walker's flight loops as it slows, turns and comes back, cm (it never turns about on the
		 *  spot: its walkers turn as they go). */
		double FleeLoopCm = 0.0;
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
	/** "flier", "herd", "mote", "leaper", "floater" (or their plurals): false for anything else. */
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
		enum : int32 { Night = 0, ScatterT0 = 1, ScatterDirX = 2, ScatterDirY = 3, ScatterDirZ = 4, Bank = 5, Startled = 6, SpreadM = 7, PanicT0 = 8, OnsetT0 = 9, Num = 10 };
	}
	/**
	 * A herd's custom primitive data, every slot the engine has (36), set each frame (SlonkFauna::StepHerd). The fright's
	 * slots are a flock's (HerdFrightData writes them: 6 whether a startle is live, 8 when it was last startled and 9 when
	 * its alarm began to come in, each on the material's clock), and 0 the night. The rest are the herd's own: 5 the way it
	 * walks, radians in its frame (from its x toward its y), turned only while it walks (by 3 a metre walked) or while it
	 * grazes (when no walker faces it), never while its walkers turn to walk or back to graze; 7 how fast its walkers'
	 * legs walk, m/s (its frame's way along the ground ahead of it, and a turn's own steps); 10 how far its legs have
	 * walked, m (wrapped to its loop's length, a whole number of every member's strides; each whole turn to walk or back
	 * to graze adds Walker::PivotM, its walkers stepping round where they stand) and 11 how long it has grazed, s (wrapped
	 * to the hour, a whole number of every member's rounds of its grazing circle; it grazes only while none of it is on
	 * the move), each as of 12, a moment on the material's clock, and running on from it at 7 and while 13 is 0; 13 the
	 * share of it that is on the move (0 grazing, 1 walking with the herd), run on through a turn at 7 over PivotM, so the
	 * material's walking is the exact integral of the speeds given and the velocity pass sees the legs move. Its latest
	 * turn to walk or back to graze and the one before it, so the material knows how each foot still down has turned and
	 * walked since it was put down: 1 how far its legs had walked when the latest began (as 10 counts it, carried back
	 * through 10's wraps), PivotBack more for a turn back to graze; 2 what came between the two: before a turn to walk, the
	 * grazing clock the stop before left it at (as 11 counts it; its grazing clock through the latest is 11, which stands
	 * still till it grazes again), before a turn back to graze how far its legs walked, m, and once it grazes again its
	 * grazing clock through that turn; 3 how fast the herd's way turns, radians a metre walked (signed as 5 turns, kept
	 * from its last walk); 4 its ways through the turn before and the latest (5 is held while its walkers turn), packed by
	 * PackTurns. 14 to 34 the drawn ground's height over the herd's frame, cm along its z, at the nodes of a 5 by 5 grid in
	 * its xy (the corners left out: the material makes each of them from the three nodes beside it), row by row from the
	 * most negative y; and 35 how far the grid reaches either way of the frame's middle, cm (its nodes half that apart).
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
		 *  radians a metre. */
		constexpr double PivotM = 4.0;
		constexpr double MaxWalkKap = 0.5;
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
	 */
	namespace Vc
	{
		constexpr float SurfaceBody = 0.f, SurfaceBeak = 0.25f, SurfaceTail = 0.5f, SurfaceMote = 0.75f, SurfaceWing = 1.f;
		constexpr float SideLeft = 0.f, SideMiddle = 0.5f, SideRight = 1.f;
		constexpr float JointRigid = 0.f, JointTail = 0.25f, JointNeck = 0.375f, JointWing = 0.5f, JointLeg = 0.75f, JointUndulate = 1.f;
		/** A wing's hinge stands this far out from the body's axis, unit-mesh cm: its root vertices (G 0) lie on it. */
		constexpr double WingRootCm = 4.0;
	}
}
