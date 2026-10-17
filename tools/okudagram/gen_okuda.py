"""Okudagram screen textures in the Star Trek VI style: black panels, thin pale frames, teal / blue / green blocks,
bar graphs, ring gauges, small condensed labels. Live panels leave two outlined bar slots (right column) that
the screen material fills from game state: slot A at uv (0.60..0.96, 0.10..0.15), slot B at (0.60..0.96, 0.20..0.25)."""
import os, random, math, json
from PIL import Image, ImageDraw, ImageFont
W, H = 2048, 1024
OUT = os.environ.get("OKUDA_OUT", os.path.dirname(os.path.abspath(__file__)))
FONT = "/System/Library/Fonts/Avenir Next Condensed.ttc"
def font(sz, idx=2): return ImageFont.truetype(FONT, sz, index=idx)
TEAL = (52, 242, 191); BLUE = (40, 150, 255); GREEN = (80, 230, 90); AMBER = (255, 180, 50); WHITE = (235, 240, 245); GREY = (150, 160, 170); DIM = (18, 22, 26); FRAME = (170, 178, 186)
PAL = [TEAL, BLUE, GREEN, TEAL, BLUE, GREEN, AMBER]

def module_frame(d, box, title=None):
    x0, y0, x1, y1 = box
    d.rectangle(box, outline=(70, 78, 86), width=2)
    if title:
        d.rectangle((x0, y0, x0 + 14 + 11 * len(title), y0 + 26), fill=(70, 78, 86))
        d.text((x0 + 7, y0 + 1), title, font=font(22, 2), fill=(10, 12, 14))

def bars(d, box, n, rnd, color):
    x0, y0, x1, y1 = box
    pad = 8; bw = (x1 - x0 - pad * (n + 1)) / n
    for i in range(n):
        h = rnd.uniform(0.15, 0.95) * (y1 - y0 - 30)
        bx = x0 + pad + i * (bw + pad)
        d.rectangle((bx, y1 - 10 - h, bx + bw, y1 - 10), fill=color)
        # tick scale
    for k in range(6):
        ty = y0 + 20 + k * (y1 - y0 - 30) / 5
        d.line((x0 + 2, ty, x0 + 10, ty), fill=GREY, width=1)

def readouts(d, box, rows, rnd, color):
    x0, y0, x1, y1 = box
    rh = (y1 - y0 - 10) / rows
    for r in range(rows):
        y = y0 + 8 + r * rh
        label = rnd.choice(["PWR", "FLUX", "DELTA", "FREQ", "GAIN", "MODE", "SYNC", "LOAD", "TEMP", "RATE", "PH", "TRQ", "VEL", "MASS", "BRG", "RNG"])
        d.text((x0 + 10, y), label, font=font(20, 5), fill=GREY)
        val = f"{rnd.uniform(0, 999):06.1f}" if rnd.random() < 0.7 else rnd.choice(["NOMINAL", "ACTIVE", "STANDBY", "LOCKED", "ONLINE"])
        d.text((x0 + 80, y), val, font=font(22, 2), fill=color)
        # small block indicator
        d.rectangle((x1 - 60, y + 4, x1 - 14, y + 16), fill=color if rnd.random() < 0.6 else DIM, outline=(60, 66, 72))

def ring(d, box, rnd, color):
    x0, y0, x1, y1 = box
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2 + 8
    r = min(x1 - x0, y1 - y0) * 0.38
    d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=(60, 66, 72), width=2)
    a0 = rnd.uniform(0, 360)
    for k in range(rnd.randint(2, 4)):
        s = a0 + k * 90 + rnd.uniform(0, 30); e = s + rnd.uniform(25, 70)
        d.arc((cx - r, cy - r, cx + r, cy + r), s, e, fill=color, width=int(r * 0.22))
    d.ellipse((cx - r * 0.45, cy - r * 0.45, cx + r * 0.45, cy + r * 0.45), outline=color, width=2)
    d.text((cx - 22, cy - 12), f"{rnd.randint(10, 99)}", font=font(26, 2), fill=WHITE)

def grid(d, box, rnd, cols=8, rows=4):
    x0, y0, x1, y1 = box
    cw = (x1 - x0 - 12) / cols; ch = (y1 - y0 - 40) / rows
    for r in range(rows):
        for c in range(cols):
            on = rnd.random() < 0.55
            col = rnd.choice(PAL) if on else DIM
            d.rectangle((x0 + 8 + c * cw, y0 + 32 + r * ch, x0 + 8 + (c + 1) * cw - 6, y0 + 32 + (r + 1) * ch - 6), fill=col)

def ship_top(d, box, color):
    # The refit from above: saucer, neck, engineering hull, pylons, nacelles; in thin lines.
    x0, y0, x1, y1 = box
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    s = min(x1 - x0, y1 - y0) / 2.3
    R = s * 0.48
    d.ellipse((cx - R, cy - s * 0.95 - R, cx + R, cy - s * 0.95 + R), outline=color, width=3)
    for f in (0.25, 0.55, 0.8):
        d.ellipse((cx - R * f, cy - s * 0.95 - R * f, cx + R * f, cy - s * 0.95 + R * f), outline=color, width=1)
    d.rectangle((cx - s * 0.06, cy - s * 0.5, cx + s * 0.06, cy - s * 0.05), outline=color, width=2)
    d.rounded_rectangle((cx - s * 0.12, cy - s * 0.1, cx + s * 0.12, cy + s * 0.85), radius=int(s * 0.1), outline=color, width=3)
    for sgn in (-1, 1):
        d.line((cx + sgn * s * 0.12, cy + s * 0.4, cx + sgn * s * 0.55, cy + s * 0.05), fill=color, width=3)
        d.rounded_rectangle((cx + sgn * s * 0.48, cy - s * 0.45, cx + sgn * s * 0.62, cy + s * 0.75), radius=int(s * 0.06), outline=color, width=3)
        d.line((cx + sgn * s * 0.48, cy - s * 0.2, cx + sgn * s * 0.62, cy - s * 0.2), fill=color, width=1)

