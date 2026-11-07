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
(`RaiseEvent`), with a vessel event's hull in a stranger's colours. Each is a template in `MakeMission`; the target comes from the galaxy's own names (`PickTarget`: a world
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

## 6. Later

Options that need a real away party in the field (the shuttle down, people on foot: the crew figures and the
interiors first), events between systems and at warp, missions that chain (a smuggler's manifest leads to a
starbase's investigation), a starbase's board that remembers what the ship turned down, rewards in stores and
recruits, Starfleet's regard gating offers and the admirals' tone, factions tracking the group outcomes beyond the
crew's own biases, time pressure that bites (a rescue's survivors dwindling by the hour), and a mission's stages
tied to real events (the freighter actually towed, the derelict actually boarded) once the ship's interiors and
the crew's figures exist to show them.
