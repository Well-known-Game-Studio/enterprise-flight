# The crew: a design for people aboard

The ship carries five hundred people. Until now they were four numbers (fit, wounded, critical, dead) and seven
named officers who speak on the bridge log. This design makes each of them a person with a record that the days
aboard, the missions and the captain's choices write into, in the manner of Dwarf Fortress: histories, states,
desires and fears, bonds between them, and growth, so that risking or losing one of them costs something
particular, and the roster you keep is a decision.

It is written to serve three later rounds as well: crew on shifts moving about the ship as the interiors grow,
away parties drawn from real people, and generated missions whose turns are decided by who is there and what
they are like.

## 1. The person

A crew member is a record (`FCrewMember`) with a stable id and these parts.

**Identity.** Name, species (the TMP-era mix: human, Vulcan, Andorian, Tellarite, Deltan, Caitian, Bolian, Betazoid,
Rigelian, Saurian), pronouns, age, home (an Earth city, a colony, or a world from the galaxy generator), the academy
class and a line of backstory. Rank (crewman, able crewman, petty officer, chief, ensign, lieutenant j.g.,
lieutenant, lieutenant commander, commander, captain), department (command, helm, operations, engineering, science,
medical, security, services) and a posting: a station in a compartment (the bridge's conn, main engineering's
warp core, sickbay's ward, a torpedo bay, the galley). The senior officers of the existing systems component are
the department heads of this roster, not a separate list.

**Skills** (0 to 100, each with experience toward the next point): command, helm, operations, engineering,
science, medicine, tactical, security, diplomacy, and a trade skill for the services (cooking, maintenance,
quartermastering). Skills grow by use: an hour at a posting earns experience in its skill, a mission check earns
more, teaching from a mentor more still. Growth slows as a skill rises, so a crewman's first year is quick and a
chief engineer's last ten points are a career.

**Facets** (0 to 100, fixed at birth, drifting slowly with experience): resolve (how stress is carried), empathy,
curiosity, discipline, courage, sociability, ambition. They decide how a person reacts to the same event: a
curious one is lifted by a nebula flight; a disciplined one by a tidy repair; a courageous one is steadier at red
alert; an ambitious one chafes without a promotion.

**Traits**: a few tags with mechanical hooks, drawn at generation and sometimes earned. Examples: Veteran (steadier
under fire, dislikes idle days), Homesick (letters and shore leave matter), Transporter-shy (beaming is a fear),
Pacifist (combat costs mood, medicine suits), Thrill-seeker (away missions are a desire), Devout, Gambler,
Chess player (bonds with other players), Wounded pride (a reprimand lingers), Survivor (earned by surviving a
hit that killed others: resolve up, a fear gained).

**Needs** (0 to 1, drifting down, restored by what satisfies them): rest, health, safety, belonging, purpose,
recreation, recognition. Each shift at a posting feeds purpose and drains rest; the off-watch hours in quarters,
the mess and the rec room restore rest, belonging and recreation; red alert drains safety; a commendation feeds
recognition. A need below a quarter is a pressure and shows on the roster.

**Mood** is derived each hour from the needs weighted by the facets, plus the person's recent **memories**: a list
of weighted moments (a friend lost, a commendation, a discovery they were part of, a narrow escape) that decay
over days, with a few marked permanent. **Stress** is separate, climbing under alert, damage and loss and falling
with rest and calm, resisted by resolve; stress above a threshold with low mood yields breakdowns: a mistake at the
station (a check failed), a request for transfer, a quarrel, a refusal.

**Desires** are a short list of wants with a horizon: a promotion, an away mission, shore leave, a letter from
home, a posting on the bridge, to see a particular kind of thing (a nebula, a gas giant up close), to serve under
a particular officer. Granting one is a strong memory and a loyalty; ignoring one for long is a pressure. **Fears**
are the mirror: death in space, a particular enemy, transporters, disgrace, losing a friend, the dark between
systems. Fears raise the stress of their trigger and can be faced down (a courageous person who beams down ten
times loses the transporter fear).

**Relationships**: a sparse map from another id to an affinity (−1 to 1) and a kind (acquaintance, friend, close
friend, rival, mentor/protégé, partner, family). They form from shared shifts, departments, messes and events,
and they carry consequences: a friend's death is grief (mood down for days, a permanent memory, a chance of a
fear), a rival's promotion is resentment, a mentor's presence speeds learning.

