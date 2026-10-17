"""Import the Blender bridge FBX as one static mesh (run in the editor with -ExecutePythonScript)."""
import unreal
SRC = "/Users/bob/well-known-game-studio/slonk-the-game/blender/bridge-st6/SM_BridgeRefit.fbx"
DEST = "/Game/Slonk/Bridge"
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
t.filename = SRC; t.destination_path = DEST; t.destination_name = "SM_BridgeRefit"
t.replace_existing = True; t.automated = True; t.save = True; t.options = opts
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
mesh = unreal.load_asset(f"{DEST}/SM_BridgeRefit")
if mesh:
    b = mesh.get_bounds()
    unreal.log(f"BRIDGE IMPORT DONE origin {b.origin} extent {b.box_extent} slots {[str(m.material_slot_name) for m in mesh.static_materials]}")
    unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
else:
    unreal.log_warning("BRIDGE IMPORT FAILED")
