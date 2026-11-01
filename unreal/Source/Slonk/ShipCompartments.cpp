#include "ShipCompartments.h"

void SlonkCompartments::BuildRefit(TArray<FCompartment>& Out, float L, const FVector& Tube, const FVector& Bay, const FVector& /*Bridge*/)
{
	Out.Reset();
	// The saucer sits about the bridge, the neck under the torpedo housing, the secondary hull from the deflector back to
	// the fantail. Everything is in fractions of the length so another hull of the same lines scales with it.
	const float X = L / 30500.f;		// 1 at the refit's 305 m
	auto Add = [&](const TCHAR* Name, const TCHAR* Deck, EShipSystem Sys, EStation St, FVector C, FVector E, float Crew, bool bBridge = false)
	{
		FCompartment K; K.Name = Name; K.Deck = Deck; K.System = Sys; K.Station = St; K.Center = C; K.Extent = E; K.CrewShare = Crew; K.bBridge = bBridge;
		Out.Add(K);
	};
	// Measured off the hull mesh (ship hullprofile): saucer centred at x 8000, z 400..3540, radius 7130; neck x 1100..4200;
	// secondary hull x -7600..4600, z -3590..-900; nacelles x -15250..-8000, z 1700..3500 at y +-4800; all for L = 30500.
	// Saucer, top down.
	Add(TEXT("Main bridge"), TEXT("A"), EShipSystem::Count, EStation::Count, FVector(8000.f, 0.f, 3230.f) * X, FVector(620.f, 620.f, 300.f) * X, 0.03f, true);
	Add(TEXT("Officers' quarters"), TEXT("B-D"), EShipSystem::Count, EStation::Count, FVector(9900.f, 0.f, 2520.f) * X, FVector(1500.f, 2500.f, 380.f) * X, 0.12f);
	Add(TEXT("Computer core"), TEXT("D-E"), EShipSystem::Sensors, EStation::Science, FVector(7300.f, 0.f, 2520.f) * X, FVector(800.f, 800.f, 380.f) * X, 0.03f);
	Add(TEXT("Crew quarters"), TEXT("F-G"), EShipSystem::Count, EStation::Count, FVector(10800.f, 0.f, 1900.f) * X, FVector(3600.f, 5500.f, 300.f) * X, 0.30f);
	Add(TEXT("Security and armoury"), TEXT("G"), EShipSystem::Count, EStation::Security, FVector(13700.f, 0.f, 1800.f) * X, FVector(900.f, 1200.f, 220.f) * X, 0.03f);
	Add(TEXT("Sickbay"), TEXT("G"), EShipSystem::Count, EStation::Medical, FVector(6600.f, 0.f, 1900.f) * X, FVector(1500.f, 1800.f, 300.f) * X, 0.05f);
	Add(TEXT("Impulse engineering"), TEXT("G-H"), EShipSystem::Impulse, EStation::Count, FVector(5500.f, 0.f, 1750.f) * X, FVector(900.f, 1100.f, 500.f) * X, 0.06f);
	Add(TEXT("Phaser control"), TEXT("H"), EShipSystem::Phasers, EStation::Count, FVector(8300.f, 0.f, 1350.f) * X, FVector(1300.f, 1300.f, 280.f) * X, 0.03f);
	Add(TEXT("Sensor bay"), TEXT("J"), EShipSystem::Sensors, EStation::Count, FVector(8000.f, 0.f, 720.f) * X, FVector(800.f, 800.f, 330.f) * X, 0.02f);
	// Neck.
	Add(TEXT("Shield generators"), TEXT("Dorsal I-K"), EShipSystem::Shields, EStation::Count, FVector(2700.f, 0.f, 600.f) * X, FVector(900.f, 500.f, 450.f) * X, 0.03f);
	Add(TEXT("Torpedo bay"), TEXT("Dorsal L-M"), EShipSystem::Torpedoes, EStation::Count, FVector(Tube.X - 300.f * X, 0.f, Tube.Z), FVector(1100.f, 650.f, 450.f) * X, 0.04f);
	// Secondary hull, fore to aft.
	Add(TEXT("Deflector"), TEXT("R"), EShipSystem::Sensors, EStation::Count, FVector(3900.f, 0.f, -2200.f) * X, FVector(700.f, 1100.f, 800.f) * X, 0.02f);
	Add(TEXT("Environmental control"), TEXT("R"), EShipSystem::LifeSupport, EStation::Count, FVector(1900.f, 0.f, -2700.f) * X, FVector(1000.f, 900.f, 500.f) * X, 0.04f);
	Add(TEXT("Main engineering"), TEXT("O-P"), EShipSystem::Warp, EStation::Engineering, FVector(-900.f, 0.f, -1900.f) * X, FVector(2000.f, 1200.f, 800.f) * X, 0.08f);
	Add(TEXT("Reactor"), TEXT("U"), EShipSystem::Warp, EStation::Count, FVector(-500.f, 0.f, -3100.f) * X, FVector(1300.f, 900.f, 380.f) * X, 0.03f);
	Add(TEXT("Cargo bays"), TEXT("S-T"), EShipSystem::Count, EStation::Count, FVector(-4200.f, 0.f, -2600.f) * X, FVector(1400.f, 1100.f, 700.f) * X, 0.04f);
	Add(TEXT("Shuttle bay"), TEXT("Q-R"), EShipSystem::Count, EStation::Count, FVector(Bay.X + 1300.f * X, 0.f, Bay.Z), FVector(1500.f, 1100.f, 500.f) * X, 0.03f);
	// The nacelles: warp coils, almost nobody aboard.
	Add(TEXT("Port nacelle"), TEXT("-"), EShipSystem::Warp, EStation::Count, FVector(-11600.f, -4800.f, 2600.f) * X, FVector(3600.f, 500.f, 800.f) * X, 0.005f);
	Add(TEXT("Starboard nacelle"), TEXT("-"), EShipSystem::Warp, EStation::Count, FVector(-11600.f, 4800.f, 2600.f) * X, FVector(3600.f, 500.f, 800.f) * X, 0.005f);
}

