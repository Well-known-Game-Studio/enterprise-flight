"""Builds M_PlanetTerrain, the surface material of the quadtree planet terrain (UPlanetTerrainComponent).

The mesh carries the height field: positions, smooth normals and per-vertex biome data (UV0 = height/relief and
slope, UV1 = moisture and rock, UV2 = ice and canyon, UV3 = lineae and forest cover, colour = crater, water, canyon
wall, alpha the stratum). On a strange world with a geology of its own (GeoKind over 0: EGeology, PlanetTerrain.h) the
lineae's place in the vertex carries that geology's mark instead, and the material draws it in the world's two geology
colours: a crystal field clear on its tops and deep on its walls, a pan cracked into polygons, a terrace's stone with a
pool on every tread, a spire's banded rock, a plain of glass crazed with pale lines. The material only
shades: it picks layer colours from those, adds a periodic detail bump that stays continuous across chunks (the
chunk origin modulo the noise period arrives through custom primitive data 0..2, the cell size in 3, the origin over
the radius in 4..6 and its tile of the period in whole periods in 7..9), and fades each noise octave out once it is
smaller than a pixel. Every lattice in noise space has a whole number of cells to the period, so the look carries on
across a wrap; the settlements are drawn in planet space instead, as SlonkCities places the towns.

Run with the editor open and no play session:  python3 tools/planet_materials/terrain_material.py
This script is the material's source: change the graph here, not in the editor (a rebuild undoes an edit made there).
To see whether the asset has drifted from it:  python3 tools/ue_mcp/matgraph.py check tools/planet_materials/terrain_material.py M_PlanetTerrain
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
from uemat import *

NAME = "M_PlanetTerrain"


# A second custom node that only defines functions: its body returns at once and the closing brace the compiler
# adds ends a dummy function, so everything between is at file scope and the main node (compiled after it, since
# it takes this node's output as an input) can call it. The texture sets are sampled per hashed lattice cell:
# each 20 m cell of a planet-local lattice (two lattices, offset by half a cell, blended by distance to their
# centres so no cell edge shows) gets its own rotation and offset in a frame tied to the ground (radial up, a
# hashed heading), and the set is projected triplanar in that frame, so 5 m tiles never line up into a quilt
# and cliff faces get the beds horizontal. Beyond a few kilometres the sets fade out and the procedural noise
# above carries the look, as it did before.
FUNCS = r"""
return 0.0;
}
// The template emits every custom node twice, for the pixel and the vertex stage, once a node feeds the world
// position offset; the functions are defined on the first pass only and the dummy that swallows the closing
// brace takes a different name each time.
#ifndef SLONK_FUNCS
#define SLONK_FUNCS
#define SLONK_TEX_FLIPY (-1.0)
void SlonkCellFrame(int3 c, uint salt, float3 U, out float3 E, out float3 N2, out float2 off)
{
    uint hh = ((uint)c.x * 73856093u) ^ ((uint)c.y * 19349663u) ^ ((uint)c.z * 83492791u) ^ (salt * 2654435761u);
    hh = (hh ^ (hh >> 13)) * 0x5bd1e995u; hh ^= hh >> 15;
    float ang = (float)(hh & 0xffffu) / 65535.0 * 6.2831853;
    off = float2((float)((hh >> 16) & 0xffu), (float)((hh >> 24) & 0xffu)) / 255.0;
    float3 E0 = normalize(cross(abs(U.z) < 0.9 ? float3(0, 0, 1) : float3(1, 0, 0), U));
    float3 N0 = cross(U, E0);
    E = E0 * cos(ang) + N0 * sin(ang);
    N2 = cross(U, E);
}
void SlonkProj(Texture2D bc, SamplerState bcs, Texture2D nr, SamplerState nrs,
    float3 d, float3 dpx, float3 dpy, float3 T, float3 B, float3 A, float2 off, float tileCm, float wgt,
    inout float3 obc, inout float3 on, inout float3 oorm)
{
    float2 uv = float2(dot(d, T), dot(d, B)) / tileCm + off;
    float2 ux = float2(dot(dpx, T), dot(dpx, B)) / tileCm;
    float2 uy = float2(dot(dpy, T), dot(dpy, B)) / tileCm;
    float4 c = Texture2DSampleGrad(bc, bcs, uv, ux, uy);
    float4 n = Texture2DSampleGrad(nr, nrs, uv, ux, uy);
    float2 txy = n.rg * 2.0 - 1.0;
    float tz = sqrt(saturate(1.0 - dot(txy, txy)));
    obc += wgt * c.rgb;
    on += wgt * (T * txy.x + B * (SLONK_TEX_FLIPY * txy.y) + A * tz);
    oorm += wgt * float3(n.b, n.a, c.a);
}
float SlonkCell(Texture2D bc, SamplerState bcs, Texture2D nr, SamplerState nrs,
    float3 p, float3 dpx, float3 dpy, float3 U, float3 Nn, float cellCm, float tileCm, float periodCm, uint salt, float shift,
    inout float3 obc, inout float3 on, inout float3 oorm)
{
    float3 q = p / cellCm + shift;
    float3 fl = floor(q); float3 fr = q - fl - 0.5;
    int3 c = (int3)fl & ((int)(periodCm / cellCm) - 1);
    float3 E, N2; float2 off;
    SlonkCellFrame(c, salt, U, E, N2, off);
    float3 d = fr * cellCm;
    float w = 1.0 - smoothstep(0.56, 0.68, length(fr));
    if (w <= 0.0) { return 0.0; }
    float3 a = abs(float3(dot(Nn, E), dot(Nn, N2), dot(Nn, U)));
    a = a * a; a = a * a; a = a * a;
    a = max(a - 0.05 * max(a.x, max(a.y, a.z)), 0.0); a /= (a.x + a.y + a.z);
    float3 cbc = 0, cn = 0, corm = 0;
    if (a.z > 0.0) { SlonkProj(bc, bcs, nr, nrs, d, dpx, dpy, E, N2, U, off, tileCm, a.z, cbc, cn, corm); }
    if (a.x > 0.0) { float s = dot(Nn, E) < 0.0 ? -1.0 : 1.0; SlonkProj(bc, bcs, nr, nrs, d, dpx, dpy, N2 * s, U, E * s, off.yx, tileCm, a.x, cbc, cn, corm); }
    if (a.y > 0.0) { float s = dot(Nn, N2) < 0.0 ? -1.0 : 1.0; SlonkProj(bc, bcs, nr, nrs, d, dpx, dpy, E * -s, U, N2 * s, off + 0.37, tileCm, a.y, cbc, cn, corm); }
    obc += w * cbc; on += w * cn; oorm += w * corm;
    return w;
}
void SlonkLayer(Texture2D bc, SamplerState bcs, Texture2D nr, SamplerState nrs,
    float3 meanBC, float3 p, float3 dpx, float3 dpy, float3 U, float3 Nn, float periodCm, float nearFade, uint salt,
    out float3 bcMod, out float3 nrm, out float3 oorm)
{
    float3 bcN = 0, nN = 0, ormN = 0;
    float w0 = SlonkCell(bc, bcs, nr, nrs, p, dpx, dpy, U, Nn, 2048.0, 512.0, periodCm, salt, 0.0, bcN, nN, ormN);
    float w1 = SlonkCell(bc, bcs, nr, nrs, p, dpx, dpy, U, Nn, 2048.0, 512.0, periodCm, salt + 7u, 0.5, bcN, nN, ormN);
    float wN = w0 + w1; bcN /= wN; nN /= wN; ormN /= wN;
    // Where the two lattices crossfade, the sum of two unrelated patches loses contrast; restore the deviation
    // from the mean by the blend's variance loss.
    float k = rsqrt((w0 * w0 + w1 * w1) / (wN * wN));
    bcN = meanBC + (bcN - meanBC) * k;
    ormN = float3(0.75, 0.85, 0.5) + (ormN - float3(0.75, 0.85, 0.5)) * k;
    nN = Nn + (nN - Nn) * k;
    bcMod = lerp(1.0, min(bcN / meanBC, 1.5), 0.75 * nearFade);
    nrm = (nN - Nn) * nearFade;
    oorm = lerp(float3(1.0, 0.85, 0.5), ormN, nearFade);
}
// Swell: four Gerstner waves with integer wave numbers across the noise period (so the field wraps with the
// chunk origins), deep-water dispersion in cm, amplitude scaled by swellCm. Returns the displacement in the
// (T, B, up) frame and the surface normal in the same frame.
void SlonkGerstner(float2 pu, float time, float periodCm, float swellCm, out float3 disp, out float3 nrm)
{
    const float2 K[4] = { float2(100.0, 70.0), float2(180.0, -60.0), float2(0.0, 256.0), float2(-300.0, 400.0) };
    const float A[4] = { 0.55, 0.3, 0.25, 0.12 };
    const float Q = 0.55;
    disp = 0; float2 dn = 0; float dz = 0;
    [unroll] for (int i = 0; i < 4; i++)
    {
        float2 k = 6.2831853 * K[i] / periodCm;
        float kl = length(k); float2 d = k / kl;
        float w = sqrt(981.0 * kl);
        float ph = dot(k, pu) - w * time;
        float a = A[i] * swellCm;
        float sn, cs; sincos(ph, sn, cs);
        disp.xy += d * (Q * a * cs); disp.z += a * sn;
        dn += d * (kl * a * cs); dz += Q * kl * a * sn;
    }
    nrm = normalize(float3(-dn, 1.0 - dz));
}
// The settlements' hash (SlonkCities' Mix and Hash01).
uint SlonkMix(uint a, uint b) { uint h = a ^ (b + 0x9e3779b9u + (a << 6) + (a >> 2)); h = (h ^ (h >> 13)) * 0x5bd1e995u; return h ^ (h >> 15); }
float SlonkU(uint h) { return (float)(h & 0xffffu) / 65535.0; }
// The squared distance (cells) to the nearest present point of a level from a point given as a pixel's cell (fl) plus
// an offset in cells (fr, any size): SlonkCities' NearestSq at a settlement point, from the pixel's own cell.
float SlonkNearestSqAt(int3 fl, float3 fr, uint kw)
{
    float3 fq = floor(fr); int3 b = fl + (int3)fq; float3 f = fr - fq;
    float best = 9.0;
    [unroll] for (int i = 0; i < 8; i++)
    {
        int3 off = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        int3 ck = (b + off) & 4095;
        uint h = ((uint)ck.x * 73856093u) ^ ((uint)ck.y * 19349663u) ^ ((uint)ck.z * 83492791u) ^ (0x9e3779b9u + kw * 0x85ebca6bu);
        h = (h ^ (h >> 13)) * 0x5bd1e995u; h ^= h >> 15;
        if ((h >> 30) >= 3u) { continue; }
        float3 jit = float3(h & 0x3ffu, (h >> 10) & 0x3ffu, (h >> 20) & 0x3ffu) / 1023.0;
        float3 dp = float3(off) + jit - f;
        best = min(best, dot(dp, dp));
    }
    return best;
}
// Value noise on a lattice in a settlement's own frame: smooth, 0..1, a hash a corner (SlonkCities' Value2).
float SlonkValue2(float2 q, uint h)
{
    float2 f = floor(q); float2 u = q - f; u = u * u * (3.0 - 2.0 * u);
    int2 i = (int2)f;
    float a = SlonkU(SlonkMix(SlonkMix(h, (uint)i.x), (uint)i.y));
    float b = SlonkU(SlonkMix(SlonkMix(h, (uint)(i.x + 1)), (uint)i.y));
    float c = SlonkU(SlonkMix(SlonkMix(h, (uint)i.x), (uint)(i.y + 1)));
    float d = SlonkU(SlonkMix(SlonkMix(h, (uint)(i.x + 1)), (uint)(i.y + 1)));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}
