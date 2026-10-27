"""Builds M_PlanetTerrain, the surface material of the quadtree planet terrain (UPlanetTerrainComponent).

The mesh carries the height field: positions, smooth normals and per-vertex biome data (UV0 = height/relief and
slope, UV1 = moisture and rock, UV2 = ice and canyon, colour = crater, water, |latitude|). The material only
shades: it picks layer colours from those, adds a periodic detail bump that stays continuous across chunks (the
chunk origin modulo the noise period arrives through custom primitive data 0..2, the cell size in 3), and fades
each noise octave out once it is smaller than a pixel.

Run with the editor open and no play session:  python3 tools/planet_materials/terrain_material.py
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
from uemat import *

NAME = "M_PlanetTerrain"

# Shared HLSL: inputs are the custom node's pins. Value noise on a wrapped integer lattice: periodic in NoisePeriodCm.
CODE = r"""
float3 p = LP + float3(OX, OY, OZ);
float hN = UV0.x; float slope = UV0.y; float wet = UV1.x; float rock = UV1.y; float ice = UV2.x; float canyon = UV2.y;
float crater = Col.r; float water = Col.g; float wall = Col.b; float strat = ColA * 8.0;
float dist = max(distance(Cam, WP), 1.0);
float3 Nn = normalize(N);
float3 T1 = normalize(cross(Nn, abs(Nn.x) < 0.9 ? float3(1, 0, 0) : float3(0, 1, 0)));
float3 T2 = cross(Nn, T1);

