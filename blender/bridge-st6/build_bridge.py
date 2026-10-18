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
WHITE = (0.9, 0.92, 0.95); BURG = (0.28, 0.045, 0.055); SEAT = (0.50, 0.50, 0.52); RAIL = (0.05, 0.05, 0.055)
def M_PanelGrey(): return mat("PanelGrey", GREY, 0.55)
def M_PanelDark(): return mat("PanelDark", DARK, 0.7)
def M_Carpet(): return mat("Carpet", CARPET, 0.95)
def M_Black(): return mat("ConsoleBlack", BLACK, 0.35)
def M_Strip(): return mat("LightStrip", WHITE, 0.8, emit=(0.95, 0.97, 1.0), strength=12.0)
def M_Burgundy(): return mat("Burgundy", BURG, 0.7)
def M_Seat(): return mat("SeatGrey", SEAT, 0.95)
def M_Rail(): return mat("RailBlack", RAIL, 0.45, 0.2)
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
            # Blender's UV origin is the image's bottom-left (the FBX importer converts to Unreal's), so the top of
            # the profile gets v = 1; u runs with increasing bearing. Checked against the station screens in game.
            u = i / (n - 1)
            uvs[base + 2 * i] = (u, 1.0 - lens[j] / total); uvs[base + 2 * i + 1] = (u, 1.0 - lens[j + 1] / total)
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
    # Blender UVs: origin bottom-left of the image. The viewer stands inboard looking outboard (+radial) with
    # their right along local -y, so the image's left (u = 0) is local +y and its top (v = 1) is local +z.
    # Checked against the text on the station screens in the previews and in game.
    uvs = {0: (1.0, 0.0), 1: (0.0, 0.0), 2: (0.0, 1.0), 3: (1.0, 1.0)}
    return mesh_from(name, verts, [(0, 3, 2, 1)], material, group, uvs=uvs)

def wallbox(name, deg, radial, z, thick, width, height, material, group, pitch=0.0, bevel=0.0):
    c, s = ang(deg)
    return box(name, (radial * c, radial * s, z), (thick, width, height), material, group, yaw=deg, pitch=pitch, bevel=bevel)

def light_bars(prefix, deg, radial, z0, width, count=3, pitch=0.11):
    for i in range(count):
        wallbox(f"{prefix}_bar{i}", deg, radial, z0 + i * pitch, 0.02, width, 0.05, M_Strip(), "Lights")

def prism(name, outline, z0, z1, material, group, smooth=False):
    """A flat-topped slab from a closed (x, y) outline: top, bottom and side walls."""
    n = len(outline)
    verts = [(x, y, z1) for (x, y) in outline] + [(x, y, z0) for (x, y) in outline]
    faces = [tuple(range(n)), tuple(reversed(range(n, 2 * n)))]
    for i in range(n):
        j = (i + 1) % n
        faces.append((i, n + i, n + j, j))
    return mesh_from(name, verts, faces, material, group, smooth=smooth)

def deck_outline(radius, x_edge, step=3.0):
    """Circle of the given radius cut by the chord x = x_edge (the well side)."""
    a0 = math.degrees(math.acos(min(1.0, x_edge / radius)))
    pts = []
    a = a0
    while a < 360.0 - a0 - 1e-6:
        c, sn = ang(a); pts.append((radius * c, radius * sn)); a += step
    c, sn = ang(360.0 - a0); pts.append((radius * c, radius * sn))
    return pts

# ---- the set -----------------------------------------------------------------------------------------------------
R = 5.6          # wall radius
HD = 0.55        # the deck: command platform and station walkway, above the front well's floor
HC = HD          # the captain's chair stands on the same deck (Versiga's v01/v02: no step between helm and captain)
H = 3.35         # ceiling
VS = 34.0        # viewscreen half-angle
RC = 2.60        # handrail radius round the command area (the helm console sits inside it)
XE = 2.45        # x of the deck's front edge; the well lies beyond it, toward the viewscreen
LIFTS = (150.0, 210.0); LIFT_HALF = 12.0
STEP = 32.0

