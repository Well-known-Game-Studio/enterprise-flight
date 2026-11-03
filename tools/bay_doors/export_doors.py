"""gores_B.obj -> bay_doors.fbx, each gore its own object with the origin on the door's axis at the deck. Run:
    blender --background --python tools/bay_doors/export_doors.py
"""
import bpy, os
from mathutils import Vector
HERE = os.path.dirname(os.path.abspath(__file__))
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.wm.obj_import(filepath=os.path.join(HERE, "gores_B.obj"), forward_axis='Y', up_axis='Z')
gores = sorted([o for o in bpy.data.objects if o.type == 'MESH'], key=lambda o: o.name)
bpy.ops.object.select_all(action='DESELECT')
for g in gores: g.select_set(True)
bpy.context.view_layer.objects.active = gores[0]
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
PIV = Vector((-63.30, 0.0, -19.48))   # pawn (-6330, 0, -1948) cm in the export frame (x, -y, z) m
# Vertices relative to the axis and the object at the origin: the import bakes an identity node transform (and
# its unit conversion), so the mesh's own origin is the axis and a yaw about it is the swing.
for g in gores:
    for v in g.data.vertices: v.co -= PIV
    g.location = (0.0, 0.0, 0.0)
bpy.ops.export_scene.fbx(filepath=os.path.join(HERE, "..", "..", "blender", "ship", "bay_doors.fbx"), use_selection=True, axis_forward='-Y', axis_up='Z', mesh_smooth_type='FACE', object_types={'MESH'}, path_mode='STRIP', bake_space_transform=False)
print("exported bay_doors.fbx")
