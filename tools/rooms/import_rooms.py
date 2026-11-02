"""Import the Blender room meshes as static meshes with per-triangle collision, and make the glass material the
rooms' GlassPanel slot binds to. Run in the editor headless while the GUI editor is closed:

    UnrealEditor-Cmd Slonk.uproject -ExecutePythonScript=tools/rooms/import_rooms.py -unattended -nosplash

Meshes land in /Game/Slonk/Rooms as SM_RoomEngineering, SM_RoomSickbay, SM_RoomShuttleBay, SM_RoomBayShuttle and
SM_RoomTransporter; the person's Boots traces hit their triangles, so each room's floors, consoles and walls are
walkable and solid without separate collision boxes."""
import os
import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "blender", "rooms")
DEST = "/Game/Slonk/Rooms"
NAMES = ["SM_RoomEngineering", "SM_RoomSickbay", "SM_RoomShuttleBay", "SM_RoomBayShuttle", "SM_RoomTransporter"]

def import_mesh(name):
    opts = unreal.FbxImportUI()
    opts.import_mesh = True; opts.import_as_skeletal = False; opts.import_animations = False
    opts.import_materials = True; opts.import_textures = False
    opts.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
    sm = opts.static_mesh_import_data
    sm.combine_meshes = True
    sm.generate_lightmap_u_vs = False
    sm.auto_generate_collision = False
    sm.build_nanite = False
    sm.import_uniform_scale = 1.0
    t = unreal.AssetImportTask()
    t.filename = os.path.join(SRC, name + ".fbx"); t.destination_path = DEST; t.destination_name = name
    t.replace_existing = True; t.automated = True; t.save = True; t.options = opts
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    # An asset already at the path would still load after a failed import: trust only what this task brought in.
    imported = [str(p) for p in t.get_editor_property("imported_object_paths")]
    if not any(p.startswith(f"{DEST}/{name}") for p in imported):
        unreal.log_warning(f"ROOM IMPORT FAILED {name}: task imported {imported}")
        return
    mesh = unreal.load_asset(f"{DEST}/{name}")
    if not mesh:
        unreal.log_warning(f"ROOM IMPORT FAILED {name}: not loadable after import")
        return
    body = mesh.get_editor_property("body_setup")
    if body:
        body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    b = mesh.get_bounds()
    unreal.log(f"ROOM IMPORT DONE {name} origin {b.origin} extent {b.box_extent} slots {[str(m.material_slot_name) for m in mesh.static_materials]}")
    unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)

def make_glass():
    path = "/Game/Slonk/Materials/M_Glass"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.log("GLASS exists"); return
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_Glass", "/Game/Slonk/Materials", unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", True)
    mel = unreal.MaterialEditingLibrary
    c = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, -100)
    c.set_editor_property("constant", unreal.LinearColor(0.75, 0.82, 0.9, 1.0))
    mel.connect_material_property(c, "", unreal.MaterialProperty.MP_BASE_COLOR)
    o = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -400, 100)
    o.set_editor_property("parameter_name", "Opacity"); o.set_editor_property("default_value", 0.16)
    mel.connect_material_property(o, "", unreal.MaterialProperty.MP_OPACITY)
    r = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 250)
    r.set_editor_property("r", 0.08)
    mel.connect_material_property(r, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat, only_if_is_dirty=False)
    unreal.log("GLASS DONE")

for n in NAMES:
    import_mesh(n)
make_glass()
unreal.log("ROOMS ALL DONE")