def floors():
    # The well floor under everything, then the raised deck: a disc cut off at the chord in front, with a black
    # front face lit under its lip. The chevron stair drops from the chord's centre into the well.
    lathe("WellFloor", [(0.0, -0.06), (R + 0.15, -0.06), (R + 0.15, 0.0), (0.0, 0.0)], M_Carpet(), "Floor", smooth=False)
    prism("Deck", deck_outline(R + 0.15, XE), 0.0, HD, M_Carpet(), "Floor")
    half = math.sqrt((R + 0.1) ** 2 - XE ** 2)
    box("DeckFace", (XE + 0.01, 0.0, HD / 2 - 0.02), (0.02, 2 * half, HD - 0.04), M_Black(), "Floor")
    box("DeckLipStrip", (XE + 0.025, 0.0, HD - 0.035), (0.02, 2 * half, 0.035), M_Strip(), "Lights")
    for st in range(2):
        top = HD * (2 - st) / 3.0; x = XE + 0.10 + st * 0.36
        box(f"Step{st}", (x, 0.0, top / 2), (0.36, 2.00, top), M_Carpet(), "Floor")
        box(f"StepFace{st}", (x + 0.175, 0.0, top / 2 - 0.02), (0.02, 2.00, top - 0.04), M_Black(), "Floor")
        for sgn in (-1, 1):
            box(f"StepWing{st}{sgn}", (x - 0.02, sgn * 1.15, top / 2), (0.36, 0.60, top), M_Carpet(), "Floor", yaw=sgn * -30)
            box(f"StepWingFace{st}{sgn}", (x + 0.15, sgn * 1.24, top / 2 - 0.02), (0.02, 0.60, top - 0.04), M_Black(), "Floor", yaw=sgn * -30)
        box(f"StepStrip{st}", (x + 0.18, 0.0, top - 0.03), (0.02, 1.96, 0.03), M_Strip(), "Lights")

def rails():
    """The black handrail round the command area with its leaning grey posts and a lower bar, the knee rail along
    the deck's front edge either side of the stair, round side tables at the rail ends, and the ringed lamp posts:
    two at the stair's top corners and one on the well floor at its foot."""
    RR = RC
    arc_sweep("TopRail", [(RR - 0.03, HD + 0.96), (RR + 0.03, HD + 0.96), (RR + 0.03, HD + 1.02), (RR - 0.03, HD + 1.02)], 32, 328, M_Rail(), "Rails")
    arc_sweep("LowRail", [(RR - 0.015, HD + 0.56), (RR + 0.015, HD + 0.56), (RR + 0.015, HD + 0.59), (RR - 0.015, HD + 0.59)], 32, 328, M_PostGrey(), "Rails")
    for i in range(17):
        a = 32 + (328 - 32) * i / 16
        wallbox(f"Post{i}", a, RR + 0.03, HD + 0.49, 0.02, 0.07, 0.98, M_PostGrey(), "Rails", pitch=-4)
    # Knee rail along the front edge, broken at the stair.
    for sgn in (-1, 1):
        y0, y1 = 1.25, 2.55
        box(f"KneeRail{sgn}", (XE - 0.12, sgn * (y0 + y1) / 2, HD + 0.46), (0.025, y1 - y0, 0.025), M_PostGrey(), "Rails")
        for k in range(3):
            box(f"KneePost{sgn}{k}", (XE - 0.12, sgn * (y0 + (y1 - y0) * k / 2), HD + 0.23), (0.02, 0.05, 0.46), M_PostGrey(), "Rails")
        # Side tables and lamp posts at the rail ends.
        tx, ty = XE - 0.35, sgn * 2.30
        o = lathe(f"TableStem{sgn}", [(0.0, HD), (0.16, HD), (0.16, HD + 0.03), (0.035, HD + 0.03), (0.035, HD + 0.72), (0.0, HD + 0.72)], M_Black(), "Rails", 24); o.location = B(tx, ty, 0)
        o = lathe(f"TableTop{sgn}", [(0.0, HD + 0.72), (0.24, HD + 0.72), (0.24, HD + 0.75), (0.0, HD + 0.75)], M_Black(), "Rails", 32); o.location = B(tx, ty, 0)
        lamp_post(f"Lamp{sgn}", XE - 0.12, sgn * 2.75, HD)
    lamp_post("LampFront", XE + 1.05, 0.0, 0.0)

def lamp_post(name, x, y, z):
    o = lathe(f"{name}_Post", [(0.0, z), (0.075, z), (0.075, z + 1.32), (0.0, z + 1.32)], M_Black(), "Rails", 24); o.location = B(x, y, 0)
    o = lathe(f"{name}_Cap", [(0.0, z + 1.32), (0.085, z + 1.32), (0.085, z + 1.36), (0.0, z + 1.36)], M_PostGrey(), "Rails", 24); o.location = B(x, y, 0)
    for k in range(2):
        for j in range(3):
            o = lathe(f"{name}_Ring{k}{j}", [(0.0, 0), (0.08, 0), (0.08, 0.035), (0.0, 0.035)], M_Strip(), "Lights", 24)
            o.location = B(x, y, z + 0.30 + k * 0.62 + j * 0.075)