// Parks and the ground not built on yet in a settlement of some size, at a point in its frame (cm from its heart along
// its grid's axes): patches a few hundred metres across where its people left the forest standing, none at its very
// heart, few in the middle and more toward the edge; a village has none. 0 built on, 1 forest. SlonkCities' ParkOf mirrors this: its builders keep
// off the parks and the scatter grows trees in them.
float SlonkParkOf(float2 l, float radiusCm, uint h)
{
    float size = smoothstep(45000.0, 120000.0, radiusCm);
    if (size <= 0.0) { return 0.0; }
    float d = length(l) / (radiusCm * 1.05 + 2500.0);
    float n = 0.65 * SlonkValue2(l / 30000.0, SlonkMix(h, 61u)) + 0.35 * SlonkValue2(l / 12000.0, SlonkMix(h, 62u));
    float t = 0.66 + 0.3 * (1.0 - smoothstep(0.0, 0.2, d)) - 0.28 * smoothstep(0.55, 1.0, d);
    return size * smoothstep(t, t + 0.12, n);
}
// A settlement's clearing at a point (rel: the point less the settlement's point; up: the settlement's up): its own
// ground out to its radius (as SlonkCities builds on it and the scatter keeps its trees off it), then the forest's
// edge, soft, wandering between 1.05 and 1.9 times that by five harmonics round the settlement whose phases come from
// its seed, so a clearing has a shape of its own; never past capCm, a cell of its level. Its parks and unbuilt ground
// (SlonkParkOf) keep their forest. SlonkCities' ClearingOf mirrors this, number for number.
float SlonkClearingOf(float3 rel, float3 up, float radiusCm, uint h, float capCm)
{
    float3 r = rel - up * dot(rel, up);
    float d = length(r);
    float rb = radiusCm * 1.05 + 2500.0;
    if (d >= min(rb * 1.9, capCm)) { return 0.0; }
    float3 e = cross(float3(0.0, 0.0, 1.0), up);
    e = dot(e, e) > 1e-12 ? normalize(e) : float3(1.0, 0.0, 0.0);
    float3 n = cross(up, e);
    float th = atan2(dot(r, n), dot(r, e));
    float w = (0.5 * sin(2.0 * th + 6.2831853 * SlonkU(SlonkMix(h, 51u)))
             + 0.3 * sin(3.0 * th + 6.2831853 * SlonkU(SlonkMix(h, 52u)))
             + 0.25 * sin(5.0 * th + 6.2831853 * SlonkU(SlonkMix(h, 53u)))
             + 0.15 * sin(8.0 * th + 6.2831853 * SlonkU(SlonkMix(h, 54u)))
             + 0.1 * sin(13.0 * th + 6.2831853 * SlonkU(SlonkMix(h, 55u)))) / 1.3;
    // Its parks keep their forest: the grid's axes as the builder lays them (FTownFrame).
    float ang = SlonkU(SlonkMix(h, 11u)) * 3.14159265;
    float3 gx = e * cos(ang) + n * sin(ang); float3 gy = cross(up, gx);
    float park = SlonkParkOf(float2(dot(r, gx), dot(r, gy)), radiusCm, h);
    return (1.0 - smoothstep(rb, min(rb * (1.475 + 0.425 * w), capCm), d)) * (1.0 - park);
}
// A town's own ways, number for number as PlanetCities.cpp's FWays::TownClearance lays them for its builders: how far
// a point at (x, y) in the town's frame is from the edge of the nearest, cm (below 0 on one). style 3: a grown town's
// ring ways halfway between its rings of pods (one lobed shape scaled ring by ring) and its spokes, logarithmic
// spirals out from the plaza (plaza is 1 inside it); 1 and 2: a village's lanes or a hamlet's paths, wandering out
// from its heart.
float SlonkTownWays(float x, float y, uint H, float style, bool isCity, float radiusCm, out float plaza)
{
    plaza = 0.0;
    float r = length(float2(x, y)); float A = atan2(y, x);
    if (style > 2.5)
    {
        float ring = 3200.0 + 1200.0 * SlonkU(SlonkMix(H, 17u));
        float lobes = SlonkU(SlonkMix(H, 0x50u)) * 6.2831853;
        float k0 = isCity ? 2.0 : 1.0;
        float rings = max(k0, floor(radiusCm / ring));
        float hw = isCity ? 350.0 : 250.0;
        float w = 1.0 + 0.18 * sin(3.0 * A + lobes);
        float rho = r / (ring * w);
        float inner = k0 - 0.5;
        float kk = clamp(round(rho - 0.5), k0 - 1.0, rings);
        float ringClr = abs(rho - (kk + 0.5)) * ring * w - hw;
        if (rho < inner) { plaza = 1.0; return ringClr; }
        if (rho > rings + 0.5) { return ringClr; }
        float ns = 3.0 + (float)(SlonkMix(H, 0x52u) % 3u);
        float phi0 = SlonkU(SlonkMix(H, 0x53u)) * 6.2831853;
        float tw = SlonkU(SlonkMix(H, 0x54u)) < 0.5 ? -0.55 : 0.55;
        float sector = 6.2831853 / ns;
        float d0 = A - phi0 - tw * log(rho / inner);
        float dl = d0 - sector * round(d0 / sector);
        return min(ringClr, r * abs(dl) / sqrt(1.0 + tw * tw) - hw);
    }
    bool stone = style > 1.5;
    float vr = min(radiusCm, stone ? 18000.0 : 40000.0);
    float r0 = stone ? 500.0 : 1200.0;
    if (r < r0) { return r0 - r; }
    if (r > vr) { return r - vr; }
    float nl = stone ? 2.0 + (float)(SlonkMix(H, 0x60u) % 2u) : 3.0 + (float)(SlonkMix(H, 0x60u) % 3u);
    float p0 = SlonkU(SlonkMix(H, 0x61u)) * 6.2831853;
    float lw = stone ? 300.0 : 600.0, lam = stone ? 1800.0 : 2600.0, hw = stone ? 75.0 : 175.0;
    float ramp = smoothstep(0.0, 3000.0, r);
    float best = 1e9;
    [loop] for (int l = 0; l < 5; l++)
    {
        if ((float)l >= nl) { break; }
        uint hl = SlonkMix(H, 0x62u + (uint)l);
        float phil = p0 + 6.2831853 * (float)l / nl + (SlonkU(hl) - 0.5) * 1.8849556 / nl;
        float psi = SlonkU(SlonkMix(hl, 5u)) * 6.2831853;
        float da = A - phil; da -= 6.2831853 * round(da / 6.2831853);
        if (abs(da) > 1.5707963) { best = min(best, max(r0, r - lw) - hw); continue; }
        float lat = lw * sin(r / lam + psi) * ramp;
        best = min(best, abs(r * sin(da) - lat) - hw);
    }
    return best;
}
// The road a pixel lies on between its two nearest town points (ap: the pixel less the nearest, ab: the second less
// the nearest, both across the ground), as FWays::RoadClearance: out of the town it starts from (townR), straight for
// a grid-building people (style 0), an S-curve for a grown one (3), winding for the early ages (1, 2). How far from
// its edge, cm (below 0 on it).
float SlonkRoad(float3 ap, float3 ab, float3 U, uint hA, uint hB, float style, float townR)
{
    float L2 = max(dot(ab, ab), 1.0); float L = sqrt(L2);
    float t = saturate(dot(ap, ab) / L2);
    float3 perp = normalize(cross(U, ab));
    float sgn = (SlonkMix(hA ^ hB, 0x70u) & 1u) != 0u ? 1.0 : -1.0;
    float st = sin(6.2831853 * t);
    float off = 0.0, hw = 450.0;
    if (style > 2.5) { off = 0.12 * st; hw = 300.0; }
    else if (style > 1.5) { off = 0.06 * st + 0.03 * sin(18.849556 * t); hw = 75.0; }
    else if (style > 0.5) { off = 0.05 * st + 0.025 * sin(12.566371 * t); hw = 200.0; }
    float along = dot(ap, ab) / L, lat = dot(ap, perp);
    float curve = length(float2(along - t * L, lat - sgn * off * L)) - hw;
    return t * L < townR ? max(curve, townR - t * L) : curve;
}
float SlonkTexFuncsEnd1(FMaterialPixelParameters Parameters)
{
#else
float SlonkTexFuncsEnd2(FMaterialVertexParameters Parameters)
{
#endif
    return 0.0;
"""

# The vertex shader side: the swell as geometry on the water vertices, within a few kilometres.
WPO_CODE = r"""
float3 p = LP + float3(OX, OY, OZ);
float water = Col.g;
// Frozen sea (SeaIce, a tidally locked world's far side) lies still: the ice runs over the water at the vertex.
float fade = (1.0 - smoothstep(80000.0, 250000.0, distance(Cam, WP))) * water * (1.0 - SeaIce * smoothstep(0.4, 0.7, UV2.x));
if (fade <= 0.001) { return float3(0, 0, 0); }
float3 U = normalize(float3(ORX, ORY, ORZ) + LP / PlanetRadiusCm);
float3 aU = abs(U); float2 pu; float3 Tw, Bw;
if (aU.x >= aU.y && aU.x >= aU.z) { pu = p.yz; Tw = float3(0, 1, 0); Bw = float3(0, 0, 1); }
else if (aU.y >= aU.z) { pu = p.xz; Tw = float3(1, 0, 0); Bw = float3(0, 0, 1); }
else { pu = p.xy; Tw = float3(1, 0, 0); Bw = float3(0, 1, 0); }
float3 d, n;
SlonkGerstner(pu, Time, NoisePeriodCm, SwellCm, d, n);
return (Tw * d.x + Bw * d.y + U * d.z) * fade;
"""

# Shared HLSL: inputs are the custom node's pins. Value noise on a wrapped integer lattice: periodic in NoisePeriodCm.
CODE = r"""
float3 p = LP + float3(OX, OY, OZ);
float hN = UV0.x; float slope = UV0.y; float wet = UV1.x; float rock = UV1.y; float ice = UV2.x; float canyon = UV2.y;
float crater = Col.r; float water = Col.g; float wall = Col.b; float strat = ColA * 8.0;
float dist = max(distance(Cam, WP), 1.0);
float3 Nn = normalize(N);
float3 T1 = normalize(cross(Nn, abs(Nn.x) < 0.9 ? float3(1, 0, 0) : float3(0, 1, 0)));
float3 T2 = cross(Nn, T1);

// Five octaves of value noise from 16 m down to 1 m (the mesh carries everything coarser, the texture sets everything finer), with the gradient
// taken analytically from the trilinear weights so the bump costs one lattice lookup per octave. Each octave is
// weighted out as it drops below a pixel. The octave values are kept for the texturing below. Each octave has a whole
// number of cells to the noise period (655, doubling: cells of 1600.9 cm down to 100.05 cm) and wraps its index by
// that count, so the noise is periodic and a chunk whose origin has wrapped matches its neighbour.
float fb0 = 0, norm = 0, amp = 1.0;
float3 gfb = 0;
float val[5]; float wgt[5];
[unroll] for (int k = 0; k < 5; k++)
{
    int wrap = 655 << k;
    float cs = NoisePeriodCm / (float)wrap;
    float w = saturate(cs / (dist * 0.0045) - 0.35);
    float3 q = p / cs;
    float3 fl = floor(q); float3 t = q - fl; float3 sm = t * t * (3.0 - 2.0 * t); float3 dsm = 6.0 * t * (1.0 - t);
    int3 c0 = (int3)fl;
    float h[8];
    [unroll] for (int i = 0; i < 8; i++)
    {
        int3 c = ((c0 + int3(i & 1, (i >> 1) & 1, (i >> 2) & 1)) % wrap + wrap) % wrap;
        uint hh = ((uint)c.x * 73856093u) ^ ((uint)c.y * 19349663u) ^ ((uint)c.z * 83492791u) ^ ((uint)k * 2654435761u);
        hh = (hh ^ (hh >> 13)) * 0x5bd1e995u; hh ^= hh >> 15;
        h[i] = (float)(hh & 0xffffu) / 65535.0;
    }
    float x00 = lerp(h[0], h[1], sm.x), x10 = lerp(h[2], h[3], sm.x), x01 = lerp(h[4], h[5], sm.x), x11 = lerp(h[6], h[7], sm.x);
    float y0 = lerp(x00, x10, sm.y), y1 = lerp(x01, x11, sm.y);
    float v = lerp(y0, y1, sm.z);
    float3 g;
    g.x = lerp(lerp(h[1] - h[0], h[3] - h[2], sm.y), lerp(h[5] - h[4], h[7] - h[6], sm.y), sm.z) * dsm.x;
    g.y = lerp(x10 - x00, x11 - x01, sm.z) * dsm.y;
    g.z = (y1 - y0) * dsm.z;
    val[k] = v; wgt[k] = w;
    fb0 += (v - 0.5) * amp * w; gfb += g / cs * amp * w;
    norm += amp; amp *= 0.6;
}
fb0 /= norm; gfb /= norm;
float crack = (1.0 - abs(val[3] - 0.5) * 2.0) * wgt[3];

// Jittered dots: one hashed disc per lattice cell of size cellCm, present in a fraction `density` of the cells,
// centred anywhere in the middle of its cell with a hashed radius, so no lattice shows. Returns coverage 0..1.
// (A function would need a separate custom node; the loop runs twice inline, scrub then stones.)
// The scrub dots are copied on the CPU: SlonkScatter::ScrubDotAt and ScrubZoneAt (TerrainScatter) stand a bush on each
// one. A change to their cell count, salt, gate, centre or radius here, or to the scrub zone below, goes there too.
// Each lattice has a whole number of cells to the noise period (about 450 cm and 180 cm a cell) and wraps its index
// by that count, so it is periodic: a chunk whose origin has wrapped draws the same dots as its neighbour.
float dotsA = 0, dotsB = 0;
[unroll] for (int kd = 0; kd < 2; kd++)
{
    int nc = kd == 0 ? 2330 : 5825;
    float cellCm = NoisePeriodCm / (float)nc;
    float3 q = p / cellCm; float3 fl = floor(q); float3 fr = q - fl;
    int3 c = ((int3)fl % nc + nc) % nc;
    uint hh = ((uint)c.x * 73856093u) ^ ((uint)c.y * 19349663u) ^ ((uint)c.z * 83492791u) ^ ((uint)(kd + 31) * 2654435761u);
    hh = (hh ^ (hh >> 13)) * 0x5bd1e995u; hh ^= hh >> 15;
    float gate = (float)(hh & 0xffu) / 255.0;
    float3 ctr = float3((float)((hh >> 8) & 0xffu), (float)((hh >> 16) & 0xffu), (float)((hh >> 24) & 0xffu)) / 255.0 * 0.5 + 0.25;
    float rad = 0.12 + 0.13 * (float)((hh >> 4) & 0xffu) / 255.0;
    // Distance in the tangent plane only: the third lattice axis is the radial one and would clip discs.
    float3 d = fr - ctr; d -= Nn * dot(d, Nn);
    float cov = 1.0 - smoothstep(rad * 0.6, rad, length(d));
    float wd = saturate(cellCm / (dist * 0.0045) - 0.35);
    if (kd == 0) dotsA = (gate < 0.35 ? cov : 0.0) * wd; else dotsB = (gate < 0.3 ? cov : 0.0) * wd;
}

// Coarse value noise for the soil's patchiness by day: 3, 6 and 12 cells to the noise period (3.5 km, 1.7 km and
// 0.9 km), the index wrapped by that count, so the patches tile with the period and carry on across a wrap.
float cn = 0, cnorm = 0, camp = 1.0;
[unroll] for (int kc = 0; kc < 3; kc++)
{
    int wrapc = 3 << kc;
    float ccs = NoisePeriodCm / (float)wrapc;
    float3 q = p / ccs; float3 fl = floor(q); float3 fr = q - fl; fr = fr * fr * (3.0 - 2.0 * fr);
    int3 c0 = (int3)fl;
    float h[8];
    [unroll] for (int i = 0; i < 8; i++)
    {
        int3 c = ((c0 + int3(i & 1, (i >> 1) & 1, (i >> 2) & 1)) % wrapc + wrapc) % wrapc;
        uint hh = ((uint)c.x * 73856093u) ^ ((uint)c.y * 19349663u) ^ ((uint)c.z * 83492791u) ^ ((uint)(kc + 11) * 2654435761u);
        hh = (hh ^ (hh >> 13)) * 0x5bd1e995u; hh ^= hh >> 15;
        h[i] = (float)(hh & 0xffffu) / 65535.0;
    }
    float v = lerp(lerp(lerp(h[0], h[1], fr.x), lerp(h[2], h[3], fr.x), fr.y), lerp(lerp(h[4], h[5], fr.x), lerp(h[6], h[7], fr.x), fr.y), fr.z);
    cn += (v - 0.5) * camp; cnorm += camp; camp *= 0.5;
}
cn = cn / cnorm + 0.5;

// Layers.
float steep = saturate(slope * 7.0 - 0.15);
float cliff = smoothstep(0.3, 0.55, slope);
// Bare rock where it is steep or mountainous; canyon floors are sediment, not rock.
float rocky = saturate(rock * 0.7 + steep * 1.2 + crater * 0.5);
float sediment = canyon * (1.0 - smoothstep(0.03, 0.3, slope + 0.08 * fb0));
float3 soil = lerp(LandColor2, LandColor, saturate(wet * 1.2 - rocky * 0.3));
float band = frac(hN * ReliefCm / 22000.0 + fb0 * 0.4);
float strata = smoothstep(0.25, 0.45, band) * (1.0 - smoothstep(0.7, 0.9, band)) * steep;
float3 rockc = MountainColor * (0.72 + 0.45 * strata);
rockc = lerp(rockc, rockc * float3(1.25, 0.9, 0.72), saturate(canyon * 1.5) * 0.7);
// Canyon walls: each stratum of the mesh's staircase gets its own bed colour (limestone creams, red shales and
// sandstones, grey-greens, dark basement) on its cliff, and the bench below it wears the scree of that bed.
{
    float layer = floor(strat); float lf = frac(strat);
    float lh = frac(sin(layer * 12.9898 + 1.7) * 43758.5453);
    float3 bed = lh < 0.3 ? float3(0.74, 0.66, 0.54) : (lh < 0.6 ? float3(0.60, 0.33, 0.22) : (lh < 0.82 ? float3(0.58, 0.44, 0.36) : float3(0.40, 0.38, 0.33)));
    bed *= 0.9 + 0.2 * frac(sin(layer * 7.31 + 3.1) * 23758.5453);
    float seam = 1.0 - smoothstep(0.0, 0.08, lf) * (1.0 - smoothstep(0.92, 1.0, lf));
    bed *= 1.0 - 0.15 * seam;
    float3 scree = lerp(bed, float3(0.55, 0.42, 0.32), 0.5) * 0.85;
    float3 wallc = lerp(scree, bed, cliff) * (0.85 + 0.3 * (fb0 + 0.5));
    float onwall = saturate(wall * 1.5) * (1.0 - sediment);
    rockc = lerp(rockc, wallc, onwall);
    rocky = max(rocky, onwall * 0.6);
}
// Patches of darker, wetter ground and paler dry ground at the kilometre scale, and again at 16 m and 8 m.
float patch = cn - 0.5;
soil = lerp(soil, soil * float3(0.72, 0.78, 0.7), saturate(patch * 3.0)) ;
soil = lerp(soil, soil * float3(1.15, 1.1, 1.0), saturate(-patch * 3.0));
soil *= 1.0 + 0.22 * (val[0] - 0.5) * wgt[0] + 0.14 * (val[1] - 0.5) * wgt[1];
// Rock gets stronger patchiness at 16 m and 8 m so the texture sets' 5 m tiles do not read as a weave.
rockc *= 1.0 + 0.25 * (val[0] - 0.5) * wgt[0] + 0.15 * (val[1] - 0.5) * wgt[1];
float3 land = lerp(soil, rockc, rocky);
// Scrub: dark bushes a few metres across on the gentler, moister ground; a mean tint holds the brightness where
// the dots have faded with distance.
float scrubzone = (1.0 - steep) * saturate(wet * 1.6 + 0.15) * (1.0 - saturate(hN * ReliefCm / 350000.0));
land = lerp(land, land * float3(0.45, 0.5, 0.35), scrubzone * saturate(dotsA + 0.1 * (1.0 - saturate(450.0 / (dist * 0.0045) - 0.35))));
// Stones: pale specks on the slopes and rock, a little darker on their shaded side.
float stony = saturate(rocky * 1.2 + (1.0 - scrubzone) * 0.3) * (1.0 - sediment);
land = lerp(land, land * float3(1.25, 1.2, 1.12), stony * dotsB * 0.35);
// Canyon floor: pale sediment with darker braided channels, from a three-octave sum so the lattice stays hidden.
float chanf = (val[0] - 0.5) + 0.5 * (val[1] - 0.5) + 0.25 * (val[2] - 0.5);
float chan = smoothstep(0.82, 0.97, 1.0 - abs(chanf) * 2.4) * wgt[1];
float3 floorc = land * float3(1.08, 1.0, 0.9) * (1.0 - 0.28 * chan);
land = lerp(land, floorc, sediment * 0.7);
float3 U = normalize(float3(ORX, ORY, ORZ) + LP / PlanetRadiusCm);
// Settlements: Worley points at three scales (25 km regions, 6 km clusters, 1.6 km towns), each level present in
// only part of its cells, so the lights gather along a few belts with dark country between. Value noise peaked on
// its lattice corners, which from orbit read as rows of dots fanning into the poles.
// The points are SlonkCities' own, in planet space, so every town's buildings stand on its lit and paved ground: not
// in the wrapped noise space p, whose 10 km period no Worley level divides. The chunk's tile of the period (TX, TY,
// TZ: whole periods, exact in a float) and p within it give each level's cell in integers, as a period is 2^20 cm
// and the cells 2^12, 2^10 and 2^8 times 625 cm (a tile is 256, 1024 or 4096 625ths of a cell), and the fraction
// from what is left, small enough to keep its centimetres. The points are kept as offsets from the pixel (townRel
// is the pixel less the town's point), never as planet positions a float would round to metres.
// They are taken at sea level under the pixel (SlonkCities places its towns on the sea-level sphere): at a height the
// nearest point, and so the town, would be another.
float3 pS = p - U * (hN >= 0.0 ? hN * ReliefCm : hN * OceanDepthCm * (1.0 - water));
int3 fl0 = 0, fl1 = 0, fl2 = 0; float3 fr0 = 0, fr1 = 0, fr2 = 0;
float town = 0.0, cluster = 0.0, region = 0.0;
float3 townRel = 0; uint townH = 0u; float3 clusterRel = 0; uint clusterH = 0u; float3 town2Rel = 0; uint town2H = 0u; float paving = 0.0;
float dt1 = 99.0, dt2 = 99.0, dc1 = 99.0;
float wayCover = 0.0, wayGlow = 0.0;
{
    int3 tile = int3(round(TX), round(TY), round(TZ));
    float dmin[3] = { 9.0, 9.0, 9.0 };
    [unroll] for (int kw = 0; kw < 3; kw++)
    {
        float wcs = kw == 0 ? 2560000.0 : (kw == 1 ? 640000.0 : 160000.0);
        int per = kw == 0 ? 256 : (kw == 1 ? 1024 : 4096);      // 625ths of a cell to a period
        uint keep = kw == 0 ? 3u : (kw == 1 ? 3u : 3u);        // of four cells
        int3 tn = tile * per;
        int3 ti = (int3)floor((float3)tn / 625.0);
        float3 qw = (float3)(tn - ti * 625) / 625.0 + pS / wcs - 0.5;
        float3 fq = floor(qw); float3 frw = qw - fq;
        int3 flw = ti + (int3)fq;
        if (kw == 0) { fl0 = flw; fr0 = frw; } else if (kw == 1) { fl1 = flw; fr1 = frw; } else { fl2 = flw; fr2 = frw; }
        // The weights from the eight cells round the pixel (SlonkCities' NearestSq); the nearest settlement points from
        // the 27 round its nearest cell corner, only those near enough the surface to be settlements (its NearestPoints).
        int3 rb = int3(frw >= 0.5) - 1;
        int nw = kw == 0 ? 8 : 27;
        [loop] for (int iw = 0; iw < nw; iw++)
        {
            int3 off = kw == 0 ? int3(iw & 1, (iw >> 1) & 1, (iw >> 2) & 1) : rb + int3(iw % 3, (iw / 3) % 3, iw / 9);
            int3 cwk = (flw + off) & 4095;
            uint hw = ((uint)cwk.x * 73856093u) ^ ((uint)cwk.y * 19349663u) ^ ((uint)cwk.z * 83492791u) ^ (0x9e3779b9u + (uint)kw * 0x85ebca6bu);
            hw = (hw ^ (hw >> 13)) * 0x5bd1e995u; hw ^= hw >> 15;
            if ((hw >> 30) >= keep) continue;
            float3 jit = float3(hw & 0x3ffu, (hw >> 10) & 0x3ffu, (hw >> 20) & 0x3ffu) / 1023.0;
            float3 dp = float3(off) + jit - frw;
            float dd = dot(dp, dp);
            if (all(off >= 0) && all(off <= 1)) { dmin[kw] = min(dmin[kw], dd); }
            if (kw == 0) continue;
            float3 rel = -dp * wcs;
            if (abs(dot(rel, U)) > 0.9 * wcs) continue;
            if (kw == 2 && dd < dt1) { town2Rel = townRel; town2H = townH; dt2 = dt1; townRel = rel; townH = hw; dt1 = dd; }
            else if (kw == 2 && dd < dt2) { town2Rel = rel; town2H = hw; dt2 = dd; }
            if (kw == 1 && dd < dc1) { clusterRel = rel; clusterH = hw; dc1 = dd; }
        }
    }
    region = 1.0 - smoothstep(0.05, 0.7, dmin[0]);
    cluster = 1.0 - smoothstep(0.02, 0.5, dmin[1]);
    town = 1.0 - smoothstep(0.03, 0.3, dmin[2]);
}
// The ground's glow comes with the people's light (Civilization.h's GroundLights): a city's electric light, or the dim
// warm firelight of a stone- or iron-age people's hearths; a world with nobody keeps its bare ground.
float lit = NightLightIntensity > 0.0 ? 1.0 : 0.0;
float settled = lit;
// The people live at only SettleDensity of the settlement points (SlonkCities' cut on each point's hash): the ground
// is paved and lit under the ones they live at.
uint hdT = townH ^ (0xD5u + 0x9e3779b9u + (townH << 6) + (townH >> 2)); hdT = (hdT ^ (hdT >> 13)) * 0x5bd1e995u; hdT ^= hdT >> 15;
uint hdC = clusterH ^ (0xD5u + 0x9e3779b9u + (clusterH << 6) + (clusterH >> 2)); hdC = (hdC ^ (hdC >> 13)) * 0x5bd1e995u; hdC ^= hdC >> 15;
float keptT = (float)(hdT & 0xffffu) / 65535.0 <= SettleDensity ? 1.0 : 0.0;
float keptC = (float)(hdC & 0xffffu) / 65535.0 <= SettleDensity ? 1.0 : 0.0;
float keptT2 = SlonkU(SlonkMix(town2H, 0xD5u)) <= SettleDensity ? 1.0 : 0.0;
settled *= max(keptT, keptC * step(0.35, region));
// Clearings round the settlements, where the people live: every city point within a cell of the pixel across the ground
// whose region is strong enough, and every town point within a cell whose region x cluster is and that no city claims
// (as EnumerateTowns skips one inside a city's ground; the city points listed for that, lived at or not, at most four).
// The points of the 27 cells round the pixel's nearest corner, as the search above takes them: all within a cell are
// among them. SlonkCities::ClearingAt mirrors this: the scatter thins its trees by it, so the ground and the trees part
// at the same edge, and it is the pixel's own, the same from any height. Only where the scatter clears its trees for
// settlements (Clearings, its own flag), and only over any forest: the clearing scales the forest alone.
// The forest's density at the vertex (UV3.y, the chance of a tree in its lattice cell) by the world's life and the
// density setting (TreeLife, as the scatter takes them), capped as the scatter caps it.
float treeD = min(UV3.y * TreeLife, 0.9);
float clearing = 0.0;
if (lit > 0.0 && Clearings > 0.5 && treeD > 0.0)
{
    float3 cityU[4] = { float3(0, 0, 0), float3(0, 0, 0), float3(0, 0, 0), float3(0, 0, 0) };
    float cityR[4] = { 0.0, 0.0, 0.0, 0.0 };
    int nCity = 0;
    [unroll] for (int lv = 1; lv <= 2; lv++)
    {
        float wcs = lv == 1 ? 640000.0 : 160000.0;
        int3 flv = lv == 1 ? fl1 : fl2; float3 frv = lv == 1 ? fr1 : fr2;
        int3 rbv = int3(frv >= 0.5) - 1;
        [loop] for (int iw = 0; iw < 27; iw++)
        {
            int3 off = rbv + int3(iw % 3, (iw / 3) % 3, iw / 9);
            int3 cwk = (flv + off) & 4095;
            uint hw = ((uint)cwk.x * 73856093u) ^ ((uint)cwk.y * 19349663u) ^ ((uint)cwk.z * 83492791u) ^ (0x9e3779b9u + (uint)lv * 0x85ebca6bu);
            hw = (hw ^ (hw >> 13)) * 0x5bd1e995u; hw ^= hw >> 15;
            if ((hw >> 30) >= 3u) { continue; }
            float3 jit = float3(hw & 0x3ffu, (hw >> 10) & 0x3ffu, (hw >> 20) & 0x3ffu) / 1023.0;
            float3 rel = -(float3(off) + jit - frv) * wcs;
            if (abs(dot(rel, U)) > 0.9 * wcs) { continue; }
            float3 relU = rel - U * dot(rel, U);
            if (dot(relU, relU) >= wcs * wcs) { continue; }
            float rg = 1.0 - smoothstep(0.05, 0.7, SlonkNearestSqAt(fl0, fr0 - rel / 2560000.0, 0u));
            bool kept = SlonkU(SlonkMix(hw, 0xD5u)) <= SettleDensity;
            if (lv == 1)
            {
                if (rg < 0.35) { continue; }
                float cr = 640000.0 * (0.25 + 0.2 * rg);
                if (nCity < 4) { cityU[nCity] = relU; cityR[nCity] = cr; nCity++; }
                if (kept) { clearing = max(clearing, SlonkClearingOf(rel, normalize(U * PlanetRadiusCm - rel), cr, hw, wcs)); }
            }
            else
            {
                float sT = rg * (1.0 - smoothstep(0.02, 0.5, SlonkNearestSqAt(fl1, fr1 - rel / 640000.0, 1u)));
                if (sT < 0.3 || !kept) { continue; }
                float rT = 160000.0 * (0.2 + 0.3 * sT);
                bool claimedT = false;
                [unroll] for (int cj = 0; cj < 4; cj++) { if (cj < nCity && length(cityU[cj] - relU) < cityR[cj] + rT + 30000.0) { claimedT = true; } }
                if (!claimedT) { clearing = max(clearing, SlonkClearingOf(rel, normalize(U * PlanetRadiusCm - rel), rT, hw, wcs)); }
            }
        }
    }
}
// Forest: the share of the ground the trees' crowns cover there (from treeD, the density that places the trees:
// a crown covers about 1.7 of its lattice cell, the crowns overlapping at random), painted the canopy's colour: the trees' own tint (the wet ground's colour browned toward
// the dry's) darkened by the shade inside and between the crowns (CanopyShade, matched to drawn forest against this
// ground with the trees hidden). A forest too far off for its trees to be drawn is there in colour, and the trees
// fade in over their own colour, not over bare soil. That is the cover seen from straight above; looked at from the
// side the crowns hide more of the ground between them, so the gap left (one minus the cover) goes as a canopy's by
// the Beer-Lambert law, to the power of the slant (CoverGain for how tall the trees stand for their spacing).
float vmu = max(dot(Nn, normalize(Cam - WP)), 0.08);
float cover = 1.0 - pow(saturate(exp(-treeD * 1.7) + (1.0 - exp(-treeD * 1.7)) * clearing), CoverGain / vmu);
float3 canopy = lerp(LandColor, LandColor2, saturate(1.0 - wet * 1.3) * 0.55) * CanopyShade;
land = lerp(land, canopy, saturate(cover));
land *= 0.8 * (0.94 + 0.12 * (fb0 + 0.5));
land *= 1.0 - saturate(rocky - sediment) * 0.45 * smoothstep(0.72, 1.0, crack);
float snowy = ice * (1.0 - saturate(slope * 3.0)) ;
land = lerp(land, IceColor, snowy);
float3 dpx = ddx(p), dpy = ddy(p);
// Turquoise over the first forty metres of water, the deep colour beyond; the depth is in cm, not a fraction
// of the ocean floor, so a shelf sea reads as sea and not as a lagoon.
float depth = smoothstep(0.0, 4000.0, max(-hN * OceanDepthCm, 0.0));
float3 sea = lerp(ShallowColor, OceanColor, depth);
// Rivers (water above sea level): darker, greener, silt in the shallows along the banks.
float river = water * saturate(hN * ReliefCm / 500.0);
sea = lerp(sea, float3(0.06, 0.16, 0.17) * (0.9 + 0.3 * (fb0 + 0.5)), river);
// Night on the ground: the side turned from the sun, with a little of the twilight.
float night = saturate(-dot(Nn, normalize(SunDirection)) * 6.0 + 0.1);
// ---- The sea surface. Wind waves from a baked normal (T_Water_NR: normal xy, foam in A) at two scales that
// scroll with time, laid down by a cube-face projection on the radial (one seam band per cube edge, lost in
// the chop). They fade out by twenty kilometres so the sun glint from orbit falls on the smooth macro
// surface, where any fine normal scattered it to nothing. The sand shows through the first metres of water,
// foam lines the shore and rides the crests, and rivers get smaller ripples. On a living world (PlanktonGlow,
// ShoalDensity) shoals swim under the surface and the plankton glow where the sea is stirred at night; a ship
// hovering low over it (ShipStir) stirs it. All three are 0 on every other world, which skips them.
float3 waterN = 0; float foam = 0; float plankton = 0.0;
if (water > 0.001)
{
    float3 aU = abs(U);
    float2 pu, dux, duy; float3 Tw, Bw;
    if (aU.x >= aU.y && aU.x >= aU.z) { pu = p.yz; dux = dpx.yz; duy = dpy.yz; Tw = float3(0, 1, 0); Bw = float3(0, 0, 1); }
    else if (aU.y >= aU.z) { pu = p.xz; dux = dpx.xz; duy = dpy.xz; Tw = float3(1, 0, 0); Bw = float3(0, 0, 1); }
    else { pu = p.xy; dux = dpx.xy; duy = dpy.xy; Tw = float3(1, 0, 0); Bw = float3(0, 1, 0); }
    float waveFade = 1.0 - smoothstep(300000.0, 2000000.0, dist);
    float tile1 = lerp(600.0, 180.0, river), tile2 = lerp(3500.0, 900.0, river);
    // The second scale is turned 37 degrees so the two lattices never line up into a grid of glints.
    const float2x2 Rot = float2x2(0.7986, -0.6018, 0.6018, 0.7986);
    float2 pu2 = mul(Rot, pu), dux2 = mul(Rot, dux), duy2 = mul(Rot, duy);
    float2 uv1 = pu / tile1 + Time * float2(0.030, 0.011);
    float2 uv2 = pu2 / tile2 - Time * float2(0.006, 0.014);
    float4 w1 = Texture2DSampleGrad(WaterNR, WaterNRSampler, uv1, dux / tile1, duy / tile1);
    float4 w2 = Texture2DSampleGrad(WaterNR, WaterNRSampler, uv2, dux2 / tile2, duy2 / tile2);
    float2 n2 = mul(float2x2(0.7986, 0.6018, -0.6018, 0.7986), w2.rg * 2.0 - 1.0);
    float2 nxy = (w1.rg * 2.0 - 1.0) * 0.55 + n2 * 0.45;
    waterN = (Tw * nxy.x + Bw * (SLONK_TEX_FLIPY * nxy.y)) * (0.3 * waveFade);
    // The swell's own normal, from the same Gerstner sum that moves the vertices, within its fade.
    float3 gd, gn;
    SlonkGerstner(pu, Time, NoisePeriodCm, SwellCm, gd, gn);
    float swellFade = 1.0 - smoothstep(80000.0, 250000.0, dist);
    waterN += (Tw * gn.x + Bw * gn.y) * swellFade;
    foam = 0.0;
    float depthCm = max(-hN * OceanDepthCm, 0.0);
    // Surf: foam over the first four metres of depth, broken up by the crest mask so it is a ragged line.
    float shore = (1.0 - smoothstep(0.0, 400.0, depthCm)) * (1.0 - river);
    foam = saturate(shore * (0.55 + 0.6 * w1.a + 0.3 * w2.a) + 0.35 * w1.a * w2.a * waveFade
        + 0.6 * smoothstep(0.55, 0.85, gd.z / max(SwellCm * 1.2, 1.0)) * w1.a * swellFade);
    // A ship's wake (ShipStir, APlanetBody::ShipStirOf: by its height over the sea's level, 1 at the water and 0 from
    // 200 m up, about 0.7 for a ship held 30 m up by its ground clearance; none from a craft set down, and given only on
    // a world whose seas live): rings of foam spreading from the point on the sea under it (ShipGroundPos, world space),
    // one every two seconds out to sixty metres, and the churn of its downwash below it, on the sea alone. Measured
    // across the sea's surface, so the swell's rise and fall does not move it (its sway does, by under a metre), and
    // only within a hundred metres of that point: the far side of the world lies square to the sea there too.
    float wake = 0.0;
    if (ShipStir > 0.0)
    {
        float3 toShip = WP - ShipGroundPos.xyz;
        float shipR = dot(toShip, toShip) < 1.0e8 ? length(toShip - Nn * dot(toShip, Nn)) : 1.0e9;
        [unroll] for (int kr = 0; kr < 2; kr++)
        {
            float age = frac(Time * 0.25 + 0.5 * (float)kr);
            float ringR = 600.0 + 5400.0 * age;
            wake += (1.0 - smoothstep(0.0, 120.0 + 380.0 * age, abs(shipR - ringR))) * (1.0 - age) * smoothstep(0.0, 0.08, age);
        }
        wake = (wake + 1.0 - smoothstep(300.0, 1500.0, shipR)) * ShipStir * (1.0 - river);
        foam = saturate(foam + wake * (0.35 + 0.65 * w1.a));
    }
    float3 sand = LandColor2 * 0.85;
    sea = lerp(lerp(sand, sea, smoothstep(0.0, 600.0, depthCm)), sea, river);
    // Shoals (ShoalDensity, the share of cells holding one): fish swimming 3 to 40 m down, dark shapes on the sea that
    // fade the deeper they swim, the sun glinting off a flank here and there by day, gone by three kilometres. One in a
    // cell of a lattice on the projection above (52 cells to the noise period, about 200 m), each swimming a loop round
    // a point near the middle of its cell, so it never leaves it and only its own cell is looked at. SlonkFauna::ShoalAt
    // (FaunaHabitat.cpp) mirrors this for the game: a change to the cells, the hash, the gate or the loop goes there too,
    // and to the test Slonk.Fauna.ShoalMirror's float copy of this block, which fails until its copy is brought in line
    // and its CRC of the block (from the "if (ShoalDensity" line to the "if (se < 1.3)" line) is set anew.
    // Nothing of them shows in the dark of the night side, where they are left out.
    float shoalFade = (1.0 - smoothstep(150000.0, 300000.0, dist)) * (1.0 - river) * (1.0 - SeaIce * smoothstep(0.4, 0.7, ice));
    if (ShoalDensity > 0.0 && shoalFade > 0.0 && night < 1.0)
    {
        float scs = NoisePeriodCm / 52.0;
        float2 sq = pu / scs; float2 sfl = floor(sq); float2 sfr = sq - sfl;
        int2 sc = ((int2)sfl % 52 + 52) % 52;
        uint sax = (aU.x >= aU.y && aU.x >= aU.z) ? 0u : (aU.y >= aU.z ? 1u : 2u);
        uint sh = ((uint)sc.x * 73856093u) ^ ((uint)sc.y * 19349663u) ^ ((41u + sax) * 2654435761u);
        sh = (sh ^ (sh >> 13)) * 0x5bd1e995u; sh ^= sh >> 15;
        if ((float)(sh & 0xffu) / 255.0 < ShoalDensity)
        {
            uint sh2 = (sh ^ 0x9e3779b9u) * 0x85ebca6bu; sh2 ^= sh2 >> 13; sh2 *= 0xc2b2ae35u; sh2 ^= sh2 >> 16;
            float2 sctr = 0.4 + 0.2 * float2((float)((sh >> 8) & 0xffu), (float)((sh >> 16) & 0xffu)) / 255.0;
            float loopR = 0.08 + 0.07 * (float)((sh >> 24) & 0xffu) / 255.0;
            float sdepth = 300.0 + 3700.0 * (float)(sh2 & 0xffu) / 255.0;
            float sphase = 6.2831853 * (float)((sh2 >> 8) & 0xffu) / 255.0;
            float sspeed = 60.0 + 90.0 * (float)((sh2 >> 16) & 0xffu) / 255.0;
            float shalf = 0.06 + 0.09 * (float)((sh2 >> 24) & 0x7fu) / 127.0;
            float sturn = (sh2 >> 31) != 0u ? -1.0 : 1.0;
            float ssn, scn; sincos(sphase + sturn * sspeed / (loopR * scs) * Time, ssn, scn);
            float2 sdir = sturn * float2(-ssn, scn);
            float2 sd = sfr - (sctr + loopR * float2(scn, ssn));
            float along = dot(sd, sdir), across = sd.y * sdir.x - sd.x * sdir.y;
            float se = length(float2(along, across / 0.45)) / shalf;
            if (se < 1.3)
            {
                // A school's edge wavers as its fish shift about.
                se += 0.1 * sin(3.0 * atan2(across, along + 1e-6) + sphase + 0.7 * Time);
                float scov = (1.0 - smoothstep(0.55, 1.0, se)) * shoalFade * (1.0 - 0.7 * smoothstep(300.0, 4000.0, sdepth))
                    * smoothstep(sdepth + 50.0, sdepth + 300.0, depthCm);
                sea *= 1.0 - 0.7 * scov;
                // The glints: a speck in one in fourteen of the 120 cm cells of the school's own frame, each lit a quarter
                // of a second at a time, by day, while a speck is still about a pixel across.
                float2 gq = float2(along, across) * (scs / 120.0);
                float2 gfl = floor(gq);
                uint gh = ((uint)(int)gfl.x * 73856093u) ^ ((uint)(int)gfl.y * 19349663u) ^ ((uint)(int)floor(Time * 4.0) * 83492791u) ^ sh2;
                gh = (gh ^ (gh >> 13)) * 0x5bd1e995u; gh ^= gh >> 15;
                float glint = (gh & 0xffu) < 18u ? 1.0 - smoothstep(0.12, 0.35, length(gq - gfl - 0.5)) : 0.0;
                sea = lerp(sea, float3(0.9, 0.93, 0.95), glint * scov * (1.0 - night) * saturate(120.0 / (dist * 0.0045) - 0.35));
            }
        }
    }
    sea = lerp(sea, float3(0.88, 0.9, 0.92), foam);
    // Plankton (PlanktonGlow): where a bloom drifts on the current (value noise of 26 cells to the noise period, about
    // 400 m, moving through the sea with Time) the sea glows where it is stirred: in the foam (the swell's breaking
    // crests, broken up by the water's own texture, so the glow never lines up with the swell's regular crests), all
    // along the surf, and in a ship's wake. Given off at night only (Emis, at the end), in PlanktonColor, so
    // left out by day; and the bloom is left out where nothing it shapes shows: where nothing stirs the sea (the open
    // sea past the waves' twenty kilometres) beyond the milky sea's sixty.
    bool bGlowNight = PlanktonGlow > 0.0 && night > 0.0;
    float stir = bGlowNight ? foam + shore * (0.3 + 0.7 * w1.a) + 4.0 * wake : 0.0;
    // The bloom's 400 m cells even out to their mean once they are under a pixel, as the noise octaves do, so a night
    // coast seen from high up glows steadily instead of in a crawling speckle.
    float bloomFade = saturate((NoisePeriodCm / 26.0) / (dist * 0.0045) - 0.35);
    float bloomV = 0.0;
    if (bGlowNight && bloomFade > 0.0 && (stir > 0.0 || dist < 6000000.0))
    {
        float3 pq = (p + Time * float3(23.0, -17.0, 11.0)) / (NoisePeriodCm / 26.0);
        float3 pfl = floor(pq); float3 pfr = pq - pfl; pfr = pfr * pfr * (3.0 - 2.0 * pfr);
        int3 pc0 = (int3)pfl;
        float bh[8];
        [unroll] for (int ib = 0; ib < 8; ib++)
        {
            int3 pc = ((pc0 + int3(ib & 1, (ib >> 1) & 1, (ib >> 2) & 1)) % 26 + 26) % 26;
            uint hb = ((uint)pc.x * 73856093u) ^ ((uint)pc.y * 19349663u) ^ ((uint)pc.z * 83492791u) ^ (47u * 2654435761u);
            hb = (hb ^ (hb >> 13)) * 0x5bd1e995u; hb ^= hb >> 15;
            bh[ib] = (float)(hb & 0xffffu) / 65535.0;
        }
        bloomV = lerp(lerp(lerp(bh[0], bh[1], pfr.x), lerp(bh[2], bh[3], pfr.x), pfr.y), lerp(lerp(bh[4], bh[5], pfr.x), lerp(bh[6], bh[7], pfr.x), pfr.y), pfr.z);
    }
    if (bGlowNight)
    {
        // 0.35 and 0.1: about the means of the bloom's two smoothsteps over this value noise. Where a bloom is thickest
        // the sea glows faintly of itself as well, unstirred (a milky sea), in drifting patches: in full within twenty
        // kilometres, fading out by sixty, gone before the night side is seen from orbit.
        float stirred = lerp(0.35, smoothstep(0.35, 0.8, bloomV), bloomFade) * stir;
        float milky = 0.35 * lerp(0.1, smoothstep(0.65, 0.95, bloomV), bloomFade) * (1.0 - smoothstep(2000000.0, 6000000.0, dist));
        // The milky glow comes up from within the water, through its surface, which lets less of it out the flatter the sea
        // is seen (it mirrors the black night sky instead: Schlick's Fresnel for water, 1 seen from straight over it). Only
        // where the bloom's cells are squashed under a pixel or so along the view, though: their 400 m foreshortened by the
        // slant, against the footprint bloomFade weighs them by, the Fresnel weight in full under 0.35 of it and gone by 0.85.
        // So the far sea seen edge-on, kilometres of its blooms evened into a few rows at the horizon, no longer glows as a
        // bright line over a dark near sea, and a sea whose blooms still show shows them exactly as before. The stirred glow
        // is the foam's, at the surface, and shows as before (SlonkFauna::MilkySeenThrough copies this).
        float cosV = saturate(dot(Nn, normalize(Cam - WP)));
        float bloomSeen = saturate(2.0 * ((NoisePeriodCm / 26.0) * cosV / (dist * 0.0045) - 0.35));
        float milkyOut = 1.0 - (1.0 - bloomSeen) * pow(1.0 - cosV, 5.0);
        plankton = PlanktonGlow * (stirred + milky * milkyOut) * water * (1.0 - river) * (1.0 - SeaIce * smoothstep(0.4, 0.7, ice));
    }
}
// Texture sets (see FUNCS): cliff on the steep ground, talus on rocky slopes, scrub where it is wet and flat,
// soil elsewhere; the two or so layers present are blended by their heights. Each set modulates the colour
// decided above (its albedo over its mean), perturbs the normal, and brings roughness and occlusion up close.
float nearFade = (1.0 - 0.5 * smoothstep(10000.0, 60000.0, dist)) * (1.0 - smoothstep(100000.0, 250000.0, dist));
float3 texMod = 1.0, texN = 0.0, texOrm = float3(1.0, 0.85, 0.5);
if (nearFade > 0.001 && water < 0.5)
{
    float rockyT = saturate(rocky * 1.3);
    float wC = cliff;
    float wT = (1.0 - cliff) * rockyT * (1.0 - sediment);
    float wS = (1.0 - cliff) * (1.0 - rockyT) * saturate(scrubzone * 1.5);
    float wG = max(1.0 - wC - wT - wS, 0.0);
    float3 mAcc = 0, nAcc = 0, oAcc = 0; float wAcc = 0;
#define SLONK_LAYER(W, SET, MEAN, SALT) if (W > 0.1) { float3 m_, n_, o_; SlonkLayer(SET##BC, SET##BCSampler, SET##NR, SET##NRSampler, MEAN, p, dpx, dpy, U, Nn, NoisePeriodCm, nearFade, SALT, m_, n_, o_); float w_ = W * pow(0.25 + o_.b, 4.0); mAcc += w_ * m_; nAcc += w_ * n_; oAcc += w_ * o_; wAcc += w_; }
    SLONK_LAYER(wC, Cliff, float3(0.571, 0.528, 0.485), 1u)
    SLONK_LAYER(wT, Talus, float3(0.462, 0.409, 0.363), 2u)
    SLONK_LAYER(wG, Soil, float3(0.666, 0.564, 0.429), 3u)
    SLONK_LAYER(wS, Scrub, float3(0.318, 0.275, 0.171), 4u)
    if (wAcc > 0.0) { texMod = mAcc / wAcc; texN = nAcc / wAcc; texOrm = oAcc / wAcc; }
    texMod = lerp(texMod, 1.0, snowy); texN *= 1.0 - snowy;
}
// Lineae on an icy crust: the band between the ridges is stained brown (hydrated salts and sulphur) and a
// little rougher than the ice around it.
float linea = GeoKind > 0.5 ? 0.0 : UV3.x;
land = lerp(land, land * float3(0.78, 0.5, 0.34), linea * 0.85);
land *= texMod;
// A strange world's own geology, where the vertex carries its mark in the lineae's place (GeoKind says which kind):
// drawn over the land in the world's two geology colours, clear of the water and, but for a frozen pan's troughs, of
// the snow.
float geoAll = GeoKind > 0.5 ? smoothstep(0.1, 0.5, UV3.x) * (1.0 - water) : 0.0;
float geo = geoAll * (1.0 - snowy);
float geoGloss = 0.0;
// Where no one builds: the mark itself, snow or no snow, gone by the tenth of it at which SlonkCities founds no town
// and takes no lot (its LandFactor and Buildable), so no street is paved or lit where nothing is built.
float geoBuilt = GeoKind > 0.5 ? smoothstep(0.02, 0.1, UV3.x) : 0.0;
[branch] if (geoAll > 0.001)
{
    // The cells of a jittered lattice in the ground's own space (a whole number to the period, so they carry across a
    // wrap): a crystal field's facets, ten metres across; a pan's polygons and a terrace's pools, twenty; a glass
    // plain's plates, and a spire forest's spires seen from too far for the mesh to carry them, forty. A line is drawn
    // where the pixel is as near the next cell's point as its own (crack), each cell has a shade of its own (cellv),
    // and gd1 is how far the pixel is from its cell's point, squared.
    int gn = GeoKind < 1.5 ? 1024 : (GeoKind < 3.5 ? 512 : 256);
    float3 gq = p / (NoisePeriodCm / (float)gn);
    float3 gqi = floor(gq); float3 gqf = gq - gqi;
    float gd1 = 9.0, gd2 = 9.0;
    uint gbest = 0u;
    for (int gz = -1; gz <= 1; gz++) { for (int gy = -1; gy <= 1; gy++) { for (int gx = -1; gx <= 1; gx++)
    {
        float3 gc = float3(gx, gy, gz);
        int3 gi = int3(gqi + gc);
        gi = ((gi % gn) + gn) % gn;
        uint gh = ((uint)gi.x * 73856093u) ^ ((uint)gi.y * 19349663u) ^ ((uint)gi.z * 83492791u) ^ (77u * 2654435761u);
        gh = (gh ^ (gh >> 13)) * 0x5bd1e995u; gh ^= gh >> 15;
        float3 gr = gc + float3((float)(gh & 0xffu), (float)((gh >> 8) & 0xffu), (float)((gh >> 16) & 0xffu)) / 255.0 - gqf;
        float gd = dot(gr, gr);
        if (gd < gd1) { gd2 = gd1; gd1 = gd; gbest = gh; } else if (gd < gd2) { gd2 = gd; }
    } } }
    // The line thinner than a pixel far off is faded rather than left to sparkle, and what each cell has of its own
    // evens out to the mean as the cells go under a pixel.
    float gedge = sqrt(gd2) - sqrt(gd1);
    float gpix = dist * 0.0012 / (NoisePeriodCm / (float)gn);
    float crack = (1.0 - smoothstep(0.03 + gpix, 0.08 + gpix * 2.0, gedge)) * saturate(0.06 / max(gpix, 0.06));
    float gseen = saturate(0.3 / max(gpix, 0.3));
    float cellv = lerp(0.5, (float)((gbest >> 24) & 0xffu) / 255.0, gseen);
    if (GeoKind < 1.5)
    {
        // Crystal: clear on the faces that look up, deep on its walls, each facet a depth of its own, and glassy.
        float3 cry = lerp(GeoColor2, GeoColor, saturate(1.0 - slope * 2.2) * 0.8 + 0.2 * saturate(fb0 + 0.5)) * (0.7 + 0.6 * cellv);
        land = lerp(land, lerp(cry, GeoColor2, crack * 0.6), geo * 0.92);
        geoGloss = geo * 0.85;
    }
    else if (GeoKind < 2.5)
    {
        // A pan: salt cracked into polygons; on frozen ground the snow's own, its troughs drawn darker.
        land = lerp(land, lerp(GeoColor, GeoColor2, crack * 0.8) * (0.92 + 0.16 * saturate(fb0 + 0.5)), geo * 0.9);
        land *= 1.0 - 0.4 * crack * geoAll * snowy;
    }
    else if (GeoKind < 3.5)
    {
        // Terraces: pale stone on the risers, and on the level treads a mosaic of pools, a rim of the stone between
        // one and the next; each pool pale over the stone where it shallows to its rim, its liquid's own colour
        // where it deepens toward its middle.
        float tread = 1.0 - smoothstep(0.03, 0.12, slope);
        float pool = tread * (1.0 - crack);
        float3 poolC = lerp(lerp(GeoColor, GeoColor2, 0.5), GeoColor2 * (0.75 + 0.35 * cellv), saturate(gedge * 2.5));
        land = lerp(land, lerp(GeoColor, poolC, pool), geo * 0.9);
        geoGloss = geo * pool * 0.9;
    }
    else if (GeoKind < 4.5)
    {
        // Spires: banded rock, a darker bed every few metres of height, clearest on the steep; the floor between them
        // the same stone, bare. On chunks too coarse for their mesh to carry them (PlanetTerrain.cpp's Refine fades
        // the hoodoos out between 80 and 200 m of wavelength, two of the chunk's cells) the forest is stippled dark
        // instead, a spire and its shadow to a cell, evening out to their mean shade as the cells go under a pixel.
        float bed = 0.5 + 0.5 * sin(hN * ReliefCm / 350.0 + fb0 * 3.0);
        land = lerp(land, lerp(GeoColor, GeoColor2, bed * 0.6 * saturate(0.3 + slope * 2.0)), geo * (0.65 + 0.25 * saturate(slope * 2.0)));
        float spot = lerp(0.35, 1.0 - smoothstep(0.04, 0.2, gd1), gseen);
        land *= 1.0 - 0.45 * spot * geo * smoothstep(8000.0, 20000.0, Cell * 2.0);
    }
    else
    {
        // Glass: dark and mirror-smooth, crazed with pale hairlines, each plate a shade of its own.
        float craze = crack * crack;
        land = lerp(land, lerp(GeoColor, GeoColor2, craze * 0.6) * (0.85 + 0.3 * cellv), geo * 0.95);
        geoGloss = geo * (1.0 - craze) * 0.95;
    }
}
float3 base = lerp(land, sea, water);
// Sea ice: on a tidally locked world (SeaIce 1) the vertex ice runs out over the seas of the far side and the cold
// edge of the ring, where the sea freezes at its surface. It takes the ice's colour, a little grey where it is thin,
// with no water normal and no swell; every other world's seas (SeaIce 0) are as they were.
float seaIce = SeaIce * smoothstep(0.4, 0.7, ice) * water;
base = lerp(base, IceColor * lerp(0.82, 1.0, smoothstep(0.7, 1.0, ice)), seaIce);

// Detail bump from the analytic gradient, scaled by a height that grows with rockiness, plus the texture normals.
float bumpCm = (25.0 + 320.0 * rocky) * (1.0 - water) * (1.0 - snowy * 0.6) * (1.0 - geoGloss);
float dh1 = dot(gfb, T1) * bumpCm;
float dh2 = dot(gfb, T2) * bumpCm;
// A geology's glassy faces (crystal, a pool, a plate of glass) carry none of the soil's or the rock's texture.
texN *= 1.0 - geoGloss;
Nrm = normalize(Nn - T1 * dh1 - T2 * dh2 + texN * (1.0 - water) + waterN * water * (1.0 - seaIce));
float seaRough = lerp(OceanRoughness, 0.12, river);
seaRough = lerp(lerp(seaRough, 0.09, 0.5 * (1.0 - smoothstep(300000.0, 2000000.0, dist))), 0.7, foam);
Rough = lerp(lerp(LandRoughness * (0.8 + 0.2 * rocky), 0.35, snowy), seaRough, water);
Rough = lerp(Rough, texOrm.g, 0.6 * nearFade * (1.0 - water) * (1.0 - snowy));
Rough = lerp(Rough, 0.8, linea * 0.5);
Rough = lerp(Rough, 0.07, geoGloss);
Rough = lerp(Rough, 0.35, seaIce);
AO = lerp(1.0, texOrm.r, nearFade * (1.0 - water) * (1.0 - geoGloss));

float lowland = 1.0 - saturate(hN * ReliefCm / 250000.0);
// The settlement points were found above the forest, which their clearings thin.
// SettleWet is the driest ground the world's people settle (Civilization.h: 0.3, or none at all for a desert people), as
// SlonkCities gates its towns.
// No one builds on a strange world's own geology (SlonkCities founds no town and takes no lot there).
float city = settled * region * cluster * town * (1.0 - water) * saturate((wet - SettleWet + 0.05) * 4.0) * (1.0 - saturate(slope * 12.0)) * lowland * saturate(fb0 * 4.0 + 0.6) * (1.0 - geoBuilt);
// Built-up ground by day: paving and roofs grey the land where the lights are at night, where the people pave
// (StreetGrid: an industrial or later people that builds on a grid; not a village's, nor a grown town's).
base = lerp(base, float3(0.30, 0.29, 0.28), saturate(city * 1.5) * 0.7 * StreetGrid);
// Streets and ways: a city at a cluster point or a town at a town point lays out its ground as its people do
// (WayStyle, Civilization.h's EWayStyle). A grid-building people's blocks on a grid (bearing and pitch from its seed,
// as the buildings are laid), the streets between the lots and a wider avenue every fifth and seventh row paved and
// lit, the lots greyed; a grown town's ring ways and spiral spokes and its plaza in pale stone, their edges aglow at
// night; a village's dirt lanes, a hamlet's trodden paths. Not at a point the people would sooner have built into a
// cliff (CliffAffinity: whether they found one is not known here), where the grid-builders' streets are all the same,
// as they try the open land first.
if (water < 0.5 && lit > 0.0)
{
    // Which settlement's ground this is, as SlonkCities chooses (its PaintAt mirrors this): the weights at the points
    // themselves, as EnumerateTowns measures them, not at the pixel (the cluster point's region, a city's weight; the
    // town point's region and cluster, a town's), the points themselves found among those near the surface.
    float regionC = 1.0 - smoothstep(0.05, 0.7, SlonkNearestSqAt(fl0, fr0 - clusterRel / 2560000.0, 0u));
    float regionT = 1.0 - smoothstep(0.05, 0.7, SlonkNearestSqAt(fl0, fr0 - townRel / 2560000.0, 0u));
    float clusterT = 1.0 - smoothstep(0.02, 0.5, SlonkNearestSqAt(fl1, fr1 - townRel / 640000.0, 1u));
    bool nearC = dc1 < 99.0, nearT = dt1 < 99.0;
    float cityR = 640000.0 * (0.25 + 0.2 * regionC);
    float townS = regionT * clusterT;
    float townR = 160000.0 * (0.2 + 0.3 * townS);
    // The footprints are round the sites SlonkCities builds on: each Worley point taken down onto the sphere (it lies
    // off the surface by up to half its cell, kilometres), the pixel's offset from it measured across the ground.
    float3 upC = normalize(U * PlanetRadiusCm - clusterRel), upW = normalize(U * PlanetRadiusCm - townRel);
    float3 relC = clusterRel - upC * dot(clusterRel, upC);
    float3 relW = townRel - upW * dot(townRel, upW);
    // A city's ground, lived at or not (no road crosses it); a town's, where no city has claimed it.
    bool cityClaim = nearC && regionC >= 0.35;
    bool inCity = cityClaim && length(relC) < cityR * 1.05;
    float3 relCU = clusterRel - U * dot(clusterRel, U), relTU = townRel - U * dot(townRel, U);
    bool claimed = cityClaim && length(relCU - relTU) < cityR + townR + 30000.0;
    bool inTownSite = !inCity && nearT && townS >= 0.3 && !claimed && length(relW) < townR * 1.05;
    // The roads' gate is the settlement weight at the pixel.
    float townSp = region * cluster;
    float3 rel = inCity ? relC : relW;
    uint H = inCity ? clusterH : townH;
    float kept = inCity ? keptC : keptT;
    float Rr = inCity ? cityR : townR;
    // The same land gating the lights and the builder use: level, wet, low ground only.
    float landOk = saturate((wet - SettleWet + 0.05) * 4.0) * (1.0 - saturate(slope * 12.0)) * lowland * (1.0 - geoBuilt);
    // The settlement's own up, at its site, and its frame (FTownFrame).
    float3 upT = inCity ? upC : upW;
    float3 eT = normalize(cross(float3(0, 0, 1), upT)); float3 nT = cross(upT, eT);
    float ang = SlonkU(SlonkMix(H, 11u)) * 3.14159265;
    float3 gX = eT * cos(ang) + nT * sin(ang); float3 gY = cross(upT, gX);
    float lx = dot(rel, gX), ly = dot(rel, gY);
    bool inSite = inCity || inTownSite;
    // A way fades out as it narrows to under a pixel on the screen, before it shimmers (a telephoto view shows it from
    // far off, a wide one loses it near); a grown way's glow widens to a pixel and a half instead, dimmed to match.
    float pixCm = max(max(length(dpx), length(dpy)), 1.0);
    float hwWay = WayStyle > 2.5 ? 300.0 : (WayStyle > 1.5 ? 75.0 : 175.0);
    float wayFade = smoothstep(0.3, 0.8, hwWay / pixCm);
    float gw = max(40.0, pixCm * 1.5);
    float glowFade = min(1.0, 80.0 / gw) * (1.0 - smoothstep(1500000.0, 4000000.0, dist));
    if (StreetGrid > 0.0 && inSite)
    {
        float pitch = 3600.0 + 1400.0 * SlonkU(SlonkMix(H, 17u));
        float gx = lx / pitch, gy = ly / pitch;
        float inTown = 1.0 - smoothstep(0.9, 1.0, length(rel) / Rr);
        float sx = abs(frac(gx) - 0.5), sy = abs(frac(gy) - 0.5);           // 0 mid-street, 0.5 mid-lot
        float street = max(1.0 - smoothstep(0.1, 0.14, sx), 1.0 - smoothstep(0.1, 0.14, sy));
        int ix = (int)round(gx), iy = (int)round(gy);
        float aveX = (ix % 5 == 0) ? 1.0 - smoothstep(0.38, 0.46, abs(frac(gx + 0.5) - 0.5)) : 0.0;
        float aveY = (iy % 7 == 0) ? 1.0 - smoothstep(0.38, 0.46, abs(frac(gy + 0.5) - 0.5)) : 0.0;
        inTown *= landOk * kept;
        paving = max(street, max(aveX, aveY)) * inTown;
        base = lerp(base, float3(0.30, 0.29, 0.28), inTown * 0.35 * (1.0 - SlonkParkOf(float2(lx, ly), Rr, H)));
    }
    else if (WayStyle > 0.5 && inSite && kept > 0.0 && SlonkU(SlonkMix(H, 0xC1u)) >= CliffAffinity)
    {
        float plaza;
        float clr = SlonkTownWays(lx, ly, H, WayStyle, inCity, Rr, plaza);
        float inTown = (1.0 - smoothstep(1.0, 1.05, length(rel) / Rr)) * landOk;
        wayCover = max((1.0 - smoothstep(-50.0, 30.0, clr)) * wayFade, WayStyle > 2.5 ? plaza : 0.0) * inTown;
        if (WayStyle > 2.5) { wayGlow = (1.0 - smoothstep(0.0, gw, abs(clr + 45.0))) * glowFade * inTown; }
    }
    // A road between the two nearest settlement points (the pair this pixel lies between), out of the town at its near
    // end and off any city's ground, where both are lived at (and, for a people who do not pave, neither is one they
    // would sooner have built into a cliff) and the pixel's ground will take one.
    bool roadEnds = keptT * keptT2 > 0.0 && (WayStyle < 0.5 || (SlonkU(SlonkMix(townH, 0xC1u)) >= CliffAffinity && SlonkU(SlonkMix(town2H, 0xC1u)) >= CliffAffinity));
    if (dt2 < 99.0 && !inCity && townSp > 0.08 && roadEnds)
    {
        // From the nearest site to the second, across the ground: both points' offsets laid in the pixel's own
        // tangent plane, so their heights off the sphere do not hold the road off the ground.
        float3 ap = townRel - U * dot(townRel, U);
        float3 ab = ap - (town2Rel - U * dot(town2Rel, U));
        float clr = SlonkRoad(ap, ab, U, townH, town2H, WayStyle, townR);
        float road = (1.0 - smoothstep(-100.0, 50.0, clr)) * (1.0 - saturate(slope * 8.0)) * saturate((wet - SettleWet + 0.1) * 5.0) * (1.0 - geoBuilt);
        if (WayStyle < 0.5) { paving = max(paving, road); }
        else
        {
            wayCover = max(wayCover, road * wayFade);
            if (WayStyle > 2.5) { wayGlow = max(wayGlow, (1.0 - smoothstep(0.0, gw, abs(clr + 45.0))) * glowFade * road); }
        }
    }
    base = lerp(base, float3(0.11, 0.11, 0.12), paving * 0.9);
    // Pale stone for the grown; for the early ages bare earth, darker and browner than any grass or scrub round it
    // (a dry world's ochre ground included), a village's lanes and tracks packed darker than a hamlet's trodden paths.
    float3 wayCol = WayStyle > 2.5 ? float3(0.66, 0.63, 0.56) : (WayStyle > 1.5 ? float3(0.31, 0.25, 0.18) : float3(0.24, 0.19, 0.14));
    base = lerp(base, wayCol, wayCover * (WayStyle > 1.5 && WayStyle < 2.5 ? 0.75 : 0.9));
}
Emis = NightLightColor * (NightLightIntensity * city * night + 0.35 * paving * night + 1.5 * wayGlow * night) + float3(1.0, 0.28, 0.05) * (LavaEmissive * water * (0.6 + 0.8 * (fb0 + 0.5)));
// The plankton's glow, stirred in the sea above, by night.
if (plankton > 0.0) { Emis += PlanktonColor.rgb * (plankton * night); }
return base;
"""

TEX_SETS = ["Cliff", "Talus", "Soil", "Scrub"]
TEX_KINDS = ["BC", "NR"]
TEX_FOLDER = "/Game/Slonk/Terrain"
TEX_INPUTS = [f"{s}{k}" for s in TEX_SETS for k in TEX_KINDS]

WPO_INPUTS = ["Fn", "LP", "Col", "OX", "OY", "OZ", "ORX", "ORY", "ORZ", "Cam", "WP", "PlanetRadiusCm", "NoisePeriodCm", "Time", "SwellCm", "UV2", "SeaIce"]
INPUTS = ["Fn", "N", "LP", "UV0", "UV1", "UV2", "UV3", "Col", "ColA", "OX", "OY", "OZ", "Cell", "ORX", "ORY", "ORZ", "Cam", "WP", "PlanetRadiusCm", "WaterNR", "Time", "SwellCm"] + TEX_INPUTS + [
          "OceanColor", "ShallowColor", "LandColor", "LandColor2", "MountainColor", "IceColor", "NightLightColor",
          "OceanRoughness", "LandRoughness", "NightLightIntensity", "LavaEmissive", "SunDirection",
          "NoisePeriodCm", "ReliefCm", "OceanDepthCm", "TX", "TY", "TZ", "StreetGrid", "SettleWet", "SettleDensity", "WayStyle", "CliffAffinity", "CanopyShade", "CoverGain", "TreeLife", "Clearings", "SeaIce",
          "PlanktonGlow", "PlanktonColor", "ShoalDensity", "ShipGroundPos", "ShipStir", "GeoKind", "GeoColor", "GeoColor2"]


def build():
    wipe(NAME)
    g = G(NAME, {"ShadingModel": "MSM_DefaultLit", "BlendMode": "BLEND_Opaque", "bTangentSpaceNormal": False, "TwoSided": False})
    n = g.node("VertexNormalWS")
    psp = g.node("PreSkinnedPosition")
    vi = g.node("VertexInterpolator"); g.c(psp, vi, "VS")
    uv0, uv1, uv2, uv3 = g.texcoord(0), g.texcoord(1), g.texcoord(2), g.texcoord(3)
    col = g.node("VertexColor")
    ox, oy, oz, cell = g.scalar("ChunkOX", 0.0, cpd=0), g.scalar("ChunkOY", 0.0, cpd=1), g.scalar("ChunkOZ", 0.0, cpd=2), g.scalar("ChunkCell", 100.0, cpd=3)
    orx, ory, orz = g.scalar("ChunkORX", 0.0, cpd=4), g.scalar("ChunkORY", 0.0, cpd=5), g.scalar("ChunkORZ", 1.0, cpd=6)
    # The chunk origin's tile of the noise period, whole periods per axis: with OX..OZ, planet space for the settlements.
    tx, ty, tz = g.scalar("ChunkTX", 0.0, cpd=7), g.scalar("ChunkTY", 0.0, cpd=8), g.scalar("ChunkTZ", 0.0, cpd=9)
    fn = g.custom(FUNCS, [], "CMOT_Float1", "texture set functions")
    cam = g.node("CameraPositionWS")
    wp = g.node("WorldPosition")
    vecs = {
        "OceanColor": (0.05, 0.17, 0.35), "ShallowColor": (0.1, 0.35, 0.5), "LandColor": (0.30, 0.36, 0.18), "LandColor2": (0.55, 0.45, 0.28),
        "MountainColor": (0.55, 0.52, 0.50), "IceColor": (0.90, 0.93, 0.97), "NightLightColor": (1.0, 0.8, 0.5), "SunDirection": (1.0, 0.0, 0.0),
        # The sea's life (SlonkFauna::SeaLifeOf): the plankton's glow, blue-green as on an Earth; the point under a ship
        # stirring the sea, world space (APlanetBody::UpdateShipWake).
        "PlanktonColor": (0.08, 0.7, 0.85), "ShipGroundPos": (0.0, 0.0, 0.0),
        # A strange world's own geology (SlonkGenome::GeologyColors): its two colours.
        "GeoColor": (0.5, 0.5, 0.5), "GeoColor2": (0.2, 0.2, 0.2),
    }
    scal = {"OceanRoughness": 0.22, "LandRoughness": 0.9, "NightLightIntensity": 0.0, "LavaEmissive": 0.0, "NoisePeriodCm": 1048576.0, "ReliefCm": 3.8e6, "OceanDepthCm": 5.0e5, "PlanetRadiusCm": 6.371e8, "SwellCm": 120.0, "StreetGrid": 1.0, "SettleWet": 0.3, "SettleDensity": 1.0, "WayStyle": 0.0, "CliffAffinity": 0.0, "CanopyShade": 0.4, "CoverGain": 2.0, "TreeLife": 1.0, "Clearings": 1.0, "SeaIce": 0.0,
            # 0: a sea with no plankton, no shoals and no ship stirring it, left exactly as it was.
            "PlanktonGlow": 0.0, "ShoalDensity": 0.0, "ShipStir": 0.0,
            # 0: no geology of its own, and the vertex's mark is the lineae's, as on every world near home.
            "GeoKind": 0.0}
    e = g.custom(CODE, INPUTS, "CMOT_Float3", "terrain layers, textures, bump, lights", extra_outputs=(("Nrm", "CMOT_Float3"), ("Rough", "CMOT_Float1"), ("Emis", "CMOT_Float3"), ("AO", "CMOT_Float1")))
    for pin, src in [("Fn", fn), ("N", n), ("LP", vi), ("UV0", uv0), ("UV1", uv1), ("UV2", uv2), ("UV3", uv3), ("Col", col), ("OX", ox), ("OY", oy), ("OZ", oz), ("Cell", cell), ("ORX", orx), ("ORY", ory), ("ORZ", orz), ("Cam", cam), ("WP", wp), ("TX", tx), ("TY", ty), ("TZ", tz)]:
        g.c(src, e, pin)
    wn = g.node("TextureObjectParameter", {"ParameterName": "WaterNR", "Texture": {"refPath": f"{TEX_FOLDER}/T_Water_NR.T_Water_NR"}})
    g.c(wn, e, "WaterNR")
    tm = g.node("Time"); g.c(tm, e, "Time")
    swell = g.scalar("SwellCm", 120.0); g.c(swell, e, "SwellCm")
    scalnodes = {}
    for set_ in TEX_SETS:
        for kind in TEX_KINDS:
            asset = f"T_{set_}_{kind}"
            t = g.node("TextureObjectParameter", {"ParameterName": f"{set_}{kind}", "Texture": {"refPath": f"{TEX_FOLDER}/{asset}.{asset}"}})
            g.c(t, e, f"{set_}{kind}")
    g.c(col, e, "ColA", "A")
    for name, rgb in vecs.items():
        g.c(g.vec(name, rgb), e, name)
    for name, v in scal.items():
        if name == "SwellCm": continue
        scalnodes[name] = g.scalar(name, v); g.c(scalnodes[name], e, name)
    # The swell as geometry: a second custom node on the world position offset, compiled in the vertex shader.
    # It takes the functions node as an input so the Gerstner function is emitted before it.
    wpo = g.custom(WPO_CODE, WPO_INPUTS, "CMOT_Float3", "swell geometry")
    for pin, src in [("Fn", fn), ("LP", psp), ("Col", col), ("OX", ox), ("OY", oy), ("OZ", oz), ("ORX", orx), ("ORY", ory), ("ORZ", orz), ("Cam", cam), ("WP", wp),
                     ("PlanetRadiusCm", scalnodes["PlanetRadiusCm"]), ("NoisePeriodCm", scalnodes["NoisePeriodCm"]), ("Time", tm), ("SwellCm", swell),
                     ("UV2", uv2), ("SeaIce", scalnodes["SeaIce"])]:
        g.c(src, wpo, pin)
    g.out(wpo, "MP_WorldPositionOffset")
    g.out(e, "MP_BaseColor")
    g.out(e, "MP_Normal", "Nrm")
    g.out(e, "MP_Roughness", "Rough")
    # A world seen through another world's sky: opaque aerial perspective stops at the sky LUT's range with its own
    # luminance factor, so from inside an atmosphere a far planet drew as a cut-out. SkyBehind (0 unless the game
    # mode sets it for a world beyond the atmosphere's planet) adds back the sky's own luminance less the aerial
    # in-scatter, over the transmittance, so the far world matches the sky round it.
    sb = g.custom("return K * (L - AP.rgb) / max(AP.a, 0.001);", ["L", "AP", "K"], "CMOT_Float3", "SkyBehind")
    g.c(g.node("SkyAtmosphereViewLuminance"), sb, "L")
    g.c(g.node("SkyAtmosphereAerialPerspective"), sb, "AP")
    g.c(g.scalar("SkyBehind", 0.0), sb, "K")
    emis = g.node("Add")
    g.c(e, emis, "A", "Emis")
    g.c(sb, emis, "B")
    g.out(emis, "MP_EmissiveColor")
    g.out(e, "MP_AmbientOcclusion", "AO")
    g.finish()


if __name__ == "__main__":
    build()
