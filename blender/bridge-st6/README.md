# Enterprise-A bridge (Star Trek VI)

`build_bridge.py` builds the bridge set in Blender from code, after Donny Versiga's renders of the 2293 bridge, and
exports `SM_BridgeRefit.fbx` for Unreal. Geometry is authored in the game's frame (x forward to the viewscreen,
y starboard, z up, metres, origin on the helm floor under the captain's chair); material names are the Unreal slot
names. The game (`UBridgeInteriorComponent`, style `RefitMesh`) binds `Screen_*` slots to the live Okudagram
material, `LightStrip` / `LightSoft` / `ClockRed` / `TouchPanel` to emissive instances, and keeps the rest.

    mkdir -p okuda_png && OKUDA_OUT=$PWD/okuda_png python3 ../../tools/okudagram/gen_okuda.py
    /Applications/Blender.app/Contents/MacOS/Blender -b --python build_bridge.py -- --render --export

`--render` writes preview frames to `previews/` (views from the captain's chair and an overview). Import the FBX
to `/Game/Slonk/Bridge/SM_BridgeRefit` with meshes combined and no collision (`tools/okudagram/import_bridge.py`
runs that headless). The `.blend` is written beside the script for hand editing; the script is the source of truth.