def walls_ceiling():
    arc_sweep("WallLower", [(R + 0.04, 0.0), (R + 0.16, 0.0), (R + 0.16, HD + 2.42), (R + 0.04, HD + 2.42)], VS, 360 - VS, M_PanelGrey(), "Walls", cap=False)
    # Above the station ledges the wall is dark and steps in slightly toward the ceiling.
    arc_sweep("WallUpper", [(R - 0.10, HD + 2.42), (R + 0.16, HD + 2.42), (R + 0.16, H + 0.02), (R - 0.10, H + 0.02)], VS, 360 - VS, M_PanelDark(), "Walls", cap=False)
    lathe("Ceiling", [(0.0, H), (R + 0.2, H), (R + 0.2, H + 0.12), (0.0, H + 0.12)], M_PanelDark(), "Ceiling", smooth=False)
    # A ring of square recessed light panels and, inside it, small round can lights.
    for i in range(12):
        a = i * 30 + 15
        wallbox(f"CeilPanel{i}", a, R * 0.80, H - 0.015, 0.62, 0.42, 0.03, M_Strip(), "Lights")
        wallbox(f"CeilPanelRim{i}", a, R * 0.80, H - 0.005, 0.70, 0.50, 0.012, M_PanelGrey(), "Ceiling")
    soft = mat("LightSoft", WHITE, 0.8, emit=(0.95, 0.97, 1.0), strength=1.5)
    for i in range(8):
        a = i * 45 + 22
        c, sn = ang(a)
        o = lathe(f"CanLight{i}", [(0.0, H - 0.03), (0.09, H - 0.03), (0.09, H), (0.0, H)], M_Strip(), "Lights", 20); o.location = B(R * 0.52 * c, R * 0.52 * sn, 0)
        o = lathe(f"CanRim{i}", [(0.09, H - 0.035), (0.12, H - 0.035), (0.12, H), (0.09, H)], M_PostGrey(), "Ceiling", 20); o.location = B(R * 0.52 * c, R * 0.52 * sn, 0)
    # The central fixture: a shallow grey dome with a bright ring and centre.
    lathe("Dome", [(0.0, H - 0.34), (0.55, H - 0.26), (0.82, H - 0.10), (0.86, H), (0.0, H)], M_PostGrey(), "Ceiling", 48)
    lathe("DomeRing", [(0.60, H - 0.21), (0.84, H - 0.21), (0.84, H - 0.17), (0.60, H - 0.17)], soft, "Lights", 48)
    lathe("DomeCentre", [(0.0, H - 0.36), (0.30, H - 0.36), (0.30, H - 0.32), (0.0, H - 0.32)], soft, "Lights", 48)

def near_lift(a, margin):
    return any(abs((a - L + 180) % 360 - 180) < LIFT_HALF + margin for L in LIFTS)

