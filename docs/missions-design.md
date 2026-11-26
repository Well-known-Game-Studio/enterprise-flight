# Missions

How missions come to the ship, how they run, and what they leave behind. The crew's side of this (checks, parties,
experiences) is in `crew-design.md` §4 and §8; this is the mission side. Code: `unreal/Source/Slonk/Missions.h/.cpp`,
`MissionPanel.h/.cpp`.

## 1. Three sources

A mission is one of three things, and the game says which it was.

- **Starfleet's orders** arrive over subspace as messages in the inbox, every six to fourteen ship's hours while
  fewer than two of Starfleet's are open. Each names an admiral, a brief and a deadline in stardates: the leg to the
  target at warp 7 plus two to four days (a board's one and a half to three), so it is reachable at any pace. Surveys,
  parleys, outbreaks, patrols, supply runs: the fleet's work. They are *offered*: accept them or let them lapse.
  Letting one lapse or dropping one accepted costs standing; the crew notice too.
- **A starbase's board** fills when the ship comes alongside a station (within resupply range): three postings for
  any ship that will take them, shorter deadlines, lesser regard, the station's own concerns (supplies for a colony,
  a survey nobody got to, a dispute, a fever). Posted once per station per ship's day, whatever became of the last batch.
- **What happens in space** happens on its own: a distress call, a dark hull across the course, an Orion freighter
  with her transponder off, a reading the sensors cannot classify, once in a while a ship of no known design. Rolled
  every one to three ship's hours while under way at impulse, not alongside, not under fire, and not already in the
  middle of one (warp encounters are for later). These are *active* at once, with a day to deal with them, and the ones with a vessel spawn one a few hundred
  kilometres off the bow (`ASpaceTraffic::SpawnNamedShip`), holding station: the freighter venting, the derelict,
  the stranger. Each hull is named uniquely (the freighters from a list, Orion freighters by name, an unknown contact by
  its number) so range checks and Set Course find the right one; when the mission closes the hull is retired, and
  beyond three retired hulls the oldest leaves the world. There is no accepting; ignoring one lets it lapse and that
  is a choice the crew read too.

## 2. The shape of a mission

A mission is a kind, a source, a target, a group, a deadline, and one to three **stages**. A stage is a situation
in a few sentences and two or three **options**. An option names the skill checked, the difficulty, the department
whose people lead the attempt, the experience tag each outcome gives the crew, the text each outcome reads, where
each outcome leads (the next stage, or the end), a risk (the chance that a failure hurts someone of the party) and
science points a success earns.

The ten kinds this round: Survey, Rescue, Diplomacy, Delivery, Anomaly, Medical, Patrol, Contact, Derelict,
Smuggler; the fleet added Support and Assist; exploration (`exploration-design.md`) added Explore (an order), and
FirstContact, PreWarp and Beacon, raised as events by the sector itself on arrival in a catalogue system
(`RaiseEvent`), with a vessel event's hull in a stranger's colours; the frontier (`frontier-design.md`) added
Outpost, Colony and Starbase (orders and postings, carried in stages that each have a place of their own: a
Station with no name is any starbase alongside) and Impact (an event or an order, a real rock in the world, the
deadline its landing). Each is a template in `MakeMission`; the target comes from the galaxy's own names (`PickTarget`: a world
with a surface in another system, a station other than the one alongside, another system for a patrol), so every
mission points at somewhere the ship can actually go, and arrival is simply the target's name turning up in the
ship's neighbourhood (the same nearby list the crew's ties use) or the ship being in the named system.

## 3. Acting

