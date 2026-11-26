"""Builds M_Star, the photosphere of a star (AStarBody, StarBody.cpp): unlit, opaque, on a unit sphere.

What it draws, by the direction from the star's centre in the star's own frame (so it turns with the star):
  - granulation: bright convection cells parted by dark lanes, each cell brightening and fading and its centre
    wandering with time, over a faint supergranule web; each scale fades out by the pixel's footprint before it can
    alias, so from far off the disc is smooth and close in it boils;
  - sunspots in groups of two (a leader and a smaller follower), each with a dark umbra and a filamented penumbra, at
    the latitudes of a star's activity belts; each group forms, grows, lingers and decays over minutes, then another
    forms elsewhere, and they drift with the star's differential rotation (the equator the faster);
  - faculae: bright patches round the spot groups, seen near the limb;
  - flares: now and then an active region flashes white for a few seconds;
  - filaments: prominences seen from above, long dark threads across the disc, each living a while;
  - limb darkening, the limb the redder.

Time is the material's own (real seconds times TimeRate), so the surface lives whatever the game clock does.

Parameters (set by AStarBody::ApplyAppearance): StarColor, Intensity (the star's emissive), DiscScale (the share of it
the disc's centre is: 0.24, so that at the ship's locked exposure the disc is bright but its colour, its limb and its
structure still show), LimbDarkening
(0.35 is the Sun's), GranuleScale (6 is the default size of the granulation), Activity (0 no spots, 1 eight
groups), Seed, TimeRate.

Run with the editor open and no play session:  python3 tools/planet_materials/star_material.py
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
from uemat import *

NAME = "M_Star"

FUNCS = r"""
return 0.0;
}
// The custom node's template wraps this in a function: the helpers are defined after closing it, once, and a dummy
// function swallows the closing brace the template adds.
#ifndef SLONK_STAR_FUNCS
#define SLONK_STAR_FUNCS
float SlonkStarHash1(float3 p)
{
    p = frac(p * 0.3183099 + float3(0.71, 0.113, 0.419));
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float3 SlonkStarHash3(float3 p) { return float3(SlonkStarHash1(p), SlonkStarHash1(p + 17.31), SlonkStarHash1(p + 41.77)); }
// Cellular noise: the distances to the nearest and second-nearest feature points, and the nearest cell's random.
// The feature points wander about their cells with t.
float3 SlonkStarWorley(float3 p, float t)
{
    float3 c0 = floor(p);
    float f1 = 9.0, f2 = 9.0, id = 0.0;
    for (int z = -1; z <= 1; ++z)
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
    {
        float3 c = c0 + float3(x, y, z);
        float3 h = SlonkStarHash3(c);
        float3 f = c + 0.5 + 0.38 * sin(6.2831853 * h + t * (0.6 + h.zxy));
        float d = length(p - f);
        if (d < f1) { f2 = f1; f1 = d; id = h.x; }
        else if (d < f2) { f2 = d; }
    }
    return float3(f1, f2, id);
}
float SlonkStarValue(float3 p)
{
    float3 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = SlonkStarHash1(i), b = SlonkStarHash1(i + float3(1, 0, 0)), c = SlonkStarHash1(i + float3(0, 1, 0)), d = SlonkStarHash1(i + float3(1, 1, 0));
    float e = SlonkStarHash1(i + float3(0, 0, 1)), g = SlonkStarHash1(i + float3(1, 0, 1)), h = SlonkStarHash1(i + float3(0, 1, 1)), k = SlonkStarHash1(i + float3(1, 1, 1));
    return lerp(lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y), lerp(lerp(e, g, f.x), lerp(h, k, f.x), f.y), f.z);
}
float SlonkStarFuncsEnd1(FMaterialPixelParameters Parameters)
{
#else
float SlonkStarFuncsEnd2(FMaterialPixelParameters Parameters)
{
#endif
    return 0.0;
"""

CODE = r"""
float3 d = normalize(LP);
float mu = saturate(dot(normalize(N), normalize(V)));
float m1 = 1.0 - mu;
float fw = max(length(fwidth(d)), 1.0e-7);
float gs = max(GranuleScale, 0.5) / 6.0;
float T = Time;

// Supergranules: a faint bright web along their boundaries.
float Fs = 18.0 * gs;
float3 sg = SlonkStarWorley(d * Fs + Seed * 3.17, T * 0.015);
float net = (1.0 - smoothstep(0.0, 0.14, sg.y - sg.x)) * saturate(1.5 - fw * Fs * 4.0);

// Granules: a bright centre falling off to a dark lane; each cell brightens and fades on its own beat.
float Fg = 240.0 * gs;
// (The cells' space warped by a slower noise, so they are rounded and uneven, not a tiling of polygons.)
float3 pg = d * Fg + Seed * 13.1;
pg += 0.45 * (float3(SlonkStarValue(pg * 0.37), SlonkStarValue(pg * 0.37 + 19.7), SlonkStarValue(pg * 0.37 + 41.3)) - 0.5);
pg += 0.32 * (float3(SlonkStarValue(pg * 1.4), SlonkStarValue(pg * 1.4 + 7.3), SlonkStarValue(pg * 1.4 + 23.9)) - 0.5);
float3 gr = SlonkStarWorley(pg, T * 0.3);
float lane = smoothstep(0.0, 0.32, gr.y - gr.x);
// Brightest at a cell's centre, where the hot gas comes up; dimmer toward its edge; then the dark lane.
float centre = 1.0 - 0.28 * saturate(gr.x * 1.25);
float cellLum = (0.92 + 0.12 * sin(6.2831853 * gr.z * 5.0 + T * (0.15 + 0.25 * gr.z))) * centre;
float granule = lerp(0.48, cellLum, lane);
float wg = saturate(1.6 - fw * Fg * 1.6);
// Mesogranular mottling, a scale between the two.
float meso = SlonkStarValue(d * 70.0 * gs + float3(0.0, 0.0, T * 0.01)) - 0.5;
float wm = saturate(1.6 - fw * 70.0 * gs * 1.6);
float b = lerp(0.80, granule, wg) * (1.0 + 0.06 * meso * wm) * (1.0 + 0.05 * net);

// Spot groups.
float spot = 1.0, fac = 0.0, flare = 0.0;
int groups = (int)clamp(Activity * 8.0 + 0.5, 0.0, 8.0);
for (int i = 0; i < 8; ++i)
{
    if (i >= groups) { break; }
    float3 hi = SlonkStarHash3(float3(i, Seed, 3.7));
    float life = 240.0 + 360.0 * hi.x;
    float tl = T / life + hi.y;
    float ep = floor(tl), u = tl - ep;
    float3 he = SlonkStarHash3(float3(i, ep, Seed + 9.1));
    float grow = smoothstep(0.0, 0.25, u) * (1.0 - smoothstep(0.6, 1.0, u));
    if (grow <= 0.001) { continue; }
    float lat = (he.x < 0.5 ? -1.0 : 1.0) * (0.09 + 0.45 * he.y);
    float sl = sin(lat);
    // The equator turns the faster: each group drifts in longitude by its own latitude's rate.
    float lon = 6.2831853 * he.z + T * 0.0012 * (1.0 - 0.7 * sl * sl);
    float r0 = (0.022 + 0.04 * frac(he.x * 7.13 + he.z * 3.31)) * grow;
    float3 mid = float3(cos(lat) * cos(lon), cos(lat) * sin(lon), sl);
    float dm = length(d - mid);
    if (dm > r0 * 9.0 + 0.02) { continue; }
    for (int k = 0; k < 2; ++k)
    {
        float lk = lon + (k == 0 ? -1.0 : 1.3) * r0 * 2.4 / max(cos(lat), 0.2);
        float rk = r0 * (k == 0 ? 1.0 : 0.7);
        float3 ck = float3(cos(lat) * cos(lk), cos(lat) * sin(lk), sl);
        float dd = length(d - ck);
        float3 t1 = normalize(cross(ck, float3(0.0, 0.0, 1.0)));
        float3 t2 = cross(ck, t1);
        float3 v = d - ck;
        float az = atan2(dot(v, t2), dot(v, t1));
        float fil = 0.72 + 0.28 * sin(az * 46.0 + 5.0 * sin(az * 7.0 + he.y * 20.0) + 3.0 * dd / max(rk, 1.0e-4));
        float umbra = 1.0 - smoothstep(0.45 * rk, 0.75 * rk, dd);
        float pen = 1.0 - smoothstep(1.5 * rk, 2.1 * rk, dd);
        float lum = lerp(1.0, lerp(0.58 * fil, 0.10, umbra), pen);
        spot = min(spot, lum);
        fac += (1.0 - smoothstep(2.0 * rk, 6.5 * rk, dd)) * (1.0 - pen) * grow;
    }
    // Now and then the region flashes.
    float beat = sin(T * (0.05 + 0.05 * hi.z) + hi.x * 40.0 + ep * 3.0);
    float f = pow(saturate(beat), 80.0) * grow;
    flare += f * (1.0 - smoothstep(0.3 * r0, 2.5 * r0, dm)) * (0.6 + 0.8 * SlonkStarValue(d * 400.0 + T * 0.2));
}
b *= 1.0 + 0.22 * saturate(fac) * pow(m1, 1.2) * (0.6 + 0.8 * granule);

// Filaments: prominences seen from above, long dark threads lying across the disc along the boundaries between
// the active regions' fields; each its own slow life, a gentle curve, ragged and thinning toward its ends.
float fil = 0.0;
int nf = (int)clamp(Activity * 6.0 + 0.5, 0.0, 6.0);
for (int j = 0; j < 6; ++j)
{
    if (j >= nf) { break; }
    float3 hj = SlonkStarHash3(float3(j, Seed, 21.3));
    float lifeF = 500.0 + 700.0 * hj.x;
    float tf = T / lifeF + hj.y;
    float epf = floor(tf), uf = tf - epf;
    float3 hf = SlonkStarHash3(float3(j, epf, Seed + 31.7));
    float growF = smoothstep(0.0, 0.2, uf) * (1.0 - smoothstep(0.75, 1.0, uf));
    float latF = (hf.x < 0.5 ? -1.0 : 1.0) * (0.15 + 0.6 * hf.y);
    float lonF = 6.2831853 * hf.z + T * 0.0010 * (1.0 - 0.7 * sin(latF) * sin(latF));
    float3 c = float3(cos(latF) * cos(lonF), cos(latF) * sin(lonF), sin(latF));
    float3 east = normalize(cross(float3(0.0, 0.0, 1.0), c));
    float3 north = cross(c, east);
    float ang = (hf.x - 0.5) * 2.0;
    float3 t = east * cos(ang) + north * sin(ang);
    float3 n = cross(c, t);
    float L = 0.08 + 0.18 * frac(hf.y * 5.7);
    float3 v = d - c;
    if (dot(v, v) > (L + 0.05) * (L + 0.05)) { continue; }
    float sp = clamp(dot(v, t), -L, L);
    float bend = (frac(hf.z * 3.3) - 0.5) * 1.5;
    float off = dot(v, n) - bend * sp * sp;
    float along = sp / L;
    float w = (0.0025 + 0.003 * frac(hf.z * 9.1)) * sqrt(saturate(1.0 - along * along)) * (0.7 + 0.6 * SlonkStarValue(d * 900.0 + j));
    float core = exp(-pow(off / max(w, 1.0e-5), 2.0)) * step(abs(dot(v, t)), L);
    fil = max(fil, core * growF * (0.6 + 0.4 * SlonkStarValue(d * 300.0 + T * 0.01)));
}
b *= 1.0 - 0.55 * fil;

// Limb darkening (the Sun's quadratic law, scaled), and the limb the redder.
float k = LimbDarkening / 0.35;
float I = saturate(1.0 - 0.47 * k * m1 - 0.23 * k * m1 * m1);
float3 tint = lerp(float3(1.0, 1.0, 1.0), float3(1.0, 0.62, 0.40), pow(m1, 2.0) * 0.7);
// The umbra a deep brown, the penumbra warmer than the disc.
float3 spotTint = lerp(float3(1.0, 0.55, 0.32), float3(1.0, 1.0, 1.0), saturate((spot - 0.1) / 0.9));
// (The star's colour deepened: at the disc's exposure a pale colour reads as grey, the Sun's as gold.)
float3 hue = pow(saturate(StarColor), 2.2);
float3 col = hue * tint * spotTint * (Intensity * DiscScale) * b * spot * I;
col += float3(1.0, 0.97, 0.92) * (Intensity * DiscScale * 1.6) * flare;
return col;
"""

INPUTS = ["Fn", "LP", "N", "V", "Time", "StarColor", "Intensity", "DiscScale", "LimbDarkening", "GranuleScale", "Activity", "Seed"]


def build():
    wipe(NAME)
    g = G(NAME, {"ShadingModel": "MSM_Unlit", "BlendMode": "BLEND_Opaque", "TwoSided": False, "bUsedWithInstancedStaticMeshes": True})
    fn = g.custom(FUNCS, [], "CMOT_Float1", "star functions")
    e = g.custom(CODE, INPUTS, "CMOT_Float3", "photosphere")
    g.c(fn, e, "Fn")
    g.c(g.node("LocalPosition", {"IncludedOffsets": "IncludeOffsets", "LocalOrigin": "Instance"}), e, "LP")
    g.c(g.node("VertexNormalWS"), e, "N")
    g.c(g.node("CameraVectorWS"), e, "V")
    g.c(g.mul(g.node("Time"), g.scalar("TimeRate", 1.0)), e, "Time")
    g.c(g.vec("StarColor", (1.0, 0.86, 0.62)), e, "StarColor")
    for name, v in (("Intensity", 80.0), ("DiscScale", 0.24), ("LimbDarkening", 0.35), ("GranuleScale", 6.0), ("Activity", 0.6), ("Seed", 0.0)):
        g.c(g.scalar(name, v), e, name)
    g.out(e, "MP_EmissiveColor")
    g.finish()


if __name__ == "__main__":
    build()
