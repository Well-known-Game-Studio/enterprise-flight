# Okudagram screen textures

`gen_okuda.py` draws the bridge screen textures (`T_Okuda_*.png`, 2048x1024, plus the round `T_Okuda_Schematic`)
in the Star Trek VI style with Pillow and the macOS Avenir Next Condensed font. Live panels (Tactical, Helm) leave
two outlined bar slots in their right column; `M_Okudagram` fills them from game state (`BarA`/`BarB`, rects in
`BarARect`/`BarBRect`, alert wash in `Flash`). Import the PNGs to `/Game/Slonk/Bridge` as sRGB UI textures.

    OKUDA_OUT=/tmp/okuda python3 tools/okudagram/gen_okuda.py

The ship on the screens (the master systems display, the helm pedestal's plan, the deflector status, the red
deck-configuration profile) is the 1701-A herself: `render_ship_views.py` renders the hull model (the Enterprise-A
OBJ the game's hull mesh was imported from, unpacked from `blender/uss-enterprise-ncc-1701-a/source`) in Blender as
a filled silhouette and a Freestyle line drawing, top (bow up) and side (bow right), into `ship_views/`, and
`gen_okuda.py` composites them (`ship_view`). Re-render only when the model changes:

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python tools/okudagram/render_ship_views.py -- <Enterprise-A.obj>
    magick ship_views/ship_top_sil.png -threshold 50% -depth 8 ship_views/ship_top_sil.png   # and the side; the lines keep their grey

`gen_okuda.py` wants Pillow: on this machine `/opt/homebrew/bin/python3.13` has it.
