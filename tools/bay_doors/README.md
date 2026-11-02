# The shuttlebay's clamshell door

The 1701-A hull's fantail door is cut out of the hull model itself, so the closed door is the hull's own painted
surface and the open gores carry its texture.

1. Unpack `blender/uss-enterprise-ncc-1701-a/source/Major Stress Enterprise-A.7z` somewhere (`7z x`), then
   `python3 tools/bay_doors/cut_doors.py <dir>/Enterprise-A.obj`: picks the door faces (the recessed cone at the
   stern, material Constitution_3), clips them along the painted seam lines between the door and the flanking
   panels, splits them into eight gores by azimuth about the door's vertical axis (pawn (-6330, 0) cm, the deck
   at -1948) and writes `gores_B.obj` (plus `gores_H.obj` in the model's own frame for checks). Each gore has an
   inner skin 6 cm inside the hull surface so the closed door reads as a wall from inside the bay.
2. `blender --background --python tools/bay_doors/export_doors.py` → `blender/ship/bay_doors.fbx`, each gore's
   origin on the axis so a yaw about it is the swing.
3. Headless, with the editor closed: `UnrealEditor-Cmd unreal/Slonk.uproject -ExecutePythonScript=tools/bay_doors/import_doors.py -unattended -nosplash`
   → `/Game/Slonk/Ship/SM_BayDoor1..8`, outer skin on `Constitution_3`, inner on `DoorInner`.

The hull keeps a hole where the door was: `/Game/Enterprise/M_HullMasked` is a copy of the Interchange phong master
with a masked blend and a Custom node that drops the pixels inside the door's trapezoid (mesh space = pawn space:
x < -6100, -1945 < z < -1243, |y| < 346 + (-1270 - z) * 0.754 - 6), and `Constitution_3_Hull` is the stern
instance re-parented to it; `AStarshipPawn::SetupBayDoors` swaps it into the Constitution_3 slot and hangs a
`UBayDoorsComponent` on the hull at the axis. The bay set's throat (`blender/rooms/build_rooms.py`, `bay_throat`)
lines the inside of the cap up to the door.
