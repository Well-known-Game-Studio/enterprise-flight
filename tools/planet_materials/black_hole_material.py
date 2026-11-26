"""Builds M_BlackHole: a black hole as it would be seen, by tracing each pixel's ray back through the curved space
about it (AStarBody's Lens, a unit sphere LensRadius Schwarzschild radii across, StarBody.cpp).

Each ray is followed in its own plane (through the hole's centre, the camera and the ray) by the photon orbit
equation of the Schwarzschild metric, u'' + u = 3/2 u^2 (u the inverse radius in Schwarzschild radii, the prime
the angle about the hole), stepped by RK4 in that angle. A ray that falls within the horizon is lost: the hole's
shadow, two and a half radii across rather than one, for the rays that pass close are bent in as well. Rays that
wind nearly once round before they go make the thin bright photon ring at the shadow's edge. Every time a ray
crosses the plane of the disk between its inner edge (the innermost stable orbit, 3 radii) and its outer, it takes
up the disk's light there: so the far side of the disk is seen bent up over the top of the shadow and down beneath
it, and the near side's light in front. The disk's own light is a thin, hot, turbulent sheet:
  - its temperature falls outward as the radius to the three quarters (a thin disk's), its colour a black body's;
  - it goes round at the speed of a circular orbit, near light's at its inner edge, so the side coming toward the
    camera is brighter and bluer and the side going away dimmer and redder (the Doppler factor to the fourth power,
    and its temperature by the same factor); and all of it reddened by climbing out of the hole's well;
  - streaked by turbulence drawn round by its Keplerian turning.
A ray that gets away carries the sky from the direction it leaves in: the scene behind (the star sphere, worlds:
the opaque scene) looked up on the screen where that direction falls, so the stars behind the hole are pulled
round it into an Einstein ring. Lensing fades to nothing toward the sphere's rim (it is not nothing there, but
the sphere must end), where the material fades out.

Units: everything in Schwarzschild radii (RsCm); so the proxy placement that shrinks it toward the camera keeps it
the same. Axis is the disk's axis (world); Time the material's (real seconds times TimeRate).

Run with the editor open and no play session:  python3 tools/planet_materials/black_hole_material.py
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
from uemat import *

NAME = "M_BlackHole"

FUNCS = r"""
return 0.0;
}
#ifndef SLONK_BH_FUNCS
#define SLONK_BH_FUNCS
float SlonkBHHash(float3 c)
{
    uint3 u = (uint3)(int3)floor(c);
    uint h = (u.x * 73856093u) ^ (u.y * 19349663u) ^ (u.z * 83492791u);
    h = (h ^ (h >> 13)) * 0x5bd1e995u; h ^= h >> 15;
    return (float)(h & 0xffffu) / 65535.0;
}
float SlonkBHNoise(float3 q)
{
    float3 fl = floor(q); float3 t = q - fl; t = t * t * (3.0 - 2.0 * t);
    float x00 = lerp(SlonkBHHash(fl), SlonkBHHash(fl + float3(1, 0, 0)), t.x);
    float x10 = lerp(SlonkBHHash(fl + float3(0, 1, 0)), SlonkBHHash(fl + float3(1, 1, 0)), t.x);
    float x01 = lerp(SlonkBHHash(fl + float3(0, 0, 1)), SlonkBHHash(fl + float3(1, 0, 1)), t.x);
    float x11 = lerp(SlonkBHHash(fl + float3(0, 1, 1)), SlonkBHHash(fl + float3(1, 1, 1)), t.x);
    return lerp(lerp(x00, x10, t.y), lerp(x01, x11, t.y), t.z);
}
// A black body's colour at a temperature (K), linear, its brightest channel 1 (Tanner Helland's fit, in sRGB, made
// linear).
float3 SlonkBHBlackbody(float T)
{
    float t = clamp(T, 1000.0, 40000.0) / 100.0;
    float r = t <= 66.0 ? 1.0 : saturate(1.2929362 * pow(t - 60.0, -0.1332048));
    float g = t <= 66.0 ? saturate(0.3900797 * log(t) - 0.6318414) : saturate(1.1298909 * pow(t - 60.0, -0.0755148));
    float b = t >= 66.0 ? 1.0 : (t <= 19.0 ? 0.0 : saturate(0.5432068 * log(t - 10.0) - 1.1962541));
    return pow(float3(r, g, b), 2.2);
}
// u'' = -u + 3/2 u^2: one RK4 step of the angle h.
void SlonkBHStep(inout float u, inout float v, float h)
{
    float k1u = v, k1v = -u + 1.5 * u * u;
    float u2 = u + 0.5 * h * k1u, v2 = v + 0.5 * h * k1v;
    float k2u = v2, k2v = -u2 + 1.5 * u2 * u2;
    float u3 = u + 0.5 * h * k2u, v3 = v + 0.5 * h * k2v;
    float k3u = v3, k3v = -u3 + 1.5 * u3 * u3;
    float u4 = u + h * k3u, v4 = v + h * k3v;
    float k4u = v4, k4v = -u4 + 1.5 * u4 * u4;
    u += h / 6.0 * (k1u + 2.0 * k2u + 2.0 * k3u + k4u);
    v += h / 6.0 * (k1v + 2.0 * k2v + 2.0 * k3v + k4v);
}
float SlonkBHFuncsEnd1(FMaterialPixelParameters Parameters)
{
#else
float SlonkBHFuncsEnd2(FMaterialPixelParameters Parameters)
{
#endif
    return 0.0;
"""

CODE = r"""
float keep = (SC.r + SD) * 0.0;   // (the scene colour and depth nodes, so the material is given those textures)
float3 ro = (Cam - Centre) / RsCm;
float3 rd = normalize(WP - Cam);
float r0 = length(ro);
float Rl = LensRadius;
Opacity = 0.0;
// The sphere is two-sided, so the trace runs with the camera inside it; outside, only its near face.
if (r0 > Rl && dot(WP - Centre, Cam - WP) < 0.0) { return float3(0.0, 0.0, 0.0); }
float3 n = normalize(Axis);
float3 e1 = ro / max(r0, 1e-4);
float cr = dot(rd, e1);
float3 perp = rd - cr * e1;
float cphi = length(perp);
float b = r0 * cphi;
float3 e2 = cphi > 1e-5 ? perp / cphi : normalize(cross(e1, abs(e1.z) < 0.9 ? float3(0, 0, 1) : float3(1, 0, 0)));
float u = 1.0 / max(r0, 1e-4);
float v = -u * cr / max(cphi, 1e-5);
float a1 = dot(e1, n), b1 = dot(e2, n);
float sPrev = a1;
float phi = 0.0;
const float h = 0.03;
float uEsc = 1.0 / max(r0 * 1.02, DiskOuter * 1.5);
float3 dx = normalize(cross(n, abs(n.z) < 0.9 ? float3(0, 0, 1) : float3(1, 0, 0)));
float3 dy = cross(n, dx);
float3 col = float3(0.0, 0.0, 0.0);
float Tr = 1.0;
bool escaped = false;
float gCam = sqrt(saturate(1.0 - 1.0 / max(r0, 1.01)));
[loop] for (int i = 0; i < 240; ++i)
{
    float uN = u, vN = v;
    SlonkBHStep(uN, vN, h);
    float phiN = phi + h;
    float s = cos(phiN) * a1 + sin(phiN) * b1;
    if (s * sPrev < 0.0 && uN > 0.0 && u > 0.0)
    {
        // Through the disk's plane: where, and if within the disk, its light.
        float f = sPrev / (sPrev - s);
        float uc = lerp(u, uN, f), vc = lerp(v, vN, f), pc = phi + h * f;
        float rc = 1.0 / max(uc, 1e-6);
        if (rc > DiskInner * 0.95 && rc < DiskOuter)
        {
            float3 radial = cos(pc) * e1 + sin(pc) * e2;
            float3 p = radial * rc;
            float3 t = normalize((-sin(pc) * e1 + cos(pc) * e2) * rc - radial * (vc * rc * rc));
            float3 k = -t;
            float x = DiskInner / rc;
            float inner = saturate(1.0 - sqrt(saturate(x)));
            float Tem = DiskTemp * pow(x, 0.75) * pow(inner, 0.25) / 0.49;
            float beta = min(sqrt(0.5 / max(rc - 1.0, 0.05)), 0.95);
            float3 vdir = normalize(cross(n, p));
            float gam = 1.0 / sqrt(1.0 - beta * beta);
            float g = (1.0 / (gam * (1.0 - beta * dot(vdir, k)))) * sqrt(saturate(1.0 - 1.0 / rc)) / gCam;
            float ang = atan2(dot(p, dy), dot(p, dx)) - Time * Spin * pow(rc / DiskInner, -1.5);
            float3 q = float3(cos(ang) * 3.0, sin(ang) * 3.0, log(rc) * 7.0) + Seed;
            float turb = SlonkBHNoise(q) * 0.6 + SlonkBHNoise(q * 2.7 + 11.0) * 0.4;
            float prof = pow(x, 1.6) * inner / 0.06;
            float alpha = 0.92 * smoothstep(DiskInner * 0.95, DiskInner * 1.1, rc) * (1.0 - smoothstep(DiskOuter * 0.55, DiskOuter, rc)) * (0.55 + 0.45 * turb);
            col += Tr * alpha * SlonkBHBlackbody(Tem * g) * (prof * pow(g, 4.0) * (0.5 + turb) * Brightness);
            Tr *= 1.0 - alpha;
        }
    }
    sPrev = s; u = uN; v = vN; phi = phiN;
    if (u >= 1.0) { break; }
    if (u <= 0.0 || (u <= uEsc && v < 0.0)) { escaped = true; break; }
    if (Tr < 0.02) { break; }
}
float3 bg = float3(0.0, 0.0, 0.0);
float w = smoothstep(Rl * 0.97, Rl * 0.55, b);
if (escaped)
{
    // The direction it leaves in, eased back to its own toward the rim; the scene there, unless something nearer
    // than the hole is there (that light never passed it).
    float rr = 1.0 / max(u, 1e-6);
    float3 radial = cos(phi) * e1 + sin(phi) * e2;
    float3 dirOut = u > 0.0 ? normalize((-sin(phi) * e1 + cos(phi) * e2) * rr - radial * (v * rr * rr)) : normalize(-sin(phi) * e1 + cos(phi) * e2);
    float3 dir = normalize(lerp(rd, dirOut, w));
    float4 clip = mul(float4(ResolvedView.TranslatedWorldCameraOrigin + dir * 1.0e5, 1.0), ResolvedView.TranslatedWorldToClip);
    if (clip.w > 0.0)
    {
        float2 vuv = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
        // (Light bent from off the screen, or from behind the camera, is not on the screen to look up: dark sky.)
        float2 m = min(vuv, 1.0 - vuv);
        float onScreen = saturate(min(m.x, m.y) * 40.0);
        float2 buv = ViewportUVToBufferUV(saturate(vuv));
        float holeDepth = dot(Centre - Cam, ResolvedView.ViewForward);
        float nearer = step(CalcSceneDepth(buv), holeDepth * 0.98);
        bg = DecodeSceneColorForMaterialNode(buv) * onScreen * (1.0 - nearer);
    }
}
col += Tr * bg + keep;
Opacity = saturate(max(w * 1.4, 1.0 - Tr));
return col;
"""

INPUTS = ["Fn", "SC", "SD", "Cam", "WP", "Centre", "RsCm", "LensRadius", "DiskInner", "DiskOuter", "DiskTemp", "Brightness", "Spin", "Seed", "Time", "Axis"]


def build():
    wipe(NAME)
    g = G(NAME, {"ShadingModel": "MSM_Unlit", "BlendMode": "BLEND_Translucent", "TwoSided": True})
    fn = g.custom(FUNCS, [], "CMOT_Float1", "black hole functions")
    e = g.custom(CODE, INPUTS, "CMOT_Float3", "black hole trace", extra_outputs=(("Opacity", "CMOT_Float1"),))
    g.c(fn, e, "Fn")
    for pin, src in [("SC", g.node("SceneColor")), ("SD", g.node("SceneDepth")), ("Cam", g.node("CameraPositionWS")),
                     ("WP", g.node("WorldPosition")), ("Centre", g.node("ObjectPositionWS"))]:
        g.c(src, e, pin)
    for name, v in (("RsCm", 3.0e6), ("LensRadius", 60.0), ("DiskInner", 3.0), ("DiskOuter", 30.0), ("DiskTemp", 9000.0),
                    ("Brightness", 2.5), ("Spin", 0.6), ("Seed", 0.0)):
        g.c(g.scalar(name, v), e, name)
    g.c(g.mul(g.node("Time"), g.scalar("TimeRate", 1.0)), e, "Time")
    ax = g.vec("Axis", (0.0, 0.0, 1.0)); g.c(mask(g, ax, r=True, gg=True, b=True), e, "Axis")
    g.out(e, "MP_EmissiveColor")
    g.out(e, "MP_Opacity", "Opacity")
    g.finish()


if __name__ == "__main__":
    build()
