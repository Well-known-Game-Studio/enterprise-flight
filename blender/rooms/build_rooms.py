"""Builds the Enterprise-A's walkable rooms as Blender scenes and exports one static mesh per room for Unreal:
main engineering, sickbay, the shuttle bay and the transporter room. Laid out after the film sets (The Motion
Picture's intermix shaft and horizontal chamber with The Wrath of Khan's console room; the Motion Picture ward beds
in The Undiscovered Country's grey palette; The Final Frontier's landing bay; the Enterprise-A transporter room
of V and VI), used as reference only. Run headless:

    /Applications/Blender.app/Contents/MacOS/Blender -b --python build_rooms.py -- [--render] [--export] [--room=eng,sick,bay,trans] [--views=a,b]

Geometry is authored in the game's frame (x forward, y starboard, z up, metres, origin on the floor at the room's
centre) and converted to Blender's axes on creation, so an FBX exported with forward -Y / up Z lands in Unreal
facing +X with starboard at +Y. Room boxes match UShipInteriorComponent: engineering 16 x 10 x 4.5 m, sickbay
12 x 9 x 3, shuttle bay 26 x 16 x 6.5 (open astern), transporter room 8 x 7 x 3. Material names are the Unreal slot
names: Screen_* bind to the Okudagram material, LightStrip / LightSoft / CoreGlow / PadGlow / LatticeLit / StripBlue /
VentGlow / WarnRed / TouchPanel to emissive instances, GlassPanel to a translucent one; the rest keep their colours.
"""
import bpy, bmesh, math, os, sys
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
TEXDIR = os.environ.get("OKUDA_PNG", os.path.join(HERE, "..", "bridge-st6", "okuda_png"))
ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []

def arg(name, default=None):
    for a in ARGS:
        if a.startswith(name + "="): return a.split("=", 1)[1]
    return default

# ---- frame ----------------------------------------------------------------------------------------------------
def B(x, y, z):
    """Game frame (x fwd, y stbd, z up) to Blender: Unreal's FBX import keeps X and negates Y."""
    return Vector((x, -y, z))

def ang(deg):
    a = math.radians(deg)
    return (math.cos(a), math.sin(a))

# ---- materials -------------------------------------------------------------------------------------------------
MATS = {}
def mat(name, color, rough=0.6, metal=0.0, emit=None, strength=0.0, tex=None, alpha=1.0):
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
    if alpha < 1.0:
        bsdf.inputs["Alpha"].default_value = alpha
        for attr, val in (("surface_render_method", "BLENDED"), ("blend_method", "BLEND")):
            try: setattr(m, attr, val)
            except Exception: pass
    if tex and os.path.exists(tex):
        img = bpy.data.images.load(tex)
        node = m.node_tree.nodes.new("ShaderNodeTexImage"); node.image = img
        m.node_tree.links.new(node.outputs["Color"], bsdf.inputs["Emission Color"])
        m.node_tree.links.new(node.outputs["Color"], bsdf.inputs["Base Color"])
        bsdf.inputs["Emission Strength"].default_value = 2.0
    MATS[name] = m
    return m

GREY = (0.50, 0.51, 0.54); LIGHT = (0.64, 0.65, 0.68); DARK = (0.14, 0.15, 0.165); BLACK = (0.02, 0.022, 0.026)
WHITE = (0.9, 0.92, 0.95); BURG = (0.28, 0.045, 0.055); SEAT = (0.50, 0.50, 0.52); CARPET = (0.09, 0.10, 0.13)
def M_PanelGrey(): return mat("PanelGrey", GREY, 0.55)
def M_PanelLight(): return mat("PanelLight", LIGHT, 0.5)
def M_PanelDark(): return mat("PanelDark", DARK, 0.7)
def M_Carpet(): return mat("Carpet", CARPET, 0.95)
def M_DeckPlate(): return mat("DeckPlate", (0.30, 0.31, 0.33), 0.5, 0.3)
def M_BayDeck(): return mat("DeckLight", (0.60, 0.61, 0.63), 0.45, 0.2)
def M_Black(): return mat("ConsoleBlack", BLACK, 0.35)
def M_Strip(): return mat("LightStrip", WHITE, 0.8, emit=(0.95, 0.97, 1.0), strength=12.0)
def M_Soft(): return mat("LightSoft", WHITE, 0.8, emit=(0.95, 0.97, 1.0), strength=4.0)
def M_Burgundy(): return mat("Burgundy", BURG, 0.7)
def M_Seat(): return mat("SeatGrey", SEAT, 0.95)
def M_Rail(): return mat("RailBlack", (0.05, 0.05, 0.055), 0.45, 0.2)
def M_Metal(): return mat("MetalDark", (0.22, 0.22, 0.24), 0.35, 0.6)
def M_Buttons(): return mat("TouchPanel", (0.03, 0.03, 0.035), 0.4, emit=(0.4, 0.7, 0.6), strength=1.0)
def M_Screen(name): return mat("Screen_" + name, BLACK, 0.2, tex=os.path.join(TEXDIR, f"T_Okuda_{name}.png"))
def M_Core(): return mat("CoreGlow", (0.25, 0.45, 0.9), 0.3, emit=(0.3, 0.55, 1.0), strength=2.2)
def M_Glass(): return mat("GlassPanel", (0.8, 0.85, 0.9), 0.05, alpha=0.14)
def M_Lattice(): return mat("LatticeLit", (0.9, 0.8, 0.6), 0.6, emit=(1.0, 0.85, 0.6), strength=1.6)
def M_Pad(): return mat("PadGlow", (0.6, 0.7, 0.85), 0.4, emit=(0.75, 0.85, 1.0), strength=3.0)
def M_PaintRed(): return mat("PaintRed", (0.55, 0.08, 0.06), 0.6)
def M_PaintYellow(): return mat("PaintYellow", (0.85, 0.6, 0.1), 0.6)
def M_StripBlue(): return mat("StripBlue", (0.2, 0.4, 0.9), 0.6, emit=(0.2, 0.5, 1.0), strength=8.0)
def M_Warn(): return mat("WarnRed", (0.5, 0.05, 0.03), 0.5, emit=(1.0, 0.1, 0.05), strength=6.0)
def M_Vent(): return mat("VentGlow", (0.5, 0.2, 0.05), 0.5, emit=(1.0, 0.45, 0.15), strength=5.0)
def M_HullWhite(): return mat("HullWhite", (0.82, 0.83, 0.85), 0.45, 0.1)
def M_HullGrey(): return mat("HullGrey", (0.58, 0.59, 0.62), 0.5, 0.2)
def M_BedWhite(): return mat("BedWhite", (0.86, 0.87, 0.89), 0.7)

# ---- mesh helpers ----------------------------------------------------------------------------------------------
COLLECTIONS = {}
COUNT = [0]
def coll(name):
    if name not in COLLECTIONS:
        c = bpy.data.collections.new(name); bpy.context.scene.collection.children.link(c); COLLECTIONS[name] = c
    return COLLECTIONS[name]

def uname(name):
    COUNT[0] += 1
    return f"{name}_{COUNT[0]:03d}"

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
    obj = bpy.data.objects.new(uname(name), me)
    return finish(obj, material, group, smooth, bevel)

def _bm(obj):
    bm = bmesh.new(); bm.from_mesh(obj.data); bm.faces.ensure_lookup_table(); bm.normal_update(); return bm

def recalc_outward(obj):
    """A closed solid: every face outward. The revolved solids keep a separate vertex strip per profile segment
    (so smoothing runs round the arc only), which leaves the strips disconnected; the recalculation runs on a
    merged copy and the original faces follow it, so the walker's traces meet front faces everywhere."""
    bm = _bm(obj)
    ref = bm.copy()
    bmesh.ops.remove_doubles(ref, verts=ref.verts, dist=1e-4)
    bmesh.ops.recalc_face_normals(ref, faces=ref.faces)
    ref.normal_update()
    def key(c): return (round(c.x, 4), round(c.y, 4), round(c.z, 4))
    want = {key(f.calc_center_median()): f.normal.copy() for f in ref.faces}
    ref.free()
    bad = []
    for f in bm.faces:
        n = want.get(key(f.calc_center_median()))
        if n is not None and f.normal.dot(n) < 0: bad.append(f)
    if bad: bmesh.ops.reverse_faces(bm, faces=bad)
    bm.to_mesh(obj.data); bm.free(); obj.data.update()
    return obj

def orient_toward(obj, point):
    """An open surface: every face toward a point in the game frame (the room's inside, the viewer)."""
    p = B(*point); bm = _bm(obj)
    bad = [f for f in bm.faces if f.normal.dot(p - f.calc_center_median()) < 0]
    if bad: bmesh.ops.reverse_faces(bm, faces=bad)
    bm.to_mesh(obj.data); bm.free(); obj.data.update()
    return obj

def rot(p, yaw=0.0, pitch=0.0, roll=0.0):
    x, y, z = p
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    cr, sr = math.cos(math.radians(roll)), math.sin(math.radians(roll))
    cyw, syw = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    y, z = y * cr - z * sr, y * sr + z * cr                 # roll about x
    x, z = x * cp + z * sp, -x * sp + z * cp                # pitch (nose up)
    x, y = x * cyw - y * syw, x * syw + y * cyw             # yaw
    return (x, y, z)

def box(name, center, size, material, group, yaw=0.0, pitch=0.0, roll=0.0, bevel=0.0):
    """Axis-aligned box in the game frame, then rolled, pitched (nose up) and yawed about its centre."""
    cx, cy, cz = center; sx, sy, sz = size
    verts = []
    for dx in (-0.5, 0.5):
        for dy in (-0.5, 0.5):
            for dz in (-0.5, 0.5):
                x, y, z = rot((dx * sx, dy * sy, dz * sz), yaw, pitch, roll)
                verts.append((cx + x, cy + y, cz + z))
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    return recalc_outward(mesh_from(name, verts, faces, material, group, bevel=bevel))

def _axis_pt(axis, r, a_deg, t):
    c, s = ang(a_deg)
    if axis == 'z': return (r * c, r * s, t)
    if axis == 'x': return (t, r * c, r * s)
    return (r * s, t, r * c)

def lathe(name, profile, center, material, group, axis='z', segments=48, a0=0.0, a1=360.0, closed=False, cap=True, smooth=True, inside=None):
    """Revolve an (r, t) profile about an axis through `center` (t along the axis, r radial; for 'x' bearing 0 is
    +y and 90 is +z, for 'z' bearing 0 is +x and 90 is +y). An open profile makes a surface (capped with discs at
    r > 0 ends on a full turn); a closed profile makes a solid ring, capped at both bearings on a partial turn.
    Closed results are made outward; open surfaces are turned toward `inside` if given."""
    cx, cy, cz = center
    full = abs(a1 - a0) >= 359.9
    n = segments if full else segments + 1
    P = len(profile); segs = P if closed else P - 1
    verts = []; faces = []
    def V(r, i, t):
        a = a0 + (a1 - a0) * i / segments
        x, y, z = _axis_pt(axis, r, a, t); return (cx + x, cy + y, cz + z)
    for j in range(segs):
        (r0, t0), (r1, t1) = profile[j], profile[(j + 1) % P]
        base = len(verts)
        for i in range(n):
            verts.append(V(r0, i, t0)); verts.append(V(r1, i, t1))
        for i in range(segments):
            i2 = (i + 1) % n
            faces.append((base + 2 * i, base + 2 * i2, base + 2 * i2 + 1, base + 2 * i + 1))
    solid = closed
    if cap and not closed and full:
        for (r, t), fl in ((profile[0], True), (profile[-1], False)):
            if r > 1e-6:
                base = len(verts)
                for i in range(segments): verts.append(V(r, i, t))
                idx = list(range(base, base + segments))
                faces.append(tuple(reversed(idx)) if fl else tuple(idx))
        solid = profile[0][0] > 1e-6 and profile[-1][0] > 1e-6 or (profile[0][0] <= 1e-6 and profile[-1][0] <= 1e-6)
    if closed and not full:
        for i in (0, segments):
            base = len(verts)
            for (r, t) in profile: verts.append(V(r, i, t))
            faces.append(tuple(range(base, base + P)))
    obj = mesh_from(name, verts, faces, material, group)
    if smooth:
        for poly in obj.data.polygons:
            if len(poly.vertices) == 4: poly.use_smooth = True
    if solid: recalc_outward(obj)
    elif inside is not None: orient_toward(obj, inside)
    return obj

def ring(name, center, r0, r1, t0, t1, material, group, axis='z', a0=0.0, a1=360.0, segments=48):
    return lathe(name, [(r0, t0), (r1, t0), (r1, t1), (r0, t1)], center, material, group, axis=axis, a0=a0, a1=a1, closed=True, segments=segments)

def tube(name, center, r, t0, t1, material, group, axis='z', segments=48):
    return lathe(name, [(r, t0), (r, t1)], center, material, group, axis=axis, segments=segments)

def disc(name, center, r, material, group, axis='z', up=(0, 0, 1), segments=48, r_in=0.0):
    cx, cy, cz = center
    obj = lathe(name, [(r_in, 0.0), (r, 0.0)], center, material, group, axis=axis, cap=False, segments=segments)
    return orient_toward(obj, (cx + up[0], cy + up[1], cz + up[2]))

def prism(name, outline, z0, z1, material, group):
    """A flat slab from a closed (x, y) outline."""
    n = len(outline)
    verts = [(x, y, z1) for (x, y) in outline] + [(x, y, z0) for (x, y) in outline]
    faces = [tuple(range(n)), tuple(reversed(range(n, 2 * n)))]
    for i in range(n):
        j = (i + 1) % n
        faces.append((i, n + i, n + j, j))
    return recalc_outward(mesh_from(name, verts, faces, material, group))

def grid_surface(name, rows, material, group, toward=None, smooth=True):
    """A quad mesh from rows of points (every row the same length), e.g. a liner swept along a profile. Faced
    toward `toward` when given."""
    n = len(rows[0]); verts = [p for row in rows for p in row]; faces = []
    for j in range(len(rows) - 1):
        for i in range(n - 1):
            a = j * n + i; faces.append((a, a + 1, a + n + 1, a + n))
    obj = mesh_from(name, verts, faces, material, group, smooth=smooth)
    return orient_toward(obj, toward) if toward else obj

def screen(name, center, w, h, material, group, yaw=0.0, pitch=0.0):
    """A one-sided quad showing an image to a viewer who looks along bearing `yaw` (0 = +x): the quad stands across
    that line of sight at `center`; positive pitch leans its top away from the viewer, so a desk screen faces up.
    UVs put the image's left to
    the viewer's left and its top up (checked on the bridge's station screens in game)."""
    cx, cy, cz = center
    pts = [(-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)]
    verts = []
    for (u, v) in pts:
        x, y, z = rot((0.0, u, v), yaw, pitch)
        verts.append((cx + x, cy + y, cz + z))
    uvs = {0: (1.0, 0.0), 1: (0.0, 0.0), 2: (0.0, 1.0), 3: (1.0, 1.0)}
    obj = mesh_from(name, verts, [(0, 3, 2, 1)], material, group, uvs=uvs)
    d = ang(yaw)
    return orient_toward(obj, (cx - d[0], cy - d[1], cz))

def panel(name, center, w, h, material, group, yaw=0.0, pitch=0.0, thick=0.02):
    """A thin slab standing across the line of sight `yaw`, like screen() but solid and unmapped."""
    return box(name, center, (thick, w, h), material, group, yaw=yaw, pitch=pitch)

# ---- furniture -------------------------------------------------------------------------------------------------
def chair(pos, yaw, group="Furniture"):
    """A grey swivel chair on a black pedestal, facing bearing `yaw`."""
    x, y, z = pos; d = ang(yaw)
    disc_c = (x, y, z + 0.015)
    lathe("ChairBase", [(0.0, 0.0), (0.30, 0.0), (0.30, 0.03), (0.0, 0.03)], (x, y, z), M_Black(), group, closed=True, segments=24)
    tube("ChairPost", (x, y, 0), 0.045, z + 0.03, z + 0.46, M_Black(), group, segments=16)
    box("ChairSeat", (x, y, z + 0.50), (0.50, 0.50, 0.08), M_Seat(), group, yaw=yaw, bevel=0.03)
    box("ChairBack", (x - d[0] * 0.24, y - d[1] * 0.24, z + 0.84), (0.08, 0.48, 0.56), M_Seat(), group, yaw=yaw, pitch=-8.0, bevel=0.03)

def console_desk(center, width, depth, yaw, tex, group="Consoles", screens=1, height=0.78):
    """A black console cabinet with a raked Okudagram screen and a touch strip, facing the viewer who looks along
    bearing `yaw` (the viewer stands on the -yaw side)."""
    cx, cy, cz = center; d = ang(yaw)
    box("Console", center[:2] + (cz + height / 2,), (depth, width, height), M_Black(), group, yaw=yaw, bevel=0.02)
    box("ConsoleLip", (cx - d[0] * (depth / 2 - 0.02), cy - d[1] * (depth / 2 - 0.02), cz + height + 0.01), (0.06, width, 0.03), M_PanelGrey(), group, yaw=yaw)
    # The raked screen on the far half of the top, the touch strip on the near half.
    sw = (width - 0.2) / screens
    for k in range(screens):
        off = -width / 2 + 0.1 + sw * (k + 0.5)
        sx, sy = cx + d[0] * (depth * 0.12) - d[1] * off, cy + d[1] * (depth * 0.12) + d[0] * off
        box("ConsoleHood", (sx, sy, cz + height + 0.21), (0.06, sw - 0.04, 0.46), M_Black(), group, yaw=yaw, pitch=32.0, bevel=0.01)
        screen("Scr", (sx - d[0] * 0.04, sy - d[1] * 0.04, cz + height + 0.21), sw - 0.12, 0.36, M_Screen(tex if isinstance(tex, str) else tex[k % len(tex)]), "Screens", yaw=yaw, pitch=32.0)
    box("ConsoleTouch", (cx - d[0] * (depth * 0.27), cy - d[1] * (depth * 0.27), cz + height + 0.006), (depth * 0.30, width - 0.3, 0.012), M_Buttons(), group, yaw=yaw)

