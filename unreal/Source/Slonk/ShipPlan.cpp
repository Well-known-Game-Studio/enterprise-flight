// Slonk - the ship's plan (see ShipPlan.h): the decks, their corridors and rooms, the turbolift stops.
#include "ShipPlan.h"

FString FDeckSpec::Label() const
{
	return Section == EDeckSection::Dorsal ? FString::Printf(TEXT("Dorsal %c"), Letter) : FString::Printf(TEXT("%c"), Letter);
}

int32 FShipPlan::DeckIndex(TCHAR Letter, EDeckSection Section) const
{
	for (int32 i = 0; i < Decks.Num(); ++i) { if (Decks[i].Letter == Letter && Decks[i].Section == Section) { return i; } }
	return INDEX_NONE;
}

int32 FShipPlan::DeckFromWord(const FString& WordIn) const
{
	FString W = WordIn.ToLower().Replace(TEXT(" "), TEXT(""));
	W.RemoveFromStart(TEXT("deck"));
	bool bDorsal = W.RemoveFromStart(TEXT("dorsal"));
	W.RemoveFromStart(TEXT("deck"));
	if (W.Len() != 1) { return INDEX_NONE; }
	const TCHAR L = FChar::ToUpper(W[0]);
	if (bDorsal) { return DeckIndex(L, EDeckSection::Dorsal); }
	// A letter that is only the dorsal's (K, L, M) names it; I and J are the saucer's first.
	for (EDeckSection S : { EDeckSection::Saucer, EDeckSection::Secondary, EDeckSection::Dorsal })
	{
		const int32 I = DeckIndex(L, S);
		if (I != INDEX_NONE) { return I; }
	}
	return INDEX_NONE;
}

TArray<int32> FShipPlan::StopsOnDeck(int32 Deck, const FVector& Near) const
{
	TArray<int32> Out;
	for (int32 i = 0; i < Stops.Num(); ++i) { if (Stops[i].Deck == Deck) { Out.Add(i); } }
	Out.Sort([&](int32 A, int32 B) { return FVector::DistSquared2D(Stops[A].Car, Near) < FVector::DistSquared2D(Stops[B].Car, Near); });
	return Out;
}

double FShipPlan::TravelSeconds(const FPlanStop& A, const FPlanStop& B)
{
	// The car climbs at 8 m/s and runs along the horizontal shafts at 15 m/s; the doors and the start and stop
	// take the rest. The shafts are not drawn, so the run is the straight distance between the two stops.
	const double Climb = FMath::Abs(A.Car.Z - B.Car.Z) / 800.0;
	const double Run = FVector::Dist2D(A.Car, B.Car) / 1500.0;
	return 2.5 + Climb + Run;
}

FString SlonkPlan::KindName(EShipRoom Kind)
{
	switch (Kind)
	{
	case EShipRoom::Passage: return TEXT("Corridor");
	case EShipRoom::Cabin: return TEXT("Quarters");
	case EShipRoom::Lab: return TEXT("Laboratory");
	case EShipRoom::Store: return TEXT("Stores");
	case EShipRoom::Workshop: return TEXT("Workshop");
	case EShipRoom::Office: return TEXT("Office");
	case EShipRoom::Lounge: return TEXT("Lounge");
	case EShipRoom::TransporterRoom: return TEXT("Transporter room");
	case EShipRoom::Brig: return TEXT("Brig");
	case EShipRoom::Armoury: return TEXT("Armoury");
	case EShipRoom::Hold: return TEXT("Cargo hold");
	case EShipRoom::Hatch: return TEXT("Hull lock");
	case EShipRoom::Equipment: return TEXT("Equipment room");
	case EShipRoom::Turbolift: return TEXT("Turbolift");
	default: return FString();
	}
}

void SlonkPlan::DeckTable(TArray<FDeckSpec>& Out, double X)
{
	// Measured off the 1701-A mesh (tools/plan/measure_hull.py): the saucer's plate is 11.4 m thick between
	// z 1144 and 2200 out to its rim (r 6600..7250, z 1434..2048), the superstructure steps up to the bridge
	// module (x 7209..8855, |y| < 700, z 3100..3450) and the lower dome down to z 166 at the centre; the neck is
	// 12 m wide and 21 m long; the secondary hull runs from z -700 to -3574. Floors: the saucer at 2.8 m from the
	// bridge module down, the dorsal at 2.8 m from the torpedo bay set, the secondary hull at 3.2 m from the
	// hangar deck. Each deck's hull limit is the radius (or box) at its ceiling.
	Out.Reset();
	auto Saucer = [&](TCHAR L, double Floor, double Radius, const TCHAR* Headline)
	{
		FDeckSpec D; D.Letter = L; D.Section = EDeckSection::Saucer; D.Floor = Floor * X; D.Radius = Radius * X; D.Headline = Headline; Out.Add(D);
	};
	auto Boxed = [&](TCHAR L, EDeckSection S, double Floor, double XMin, double XMax, double HalfBeam, const TCHAR* Headline)
	{
		FDeckSpec D; D.Letter = L; D.Section = S; D.Floor = Floor * X; D.XMin = XMin * X; D.XMax = XMax * X; D.HalfBeam = HalfBeam * X; D.Headline = Headline; Out.Add(D);
	};
	Saucer('A', 3130.0, 900.0, TEXT("Main bridge, upper sensor array, docking port 1"));
	Boxed('B', EDeckSection::Saucer, 2850.0, 6321.0, 9254.0, 850.0, TEXT("Briefing room 1, captain's ready room, executive officer's office, the airlock"));
	Saucer('C', 2570.0, 1800.0, TEXT("Officers' lounge complex: dining lounge, observation lounge, wardrooms, kitchen"));
	Saucer('D', 2290.0, 2400.0, TEXT("Computer lab, high-energy physics lab, senior officers' quarters"));
	Saucer('E', 2010.0, 3200.0, TEXT("Officers' dining mess, main computer memory banks, turbolift maintenance, officers' quarters"));
	Saucer('F', 1730.0, 7100.0, TEXT("Enlisted quarters, damage control locker, forward observation lounge, bowling alley"));
	Saucer('G', 1450.0, 7050.0, TEXT("Labs, transporter rooms, security complex, sickbay complex, rec deck, impulse engineering"));
	Saucer('H', 1170.0, 6800.0, TEXT("Hull locks, phaser control, airlocks, workshops, communications bay, physics labs"));
	Saucer('I', 890.0, 1700.0, TEXT("Cargo transporters, cargo holds, observation gallery"));
	Saucer('J', 610.0, 1000.0, TEXT("Science sensor arrays, fire-control sensors, sensor bay: no access"));
	Boxed('I', EDeckSection::Dorsal, 572.0, 1250.0, 3150.0, 560.0, TEXT("Observation lounge, engineering equipment"));
	Boxed('J', EDeckSection::Dorsal, 292.0, 1250.0, 3150.0, 560.0, TEXT("Engineering equipment"));
	Boxed('K', EDeckSection::Dorsal, 12.0, 1250.0, 3150.0, 560.0, TEXT("Observation lounge, shield generators"));
	Boxed('L', EDeckSection::Dorsal, -268.0, 1250.0, 3150.0, 560.0, TEXT("Torpedo exhaust ducting, casing storage, warhead locker"));
	Boxed('M', EDeckSection::Dorsal, -548.0, 1250.0, 3150.0, 560.0, TEXT("Torpedo launch tubes and bay, docking ports 2 and 3"));
	Boxed('N', EDeckSection::Secondary, -986.0, -5000.0, 2800.0, 1200.0, TEXT("Engineering systems"));
	Boxed('O', EDeckSection::Secondary, -1306.0, -6300.0, 3600.0, 1300.0, TEXT("Main engineering: the intermix shaft, nacelle conduits, dilithium chamber; flight control"));
	Boxed('P', EDeckSection::Secondary, -1626.0, -6300.0, 3800.0, 1450.0, TEXT("Thruster control, briefing room 2, engineering offices and workshops, hangar control gallery"));
	Boxed('Q', EDeckSection::Secondary, -1946.0, -6300.0, 3800.0, 1550.0, TEXT("Power distribution, cargo high-bay, docking ports 4 and 5, the hangar bay"));
	Boxed('R', EDeckSection::Secondary, -2266.0, -6200.0, 3800.0, 1450.0, TEXT("Deflector beam emitter, environmental control, cargo mid-bay, parking bay 1"));
	Boxed('S', EDeckSection::Secondary, -2586.0, -4500.0, 3800.0, 1450.0, TEXT("Fusion reactor control, secondary hull bridge, the cargo bay, parking bay 2"));
	Boxed('T', EDeckSection::Secondary, -2906.0, -3900.0, 3600.0, 1350.0, TEXT("Reactor control, botanical garden and lab, observation lounge, pool complex"));
	Boxed('U', EDeckSection::Secondary, -3226.0, -3700.0, 3000.0, 1050.0, TEXT("Reactor complex: intermix shaft foot, antimatter bottles, matter reserve tanks"));
}

