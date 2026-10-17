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

def ship_top(d, box, color, lw=2):
    """The refit from above, bow up, as a green wireframe plan like the helm pedestal's display: saucer with a
    radial grid and the bridge dome, impulse deck, neck, engineering hull with the deflector and shuttlebay,
    swept pylons, nacelles with bussard caps and grilles. Proportions of the 305 m ship (saucer 142 m across)."""
    x0, y0, x1, y1 = box
    L = min((y1 - y0) * 0.98, (x1 - x0) / 0.47)        # length on the texture
    cx, top = (x0 + x1) / 2, (y0 + y1) / 2 - L / 2
    def P(u, t): return (cx + u * L, top + t * L)       # u across (right +), t down the length, both fractions of L
    def ell(u, t, r, w=lw): d.ellipse((cx + (u - r) * L, top + (t - r) * L, cx + (u + r) * L, top + (t + r) * L), outline=color, width=w)
    def rrect(u0, t0, u1, t1, rad, w=lw):
        xa, xb = sorted((cx + u0 * L, cx + u1 * L))
        d.rounded_rectangle((xa, top + t0 * L, xb, top + t1 * L), radius=max(1, int(rad * L)), outline=color, width=w)
    def line(a, b, w=lw): d.line((P(*a), P(*b)), fill=color, width=w)
    # Saucer: rim, grid rings, 24 radials, the bridge and the B/C deck superstructure.
    SC, R = 0.233, 0.2325
    ell(0, SC, R, lw + 1)
    for f in (0.18, 0.4, 0.62, 0.82): ell(0, SC, R * f, 1)
    for k in range(24):
        a = math.radians(k * 15)
        line((R * 0.18 * math.sin(a), SC - R * 0.18 * math.cos(a)), (R * 0.985 * math.sin(a), SC - R * 0.985 * math.cos(a)), 1)
    ell(0, SC, 0.03, lw); ell(0, SC, 0.014, 1)
    rrect(-0.035, SC + 0.02, 0.035, SC + 0.21, 0.02, 1)                      # B/C decks and the impulse deck
    rrect(-0.06, SC + 0.19, 0.06, SC + 0.245, 0.01, lw)
    for u in (-0.04, -0.013, 0.013, 0.04): line((u, SC + 0.2), (u, SC + 0.24), 1)
    # Neck down to the engineering hull.
    line((-0.028, SC + 0.2), (-0.02, 0.5)); line((0.028, SC + 0.2), (0.02, 0.5))
    # Engineering hull: deflector housing forward, the hull, the shuttlebay doors aft.
    ell(0, 0.455, 0.045, lw + 1); ell(0, 0.455, 0.025, 1)
    rrect(-0.058, 0.44, 0.058, 0.94, 0.05, lw + 1)
    line((-0.045, 0.86), (-0.045, 0.93), 1); line((0.045, 0.86), (0.045, 0.93), 1); line((-0.03, 0.88), (0.03, 0.88), 1)
    line((0, 0.5), (0, 0.84), 1)
    for sgn in (-1, 1):
        # Pylon: a thin swept quad from the hull aft to the nacelle's forward third.
        pts = [P(sgn * 0.058, 0.70), P(sgn * 0.058, 0.76), P(sgn * 0.17, 0.60), P(sgn * 0.17, 0.555)]
        d.polygon(pts, outline=color, width=lw)
        # Nacelle: body, centre seam, bussard cap, grilles, aft cap.
        rrect(sgn * 0.17, 0.42, sgn * 0.225, 1.0, 0.025, lw + 1)
        line((sgn * 0.1975, 0.47), (sgn * 0.1975, 0.95), 1)
        ell(sgn * 0.1975, 0.445, 0.02, 1)
        for t in (0.66, 0.74, 0.82):
            rrect(sgn * 0.178, t, sgn * 0.217, t + 0.05, 0.005, 1)
        line((sgn * 0.17, 0.96), (sgn * 0.225, 0.96), 1)