int32 SlonkCompartments::Nearest(const TArray<FCompartment>& C, const FVector& P)
{
	int32 Best = INDEX_NONE; double BestD = TNumericLimits<double>::Max();
	for (int32 i = 0; i < C.Num(); ++i)
	{
		const double D = C[i].Box().ComputeSquaredDistanceToPoint(P);
		if (D < BestD) { BestD = D; Best = i; }
	}
	return Best;
}

int32 SlonkCompartments::Hit(const TArray<FCompartment>& C, const FVector& P, const FVector& DirIn, float L)
{
	// The point itself first: the smallest compartment it lies in.
	int32 In = INDEX_NONE; double InArea = TNumericLimits<double>::Max();
	for (int32 i = 0; i < C.Num(); ++i)
	{
		if (C[i].Contains(P)) { const double A = C[i].Extent.X * C[i].Extent.Y * C[i].Extent.Z; if (A < InArea) { InArea = A; In = i; } }
	}
	if (In != INDEX_NONE) { return In; }
	const FVector Dir = DirIn.GetSafeNormal();
	if (Dir.IsNearlyZero()) { return Nearest(C, P); }
	// Else carry the shot on from just outside the skin and take the first box it enters.
	const FVector Start = P - Dir * (L * 0.05f);
	int32 Best = INDEX_NONE; double BestT = TNumericLimits<double>::Max();
	for (int32 i = 0; i < C.Num(); ++i)
	{
		double T0 = 0.0, T1 = 2.0 * L;
		bool bMiss = false;
		for (int32 a = 0; a < 3 && !bMiss; ++a)
		{
			const double Lo = C[i].Center[a] - C[i].Extent[a], Hi = C[i].Center[a] + C[i].Extent[a];
			if (FMath::Abs(Dir[a]) < 1e-6) { if (Start[a] < Lo || Start[a] > Hi) { bMiss = true; } continue; }
			double Ta = (Lo - Start[a]) / Dir[a], Tb = (Hi - Start[a]) / Dir[a];
			if (Ta > Tb) { Swap(Ta, Tb); }
			T0 = FMath::Max(T0, Ta); T1 = FMath::Min(T1, Tb);
			if (T0 > T1) { bMiss = true; }
		}
		if (!bMiss && T0 < BestT) { BestT = T0; Best = i; }
	}
	return Best != INDEX_NONE ? Best : Nearest(C, P);
}