namespace
{
	using namespace SlonkPlan;

	struct FRoomSpec
	{
		const TCHAR* Name;
		EShipRoom Kind;
		int32 Bays;
		int32 Count;   // how many of these before the next; -1 fills whatever is left
	};

	/** The plan's builder: geometry helpers over FShipPlan. All lengths cm; angles about the saucer's centre
	 *  measured from the bow toward starboard. */
	struct FBuilder
	{
		FShipPlan& P;
		double X;
		const TArray<FPlanReserved>& Reserved;
		TMap<FString, int32> Numbers;   // running numbers per deck and room name

		FBuilder(FShipPlan& InP, double InX, const TArray<FPlanReserved>& InReserved) : P(InP), X(InX), Reserved(InReserved) {}

		double Floor(int32 Deck) const { return P.Decks[Deck].Floor; }
		FVector Polar(double Deg, double R, double Z) const
		{
			const double A = FMath::DegreesToRadians(Deg);
			return FVector(P.SaucerX + R * FMath::Cos(A), R * FMath::Sin(A), Z);
		}
		double AngleOf(const FVector& Pt) const { return FMath::RadiansToDegrees(FMath::Atan2(Pt.Y, Pt.X - P.SaucerX)); }
		double RadiusOf(const FVector& Pt) const { return FVector::Dist2D(Pt, FVector(P.SaucerX, 0, 0)); }

		/** Whether a floor-level box lies clear of the reserved rooms and inside the deck's hull. */
		bool Clear(const FBox& B, int32 Deck) const
		{
			for (const FPlanReserved& R : Reserved) { if (R.Box.ExpandBy(30.0).Intersect(B)) { return false; } }
			const FDeckSpec& D = P.Decks[Deck];
			if (D.Radius > 0.0)
			{
				for (int32 c = 0; c < 4; ++c)
				{
					const FVector Corner(c & 1 ? B.Max.X : B.Min.X, c & 2 ? B.Max.Y : B.Min.Y, 0.0);
					if (RadiusOf(Corner) > D.Radius) { return false; }
				}
			}
			else if (B.Min.X < D.XMin || B.Max.X > D.XMax || B.Min.Y < -D.HalfBeam || B.Max.Y > D.HalfBeam) { return false; }
			return true;
		}

		FString Numbered(const TCHAR* Name, int32 Deck, int32 Count)
		{
			if (Count == 1) { return Name; }
			const FString Key = FString::Printf(TEXT("%s|%d"), Name, Deck);
			const int32 N = ++Numbers.FindOrAdd(Key);
			return FString::Printf(TEXT("%s %s-%d"), Name, *P.Decks[Deck].Label(), N);
		}

		// ---- passages ----------------------------------------------------------------------------------------

		/** A straight run of bays from A to B at the deck's floor, the pitch as near CorrPitch as divides it. */
		int32 Straight(int32 Deck, const FString& Name, const FVector& A, const FVector& B, bool bEnd0, bool bEnd1)
		{
			FPlanStretch S; S.Name = Name; S.Deck = Deck; S.bEnd0 = bEnd0; S.bEnd1 = bEnd1;
			const double Len = FVector::Dist2D(A, B);
			const int32 N = FMath::Max(1, FMath::RoundToInt(Len / CorrPitch));
			const FVector A0(A.X, A.Y, Floor(Deck)), B0(B.X, B.Y, Floor(Deck));
			for (int32 i = 0; i < N; ++i)
			{
				FPlanBay Bay; Bay.P0 = FMath::Lerp(A0, B0, (double)i / N); Bay.P1 = FMath::Lerp(A0, B0, (double)(i + 1) / N);
				S.Bays.Add(Bay);
			}
			return P.Stretches.Add(S);
		}

		/** A ring of N bays at radius R about the saucer's centre, its vertices on the circle from Deg0; one stretch,
		 *  closed on itself (no end walls). */
		int32 Ring(int32 Deck, const FString& Name, double R, int32 N, double Deg0 = 0.0)
		{
			FPlanStretch S; S.Name = Name; S.Deck = Deck; S.bEnd0 = S.bEnd1 = false;
			const double Step = 360.0 / N;
			for (int32 i = 0; i < N; ++i)
			{
				FPlanBay Bay; Bay.P0 = Polar(Deg0 + i * Step, R, Floor(Deck)); Bay.P1 = Polar(Deg0 + (i + 1) * Step, R, Floor(Deck));
				Bay.TurnIn = FMath::DegreesToRadians(Step);
				S.Bays.Add(Bay);
			}
			return P.Stretches.Add(S);
		}

