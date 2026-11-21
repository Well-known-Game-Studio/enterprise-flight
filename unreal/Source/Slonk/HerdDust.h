// Slonk - the dust a herd raises as it trots and gallops over dry ground (UPlanetFaunaComponent keeps it): a Niagara
// system, NS_HerdDust, one component of it on each herd going faster than a walk near the camera, carried with the herd's
// set while its emitter leaves the puffs behind in the world. What a herd's dust is (how many puffs, how big, of what
// colour, drifting how) is worked out here from the herd and its ground alone, and the components are lent out of a pool,
// so both are tested without the asset.
//
// The asset's contract (it is built in the editor, not by code): /Game/Slonk/FX/NS_HerdDust, its emitters simulating in
// world space, puffs rising off the ground about the component's origin, each living no more than DustPuffLifeSec; its
// six user parameters, set each frame by the game:
//     User.Rate    float        puffs a second over the whole herd (0: none; the puffs already out fade as they would)
//     User.Radius  float        cm: the herd's footprint, a disc this far either way of the component's origin in its xy
//     User.Color   LinearColor  the dust's colour, from the ground under the herd, paler than it; its alpha the opacity
//     User.Drift   Vector       world cm/s: the herd's velocity over the ground, in world axes, of which the asset gives
//                               each puff a random third (a third of it, varied puff to puff) at its birth
//     User.Size    float        cm: a puff's starting size, which the walkers' size scales
//     User.Carry   Vector       world cm/s: the ground's own velocity through the world at the herd, from the
//                               planet's spin (the world turns every frame unless slonk.Planet.HoldSpin holds it,
//                               carrying the herd, the ship and the camera with it at 150 m/s to 1.2 km/s): the asset
//                               gives each puff all of it at its birth, and its drag is measured against it (a puff's
//                               velocity less Carry is what drag slows), so a puff slows to rest on the ground the herd
//                               raised it from, not in the empty space the world has turned away from. Zero while the
//                               spin is held or the world does not turn; at the game clock's rate.
// Without the asset there is no dust and one line in the log says so; nothing else changes.

#pragma once

#include "CoreMinimal.h"

class UNiagaraComponent;
struct FScatterPalette;

namespace SlonkFauna
{
	/** The herds' dust's Niagara system, its package, and the console variable that switches it off (slonk.Fauna.Dust). */
	inline const TCHAR* HerdDustPath = TEXT("/Game/Slonk/FX/NS_HerdDust.NS_HerdDust");
	inline const TCHAR* HerdDustPackage = TEXT("/Game/Slonk/FX/NS_HerdDust");
	SLONK_API bool DustEnabled();
	/** A herd raises dust within this of the camera, cm (its dust let go a tenth further out), once its frame goes faster
	 *  over the ground than a brisk walk, m/s (its walkers' walk is 1 to 1.4 m/s). */
	constexpr double DustReachCm = 1.5e5;
	constexpr double DustWalkMS = 1.2;
	/** How a herd's speed is read for its dust: smoothed over DustSmoothSec (a single fast frame, a hitch or its ground
	 *  shifting under it, is no trot), its dust raised once that passes DustWalkMS by DustOnMS and kept at least
	 *  DustMinOnSec, let go once it falls back to DustWalkMS. */
	constexpr double DustSmoothSec = 0.5;
	constexpr double DustOnMS = 0.3;
	constexpr double DustMinOnSec = 3.0;
	/** Puffs a second for each walker for each m/s it goes faster than that, over dry ground in full life by day; and the
	 *  most a herd raises. */
	constexpr double DustPuffsPerWalker = 0.25;
	constexpr double DustMaxRate = 200.0;
	/** A puff's starting size, cm for each metre of the walkers' length, and the longest a puff lives, s: a component
	 *  given back rests that long before it is lent again, so its last puffs fade where they were left. */
	constexpr double DustPuffCmPerM = 45.0;
	constexpr double DustPuffLifeSec = 6.0;

