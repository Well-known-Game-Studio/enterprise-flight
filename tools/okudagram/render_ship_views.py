"""Render the ship's plan and profile from the real 1701-A model for the Okudagram screens: a filled silhouette and
a line drawing (Freestyle: silhouette, creases, borders) of the top and side views, white on black, 2048 px.

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python tools/okudagram/render_ship_views.py -- <Enterprise-A.obj> [outdir]

The OBJ (Major Stress's Enterprise-A, the hull the game flies) is in metres, y along the length, z up; the bow is
found as the end where the saucer is (the widest slices). Outputs: ship_top_sil.png, ship_top_lines.png,
ship_side_sil.png, ship_side_lines.png in tools/okudagram/ship_views (gen_okuda.py reads them)."""
import bpy, sys, os, math
from mathutils import Vector

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OBJ = ARGS[0]
OUT = ARGS[1] if len(ARGS) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "ship_views")
os.makedirs(OUT, exist_ok=True)
RES = 2048

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.wm.obj_import(filepath=OBJ, forward_axis='Y', up_axis='Z')
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
# Bounds and the bow: the saucer is the widest part of the ship; whichever end of y it sits at is forward.
pts = [o.matrix_world @ Vector(c) for o in meshes for c in o.bound_box]
lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
bins = {}
for o in meshes:
    for v in o.data.vertices:
        w = o.matrix_world @ v.co
        k = int((w.y - lo.y) / (hi.y - lo.y) * 20); k = min(max(k, 0), 19)
        bins[k] = max(bins.get(k, 0.0), abs(w.x))
widest = max(bins, key=bins.get)
bow_sign = 1.0 if widest >= 10 else -1.0
centre = (lo + hi) / 2
L = hi.y - lo.y
print(f"SHIP bounds {tuple(round(v, 1) for v in lo)} .. {tuple(round(v, 1) for v in hi)} length {L:.1f} bow at {'+y' if bow_sign > 0 else '-y'}")

# One flat material on everything, so the silhouette is a single tone and Freestyle sees only the geometry.
mat = bpy.data.materials.new("Flat"); mat.use_nodes = True
bsdf = mat.node_tree.nodes["Principled BSDF"]
bsdf.inputs["Base Color"].default_value = (0, 0, 0, 1); bsdf.inputs["Emission Color"].default_value = (0, 0, 0, 1)
bsdf.inputs["Emission Strength"].default_value = 0.0
for o in meshes:
    o.data.materials.clear(); o.data.materials.append(mat)
    o.rotation_euler = (0, 0, 0 if bow_sign > 0 else math.pi)   # bow to +y

scn = bpy.context.scene
w = bpy.data.worlds.new("W"); scn.world = w; w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0, 0, 0, 1)
scn.render.resolution_x = scn.render.resolution_y = RES; scn.render.resolution_percentage = 100
scn.render.film_transparent = False
scn.view_settings.view_transform = 'Standard'
cam_data = bpy.data.cameras.new("Cam"); cam_data.type = 'ORTHO'; cam_data.ortho_scale = L * 1.04; cam_data.clip_end = 5000
cam = bpy.data.objects.new("Cam", cam_data); scn.collection.objects.link(cam); scn.camera = cam

def aim(loc, rot):
    cam.location = loc; cam.rotation_euler = rot

# Top: looking down, bow (+y) up the frame. Side: from starboard looking to port, bow to the right, z up.
VIEWS = {
    "top": ((centre.x, centre.y, hi.z + 400), (0, 0, 0)),
    "side": ((hi.x + 400, centre.y, centre.z), (math.pi / 2, 0, math.pi / 2)),
}

def render(name):
    scn.render.filepath = os.path.join(OUT, name); bpy.ops.render.render(write_still=True); print("WROTE", name)

# 1. Silhouettes: Workbench, flat single white.
scn.render.engine = 'BLENDER_WORKBENCH'
sh = scn.display.shading
sh.light = 'FLAT'; sh.color_type = 'SINGLE'; sh.single_color = (1, 1, 1); sh.show_object_outline = False
sh.show_cavity = False; sh.show_shadows = False; sh.show_xray = False
for name, (loc, rot) in VIEWS.items():
    aim(loc, rot); render(f"ship_{name}_sil.png")

# 2. Line drawings: EEVEE with Freestyle over the black material.
engines = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items]
scn.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in engines else 'BLENDER_EEVEE'
scn.render.use_freestyle = True
scn.render.line_thickness_mode = 'ABSOLUTE'; scn.render.line_thickness = 2.4
vl = bpy.context.view_layer
vl.use_freestyle = True
fs = vl.freestyle_settings
fs.crease_angle = math.radians(110)
for ls in list(fs.linesets): fs.linesets.remove(ls)
ls = fs.linesets.new("Lines")
ls.select_silhouette = True; ls.select_crease = True; ls.select_border = True; ls.select_edge_mark = False
ls.select_by_visibility = True; ls.visibility = 'VISIBLE'
ls.linestyle.color = (1, 1, 1); ls.linestyle.thickness = 2.4
for name, (loc, rot) in VIEWS.items():
    aim(loc, rot); render(f"ship_{name}_lines.png")
print("SHIP VIEWS DONE")
