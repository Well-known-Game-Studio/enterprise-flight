"""Builds M_StarGlow, the light round a star (AStarBody's Corona: an engine plane turned to face the camera each frame,
through the star's centre, so the front of the photosphere hides what lies behind the disc). Unlit and additive.

Two looks, crossed by Near (AStarBody sets it from the camera's distance in the star's radii):
  - far (Near 0): the star as an eye sees one across a system: a hot core of halo and fine rays of uneven length,
    turning slowly;
  - near (Near 1): what a ship beside it sees: the chromosphere, a thin red rim at the limb fringed with spicules;
    prominences standing on the limb, arches and hedgerows of glowing gas that rise, stream and fade, now and then
    one lifting off; and the corona, falling off steeply from the limb, combed into streamers that drift.

Everything round the limb is laid out by its angle from the star's own north (StarUp), not the camera's, so it stays
where it is on the star as the ship moves round it. Radii are the star's (StarRadiusCm): 1 is the limb, 1/DiscFraction
the plane's edge, where everything has faded to nothing.

Parameters: CoronaColor, StarColor, Intensity, Falloff, DiscFraction (AStarBody sets these), Near, StarUp,
StarRadiusCm, Activity, Seed (likewise, for the near look), CamRadii (the camera's distance in the star's radii), DiscFill, DiscIntensity,
DiscLimbDarkening (the distant stand-in's disc, UDistantStarsComponent), NearGain (how much brighter the near look is than the
far one's Intensity), RayCount, RaySpeed, RayStrength, TimeRate.

Run with the editor open and no play session:  python3 tools/planet_materials/star_glow_material.py
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
from uemat import *

NAME = "M_StarGlow"

FUNCS = r"""
return 0.0;
}
#ifndef SLONK_GLOW_FUNCS
#define SLONK_GLOW_FUNCS
float SlonkGlowHash(float x) { return frac(sin(x * 127.1 + 311.7) * 43758.5453); }
// Value noise round a circle: smooth in the angle a (radians), with n cells to the turn, so it wraps.
float SlonkGlowRing(float a, float n, float seed)
{
    float u = a / 6.2831853 * n;
    float i = floor(u), f = frac(u);
    f = f * f * (3.0 - 2.0 * f);
    float h0 = SlonkGlowHash(fmod(i + n, n) + seed * 17.0), h1 = SlonkGlowHash(fmod(i + 1.0 + n, n) + seed * 17.0);
    return lerp(h0, h1, f);
}
float SlonkGlowFuncsEnd1(FMaterialPixelParameters Parameters)
{
#else
float SlonkGlowFuncsEnd2(FMaterialPixelParameters Parameters)
{
#endif
    return 0.0;
"""

CODE = r"""
float3 Vn = normalize(V);
// Where on the plane, in its half-widths from the centre, and in the star's radii (DiscFraction is the disc's radius
// in half-widths; 0 for a glow with no disc, a nebula's).
float2 q = (UV - 0.5) * 2.0;
float rq = length(q);
float df = DiscFraction;
float r = df > 1.0e-4 ? rq / df : 1.0e4;
float x = df > 1.0e-4 ? saturate((rq - df) / max(1.0 - df, 1.0e-3)) : saturate(rq);
float fadeOut = pow(1.0 - x, 2.0);
// The angle round the disc from the star's north, as the camera sees it.
float3 e1 = StarUp - Vn * dot(StarUp, Vn);
e1 = dot(e1, e1) > 1.0e-6 ? normalize(e1) : normalize(cross(Vn, float3(0.0, 0.0, 1.0)));
float3 e2 = cross(Vn, e1);
float th = atan2(dot(Off, e2), dot(Off, e1));
float thq = atan2(q.y, q.x);
float T = Time;
float3 col = float3(0.0, 0.0, 0.0);

// ---- far: halo and rays ----
float far = 1.0 - Near;
if (far > 0.001)
{
    float halo = pow(1.0 - x, 3.0) * (0.35 + 0.65 * exp(-x * Falloff * 2.0));
    float rays = 0.0;
    for (int k = 0; k < 3; ++k)
    {
        float n = max(RayCount, 1.0) * (2.0 + 2.0 * k);
        float spin = T * RaySpeed * (k == 1 ? -0.05 : 0.04);
        float a = thq + spin + k * 1.7;
        float sharp = pow(abs(cos(a * n * 0.5)), 30.0 + 30.0 * k);
        float len = 0.35 + 0.65 * SlonkGlowRing(a, n, Seed + k);
        rays += sharp * saturate(1.0 - x / len) * (1.0 - 0.25 * k);
    }
    col += far * CoronaColor * Intensity * (halo + RayStrength * rays * (1.0 - x)) * step(df, rq);
    // The disc itself, where the glow stands in for a star too far off to draw (DiscFill).
    if (DiscFill > 0.0 && rq < df)
    {
        float mu = sqrt(saturate(1.0 - (rq / df) * (rq / df)));
        col += far * DiscFill * StarColor * DiscIntensity * (1.0 - DiscLimbDarkening * (1.0 - mu));
    }
}

// ---- near: chromosphere, prominences, corona ----
// Close in, the edge of the sphere the camera sees lies outside a radius on this plane through the centre:
// heights are measured from that edge, or everything low on the limb would be behind the disc.
float rapp = (CamRadii > 1.001 ? CamRadii / sqrt(CamRadii * CamRadii - 1.0) : 1.0) * 0.997;   // (a touch inside: the sphere is a mesh)
if (Near > 0.001 && r >= rapp)
{
    float h = r - rapp;
    float gain = Intensity * NearGain;
    // Streamers: a slow pattern round the limb, broadest toward the equator, curving a little as they reach out.
    float lat = abs(cos(th));      // 1 at the poles, 0 at the equator (th is measured from north)
    float a = th + 0.06 * sin(h * 3.0 + th * 2.0);
    float s = SlonkGlowRing(a + T * 0.0007, 14.0, Seed) * 0.6 + SlonkGlowRing(a - T * 0.0011, 37.0, Seed + 3.0) * 0.4;
    // Coronal holes: toward the poles the field opens into space and the corona there is thin and dark, the hole's
    // edge wandering with longitude and time.
    float holeEdge = 0.62 + 0.18 * (SlonkGlowRing(th + T * 0.0002, 7.0, Seed + 11.0) - 0.5);
    float hole = smoothstep(holeEdge - 0.1, holeEdge + 0.1, lat);
    float streamer = lerp(0.35, 1.0, s * s) * lerp(1.0, 0.18, hole);
    float rr = r / rapp;
    float corona = (0.55 * pow(rr, -7.0) + 0.45 * pow(rr, -2.6)) * streamer;
    col += Near * CoronaColor * gain * 0.9 * corona * fadeOut;

    // The chromosphere: a thin red rim, its outer edge ragged with spicules that flicker.
    float spic = SlonkGlowRing(th + T * 0.0004, 900.0, Seed + 5.0) * 0.6 + SlonkGlowRing(th - T * 0.0009, 2300.0, Seed + 6.0) * 0.4;
    float rimW = 0.004 + 0.007 * spic;
    float rim = exp(-h / rimW);
    float3 red = float3(1.0, 0.22, 0.28);
    col += Near * red * gain * 1.2 * rim;

    // Prominences: up to eight, each on its own cycle at a latitude of the activity belts, an arch or a hedgerow.
    int count = (int)clamp(Activity * 8.0 + 0.5, 0.0, 8.0);
    float prom = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        if (i >= count) { break; }
        float hi = SlonkGlowHash(i * 3.1 + Seed);
        float life = 300.0 + 500.0 * SlonkGlowHash(i * 7.7 + Seed);
        float tl = T / life + hi;
        float ep = floor(tl), u = tl - ep;
        float he = SlonkGlowHash(i * 11.3 + ep * 5.9 + Seed);
        float hf = SlonkGlowHash(i * 2.3 + ep * 9.1 + Seed);
        float grow = smoothstep(0.0, 0.2, u) * (1.0 - smoothstep(0.75, 1.0, u));
        // Where on the limb: in the belts either side of the equator (th is pi/2 or -pi/2 there), each side of the disc.
        float side = he < 0.5 ? 1.0 : -1.0;
        float phi = side * (1.5708 + (hf < 0.5 ? -1.0 : 1.0) * (0.12 + 0.45 * frac(he * 9.7)));
        float W = 0.04 + 0.08 * frac(hf * 5.3);                 // half-width along the limb, radii
        float H = (0.05 + 0.12 * frac(he * 3.7 + hf)) * grow;     // height, radii
        // One in four lifts off at the end of its life: it climbs and thins.
        float lift = (frac(he * 13.0) < 0.25) ? smoothstep(0.6, 1.0, u) : 0.0;
        H *= 1.0 + 3.0 * lift;
        float dth = atan2(sin(th - phi), cos(th - phi));
        float sx = dth * r / max(W, 1.0e-3);                      // -1..1 across its foot
        if (abs(sx) > 1.6 || H < 0.002) { continue; }
        float shape;
        if (frac(hf * 7.1) < 0.5)
        {
            // An arch: a loop from foot to foot, its gas streaming along it.
            float hc = H * sqrt(saturate(1.0 - sx * sx));
            float w = 0.003 + 0.006 * H;
            shape = exp(-pow((h - hc) / w, 2.0)) * step(abs(sx), 1.0);
            shape += 0.5 * exp(-pow((h - hc) / (w * 3.0), 2.0)) * step(abs(sx), 1.0);
            float flow = 0.6 + 0.4 * sin(sx * 25.0 - T * 0.35 + i * 2.0);
            shape *= flow;
        }
        else
        {
            // A hedgerow: a sheet of fine threads standing on the limb.
            float top = H * (0.55 + 0.45 * SlonkGlowRing(th + 0.3 * i, 160.0, i + Seed) * (0.7 + 0.3 * SlonkGlowRing(th, 900.0, i + Seed + 2.0)));
            float threads = 0.35 + 0.65 * pow(SlonkGlowRing(th + T * 0.0002, 24000.0, i + Seed + 8.0), 1.5);
            float clumps = 0.6 + 0.4 * SlonkGlowRing(th - T * 0.0001 + h * 4.0, 2600.0, i + Seed + 9.0);
            shape = (1.0 - smoothstep(top * 0.85, top, h)) * (1.0 - smoothstep(0.85, 1.1, abs(sx))) * threads * clumps;
        }
        prom += shape * grow * (1.0 - 0.6 * lift);
    }
    col += Near * red * gain * 4.0 * prom * fadeOut;

    // Coronal loops: over each active region, a nest of thin bright arches, the hot gas of the corona held on its
    // field lines; they flicker as small heating events come and go along them.
    float loops = 0.0;
    for (int j = 0; j < 6; ++j)
    {
        if (j >= count) { break; }
        float hj = SlonkGlowHash(j * 5.3 + Seed + 1.0);
        float lifeJ = 400.0 + 400.0 * SlonkGlowHash(j * 9.1 + Seed + 2.0);
        float tj = T / lifeJ + hj;
        float epj = floor(tj), uj = tj - epj;
        float gj = SlonkGlowHash(j * 13.7 + epj * 3.3 + Seed + 3.0);
        float growJ = smoothstep(0.0, 0.2, uj) * (1.0 - smoothstep(0.7, 1.0, uj));
        float phiJ = (gj < 0.5 ? 1.0 : -1.0) * (1.5708 + (frac(gj * 7.0) < 0.5 ? -1.0 : 1.0) * (0.15 + 0.4 * frac(gj * 3.1)));
        float dj = atan2(sin(th - phiJ), cos(th - phiJ));
        float bundle = 0.0;
        for (int m = 0; m < 5; ++m)
        {
            // Each loop its own feet (shifted along the limb), width and flattening, so the bundle is loose.
            float hm1 = SlonkGlowHash(j * 31.0 + m * 7.0 + epj + Seed), hm2 = SlonkGlowHash(j * 17.0 + m * 11.0 + epj + Seed + 4.0);
            float Wm = 0.03 + 0.06 * hm1;
            float cm = (hm2 - 0.5) * 0.06;
            float sxm = (dj * r - cm) / Wm;
            if (abs(sxm) > 1.0) { continue; }
            float hm = Wm * (0.5 + 0.7 * hm2) * sqrt(saturate(1.0 - sxm * sxm));
            float flick = 0.5 + 0.5 * sin(T * (0.3 + 0.4 * hm1) + j * 3.0 + m * 1.7 + sxm * (4.0 + 6.0 * hm2));
            // Brightest low down near the feet, fading toward the top as the gas thins.
            float fadeUp = exp(-h / (0.04 + 0.03 * hm1));
            bundle += exp(-pow((h - hm) / (0.004 + 0.004 * hm2), 2.0)) * flick * fadeUp;
        }
        loops += bundle * growJ;
    }
    col += Near * lerp(CoronaColor, float3(1.0, 0.95, 0.85), 0.5) * gain * 0.9 * loops * fadeOut;
}
return col;
"""

INPUTS = ["Fn", "UV", "Off", "V", "Time", "StarUp", "StarRadiusCm", "DiscFraction", "Near", "CoronaColor", "StarColor", "Intensity", "Falloff",
          "RayCount", "RaySpeed", "RayStrength", "Activity", "Seed", "NearGain", "CamRadii", "DiscFill", "DiscIntensity", "DiscLimbDarkening"]


def build():
    wipe(NAME)
    g = G(NAME, {"ShadingModel": "MSM_Unlit", "BlendMode": "BLEND_Additive", "TwoSided": True, "bUsedWithInstancedStaticMeshes": True})
    fn = g.custom(FUNCS, [], "CMOT_Float1", "glow functions")
    e = g.custom(CODE, INPUTS, "CMOT_Float3", "corona")
    g.c(fn, e, "Fn")
    # Where on the plane, from the star's centre (the subtraction in the graph keeps the world's large coordinates exact).
    off = g.sub(g.node("WorldPosition"), g.node("ObjectPositionWS"))
    g.c(off, e, "Off")
    g.c(g.texcoord(0), e, "UV")
    g.c(g.node("CameraVectorWS"), e, "V")
    g.c(g.mul(g.node("Time"), g.scalar("TimeRate", 1.0)), e, "Time")
    up = g.vec("StarUp", (0.0, 0.0, 1.0))
    upm = g.node("ComponentMask", {"R": True, "G": True, "B": True, "A": False}); g.c(up, upm, "")
    g.c(upm, e, "StarUp")
    for name, v in (("StarRadiusCm", 6.96e10), ("DiscFraction", 0.3), ("Near", 0.0)):
        g.c(g.scalar(name, v), e, name)
    for name, rgb in (("CoronaColor", (1.0, 0.72, 0.4)), ("StarColor", (1.0, 0.86, 0.62))):
        v = g.vec(name, rgb)
        m = g.node("ComponentMask", {"R": True, "G": True, "B": True, "A": False}); g.c(v, m, "")
        g.c(m, e, name)
    for name, v in (("Intensity", 3.0), ("Falloff", 3.5), ("RayCount", 9.0), ("RaySpeed", 0.15), ("RayStrength", 0.5), ("Activity", 0.6), ("Seed", 0.0), ("NearGain", 3.0),
                    ("CamRadii", 100.0), ("DiscFill", 0.0), ("DiscIntensity", 80.0), ("DiscLimbDarkening", 0.35)):
        g.c(g.scalar(name, v), e, name)
    g.out(e, "MP_EmissiveColor")
    g.finish()


if __name__ == "__main__":
    build()
