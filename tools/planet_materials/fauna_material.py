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
camera's right and its V up (the mesh winds every card so the camera sees its front: Slonk.Fauna.MoteCloud checks a
copy of the turn against the mesh, and holds the CRC of the mote branch's lines, its comments aside, as the copy was
made, so it fails when they change). By day the pollen drifts pale gold in
the sun and the gnats dance dark in their swarm, a speck far off thinning out rather than swelling (its share of the
cover falls as its card grows, and over the last fifth of the fade): the cards that make up the share are chosen each by
a number of its own that holds from frame to frame, so a speck shown is drawn whole every frame (the temporal upscaler
keeps a speck of a pixel or two only so) and the cards drop out one by one. At dusk and through the night (from the instance's up against SunDirection, so on a locked world the fireflies keep
to the terminator for ever) the fireflies flash, each for 0.45 s every 1.5 to 4 s of its own, brightest at the speck's
middle, one drawn bigger than itself fainter by the square root of its growth so a meadow far off still twinkles. A card
of a far LOD flashes for every firefly it stands for.

The walker branch (Walkers 1, which only the herds' instance of this material sets, UPlanetFaunaComponent: a material
built before the branch has no Walkers parameter, and the game then keeps no herds). A herd is one instanced set whose
frame the game sets each frame on the drawn ground on the herd's loop, tilted to the plane fitted to the ground under it,
its x and y the herd's own east and north (it never turns); its walkers (SlonkFaunaMeshes' walker: 150, 30 and 2
triangles, a metre from the tail's tip to the muzzle, feet on z 0, nose along +x) stand unturned in it, each at the middle
of the small circle it ambles round as it grazes. Their vertex colours: R the surface (0 coat, 0.25 horn, hoof and
muzzle, 0.5 tail), G the flex (a leg's 0 at the hip to 1 at the hoof, the neck's and head's 0 at the root to 1 at the
muzzle, the tail's from its root), B a leg's place in the gait (0 hind left, 0.25 fore left, 0.5 hind right, 0.75 fore
right), A the joint (0 rigid, 0.25 tail, 0.375 neck, 0.75 leg); UV its place along the body (U from the rump) and up it
(V, z over 60 cm). Per-instance custom data in the same slots:
    0-2 the coat
    3   glow at night (0 on an Earth)
    4   seed
    5   its length, m (its scale)
    6   where on its grazing circle it starts, a share of a round
    7   its stride, m (its herd's loop a whole number of them, its circle too)
    8   its grazing circle's radius, m, negative for one walked clockwise
    9   the distance it fades out by, cm (a walker at a time, by a number of its own, over the last fifth of it)
Per-herd custom primitive data (SlonkFauna::HerdData, every one of the 36 slots, set each frame by SlonkFauna::StepHerd):
    0   night; 6, 8 and 9 the fright as a flock's (live, when last startled, when its alarm began to come in)
    5   the way the herd walks, radians in its frame (from x toward y): turned only as it walks (run on here by 3 a metre)
        or while it grazes, held while its walkers turn to walk or back to graze
    7   how fast its walkers' legs walk, m/s: its frame's way ahead over its plane, and a turn's own steps
    10  how far its legs have walked, m (a whole turn to walk or back to graze adds 4, its walkers stepping round where
        they stand), and 11 how long it has grazed, s, each as of 12 (a moment on this material's clock), running on from
        then at 7 and, only while none of the herd is on the move, at 1; 13 the share of the herd on the move, run on
        through a turn at 7 over 4 m: so its walking is the exact integral of the rates the game gives, and the velocity
        pass sees its legs move
    1   how far its legs had walked when its latest turn began (as 10 counts it), 10000 more for a turn back to graze;
        2 what came between it and the turn before: before a turn to walk the grazing clock the stop before left it at
        (its clock through the latest being 11, which stands till it grazes), before one back to graze how far its legs
        walked, m, and once it grazes again its clock through that turn; 4 the herd's ways through the turn before and the
        latest, each to a 3142nd of a round, the one times 3142 plus the other; 3 how sharply its way turns as it walks,
        radians a metre
    14-34 the drawn ground over the herd's plane, cm along its normal, at a 5 by 5 grid's nodes less its corners (row by
        row from the most negative y)
    35  how far that grid reaches either way of the herd's middle, cm (its nodes half that apart)
Each walker: grazing, it walks its circle a stride at a time (a whole number of times an hour of grazing), its head down
in the grass and up now and then; on the move with its herd it turns the way the herd walks, its head carried, a little
higher at a trot, up while frightened; turning to walk or back to graze, it steps round where it stands, the same way
all through the turn (its body by the turn's own way as slot 4 packs it, as its feet are carried round, so both go the
same way round however near a half turn it is); its legs sheared from the hip, each foot planted for 0.6 of each stride
where it was put down while the body goes on over it (walking, round its circle, round where it stands: each way the
body has gone since the foot was put down, through the turn before the latest, what came between, the latest and what
came after, carries it back in the body's frame), so no foot slides, then swung forward to where the way the body will
then be going puts it down, and lifted; its tail swishing;
grown by a speck floor (no shorter than 3 pixels, at most 4 times its size, from its feet); and every vertex stood on the
ground under it, read from the grid. Every vertex stays within 400 unit-mesh cm of the mesh's box (its circle, up to
2.8 m for a walker at least 1.45 m long, 193; the speck floor's growth of a vertex at most 53 cm out to four times that;
the ground, up to 150 cm, 104), which the instances' boxes are padded by (FFaunaKindDesc::CullPaddingUnits), and
Slonk.Fauna.Walker checks a float copy of this branch against it (SlonkFauna::WalkerMotion, which must change with it:
the test holds the CRC of the branch's lines, its comments aside, as the copy was made, and fails when they change).
Its normals are turned the way it faces (the Normal output, in tangent space; every other branch gives the vertex's own).

The motion (WPO_CODE): bursts of wingbeats on a cycle of 3 to 7 s of each bird's own, gliding with the wings a little
raised between, beating all the time and faster while startled; the body banked into the turn; a ripple through the
flock; a startle's scatter, out from the flock's middle and mostly away from what startled it, opening in a second and
closing over half a minute (the flight itself is the group's frame); and a speck floor, a bird far off drawn no smaller
than about two pixels across (at most four times its size), its wings and bank scaled back as it grows so the box it is
culled by holds it. Every vertex stays within 400 unit-mesh cm of the mesh's box: the instances' boxes are padded by
that (FFaunaKindDesc::CullPaddingUnits), and Slonk.Fauna.Meshes checks a float copy of this code against it
(SlonkFauna::FlierMotion, which must change with it: the test holds the CRC of this code's lines, its comments and the
mote branch aside, as the copy was made, and fails when they change). Two-sided foliage shading lets the sun through
the wings.

Run with the editor open and no play session:  python3 tools/planet_materials/fauna_material.py
To see whether the asset has drifted from it:  python3 tools/ue_mcp/matgraph.py check tools/planet_materials/fauna_material.py M_Fauna
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
from uemat import *

NAME = "M_Fauna"

CODE = r"""
// The vertex's own normal, but for a walker's, turned the way it faces.
Nrm = float3(0.0, 0.0, 1.0);
[branch] if (Walkers > 0.5)
{
    // A herd's walker (SlonkFauna::Walker): its coat from its pigment, countershaded, its legs darkening to the hooves, a
    // share of the coats pied with white patches of their own, the tail, horns, hooves and muzzle dark.
    float wleg = (VCA > 0.65 && VCA < 0.85) ? 1.0 : 0.0;
    float wtail = (VCA > 0.15 && VCA < 0.35) ? 1.0 : 0.0;
    float wdark = saturate(1.0 - abs(VC.r - 0.25) * 8.0);
    float3 coat = Pigment * (0.85 + 0.3 * frac(Seed * 11.3));
    float3 wcol = coat * lerp(1.15, 0.9, saturate(UV.y));
    wcol *= lerp(1.0, 0.6, wleg * VC.g);
    float pied = frac(Seed * 17.7) < 0.2 ? 1.0 : 0.0;
    float patch = sin(UV.x * 21.0 + Seed * 37.0) * sin(UV.y * 11.0 + Seed * 23.0) + 0.3 * sin(UV.x * 47.0 + UV.y * 13.0 + Seed * 7.0);
    wcol = lerp(wcol, float3(0.72, 0.7, 0.66), pied * step(0.35, patch) * (1.0 - wleg) * (1.0 - wtail));
    wcol = lerp(wcol, coat * 0.45, wtail);
    wcol = lerp(wcol, float3(0.045, 0.04, 0.035), wdark);
    // Fading out over the last fifth of its fade, a walker at a time by a number of its own that holds from frame to
    // frame (a speck far off is drawn whole or not at all, as the temporal upscaler needs a speck to be).
    float wfade = FadeFar > 0.0 ? saturate((FadeFar - length(WP - ViewPos)) / (0.2 * FadeFar)) : 1.0;
    Mask = wfade > frac(Seed * 91.37 + 0.13) ? 1.0 : 0.0;
    Rough = 0.85;
    Sub = float3(0.0, 0.0, 0.0);
    Emis = GlowColor * (Glow * Night * 0.3);
    // The way it faces in its herd's frame, as the vertex shader turns it (Heading: the herd's way, slot 5, run on as it
    // walks by WalkKap, slot 3; HerdGait: how fast its legs walk, slot 7; PivotRef: its latest turn, slot 1; TurnPack: the
    // ways through its turns, slot 4; PerKind: its grazing circle; Rate: its stride): its normal turned by it about its
    // up, then into the tangent space the engine takes the normal in.
    float wls = max(Rate, 0.05);
    float wrho = clamp(abs(PerKind), 0.01, 2.8);
    float wsg = PerKind < 0.0 ? -1.0 : 1.0;
    float wage = fmod(T - ClockT0 + 7201.0, 3600.0) - 1.0;
    float wspd = max(HerdGait, 0.0);
    float wtr0 = saturate(Travel);
    float wpsg = PivotRef > 5000.0 ? -1.0 : 1.0;
    float wtr = (wtr0 > 0.0 && wtr0 < 1.0) ? saturate(wtr0 + wpsg * wspd * wage / 4.0) : wtr0;
    float wgz = Graze + (wtr0 <= 0.0 ? wage : 0.0);
    float wk = max(round(6.2831853 * wrho / wls), 1.0);
    float wn = max(round(3600.0 / (wk * (18.0 + 16.0 * frac(Seed * 5.17)))), 1.0);
    float wps = 3600.0 / (wn * wk);
    float wu = wgz / wps + frac(Seed * 3.31);
    float wt1 = saturate((frac(wu) - 0.15) / 0.35);
    float wst = floor(wu) + wt1 * wt1 * (3.0 - 2.0 * wt1);
    float wyc = 6.2831853 * (Phase + wsg * wst / wk) + wsg * 1.5707963;
    float whd = Heading + (wtr0 >= 1.0 ? WalkKap * wspd * wage : 0.0);
    float wdy = whd - wyc;
    wdy -= 6.2831853 * round(wdy / 6.2831853);
    if (wtr0 > 0.0 && wtr0 < 1.0)
    {
        // Through a turn, by the turn's own way as TurnPack (slot 4) packs it, as the vertex shader turns the body.
        float wtq = floor((TurnPack + 0.5) / 3142.0);
        wdy = (TurnPack - wtq * 3142.0) * (6.2831853 / 3142.0) - wyc;
        wdy -= 6.2831853 * round(wdy / 6.2831853);
    }
    float wyaw = wyc + wtr * wdy;
    float3 wkx = normalize(BodyUp);
    float3 wn0 = normalize(N);
    float wcy = cos(wyaw);
    float wsy = sin(wyaw);
    float3 wnw = wn0 * wcy + cross(wkx, wn0) * wsy + wkx * dot(wkx, wn0) * (1.0 - wcy);
    float3 wt0 = Parameters.TangentToWorld[0];
    float3 wb0 = Parameters.TangentToWorld[1];
    float3 wz0 = Parameters.TangentToWorld[2];
    float wdet = dot(wt0, cross(wb0, wz0));
    Nrm = abs(wdet) > 1e-4 ? normalize(float3(dot(cross(wb0, wz0), wnw), dot(cross(wz0, wt0), wnw), dot(cross(wt0, wb0), wnw)) / wdet) : float3(0.0, 0.0, 1.0);
    return wcol;
}
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
[branch] if (Walkers > 0.5)
{
    // A herd's walker (SlonkFauna::Walker; SlonkFauna::WalkerMotion is a float copy of this: change both). Its herd's
    // slots it reads by their flock's pins: Bank is the way the herd walks (slot 5), Spread how fast its legs walk (slot
    // 7), ScatterT0 where its latest turn to walk or back to graze began (slot 1), SDX what came before it (2), SDY how
    // sharply its way turns as it walks (3), SDZ its ways through that turn and the one before (4); Bearing is its
    // grazing circle's radius (custom 8), Rate its stride (custom 7). Offsets in the unit mesh's cm, in the instance's frame,
    // which is the herd's unturned and scaled by its length.
    float s = max(Size, 0.1);
    float ls = max(Rate, 0.05);
    float wrho = clamp(abs(Bearing), 0.01, 2.8);
    float wsg = Bearing < 0.0 ? -1.0 : 1.0;
    // The herd's clocks as the game gave them, run on from then at the rates it gave: how far its legs have walked, m;
    // its share on the move, through a turn in step with its legs (a whole turn is 4 m of their walk); how long it has
    // grazed, s (only while none of it is on the move).
    float wage = fmod(T - ClockT0 + 7201.0, 3600.0) - 1.0;
    float spd = max(Spread, 0.0);
    float wdist = Dist + spd * wage;
    float wtr0 = saturate(Travel);
    float wpsg = ScatterT0 > 5000.0 ? -1.0 : 1.0;
    float wmps = ScatterT0 - (wpsg < 0.0 ? 10000.0 : 0.0);
    float wtr = (wtr0 > 0.0 && wtr0 < 1.0) ? saturate(wtr0 + wpsg * spd * wage / 4.0) : wtr0;
    float wgz = Graze + (wtr0 <= 0.0 ? wage : 0.0);
    // Grazing, round its own circle a stride a step, a whole number of steps a round and of rounds an hour of grazing,
    // each step coming in and going smoothly and the walker standing still between.
    float wk = max(round(6.2831853 * wrho / ls), 1.0);
    float wn = max(round(3600.0 / (wk * (18.0 + 16.0 * frac(Seed * 5.17)))), 1.0);
    float wps = 3600.0 / (wn * wk);
    float wof = frac(Seed * 3.31);
    float wu = wgz / wps + wof;
    float wt1 = saturate((frac(wu) - 0.15) / 0.35);
    float wst = floor(wu) + wt1 * wt1 * (3.0 - 2.0 * wt1);
    float wth = 6.2831853 * (Phase + wsg * wst / wk);
    float2 wcirc = float2(cos(wth), sin(wth)) * wrho * 100.0;
    float wyc = wth + wsg * 1.5707963;
    // The herd's latest turn, as this walker takes it. Its grazing clock through it (it grazes no more till all the herd
    // stands grazing again: Graze while any of it is on the move, after a turn back to graze SDX), so where it stood on
    // its circle then; the herd's ways through the turn before and the latest (SDZ, each to a 3142nd of a round); and
    // how far it turned, the way round its feet below are carried by.
    float wgr0 = wtr0 > 0.0 ? Graze : SDX;
    float wu1 = wgr0 / wps + wof;
    float wq1 = saturate((frac(wu1) - 0.15) / 0.35);
    float wsr0 = floor(wu1) + wq1 * wq1 * (3.0 - 2.0 * wq1);
    float wtq = floor((SDZ + 0.5) / 3142.0);
    float wway1 = (SDZ - wtq * 3142.0) * (6.2831853 / 3142.0);
    float wway0 = wtq * (6.2831853 / 3142.0);
    float wdyp = wway1 - (6.2831853 * (Phase + wsg * wsr0 / wk) + wsg * 1.5707963);
    wdyp -= 6.2831853 * round(wdyp / 6.2831853);
    // On the move, turned the way the herd walks (run on as it walks by how sharply that turns), by its share on the
    // move. The herd's way never moves while its walkers turn, nor their circles, so each turns one way all through:
    // through a turn by that same turn as its feet take it, so body and feet go the same way round however near a half
    // turn it is.
    float whd = Bank + (wtr0 >= 1.0 ? SDY * spd * wage : 0.0);
    float wdy = whd - wyc;
    wdy -= 6.2831853 * round(wdy / 6.2831853);
    if (wtr0 > 0.0 && wtr0 < 1.0)
    {
        wdy = wdyp;
    }
    float wyaw = wyc + wtr * wdy;
    // Its strides walked: round its circle and with the herd (its turns' steps among them), the exact integral of each
    // rate; and how fast it walks.
    float wstr = wst + wdist / ls;
    float wv = spd + (wtr0 <= 0.0 ? ls * (6.0 * wt1 * (1.0 - wt1) / 0.35) / wps : 0.0);
    // Its head: down in the grass as it grazes, looking up now and then (a whole number of times an hour); carried as it
    // walks, a little higher at a trot; up and alert while it is frightened.
    float wpa = fmod(T - PanicT0 + 7201.0, 3600.0) - 1.0;
    float woa = fmod(T - OnsetT0 + 7201.0, 3600.0) - 1.0;
    float walarm = (Startled > 0.5 ? 1.0 : 0.0) * saturate((1.0 - smoothstep(20.0, 120.0, wpa)) * 3.0) * smoothstep(0.0, 0.3, woa);
    float wlc = frac(T * ((60.0 + floor(frac(Seed * 9.13) * 90.0)) / 3600.0) + Seed * 7.7);
    float wlook = smoothstep(0.0, 0.03, wlc) * (1.0 - smoothstep(0.09, 0.12, wlc));
    float wpitch = lerp(lerp(-1.25, -0.35, wlook), lerp(-0.1, 0.05, smoothstep(1.5, 2.5, wtr >= 1.0 ? spd : 0.0)), wtr);
    wpitch = lerp(wpitch, 0.25, walarm);
    float3 p = LP;
    if (VCA > 0.65 && VCA < 0.85)
    {
        // A leg, sheared from its hip: its foot planted where it was put down for 0.6 of each stride while the body goes
        // on over it (walking, grazing round its circle, stepping round where it stands as the herd turns to walk or back
        // to graze), then swung forward to where it will next be put down, lifted the more the faster it walks.
        // The herd's latest turn (above) and the one before it, as this walker's feet take them. The turn before, the
        // other way, and what came between: before a turn to walk, the last stop (from the herd's way then back to where
        // it stood grazing at the clock SDX) and its own steps grazing since; before one back to graze, the last set-off
        // (from its circle's way to the herd's) and the herd's walk since (SDX metres; none once it grazes again).
        float wbetween = 0.0;
        float wat2 = 0.0;
        float wdy0 = 0.0;
        if (wpsg > 0.0)
        {
            float wu2 = SDX / wps + wof;
            float wq2 = saturate((frac(wu2) - 0.15) / 0.35);
            float wsold = floor(wu2) + wq2 * wq2 * (3.0 - 2.0 * wq2);
            wbetween = wsr0 - wsold;
            wat2 = wsold + wmps / ls;
            wdy0 = wway0 - (6.2831853 * (Phase + wsg * wsold / wk) + wsg * 1.5707963);
            wdy0 = 6.2831853 * round(wdy0 / 6.2831853) - wdy0;
        }
        else
        {
            float wwm = wtr0 > 0.0 ? SDX : 1000.0;
            wbetween = wwm / ls;
            wat2 = wsr0 + (wmps - wwm) / ls;
            wdy0 = wway0 - (6.2831853 * (Phase + wsg * wsr0 / wk) + wsg * 1.5707963);
            wdy0 -= 6.2831853 * round(wdy0 / 6.2831853);
        }
        // How many of its strides ago the latest turn ended (negative while it runs) and began, and the turn before ended
        // and began; its strides walked at the first three of those moments, and this foot's phase at each.
        float4 wago;
        wago.x = (wdist - wmps - 4.0) / ls + (wst - wsr0);
        wago.y = wago.x + 4.0 / ls;
        wago.z = wago.y + wbetween;
        wago.w = wago.z + 4.0 / ls;
        float3 wph = frac(float3(wsr0 + (wmps + 4.0) / ls, wsr0 + wmps / ls, wat2) - VC.b);
        // The ways the body has gone, a stride of each ahead (m) and round (radians): the turn before (stepping round where
        // it stands), between, the latest turn, after (grazing round its circle, walking with the herd as its way turns).
        float wgr = wsg * ls / wrho;
        float wwk = SDY * ls;
        float4 wsa4 = float4(0.0, ls, 0.0, ls);
        float4 wsr4 = float4(wdy0 * ls / 4.0, wpsg > 0.0 ? wgr : wwk, wpsg * wdyp * ls / 4.0, wpsg > 0.0 ? wwk : wgr);
        float wlp = frac(wstr - VC.b);
        bool wsw = wlp >= 0.6;
        // Where the foot put down is, in the body's frame: in the stance it is in, or for a foot in the air the one it
        // lifted from as it was then (wl of its strides after it was put down, wsh strides ago): put down half a stance
        // ahead of where it stands at its stance's middle (the vertex's own place) by the way the body was then going
        // (known by the foot's phase at each moment, so a foot put down just as a turn began is so put down for good),
        // then carried back by each way the body has gone since, the vertex turned back by the body's turn and moved back
        // by its way (a chord of its arc).
        float wsh = wsw ? wlp - 0.6 : 0.0;
        float wl = wlp - wsh;
        float4 wb = wago - wsh;
        float4 wam = float4(max(min(wl, wb.w) - max(wb.z, 0.0), 0.0), max(min(wl, wb.z) - max(wb.y, 0.0), 0.0), max(min(wl, wb.y) - max(wb.x, 0.0), 0.0), clamp(wb.x, 0.0, wl));
        int wtd = round(wlp - wago.z - wph.z) >= 0.0 ? 0 : (round(wlp - wago.y - wph.y) >= 0.0 ? 1 : (round(wlp - wago.x - wph.x) >= 0.0 ? 2 : 3));
        float2 wq = LP.xy;
        [unroll] for (int wi = 0; wi < 5; wi++)
        {
            int wm = wi == 0 ? wtd : wi - 1;
            float wsn = wi == 0 ? -0.3 : wam[max(wi - 1, 0)];
            float wda = wsa4[wm];
            float wdr = wsr4[wm];
            float wan = wdr * wsn;
            float wcn = cos(wan);
            float wsn2 = sin(wan);
            float2 wch = abs(wdr) < 1e-4 ? float2(-wda * wsn, 0.5 * wda * wsn * wan) : float2(-wsn2, 1.0 - wcn) * (wda / wdr);
            wq = float2(wq.x * wcn + wq.y * wsn2, -wq.x * wsn2 + wq.y * wcn) + wch * (100.0 / s);
        }
        float2 wfoot = wq - LP.xy;
        float wzf = 0.0;
        if (wsw)
        {
            // Swung forward to where it will be put down: half a stance ahead of its place, as the way the body will then
            // be going moves it (through the latest turn or after it, as its end falls before or after); and, the turn
            // having begun while it was in the air, from where it was to be put down before, its aim moving over as it
            // comes down.
            float wqs = wsh / 0.4;
            float wsq = wqs * wqs * (3.0 - 2.0 * wqs);
            int wnew = round(wlp - 1.0 - wago.x - wph.x) >= 0.0 ? 2 : 3;
            bool wlf = round(wlp - wago.y - wph.y) >= 0.0 && wph.y >= 0.6;
            int wold = wlf ? (wbetween > 1e-4 ? 1 : 0) : wnew;
            float2 waim[2];
            [unroll] for (int wj = 0; wj < 2; wj++)
            {
                int wm2 = wj == 0 ? wold : wnew;
                float wda2 = wsa4[wm2];
                float wdr2 = wsr4[wm2];
                float wan2 = wdr2 * -0.3;
                float wcn2 = cos(wan2);
                float wsn3 = sin(wan2);
                float2 wch2 = abs(wdr2) < 1e-4 ? float2(0.3 * wda2, -0.15 * wda2 * wan2) : float2(-wsn3, 1.0 - wcn2) * (wda2 / wdr2);
                waim[wj] = float2(LP.x * wcn2 + LP.y * wsn3, -LP.x * wsn3 + LP.y * wcn2) + wch2 * (100.0 / s) - LP.xy;
            }
            float wqb = saturate((wph.y - 0.6) / 0.4);
            float wh = wlf ? saturate((wqs - wqb) / max(1.0 - wqb, 1e-3)) : 1.0;
            wfoot = lerp(wfoot, lerp(waim[0], waim[1], wh * wh * (3.0 - 2.0 * wh)), wsq);
            wzf = 6.0 * sin(3.14159265 * wqs) * smoothstep(0.03, 0.25, wv);
        }
        p.x += wfoot.x * VC.g;
        p.y += wfoot.y * VC.g;
        p.z += wzf * VC.g;
    }
    else if (VCA > 0.3 && VCA < 0.45)
    {
        // The neck and head, turned about the neck's root (14, 40), the root itself held to the body.
        float wa = wpitch * smoothstep(0.0, 0.3, VC.g);
        float wrx = p.x - 14.0;
        float wrz = p.z - 40.0;
        float wca = cos(wa);
        float wsa = sin(wa);
        p.x = 14.0 + wrx * wca - wrz * wsa;
        p.z = 40.0 + wrx * wsa + wrz * wca;
    }
    else if (VCA > 0.15 && VCA < 0.35)
    {
        // The tail swishes, a whole number of times an hour.
        p.y += VC.g * 10.0 * sin(6.2831853 * frac(T * ((400.0 + floor(frac(Seed * 2.71) * 500.0)) / 3600.0) + Seed * 1.9));
    }
    // The speck floor: no shorter than 3 pixels far off, at most four times its size, grown from its feet.
    float wpx = s * 100.0 * PxPerRad / max(length(WP - ViewPos), 1.0);
    p *= clamp(3.0 / max(wpx, 0.001), 1.0, 4.0);
    // Turned the way it faces, and set where it stands on its circle.
    float wcy = cos(wyaw);
    float wsy = sin(wyaw);
    p = float3(p.x * wcy - p.y * wsy, p.x * wsy + p.y * wcy, p.z);
    p.xy += wcirc / s;
    // On the drawn ground under each vertex, over its herd's plane: bilinear over the grid of its heights, each corner
    // made from the three nodes beside it, held at the grid's edge beyond it.
    float hg[25];
    hg[1] = H0; hg[2] = H1; hg[3] = H2;
    hg[5] = H3; hg[6] = H4; hg[7] = H5; hg[8] = H6; hg[9] = H7;
    hg[10] = H8; hg[11] = H9; hg[12] = H10; hg[13] = H11; hg[14] = H12;
    hg[15] = H13; hg[16] = H14; hg[17] = H15; hg[18] = H16; hg[19] = H17;
    hg[21] = H18; hg[22] = H19; hg[23] = H20;
    hg[0] = hg[1] + hg[5] - hg[6];
    hg[4] = hg[3] + hg[9] - hg[8];
    hg[20] = hg[21] + hg[15] - hg[16];
    hg[24] = hg[23] + hg[19] - hg[18];
    float2 wat = (LPP.xy - s * LP.xy) + s * p.xy;
    float wgsp = max(GroundHalf, 100.0) * 0.5;
    float2 wgg = clamp(wat / wgsp + 2.0, 0.0, 4.0);
    int wgx = min((int)floor(wgg.x), 3);
    int wgy = min((int)floor(wgg.y), 3);
    float2 wgf = wgg - float2((float)wgx, (float)wgy);
    float wga = lerp(hg[wgy * 5 + wgx], hg[wgy * 5 + wgx + 1], wgf.x);
    float wgb = lerp(hg[(wgy + 1) * 5 + wgx], hg[(wgy + 1) * 5 + wgx + 1], wgf.x);
    p.z += clamp(lerp(wga, wgb, wgf.y), -150.0, 150.0) / s;
    return p - LP;
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

HERD_GROUND = ["H%d" % i for i in range(21)]
INPUTS = ["VC", "N", "BodyUp", "Pigment", "Glow", "Seed", "FadeFar", "WP", "ViewPos", "Night", "GlowColor",
          "Motes", "UV", "SunDir", "T", "Phase", "PerKind", "PxPerRad",
          "Walkers", "Rate", "Heading", "Graze", "ClockT0", "Travel", "VCA", "HerdGait", "PivotRef", "WalkKap", "TurnPack"]
WPO_INPUTS = ["LP", "VC", "VCA", "T", "Size", "Seed", "Phase", "Rate", "Bearing", "ScatterT0", "SDX", "SDY", "SDZ", "Bank", "Startled", "Spread",
              "PanicT0", "OnsetT0", "WP", "ViewPos", "PxPerRad", "Motes", "UV", "SunDir", "BodyUp", "AxX", "AxY",
              "Walkers", "LPP", "Dist", "Graze", "ClockT0", "Travel", "GroundHalf"] + HERD_GROUND


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
    # The walker branch: on in the herds' instance alone. A herd's own slots of its custom primitive data
    # (SlonkFauna::HerdData): its clocks, its share on the move, the ground under it.
    walkers = g.scalar("Walkers", 0.0)
    herd_dist = g.scalar("HerdDist", 0.0, cpd=10)
    herd_graze = g.scalar("HerdGraze", 0.0, cpd=11)
    herd_clock = g.scalar("HerdClockT0", 0.0, cpd=12)
    herd_travel = g.scalar("HerdTravel", 0.0, cpd=13)
    herd_ground = [g.scalar("HerdGround%d" % i, 0.0, cpd=14 + i) for i in range(21)]
    herd_half = g.scalar("HerdGroundHalf", 1000.0, cpd=35)
    # Where a vertex is in its herd's frame (its instance's place in it), for the ground under it.
    lpp = g.node("LocalPosition", {"IncludedOffsets": "ExcludeOffsets", "LocalOrigin": "Primitive"})
    uv = g.texcoord(0)
    # The material's own clock, wrapped to the hour every path and cycle divides: never a parameter, so the velocity
    # pass sees the previous frame's motion as well as this one's.
    tm = g.node("Time", {"bOverride_Period": True, "Period": 3600.0})
    lp = g.node("LocalPosition", {"IncludedOffsets": "ExcludeOffsets", "LocalOrigin": "Instance"})
    wp = g.node("WorldPosition")

    e = g.custom(CODE, INPUTS, "CMOT_Float3", "fauna",
                 extra_outputs=(("Mask", "CMOT_Float1"), ("Rough", "CMOT_Float1"), ("Sub", "CMOT_Float3"), ("Emis", "CMOT_Float3"), ("Nrm", "CMOT_Float3")))
    for pin, src in [("VC", vc), ("N", n), ("BodyUp", up), ("Pigment", pigment), ("Glow", glow), ("Seed", seed), ("FadeFar", fade_far),
                     ("WP", wp), ("ViewPos", view_pos), ("Night", night), ("GlowColor", glow_color),
                     ("Motes", motes), ("UV", uv), ("SunDir", sun_dir), ("T", tm), ("Phase", phase), ("PerKind", bearing), ("PxPerRad", px_per_rad),
                     ("Walkers", walkers), ("Rate", rate), ("Heading", bank), ("Graze", herd_graze), ("ClockT0", herd_clock), ("Travel", herd_travel),
                     ("HerdGait", spread), ("PivotRef", scatter_t0), ("WalkKap", sdy), ("TurnPack", sdz)]:
        g.c(src, e, pin)
    # A walker's joint kind, the colour node's alpha pin.
    g.c(vc, e, "VCA", "A")

    w = g.custom(WPO_CODE, WPO_INPUTS, "CMOT_Float3", "fauna motion")
    for pin, src in [("LP", lp), ("VC", vc), ("T", tm), ("Size", size), ("Seed", seed), ("Phase", phase), ("Rate", rate), ("Bearing", bearing),
                     ("ScatterT0", scatter_t0), ("SDX", sdx), ("SDY", sdy), ("SDZ", sdz), ("Bank", bank), ("Startled", startled), ("Spread", spread),
                     ("PanicT0", panic_t0), ("OnsetT0", onset_t0), ("WP", wp), ("ViewPos", view_pos), ("PxPerRad", px_per_rad),
                     ("Motes", motes), ("UV", uv), ("SunDir", sun_dir), ("BodyUp", up), ("AxX", axx), ("AxY", axy),
                     ("Walkers", walkers), ("LPP", lpp), ("Dist", herd_dist), ("Graze", herd_graze), ("ClockT0", herd_clock), ("Travel", herd_travel),
                     ("GroundHalf", herd_half)] + list(zip(HERD_GROUND, herd_ground)):
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
    g.out(e, "MP_Normal", "Nrm")
    g.out(wt, "MP_WorldPositionOffset")
    g.finish()


if __name__ == "__main__":
    build()
