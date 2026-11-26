# Testing the ship's systems

How the systems are kept working as they grow. Three tiers, from cheapest to fullest.

## 1. Headless automation tests (`unreal/Source/Slonk/Tests/`)

The crew roster, the missions, the fleet, the exploration catalogue and the frontier are game-instance subsystems whose logic does not need a world: the
clock, the checks, the experiences, the mission stages, the fleet's hours, transfers. They get a test-only
constructor path (`InitForTest`) that wires them to each other directly, skips the save slots and never writes
them, so the tests touch no save on disk and run in a second editor process with no window and no GPU.

Tests are plain engine automation tests (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`, under `WITH_DEV_AUTOMATION_TESTS`),
named `Slonk.<System>.<Case>`. Run them all, or a prefix:

```
tools/run_tests.sh
tools/run_tests.sh Slonk.Fleet
```

The script runs `UnrealEditor-Cmd` with `-nullrhi` and `Automation RunTests Slonk.`, writes the engine's JSON
report to `/tmp/slonk-tests` and prints one line per test. They also appear in the editor's Session Frontend
(Window > Test Automation) under Slonk. A test takes a fraction of a second; the process start takes most of a
minute, so run them before a push rather than on every save.

What is covered now: the complement (502 named people, every department with a head, deterministic by seed), a
check that cannot fail and one that cannot pass, an experience moving trust, a transfer out and a join back (the
friend's tie, the posting, the new id), a tie matched by name and ship (a namesake lost elsewhere is not mourned; two ties to one name go together, mourned once), the game clock's presets, ramp and the stock warp law (warp 9 is 1516 c, four light-years at warp 7 is 53 h), a fleet leg from the distance (an hour near, a day far without a world, the way home the same leg), an event mission acted through to its end and one failed by its deadline,
a fleet ship's trouble becoming a support mission answered in range (her trouble over, her regard up, no stale
ids), the crew's ties aboard a ship bound to named people once each and never twice, a call abandoned (she is told, fights on alone, her day runs its course), an offer to lend a hand made once
and lapsing with her work (no stale id, no standing lost) and the board wiped clean of her record, a transfer each
way with the complements kept, a request refused for an officer, a ship lost with all hands, the ship's clock
started over under a running fleet, the sector catalogue (a dozen stars, apart, in the band, the same every game),
a system charted by arrival (once, with its regard and its first contact), named (never an unknown star, never a
taken name) and surveyed, a people recorded once, an Explore order carried from arrival through the survey
requirement to the chart filed under the catalogue's name, a first contact played through the hard start to its
record, and a beacon decoded to the nearest unknown star (and, with every star charted, closed complete with its
science kept); the frontier's rules (an outpost off the habitable world, the colony after it, the starbase when
Starfleet's regard allows, the works run down by the clock and commissioned, the settlements following a rename, a
small colony lost entire to a strike), an outpost order carried alongside a starbase then in the system and a
starbase's keel laid and its commissioning refused until the works are done, a rock raised for a colony world and
diverted at the rock or left to land (the regard lost, the colony gone) or abandoned and landing all the same (the
loss taken once), a colony order founding the colony at the landing and refusing a second order for the system,
a system with two lit worlds keeping both (the outpost off the dead one, no colony), and the rock's track maths
(the miss, the time, a push a day out clearing a world); the interior's room table (every room named and decked,
the lift stopping at deck G's corridor for the three rooms off it, the console's words, every compartment the crew
are placed in a room), and a ship's day of the crew's places (meals in the officers' mess or on the rec deck,
sleep in quarters, the off-watch hours on the rec deck, every place named a room of the interior); and the rocks on
the ground, built as a chunk build builds them (`SlonkTerrainTests.cpp`): a chunk and its four children placing the
same boulders in the same place, every rock seated in the drawn ground and none in the water, pebbles on the
finest chunks, the same chunk carrying the same rocks every time, and none inside a town; and the plants: grass,
bushes and trees on moist lowland and nothing with no life or no air, every tree found again on the finer chunks and
standing in the drawn ground, and every bush on the centre of a scrub dot the terrain material paints, as wide as
the dot, with about as many found on the coarsest chunk grid the terrain allows as on the default one, and every
bush found again on the finer chunks where a wrap plane of the material's noise period runs between them.
And the mountains: the relief law at three sizes and a range's peaks reaching toward the relief on a small world
and an Earth-size one, bare rock at the top; the coarse levels of detail of the ranges standing where the fine ones
average; snow above the snowline for the latitude and none below it, and on high ground the trees climbing toward
the treeline under it and stopping there. And the canyons: depth and width by the same root as the relief, cut
that deep and as wide as they say rim to rim; the plateaus ending in cliffs with islands of the caprock before them;
and badlands gullied metres deep below the scarps of a dry world, none without air.
`Slonk.Cities.*` (Tests/SlonkCitiesTests.cpp) covers the peoples and their settlements: who lives on each kind of world
(a lit Earth-like world an electric people, a dark one a people before electricity half the time, two deserts in
five, mostly cliff dwellers, the dead worlds nobody; only the modern and later grow their buildings, and no grid
under a grown or an early people's towns, an early people's ground lit only dimly by their fires; the colonists
never in the cliffs); every building shape in its unit box, a closed solid wound outward with collision of its own
shape (a fire none); each age's own kinds of building, no corner of any hanging over the ground (sampled at the
corners from the transforms, not where the builder looked) unless it stands on its footing, and no two buildings
in each other in a town or across neighbouring towns; and the towns built into cliffs, their houses on the ledges at
the foot of the risers with both rear corners in the rock and nothing hanging over the drop, and the towns along a
stretch of cliffs never building on each other; and each people's ways as the terrain paints them (a hamlet's paths,
a village's lanes, a grown town's ring ways and spokes, the roads between towns, through SlonkCities::PaintedWayCm,
which mirrors the material's choice of settlement point by point), no part of any building on one (every footprint
sampled all over, with no shortcut), ways in most towns and roads leaving most of them, and a village's houses
fronting onto their lanes by the painted ways' own slope.
`Slonk.Flora.*` (Tests/SlonkFloraTests.cpp, Tests/SlonkFloraWorldTests.cpp) covers what grows as a life form
(FloraTypes.h): the Earth's grass, bush, broadleaf and conifer held to the bit at every level of detail, and the static
meshes made of them, as they were before a plant became a descriptor (where each stands is held by
Slonk.Terrain.ClimateNeutral's chunks); every world to strangeness 0.15 growing the Earth's four under any star, its
pigment changing no style; a world grown stranger keeping every strange species it had, its roles always filled, a
role's two species never of one plan, no giant under the strangeness they begin at; the pigment by the star's light
(dark under a red dwarf, orange and red under a blue-white star) and how far the living ground takes it; the four
strange plans as meshes for every size class at the ends of their proportions (the unit height, their width, their
class's triangles at each level, solid parts wound outward, the glow's UV, a far crown and a capsule for a tree and a
giant); a second species of a role sharing its plants by stands and keeping each tree's species on the finer chunks,
a species' own height scaling its trees and moving none; the giants on their own lattice (their heights, upright, in
the drawn ground, the same on the finer chunks and thinned on the far level, none on a world without one, a dead world
or the sea); the meshes kept while a world holds their forms and let go after; and the terrain drawing a strange
species in a material of its own carrying its look, every parameter of which the foliage material has.
`Slonk.Geology.*` (Tests/SlonkGeologyTests.cpp) covers a strange world's own geology (EGeology, PlanetTerrain.h): none
rolled to strangeness 0.15 and on about three strange worlds in four at 1, each of a kind its archetype's ground can
make, the same on every roll and kept as the world grows stranger, reaching the terrain of every world that rolled one
unless its crust is lineae; the field marking it on a share of the land in provinces, never in the water, on a world
without rain too, and moving nothing of the world's structure; what the refinement makes of the marked ground (a pan and
a plain of glass smoother than the soil, crystal columns and prisms and spires standing tens of metres out of it,
terraces stepping a slope into level treads); the coarser levels of detail still carrying a spire forest and a crystal
field, lower, down to nothing where the refinement begins, a chunk's skirt hanging as far as they stand, and the ground a
flier reads clearing every one; no tree on it, nor the forest's painted cover, no herd grazing or crossing it, and the
chunk's vertices carrying its mark to the material; and the terrain material having its three parameters.
`Slonk.Weather.*` (Tests/SlonkWeatherTests.cpp) covers the cloud layer's cover over a place as the CPU reckons it
(PlanetWeather.h): clear between the weather map's masses and a deck inside a mass's cells, never less with more of the
masses, the cells, the style's coverage or its deck, the style's own even sky before the map is in, and a mean over a
whole map that rises with the coverage; the map read at a direction as the materials' sampler reads it, at a texel's
middle, half way to the next and round the seam; where the sun's light crosses the layer on its way down to a place; a
deck shading all the ground under it and a broken sky none of it as a whole; and the herds' dust and the leapers'
spray, which light themselves, dimmed by the sun's share of their light under a deck and exactly as they were under a
clear sky.
`Slonk.Missions.TimeLoop*` (Tests/SlonkSystemTests.cpp) cover a loop in time. `TimeLoop`: held, not running out, not
abandoned; its hours untouched by the clock; brought round by the run for clear space (an order, with no check), by
the ship going to warp (once, not every tick of it), and by its hours being spent; an attempt's hours taken from the
watch and from the stars' time, carried or not; what nobody aboard knows to try not offered or carried, and what the
watch has not the hours for refused; what a pass learns kept and the option that taught it off the board; what a pass
makes ready undone at its end; Starfleet's orders held while it lasts; the twelfth pass the last; one offered turned
down, or begun at the accepting. `TimeLoopLock`: in sixty folds one answer of three is right, has every field the
board shows as the wrong ones have, and is the one the two clues leave; both wrong answers refused with no check,
each the pass over, the next an hour shorter down to three, the answer closed; the right one the best regard and all
the science; orders reaching the ship again; the next fold known at sight. `TimeLoopExits`: the cold core (the flag
and its twelve hours), the hard point broken with nothing known but the fold (least regard, no science for it, the
next stone harder), the closing ridden (five to eleven whole days on, closed where she came out). Not covered by a
test, and seen in the game instead: the drive let go while held or cold, and the blows to the ship (they are the
ship's own, and the tests have no ship); nor the one-in-sixteen roll and its ten days, nor the save.

`Slonk.Missions.Leviathan*` cover a leviathan alongside. `Leviathan`: alongside at once with its seven hours; not to
be abandoned from the board; its hours untouched by thirty hours of the clock; an act's hours taken carried or not,
of the work left and of the stars' time, and the board saying how near that brings it; a finding kept, off the board
and its science earned the once; what the hours left will not cover refused with nothing spent; a gift it does not
want done as ordered with no check; at no hours alongside still a day on, nothing that takes hours to be had, and
standing fast (an order) ending it for small regard and the science of seeing what it did; its station (`StationCm`)
at seven hours, at none and with the road's hours given back; one offered turned down, or alongside from the
accepting, with the helm's rote let go the once. `LeviathanWant`: in ninety-six meetings one gift of four is wanted,
nothing shown of a gift varies from one meeting to the next, the three findings strike out three different wants and
never the wanted one, and each of the four is wanted somewhere; a wrong gift's hours and one more, its mark, its
line on the board, and twelve off the regard of the right one after it; the next of its kind with its call on file;
a full investigation leaving the hours for the wanted gift and not for the cold core, and the tending's watch lived.
`LeviathanExits`: the first shot the once and the driving off (no regard, the pieces' science, the next with five
hours); left astern at warp (the captain's leaving, nothing counted, the same want and what was learned back with two
hours fewer, the third leaving the last), by going down into air, and not by a drive found engaged at the first
look; a shot and a refused gift carried across a leaving, and a shot at the second meeting no first shot again; the
hours never under three, and less patience promised only where there is less; twelve off the regard for a shot,
six for a leaving, and its floor of five; the cold core cold whether wanted or not, a cold ship not leaving at
warp, and the refusal saying so; the lee's day and where it closes; one that wants quiet taking the core when stood
fast for; the road's beats (three hours for one, the once), the broken lock's hour, a giving unsaid by a burning
and a burning by a giving. `LeviathanRoll` lives a hundred and twenty days three times, hour by hour: out of open
space no leviathan comes; in it the same events fall, the same, up to the first leviathan; strangers keep ten days
apart; and one left astern is the next of its kind to come, the same creature, two days on or a little more. A
check that must carry inside the hours is tried under one crew after another. Not covered by a test, and seen in
the game instead: its body (station at rest and under way, turning, the ripples, the lash, its going), the ship's
guns and beam on it, the blows, the lockers and the hull, the autopilot let go, and what the panel and the HUD say
of it; nor the rate of the roll (one in sixteen), the save, what the crew carry of it (`Beast.*`), the scanner's
refusal, or the clock's hold. That the events without a leviathan are the events there were before is read from the
roll (its draw is a stream of its own), not tested.
`Slonk.Landmarks.*` (Tests/SlonkLandmarkTests.cpp) covers a strange world's landmarks (Landmarks.h): none rolled to
strangeness 0.15 or on a world with no ground, the same on every roll and kept as the world grows stranger, arches, a
vanished people's stones (monoliths or great rings, never both), reef towers, mound cities and giant shells at their
shares of the strange worlds, rock hanging in the air over none under strangeness 0.4, nothing its life would have built on a dead world and no reef where the sea is not liquid, and a kind
asked for by name (`ship spawnplanet ... landmarks <kind|all>`) standing whatever the roll, though none on a world that
is all sea and no reef on one with no sea; each kind's mesh at each
level of detail a closed solid wound outward, a hundred of its own centimetres in its kind's measure, its collision
convex parts that leave the way under an arch and through a ring open; the stands the field gives
(SlonkScatter::LandmarksNear, what `ship terrain goto <arch|monolith|greatring|reef|mound|landmark>` flies to) being the
pieces the chunks carry, of their kind's size, upright, their feet in the drawn ground or on the floor of a sea's
shallows (a floating rock as far over the field's ground as its stand says, its root clear of it), the same on the
chunks twenty kilometres across as on the nearest, and none on a world without them; nothing
else the scatter places standing in one's foot; and the nearest found first, the same every time.

Writing one: make an `FSystems` (a roster seed gives the same people every time), drive the subsystems through
their public API, assert with `TestEqual`/`TestTrue`. For anything that depends on the world (targets picked from
star systems, hulls spawned, the nearby list), feed the subsystem a hand-made `FMissionShipState` through
`AdvanceSeconds` instead; the fleet's `RegisterShip` makes a ship without an actor.

## 2. Play-in-editor scripts (the MCP harness)

The rounds so far were verified with Python scripts that drive a PIE session over the editor's MCP plugin: they
send `ship ...` console commands through the pawn's debug property, read the editor log for the `Crew:`,
`Missions:` and `Fleet:` lines the systems print, take screenshots of panels with `exec shot showui`, and stop the
session. They cover what the headless tests cannot: the actors (hulls warping, the nearby list, bay doors, the
shuttle), the panels, the HUD. They are the smoke suite for a round: `crewtest.py`, `missiontest2.py`,
`fleettest2.py` and their predecessors. The next step is to bring them into the repository under `tools/pie/` with
the small MCP helper they share, and a runner that starts the editor, waits for the MCP port, runs each script and
greps the log for the expected lines, so a round can be smoke-tested with one command.

## 3. Continuous integration

Building the editor module takes minutes and needs the engine installed, so hosted runners are out for now. A
self-hosted runner on the development Mac can run `Build.sh SlonkEditor Mac Development` and `tools/run_tests.sh`
on every pull request; the PIE scripts stay a manual step until the runner has a display. Until then: the headless
tests before a push, the PIE scripts before a merge.

## Later

Golden-image checks of the panels (the screenshots the PIE scripts already take, compared against a kept set with
a tolerance); property tests of the generators (every seed gives a valid complement and a mission with at least
one option per stage); a save-compatibility test that loads the previous round's save slots; timing budgets for
the hourly simulations (502 people, 65 ships) so a round cannot quietly make the hour expensive.
