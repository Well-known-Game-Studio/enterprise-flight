"""Builds the Enterprise-A bridge of Star Trek VI (after Donny Versiga's renders) as a Blender scene and exports it
for Unreal. Run headless:

    /Applications/Blender.app/Contents/MacOS/Blender -b --python build_bridge.py -- [--render] [--export]

Geometry is laid out in the game's frame (x forward toward the viewscreen, y to starboard, z up, metres, origin on
the helm floor under the captain's chair) and converted to Blender's axes on creation, so an FBX exported with
forward -Y / up Z lands in Unreal facing +X with starboard at +Y. Material names are the Unreal slot names: the
game binds `Screen_*` slots to the live Okudagram material and keeps the rest as imported.
"""
import bpy, bmesh, math, os, sys
from mathutils import Vector, Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
OKUDA = os.path.join(HERE, "..", "..", "unreal", "Content", "Slonk", "Bridge")   # not used for files; textures come from tools/okudagram
TEXDIR = os.environ.get("OKUDA_PNG", os.path.join(HERE, "okuda_png"))
ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []

# ---- frame ----------------------------------------------------------------------------------------------------
def B(x, y, z):
    """Game frame (x fwd, y stbd, z up) to Blender. Unreal's FBX import keeps Blender X and negates Y, so Blender
    +X is the game's forward and Blender -Y its starboard."""
    return Vector((x, -y, z))

def ang(deg):
    """Unit vector in the game frame for a bearing: 0 forward, 90 starboard."""
    a = math.radians(deg)
    return (math.cos(a), math.sin(a))

