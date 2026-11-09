// Slonk - the ship's plan: her decks A to U as data at the ship's real size, each deck's corridors laid out from
// the Strategic Design deck plans inside the hull the game flies (measured off its mesh), the rooms off them,
// and the turbolift stops. The plan is pure data (no components), built once by UShipInteriorComponent, which
// draws it from instanced boxes zone by zone as the walker comes near (docs/interiors-design.md §7).
#pragma once

#include "CoreMinimal.h"
#include "ShipPlan.generated.h"

/** The rooms. The first thirteen are the signature rooms with their own sets; the rest are the kinds the plan
 *  generates (a corridor stretch, a cabin, a lab...), one kind for many places. Airlock is the staging area under
 *  the saucer hatch. Corridor is deck G's authored L, off which sickbay, the transporter room and the rec deck open. */
UENUM(BlueprintType)
enum class EShipRoom : uint8
{
	Bridge,
	Engineering,
	Sickbay,
	ShuttleBay,
	Transporter,
	Airlock,
	Corridor,
	RecDeck,
	Mess,
	Quarters,
	CrewQuarters,
	TorpedoBay,
	CargoBay,
	// The plan's kinds.
	Passage,
	Cabin,
	Lab,
	Store,
	Workshop,
	Office,
	Lounge,
	TransporterRoom,
	Brig,
	Armoury,
	Hold,
	Hatch,
	Equipment,
	Turbolift,
	Count UMETA(Hidden)
};

/** The number of signature rooms (the enum's first kinds, each a single place with an authored set). */
static constexpr int32 SlonkSignatureRooms = (int32)EShipRoom::Passage;

enum class EDeckSection : uint8 { Saucer, Dorsal, Secondary };

/** One deck: its letter, its floor height in pawn space (cm, for the 1701-A's 305 m), what the plans put there,
 *  and the hull around it (the saucer's radius at the deck's ceiling about the saucer's centre; the dorsal's and the
 *  secondary hull's box). */
struct FDeckSpec
{
	TCHAR Letter = 'A';
	EDeckSection Section = EDeckSection::Saucer;
	double Floor = 0.0;
	FString Headline;
	/** Saucer: the hull's radius at the ceiling (cm) about (SaucerX, 0); zero otherwise. */
	double Radius = 0.0;
	/** Dorsal and secondary hull: the box the deck fits in. */
	double XMin = 0.0, XMax = 0.0, HalfBeam = 0.0;
	/** "A", or "Dorsal L" for the neck's decks that repeat the saucer's letters. */
	FString Label() const;
	double Ceiling() const { return Floor + 250.0; }
};

/** What a bay's side is: the lit canted panel bay, a flat wall (a room's frontage), the same with a doorway cut
 *  (open) or shut leaves, the turbolift's door, or nothing (a junction with another passage). */
enum class EPlanWall : uint8 { Panel, Flat, DoorOpen, DoorShut, Lift, Open };

/** One bay of corridor between two frames: its centre line on the floor from P0 to P1, and each side. Side 0 is
 *  to the right of the heading (Up x heading), side 1 to the left. */
struct FPlanBay
{
	FVector P0 = FVector::ZeroVector, P1 = FVector::ZeroVector;
	EPlanWall Side[2] = { EPlanWall::Panel, EPlanWall::Panel };
	int32 Room[2] = { -1, -1 };
	int32 Stop[2] = { -1, -1 };
	/** The turn at P0 from the previous bay's heading (radians, signed): the frame there widens to cover the gap. */
	double TurnIn = 0.0;
	FVector Dir() const { return (P1 - P0).GetSafeNormal(); }
	FVector Mid() const { return (P0 + P1) * 0.5; }
	FVector SideDir(int32 S) const { const FVector D = Dir(); return FVector(-D.Y, D.X, 0.0) * (S == 0 ? 1.0 : -1.0); }
	double Length() const { return FVector::Dist(P0, P1); }
};

/** A run of bays (a ring's arc between junctions, a spoke, a straight of the secondary hull's corridor): one place
 *  to be in, one zone to draw. */
