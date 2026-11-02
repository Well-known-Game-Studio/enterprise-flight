# The ship's rooms

`build_rooms.py` builds main engineering, sickbay, the shuttle bay, the transporter room and the airlock staging area of the Enterprise-A in
Blender from code and exports one static mesh per room (plus the parked shuttle on its own) for Unreal. The layouts
follow the film sets, used as reference only: The Motion Picture's intermix shaft and horizontal chamber with The
Wrath of Khan's console room; the Motion Picture ward beds in The Undiscovered Country's palette; The Final
Frontier's landing bay; the transporter room of V and VI. Geometry is authored in the game's frame (x forward, y
starboard, z up, metres, origin on the floor at the room's centre) and the room boxes match `UShipInteriorComponent`.

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python build_rooms.py -- --render --export [--room=eng,sick,bay,trans] [--views=a,b]

`--render` writes previews to `previews/`; `--export` writes `SM_Room*.fbx`. Import them headless with
`tools/rooms/import_rooms.py` while the GUI editor is closed: it lands them in `/Game/Slonk/Rooms` with their
triangles as collision (the person's Boots traces walk on them) and makes `M_Glass`. Material names are the Unreal
slot names the game binds: `Screen_<Okudagram>`, `LightStrip`, `LightSoft`, `CoreGlow`, `PadGlow`, `LatticeLit`,
`StripBlue`, `VentGlow`, `WarnRed`, `TouchPanel`, `GlassPanel`; the rest keep their colours. A raked screen is
built with a positive pitch (its top away from the viewer) so it faces whoever stands or sits at it.