def station_unit(a, chord, name):
    """One perimeter station after Versiga's v13: a light-grey cabinet with a dark recess holding three white bars,
    a raked black desk with a touch strip, and above it one tall black hood that leans back from the desk and curls
    forward at the top under a pale ledge, its whole face the Okudagram."""
    half = STEP / 2 - 1.1
    # Cabinet, the dark recess with the three bars, and the desk lip.
    arc_sweep(f"Cab_{name}", [(R - 0.78, HD), (R - 0.05, HD), (R - 0.05, HD + 0.74), (R - 0.78, HD + 0.74)], a - half, a + half, M_PanelGrey(), "Consoles")
    arc_sweep(f"CabRecess_{name}", [(R - 0.80, HD + 0.14), (R - 0.74, HD + 0.14), (R - 0.74, HD + 0.52), (R - 0.80, HD + 0.52)], a - half + 2.0, a + half - 2.0, M_PanelDark(), "Consoles")
    light_bars(f"Cab_{name}", a, R - 0.79, HD + 0.19, chord - 0.60, pitch=0.12)
    # Raked desk: black slope from the lip up to the foot of the hood, with a teal touch strip along it.
    arc_sweep(f"Desk_{name}", [(R - 0.84, HD + 0.74), (R - 0.84, HD + 0.80), (R - 0.34, HD + 0.98), (R - 0.34, HD + 0.92)], a - half, a + half, M_Black(), "Consoles")
    arc_sweep(f"DeskLip_{name}", [(R - 0.86, HD + 0.72), (R - 0.78, HD + 0.72), (R - 0.78, HD + 0.82), (R - 0.86, HD + 0.82)], a - half, a + half, M_PanelGrey(), "Consoles")
    arc_sweep(f"DeskTouch_{name}", [(R - 0.70, HD + 0.861), (R - 0.46, HD + 0.947), (R - 0.46, HD + 0.95), (R - 0.70, HD + 0.864)], a - half + 1.5, a + half - 1.5, M_Buttons(), "Consoles", closed=False, cap=False)
    # The hood: from the desk foot up and back, then curling forward under the ledge.
    face = [(R - 0.10, HD + 2.26), (R - 0.04, HD + 2.05), (R - 0.02, HD + 1.70), (R - 0.08, HD + 1.35), (R - 0.22, HD + 1.10), (R - 0.34, HD + 0.99)]
    arc_sweep(f"Scr_{name}", face, a - half + 0.6, a + half - 0.6, M_Screen(name), "Screens", closed=False, cap=False, uv=True)
    shell = [(r + 0.03, z + 0.02) for (r, z) in face] + [(R + 0.04, HD + 0.95), (R + 0.04, HD + 2.34), (R - 0.10, HD + 2.34)]
    arc_sweep(f"Hood_{name}", shell, a - half, a + half, M_Black(), "Consoles")
    # Pale rounded frame: thin grey strips just proud of the screen along its two sides and top.
    rim_out = [(r - 0.025, z) for (r, z) in face]
    rim_in = [(r - 0.015, z) for (r, z) in face]
    for sgn, aa in ((-1, a - half + 0.3), (1, a + half - 0.3)):
        arc_sweep(f"HoodRim_{name}{sgn}", rim_out, aa - 0.3, aa + 0.3, M_PanelGrey(), "Consoles", closed=False, cap=False)
    arc_sweep(f"HoodRimTop_{name}", [(R - 0.16, HD + 2.33), (R - 0.09, HD + 2.26), (R - 0.07, HD + 2.18)], a - half, a + half, M_PanelGrey(), "Consoles", closed=False, cap=False)
    arc_sweep(f"HoodRimBot_{name}", [(R - 0.33, HD + 1.00), (R - 0.37, HD + 0.95)], a - half, a + half, M_PanelGrey(), "Consoles", closed=False, cap=False)
    # Pale ledge over the hood and a dark soffit above it.
    arc_sweep(f"Ledge_{name}", [(R - 0.30, HD + 2.30), (R + 0.04, HD + 2.30), (R + 0.04, HD + 2.42), (R - 0.30, HD + 2.42)], a - half - 0.5, a + half + 0.5, M_PanelGrey(), "Walls", cap=False)

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
    # Pilasters: light-grey slabs between the stations, flaring out at the foot to the cabinet line, with a white
    # light bar up each inner edge from the deck to the ceiling.
    a = VS + 4
    while a <= 360 - VS - 3:
        if not near_lift(a, 8):
            wallbox(f"Pilaster{int(a)}", a, R - 0.22, HD + 0.95 + (H - HD - 0.95) / 2, 0.56, 0.34, H - HD - 0.95, M_PanelGrey(), "Walls", bevel=0.012)
            # The foot: a slab leaning in from the wall to the cabinet front.
            wallbox(f"PilFoot{int(a)}", a, R - 0.50, HD + 0.48, 0.14, 0.34, 1.06, M_PanelGrey(), "Walls", pitch=28, bevel=0.012)
            wallbox(f"PilFootFill{int(a)}", a, R - 0.24, HD + 0.44, 0.56, 0.34, 0.88, M_PanelGrey(), "Walls")
            c, sn = ang(a); tc, ts = ang(a + 90)
            for sgn in (-1, 1):
                z0 = HD + 0.98; hgt = H - HD - 1.02
                box(f"PilBar{int(a)}{sgn}", ((R - 0.51) * c + tc * sgn * 0.145, (R - 0.51) * sn + ts * sgn * 0.145, z0 + hgt / 2), (0.02, 0.03, hgt), M_Strip(), "Lights", yaw=a)
                box(f"PilBarFoot{int(a)}{sgn}", ((R - 0.57) * c + tc * sgn * 0.145, (R - 0.57) * sn + ts * sgn * 0.145, HD + 0.49), (0.02, 0.03, 1.00), M_Strip(), "Lights", yaw=a, pitch=28)
        a += STEP

