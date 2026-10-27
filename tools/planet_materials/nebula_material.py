"""Builds M_Nebula: an unlit, premultiplied-alpha, two-sided material that ray-marches gas through the sphere an
ANebula is scaled to. The march runs from the camera (or the sphere's near side) to the far side or the opaque
scene, whichever is nearer, through domain-warped value noise; each step scatters the star's light (with a short
shadow march toward it) and the gas's own glow, and the transmittance it leaves becomes the alpha.

Run with the editor open and no play session:  python3 tools/planet_materials/nebula_material.py
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
from uemat import *

NAME = "M_Nebula"

FUNCS = r"""
return 0.0;
}
#ifndef SLONK_NEB_FUNCS
#define SLONK_NEB_FUNCS
float NebHash(float3 c)
{
    uint3 u = (uint3)(int3)c;
    uint h = (u.x * 73856093u) ^ (u.y * 19349663u) ^ (u.z * 83492791u);
    h = (h ^ (h >> 13)) * 0x5bd1e995u; h ^= h >> 15;
    return (float)(h & 0xffffu) / 65535.0;
}
float NebNoise(float3 q)
{
    float3 fl = floor(q); float3 t = q - fl; t = t * t * (3.0 - 2.0 * t);
    float x00 = lerp(NebHash(fl), NebHash(fl + float3(1, 0, 0)), t.x);
    float x10 = lerp(NebHash(fl + float3(0, 1, 0)), NebHash(fl + float3(1, 1, 0)), t.x);
    float x01 = lerp(NebHash(fl + float3(0, 0, 1)), NebHash(fl + float3(1, 0, 1)), t.x);
    float x11 = lerp(NebHash(fl + float3(0, 1, 1)), NebHash(fl + float3(1, 1, 1)), t.x);
    return lerp(lerp(x00, x10, t.y), lerp(x01, x11, t.y), t.z);
}
float NebFbm(float3 q, int oct)
{
    float v = 0, a = 0.5, n = 0;
    for (int i = 0; i < oct; i++) { v += a * NebNoise(q); n += a; q = q * 2.03 + 11.7; a *= 0.5; }
    return v / n;
}
// Density of the gas at a unit-sphere position: warped fbm shells, thinning toward the edge of the sphere.
// Six noise lookups: one per warp axis, three octaves of the warped field.
// The cloud ends somewhere between 0.5 and 0.95 of the sphere depending on direction (the slow warp noise
// decides), so the silhouette is ragged instead of a disc.
float NebFall(float3 p, float edgeNoise)
{
    float edge = 0.5 + 0.45 * saturate(edgeNoise);
    float f = 1.0 - smoothstep(edge * 0.6, edge, length(p));
    return f * f;
}
float NebDensity(float3 p, float seed, float filaments, out float3 warp)
{
    float3 q = p * 2.2 + seed;
    warp = float3(NebNoise(q * 0.6 + 1.3), NebNoise(q * 0.6 + 7.1), NebNoise(q * 0.6 + 3.9)) - 0.5;
    float n = NebFbm(q + warp * (1.0 + 2.5 * filaments), 3);
    return saturate((n - 0.5) * 6.0) * NebFall(p, warp.z * 2.0 + 0.5);
}
// A two-octave stand-in for the shadow march.
float NebDensityCheap(float3 p, float seed)
{
    float3 q = p * 2.2 + seed;
    return saturate((NebFbm(q, 2) - 0.48) * 5.0) * NebFall(p, NebNoise(q * 0.6 + 3.9) * 2.0 - 0.5);
}
float NebFuncsEnd1(FMaterialPixelParameters Parameters)
{
#else
float NebFuncsEnd2(FMaterialVertexParameters Parameters)
{
#endif
    return 0.0;
"""

CODE = r"""
float3 ro = Cam;
float3 rd = normalize(WP - Cam);
float3 oc = ro - Centre;
float b = dot(oc, rd);
float c = dot(oc, oc) - RadiusCm * RadiusCm;
float h = b * b - c;
Alpha = 0.0;
if (h < 0.0) { return float3(0, 0, 0); }
h = sqrt(h);
float t0 = max(-b - h, 0.0);
float t1 = -b + h;
// The opaque scene ends the march: the depth is along the view axis, the ray is not.
float tScene = Depth / max(dot(rd, View.ViewForward), 1e-4);
t1 = min(t1, tScene);
if (t1 <= t0) { return float3(0, 0, 0); }
const int N = 32;
float dt = (t1 - t0) / N;
float ds = dt / RadiusCm;
float jitter = frac(sin(dot(SP.xy * 1000.0, float2(12.9898, 78.233))) * 43758.5453);
float t = t0 + dt * jitter;
float3 col = 0; float T = 1.0;
float3 sun = normalize(SunDirection);
[loop] for (int i = 0; i < N; i++)
{
    float3 p = (ro + rd * t - Centre) / RadiusCm;
    float3 w;
    float d = NebDensity(p, Seed, Filaments, w) * Density;
    if (d > 0.002)
    {
        // Shadow toward the star: one cheap sample a sixth of a radius sunward.
        float sh = exp(-NebDensityCheap(p + sun * 0.16, Seed) * Density * 3.0);
        float3 albedo = lerp(ColorA, ColorB, saturate(w.x * 2.5 + 0.5));
        // Brightness puts the gas on the scene's scale: the sun is tens of lux against a locked exposure, so a
        // lit white surface sits near 40 and a unit emissive is invisible.
        float3 light = (StarColor * (0.2 + 0.8 * sh) + EmissiveColor * (Emission * (0.4 + 0.6 * saturate(w.y * 2.0 + 0.6)))) * Brightness;
        float a = 1.0 - exp(-d * ds * 14.0);
        col += T * a * albedo * light;
        T *= 1.0 - a;
        if (T < 0.03) { break; }
    }
    t += dt;
}
Alpha = 1.0 - T;
return col;
"""

INPUTS = ["Fn", "Cam", "WP", "Centre", "Depth", "SP", "RadiusCm", "Density", "Emission", "Filaments", "Seed", "Brightness", "ColorA", "ColorB", "EmissiveColor", "SunDirection", "StarColor"]


def build():
    wipe(NAME)
    g = G(NAME, {"ShadingModel": "MSM_Unlit", "BlendMode": "BLEND_AlphaComposite", "TwoSided": True, "bDisableDepthTest": False})
    fn = g.custom(FUNCS, [], "CMOT_Float1", "nebula functions")
    e = g.custom(CODE, INPUTS, "CMOT_Float3", "nebula march", extra_outputs=(("Alpha", "CMOT_Float1"),))
    for pin, src in [("Fn", fn), ("Cam", g.node("CameraPositionWS")), ("WP", g.node("WorldPosition")), ("Centre", g.node("ObjectPositionWS")),
                     ("Depth", g.node("SceneDepth")), ("SP", g.node("ScreenPosition"))]:
        g.c(src, e, pin)
    for name, v in {"RadiusCm": 1.5e10, "Density": 0.6, "Emission": 0.5, "Filaments": 0.7, "Seed": 0.0, "Brightness": 20.0}.items():
        g.c(g.scalar(name, v), e, name)
    for name, rgb in {"ColorA": (0.95, 0.35, 0.55), "ColorB": (0.3, 0.45, 1.0), "EmissiveColor": (1.0, 0.55, 0.8), "SunDirection": (1.0, 0.0, 0.0), "StarColor": (1.0, 0.9, 0.7)}.items():
        g.c(g.vec(name, rgb), e, name)
    g.out(e, "MP_EmissiveColor")
    g.out(e, "MP_Opacity", "Alpha")
    g.finish()


if __name__ == "__main__":
    build()
