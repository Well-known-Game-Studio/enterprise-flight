// Slonk - automation tests for the cover the cloud layer keeps over a place (PlanetWeather.h: read from a world's
// weather map as M_SlonkVolumetricCloud lays its coverage out), and for what it does to the animals' dust and spray,
// which light themselves and so take no cloud's shadow from the engine. Under "Slonk.Weather."; tools/run_tests.sh runs
// them headless.

#include "Misc/AutomationTest.h"
#include "../FishSplash.h"
#include "../HerdDust.h"
#include "../PlanetWeather.h"

#if WITH_DEV_AUTOMATION_TESTS

#ifndef SLONK_TEST_FLAGS
#define SLONK_TEST_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#endif

namespace SlonkWeatherTestHelpers
{
	/** A small weather map, as a world's is made. */
	void WeatherMapForTest(TArray<FColor>& Out, int32 W, int32 H, const FVector& Seed)
	{
		FPlanetWeatherSpec Spec;
		Spec.Seed = Seed;
		Spec.Scale = 4.f;
		Spec.Fronts = 0.5f;
		SlonkWeather::Generate(Spec, W, H, Out);
	}
	/** The direction the generator gives a texel's middle (PlanetWeather.cpp's Generate). */
	FVector WeatherTexelDir(double Col, double Row, int32 W, int32 H)
	{
		const double Lat = (0.5 - (Row + 0.5) / H) * UE_DOUBLE_PI, Lon = ((Col + 0.5) / W - 0.5) * UE_DOUBLE_TWO_PI;
		return FVector(FMath::Cos(Lat) * FMath::Cos(Lon), FMath::Cos(Lat) * FMath::Sin(Lon), FMath::Sin(Lat));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkWeatherCoverTest, "Slonk.Weather.Cover", SLONK_TEST_FLAGS)
bool FSlonkWeatherCoverTest::RunTest(const FString& Parameters)
{
	// The cover over a place, as the cloud layer's material lays it out from the weather map: none in the clear air
	// between the masses, a deck inside a mass's cells; more of it with more of the map's masses and cells there, with
	// the style's coverage, and with its deck. Before the map is in, the style's own even sky: about half covered at an
	// Earth's coverage, clear on a nearly cloudless world, a deck on a Venus. Over a whole map the mean cover rises with
	// the style's coverage and is never all or nothing.
	using namespace SlonkWeatherTestHelpers;
	TestEqual(TEXT("clear air between the masses"), SlonkWeather::CoverOf(0.f, 0.f, 0.45f, 0.f, 1.f), 0.f);
	TestEqual(TEXT("a deck inside a mass's cells"), SlonkWeather::CoverOf(1.f, 1.f, 0.45f, 0.f, 1.f), 1.f);
	int32 Falls = 0;
	for (int32 i = 0; i <= 20; ++i)
	{
		for (int32 j = 0; j <= 20; ++j)
		{
			const float M = i / 20.f, C = j / 20.f;
			const float Here = SlonkWeather::CoverOf(M, C, 0.45f, 0.2f, 1.f);
			Falls += Here < 0.f || Here > 1.f ? 1 : 0;
			Falls += SlonkWeather::CoverOf(FMath::Min(M + 0.05f, 1.f), C, 0.45f, 0.2f, 1.f) < Here ? 1 : 0;
			Falls += SlonkWeather::CoverOf(M, FMath::Min(C + 0.05f, 1.f), 0.45f, 0.2f, 1.f) < Here ? 1 : 0;
			Falls += SlonkWeather::CoverOf(M, C, 0.55f, 0.2f, 1.f) < Here ? 1 : 0;
			Falls += SlonkWeather::CoverOf(M, C, 0.45f, 0.6f, 1.f) < Here ? 1 : 0;
		}
	}
	TestEqual(TEXT("never less with more masses, cells, coverage or deck, and always a share of the sky"), Falls, 0);
	const float Earth = SlonkWeather::CoverOf(0.5f, 0.5f, 0.45f, 0.f, 0.f), Bare = SlonkWeather::CoverOf(0.5f, 0.5f, 0.1f, 0.f, 0.f), Venus = SlonkWeather::CoverOf(0.5f, 0.5f, 0.95f, 1.f, 0.f);
	AddInfo(FString::Printf(TEXT("before the map is in: %.2f of the sky at an Earth's coverage, %.2f on a nearly cloudless world, %.2f on a Venus"), Earth, Bare, Venus));
	TestTrue(TEXT("the style's own even sky before the map is in"), Earth > 0.3f && Earth < 0.7f && Bare == 0.f && Venus == 1.f);
	TestEqual(TEXT("whatever the map holds there, till it is in"), SlonkWeather::CoverOf(1.f, 0.f, 0.45f, 0.f, 0.f), Earth);

	const int32 W = 256, H = 128;
	TArray<FColor> Map;
	WeatherMapForTest(Map, W, H, FVector(3.1, 7.7, 1.3));
	double Means[3] = {};
	const float Coverages[3] = { 0.2f, 0.45f, 0.7f };
	for (int32 k = 0; k < 3; ++k)
	{
		double Sum = 0.0, Weight = 0.0;
		for (int32 Row = 0; Row < H; ++Row)
		{
			// Each row weighed by the sphere it covers.
			const double Wt = FMath::Cos((0.5 - (Row + 0.5) / H) * UE_DOUBLE_PI);
			for (int32 Col = 0; Col < W; ++Col)
			{
				const FColor& Px = Map[Row * W + Col];
				Sum += Wt * SlonkWeather::CoverOf(Px.R / 255.f, Px.G / 255.f, Coverages[k], 0.f, 1.f);
				Weight += Wt;
			}
		}
		Means[k] = Sum / Weight;
	}
	AddInfo(FString::Printf(TEXT("the mean cover over a map: %.2f at a coverage of 0.2, %.2f at 0.45, %.2f at 0.7"), Means[0], Means[1], Means[2]));
	TestTrue(TEXT("over a whole map the mean cover rises with the style's coverage, and is never all or nothing"),
		Means[0] > 0.01 && Means[0] < Means[1] && Means[1] < Means[2] && Means[2] < 0.99 && Means[1] > 0.1 && Means[1] < 0.75);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkWeatherSampleTest, "Slonk.Weather.Sample", SLONK_TEST_FLAGS)
bool FSlonkWeatherSampleTest::RunTest(const FString& Parameters)
{
	// The map read on the CPU as the materials' sampler reads it: at a texel's middle, in the direction the generator
	// made that texel for, the texel itself; half way to the next, the mean of the two; round the seam at 180 degrees
	// of longitude without a break; and not at all from a map of the wrong size.
	using namespace SlonkWeatherTestHelpers;
	const int32 W = 256, H = 128;
	TArray<FColor> Map;
	WeatherMapForTest(Map, W, H, FVector(5.5, 2.2, 8.8));
	double Worst = 0.0, WorstBetween = 0.0;
	int32 Read = 0;
	for (int32 i = 0; i < 400; ++i)
	{
		// Texels all over the map, the seam's columns and the polar rows among them.
		const int32 Col = i % 8 == 0 ? (i % 16 == 0 ? 0 : W - 1) : (i * 37) % W, Row = i % 10 == 0 ? (i % 20 == 0 ? 0 : H - 1) : (i * 53) % H;
		float M = 0.f, C = 0.f;
		if (!SlonkWeather::SampleAt(Map, W, H, WeatherTexelDir(Col, Row, W, H), M, C)) { continue; }
		++Read;
		const FColor& Px = Map[Row * W + Col];
		Worst = FMath::Max3(Worst, (double)FMath::Abs(M - Px.R / 255.f), (double)FMath::Abs(C - Px.G / 255.f));
		// Half way to the next texel east, across the seam for the last column.
		const FColor& East = Map[Row * W + (Col + 1) % W];
		if (SlonkWeather::SampleAt(Map, W, H, WeatherTexelDir(Col + 0.5, Row, W, H), M, C))
		{
			WorstBetween = FMath::Max3(WorstBetween, (double)FMath::Abs(M - 0.5f * (Px.R + East.R) / 255.f), (double)FMath::Abs(C - 0.5f * (Px.G + East.G) / 255.f));
		}
	}
	AddInfo(FString::Printf(TEXT("%d texels read at their middles: off by %.5f at most; half way to the next east: off the two's mean by %.5f at most"), Read, Worst, WorstBetween));
	TestEqual(TEXT("every one read"), Read, 400);
	TestTrue(TEXT("a texel's middle reads as the texel"), Worst < 0.002);
	TestTrue(TEXT("half way to the next reads as the mean of the two, round the seam too"), WorstBetween < 0.004);
	float M = 0.f, C = 0.f;
	TArray<FColor> Short;
	Short.SetNum(W * H - 1);
	TestFalse(TEXT("no reading from a map of another size, or none"), SlonkWeather::SampleAt(Short, W, H, FVector::XAxisVector, M, C) || SlonkWeather::SampleAt(TArray<FColor>(), 0, 0, FVector::XAxisVector, M, C));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkWeatherShadeTest, "Slonk.Weather.Shade", SLONK_TEST_FLAGS)
bool FSlonkWeatherShadeTest::RunTest(const FString& Parameters)
{
	// The shade over a place is read where the sun's light crosses the layer on its way down to it: straight overhead
	// with the sun at the zenith, away toward the sun the lower it stands (a layer 5 km up is crossed 5 km off with the
	// sun 45 degrees up), and the place itself for one above the layer or with no sun given. Only a deck shades all the
	// ground under it: a broken sky, where the sun comes and goes between the clouds, and a veil over four fifths of it,
	// which the sun still shines through, shade nothing as a whole (which cloud's shadow falls where is the renderer's
	// to know); a sky cloud all over but a thirtieth shades all of it, and in between more the more of it is cloud.
	const double R = 6.371e8, Layer = R + 5.0e5;
	const FVector Place(0.0, 0.0, R);
	double Up = 0.0;
	const FVector Over = SlonkWeather::SunThroughLayer(Place, FVector::ZAxisVector, Layer, Up);
	TestTrue(TEXT("straight overhead with the sun at the zenith"), Over.Equals(FVector(0.0, 0.0, Layer), 1.0) && FMath::IsNearlyEqual(Up, 1.0, 1e-9));
	const FVector Slant = SlonkWeather::SunThroughLayer(Place, FVector(1.0, 0.0, 1.0), Layer, Up);
	AddInfo(FString::Printf(TEXT("the sun 45 degrees up: its light crosses a layer 5 km up %.2f km toward it, standing %.3f high there"), Slant.X / 1.0e5, Up));
	TestTrue(TEXT("5 km toward a sun 45 degrees up, on the layer"), FMath::IsNearlyEqual(Slant.X, 5.0e5, 1.0e3) && FMath::IsNearlyEqual(Slant.Size(), Layer, 1.0) && Up > 0.70 && Up < 0.71);
	const FVector Above = SlonkWeather::SunThroughLayer(FVector(0.0, 0.0, Layer + 1.0e5), FVector(1.0, 0.0, 1.0), Layer, Up);
	TestTrue(TEXT("the place itself for one above the layer"), Above.Equals(FVector(0.0, 0.0, Layer + 1.0e5), 1e-6));
	TestTrue(TEXT("and with no sun given, the sun taken as overhead"), SlonkWeather::SunThroughLayer(Place, FVector::ZeroVector, Layer, Up).Equals(Place, 1e-6) && Up == 1.0);
	TestTrue(TEXT("no shade as a whole under a clear sky, a broken one or a veil"), SlonkWeather::DeckShade(0.f) == 0.f && SlonkWeather::DeckShade(0.54f) == 0.f && SlonkWeather::DeckShade(0.83f) == 0.f);
	TestTrue(TEXT("all of it under a deck"), SlonkWeather::DeckShade(0.97f) == 1.f && SlonkWeather::DeckShade(1.f) == 1.f && SlonkWeather::DeckShade(0.96f) > 0.9f);
	TestTrue(TEXT("and between them more the more of the sky is cloud"), SlonkWeather::DeckShade(0.9f) > 0.f && SlonkWeather::DeckShade(0.9f) < SlonkWeather::DeckShade(0.94f)
		&& SlonkWeather::DeckShade(0.94f) < 1.f && FMath::IsNearlyEqual(SlonkWeather::DeckShade(0.925f), 0.5f, 1e-3f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkWeatherSelfLitTest, "Slonk.Weather.SelfLit", SLONK_TEST_FLAGS)
bool FSlonkWeatherSelfLitTest::RunTest(const FString& Parameters)
{
	// The herds' dust and the leapers' spray light themselves, so no cloud's shadow falls on them: under cloud their
	// colour is dimmed by the sun's share of their light, as much of it as the cloud keeps. Under a clear sky they are
	// exactly as they were; under a deck the dust keeps a third of its light and the spray a little less; their opacity,
	// and everything else about them, is the same under any sky.
	SlonkFauna::FHerdDustInput In;
	In.SpeedMS = 3.4;
	In.Members = 20;
	In.Wet = 0.2;
	In.RadiusCm = 2000.0;
	In.Color = FLinearColor(0.62f, 0.52f, 0.38f, 0.45f);
	const SlonkFauna::FHerdDust Clear = SlonkFauna::HerdDustFor(In);
	In.Shade = 1.0;
	const SlonkFauna::FHerdDust Deck = SlonkFauna::HerdDustFor(In);
	In.Shade = 0.5;
	const SlonkFauna::FHerdDust Half = SlonkFauna::HerdDustFor(In);
	In.Shade = 7.0;
	const SlonkFauna::FHerdDust Over = SlonkFauna::HerdDustFor(In);
	AddInfo(FString::Printf(TEXT("the dust's red: %.3f under a clear sky, %.3f under half a sky of cloud, %.3f under a deck"), Clear.Color.R, Half.Color.R, Deck.Color.R));
	TestTrue(TEXT("the dust exactly as given under a clear sky"), Clear.Color == FLinearColor(0.62f, 0.52f, 0.38f, 0.45f) && Clear.Rate > 0.f);
	TestTrue(TEXT("a third of its light left under a deck, each channel alike"), FMath::IsNearlyEqual(Deck.Color.R, 0.62f * 0.32f, 1e-4f) && FMath::IsNearlyEqual(Deck.Color.G, 0.52f * 0.32f, 1e-4f)
		&& FMath::IsNearlyEqual(Deck.Color.B, 0.38f * 0.32f, 1e-4f));
	TestTrue(TEXT("half way between under half a sky of cloud, and no darker than a deck's however much shade is asked"),
		FMath::IsNearlyEqual(Half.Color.R, 0.5f * (Clear.Color.R + Deck.Color.R), 1e-4f) && Over.Color == Deck.Color);
	TestTrue(TEXT("its opacity, rate, size and reach the same under any sky"), Deck.Color.A == Clear.Color.A && Deck.Rate == Clear.Rate && Deck.Size == Clear.Size && Deck.Radius == Clear.Radius);

	SlonkFauna::FFishSplashInput Sp;
	Sp.SizeCm = 80.0;
	const SlonkFauna::FFishSplash Spray = SlonkFauna::FishSplashFor(Sp);
	Sp.Shade = 1.0;
	const SlonkFauna::FFishSplash Dim = SlonkFauna::FishSplashFor(Sp);
	TestTrue(TEXT("the spray the foam's own colour under a clear sky"), Spray.Color == SlonkFauna::FoamColor(Sp.Shallow, Sp.Ocean));
	TestTrue(TEXT("under a deck a little over a quarter of its light, its opacity and size the same"), FMath::IsNearlyEqual(Dim.Color.R, Spray.Color.R * 0.28f, 1e-4f)
		&& FMath::IsNearlyEqual(Dim.Color.B, Spray.Color.B * 0.28f, 1e-4f) && Dim.Color.A == Spray.Color.A && Dim.Size == Spray.Size);
	return true;
}

#endif