# ---- materials -------------------------------------------------------------------------------------------------
MATS = {}
def mat(name, color, rough=0.6, metal=0.0, emit=None, strength=0.0, tex=None):
    if name in MATS:
        return MATS[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    if emit is not None:
        bsdf.inputs["Emission Color"].default_value = (*emit, 1.0)
        bsdf.inputs["Emission Strength"].default_value = strength
    if tex and os.path.exists(tex):
        img = bpy.data.images.load(tex)
        node = m.node_tree.nodes.new("ShaderNodeTexImage"); node.image = img
        m.node_tree.links.new(node.outputs["Color"], bsdf.inputs["Emission Color"])
        m.node_tree.links.new(node.outputs["Color"], bsdf.inputs["Base Color"])
        bsdf.inputs["Emission Strength"].default_value = 2.0
    MATS[name] = m
    return m

GREY = (0.52, 0.53, 0.55); DARK = (0.14, 0.15, 0.165); CARPET = (0.11, 0.115, 0.12); BLACK = (0.02, 0.022, 0.026)
WHITE = (0.9, 0.92, 0.95); BURG = (0.28, 0.045, 0.055); SEAT = (0.58, 0.58, 0.59); RAIL = (0.05, 0.05, 0.055)
def M_PanelGrey(): return mat("PanelGrey", GREY, 0.55)
def M_PanelDark(): return mat("PanelDark", DARK, 0.7)
def M_Carpet(): return mat("Carpet", CARPET, 0.95)
def M_Black(): return mat("ConsoleBlack", BLACK, 0.35)
def M_Strip(): return mat("LightStrip", WHITE, 0.8, emit=(0.95, 0.97, 1.0), strength=6.0)
def M_Burgundy(): return mat("Burgundy", BURG, 0.7)
def M_Seat(): return mat("SeatGrey", SEAT, 0.95)
def M_Rail(): return mat("RailBlack", RAIL, 0.3, 0.6)
def M_PostGrey(): return mat("PostGrey", (0.45, 0.45, 0.47), 0.35, 0.4)
def M_Clock(): return mat("ClockRed", (0.1, 0.0, 0.0), 0.5, emit=(1.0, 0.1, 0.05), strength=8.0)
def M_Buttons(): return mat("TouchPanel", (0.03, 0.03, 0.035), 0.4, emit=(0.4, 0.7, 0.6), strength=1.0)
def M_Screen(name): return mat("Screen_" + name, BLACK, 0.2, tex=os.path.join(TEXDIR, f"T_Okuda_{name}.png"))

# ---- mesh helpers ----------------------------------------------------------------------------------------------
COLLECTIONS = {}
def coll(name):
    if name not in COLLECTIONS:
        c = bpy.data.collections.new(name); bpy.context.scene.collection.children.link(c); COLLECTIONS[name] = c
    return COLLECTIONS[name]

def finish(obj, material, group, smooth=False, bevel=0.0):
    obj.data.materials.append(material)
    coll(group).objects.link(obj)
    if smooth:
        for p in obj.data.polygons: p.use_smooth = True
    if bevel > 0:
        b = obj.modifiers.new("Bevel", "BEVEL"); b.width = bevel; b.segments = 3; b.limit_method = "ANGLE"
    return obj

def mesh_from(name, verts, faces, material, group, smooth=False, bevel=0.0, uvs=None):
    me = bpy.data.meshes.new(name)
    me.from_pydata([B(*v) for v in verts], [], faces)
    me.update()
    if uvs:
        uv = me.uv_layers.new(name="UVMap")
        for poly in me.polygons:
            for li in poly.loop_indices:
                uv.data[li].uv = uvs[me.loops[li].vertex_index]
    obj = bpy.data.objects.new(name, me)
    return finish(obj, material, group, smooth, bevel)

def box(name, center, size, material, group, yaw=0.0, pitch=0.0, roll=0.0, bevel=0.0):
    """Axis-aligned box in the game frame, then yawed (about z) and pitched (nose up about y)."""
    cx, cy, cz = center; sx, sy, sz = size
    corners = []
    for dx in (-0.5, 0.5):
        for dy in (-0.5, 0.5):
            for dz in (-0.5, 0.5):
                corners.append((dx * sx, dy * sy, dz * sz))
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    cr, sr = math.cos(math.radians(roll)), math.sin(math.radians(roll))
    cyw, syw = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    verts = []
    for (x, y, z) in corners:
        y, z = y * cr - z * sr, y * sr + z * cr                 # roll about x
        x, z = x * cp + z * sp, -x * sp + z * cp                # pitch (nose up)
        x, y = x * cyw - y * syw, x * syw + y * cyw             # yaw
        verts.append((cx + x, cy + y, cz + z))
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    return mesh_from(name, verts, faces, material, group, bevel=bevel)

def arc_sweep(name, profile, a0, a1, material, group, step=3.0, smooth=True, cap=True, closed=True, uv=False):
    """Sweep an (r, z) profile round the vertical axis from bearing a0 to a1. Each profile segment gets its own
    strip of vertices so smoothing runs along the arc only. With uv=True the strip is UV-mapped as one screen:
    u along the arc (increasing bearing = viewer's right when facing outboard), v down the profile."""
    n = max(2, int(round(abs(a1 - a0) / step)) + 1)
    P = len(profile)
    segs = P if closed else P - 1
    lens = [0.0]
    for j in range(segs):
        (r0, z0), (r1, z1) = profile[j], profile[(j + 1) % P]
        lens.append(lens[-1] + math.hypot(r1 - r0, z1 - z0))
    total = max(lens[-1], 1e-6)
    verts = []; faces = []; uvs = {}
    for j in range(segs):
        (r0, z0), (r1, z1) = profile[j], profile[(j + 1) % P]
        base = len(verts)
        for i in range(n):
            a = a0 + (a1 - a0) * i / (n - 1)
            c, sn = ang(a)
            verts.append((r0 * c, r0 * sn, z0)); verts.append((r1 * c, r1 * sn, z1))
            u = i / (n - 1)
            uvs[base + 2 * i] = (u, lens[j] / total); uvs[base + 2 * i + 1] = (u, lens[j + 1] / total)
        for i in range(n - 1):
            faces.append((base + 2 * i, base + 2 * i + 2, base + 2 * i + 3, base + 2 * i + 1))
    if cap and closed and abs(a1 - a0) < 359.9:
        # end caps from fresh vertices
        for i, flip in ((0, False), (n - 1, True)):
            a = a0 + (a1 - a0) * i / (n - 1); c, sn = ang(a)
            base = len(verts)
            for (r, z) in profile:
                verts.append((r * c, r * sn, z)); uvs[len(verts) - 1] = (0, 0)
            idx = list(range(base, base + P))
            faces.append(tuple(reversed(idx)) if flip else tuple(idx))
    obj = mesh_from(name, verts, faces, material, group, uvs=uvs if uv else None)
    if smooth:
        for poly in obj.data.polygons:
            if len(poly.vertices) == 4: poly.use_smooth = True
    return obj

def lathe(name, profile, material, group, segments=48, smooth=True):
    """Revolve an open (r, z) profile a full turn, capped at both ends; split per segment like arc_sweep."""
    verts = []; faces = []
    P = len(profile)
    for j in range(P - 1):
        (r0, z0), (r1, z1) = profile[j], profile[j + 1]
        base = len(verts)
        for i in range(segments):
            c, sn = ang(360.0 * i / segments)
            verts.append((r0 * c, r0 * sn, z0)); verts.append((r1 * c, r1 * sn, z1))
        for i in range(segments):
            i2 = (i + 1) % segments
            faces.append((base + 2 * i, base + 2 * i2, base + 2 * i2 + 1, base + 2 * i + 1))
    for (r, z), flip in ((profile[0], True), (profile[-1], False)):
        if r > 1e-6:
            base = len(verts)
            for i in range(segments):
                c, sn = ang(360.0 * i / segments); verts.append((r * c, r * sn, z))
            idx = list(range(base, base + segments))
            faces.append(tuple(reversed(idx)) if flip else tuple(idx))
    obj = mesh_from(name, verts, faces, material, group)
    if smooth:
        for poly in obj.data.polygons:
            if len(poly.vertices) == 4: poly.use_smooth = True
    return obj

def screen(name, center, w, h, material, group, yaw=0.0, pitch=0.0):
    """A quad facing the bridge centre at a bearing: built in a local frame (y across, z up) looking along -x, then
    pitched (top leaning back is negative pitch) and yawed. UVs 0..1 with v down the screen, u to the viewer's right."""
    cx, cy, cz = center
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    cyw, syw = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    pts = [(-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)]   # (across, up)
    verts = []
    for (u, v) in pts:
        x, y, z = 0.0, u, v
        x, z = x * cp + z * sp, -x * sp + z * cp
        x, y = x * cyw - y * syw, x * syw + y * cyw
        verts.append((cx + x, cy + y, cz + z))
    # Viewer stands inboard looking outboard (+radial): their right is -tangential, i.e. local -y. u runs with
    # local -y; v runs downward.
    uvs = {0: (1.0, 1.0), 1: (0.0, 1.0), 2: (0.0, 0.0), 3: (1.0, 0.0)}
    return mesh_from(name, verts, [(0, 3, 2, 1)], material, group, uvs=uvs)

def wallbox(name, deg, radial, z, thick, width, height, material, group, pitch=0.0, bevel=0.0):
    c, s = ang(deg)
    return box(name, (radial * c, radial * s, z), (thick, width, height), material, group, yaw=deg, pitch=pitch, bevel=bevel)

def light_bars(prefix, deg, radial, z0, width, count=3, pitch=0.11):
    for i in range(count):
        wallbox(f"{prefix}_bar{i}", deg, radial, z0 + i * pitch, 0.02, width, 0.05, M_Strip(), "Lights")

# ---- the set -----------------------------------------------------------------------------------------------------
R = 5.6          # wall radius
HD = 0.40        # perimeter deck above the helm floor
HC = HD + 0.26   # command platform
H = 3.0          # ceiling
VS = 34.0        # viewscreen half-angle
RC = 2.15        # command platform radius
RD = 3.35        # perimeter deck inner edge
LIFTS = (150.0, 210.0); LIFT_HALF = 12.0
STEP = 32.0

def floors():
    lathe("HelmFloor", [(0.0, -0.06), (RD + 0.1, -0.06), (RD + 0.1, 0.0), (0.0, 0.0)], M_Carpet(), "Floor", smooth=False)
    arc_sweep("DeckRing", [(RD, 0.0), (R + 0.15, 0.0), (R + 0.15, HD), (RD, HD)], 0, 360, M_Carpet(), "Floor", smooth=False, cap=False)
    arc_sweep("DeckRiser", [(RD - 0.015, 0.0), (RD + 0.01, 0.0), (RD + 0.01, HD - 0.05), (RD - 0.015, HD - 0.05)], 0, 360, M_Black(), "Floor", cap=False)
    arc_sweep("DeckLipStrip", [(RD - 0.01, HD - 0.05), (RD + 0.03, HD - 0.05), (RD + 0.03, HD - 0.01), (RD - 0.01, HD - 0.01)], 0, 360, M_Strip(), "Lights", cap=False)
    # Command platform: a horseshoe open at the front with squared shoulders and a chevron stair.
    arc_sweep("Platform", [(0.0, 0.0), (RC, 0.0), (RC, HC), (0.0, HC)], 46, 314, M_Carpet(), "Floor", smooth=False)
    arc_sweep("PlatformRiser", [(RC - 0.01, 0.0), (RC + 0.015, 0.0), (RC + 0.015, HC - 0.05), (RC - 0.01, HC - 0.05)], 46, 314, M_Black(), "Floor")
    arc_sweep("PlatformStrip", [(RC - 0.01, HC - 0.05), (RC + 0.025, HC - 0.05), (RC + 0.025, HC - 0.01), (RC - 0.01, HC - 0.01)], 46, 314, M_Strip(), "Lights")
    for s in (-1, 1):
        box(f"Shoulder{s}", (1.00, s * 1.70, HC / 2), (0.96, 1.12, HC), M_Carpet(), "Floor")
        box(f"ShoulderFace{s}", (1.49, s * 1.70, HC / 2 - 0.025), (0.02, 1.12, HC - 0.05), M_Black(), "Floor")
        box(f"ShoulderStrip{s}", (1.495, s * 1.70, HC - 0.03), (0.02, 1.10, 0.04), M_Strip(), "Lights")
    for st in range(2):
        top = HC * (0.5 if st == 0 else 1.0); x = 1.60 if st == 0 else 1.36
        box(f"Step{st}", (x, 0.0, top / 2), (0.28, 2.32 - st * 0.2, top), M_Carpet(), "Floor")
        for s in (-1, 1):
            box(f"StepWing{st}{s}", (x + 0.12, s * 0.50, top / 2), (0.28, 1.00, top), M_Carpet(), "Floor", yaw=s * -22)
        box(f"StepStrip{st}", (x + 0.26, 0.0, top - 0.025), (0.02, 0.6, 0.04), M_Strip(), "Lights")

def rails():
    RR = RC + 0.10
    arc_sweep("TopRail", [(RR - 0.03, HC + 0.93), (RR + 0.03, HC + 0.93), (RR + 0.03, HC + 0.99), (RR - 0.03, HC + 0.99)], 50, 310, M_Rail(), "Rails")
    arc_sweep("LowRail", [(RR - 0.015, HC + 0.52), (RR + 0.015, HC + 0.52), (RR + 0.015, HC + 0.55), (RR - 0.015, HC + 0.55)], 50, 310, M_PostGrey(), "Rails")
    for i in range(16):
        a = 50 + (310 - 50) * i / 15
        wallbox(f"Post{i}", a, RR, HC + 0.47, 0.02, 0.07, 0.94, M_PostGrey(), "Rails")
    for s in (-1, 1):
        px, py = 1.76, s * 1.32
        lathe(f"LampPost{s}", [(0.0, HC / 2), (0.07, HC / 2), (0.07, HC / 2 + 1.2), (0.0, HC / 2 + 1.2)], M_Black(), "Rails", 24)
        bpy.data.objects[f"LampPost{s}"].location = B(px, py, 0)
        for k in range(3):
            o = lathe(f"LampRing{s}{k}", [(0.0, 0), (0.076, 0), (0.076, 0.05), (0.0, 0.05)], M_Strip(), "Lights", 24)
            o.location = B(px, py, HC / 2 + 0.70 + k * 0.14)
        o = lathe(f"TableStem{s}", [(0.0, HC), (0.04, HC), (0.04, HC + 0.60), (0.0, HC + 0.60)], M_Black(), "Rails", 16); o.location = B(px - 0.30, py + s * 0.30, 0)
        box(f"TableTop{s}", (px - 0.30, py + s * 0.30, HC + 0.86), (0.70, 0.34, 0.04), M_Black(), "Rails", yaw=s * 30, bevel=0.015)

def walls_ceiling():
    arc_sweep("WallLower", [(R + 0.04, HD), (R + 0.16, HD), (R + 0.16, HD + 1.5), (R + 0.04, HD + 1.5)], VS, 360 - VS, M_PanelGrey(), "Walls", cap=False)
    arc_sweep("WallUpper", [(R + 0.04, HD + 1.5), (R + 0.16, HD + 1.5), (R + 0.16, H + 0.02), (R + 0.04, H + 0.02)], VS, 360 - VS, M_PanelDark(), "Walls", cap=False)
    arc_sweep("CoveStrip", [(R - 0.02, H - 0.36), (R + 0.06, H - 0.36), (R + 0.06, H - 0.32), (R - 0.02, H - 0.32)], VS, 360 - VS, M_Strip(), "Lights")
    lathe("Ceiling", [(0.0, H), (R + 0.2, H), (R + 0.2, H + 0.12), (0.0, H + 0.12)], M_PanelDark(), "Ceiling", smooth=False)
    for i in range(10):
        a = i * 36 + 18
        wallbox(f"CeilPanel{i}", a, R * 0.72, H - 0.02, 0.70, 0.44, 0.03, M_Strip(), "Lights")
    lathe("Dome", [(0.0, H - 0.34), (0.55, H - 0.26), (0.82, H - 0.10), (0.86, H), (0.0, H)], M_PanelDark(), "Ceiling", 48)
    soft = mat("LightSoft", WHITE, 0.8, emit=(0.95, 0.97, 1.0), strength=1.5)
    lathe("DomeRing", [(0.60, H - 0.21), (0.84, H - 0.21), (0.84, H - 0.17), (0.60, H - 0.17)], soft, "Lights", 48)
    lathe("DomeCentre", [(0.0, H - 0.36), (0.30, H - 0.36), (0.30, H - 0.32), (0.0, H - 0.32)], soft, "Lights", 48)

def near_lift(a, margin):
    return any(abs((a - L + 180) % 360 - 180) < LIFT_HALF + margin for L in LIFTS)

def station_unit(a, chord, name):
    """One perimeter station: grey cabinet with three light bars at the kick, and above the desk a single black
    curved hood that leans back from the desk and curls over at the top, carrying the Okudagram across its face."""
    half = STEP / 2 - 1.2
    # Cabinet under the desk and the desk's grey lip.
    arc_sweep(f"Cab_{name}", [(R - 0.72, HD), (R - 0.05, HD), (R - 0.05, HD + 0.80), (R - 0.72, HD + 0.80)], a - half, a + half, M_PanelGrey(), "Consoles")
    light_bars(f"Cab_{name}", a, R - 0.71, HD + 0.08, chord - 0.30)
    arc_sweep(f"Lip_{name}", [(R - 0.76, HD + 0.78), (R - 0.60, HD + 0.78), (R - 0.60, HD + 0.84), (R - 0.76, HD + 0.84)], a - half, a + half, M_PanelGrey(), "Consoles")
    # The hood: profile from the top lip down the raked face to the desk front, in (r, z). Screen first, then a
    # slightly larger black shell behind it closed round the back.
    face = [(R - 0.52, HD + 2.25), (R - 0.30, HD + 2.18), (R - 0.14, HD + 1.95), (R - 0.08, HD + 1.60), (R - 0.12, HD + 1.25), (R - 0.26, HD + 1.02), (R - 0.45, HD + 0.92), (R - 0.60, HD + 0.86)]
    arc_sweep(f"Scr_{name}", face, a - half + 0.8, a + half - 0.8, M_Screen(name), "Screens", closed=False, cap=False, uv=True)
    shell = [(r + 0.03, z + 0.03) for (r, z) in face] + [(R + 0.02, HD + 0.80), (R + 0.02, HD + 2.32), (R - 0.52, HD + 2.32)]
    arc_sweep(f"Hood_{name}", shell, a - half, a + half, M_Black(), "Consoles")

def stations():
    chord = 2 * R * math.sin(math.radians(STEP / 2))
    order = ["Weapons", "Tactical", "Communications", "Science", "Engineering", "Environmental", "Navigation"]
    k = 0
    a = VS + STEP / 2 + 4
    while a < 360 - VS - STEP * 0.4:
        if near_lift(a, STEP * 0.45):
            a += STEP; continue
        aft = abs((a - 180 + 180) % 360 - 180) < 8
        if not aft:
            name = order[k % len(order)]; k += 1
            station_unit(a, chord, name)
        else:
            # The master situation display over the aft bench: a shallow hood without a desk.
            half = STEP / 2 - 1.2
            face = [(R - 0.30, HD + 2.25), (R - 0.12, HD + 2.05), (R - 0.06, HD + 1.60), (R - 0.10, HD + 1.20)]
            arc_sweep("Scr_MasterSituation", face, a - half + 0.8, a + half - 0.8, M_Screen("MasterSituation"), "Screens", closed=False, cap=False, uv=True)
            shell = [(r + 0.03, z + 0.03) for (r, z) in face] + [(R + 0.02, HD + 1.14), (R + 0.02, HD + 2.32), (R - 0.30, HD + 2.32)]
            arc_sweep("Hood_Master", shell, a - half, a + half, M_Black(), "Consoles")
        a += STEP
    # Pilasters with a light bar up each edge.
    a = VS + 4
    while a <= 360 - VS - 3:
        if not near_lift(a, 8):
            wallbox(f"Pilaster{int(a)}", a, R - 0.30, (HD + H) / 2, 0.64, 0.18, H - HD, M_PanelGrey(), "Walls", bevel=0.012)
            c, s = ang(a); tc, ts = ang(a + 90)
            for sgn in (-1, 1):
                z0 = HD + 0.9; hgt = H - HD - 1.1
                box(f"PilBar{int(a)}{sgn}", ((R - 0.63) * c + tc * sgn * 0.075, (R - 0.63) * s + ts * sgn * 0.075, z0 + hgt / 2), (0.02, 0.025, hgt), M_Strip(), "Lights", yaw=a)
        a += STEP

def viewscreen():
    VB, VT = 0.95, 2.55
    XW = R * math.cos(math.radians(VS)) - 0.10      # the flat front wall sits just inside the ring
    WW = 2 * R * math.sin(math.radians(VS))         # opening width across the chord
    # Flat front wall panel with the opening, in four pieces, and a thick pale frame round it.
    box("FrontSill", (XW, 0, (HD + VB) / 2), (0.12, WW + 0.6, VB - HD), M_PanelGrey(), "Walls")
    box("FrontLintel", (XW, 0, (VT + H + 0.02) / 2), (0.12, WW + 0.6, H + 0.02 - VT), M_PanelDark(), "Walls")
    for s in (-1, 1):
        box(f"FrontJamb{s}", (XW, s * (WW / 2 + 0.6), (HD + H) / 2), (0.12, 1.2, H - HD), M_PanelGrey(), "Walls")
    fw = 0.16
    box("FrameLo", (XW - 0.06, 0, VB - fw / 2), (0.14, WW + 2 * fw, fw), M_PanelGrey(), "Walls", bevel=0.02)
    box("FrameHi", (XW - 0.06, 0, VT + fw / 2), (0.14, WW + 2 * fw, fw), M_PanelGrey(), "Walls", bevel=0.02)
    for s in (-1, 1):
        box(f"FrameSide{s}", (XW - 0.06, s * (WW / 2 + fw / 2), (VB + VT) / 2), (0.14, fw, VT - VB + 2 * fw), M_PanelGrey(), "Walls", bevel=0.02)
        box(f"FrameBar{s}", (XW - 0.14, s * (WW / 2 - 0.02), (VB + VT) / 2), (0.02, 0.025, VT - VB), M_Strip(), "Lights")
    light_bars("Sill", 0, XW - 0.07, HD + 0.08, WW - 0.4)
    box("ClockFront", (XW - 0.07, 0, VT + 0.30), (0.02, 0.44, 0.10), M_Clock(), "Lights")
    for s in (-1, 1):
        wallbox(f"ScreenPillar{s}", s * (VS + 1.5), R + 0.01, (HD + H) / 2, 0.34, 0.24, H - HD, M_PanelGrey(), "Walls", bevel=0.012)

def lifts_and_bench():
    for L in LIFTS:
        wallbox(f"LiftSurround{int(L)}", L, R + 0.08, HD + 1.10, 0.08, 1.50, 2.20, M_PanelGrey(), "Walls", bevel=0.01)
        wallbox(f"LiftDoors{int(L)}", L, R + 0.01, HD + 1.00, 0.04, 1.10, 2.00, M_Burgundy(), "Walls")
        wallbox(f"LiftSeam{int(L)}", L, R - 0.015, HD + 1.00, 0.01, 0.02, 1.96, M_Black(), "Walls")
        wallbox(f"LiftSign{int(L)}", L, R - 0.01, HD + 2.12, 0.02, 0.40, 0.07, M_Clock(), "Lights")
        for s in (-1, 1):
            wallbox(f"LiftBar{int(L)}{s}", L + s * 7.6, R + 0.03, HD + 1.10, 0.02, 0.03, 2.16, M_Strip(), "Lights")
    wallbox("BenchSeat", 180, R - 0.32, HD + 0.22, 0.52, 3.0, 0.44, M_Burgundy(), "Furniture", bevel=0.03)
    wallbox("BenchBack", 180, R - 0.10, HD + 0.62, 0.14, 3.0, 0.44, M_Burgundy(), "Furniture", bevel=0.03)
    light_bars("Bench", 180, R - 0.59, HD + 0.08, 2.96)
    wallbox("Clock", 180, R + 0.02, H - 0.48, 0.02, 0.36, 0.09, M_Clock(), "Lights")

def chair(name, pos, yaw, tall=False):
    """Grey seat and back on a black column and round base, black hoop arms. Perimeter chairs have the egg back;
    the helm chairs a tall black-shelled back."""
    x, y, z = pos
    o = lathe(f"{name}_Base", [(0.0, 0.0), (0.27, 0.0), (0.27, 0.06), (0.0, 0.06)], M_Black(), "Furniture", 32); o.location = B(x, y, z)
    o = lathe(f"{name}_Col", [(0.0, 0.06), (0.06, 0.06), (0.06, 0.49), (0.0, 0.49)], M_Black(), "Furniture", 16); o.location = B(x, y, z)
    c, s = ang(yaw)
    box(f"{name}_Seat", (x, y, z + 0.50), (0.46, 0.46, 0.09), M_Seat(), "Furniture", yaw=yaw, bevel=0.03)
    if tall:
        box(f"{name}_Shell", (x - 0.24 * c, y - 0.24 * s, z + 0.98), (0.07, 0.42, 1.00), M_Black(), "Furniture", yaw=yaw, pitch=-6, bevel=0.025)
        box(f"{name}_Pad", (x - 0.19 * c, y - 0.19 * s, z + 0.96), (0.04, 0.34, 0.80), M_Seat(), "Furniture", yaw=yaw, pitch=-6, bevel=0.015)
    else:
        box(f"{name}_Spine", (x - 0.20 * c, y - 0.20 * s, z + 0.62), (0.06, 0.14, 0.30), M_Black(), "Furniture", yaw=yaw)
        bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=16, radius=0.5)
        egg = bpy.context.active_object; egg.name = f"{name}_Back"
        egg.scale = (0.12, 0.44, 0.64)
        egg.rotation_euler = (0, 0, math.radians(-yaw))
        egg.location = B(x - 0.23 * c, y - 0.23 * s, z + 0.90)
        for col in egg.users_collection: col.objects.unlink(egg)
        finish(egg, M_Seat(), "Furniture", smooth=True)
    tc, ts = ang(yaw + 90)
    for sgn in (-1, 1):
        box(f"{name}_Arm{sgn}", (x - 0.04 * c + tc * sgn * 0.25, y - 0.04 * s + ts * sgn * 0.25, z + 0.72), (0.34, 0.035, 0.035), M_Black(), "Furniture", yaw=yaw)
        box(f"{name}_ArmUp{sgn}", (x - 0.20 * c + tc * sgn * 0.25, y - 0.20 * s + ts * sgn * 0.25, z + 0.62), (0.035, 0.035, 0.22), M_Black(), "Furniture", yaw=yaw)

