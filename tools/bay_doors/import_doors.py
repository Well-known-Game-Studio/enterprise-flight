"""Import the clamshell door gores (blender/ship/bay_doors.fbx) as SM_BayDoor1..8 in /Game/Slonk/Ship, on the
hull's own stern material. Run in the editor headless while the GUI editor is closed:

    UnrealEditor-Cmd Slonk.uproject -ExecutePythonScript=tools/bay_doors/import_doors.py -unattended -nosplash
"""
import os
import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "blender", "ship", "bay_doors.fbx")
DEST = "/Game/Slonk/Ship"
STERN = "/Game/Enterprise/Constitution_3"

opts = unreal.FbxImportUI()
opts.import_mesh = True; opts.import_as_skeletal = False; opts.import_animations = False
opts.import_materials = True; opts.import_textures = False
opts.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
sm = opts.static_mesh_import_data
sm.combine_meshes = False
sm.generate_lightmap_u_vs = False
sm.auto_generate_collision = False
sm.build_nanite = False
sm.import_uniform_scale = 1.0
t = unreal.AssetImportTask()
t.filename = SRC; t.destination_path = DEST
t.replace_existing = True; t.automated = True; t.save = True; t.options = opts
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
imported = [str(p) for p in t.get_editor_property("imported_object_paths")]
unreal.log(f"DOORS imported {imported}")
stern = unreal.load_asset(STERN)
inner = unreal.load_asset(f"{DEST}/DoorInner") or unreal.load_asset("/Game/Slonk/Materials/M_PanelDark")
for k in range(1, 9):
    mesh = unreal.load_asset(f"{DEST}/SM_BayDoor{k}")
    if not mesh:
        unreal.log_warning(f"DOORS IMPORT FAILED SM_BayDoor{k}"); continue
    # The FBX brings its own Constitution_3 and DoorInner materials into the folder; the outer skin is pointed at
    # the hull's real stern instance instead (the struct array has to be rebuilt element by element to stick).
    mats = list(mesh.static_materials)
    for i in range(len(mats)):
        m = mats[i]; name = str(m.material_slot_name)
        if name == "Constitution_3" and stern: m.set_editor_property("material_interface", stern)
        elif name == "DoorInner" and inner: m.set_editor_property("material_interface", inner)
        mats[i] = m
    mesh.set_editor_property("static_materials", mats)
    stray = f"{DEST}/Constitution_3"
    if unreal.EditorAssetLibrary.does_asset_exist(stray) and k == 8: unreal.EditorAssetLibrary.delete_asset(stray)
    b = mesh.get_bounds()
    unreal.log(f"DOORS DONE SM_BayDoor{k} origin {b.origin} extent {b.box_extent} slots {[str(m.material_slot_name) for m in mesh.static_materials]}")
    unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
wall = unreal.load_asset(f"{DEST}/SM_SternWall")
hull = unreal.load_asset("/Game/Enterprise/Constitution_3_Hull")
if wall and hull:
    mats = list(wall.static_materials)
    for i in range(len(mats)):
        m = mats[i]
        if str(m.material_slot_name) == "SternWall": m.set_editor_property("material_interface", hull)
        mats[i] = m
    wall.set_editor_property("static_materials", mats)
    unreal.EditorAssetLibrary.save_loaded_asset(wall, only_if_is_dirty=False)
    stray = f"{DEST}/SternWall"
    if unreal.EditorAssetLibrary.does_asset_exist(stray): unreal.EditorAssetLibrary.delete_asset(stray)
    b = wall.get_bounds()
    unreal.log(f"DOORS DONE SM_SternWall origin {b.origin} extent {b.box_extent} slots {[str(m.material_slot_name) for m in wall.static_materials]}")
else:
    unreal.log_warning(f"DOORS: stern wall {'missing' if not wall else 'has no hull material'}")
unreal.log("DOORS ALL DONE")