def wall_screen(center, w, h, yaw, tex, group="Screens"):
    """A framed display flat on a wall, seen by a viewer looking along `yaw`."""
    cx, cy, cz = center; d = ang(yaw)
    box("ScreenFrame", (cx + d[0] * 0.02, cy + d[1] * 0.02, cz), (0.04, w + 0.12, h + 0.12), M_PanelDark(), "Consoles", yaw=yaw)
    screen("Scr", (cx - d[0] * 0.005, cy - d[1] * 0.005, cz), w, h, M_Screen(tex), group, yaw=yaw)

# The refit's corridor section (metres from the centre line): the deck grating, the lit lower panels leaning out
# to the waist, the straight band, the upper panels leaning in to the narrow dark ceiling (The Motion Picture to
# The Undiscovered Country; the waist is where a room's wall meets the corridor, back to back).
CORR_F, CORR_W, CORR_C, CORR_Z1, CORR_Z2, CORR_H = 0.95, 1.2, 0.8, 0.95, 1.55, 2.5
# Deck G's corridor: the athwartships leg (y) with its bays anchored at the junction with the forward leg, the
# forward leg (x) in seven bays; the doors sit at bay centres, and the rooms' sets and the game put their doorways
# at the same places (ShipInterior.cpp follows these numbers).
CORR_A_Y0, CORR_A_Y1, CORR_A_ANCHOR, CORR_A_PITCH = -11.5, 2.1, -9.1, 2.24
CORR_B_X0, CORR_B_X1, CORR_B_Y0, CORR_B_Y1 = 1.2, 18.2, -11.5, -9.1
CORR_B_PITCH = (CORR_B_X1 - CORR_B_X0) / 7
SICK_DOOR_Y = CORR_A_ANCHOR + 3.5 * CORR_A_PITCH          # -1.26: sickbay's doorway, to starboard
REC_DOOR_Y = CORR_A_ANCHOR + 1.5 * CORR_A_PITCH           # -5.74: the rec deck's, to port
TRANS_DOOR_X = CORR_B_X0 + 5.5 * CORR_B_PITCH             # 14.56: the transporter room's, off the forward leg

# ---- room shell ------------------------------------------------------------------------------------------------
class Room:
    def __init__(self, code, L, W, H):
        self.code, self.L, self.W, self.H = code, L, W, H
        self.lights = []        # (kind, pos, energy, color, size)

    def light(self, pos, energy, color=(0.95, 0.97, 1.0), size=0.8, kind='AREA'):
        self.lights.append((kind, pos, energy, color, size))

def wall_run(name, axis, pos, a0, a1, z0, z1, thick, material, group, openings=()):
    """A wall slab running along `axis` ('x' or 'y') at `pos` across it, from a0 to a1 along it and z0..z1 up, with
    rectangular openings cut through it: each (centre along the run, width, height above z0). The slab is built
    as the pieces round the openings (a lintel over each), so no booleans."""
    pieces = []; a = a0
    for (c, w, h) in sorted(openings):
        if c - w / 2 > a: pieces.append((a, c - w / 2, z0, z1))
        pieces.append((c - w / 2, c + w / 2, z0 + h, z1))
        a = c + w / 2
    if a < a1: pieces.append((a, a1, z0, z1))
    out = []
    for (s0, s1, b0, b1) in pieces:
        if s1 - s0 < 1e-3 or b1 - b0 < 1e-3: continue
        if axis == 'x': out.append(box(name, ((s0 + s1) / 2, pos, (b0 + b1) / 2), (s1 - s0, thick, b1 - b0), material, group))
        else: out.append(box(name, (pos, (s0 + s1) / 2, (b0 + b1) / 2), (thick, s1 - s0, b1 - b0), material, group))
    return out

def doorway(axis, pos, at, inward, dw=1.3, dh=2.3, label="Doorway", depth=0.24, leaves=None):
    """A door in a wall running along `axis` at `pos`, centred `at` along it: the dark jambs and head standing
    proud of the wall on both faces, a metal sill, a lit strip each side on the room's face (`inward` is the sign
    toward the room across the wall). With `leaves` the door is shut (two leaves of that material flush in the
    frame, the wall behind them left solid); without, it is an open doorway and the wall must carry the opening
    (see `wall_run` / `shell(openings=...)`)."""
    def at_(a, c, z): return (a, c, z) if axis == 'x' else (c, a, z)
    def sz_(a, c, z): return (a, c, z) if axis == 'x' else (c, a, z)
    for s in (-1, 1):
        box(label + "Jamb", at_(at + s * (dw / 2 + 0.08), pos, dh / 2 + 0.05), sz_(0.16, depth, dh + 0.1), M_PanelDark(), "Walls")
        box(label + "Lamp", at_(at + s * (dw / 2 + 0.26), pos + inward * (depth / 2 + 0.02), dh / 2 + 0.1), sz_(0.06, 0.03, dh - 0.3), M_Strip(), "Lights")
    box(label + "Head", at_(at, pos, dh + 0.1), sz_(dw + 0.32, depth, 0.2), M_PanelDark(), "Walls")
    box(label + "Sill", at_(at, pos, 0.005), sz_(dw, depth, 0.01), M_Metal(), "Floors")
    if leaves is not None:
        box(label, at_(at, pos + inward * (depth / 2 - 0.03), dh / 2), sz_(dw, 0.06, dh), leaves, "Walls")
        box(label + "Split", at_(at, pos + inward * (depth / 2 + 0.005), dh / 2), sz_(0.02, 0.01, dh), M_PanelDark(), "Walls")

def extrude_x(name, outline, x0, x1, material, group, xf=None, bevel=0.0):
    """A solid from a closed (y, z) section extruded along x0..x1, optionally through a transform."""
    n = len(outline)
    pts = [(x0, y, z) for (y, z) in outline] + [(x1, y, z) for (y, z) in outline]
    if xf: pts = [xf(p) for p in pts]
    faces = [tuple(range(n)), tuple(reversed(range(n, 2 * n)))]
    for i in range(n):
        j = (i + 1) % n
        faces.append((i, n + i, n + j, j))
    return recalc_outward(mesh_from(name, pts, faces, material, group, bevel=bevel))

def shell(R, wall_mat, floor_mat, ceil_mat, open_aft=False, floor=True, aft_trim=0.0, openings=(), skip_walls=()):
    """`aft_trim` shortens the floor, ceiling and side walls at the aft end (the bay stops at the hull's stern wall).
    `openings` cuts doorways: (wall, position along it, width, height) with wall one of "+x" (forward), "-x",
    "+y" (starboard), "-y"; `skip_walls` leaves walls out for the room to build its own (a window wall)."""
    L, W, H = R.L, R.W, R.H
    T = 0.12
    xa = -L / 2 + aft_trim if aft_trim > 0 else -L / 2 - T   # the aft end of the floor, ceiling and side walls
    cx, Lt = (xa + L / 2 + T) / 2, L / 2 + T - xa
    if floor:
        box("Floor", (cx, 0, -T / 2), (Lt, W + 2 * T, T), floor_mat, "Floors")
    box("Ceiling", (cx, 0, H + T / 2), (Lt, W + 2 * T, T), ceil_mat, "Ceiling")
    def cuts(wall, h=None): return [(p, w, hh if h is None else h) for (wl, p, w, hh) in openings if wl == wall]
    if "+x" not in skip_walls:
        wall_run("WallFwd", 'y', L / 2 + T / 2, -W / 2 - T, W / 2 + T, 0, H, T, wall_mat, "Walls", cuts("+x"))
        wall_run("Skirt", 'y', L / 2 - 0.02, -W / 2, W / 2, 0, 0.36, 0.04, M_PanelDark(), "Walls", cuts("+x", 0.36))
    if not open_aft and "-x" not in skip_walls:
        wall_run("WallAft", 'y', -L / 2 - T / 2, -W / 2 - T, W / 2 + T, 0, H, T, wall_mat, "Walls", cuts("-x"))
        wall_run("Skirt", 'y', -L / 2 + 0.02, -W / 2, W / 2, 0, 0.36, 0.04, M_PanelDark(), "Walls", cuts("-x", 0.36))
    for sgn, wall in ((-1, "-y"), (1, "+y")):
        if wall in skip_walls: continue
        wall_run("WallSide", 'x', sgn * (W / 2 + T / 2), xa, L / 2 + T, 0, H, T, wall_mat, "Walls", cuts(wall))
        wall_run("Skirt", 'x', sgn * (W / 2 - 0.02), xa if aft_trim > 0 else -L / 2, L / 2, 0, 0.36, 0.04, M_PanelDark(), "Walls", cuts(wall, 0.36))

