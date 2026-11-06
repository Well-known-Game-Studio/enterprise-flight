# Time

One clock for everything that happens in the world, scaled by the player.

## The problem it solves

Before this round time ran at two speeds. The crew's clock, the missions and the fleet lived in ship's hours at
one real minute an hour, while the hull's travel, the traffic's warps, repairs, shields and sickbay treatment ran
in real seconds. So at warp 9 the ship crossed an AU in a third of a second while a fleet ship took "two hours"
to the next planet, and a four light-year leg took twenty-three real hours, which the clock read as fifty-eight
ship's days. Canon says about a day. Fleet legs and mission deadlines were random hours and days, unrelated to the
distances involved.

## The shape

- **One simulation clock.** `UGameClockSubsystem` (a world subsystem) owns a time scale and hands out simulation
  seconds. At ×1 a ship's second is a real second: the roster's hour is 3600 simulation seconds, and a day aboard
  is a day. The scale the player asks for (`Requested`) is clamped to [1, `MaxScale`] (3600); the effective scale
  ramps toward the request or the autopilot's drive through log space, a decade in about half a second going up
  and faster coming down, and a hold clamps it at once: a ramp down from ×3600 toward a ×1 hold would still run
  the next frame at ×2971, fifty cruise seconds in one step past a station ten seconds away. `Sim()` clamps the
  frame it scales to the same tenth of a second as the clock's own advance, so a hitch moves every consumer by the
  same world time.
- **Two kinds of consumer.** Everything that models the world takes simulation seconds (`Clock->Sim(RealDt)`):
  the hull's translation (impulse and warp), the orbit hold, the autopilot's distances, the traffic's own
  movement and warp legs, the fleet hulls, repairs, shields, life support, sickbay, resupply, the roster, the
  missions, the fleet, the spin of stars, planets and stations. Everything presentational keeps real seconds:
  steering and attitude (game feel), the camera, input, LCARS, Niagara, streaks, plumes, shake, audio, the warp
  drive's charge, jump and drop sequences, the traffic's stretch. The engine's global time dilation is not used:
  it would speed the presentation too, and the movement component caps a frame at 0.1 s so a dilated frame has to
  be integrated as one long simulation step anyway.
- **Holds.** The possessed pawn reports each frame what the situation allows (`Allow(Cap, Why)`): red alert,
  under fire, phasers firing, any torpedo in flight (they fly in real seconds, and at ×60 the ship would overrun
  her own shot), the tractor beam, a warp charge, jump or drop in progress, landed or in atmosphere all hold ×1; a body or station the ship is closing on holds the scale to the square of its time away at the
  closing rate over ten seconds (ten real seconds away holds ×1, a hundred ×100). The square matters: a hold
  proportional to the distance makes every approach an exponential tail that never arrives, while the square
  tightens late and an approach takes seconds. The shuttle holds ×1 (her launch, dock and landing are real
  seconds), and with nobody allowing anything (on foot) the clock holds ×1. The effective scale is the smaller of
  the request and the hold, so a request survives a fight and returns after it.
- **The autopilot drives the scale.** On a warp leg it picks a cruise factor (the drive's setting, raised if the
  leg would otherwise take more than a minute of real time at the maximum scale, and never past 9.9 unless the
  player already set transwarp). At its first frame at warp it plans the leg: the scale that makes the whole leg
  about a minute of real time, held until the remaining distance is ten real seconds of travel at that scale,
  then brought down to ×1 by the square root of what is left of that stretch, reaching ×1 ten seconds of cruise
  out (`Drive(Scale)`). The last stretch runs at ×1, so the deceleration and the drop out of warp happen as
  before, with terrain, cities and clouds streaming in behind it. A four light-year leg at warp 7 is about
  ninety real seconds.
- **Distance drives the generators.** A fleet ship's leg is its distance over the warp law at her class's cruise
  (Excelsior 8, Constitution 7, Miranda 6), whole hours, at least one; her hull's warp uses the same law, so "9 h
  out" is when she arrives. A far ship's trouble lasts long enough for a ship to come from her home system. A
  mission's deadline is the travel time to its target at warp 7 plus two to four days of margin, so it is
  reachable at any scale and tight at low warp. Orders and events keep scheduling in ship's hours.
- **The readout.** The HUD shows `TIME ×60` beside the shields line with the reason for a hold, and the nav line's
  ETA is the ship's time with the real time after it when they differ. 8 and 9 step the scale through the presets
  (1, 10, 60, 600, 3600); the console has `ship clock [n|up|down|status]`.

## Default

A new game asks for ×60: the ship's routine passes as it did (a day aboard in twenty-four real minutes) and the
holds take it to ×1 whenever something is happening. ×1 is a setting away for those who want the day to be a day.

## What this means in play

- Pre-transwarp, a four light-year leg at warp 9 is one ship's day: twenty-four real minutes at ×60, twenty-four
  seconds at ×3600. Transwarp stays what makes a day a minute at ×1.
- A twenty-four hour injury is a real day at ×1; the dilation is how the ship's day passes at all.
- A fleet ship on a far run is gone for days or weeks of ship's time, which is hours of play at ×60.

## Traps

- Large steps: at ×3600 and warp 9 a 60 fps frame moves 0.03 light-years. Arrival logic is "the target is behind
  us" as well as "within the drop window", and the drop window scales with the simulation step. Doubles and the
  floating origin carry the magnitude.
- A hold set during one frame's ticks applies from the next frame: the pawn ticks after the movement component.
  So the movement's proximity governor protects the frame itself: its approach interval is at least two simulation
  steps (a dilated frame covers at most half the distance left to a body) and the unswept warp move is bounded by
  that distance, whatever the floor speed times the step comes to.
- The traffic's spool before a leg is the stretch's real seconds (`PhaseTimer`); the leg's depart and arrive
  timestamps are set in the world's seconds when travel begins, not when the leg is ordered, or at ×60 the hull is
  hidden before the stretch plays.
- The roster's `SecondsPerShipHour` is 3600; the one formula that converts seconds to hours (a head of department
  hurt in a hit) reads it.
- Saves and the headless tests work in hours and never saw the scale.
