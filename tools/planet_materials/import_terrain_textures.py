"""Import the baked terrain texture sets (tools/planet_materials/bake_terrain_textures.py) into /Game/Slonk/Terrain.

Runs inside the editor's Python, headless, with the GUI editor closed:
    UnrealEditor-Cmd unreal/Slonk.uproject -ExecutePythonScript="tools/planet_materials/import_terrain_textures.py <src dir>" -unattended
T_<Set>_BC is sRGB colour with the height in its alpha; T_<Set>_NR is linear BC7 (normal xy as baked, +Y = +V,
the terrain material builds its own tangent frame; B occlusion, A roughness).
"""
import unreal, os, sys, glob

SRC = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("TERRAIN_TEX_SRC", "")
DEST = "/Game/Slonk/Terrain"
tools = unreal.AssetToolsHelpers.get_asset_tools()
paths = sorted(glob.glob(os.path.join(SRC, "T_*_*.png")))
tasks = []
for path in paths:
    t = unreal.AssetImportTask()
    t.filename = path
    t.destination_path = DEST
    t.destination_name = os.path.splitext(os.path.basename(path))[0]
    t.replace_existing = True
    t.automated = True
    t.save = False
    tasks.append(t)
tools.import_asset_tasks(tasks)
saved = 0
for path in paths:
    name = os.path.splitext(os.path.basename(path))[0]
    tex = unreal.load_asset(f"{DEST}/{name}")
    if not tex:
        unreal.log_warning("not imported " + name); continue
    kind = name.rsplit("_", 1)[1]
    if kind == "NR":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_BC7)
        tex.set_editor_property("srgb", False)
    else:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        tex.set_editor_property("srgb", True)
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
    tex.set_editor_property("address_x", unreal.TextureAddress.TA_WRAP)
    tex.set_editor_property("address_y", unreal.TextureAddress.TA_WRAP)
    tex.set_editor_property("never_stream", False)
    unreal.EditorAssetLibrary.save_loaded_asset(tex, only_if_is_dirty=False)
    saved += 1
    unreal.log(f"saved {name} {tex.blueprint_get_size_x()}x{tex.blueprint_get_size_y()}")
unreal.log(f"TERRAIN TEXTURE IMPORT DONE saved {saved} of {len(paths)}")
