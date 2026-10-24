#!/usr/bin/python3
"""Side-by-side sheets of Versiga's renders against the Blender previews. Usage:
    /usr/bin/python3 make_sheets.py --refs <dir with v01.jpg .. v21.jpg and iso_main.jpg>
(the references are not in the repo). Writes compare_views.png, compare_captain.png and compare_iso.png into previews/.
"""
import os, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
PREV = os.path.join(HERE, "previews")
refs = sys.argv[sys.argv.index("--refs") + 1] if "--refs" in sys.argv else os.path.expanduser("~/bridge_ref")

def load(path, h):
    im = Image.open(path).convert("RGB")
    return im.resize((int(im.width * h / im.height), h), Image.LANCZOS)

def sheet(pairs, out, h=300):
    rows = []
    for ref, prev in pairs:
        a = load(os.path.join(refs, ref), h); b = load(os.path.join(PREV, prev), h)
        row = Image.new("RGB", (a.width + b.width + 12, h + 22), (20, 20, 22))
        row.paste(a, (0, 22)); row.paste(b, (a.width + 12, 22))
        d = ImageDraw.Draw(row); d.text((4, 4), f"ref {ref}", fill=(230, 230, 230)); d.text((a.width + 16, 4), f"preview {prev}", fill=(230, 230, 230))
        rows.append(row)
    W = max(r.width for r in rows); H = sum(r.height + 6 for r in rows)
    im = Image.new("RGB", (W, H), (20, 20, 22)); y = 0
    for r in rows: im.paste(r, (0, y)); y += r.height + 6
    im.save(os.path.join(PREV, out)); print("wrote", out, im.size)

sheet([("v21.jpg", "prev_ceil.png"), ("v01.jpg", "prev_v01.png"), ("v19.jpg", "prev_lift.png"), ("v13.jpg", "prev_v13.png"),
       ("v02.jpg", "prev_v02.png"), ("v09.jpg", "prev_v09.png"), ("v08.jpg", "prev_v08.png")], "compare_views.png")
sheet([("v02.jpg", "prev_capt.png"), ("v06.jpg", "prev_v06.png"), ("v05.jpg", "prev_capt_side.png"), ("v03.jpg", "prev_capt_port.png")], "compare_captain.png", h=360)
sheet([("v13.jpg", "prev_truss.png"), ("v19.jpg", "prev_lift.png"), ("v02.jpg", "prev_chair.png")], "compare_bulkheads.png", h=360)
sheet([("iso_main.jpg", "prev_iso_open.png")], "compare_iso.png", h=520)