def helm():
    """One wide console for the two helmsmen: a shallow V of two grey wings with black tops outlined in white and
    slanted black end panels, a grey centre block, and between the two tall-backed chairs the schematic pedestal
    whose large screen faces the captain."""
    X0 = 2.30
    for s in (-1, 1):
        yaw = s * 18
        cx, cy = X0, s * 0.72
        box(f"HelmWing{s}", (cx, cy, 0.44), (0.60, 1.50, 0.88), M_PanelGrey(), "Consoles", yaw=yaw, bevel=0.02)
        box(f"HelmTop{s}", (cx, cy, 0.90), (0.70, 1.56, 0.05), M_Black(), "Consoles", yaw=yaw, pitch=-9, bevel=0.01)
        box(f"HelmOutline{s}", (cx, cy, 0.927), (0.72, 1.58, 0.012), M_Strip(), "Lights", yaw=yaw, pitch=-9)
        box(f"HelmInlay{s}", (cx + 0.01, cy, 0.934), (0.64, 1.50, 0.012), M_Black(), "Consoles", yaw=yaw, pitch=-9)
        name = "Helm" if s < 0 else "Navigation"
        screen(f"Scr_{name}", (cx + 0.03, cy, 0.944), 1.30, 0.56, M_Screen(name), "Screens", yaw=yaw + 180, pitch=-81)
        # Slanted black end panel with three white bars.
        c, sn = ang(yaw); tc, ts = ang(yaw + 90)
        ex, ey = cx + tc * s * 0.82, cy + ts * s * 0.82
        box(f"HelmEnd{s}", (ex, ey, 0.55), (0.62, 0.08, 0.70), M_Black(), "Consoles", yaw=yaw, roll=s * 25, bevel=0.01)
        for k in range(3):
            box(f"HelmBar{s}{k}", (ex - 0.14 * c + k * 0.13 * c, ey - 0.14 * sn + k * 0.13 * sn, 0.42 + k * 0.09), (0.03, 0.02, 0.24), M_Strip(), "Lights", yaw=yaw, roll=s * 25)
    box("HelmCentre", (X0 - 0.18, 0.0, 0.44), (0.60, 0.56, 0.88), M_PanelGrey(), "Consoles", bevel=0.02)
    box("HelmCentreLight", (X0 - 0.485, 0.0, 0.40), (0.02, 0.36, 0.44), M_Strip(), "Lights")
    # The pedestal between the helm chairs: grey base with a lit control panel, black bezel, big round schematic.
    box("Pedestal", (X0 - 0.95, 0.0, 0.34), (0.44, 0.56, 0.68), M_PanelGrey(), "Consoles", bevel=0.015)
    box("PedestalPanel", (X0 - 1.175, 0.0, 0.50), (0.01, 0.44, 0.16), M_Buttons(), "Consoles")
    box("PedestalHead", (X0 - 0.95, 0.0, 1.08), (0.34, 0.66, 0.84), M_Black(), "Consoles", bevel=0.012)
    screen("Scr_Schematic", (X0 - 1.125, 0.0, 1.06), 0.60, 0.60, M_Screen("Schematic"), "Screens", yaw=180)
    box("PedestalStrip", (X0 - 1.125, 0.0, 1.44), (0.01, 0.50, 0.03), M_Strip(), "Lights")
    for s in (-1, 1):
        chair(f"HelmChair{s}", (X0 - 0.95, s * 0.68, 0.0), 0.0, tall=True)