def gauge_ring(d, cx, cy, R):
    """The pedestal's circular status gauge: a thick blue ring broken into segments with thin white sections
    between, a thinner teal ring of shorter arcs inside, and small blue tabs at the cardinals."""
    for k in range(12):
        a = k * 30 + 90
        d.arc((cx - R, cy - R, cx + R, cy + R), a + 1, a + 19, fill=BLUE, width=int(R * 0.045))
        d.arc((cx - R * 0.985, cy - R * 0.985, cx + R * 0.985, cy + R * 0.985), a + 21, a + 28, fill=WHITE, width=int(R * 0.022))
    r2 = R * 0.915
    for k in range(8):
        a = k * 45 + 100
        d.arc((cx - r2, cy - r2, cx + r2, cy + r2), a, a + 26, fill=TEAL, width=int(R * 0.02))
    for a in (0, 90, 180, 270):
        x = cx + R * 0.955 * math.cos(math.radians(a)); y = cy + R * 0.955 * math.sin(math.radians(a))
        s = R * 0.035; d.rectangle((x - s, y - s, x + s, y + s), fill=BLUE)
    for a in range(0, 360, 10):
        if a % 30 == 0: continue
        x0 = cx + R * 0.88 * math.cos(math.radians(a)); y0 = cy + R * 0.88 * math.sin(math.radians(a))
        x1 = cx + R * 0.86 * math.cos(math.radians(a)); y1 = cy + R * 0.86 * math.sin(math.radians(a))
        d.line((x0, y0, x1, y1), fill=GREY, width=1)

def command_strip(d, box):
    """The slim readout above the pedestal's gauge: a white-framed black strip with a red block on the left, two
    rows of small pale buttons, and a caption, after the MANUAL COMMAND INPUT panel in Versiga's helm closeup."""
    x0, y0, x1, y1 = box
    d.rounded_rectangle(box, radius=10, outline=WHITE, width=3)
    d.rounded_rectangle((x0 + 18, y0 + 12, x0 + 150, y1 - 24), radius=6, fill=(190, 48, 48))
    for k in range(3):
        d.rounded_rectangle((x0 + 34 + k * 38, y0 + 26, x0 + 58 + k * 38, y1 - 38), radius=5, fill=(235, 225, 215))
    bx = x0 + 180
    for row, yy in enumerate((y0 + 16, (y0 + y1) / 2 - 2)):
        for k in range(14):
            x = bx + k * 52
            if x + 36 > x1 - 70: break
            col = (240, 236, 220) if (k + row) % 3 else (255, 190, 80)
            d.rounded_rectangle((x, yy, x + 36, yy + 20), radius=4, fill=col)
            d.rectangle((x + 4, yy + 24, x + 32, yy + 26), fill=(120, 126, 132))
    d.text((x1 - 300, y1 - 22), "MANUAL COMMAND INPUT", font=font(16, 2), fill=GREY)
    d.text((x1 - 62, y0 + 10), "03-98-1", font=font(16, 2), fill=GREY)

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
# The helm pedestal's display, between the two helmsmen and facing the captain: the slim command strip across the
# top, then the big segmented blue gauge ring with the ship's green plan inside it, bow up. One texture covers the
# whole black face of the pedestal head; the bridge mesh maps its Scr_Schematic quad to it square.
im = Image.new("RGB", (1024, 1024), (0, 0, 0)); d = ImageDraw.Draw(im)
command_strip(d, (52, 28, 972, 124))
gauge_ring(d, 512, 598, 388)
ship_top(d, (512 - 300, 598 - 330, 512 + 300, 598 + 330), (34, 215, 80), lw=2)
for (x, y) in ((40, 160), (984, 160), (40, 1000), (984, 1000)):
    d.ellipse((x - 5, y - 5, x + 5, y + 5), fill=(60, 64, 70))                # bezel screws
im.save(os.path.join(OUT, "T_Okuda_Schematic.png"), optimize=True); print("schematic")
json.dump({"liveSlots": {"A": [0.60, 0.10, 0.96, 0.15], "B": [0.60, 0.20, 0.96, 0.25]}}, open(os.path.join(OUT, "layout.json"), "w"))
