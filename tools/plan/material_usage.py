"""Let the interior's lit materials draw on instanced static meshes: the plan's corridors and rooms are instanced
boxes, and a material without the usage flag is drawn as the engine's default grey grid. Run headless while the
GUI editor is closed:

    UnrealEditor-Cmd Slonk.uproject -ExecutePythonScript=tools/plan/material_usage.py -unattended -nosplash
"""
import unreal

for path in ("/Game/Slonk/Materials/M_BridgeLit", "/Game/Slonk/Materials/M_BridgeButtons"):
    m = unreal.load_asset(path)
    if not m:
        unreal.log_warning(f"USAGE: {path} not found"); continue
    if m.get_editor_property("used_with_instanced_static_meshes"):
        unreal.log(f"USAGE already set {path}"); continue
    m.set_editor_property("used_with_instanced_static_meshes", True)
    unreal.MaterialEditingLibrary.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m, only_if_is_dirty=False)
    unreal.log(f"USAGE DONE {path}")
unreal.log("USAGE ALL DONE")