struct FPlanStretch
{
	FString Name;
	int32 Deck = 0;
	TArray<FPlanBay> Bays;
	/** Whether the run is closed by a wall at its start / end (false where it meets another passage). */
	bool bEnd0 = true, bEnd1 = true;
	/** The passages this one joins: at its ends, and through open bays. */
	TArray<int32> Links;
	FBox Bounds = FBox(ForceInit);
};

/** A generated room off a passage: an oriented box (its +X from the door into the room, Yaw about Up), the floor
 *  polygon the walls follow (four corners; a ring's rooms are wedges), the door on the corridor's wall. */
struct FPlanRoom
{
	FString Name;
	EShipRoom Kind = EShipRoom::Cabin;
	int32 Deck = 0;
	FVector Center = FVector::ZeroVector;
	FVector Half = FVector::ZeroVector;
	double Yaw = 0.0;
	FVector Corners[4];
	/** The doorway's centre on the floor at the wall, and the way in. */
	FVector Door = FVector::ZeroVector;
	FVector DoorIn = FVector::ZeroVector;
	int32 Stretch = -1;
	/** Two party walls; the room's own back wall; the front wall is the passage's flat bay. */
	FBox Bounds = FBox(ForceInit);
};

/** A turbolift stop: where the car stands (its floor centre) and which way its door faces (toward the passage or
 *  the room). A signature room's stop is its set's shut lift door: the car is entered by a fade there. */
struct FPlanStop
{
	FString Name;
	int32 Deck = 0;
	FVector Car = FVector::ZeroVector;
	FVector Facing = FVector(1, 0, 0);
	int32 Stretch = -1;
	EShipRoom Room = EShipRoom::Count;
	FString Shaft;
	bool bWalkIn = true;
};

/** The whole plan. */
struct FShipPlan
{
	TArray<FDeckSpec> Decks;
	TArray<FPlanStretch> Stretches;
	TArray<FPlanRoom> Rooms;
	TArray<FPlanStop> Stops;
	/** The saucer's centre and the scale the plan was built at (1 at 305 m). */
	double SaucerX = 8000.0;
	double Scale = 1.0;

	int32 DeckIndex(TCHAR Letter, EDeckSection Section) const;
	/** A deck by its label ("g", "dorsal l", "l": the dorsal's when the letter is only there). */
	int32 DeckFromWord(const FString& Word) const;
	const FDeckSpec* Deck(int32 Index) const { return Decks.IsValidIndex(Index) ? &Decks[Index] : nullptr; }
	/** The stops on a deck, nearest a point first. */
	TArray<int32> StopsOnDeck(int32 Deck, const FVector& Near) const;
	/** The time the car takes between two stops: the shafts' climb and the run between them, with the doors. */
	static double TravelSeconds(const FPlanStop& A, const FPlanStop& B);
};

/** A room the plan must keep clear of (a signature room's box, with its name). */
struct FPlanReserved
{
	FString Name;
	FBox Box = FBox(ForceInit);
};

namespace SlonkPlan
{
	/** The refit's corridor section (cm): the waist half-width (a room's wall stands back to back here), the floor
	 *  and ceiling half-widths, the heights of the band, the ceiling, the bay pitch between frames. */
	constexpr double CorrW = 120.0, CorrF = 95.0, CorrC = 80.0, CorrZ1 = 95.0, CorrZ2 = 155.0, CorrH = 250.0, CorrPitch = 224.0;
	constexpr double DoorW = 130.0, DoorH = 230.0;
	/** A generated room's depth from the corridor's wall. */
	constexpr double RoomDepth = 480.0;

	/** The decks of the 1701-A, floors in pawn cm at scale 1 (305 m). */
	void DeckTable(TArray<FDeckSpec>& Out, double Scale);
	/** Build the plan at a scale (the ship's length over 305 m) around the signature rooms' boxes. */
	void Build(FShipPlan& Out, double Scale, const TArray<FPlanReserved>& Reserved);
	/** The generic name of a kind the plan generates ("Cabin"); empty for the signature rooms. */
	FString KindName(EShipRoom Kind);
}
