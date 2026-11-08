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

`ship lift <bridge|airlock|mess|quarters|crew|deck g|sick|trans|rec|torp|eng|bay|cargo>` (the corridor for sick,
trans, rec); `ship walkto <room>` (dev: straight to a room's inner door, for tests); `ship walkto use|lift` as
before. `ship person status` logs the room underfoot.

## 7. Later

The figures on their shifts (the roster's placement read into the rooms, walking the corridors between watches);
the saucer's curved corridors and more of them (deck F's cabins onto a ring, engineering's corridor to the
torpedo bay); sliding doors that open as you come; the brig and the armoury; the observation lounge in the neck;
the captain's quarters as a place to sleep the clock forward.