def captain():
    """The one captain's chair, at the centre-rear of the command platform: tall quilted grey back with a headrest,
    black arms with small panels, grey column on a black base."""
    cx = -0.30
    o = lathe("CaptBase", [(0.0, HC), (0.42, HC), (0.42, HC + 0.05), (0.0, HC + 0.05)], M_Black(), "Furniture", 48); o.location = B(cx, 0, 0)
    o = lathe("CaptCol", [(0.0, HC + 0.05), (0.12, HC + 0.05), (0.12, HC + 0.40), (0.0, HC + 0.40)], M_PanelGrey(), "Furniture", 24); o.location = B(cx, 0, 0)
    box("CaptSeat", (cx - 0.02, 0.0, HC + 0.46), (0.60, 0.58, 0.12), M_Seat(), "Furniture", bevel=0.04)
    for k in range(5):
        box(f"CaptCushion{k}", (cx - 0.31 - k * 0.012, 0.0, HC + 0.56 + k * 0.145), (0.12, 0.54 - k * 0.02, 0.135), M_Seat(), "Furniture", pitch=-6, bevel=0.04)
    box("CaptSpine", (cx - 0.39, 0.0, HC + 0.92), (0.04, 0.58, 0.92), M_Black(), "Furniture", pitch=-6, bevel=0.01)
    for s in (-1, 1):
        box(f"CaptArm{s}", (cx - 0.02, s * 0.36, HC + 0.70), (0.56, 0.10, 0.09), M_Black(), "Furniture", bevel=0.02)
        box(f"CaptArmPanel{s}", (cx + 0.14, s * 0.36, HC + 0.75), (0.18, 0.08, 0.015), M_Buttons(), "Consoles")

