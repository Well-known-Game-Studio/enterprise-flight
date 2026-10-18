"""Import the Okudagram screen textures (run in the editor with -ExecutePythonScript)."""
import unreal, os, glob
# The generated PNGs: OKUDA_SRC in the environment, else the Blender set's okuda_png folder (see blender/bridge-st6).
SRC = os.environ.get("OKUDA_SRC", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "blender", "bridge-st6", "okuda_png"))
DEST = "/Game/Slonk/Bridge"
tools = unreal.AssetToolsHelpers.get_asset_tools()
tasks = []
names = []
for path in sorted(glob.glob(os.path.join(SRC, "T_Okuda_*.png"))):
    name = os.path.splitext(os.path.basename(path))[0]
    names.append(name)
    t = unreal.AssetImportTask(); t.filename = path; t.destination_path = DEST; t.destination_name = name
    t.replace_existing = True; t.automated = True; t.save = False
    tasks.append(t)
tools.import_asset_tasks(tasks)
n = 0
for name in names:
    tex = unreal.load_asset(f"{DEST}/{name}")
    if not tex:
        unreal.log_warning("not imported " + name); continue
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    tex.set_editor_property("srgb", True)
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    tex.set_editor_property("never_stream", True)
    unreal.EditorAssetLibrary.save_loaded_asset(tex, only_if_is_dirty=False); n += 1
unreal.log(f"OKUDA IMPORT DONE {n} of {len(names)}")
