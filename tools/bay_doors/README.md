# The shuttlebay's clamshell door

The 1701-A hull's fantail door is cut out of the hull model itself, so the closed door is the hull's own painted
surface and the open gores carry its texture.

1. Unpack `blender/uss-enterprise-ncc-1701-a/source/Major Stress Enterprise-A.7z` somewhere (`7z x`), then
   `python3 tools/bay_doors/cut_doors.py <dir>/Enterprise-A.obj`: picks the door faces (the inner half cone standing
   in the stern recess, material Constitution_3: 828 cm radius at its base ring under the deck fan, 280 cm at its
   top, about a vertical axis at pawn (-6330, 0), the deck at -1948), trims them at the door's top edge, splits them
   into eight gores of 22.5° by azimuth and writes `gores_B.obj` (plus `gores_H.obj` in the model's own frame for
   checks). Each gore has an inner skin (the same triangles wound inward, on the DoorInner slot) so the closed door reads as a wall from inside the bay; an offset skin left a slit open at the silhouette. The outermost gores run on to the cone's tail behind the axis plane, up to the bay's junction wall.
   The script also writes `SM_SternWall`: the model's sloped stern bulkhead beside the door stops 2.6 m above the
   fantail deck and the hull's side skin ends in a diagonal cut just aft of x -6185, so from the quarter you looked
   under the bulkhead into the hollow where the bay set stands. A wall on each side at x -6175 (the stern wall),
   just inside that cut, from the deck up behind the bulkhead's bottom edge and from the bay wall out to 4 cm
   inside the skin, closes the slot; it maps to a plain white texel of the stern texture and takes the hull's
   two-sided masked material. The bay set ends at the same plane (`JUNCTION` in `blender/rooms/build_rooms.py`), so
   nothing of the set stands in the recess.
2. `blender --background --python tools/bay_doors/export_doors.py` → `blender/ship/bay_doors.fbx`, each gore's
   origin on the axis so a yaw about it is the swing.
3. Headless, with the editor closed: `UnrealEditor-Cmd unreal/Slonk.uproject -ExecutePythonScript=tools/bay_doors/import_doors.py -unattended -nosplash`
   → `/Game/Slonk/Ship/SM_BayDoor1..8` (outer skin on `Constitution_3`, inner on `DoorInner`) and `SM_SternWall` (on `Constitution_3_Hull`).

The hull keeps a hole where the door was: `/Game/Enterprise/M_HullMasked` is a copy of the Interchange phong master
with a masked blend and a Custom node that drops every pixel inside the door cone's radius (mesh space = pawn
space: x < -6100, -1945 < z < -1243, aft of x -6530, r about the axis < cone + 6 cm, where cone blends the rings
measured off the mesh, 828 - 0.24 deg at the base and 280 - 0.27 deg at the top for deg the azimuth from astern,
fitted within -2..+5 cm; the margin must exceed that worst error or slivers of the hull's cone survive along its
triangle edges. Forward of the stern wall at x -6175 the margin is -30 cm, so the cone's tail and the
filler ribs there go but the stern's own skin stays), which takes the cone itself and the model's filler behind it and leaves the recess walls, the flanking
panels and the deck fan; `Constitution_3_Hull` is the stern instance re-parented to it (`scratch-backup/hullmask.py`
builds the node). `AStarshipPawn::SetupBayDoors` swaps it into the Constitution_3 slot and hangs a
`UBayDoorsComponent` on the hull at the axis: opening swings each pair of gores toward its side by the width of
the gores outside it, so all eight nest in the outermost pair's place with a shrinking radial scale; closed, the gores stand 2.5 % proud
of the hull cone so they cover the hole's margin band. The bay set's
throat (`blender/rooms/build_rooms.py`, `bay_throat`) lines the inside of the recess up to the door.