def crew_chairs():
    chord = 2 * R * math.sin(math.radians(STEP / 2))
    a = VS + STEP / 2 + 4
    while a < 360 - VS - STEP * 0.4:
        if not near_lift(a, STEP * 0.45) and abs((a - 180 + 180) % 360 - 180) >= 8:
            c, s = ang(a)
            chair(f"Chair{int(a)}", ((R - 1.30) * c, (R - 1.30) * s, HD), a)
        a += STEP

def fix_normals():
    """Closed shells get consistent outward normals; open screen strips face the captain's eye (the game material
    is one-sided, so a screen wound the wrong way is culled and shows the black hood behind it)."""
    eye = B(0.12, 0.0, 1.65)
    for o in bpy.data.objects:
        if o.type != 'MESH':
            continue
        bm = bmesh.new(); bm.from_mesh(o.data)
        if o.name.startswith("Scr_"):
            for f in bm.faces:
                c = o.matrix_world @ f.calc_center_median()
                n = (o.matrix_world.to_3x3() @ f.normal).normalized()
                if n.dot(eye - c) < 0:
                    f.normal_flip()
        else:
            # Leave the rest alone: the sweeps and lathes are wound with their inner faces toward the room, and a
            # generic recalc on an open ring strip orients it away from its own centroid, i.e. away from the viewer.
            bm.free(); continue
        bm.to_mesh(o.data); bm.free(); o.data.update()

