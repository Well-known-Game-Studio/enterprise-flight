"""Builds the materials of a young star's surroundings (AStarBody's Disk and Jets, StarBody.cpp):

M_StarDisk, the protoplanetary disk: on an engine plane laid in the star's equator, through its centre. Translucent and
unlit, two-sided: dust and gas from the inner edge (InnerFrac of the plane's half-width) to the outer, thinning
outward, parted by the dark gaps young planets sweep (GapCount, at radii drawn from the Seed), clumped and drawn out
into spiral streaks that turn faster the nearer the star (each radius at its own Keplerian rate, so the pattern shears
as it goes round); lit by the star, the brighter the nearer (DustColor tinted by StarColor, Brightness), a hot rim at
the inner edge.

M_StarJet, a polar jet: on an engine cone (its point at the star, its base out along the axis), additive and unlit:
a narrow glowing column, brightest along its axis, with knots of shocked gas streaming out along it (JetSpeed), fading
toward the far end. JetColor.

Time is the material's own (real seconds times TimeRate).

Run with the editor open and no play session:  python3 tools/planet_materials/star_disk_material.py
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
from uemat import *

FUNCS = r"""
return 0.0;
}
#ifndef SLONK_DISK_FUNCS
#define SLONK_DISK_FUNCS
float SlonkDiskHash(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
float SlonkDiskValue(float2 p)
{
    float2 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(SlonkDiskHash(i), SlonkDiskHash(i + float2(1, 0)), f.x), lerp(SlonkDiskHash(i + float2(0, 1)), SlonkDiskHash(i + float2(1, 1)), f.x), f.y);
}
float SlonkDiskFuncsEnd1(FMaterialPixelParameters Parameters)
{
#else
float SlonkDiskFuncsEnd2(FMaterialPixelParameters Parameters)
{
#endif
    return 0.0;
"""

DISK = r"""
float2 q = (UV - 0.5) * 2.0;
float r = length(q);
float a = atan2(q.y, q.x);
float inner = max(InnerFrac, 0.01);
if (r > 1.0 || r < inner * 0.97) { Opacity = 0.0; return float3(0.0, 0.0, 0.0); }
float x = (r - inner) / (1.0 - inner);      // 0 at the inner edge, 1 at the outer
float dr = (r - inner) / inner;             // and out from the inner edge in inner radii
// Thinning outward, soft at both edges.
// (Just behind the inner wall the disk lies in its shadow, and comes out of it a little farther on.)
float dens = pow(inner / max(r, inner), 0.9) * smoothstep(0.05, 0.6, dr) * (1.0 - smoothstep(0.8, 1.0, x));
// Gaps, where young planets have swept their orbits clear.
int gaps = (int)clamp(GapCount, 0.0, 5.0);
for (int i = 0; i < 5; ++i)
{
    if (i >= gaps) { break; }
    float gr = 0.15 + 0.7 * frac(sin((i + 1.0) * 12.9898 + Seed * 7.13) * 43758.5453);
    float gw = 0.018 + 0.03 * frac(sin((i + 1.0) * 78.233 + Seed * 3.1) * 43758.5453);
    // (Dust piles up at a gap's outer edge, in a brighter ring.)
    dens *= (1.0 - 0.94 * exp(-pow((x - gr) / gw, 2.0))) * (1.0 + 0.7 * exp(-pow((x - gr - 2.2 * gw) / gw, 2.0)));
}
// Fine concentric ringlets, and faint clumps drawn out along the orbits, turning faster the nearer the star (each
// radius at its own Keplerian rate, so they shear as they go).
float ringlets = 0.9 + 0.1 * sin(x * 140.0 + 14.0 * SlonkDiskValue(float2(x * 7.0, Seed))) * SlonkDiskValue(float2(x * 23.0, Seed + 5.0));
float spin = Time * 0.05 * pow(inner / max(r, inner), 1.5);
// (Fine across the radius, long around the orbit: at the radius r an angle a is r*a along it.)
float2 sp = float2(x * 90.0, (a - spin) * 3.0);
float n = SlonkDiskValue(sp + Seed) * 0.6 + SlonkDiskValue(sp * 3.0 - Seed) * 0.4;
dens *= ringlets * (0.8 + 0.4 * n);
// Lit by the star: falling off with the square of the distance (softened), and a hot rim at the inner edge.
float lit = 0.25 + 0.75 / (1.0 + 12.0 * x * x);
// The inner wall, where the star heats the dust to burning: narrow, and hotter in colour than the disk.
float rim = smoothstep(-0.03, 0.02, dr) * exp(-max(dr, 0.0) / 0.12);
float3 col = StarColor * Brightness * (DustColor * lit * dens + lerp(DustColor, float3(1.0, 0.92, 0.8), 0.6) * 3.0 * rim);
// (The inner rim has its own opacity: a translucent surface shows its glow only where it is opaque.)
Opacity = saturate(dens * 0.85 + rim * 0.9);
return col;
"""

JET = r"""
// The cone's length from its point (at the star) to its base; and how near its axis.
float along = saturate(H);
float rad = saturate(length(Side));
float core = exp(-rad * rad * 6.0) + 0.3 * exp(-rad * rad * 1.5);
// Knots, streaming out.
float knots = 0.55 + 0.45 * pow(saturate(sin(along * 40.0 - Time * JetSpeed)), 6.0);
float fade = (1.0 - along) * smoothstep(0.0, 0.05, along);
return JetColor * Brightness * core * knots * fade;
"""


def build_disk():
    name = "M_StarDisk"
    wipe(name)
    g = G(name, {"ShadingModel": "MSM_Unlit", "BlendMode": "BLEND_Translucent", "TwoSided": True})
    fn = g.custom(FUNCS, [], "CMOT_Float1", "disk functions")
    e = g.custom(DISK, ["Fn", "UV", "Time", "InnerFrac", "GapCount", "Seed", "DustColor", "StarColor", "Brightness"], "CMOT_Float3", "protoplanetary disk",
                 extra_outputs=(("Opacity", "CMOT_Float1"),))
    g.c(fn, e, "Fn")
    g.c(g.texcoord(0), e, "UV")
    g.c(g.mul(g.node("Time"), g.scalar("TimeRate", 1.0)), e, "Time")
    for n, v in (("InnerFrac", 0.04), ("GapCount", 3.0), ("Seed", 0.0), ("Brightness", 28.0)):
        g.c(g.scalar(n, v), e, n)
    for n, rgb in (("DustColor", (0.85, 0.62, 0.45)), ("StarColor", (1.0, 0.8, 0.6))):
        v = g.vec(n, rgb); m = mask(g, v, r=True, gg=True, b=True); g.c(m, e, n)
    g.out(e, "MP_EmissiveColor")
    g.out(e, "MP_Opacity", "Opacity")
    g.finish()


def build_jet():
    name = "M_StarJet"
    wipe(name)
    g = G(name, {"ShadingModel": "MSM_Unlit", "BlendMode": "BLEND_Additive", "TwoSided": True})
    # The engine cone: 100 cm tall, its point at +50 z, its base radius 50 at -50 z. Turned in the star so its point
    # is at the centre: H runs 0 at the point to 1 at the base, Side is across the axis in the cone's base radii at
    # that height.
    lp = g.node("LocalPosition", {"IncludedOffsets": "IncludeOffsets", "LocalOrigin": "Instance"})
    e = g.custom(JET, ["H", "Side", "Time", "JetColor", "Brightness", "JetSpeed"], "CMOT_Float3", "jet")
    hc = g.custom("return saturate((50.0 - LP.z) / 100.0);", ["LP"], "CMOT_Float1", "jet height")
    g.c(lp, hc, "LP")
    sc = g.custom("float h = saturate((50.0 - LP.z) / 100.0); return LP.xy / max(50.0 * h, 1.0);", ["LP"], "CMOT_Float2", "jet across")
    g.c(lp, sc, "LP")
    g.c(hc, e, "H")
    g.c(sc, e, "Side")
    g.c(g.mul(g.node("Time"), g.scalar("TimeRate", 1.0)), e, "Time")
    v = g.vec("JetColor", (0.55, 0.7, 1.0)); m = mask(g, v, r=True, gg=True, b=True); g.c(m, e, "JetColor")
    g.c(g.scalar("Brightness", 60.0), e, "Brightness")
    g.c(g.scalar("JetSpeed", 2.0), e, "JetSpeed")
    g.out(e, "MP_EmissiveColor")
    g.finish()


if __name__ == "__main__":
    build_disk()
    build_jet()
