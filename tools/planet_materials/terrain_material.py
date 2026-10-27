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
float crater = Col.r; float water = Col.g;
float dist = max(distance(Cam, WP), 1.0);
float3 Nn = normalize(N);
float3 T1 = normalize(cross(Nn, abs(Nn.x) < 0.9 ? float3(1, 0, 0) : float3(0, 1, 0)));
float3 T2 = cross(Nn, T1);

// Seven octaves of value noise from 64 m down to 1 m, each weighted out as it drops below a pixel; the 8 m
// octave is kept aside as a crack pattern for the rock.
float fb0 = 0, fb1 = 0, fb2 = 0, norm = 0, amp = 1.0, cs = 6400.0, crack = 0;
float eps = 200.0;
float3 pa = p + T1 * eps, pb = p + T2 * eps;
[unroll] for (int k = 0; k < 7; k++)
{
    int wrap = (int)(NoisePeriodCm / cs);
    float w = saturate(cs / (dist * 0.0045) - 0.35);
    float v0 = 0, v1 = 0, v2 = 0;
    [unroll] for (int s = 0; s < 3; s++)
    {
        float3 q = (s == 0 ? p : (s == 1 ? pa : pb)) / cs;
        float3 fl = floor(q); float3 fr = q - fl; fr = fr * fr * (3.0 - 2.0 * fr);
        int3 c0 = (int3)fl;
        float h[8];
        [unroll] for (int i = 0; i < 8; i++)
        {
            int3 c = (c0 + int3(i & 1, (i >> 1) & 1, (i >> 2) & 1)) & (wrap - 1);
            uint hh = ((uint)c.x * 73856093u) ^ ((uint)c.y * 19349663u) ^ ((uint)c.z * 83492791u) ^ ((uint)k * 2654435761u);
            hh = (hh ^ (hh >> 13)) * 0x5bd1e995u; hh ^= hh >> 15;
            h[i] = (float)(hh & 0xffffu) / 65535.0;
        }
        float v = lerp(lerp(lerp(h[0], h[1], fr.x), lerp(h[2], h[3], fr.x), fr.y), lerp(lerp(h[4], h[5], fr.x), lerp(h[6], h[7], fr.x), fr.y), fr.z);
        if (s == 0) v0 = v; else if (s == 1) v1 = v; else v2 = v;
    }
    fb0 += (v0 - 0.5) * amp * w; fb1 += (v1 - 0.5) * amp * w; fb2 += (v2 - 0.5) * amp * w;
    if (k == 3) crack = (1.0 - abs(v0 - 0.5) * 2.0) * w;
    norm += amp; amp *= 0.6; cs *= 0.5;
}
fb0 /= norm; fb1 /= norm; fb2 /= norm;

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
// Bare rock where it is steep or mountainous; canyon floors are sediment, not rock.
float rocky = saturate(rock * 0.7 + steep * 1.2 + crater * 0.5);
float sediment = canyon * (1.0 - steep);
float3 soil = lerp(LandColor2, LandColor, saturate(wet * 1.2 - rocky * 0.3));
float band = frac(hN * ReliefCm / 22000.0 + fb0 * 0.4);
float strata = smoothstep(0.25, 0.45, band) * (1.0 - smoothstep(0.7, 0.9, band)) * steep;
float3 rockc = MountainColor * (0.72 + 0.45 * strata);
rockc = lerp(rockc, rockc * float3(1.25, 0.9, 0.72), saturate(canyon * 1.5) * 0.7);
// Patches of darker, wetter ground and paler dry ground at the kilometre scale; a gentle fine grain on top.
float patch = cn - 0.5;
soil = lerp(soil, soil * float3(0.72, 0.78, 0.7), saturate(patch * 3.0)) ;
soil = lerp(soil, soil * float3(1.15, 1.1, 1.0), saturate(-patch * 3.0));
float3 land = lerp(soil, rockc, rocky);
land = lerp(land, land * float3(1.08, 1.0, 0.9), sediment * 0.5);
land *= 0.8 * (0.94 + 0.12 * (fb0 + 0.5));
land *= 1.0 - saturate(rocky - sediment) * 0.45 * smoothstep(0.72, 1.0, crack);
float snowy = ice * (1.0 - saturate(slope * 3.0)) ;
land = lerp(land, IceColor, snowy);
float depth = saturate(-hN * 2.5);
float3 sea = lerp(ShallowColor, OceanColor, depth);
float3 base = lerp(land, sea, water);

// Detail bump: finite differences of the noise, scaled by a height that grows with rockiness.
float bumpCm = (120.0 + 2600.0 * rocky) * (1.0 - water) * (1.0 - snowy * 0.6);
float dh1 = (fb1 - fb0) * bumpCm / eps;
float dh2 = (fb2 - fb0) * bumpCm / eps;
Nrm = normalize(Nn - T1 * dh1 - T2 * dh2);
Rough = lerp(lerp(LandRoughness * (0.8 + 0.2 * rocky), 0.35, snowy), OceanRoughness, water);

float night = saturate(-dot(Nn, normalize(SunDirection)) * 6.0 + 0.1);
float lowland = 1.0 - saturate(hN * ReliefCm / 250000.0);
float city = smoothstep(0.58, 0.68, cn) * (1.0 - water) * saturate((wet - 0.25) * 4.0) * (1.0 - saturate(slope * 12.0)) * lowland * saturate(fb0 * 4.0 + 0.6);
Emis = NightLightColor * (NightLightIntensity * city * night) + float3(1.0, 0.28, 0.05) * (LavaEmissive * water * (0.6 + 0.8 * (fb0 + 0.5)));
return base;
"""

INPUTS = ["N", "LP", "UV0", "UV1", "UV2", "Col", "OX", "OY", "OZ", "Cell", "Cam", "WP",
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
