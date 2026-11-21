// Slonk - where a world's animals are: pure functions of the planet and the time, safe off the game thread, so the
// game finds the same animal wherever and whenever it looks. For now the shoals of the sea, mirrored from the terrain
// material's own lattice (terrain_material.py, its water block), so whatever leaps from the sea leaps out of a shoal
// that is drawn there; the habitats of the fliers and the herds follow.

#pragma once

#include "CoreMinimal.h"

namespace SlonkFauna
{
	/** The terrain material's shoals: at most one in each cell of a lattice laid on the sea's cube-face projection (the
	 *  plane across the largest axis of the planet-local direction, as the material lays its waves), a whole number of
	 *  cells to the noise period along each axis (about 200 m a cell), so the lattice wraps with the chunk origins. A
	 *  shoal swims a loop 16 to 30 m round a point near the middle of its cell, at 0.6 to 1.5 m/s, and never leaves it. */
	constexpr int32 ShoalCellsPerPeriod = 52;

	struct FShoal
	{
		/** The projection's plane (0 across x, 1 across y, 2 across z) and the cell on the periodic lattice: an id. */
		int32 Axis = 0;
		FIntPoint Key = FIntPoint::ZeroValue;
		/** Its middle at the time asked, planet-local cm, on the sea-level sphere, and the way it swims there (unit,
		 *  along the sea). */
		FVector CentreCm = FVector::ZeroVector;
		FVector Heading = FVector::XAxisVector;
		/** Across it as the material lays it (unit, along the sea): square to Heading on the plane it is drawn on, and so
		 *  a little off square on the sea toward the corners of the cube's faces. */
		FVector Side = FVector::YAxisVector;
		/** Half its length along Heading and half its width along Side, cm (its painted shape's edge, which the material
		 *  frays a tenth either way), and how deep it swims, 3 to 40 m: the material shows it only where the sea is
		 *  deeper than that. */
		double HalfLengthCm = 0.0, HalfWidthCm = 0.0, DepthCm = 0.0;
	};

	/** The shoal of the cell holding a planet-local point (on the sea-level sphere of RadiusCm, or near it) at a time
	 *  (the terrain material's Time: the world's time, UWorld::GetTimeSeconds), exactly as the material hashes it, in
	 *  Density of the cells (its ShoalDensity, SlonkFauna::SeaLifeOf): false if the cell has none. */
	SLONK_API bool ShoalAt(const FVector& PlanetLocalCm, double RadiusCm, float Density, double TimeSec, FShoal& Out);
	/** Where a planet-local point near a shoal lies in its painted shape: 0 at its middle, 1 at its edge. */
	SLONK_API double ShoalShapeAt(const FShoal& S, const FVector& PlanetLocalCm);
}