An option is taken from the panel (a button per option) or `ship mission act <id> <option>`. The stage must be at
the target if it says so. The **party** is the captain's named one for the mission (`ship mission party <id>
<names>`; if none of them is fit and aboard the option is refused rather than sent with others), else the option's
department: its head and second if fit, then the two best aboard at the skill. The
check is the roster's (`UCrewRosterSubsystem::Check`) with the mission's group, so the party's skills, mood,
stress, facets and their biases toward the people concerned all count, and the summary names who carried it or let
it slip. Then:

- the outcome's text goes in the mission log, and the crew live it: the party as those who were there, the ship
  hearing of it, with the group and the place, and as the captain's choice (`FCrewExperience`);
- a failure with risk may wound or kill someone of the party, through `UShipSystemsComponent::HurtCrewMember`, so
  the medical counts and the roster move together and the next reconciliation agrees;
- science points accrue to the mission and are paid into the science log as one record when it completes;
- the mission moves to the next stage or closes: Completed (the full regard, or a third of it when the last option
  failed but did not end the mission), or Failed (half the regard lost).

## 4. Standing and the log

Starfleet's regard is one number, saved, with a title from "facing a board of inquiry" to "the fleet's finest";
it is the campaign's score for now and later gates what Starfleet asks and offers. The mission log keeps every
closed mission with its stage-by-stage lines. All of it is in the "Missions" save slot, written a couple of seconds
after any change and loaded with the game instance, like the crew.

## 5. The panel, the HUD, the console

`-` (or Missions from the main computer, `ship ui missions`) opens the board: left the open missions, the inbox or
the log (Show cycles); right the selected mission's brief, its present situation with the options as buttons, the
party note and the log. Sidebar: Accept, Abandon (confirmed), Set Course (nav target on the mission's target),
Crew, Main Menu. The HUD shows one MISSION line: the active mission with the nearest deadline, whether its target
is in range, how many more are active and what waits on the board. The bridge log carries the operations officer's
"subspace message from Starfleet" and the sightings that start an event. Console: `ship mission list | log | inbox
| read <n> | show <id> | accept <id> | act [<id>] <option> | party <id> <names> | abandon <id> | here | dev <kind>
[orders|board|event] | reset`.

## 6. Puzzles in text: the fold in time

A mission can be a puzzle played on the board, and the mission system has the pieces for one. The first is a **loop
in time** (`EMissionKind::TimeLoop`, "A fold in time": one event in sixteen, never within ten days of the last, or
`ship mission dev timeloop`): one stage the ship lives over and over until she is got out of it, by one of four ways
that each cost something different. What makes it a puzzle and not a list:

- **Acting takes time, and there is not time for everything.** An option can cost hours (`FMissionOption::Hours`,
  on its button). They are lived, carried or not: the crew live them and the stardate moves. A pass has five
  (`PassHours`; `FMission::HoursLeft`), so no pass can do everything, and an option the pass has not the hours for
  is on the board dimmed and refused. The hours are spent by acting and by nothing else: the clock takes none of
  them, and while the ship is held it runs at ×1, so the board can be read at leisure.
- **What is learned is kept; what is made ready is not.** A success can teach the mission something (`GivesMark`,
  kept in `FMission::Marks` from pass to pass), and the option that taught it leaves the board: a thing is found
  out, and its science earned, the once. A success can also make something ready for the rest of the pass
  (`MakesReady`, `FMission::Readied`, undone when the pass ends; `NeedsReady`, `SaferWhenReady`). The stage's text
  is written again each time from both: the same watch, then one line a thing known.
- **Options appear, and close.** An option can need a thing known, or two, or a pass reached (`NeedsMark`,
  `NeedsMark2`, `MinPass`): until then it is not on the board, the console does not list it, and `Act` refuses it.
  It can be closed by a thing learned (`NotIfMark`), and a failure can teach too (`FailGivesMark`).
- **Clues, and a guess that can be wrong.** A mission rolls its particulars (`FMission::Secret`), so the answer is
  another one each time. An option can be a wrong guess (`bWrong`): it fails whoever makes it, with no check, and
  nothing the board or the console shows of it differs from the right one. A wrong guess can cost: a blow to the
  ship (`WrongBlow`), a shorter pass from then on (`FailShortensPass`). What tells right from wrong is in the text
  of what was found.
- **An order is not an attempt.** An option can be carried as given, with no check (`bSure`): there is no skill to
  be got from repeating it.
- **More than one way out.** An option that ends the mission can name the way out it is (`Exit`, kept in
  `FMission::Exit`) and Starfleet's regard for that way (`ExitStanding`); what follows is the way's own. The board
  says what each way will cost, as far as the crew can see it, before it is taken.
- **Later missions remember.** The mission system keeps what earlier missions left (`Remembers`, saved, for good):
  a later one is made differently for it.

A pass ends when its hours are spent, the moment the ship goes to warp to run for it, or when an option's outcome
says so (`ComeRound`). A loop the ship is in does not run out and cannot be abandoned, and the twelfth pass is the
last: no crew is held for good, whatever its checks. Each pass wearies the crew a little (`Time.Loop`). While the
ship is held (`IsHeldInTime`) no orders reach her from Starfleet, no call from the fleet, no starbase's board, and
no other event; the drive is let go the moment it is engaged, and the autopilot with it. The stars' time goes on
with every hour spent: an hour off every other mission's deadline (a rock already in flight keeps its own time).

### The fold

A lock left on an old road by the people who raised the rings and the standing stones. It holds a ship with a live
drive in one closed watch. What there is to find, and what each costs:

| Found by | Hours | Needs | It teaches |
| --- | --- | --- | --- |
| Scan the fold | 2 | | The fold has the ship by her drive's field; there is a dead hull in it, and a hard point at the seam |
| Hail Starfleet | 1 | | The hail comes back before it is sent |
| Ask the doctor | 1 | the second pass | What a watch learns is carried through the closing; a ship might be |
| Send a probe to the hard point | 3 | the fold | The stone: a lock, calling in five beats, wanting an answer |
| Board the dead hull | 2 | the fold | The answer her people gave, which did not serve them |
| Read the stone's faces | 2 | the stone | An answer its makers would not have taken |
| Rig for a blow | 1 | the fold | (made ready, this pass only: a third of every blow, and of the risk) |
| Run for clear space | 0 | | (an order: the pass is given up, and the next begins) |

The stone takes one answer of three (its call as given, its call turned about, silence for its length). The dead
hull rules out one wrong answer and the stone's faces the other; either alone leaves an even guess. A wrong answer
is refused: a blow to the ship (25 hull points by her own model of a blow with no place to it: a system damaged,
casualties), the pass over, every pass after an hour shorter (never under three), and the answer known for wrong.

| Way out | Hours | Needs | Regard | What follows |
| --- | --- | --- | --- | --- |
| Answer the stone | 1 | the stone | 50 | Science 250. The road knows the ship: every later fold's stone is known at sight (its scan and probe are not there to be done again) |
| Shut the core down cold | 4 | the fold | 35 | Science 60. No warp for twelve hours while the core is lit again |
| Torpedo the hard point | 1 | the fold | 20 | A blow to the ship (70 hull points; a third if she was rigged). Every later stone is harder to answer (95 for 80). It can be done without ever learning what the hard point was |
| Ride the closing through | 1 | the fold, the echoes | 30 | Science 150. Thrown five to eleven days on: other missions' deadlines pass. The crew's own days do not move (a promise is no nearer broken) |
| (none: the twelfth pass) | | | 0 | The fold lets a worn ship go. The core is cold, as above |

The regard is less three for every pass after the first, never under ten. Torpedoing can be done on the first pass
(scan, rig, fire); every other way on the second at the soonest, by a crew whose checks all carry; a full
investigation and the right answer is two passes with not an hour to spare (scan and probe; hull, faces and the
answer). Guessing blind costs a wrong answer on the average, and its blow.

## 7. Later

Options that need a real away party in the field (the shuttle down, people on foot: the crew figures and the
interiors first), events between systems and at warp, missions that chain (a smuggler's manifest leads to a
starbase's investigation), a starbase's board that remembers what the ship turned down, rewards in stores and
recruits, Starfleet's regard gating offers and the admirals' tone, factions tracking the group outcomes beyond the
crew's own biases, time pressure that bites (a rescue's survivors dwindling by the hour), and a mission's stages
tied to real events (the freighter actually towed, the derelict actually boarded) once the ship's interiors and
the crew's figures exist to show them.