def viewscreen():
    VB, VT = 1.05, 2.75
    XW = R * math.cos(math.radians(VS)) - 0.10      # the flat front wall sits just inside the ring
    WW = 2 * R * math.sin(math.radians(VS))         # opening width across the chord
    # Flat front wall panel with the opening, in four pieces, and a thick pale frame round it.
    box("FrontSill", (XW, 0, VB / 2), (0.12, WW + 0.6, VB), M_PanelGrey(), "Walls")
    box("FrontLintel", (XW, 0, (VT + H + 0.02) / 2), (0.12, WW + 0.6, H + 0.02 - VT), M_PanelDark(), "Walls")
    for s in (-1, 1):
        box(f"FrontJamb{s}", (XW, s * (WW / 2 + 0.6), H / 2), (0.12, 1.2, H), M_PanelGrey(), "Walls")
    fw = 0.16
    box("FrameLo", (XW - 0.06, 0, VB - fw / 2), (0.14, WW + 2 * fw, fw), M_PanelGrey(), "Walls", bevel=0.02)
    box("FrameHi", (XW - 0.06, 0, VT + fw / 2), (0.14, WW + 2 * fw, fw), M_PanelGrey(), "Walls", bevel=0.02)
    for s in (-1, 1):
        box(f"FrameSide{s}", (XW - 0.06, s * (WW / 2 + fw / 2), (VB + VT) / 2), (0.14, fw, VT - VB + 2 * fw), M_PanelGrey(), "Walls", bevel=0.02)
        box(f"FrameBar{s}", (XW - 0.14, s * (WW / 2 - 0.02), (VB + VT) / 2), (0.02, 0.025, VT - VB), M_Strip(), "Lights")
    light_bars("Sill", 0, XW - 0.07, 0.10, WW - 0.4)
    box("ClockFront", (XW - 0.07, 0, VT + 0.30), (0.02, 0.44, 0.10), M_Clock(), "Lights")
    for s in (-1, 1):
        wallbox(f"ScreenPillar{s}", s * (VS + 1.5), R + 0.01, H / 2, 0.34, 0.24, H, M_PanelGrey(), "Walls", bevel=0.012)

def lifts_and_bench():
    for L in LIFTS:
        wallbox(f"LiftSurround{int(L)}", L, R + 0.08, HD + 1.10, 0.08, 1.50, 2.20, M_PanelGrey(), "Walls", bevel=0.01)
        wallbox(f"LiftDoors{int(L)}", L, R + 0.01, HD + 1.00, 0.04, 1.10, 2.00, M_Burgundy(), "Walls")
        wallbox(f"LiftSeam{int(L)}", L, R - 0.015, HD + 1.00, 0.01, 0.02, 1.96, M_Black(), "Walls")
        wallbox(f"LiftSign{int(L)}", L, R - 0.01, HD + 2.12, 0.02, 0.40, 0.07, M_Clock(), "Lights")
        for s in (-1, 1):
            wallbox(f"LiftBar{int(L)}{s}", L + s * 7.6, R + 0.03, HD + 1.10, 0.02, 0.03, 2.16, M_Strip(), "Lights")
    wallbox("BenchBase", 180, R - 0.30, HD + 0.12, 0.50, 3.0, 0.24, M_PanelDark(), "Furniture")
    wallbox("BenchSeat", 180, R - 0.32, HD + 0.32, 0.52, 3.0, 0.16, M_Burgundy(), "Furniture", bevel=0.03)
    for k in range(7):
        off = -1.32 + k * 0.44
        c, sn = ang(180); tc, ts = ang(270)
        box(f"BenchCushion{k}", ((R - 0.10) * c + tc * off, (R - 0.10) * sn + ts * off, HD + 0.64), (0.14, 0.40, 0.48), M_Burgundy(), "Furniture", yaw=180, bevel=0.04)
    light_bars("Bench", 180, R - 0.56, HD + 0.12, 2.90, pitch=0.07)
    wallbox("Clock", 180, R + 0.02, H - 0.48, 0.02, 0.36, 0.09, M_Clock(), "Lights")

def egg(name, pos, yaw, scale, material):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=16, radius=0.5)
    o = bpy.context.active_object; o.name = name
    o.scale = scale
    o.rotation_euler = (0, 0, math.radians(-yaw))
    o.location = B(*pos)
    for col in o.users_collection: col.objects.unlink(o)
    return finish(o, material, "Furniture", smooth=True)

