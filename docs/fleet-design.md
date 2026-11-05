# The fleet

The other Starfleet ships as crewed ships with lives of their own, so that transfers mean something, the crew's
people aboard them are real names, and the ships flying about and between the stars are doing something we can
be asked to help with or choose to join. Code: `unreal/Source/Slonk/Fleet.h/.cpp`, with hooks in `Missions.cpp`,
`CrewRoster.cpp`, `SpaceVessel.cpp` and the missions panel. The crew's side is in `crew-design.md`, the missions'
in `missions-design.md`.

## 1. The registry

Every `USS` in the world gets a record the first time it is seen (a scan every few seconds binds names to
actors): name, class, home station (the nearest), star system, a captain and seven heads of department made from
the same name pools as our crew (`UCrewRosterSubsystem::MakeOutsider`), a complement, morale, condition, and their
regard for us. Records live in the "Fleet" save slot; the actor is only a pointer bound by name, so a ship in a
system we have left still has her record and her clock. Ship names are now unique across the galaxy (a longer
table and a check against the spawned ships), since the name is the key everywhere.

## 2. Their missions

On the ship's clock each ship lives an hour at a time (the same clock as the crew, so dev time moves them too):

- **On station** at home, repairing and resting, until her next mission comes up (three to ten hours).
- **Under way**: a survey, a supply run, talks, a medical call, a patrol or a distress call, at a planet or
  station of her own system (one to three hours out) or, one time in four, at another star (six to fourteen hours).
  In her own system the actor actually warps to the place and patrols about it; for another star she goes out of
  sight (`AProceduralStarship::SetAway`) and reappears at home when she is back. A ship away is not "nearby".
- **At work** for three to ten hours. Each hour there is a small chance of **trouble**, more on patrols and
  rescues and for a ship in poor condition: an engineering failure, a medical crisis, hostiles, or a party pinned
  down. Trouble stops the work, costs a casualty now and then, and goes out as a **request for support** on
  subspace. She works at it herself too (a chance each hour by her own best hand at it), and after a day without
  help it is beyond her: the mission fails, the ship is hurt, people die, and she remembers that no help came.
- When the work is done it comes off or not by her own people's skill, her condition and morale, and whether we
  lent a hand; then she **returns** home.

## 3. Our part

- **Support** (`EMissionKind::Support`, source Fleet): her request becomes an active mission on our board with a
  day to answer it, targeting the ship herself (Set Course finds her when she is in the system). One stage, two
  options by the kind of trouble: a damage-control party or a tow and a power lead; the medical staff across or
  supplies and protocols; drive the hostiles off or talk them down; a security party across or a transporter lock
  through the interference. A success ends her trouble, raises her regard for us a good deal and counts as a hand
  lent; a failure, an abandonment or a lapse means no help came.
- **Lending a hand** (`EMissionKind::Assist`): when we are in hailing range of a ship at work with no trouble, an
  offer appears on the board (once per mission of hers): a share of her work by its kind (the science watch, the
  doctors, a joint sweep, a seat at the talks, a share of the survivors, stores across), or an exchange of
  officers for the day. Success halves her remaining work and raises her regard; her own result then counts our
  hand. If she finishes before we act, the offer lapses quietly.
- All of it goes through the crew's checks and experiences as any mission does, with the group "Federation" and
  the place her name; her regard for us is a number on her record, -1 to 1, that gates whether she will give us
  people.

## 4. People between ships

- **Ties become people.** A crew member's family or friend "aboard the USS Hood" (the roster's ties, now bound to
  the real names) becomes a named person in the Hood's record, with a rank, department and posting, shown in the
  crew file ("serving as Lt. Ana Dahl, chief, helm; the ship is under way to Sirius III") and in the ship's file
  ("People we know aboard").
- **News travels.** Trouble aboard a ship, its end, its loss, all reach everyone with a tie aboard her as memories
  and stress (`NoteShipNews`); a notable killed in her trouble, or the whole ship lost with all hands, is a
  bereavement by name (`TieLost`), reported on the bridge.
- **Transfers out** (`ship fleet transfer <name> <ship>`, or `ship crew transfer`): one of ours, fit and not a
  head, with the ship in hailing range, goes to her: the record stays Transferred, their close friends keep a tie
  to them aboard her, the ship's record gains them as a notable "formerly of the Enterprise", and her regard
  rises a little.
- **Transfers in** (`ship fleet request <ship> <name>`): one of her notables comes to us, if the person has family
  or a friend aboard (a reunion: the tie becomes a bond, a lasting memory on both sides) or her captain thinks well
  enough of us (regard at least +20); never her senior staff. Her regard drops a little for a hand lost.

## 5. The surface

- The missions panel's fourth view, Show: Fleet: every ship by state and regard, those in range first, and the
  selected ship's file: her captain and senior staff, the people we know aboard, her mission and trouble, her log.
  Set Course on a ship.
- The bridge log: her signals when she is in range (under way, done, home), her call for support always, her
  loss always.
- Console: `ship fleet list | show <ship> | transfer <name> <ship> | request <ship> <name> | reset`, and for testing
  `trouble <ship> [kind]` (trouble now, on a mission in her own system), `mission <ship>` (a mission now, the hull
  goes), `work <ship>` (at work now, the hull stays where it is), `summon <ship>` (her hull warps to our bow, her
  mission recalled); `ship crew transfer <name> <ship>`; `ship ui crew <name>` opens a crew file.

## 6. Later

A ship's own crew lists beyond the senior staff; ships in other systems whose actors we never see, with records
seeded from the system; convoys and rendezvous (two ships on one mission); their requests reaching us by relay
when we are far, with the standing cost of being the ship that never comes; a captain's personality in how they
ask and what they remember; the fleet's regard feeding Starfleet's; officers exchanged for the day as a real
away party once the interiors and figures exist.
