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
diverted at the rock or left to land (the regard lost, the colony gone), and the rock's track maths (the miss, the
time, a push a day out clearing a world).

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