def chair(name, pos, yaw, tall=False):
    """Grey egg pad and seat on a black stem and round base, black hoop arms. The helm chairs carry a tall narrow
    black shell behind the pad, the angular fin seen from the captain's side in v08/v09."""
    x, y, z = pos
    c, s = ang(yaw)
    base_r = 0.36 if tall else 0.27
    o = lathe(f"{name}_Base", [(0.0, 0.0), (base_r, 0.0), (base_r, 0.07), (base_r - 0.04, 0.09), (0.0, 0.09)], M_Black(), "Furniture", 32); o.location = B(x, y, z)
    o = lathe(f"{name}_Col", [(0.0, 0.08), (0.055, 0.08), (0.055, 0.47), (0.0, 0.47)], M_Black(), "Furniture", 16); o.location = B(x, y, z)
    box(f"{name}_SeatShell", (x - 0.02 * c, y - 0.02 * s, z + 0.47), (0.46, 0.46, 0.05), M_Black(), "Furniture", yaw=yaw, bevel=0.02)
    egg(f"{name}_Seat", (x, y, z + 0.52), yaw, (0.48, 0.46, 0.10), M_Seat())
    if tall:
        # The fin: a narrow black shell rising from behind the seat, leaning back, with a slanted top.
        box(f"{name}_Shell", (x - 0.26 * c, y - 0.26 * s, z + 1.00), (0.07, 0.30, 1.05), M_Black(), "Furniture", yaw=yaw, pitch=-7, bevel=0.015)
        box(f"{name}_ShellTop", (x - 0.30 * c, y - 0.30 * s, z + 1.56), (0.07, 0.30, 0.16), M_Black(), "Furniture", yaw=yaw, pitch=-7, roll=18, bevel=0.012)
        egg(f"{name}_Back", (x - 0.20 * c, y - 0.20 * s, z + 1.02), yaw, (0.10, 0.44, 0.70), M_Seat())
    else:
        box(f"{name}_Spine", (x - 0.20 * c, y - 0.20 * s, z + 0.66), (0.05, 0.12, 0.34), M_Black(), "Furniture", yaw=yaw, pitch=-8)
        egg(f"{name}_Back", (x - 0.23 * c, y - 0.23 * s, z + 0.92), yaw, (0.10, 0.42, 0.60), M_Seat())
    tc, ts = ang(yaw + 90)
    for sgn in (-1, 1):
        box(f"{name}_Arm{sgn}", (x - 0.04 * c + tc * sgn * 0.25, y - 0.04 * s + ts * sgn * 0.25, z + 0.74), (0.36, 0.035, 0.035), M_Black(), "Furniture", yaw=yaw, bevel=0.01)
        box(f"{name}_ArmUp{sgn}", (x - 0.21 * c + tc * sgn * 0.25, y - 0.21 * s + ts * sgn * 0.25, z + 0.62), (0.035, 0.035, 0.26), M_Black(), "Furniture", yaw=yaw)

