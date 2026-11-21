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
		/** Whether this build spawns it (the fliers so far; the others are the later phases'). */
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
		/** A craft lower than ScareAltitudeCm over the ground and within ScareRadiusCm of a group startles it; so does
		 *  someone on foot within CrewScareCm. A startled group flees this far, cm, before it drifts back. */
		double ScareRadiusCm = 0.0;
		double ScareAltitudeCm = 0.0;
		double CrewScareCm = 0.0;
		double FleeCm = 0.0;
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
	 * A fauna mesh's vertex colour: R the surface (0 body, 0.25 a bill or a beak, 0.5 a tail, 1 a wing), G how far it
	 * flexes (0 at its joint, rising to 1 at the tip), B its side (0 left, 0.5 the middle, 1 right) and A the kind of
	 * joint the material moves it by (0 rigid, 0.25 a tail, 0.5 a wing hinged along the body, 0.75 a leg, 1 a fin or a
	 * body that undulates).
	 */
	namespace Vc
	{
		constexpr float SurfaceBody = 0.f, SurfaceBeak = 0.25f, SurfaceTail = 0.5f, SurfaceWing = 1.f;
		constexpr float SideLeft = 0.f, SideMiddle = 0.5f, SideRight = 1.f;
		constexpr float JointRigid = 0.f, JointTail = 0.25f, JointWing = 0.5f, JointLeg = 0.75f, JointUndulate = 1.f;
		/** A wing's hinge stands this far out from the body's axis, unit-mesh cm: its root vertices (G 0) lie on it. */
		constexpr double WingRootCm = 4.0;
	}
}
