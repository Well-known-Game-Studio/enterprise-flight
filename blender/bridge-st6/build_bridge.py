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
    obj = mesh_from(name, verts, faces, material, group, smooth=smooth)
    # A closed solid: let bmesh point every face outward, whatever the outline's winding.
    bm = bmesh.new(); bm.from_mesh(obj.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(obj.data); bm.free(); obj.data.update()
    return obj

def deck_outline(radius, x_edge, step=3.0):
    """Circle of the given radius cut by the chord x = x_edge (the well side)."""
    a0 = math.degrees(math.acos(min(1.0, x_edge / radius)))
    pts = []
    a = a0
    while a < 360.0 - a0 - 1e-6:
        c, sn = ang(a); pts.append((radius * c, radius * sn)); a += step
    c, sn = ang(360.0 - a0); pts.append((radius * c, radius * sn))
    return pts

def arc_pts(radius, a0, a1, step=3.0):
    """Points along an arc of bearings a0..a1 (either direction)."""
    n = max(2, int(round(abs(a1 - a0) / step)) + 1)
    out = []
    for i in range(n):
        a = a0 + (a1 - a0) * i / (n - 1); c, sn = ang(a); out.append((radius * c, radius * sn))
    return out

def oriented(outline):
    """Same winding as deck_outline (bearings increasing), so prism tops face up in the game."""
    area = 0.0
    for i in range(len(outline)):
        (x0, y0), (x1, y1) = outline[i], outline[(i + 1) % len(outline)]
        area += x0 * y1 - x1 * y0
    return outline if area > 0 else list(reversed(outline))

def chevron(apex_x, spread_deg, y_max):
    """The helm platform's front: apex at (apex_x, 0), sides swept back by spread_deg to |y| = y_max. Returns the
    points from the port end through the apex to the starboard end."""
    t = y_max / math.sin(math.radians(spread_deg))
    back = t * math.cos(math.radians(spread_deg))
    return [(apex_x - back, -y_max), (apex_x, 0.0), (apex_x - back, y_max)]

# ---- the set -----------------------------------------------------------------------------------------------------
R = 5.6          # wall radius
# Four floor levels, after Versiga's v01/v02: a sunken well in the middle, out of which rise the helm platform and,
# behind it, the captain's platform; the outer station ring is the highest and surrounds the well.
ZW = 0.0         # the well
ZH = 0.36        # helm platform (three 12 cm risers up from the well: two steps and the platform edge)
ZC = 0.74        # captain's platform
ZR = 1.00        # outer station ring
HD = ZR          # the stations, lifts and walls stand on the ring
HC = ZC
H = 3.60         # ceiling above the well floor
VS = 34.0        # viewscreen half-angle
RI = 2.55        # radius of the central island (helm + captain)
RW = 3.55        # inner radius of the outer ring; the well lies between RI and RW
XC = 0.55        # chord between the helm platform (in front) and the captain's platform (behind)
XA = 2.15        # apex of the helm platform's chevron front
LIFTS = (100.0, 260.0); LIFT_HALF = 12.0
STEP = 32.0

def floors():
    """Well floor; the outer ring with its curved black riser lit under the lip; the captain's platform (the
    island behind the chord, joined to the aft ring); the helm platform in front with its chevron nose; and the
    two chevron steps down from the nose into the well."""
    lathe("WellFloor", [(0.0, -0.06), (R + 0.15, -0.06), (R + 0.15, ZW), (0.0, ZW)], M_Carpet(), "Floor", smooth=False)
    # Outer ring and its riser; at the rear the captain's platform meets it, so the riser there is shorter.
    arc_sweep("Ring", [(RW, ZW), (R + 0.15, ZW), (R + 0.15, ZR), (RW, ZR)], 0, 360, M_Carpet(), "Floor", smooth=False, cap=False)
    arc_sweep("RingRiser", [(RW - 0.015, ZW), (RW + 0.01, ZW), (RW + 0.01, ZR - 0.05), (RW - 0.015, ZR - 0.05)], -126, 126, M_Black(), "Floor", cap=False)
    arc_sweep("RingLipStrip", [(RW - 0.012, ZR - 0.05), (RW + 0.03, ZR - 0.05), (RW + 0.03, ZR - 0.012), (RW - 0.012, ZR - 0.012)], -126, 126, M_Strip(), "Lights", cap=False)
    arc_sweep("RingRiserAft", [(RW - 0.015, ZC), (RW + 0.01, ZC), (RW + 0.01, ZR - 0.04), (RW - 0.015, ZR - 0.04)], 126, 234, M_Black(), "Floor", cap=False)
    arc_sweep("RingLipStripAft", [(RW - 0.012, ZR - 0.04), (RW + 0.03, ZR - 0.04), (RW + 0.03, ZR - 0.012), (RW - 0.012, ZR - 0.012)], 126, 234, M_Strip(), "Lights", cap=False)
    # Captain's platform: the island behind the chord, plus the sector that reaches the aft ring.
    a0 = math.degrees(math.acos(XC / RI))
    cap = arc_pts(RI, a0, 126) + arc_pts(RW - 0.02, 126, 234) + arc_pts(RI, 234, 360 - a0)
    prism("CaptainPlatform", oriented(cap), ZW, ZC, M_Carpet(), "Floor")
    arc_sweep("CaptRiser", [(RI - 0.015, ZW), (RI + 0.01, ZW), (RI + 0.01, ZC - 0.05), (RI - 0.015, ZC - 0.05)], a0, 126, M_Black(), "Floor", cap=False)
    arc_sweep("CaptRiser2", [(RI - 0.015, ZW), (RI + 0.01, ZW), (RI + 0.01, ZC - 0.05), (RI - 0.015, ZC - 0.05)], 234, 360 - a0, M_Black(), "Floor", cap=False)
    arc_sweep("CaptLipStrip", [(RI - 0.012, ZC - 0.05), (RI + 0.03, ZC - 0.05), (RI + 0.03, ZC - 0.012), (RI - 0.012, ZC - 0.012)], a0, 126, M_Strip(), "Lights", cap=False)
    arc_sweep("CaptLipStrip2", [(RI - 0.012, ZC - 0.05), (RI + 0.03, ZC - 0.05), (RI + 0.03, ZC - 0.012), (RI - 0.012, ZC - 0.012)], 234, 360 - a0, M_Strip(), "Lights", cap=False)
    # The step between the captain's platform and the helm platform, along the chord.
    y0 = math.sqrt(RI * RI - XC * XC)
    box("ChordFace", (XC + 0.01, 0.0, (ZH + ZC) / 2 - 0.02), (0.02, 2 * y0, ZC - ZH - 0.04), M_Black(), "Floor")
    box("ChordStrip", (XC + 0.025, 0.0, ZC - 0.03), (0.02, 2 * y0, 0.03), M_Strip(), "Lights")
    # Helm platform: from the chord forward to the chevron nose, within the island circle.
    chev = chevron(XA, 32.0, 1.35)
    ac = math.degrees(math.atan2(1.35, chev[2][0]))
    helm_pts = [(XC, -y0)] + arc_pts(RI, -a0, -ac) + chev[:1] + chev[1:2] + chev[2:] + arc_pts(RI, ac, a0) + [(XC, y0)]
    # (the chevron ends sit a little inside the circle; the arcs run to the matching bearings)
    prism("HelmPlatform", oriented(helm_pts), ZW, ZH, M_Carpet(), "Floor")
    for i in range(2):
        (x0, y0c), (x1, _), (x2, y2c) = chev
        seg = [(x0, y0c), (x1, 0.0), (x2, y2c)]
        for j, (px, py) in enumerate(seg):
            pass
    # Risers: the platform nose and two steps, each 12 cm, as chevron bands offset forward.
    def offset_chev(d):
        return chevron(XA + d, 32.0, 1.35)
    bands = [(0.0, ZH), (0.40, ZH - 0.12), (0.80, ZH - 0.24)]
    for k, (d, ztop) in enumerate(bands):
        inner = offset_chev(d); outer = offset_chev(d + 0.40)
        poly = inner + list(reversed(outer))
        name = "HelmNose" if k == 0 else f"Step{k}"
        prism(name, oriented(poly), ZW, ztop, M_Carpet(), "Floor")
        # Black face along the outer edge with a strip under the lip.
        for sgn in (-1, 1):
            (ax, ay), (bx, by) = outer[1], outer[2 if sgn > 0 else 0]
            L = math.hypot(bx - ax, by - ay); yaw = math.degrees(math.atan2(by - ay, bx - ax))
            mx, my = (ax + bx) / 2, (ay + by) / 2
            box(f"{name}Face{sgn}", (mx, my, ztop / 2 - 0.01), (L, 0.02, ztop - 0.02), M_Black(), "Floor", yaw=yaw)
            box(f"{name}Strip{sgn}", (mx, my, ztop - 0.03), (L, 0.025, 0.03), M_Strip(), "Lights", yaw=yaw)
    # Black riser along the chevron sides between the island arc ends and the nose (the sides of the helm platform).
    for sgn in (-1, 1):
        c, sn = ang(sgn * (a0 + ac) / 2)
    arc_sweep("HelmRiser", [(RI - 0.015, ZW), (RI + 0.01, ZW), (RI + 0.01, ZH - 0.05), (RI - 0.015, ZH - 0.05)], -a0, -ac, M_Black(), "Floor", cap=False)
    arc_sweep("HelmRiser2", [(RI - 0.015, ZW), (RI + 0.01, ZW), (RI + 0.01, ZH - 0.05), (RI - 0.015, ZH - 0.05)], ac, a0, M_Black(), "Floor", cap=False)
    arc_sweep("HelmLipStrip", [(RI - 0.012, ZH - 0.05), (RI + 0.03, ZH - 0.05), (RI + 0.03, ZH - 0.012), (RI - 0.012, ZH - 0.012)], -a0, -ac, M_Strip(), "Lights", cap=False)
    arc_sweep("HelmLipStrip2", [(RI - 0.012, ZH - 0.05), (RI + 0.03, ZH - 0.05), (RI + 0.03, ZH - 0.012), (RI - 0.012, ZH - 0.012)], ac, a0, M_Strip(), "Lights", cap=False)

def rails():
    """The black handrail round the island on flat posts, higher behind the captain than beside the helm; the knee
    rail along the outer ring's edge over the well with a lamp post at its centre and at each end, and round side
    tables on the ring beside them."""
    RR = RI - 0.10
    for (b0, b1, zf) in ((38, 70, ZH), (290, 322, ZH), (70, 290, ZC)):
        arc_sweep(f"TopRail{b0}", [(RR - 0.03, zf + 1.00), (RR + 0.03, zf + 1.00), (RR + 0.03, zf + 1.06), (RR - 0.03, zf + 1.06)], b0, b1, M_Rail(), "Rails")
        arc_sweep(f"LowRail{b0}", [(RR - 0.015, zf + 0.58), (RR + 0.015, zf + 0.58), (RR + 0.015, zf + 0.61), (RR - 0.015, zf + 0.61)], b0, b1, M_PostGrey(), "Rails")
        n = max(2, int(round((b1 - b0) / 18.0)) + 1)
        for i in range(n):
            a = b0 + (b1 - b0) * i / (n - 1)
            wallbox(f"Post{b0}_{i}", a, RR + 0.03, zf + 0.52, 0.02, 0.07, 1.04, M_PostGrey(), "Rails", pitch=-5)
    # Knee rail on the ring, round the front half of the well.
    arc_sweep("KneeRail", [(RW - 0.14, ZR + 0.47), (RW - 0.11, ZR + 0.47), (RW - 0.11, ZR + 0.495), (RW - 0.14, ZR + 0.495)], -64, 64, M_PostGrey(), "Rails")
    for a in range(-60, 61, 12):
        if abs(a) < 6 or abs(abs(a) - 60) < 1: continue
        wallbox(f"KneePost{a}", a, RW - 0.125, ZR + 0.235, 0.02, 0.05, 0.47, M_PostGrey(), "Rails")
    # Lamp posts at the knee rail's ends (the one at its centre in v01 would stand in the captain's view of the viewscreen).
    for a in (-62, 62):
        c, sn = ang(a)
        lamp_post(f"Lamp{a}", (RW - 0.125) * c, (RW - 0.125) * sn, ZR)
    for sgn in (-1, 1):
        c, sn = ang(sgn * 50)
        tx, ty = (RW - 0.42) * c, (RW - 0.42) * sn
        o = lathe(f"TableStem{sgn}", [(0.0, ZR), (0.16, ZR), (0.16, ZR + 0.03), (0.035, ZR + 0.03), (0.035, ZR + 0.72), (0.0, ZR + 0.72)], M_Black(), "Rails", 24); o.location = B(tx, ty, 0)
        o = lathe(f"TableTop{sgn}", [(0.0, ZR + 0.72), (0.24, ZR + 0.72), (0.24, ZR + 0.75), (0.0, ZR + 0.75)], M_Black(), "Rails", 32); o.location = B(tx, ty, 0)

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
    if is_aft(a):
        # Under the aft desks the three bars sit low, below the red quilted band.
        arc_sweep(f"CabRecess_{name}", [(R - 0.80, HD + 0.05), (R - 0.74, HD + 0.05), (R - 0.74, HD + 0.30), (R - 0.80, HD + 0.30)], a - half, a + half, M_PanelDark(), "Consoles")
        light_bars(f"Cab_{name}", a, R - 0.79, HD + 0.08, chord - 0.20, pitch=0.075)
    else:
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
        if aft:
            station_unit(a, chord, "MasterSituation")
        elif True:
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
    VB, VT = ZR + 0.55, ZR + 2.20
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
    light_bars("Sill", 0, XW - 0.07, ZR + 0.10, WW - 0.4)
    box("ClockFront", (XW - 0.07, 0, VT + 0.30), (0.02, 0.44, 0.10), M_Clock(), "Lights")
    for s in (-1, 1):
        wallbox(f"ScreenPillar{s}", s * (VS + 1.5), R + 0.01, H / 2, 0.34, 0.24, H, M_PanelGrey(), "Walls", bevel=0.012)

def lifts_and_bench():
    """Turbolifts at the sides (v01/v13), and the aft wall under the screens: the red quilted band over the light
    bars, continuous across the aft stations, with the clock above."""
    for L in LIFTS:
        wallbox(f"LiftSurround{int(L)}", L, R + 0.08, HD + 1.10, 0.08, 1.50, 2.20, M_PanelGrey(), "Walls", bevel=0.01)
        wallbox(f"LiftDoors{int(L)}", L, R + 0.01, HD + 1.00, 0.04, 1.10, 2.00, M_Burgundy(), "Walls")
        wallbox(f"LiftSeam{int(L)}", L, R - 0.015, HD + 1.00, 0.01, 0.02, 1.96, M_Black(), "Walls")
        wallbox(f"LiftRail{int(L)}", L, R - 0.015, HD + 0.95, 0.01, 1.08, 0.03, M_Black(), "Walls")
        wallbox(f"LiftSign{int(L)}", L, R - 0.01, HD + 2.12, 0.02, 0.40, 0.07, M_Clock(), "Lights")
        for s in (-1, 1):
            wallbox(f"LiftBar{int(L)}{s}", L + s * 7.6, R + 0.03, HD + 1.10, 0.02, 0.03, 2.16, M_Strip(), "Lights")
    # Red quilted band: upright cushions along the aft cabinet front from 120 to 240 degrees.
    a = 121.5
    k = 0
    while a < 239:
        c, sn = ang(a)
        box(f"AftCushion{k}", ((R - 0.80) * c, (R - 0.80) * sn, HD + 0.52), (0.10, 0.32, 0.42), M_Burgundy(), "Furniture", yaw=a, bevel=0.035)
        a += 3.7; k += 1
    arc_sweep("AftCushionBase", [(R - 0.78, HD + 0.30), (R - 0.05, HD + 0.30), (R - 0.05, HD + 0.74), (R - 0.78, HD + 0.74)], 120, 240, M_PanelDark(), "Furniture")
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
    """The helm, after v01/v02/v09: one gently curved console in front of the helm chairs, its raked black top
    outlined in white and carrying the two Okudagrams, on a grey cabinet that narrows to a central pedestal with a
    lit panel on its front (the wings overhang the chairs' bases); angled black end panels with three bars and
    grated side shelves; the two chairs; and between them the pedestal whose screen faces the captain."""
    RA = 2.05                      # radius of the console arc about the bridge centre
    SP = 36.0                      # half-angle of the console
    top = [(RA - 0.30, ZH + 0.78), (RA + 0.26, ZH + 0.96), (RA + 0.26, ZH + 0.91), (RA - 0.30, ZH + 0.73)]
    # The white outline: a slab a little wider than the top and just under it, so only a rim shows round the black.
    arc_sweep("HelmTopOutline", [(r - 0.012 if i in (0, 3) else r + 0.012, z - 0.006) for i, (r, z) in enumerate(top)], -SP - 0.4, SP + 0.4, M_Strip(), "Lights")
    arc_sweep("HelmTop", top, -SP, SP, M_Black(), "Consoles")
    for name, b0, b1 in (("Helm", -SP + 1.5, -2.0), ("Navigation", 2.0, SP - 1.5)):
        arc_sweep(f"Scr_{name}", [(RA + 0.22, ZH + 0.953), (RA - 0.24, ZH + 0.805)], b0, b1, M_Screen(name), "Screens", closed=False, cap=False, uv=True)
    # Cabinet under the top, open beneath the wings; the central pedestal from the floor with the lit panel.
    arc_sweep("HelmCabinet", [(RA - 0.26, ZH + 0.40), (RA + 0.18, ZH + 0.40), (RA + 0.18, ZH + 0.80), (RA - 0.26, ZH + 0.80)], -SP + 1.0, SP - 1.0, M_PanelGrey(), "Consoles")
    arc_sweep("HelmPedestal", [(RA - 0.22, ZH), (RA + 0.14, ZH), (RA + 0.14, ZH + 0.42), (RA - 0.22, ZH + 0.42)], -11, 11, M_PanelGrey(), "Consoles")
    arc_sweep("HelmPedestalLight", [(RA + 0.14, ZH + 0.08), (RA + 0.165, ZH + 0.08), (RA + 0.165, ZH + 0.74), (RA + 0.14, ZH + 0.74)], -5, 5, M_Strip(), "Lights")
    for sgn in (-1, 1):
        a = sgn * SP
        c, sn = ang(a)
        wallbox(f"HelmEnd{sgn}", a, RA - 0.02, ZH + 0.60, 0.62, 0.08, 0.60, M_Black(), "Consoles", bevel=0.01)
        for k in range(3):
            box(f"HelmBar{sgn}{k}", ((RA - 0.18 + k * 0.14) * c, (RA - 0.18 + k * 0.14) * sn, ZH + 0.48 + k * 0.09), (0.03, 0.02, 0.22), M_Strip(), "Lights", yaw=a)
        a2 = sgn * (SP + 11)
        c2, s2 = ang(a2)
        box(f"HelmShelf{sgn}", (RA * c2, RA * s2, ZH + 0.66), (0.46, 0.44, 0.03), M_PostGrey(), "Consoles", yaw=a2)
        box(f"HelmShelfLeg{sgn}", (RA * c2, RA * s2, ZH + 0.33), (0.06, 0.06, 0.66), M_Black(), "Consoles", yaw=a2)
    # The pedestal between the helm chairs: narrow plinth, the block with the keypad plate and plaque toward the
    # captain, the black head edged in grey whose face toward the captain is the display.
    px = 0.95
    # Low, as in v09: the head's top is about level with the helm chairs' seat backs.
    box("PedestalPlinth", (px, 0.0, ZH + 0.15), (0.30, 0.42, 0.30), M_PanelGrey(), "Consoles", bevel=0.01)
    box("PedestalBlock", (px, 0.0, ZH + 0.41), (0.40, 0.54, 0.22), M_PanelGrey(), "Consoles", bevel=0.012)
    box("PedestalPlate", (px - 0.203, 0.0, ZH + 0.44), (0.008, 0.30, 0.14), M_PanelDark(), "Consoles", bevel=0.003)
    for r in range(4):
        for c in range(6):
            if r in (1, 2) and c in (3, 4): continue
            box(f"PedKey{r}{c}", (px - 0.21, -0.11 + c * 0.044, ZH + 0.39 + r * 0.033), (0.008, 0.028, 0.018), M_PanelGrey(), "Consoles", bevel=0.004)
    box("PedKeypad", (px - 0.21, 0.044, ZH + 0.44), (0.006, 0.07, 0.05), M_PostGrey(), "Consoles")
    box("PedestalPlaque", (px - 0.203, 0.0, ZH + 0.33), (0.006, 0.30, 0.045), M_Black(), "Consoles")
    box("PedestalHeadRim", (px + 0.01, 0.0, ZH + 0.80), (0.34, 0.56, 0.52), M_PanelGrey(), "Consoles", bevel=0.012)
    box("PedestalHead", (px - 0.005, 0.0, ZH + 0.80), (0.34, 0.52, 0.48), M_Black(), "Consoles", bevel=0.008)
    screen("Scr_Schematic", (px - 0.178, 0.0, ZH + 0.80), 0.46, 0.42, M_Screen("Schematic"), "Screens", yaw=180)
    # Its front, toward the viewscreen: a dark panel with a blue-edged display (v02).
    box("PedestalFrontPanel", (px + 0.205, 0.0, ZH + 0.36), (0.01, 0.30, 0.20), M_Buttons(), "Consoles")
    for sgn in (-1, 1):
        chair(f"HelmChair{sgn}", (1.20, sgn * 0.95, ZH), 0.0, tall=True)

def captain():
    """The captain's chair at the centre of its platform, after v02: six rounded grey cushions up a leaning black
    spine with a narrower headrest, grey seat on a black shell, black arms on posts with keypads, black stem and base."""
    cx = -0.55
    o = lathe("CaptBase", [(0.0, ZC), (0.40, ZC), (0.40, ZC + 0.06), (0.36, ZC + 0.08), (0.0, ZC + 0.08)], M_Black(), "Furniture", 48); o.location = B(cx, 0, 0)
    o = lathe("CaptCol", [(0.0, ZC + 0.08), (0.10, ZC + 0.08), (0.10, ZC + 0.42), (0.0, ZC + 0.42)], M_Black(), "Furniture", 24); o.location = B(cx, 0, 0)
    box("CaptSeatShell", (cx - 0.02, 0.0, ZC + 0.44), (0.62, 0.60, 0.06), M_Black(), "Furniture", bevel=0.02)
    box("CaptSeat", (cx - 0.02, 0.0, ZC + 0.50), (0.58, 0.56, 0.12), M_Seat(), "Furniture", bevel=0.05)
    box("CaptSpine", (cx - 0.40, 0.0, ZC + 0.90), (0.05, 0.50, 0.84), M_Black(), "Furniture", pitch=-7, bevel=0.01)
    for k in range(6):
        w = 0.54 if k < 5 else 0.40
        egg(f"CaptCushion{k}", (cx - 0.33 - k * 0.011, 0.0, ZC + 0.62 + k * 0.115), 0.0, (0.18, w, 0.10), M_Seat())
    for sgn in (-1, 1):
        box(f"CaptArm{sgn}", (cx - 0.04, sgn * 0.36, ZC + 0.72), (0.52, 0.09, 0.08), M_Black(), "Furniture", bevel=0.02)
        box(f"CaptArmPost{sgn}", (cx - 0.20, sgn * 0.36, ZC + 0.58), (0.08, 0.06, 0.22), M_Black(), "Furniture")
        box(f"CaptArmPanel{sgn}", (cx + 0.10, sgn * 0.36, ZC + 0.765), (0.16, 0.07, 0.012), M_Buttons(), "Consoles")

def is_aft(a):
    return abs((a - 180 + 180) % 360 - 180) < 62

def crew_chairs():
    # Chairs at the side stations only; the aft stations are stood at, over the red quilted wall.
    chord = 2 * R * math.sin(math.radians(STEP / 2))
    a = VS + STEP / 2 + 4
    while a < 360 - VS - STEP * 0.4:
        if not near_lift(a, STEP * 0.45) and not is_aft(a):
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
        "v01": ((4.55, 0.0, ZR + 1.70), (-0.5, 0.0, 0.5), 14),
        "v02": ((2.45, 0.0, ZH + 1.05), (-0.6, 0.0, ZC + 0.9), 24),
        "v09": ((-1.2, 0.0, ZC + 1.6), (1.7, 0.0, ZH + 0.7), 24),
        "v13": ((0.6, 1.6, ZC + 1.1), (5.0 * math.cos(math.radians(80)), 5.0 * math.sin(math.radians(80)), ZR + 1.0), 22),
        "v08": ((-0.45, 0.0, ZC + 1.28), (5.0, 0.0, ZC + 0.9), 22),
        "dbg": ((4.2, -2.2, 3.1), (1.4, 0.0, 0.3), 22),
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
