"""Builds M_Stars, the ship's star sphere: a direction-hashed star field (the same STAR_FN the warp post-process
synthesises outside the frame) times a Fade scalar the game mode drives from daylight, so the stars dim into a
daylit sky instead of switching off.

Run with the editor open and no play session:  python3 tools/ship_materials/stars_material.py
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
from uemat import *

STAR_FN = """
float2 suv = float2(atan2(d.y, d.x) * 0.15915494 + 0.5, acos(clamp(d.z, -1.0, 1.0)) * 0.31830989);
float2 q = suv * float2(600.0, 300.0);
float2 cell = floor(q);
float3 p3 = frac(float3(cell.xyx) * float3(0.1031, 0.1030, 0.0973));
p3 += dot(p3, p3.yzx + 33.33);
float2 h = frac((p3.xx + p3.yz) * p3.zy);
float pres = saturate((h.x - 0.985) * 66.6667);
float dotf = saturate(1.0 - 2.5 * length(frac(q) - 0.5));
dotf *= dotf;
float3 scol = pres * dotf * (0.3 + h.y) * float3(10.8, 11.4, 12.0);
"""

CODE = "float3 d = normalize(-CamVec);" + STAR_FN + "return scol * Fade;"


def build():
    wipe("M_Stars")
    g = G("M_Stars", {"ShadingModel": "MSM_Unlit", "BlendMode": "BLEND_Opaque", "TwoSided": True})
    e = g.custom(CODE, ["CamVec", "Fade"], "CMOT_Float3", "star field by direction, faded by daylight")
    g.c(g.node("CameraVectorWS"), e, "CamVec")
    g.c(g.scalar("Fade", 1.0), e, "Fade")
    g.out(e, "MP_EmissiveColor")
    g.finish()


if __name__ == "__main__":
    build()
