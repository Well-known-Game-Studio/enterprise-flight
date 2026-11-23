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
    9   the distance it fades out by, cm (0: never): over the last fifth of it the plant is dithered away, so it is
        gone before the coarser chunk without it takes over and none pops in or out. The distance is from ViewPos,
        the camera as the terrain component sets it each frame, so the shadow passes fade it the same.
Two-sided foliage shading lets the sun through the leaves. The wind bends each plant from its root, along the lines
of latitude (about the planet's axis, the instanced set's own up) so a stand sways one way, in slow gusts at its own phase, and the leaves and needles flutter on top;
UPlanetTerrainComponent stops it beyond PlantWindDistanceCm. IceColor is set per planet by the terrain component.

A strange world's species (FloraTypes.h) are drawn by instances of this material, one a species, carrying its look
(SlonkFlora::LookOf); the defaults are the Earth's, so every Earth plant draws as it always has:
    StemColor      its stems' and stalks' colour (the Earth's bark)
    Gloss          0 a leaf's or a fungus's matt skin, to 1 a crystal's faces
    Translucence   how much light comes through it against a leaf's (1): under it flesh and chitin, over it a membrane
    GlowColor, Glow, GlowForm, GlowBody   its glow at night: the colour, how bright (0: none), and how much of each
                   pattern it is laid in (GlowForm's R spots, G ribs, B rims; GlowBody all of it)
The strange plans' meshes are solid (no cards), and their foliage's UV is a place on each part: x round it or across
it, y from its root to its rim. The glow's marks are laid in that UV (SlonkFlora::Uv: spots on a lattice of 6 by 4,
eight ribs along x, the rim from 0.82 of y), lit by night and pale by day; night is where the sun (SunDirection, the
world's, set by the terrain component as the motes' is) stands under the plant's own horizon, so a tidally locked
world's forest glows along its terminator for ever. Each plant breathes at a rate of its own, a whole number of
breaths an hour so the clock's hourly wrap is not seen. Far off, where a mark is smaller than a pixel, the plant
glows as its marks would on the whole.

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
// Fading out with distance: dithered against interleaved gradient noise that moves each frame, which the temporal
// upscaler smooths into a fade.
if (FadeFar > 0.0)
{
    float fade = saturate((FadeFar - length(WP - ViewPos)) / (0.2 * FadeFar));
    float2 px = Parameters.SvPosition.xy + 5.588238 * (float)(View.StateFrameIndexMod8);
    float ign = frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));
    mask *= fade > ign ? 1.0 : 0.0;
}
Mask = mask;
float3 bark = StemColor * (0.8 + 0.4 * frac(Seed * 13.1));
// Tips and outer leaves catch more light than the inside of a crown; a dry plant yellows.
float3 leaf = Tint * (0.8 + 0.4 * rnd) * lerp(0.75, 1.15, sway);
leaf = lerp(leaf, leaf * float3(1.15, 1.0, 0.55), Dry * 0.5);
float3 col = lerp(bark, leaf, part);
// A strange plant's glow: marks laid in its own UV (spots, ribs, its rims, or all of it), lit at night and breathing,
// pale by day. Far off, where a mark is smaller than a pixel, it glows as the marks would on the whole.
float3 emis = float3(0.0, 0.0, 0.0);
[branch] if (Glow > 0.0)
{
    float2 cell = float2(UV.x * 6.0 + (frac(floor(UV.y * 4.0) * 0.5) > 0.25 ? 0.5 : 0.0), UV.y * 4.0);
    float spots = 1.0 - smoothstep(0.2, 0.28, length(frac(cell) - 0.5));
    float ribs = 1.0 - smoothstep(0.13, 0.22, abs(frac(UV.x * 8.0) - 0.5) * 2.0);
    float rim = smoothstep(0.82, 0.94, UV.y);
    float marks = saturate(GlowForm.x * spots + GlowForm.y * ribs + GlowForm.z * rim + GlowBody);
    float whole = saturate(GlowForm.x * 0.22 + GlowForm.y * 0.19 + GlowForm.z * 0.14 + GlowBody);
    float tall = max(SZ, 0.05) * 100.0;
    marks = lerp(marks, whole, saturate((length(WP - ViewPos) / tall - 40.0) / 40.0)) * part;
    float night = 1.0 - smoothstep(-0.06, 0.1, dot(normalize(PlantUp), normalize(SunDirection)));
    float rate = (300.0 + floor(frac(Seed * 3.3) * 420.0)) / 3600.0;
    float breath = 1.0 - 0.55 * (0.5 + 0.5 * sin(6.2831853 * frac(Clock * rate + Seed * 2.1)));
    col = lerp(col, GlowColor * 0.35 + 0.4, marks * 0.35 * (1.0 - night));
    emis = GlowColor * (Glow * night * marks * breath);
}
Emis = emis;
float snow = Snow * saturate(dot(normalize(N), normalize(PlantUp)) * 1.6 - 0.3);
col = lerp(col, IceColor, snow);
Rough = lerp(lerp(lerp(0.88, 0.62, part), 0.12, Gloss), 0.55, snow);
Sub = leaf * 0.55 * part * (1.0 - snow) * Translucence;
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
    fade_far = g.node("PerInstanceCustomData", {"DataIndex": 9, "ConstDefaultValue": 0.0})
    ice = g.vec("IceColor", (0.90, 0.93, 0.97))
    view_pos = g.vec("ViewPos", (0.0, 0.0, 0.0))
    # A species' look: the Earth's by default, so the shared instance draws the Earth's plants as they always were.
    stem = g.vec("StemColor", (0.24, 0.18, 0.13))
    gloss = g.scalar("Gloss", 0.0)
    translucence = g.scalar("Translucence", 1.0)
    glow_color = g.vec("GlowColor", (0.0, 0.0, 0.0))
    glow = g.scalar("Glow", 0.0)
    glow_form = g.vec("GlowForm", (0.0, 0.0, 0.0))
    glow_body = g.scalar("GlowBody", 0.0)
    sun_dir = g.vec("SunDirection", (0.0, 0.0, 1.0))
    # The glow's clock wraps each hour; every breath is a whole number of cycles an hour, so the wrap is not seen.
    clock = g.node("Time", {"bOverride_Period": True, "Period": 3600.0})
    e = g.custom(CODE, ["VC", "VCA", "UV", "N", "PlantUp", "Tint", "Snow", "Seed", "Dry", "IceColor", "FadeFar", "WP", "ViewPos",
                        "StemColor", "Gloss", "Translucence", "GlowColor", "Glow", "GlowForm", "GlowBody", "SunDirection", "SZ", "Clock"], "CMOT_Float3", "foliage",
                 extra_outputs=(("Mask", "CMOT_Float1"), ("Rough", "CMOT_Float1"), ("Sub", "CMOT_Float3"), ("Emis", "CMOT_Float3")))
    for pin, src in [("VC", vc), ("UV", g.texcoord(0)), ("N", n), ("PlantUp", up), ("Tint", tint), ("Snow", snow), ("Seed", seed), ("Dry", dry), ("IceColor", ice),
                     ("FadeFar", fade_far), ("WP", g.node("WorldPosition")), ("ViewPos", view_pos), ("StemColor", stem), ("Gloss", gloss), ("Translucence", translucence),
                     ("GlowColor", glow_color), ("Glow", glow), ("GlowForm", glow_form), ("GlowBody", glow_body), ("SunDirection", sun_dir), ("SZ", sz), ("Clock", clock)]:
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
    g.out(e, "MP_EmissiveColor", "Emis")
    g.out(w, "MP_WorldPositionOffset")
    g.finish()


if __name__ == "__main__":
    build()
