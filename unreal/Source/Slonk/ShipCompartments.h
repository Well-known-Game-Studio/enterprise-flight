// Slonk - the compartment map: where things are inside the ship, in pawn space, so a hit lands somewhere in particular
// and the master systems display has something to draw.
#pragma once

#include "CoreMinimal.h"
#include "ShipSystemsComponent.h"


namespace SlonkCompartments
{
	/** The Constitution refit's decks, placed from the hull's own anchors (length, tube centre, bay, bridge). */
	void BuildRefit(TArray<FCompartment>& Out, float LengthCm, const FVector& TubeCentre, const FVector& BayOffset, const FVector& BridgeOffset);
	/** The compartment a shot along LocalDir through LocalPoint strikes first (the ray is backed out of the hull first), else the nearest to the point. */
	int32 Hit(const TArray<FCompartment>& Compartments, const FVector& LocalPoint, const FVector& LocalDir, float LengthCm);
	int32 Nearest(const TArray<FCompartment>& Compartments, const FVector& LocalPoint);
}
