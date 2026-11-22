// Slonk - the splash a leaping fish throws up where it leaves the sea and where it dives back in (UPlanetFaunaComponent
// spawns it): a Niagara system, NS_FishSplash, spawned as a one-shot from a pool of components, at the moment and the
// place each leap crosses the sea's surface (SlonkFauna::LeapEventsBetween and LeapCrossing, the same closed-form
// schedule the material leaps the fish on), only near the camera, and only by day and at dusk: the splash is lit by the
// sun alone, so none is thrown once the night there is deeper than SplashNightMax (the sun far enough under the horizon
// that the sky atmosphere's planet shades it wholly, where it would draw black). What a splash is (how big, of what
// colour, carried how) is worked out here from the fish and its sea alone, and the components are lent out of a pool, so
// both are tested without the asset.
//
// The asset's contract (it is built separately, not by code): /Game/Slonk/FX/NS_FishSplash, a one-shot system: it bursts
// once and finishes by itself within SplashLifeSec, simulating on the CPU in world space, culled past about 1.5 km, at
// most a few hundred particles. It is spawned at a point on the sea's surface where a fish leaves or re-enters the
// water, its component's z axis the sea's up there. It draws a ring and crown of droplets thrown up and out under
// gravity (sprites), a short-lived ring of foam on the surface, and fine spray, lit as M_HerdDust lights itself (unlit,
// by the sky atmosphere's sun at each particle's own position: SkyAtmosphereLightIlluminance with the pixel's world
// position and SkyAtmosphereLightDirection, never the value on the ground, which on these worlds is a sunset for ever),
// so its foam reads white in the sun. Its user parameters, set by the game before each burst:
//     User.Size    float        cm: the fish's length; the splash scales with it (a 60 cm fish throws a crown about 1 to
//                               1.5 m high)
//     User.Color   LinearColor  the foam's colour, from the world's sea and its solvent; its alpha the opacity
//     User.Carry   Vector       world cm/s: the ground's own velocity through the world there, from the planet's spin,
//                               given to every droplet in full, its drag measured against it (as NS_HerdDust's User.Carry,
//                               SlonkFauna::GroundCarryCmS, but the plain velocity: set once for the whole burst, it is
//                               not scaled to a frame's tick as the dust's is)
//     User.Entry   float        0 a fish leaving the water, 1 one diving back in: an entry throws a taller, narrower crown
//     User.Night   float        0..SplashNightMax: how deep in the night it is there (dimmer through dusk; past
//                               SplashNightMax no splash is thrown at all, the sun too far under the horizon to light it)
// Without the asset there are no splashes and one line in the log says so; nothing else changes. slonk.Fauna.Splash 0
// switches them off.

#pragma once

#include "CoreMinimal.h"

class UNiagaraComponent;

namespace SlonkFauna
{
	/** The splash's Niagara system, its package, and the console variable that switches it off (slonk.Fauna.Splash). */
	inline const TCHAR* FishSplashPath = TEXT("/Game/Slonk/FX/NS_FishSplash.NS_FishSplash");
	inline const TCHAR* FishSplashPackage = TEXT("/Game/Slonk/FX/NS_FishSplash");
	SLONK_API bool SplashEnabled();
	/** Splashes are thrown within this of the camera, cm. */
	constexpr double SplashReachCm = 6.0e4;
	/** A splash's burst is over within this, s (the asset finishes by itself by then): its component rests till then, and
	 *  is lent again after. */
	constexpr double SplashLifeSec = 2.5;
	/** The most splash components ever made (a burst that finds none free is let go: about 30 fish leaping every 13 to 32 s
	 *  keep a dozen at most in the air). */
	constexpr int32 MaxSplashComponents = 24;
	/** A crossing further back than this is too old to splash for (a long hitch), s. */
	constexpr double SplashLateSec = 0.25;
	/** How deep in the night a group of leapers is (0 day, 1 night), from the sun's height over its horizon there (the dot
	 *  of the sun's direction with its up), as its material and its splashes are given it: from 0 with the sun 4.6 degrees
	 *  up to 1 with it 6.9 degrees down, to a hundredth. */
	SLONK_API float LeaperNight(double SunUp);
	/** No splash is thrown where the night is deeper than this: the sun about 1.5 degrees under the horizon there. It lights
	 *  the splash alone, through the sky atmosphere, whose planet (its ground 0.05% of the radius under the sea,
	 *  SlonkGameMode) shades a splash wholly once the sun is 1.8 degrees under (a night of 0.59), and the splash would draw
	 *  black. */
	constexpr float SplashNightMax = 0.55f;
	/** Whether a splash is thrown where the night is as deep as this. */
	inline bool SplashLitAt(float Night) { return Night <= SplashNightMax; }

	/** What a splash is worked out from: the fish's length (cm), whether it dives back in, how deep in the night it is there
	 *  (0 day, 1 night), the ground's own velocity through the world there (world cm/s: GroundCarryCmS), and the sea's
	 *  colours (its shallows' and its depths': its solvent's). */
	struct FFishSplashInput
	{
		double SizeCm = 60.0;
		bool bEntry = false;
		double Night = 0.0;
		FVector CarryWorldCmS = FVector::ZeroVector;
		FLinearColor Shallow = FLinearColor(0.1f, 0.35f, 0.5f);
		FLinearColor Ocean = FLinearColor(0.05f, 0.17f, 0.35f);
	};
	/** A splash as the system's user parameters take it (see the contract above). */
	struct FFishSplash
	{
		float Size = 0.f, Entry = 0.f, Night = 0.f;
		FLinearColor Color = FLinearColor::White;
		FVector Carry = FVector::ZeroVector;
	};
	SLONK_API FFishSplash FishSplashFor(const FFishSplashInput& In);
	/** The foam's colour on a sea: the terrain material's own foam (near white), a tenth of it the sea's shallows' hue (its
	 *  solvent's), as opaque as foam is. */
	SLONK_API FLinearColor FoamColor(const FLinearColor& Shallow, const FLinearColor& Ocean);

	/**
	 * The splashes' components, lent one to a burst: one lent rests SplashLifeSec (its burst finishing by itself) and is lent
	 * again after; a new one is made only while none has finished, up to MaxSplashComponents, past which a burst is let go.
	 * It keeps the components' pointers alone: whoever makes them keeps them from the garbage collector, and places and
	 * starts them.
	 */
	class SLONK_API FSplashPool
	{
	public:
		/** A component for a burst at a time (seconds): the one that has rested longest if its last burst is over, else a new
		 *  one from Make (null if Make gives none, or the pool is full). */
		UNiagaraComponent* Acquire(double Now, TFunctionRef<UNiagaraComponent*()> Make);
		/** Takes back the components whose bursts are over by a time. */
		void Tick(double Now);
		int32 NumLive() const { return Live.Num(); }
		int32 NumResting() const { return Resting.Num(); }
		int32 NumMade() const { return Made; }
		/** How many bursts have been thrown, and let go for want of a free component. */
		int32 NumBursts() const { return Bursts; }
		int32 NumDropped() const { return Dropped; }
		/** Forgets every component (their owner is going). */
		void Reset() { Live.Reset(); Resting.Reset(); Made = 0; }

	private:
		struct FLent { UNiagaraComponent* Component = nullptr; double Since = 0.0; };
		TArray<FLent> Live;
		TArray<UNiagaraComponent*> Resting;
		int32 Made = 0, Bursts = 0, Dropped = 0;
	};
}
