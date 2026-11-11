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
	/** The bay of its passage the doorway is in (the zone that draws the room). */
	int32 Bay = -1;
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

/** A leg of a route through the plan: on foot along points on the floor (pawn space, bay middle to bay middle),
 *  or a ride in a lift between two stops taking so many seconds. */
struct FPlanRouteLeg
{
	bool bLift = false;
	TArray<FVector> Points;
	int32 FromStop = INDEX_NONE, ToStop = INDEX_NONE;
	double Seconds = 0.0;
};

struct FPlanRoute
{
	TArray<FPlanRouteLeg> Legs;
	/** The distance on foot, cm. */
	double Walked() const;
};

/** Paths through the plan's passages for whoever walks them: every bay a node, consecutive bays and the
 *  junctions between linked passages the edges, Dijkstra between them; across decks by the nearest walk-in
 *  stops, and from a set's own shut door (a stop that is not walked into) straight by lift. */
class SLONK_API FPlanRouter
{
public:
	explicit FPlanRouter(const FShipPlan& InPlan);
	int32 NumNodes() const { return Nodes.Num(); }
	int32 NodeOf(int32 Stretch, int32 Bay) const;
	/** The bay nearest a point on a deck, within Reach of its middle; INDEX_NONE if none. */
	int32 NodeNear(int32 Deck, const FVector& P, double Reach = 600.0) const;
	/** The bay a walk-in stop's door is in; INDEX_NONE for a set's stop. */
	int32 NodeOfStop(int32 Stop) const;
	/** Whether a deck has a stop that is walked into (deck G's passages ride from the authored corridor's door). */
	bool DeckHasStop(int32 Deck) const;
	/** Where to stand at a stop's door, in the passage (a set's: its car's place). */
	FVector DoorOf(int32 Stop) const;
	/** The points from one bay to another along the passages (same deck), false if unconnected. */
	bool Walk(int32 A, int32 B, TArray<FVector>& OutPoints) const;
	/** A route from a point (or a set's stop, when FromStop names one that is not walked into) to another, on
	 *  foot where the passages join, else by lift between the nearest stops. False when no way exists. */
	bool Route(int32 FromDeck, const FVector& From, int32 FromStop, int32 ToDeck, const FVector& To, int32 ToStop, FPlanRoute& Out) const;

private:
	const FShipPlan& Plan;
	struct FNode { int32 Stretch = 0, Bay = 0; FVector Mid; TArray<TPair<int32, double>> Edges; };
	TArray<FNode> Nodes;
	TArray<int32> First;   // the first node of each stretch
	void Dijkstra(int32 Start, TArray<double>& Dist, TArray<int32>& Prev) const;
	/** The walk-in stop on the start's deck nearest it along the passages, with the distance; INDEX_NONE if none. */
	int32 NearestStop(int32 Start, double& OutDist) const;
	void Trace(const TArray<int32>& Prev, int32 From, int32 To, TArray<FVector>& OutPoints) const;
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
	/** The saucer's skin at a radius from its centre (pawn cm at the scale): the underside's and the top's heights,
	 *  measured off the hull mesh in its forward half. A floor must lie above the one and a ceiling below the other. */
	void SaucerSkin(double R, double Scale, double& Under, double& Top);
	/** Build the plan at a scale (the ship's length over 305 m) around the signature rooms' boxes. */
	void Build(FShipPlan& Out, double Scale, const TArray<FPlanReserved>& Reserved);
	/** The generic name of a kind the plan generates ("Cabin"); empty for the signature rooms. */
	FString KindName(EShipRoom Kind);
}
