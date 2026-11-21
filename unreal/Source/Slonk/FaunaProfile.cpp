#include "FaunaProfile.h"

SlonkFauna::FFaunaLook SlonkFauna::LookOf(const FWorldGenome& G)
{
	FFaunaLook L;
	const float Turn = G.SkyHueShiftDeg * G.Strangeness;
	if (Turn != 0.f)
	{
		// Round the colour wheel, its saturation and brightness kept.
		FLinearColor Hsv = L.Glow.LinearRGBToHSV();
		Hsv.R = FMath::Fmod(Hsv.R + Turn + 720.f, 360.f);
		L.Glow = Hsv.HSVToLinearRGB();
		L.Glow.A = 1.f;
	}
	return L;
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
