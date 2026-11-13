"""Builds M_GroundRock, the material of the rocks on the worlds' ground (GroundRocks.h, placed by TerrainScatter.h).

The rock meshes fill a unit box and are sized per instance, so the material lays the terrain's cliff texture set
triplanar in the rock's own frame in centimetres of rock (the unit-box position times the instance's size), the
same density on a pebble as on a boulder, offset per rock by its seed. Per-instance custom data, written by
SlonkScatter::ScatterChunk:
    0-2 tint (the terrain's rock colour where the rock lies: MountainColor, warmed in a canyon, varied per rock)
    3   snow cover (the ground's ice): white on the faces that look up
    4   seed
    5-7 size in metres along the mesh's x, y and z
    8   the canyon-wall bed the rock fell from (-1 for none), coloured with the terrain material's own hash so the
        two agree on the GPU
    9   how much of the bed's colour it wears
The normal is perturbed in the scaled rock frame and handed to a Local to World transform divided by the scale, so
it comes out right on a squashed slab. IceColor is set per planet by the terrain component.

Run with the editor open and no play session:  python3 tools/planet_materials/ground_rock_material.py
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
from uemat import *

NAME = "M_GroundRock"
TEX_FOLDER = "/Game/Slonk/Terrain"

CODE = r"""
float3 sz = max(float3(SX, SY, SZ), 0.01);
// Centimetres of rock in the rock's own frame (the unit box is a metre across at scale 1), shifted per rock.
float3 p = LP * sz + Seed * float3(371.0, 113.0, 257.0);
// The mesh normal in that scaled frame: the inverse transpose of a diagonal scale.
float3 n = normalize(LN / sz);
float3 w = pow(abs(n), 4.0); w /= (w.x + w.y + w.z);
const float tile = 220.0;
float2 uvX = p.zy / tile, uvY = p.xz / tile, uvZ = p.xy / tile;
float4 cX = Texture2DSample(CliffBC, CliffBCSampler, uvX), cY = Texture2DSample(CliffBC, CliffBCSampler, uvY), cZ = Texture2DSample(CliffBC, CliffBCSampler, uvZ);
float4 nX = Texture2DSample(CliffNR, CliffNRSampler, uvX), nY = Texture2DSample(CliffNR, CliffNRSampler, uvY), nZ = Texture2DSample(CliffNR, CliffNRSampler, uvZ);
float3 bc = cX.rgb * w.x + cY.rgb * w.y + cZ.rgb * w.z;
float4 nr = nX * w.x + nY * w.y + nZ * w.z;
// Whiteout blend of the three tangent-space normals (+Y is +V as Blender bakes them, as on the terrain).
float3 tX = float3(nX.rg * 2.0 - 1.0, 0.0); tX.y = -tX.y; tX.z = sqrt(saturate(1.0 - dot(tX.xy, tX.xy)));
float3 tY = float3(nY.rg * 2.0 - 1.0, 0.0); tY.y = -tY.y; tY.z = sqrt(saturate(1.0 - dot(tY.xy, tY.xy)));
float3 tZ = float3(nZ.rg * 2.0 - 1.0, 0.0); tZ.y = -tZ.y; tZ.z = sqrt(saturate(1.0 - dot(tZ.xy, tZ.xy)));
tX = float3(tX.xy + n.zy, abs(tX.z) * n.x);
tY = float3(tY.xy + n.xz, abs(tY.z) * n.y);
tZ = float3(tZ.xy + n.xy, abs(tZ.z) * n.z);
float3 nl = normalize(tX.zyx * w.x + tY.xzy * w.y + tZ.xyz * w.z);
// The texture only modulates: the tint carries the colour, the set's own mean (its last mip) divided out.
float3 mean = max(Texture2DSampleLevel(CliffBC, CliffBCSampler, float2(0.5, 0.5), 11.0).rgb, 0.02);
float3 detail = lerp(1.0, min(bc / mean, 1.6), 0.8);
float3 col = Tint;
// A rock off a canyon wall is a piece of a bed above it, coloured as the terrain material colours that bed.
if (Layer >= 0.0 && OnWall > 0.0)
{
    float lh = frac(sin(Layer * 12.9898 + 1.7) * 43758.5453);
    float3 bed = lh < 0.3 ? float3(0.74, 0.66, 0.54) : (lh < 0.6 ? float3(0.60, 0.33, 0.22) : (lh < 0.82 ? float3(0.58, 0.44, 0.36) : float3(0.40, 0.38, 0.33)));
    bed *= 0.9 + 0.2 * frac(sin(Layer * 7.31 + 3.1) * 23758.5453);
    col = lerp(col, bed * 0.8 * (0.82 + 0.3 * frac(Seed * 7.77)), OnWall);
}
col *= detail;
// Snow lies on the faces that look up (the rock's z is near the ground's up), patchy with the texture.
float snow = Snow * smoothstep(0.35, 0.75, n.z + 0.15 * (bc.r / mean.r - 1.0));
col = lerp(col, IceColor, snow);
// Out in the scaled frame divided by the scale, for the Local to World transform that follows to undo.
Nrm = lerp(nl, n, snow * 0.7) / sz;
Rough = lerp(nr.a, 0.55, snow);
AO = lerp(nr.b, 1.0, snow * 0.5);
return col;
"""

INPUTS = ["LP", "LN", "Tint", "Snow", "Seed", "SX", "SY", "SZ", "Layer", "OnWall", "IceColor", "CliffBC", "CliffNR"]


def build():
    wipe(NAME)
    g = G(NAME, {"ShadingModel": "MSM_DefaultLit", "BlendMode": "BLEND_Opaque", "bTangentSpaceNormal": False, "TwoSided": False,
                 "bUsedWithInstancedStaticMeshes": True})
    e = g.custom(CODE, INPUTS, "CMOT_Float3", "ground rock", extra_outputs=(("Nrm", "CMOT_Float3"), ("Rough", "CMOT_Float1"), ("AO", "CMOT_Float1")))
    # The mesh's own position and normal exist in the vertex shader only: interpolated across to the pixels.
    for cls, pin in (("PreSkinnedPosition", "LP"), ("PreSkinnedNormal", "LN")):
        vi = g.node("VertexInterpolator"); g.c(g.node(cls), vi, "VS"); g.c(vi, e, pin)
    g.c(g.node("PerInstanceCustomData3Vector", {"DataIndex": 0, "ConstDefaultValue": {"R": 0.42, "G": 0.40, "B": 0.38, "A": 1}}), e, "Tint")
    for pin, idx, default in [("Snow", 3, 0.0), ("Seed", 4, 0.0), ("SX", 5, 1.0), ("SY", 6, 1.0), ("SZ", 7, 1.0), ("Layer", 8, -1.0), ("OnWall", 9, 0.0)]:
        g.c(g.node("PerInstanceCustomData", {"DataIndex": idx, "ConstDefaultValue": default}), e, pin)
    g.c(g.vec("IceColor", (0.90, 0.93, 0.97)), e, "IceColor")
    for kind in ("BC", "NR"):
        asset = f"T_Cliff_{kind}"
        t = g.node("TextureObjectParameter", {"ParameterName": f"Cliff{kind}", "Texture": {"refPath": f"{TEX_FOLDER}/{asset}.{asset}"}})
        g.c(t, e, f"Cliff{kind}")
    # The normal from the rock's frame to the world's, then unit length again.
    tr = g.node("Transform", {"TransformSourceType": "TRANSFORMSOURCE_Local", "TransformType": "TRANSFORM_World"})
    g.c(e, tr, "", "Nrm")
    nn = g.un("Normalize", tr)
    g.out(e, "MP_BaseColor")
    g.out(nn, "MP_Normal")
    g.out(e, "MP_Roughness", "Rough")
    g.out(e, "MP_AmbientOcclusion", "AO")
    g.finish()


if __name__ == "__main__":
    build()