def live_slots(d):
    # Two outlined bar slots in the right column with labels; the material fills them.
    for (lab, y0, y1) in (("SHIELDS", 0.10, 0.15), ("HULL", 0.20, 0.25)):
        d.text((int(W * 0.60), int(H * y0) - 26), lab, font=font(22, 2), fill=WHITE)
        d.rectangle((int(W * 0.60), int(H * y0), int(W * 0.96), int(H * y1)), outline=FRAME, width=2)
        for k in range(1, 10):
            x = int(W * (0.60 + 0.36 * k / 10)); d.line((x, int(H * y1) - 6, x, int(H * y1)), fill=GREY, width=1)

def panel(name, title, seed, live=False, schematic=False, live_labels=None):
    rnd = random.Random(seed)
    im = Image.new("RGB", (W, H), (0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle((6, 6, W - 7, H - 7), radius=28, outline=FRAME, width=3)
    d.text((40, 18), title, font=font(44, 8), fill=WHITE)
    d.text((W - 300, 26), f"NCC-1701-A   {rnd.randint(1000,9999)}", font=font(24, 5), fill=GREY)
    d.line((40, 76, W - 40, 76), fill=(70, 78, 86), width=2)
    # Left two-thirds: a 3 x 2 grid of modules; right third: readouts (or the live slots on live panels).
    cols = [(40, 640), (660, 1200 if live else 1260)]
    rows = [(96, 530), (550, 980)]
    kinds = ["bars", "ring", "grid", "readouts"]
    if schematic:
        module_frame(d, (40, 96, 1260, 980), "MASTER SYSTEMS DISPLAY")
        ship_top(d, (60, 130, 1240, 970), GREEN)
    else:
        for ci, (cx0, cx1) in enumerate(cols):
            for ri, (ry0, ry1) in enumerate(rows):
                kind = rnd.choice(kinds); color = rnd.choice([TEAL, BLUE, GREEN])
                box = (cx0, ry0, cx1, ry1)
                module_frame(d, box, rnd.choice(["SENSOR", "SUBSPACE", "POWER", "STATUS", "RELAY", "COOLANT", "FIELD", "ARRAY", "LINK", "PHASE"]))
                inner = (cx0 + 6, ry0 + 30, cx1 - 6, ry1 - 6)
                if kind == "bars": bars(d, inner, rnd.randint(6, 12), rnd, color)
                elif kind == "ring": ring(d, inner, rnd, color)
                elif kind == "grid": grid(d, inner, rnd)
                else: readouts(d, inner, 9, rnd, color)
    # Right column.
    if live:
        live_slots(d)
        module_frame(d, (int(W * 0.60), 300, int(W * 0.96), 980), "SYSTEMS")
        readouts(d, (int(W * 0.60) + 6, 330, int(W * 0.96) - 6, 974), 14, rnd, TEAL)
    else:
        module_frame(d, (1280, 96, 2008, 530), "TELEMETRY")
        readouts(d, (1286, 126, 2002, 524), 9, rnd, rnd.choice([TEAL, BLUE]))
        module_frame(d, (1280, 550, 2008, 980), "ALLOCATION")
        grid(d, (1286, 556, 2002, 974), rnd, 6, 5)
    im.save(os.path.join(OUT, f"T_Okuda_{name}.png"), optimize=True)
    print(name, os.path.getsize(os.path.join(OUT, f"T_Okuda_{name}.png")) // 1024, "KB")

panel("Tactical", "TACTICAL", 1, live=True)
panel("MasterSituation", "MASTER SITUATION", 2, schematic=True)
panel("Communications", "COMMUNICATIONS", 3)
panel("Science", "SCIENCE", 4)
panel("Engineering", "ENGINEERING", 5)
panel("Environmental", "ENVIRONMENTAL", 6)
panel("Navigation", "NAVIGATION", 7)
panel("Helm", "HELM", 8, live=True)
panel("Weapons", "WEAPONS", 9)
# Round schematic for the helm pedestal: the ship from above in green on black.
im = Image.new("RGB", (1024, 1024), (0, 0, 0)); d = ImageDraw.Draw(im)
d.ellipse((12, 12, 1011, 1011), outline=BLUE, width=10)
for a in range(0, 360, 30):
    r0, r1 = 470, 500; x0 = 512 + r0 * math.cos(math.radians(a)); y0 = 512 + r0 * math.sin(math.radians(a))
    x1 = 512 + r1 * math.cos(math.radians(a)); y1 = 512 + r1 * math.sin(math.radians(a)); d.line((x0, y0, x1, y1), fill=WHITE, width=6)
ship_top(d, (160, 120, 864, 904), GREEN)
im.save(os.path.join(OUT, "T_Okuda_Schematic.png"), optimize=True); print("schematic")
json.dump({"liveSlots": {"A": [0.60, 0.10, 0.96, 0.15], "B": [0.60, 0.20, 0.96, 0.25]}}, open(os.path.join(OUT, "layout.json"), "w"))
