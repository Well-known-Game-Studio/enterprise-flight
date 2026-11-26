"""Builds M_Asteroid, the material of the rocks in space (RockMeshes.h): belt asteroids and ring rocks (instanced),
ASpaceTarget and its kin (an impactor, a leviathan's case).

The rock is coloured and roughened by noise laid in its own frame (the instance's local position, shifted per
instance), so the pattern is the same density on a boulder as on a mountain and does not swim when the world's
origin moves. Its relief is the gradient of two octaves of the same noise, which tilts the mesh's normal.

That gradient is a direction in the rock's frame. Turned to the world it comes out multiplied by the rock's scale
(a rock a kilometre across is scaled some hundreds of times), so it is divided by that scale before it is used: a
bump is a slope, and a slope has no size. (Until this builder the scale was left in, the normal of every rock was
the noise's gradient and nothing else, and a rock was lit as dazzle paint: NormalStrength did nothing.) Only the
part of the slope that lies along the surface tilts the normal. The transform is the instance's own, not its
set's, as in ground_rock_material.py.

Parameters: RockColorA, RockColorB (the rock, blended by the broad noise), RockColorC (pale patches), NormalStrength
(how steep the relief: 0 is the mesh's own normal).

Run with the editor open and no play session:  python3 tools/planet_materials/asteroid_material.py
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
from uemat import *

NAME = "M_Asteroid"

NORMAL = r"""
// The slope in the world's frame with the rock's size taken out of it (TX is a unit length of the rock's frame).
float3 slope = TG / max(length(TX), 1.0e-6);
// Only what lies along the surface tilts the normal.
slope -= N * dot(slope, N);
return normalize(N - slope * Strength);
"""


def build():
    wipe(NAME)
    g = G(NAME, {"ShadingModel": "MSM_DefaultLit", "BlendMode": "BLEND_Opaque", "bTangentSpaceNormal": False, "TwoSided": False,
                 "bUsedWithInstancedStaticMeshes": True})
    # Where on the rock, in the rock's own frame, shifted per instance.
    lp = g.node("LocalPosition", {"IncludedOffsets": "IncludeOffsets", "LocalOrigin": "Instance"})
    scaled = g.node("Multiply"); g.c(lp, scaled, "A", "XYZ"); g.c(g.const(0.045), scaled, "B")
    p = g.add_(scaled, g.mul(g.node("PerInstanceRandom"), g.const(37.0)))
    grad = "NOISEFUNCTION_GradientALU"
    # Colour: two rocks blended by the broad noise, pale patches where a slower noise runs high, all of it mottled finely.
    broad = g.noise(p, levels=5, fn=grad, levelscale=2.2)
    col = g.lerp(g.vec("RockColorA", (0.20, 0.17, 0.14)), g.vec("RockColorB", (0.34, 0.31, 0.28)), broad)
    slow = g.noise(g.add_(g.mul(p, g.const(0.7)), g.const(5.0)), levels=3, fn=grad)
    patches = g.mul(g.smooth(g.const(0.6), g.const(0.75), slow), g.const(0.6))
    col = g.lerp(col, g.vec("RockColorC", (0.45, 0.43, 0.40)), patches)
    fine = g.noise(g.mul(p, g.const(5.0)), levels=3, fn=grad)
    g.out(g.mul(col, g.add_(g.const(0.75), g.mul(fine, g.const(0.5)))), "MP_BaseColor")
    g.out(g.add_(g.const(0.82), g.mul(fine, g.const(0.15))), "MP_Roughness")
    g.out(g.const(0.25), "MP_Specular")
    # Relief: the gradient of two octaves of noise, in the rock's frame.
    def gradient(freq):
        n = g.node("VectorNoise", {"NoiseFunction": "VNF_GradientALU", "Quality": 1})
        g.c(g.mul(p, g.const(freq)), n, "World Position")
        return mask(g, n, r=True, gg=True, b=True)
    slope = g.add_(gradient(3.0), g.mul(gradient(11.0), g.const(0.5)))
    def to_world(e):
        t = g.node("Transform", {"TransformSourceType": "TRANSFORMSOURCE_Instance", "TransformType": "TRANSFORM_World"})
        g.c(e, t, ""); return t
    unit = g.node("Constant3Vector", {"Constant": {"R": 1.0, "G": 0.0, "B": 0.0, "A": 1.0}})
    n = g.custom(NORMAL, ["TG", "TX", "N", "Strength"], "CMOT_Float3", "asteroid normal")
    g.c(to_world(slope), n, "TG")
    g.c(to_world(unit), n, "TX")
    g.c(g.node("VertexNormalWS"), n, "N")
    g.c(g.scalar("NormalStrength", 0.28), n, "Strength")
    g.out(n, "MP_Normal")
    g.finish()


if __name__ == "__main__":
    build()
