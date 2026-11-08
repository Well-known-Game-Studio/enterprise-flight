# The frontier

What the Federation does with new space once it is charted, and what the ship does when a world is in danger.
Two systems: **expansion** (outposts, colonies and starbases founded in charted systems, by Starfleet's orders
carried by the ship) and **planetary defence** (an asteroid or comet on a collision course with an inhabited
world, tracked, diverted or broken up). Code: `unreal/Source/Slonk/Frontier.h/.cpp` (the settlements and their
growth), `Impactor.h/.cpp` (the rock), `Missions.h/.cpp` (the four mission kinds). Exploration is in
`exploration-design.md`; missions in general in `missions-design.md`.

## 1. Settlements

`UFrontierSubsystem` (a game instance subsystem, save slot "Frontier") keeps the **settlements**: what the
Federation has founded in a star system beyond the home system's own stations. Each is a kind, a system, a world,
a name, a stardate and a population:

- **Outpost**: a small station (K-7 lines, a kilometre or two across) off a solid world, unmanned but for a
  science and operations crew. It is a place to resupply, a board with a posting or two, and the Federation's
  claim on the system. Any charted system with a solid world can take one.
- **Colony**: eight hundred settlers on a habitable world (a terrestrial or ocean world of the temperate band,
  never a pre-warp world), lit at night as a settlement is, growing a little every ship's day and more with every
  supply run that lands. A colony needs the outpost first. It is the thing the sector's dangers are measured
  against.
- **Starbase**: a Spacedock-class station with its own patrol ships, which the fleet counts as a home. The works
  take three ship's days from the keel to the commissioning, and the station is in the sky from the keel (the
  works are a state of the settlement, shown on the chart). A starbase needs an outpost or colony in the system,
  and Starfleet's regard for the ship well up (sixty): the fleet grows where its best captains have been.

Settlements are made by missions, never spawned on their own, and replayed from the save at the start of every
session: the station is spawned off its world (`ASpaceTraffic::SpawnStation`), the colony world lit
(`APlanetBody::SetNightLights`). Names: the outpost after its world ("Vashti's Reach II Outpost"), the colony from
the generator ("New Tavira"), the starbase by the next number from four hundred. A system renamed renames them.

## 2. The expansion orders

Three mission kinds, Starfleet's orders over subspace while there is somewhere to found (a sixth of the orders)
and a starbase board's postings; or asked for: `ship frontier propose <kind> [<system>]` puts the case to
Starfleet, which answers with the order in the inbox when the system qualifies, else says why not. Each is carried
in stages with a **target per stage** (new in `FMissionStage`: a stage may name its own place), so the orders
have the shape of the work: load at a starbase, then go.

- **Outpost** (`Outpost`, +15 regard): at any starbase, *take on the outpost module and its crew* (operations) or
  *load it fast and go* (engineering, risk); in the system, *survey the worlds for a site* (science: the best
  world) or *put it on the first solid ground* (engineering); then *land the module and raise the outpost*
  (engineering, risk). The outpost exists from the last option.
- **Colony** (`Colony`, +25): at a starbase, *embark eight hundred colonists and their seed stock* (medical) or
  *take the volunteers and the stores there are* (operations, fewer settlers); at the world, *shuttle them down a
  hundred at a time* (engineering, risk) or *beam them down and the cargo after* (operations); then *see them
  through the first weeks* (medical, science) or *leave them to it and report* (command). The world is lit from
  the landing.
- **Starbase** (`Starbase`, +40): at a starbase, *embark the construction corps and the first hull sections*
  (engineering); in the system, *lay the keel* (engineering; the station appears, the works begin, three days);
  then *hold station and guard the works* (tactical, as often as the captain likes) and, when the works are done
  (the option is refused with the hours left until then), *commission the starbase* (command, without a check):
  its patrol ships spawn, the fleet takes it as a home.

An order whose system is renamed follows the name; a colony's world the same.

## 3. The threat from the sky

**Impact** (`Impact`, +35, a failure −40) is raised where there is something to lose: an inhabited world, meaning
a colony of ours, a pre-warp world, or one of the home system's lit worlds. It comes as an event in the system (a
rock the sensors pick up on a closing course, one event in six where a world qualifies) or as Starfleet's orders
for another system ("the Astronomical Survey has tracked…", the leg counted into the time). Either way the rock is
real: an `AImpactor` (an `ASpaceTarget` with a velocity in universe space, under the game clock), a kilometre or
three across, named for the stardate ("Asteroid 8305-K"; one in three a comet, with a coma and a tail away from
the star), spawned a few million kilometres out on a course that meets the world in a day or two of ship's time.
The deadline is the impact. It is in the nav list and on the HUD (IMPACT line: the world, the hours to impact,
the miss distance), it can be locked, scanned and tractored like any rock, and the clock treats it as a body to
close on.

Two stages:

- **The track** (anywhere): *refine its orbit* (science, +science: the impact point and the hour are known) or
  *warn the world and begin the evacuation* (operations: if the rock lands, half the loss).
- **Divert it** (at the rock, within hailing distance): *tractor it off its course* (engineering), *break it up
  with torpedoes* (tactical) or *a party to plant charges on it* (engineering, risk, science). A success changes
  the rock's course or shatters it, and the mission completes; a failure leaves it on course and the stage open
  for another try, the clock running.

And the ship can do it herself: the tractor beam on the rock gives it a sideways push the longer it holds (a
three metres per second a second across its track, the beam's hold on the clock making it real seconds), so a rock
caught a day out needs a minute of the beam and one caught an hour out needs more than the beam has; phasers and
torpedoes break it (its health by its size, two or three kilometres of rock being ten seconds of phasers or a
spread of torpedoes). A torpedo flies at six hundred metres a second over the ship's own speed, and the rock at
twenty-five kilometres a second: fired from ahead of it, or at a rock held in the beam, they meet; fired after a
rock already past, they never catch it. The mission sees the rock miss or die and closes complete. If the rock lands: the world's lights dim by the blow, a colony loses most
of its people (all of them, if small), Starfleet's regard falls by forty, and the crew carry it (`Loss.Civilians`,
with the evacuation halving the dead).

## 4. Readout and console

- HUD: the CHART line adds the settlement (OUTPOST, COLONY <name> <people>, STARBASE 400, WORKS 31 H); an
  IMPACT line while a rock is on course in the system (red), green when it will miss.
- The star chart and nav list carry outposts and starbases as stations, the rock as a rock.
- Console: `ship frontier [list | propose <outpost|colony|starbase> [<system>] | dev found <kind> [<system>] |
  reset]`; `ship impact [status | raise [<world>] | deflect | shatter | hit]` (the dev ops for testing);
  `ship mission dev outpost|colony|starbase|impact [orders|board|event]`.

## 5. Later

Colonies that ask for things (their own board, a fever, a bad harvest, raiders) and grow into worlds with a
voice; outposts that become starbases' yards; the fleet's new ships commissioned at a starbase of ours by name;
the tender in the works as a fleet ship with a directed mission; comets as the science they are (a close pass
the science log notes); the impactor's fragments as a hazard of their own; evacuation as a mission of ships; and
the Federation's reach as a thing the chart shows, system by system.