**Ties elsewhere**: family and friends off the ship, each a name, a relation (a sister, a father, an academy
roommate, a former captain), and a place: a world, another ship of the fleet, or a station, bound to the galaxy's
own names once the systems exist. Letters come from them and carry news, good or bad; when their ship is in
hailing range a channel is asked for and granted; when the ship lies alongside their station the person goes
ashore for an evening and comes back the better for it, and the desire to visit home is met. They are the
mission generator's hostages: a colony in trouble is somebody's mother's colony.

**Condition**: fit, injured (hours to recover, under treatment or walking), critical (in sickbay, may die), dead,
on leave, away (on an away mission), transferred. The medical round's four counts become sums over the roster,
so a hit that wounds twelve wounds twelve people by name.

**History**: the personal log, a capped list of entries (stardate, kind, text, the ids involved), oldest dropped
first except milestones (joined, promoted, commended, wounded, bereaved, decorated). The service record counts
missions, commendations, reprimands, wounds, kills (for an officer: engagements) and days aboard.

## 2. The ship's day

A ship's clock runs in stardates. By default a ship's hour is one real minute, so a day aboard is twenty-four
minutes of play; the scale is a tunable and warp legs may run it faster later. Three watches of eight hours
(alpha, beta, gamma) divide the complement; each person has a watch, a posting and, off watch, a routine of
quarters, mess and recreation. The hour is the simulation step: every crew member advances one hour (needs,
experience, memories decaying, a mood recomputed, a roll for a personal moment); the day step writes the first
officer's morale summary to the bridge log, re-evaluates desires and promotions due, and saves.

**Where they are.** The hourly step places every person in a compartment: their posting while on watch, their
quarters, the mess, the rec deck, sickbay or the shuttle bay otherwise, by routine and need. That table is what a
later round reads to put figures in the rooms as they are built, and it is what the damage model reads now to
decide who was in the compartment that was hit.

**Personal moments** roll once per crew-hour at a small rate, weighted by traits, needs and company: a friendship
formed over chess, a quarrel between a rival pair, a letter from home, a birthday in the mess, an illness, a
breakthrough at a science station, a request to see the captain. Each writes to the histories involved and some
reach the bridge log.

## 3. The ship's events as their events

The ship already produces the big events; the roster listens.

- **Combat**: red alert raises stress for all, more for the fearful; a located hit chooses its casualties from
  the people in that compartment on that watch, by name; each wound gets a recovery time by severity and the
  medical skill treating it; deaths are permanent, their friends grieve, the compartment's survivors gain the
  Survivor trait or a fear.
- **Repairs**: the engineers on the repair gain experience; a system restored under fire is a memory.
- **Science**: a scan or a survey credits the science watch; a discovery is a memory for the curious; a nebula
  passage or a giant at close quarters satisfies the desire to see it.
- **Orders**: diverting power and alert changes are the captain's doing; a long red alert without a fight wears
  the crew; a reprimand or a commendation after an engagement lands on the person it names.
- **Starbases**: alongside, shore leave restores the needs of whoever is granted it, letters arrive for the
  homesick, anyone with people at the station goes across to them, the dead are replaced from a recruit pool
  generated for that station and date (browse, sign on, refuse), and the discontented may ask to transfer.
- **Passing places and ships**: a world or a vessel within hailing distance that someone aboard has a tie to
  is a signal home for them, logged and remembered.
- **The captain on foot**: walking the ship and speaking to someone at their station is recognition; later, a
  conversation with a few lines (listen, encourage, order, promise) that the person's facets read.

## 4. Away parties and missions

An **away party** is a selection of crew (with the rule of the show: a department head may go, the captain may
go, but the ship keeps a watch). Members are marked away, their skills and facets are the party's, and the party
is the unit a mission's turns are resolved against. A **check** (`SlonkCrew::Check(skill, difficulty, party,
captain's choice)`) returns success and a margin from the best relevant skill with support from the rest, the
facets modulating (courage under threat, empathy in a parley, discipline in a procedure), traits adding or
subtracting, and stress and fatigue costing. Checks write histories: the one who made the difference is named,
the one who froze too. The mission generator of a later round builds its situations from these checks; this
round provides the interface and a dev op to run one.

## 5. The captain's hand

Console and panel: promote and demote (ranks and postings), commend and reprimand, assign watch and posting,
grant leave, pick an away party, sign on and discharge at a starbase, hold a memorial (a day of mourning that
settles grief faster), and speak with a crew member. Every one of these is a history entry for the person and
some for those who care about them, and the mood of the roster is the captain's ledger.

