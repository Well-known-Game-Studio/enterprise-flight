# The ship's interiors

What of the ship can be walked, how the rooms join, and where the crew's day happens. The first rooms (the
bridge, main engineering, sickbay, the shuttle bay, the transporter room, the airlock's staging area) came in the
captain rounds as lift stops: one room shown at a time, the turbolift a fade between them. This round builds the
interior out into the places the crew's routine names (`crew-design.md` §2: posting, quarters, the mess, the rec
deck) and joins some of them with corridors, so the next round can put the figures on their shifts in them. Code:
`unreal/Source/Slonk/ShipInterior.h/.cpp` (the rooms), `PersonPawn.cpp` (walking between them), `LcarsPanels.cpp`
(the turbolift), `CrewRoster.cpp` (where people are); sets: `blender/rooms/build_rooms.py`.

## 1. The rooms

Every room is a box in the ship's frame at the place the compartment map (`ShipCompartments.cpp`, after the
Strategic Design deck plans) gives it, a Blender set modelled after the film sets (the primitives stand in while a
set's mesh is missing), a deck letter, where the lift lets you out and one thing to use. The new ones:

| Room | Deck | After | Use |
| --- | --- | --- | --- |
| **Deck G corridor** | G | TUC's corridor outside the transporter room (the lit schematic panel at its end) | the lift |
| **Rec deck** | G | TMP's rec deck: two levels, the gallery, the big screen forward | the screen: the star chart |
| **Officers' mess** | C | TUC's dinner: the long table, round ceiling lamps, slatted walls | the table: the crew panel |
| **Captain's quarters** | D | TWOK's cabin: the angled window, the sunken seat, the desk viewer, the shelves | the desk: the missions panel (Starfleet's inbox) |
| **Crew quarters** | F | a cabin corridor of the saucer, doors both sides, one cabin open at the end | the bunk |
| **Torpedo bay** | M | TWOK/TUC: the loading track to the two tubes, the racks, the catwalk | the control console: the tactical board |
| **Cargo bay** | S | TMP: container stacks, the gantry, the cargo transporter | the manifest console: the master systems display |

Deck letters follow the plans: A bridge, B airlock, C officers' lounge, D senior officers' quarters, F enlisted
quarters, G sickbay / transporters / rec deck, M the torpedo bay in the neck, O engineering, Q the hangar, S cargo.

## 2. Corridors and doors

A corridor is a room like any other with the lift at one end and the doors of the rooms off it open: sickbay's
and the transporter room's lift doors become doorways onto the **deck G corridor** (an L: athwartships past
sickbay's aft door to the lift alcove, then forward along the port side past the transporter room to the lit
panel), and the rec deck opens off its aft wall. The person walks through; the room they are in follows their
feet (`UShipInteriorComponent::RoomAt`, the current room kept while they are still inside its box), and a room is
shown with its **neighbours** (the rooms whose boxes touch it, found once when the rooms are built), so a doorway
always shows the room beyond and the walls on both sides of it are solid. Rooms not adjoining anything stay lift
stops with their burgundy doors shut: the lift is still a fade, since the shafts are not modelled.

The turbolift's stops are the rooms with a lift door: the corridor stands for sickbay, the transporter room and
the rec deck on its panel ("Deck G"), and asking the lift for any of the three (`ship lift sick`) puts you out at
the corridor's alcove. The HUD's aboard block names the room underfoot.

## 3. Where the crew are

The roster's hourly placement (`UCrewRosterSubsystem::Place`) now names the new rooms: the mess hour is in the
**officers' mess** for officers and on the **rec deck** for the rest (the galley serves there), the off-watch hours
on the rec deck unless the person is bookish or a loner (their quarters), sleep in **officers'** or **crew
quarters**. The officers' mess and the rec deck join the compartment map, so a hit there finds the people in it and
the master systems display draws them. The figures themselves come next round; this one gives them rooms to be in.

## 4. The shuttle

The bay's parked shuttle (and the flying one, which is the same mesh) is drawn after the Final Frontier's Type-4
Galileo: the long white fuselage with its lower chines, the tapered nose with the windscreen on its slope, the
raised roof deck with its spine, the flat sensor and drive pods slung low beside the hull on short pylons, the
bevelled aft door, the red stripe. Reference: the film, Memory Beta's profile, Tadeo D'Oria's renders (layout
only; nothing third-party enters the repo).