def helm():
    """The helm on the platform's front edge, after v01/v09: one long shallow-V console with a raked black top
    outlined in white, grey cabinet below with a lit column on its front, angled black end panels, grated side
    shelves at the ends; the two helm chairs with tall black shells; and between them the pedestal whose large
    screen faces the captain."""
    X0 = 1.95
    for sgn in (-1, 1):
        yaw = sgn * 16
        cx, cy = X0, sgn * 0.78
        box(f"HelmWing{sgn}", (cx, cy, HD + 0.40), (0.56, 1.60, 0.80), M_PanelDark(), "Consoles", yaw=yaw, bevel=0.02)
        c0, s0 = ang(yaw)
        box(f"HelmFront{sgn}", (cx + 0.285 * c0, cy + 0.285 * s0, HD + 0.36), (0.03, 1.58, 0.70), M_PanelGrey(), "Consoles", yaw=yaw, bevel=0.01)
        box(f"HelmTop{sgn}", (cx, cy, HD + 0.84), (0.68, 1.66, 0.05), M_Black(), "Consoles", yaw=yaw, pitch=-10, bevel=0.01)
        box(f"HelmOutline{sgn}", (cx, cy, HD + 0.867), (0.70, 1.68, 0.012), M_Strip(), "Lights", yaw=yaw, pitch=-10)
        box(f"HelmInlay{sgn}", (cx + 0.01, cy, HD + 0.874), (0.62, 1.60, 0.012), M_Black(), "Consoles", yaw=yaw, pitch=-10)
        name = "Helm" if sgn < 0 else "Navigation"
        screen(f"Scr_{name}", (cx + 0.03, cy, HD + 0.884), 1.40, 0.54, M_Screen(name), "Screens", yaw=yaw + 180, pitch=-80)
        c, sn = ang(yaw); tc, ts = ang(yaw + 90)
        ex, ey = cx + tc * sgn * 0.86, cy + ts * sgn * 0.86
        box(f"HelmEnd{sgn}", (ex, ey, HD + 0.50), (0.60, 0.08, 0.62), M_Black(), "Consoles", yaw=yaw, roll=sgn * 22, bevel=0.01)
        for k in range(3):
            box(f"HelmBar{sgn}{k}", (ex - 0.14 * c + k * 0.13 * c, ey - 0.14 * sn + k * 0.13 * sn, HD + 0.38 + k * 0.09), (0.03, 0.02, 0.22), M_Strip(), "Lights", yaw=yaw, roll=sgn * 22)
        # Grated side shelf on a short leg beyond the end panel.
        sx, sy = cx + tc * sgn * 1.20, cy + ts * sgn * 1.20
        box(f"HelmShelf{sgn}", (sx, sy, HD + 0.62), (0.50, 0.42, 0.03), M_PostGrey(), "Consoles", yaw=yaw)
        box(f"HelmShelfLeg{sgn}", (sx, sy, HD + 0.31), (0.06, 0.06, 0.62), M_Black(), "Consoles", yaw=yaw)
    box("HelmCentre", (X0 + 0.16, 0.0, HD + 0.40), (0.56, 0.60, 0.80), M_PanelDark(), "Consoles", bevel=0.02)
    box("HelmCentreFront", (X0 + 0.45, 0.0, HD + 0.36), (0.03, 0.62, 0.70), M_PanelGrey(), "Consoles", bevel=0.01)
    box("HelmCentreLight", (X0 + 0.445, 0.0, HD + 0.36), (0.02, 0.30, 0.52), M_Strip(), "Lights")
    # The pedestal between the helm chairs, after v08/v09: narrow light-grey plinth, a wider block with the keypad
    # plate and the black plaque toward the captain, and the black head edged in grey whose face is the display.
    px = X0 - 0.95
    box("PedestalPlinth", (px, 0.0, HD + 0.25), (0.30, 0.42, 0.50), M_PanelGrey(), "Consoles", bevel=0.01)
    box("PedestalBlock", (px, 0.0, HD + 0.625), (0.40, 0.54, 0.25), M_PanelGrey(), "Consoles", bevel=0.012)
    box("PedestalPlate", (px - 0.203, 0.0, HD + 0.66), (0.008, 0.30, 0.15), M_PanelDark(), "Consoles", bevel=0.003)
    for r in range(4):
        for c in range(6):
            if r in (1, 2) and c in (3, 4): continue
            box(f"PedKey{r}{c}", (px - 0.21, -0.11 + c * 0.044, HD + 0.605 + r * 0.036), (0.008, 0.028, 0.02), M_PanelGrey(), "Consoles", bevel=0.004)
    box("PedKeypad", (px - 0.21, 0.044, HD + 0.66), (0.006, 0.07, 0.05), M_PostGrey(), "Consoles")
    box("PedestalPlaque", (px - 0.203, 0.0, HD + 0.535), (0.006, 0.30, 0.045), M_Black(), "Consoles")
    box("PedestalHeadRim", (px + 0.01, 0.0, HD + 1.08), (0.34, 0.62, 0.62), M_PanelGrey(), "Consoles", bevel=0.012)
    box("PedestalHead", (px - 0.005, 0.0, HD + 1.08), (0.34, 0.58, 0.58), M_Black(), "Consoles", bevel=0.008)
    screen("Scr_Schematic", (px - 0.178, 0.0, HD + 1.08), 0.52, 0.52, M_Screen("Schematic"), "Screens", yaw=180)
    for sgn in (-1, 1):
        chair(f"HelmChair{sgn}", (X0 - 0.90, sgn * 0.95, HD), 0.0, tall=True)

def captain():
    """The captain's chair at the centre-rear of the platform, after v02: six rounded grey cushions up a tall back
    with a headrest, a grey seat, black arms with keypads, a black stem on a round base."""
    cx = -0.75
    o = lathe("CaptBase", [(0.0, HC), (0.40, HC), (0.40, HC + 0.06), (0.36, HC + 0.08), (0.0, HC + 0.08)], M_Black(), "Furniture", 48); o.location = B(cx, 0, 0)
    o = lathe("CaptCol", [(0.0, HC + 0.08), (0.10, HC + 0.08), (0.10, HC + 0.42), (0.0, HC + 0.42)], M_Black(), "Furniture", 24); o.location = B(cx, 0, 0)
    box("CaptSeatShell", (cx - 0.02, 0.0, HC + 0.44), (0.62, 0.60, 0.06), M_Black(), "Furniture", bevel=0.02)
    box("CaptSeat", (cx - 0.02, 0.0, HC + 0.50), (0.58, 0.56, 0.12), M_Seat(), "Furniture", bevel=0.05)
    box("CaptSpine", (cx - 0.40, 0.0, HC + 0.92), (0.05, 0.50, 0.88), M_Black(), "Furniture", pitch=-7, bevel=0.01)
    for k in range(6):
        w = 0.54 if k < 5 else 0.40
        egg(f"CaptCushion{k}", (cx - 0.33 - k * 0.012, 0.0, HC + 0.62 + k * 0.125), 0.0, (0.18, w, 0.105), M_Seat())
    for sgn in (-1, 1):
        box(f"CaptArm{sgn}", (cx - 0.04, sgn * 0.36, HC + 0.72), (0.52, 0.09, 0.08), M_Black(), "Furniture", bevel=0.02)
        box(f"CaptArmPost{sgn}", (cx - 0.20, sgn * 0.36, HC + 0.58), (0.08, 0.06, 0.22), M_Black(), "Furniture")
        box(f"CaptArmPanel{sgn}", (cx + 0.10, sgn * 0.36, HC + 0.765), (0.16, 0.07, 0.012), M_Buttons(), "Consoles")

