"""Builds M_Fauna, the material of the animals on the worlds with life (FaunaMeshes.h, kept about the camera by
UPlanetFaunaComponent). The game reads nothing back from it: it moves each group's frame along its path, and this
material moves the members within it, on its own clock (the Time node, its period the hour every path and cycle
divides), so the velocity pass sees both and none of the motion comes from a parameter.

The meshes carry their parts in the vertex colour (SlonkFauna::Vc, FaunaTypes.h): R the surface (0 body, 0.25 bill,
0.5 tail, 1 wing), G how far it flexes (0 at its joint, 1 at the tip), B its side (0 left, 0.5 middle, 1 right), A the
joint the material moves it by (0 rigid, 0.25 tail, 0.5 a wing hinged along the body 4 cm of the unit mesh out from its
axis, 0.75 a leg, 1 a fin). A unit mesh is 1 m across (a flier's wingspan); an instance's scale is its span in metres.

Per-instance custom data, written by SlonkFauna::MembersOf (SlonkFauna::Custom):
    0-2 pigment
    3   glow at night (0 on an Earth)
    4   seed
    5   span, m
    6   ripple phase, rolling across the formation
    7   wingbeat, Hz (a whole number of beats a minute)
    8   the bird's bearing from its group's middle, radians: a startle scatters it along it
    9   the distance it fades out by, cm (0: never): dithered away over the last fifth of it, from ViewPos
Per-group custom primitive data, set by UPlanetFaunaComponent each frame (SlonkFauna::GroupData, SlonkFauna::FrightData):
    0   night (0 day, 1 night)
    1   when its birds' scatter last opened, on this material's clock (s, wrapped to the hour): a startle while it is
        open keeps it; the scatter's shape runs from it alone
    2-4 the way the scatter opened (the way it fled then), in its own frame (unit)
    5   the bank its birds lean into the turn with, radians (positive lifts the right wing)
    6   1 while a startle is live (two minutes after the last)
    7   how far its birds scatter, m (at most two spans here)
    8   when it was last startled, on the same clock: its panic eases from then
    9   when its wingbeat's urge began to come in, on the same clock: the fast beat comes in over 0.3 s from then (a
        startle while it is shaken sets it where the coming-in stands, SlonkFauna::FPanic::OnsetT0)
Parameters set by UPlanetFaunaComponent: ViewPos (the camera, world) and PxPerRad (its pixels a radian), so the fade
and the speck floor are the same in the shadow passes as in the view. GlowColor is left at its default: no flier glows
yet (MembersOf gives every one a glow of 0).

The mote branch (Motes 1, which only the terrain's instance of this material sets, UPlanetTerrainComponent: a material
built before the branch has no Motes parameter, and the game then draws no motes). The terrain's chunks carry clouds of
motes (SlonkScatter::PlaceMotes; the cloud mesh is SlonkFaunaMeshes::BuildMoteCloudLod): 24 cards at LOD 0, 8 at LOD 1, 2
at LOD 2, each a quad in the unit mesh's xz plane 0.6 cm each way of its middle, its corner in its UV, its vertex colour
R 0.75 (a mote), G how many of its cloud's 24 motes it stands for over 24, B its own random (a gnat's under 0.25). Their
per-instance custom data, in the same slots:
    0-2 the cloud's colour by day, the pollen's gold (its gnats a dark shade of it)
    3   how brightly its fireflies glow at night
    4   seed
    5   its span, m (its scale)
    6   a phase for its motes' cycles
    7   how fast they wander, Hz (a whole number of cycles a minute)
    8   the share of its motes that are fireflies at night
    9   the distance it fades out by, cm (from ViewPos, over the last fifth of it, a card at a time as below)
Parameters set by the terrain: ViewPos and PxPerRad as above, SunDirection (the way to the star, world), and GlowColor
(the fireflies' light, SlonkFauna::FFaunaLook::Firefly: an Earth's warm yellow-green, turned with a strange world's sky).
Each card's middle wanders about its place on cycles a whole number of times an hour, the card turned to face the camera
and grown by a speck floor (no smaller than a pixel by day, three at night, at most forty times its size), every vertex
kept within 33 cm of the unit mesh of where the mesh has it (the mesh's bounds are grown by that). It is turned from where
its middle is drawn, so a card the wander carries past a camera standing in the cloud still faces it, its U to the
camera's right and its V up (the mesh winds every card so the camera sees its front). By day the pollen drifts pale gold in
the sun and the gnats dance dark in their swarm, a speck far off thinning out rather than swelling (its share of the
cover falls as its card grows, and over the last fifth of the fade): the cards that make up the share are chosen each by
a number of its own that holds from frame to frame, so a speck shown is drawn whole every frame (the temporal upscaler
keeps a speck of a pixel or two only so) and the cards drop out one by one. At dusk and through the night (from the instance's up against SunDirection, so on a locked world the fireflies keep
to the terminator for ever) the fireflies flash, each for 0.45 s every 1.5 to 4 s of its own, brightest at the speck's
middle, one drawn bigger than itself fainter by the square root of its growth so a meadow far off still twinkles. A card
of a far LOD flashes for every firefly it stands for.

The motion (WPO_CODE): bursts of wingbeats on a cycle of 3 to 7 s of each bird's own, gliding with the wings a little
raised between, beating all the time and faster while startled; the body banked into the turn; a ripple through the
flock; a startle's scatter, out from the flock's middle and mostly away from what startled it, opening in a second and
closing over half a minute (the flight itself is the group's frame); and a speck floor, a bird far off drawn no smaller
than about two pixels across (at most four times its size), its wings and bank scaled back as it grows so the box it is
culled by holds it. Every vertex stays within 400 unit-mesh cm of the mesh's box: the instances' boxes are padded by
that (FFaunaKindDesc::CullPaddingUnits), and Slonk.Fauna.Meshes checks a float copy of this code against it
(SlonkFauna::FlierMotion, which must change with it: the test holds the CRC of this code's lines, its comments aside,
as the copy was made, and fails when they change). Two-sided foliage shading lets the sun through the wings.

Run with the editor open and no play session:  python3 tools/planet_materials/fauna_material.py
To see whether the asset has drifted from it:  python3 tools/ue_mcp/matgraph.py check tools/planet_materials/fauna_material.py M_Fauna
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
from uemat import *

NAME = "M_Fauna"

CODE = r"""
[branch] if (Motes > 0.5)
{
    // A mote cloud's card (SlonkFauna::Mote): a round speck the vertex shader has turned to the camera. By day the pollen
    // drifting gold in the sun and the gnats dark in their swarm; at dusk and through the night the fireflies, each lit for
    // 0.45 s every 1.5 to 4 s of its own, in the world's own colour (GlowColor). A card of a far LOD stands for several of its
    // cloud's motes (VC.g, over 24) and flashes for each of them.
    float2 c = UV * 2.0 - 1.0;
    float r2 = dot(c, c);
    float sunUp = dot(normalize(BodyUp), normalize(SunDir));
    float fly = 1.0 - smoothstep(-0.06, 0.1, sunUp);
    float day = smoothstep(0.0, 0.16, sunUp);
    float rnd = VC.b;
    float gnat = rnd < 0.25 ? 1.0 : 0.0;
    float stands = clamp(round(VC.g * 24.0), 1.0, 12.0);
    // The speck floor as the vertex shader grows the card (no smaller than a pixel by day, three at night): how many times
    // its own size it is drawn.
    float dist = max(length(WP - ViewPos), 1.0);
    float sizeCm = lerp(lerp(2.0, 0.7, gnat), 3.0, fly);
    float k = clamp(lerp(1.0, 3.0, fly) * dist / (sizeCm * max(PxPerRad, 1.0)), 1.0, 40.0);
    // The flash: the brightest of the motes the card stands for that are fireflies (PerKind of them), each on a cycle of
    // its own, a whole number of them an hour.
    float flash = 0.0;
    [loop] for (int i = 0; i < 12; i++)
    {
        if ((float)i >= stands) { break; }
        float h = frac(rnd * 13.17 + Seed * 5.31 + (float)i * 0.6180339);
        if (frac(h * 7.77 + 0.31) < PerKind)
        {
            float cycles = 900.0 + floor(h * 1500.0);
            float x = frac(T * (cycles / 3600.0) + frac(h * 91.7 + Phase)) * (3600.0 / cycles) / 0.45;
            flash = max(flash, smoothstep(0.0, 0.2, x) * (1.0 - smoothstep(0.35, 1.0, x)));
        }
    }
    flash *= 1.0 - gnat;
    // What shows: by day the gnats and about half the rest as pollen, a speck's share of the cover kept as its card grows
    // past its own size (so the far ones thin out rather than swell), the more the more motes it stands for; at night a
    // firefly while it is lit; and less of either over the last fifth of the fade. Which cards make up that share is a
    // number of each card's own that holds from frame to frame (from its random and its cloud's seed), so a speck shown is
    // drawn whole every frame, as the temporal upscaler needs a speck a pixel or two across to be, and as the share falls
    // the cards drop out one by one (every card whole while the share is 1).
    uint hk = (uint)(rnd * 255.0 + 0.5) * 747796405u + (uint)(Seed * 65536.0) * 2654435761u + 2891336453u;
    hk = ((hk >> ((hk >> 28u) + 4u)) ^ hk) * 277803737u;
    hk = (hk >> 22u) ^ hk;
    float keep = (float)(hk & 0xffffu) / 65536.0;
    float pollen = max(gnat, frac(rnd * 5.31 + 0.17) < 0.55 ? 1.0 : 0.0);
    float fade = FadeFar > 0.0 ? saturate((FadeFar - length(WP - ViewPos)) / (0.2 * FadeFar)) : 1.0;
    float share = max(day * pollen * saturate(stands / (k * k)), fly * (flash > 0.02 ? 1.0 : 0.0)) * fade;
    Mask = (r2 < 1.0 && share > keep) ? 1.0 : 0.0;
    // Pale gold fluff catching the sun, half the pigment and half a seed head's down; the gnats near black; at night dark
    // bodies under the glow.
    float3 fluff = lerp(Pigment * 0.6, float3(0.9, 0.88, 0.8), 0.5);
    float3 mcol = lerp(fluff, Pigment * 0.06, gnat) * (1.0 - 0.85 * fly);
    Rough = 0.6;
    Sub = mcol * (1.0 - gnat);
    // The glow, brightest at the speck's middle and bright across it (a speck of two or three pixels keeps its light through
    // the temporal upscaler); one drawn bigger than itself fainter, but a twinkle still far off.
    float core = saturate(1.0 - 0.7 * r2);
    Emis = GlowColor * (Glow * 40.0 * fly * flash * core * rsqrt(k));
    return mcol;
}
float surf = VC.r; float flex = VC.g;
float3 base = Pigment * (0.85 + 0.3 * frac(Seed * 11.3));
float wing = saturate((surf - 0.75) * 4.0);
float tail = saturate(1.0 - abs(surf - 0.5) * 8.0);
float bill = saturate(1.0 - abs(surf - 0.25) * 8.0);
// The flight feathers darken toward the tips, the tail a little; the bill is dark; the belly is paler.
float3 col = base * lerp(1.0, lerp(0.9, 0.5, flex), wing);
col = lerp(col, base * 0.75, tail);
col = lerp(col, float3(0.1, 0.085, 0.05), bill);
float belly = saturate(-dot(normalize(N), normalize(BodyUp)) * 1.5) * (1.0 - wing) * (1.0 - bill);
col = lerp(col, col * 1.6 + 0.04, belly * 0.6);
// Fading out with distance: dithered against interleaved gradient noise that moves each frame, which the temporal
// upscaler smooths into a fade (as M_Foliage does).
float mask = 1.0;
if (FadeFar > 0.0)
{
    float fade = saturate((FadeFar - length(WP - ViewPos)) / (0.2 * FadeFar));
    float2 px = Parameters.SvPosition.xy + 5.588238 * (float)(View.StateFrameIndexMod8);
    float ign = frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));
    mask *= fade > ign ? 1.0 : 0.0;
}
Mask = mask;
Rough = lerp(0.8, 0.6, wing);
Sub = col * 0.45 * wing;
Emis = GlowColor * (Glow * Night * (0.3 + 0.7 * flex));
return col;
"""

WPO_CODE = r"""
[branch] if (Motes > 0.5)
{
    // A mote cloud's card: its middle (the mesh stands every card in the xz plane, its corners 0.6 cm of the unit mesh from
    // it as its UV says), wandering about its place, the card turned to face the camera and grown by the speck floor.
    float2 cn = UV * 2.0 - 1.0;
    float3 home = LP - float3(cn.x, 0.0, cn.y) * 0.6;
    float s = max(Size, 0.5);
    float rnd = VC.b;
    float gnat = rnd < 0.25 ? 1.0 : 0.0;
    float sunUp = dot(normalize(BodyUp), normalize(SunDir));
    float fly = 1.0 - smoothstep(-0.06, 0.1, sunUp);
    // Wandering, on cycles a whole number of times an hour: the pollen and the fireflies in slow loops at the cloud's own
    // pace (Rate, a whole number of cycles a minute), the gnats in a quick dance about their swarm. World cm, over the
    // span for the unit mesh's.
    float3 hh = frac(float3(rnd * 17.31 + Seed * 3.1, rnd * 29.13 + Seed * 7.7, rnd * 41.97 + Phase));
    float base = floor(Rate * 3600.0 + 0.5);
    float3 cyc = gnat > 0.5 ? 2700.0 + floor(hh * 1500.0) : base + floor(hh * 120.0);
    float3 amp = gnat > 0.5 ? float3(14.0, 14.0, 10.0) : float3(70.0, 70.0, 30.0);
    float3 wander = amp * sin(6.2831853 * frac(T * (cyc / 3600.0) + hh + Phase));
    float3 centre = home + wander / s;
    // Where that middle is drawn in the world: the vertex's own place moved to it through the instance's axes (which carry
    // its scale, the span), so a card the wander has carried past the camera still turns to it.
    float3 tc = centre - LP;
    float3 cw = WP + AxX * tc.x + AxY * tc.y + BodyUp * tc.z;
    float3 d = ViewPos - cw;
    // The speck floor, as the pixel shader reckons it, from there: the card's half-width, unit-mesh cm.
    float sizeCm = lerp(lerp(2.0, 0.7, gnat), 3.0, fly);
    float dist = max(length(d), 1.0);
    float k = clamp(lerp(1.0, 3.0, fly) * dist / (sizeCm * max(PxPerRad, 1.0)), 1.0, 40.0);
    float hw = 0.5 * sizeCm * k / s;
    // Facing the camera: the way to it in the cloud's own frame, from its frame's axes in the world; the card's U laid to
    // the camera's right and its V up, which with the mesh's winding (SlonkFaunaMeshes::BuildMoteCloudLod) shows the camera
    // each card's front.
    float3 v = float3(dot(d, AxX), dot(d, AxY), dot(d, BodyUp));
    v = v / max(length(v), 1e-4);
    float3 rt = cross(float3(0.0, 0.0, 1.0), v);
    rt = dot(rt, rt) > 1e-6 ? normalize(rt) : float3(1.0, 0.0, 0.0);
    float3 upv = cross(v, rt);
    float3 moff = centre + (rt * cn.x + upv * cn.y) * hw - LP;
    // Never past the box the instance is culled by: the mesh's bounds are grown by 33 cm of the unit mesh (Mote::ReachCm),
    // which holds the most a card is carried and grown (the wander, its corner at forty times a firefly's size on the
    // smallest cloud, and its place), so the clamp only guards.
    return moff * min(1.0, 33.0 / max(length(moff), 1e-4));
}
// Every cycle here is a whole number of cycles an hour (T is the world's time wrapped to 3600 s), so the hour's wrap
// shows no seam. Offsets are in the unit mesh's centimetres, in the instance's frame (a span is 100); the transform
// after this node turns them into the world's. SlonkFauna::FlierMotion is a float copy of this: change both.
// A startle's three moments, each driving its own part: the scatter's opening its shape, the latest startle how the
// panic eases, the urge's onset the fast beat's coming-in; so a startle that restarts one moves nothing the others drive.
// A moment stamped a hair ahead of this frame's clock (the two round differently) reads as just now, not as an hour
// ago: an age up to a second short of zero stays negative.
float scatterAge = fmod(T - ScatterT0 + 7201.0, 3600.0) - 1.0;
float panicAge = fmod(T - PanicT0 + 7201.0, 3600.0) - 1.0;
float onsetAge = fmod(T - OnsetT0 + 7201.0, 3600.0) - 1.0;
float live = Startled > 0.5 ? 1.0 : 0.0;
float panic = live * (1.0 - smoothstep(20.0, 120.0, panicAge));
// The speck floor: no smaller than about two pixels across, at most four times its size.
float dist = max(length(WP - ViewPos), 1.0);
float px = max(Size, 0.01) * 100.0 * PxPerRad / dist;
float k = clamp(2.0 / max(px, 0.001), 1.0, 4.0);
// Flap and glide: bursts of beating on a cycle of 3 to 7 s of its own, gliding between with the wings a little raised;
// startled, it beats all the time, and half as fast again, the fast beat coming in over 0.3 s from its onset.
float n = 514.0 + floor(frac(Seed * 7.31) * 686.0);
float cyc = frac(T * (n / 3600.0) + Seed);
float share = 0.45 + 0.35 * frac(Seed * 3.77);
float burst = smoothstep(0.0, 0.08, cyc) * (1.0 - smoothstep(share, share + 0.08, cyc));
float urge = saturate(panic * 3.0) * smoothstep(0.0, 0.3, onsetAge);
float flapOn = max(burst, urge);
float beat = lerp(sin(6.2831853 * frac(T * Rate + Seed * 5.0)), sin(6.2831853 * frac(T * Rate * 1.5 + Seed * 5.0)), urge);
float ang = lerp(0.12, 0.9 * beat, flapOn) / k;
float3 p = LP;
if (VCA > 0.4 && VCA < 0.6)
{
    // A wing, about its hinge along the body 4 cm out from its axis, the outer wing bending further.
    float s = VC.b > 0.5 ? 1.0 : -1.0;
    float d = max(abs(LP.y) - 4.0, 0.0);
    float a = ang * (0.8 + 0.4 * VC.g);
    p.y = s * (4.0 + d * cos(a));
    p.z = LP.z + d * sin(a);
}
else if (VCA > 0.15 && VCA < 0.35)
{
    // The tail dips with the beat.
    p.z = LP.z - VC.g * 3.0 * flapOn * beat;
}
// Banked into the turn about its own length, then grown by the speck floor about its middle.
float b = clamp(Bank, -0.7, 0.7) / k;
float cb = cos(b);
float sb = sin(b);
p = float3(p.x, p.y * cb - p.z * sb, p.y * sb + p.z * cb) * k;
// The ripple through the flock: each bird rises and falls and surges a little, the phase rolling across the formation.
p.z += 100.0 * sin(6.2831853 * frac(T * (600.0 / 3600.0) + Phase));
p.x += 50.0 * sin(6.2831853 * frac(T * (420.0 / 3600.0) + Phase * 1.3 + Seed));
// A startle's scatter: out from the flock's middle and mostly away from what startled it, the near side first.
float3 outv = normalize(float3(cos(Bearing), sin(Bearing), 0.25));
float3 sdir = float3(SDX, SDY, SDZ);
float3 sv = normalize(outv + 0.8 * sdir);
float delay = 0.6 * saturate(0.5 + 0.5 * dot(outv, sdir));
float open = live * smoothstep(0.0, 1.2, scatterAge - delay) * (1.0 - smoothstep(8.0, 40.0, scatterAge));
float spread = min(max(Spread, 0.0), 2.0 * max(Size, 0.01)) * 100.0 / max(Size, 0.01);
p += sv * (spread * open);
return p - LP;
"""

INPUTS = ["VC", "N", "BodyUp", "Pigment", "Glow", "Seed", "FadeFar", "WP", "ViewPos", "Night", "GlowColor",
          "Motes", "UV", "SunDir", "T", "Phase", "PerKind", "PxPerRad"]
WPO_INPUTS = ["LP", "VC", "VCA", "T", "Size", "Seed", "Phase", "Rate", "Bearing", "ScatterT0", "SDX", "SDY", "SDZ", "Bank", "Startled", "Spread",
              "PanicT0", "OnsetT0", "WP", "ViewPos", "PxPerRad", "Motes", "UV", "SunDir", "BodyUp", "AxX", "AxY"]


def build():
    wipe(NAME)
    g = G(NAME, {"ShadingModel": "MSM_TwoSidedFoliage", "BlendMode": "BLEND_Masked", "TwoSided": True, "OpacityMaskClipValue": 0.33,
                 "bUsedWithInstancedStaticMeshes": True})
    vc = g.node("VertexColor")
    n = g.node("VertexNormalWS")
    # The bird's own up in the world: its instance's z.
    upl = g.node("Constant3Vector", {"Constant": {"R": 0.0, "G": 0.0, "B": 1.0, "A": 0.0}})
    up = g.node("Transform", {"TransformSourceType": "TRANSFORMSOURCE_Instance", "TransformType": "TRANSFORM_World"})
    g.c(upl, up, "")
    # And its other two axes, for the motes' turn to the camera in the instance's own frame.
    axl = g.node("Constant3Vector", {"Constant": {"R": 1.0, "G": 0.0, "B": 0.0, "A": 0.0}})
    axx = g.node("Transform", {"TransformSourceType": "TRANSFORMSOURCE_Instance", "TransformType": "TRANSFORM_World"})
    g.c(axl, axx, "")
    ayl = g.node("Constant3Vector", {"Constant": {"R": 0.0, "G": 1.0, "B": 0.0, "A": 0.0}})
    axy = g.node("Transform", {"TransformSourceType": "TRANSFORMSOURCE_Instance", "TransformType": "TRANSFORM_World"})
    g.c(ayl, axy, "")
    pigment = g.node("PerInstanceCustomData3Vector", {"DataIndex": 0, "ConstDefaultValue": {"R": 0.16, "G": 0.12, "B": 0.085, "A": 1}})
    glow = g.node("PerInstanceCustomData", {"DataIndex": 3, "ConstDefaultValue": 0.0})
    seed = g.node("PerInstanceCustomData", {"DataIndex": 4, "ConstDefaultValue": 0.0})
    size = g.node("PerInstanceCustomData", {"DataIndex": 5, "ConstDefaultValue": 1.0})
    phase = g.node("PerInstanceCustomData", {"DataIndex": 6, "ConstDefaultValue": 0.0})
    rate = g.node("PerInstanceCustomData", {"DataIndex": 7, "ConstDefaultValue": 4.0})
    bearing = g.node("PerInstanceCustomData", {"DataIndex": 8, "ConstDefaultValue": 0.0})
    fade_far = g.node("PerInstanceCustomData", {"DataIndex": 9, "ConstDefaultValue": 0.0})
    # The group's, from its component's custom primitive data.
    night = g.scalar("Night", 0.0, cpd=0)
    scatter_t0 = g.scalar("ScatterT0", 0.0, cpd=1)
    sdx, sdy, sdz = g.scalar("ScatterDirX", 0.0, cpd=2), g.scalar("ScatterDirY", 0.0, cpd=3), g.scalar("ScatterDirZ", 0.0, cpd=4)
    bank = g.scalar("Bank", 0.0, cpd=5)
    startled = g.scalar("Startled", 0.0, cpd=6)
    spread = g.scalar("SpreadM", 0.0, cpd=7)
    panic_t0 = g.scalar("PanicT0", 0.0, cpd=8)
    onset_t0 = g.scalar("OnsetT0", 0.0, cpd=9)
    view_pos = g.vec("ViewPos", (0.0, 0.0, 0.0))
    px_per_rad = g.scalar("PxPerRad", 960.0)
    glow_color = g.vec("GlowColor", (0.3, 0.9, 0.6))
    # The mote branch: on in the terrain's instance alone, which also gives it the way to the star.
    motes = g.scalar("Motes", 0.0)
    sun_dir = g.vec("SunDirection", (0.0, 0.0, 1.0))
    uv = g.texcoord(0)
    # The material's own clock, wrapped to the hour every path and cycle divides: never a parameter, so the velocity
    # pass sees the previous frame's motion as well as this one's.
    tm = g.node("Time", {"bOverride_Period": True, "Period": 3600.0})
    lp = g.node("LocalPosition", {"IncludedOffsets": "ExcludeOffsets", "LocalOrigin": "Instance"})
    wp = g.node("WorldPosition")

    e = g.custom(CODE, INPUTS, "CMOT_Float3", "fauna",
                 extra_outputs=(("Mask", "CMOT_Float1"), ("Rough", "CMOT_Float1"), ("Sub", "CMOT_Float3"), ("Emis", "CMOT_Float3")))
    for pin, src in [("VC", vc), ("N", n), ("BodyUp", up), ("Pigment", pigment), ("Glow", glow), ("Seed", seed), ("FadeFar", fade_far),
                     ("WP", wp), ("ViewPos", view_pos), ("Night", night), ("GlowColor", glow_color),
                     ("Motes", motes), ("UV", uv), ("SunDir", sun_dir), ("T", tm), ("Phase", phase), ("PerKind", bearing), ("PxPerRad", px_per_rad)]:
        g.c(src, e, pin)

    w = g.custom(WPO_CODE, WPO_INPUTS, "CMOT_Float3", "fauna motion")
    for pin, src in [("LP", lp), ("VC", vc), ("T", tm), ("Size", size), ("Seed", seed), ("Phase", phase), ("Rate", rate), ("Bearing", bearing),
                     ("ScatterT0", scatter_t0), ("SDX", sdx), ("SDY", sdy), ("SDZ", sdz), ("Bank", bank), ("Startled", startled), ("Spread", spread),
                     ("PanicT0", panic_t0), ("OnsetT0", onset_t0), ("WP", wp), ("ViewPos", view_pos), ("PxPerRad", px_per_rad),
                     ("Motes", motes), ("UV", uv), ("SunDir", sun_dir), ("BodyUp", up), ("AxX", axx), ("AxY", axy)]:
        g.c(src, w, pin)
    # The colour node's main output is RGB; the joint kind is its alpha pin.
    g.c(vc, w, "VCA", "A")
    # The offset is in the instance's frame: into the world's (with the instance's scale, its span).
    wt = g.node("Transform", {"TransformSourceType": "TRANSFORMSOURCE_Instance", "TransformType": "TRANSFORM_World"})
    g.c(w, wt, "")

    g.out(e, "MP_BaseColor")
    g.out(e, "MP_OpacityMask", "Mask")
    g.out(e, "MP_Roughness", "Rough")
    g.out(e, "MP_SubsurfaceColor", "Sub")
    g.out(e, "MP_EmissiveColor", "Emis")
    g.out(wt, "MP_WorldPositionOffset")
    g.finish()


if __name__ == "__main__":
    build()