## 5. Screens

New Okudagram textures (`tools/okudagram/gen_okuda.py`): **TorpedoBay** (the blue cross-section and rack
inventory of the torpedo bay display), **Deflector** (the shield status: the ship's plan inside the shield
envelope, the power bar) and **DeckConfig** (the red dot-matrix profile with the deck access readout) for the
corridor's panel, the torpedo bay's consoles and the cargo bay's gantry.

## 6. Console

`ship lift <bridge|airlock|mess|quarters|crew|sick|trans|rec|torp|eng|bay|cargo>` or `ship lift deck <letter>`
(the nearest stop on that deck; §7); `ship walkto <room>` (dev: straight to a room's inner door, for tests);
`ship walkto use|lift` as before. `ship person status` logs the room underfoot. F (use) opens the turbolift only
at a lift door: at a doorway onto the corridor it does nothing, and the hint names the doorway instead.

## 7. The ship's plan

The decks A to U as data at the ship's real size (`ShipPlan.h/.cpp`), drawn round the signature rooms from
instanced boxes as the walker comes near, with a turbolift car that travels between the stops. Nothing is
compressed: the saucer's rings are as long as the plans make them (the outer ring of deck F is 377 m round), and
if the walks prove too long the radii and bay counts are tuned afterwards.

**The decks.** Measured off the hull the game flies (`tools/plan/measure_hull.py`): the saucer's plate is 11.4 m
thick from z 1144 to 2200 out to its rim, the superstructure steps up to the bridge module, the lower dome falls
to the sensor dome; the neck is 12 m wide and 21 m long; the secondary hull runs 28 m deep. The floors sit on three
grids, each 2.5 m of room with the structure between: the saucer at 2.8 m from the bridge module's floor (A 31.3 m
over the hull's centre, B, C, D and E in the superstructure to r 32 m, F the plate deck out to the rim, G and H
under the plate in the lower dome, I and J lower still), the dorsal at 2.8 m from the torpedo bay set (its I to M), the secondary hull at 3.2 m
from the hangar deck (N to U). The bridge set now stands in the bridge module at the top of the saucer, the
airlock's staging area to port of deck B's corridor, the officers' mess inside deck C's lobby ring, the captain's
cabin outside deck D's, the cargo bay forward on deck S. Each deck carries the plans' headline (the turbolift
panel shows it) and its hull limit (a radius about the saucer's centre, or a box), inside which the generator
keeps every room. The saucer's skin is a measured section too (`SaucerSkin`: the underside and the top by
radius, taken off the mesh's forward half where nothing but saucer lies): a saucer room's floor must lie above
the one and its ceiling below the other, and the plan test holds every saucer bay and room to it. That section
is what puts only F in the plate: the mesh's rim is 6.1 m thick and the plate inboard of it 5 m, one 2.8 m deck's
worth, where the published plans draw F, G and H across it (the refit's rim is an 8 m ring of two decks in the
references). G's and H's rings once hung a metre and more below the underside.

**Corridors.** The kit is the refit section the deck G set uses (`corridor_leg`: the grated deck, the lit lower
panels leaning out to the waist, the band, the pale upper panels leaning in to the narrow dark ceiling), built
in bays of 2.24 m between frames. A ring is a polygon of bays on a radius, each turning 360/N degrees at a frame
whose rib widens to cover the wedge the turn opens; a spoke is a straight between two rings, opening a bay of
each (a junction is a bay with no wall on that side, the spoke's floor beginning at the wall plane); an arc of
bays turning five degrees each swings the deck G set's opened port end round to run out radially to the inner
ring. Deck F: an outer ring at r 60 m (168 bays) and an inner ring at r 47 m (132), eight short spokes between
them, three long spokes from the inner ring in toward the core, and the L of the deck G set generated on F (so
the aft lift shaft lines up with G's) with its arc to the inner ring. Deck G: the set, its opened port end run on
as a straight passage to r 32 m with the labs and the security complex either side (the dome's skin at this floor
allows no ring the set's end could turn onto). Decks C, D, E, H and I: a lobby ring at r 10 m round the core (the
main shaft's door aft), E with a second ring at r 22 m and four spokes.
Deck B: a straight along the bridge module. The dorsal's decks I to K: a fore-and-aft corridor down the neck
(L and M are the torpedo bay's set, two decks tall). The secondary hull: a trunk corridor on the centre line each
deck with the dorsal shaft's door in its starboard wall. The sets stand two decks tall, so the trunk jogs to
starboard round main engineering on N and O and round the cargo bay on R and S (a forward leg, a cross passage
from its last bay, the bypass, a cross passage back to the aft leg, every junction a T on a bay's middle), ends
at the cargo bay's forward wall on Q and at the hangar's on P.

**Rooms.** The signature rooms keep their sets. The rest are generated off the corridors from the plans' lists:
a room takes whole bays of a passage (two for quarters, three for labs, up to six for a lounge), its frontage
becoming flat wall with the doorway in the middle bay, party walls radial on a ring (the rooms are wedges) and a
back wall at the room's depth of 4.8 m; a room that would meet a set's box or the hull is left out, the bays
staying panelled. Twelve kits furnish them by kind: quarters (the bunk, the desk and viewer, the locker), labs
(benches and screens, the cabinet), stores (shelving and crates), workshops, offices, lounges (loungers before a
screen), transporter rooms (the round platform and its pads), brig cells (a force field in the doorway),
armouries, holds (container stacks), hull locks (the round hatch), equipment rooms (cabinets, a lit conduit).
Deck F holds the enlisted quarters by the hundred on both sides of its rings; G the labs, the transporter rooms,
the junior officers' quarters, the holds and the security complex with its eight cells; H the hull locks, phaser
rooms, workshops and physics labs; the secondary hull the plans' engineering rooms, hangar gallery, botanical
garden and pool, the reactor rooms on U. Every room is a place of its own, named ("Enlisted quarters F-37"),
shown on the HUD as the feet cross its sill.

**Zones.** A stretch of corridor is drawn in zones of twelve bays (27 m; a ring of 168 bays is fourteen
zones, a spoke one) with the rooms whose doorways are in those bays, each a set of instanced boxes (one
component per material, plus an unseen set that blocks the boots), built the first time it comes within 60 m of
the walker on their deck and shown while it is; solid within 14 m, and the zone under the feet always. A pool of
point lights goes to the nearest lamps. Zones away from the walker are hidden again, so the hull's walkers and
the airlock trace meet no wall that is not around them. A walker's place is tested against the bays as they lie
(oriented rectangles), not boxes round them: on the diagonal of a ring an axis box would hold them a metre past
the wall, and the car would open late.

**The turbolift.** The stops are a list: the plan's own lift doors (the main shaft aft of the lobby rings on B
to E, H and I, the aft shaft at the core corridor's end on F, four rim shafts beside the diagonal junctions of
the outer ring, the dorsal shaft from the plate deck down the neck to deck U) and the sets' shut
lift doors. F at one of the plan's doors brings the car (a round car with doors of its own behind the landing's,
both pairs opening onto the corridor; at a set's stop only the car's close its gap); step
in and, once you are clear of the doors' sensor, the deck buttons come up; choose, and the doors shut, the car travels (8 m/s up and down, 15 m/s along,
the decks called off on the screen as it passes them), the car and whoever still stands in it arrive at the stop
and the doors open. The doors have a sensor: step back into the doorway, or out, while they shut and they open
again and the ride is off; a rider who is not in the car when it arrives is left where they are. A car called
to another door while it stands open goes there and opens, and the doors it stood open at shut behind it. The
bays either side of a lift door take no room (a cabin's party wall would run through the car, which stands
127 cm round and 140 behind the wall). A set's shut door is a fade's worth of walk into or
out of the car. `ship lift deck <letter>` goes to the deck's nearest corridor door (a set's door only where the
deck has none; `dorsal k` for the neck's), `ship lift stop <n>`; `ship walkto deck <letter>` to that door, `ship
walkto <a word of a place's name>`.

**Tests.** `Slonk.Interior.Plan` builds the plan headless and checks the decks, that every room keeps inside the
hull and clear of the sets and of each other, that no passage runs through a set at any height the set reaches
or crosses another passage or a room (junctions abut at the wall plane), that no room reaches into the car behind
a corridor door, that the bypasses open at both ends and link up, that the shafts exist and the dorsal one spans the ship, and that rides take their time.

## 8. Crew on shifts

The roster places every member of the crew each hour of the ship's day (`UCrewRosterSubsystem::Place`, §3):
their posting on watch, the mess, their quarters, the rec deck, sickbay. `UCrewWalkersComponent` on the ship reads
that placement into the plan and walks them there. Each person is a walker (`FCrewWalker`): where they stand in
the ship's frame, the place they are in, the one they are bound for and the route between, simulated for the
whole complement all the time, which costs a position along a polyline each. The roster's compartment names
map to places by a table (`PlaceFor`): a set where one stands for the compartment (sickbay, main engineering, the
shuttle bay, the cargo bay, the torpedo bay, the rec deck, the officers' mess, the crew quarters), and the plan's
rooms by name otherwise (the computer core, phaser control, the security complex, the reactor rooms, environmental
control, the quarters by rank...), a compartment with many rooms spreading its people over them by a hash of who
they are, so everyone has a cabin and a lab of their own. The nacelles, the shore and an away team are nowhere on
the plan, and nobody is drawn standing on the bridge, where the seated officers already are.

**Routes.** `FPlanRouter` (ShipPlan.h) makes a graph of the plan: every bay a node, consecutive bays and the
junctions between linked passages the edges (an open end meeting an open side bay at the wall plane, or two
open ends meeting), Dijkstra between them. A route (`FPlanRoute`) is legs on foot (points on the floor, bay
middle to bay middle, through a room's doorway at either end) and rides in a lift between two stops, which take
the car's travel time. On one deck the walk is on foot where the passages join; between decks it goes by the
nearest walk-in stops at either end; a set is left and entered by its own shut door (its stop, not walked into),
and the three rooms off deck G's corridor by that corridor's door: out through the room's doorway onto the
corridor's centre line, along the leg and round its corner (two rooms off the same door walk between them, no
ride). Deck G's generated passage has no lift of its own: its rooms walk the passage to its open end on the
authored corridor and ride from that corridor's door, or walk on through the corridor to the rooms off it. A
ring's seam is an edge like any other. In a set the crew stand on the clear floor just inside the door, a pace
to three in, where no furniture stands between the door and them. The rides are not the player's car: the walker is in a lift
for the leg's seconds and steps out of the destination's door, so the car the player rides is never
commandeered. A walker on its way finishes the route before taking a new place (a route from mid-passage would
start at the old room's door).

**Figures.** The walkers within the interior's live radius of the player on foot, on the same deck, are given
figures from a pool of `MaxFigures` (24), nearest first: `ACrewFigure`, the mannequin with the person's
locomotion blend (standing or walking by the speed), dressed by the crew material in the maroon jacket with a
skin and hair of their own, a cap for hair, no collision, posed from the walker's feet in the craft's frame every
frame. With the captain in the chair there are no figures; the walk goes on regardless, so stepping out of the
lift finds the shift where it should be. A watch change moves a third of the ship at once: the component replans
two dozen routes a half-second, and settles everyone straight to their places when a session starts or the
clock is jumped (`ship crew walkers settle`). Console: `ship crew walkers` (counts and the nearest ten, with where
they are bound), `ship crew walkers <id>` (one's route), `ship crew figures on|off`. The test
`Slonk.Crew.Walkers` routes along a deck, between decks and from a set's door, and has every room on a deck with
stops reach one.

## 9. Later

Sliding doors that open as you come; the sets' lift doors as doorways onto the plan's corridors (the
mess, the cabin, the crew quarters, the torpedo bay, engineering, the bay and the cargo bay are reached by lift
only); the brig and the armoury as rooms with a use; the observation lounge in the neck; the captain's quarters
as a place to sleep the clock forward; tuning the rings' radii and bay counts if the walks prove too long.