def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.unit_settings.system = 'METRIC'
    floors(); rails(); walls_ceiling(); stations(); viewscreen(); lifts_and_bench(); helm(); captain(); crew_chairs()
    fix_normals()
    # World: black, with a little ambient so the previews read.
    w = bpy.context.scene.world or bpy.data.worlds.new("World"); bpy.context.scene.world = w; w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.02, 0.02, 0.025, 1); w.node_tree.nodes["Background"].inputs[1].default_value = 1.0

def previews(outdir):
    os.makedirs(outdir, exist_ok=True)
    scn = bpy.context.scene
    scn.render.engine = 'BLENDER_EEVEE_NEXT' if hasattr(bpy.types, "SceneEEVEE") and 'BLENDER_EEVEE_NEXT' in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items] else 'BLENDER_EEVEE'
    scn.render.resolution_x = 1280; scn.render.resolution_y = 720; scn.render.resolution_percentage = 100
    cam_data = bpy.data.cameras.new("Cam"); cam_data.lens = 20
    cam = bpy.data.objects.new("Cam", cam_data); scn.collection.objects.link(cam); scn.camera = cam
    # Fill lights so the previews are legible (the strips are emissive already).
    for i in range(6):
        c, s = ang(i * 60 + 30)
        ld = bpy.data.lights.new(f"Fill{i}", 'POINT'); ld.energy = 400; ld.color = (0.9, 0.93, 1.0); ld.shadow_soft_size = 0.3
        lo = bpy.data.objects.new(f"Fill{i}", ld); scn.collection.objects.link(lo); lo.location = B(R * 0.6 * c, R * 0.6 * s, H - 1.0)
    eye = B(0.12, 0.0, 1.65)
    views = {"fwd": (0, -4), "helm": (0, -30), "stbd": (75, -5), "aft": (180, -6), "port": (-75, -5)}
    for name, (yaw, pitch) in views.items():
        cam.location = eye
        c, s = ang(yaw)
        target = B(0.12 + 5 * c * math.cos(math.radians(pitch)), 5 * s * math.cos(math.radians(pitch)), 1.65 + 5 * math.sin(math.radians(pitch)))
        d = target - cam.location
        cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
        scn.render.filepath = os.path.join(outdir, f"prev_{name}.png"); bpy.ops.render.render(write_still=True)
    # Overview from above-aft like the reference, ceiling hidden.
    coll("Ceiling").hide_render = True
    cam.location = B(-9.0, 6.0, 9.0); d = B(0.5, 0.0, 0.8) - cam.location
    cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler(); cam_data.lens = 28
    scn.render.filepath = os.path.join(outdir, "prev_overview.png"); bpy.ops.render.render(write_still=True)
    coll("Ceiling").hide_render = False

def export(path):
    bpy.ops.object.select_all(action='DESELECT')
    for o in bpy.data.objects:
        if o.type == 'MESH': o.select_set(True)
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, apply_unit_scale=True, global_scale=1.0, axis_forward='-Y', axis_up='Z', object_types={'MESH'}, use_mesh_modifiers=True, mesh_smooth_type='FACE', add_leaf_bones=False, bake_anim=False, path_mode='COPY', embed_textures=False)

if __name__ == "__main__":
    build()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "bridge_st6.blend"))
    if "--render" in ARGS:
        previews(os.path.join(HERE, "previews"))
    if "--export" in ARGS:
        export(os.path.join(HERE, "SM_BridgeRefit.fbx"))
    print("BRIDGE BUILD DONE objects", len(bpy.data.objects))
