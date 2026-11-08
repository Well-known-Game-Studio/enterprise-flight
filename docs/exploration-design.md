# Exploration

The ship's other job: new space. Uncharted stars beyond the three charted neighbours, generated from a seed the
first time anyone goes there, with nothing of Starfleet in them; charting them, surveying their worlds, and the
people met there. Code: `unreal/Source/Slonk/Exploration.h/.cpp` (the sector catalogue and the charting),
`Missions.h/.cpp` (the four exploration mission kinds), `StarSystem.h/.cpp` (a system that is a star until it is
revealed). Missions in general are in `missions-design.md`; the crew's side in `crew-design.md`.

## 1. The sector catalogue

`UExplorationSubsystem` (a game instance subsystem, saved to the "Exploration" slot) holds the **catalogue**: a
dozen uncharted systems within thirty light-years of Sol, made once from a fixed seed so every game has the same
sky. Each entry is a catalogue designation (`UFC 2461`, the United Federation Catalogue), a real-sky bearing and
distance, a spectral class (red dwarfs most common, white stars rare), sometimes a companion, a seed, a proper
name nobody has given it yet, and a state:

- **Unknown**: a star in the catalogue and nothing more. It is in the sky from the start, a point with its
  designation, in the nav list, the star chart's sector view and the HUD; the autopilot and the transwarp leg
  take the ship there like anywhere else.
- **Charted**: the ship has been there. The system was revealed on the approach (within a light-year its worlds,
  belt and companion are generated from its seed, the same every time), the science officer reported what the
  sensors found, the ship's log took the stardate and the chart earned its regard and science. No stations, no
  traffic: Starfleet has never been here.
- **Surveyed**: the science log's survey of the system is complete (the star and every world scanned, the
  existing survey bonus), which is what makes a chart worth filing.

A charted system is named: the captain's choice (`ship chart name <name>`; the designation stays as its catalogue
number) or, when the chart is filed unnamed, the catalogue's own. Its worlds carry the system's name with
numerals until the survey names them. Charted systems join the galaxy's names: missions can be sent to their
worlds, the fleet's runs can go there, the star chart calls them by name. A name given follows everywhere the old
one went: the star and its worlds in the sky, the clouds hung off its giants, the science log's records and keys,
the missions' targets, the contact records, a scan under way at the moment of naming; `ship chart reset` takes
them all back to the catalogue numbers the same way.

Charting happens by **arrival**, mission or not. The captain who points the ship at an unknown star and goes is
exploring, and the subsystem rewards it the same.

## 2. Revealing a system

`AStarSystem` now has two steps. `Generate` spawns the star (and the companion) from the spec and, unless the spec
says `bStarOnly`, fills the system at once as before. `Reveal()` fills a star-only system later: the planets and the
belt from the seed, `PopulatePlanets` only when the spec says Starfleet is there (`bPopulate`, false for the
catalogue). The exploration subsystem reveals a system when the ship comes within a light-year of its star (checked
every couple of seconds from the ship's tick) or when a mission or the console asks; charted systems are revealed
at the start of a session, so the worlds are there to come back to.

## 3. What happens there

Entering a system the first time, the subsystem rolls what the sector holds, and the mission system raises it as an
event (`UMissionSubsystem::RaiseEvent`), active at once like any encounter:

- **First contact** (`FirstContact`): a vessel of no known design closes on the ship, holding station off the bow:
  an alien hull (the procedural starship in a stranger's colours and proportions). The first charted system
  always has one; after that one system in two. The people are **new**: a species named from the generator
  (`the Vethrani`), with a record in the exploration save of where they were met, when and how it ended, and the
  crew's biases toward them begin from nothing and move with the contact. Three stages: *approach* (hail on all
  frequencies; observe and record first; shields up and hold, which the crew read as hostility and makes the
  next stage harder), *the exchange* (the linguists work; the ship's own records offered; a delegation invited
  aboard, with risk), *terms* (a treaty of friendship; trade, their charts for our medicine; part in peace).
  Starfleet's regard for a contact that holds is the biggest in the game; one that turns hostile costs it, and the
  record says *hostile* for the next ship that meets them.
- **A pre-warp world** (`PreWarp`): a charted system whose worlds include one with city lights at night is a
  civilisation that has not left its world. The Prime Directive applies. One stage at the world: *observe from
  high orbit, sensors passive* (science, safe); *a survey party in disguise* (science, risk to the party, more
  science); *make contact, they are ready* (diplomacy, hard: a success is a first contact of its own, a failure is
  a breach, the mission fails and the regard loss is heavy).
- **An ancient beacon** (`Beacon`): one charted system in three holds a signal older than any people known, and
  it points at another unknown star in the catalogue. *Decode it* (science, not at the target) sets the mission's
  target to that star; *find its source* there (science, the largest science reward of the kinds) charts it. One
  leads to the next: a chain into the dark.

And the orders: Starfleet asks for charts. **Explore** (`Explore`) is the most common order while unknown stars
remain, and a starbase's board posts them too: *Chart UFC 2461*, the leg at warp 7 plus the margin. At the target:
*complete the survey* (the real one: the option is refused while the science log's survey of the system is not
complete, and carried without a check when it is, for the mission's full science) or *send the shuttle to the most
promising world* (a science check with risk, less science). Then, anywhere, *name it and file the chart*, which
names the system if the captain has not.

## 4. Rewards

| | regard | science |
|---|---|---|
| charting a system by arrival | +10 | 150 |
| Explore carried | +20 | 300 (survey) / 180 (shuttle) + 60 |
| first contact that holds | +40 | 400 |
| first contact turned hostile | −20 | |
| pre-warp world observed | +12 | 150–250 |
| pre-warp breach | −30 | |
| beacon's source found | +15 | 500 |

The science goes through the science log as mission rewards, as before; the regard through Starfleet's standing.

## 5. The readout and the console

The HUD's MISSION area gains a CHART line: in an unknown or charted system its designation, name and state and the
worlds the sensors count; elsewhere how many of the catalogue are charted. The star chart's sector scope shows the
catalogue's stars by designation until they are named. The science officer reports the arrival and what the system
holds; the bridge log keeps it. Console: `ship chart [list | here | name <name> | reveal <designation> | reset]`;
`ship mission dev explore|firstcontact|prewarp|beacon [orders|board|event]`.

## 6. Later

The alien hull as a design of its own (parts and proportions the builder does not make yet), their language as a
puzzle rather than a check, the contact record driving later encounters (their ships in the sector, a second
contact that remembers the first), colonies and outposts the Federation founds in charted space, a sector beyond
thirty light-years once transwarp is an unlock, the chart as a thing the captain draws on, and away missions on
the pre-warp world once the interiors and the figures are there to show them.
