"""Okudagram screen textures in the Star Trek VI style: black panels, thin pale frames, teal / blue / green blocks,
bar graphs, ring gauges, small condensed labels. Live panels leave two outlined bar slots (right column) that
the screen material fills from game state: slot A at uv (0.60..0.96, 0.10..0.15), slot B at (0.60..0.96, 0.20..0.25)."""
import os, random, math, json
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops
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

VIEWS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ship_views")
def ship_fit(box, view, rotate=0):
    """The rendered view's silhouette and line drawing (render_ship_views.py: "top" bow up, "side" bow right),
    turned by `rotate` degrees (PIL's sense: positive anticlockwise) and fitted into `box`: (offset, silhouette,
    lines), both greyscale masks of the same size."""
    x0, y0, x1, y1 = box
    sil = Image.open(os.path.join(VIEWS_DIR, f"ship_{view}_sil.png")).convert("L")
    lin = Image.open(os.path.join(VIEWS_DIR, f"ship_{view}_lines.png")).convert("L")
    if rotate: sil = sil.rotate(rotate, expand=True); lin = lin.rotate(rotate, expand=True)
    bb = sil.getbbox(); pad = 24
    bb = (max(0, bb[0] - pad), max(0, bb[1] - pad), min(sil.width, bb[2] + pad), min(sil.height, bb[3] + pad))
    sil = sil.crop(bb); lin = lin.crop(bb)
    sc = min((x1 - x0) / sil.width, (y1 - y0) / sil.height)
    size = (max(1, int(sil.width * sc)), max(1, int(sil.height * sc)))
    sil = sil.resize(size, Image.LANCZOS); lin = lin.resize(size, Image.LANCZOS).point(lambda v: min(255, int(v * 1.8)))
    return (int(x0 + ((x1 - x0) - size[0]) / 2), int(y0 + ((y1 - y0) - size[1]) / 2)), sil, lin

def ship_view(im, box, view, color, fill=None, lines=True, sil_alpha=0.16, rotate=0):
    """The ship herself on a screen, from the renders of the 1701-A model: the silhouette as a dim wash of `color`
    (or solid `fill`), the line drawing over it in `color`."""
    off, sil, lin = ship_fit(box, view, rotate)
    if fill is not None: im.paste(Image.new("RGB", sil.size, fill), off, sil)
    else: im.paste(Image.new("RGB", sil.size, tuple(int(c * sil_alpha) for c in color)), off, sil)
    if lines: im.paste(Image.new("RGB", sil.size, color), off, lin)
    return off, sil

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
        ship_view(im, (60, 130, 1240, 970), "top", GREEN)
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

def deck_config():
    """The red dot-matrix profile of the deck-configuration display (the corridor's wall panel): the ship filled in
    red with scan lines through it, the deck letters down the left, the readout along the foot."""
    im = Image.new("RGB", (W, H), (0, 0, 0)); d = ImageDraw.Draw(im)
    RED = (235, 30, 30)
    ship_view(im, (200, 140, W - 80, 760), "side", RED, fill=RED, lines=False)
    for y in range(0, H, 6): d.line((0, y, W, y), fill=(0, 0, 0), width=2)      # scan lines
    for k, letter in enumerate("ABCDEFGHIJKLMNOPQRSTU"):
        y = 90 + k * 40
        d.text((40, y), letter, font=font(26, 2), fill=(255, 120, 120) if k in (0, 6, 14, 16) else (150, 40, 40))
        d.rectangle((80, y + 8, 80 + (26 if k in (0, 1, 6, 12, 14, 16, 18) else 12), y + 20), fill=(180, 40, 40))
    d.text((300, 820), "DECK CONFIGURATION", font=font(40, 8), fill=WHITE)
    d.text((300, 880), "DECK SPS 0019     DECK ACCESS 0073     TURBOLIFT NET 004     GRAV 1.0 G", font=font(26, 5), fill=(120, 230, 255))
    d.text((W - 380, 820), "NCC-1701-A", font=font(40, 8), fill=(255, 120, 120))
    im.save(os.path.join(OUT, "T_Okuda_DeckConfig.png"), optimize=True); print("DeckConfig")

