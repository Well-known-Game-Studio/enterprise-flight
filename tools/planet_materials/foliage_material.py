"""Builds M_Foliage, the material of the plants on the worlds' ground (PlantMeshes.h, placed by TerrainScatter.h).

The meshes carry their parts in the vertex colour: R bark (0) or foliage (1), G how far a vertex sways (0 at the root,
1 at the tips), B a random value per blade, tier or leaf cluster, A the surface (0 solid, 0.4 a card of broad leaves,
1 a card of needles). The cards are masked from their UVs, no textures needed: seven leaves round a card's middle,
turned by its random value, or fine strands of needles thinning toward the tips. Per-instance custom data, written by
SlonkScatter::ScatterChunk:
    0-2 the foliage tint (the terrain's wet ground colour where the plant stands, browned toward its dry one)
    3   snow (the ground's ice): white on the faces that look up
    4   seed
    5-7 size in metres (7 is the height, which scales the sway)
    8   dryness: a parched plant yellows
Two-sided foliage shading lets the sun through the leaves. The wind bends each plant from its root, along the lines
of latitude (about the planet's axis, the instanced set's own up) so a stand sways one way, in slow gusts at its own phase, and the leaves and needles flutter on top;
UPlanetTerrainComponent stops it beyond PlantWindDistanceCm. IceColor is set per planet by the terrain component.

Run with the editor open and no play session:  python3 tools/planet_materials/foliage_material.py
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
from uemat import *

NAME = "M_Foliage"

CODE = r"""
float part = VC.r; float sway = VC.g; float rnd = VC.b; float kind = VCA;
float mask = 1.0;
if (kind > 0.2 && kind < 0.85)
{
    // A card of broad leaves: one leaf in the middle and six round it, each an ellipse pointing outward.
    mask = 0.0;
    [unroll] for (int i = 0; i < 7; i++)
    {
        float a = ((float)i / 7.0 + rnd * 0.37) * 6.2831853;
        float2 c = float2(0.5, 0.5) + float2(cos(a), sin(a)) * (i == 0 ? 0.0 : 0.27);
        float2 d = UV - c;
        float b = a + 0.4 * (rnd - 0.5);
        float2 r = float2(dot(d, float2(cos(b), sin(b))), dot(d, float2(-sin(b), cos(b))));
        mask = max(mask, 1.0 - (r.x * r.x) / 0.05 - (r.y * r.y) / 0.012);
    }
}
else if (kind >= 0.85)
{
    // A card of needles: fine strands out from the stem, a little wavy, thinning toward the tips and the sides.
    float strands = abs(frac(UV.x * 17.0 + sin(UV.y * 9.0 + rnd * 20.0) * 0.18) - 0.5) * 2.0;
    mask = (strands < 0.6 * (1.0 - UV.y * 0.55) ? 1.0 : 0.0) * (abs(UV.x - 0.5) < 0.5 - UV.y * 0.12 ? 1.0 : 0.0);
}
Mask = mask;
float3 bark = float3(0.24, 0.18, 0.13) * (0.8 + 0.4 * frac(Seed * 13.1));
// Tips and outer leaves catch more light than the inside of a crown; a dry plant yellows.
float3 leaf = Tint * (0.8 + 0.4 * rnd) * lerp(0.75, 1.15, sway);
leaf = lerp(leaf, leaf * float3(1.15, 1.0, 0.55), Dry * 0.5);
float3 col = lerp(bark, leaf, part);
float snow = Snow * saturate(dot(normalize(N), normalize(PlantUp)) * 1.6 - 0.3);
col = lerp(col, IceColor, snow);
Rough = lerp(lerp(0.88, 0.62, part), 0.55, snow);
Sub = leaf * 0.55 * part * (1.0 - snow);
return col;
"""

WPO_CODE = r"""
// The plant bends from its root with the wind, along the lines of latitude so a stand sways one way, in slow gusts at
// its own phase; the leaves and needles flutter about their place on top. Axis is the planet's: the instanced set
// sits unrotated on its chunk, in the planet's frame, so the set's own up is the planet's spin axis as it turns.
float3 up = normalize(PlantUp);
float3 across = cross(up, normalize(Axis));
float3 wind = dot(across, across) > 1e-4 ? normalize(across) : normalize(cross(up, float3(1.0, 0.0, 0.0)) + 1e-5);
float h = max(SZ, 0.05) * 100.0;
float ph = T * 1.1 + Seed * 6.2831853;
float gust = sin(ph) * 0.6 + sin(ph * 2.3 + 1.7) * 0.25 + 0.35;
float3 off = wind * (VC.g * VC.g * h * 0.025 * gust);
if (VCA > 0.2) { off += normalize(N) * (sin(T * 9.0 + VC.b * 40.0 + Seed * 17.0) * 1.2 * VC.g); }
return off;
"""


def build():
    wipe(NAME)
    g = G(NAME, {"ShadingModel": "MSM_TwoSidedFoliage", "BlendMode": "BLEND_Masked", "TwoSided": True, "OpacityMaskClipValue": 0.33,
                 "bUsedWithInstancedStaticMeshes": True})
    vc = g.node("VertexColor")
    n = g.node("VertexNormalWS")
    # The plant's own up in the world: the instance's local z.
    upl = g.node("Constant3Vector", {"Constant": {"R": 0.0, "G": 0.0, "B": 1.0, "A": 0.0}})
    up = g.node("Transform", {"TransformSourceType": "TRANSFORMSOURCE_Local", "TransformType": "TRANSFORM_World"})
    g.c(upl, up, "")
    tint = g.node("PerInstanceCustomData3Vector", {"DataIndex": 0, "ConstDefaultValue": {"R": 0.25, "G": 0.34, "B": 0.14, "A": 1}})
    snow = g.node("PerInstanceCustomData", {"DataIndex": 3, "ConstDefaultValue": 0.0})
    seed = g.node("PerInstanceCustomData", {"DataIndex": 4, "ConstDefaultValue": 0.0})
    sz = g.node("PerInstanceCustomData", {"DataIndex": 7, "ConstDefaultValue": 1.0})
    dry = g.node("PerInstanceCustomData", {"DataIndex": 8, "ConstDefaultValue": 0.0})
    ice = g.vec("IceColor", (0.90, 0.93, 0.97))
    e = g.custom(CODE, ["VC", "VCA", "UV", "N", "PlantUp", "Tint", "Snow", "Seed", "Dry", "IceColor"], "CMOT_Float3", "foliage",
                 extra_outputs=(("Mask", "CMOT_Float1"), ("Rough", "CMOT_Float1"), ("Sub", "CMOT_Float3")))
    for pin, src in [("VC", vc), ("UV", g.texcoord(0)), ("N", n), ("PlantUp", up), ("Tint", tint), ("Snow", snow), ("Seed", seed), ("Dry", dry), ("IceColor", ice)]:
        g.c(src, e, pin)
    # The colour node's main output is RGB; the surface kind is its alpha pin.
    g.c(vc, e, "VCA", "A")
    w = g.custom(WPO_CODE, ["VC", "VCA", "N", "PlantUp", "Axis", "Seed", "SZ", "T"], "CMOT_Float3", "wind")
    for pin, src in [("VC", vc), ("N", n), ("PlantUp", up), ("Axis", g.node("ObjectOrientation")), ("Seed", seed), ("SZ", sz), ("T", g.node("Time"))]:
        g.c(src, w, pin)
    g.c(vc, w, "VCA", "A")
    g.out(e, "MP_BaseColor")
    g.out(e, "MP_OpacityMask", "Mask")
    g.out(e, "MP_Roughness", "Rough")
    g.out(e, "MP_SubsurfaceColor", "Sub")
    g.out(w, "MP_WorldPositionOffset")
    g.finish()


if __name__ == "__main__":
    build()