	/** What a herd's dust is worked out from: how fast its frame goes over the ground (m/s, smoothed: FDustGate) and its
	 *  velocity there in the world's axes (cm/s), the ground's own velocity through the world there (cm/s:
	 *  GroundCarryCmS), how wet the ground under it is (0..1), how much lives on the world (0..1), how deep in the night it
	 *  is (0 day, 1 night), how far it is from the camera (cm), how many walkers it has and their length (m), its
	 *  footprint's radius (cm), and the dust's colour (HerdDustColor). */
	struct FHerdDustInput
	{
		double SpeedMS = 0.0;
		FVector VelWorldCmS = FVector::ZeroVector;
		FVector CarryWorldCmS = FVector::ZeroVector;
		double Wet = 0.5, Life = 1.0, Night = 0.0, DistCm = 0.0;
		int32 Members = 0;
		double SpanM = 2.0, RadiusCm = 0.0;
		FLinearColor Color = FLinearColor::White;
	};
	/** A herd's dust as the system's user parameters take it (see the contract above). */
	struct FHerdDust
	{
		float Rate = 0.f, Radius = 0.f, Size = 0.f;
		FLinearColor Color = FLinearColor::White;
		FVector Drift = FVector::ZeroVector;
		FVector Carry = FVector::ZeroVector;
	};
	/** The dust a herd raises: none while it grazes, stands or walks (a frame no faster than DustWalkMS), past
	 *  DustReachCm, at night or on a dead world; more the faster it goes past a walk, the more walkers it has and the
	 *  drier its ground, fewer as the light goes, at most DustMaxRate. */
	SLONK_API FHerdDust HerdDustFor(const FHerdDustInput& In);
	/** The dust's colour over ground of a wetness: the terrain's own dry ground (its palette's Land2), a little of its
	 *  living cover (Land) the wetter it is, paled as dust in the air is; its opacity less as the light goes. */
	SLONK_API FLinearColor HerdDustColor(const FScatterPalette& Pal, double Wet, double Night);
	/** The ground's own velocity through the world at a place on a turning world, world cm/s (User.Carry): the planet's
	 *  spin (SpinDegPerSec about its actor's up, as APlanetBody::Tick turns it, at the game clock's ClockScale) crossed
	 *  with the place less the planet's middle; zero while the spin is held, or for a world that does not turn. */
	SLONK_API FVector GroundCarryCmS(const FVector& PlanetUp, const FVector& PlanetCentre, double SpinDegPerSec, double ClockScale, bool bSpinHeld, const FVector& AtWorld);

	/** Whether a herd goes fast enough to raise dust, from its frame's speed frame by frame: the speed smoothed, whether
	 *  it is on and since when (the world's seconds). */
	struct FDustGate
	{
		double SpeedMS = 0.0;
		bool bOn = false;
		bool bPrimed = false;
		double OnAt = -1.0e9;
	};
	/** Steps the gate by a frame of Dt seconds at a time (the world's seconds) with the herd's speed then (m/s): true while
	 *  it raises dust; the smoothed speed, which the dust's rate is reckoned from, in Gate.SpeedMS. Its first frame takes
	 *  the speed as it is. */
	SLONK_API bool StepDustGate(FDustGate& Gate, double SpeedMS, double Dt, double Now);

	/**
	 * The dust's components, lent to herds and given back: a herd's own while it raises dust; one given back rests
	 * DustPuffLifeSec (its last puffs fading where they were left) before it is lent again; a new one is made only when
	 * none rests long enough. It keeps the components' pointers alone: whoever makes them keeps them from the garbage
	 * collector, and stops and starts them.
	 */
	class SLONK_API FDustPool
	{
	public:
		/** The component raising a herd's dust, or null. */
		UNiagaraComponent* Find(uint64 Id) const;
		/** A herd's component at a time (seconds): its own, else the one that has rested longest if it has rested long
		 *  enough, else a new one from Make (null if Make gives none). */
		UNiagaraComponent* Acquire(uint64 Id, double Now, TFunctionRef<UNiagaraComponent*()> Make);
		/** Takes a herd's component back at a time (seconds) to rest: the component, for the caller to stop, or null if
		 *  the herd had none. */
		UNiagaraComponent* Release(uint64 Id, double Now);
		/** The herds raising dust. */
		void LiveIds(TArray<uint64>& Out) const { Live.GetKeys(Out); }
		int32 NumLive() const { return Live.Num(); }
		int32 NumResting() const { return Resting.Num(); }
		int32 NumMade() const { return Made; }
		/** Forgets every component (their owner is going). */
		void Reset() { Live.Reset(); Resting.Reset(); Made = 0; }

	private:
		TMap<uint64, UNiagaraComponent*> Live;
		struct FRest { UNiagaraComponent* Component = nullptr; double Since = 0.0; };
		TArray<FRest> Resting;
		int32 Made = 0;
	};
}