def deflector():
    """The shield-status display: the header bar, the deflector power scale, the ship's plan inside the dotted
    shield envelope with its figures, the footer."""
    im = Image.new("RGB", (W, H), (0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle((6, 6, W - 7, H - 7), radius=28, outline=BLUE, width=4)
    d.rectangle((40, 30, W - 40, 110), fill=(30, 110, 200))
    d.text((70, 36), "SHIELD STATUS: NOMINAL", font=font(58, 8), fill=(200, 235, 255))
    d.text((70, 128), "DEFLECTOR POWER", font=font(30, 5), fill=(120, 200, 255))
    d.rectangle((420, 130, W - 80, 160), outline=(120, 200, 255), width=2)
    d.rectangle((422, 132, 422 + int((W - 504) * 0.72), 158), fill=(120, 200, 255))
    for k in range(1, 10):
        x = 420 + (W - 500) * k / 10
        d.line((x, 160, x, 172), fill=(120, 200, 255), width=2)
        d.text((x - 12, 174), str(k * 10), font=font(18, 2), fill=(120, 200, 255))
    # The shield: a bubble grown from the ship's own silhouette (the plan turned bow-right), its body a faint blue
    # wash with a brighter rim, the grid behind it.
    box = (300, 320, W - 300, 920)
    off, sil, lin = ship_fit(box, "top", rotate=-90)
    full = Image.new("L", (W, H), 0); full.paste(sil, off)
    bubble = full.filter(ImageFilter.GaussianBlur(130)).point(lambda v: 255 if v > 26 else 0).filter(ImageFilter.GaussianBlur(6))
    rim = ImageChops.subtract(bubble, bubble.filter(ImageFilter.MinFilter(15)))
    for yy in range(300, 920, 60): d.line((170, yy, W - 170, yy), fill=(20, 50, 80), width=1)
    im.paste(Image.new("RGB", (W, H), (40, 120, 220)), (0, 0), bubble.point(lambda v: int(v * 0.30)))
    im.paste(Image.new("RGB", (W, H), (150, 215, 255)), (0, 0), rim)
    ship_view(im, box, "top", (110, 210, 255), rotate=-90)
    cx = W // 2
    for (x, y, s) in ((cx + 560, 300, "1788\n9573\n8318"), (cx - 660, 760, "648\n590045\n2 8 6"), (cx - 120, 910, "41154\n0497"), (cx + 540, 840, "2748")):
        d.multiline_text((x, y), s, font=font(20, 5), fill=(120, 200, 255), spacing=2)
    d.text((40, H - 90), "DEFLECTOR STATUS", font=font(40, 8), fill=(60, 140, 220))
    d.text((W - 300, H - 100), "1181.1", font=font(52, 8), fill=(200, 235, 255))
    im.save(os.path.join(OUT, "T_Okuda_Deflector.png"), optimize=True); print("Deflector")

def torpedo_bay():
    """The torpedo bay display: the casing's plan to the left, the inventory table and the rack cross-section to the
    right, the footer."""
    im = Image.new("RGB", (W, H), (0, 0, 0)); d = ImageDraw.Draw(im)
    C = (70, 150, 255); DIMC = (30, 70, 130)
    d.rounded_rectangle((6, 6, W - 7, H - 7), radius=28, outline=C, width=4)
    d.line((W * 0.36, 20, W * 0.36, H - 110), fill=C, width=2)
    d.text((60, 40), "PLATFORM   AREA 11", font=font(24, 5), fill=C)
    # The casing in plan: an elongated hexagon with ribs and the warhead chamber.
    px, py, pw, ph = 170, 120, 380, 700
    pts = [(px + pw / 2, py), (px + pw, py + 90), (px + pw, py + ph - 90), (px + pw / 2, py + ph), (px, py + ph - 90), (px, py + 90)]
    d.polygon(pts, outline=C, width=3)
    for k in range(7):
        y = py + 130 + k * 70
        d.line((px + 40, y, px + pw - 40, y), fill=DIMC, width=2)
    d.rectangle((px + 120, py + 300, px + pw - 120, py + 420), outline=C, width=3)
    d.rectangle((px + 140, py + 330, px + 200, py + 390), fill=(160, 230, 255))
    d.rectangle((px + 210, py + 330, px + 240, py + 390), fill=(160, 230, 255))
    d.text((W * 0.40, 40), "PHOTON TORPEDO", font=font(24, 5), fill=C)
    rnd = random.Random(11)
    for r in range(3):
        for c in range(6):
            d.text((W * 0.40 + c * 170, 80 + r * 34), f"{rnd.randint(1000, 99999):05d}", font=font(22, 2), fill=C)
    d.text((W * 0.40, 230), "CROSS SECTION   AREA 11", font=font(24, 5), fill=C)
    gx, gy = int(W * 0.44), 290
    for r in range(9):
        for c in range(6):
            x, y = gx + c * 110, gy + r * 50
            d.rounded_rectangle((x, y, x + 90, y + 36), radius=10, outline=C, width=2, fill=(160, 230, 255) if (r, c) == (4, 2) else None)
    d.rectangle((gx - 90, gy + 120, gx - 10, gy + 330), outline=C, width=2)                  # the breech
    for k in range(5): d.line((gx - 90, gy + 140 + k * 40, gx - 10, gy + 140 + k * 40), fill=DIMC, width=1)
    d.rectangle((gx + 680, gy + 60, gx + 820, gy + 160), outline=C, width=2)                 # the tube
    d.text((60, H - 90), "TORPEDO BAY", font=font(40, 8), fill=(120, 190, 255))
    d.text((W - 330, H - 100), "1180.2", font=font(52, 8), fill=(200, 235, 255))
    im.save(os.path.join(OUT, "T_Okuda_TorpedoBay.png"), optimize=True); print("TorpedoBay")

panel("Tactical", "TACTICAL", 1, live=True)
panel("MasterSituation", "MASTER SITUATION", 2, schematic=True)
panel("Communications", "COMMUNICATIONS", 3)
panel("Science", "SCIENCE", 4)
panel("Engineering", "ENGINEERING", 5)
panel("Environmental", "ENVIRONMENTAL", 6)
panel("Navigation", "NAVIGATION", 7)
panel("Helm", "HELM", 8, live=True)
panel("Weapons", "WEAPONS", 9)
deck_config(); deflector(); torpedo_bay()
# The helm pedestal's display, between the two helmsmen and facing the captain: the slim command strip across the
# top, then the big segmented blue gauge ring with the ship's green plan inside it, bow up. One texture covers the
# whole black face of the pedestal head; the bridge mesh maps its Scr_Schematic quad to it square.
im = Image.new("RGB", (1024, 1024), (0, 0, 0)); d = ImageDraw.Draw(im)
command_strip(d, (52, 28, 972, 124))
gauge_ring(d, 512, 598, 388)
ship_view(im, (512 - 300, 598 - 330, 512 + 300, 598 + 330), "top", (34, 215, 80))
for (x, y) in ((40, 160), (984, 160), (40, 1000), (984, 1000)):
    d.ellipse((x - 5, y - 5, x + 5, y + 5), fill=(60, 64, 70))                # bezel screws
im.save(os.path.join(OUT, "T_Okuda_Schematic.png"), optimize=True); print("schematic")
json.dump({"liveSlots": {"A": [0.60, 0.10, 0.96, 0.15], "B": [0.60, 0.20, 0.96, 0.25]}}, open(os.path.join(OUT, "layout.json"), "w"))
