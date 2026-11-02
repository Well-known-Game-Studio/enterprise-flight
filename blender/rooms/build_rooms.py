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

# ---- room shell ------------------------------------------------------------------------------------------------
class Room:
    def __init__(self, code, L, W, H):
        self.code, self.L, self.W, self.H = code, L, W, H
        self.lights = []        # (kind, pos, energy, color, size)

    def light(self, pos, energy, color=(0.95, 0.97, 1.0), size=0.8, kind='AREA'):
        self.lights.append((kind, pos, energy, color, size))

def shell(R, wall_mat, floor_mat, ceil_mat, open_aft=False, floor=True):
    L, W, H = R.L, R.W, R.H
    T = 0.12
    if floor:
        box("Floor", (0, 0, -T / 2), (L + 2 * T, W + 2 * T, T), floor_mat, "Floors")
    box("Ceiling", (0, 0, H + T / 2), (L + 2 * T, W + 2 * T, T), ceil_mat, "Ceiling")
    box("WallFwd", (L / 2 + T / 2, 0, H / 2), (T, W + 2 * T, H), wall_mat, "Walls")
    if not open_aft:
        box("WallAft", (-L / 2 - T / 2, 0, H / 2), (T, W + 2 * T, H), wall_mat, "Walls")
    for sgn in (-1, 1):
        box("WallSide", (0, sgn * (W / 2 + T / 2), H / 2), (L + 2 * T, T, H), wall_mat, "Walls")
    # A dark skirting all round.
    for sgn in (-1, 1):
        box("Skirt", (0, sgn * (W / 2 - 0.02), 0.18), (L, 0.04, 0.36), M_PanelDark(), "Walls")
    box("Skirt", (L / 2 - 0.02, 0, 0.18), (0.04, W, 0.36), M_PanelDark(), "Walls")
    if not open_aft:
        box("Skirt", (-L / 2 + 0.02, 0, 0.18), (0.04, W, 0.36), M_PanelDark(), "Walls")

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
    shell(R, M_PanelLight(), M_Carpet(), M_PanelGrey())
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
    # A second door forward to the rest of the complex, and the lift aft.
    lift_door(R, "+x", -2.0 + 0.0 if False else 4.1, mat_=M_PanelDark(), label="WardDoor")
    pilasters(R, 2.0, walls=("+y", "-x"), skip=(("-x", -2.5),))
    lift_door(R, "-x", -2.5)
    cove_ceiling(R, drop=0.32, width=0.9, panels=[(-2.5, -1.6), (-2.5, 1.6), (2.5, -1.6), (2.5, 1.6)], energy=16.0)
    return R

# ---- shuttle bay -----------------------------------------------------------------------------------------------
def shuttle(center, yaw=0.0, name="Galileo"):
    """A Final Frontier-style shuttle on its skids: a long white body from a side profile, the windscreen let into
    the slope of the nose, low pods set into the flanks, a roof pod, the port door over its step and a rear hatch;
    built facing +x then yawed about `center`."""
    cx, cy, cz = center
    def P(p): x, y, z = rot(p, yaw); return (cx + x, cy + y, cz + z)
    body = [(-3.6, 0.62), (-3.6, 2.28), (-3.3, 2.4), (1.7, 2.4), (3.25, 1.6), (3.5, 1.15), (3.2, 0.62)]
    extrude_y("Hull", body, -1.3, 1.3, M_HullWhite(), "Shuttle", xf=P, bevel=0.08)
    # The pods sit low enough for a walker's knee to meet them (the deck walk traces at 45 cm).
    pod = [(-2.9, 0.38), (-2.9, 1.02), (1.3, 1.02), (1.8, 0.7), (1.3, 0.38)]
    for s_ in (-1, 1):
        y0, y1 = (1.3, 1.95) if s_ > 0 else (-1.95, -1.3)
        extrude_y("Pod", pod, y0, y1, M_HullWhite(), "Shuttle", xf=P, bevel=0.07)
        box("PodGlow", P((-2.92, s_ * 1.62, 0.7)), (0.04, 0.5, 0.4), M_Core(), "Shuttle", yaw=yaw)
        box("PodStripe", P((-0.6, s_ * 1.96, 0.88)), (4.0, 0.02, 0.06), M_StripBlue(), "Shuttle", yaw=yaw)
        box("Stripe", P((-0.5, s_ * 1.315, 2.1)), (5.6, 0.02, 0.1), M_PaintRed(), "Shuttle", yaw=yaw)
        box("Skid", P((-0.6, s_ * 0.9, 0.2)), (4.6, 0.28, 0.4), M_PanelDark(), "Shuttle", yaw=yaw)
    # The windscreen on the nose slope, set just proud of it.
    (x0, z0), (x1, z1) = (1.7, 2.4), (3.25, 1.6)
    ptc = math.degrees(math.atan2(z0 - z1, x1 - x0))
    nrm = rot((0.0, 0.0, 1.0), pitch=ptc)
    mx, mz = (x0 + x1) / 2, (z0 + z1) / 2
    box("Windscreen", P((mx + nrm[0] * 0.02, 0.0, mz + nrm[2] * 0.02)), (1.3, 1.9, 0.04), M_Black(), "Shuttle", yaw=yaw, pitch=ptc)
    box("RoofPod", P((-0.8, 0.0, 2.55)), (2.4, 1.4, 0.3), M_HullWhite(), "Shuttle", yaw=yaw, bevel=0.08)
    box("Beacon", P((0.5, 0.0, 2.72)), (0.12, 0.12, 0.04), M_Warn(), "Shuttle", yaw=yaw)
    box("RearHatch", P((-3.62, 0.0, 1.45)), (0.04, 1.8, 1.4), M_PanelDark(), "Shuttle", yaw=yaw)
    box("PortDoor", P((-0.3, -1.315, 1.7)), (0.9, 0.03, 1.2), M_PanelDark(), "Shuttle", yaw=yaw)
    for dz, h in ((0.0, 0.03), (1.2, 0.03)):
        box("DoorEdge", P((-0.3, -1.325, 1.1 + dz)), (0.94, 0.01, h), M_Strip(), "Shuttle", yaw=yaw)
    box("DoorStep", P((-0.3, -1.6, 1.0)), (0.9, 0.5, 0.06), M_PanelDark(), "Shuttle", yaw=yaw)
    box("NoseLamp", P((3.46, 0.0, 0.9)), (0.04, 0.6, 0.08), M_Strip(), "Shuttle", yaw=yaw)

