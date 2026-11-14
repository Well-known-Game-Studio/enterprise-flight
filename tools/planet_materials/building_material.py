"""Builds M_Building, the material of the settlements' buildings (BuildingMeshes.h, laid out by PlanetCities.h).

Every building is a unit mesh scaled to its lot; the instance's size in metres is per-instance custom data 0-2
(written by APlanetBody::UpdateCities), so windows keep their size whatever the building's, and its kind (the index
of EBuildingKind, BuildingMeshes.h) is custom data 3: a fire (11) burns, flickering, brightest at night; a footing
(12) is plain masonry with no windows. Who lives on the world
(Civilization.h) sets, per planet, through the MID (APlanetBody::ApplyBuildingParams):
    ToneA, ToneB    the walls: each building takes a colour between the two by its random (adobe, stone, brick,
                    concrete, white and glass, ivory and moss)
    LightColor      their night lights: firelight, lamps, sodium, warm white, cool white, a living teal or violet
    WindowLit       the share of windows lit at night
    WindowGlow      how bright
    Organic         0 a grid of windows (built); 1 grown: light in soft bands round the shell, ports along them
SunDirectionLocal is the star's direction (APlanetBody::UpdateCities), so the windows light on the night side. A
slab under a metre tall is a city's landing pad: grey, with a lit ring and cross.

Run with the editor open and no play session:  python3 tools/planet_materials/building_material.py
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
from uemat import *

NAME = "M_Building"

CODE = r"""
// building metres from the unit cube's local position and the instance scale
float3 m = LP * OS / 100.0;
float3 an = abs(N);
float2 uv = an.x > 0.5 ? m.yz : (an.y > 0.5 ? m.xz : m.xy);
float roof = an.z > 0.5 ? 1.0 : 0.0;
float night = saturate(-dot(Up, Sun) * 5.0 + 0.3);
float3 tone = lerp(ToneA, ToneB, R);
Rough = lerp(0.78, 0.45, saturate(Organic));
// a fire: a ring of stones with embers among them, a cone of flame in it that flickers (each its own way), yellow at
// the foot to red at the tip; lit by day, bright by night
if (abs(Kind - 11.0) < 0.5)
{
    float z = LP.z / 100.0 + 0.5;
    if (z < 0.125)
    {
        Base = float3(0.24, 0.22, 0.2); Rough = 0.95;
        return float3(1.0, 0.3, 0.05) * 0.3 * (0.4 + 2.0 * night);
    }
    float fl = 0.75 + 0.15 * sin(Time * 11.0 + R * 40.0) + 0.1 * sin(Time * 23.0 + R * 13.0 + z * 6.0);
    float3 flame = lerp(float3(1.0, 0.72, 0.25), float3(1.0, 0.25, 0.04), saturate(z * 1.2));
    Base = float3(0.05, 0.03, 0.02);
    return flame * fl * (4.0 + 30.0 * night) * (1.0 - 0.5 * z);
}
// a footing: the walls' own stone or adobe a shade darker, laid in courses, no windows
if (abs(Kind - 12.0) < 0.5)
{
    float course = step(0.5, frac(m.z / 0.6));
    Base = tone * (0.72 + 0.08 * course);
    Rough = 0.9;
    return float3(0, 0, 0);
}
// a landing pad (a slab under a metre tall): grey, a lit ring and a cross on top, dark otherwise
if (OS.z < 3.0)
{
    Base = float3(0.22, 0.22, 0.22);
    float2 pm = m.xy; float r = length(pm) / (OS.x * 0.5);
    float ring = 1.0 - smoothstep(0.03, 0.05, abs(r - 0.82));
    float cross = (abs(pm.x) < 1.2 || abs(pm.y) < 1.2) && r < 0.6 ? 1.0 : 0.0;
    float mark = max(ring, cross) * roof;
    return float3(1.0, 0.75, 0.2) * mark * (1.5 + 6.0 * night);
}
float win, lit;
float2 cell;
if (Organic > 0.5)
{
    // Grown: light in soft bands round the shell every few metres, broken into ports along them.
    float b = abs(frac(m.z / 4.2 + R * 3.0) - 0.5);
    float band = 1.0 - smoothstep(0.06, 0.16, b);
    cell = floor(float2(uv.x / 2.6, m.z / 4.2 + R * 3.0));
    float along = abs(frac(uv.x / 2.6) - 0.5);
    win = band * (1.0 - smoothstep(0.25, 0.4, along));
}
else
{
    cell = floor(uv / float2(3.0, 3.4));
    float2 f = frac(uv / float2(3.0, 3.4));
    win = (f.x > 0.25 && f.x < 0.75 && f.y > 0.3 && f.y < 0.8) ? 1.0 : 0.0;
}
float h = frac(sin(dot(cell + R * 97.0, float2(12.9898, 78.233))) * 43758.5453);
lit = h < WindowLit ? 1.0 : 0.0;
// By day the windows are darker than the wall (the openings of the early ages, the glass of the later).
Base = tone * (1.0 - 0.4 * win * (1.0 - roof));
float3 col = LightColor * (0.8 + 0.4 * frac(h * 7.0));
return col * (win * lit * night * (1.0 - roof) * WindowGlow);
"""


def build():
    wipe(NAME)
    g = G(NAME, {"bUsedWithInstancedStaticMeshes": True})
    lp = g.node("LocalPosition")
    os_ = g.node("PerInstanceCustomData3Vector", {"DataIndex": 0, "ConstDefaultValue": {"R": 0, "G": 0, "B": 0, "A": 0}})
    kind = g.node("PerInstanceCustomData", {"DataIndex": 3, "ConstDefaultValue": 0.0})
    time = g.node("Time")
    # The normal in the instance's own frame, for which face a window grid runs on.
    n = g.node("Transform", {"TransformSourceType": "TRANSFORMSOURCE_World", "TransformType": "TRANSFORM_Instance"})
    g.c(g.node("VertexNormalWS"), n, "")
    # The instance's up in the world, against the star's direction for night.
    upl = g.node("Constant3Vector", {"Constant": {"R": 0.0, "G": 0.0, "B": 1.0, "A": 0.0}})
    up = g.node("Transform", {"TransformSourceType": "TRANSFORMSOURCE_Instance", "TransformType": "TRANSFORM_World"})
    g.c(upl, up, "")
    rnd = g.node("PerInstanceRandom")
    sun = g.vec("SunDirectionLocal", (1.0, 0.0, 0.0))
    tone_a = g.vec("ToneA", (0.52, 0.48, 0.44))
    tone_b = g.vec("ToneB", (0.78, 0.76, 0.72))
    light = g.vec("LightColor", (1.0, 0.8, 0.5))
    lit = g.scalar("WindowLit", 0.55)
    glow = g.scalar("WindowGlow", 12.0)
    organic = g.scalar("Organic", 0.0)
    e = g.custom(CODE, ["LP", "OS", "N", "R", "Sun", "Up", "ToneA", "ToneB", "LightColor", "WindowLit", "WindowGlow", "Organic", "Kind", "Time"], "CMOT_Float3", "building",
                 extra_outputs=(("Base", "CMOT_Float3"), ("Rough", "CMOT_Float1")))
    for pin, src in [("LP", lp), ("OS", os_), ("N", n), ("R", rnd), ("Sun", sun), ("Up", up), ("ToneA", tone_a), ("ToneB", tone_b),
                     ("LightColor", light), ("WindowLit", lit), ("WindowGlow", glow), ("Organic", organic),
                     ("Kind", kind), ("Time", time)]:
        g.c(src, e, pin)
    g.out(e, "MP_EmissiveColor")
    g.out(e, "MP_BaseColor", "Base")
    g.out(e, "MP_Roughness", "Rough")
    g.finish()


if __name__ == "__main__":
    build()