		/** An arc of bays turning `TurnDeg` per bay for `N` bays from pose (A, Dir), then a straight to `End`. */
		int32 ArcThenStraight(int32 Deck, const FString& Name, const FVector& A, const FVector& DirIn, double TurnDeg, int32 N, const FVector& End, bool bEnd1)
		{
			FPlanStretch S; S.Name = Name; S.Deck = Deck; S.bEnd0 = false; S.bEnd1 = bEnd1;
			FVector Pos(A.X, A.Y, Floor(Deck)); FVector Dir = DirIn.GetSafeNormal2D();
			for (int32 i = 0; i < N; ++i)
			{
				FPlanBay Bay; Bay.P0 = Pos;
				if (i > 0) { Dir = Dir.RotateAngleAxis(TurnDeg, FVector::UpVector); Bay.TurnIn = FMath::DegreesToRadians(TurnDeg); }
				Pos = Pos + Dir * CorrPitch; Bay.P1 = Pos;
				S.Bays.Add(Bay);
			}
			const FVector E(End.X, End.Y, Floor(Deck));
			const double Len = FVector::Dist2D(Pos, E);
			const int32 M = FMath::Max(1, FMath::RoundToInt(Len / CorrPitch));
			const FVector Start = Pos;
			for (int32 i = 0; i < M; ++i)
			{
				FPlanBay Bay; Bay.P0 = FMath::Lerp(Start, E, (double)i / M); Bay.P1 = FMath::Lerp(Start, E, (double)(i + 1) / M);
				if (i == 0) { Bay.TurnIn = FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir, (E - Start).GetSafeNormal2D()), -1.0, 1.0)) * FMath::Sign(FVector::CrossProduct(Dir, (E - Start).GetSafeNormal2D()).Z); }
				S.Bays.Add(Bay);
			}
			return P.Stretches.Add(S);
		}

		/** The ring bay whose middle is nearest an angle. */
		int32 BayAtAngle(int32 Stretch, double Deg) const
		{
			const FPlanStretch& S = P.Stretches[Stretch];
			int32 Best = 0; double BestD = 1e9;
			for (int32 i = 0; i < S.Bays.Num(); ++i)
			{
				const double D = FMath::Abs(FMath::FindDeltaAngleDegrees(AngleOf(S.Bays[i].Mid()), Deg));
				if (D < BestD) { BestD = D; Best = i; }
			}
			return Best;
		}
		/** The bay whose middle is nearest a point. */
		int32 BayNear(int32 Stretch, const FVector& Pt) const
		{
			const FPlanStretch& S = P.Stretches[Stretch];
			int32 Best = 0; double BestD = 1e18;
			for (int32 i = 0; i < S.Bays.Num(); ++i) { const double D = FVector::DistSquared2D(S.Bays[i].Mid(), Pt); if (D < BestD) { BestD = D; Best = i; } }
			return Best;
		}

		/** Open a bay's side onto another passage and link the two. Returns the point on the opened wall's plane
		 *  under the bay's middle, where the other passage begins. */
		FVector Open(int32 Stretch, int32 Bay, int32 Side, int32 Other)
		{
			FPlanStretch& S = P.Stretches[Stretch];
			S.Bays[Bay].Side[Side] = EPlanWall::Open;
			// The other passage may not be built yet (its index is known ahead); Finish() makes the links mutual.
			if (Other >= 0) { S.Links.AddUnique(Other); }
			return S.Bays[Bay].Mid() + S.Bays[Bay].SideDir(Side) * CorrW;
		}

		/** A turbolift door in a bay's side; the car stands behind it. */
		int32 Lift(int32 Stretch, int32 Bay, int32 Side, const FString& Shaft)
		{
			FPlanStretch& S = P.Stretches[Stretch];
			FPlanBay& B = S.Bays[Bay];
			B.Side[Side] = EPlanWall::Lift;
			FPlanStop St; St.Name = FString::Printf(TEXT("Deck %s, %s"), *P.Decks[S.Deck].Label(), *S.Name); St.Deck = S.Deck; St.Shaft = Shaft;
			St.Car = B.Mid() + B.SideDir(Side) * (CorrW + 140.0); St.Facing = -B.SideDir(Side); St.Stretch = Stretch;
			B.Stop[Side] = P.Stops.Num();
			return P.Stops.Add(St);
		}

		// ---- rooms -------------------------------------------------------------------------------------------

		/** Rooms along one side of a stretch: specs in order, each its count, the last with -1 filling the rest.
		 *  A room takes whole bays whose side is still the panel kind, its frontage becoming flat wall with the
		 *  doorway in the middle bay; one that would meet a reserved room or the hull is left as panel bays. */
		void RoomsAlong(int32 Stretch, int32 Side, const TArray<FRoomSpec>& Specs, double Depth = RoomDepth, bool bWedge = false)
		{
			FPlanStretch& S = P.Stretches[Stretch];
			const int32 N = S.Bays.Num();
			// A bay can front a room if its side is still panel, it is not beside a junction on that side (the
			// other passage's walls pass there), it lies clear of an open end by a room's depth (the rooms of the
			// passage beyond stand there), and, off a ring, it does not turn (party walls would splay).
			double Along = 0.0; TArray<double> Start; Start.Reserve(N);
			for (const FPlanBay& B : S.Bays) { Start.Add(Along); Along += B.Length(); }
			const double Total = Along;
			auto Eligible = [&](int32 k)
			{
				if (S.Bays[k].Side[Side] != EPlanWall::Panel) { return false; }
				if (!bWedge && FMath::Abs(S.Bays[k].TurnIn) > 0.05) { return false; }
				if (k + 1 < N && FMath::Abs(S.Bays[k + 1].TurnIn) > 0.05 && !bWedge) { return false; }
				for (int32 n : { k - 1, k + 1 }) { if (S.Bays.IsValidIndex(n) && S.Bays[n].Side[Side] == EPlanWall::Open) { return false; } }
				if (!S.bEnd0 && Start[k] < Depth + 20.0) { return false; }
				if (!S.bEnd1 && Total - (Start[k] + S.Bays[k].Length()) < Depth + 20.0) { return false; }
				return true;
			};
			int32 Spec = 0, Placed = 0;
			int32 i = 0;
			while (i < N && Spec < Specs.Num())
			{
				const FRoomSpec& R = Specs[Spec];
				// Enough consecutive eligible bays from i for this room?
				int32 j = i; while (j < N && j - i < R.Bays && Eligible(j)) { ++j; }
				if (j - i < R.Bays) { i = FMath::Max(j, i + 1); continue; }
				if (TryRoom(Stretch, i, j - 1, Side, R, Depth, bWedge)) { ++Placed; }
				i = j;
				if (R.Count > 0 && Placed >= R.Count) { ++Spec; Placed = 0; }
			}
		}

		bool TryRoom(int32 Stretch, int32 I0, int32 I1, int32 Side, const FRoomSpec& Spec, double Depth, bool bWedge)
		{
			FPlanStretch& S = P.Stretches[Stretch];
			const FPlanBay& B0 = S.Bays[I0]; const FPlanBay& B1 = S.Bays[I1];
			const int32 Mid = (I0 + I1) / 2;
			const FVector Z0(0, 0, Floor(S.Deck));
			// The frontage's chord and the way in.
			const FVector F0 = B0.P0 + B0.SideDir(Side) * CorrW, F1 = B1.P1 + B1.SideDir(Side) * CorrW;
			const FVector Along = (F1 - F0).GetSafeNormal2D();
			FVector In = S.Bays[Mid].SideDir(Side);
			In = (In - Along * FVector::DotProduct(In, Along)).GetSafeNormal2D();
			const double Frontage = FVector::Dist2D(F0, F1);
			FPlanRoom Room; Room.Kind = Spec.Kind; Room.Deck = S.Deck; Room.Stretch = Stretch;
			Room.Yaw = FMath::Atan2(In.Y, In.X);
			Room.Half = FVector(Depth / 2, Frontage / 2, CorrH / 2);
			Room.Center = (F0 + F1) * 0.5 + In * (Depth / 2) + FVector(0, 0, CorrH / 2);
			Room.Center.Z = Floor(S.Deck) + CorrH / 2;
			Room.Door = S.Bays[Mid].Mid() + S.Bays[Mid].SideDir(Side) * CorrW; Room.Door.Z = Floor(S.Deck);
			Room.DoorIn = S.Bays[Mid].SideDir(Side);
			Room.Bay = Mid;
			// Corners: the front two at the wall; the back two along the party walls (radial on a ring).
			Room.Corners[0] = F0; Room.Corners[1] = F1;
			if (bWedge)
			{
				const FVector C(P.SaucerX, 0, Floor(S.Deck));
				const double Sign = FVector::DotProduct(In, (F0 - C).GetSafeNormal2D()) > 0 ? 1.0 : -1.0;
				Room.Corners[2] = C + (F1 - C) * ((RadiusOf(F1) + Sign * Depth) / RadiusOf(F1));
				Room.Corners[3] = C + (F0 - C) * ((RadiusOf(F0) + Sign * Depth) / RadiusOf(F0));
			}
			else
			{
				Room.Corners[2] = F1 + In * Depth; Room.Corners[3] = F0 + In * Depth;
			}
			// The box is the one inscribed in the wedge (the narrower of its front and back), so neighbours' boxes
			// never overlap; the floor drawn later covers the wider end.
			Room.Half.Y = FMath::Min(Frontage, FVector::Dist2D(Room.Corners[2], Room.Corners[3])) / 2;
			FBox B(ForceInit);
			for (const FVector& Cn : Room.Corners) { B += FVector(Cn.X, Cn.Y, Floor(S.Deck)); B += FVector(Cn.X, Cn.Y, Floor(S.Deck) + CorrH); }
			Room.Bounds = B;
			if (!Clear(B, S.Deck)) { return false; }
			Room.Name = Numbered(Spec.Name, S.Deck, Spec.Count);
			const int32 Index = P.Rooms.Add(Room);
			for (int32 k = I0; k <= I1; ++k) { S.Bays[k].Side[Side] = k == Mid ? EPlanWall::DoorOpen : EPlanWall::Flat; S.Bays[k].Room[Side] = Index; }
			return true;
		}

		// ---- decks -------------------------------------------------------------------------------------------

		/** The core's lobby ring on a saucer deck with the main shaft's door aft, and the four diagonal junctions
		 *  opened where asked. */
		int32 Lobby(int32 Deck, const TArray<double>& OutAngles, bool bLiftAft = true)
		{
			const int32 L = Ring(Deck, TEXT("lobby ring"), 1000.0 * X, 28);
			if (bLiftAft) { Lift(L, BayAtAngle(L, 180.0), 1, TEXT("Main")); }
			for (double A : OutAngles) { Open(L, BayAtAngle(L, A), 1, -1); }
			return L;
		}

		/** A spoke from one ring's wall to another's, radial at an angle; opens both rings' bays onto it. */
		int32 Spoke(int32 Deck, const FString& Name, int32 Inner, double RInner, int32 Outer, double ROuter, double Deg)
		{
			const int32 Bi = BayAtAngle(Inner, Deg), Bo = BayAtAngle(Outer, Deg);
			const int32 S = P.Stretches.Num();
			const FVector A = Open(Inner, Bi, 1, S);
			const FVector B = Open(Outer, Bo, 0, S);
			return Straight(Deck, Name, A, B, false, false);
		}

		void PlateDeck(TCHAR Letter, bool bCoreSet)
		{
			const int32 D = P.DeckIndex(Letter, EDeckSection::Saucer);
			const FString Dl = P.Decks[D].Label();
			// The rings: the outer one near the rim, the inner one midway; eight short spokes between them.
			const int32 Outer = Ring(D, TEXT("outer ring"), 6000.0 * X, 168);
			const int32 Inner = Ring(D, TEXT("inner ring"), 4700.0 * X, 132);
			for (int32 k = 0; k < 8; ++k) { Spoke(D, FString::Printf(TEXT("spoke %d"), k + 1), Inner, 4700.0 * X, Outer, 6000.0 * X, k * 45.0); }
			// The rim lifts: in the outer ring's inner wall beside the diagonal junctions; the dorsal shaft's door
			// just aft of the aft junction, the one shaft that runs down the neck into the secondary hull.
			const int32 NOuter = P.Stretches[Outer].Bays.Num();
			for (int32 k = 0; k < 4; ++k) { Lift(Outer, (BayAtAngle(Outer, 45.0 + k * 90.0) + 2) % NOuter, 0, FString::Printf(TEXT("Rim %d"), k + 1)); }
			Lift(Outer, (BayAtAngle(Outer, 180.0) + 2) % NOuter, 0, TEXT("Dorsal"));
			// The core: deck G's own L of corridor (the authored set) with its lift aft of sickbay and its port
			// end open; on F and H the same L is generated, so the lift shaft runs straight through the three.
			const FVector LegAEnd(5880.0 * X, -1150.0 * X, 0.0);
			int32 LegA = -1;
			if (!bCoreSet)
			{
				LegA = Straight(D, TEXT("core corridor, athwartships leg"), FVector(5880.0 * X, 210.0 * X, 0), LegAEnd, true, false);
				P.Stretches[LegA].bEnd0 = true;
				// The lift in the forward end wall: the stop stands past it.
				FPlanStop St; St.Name = FString::Printf(TEXT("Deck %s, core corridor"), *Dl); St.Deck = D; St.Shaft = TEXT("Aft");
				St.Car = FVector(5880.0 * X, (210.0 + 140.0) * X, Floor(D)); St.Facing = FVector(0, -1, 0); St.Stretch = LegA;
				P.Stops.Add(St);
				const int32 LegB = P.Stretches.Num();
				const FVector BStart = Open(LegA, P.Stretches[LegA].Bays.Num() - 1, 0, LegB);
				Straight(D, TEXT("core corridor, forward leg"), FVector(BStart.X, -1030.0 * X, 0), FVector(7700.0 * X, -1030.0 * X, 0), false, true);
			}
			// From the L's open port end an arc swings the corridor round to run out radially to the inner ring.
			const int32 Arc = P.Stretches.Num();
			FVector ArcEnd; int32 Turns = 0; double TurnDeg = 0.0;
			ArcToRing(LegAEnd, FVector(0, -1, 0), 4700.0 * X, TurnDeg, Turns, ArcEnd);
			const int32 ArcBay = BayAtAngle(Inner, AngleOf(ArcEnd));
			const FVector Join = Open(Inner, ArcBay, 0, Arc);
			ArcThenStraight(D, TEXT("core spoke"), LegAEnd, FVector(0, -1, 0), TurnDeg, Turns, Join, false);
			if (LegA >= 0) { P.Stretches[LegA].Links.AddUnique(Arc); P.Stretches[Arc].Links.AddUnique(LegA); }
			// Three long spokes from the inner ring in toward the core on the other diagonals, dead-ending short
			// of it (the core's own rooms stand there).
			for (double A : { 45.0, 135.0, 315.0 })
			{
				const int32 Sp = P.Stretches.Num();
				const FVector From = Open(Inner, BayAtAngle(Inner, A), 0, Sp);
				Straight(D, FString::Printf(TEXT("spoke at %.0f"), A), From, Polar(A, 2000.0 * X, 0), false, true);
			}
			// The rooms. Deck F: the enlisted quarters both sides of the outer ring, the rest by the plans. G: labs and
			// the security complex, the transporter rooms, the junior officers' quarters, the holds. H: the hull
			// locks and the phaser rooms at the rim, the workshops and physics labs.
			TArray<FRoomSpec> OuterOut, OuterIn, InnerOut, InnerIn, Spokes;
			if (Letter == 'F')
			{
				OuterOut = { { TEXT("Forward observation lounge"), EShipRoom::Lounge, 4, 1 }, { TEXT("Enlisted quarters"), EShipRoom::Cabin, 2, -1 } };
				OuterIn = { { TEXT("Damage control locker"), EShipRoom::Store, 2, 3 }, { TEXT("Enlisted quarters"), EShipRoom::Cabin, 2, -1 } };
				InnerOut = { { TEXT("Observation dining mess"), EShipRoom::Lounge, 4, 6 }, { TEXT("Enlisted quarters"), EShipRoom::Cabin, 2, -1 } };
				InnerIn = { { TEXT("Bowling alley"), EShipRoom::Lounge, 5, 1 }, { TEXT("Firing range"), EShipRoom::Armoury, 5, 1 }, { TEXT("Upper rec deck balcony"), EShipRoom::Lounge, 4, 1 }, { TEXT("Enlisted quarters"), EShipRoom::Cabin, 2, -1 } };
				Spokes = { { TEXT("Enlisted quarters"), EShipRoom::Cabin, 2, -1 } };
			}
			else if (Letter == 'G')
			{
				OuterOut = { { TEXT("Biochemistry lab"), EShipRoom::Lab, 3, 1 }, { TEXT("Chemistry lab"), EShipRoom::Lab, 3, 1 }, { TEXT("Anthropology lab"), EShipRoom::Lab, 3, 1 }, { TEXT("Biophysics lab"), EShipRoom::Lab, 3, 1 },
							 { TEXT("Junior officers' quarters"), EShipRoom::Cabin, 2, -1 } };
				OuterIn = { { TEXT("Personnel transporter room"), EShipRoom::TransporterRoom, 3, 5 }, { TEXT("General cargo hold"), EShipRoom::Hold, 3, 9 }, { TEXT("Junior officers' quarters"), EShipRoom::Cabin, 2, -1 } };
				InnerOut = { { TEXT("Brig cell"), EShipRoom::Brig, 1, 8 }, { TEXT("Security control"), EShipRoom::Office, 2, 1 }, { TEXT("Armoury"), EShipRoom::Armoury, 3, 1 }, { TEXT("Security lounge"), EShipRoom::Lounge, 2, 1 }, { TEXT("Staging room"), EShipRoom::Store, 3, 1 },
							 { TEXT("Science lab"), EShipRoom::Lab, 3, -1 } };
				InnerIn = { { TEXT("Impulse engineering"), EShipRoom::Workshop, 6, 1 }, { TEXT("Computer archival memory"), EShipRoom::Equipment, 3, 2 }, { TEXT("Command intelligence center"), EShipRoom::Office, 4, 1 }, { TEXT("Auxiliary bridge"), EShipRoom::Office, 4, 1 }, { TEXT("Science lab"), EShipRoom::Lab, 3, -1 } };
				Spokes = { { TEXT("Science lab"), EShipRoom::Lab, 3, -1 } };
			}
			else
			{
				OuterOut = { { TEXT("Hull lock"), EShipRoom::Hatch, 2, 4 }, { TEXT("Phaser control room"), EShipRoom::Equipment, 3, 3 }, { TEXT("Evacuation transporter"), EShipRoom::TransporterRoom, 3, 2 }, { TEXT("Equipment store"), EShipRoom::Store, 2, -1 } };
				OuterIn = { { TEXT("Airlock"), EShipRoom::Hatch, 2, 2 }, { TEXT("Maintenance workshop"), EShipRoom::Workshop, 3, 2 }, { TEXT("Damage control workshop"), EShipRoom::Workshop, 3, 2 }, { TEXT("Damage control locker"), EShipRoom::Store, 2, 2 }, { TEXT("Waiting lounge"), EShipRoom::Lounge, 2, 2 }, { TEXT("Equipment store"), EShipRoom::Store, 2, -1 } };
				InnerOut = { { TEXT("Communications bay"), EShipRoom::Equipment, 4, 1 }, { TEXT("Physics lab"), EShipRoom::Lab, 3, 2 }, { TEXT("Astrophysics lab"), EShipRoom::Lab, 3, 1 }, { TEXT("Chaos physics lab"), EShipRoom::Lab, 3, 1 }, { TEXT("Tachyonics lab"), EShipRoom::Lab, 3, 1 }, { TEXT("Equipment store"), EShipRoom::Store, 2, -1 } };
				InnerIn = { { TEXT("Staging room"), EShipRoom::Store, 3, 2 }, { TEXT("Equipment store"), EShipRoom::Store, 2, -1 } };
				Spokes = { { TEXT("Equipment store"), EShipRoom::Store, 2, -1 } };
			}
			RoomsAlong(Outer, 1, OuterOut, RoomDepth, true);
			RoomsAlong(Outer, 0, OuterIn, RoomDepth, true);
			RoomsAlong(Inner, 1, InnerOut, RoomDepth, true);
			RoomsAlong(Inner, 0, InnerIn, RoomDepth, true);
			for (int32 s = Inner + 1; s < P.Stretches.Num(); ++s)
			{
				if (P.Stretches[s].Deck != D || P.Stretches[s].Bays.Num() < 8) { continue; }   // the short spokes stay bare
				RoomsAlong(s, 0, Spokes); RoomsAlong(s, 1, Spokes);
			}
		}

		/** From pose (A, Dir) an arc turning a few degrees a bay, then a straight, arrives on the ring of radius R
		 *  heading radially out: the turn per bay and the number of bays that get nearest, and where the arc ends. */
		void ArcToRing(const FVector& A, const FVector& Dir, double R, double& OutTurnDeg, int32& OutBays, FVector& OutArcEnd) const
		{
			const FVector C(P.SaucerX, 0, 0);
			double Best = 1e9;
			for (double Turn : { -6.0, -5.0, -4.0, -3.0, 3.0, 4.0, 5.0, 6.0 })
			{
				for (int32 N = 2; N <= 16; ++N)
				{
					FVector Pos = A; FVector D = Dir;
					for (int32 i = 0; i < N; ++i) { if (i > 0) { D = D.RotateAngleAxis(Turn, FVector::UpVector); } Pos += D * CorrPitch; }
					// Where the straight on from here meets the ring, and how far from radial it arrives.
					const FVector Rel = Pos - C;
					const double b = FVector::DotProduct(Rel, D), c = Rel.SizeSquared2D() - R * R;
					const double Disc = b * b - c;
					if (Disc < 0) { continue; }
					const double t = -b + FMath::Sqrt(Disc);
					if (t < CorrPitch) { continue; }
					const FVector Hit = Pos + D * t;
					const double Err = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(D, (Hit - C).GetSafeNormal2D()), -1.0, 1.0)));
					if (Err < Best) { Best = Err; OutTurnDeg = Turn; OutBays = N; OutArcEnd = Pos; }
				}
			}
		}

		void DeckE()
		{
			const int32 D = P.DeckIndex('E', EDeckSection::Saucer);
			const int32 L = Lobby(D, { 45.0, 135.0, 225.0, 315.0 });
			const int32 R2 = Ring(D, TEXT("quarters ring"), 2200.0 * X, 62);
			for (double A : { 45.0, 135.0, 225.0, 315.0 }) { Spoke(D, FString::Printf(TEXT("spoke at %.0f"), A), L, 1000.0 * X, R2, 2200.0 * X, A); }
			RoomsAlong(L, 0, { { TEXT("Computer core access"), EShipRoom::Equipment, 2, -1 } }, RoomDepth, true);
			RoomsAlong(R2, 1, { { TEXT("Officers' dining mess"), EShipRoom::Lounge, 5, 1 }, { TEXT("Turbolift maintenance garage"), EShipRoom::Workshop, 4, 1 }, { TEXT("Officers' quarters"), EShipRoom::Cabin, 2, -1 } }, RoomDepth, true);
			RoomsAlong(R2, 0, { { TEXT("Main computer memory bank"), EShipRoom::Equipment, 3, 4 }, { TEXT("Officers' quarters"), EShipRoom::Cabin, 2, -1 } }, RoomDepth, true);
			for (int32 s = R2 + 1; s < P.Stretches.Num(); ++s) { RoomsAlong(s, 0, { { TEXT("Officers' quarters"), EShipRoom::Cabin, 2, -1 } }); RoomsAlong(s, 1, { { TEXT("Officers' quarters"), EShipRoom::Cabin, 2, -1 } }); }
		}

		void DeckD()
		{
			const int32 D = P.DeckIndex('D', EDeckSection::Saucer);
			const int32 L = Lobby(D, {});
			RoomsAlong(L, 1, { { TEXT("Computer lab"), EShipRoom::Lab, 3, 1 }, { TEXT("High-energy physics lab"), EShipRoom::Lab, 3, 1 }, { TEXT("Senior officers' quarters"), EShipRoom::Cabin, 3, -1 } }, RoomDepth, true);
			RoomsAlong(L, 0, { { TEXT("Computer core"), EShipRoom::Equipment, 3, -1 } }, RoomDepth, true);
		}

		void DeckC()
		{
			const int32 D = P.DeckIndex('C', EDeckSection::Saucer);
			const int32 L = Lobby(D, {});
			RoomsAlong(L, 1, { { TEXT("Officers' observation lounge"), EShipRoom::Lounge, 4, 1 }, { TEXT("Wardroom"), EShipRoom::Office, 2, 2 }, { TEXT("Kitchen"), EShipRoom::Workshop, 3, 1 }, { TEXT("Officers' lounge"), EShipRoom::Lounge, 4, -1 } }, RoomDepth, true);
			RoomsAlong(L, 0, { { TEXT("Pantry"), EShipRoom::Store, 2, -1 } }, RoomDepth, true);
		}

		void DeckB()
		{
			// A straight along the bridge module, the main lift's door in its port wall (the car at the shaft's
			// place), the briefing room, the ready room and the exec's office to starboard; the airlock's staging
			// area is its own set across the way.
			const int32 D = P.DeckIndex('B', EDeckSection::Saucer);
			const double CarY = 0.0, Y = CarY + 140.0 * X + CorrW;
			const int32 S = Straight(D, TEXT("bridge module corridor"), FVector(6500.0 * X, Y, 0), FVector(9000.0 * X, Y, 0), true, true);
			Lift(S, BayNear(S, FVector(6744.0 * X, Y, 0)), 1, TEXT("Main"));
			RoomsAlong(S, 0, { { TEXT("Briefing room 1"), EShipRoom::Office, 4, 1 }, { TEXT("Captain's ready room"), EShipRoom::Office, 3, 1 }, { TEXT("Executive officer's office"), EShipRoom::Office, 3, 1 } }, 440.0 * X);
		}

		void DeckI()
		{
			const int32 D = P.DeckIndex('I', EDeckSection::Saucer);
			const int32 L = Lobby(D, {});
			RoomsAlong(L, 1, { { TEXT("Cargo transporter"), EShipRoom::TransporterRoom, 3, 2 }, { TEXT("Observation gallery"), EShipRoom::Lounge, 3, 3 }, { TEXT("Cargo hold"), EShipRoom::Hold, 2, -1 } }, RoomDepth, true);
			RoomsAlong(L, 0, { { TEXT("Lower sensor array"), EShipRoom::Equipment, 3, -1 } }, RoomDepth, true);
		}

		/** A fore-and-aft corridor on the centre line (or offset), from X1 aft to X0, with the dorsal shaft's door
		 *  in its starboard wall. Heading aft, side 0 is to port (-y) and side 1 to starboard (+y). */
		int32 Trunk(int32 Deck, const FString& Name, double X0, double X1, double Y, bool bDorsalLift, bool bEnd0 = true, bool bEnd1 = true)
		{
			const int32 S = Straight(Deck, Name, FVector(X1 * X, Y * X, 0), FVector(X0 * X, Y * X, 0), bEnd0, bEnd1);
			if (bDorsalLift) { Lift(S, BayNear(S, FVector(2309.0 * X, Y * X, 0)), 1, TEXT("Dorsal")); }
			return S;
		}

		void Dorsal()
		{
			struct FNeck { TCHAR L; TArray<FRoomSpec> Stbd, Port; };
			const FNeck Necks[] = {
				{ 'I', { { TEXT("Observation lounge"), EShipRoom::Lounge, 3, 1 }, { TEXT("Engineering equipment"), EShipRoom::Equipment, 2, -1 } }, { { TEXT("Observation lounge"), EShipRoom::Lounge, 3, 1 }, { TEXT("Engineering equipment"), EShipRoom::Equipment, 2, -1 } } },
				{ 'J', { { TEXT("Engineering equipment"), EShipRoom::Equipment, 2, -1 } }, { { TEXT("Engineering equipment"), EShipRoom::Equipment, 2, -1 } } },
				{ 'K', { { TEXT("Observation lounge"), EShipRoom::Lounge, 3, 1 }, { TEXT("Torpedo casing storage"), EShipRoom::Store, 3, 1 }, { TEXT("Shield generator room"), EShipRoom::Equipment, 3, -1 } }, { { TEXT("Warhead locker"), EShipRoom::Armoury, 3, 1 }, { TEXT("Shield generator room"), EShipRoom::Equipment, 3, -1 } } },
				// Deck L has no corridor of its own: the torpedo bay (its set, two decks tall from M) fills the neck there.
			};
			for (const FNeck& N : Necks)
			{
				const int32 D = P.DeckIndex(N.L, EDeckSection::Dorsal);
				const int32 S = Trunk(D, TEXT("dorsal corridor"), 1300.0, 3100.0, 0.0, true);
				RoomsAlong(S, 0, N.Stbd, 430.0 * X); RoomsAlong(S, 1, N.Port, 430.0 * X);
			}
		}

		/** A trunk that jogs round a set on the centre line: the forward leg ends at `XF`, a cross passage runs
		 *  to starboard from its last bay, the bypass heads aft at `Yb`, a second cross passage comes back to the
		 *  aft leg, which resumes at `XA`. Every junction is a T on a bay's middle: the cross passages leave the
		 *  trunks' last and first bays through their starboard walls (side 1 heading aft) and meet the bypass's
		 *  first and last bays through their inboard walls (side 0), the bypass laid out so those bays' middles
		 *  lie on the cross passages' axes. */
		void Bypass(int32 Deck, double X1, double XF, double XA, double X0, double Yb, const TArray<FRoomSpec>& Stbd, const TArray<FRoomSpec>& Port)
		{
			const double W = CorrW;
			const int32 Fwd = Trunk(Deck, TEXT("forward trunk"), XF, X1, 0.0, true, true, true);
			const int32 Aft = Trunk(Deck, TEXT("aft trunk"), X0, XA, 0.0, false, true, true);
			const int32 Cross1 = P.Stretches.Num(), Byp = Cross1 + 1, Cross2 = Cross1 + 2;
			const FVector C1 = Open(Fwd, P.Stretches[Fwd].Bays.Num() - 1, 1, Cross1);   // on the starboard wall, abreast of the last bay's middle
			const FVector A0 = Open(Aft, 0, 1, Cross2);                                 // the same at the aft leg's first bay
			const double Yin = Yb * X - W;                                               // the bypass's inboard wall
			Straight(Deck, TEXT("cross passage"), C1, FVector(C1.X, Yin, 0), false, false);
			P.Stretches[Cross1].Links.AddUnique(Byp);
			// The bypass's pitch is chosen so a bay's middle falls on each cross passage's axis: N bays over the
			// span between the axes plus one, its ends half a bay beyond them, closed by end walls at the corners.
			const double Span = C1.X - A0.X;
			const int32 N = FMath::Max(2, FMath::RoundToInt(Span / CorrPitch) + 1);
			const double Pitch = Span / (N - 1);
			Straight(Deck, TEXT("bypass"), FVector(C1.X + Pitch / 2, Yb * X, 0), FVector(A0.X - Pitch / 2, Yb * X, 0), true, true);
			Open(Byp, 0, 0, Cross1);
			Open(Byp, P.Stretches[Byp].Bays.Num() - 1, 0, Cross2);
			Straight(Deck, TEXT("cross passage"), A0, FVector(A0.X, Yin, 0), false, false);
			P.Stretches[Cross2].Links.AddUnique(Byp);
			RoomsAlong(Fwd, 1, Stbd); RoomsAlong(Fwd, 0, Port);
			RoomsAlong(Byp, 1, Stbd);   // outboard of the bypass; inboard is the set it goes round
			RoomsAlong(Aft, 1, Stbd); RoomsAlong(Aft, 0, Port);
		}

		void Secondary()
		{
			const TArray<FRoomSpec> Eng = { { TEXT("Engineering systems room"), EShipRoom::Equipment, 3, -1 } };
			// N: main engineering's set stands two decks tall from O, so the trunk jogs round its upper half here too.
			Bypass(P.DeckIndex('N', EDeckSection::Secondary), 2600.0, 500.0, -2100.0, -4800.0, 800.0,
				   { { TEXT("Engineering systems room"), EShipRoom::Equipment, 3, 3 }, { TEXT("Engineering workshop"), EShipRoom::Workshop, 3, -1 } },
				   Eng);
			// O: the trunk jogs to starboard round main engineering (its own set, its lift door the trunk's end).
			Bypass(P.DeckIndex('O', EDeckSection::Secondary), 3400.0, 500.0, -2100.0, -6100.0, 800.0,
				   { { TEXT("Flight control"), EShipRoom::Office, 4, 1 }, { TEXT("Dilithium chamber access"), EShipRoom::Equipment, 3, 1 }, { TEXT("Nacelle energy conduit room"), EShipRoom::Equipment, 3, -1 } },
				   { { TEXT("Nacelle energy conduit room"), EShipRoom::Equipment, 3, -1 } });
			// P.
			{
				// The hangar's set rises from Q through P: the trunk ends at its forward wall, the gallery looking over it.
				const int32 D = P.DeckIndex('P', EDeckSection::Secondary);
				const int32 S = Trunk(D, TEXT("trunk corridor"), -3500.0, 3600.0, 0.0, true);
				RoomsAlong(S, 0, { { TEXT("Maneuvering thruster control"), EShipRoom::Equipment, 3, 1 }, { TEXT("Briefing room 2"), EShipRoom::Office, 4, 1 }, { TEXT("Engineering office"), EShipRoom::Office, 2, 4 }, { TEXT("Engineer's lounge"), EShipRoom::Lounge, 3, 2 }, { TEXT("Hangar control gallery"), EShipRoom::Office, 4, -1 } });
				RoomsAlong(S, 1, { { TEXT("Nanotech workshop"), EShipRoom::Workshop, 3, 1 }, { TEXT("Diagnostic workshop"), EShipRoom::Workshop, 3, 1 }, { TEXT("Shipfitting workshop"), EShipRoom::Workshop, 4, 1 }, { TEXT("Electronic workshop"), EShipRoom::Workshop, 3, 1 }, { TEXT("Engineer's dining mess"), EShipRoom::Lounge, 4, 1 }, { TEXT("Engineering store"), EShipRoom::Store, 2, -1 } });
			}
			// Q: ends short of the cargo bay's set, whose hold rises from S through Q's floor (the hangar beyond
			// it is reached by its own set's lift door).
			{
				const int32 D = P.DeckIndex('Q', EDeckSection::Secondary);
				const int32 S = Trunk(D, TEXT("trunk corridor"), -1350.0, 3600.0, 0.0, true);
				RoomsAlong(S, 0, { { TEXT("Ship's power distribution"), EShipRoom::Equipment, 3, 2 }, { TEXT("Docking port 4"), EShipRoom::Hatch, 2, 1 }, { TEXT("Cargo high-bay"), EShipRoom::Hold, 4, -1 } });
				RoomsAlong(S, 1, { { TEXT("Damage control locker"), EShipRoom::Store, 2, 1 }, { TEXT("Docking port 5"), EShipRoom::Hatch, 2, 1 }, { TEXT("Cargo high-bay"), EShipRoom::Hold, 4, -1 } });
			}
			// R: the trunk jogs round the cargo bay's set (its hold is two decks tall from S).
			Bypass(P.DeckIndex('R', EDeckSection::Secondary), 3400.0, -900.0, -3700.0, -6000.0, 800.0,
				   { { TEXT("Deflector beam emitter room"), EShipRoom::Equipment, 4, 1 }, { TEXT("Environmental control"), EShipRoom::Equipment, 3, 2 }, { TEXT("Parking bay 1"), EShipRoom::Hold, 5, 1 }, { TEXT("Cargo mid-bay"), EShipRoom::Hold, 4, -1 } },
				   { { TEXT("Observation lounge"), EShipRoom::Lounge, 3, 2 }, { TEXT("Travel pod parking"), EShipRoom::Hold, 4, 1 }, { TEXT("Cargo mid-bay"), EShipRoom::Hold, 4, -1 } });
			// S: the trunk jogs round the cargo bay (its set).
			Bypass(P.DeckIndex('S', EDeckSection::Secondary), 3600.0, -900.0, -3700.0, -4400.0, 800.0,
				   { { TEXT("Fusion reactor control"), EShipRoom::Equipment, 3, 1 }, { TEXT("Secondary hull bridge"), EShipRoom::Office, 4, 1 }, { TEXT("Cargo transporter"), EShipRoom::TransporterRoom, 3, 2 }, { TEXT("Parking bay 2"), EShipRoom::Hold, 4, -1 } },
				   { { TEXT("Observation lounge"), EShipRoom::Lounge, 3, 1 }, { TEXT("Cargo transporter"), EShipRoom::TransporterRoom, 3, 1 }, { TEXT("Parking bay 2"), EShipRoom::Hold, 4, -1 } });
			// T.
			{
				const int32 D = P.DeckIndex('T', EDeckSection::Secondary);
				const int32 S = Trunk(D, TEXT("trunk corridor"), -3700.0, 3400.0, 0.0, true);
				RoomsAlong(S, 0, { { TEXT("Reactor control"), EShipRoom::Equipment, 3, 1 }, { TEXT("Botanical garden"), EShipRoom::Lounge, 6, 1 }, { TEXT("Botanical lab"), EShipRoom::Lab, 3, 1 }, { TEXT("Observation lounge"), EShipRoom::Lounge, 3, -1 } });
				RoomsAlong(S, 1, { { TEXT("Swimming pool"), EShipRoom::Lounge, 6, 1 }, { TEXT("Jacuzzi pool"), EShipRoom::Lounge, 2, 1 }, { TEXT("Pool deck"), EShipRoom::Lounge, 3, 1 }, { TEXT("Personnel transporter room"), EShipRoom::TransporterRoom, 3, -1 } });
			}
			// U.
			{
				const int32 D = P.DeckIndex('U', EDeckSection::Secondary);
				const int32 S = Trunk(D, TEXT("trunk corridor"), -3500.0, 2800.0, 0.0, true);
				RoomsAlong(S, 0, { { TEXT("Reactor"), EShipRoom::Equipment, 4, 1 }, { TEXT("Antimatter bottles"), EShipRoom::Equipment, 3, -1 } });
				RoomsAlong(S, 1, { { TEXT("Intermix shaft foot"), EShipRoom::Equipment, 3, 1 }, { TEXT("Matter reserve tanks"), EShipRoom::Equipment, 3, 2 }, { TEXT("Bussard collector control"), EShipRoom::Equipment, 2, -1 } });
			}
		}

		void Finish()
		{
			for (int32 s = 0; s < P.Stretches.Num(); ++s)
			{
				for (int32 L : P.Stretches[s].Links) { if (P.Stretches.IsValidIndex(L)) { P.Stretches[L].Links.AddUnique(s); } }
			}
			for (FPlanStretch& S : P.Stretches)
			{
				FBox B(ForceInit);
				for (const FPlanBay& Bay : S.Bays) { B += Bay.P0; B += Bay.P1; }
				S.Bounds = B.ExpandBy(FVector(CorrW, CorrW, 0.0)) + FVector(B.Min.X, B.Min.Y, B.Min.Z + CorrH);
			}
		}
	};
}

void SlonkPlan::Build(FShipPlan& P, double X, const TArray<FPlanReserved>& Reserved)
{
	P.Stretches.Reset(); P.Rooms.Reset(); P.Stops.Reset();
	P.Scale = X; P.SaucerX = 8000.0 * X;
	DeckTable(P.Decks, X);
	FBuilder B(P, X, Reserved);
	B.PlateDeck('F', false);
	B.PlateDeck('G', true);
	B.PlateDeck('H', false);
	B.DeckE(); B.DeckD(); B.DeckC(); B.DeckB(); B.DeckI();
	B.Dorsal(); B.Secondary();
	B.Finish();
}
