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

M_AccretionDisk, a close pair's accretion disk and the stream that feeds it: on an engine plane in the orbital plane,
centred on the companion that takes the gas, its +X toward the star that gives it. Additive and unlit (hot gas, it
glows; and so it adds to the donor's far sprite behind it rather than hiding it). The disk from InnerFrac to DiskFrac of
the plane's half-width, hottest and bluest at its inner edge (the temperature falling as the radius to the three
quarters), orange and red outward, in spiral streaks sheared by its Keplerian turning; the stream leaves the inner
Lagrange point (L1Frac along +X), bent by the pair's turning toward +Y (the pair turns counter-clockwise seen from +Z),
and strikes the disk's rim in a hot spot.

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


ACCRETION = r"""
float2 q = LP.xy / 50.0;
float r = length(q);
float a = atan2(q.y, q.x);
float3 col = float3(0.0, 0.0, 0.0);
float inner = max(InnerFrac, 0.005);
// The disk.
if (r < DiskFrac * 1.08 && r > inner * 0.8)
{
    float temp = pow(inner / max(r, inner), 0.75);
    float spin = Time * 0.3 * pow(inner / max(r, inner), 1.5);
    float x = saturate((r - inner) / max(DiskFrac - inner, 1e-3));
    // (Graded across its width as it is seen: hot within, cooling outward; and bright enough to the rim to be seen.)
    float3 c = lerp(HotColor, CoolColor, smoothstep(0.0, 0.75, x));
    float2 sp = float2(x * 70.0, (a - spin) * 3.0);
    float n = SlonkDiskValue(sp + Seed) * 0.6 + SlonkDiskValue(sp * 3.1 - Seed) * 0.4;
    float edge = smoothstep(inner * 0.8, inner * 1.4, r) * (1.0 - smoothstep(DiskFrac * 0.8, DiskFrac * 1.08, r));
    col += c * (0.45 + 0.9 * (1.0 - x) + 3.0 * temp * temp) * (0.6 + 0.8 * n) * edge;
}
// The stream: a quadratic curve from L1 to the rim, ahead of the donor's line.
float2 P0 = float2(L1Frac, 0.0);
float2 P2 = DiskFrac * float2(cos(1.25), sin(1.25));
float2 P1 = float2(0.55 * L1Frac, 0.42 * L1Frac);
float best = 1e9, bestS = 0.0;
[loop] for (int i = 0; i <= 24; ++i)
{
    float s = i / 24.0;
    float2 P = (1.0 - s) * (1.0 - s) * P0 + 2.0 * (1.0 - s) * s * P1 + s * s * P2;
    float d2 = dot(q - P, q - P);
    if (d2 < best) { best = d2; bestS = s; }
}
float w = lerp(0.012, 0.03, bestS);
float flow = 0.7 + 0.3 * sin(bestS * 40.0 - Time * 3.0) * SlonkDiskValue(float2(bestS * 12.0 - Time * 0.4, Seed));
col += StreamColor * exp(-best / (w * w)) * flow * (0.8 + 0.6 * bestS);
// Where it strikes the disk.
col += HotColor * 4.0 * exp(-dot(q - P2, q - P2) / (0.035 * 0.035));
return col * Brightness;
"""


def build_accretion():
    name = "M_AccretionDisk"
    wipe(name)
    g = G(name, {"ShadingModel": "MSM_Unlit", "BlendMode": "BLEND_Additive", "TwoSided": True})
    fn = g.custom(FUNCS, [], "CMOT_Float1", "disk functions")
    e = g.custom(ACCRETION, ["Fn", "LP", "Time", "InnerFrac", "DiskFrac", "L1Frac", "Seed", "HotColor", "CoolColor", "StreamColor", "Brightness"], "CMOT_Float3", "accretion disk")
    g.c(fn, e, "Fn")
    g.c(g.node("LocalPosition", {"IncludedOffsets": "IncludeOffsets", "LocalOrigin": "Instance"}), e, "LP")
    g.c(g.mul(g.node("Time"), g.scalar("TimeRate", 1.0)), e, "Time")
    for n, v in (("InnerFrac", 0.03), ("DiskFrac", 0.62), ("L1Frac", 0.9), ("Seed", 0.0), ("Brightness", 30.0)):
        g.c(g.scalar(n, v), e, n)
    for n, rgb in (("HotColor", (0.75, 0.85, 1.0)), ("CoolColor", (1.0, 0.45, 0.2)), ("StreamColor", (1.0, 0.7, 0.45))):
        v = g.vec(n, rgb); m = mask(g, v, r=True, gg=True, b=True); g.c(m, e, n)
    g.out(e, "MP_EmissiveColor")
    g.finish()


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
    build_accretion()