def build_shuttle_bay():
    """The landing bay (The Final Frontier): a pale deck with the red approach line down the middle and the painted
    landing box, blue strip lights along the grey walls, the control gallery's windows high on the forward
    bulkhead, the open door frame astern, the shuttle on its skids on the box and cargo stacked to starboard.
    Lift door forward, starboard; a side door to port."""
    R = Room("bay", 26.0, 16.0, 6.5)
    L, W, H = R.L, R.W, R.H
    shell(R, M_PanelGrey(), M_BayDeck(), M_PanelDark(), open_aft=True)
    # The door frame astern.
    for s in (-1, 1):
        box("DoorPost", (-L / 2 + 0.3, s * (W / 2 - 0.3), H / 2), (0.6, 0.6, H), M_PanelDark(), "Walls")
        for z in (2.0, 4.0):
            box("WarnLamp", (-L / 2 + 0.61, s * (W / 2 - 0.3), z), (0.03, 0.12, 0.3), M_Warn(), "Lights")
    box("Lintel", (-L / 2 + 0.3, 0, H - 0.45), (0.6, W, 0.9), M_PanelDark(), "Walls")
    box("LintelStrip", (-L / 2 + 0.61, 0, H - 0.95), (0.03, W - 1.4, 0.12), M_Strip(), "Lights")
    # Wall bands: the blue strip at 4 m, a dark rail at 1.2 m, pilasters every 4 m.
    for s in (-1, 1):
        box("BlueStrip", (0.3, s * (W / 2 - 0.02), 4.0), (L - 0.6, 0.04, 0.10), M_StripBlue(), "Lights")
        box("RailBand", (0.3, s * (W / 2 - 0.03), 1.2), (L - 0.6, 0.06, 0.10), M_PanelDark(), "Walls")
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
    for x in range(-12, 8, 2):
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
    shell(R, M_PanelGrey(), M_Carpet(), M_PanelGrey())
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
    pilasters(R, 2.0, walls=("+y", "+x"), skip=(("+y", 1.5),))
    lift_door(R, "+y", 1.5)
    radial_ceiling(R, (PX, PY), 1.9, ribs=8, energy=14.0, panels=[(2.5, -1.8), (2.5, 1.8), (0.6, -2.6), (0.6, 2.6)])
    return R

ROOMS = {"eng": build_engineering, "sick": build_sickbay, "bay": build_shuttle_bay, "trans": build_transporter}
MESH_NAMES = {"eng": "SM_RoomEngineering", "sick": "SM_RoomSickbay", "bay": "SM_RoomShuttleBay", "trans": "SM_RoomTransporter"}

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
    },
    "trans": {
        "door": ((1.5, 2.6, 1.65), (-1.5, 0.0, 1.0), 20),
        "console": ((3.3, 0.0, 1.65), (-1.5, 0.0, 1.0), 22),
        "pad": ((-1.5, 0.0, 1.85), (3.3, 0.0, 1.0), 22),
        "high": ((6.0, -5.0, 5.5), (-0.8, 0.0, 0.8), 22),
    },
}

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
    rooms = (arg("--room") or "eng,sick,bay,trans").split(",")
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
