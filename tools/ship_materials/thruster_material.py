"""M_Thruster and M_ThrusterFlare: the reaction control plumes.

M_Thruster goes on the instanced engine cone (apex +Z = the nozzle, base = the far end). It is additive and unlit: a
hot white throat fading into the plume colour and a cooler tail, the sides turning transparent where they face away
from the camera (so the cone reads as a soft volume rather than a solid spike), and value-noise filaments round the
plume scrolling away from the nozzle. M_ThrusterFlare goes on the small instanced sphere at each nozzle mouth: a
view-facing glow blob that gives the jet its bright ignition point from any angle.

Per-instance custom data: 0 activation, 1 ignition flash (1 right after lighting, decaying), 2 per-nozzle seed,
3 core flag (each nozzle has a wide faint envelope cone and a narrow bright core cone inside it).
Being additive neither writes depth, so under the relativistic warp view a plume over the sky is treated as sky;
the thrusters stay quiet at warp apart from translation, so that is rarely seen.

Run with the editor open and no play session: python3 tools/ship_materials/thruster_material.py
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
from uemat import *

PLUME_CODE = """
float t = saturate((LP.z + 50.0) * 0.01);                 // 1 at the nozzle, 0 at the far end
float ang = atan2(LP.y, LP.x);                             // round the plume, -pi..pi
// Filaments: 2D value noise in (angle, length), 16 cells round so it wraps, scrolling away from the nozzle.
float2 p = float2((ang + 3.14159265) * (16.0 / 6.2831853), (1.0 - t) * 3.0 - T * 7.0 + Seed * 37.0);
float2 i = floor(p); float2 f = frac(p); f = f * f * (3.0 - 2.0 * f);
float x0 = fmod(i.x, 16.0), x1 = fmod(i.x + 1.0, 16.0);
float h00 = frac(sin(dot(float2(x0, i.y), float2(127.1, 311.7))) * 43758.5453);
float h10 = frac(sin(dot(float2(x1, i.y), float2(127.1, 311.7))) * 43758.5453);
float h01 = frac(sin(dot(float2(x0, i.y + 1.0), float2(127.1, 311.7))) * 43758.5453);
float h11 = frac(sin(dot(float2(x1, i.y + 1.0), float2(127.1, 311.7))) * 43758.5453);
float n = lerp(lerp(h00, h10, f.x), lerp(h01, h11, f.x), f.y);
// A finer, faster layer along the axis only: the shimmer of the jet.
float s = 0.5 + 0.5 * sin((1.0 - t) * 40.0 - T * 55.0 + Seed * 11.0);
// Two cones per nozzle: the wide faint envelope (Core 0) carries the filaments and a cool tail; the narrow bright
// core (Core 1) inside it is whiter, steadier and shorter. Both fade where their sides turn from the camera and
// feather out at the far end so no base ring ever shows.
float endfade = smoothstep(0.0, 0.3, t);
float edge = pow(saturate(abs(dot(N, V))), lerp(2.0, 1.2, Core));
float body = pow(t, lerp(1.6, 1.2, Core));
float fil = lerp(lerp(0.35, 1.0, n) * lerp(0.75, 1.0, s), lerp(0.8, 1.0, s), Core);
float prof = body * fil * edge * endfade * lerp(1.0, 2.6, Core);
float3 col = lerp(Color, float3(1.0, 1.0, 1.0), Core * 0.7 + 0.3 * pow(t, 3.0));
col = lerp(col, Color * float3(0.55, 0.7, 1.0), (1.0 - t) * (1.0 - t) * (1.0 - Core));
return col * Intensity * prof * A * (1.0 + 1.5 * Flash);
"""

FLARE_CODE = """
float d = saturate(dot(N, V));
float g = pow(d, 6.0);
float3 col = lerp(Color, float3(1.0, 1.0, 1.0), 0.7);
return col * Intensity * g * A * (1.0 + 2.5 * Flash);
"""


def plume():
    wipe("M_Thruster")
    g = G("M_Thruster", {"ShadingModel": "MSM_Unlit", "BlendMode": "BLEND_Additive", "TwoSided": True, "bUsedWithInstancedStaticMeshes": True})
    e = g.custom(PLUME_CODE, ["LP", "N", "V", "A", "Flash", "Seed", "Core", "Color", "Intensity", "T"], "CMOT_Float3", "rcs plume")
    lp = g.node("PreSkinnedPosition"); vi = g.node("VertexInterpolator"); g.c(lp, vi, "VS"); g.c(vi, e, "LP")
    g.c(g.node("VertexNormalWS"), e, "N")
    g.c(g.node("CameraVectorWS"), e, "V")
    g.c(g.node("PerInstanceCustomData", {"DataIndex": 0, "ConstDefaultValue": 1.0}), e, "A")
    g.c(g.node("PerInstanceCustomData", {"DataIndex": 1, "ConstDefaultValue": 0.0}), e, "Flash")
    g.c(g.node("PerInstanceCustomData", {"DataIndex": 2, "ConstDefaultValue": 0.0}), e, "Seed")
    g.c(g.node("PerInstanceCustomData", {"DataIndex": 3, "ConstDefaultValue": 0.0}), e, "Core")
    g.c(g.vec("Color", (0.85, 0.92, 1.0)), e, "Color")
    g.c(g.scalar("Intensity", 30.0), e, "Intensity")
    g.c(g.node("Time"), e, "T")
    g.out(e, "MP_EmissiveColor")
    g.finish(); return g


def flare():
    wipe("M_ThrusterFlare")
    g = G("M_ThrusterFlare", {"ShadingModel": "MSM_Unlit", "BlendMode": "BLEND_Additive", "TwoSided": False, "bUsedWithInstancedStaticMeshes": True})
    e = g.custom(FLARE_CODE, ["N", "V", "A", "Flash", "Color", "Intensity"], "CMOT_Float3", "rcs flare")
    g.c(g.node("VertexNormalWS"), e, "N")
    g.c(g.node("CameraVectorWS"), e, "V")
    g.c(g.node("PerInstanceCustomData", {"DataIndex": 0, "ConstDefaultValue": 1.0}), e, "A")
    g.c(g.node("PerInstanceCustomData", {"DataIndex": 1, "ConstDefaultValue": 0.0}), e, "Flash")
    g.c(g.vec("Color", (0.85, 0.92, 1.0)), e, "Color")
    g.c(g.scalar("Intensity", 45.0), e, "Intensity")
    g.out(e, "MP_EmissiveColor")
    g.finish(); return g


if __name__ == "__main__":
    which = sys.argv[1:] or ["plume", "flare"]
    if "plume" in which: plume()
    if "flare" in which: flare()
