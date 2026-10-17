# Okudagram screen textures

`gen_okuda.py` draws the bridge screen textures (`T_Okuda_*.png`, 2048x1024, plus the round `T_Okuda_Schematic`)
in the Star Trek VI style with Pillow and the macOS Avenir Next Condensed font. Live panels (Tactical, Helm) leave
two outlined bar slots in their right column; `M_Okudagram` fills them from game state (`BarA`/`BarB`, rects in
`BarARect`/`BarBRect`, alert wash in `Flash`). Import the PNGs to `/Game/Slonk/Bridge` as sRGB UI textures.

    OKUDA_OUT=/tmp/okuda python3 tools/okudagram/gen_okuda.py