def pilasters(R, pitch, walls=("+y", "-y", "+x", "-x"), width=0.36, depth=0.14, skip=()):
    """Lit pilasters along the walls every `pitch` metres: a grey rib with a white strip up its middle. `skip`
    holds (wall, pos) pairs to leave clear (doors)."""
    L, W, H = R.L, R.W, R.H
    def run(wall, length):
        n = int(length // pitch)
        start = -(n - 1) * pitch / 2
        for k in range(n):
            s = start + k * pitch
            if any(w == wall and abs(p - s) < 1.0 for (w, p) in skip): continue
            if wall in ("+y", "-y"):
                sgn = 1 if wall == "+y" else -1
                box("Pilaster", (s, sgn * (W / 2 - depth / 2), H / 2), (width, depth, H), M_PanelGrey(), "Walls")
                box("PilasterStrip", (s, sgn * (W / 2 - depth - 0.01), H / 2 + 0.1), (0.07, 0.02, H - 0.9), M_Strip(), "Lights")
            else:
                sgn = 1 if wall == "+x" else -1
                box("Pilaster", (sgn * (L / 2 - depth / 2), s, H / 2), (depth, width, H), M_PanelGrey(), "Walls")
                box("PilasterStrip", (sgn * (L / 2 - depth - 0.01), s, H / 2 + 0.1), (0.02, 0.07, H - 0.9), M_Strip(), "Lights")
    for wall in walls:
        run(wall, W if wall in ("+x", "-x") else L)

def lift_door(R, wall, pos, mat_=None, label="Door"):
    """A door in a wall at `pos` along it: burgundy leaves in a dark frame, a lit strip each side."""
    L, W, H = R.L, R.W, R.H
    mat_ = mat_ or M_Burgundy()
    dw, dh = 1.3, 2.3
    if wall in ("+y", "-y"):
        sgn = 1 if wall == "+y" else -1
        y = sgn * (W / 2 - 0.05)
        box(label + "Frame", (pos, y, dh / 2 + 0.1), (dw + 0.36, 0.10, dh + 0.2), M_PanelDark(), "Walls")
        box(label, (pos, y - sgn * 0.03, dh / 2), (dw, 0.06, dh), mat_, "Walls")
        box(label + "Split", (pos, y - sgn * 0.065, dh / 2), (0.02, 0.01, dh), M_PanelDark(), "Walls")
        for s in (-1, 1):
            box(label + "Lamp", (pos + s * (dw / 2 + 0.26), y - sgn * 0.02, dh / 2 + 0.1), (0.06, 0.03, dh - 0.3), M_Strip(), "Lights")
    else:
        sgn = 1 if wall == "+x" else -1
        x = sgn * (L / 2 - 0.05)
        box(label + "Frame", (x, pos, dh / 2 + 0.1), (0.10, dw + 0.36, dh + 0.2), M_PanelDark(), "Walls")
        box(label, (x - sgn * 0.03, pos, dh / 2), (0.06, dw, dh), mat_, "Walls")
        box(label + "Split", (x - sgn * 0.065, pos, dh / 2), (0.01, 0.02, dh), M_PanelDark(), "Walls")
        for s in (-1, 1):
            box(label + "Lamp", (x - sgn * 0.02, pos + s * (dw / 2 + 0.26), dh / 2 + 0.1), (0.03, 0.06, dh - 0.3), M_Strip(), "Lights")

def ceiling_lights(R, spots, size=(1.2, 0.6), energy=40.0):
    for (x, y) in spots:
        box("CeilLight", (x, y, R.H - 0.02), (size[0], size[1], 0.04), M_Soft(), "Lights")
        R.light((x, y, R.H - 0.08), energy, size=max(size))

def ceiling_beams(R, pitch, size=0.3):
    n = int(R.L // pitch)
    start = -(n - 1) * pitch / 2
    for k in range(n):
        box("Beam", (start + k * pitch, 0, R.H - size / 2), (size, R.W, size), M_PanelGrey(), "Ceiling")

def extrude_y(name, outline, y0, y1, material, group, xf=None, bevel=0.0):
    """A solid from a closed (x, z) side profile extruded across y0..y1, optionally through a transform."""
    n = len(outline)
    pts = [(x, y0, z) for (x, z) in outline] + [(x, y1, z) for (x, z) in outline]
    if xf: pts = [xf(p) for p in pts]
    faces = [tuple(range(n)), tuple(reversed(range(n, 2 * n)))]
    for i in range(n):
        j = (i + 1) % n
        faces.append((i, n + i, n + j, j))
    return recalc_outward(mesh_from(name, pts, faces, material, group, bevel=bevel))

def vault_ceiling(R, pitch_x, rise=1.2, energy=30.0):
    """The Motion Picture's engineering ceiling: a shallow barrel vault across the room under dark ribs, long soft
    light panels on the vault between the ribs."""
    L, W, H = R.L, R.W, R.H
    half = W / 2
    Ra = (half * half + rise * rise) / (2 * rise)
    zc = H - 0.12 - Ra
    th = math.degrees(math.asin(half / Ra))
    lathe("Vault", [(Ra, -L / 2), (Ra, L / 2)], (0, 0, zc), M_PanelGrey(), "Ceiling", axis='x', a0=90 - th, a1=90 + th, segments=28, inside=(0, 0, 0))
    n = int(L // pitch_x); start = -(n - 1) * pitch_x / 2
    for k in range(n):
        x = start + k * pitch_x
        ring("Rib", (x, 0, zc), Ra - 0.30, Ra + 0.02, -0.16, 0.16, M_PanelDark(), "Ceiling", axis='x', a0=90 - th, a1=90 + th, segments=28)
        ring("RibEdge", (x, 0, zc), Ra - 0.31, Ra - 0.29, -0.06, 0.06, M_Strip(), "Lights", axis='x', a0=90 - th, a1=90 + th, segments=28)
    for k in range(n - 1):
        x = start + (k + 0.5) * pitch_x
        for b in (-9.0, 9.0):
            ring("VaultLight", (x, 0, zc), Ra - 0.05, Ra - 0.01, -(pitch_x - 0.7) / 2, (pitch_x - 0.7) / 2, M_Soft(), "Lights", axis='x', a0=90 + b - 3.2, a1=90 + b + 3.2, segments=4)
            c, sn = ang(90 + b)
            R.light((x, Ra * c, zc + Ra * sn - 0.12), energy, size=1.0)

def truss_ceiling(R, pitch_x, depth=0.9, light_size=(2.2, 0.9), energy=120.0, runner_y=3.5):
    """The landing bay's roof: open trusses across the room, two long runners under them carrying the light
    panels."""
    L, W, H = R.L, R.W, R.H
    n = int(L // pitch_x); start = -(n - 1) * pitch_x / 2
    for k in range(n):
        x = start + k * pitch_x
        box("TrussTop", (x, 0, H - 0.1), (0.3, W, 0.2), M_PanelGrey(), "Ceiling")
        box("TrussBot", (x, 0, H - depth), (0.24, W - 0.4, 0.14), M_PanelGrey(), "Ceiling")
        m = int(W // 2.0)
        for j in range(m + 1):
            y = -W / 2 + 0.2 + (W - 0.4) * j / m
            box("TrussPost", (x, y, H - depth / 2 - 0.05), (0.12, 0.12, depth - 0.2), M_PanelGrey(), "Ceiling")
            if j < m:
                y1 = -W / 2 + 0.2 + (W - 0.4) * (j + 1) / m
                seg = (W - 0.4) / m; sgn = 1 if j % 2 == 0 else -1
                box("TrussWeb", (x, (y + y1) / 2, H - depth / 2 - 0.05), (0.08, math.hypot(seg, depth - 0.3), 0.08), M_PanelGrey(), "Ceiling", roll=sgn * math.degrees(math.atan2(depth - 0.3, seg)))
    for sgn in (-1, 1):
        box("Runner", (0, sgn * runner_y, H - depth - 0.18), (L - 0.4, 0.26, 0.2), M_PanelDark(), "Ceiling")
    for k in range(n - 1):
        x = start + (k + 0.5) * pitch_x
        for sgn in (-1, 1):
            box("BayLight", (x, sgn * runner_y, H - depth - 0.32), (light_size[0], light_size[1], 0.08), M_Soft(), "Lights")
            R.light((x, sgn * runner_y, H - depth - 0.42), energy, size=max(light_size))

def cove_ceiling(R, drop=0.32, width=0.9, panels=(), energy=18.0):
    """Sickbay's ceiling: a lowered soffit round the walls with a hidden light cove along its inner edge, the
    raised centre coffered by slim bars, soft panels in the coffers."""
    L, W, H = R.L, R.W, R.H
    for sgn in (-1, 1):
        box("Soffit", (0, sgn * (W / 2 - width / 2), H - drop / 2), (L, width, drop), M_PanelGrey(), "Ceiling")
        box("Soffit", (sgn * (L / 2 - width / 2), 0, H - drop / 2), (width, W, drop), M_PanelGrey(), "Ceiling")
        box("Cove", (0, sgn * (W / 2 - width - 0.03), H - drop + 0.04), (L - 2 * width, 0.06, 0.06), M_Soft(), "Lights")
        box("Cove", (sgn * (L / 2 - width - 0.03), 0, H - drop + 0.04), (0.06, W - 2 * width, 0.06), M_Soft(), "Lights")
        R.light((0, sgn * (W / 2 - width - 0.3), H - 0.1), energy * 1.5, size=0.4)
    for x in (-L / 2 + width + (L - 2 * width) / 3, L / 2 - width - (L - 2 * width) / 3):
        box("Coffer", (x, 0, H - 0.05), (0.12, W - 2 * width, 0.1), M_PanelGrey(), "Ceiling")
    box("Coffer", (0, 0, H - 0.05), (L - 2 * width, 0.12, 0.1), M_PanelGrey(), "Ceiling")
    for (x, y) in panels:
        box("CeilLight", (x, y, H - 0.02), (1.4, 0.7, 0.04), M_Soft(), "Lights")
        R.light((x, y, H - 0.08), energy, size=1.2)

def radial_ceiling(R, centre, r_in, ribs=8, energy=14.0, panels=()):
    """The transporter room's ceiling: a dark step ring round the lit ring over the pad, grey ribs radiating from
    it to the walls, soft panels between."""
    H = R.H; cx, cy = centre
    ring("StepRing", (cx, cy, 0), r_in, r_in + 0.4, H - 0.16, H, M_PanelDark(), "Ceiling", segments=64)
    for k in range(ribs):
        a = 360.0 * k / ribs + 22.5; c, s = ang(a)
        box("RadialRib", (cx + (r_in + 0.4 + 1.6) * c, cy + (r_in + 0.4 + 1.6) * s, H - 0.07), (3.2, 0.14, 0.14), M_PanelGrey(), "Ceiling", yaw=a)
    for (x, y) in panels:
        box("CeilLight", (x, y, H - 0.02), (1.0, 0.5, 0.04), M_Soft(), "Lights")
        R.light((x, y, H - 0.08), energy, size=0.8)

# ---- engineering -----------------------------------------------------------------------------------------------
def build_engineering():
    """Main engineering: the vertical intermix shaft rising through a railed well at the aft end of the room,
    the horizontal chamber leaving it toward the stern at head height in its ribbed cage, and forward of it the
    control room: the chief's console facing the core, long consoles with screen walls down both sides, glass
    partitions either side of the well. Lift door forward, starboard."""
    R = Room("eng", 16.0, 10.0, 4.5)
    L, W, H = R.L, R.W, R.H
    SX, WR, WD = -3.0, 2.2, 2.5          # shaft x, well radius, well depth
    shell(R, M_PanelGrey(), M_DeckPlate(), M_PanelDark(), floor=False)
    # The deck in two halves, each notched round the well.
    for sgn in (-1, 1):
        outline = [(L / 2, 0.0), (L / 2, sgn * W / 2), (-L / 2, sgn * W / 2), (-L / 2, 0.0), (SX - WR, 0.0)]
        for k in range(1, 30):
            a = 180.0 - 180.0 * k / 30.0
            c, s = ang(a); outline.append((SX + WR * c, sgn * WR * s))
        outline.append((SX + WR, 0.0))
        prism("Deck", outline, -0.12, 0.0, M_DeckPlate(), "Floors")
    # The well: dark wall, lit floor ring, the shaft's base collar with its vents.
    lathe("WellWall", [(WR, 0.0), (WR, -WD)], (SX, 0, 0), M_PanelDark(), "Walls", inside=(SX, 0, -1.0))
    disc("WellFloor", (SX, 0, -WD), WR, M_PanelDark(), "Floors")
    ring("WellRing", (SX, 0, 0), 1.15, 1.6, -WD + 0.005, -WD + 0.03, M_Strip(), "Lights")
    ring("DeckLip", (SX, 0, 0), WR, WR + 0.12, -0.12, 0.03, M_Metal(), "Floors")
    ring("BaseCollar", (SX, 0, 0), 0.78, 1.35, -WD + 0.3, -WD + 1.0, M_PanelDark(), "Core")
    for k in range(4):
        c, s = ang(45 + 90 * k)
        box("Vent", (SX + 1.36 * c, 1.36 * s, -WD + 0.65), (0.06, 0.5, 0.2), M_Vent(), "Lights", yaw=45 + 90 * k)
    # The shaft itself and its cage.
    tube("Shaft", (SX, 0, 0), 0.72, -WD + 0.3, H, M_Core(), "Core")
    for k in range(8):
        c, s = ang(22.5 + 45 * k)
        box("ShaftRib", (SX + 0.86 * c, 0.86 * s, (H - WD + 0.3) / 2), (0.10, 0.16, H + WD - 0.3), M_Metal(), "Core", yaw=22.5 + 45 * k)
    for z in (0.6, 1.9, 3.2):
        ring("Collar", (SX, 0, 0), 0.78, 1.02, z, z + 0.28, M_PanelDark(), "Core")
    ring("TopCollar", (SX, 0, 0), 0.78, 1.3, H - 0.35, H, M_PanelDark(), "Core")
    R.light((SX, 0, 2.0), 150.0, color=(0.4, 0.65, 1.0), size=0.5, kind='POINT')
    R.light((SX, 0, -1.5), 60.0, color=(0.4, 0.65, 1.0), size=0.5, kind='POINT')
    # The horizontal chamber, aft from the shaft through the aft bulkhead, in its ringed cage with four runners.
    ZC, X0, X1 = 3.1, SX - 0.8, -L / 2 - 0.6
    tube("Chamber", (0, 0, ZC), 0.52, X0, X1, M_Core(), "Core", axis='x')
    ring("Junction", (SX - 0.95, 0, ZC), 0.56, 1.0, -0.2, 0.2, M_Metal(), "Core", axis='x')
    x = X0 - 0.6
    while x > -L / 2 + 0.3:
        ring("ChamberRing", (x, 0, ZC), 0.58, 0.74, -0.07, 0.07, M_PanelDark(), "Core", axis='x')
        x -= 0.8
    for k in range(4):
        a = 45 + 90 * k; c, s = ang(a)
        box("Runner", ((X0 + (-L / 2)) / 2, 0.66 * c, ZC + 0.66 * s), (abs(X0 + L / 2), 0.10, 0.14), M_Metal(), "Core", roll=a)
    ring("AftRing", (-L / 2 + 0.07, 0, ZC), 0.8, 1.25, -0.08, 0.08, M_Strip(), "Lights", axis='x')
    # The rail round the well.
    for k in range(12):
        c, s = ang(30 * k)
        box("RailPost", (SX + 2.4 * c, 2.4 * s, 0.52), (0.06, 0.06, 1.04), M_Rail(), "Rails", yaw=30 * k)
    # Three rails: the top one at hand height, a mid rail, and a low one across the walker's knee probe (45 cm) so the
    # railing stops anyone between the posts.
    ring("RailTop", (SX, 0, 0), 2.37, 2.44, 1.0, 1.06, M_Rail(), "Rails")
    ring("RailMid", (SX, 0, 0), 2.38, 2.42, 0.70, 0.74, M_Rail(), "Rails")
    ring("RailLow", (SX, 0, 0), 2.38, 2.42, 0.42, 0.48, M_Rail(), "Rails")
    # Glass partitions either side of the well, leaving the middle open.
    for sgn in (-1, 1):
        box("Glass", (-0.9, sgn * 3.1, 1.25), (0.03, 3.1, 2.3), M_Glass(), "Glass")
        for y in (1.55, 4.65):
            box("GlassPost", (-0.9, sgn * y, 1.25), (0.08, 0.08, 2.5), M_Metal(), "Rails")
        box("GlassRail", (-0.9, sgn * 3.1, 2.46), (0.08, 3.1, 0.08), M_Metal(), "Rails")
        box("GlassFoot", (-0.9, sgn * 3.1, 0.05), (0.10, 3.1, 0.10), M_Metal(), "Rails")
    # The chief's console: faces the core; the player stands at x 2.2 looking aft (the Use spot).
    console_desk((1.2, 0.0, 0.0), 2.0, 1.1, 180.0, "Engineering", screens=2)
    for y in (-0.85, 0.85):
        chair((2.35, y, 0.0), 180.0)
    # Side consoles down both walls with the screen walls above them (The Wrath of Khan), three chairs each.
    for sgn in (-1, 1):
        yaw = 90.0 * sgn
        console_desk((3.0, sgn * 4.45, 0.0), 8.0, 0.75, yaw, ["Engineering", "Science", "MasterSituation", "Tactical"], screens=4)
        for k, x in enumerate((0.0, 2.0, 4.0, 6.0)):
            wall_screen((x, sgn * (W / 2 - 0.16), 1.85), 1.4, 0.7, yaw, ["MasterSituation", "Engineering", "Tactical", "Science"][k])
        for x in (1.0, 3.0, 5.0):
            chair((x, sgn * 3.55, 0.0), yaw)
    # A big master systems display on the forward bulkhead, port of the lift.
    wall_screen((L / 2 - 0.16, -2.0, 1.9), 2.4, 1.2, 0.0, "MasterSituation")
    pilasters(R, 2.0, skip=(("+x", 3.0), ("+x", -2.0), ("-x", 0.0)))
    lift_door(R, "+x", 3.0)
    vault_ceiling(R, 2.0, rise=1.2, energy=30.0)
    return R

# ---- sickbay ---------------------------------------------------------------------------------------------------
def build_sickbay():
    """Sickbay: three ward beds in bays along the port wall with their hooded monitors, the diagnostic bed under its
    scanner arch in the middle, the doctor's desk to starboard facing the ward (the Use spot), a lit supplies
    cabinet on the starboard wall and the canted lit panels of the forward bulkhead. Lift door aft, port."""
    R = Room("sick", 12.0, 9.0, 3.0)
    L, W, H = R.L, R.W, R.H
    shell(R, M_PanelLight(), M_Carpet(), M_PanelGrey(), openings=[("-x", SICK_DOOR_Y, 1.3, 2.3)])
    # Ward beds.
    for bx in (-3.6, -1.2, 1.2):
        for s in (-1, 1):
            box("BayDivider", (bx + s * 1.15, -W / 2 + 0.65, 1.15), (0.08, 1.3, 2.3), M_PanelGrey(), "Walls")
        box("BedBase", (bx, -W / 2 + 1.15, 0.26), (0.6, 1.5, 0.52), M_PanelDark(), "Furniture")
        box("Bed", (bx, -W / 2 + 1.2, 0.62), (0.9, 2.0, 0.2), M_BedWhite(), "Furniture", bevel=0.03)
        box("Pillow", (bx, -W / 2 + 0.55, 0.77), (0.5, 0.4, 0.1), M_BedWhite(), "Furniture", bevel=0.03)
        # The clamshell: a white hoop over the pillow, open toward the foot, a small readout inside it.
        ring("BedHood", (bx, -W / 2 + 0.62, 0.78), 0.50, 0.60, -0.26, 0.26, M_BedWhite(), "Furniture", axis='y', a0=-82.0, a1=82.0, segments=24)
        ring("BedHoodBand", (bx, -W / 2 + 0.62, 0.78), 0.49, 0.51, -0.05, 0.05, M_Buttons(), "Furniture", axis='y', a0=-60.0, a1=60.0, segments=24)
        # The biofunction panel on the wall above: a black board with three lit bars and the Okudagram readout.
        box("BioPanel", (bx, -W / 2 + 0.05, 1.85), (1.0, 0.08, 0.9), M_Black(), "Consoles", bevel=0.01)
        for j, h in enumerate((1.55, 1.68, 1.81)):
            box("BioBar", (bx - 0.22, -W / 2 + 0.095, h), (0.42, 0.01, 0.06), M_Buttons() if j != 1 else M_Vent(), "Lights")
        screen("Scr", (bx + 0.22, -W / 2 + 0.095, 1.7), 0.42, 0.34, M_Screen("Science"), "Screens", yaw=-90.0)
        box("BioLamp", (bx, -W / 2 + 0.095, 2.17), (0.9, 0.01, 0.1), M_Soft(), "Lights")
        R.light((bx, -W / 2 + 1.0, H - 0.4), 10.0, size=0.5)
    # The diagnostic bed and its scanner arch.
    EX, EY = 0.8, 1.3
    box("ExamBase", (EX, EY, 0.32), (1.4, 0.5, 0.64), M_PanelDark(), "Furniture")
    box("ExamBed", (EX, EY, 0.74), (2.1, 0.85, 0.2), M_BedWhite(), "Furniture", bevel=0.03)
    ring("ScanArch", (EX, EY, 0.95), 0.9, 1.02, -0.14, 0.14, M_PanelGrey(), "Furniture", axis='x', a0=0.0, a1=180.0, segments=32)
    for s in (-1, 1):
        box("ArchPost", (EX, EY + s * 0.96, 0.47), (0.28, 0.12, 0.95), M_PanelGrey(), "Furniture")
    box("ArchLamp", (EX, EY, 1.95), (0.2, 0.5, 0.04), M_Soft(), "Lights")
    # The surgical light cluster over the table, on a stalk from the ceiling, and the scanner pod with its readout.
    box("LampStalk", (EX - 0.6, EY, H - 0.3), (0.06, 0.06, 0.6), M_Metal(), "Furniture")
    for k in range(3):
        c, s = ang(120 * k + 90)
        lathe("SurgLamp", [(0.0, 0.0), (0.22, 0.0), (0.22, 0.08), (0.0, 0.08)], (EX - 0.6 + 0.3 * c, EY + 0.3 * s, H - 0.68), M_PanelLight(), "Furniture", closed=True, segments=20)
        disc("SurgLampFace", (EX - 0.6 + 0.3 * c, EY + 0.3 * s, H - 0.685), 0.19, M_Soft(), "Lights", up=(0, 0, -1), segments=20)
    R.light((EX - 0.6, EY, H - 0.8), 20.0, size=0.5, kind='POINT')
    box("ScanPod", (EX, EY, 2.45), (0.5, 0.7, 0.3), M_PanelDark(), "Furniture", bevel=0.03)
    screen("Scr", (EX + 0.26, EY, 2.45), 0.5, 0.22, M_Screen("Science"), "Screens", yaw=180.0)
    # An instrument cart beside the table.
    box("Cart", (EX - 1.6, EY, 0.42), (0.5, 0.7, 0.84), M_PanelLight(), "Furniture", bevel=0.02)
    box("CartTop", (EX - 1.6, EY, 0.86), (0.54, 0.74, 0.04), M_Black(), "Furniture")
    for j in range(4):
        box("Hypo", (EX - 1.75 + 0.1 * j, EY - 0.2, 0.92), (0.03, 0.14, 0.08), M_Metal(), "Furniture")
    box("CartTray", (EX - 1.6, EY + 0.15, 0.9), (0.3, 0.3, 0.03), M_Buttons(), "Lights")
    # The doctor's desk, facing the ward: the player stands on the ward side at x 2.35 looking +x.
    DX, DY = 3.2, 2.0
    box("Desk", (DX, DY, 0.36), (0.75, 1.5, 0.72), M_PanelGrey(), "Furniture", bevel=0.02)
    box("DeskTop", (DX, DY, 0.74), (0.82, 1.56, 0.04), M_Black(), "Furniture", bevel=0.01)
    box("DeskMonitor", (DX - 0.05, DY + 0.3, 1.0), (0.05, 0.5, 0.36), M_Black(), "Furniture", pitch=12.0, bevel=0.008)
    screen("Scr", (DX - 0.08, DY + 0.3, 1.0), 0.44, 0.3, M_Screen("Science"), "Screens", yaw=0.0, pitch=12.0)
    box("DeskTouch", (DX + 0.1, DY - 0.3, 0.765), (0.3, 0.5, 0.01), M_Buttons(), "Furniture")
    chair((DX + 0.75, DY, 0.0), 180.0)
    # The office is screened from the ward by a glass partition on a low wall, with a gap by the desk.
    box("OfficeWall", (1.9, 2.9, 0.45), (0.12, 1.3, 0.9), M_PanelGrey(), "Walls")
    box("OfficeGlass", (1.9, 2.9, 1.65), (0.03, 1.3, 1.5), M_Glass(), "Glass")
    for y in (2.3, 3.5):
        box("OfficePost", (1.9, y, 1.2), (0.08, 0.08, 2.4), M_Metal(), "Rails")
    box("OfficeRail", (1.9, 2.9, 2.42), (0.08, 1.3, 0.08), M_Metal(), "Rails")
    wall_screen((2.6, W / 2 - 0.16, 1.75), 1.6, 0.8, 90.0, "Science")
    # Drawers of supplies on the aft bulkhead, starboard of the lift.
    for row in range(4):
        for col in range(4):
            box("Drawer", (-L / 2 + 0.2, 0.3 + col * 0.72, 0.5 + row * 0.42), (0.36, 0.66, 0.36), M_PanelLight(), "Furniture", bevel=0.01)
            box("DrawerPull", (-L / 2 + 0.39, 0.3 + col * 0.72, 0.5 + row * 0.42), (0.02, 0.3, 0.03), M_Buttons(), "Lights")
    box("DrawerTop", (-L / 2 + 0.2, 1.38, 2.2), (0.4, 3.1, 0.06), M_Black(), "Furniture")
    # Supplies cabinet on the starboard wall, lit behind glass.
    # A case open at the front: back, sides, top and the lower cupboard, lit shelves inside, glass across the opening.
    CY = W / 2 - 0.26
    box("CabinetBack", (-2.25, W / 2 - 0.03, 1.1), (3.5, 0.06, 2.2), M_PanelGrey(), "Furniture")
    box("CabinetBackLit", (-2.25, W / 2 - 0.07, 1.5), (3.3, 0.02, 1.2), M_Soft(), "Lights")
    for s_ in (-1, 1):
        box("CabinetSide", (-2.25 + s_ * 1.72, CY, 1.1), (0.06, 0.5, 2.2), M_PanelGrey(), "Furniture")
    box("CabinetTop", (-2.25, CY, 2.17), (3.5, 0.5, 0.06), M_PanelGrey(), "Furniture")
    box("CabinetHead", (-2.25, W / 2 - 0.28, 2.0), (3.5, 0.46, 0.3), M_PanelGrey(), "Furniture")
    box("CabinetLower", (-2.25, CY, 0.44), (3.5, 0.5, 0.88), M_PanelGrey(), "Furniture")
    for z in (1.15, 1.55):
        box("Shelf", (-2.25, W / 2 - 0.28, z), (3.38, 0.4, 0.03), M_PanelLight(), "Furniture")
    box("CabinetGlass", (-2.25, W / 2 - 0.5, 1.37), (3.38, 0.02, 0.96), M_Glass(), "Glass")
    box("Counter", (-2.25, W / 2 - 0.4, 0.86), (3.6, 0.8, 0.06), M_Black(), "Furniture", bevel=0.01)
    # The forward bulkhead's canted panels with lit louvres (The Motion Picture).
    for y in (-2.8, -0.95, 0.95, 2.8):
        cx, cz = L / 2 - 0.36, 1.3
        box("CantPanel", (cx, y, cz), (0.22, 1.6, 2.4), M_PanelGrey(), "Walls", pitch=-10.0)
        for h in (-0.55, 0.0, 0.55):
            p = rot((-0.125, 0.0, h), pitch=-10.0)
            box("Louvre", (cx + p[0], y, cz + p[2]), (0.02, 1.3, 0.09), M_Soft(), "Lights", pitch=-10.0)
    # A second door forward to the rest of the complex, and the doorway aft onto deck G's corridor (the lift is
    # down the corridor).
    lift_door(R, "+x", -2.0 + 0.0 if False else 4.1, mat_=M_PanelDark(), label="WardDoor")
    pilasters(R, 2.0, walls=("+y", "-x"), skip=(("-x", SICK_DOOR_Y),))
    doorway('y', -L / 2 - 0.06, SICK_DOOR_Y, 1, label="CorridorDoor")
    cove_ceiling(R, drop=0.32, width=0.9, panels=[(-2.5, -1.6), (-2.5, 1.6), (2.5, -1.6), (2.5, 1.6)], energy=16.0)
    return R

# ---- shuttle bay -----------------------------------------------------------------------------------------------
def shuttle(center, yaw=0.0, name="Galileo"):
    """The Type-4 shuttle of The Final Frontier (the Galileo) on her skids: the long white cabin with its chamfered
    lower chines and canted upper sides, the nose sloping down to the windscreen and back under the chin, the
    raised roof deck with its spine, the flat sensor and drive pods slung low beside the hull on short pylons
    (their forward faces lit, grilles aft), the bevelled aft door, the port door over its step forward, four landing
    struts on pads, and the red stripe along the flank; built facing +x then yawed about `center`."""
    cx, cy, cz = center
    def P(p): x, y, z = rot(p, yaw); return (cx + x, cy + y, cz + z)
    # The cabin: one section from the aft door to the cockpit bulkhead.
    sec = [(-1.25, 0.95), (-0.95, 0.55), (0.95, 0.55), (1.25, 0.95), (1.25, 1.95), (1.05, 2.35), (-1.05, 2.35), (-1.25, 1.95)]
    extrude_x("Hull", sec, -3.7, 1.5, M_HullWhite(), "Shuttle", xf=P, bevel=0.05)
    # The nose, a little narrower: the cockpit roof slopes to the windscreen, the chin slopes back under it.
    nose = [(1.5, 0.55), (1.5, 2.35), (2.3, 2.3), (3.2, 1.75), (3.65, 1.25), (3.65, 1.0), (3.1, 0.55)]
    extrude_y("Nose", nose, -1.1, 1.1, M_HullWhite(), "Shuttle", xf=P, bevel=0.07)
    # The roof deck, its spine and the beacon.
    box("RoofDeck", P((-1.2, 0.0, 2.45)), (4.6, 1.6, 0.2), M_HullWhite(), "Shuttle", yaw=yaw, bevel=0.06)
    box("Spine", P((-1.0, 0.0, 2.62)), (3.4, 0.5, 0.16), M_HullGrey(), "Shuttle", yaw=yaw, bevel=0.05)
    box("Beacon", P((0.9, 0.0, 2.73)), (0.14, 0.14, 0.06), M_Warn(), "Shuttle", yaw=yaw)
    # The windscreen on the nose slope, set just proud of it.
    (x0, z0), (x1, z1) = (2.3, 2.3), (3.2, 1.75)
    ptc = math.degrees(math.atan2(z0 - z1, x1 - x0))
    nrm = rot((0.0, 0.0, 1.0), pitch=ptc)
    mx, mz = (x0 + x1) / 2, (z0 + z1) / 2
    box("Windscreen", P((mx + nrm[0] * 0.02, 0.0, mz + nrm[2] * 0.02)), (1.0, 1.7, 0.04), M_Black(), "Shuttle", yaw=yaw, pitch=ptc)
    # The pods: low enough for a walker's knee to meet them (the deck walk traces at 45 cm), aft of the port door.
    for s_ in (-1, 1):
        y = s_ * 1.95
        box("Pod", P((-1.5, y, 0.75)), (4.0, 0.9, 0.7), M_HullWhite(), "Shuttle", yaw=yaw, bevel=0.12)
        box("Pylon", P((-1.5, s_ * 1.4, 0.95)), (2.2, 0.3, 0.3), M_HullGrey(), "Shuttle", yaw=yaw, bevel=0.04)
        box("PodSensor", P((0.52, y, 0.75)), (0.04, 0.6, 0.3), M_StripBlue(), "Shuttle", yaw=yaw)
        box("PodGlow", P((-3.52, y, 0.75)), (0.04, 0.6, 0.4), M_Core(), "Shuttle", yaw=yaw)
        box("PodStripe", P((-1.5, s_ * 2.41, 0.95)), (3.6, 0.02, 0.06), M_PaintRed(), "Shuttle", yaw=yaw)
        box("Stripe", P((-0.6, s_ * 1.26, 1.95)), (4.4, 0.02, 0.1), M_PaintRed(), "Shuttle", yaw=yaw)
        box("StripeGrey", P((-0.6, s_ * 1.26, 1.8)), (4.4, 0.02, 0.05), M_HullGrey(), "Shuttle", yaw=yaw)
        # Four landing struts on their pads under the chines (the Galileo stood on legs, not skids).
        for sx in (-2.6, 0.8):
            box("Strut", P((sx, s_ * 0.75, 0.31)), (0.18, 0.18, 0.5), M_HullGrey(), "Shuttle", yaw=yaw)
            box("Pad", P((sx, s_ * 0.75, 0.03)), (0.5, 0.4, 0.06), M_PanelDark(), "Shuttle", yaw=yaw, bevel=0.01)
    # The aft door in its bevelled frame; the port door, forward, over its step.
    box("AftFrame", P((-3.74, 0.0, 1.45)), (0.08, 2.1, 1.7), M_HullGrey(), "Shuttle", yaw=yaw, bevel=0.2)
    box("AftDoor", P((-3.78, 0.0, 1.45)), (0.03, 1.7, 1.4), M_PanelDark(), "Shuttle", yaw=yaw, bevel=0.1)
    # The port door on the flank's vertical run (z 0.95..1.95; above it the side cants in and a taller door would stand proud).
    box("PortDoor", P((1.0, -1.27, 1.45)), (0.9, 0.04, 1.0), M_PanelDark(), "Shuttle", yaw=yaw)
    for dz, h in ((0.0, 0.03), (1.0, 0.03)):
        box("DoorEdge", P((1.0, -1.29, 0.95 + dz)), (0.94, 0.01, h), M_Strip(), "Shuttle", yaw=yaw)
    box("DoorStep", P((1.0, -1.55, 0.88)), (0.9, 0.5, 0.06), M_PanelDark(), "Shuttle", yaw=yaw)
    box("NoseLamp", P((3.6, 0.0, 0.78)), (0.04, 0.7, 0.08), M_Strip(), "Shuttle", yaw=yaw)


# The stern cap behind the straight bay: the hull's clamshell door is a surface of revolution about a vertical axis
# at the bay's aft end (pawn x -6330, the floor at -1948), with this radius profile (metres above the bay floor ->
# metres), measured off the 1701-A mesh; the painted door spans |y| < 3.46 + (6.76 - z) * 0.754 of it, the
# flanking panels the rest.
CAP_PROFILE = [(-0.02, 10.00), (1.46, 9.90), (2.46, 9.63), (2.96, 9.28), (3.96, 8.92), (4.46, 8.49), (5.46, 8.02), (5.96, 7.51), (6.46, 6.96), (7.03, 6.50), (7.49, 2.80)]
def cap_radius(z):
    for (z0, r0), (z1, r1) in zip(CAP_PROFILE, CAP_PROFILE[1:]):
        if z <= z1: return r0 + (r1 - r0) * (z - z0) / (z1 - z0)
    return CAP_PROFILE[-1][1]
def door_edge_y(z): return 3.46 + (6.76 - z) * 0.754
JUNCTION = 1.55   # metres from the door's axis plane forward to the hull's stern wall (pawn x -6175): the bay's aft end

def bay_throat(R):
    """The throat between the straight bay and the clamshell door: the hull's own stern (its hood, bulkhead and
    side skin, with the game's stern wall closing the slot under the bulkhead) is the sides of it, since the game
    keeps the hull drawn in this room; this adds only a deck over the painted fan inside the door's base ring, the
    door posts and the lintel over the bay's own ceiling, all just forward of the stern wall so nothing of the set
    stands in the hull's recess where the quarter view would catch it. The door itself is the hull's own gores,
    placed by the game."""
    L, W, H = R.L, R.W, R.H
    HT = 7.0                                            # the throat's height: the door's top, over the bay's own ceiling
    A = (-L / 2, 0.0)                                   # the door's axis on the deck
    J = A[0] + JUNCTION                                 # the stern wall's plane: the bay's aft end
    seg = 40
    def arc(r, z, a0=90.0, a1=270.0, n=seg):
        return [(A[0] + r * math.cos(math.radians(a0 + (a1 - a0) * k / n)), A[1] + r * math.sin(math.radians(a0 + (a1 - a0) * k / n)), z) for k in range(n + 1)]
    # The deck: a half-disc inside the door's base ring (8.28 m), 2 cm proud of the hull's own fan, which carries on
    # outside it to the rim; the recess's roof above is the hull's.
    RD = 8.2
    deck = [(x, y) for x, y, _ in arc(RD, 0.0)] + [(J, -RD), (J, RD)]
    prism("ThroatDeck", deck, -0.12, 0.0, M_BayDeck(), "Floors")
    for side in (-1, 1):
        box("DoorPost", (J + 0.45, side * (W / 2 - 0.3), H / 2), (0.5, 0.6, H), M_PanelDark(), "Walls")
        for z in (2.0, 4.0):
            box("WarnLamp", (J + 0.71, side * (W / 2 - 0.3), z), (0.03, 0.12, 0.3), M_Warn(), "Lights")
    # The lintel closes the throat's extra height across its whole width above the bay's ceiling.
    zl = [H + (HT - H) * k / 4 for k in range(5)]
    lintel = [[(J + 0.2, -(cap_radius(z) - 0.10) + (2 * (cap_radius(z) - 0.10)) * k / 16, z) for k in range(17)] for z in zl]
    grid_surface("JunctionLintel", lintel, M_PanelGrey(), "Walls", toward=(J + 3.0, 0.0, H / 2))
    box("LintelStrip", (J + 0.23, 0, H - 0.3), (0.03, 2 * (cap_radius(H) - 0.4), 0.12), M_Strip(), "Lights")
    # Deck lights round the fan, and the approach line carried out to the door.
    for k in range(1, 8):
        a = 90.0 + 180.0 * k / 8
        r = RD - 0.6
        box("EdgeLamp", (A[0] + r * math.cos(math.radians(a)), A[1] + r * math.sin(math.radians(a)), 0.01), (0.2, 0.2, 0.02), M_Strip(), "Lights")
    box("RedLineAft", (A[0] - 4.5, 0, 0.004), (9.0, 0.5, 0.008), M_PaintRed(), "Floors")
    R.light((A[0] - 4.0, 0.0, HT - 0.5), 150.0, size=3.0)

def build_shuttle_bay():
    """The landing bay (The Final Frontier): a pale deck with the red approach line down the middle and the painted
    landing box, blue strip lights along the grey walls, the control gallery's windows high on the forward
    bulkhead, the open door frame astern, the shuttle on its skids on the box and cargo stacked to starboard.
    Lift door forward, starboard; a side door to port."""
    R = Room("bay", 26.0, 16.0, 5.4)   # the hull's side skin at the stern wall is 5.8 m over the deck at the bay's width (|y| 8.1 m), 6.3 m at 7.7 m
    L, W, H = R.L, R.W, R.H
    shell(R, M_PanelGrey(), M_BayDeck(), M_PanelDark(), open_aft=True, aft_trim=JUNCTION)
    bay_throat(R)
    # Wall bands: the blue strip at 4 m, a dark rail at 1.2 m, pilasters every 4 m.
    for s in (-1, 1):
        box("BlueStrip", (0.3 + JUNCTION / 2, s * (W / 2 - 0.02), 4.0), (L - 0.6 - JUNCTION, 0.04, 0.10), M_StripBlue(), "Lights")
        box("RailBand", (0.3 + JUNCTION / 2, s * (W / 2 - 0.03), 1.2), (L - 0.6 - JUNCTION, 0.06, 0.10), M_PanelDark(), "Walls")
        for x in (-10, -6, -2, 2, 6, 10):
            box("BayPilaster", (x, s * (W / 2 - 0.13), H / 2), (0.5, 0.26, H), M_PanelGrey(), "Walls")
    box("BlueStripFwd", (L / 2 - 0.02, 0, 4.0), (0.04, W - 0.6, 0.10), M_StripBlue(), "Lights")
    # The control gallery over the forward bulkhead: a dark bulge with three lit windows.
    box("Gallery", (L / 2 - 0.2, 0, 4.4), (0.4, 7.6, 1.6), M_PanelDark(), "Walls")
    for y in (-2.3, 0.0, 2.3):
        box("GalleryLight", (L / 2 - 0.36, y, 4.4), (0.02, 1.8, 1.0), M_Soft(), "Lights")
        box("GalleryGlass", (L / 2 - 0.42, y, 4.4), (0.02, 1.8, 1.0), M_Glass(), "Glass")
    wall_screen((L / 2 - 0.16, -4.5, 2.4), 2.4, 1.2, 0.0, "MasterSituation")
    # Deck markings: the red approach line, the landing box, blue deck lights, white edge lights.
    box("RedLine", (-4.5, 0, 0.004), (17.0, 0.5, 0.008), M_PaintRed(), "Floors")
    BX, BY, BW, BL = 1.5, 0.0, 5.2, 9.6
    for s in (-1, 1):
        box("BoxLine", (BX, BY + s * BW / 2, 0.004), (BL, 0.14, 0.008), M_PaintYellow(), "Floors")
        box("BoxLine", (BX + s * BL / 2, BY, 0.004), (0.14, BW, 0.008), M_PaintYellow(), "Floors")
    for x in range(-10, 8, 2):
        for s in (-1, 1):
            box("DeckLamp", (x, s * 3.6, 0.01), (0.25, 0.25, 0.02), M_StripBlue(), "Lights")
            box("EdgeLamp", (x, s * 6.8, 0.01), (0.2, 0.2, 0.02), M_Strip(), "Lights")
    # The side door to port with its chevrons on the deck, and the lift forward to starboard.
    lift_door(R, "-y", 8.0, mat_=M_PanelDark(), label="SideDoor")
    for k in range(3):
        box("Chevron", (8.0 - 0.6 + 0.6 * k, -W / 2 + 1.2, 0.004), (0.25, 1.4, 0.008), M_PaintYellow(), "Floors", yaw=40.0)
    lift_door(R, "+x", 5.0)
    # The shuttle on the box, and cargo stacked along the starboard wall.
    shuttle((BX, BY, 0.0))
    for k, x in enumerate((6.0, 8.3, 10.6)):
        box("Cargo", (x, W / 2 - 1.3, 1.0), (2.0, 2.0, 2.0), M_HullGrey() if k % 2 else M_PanelGrey(), "Cargo", bevel=0.04)
        box("CargoBand", (x, W / 2 - 1.3, 1.0), (2.02, 2.02, 0.2), M_PaintRed() if k == 1 else M_PanelDark(), "Cargo")
    box("Cargo", (7.15, W / 2 - 1.3, 3.0), (2.0, 2.0, 2.0), M_HullGrey(), "Cargo", bevel=0.04)
    box("WorkbeeBody", (-9.0, W / 2 - 1.6, 1.2), (2.4, 1.6, 1.4), M_PaintYellow(), "Cargo", bevel=0.2)
    box("WorkbeeGlass", (-7.9, W / 2 - 1.6, 1.3), (0.3, 1.2, 0.9), M_Black(), "Cargo", bevel=0.1)
    for s in (-1, 1):
        box("WorkbeeSkid", (-9.0, W / 2 - 1.6 + s * 0.6, 0.25), (2.0, 0.2, 0.5), M_PanelDark(), "Cargo")
    # Ceiling beams and lights.
    truss_ceiling(R, 4.0, depth=0.9, light_size=(2.2, 0.9), energy=120.0)
    return R

# ---- transporter room ------------------------------------------------------------------------------------------
def build_transporter():
    """The transporter room of the Enterprise-A: the round pad under its lit ceiling ring before a concave wall of
    warm lattice panels, the operator's curved console facing it behind glass screens, lockers along the port
    wall. Lift door starboard."""
    R = Room("trans", 8.0, 7.0, 3.0)
    L, W, H = R.L, R.W, R.H
    PX, PY = -1.5, 0.0
    shell(R, M_PanelGrey(), M_Carpet(), M_PanelGrey(), openings=[("+y", TRANS_DOOR_X - 13.2, 1.3, 2.3)])
    # The pad: a two-step round platform, its top a pale glowing disc with the six stations and their rim.
    lathe("PadBase", [(0.0, 0.0), (2.1, 0.0), (2.1, 0.1), (1.82, 0.1), (1.82, 0.24), (0.0, 0.24)], (PX, PY, 0), M_PanelGrey(), "Pad", segments=64)
    disc("PadTop", (PX, PY, 0.245), 1.72, M_Pad(), "Pad", segments=64)
    ring("PadRim", (PX, PY, 0), 1.72, 1.80, 0.24, 0.26, M_Strip(), "Lights", segments=64)
    for k in range(6):
        c, s = ang(60 * k)
        ring("PadStation", (PX + 1.1 * c, PY + 1.1 * s, 0), 0.30, 0.36, 0.245, 0.255, M_Strip(), "Lights", segments=24)
    ring("CeilRing", (PX, PY, 0), 1.25, 1.9, H - 0.05, H - 0.01, M_Soft(), "Lights", segments=64)
    disc("CeilDisc", (PX, PY, H - 0.03), 1.2, M_PanelDark(), "Ceiling", up=(0, 0, -1), segments=48)
    ring("CeilLamp", (PX, PY, 0), 0.0, 0.3, H - 0.06, H - 0.03, M_Pad(), "Lights", segments=24)
    R.light((PX, PY, H - 0.12), 60.0, color=(0.8, 0.88, 1.0), size=1.4)
    # The lattice wall: an arc of lit panels behind the pad, mullions between, sill and lintel.
    A0, A1, RL = 112.0, 248.0, 2.5
    lathe("Lattice", [(RL, 0.32), (RL, 2.55)], (PX, PY, 0), M_Lattice(), "Lights", a0=A0, a1=A1, segments=34, inside=(PX, PY, 1.4))
    n = 7
    for k in range(n + 1):
        a = A0 + (A1 - A0) * k / n; c, s = ang(a)
        box("Mullion", (PX + RL * c, PY + RL * s, 1.43), (0.12, 0.10, 2.3), M_Metal(), "Walls", yaw=a)
    # The lattice itself: thin dark muntins over each lit panel, two uprights and a ladder of cross bars.
    span = (A1 - A0) / n
    for k in range(n):
        for f in (1.0 / 3.0, 2.0 / 3.0):
            a = A0 + span * (k + f); c, s = ang(a)
            box("Muntin", (PX + (RL - 0.03) * c, PY + (RL - 0.03) * s, 1.43), (0.04, 0.035, 2.2), M_Metal(), "Walls", yaw=a)
        for z in (0.62, 0.95, 1.28, 1.61, 1.94, 2.27):
            ring("MuntinBar", (PX, PY, 0), RL - 0.05, RL - 0.01, z - 0.015, z + 0.015, M_Metal(), "Walls", a0=A0 + span * k, a1=A0 + span * (k + 1), segments=6)
    ring("LatticeSill", (PX, PY, 0), RL - 0.06, RL + 0.14, 0.16, 0.32, M_PanelGrey(), "Walls", a0=A0, a1=A1, segments=34)
    ring("LatticeLintel", (PX, PY, 0), RL - 0.06, RL + 0.14, 2.55, 2.75, M_PanelGrey(), "Walls", a0=A0, a1=A1, segments=34)
    ring("LatticeHead", (PX, PY, 0), RL - 0.4, RL + 0.14, 2.75, H, M_PanelGrey(), "Walls", a0=A0, a1=A1, segments=34)
    for s in (-1, 1):
        c, sn = ang(A0 if s > 0 else A1)
        ex, ey = PX + RL * c, PY + RL * sn
        box("Return", (ex, (ey + s * W / 2) / 2, H / 2), (0.14, abs(W / 2 - abs(ey)), H), M_PanelGrey(), "Walls")
    R.light((PX - 1.2, PY, 1.5), 40.0, color=(1.0, 0.9, 0.75), size=1.6)
    # The operator's console: a curved desk on the pad's axis, two raked screens, touch strips, glass screens.
    RD = 4.0
    lathe("OpDesk", [(RD - 0.4, 0.0), (RD + 0.35, 0.0), (RD + 0.35, 0.80), (RD - 0.4, 0.80)], (PX, PY, 0), M_Black(), "Consoles", a0=-24.0, a1=24.0, closed=True, segments=16)
    ring("OpLip", (PX, PY, 0), RD - 0.44, RD - 0.36, 0.78, 0.83, M_PanelGrey(), "Consoles", a0=-24.0, a1=24.0, segments=16)
    ring("OpTouch", (PX, PY, 0), RD + 0.02, RD + 0.30, 0.80, 0.812, M_Buttons(), "Consoles", a0=-21.0, a1=21.0, segments=16)
    for b in (-10.0, 10.0):
        c, s = ang(b)
        hx, hy = PX + (RD - 0.18) * c, PY + (RD - 0.18) * s
        box("OpHood", (hx, hy, 1.02), (0.06, 1.1, 0.46), M_Black(), "Consoles", yaw=b + 180.0, pitch=32.0, bevel=0.01)
        screen("Scr", (hx + 0.04 * c, hy + 0.04 * s, 1.02), 1.0, 0.36, M_Screen("Engineering" if b < 0 else "Science"), "Screens", yaw=b + 180.0, pitch=32.0)
    for s in (-1, 1):
        box("Glass", (2.3, s * 1.95, 1.2), (0.03, 1.3, 2.3), M_Glass(), "Glass")
        for z in (0.9, 1.25, 1.6):
            box("FrostBand", (2.3, s * 1.95, z), (0.04, 1.3, 0.07), M_PanelGrey(), "Glass")
        for y in (1.3, 2.6):
            box("GlassPost", (2.3, s * y, 1.2), (0.08, 0.08, 2.4), M_Metal(), "Rails")
        box("GlassRail", (2.3, s * 1.95, 2.42), (0.08, 1.3, 0.08), M_Metal(), "Rails")
    R.light((2.5, 0.0, H - 0.1), 25.0, size=0.8)
    # Lockers along the port wall, the lift door to starboard.
    for x in (0.4, 1.2, 2.0, 2.8):
        box("Locker", (x, -W / 2 + 0.25, 1.05), (0.7, 0.5, 2.1), M_PanelDark(), "Furniture", bevel=0.01)
        box("LockerSeam", (x, -W / 2 + 0.51, 1.05), (0.02, 0.01, 1.9), M_PanelGrey(), "Furniture")
    pilasters(R, 2.0, walls=("+y", "+x"), skip=(("+y", TRANS_DOOR_X - 13.2),))
    doorway('x', W / 2 + 0.06, TRANS_DOOR_X - 13.2, -1, label="CorridorDoor")   # onto deck G's corridor; the lift is along it
    radial_ceiling(R, (PX, PY), 1.9, ribs=8, energy=14.0, panels=[(2.5, -1.8), (2.5, 1.8), (0.6, -2.6), (0.6, 2.6)])
    return R

# ---- airlock staging area --------------------------------------------------------------------------------------
def build_airlock():
    """The airlock's staging area under the saucer hatch (The Motion Picture): four suit lockers down the port wall
    with the suits standing in them behind glass, a bench and the arms cabinet to starboard, thruster packs racked
    by the lift, and the round inner hatch in the forward bulkhead with its lit ring and control panel. Lift door
    aft, in the middle."""
    R = Room("lock", 6.0, 5.0, 3.0)
    L, W, H = R.L, R.W, R.H
    shell(R, M_PanelGrey(), M_DeckPlate(), M_PanelGrey())
    # The hatch: a dark ring set into the bulkhead, the grey door disc inside it, a lit ring round the door, the
    # cross-brace on its face, a red lamp above and the panel beside it.
    HX, HZ = L / 2 - 0.02, 1.25
    ring("HatchRing", (HX, 0.0, HZ), 0.86, 1.02, -0.18, 0.0, M_Metal(), "Walls", axis='x', segments=48)
    ring("HatchGlow", (HX - 0.17, 0.0, HZ), 0.80, 0.86, -0.02, 0.02, M_Strip(), "Lights", axis='x', segments=48)
    lathe("HatchDoor", [(0.0, -0.14), (0.80, -0.14), (0.80, -0.06), (0.0, -0.06)], (HX, 0.0, HZ), M_HullGrey(), "Walls", axis='x', closed=True, segments=48)
    box("HatchBrace", (HX - 0.16, 0.0, HZ), (0.04, 0.34, 0.08), M_PanelDark(), "Walls")
    box("HatchBrace", (HX - 0.16, 0.0, HZ), (0.04, 0.08, 0.34), M_PanelDark(), "Walls")
    box("HatchLamp", (HX - 0.03, 0.0, HZ + 1.15), (0.05, 0.24, 0.08), M_Warn(), "Lights")
    box("HatchPanel", (HX - 0.03, 1.45, 1.35), (0.06, 0.4, 0.32), M_Black(), "Consoles", bevel=0.01)
    box("HatchKeys", (HX - 0.065, 1.45, 1.35), (0.01, 0.32, 0.24), M_Buttons(), "Lights")
    R.light((HX - 0.6, 0.0, HZ), 10.0, size=0.4, kind='POINT')
    # Suit lockers: an open case each with the suit inside, glass across the front.
    for k, x in enumerate((-1.8, -0.9, 0.0, 0.9)):
        y = -W / 2 + 0.32
        box("LockerBack", (x, -W / 2 + 0.03, 1.15), (0.84, 0.06, 2.3), M_PanelLight(), "Furniture")
        for s_ in (-1, 1):
            box("LockerSide", (x + s_ * 0.4, y, 1.15), (0.04, 0.6, 2.3), M_PanelLight(), "Furniture")
        box("LockerTop", (x, y, 2.28), (0.84, 0.6, 0.04), M_PanelLight(), "Furniture")
        box("LockerFoot", (x, y, 0.09), (0.84, 0.6, 0.18), M_PanelLight(), "Furniture")
        box("LockerLamp", (x, -W / 2 + 0.07, 2.2), (0.6, 0.02, 0.06), M_Soft(), "Lights")
        # The suit: boots, legs, torso, the pack behind, the helmet on top.
        for s_ in (-1, 1):
            box("SuitLeg", (x + s_ * 0.12, y + 0.02, 0.62), (0.17, 0.24, 0.86), M_HullWhite(), "Furniture", bevel=0.04)
            box("SuitBoot", (x + s_ * 0.12, y + 0.06, 0.26), (0.19, 0.32, 0.16), M_PanelDark(), "Furniture", bevel=0.03)
            box("SuitArm", (x + s_ * 0.31, y + 0.02, 1.3), (0.13, 0.2, 0.72), M_HullWhite(), "Furniture", bevel=0.04)
        box("SuitTorso", (x, y + 0.02, 1.38), (0.46, 0.3, 0.72), M_HullWhite(), "Furniture", bevel=0.06)
        box("SuitChest", (x, y + 0.18, 1.45), (0.3, 0.03, 0.2), M_Buttons(), "Lights")
        box("SuitPack", (x, y - 0.18, 1.35), (0.4, 0.14, 0.6), M_PanelDark(), "Furniture", bevel=0.03)
        lathe("Helmet", [(0.0, -0.18), (0.17, -0.1), (0.2, 0.05), (0.14, 0.17), (0.0, 0.2)], (x, y + 0.02, 1.9), M_HullWhite(), "Furniture", segments=20)
        box("Visor", (x, y + 0.14, 1.9), (0.2, 0.12, 0.14), M_Black(), "Furniture", bevel=0.04)
        box("LockerGlass", (x, -W / 2 + 0.6, 1.2), (0.76, 0.02, 2.0), M_Glass(), "Glass")
        R.light((x, -W / 2 + 0.3, 2.1), 5.0, size=0.3)
    # The bench, the arms cabinet with its keypad, and two thruster packs racked by the lift.
    box("Bench", (-0.9, W / 2 - 0.27, 0.45), (2.2, 0.46, 0.06), M_PanelDark(), "Furniture", bevel=0.01)
    for x in (-1.9, 0.1):
        box("BenchLeg", (x, W / 2 - 0.27, 0.21), (0.08, 0.4, 0.42), M_Metal(), "Furniture")
    box("ArmsCabinet", (1.6, W / 2 - 0.22, 1.2), (1.2, 0.44, 1.7), M_PanelDark(), "Furniture", bevel=0.01)
    box("ArmsSeam", (1.6, W / 2 - 0.45, 1.2), (0.02, 0.01, 1.5), M_PanelGrey(), "Furniture")
    box("ArmsKeys", (1.6, W / 2 - 0.455, 1.5), (0.3, 0.01, 0.12), M_Buttons(), "Lights")
    box("ArmsLabel", (1.6, W / 2 - 0.455, 1.95), (0.6, 0.01, 0.08), M_Strip(), "Lights")
    for y in (-1.5, 1.5):
        box("PackRack", (-L / 2 + 0.2, y, 1.3), (0.3, 0.7, 0.9), M_PanelDark(), "Furniture", bevel=0.02)
        box("Pack", (-L / 2 + 0.38, y, 1.3), (0.2, 0.5, 0.7), M_HullGrey(), "Furniture", bevel=0.04)
        for s_ in (-1, 1):
            box("PackNozzle", (-L / 2 + 0.5, y + s_ * 0.18, 1.0), (0.08, 0.08, 0.1), M_Core(), "Lights")
    pilasters(R, 2.0, walls=("-x", "+x"), skip=(("-x", 0.0), ("+x", 0.0)))
    lift_door(R, "-x", 0.0)
    cove_ceiling(R, drop=0.28, width=0.7, panels=[(-1.0, 0.0), (1.0, 0.0)], energy=12.0)
    return R

# ---- the crew's places (interiors round) -----------------------------------------------------------------------
def M_CarpetGrey(): return mat("CarpetGrey", (0.30, 0.31, 0.34), 0.95)
def M_CarpetRed(): return mat("CarpetRed", (0.30, 0.06, 0.06), 0.95)
def M_Wood(): return mat("Wood", (0.36, 0.22, 0.12), 0.5)
def M_Linen(): return mat("Linen", (0.78, 0.76, 0.70), 0.9)
def M_Brass(): return mat("Brass", (0.75, 0.6, 0.3), 0.35, 0.8)
def M_Torpedo(): return mat("TorpedoBlack", (0.03, 0.03, 0.035), 0.4, 0.3)
def M_Grating(): return mat("Grating", (0.18, 0.19, 0.21), 0.6, 0.5)
def M_Plant(): return mat("Plant", (0.12, 0.35, 0.14), 0.8)

def corridor_leg(R, x0, x1, y0, y1, walls=("-x", "+x", "-y", "+y"), doors=None, anchor=None, pitch=2.24, lights=True, gap=None):
    """One straight run of the refit's corridor between (x0,y0) and (x1,y1): the section above, built in bays of
    `pitch` between frames (ribs following the section), each bay a lit lower panel under grey and pale panels, or
    a door bay (a straight wall with the door set in it: `doors` maps a side wall to [(centre, kind)] with kind
    "open" for a doorway onto a room, its opening cut, or "shut" for leaves). `anchor` is a bay boundary along the
    run; `gap` = (a0, a1, side) leaves that stretch of a side wall out (the junction with another leg). The end
    walls across the run are flat (the caller sets a lift door or a panel in them)."""
    doors = doors or {}
    along_y = (y1 - y0) >= (x1 - x0)
    a0, a1 = (y0, y1) if along_y else (x0, x1)
    cc = (x0 + x1) / 2 if along_y else (y0 + y1) / 2          # the centre line's cross coordinate
    F, W, Cw, Z1, Z2, H, T = CORR_F, CORR_W, CORR_C, CORR_Z1, CORR_Z2, CORR_H, 0.12
    mid = (a0 + a1) / 2
    def pt(a, u, z): return (cc + u, a, z) if along_y else (a, cc + u, z)
    def sz(la, th, hz): return (th, la, hz) if along_y else (la, th, hz)
    def cant(name, a_mid, la, u_lo, z_lo, u_hi, z_hi, mat_, group, thick=0.04, s=1):
        """A panel leaning from (u_lo, z_lo) to (u_hi, z_hi) across the section (u outward along side s)."""
        ln = math.hypot(u_hi - u_lo, z_hi - z_lo); th = math.degrees(math.atan2(u_hi - u_lo, z_hi - z_lo))
        um, zm = (u_lo + u_hi) / 2, (z_lo + z_hi) / 2
        if along_y: box(name, pt(a_mid, s * um, zm), sz(la, thick, ln), mat_, group, pitch=s * th)
        else: box(name, pt(a_mid, s * um, zm), sz(la, thick, ln), mat_, group, roll=-s * th)
    # Floor and ceiling the length of the run.
    box("Floor", pt(mid, 0, -T / 2), sz(a1 - a0, 2 * W + 2 * T, T), M_Grating(), "Floors")
    box("Ceiling", pt(mid, 0, H + T / 2), sz(a1 - a0, 2 * Cw, T), M_PanelDark(), "Ceiling")
    # Bays.
    anchor = a0 if anchor is None else anchor
    k0 = math.floor((a0 - anchor) / pitch + 1e-6); k1 = math.ceil((a1 - anchor) / pitch - 1e-6)
    bounds = [max(a0, min(a1, anchor + k * pitch)) for k in range(k0, k1 + 1)]
    bounds = [b for i, b in enumerate(bounds) if i == 0 or b - bounds[i - 1] > 0.05]
    for s, wall in ((-1, "-x" if along_y else "-y"), (1, "+x" if along_y else "+y")):
        if wall not in walls: continue
        wall_doors = doors.get(wall, [])
        for i in range(len(bounds) - 1):
            b0, b1 = bounds[i], bounds[i + 1]; bm = (b0 + b1) / 2; la = b1 - b0 - 0.16
            if gap and gap[2] == wall and b0 >= gap[0] - 0.05 and b1 <= gap[1] + 0.05: continue
            door = next(((c, k) for (c, k) in wall_doors if b0 <= c <= b1), None)
            if door is None:
                # A wall bay: the lit lower panel in its grey surround, the straight band, the pale upper panel.
                cant("LowerFrame", bm, la, F, 0.0, W, Z1, M_PanelGrey(), "Walls", s=s)
                cant("LowerLit", bm, la - 0.24, F - 0.035, 0.12, W - 0.035, Z1 - 0.12, M_Soft(), "Lights", thick=0.02, s=s)
                box("Band", pt(bm, s * (W + 0.02), (Z1 + Z2) / 2), sz(la, 0.04, Z2 - Z1), M_PanelGrey(), "Walls")
                box("BandRail", pt(bm, s * (W - 0.01), Z1 + 0.08), sz(la, 0.03, 0.05), M_PanelDark(), "Walls")
                cant("Upper", bm, la, W, Z2, Cw, H, M_PanelLight(), "Walls", s=s)
            else:
                # A door bay: a straight wall at the waist the full height, the door (open or shut) set in it, the
                # floor and ceiling filled out to it.
                c, kind = door
                cuts = [(c, 1.3, 2.3)] if kind == "open" else []
                wall_run("DoorWall", 'y' if along_y else 'x', cc + s * (W + T / 2), b0, b1, 0, H, T, M_PanelLight(), "Walls", cuts)
                box("DoorFloor", pt(bm, s * (F + W) / 2, -0.005), sz(b1 - b0, W - F, 0.01), M_Grating(), "Floors")
                box("DoorCeiling", pt(bm, s * (Cw + W) / 2, H - 0.02), sz(b1 - b0, W - Cw + 0.02, 0.04), M_PanelDark(), "Ceiling")
                doorway('y' if along_y else 'x', cc + s * W, c, -s, depth=0.14 if kind == "shut" else 0.24, leaves=M_Burgundy() if kind == "shut" else None, label="CorrDoor")
        # The frames between bays: ribs following the section, proud of the panels.
        for i, b in enumerate(bounds):
            if b <= a0 + 0.05 or b >= a1 - 0.05: continue
            if gap and gap[2] == wall and gap[0] - 0.05 < b < gap[1] + 0.05: continue
            cant("Rib", b, 0.16, F - 0.06, 0.0, W - 0.06, Z1, M_PanelGrey(), "Walls", thick=0.12, s=s)
            box("Rib", pt(b, s * (W - 0.03), (Z1 + Z2) / 2), sz(0.16, 0.12, Z2 - Z1), M_PanelGrey(), "Walls")
            cant("Rib", b, 0.16, W - 0.06, Z2, Cw - 0.06, H, M_PanelGrey(), "Walls", thick=0.12, s=s)
    # Ceiling beams across at the frames, a light in each bay.
    for i, b in enumerate(bounds):
        if a0 + 0.05 < b < a1 - 0.05: box("CeilBeam", pt(b, 0, H - 0.06), sz(0.16, 2 * Cw - 0.1, 0.12), M_PanelGrey(), "Ceiling")
        if i < len(bounds) - 1 and lights:
            bm = (b + bounds[i + 1]) / 2
            box("CeilLight", pt(bm, 0, H - 0.015), sz(0.6, 0.25, 0.03), M_Strip(), "Lights")
            R.light(pt(bm, 0, H - 0.1), 10.0, size=0.6)
            R.light(pt(bm, 0, 0.6), 14.0, size=1.6)
    # The end walls across the run (flat; the caller sets what is in them).
    for s, wall in ((-1, "-y" if along_y else "-x"), (1, "+y" if along_y else "+x")):
        if wall not in walls: continue
        a = a0 if s < 0 else a1
        box("EndWall", pt(a + s * T / 2, 0, H / 2), sz(T, 2 * W + 2 * T, H), M_PanelLight(), "Walls")

def torpedo(pos, yaw=0.0):
    """A photon torpedo casing: the black elongated case with its red band, the white nose and the glow aft."""
    cx, cy, cz = pos; d = ang(yaw)
    box("Casing", (cx, cy, cz + 0.25), (2.1, 0.76, 0.5), M_Torpedo(), "Torpedoes", yaw=yaw, bevel=0.12)
    box("CasingBand", (cx - d[0] * 0.5, cy - d[1] * 0.5, cz + 0.25), (0.12, 0.78, 0.52), M_PaintRed(), "Torpedoes", yaw=yaw, bevel=0.1)
    box("Nose", (cx + d[0] * 1.0, cy + d[1] * 1.0, cz + 0.25), (0.3, 0.5, 0.34), M_HullWhite(), "Torpedoes", yaw=yaw, bevel=0.12)
    box("CasingGlow", (cx - d[0] * 1.06, cy - d[1] * 1.06, cz + 0.25), (0.02, 0.4, 0.2), M_Core(), "Torpedoes", yaw=yaw)

def lounge_chair(pos, yaw, mat_=None):
    """A low white lounge chair with a raked back and arms, facing bearing `yaw`."""
    x, y, z = pos; d = ang(yaw); mat_ = mat_ or M_PanelLight()
    box("LoungeSeat", (x, y, z + 0.22), (0.8, 0.8, 0.44), mat_, "Furniture", yaw=yaw, bevel=0.06)
    box("LoungeBack", (x - d[0] * 0.33, y - d[1] * 0.33, z + 0.72), (0.14, 0.8, 0.7), mat_, "Furniture", yaw=yaw, pitch=-14.0, bevel=0.05)
    for s in (-1, 1):
        box("LoungeArm", (x + d[1] * s * 0.45, y - d[0] * s * 0.45, z + 0.5), (0.8, 0.1, 0.3), mat_, "Furniture", yaw=yaw, bevel=0.04)

def build_corridor():
    """Deck G's corridor (the refit's, The Motion Picture to The Undiscovered Country): an L of the canted section
    in bays between frames, the lit lower panels, the grated deck. The athwartships leg has sickbay's doorway to
    starboard and the rec deck's to port, the lift in its forward end wall, shut doors between; the forward leg
    along the port side has the transporter room's doorway and ends at the lit deck-configuration panel over its
    console. Origin: the floor at the athwartships leg's centre line, abreast of the corridor's forward end."""
    R = Room("corr", 2 * CORR_W, CORR_A_Y1 - CORR_A_Y0, CORR_H)
    H = CORR_H
    A = CORR_A_ANCHOR; P = CORR_A_PITCH
    corridor_leg(R, -CORR_W, CORR_W, CORR_A_Y0, CORR_A_Y1, walls=("-x", "+x", "+y"), anchor=A, pitch=P,
                 doors={"+x": [(SICK_DOOR_Y, "open"), (A + 0.5 * P, "shut")], "-x": [(REC_DOOR_Y, "open"), (A + 4.5 * P, "shut")]},
                 gap=(CORR_B_Y0, CORR_B_Y1, "+x"))
    PB = CORR_B_PITCH
    corridor_leg(R, CORR_B_X0, CORR_B_X1, CORR_B_Y0, CORR_B_Y1, walls=("-y", "+y", "+x"), anchor=CORR_B_X0, pitch=PB,
                 doors={"-y": [(TRANS_DOOR_X, "open"), (CORR_B_X0 + 1.5 * PB, "shut")], "+y": [(CORR_B_X0 + 3.5 * PB, "shut")]})
    # The outer corner: the forward leg's port wall line carried across the athwartships leg's end, and a post
    # where the two canted walls meet at the inner corner.
    box("EndWall", (0.0, CORR_B_Y0 - 0.06, H / 2), (2 * CORR_W + 0.24, 0.12, H), M_PanelLight(), "Walls")
    box("CornerPost", (CORR_W + 0.02, CORR_B_Y1 - 0.02, H / 2), (0.36, 0.36, H), M_PanelGrey(), "Walls")
    # The lift in the forward end wall, the deck-configuration panel and its console shelf at the forward leg's end.
    doorway('x', CORR_A_Y1, 0.0, -1, depth=0.14, leaves=M_Burgundy(), label="Lift")
    box("LiftLabel", (0.0, CORR_A_Y1 - 0.08, 2.42), (0.6, 0.02, 0.08), M_Strip(), "Lights")
    ym = (CORR_B_Y0 + CORR_B_Y1) / 2
    wall_screen((CORR_B_X1 - 0.16, ym, 1.55), 1.5, 0.95, 0.0, "DeckConfig")
    box("PanelShelf", (CORR_B_X1 - 0.3, ym, 0.95), (0.5, 1.6, 0.08), M_PanelDark(), "Furniture")
    box("PanelKeys", (CORR_B_X1 - 0.32, ym, 0.995), (0.36, 1.2, 0.012), M_Buttons(), "Lights")
    R.light((CORR_B_X1 - 0.8, ym, 1.6), 10.0, color=(0.6, 0.75, 1.0), size=0.6)
    return R

def build_rec_deck():
    """The rec deck (The Motion Picture): a tall hall two decks high with the great screen on its aft wall, lit
    columns between ribbed panels down both sides, a gallery round three sides on posts with its rails, a stair up
    to it, tables and stools on the floor, the galley's dispensers along the port wall by the entrance. The
    doorway from deck G's corridor is in the forward wall, to port of centre."""
    R = Room("rec", 18.0, 14.0, 6.0)
    L, W, H = R.L, R.W, R.H
    shell(R, M_PanelLight(), M_CarpetGrey(), M_PanelDark(), openings=[("+x", REC_DOOR_Y + 2.0, 1.3, 2.3)])
    doorway('y', L / 2 + 0.06, REC_DOOR_Y + 2.0, -1, label="CorridorDoor")
    # The screen aft, its sill lit; the viewer stands before it (the Use spot).
    wall_screen((-L / 2 + 0.16, 0.0, 3.0), 9.0, 4.0, 180.0, "Navigation")
    box("ScreenSill", (-L / 2 + 0.35, 0, 0.9), (0.5, 9.6, 0.1), M_PanelDark(), "Furniture")
    box("SillLamp", (-L / 2 + 0.6, 0, 0.86), (0.04, 9.4, 0.05), M_Soft(), "Lights")
    R.light((-L / 2 + 1.5, 0, 3.0), 80.0, color=(0.6, 0.75, 1.0), size=3.0)
    # The side walls: lit columns every three metres, ribbed panels between.
    for s in (-1, 1):
        for x in (-7, -4, -1, 2, 5):
            box("Column", (x, s * (W / 2 - 0.2), H / 2), (0.6, 0.4, H), M_PanelLight(), "Walls")
            box("ColumnLight", (x, s * (W / 2 - 0.41), 1.6), (0.3, 0.02, 2.4), M_Soft(), "Lights")
            box("ColumnLight", (x, s * (W / 2 - 0.41), 4.6), (0.3, 0.02, 2.0), M_Soft(), "Lights")
        for x in (-5.5, -2.5, 0.5, 3.5, 6.5):
            for z in (1.2, 1.7, 2.2):
                box("Rib", (x, s * (W / 2 - 0.03), z), (2.2, 0.06, 0.08), M_PanelGrey(), "Walls")
    # The gallery round the sides and over the entrance, its fascia lit blue, rails at the knee and the hand; the
    # starboard rail opens where the stair's landing meets the gallery.
    GZ, GD = 3.0, 2.2
    LANDING_X = 2.8
    def rails(cx, cy, length, along_x, gap=None):
        """Knee and hand rails with posts every two metres; `gap` = (a0, a1) along the run leaves an opening (the
        stair landing's exit onto the gallery), rails split round it and no post in it."""
        runs = [(-length / 2, length / 2)] if gap is None else [(-length / 2, gap[0]), (gap[1], length / 2)]
        for (a0, a1) in runs:
            am, la = (a0 + a1) / 2, a1 - a0
            for z in (0.45, 1.0):
                box("GalleryRail", (cx + (am if along_x else 0), cy + (0 if along_x else am), GZ + z), (la, 0.06, 0.06) if along_x else (0.06, la, 0.06), M_Metal(), "Rails")
        n = int(length // 2)
        for k in range(n + 1):
            t = -length / 2 + length * k / max(n, 1)
            if gap is not None and gap[0] - 0.03 < t < gap[1] + 0.03: continue
            box("RailPost", (cx + (t if along_x else 0), cy + (0 if along_x else t), GZ + 0.5), (0.06, 0.06, 1.0), M_Metal(), "Rails")
    for s in (-1, 1):
        box("Gallery", (0.0, s * (W / 2 - GD / 2), GZ - 0.1), (L, GD, 0.2), M_PanelGrey(), "Floors")
        box("GalleryFascia", (0, s * (W / 2 - GD - 0.02), GZ - 0.3), (L, 0.04, 0.6), M_PanelDark(), "Walls")
        box("FasciaStrip", (0, s * (W / 2 - GD - 0.05), GZ - 0.55), (L - 0.4, 0.02, 0.06), M_StripBlue(), "Lights")
        rails(0.0, s * (W / 2 - GD + 0.05), L - 0.4, True, gap=(LANDING_X - 0.5, LANDING_X + 0.5) if s > 0 else None)
        for x in (-6, -2, 2, 6):
            box("GalleryPost", (x, s * (W / 2 - GD + 0.15), GZ / 2 - 0.1), (0.2, 0.2, GZ - 0.2), M_PanelGrey(), "Walls")
            R.light((x, s * (W / 2 - 1.1), GZ - 0.4), 25.0, size=1.0)
    box("Gallery", (L / 2 - GD / 2, 0, GZ - 0.1), (GD, W - 2 * GD, 0.2), M_PanelGrey(), "Floors")
    box("GalleryFascia", (L / 2 - GD - 0.02, 0, GZ - 0.3), (0.04, W - 2 * GD, 0.6), M_PanelDark(), "Walls")
    rails(L / 2 - GD + 0.05, 0.0, W - 2 * GD - 0.4, False)
    # The stair: a solid flight along the starboard side up to the gallery's edge, with a landing and a rail.
    SY = 4.0
    for i in range(15):
        box("Step", (-2.0 + 0.3 * i + 0.15, SY, 0.1 * (i + 1)), (0.3, 1.2, 0.2 * (i + 1)), M_PanelGrey(), "Floors")
    box("Landing", (LANDING_X, 4.4, 2.9), (0.6, 2.0, 0.2), M_PanelGrey(), "Floors")
    box("StairRail", (0.25, SY - 0.6, 2.35), (5.4, 0.06, 0.06), M_Metal(), "Rails", pitch=-math.degrees(math.atan2(3.0, 4.5)))
    box("StairRail", (0.25, SY - 0.6, 1.75), (5.4, 0.06, 0.06), M_Metal(), "Rails", pitch=-math.degrees(math.atan2(3.0, 4.5)))
    for i in range(0, 15, 3):
        box("StairPost", (-2.0 + 0.3 * i + 0.15, SY - 0.6, 0.2 * (i + 1) + 0.5), (0.06, 0.06, 1.0), M_Metal(), "Rails")
    # Tables and stools on the floor, the galley's dispensers and counter to port by the door.
    for (tx, ty) in ((-4.5, -3.0), (-4.5, 0.6), (-0.5, -3.5), (-0.5, 1.5), (3.5, -2.5), (3.5, 1.0)):
        lathe("Table", [(0.0, 0.0), (0.08, 0.0), (0.08, 0.7), (0.55, 0.7), (0.55, 0.75), (0.0, 0.75)], (tx, ty, 0), M_PanelLight(), "Furniture", closed=True, segments=24)
        for k in range(4):
            c, s = ang(45 + 90 * k)
            lathe("Stool", [(0.0, 0.0), (0.06, 0.0), (0.06, 0.42), (0.2, 0.42), (0.2, 0.48), (0.0, 0.48)], (tx + 0.85 * c, ty + 0.85 * s, 0), M_Seat(), "Furniture", closed=True, segments=16)
    for k in range(5):
        x = 6.5 - k * 0.9
        box("Dispenser", (x, -W / 2 + 0.25, 1.2), (0.8, 0.46, 1.8), M_PanelGrey(), "Furniture", bevel=0.01)
        box("DispenserHatch", (x, -W / 2 + 0.49, 1.35), (0.5, 0.02, 0.5), M_Buttons(), "Lights")
    box("Counter", (4.7, -W / 2 + 0.6, 0.9), (4.8, 0.7, 0.08), M_Black(), "Furniture")
    # The ceiling's light troughs.
    for x in (-6, -2, 2, 6):
        box("Trough", (x, 0, H - 0.05), (0.6, W - 1.0, 0.1), M_Soft(), "Lights")
        R.light((x, 0, H - 0.3), 300.0, size=3.0)
    return R

def build_mess():
    """The officers' mess (The Undiscovered Country's dinner): the long dark table and its chairs under round
    ceiling lamps, slatted wall panels between lit strips, the sideboard to port, the display case of plates and
    the ship's model lit behind glass on the forward wall. Lift door aft, in the middle."""
    R = Room("mess", 11.0, 7.0, 2.8)
    L, W, H = R.L, R.W, R.H
    shell(R, M_PanelGrey(), M_Carpet(), M_PanelDark())
    def slats(axis, pos, a0, a1, sgn, strips=True):
        n = int((a1 - a0) // 1.8); start = a0 + ((a1 - a0) - n * 1.8) / 2 + 0.9
        for k in range(n):
            c = start + k * 1.8
            for z in (0.55, 0.8, 1.05, 1.3, 1.55, 1.8, 2.05, 2.3):
                if axis == 'x': box("Slat", (c, pos - sgn * 0.03, z), (1.5, 0.06, 0.1), M_PanelLight(), "Walls")
                else: box("Slat", (pos - sgn * 0.03, c, z), (0.06, 1.5, 0.1), M_PanelLight(), "Walls")
            if strips and k < n - 1:
                e = c + 0.9
                if axis == 'x': box("WallStrip", (e, pos - sgn * 0.03, 1.45), (0.06, 0.03, 2.0), M_Strip(), "Lights")
                else: box("WallStrip", (pos - sgn * 0.03, e, 1.45), (0.03, 0.06, 2.0), M_Strip(), "Lights")
    slats('x', W / 2, -L / 2 + 0.3, L / 2 - 0.3, 1)
    slats('x', -W / 2, -L / 2 + 0.3, L / 2 - 0.3, -1)
    slats('y', -L / 2, -W / 2 + 0.3, -1.0, -1); slats('y', -L / 2, 1.0, W / 2 - 0.3, -1)
    # The table and twelve chairs; plates and glasses down it.
    box("TableTop", (0.3, 0, 0.74), (6.6, 1.3, 0.06), M_Black(), "Furniture", bevel=0.01)
    box("TableLip", (0.3, 0, 0.705), (6.6, 1.3, 0.02), M_Metal(), "Furniture")
    box("TableBase", (0.3, 0, 0.36), (5.6, 0.6, 0.72), M_PanelDark(), "Furniture")
    for k in range(5):
        x = 0.3 - 2.4 + 1.2 * k
        chair((x, -1.15, 0), 90.0); chair((x, 1.15, 0), -90.0)
        for s in (-1, 1):
            disc("Plate", (x, s * 0.42, 0.78), 0.16, M_PanelLight(), "Furniture", segments=24)
            tube("Glass", (x + 0.3, s * 0.3, 0), 0.04, 0.77, 0.9, M_Glass(), "Glass", segments=12)
    chair((0.3 + 3.9, 0, 0), 180.0); chair((0.3 - 3.9, 0, 0), 0.0)
    for s in (-1, 1): lathe("Centrepiece", [(0.0, 0.0), (0.12, 0.0), (0.18, 0.2), (0.12, 0.3), (0.0, 0.3)], (0.3 + s * 1.5, 0, 0.77), M_Brass(), "Furniture", segments=16)
    # Round ceiling lamps on their stems, warm.
    for (x, y) in ((-3.0, -1.4), (-3.0, 1.4), (0.3, -1.4), (0.3, 1.4), (3.6, -1.4), (3.6, 1.4)):
        lathe("Lamp", [(0.0, 0.0), (0.6, 0.0), (0.6, 0.08), (0.0, 0.08)], (x, y, H - 0.2), M_PanelLight(), "Lights", closed=True, segments=32)
        disc("LampFace", (x, y, H - 0.205), 0.55, M_Soft(), "Lights", up=(0, 0, -1), segments=32)
        tube("LampStem", (x, y, 0), 0.03, H - 0.12, H, M_Metal(), "Lights", segments=12)
        R.light((x, y, H - 0.35), 25.0, color=(1.0, 0.92, 0.8), size=1.0)
    # The sideboard to port with its service; the display case forward.
    box("Sideboard", (-1.0, -W / 2 + 0.3, 0.45), (3.6, 0.56, 0.9), M_PanelDark(), "Furniture", bevel=0.01)
    box("SideboardTop", (-1.0, -W / 2 + 0.3, 0.92), (3.7, 0.6, 0.04), M_Black(), "Furniture")
    for k in range(4):
        lathe("Decanter", [(0.0, 0.0), (0.1, 0.0), (0.1, 0.18), (0.04, 0.26), (0.04, 0.34), (0.0, 0.34)], (-2.2 + k * 0.8, -W / 2 + 0.3, 0.94), M_Glass(), "Glass", segments=16)
    box("CaseBack", (L / 2 - 0.04, 0, 1.5), (0.06, 4.0, 2.2), M_PanelDark(), "Furniture")
    box("CaseLit", (L / 2 - 0.08, 0, 1.6), (0.02, 3.8, 1.6), M_Soft(), "Lights")
    for z in (0.95, 1.55, 2.15): box("CaseShelf", (L / 2 - 0.3, 0, z), (0.5, 4.0, 0.04), M_PanelLight(), "Furniture")
    for k in range(6): disc("Plate", (L / 2 - 0.3, -1.6 + k * 0.64, 1.25), 0.18, M_PanelLight(), "Furniture", axis='x', up=(-1, 0, 0), segments=24)
    box("ShipModel", (L / 2 - 0.3, 0, 1.85), (0.5, 0.9, 0.2), M_HullGrey(), "Furniture", bevel=0.05)
    box("CaseGlass", (L / 2 - 0.56, 0, 1.6), (0.02, 4.0, 2.0), M_Glass(), "Glass")
    for z in (0.58, 2.62): box("CaseFrame", (L / 2 - 0.3, 0, z), (0.56, 4.1, 0.06), M_PanelDark(), "Furniture")
    R.light((L / 2 - 0.4, 0, 2.0), 12.0, size=0.5)
    lift_door(R, "-x", 0.0)
    cove_ceiling(R, drop=0.3, width=0.8, panels=(), energy=0.0)
    return R

def build_cabin():
    """The captain's quarters (The Wrath of Khan): the window wall to port with its lit braces across three bays of
    glass (space beyond: the hull is hidden indoors), the lower sitting area before it with two lounge chairs and
    a low table, the raised floor behind carrying the desk and its viewer against the forward wall and the bunk to
    starboard, the shelves of antiques aft. Lift door aft, starboard of centre; red carpet."""
    R = Room("cabin", 9.0, 7.0, 2.8)
    L, W, H = R.L, R.W, R.H
    shell(R, M_PanelLight(), M_CarpetRed(), M_PanelLight(), skip_walls=("-y",))
    # The window wall.
    W2 = W / 2 + 0.06
    box("WinSill", (0, -W2, 0.35), (L + 0.24, 0.12, 0.7), M_PanelLight(), "Walls")
    box("WinHead", (0, -W2, H - 0.2), (L + 0.24, 0.12, 0.4), M_PanelLight(), "Walls")
    for x in (-L / 2 - 0.06, -1.5, 1.5, L / 2 + 0.06):
        box("WinPost", (x, -W2, 1.6), (0.3 if abs(x) > 4 else 0.24, 0.12, 1.8), M_PanelLight(), "Walls")
    bl = math.hypot(1.3, 1.8); ba = math.degrees(math.atan2(1.3, 1.8))
    for cx in (-3.0, 0.0, 3.0):
        box("WinGlass", (cx, -W2, 1.6), (2.76, 0.02, 1.8), M_Glass(), "Glass")
        for s in (-1, 1):
            box("WinBrace", (cx + s * 0.65, -W2 + 0.04, 1.6), (0.08, 0.03, bl), M_Strip(), "Lights", pitch=s * ba)
    box("SillTop", (0, -W2 + 0.1, 0.72), (L, 0.32, 0.04), M_PanelLight(), "Furniture")
    # The raised floor aft and to starboard, its nosing lit; the sitting area below it at the window.
    box("Dais", (1.0, 1.4, 0.125), (7.0, 4.2, 0.25), M_CarpetRed(), "Floors")
    box("DaisNose", (1.0, -0.7, 0.23), (7.0, 0.04, 0.04), M_Strip(), "Lights")
    box("DaisEnd", (-2.5, 1.4, 0.125), (0.04, 4.2, 0.25), M_Metal(), "Floors")
    lounge_chair((-1.2, -2.0, 0.0), -90.0); lounge_chair((1.4, -2.0, 0.0), -90.0)
    box("LowTable", (0.1, -2.6, 0.38), (1.2, 0.6, 0.03), M_Glass(), "Glass")
    for s in (-1, 1): box("LowTableLeg", (0.1 + s * 0.5, -2.6, 0.18), (0.06, 0.5, 0.36), M_Metal(), "Furniture")
    lathe("Pot", [(0.0, 0.0), (0.25, 0.0), (0.3, 0.5), (0.0, 0.5)], (-3.8, -2.6, 0), M_PanelDark(), "Furniture", segments=20)
    lathe("Plant", [(0.0, 0.0), (0.45, 0.3), (0.5, 0.7), (0.3, 1.0), (0.0, 1.1)], (-3.8, -2.6, 0.5), M_Plant(), "Furniture", segments=16)
    # The desk and its viewer against the forward wall (the Use spot before it), the chair.
    DZ = 0.25
    box("Desk", (L / 2 - 0.7, 1.4, DZ + 0.37), (0.9, 1.8, 0.74), M_PanelLight(), "Furniture", bevel=0.02)
    box("DeskTop", (L / 2 - 0.7, 1.4, DZ + 0.76), (0.96, 1.86, 0.04), M_Black(), "Furniture", bevel=0.01)
    box("Viewer", (L / 2 - 0.55, 1.4, DZ + 1.05), (0.1, 0.6, 0.45), M_Black(), "Furniture", pitch=10.0, bevel=0.01)
    screen("Scr", (L / 2 - 0.61, 1.4, DZ + 1.05), 0.5, 0.36, M_Screen("Communications"), "Screens", yaw=0.0, pitch=10.0)
    box("DeskTouch", (L / 2 - 0.85, 1.0, DZ + 0.785), (0.3, 0.4, 0.01), M_Buttons(), "Furniture")
    chair((L / 2 - 1.6, 1.4, DZ), 0.0)
    R.light((L / 2 - 1.0, 1.4, 1.8), 8.0, color=(0.6, 0.75, 1.0), size=0.5)
    # The bunk to starboard under its lit panel.
    box("Bed", (-1.0, W / 2 - 1.0, DZ + 0.3), (2.2, 1.8, 0.6), M_PanelLight(), "Furniture", bevel=0.03)
    box("Cover", (-1.0, W / 2 - 1.0, DZ + 0.62), (2.1, 1.7, 0.08), M_Burgundy(), "Furniture", bevel=0.03)
    box("Pillow", (-1.0, W / 2 - 0.35, DZ + 0.7), (1.6, 0.5, 0.12), M_Linen(), "Furniture", bevel=0.04)
    box("BedPanel", (-1.0, W / 2 - 0.04, 1.9), (2.0, 0.04, 0.5), M_Soft(), "Lights")
    R.light((-1.0, W / 2 - 1.0, 2.2), 10.0, color=(1.0, 0.92, 0.8), size=0.8)
    # Shelves of antiques aft, port of the lift: books, a sextant, a model ship; a round mirror by the door.
    for z in (DZ + 0.9, DZ + 1.4, DZ + 1.9): box("Shelf", (-L / 2 + 0.25, -0.3, z), (0.4, 2.6, 0.04), M_Wood(), "Furniture")
    for k in range(9):
        box("Book", (-L / 2 + 0.25, -1.4 + k * 0.14, DZ + 1.04), (0.22, 0.08, 0.24 + 0.03 * (k % 3)), (M_Burgundy(), M_PanelDark(), M_Wood())[k % 3], "Furniture")
    box("Sextant", (-L / 2 + 0.25, 0.3, DZ + 1.52), (0.2, 0.25, 0.2), M_Brass(), "Furniture", bevel=0.05)
    box("ModelShip", (-L / 2 + 0.25, -0.6, DZ + 2.02), (0.15, 0.9, 0.2), M_Wood(), "Furniture", bevel=0.03)
    box("ShelfLamp", (-L / 2 + 0.12, -0.3, DZ + 2.3), (0.2, 2.6, 0.04), M_Soft(), "Lights")
    R.light((-L / 2 + 0.8, -0.3, 1.9), 10.0, color=(1.0, 0.9, 0.75), size=0.8)
    disc("Mirror", (-L / 2 + 0.03, 2.9, 1.6), 0.35, M_Glass(), "Glass", axis='x', up=(1, 0, 0), segments=32)
    ring("MirrorRim", (-L / 2 + 0.03, 2.9, 1.6), 0.35, 0.39, -0.02, 0.02, M_Brass(), "Furniture", axis='x', segments=32)
    lift_door(R, "-x", 1.5)
    cove_ceiling(R, drop=0.3, width=0.8, panels=[(2.0, 1.2), (-1.5, 1.2)], energy=12.0)
    return R

def build_crew_quarters():
    """Deck F, crew quarters: a cabin corridor of the refit's section athwartships, the lift at its starboard end,
    cabin doors in alternate bays down both sides, the last cabin forward of the port end open: two bunks, a desk
    with its viewer, a locker, a wash stand. Origin: the floor at the corridor's centre; the cabin lies forward of
    the corridor at its port end."""
    R = Room("crew", 2 * CORR_W, 16.0, CORR_H)
    H = CORR_H; W = CORR_W
    Y0, Y1, P = -8.0, 8.0, 2.0
    CX0, CX1, CY0, CY1 = W, W + 4.2, -8.0, -4.0            # the open cabin: two bays wide
    shut = [(-3.0, "shut"), (1.0, "shut"), (5.0, "shut")]
    corridor_leg(R, -W, W, Y0, Y1, walls=("-x", "+x", "-y", "+y"), anchor=Y0, pitch=P,
                 doors={"-x": [(-7.0, "shut"), (-3.0, "shut"), (1.0, "shut"), (5.0, "shut")], "+x": [(-7.0, "open")] + shut})
    for y in (-7.0, -3.0, 1.0, 5.0):
        box("CabinNumber", (-W + 0.02, y + 0.85, 1.9), (0.02, 0.3, 0.12), M_Buttons(), "Lights")
        box("CabinNumber", (W - 0.02, y + 0.85, 1.9), (0.02, 0.3, 0.12), M_Buttons(), "Lights")
    doorway('x', Y1, 0.0, -1, depth=0.14, leaves=M_Burgundy(), label="Lift")
    box("LiftLabel", (0.0, Y1 - 0.08, 2.42), (0.6, 0.02, 0.08), M_Strip(), "Lights")
    # The open cabin: its floor, ceiling and walls (the corridor's door wall is its aft side).
    T = 0.12
    box("Floor", ((CX0 + CX1) / 2, (CY0 + CY1) / 2, -T / 2), (CX1 - CX0, CY1 - CY0, T), M_CarpetGrey(), "Floors")
    box("Ceiling", ((CX0 + CX1) / 2, (CY0 + CY1) / 2, 2.4 + T / 2), (CX1 - CX0, CY1 - CY0, T), M_PanelGrey(), "Ceiling")
    wall_run("CabinWall", 'y', CX1 + T / 2, CY0 - T, CY1 + T, 0, 2.4, T, M_PanelLight(), "Walls")
    wall_run("CabinWall", 'x', CY0 - T / 2, CX0, CX1 + T, 0, 2.4, T, M_PanelLight(), "Walls")
    wall_run("CabinWall", 'x', CY1 + T / 2, CX0, CX1 + T, 0, 2.4, T, M_PanelLight(), "Walls")
    mx, my = (CX0 + CX1) / 2, -7.0
    for z in (0.35, 1.45):
        box("Bunk", (CX1 - 0.5, my + 1.0, z), (1.0, 2.0, 0.16), M_PanelLight(), "Furniture", bevel=0.02)
        box("Mattress", (CX1 - 0.5, my + 1.0, z + 0.14), (0.96, 1.96, 0.12), M_Linen(), "Furniture", bevel=0.03)
        box("BunkCover", (CX1 - 0.5, my + 0.7, z + 0.2), (0.96, 1.3, 0.06), M_Burgundy(), "Furniture", bevel=0.02)
        box("BunkPillow", (CX1 - 0.5, my + 1.75, z + 0.24), (0.7, 0.4, 0.1), M_Linen(), "Furniture", bevel=0.03)
        box("BunkLamp", (CX1 - 0.04, my + 1.7, z + 0.9), (0.04, 0.4, 0.06), M_Soft(), "Lights")
    box("BunkLadder", (CX1 - 1.02, my + 0.4, 0.9), (0.04, 0.4, 1.6), M_Metal(), "Furniture")
    for z in (0.3, 0.6, 0.9, 1.2, 1.5): box("Rung", (CX1 - 1.02, my + 0.4, z), (0.03, 0.36, 0.03), M_Metal(), "Furniture")
    box("Desk", (mx - 0.4, CY1 - 0.35, 0.37), (1.4, 0.6, 0.74), M_PanelLight(), "Furniture", bevel=0.02)
    box("DeskTop", (mx - 0.4, CY1 - 0.35, 0.76), (1.46, 0.66, 0.04), M_Black(), "Furniture")
    box("Viewer", (mx - 0.4, CY1 - 0.2, 1.05), (0.5, 0.1, 0.4), M_Black(), "Furniture", bevel=0.01)
    screen("Scr", (mx - 0.4, CY1 - 0.26, 1.05), 0.42, 0.3, M_Screen("Science"), "Screens", yaw=90.0, pitch=8.0)
    lathe("Stool", [(0.0, 0.0), (0.06, 0.0), (0.06, 0.42), (0.2, 0.42), (0.2, 0.48), (0.0, 0.48)], (mx - 0.4, CY1 - 1.0, 0), M_Seat(), "Furniture", closed=True, segments=16)
    box("Locker", (CX0 + 0.4, CY0 + 0.4, 1.05), (0.7, 0.7, 2.1), M_PanelDark(), "Furniture", bevel=0.01)
    box("LockerSeam", (CX0 + 0.4, CY0 + 0.76, 1.05), (0.02, 0.01, 1.9), M_PanelGrey(), "Furniture")
    box("WashStand", (mx + 0.6, CY0 + 0.3, 0.45), (0.7, 0.5, 0.9), M_PanelLight(), "Furniture", bevel=0.02)
    lathe("Basin", [(0.0, 0.0), (0.22, 0.0), (0.26, 0.1), (0.0, 0.1)], (mx + 0.6, CY0 + 0.3, 0.9), M_PanelLight(), "Furniture", segments=20)
    disc("Mirror", (mx + 0.6, CY0 + 0.03, 1.6), 0.3, M_Glass(), "Glass", axis='y', up=(0, 1, 0), segments=32)
    box("CabinLight", (mx, (CY0 + CY1) / 2, 2.38), (1.0, 0.5, 0.04), M_Soft(), "Lights")
    R.light((mx, (CY0 + CY1) / 2, 2.2), 20.0, size=0.9)
    return R

def build_torpedo_bay():
    """The torpedo bay in the neck (The Wrath of Khan, The Undiscovered Country): the loading track down the
    middle to the two tube breeches in the forward bulkhead, a casing on its carriage, racks of casings two tiers
    high along both walls, the catwalk along the port wall with its ladder, the control console aft facing the
    track, red working lights. Lift door aft, starboard of centre."""
    R = Room("torp", 16.0, 7.0, 4.5)
    L, W, H = R.L, R.W, R.H
    shell(R, M_PanelDark(), M_DeckPlate(), M_PanelDark())
    # The track.
    box("TrackBed", (0.5, 0, 0.2), (13.0, 1.4, 0.4), M_PanelGrey(), "Furniture")
    for s in (-1, 1): box("Rail", (0.5, s * 0.5, 0.45), (13.0, 0.08, 0.1), M_Metal(), "Furniture")
    for k in range(10):
        for s in (-1, 1): box("Hazard", (-5.5 + k * 1.3, s * 0.95, 0.004), (0.5, 0.3, 0.008), M_PaintYellow(), "Floors", yaw=45.0)
    box("Carriage", (1.0, 0, 0.6), (2.4, 1.0, 0.3), M_Metal(), "Furniture")
    torpedo((1.0, 0, 0.75), 0.0)
    # The breeches.
    for y in (-1.0, 1.0):
        ring("BreechRing", (L / 2 - 0.02, y, 1.1), 0.55, 0.7, -0.5, 0.0, M_Metal(), "Walls", axis='x', segments=40)
        lathe("BreechDoor", [(0.0, -0.1), (0.55, -0.1), (0.55, -0.02), (0.0, -0.02)], (L / 2 - 0.02, y, 1.1), M_PanelGrey(), "Walls", axis='x', closed=True, segments=40)
        ring("BreechGlow", (L / 2 - 0.3, y, 1.1), 0.56, 0.6, -0.02, 0.02, M_Warn(), "Lights", axis='x', segments=40)
        box("BreechLamp", (L / 2 - 0.05, y, 2.1), (0.06, 0.3, 0.1), M_Warn(), "Lights")
    box("TubeLabel", (L / 2 - 0.05, 0, 2.6), (0.06, 2.4, 0.3), M_Buttons(), "Lights")
    box("LoadArm", (L / 2 - 1.2, 0, 2.2), (0.3, 2.6, 0.3), M_Metal(), "Furniture")
    for y in (-1.0, 1.0): box("LoadPiston", (L / 2 - 1.2, y, 1.6), (0.2, 0.2, 1.0), M_Metal(), "Furniture")
    # The racks.
    for s in (-1, 1):
        for z in (0.6, 1.6):
            box("RackShelf", (-1.0, s * (W / 2 - 0.8), z - 0.3), (8.4, 1.3, 0.1), M_Metal(), "Furniture")
            for k in range(4): torpedo((-4.0 + k * 2.2 + 0.1, s * (W / 2 - 0.8), z - 0.25), 0.0)
        for x in (-5.2, -1.0, 3.2):
            box("RackPost", (x, s * (W / 2 - 0.2), 1.2), (0.12, 0.12, 2.4), M_Metal(), "Furniture")
            box("RackPost", (x, s * (W / 2 - 1.4), 1.2), (0.12, 0.12, 2.4), M_Metal(), "Furniture")
    # The catwalk to port with its rails and ladder.
    box("Catwalk", (-1.0, -W / 2 + 0.7, 2.45), (10.0, 1.3, 0.1), M_Grating(), "Floors")
    for z in (0.45, 1.0): box("CatRail", (-1.0, -W / 2 + 1.38, 2.5 + z), (10.0, 0.06, 0.06), M_Metal(), "Rails")
    for x in range(-5, 4, 2): box("CatPost", (x, -W / 2 + 1.38, 3.0), (0.06, 0.06, 1.0), M_Metal(), "Rails")
    for s in (-1, 1): box("LadderStile", (4.2, -W / 2 + 1.1 + s * 0.25, 1.4), (0.05, 0.05, 2.5), M_Metal(), "Furniture")
    for z in (0.3, 0.6, 0.9, 1.2, 1.5, 1.8, 2.1, 2.4): box("Rung", (4.2, -W / 2 + 1.1, z), (0.04, 0.5, 0.04), M_Metal(), "Furniture")
    # The console aft facing the track (Use), the tactical screen on the aft wall.
    console_desk((-6.2, 0, 0), 2.4, 0.8, 0.0, ("TorpedoBay", "Weapons"), screens=2)
    wall_screen((-L / 2 + 0.16, -2.0, 2.0), 1.6, 0.9, 180.0, "Tactical")
    # Lights: warm troughs down the ceiling, red lamps along the walls, the beams.
    ceiling_beams(R, 2.6, 0.3)
    for x in (-5.0, -1.5, 2.0, 5.5):
        box("Trough", (x, 0, H - 0.05), (0.4, W - 1.0, 0.1), M_Soft(), "Lights")
        R.light((x, 0, H - 0.3), 60.0, color=(1.0, 0.85, 0.8), size=2.0)
    for s in (-1, 1):
        for x in (-4.0, 0.0, 4.0): box("RedLamp", (x, s * (W / 2 - 0.03), 3.6), (0.3, 0.04, 0.08), M_Warn(), "Lights")
    lift_door(R, "-x", 2.0)
    return R

def build_cargo_bay():
    """The cargo bay (The Motion Picture): container stacks three high in their racks down both sides of the
    aisle, the gantry crane across the bay on its rails with the hoist over the aisle, the cargo transporter's
    wide pad at the aft end under its lattice, the manifest console by the lift (Use). Lift door forward,
    starboard of centre."""
    R = Room("cargo", 20.0, 12.0, 7.0)
    L, W, H = R.L, R.W, R.H
    shell(R, M_PanelGrey(), M_DeckPlate(), M_PanelDark())
    bands = (M_PaintRed(), M_PaintYellow(), M_StripBlue(), M_PanelDark())
    for s in (-1, 1):
        for i in range(8):
            x = -8.0 + i * 2.3
            for tier in range(3):
                if (i * 7 + tier * 3) % 5 == 4: continue
                z = 1.05 + tier * 2.2
                box("Container", (x, s * (W / 2 - 1.2), z), (2.0, 2.0, 2.0), M_HullGrey() if (i + tier) % 2 else M_PanelGrey(), "Cargo", bevel=0.04)
                box("ContainerBand", (x, s * (W / 2 - 1.2), z), (2.02, 2.02, 0.2), bands[(i + tier) % 4], "Cargo")
            for z in (2.2, 4.4): box("RackBeam", (x, s * (W / 2 - 1.2), z - 0.05), (2.3, 2.3, 0.1), M_Metal(), "Furniture")
        for x in (-9.2, -4.6, 0.0, 4.6, 9.2):
            for yy in (s * (W / 2 - 0.1), s * (W / 2 - 2.3)): box("RackPost", (x, yy, 3.3), (0.15, 0.15, 6.6), M_Metal(), "Furniture")
        box("GantryRail", (0, s * (W / 2 - 2.5), 5.6), (L - 1.0, 0.2, 0.2), M_Metal(), "Furniture")
    box("Gantry", (2.0, 0, 5.85), (0.6, W - 4.6, 0.5), M_PaintYellow(), "Furniture", bevel=0.03)
    box("Hoist", (2.0, -1.0, 5.25), (0.5, 0.5, 0.7), M_PanelDark(), "Furniture")
    box("HoistCable", (2.0, -1.0, 3.6), (0.04, 0.04, 2.6), M_Metal(), "Furniture")
    box("HoistHook", (2.0, -1.0, 2.15), (0.2, 0.1, 0.3), M_Metal(), "Furniture")
    # The cargo transporter aft.
    PX = -L / 2 + 2.2
    box("CargoPad", (PX, 0, 0.1), (3.6, 4.6, 0.2), M_PanelGrey(), "Pad")
    for i in range(3):
        for j in range(4): box("PadCell", (PX - 1.1 + i * 1.1, -1.65 + j * 1.1, 0.205), (0.9, 0.9, 0.01), M_Pad(), "Lights")
    box("PadWall", (-L / 2 + 0.05, 0, 1.6), (0.06, 5.0, 2.6), M_Lattice(), "Lights")
    for y in (-2.5, -1.25, 0.0, 1.25, 2.5): box("PadMullion", (-L / 2 + 0.1, y, 1.6), (0.12, 0.1, 2.7), M_Metal(), "Walls")
    console_desk((-L / 2 + 4.6, 3.4, 0), 1.2, 0.7, 180.0, "Engineering")
    R.light((PX, 0, 3.0), 60.0, color=(0.8, 0.88, 1.0), size=2.0)
    # The manifest console by the lift (the person stands aft of it), the aisle line, deck lamps.
    console_desk((L / 2 - 1.6, 0.6, 0), 1.6, 0.8, 0.0, ("MasterSituation", "Deflector"), screens=2)
    box("AisleLine", (0, 0, 0.004), (18.0, 0.3, 0.008), M_PaintYellow(), "Floors")
    for x in range(-8, 9, 4):
        for s in (-1, 1): box("DeckLamp", (x, s * 2.6, 0.01), (0.25, 0.25, 0.02), M_StripBlue(), "Lights")
    truss_ceiling(R, 4.0, depth=0.8, light_size=(2.0, 0.9), energy=150.0, runner_y=2.4)
    pilasters(R, 5.0, walls=("+x", "-x"), skip=(("+x", 3.0),))
    lift_door(R, "+x", 3.0)
    return R

ROOMS = {"eng": build_engineering, "sick": build_sickbay, "bay": build_shuttle_bay, "trans": build_transporter, "lock": build_airlock,
         "corr": build_corridor, "rec": build_rec_deck, "mess": build_mess, "cabin": build_cabin, "crew": build_crew_quarters, "torp": build_torpedo_bay, "cargo": build_cargo_bay}
MESH_NAMES = {"eng": "SM_RoomEngineering", "sick": "SM_RoomSickbay", "bay": "SM_RoomShuttleBay", "trans": "SM_RoomTransporter", "lock": "SM_RoomAirlock",
              "corr": "SM_RoomCorridorG", "rec": "SM_RoomRecDeck", "mess": "SM_RoomMess", "cabin": "SM_RoomCabin", "crew": "SM_RoomCrewQuarters", "torp": "SM_RoomTorpedoBay", "cargo": "SM_RoomCargoBay"}

# Preview cameras per room: (eye, target, lens mm), in the game frame.
VIEWS = {
    "eng": {
        "lift": ((7.0, 3.0, 1.7), (-3.0, 0.0, 1.6), 20),
        "console": ((2.4, 0.0, 1.7), (-3.0, 0.0, 2.4), 22),
        "side": ((5.5, -3.3, 1.6), (-3.0, 1.0, 2.0), 20),
        "well": ((-0.6, 0.0, 1.7), (-3.0, 0.0, -0.5), 20),
        "high": ((9.5, -7.0, 7.5), (-1.5, 0.5, 1.0), 22),
    },
    "sick": {
        "door": ((-5.1, -2.5, 1.65), (2.0, 1.0, 1.1), 20),
        "ward": ((2.5, 1.5, 1.6), (-2.4, -4.5, 1.0), 20),
        "desk": ((1.0, 2.0, 1.6), (3.5, 2.0, 1.0), 24),
        "high": ((7.5, -6.0, 6.0), (0.0, 0.5, 0.6), 22),
    },
    "bay": {
        "lift": ((12.0, 5.0, 1.7), (1.5, -1.0, 1.5), 20),
        "deck": ((-11.0, -3.0, 1.7), (6.0, 1.0, 2.5), 20),
        "shuttle": ((0.6, -6.5, 1.7), (1.5, 0.0, 1.3), 18),
        "gallery": ((-3.0, 5.0, 1.7), (13.0, 0.0, 4.4), 24),
        "high": ((-16.0, -12.0, 10.0), (2.0, 0.0, 1.5), 22),
        "throat": ((-6.0, 4.0, 1.7), (-17.0, 0.0, 3.0), 24),
        "door": ((-24.0, 2.0, 2.0), (-8.0, 0.0, 2.5), 26),
    },
    "lock": {
        "lift": ((-2.1, 0.0, 1.65), (3.0, 0.0, 1.2), 20),
        "hatch": ((1.4, 0.3, 1.65), (3.0, 0.0, 1.25), 26),
        "lockers": ((1.6, 1.6, 1.6), (-0.6, -2.5, 1.2), 22),
        "high": ((5.2, -4.6, 4.6), (0.0, 0.0, 0.8), 24),
    },
    "trans": {
        "door": ((1.5, 2.6, 1.65), (-1.5, 0.0, 1.0), 20),
        "console": ((3.3, 0.0, 1.65), (-1.5, 0.0, 1.0), 22),
        "pad": ((-1.5, 0.0, 1.85), (3.3, 0.0, 1.0), 22),
        "high": ((6.0, -5.0, 5.5), (-0.8, 0.0, 0.8), 22),
    },
}

VIEWS.update({
    "corr": {
        "lift": ((0.0, 1.0, 1.65), (0.0, -9.0, 1.3), 20),
        "sickdoor": ((-0.6, -4.5, 1.65), (1.2, -1.3, 1.2), 22),
        "legb": ((2.0, -10.3, 1.65), (18.0, -10.3, 1.4), 22),
        "panel": ((15.0, -10.3, 1.65), (18.2, -10.3, 1.5), 24),
        "high": ((-6.0, -5.0, 8.0), (6.0, -6.0, 1.0), 22),
    },
    "rec": {
        "door": ((8.0, -2.0, 1.65), (-8.0, 1.0, 2.5), 20),
        "screen": ((2.0, 3.0, 1.65), (-9.0, 0.0, 3.0), 22),
        "gallery": ((-7.5, 5.8, 4.7), (6.0, -3.0, 1.5), 20),
        "stair": ((-5.0, -2.0, 1.65), (2.0, 4.5, 1.8), 22),
        "high": ((-14.0, -12.0, 12.0), (0.0, 0.0, 1.5), 24),
    },
    "mess": {
        "door": ((-4.6, 0.0, 1.65), (5.0, 0.0, 1.0), 20),
        "head": ((4.8, 0.3, 1.6), (-4.0, 0.0, 0.9), 22),
        "case": ((1.0, -2.8, 1.6), (5.5, 0.5, 1.5), 22),
        "high": ((8.0, -7.0, 6.0), (0.0, 0.0, 0.8), 22),
    },
    "cabin": {
        "door": ((-3.6, 1.5, 1.65), (3.0, -1.5, 1.0), 20),
        "window": ((2.5, 2.5, 1.65), (-1.0, -3.4, 1.2), 20),
        "desk": ((1.2, 1.4, 1.75), (4.5, 1.4, 1.2), 24),
        "high": ((7.0, -7.0, 6.0), (0.0, 0.5, 0.8), 22),
    },
    "crew": {
        "lift": ((0.0, 7.0, 1.65), (0.0, -8.0, 1.2), 20),
        "cabindoor": ((-0.7, -3.8, 1.65), (1.3, -7.0, 1.1), 22),
        "bunks": ((1.8, -4.4, 1.6), (5.3, -6.4, 0.9), 22),
        "high": ((-5.0, -12.0, 8.0), (1.5, -3.0, 1.0), 22),
    },
    "torp": {
        "lift": ((-7.0, 2.0, 1.65), (7.0, 0.0, 1.2), 20),
        "track": ((-5.5, -0.9, 1.65), (8.0, 0.0, 1.1), 22),
        "racks": ((3.0, 1.2, 1.6), (-6.0, -3.2, 1.2), 20),
        "high": ((-12.0, -9.0, 9.0), (1.0, 0.0, 1.0), 22),
    },
    "cargo": {
        "lift": ((9.0, 3.0, 1.65), (-9.0, 0.0, 2.0), 20),
        "aisle": ((-3.0, 0.5, 1.65), (10.0, 0.5, 1.5), 22),
        "pad": ((-3.0, -3.0, 1.65), (-9.5, 0.0, 1.2), 22),
        "high": ((16.0, -14.0, 14.0), (0.0, 0.0, 2.0), 24),
    },
})

def new_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.unit_settings.system = 'METRIC'
    MATS.clear(); COLLECTIONS.clear(); COUNT[0] = 0
    w = bpy.context.scene.world or bpy.data.worlds.new("World"); bpy.context.scene.world = w; w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.02, 0.02, 0.025, 1); w.node_tree.nodes["Background"].inputs[1].default_value = 1.0

def previews(R, outdir, only=None):
    os.makedirs(outdir, exist_ok=True)
    scn = bpy.context.scene
    engines = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items]
    scn.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in engines else 'BLENDER_EEVEE'
    scn.render.resolution_x = 1280; scn.render.resolution_y = 720; scn.render.resolution_percentage = 100
    scn.view_settings.view_transform = 'AgX' if 'AgX' in [i.identifier for i in bpy.types.ColorManagedViewSettings.bl_rna.properties['view_transform'].enum_items] else 'Filmic'
    cam_data = bpy.data.cameras.new("Cam"); cam = bpy.data.objects.new("Cam", cam_data); scn.collection.objects.link(cam); scn.camera = cam
    for i, (kind, pos, energy, color, size) in enumerate(R.lights):
        ld = bpy.data.lights.new(f"L{i}", kind); ld.energy = energy; ld.color = color
        if kind == 'AREA': ld.size = size
        else: ld.shadow_soft_size = size
        lo = bpy.data.objects.new(f"L{i}", ld); scn.collection.objects.link(lo); lo.location = B(*pos)
        if kind == 'AREA': lo.rotation_euler = (0, 0, 0)       # area lights point down -Z by default
    for name, (eye, target, lens) in VIEWS[R.code].items():
        if only is not None and name not in only: continue
        hide = name == "high"
        if hide:
            for o in coll("Ceiling").objects: o.hide_render = True
            for o in coll("Lights").objects:
                if o.name.startswith("CeilLight") or o.name.startswith("CeilRing"): o.hide_render = True
        cam.location = B(*eye); cam_data.lens = lens
        d = B(*target) - cam.location
        cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
        scn.render.filepath = os.path.join(outdir, f"prev_{R.code}_{name}.png"); bpy.ops.render.render(write_still=True)
        if hide:
            for o in coll("Ceiling").objects: o.hide_render = False
            for o in coll("Lights").objects: o.hide_render = False

def export(path, only=None, exclude=()):
    """Export the scene's meshes; `only` keeps one collection, `exclude` drops some (the bay's shuttle is its own
    mesh so the game can hide it while the real shuttle is out)."""
    bpy.ops.object.select_all(action='DESELECT')
    for o in bpy.data.objects:
        if o.type != 'MESH': continue
        groups = [c.name for c in o.users_collection]
        if only is not None and only not in groups: continue
        if any(g in exclude for g in groups): continue
        o.select_set(True)
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, apply_unit_scale=True, global_scale=1.0, axis_forward='-Y', axis_up='Z', object_types={'MESH'}, use_mesh_modifiers=True, mesh_smooth_type='FACE', add_leaf_bones=False, bake_anim=False, path_mode='COPY', embed_textures=False)

if __name__ == "__main__":
    rooms = (arg("--room") or "eng,sick,bay,trans,lock,corr,rec,mess,cabin,crew,torp,cargo").split(",")
    only = arg("--views"); only = set(only.split(",")) if only else None
    for code in rooms:
        new_scene()
        R = ROOMS[code]()
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, f"room_{code}.blend"))
        if "--render" in ARGS:
            previews(R, os.path.join(HERE, "previews"), only)
        if "--export" in ARGS:
            if code == "bay":
                export(os.path.join(HERE, f"{MESH_NAMES[code]}.fbx"), exclude=("Shuttle",))
                export(os.path.join(HERE, "SM_RoomBayShuttle.fbx"), only="Shuttle")
            else:
                export(os.path.join(HERE, f"{MESH_NAMES[code]}.fbx"))
        print(f"ROOM {code} DONE objects", len(bpy.data.objects))