def crew_chairs():
    # The two small chairs flanking the captain at the platform's rear (v01/v02), then one at each station.
    for sgn in (-1, 1):
        chair(f"FlankChair{sgn}", (-0.95, sgn * 1.60, HD), 0.0)
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
    """Renders from viewpoints matched to Versiga's: v01 (high behind the captain), v02 (low at the helm looking
    aft), v09 (the helm from the viewscreen), v13 (a station from the platform), v06 (the captain's view) and an
    overview. Fill light is low so the emissive strips and screens carry the room as in the references."""
    os.makedirs(outdir, exist_ok=True)
    scn = bpy.context.scene
    scn.render.engine = 'BLENDER_EEVEE_NEXT' if hasattr(bpy.types, "SceneEEVEE") and 'BLENDER_EEVEE_NEXT' in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items] else 'BLENDER_EEVEE'
    scn.render.resolution_x = 1280; scn.render.resolution_y = 720; scn.render.resolution_percentage = 100
    scn.view_settings.view_transform = 'AgX' if 'AgX' in [i.identifier for i in bpy.types.ColorManagedViewSettings.bl_rna.properties['view_transform'].enum_items] else 'Filmic'
    cam_data = bpy.data.cameras.new("Cam"); cam_data.lens = 22
    cam = bpy.data.objects.new("Cam", cam_data); scn.collection.objects.link(cam); scn.camera = cam
    for i in range(6):
        c, s = ang(i * 60 + 30)
        ld = bpy.data.lights.new(f"Fill{i}", 'POINT'); ld.energy = 14; ld.color = (0.9, 0.93, 1.0); ld.shadow_soft_size = 0.5
        lo = bpy.data.objects.new(f"Fill{i}", ld); scn.collection.objects.link(lo); lo.location = B(R * 0.62 * c, R * 0.62 * s, H - 0.25)
    for i in range(12):
        a = i * 30 + 15; c, s = ang(a)
        ld = bpy.data.lights.new(f"Panel{i}", 'AREA'); ld.energy = 18; ld.size = 0.6; ld.color = (0.92, 0.95, 1.0)
        lo = bpy.data.objects.new(f"Panel{i}", ld); scn.collection.objects.link(lo); lo.location = B(R * 0.80 * c, R * 0.80 * s, H - 0.05)
    ld = bpy.data.lights.new("DomeLight", 'POINT'); ld.energy = 60; ld.shadow_soft_size = 0.6
    lo = bpy.data.objects.new("DomeLight", ld); scn.collection.objects.link(lo); lo.location = B(0, 0, H - 0.45)
    views = {
        "v01": ((4.4, 0.0, 2.1), (-0.5, 0.0, 0.2), 14),
        "v02": ((2.85, 0.0, HD + 1.0), (-0.6, 0.0, HD + 0.95), 24),
        "v09": ((-0.9, 0.0, HD + 2.25), (1.6, 0.0, HD + 0.65), 24),
        "v13": ((0.6, 1.6, HD + 1.1), (5.0 * math.cos(math.radians(80)), 5.0 * math.sin(math.radians(80)), HD + 1.0), 22),
        "v08": ((-0.65, 0.0, HD + 1.34), (5.0, 0.0, HD + 1.0), 22),
    }
    for name, (eye, target, lens) in views.items():
        cam.location = B(*eye); cam_data.lens = lens
        d = B(*target) - cam.location
        cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
        scn.render.filepath = os.path.join(outdir, f"prev_{name}.png"); bpy.ops.render.render(write_still=True)
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
