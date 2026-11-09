# The ship's rooms

`build_rooms.py` builds the rooms of the Enterprise-A in Blender from code and exports one static mesh per room
(plus the parked shuttle on its own) for Unreal: main engineering, sickbay, the shuttle bay, the transporter room,
the airlock staging area, deck G's corridor, the rec deck, the officers' mess, the captain's quarters, the crew
quarters, the torpedo bay and the cargo bay. The layouts follow the film sets, used as reference only: The Motion
Picture's intermix shaft and horizontal chamber with The Wrath of Khan's console room; the Motion Picture ward beds
in The Undiscovered Country's palette; The Final Frontier's landing bay and its Type-4 Galileo; the transporter
room, corridor and officers' mess of VI; the rec deck and cargo bay of The Motion Picture; the captain's cabin of
II; the torpedo bay of II and VI. Geometry is authored in the game's frame (x forward, y starboard, z up, metres,
origin on the floor at the room's centre; a corridor's at its first leg's centre line) and the room boxes match
`UShipInteriorComponent`. Rooms off a corridor have open doorways (`doorway`) where their lift doors were: the shell
carries the opening (`shell(openings=...)`) and the corridor's wall stands back to back with the room's. The
corridor's athwartships leg is open at its port end: the ship's plan (`ShipPlan.cpp`) carries the corridor on from
there in the same section (`corridor_leg`'s constants are the kit's), so the set's frame and the generated one meet
flush.

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python build_rooms.py -- --render --export [--room=eng,sick,bay,trans,lock,corr,rec,mess,cabin,crew,torp,cargo] [--views=a,b]

`--render` writes previews to `previews/`; `--export` writes `SM_Room*.fbx`. Import them headless with
`tools/rooms/import_rooms.py` while the GUI editor is closed: it lands them in `/Game/Slonk/Rooms` with their
triangles as collision (the person's Boots traces walk on them) and makes `M_Glass`. Material names are the Unreal
slot names the game binds: `Screen_<Okudagram>`, `LightStrip`, `LightSoft`, `CoreGlow`, `PadGlow`, `LatticeLit`,
`StripBlue`, `VentGlow`, `WarnRed`, `TouchPanel`, `GlassPanel`; the rest keep their colours. A raked screen is
built with a positive pitch (its top away from the viewer) so it faces whoever stands or sits at it.