// Seven octaves of value noise from 16 m down to 25 cm (the mesh carries everything coarser), with the gradient
// taken analytically from the trilinear weights so the bump costs one lattice lookup per octave. Each octave is
// weighted out as it drops below a pixel. The octave values are kept for the texturing below.
float fb0 = 0, norm = 0, amp = 1.0, cs = 1600.0;
float3 gfb = 0;
float val[7]; float wgt[7];
[unroll] for (int k = 0; k < 7; k++)
{
    int wrap = (int)(NoisePeriodCm / cs);
    float w = saturate(cs / (dist * 0.0045) - 0.35);
    float3 q = p / cs;
    float3 fl = floor(q); float3 t = q - fl; float3 sm = t * t * (3.0 - 2.0 * t); float3 dsm = 6.0 * t * (1.0 - t);
    int3 c0 = (int3)fl;
    float h[8];
    [unroll] for (int i = 0; i < 8; i++)
    {
        int3 c = (c0 + int3(i & 1, (i >> 1) & 1, (i >> 2) & 1)) & (wrap - 1);
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
    norm += amp; amp *= 0.6; cs *= 0.5;
}
fb0 /= norm; gfb /= norm;
float crack = (1.0 - abs(val[3] - 0.5) * 2.0) * wgt[3];

// Jittered dots: one hashed disc per lattice cell of size cellCm, present in a fraction `density` of the cells,
// centred anywhere in the middle of its cell with a hashed radius, so no lattice shows. Returns coverage 0..1.
// (A function would need a separate custom node; the loop runs twice inline, scrub then stones.)
float dotsA = 0, dotsB = 0;
[unroll] for (int kd = 0; kd < 2; kd++)
{
    float cellCm = kd == 0 ? 450.0 : 180.0;
    float3 q = p / cellCm; float3 fl = floor(q); float3 fr = q - fl;
    int3 c = (int3)fl & ((int)(NoisePeriodCm / cellCm) - 1);
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

// Coarse value noise (3.2 km, 1.6 km, 0.8 km cells): soil patchiness by day, settlement blobs by night.
float cn = 0, cnorm = 0, camp = 1.0, ccs = 320000.0;
[unroll] for (int kc = 0; kc < 3; kc++)
{
    int wrapc = (int)(NoisePeriodCm / ccs);
    float3 q = p / ccs; float3 fl = floor(q); float3 fr = q - fl; fr = fr * fr * (3.0 - 2.0 * fr);
    int3 c0 = (int3)fl;
    float h[8];
    [unroll] for (int i = 0; i < 8; i++)
    {
        int3 c = (c0 + int3(i & 1, (i >> 1) & 1, (i >> 2) & 1)) & (wrapc - 1);
        uint hh = ((uint)c.x * 73856093u) ^ ((uint)c.y * 19349663u) ^ ((uint)c.z * 83492791u) ^ ((uint)(kc + 11) * 2654435761u);
        hh = (hh ^ (hh >> 13)) * 0x5bd1e995u; hh ^= hh >> 15;
        h[i] = (float)(hh & 0xffffu) / 65535.0;
    }
    float v = lerp(lerp(lerp(h[0], h[1], fr.x), lerp(h[2], h[3], fr.x), fr.y), lerp(lerp(h[4], h[5], fr.x), lerp(h[6], h[7], fr.x), fr.y), fr.z);
    cn += (v - 0.5) * camp; cnorm += camp; camp *= 0.5; ccs *= 0.5;
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
float3 land = lerp(soil, rockc, rocky);
// Scrub: dark bushes a few metres across on the gentler, moister ground; a mean tint holds the brightness where
// the dots have faded with distance.
float scrubzone = (1.0 - steep) * saturate(wet * 1.6 + 0.15) * (1.0 - saturate(hN * ReliefCm / 350000.0));
land = lerp(land, land * float3(0.45, 0.5, 0.35), scrubzone * saturate(dotsA + 0.1 * (1.0 - saturate(450.0 / (dist * 0.0045) - 0.35))));
// Stones: pale specks on the slopes and rock, a little darker on their shaded side.
float stony = saturate(rocky * 1.2 + (1.0 - scrubzone) * 0.3) * (1.0 - sediment);
land = lerp(land, land * float3(1.25, 1.2, 1.12), stony * dotsB * 0.8);
// Canyon floor: pale sediment with darker braided channels, from a three-octave sum so the lattice stays hidden.
float chanf = (val[0] - 0.5) + 0.5 * (val[1] - 0.5) + 0.25 * (val[2] - 0.5);
float chan = smoothstep(0.82, 0.97, 1.0 - abs(chanf) * 2.4) * wgt[1];
float3 floorc = land * float3(1.08, 1.0, 0.9) * (1.0 - 0.28 * chan);
land = lerp(land, floorc, sediment * 0.7);
land *= 0.8 * (0.94 + 0.12 * (fb0 + 0.5));
land *= 1.0 - saturate(rocky - sediment) * 0.45 * smoothstep(0.72, 1.0, crack);
float snowy = ice * (1.0 - saturate(slope * 3.0)) ;
land = lerp(land, IceColor, snowy);
float depth = saturate(-hN * 2.5);
float3 sea = lerp(ShallowColor, OceanColor, depth);
float3 base = lerp(land, sea, water);

// Detail bump from the analytic gradient, scaled by a height that grows with rockiness.
float bumpCm = (25.0 + 320.0 * rocky) * (1.0 - water) * (1.0 - snowy * 0.6);
float dh1 = dot(gfb, T1) * bumpCm;
float dh2 = dot(gfb, T2) * bumpCm;
Nrm = normalize(Nn - T1 * dh1 - T2 * dh2);
Rough = lerp(lerp(LandRoughness * (0.8 + 0.2 * rocky), 0.35, snowy), OceanRoughness, water);

float night = saturate(-dot(Nn, normalize(SunDirection)) * 6.0 + 0.1);
float lowland = 1.0 - saturate(hN * ReliefCm / 250000.0);
float city = smoothstep(0.58, 0.68, cn) * (1.0 - water) * saturate((wet - 0.25) * 4.0) * (1.0 - saturate(slope * 12.0)) * lowland * saturate(fb0 * 4.0 + 0.6);
Emis = NightLightColor * (NightLightIntensity * city * night) + float3(1.0, 0.28, 0.05) * (LavaEmissive * water * (0.6 + 0.8 * (fb0 + 0.5)));
return base;
"""

INPUTS = ["N", "LP", "UV0", "UV1", "UV2", "Col", "ColA", "OX", "OY", "OZ", "Cell", "Cam", "WP",
          "OceanColor", "ShallowColor", "LandColor", "LandColor2", "MountainColor", "IceColor", "NightLightColor",
          "OceanRoughness", "LandRoughness", "NightLightIntensity", "LavaEmissive", "SunDirection",
          "NoisePeriodCm", "ReliefCm", "OceanDepthCm"]


def build():
    wipe(NAME)
    g = G(NAME, {"ShadingModel": "MSM_DefaultLit", "BlendMode": "BLEND_Opaque", "bTangentSpaceNormal": False, "TwoSided": False})
    n = g.node("VertexNormalWS")
    psp = g.node("PreSkinnedPosition")
    vi = g.node("VertexInterpolator"); g.c(psp, vi, "VS")
    uv0, uv1, uv2 = g.texcoord(0), g.texcoord(1), g.texcoord(2)
    col = g.node("VertexColor")
    ox, oy, oz, cell = g.scalar("ChunkOX", 0.0, cpd=0), g.scalar("ChunkOY", 0.0, cpd=1), g.scalar("ChunkOZ", 0.0, cpd=2), g.scalar("ChunkCell", 100.0, cpd=3)
    cam = g.node("CameraPositionWS")
    wp = g.node("WorldPosition")
    vecs = {
        "OceanColor": (0.05, 0.17, 0.35), "ShallowColor": (0.1, 0.35, 0.5), "LandColor": (0.30, 0.36, 0.18), "LandColor2": (0.55, 0.45, 0.28),
        "MountainColor": (0.55, 0.52, 0.50), "IceColor": (0.90, 0.93, 0.97), "NightLightColor": (1.0, 0.8, 0.5), "SunDirection": (1.0, 0.0, 0.0),
    }
    scal = {"OceanRoughness": 0.22, "LandRoughness": 0.9, "NightLightIntensity": 0.0, "LavaEmissive": 0.0, "NoisePeriodCm": 1048576.0, "ReliefCm": 3.8e6, "OceanDepthCm": 5.0e5}
    e = g.custom(CODE, INPUTS, "CMOT_Float3", "terrain layers, bump, lights", extra_outputs=(("Nrm", "CMOT_Float3"), ("Rough", "CMOT_Float1"), ("Emis", "CMOT_Float3")))
    for pin, src in [("N", n), ("LP", vi), ("UV0", uv0), ("UV1", uv1), ("UV2", uv2), ("Col", col), ("OX", ox), ("OY", oy), ("OZ", oz), ("Cell", cell), ("Cam", cam), ("WP", wp)]:
        g.c(src, e, pin)
    g.c(col, e, "ColA", "A")
    for name, rgb in vecs.items():
        g.c(g.vec(name, rgb), e, name)
    for name, v in scal.items():
        g.c(g.scalar(name, v), e, name)
    g.out(e, "MP_BaseColor")
    g.out(e, "MP_Normal", "Nrm")
    g.out(e, "MP_Roughness", "Rough")
    g.out(e, "MP_EmissiveColor", "Emis")
    g.finish()


if __name__ == "__main__":
    build()