## 6. The crew panel

An LCARS panel (`=`; `ship ui crew`): the roster by department with rank, posting, watch, condition and mood,
filters (department, watch, in trouble, away), sort; a personnel file for the selected person (identity, skills
and facets as bars, traits, needs, desires and fears, relationships, the personal log); and the captain's actions
as the sidebar. The senior officers' files are a click from the existing boards. The HUD shows a morale line when
it is low and an away-party line while one is out.

## 7. Persistence

A game-instance subsystem (`UCrewRosterSubsystem`) owns the roster, the clock and the lost list, and saves to the
`Crew` slot after every ship's day and after every event that changes a record, in the science log's manner. A
new game generates the complement from a seed; the ship's systems component reads the roster for its counts and
its officers rather than keeping its own.

## 8. Experiences, trust and bias (the next round)

What the first round wired by hand (alerts, hits, repairs, scans, sights, starbases, the captain's actions)
becomes one interface, and the people gain the two things a captain spends a career managing: trust and bias.

**Experiences.** A mission or an encounter emits an experience: a tag (an enemy ship destroyed, a ship spared, a
first contact, a rescue, civilians lost, a parley that held, an away party in danger, an order refused, a
promise kept or broken), an intensity, who was there (people, departments, the whole ship) and where. Every
person reads it through facets, traits, fears, desires, bonds, ties and biases and comes away with their own
memory and line of history, stress, a changed need, and now and then a new trait, fear or desire. A pacifist
takes the destroyed ship hard and the spared one well; the one who feared the Klingons meets them; the one with
a sister on the colony lives its rescue; the thrill-seeker comes back from danger lit up. The existing hooks are
rewritten as emitters, so combat, scanning and the starbases are the first encounters it carries.

**Trust.** Each person holds an opinion of the captain, moved by the captain's choices as they see them (the
same experience read through the same biases: sparing a ship raises a pacifist's trust and lowers a hawk's; a
promise kept raises everyone's; a reprimand they thought unjust lowers theirs and their friends'; a memorial
raises the bereaved's). Trust is what steadies them under fire, what makes an order hold when it is hard, and
what runs out: low trust is the source of refusals, requests for transfer, insubordination events and, at the
bottom, a word to a starbase. Trust between people is the affinity the first round already keeps, and suspicion
is its negative made specific: a person suspected of something (a recruit from a border world after a sabotage,
a rival after a loss) by named others, with rumour as its carrier.

**Bias.** Each person carries dispositions toward species and toward the powers (Klingon, Romulan, Orion, the
Federation's own neighbours), from their home, their species, their history and their traits: a colonist whose
world the Klingons raided, a Vulcan's view of Tellarites, an Andorian's of Vulcans, a human who has never left
Earth. They move with experience: serving a watch beside a Vulcan who pulled them out of a burning compartment,
a captain who treats a species fairly or unfairly in front of them, a first contact that went well, a betrayal.
They have teeth: bonds form slower across a bias and quarrels come easier; a diplomacy or security check that
involves a species suffers or gains by the party's dispositions, so who goes on the away party matters; incidents
aboard (a slur in the mess, a fight on the gamma watch, a refusal to serve under someone) need handling; and off
the ship a world's or a station's crowd has its own dispositions toward the crew the captain sends ashore.

**The captain's tools** are the ones already there, now with these levers behind them: a word (listen,
encourage, order, promise), commendation and reprimand, who is posted and watched with whom, leave, memorials,
and above all the choices in missions (whom to spare, whom to trust, whom to send). The first officer reports
rifts as they open; the personnel file shows trust in the captain and the dispositions that matter; a ship-wide
cohesion reading sums it up. Squashed early and nurtured right, biases fade and the crew holds; left alone they
harden and the ship divides.

## 9. This round

Built now: the record and its generator (names by species, facets, skills by rank and department, traits, initial
bonds), the clock and the hourly and daily steps (needs, mood, stress, experience, memories, personal moments,
the placement table), the hooks into alert, hits, repairs, scans and starbases, casualties by name, recruits at a
starbase, the captain's console actions, the check interface with a dev op, the crew panel, and the save. Later
rounds: figures on their shifts in the rooms, the conversation lines, away parties in the field, the mission
generator, and the events between systems.
