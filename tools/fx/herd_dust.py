"""Builds the herd dust: NS_HerdDust, the soft puffs a herd of grazing animals throws up while it trots or gallops, and
M_HerdDust, the lit translucent material they are drawn with. The game spawns one pooled component of the system per
moving herd, attached to the herd's frame (its origin on the ground at the herd's middle, its z up from the ground), and
sets its user parameters; this script is the only place the asset is authored.

The user parameters (the contract with the game's C++):
    User.Rate    float, puffs a second (0 none)
    User.Radius  float, cm: the herd's footprint radius about the component's origin, typically 1500-3000
    User.Color   LinearColor: the dust's colour, its alpha the opacity, typically a light soil tan at 0.3-0.6
    User.Drift   Vector, world cm/s: the herd's velocity over the ground, typically 300-1100 while it trots or gallops
    User.Size    float, cm: a puff's starting size, typically 120-250
    User.Carry   Vector, world cm/s: the ground's own world velocity at the herd from the planet's spin (0 while the spin
                 is held), given to each puff in full and the velocity its drag is measured against

One CPU emitter, Dust, simulating in world space so a puff stays where it was raised while the herd moves on. Puffs are
born User.Rate a second on a cylinder of radius User.Radius about the component's origin, 0 to 60 cm up its z, and live
3 to 6 s. Each starts with the ground's own velocity (User.Carry: the world turns under the herd, and the ship and the
camera ride it) and is thrown with a quarter to two fifths of User.Drift (in the world) and a gentle rise and spread of
tens of cm/s in the herd's own frame (so the rise is up from the ground however the planet's surface is turned); drag
against the ground (its velocity less User.Carry decays at 0.7 a second) slows it to rest on the turning ground, and a
little curl noise makes it billow, so the herd draws ahead of its dust. A puff starts 0.7 to 1.3 times
User.Size, grows to three times that over its life (a curve, fast at first), turns at a random angle and spins slowly
(at most 15 degrees a second, from the puff's own random). Its colour is User.Color as it was at the puff's birth, its
alpha scaled by a curve that fades in over the first 15% of the life and out to zero at the end. The herd's up at its
birth (the component's z) goes to the material as dynamic parameter 0. Past 2 km from the camera the emitter sleeps and
lets its puffs finish. At most 512 puffs a herd (FixedCount): Rate 60 with 6 s lives is ~360.

M_HerdDust is translucent and unlit, and lights itself: the sky atmosphere's sun (its way, and its light where the puff
is, through the air above it) on a soft wrap round the puff plus a quarter of it from the sky, as the luminance a surface
of the puff's colour would have, so it sits at the ground's own exposure. Lit translucency came out underlit in the game
(the translucency lighting volume ends 50 m from the camera and its voxels straddle the ground the dust hugs, and the
per-pixel forward-shaded puffs came out a dark maroon over sunlit grass: translucency takes the sun through the engine's
one global atmosphere transmittance, a permanent sunset on these worlds). Its opacity is the particle's alpha times a soft round mask
from the sprite's UVs (its rim wobbled and its middle clumped by the particle's own random, so puffs are not perfect
discs) times a depth fade over 150 cm, so puffs do not cut into the ground. The normal it lights by is a world one: the
herd's up tilted toward the sprite's rims, so a puff seen from any side is lit like the ground it rose from and shaded
like a ball of dust (a card facing the camera went black whenever the sun was beside or behind the puffs).

Run with the editor open and no play session:  python3 tools/fx/herd_dust.py
It deletes and recreates both assets, checks the system's compile state and stack issues, and saves both.
"""
import json, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
import mcp
import uemat
from uemat import G, T, AS, MT

FOLDER = "/Game/Slonk/FX"
MAT = "M_HerdDust"
SYS = "NS_HerdDust"
EMITTER = "Dust"
SYS_REF = {"refPath": f"{FOLDER}/{SYS}.{SYS}"}
NS = "NiagaraToolsets.NiagaraToolset_System"
NI = "NiagaraToolsets.NiagaraToolset_Info"

# ----------------------------------------------------------------------------------------------------------------------
# The material.

MAT_CODE = r"""
// The sprite's middle at 0, its edges at 1.
float2 c = UV * 2.0 - 1.0;
float ang = atan2(c.y, c.x);
float ph = Rnd * 6.2831853;
// A ragged rim: two slow lobes of the puff's own phase.
float rim = 1.0 + 0.11 * sin(ang * 3.0 + ph) + 0.07 * sin(ang * 5.0 - ph * 2.3);
// Scaled so even the rim's farthest lobe ends inside the card: no straight cut at its edges.
float r = length(c) * 1.18 / rim;
// Soft all the way to the rim: no hard edge.
float m = saturate(1.0 - r);
m = m * m * (3.0 - 2.0 * m);
// Clumps inside it, from the same random.
float clump = 0.5 + 0.5 * sin(c.x * 3.3 + ph) * sin(c.y * 2.9 - ph * 1.7);
m *= lerp(0.7, 1.0, clump);
Opacity = saturate(A * m);
// The normal, in the world: the herd's up (the emitter writes its frame's z into the dynamic parameter), so a puff is
// lit like the ground it rose from whichever side the camera sees it from, tilted toward the sprite's rims along its own
// axes so it is shaded like a ball of dust. Without the parameter (the material's own preview; its default is 0) it falls
// back to the card's normal.
float3 tx = Parameters.TangentToWorld[0];
float3 ty = Parameters.TangentToWorld[1];
float3 up = dot(Up, Up) > 0.01 ? normalize(Up) : Parameters.TangentToWorld[2];
float3 n = normalize(up + (c.x * tx + c.y * ty) * 0.6);
// Lit as the ground beside it is: by the sky atmosphere's sun (its way and its light where the puff is, the air's colour
// through it included), a soft wrap round the ball, and a quarter of it from the sky on the shaded side, as luminance a
// surface of the puff's colour would have (light over pi), so it sits in the scene at the ground's own exposure. Without
// a sky atmosphere's sun (the material's preview) a light of 1 from overhead.
float3 l = dot(L, L) > 1e-6 ? normalize(L) : up;
float3 e = max(E.r, max(E.g, E.b)) > 0.0 ? E : float3(1.0, 1.0, 1.0);
float lit = saturate(dot(n, l) * 0.6 + 0.4);
return Col * e * (0.25 + 0.75 * lit) * 0.31830989;
"""


def strict(ts, tool, args):
    """The material toolset's calls, failing the build on any error instead of printing it and going on."""
    r = mcp.unwrap(mcp.call_tool("call_tool", {"toolset_name": ts, "tool_name": tool, "arguments": args}))
    if not isinstance(r, dict) or r.get("isError") or "error" in r or r.get("returnValue") is False:
        raise RuntimeError(f"{tool} failed: {json.dumps(r, default=str)[:1200]}")
    return r


def build_material():
    uemat.MATERIAL_FOLDER = FOLDER
    # Every graph call of uemat's (G, node, c, custom, out) goes through its T, and this script's own T from here on
    # (creating, recompiling, saving): made strict for this build.
    global T
    uemat.T = strict
    T = strict
    T(MT, "create_material", {"folder_path": FOLDER, "asset_name": MAT})
    g = G(MAT, {"BlendMode": "BLEND_Translucent", "ShadingModel": "MSM_Unlit", "TwoSided": False,
                "bUsedWithNiagaraSprites": True})
    pc = g.node("ParticleColor")
    rnd = g.node("ParticleRandom")
    uv = g.texcoord(0)
    # Dynamic parameter 0: the herd's up in the world (its RGB), from the emitter.
    # Its default 0, so without the emitter (the material's own preview) the normal falls back to the card's.
    herd_up = g.node("DynamicParameter", {"DefaultValue": {"R": 0.0, "G": 0.0, "B": 0.0, "A": 0.0}})
    # The sky atmosphere's sun (light 0): its way, and its light where the puff is, through the air above that point (the
    # node's world position left to the pixel's own). Not the light 'on ground': that is the engine's one global value,
    # reckoned with the atmosphere's +Z as up, which on these worlds is a permanent sunset (SlonkGameMode turns on the
    # sun's per-pixel transmittance for the same reason), and it lit the dust a dark maroon.
    sun_dir = g.node("SkyAtmosphereLightDirection")
    sun_lux = g.node("SkyAtmosphereLightIlluminance")
    e = g.custom(MAT_CODE, ["Col", "A", "UV", "Rnd", "Up", "L", "E"], "CMOT_Float3", "herd dust puff",
                 extra_outputs=(("Opacity", "CMOT_Float1"),))
    # The particle colour node's main output is its RGB; the alpha is its own pin.
    g.c(pc, e, "Col")
    g.c(pc, e, "A", "A")
    g.c(uv, e, "UV")
    g.c(rnd, e, "Rnd")
    g.c(herd_up, e, "Up", "RGB")
    g.c(sun_dir, e, "L")
    g.c(sun_lux, e, "E")
    fade = g.node("DepthFade", {"FadeDistanceDefault": 150.0})
    g.c(e, fade, "Opacity", "Opacity")
    g.out(e, "MP_EmissiveColor")
    g.out(fade, "MP_Opacity")
    r = T(MT, "recompile", {"material_or_function": g.mat})
    if isinstance(r, dict) and r.get("isError"):
        raise RuntimeError("M_HerdDust failed to compile")
    print("  material compiled")
    return g.mat


# ----------------------------------------------------------------------------------------------------------------------
# The system: helpers over the Niagara toolset.

LINKED = "/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked"
HLSL = "/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression"
DYNAMIC = "/Script/NiagaraEditor.NiagaraExt_StackInputData_DynamicInput"
DATA_IF = "/Script/NiagaraEditor.NiagaraExt_StackInputData_DataInterface"
ENUM = "/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum"
FLOAT = "/Script/Niagara.NiagaraFloat"
BOOL = "/Script/Niagara.NiagaraBool"
VEC3 = "/Script/CoreUObject.Vector3f"
COLOR = "/Script/CoreUObject.LinearColor"


def call(ts, tool, args):
    r = mcp.tool(ts, tool, args)
    if not isinstance(r, dict) or r.get("isError"):
        raise RuntimeError(f"{tool} failed: {json.dumps(r, default=str)[:1200]}")
    return r.get("returnValue")


def at(script="", module="", stack=(), renderer=-1, emitter=EMITTER):
    return {"system": SYS_REF, "emitterName": emitter, "scriptName": script, "moduleName": module,
            "rendererIndex": renderer, "inputNameStack": list(stack)}


def f(v): return (FLOAT, {"value": float(v)})
def b(v): return (BOOL, {"value": -1 if v else 0})
def vec(x, y, z): return (VEC3, {"x": x, "y": y, "z": z})
def link(name, typ): return (LINKED, {"linkedVariable": {"name": name, "type": {"classStructOrEnum": {"refPath": typ}}}})
def hlsl(expr): return (HLSL, {"hlslExpression": expr})
def dyn(asset): return (DYNAMIC, {"dynamicInputAsset": {"refPath": asset}})


_ENUMS = {}
def enum(path, display):
    """An enum input by its display name, looked up in the enum itself."""
    if path not in _ENUMS:
        _ENUMS[path] = json.loads(call(NI, "UEnum_Info", {"enum": {"refPath": path}}))
    info = _ENUMS[path]
    for v in info["Values"]:
        if v["DisplayName"] == display:
            return (ENUM, {"enum": {"refPath": path}, "enumName": f"{info['Name']}::{v['Name']}", "displayName": display})
    raise KeyError(f"{display} not in {path}")


def curve(keys):
    """A float curve data interface from (time, value, arrive tangent, leave tangent, interp) keys. The tangents are
    set by hand (user tangents): auto tangents are only worked out by the curve editor. The curve is sampled directly
    rather than through its lookup table, which a CPU emitter does not need: the toolset builds the curve on a copy whose
    table it never rebuilds, so as it copies it into the stack the log notes "Post CopyToInternal LUT generation is out
    of sync" (the stack's own copy rebuilds its table), and with no table in use nothing can depend on a stale one."""
    ks = [{"interpMode": mode, "tangentMode": "RCTM_User", "tangentWeightMode": "RCTWM_WeightedNone", "time": t, "value": v,
           "arriveTangent": at_, "arriveTangentWeight": 0, "leaveTangent": lt, "leaveTangentWeight": 0}
          for t, v, at_, lt, mode in keys]
    return (DATA_IF, {"propertyValues": json.dumps({
        "Curve": {"keys": ks, "defaultValue": 3.4028234663852886e+38, "preInfinityExtrap": "RCCE_Constant", "postInfinityExtrap": "RCCE_Constant"},
        "bUseLUT": False, "bOptimizeLUT": False})})


def put(script, module, stack, value):
    if isinstance(stack, str):
        stack = [stack]
    call(NS, "SetStackInputData", {"stackInputRef": at(script, module, stack),
                                   "inputData": {"struct": {"refPath": value[0]}, "value": value[1]}})


def add_module(script, asset):
    rv = call(NS, "AddModule", {"moduleLocationRef": at(script), "moduleAsset": {"refPath": asset}})
    return rv["moduleName"]


def user_var(name, typ, value, desc):
    return {"name": name, "type": {"classStructOrEnum": {"refPath": typ}},
            "defaultValue": {"struct": {"refPath": typ}, "value": value}, "description": desc}


# ----------------------------------------------------------------------------------------------------------------------
# The system.

def build_system(material):
    call(NS, "CreateNiagaraSystem", {"assetName": SYS, "assetPath": FOLDER,
                                     "templateSystem": {"refPath": "/Niagara/DefaultAssets/DefaultSystem.DefaultSystem"}})
    # The template's own emitter goes; ours starts from the minimal one (initialize particle, particle state, a sprite).
    for em in call(NS, "GetSystemSummary", {"system": SYS_REF})["emitters"]:
        call(NS, "RemoveEmitter", {"emitterToRemove": at(emitter=em["emitterName"])})
    call(NS, "AddEmitter", {"system": SYS_REF, "emitterName": EMITTER,
                            "templateEmitter": {"refPath": "/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"}})

    call(NS, "AddUserVariables", {"system": SYS_REF, "variablesToAdd": [
        user_var("User.Rate", FLOAT, {"value": 40.0}, "Puffs a second; 0 none."),
        user_var("User.Radius", FLOAT, {"value": 2000.0}, "The herd's footprint radius about the component's origin, cm."),
        user_var("User.Color", COLOR, {"r": 0.62, "g": 0.52, "b": 0.38, "a": 0.45}, "The dust's colour; its alpha the opacity."),
        user_var("User.Drift", VEC3, {"x": 600.0, "y": 0.0, "z": 0.0}, "The herd's velocity, world cm/s."),
        user_var("User.Size", FLOAT, {"value": 180.0}, "A puff's starting size, cm."),
        user_var("User.Carry", VEC3, {"x": 0.0, "y": 0.0, "z": 0.0},
                 "The ground's own world velocity at the herd (the planet's spin), world cm/s: each puff takes it in full at "
                 "birth, and its drag is measured against it, so it settles on the turning ground rather than in empty space."),
    ]})

    # CPU, world space, a fixed allocation that bounds the count.
    call(NS, "SetEmitterData", {"emitter": at(), "emitterData": {"propertyValues": json.dumps({
        "SimTarget": "CPUSim", "bLocalSpace": False, "CalculateBoundsMode": "Dynamic",
        "AllocationMode": "FixedCount", "PreAllocationCount": 512})}})

    # Emitter update: the emitter culls itself past 2 km (it sleeps and lets its puffs finish, and wakes when the
    # camera comes back), then spawns User.Rate puffs a second.
    EU = "EmitterUpdateScript"
    put(EU, "EmitterState", "Scalability Mode", enum("/Niagara/Enums/ENiagaraEmitterScalabilityMode_Limited.ENiagaraEmitterScalabilityMode_Limited", "Self"))
    put(EU, "EmitterState", "Enable Distance Culling", b(True))
    put(EU, "EmitterState", "Module.bUseMaxDistance", b(True))
    put(EU, "EmitterState", "MaxDistance", f(200000.0))
    put(EU, "EmitterState", "Max Distance Response", enum("/Script/Niagara.ENiagaraExecutionStateManagement", "Sleep and Let Particles Finish"))
    spawn_rate = add_module(EU, "/Niagara/Modules/Emitter/SpawnRate.SpawnRate")
    put(EU, spawn_rate, "SpawnRate", link("User.Rate", FLOAT))

    # Particle spawn.
    PS = "ParticleSpawnScript"
    put(PS, "InitializeParticle", "Lifetime Mode", enum("/Niagara/Enums/ENiagara_LifetimeMode.ENiagara_LifetimeMode", "Random"))
    put(PS, "InitializeParticle", "Lifetime Min", f(3.0))
    put(PS, "InitializeParticle", "Lifetime Max", f(6.0))
    put(PS, "InitializeParticle", "Color Mode", enum("/Niagara/Enums/ENiagara_ColorInitializationMode.ENiagara_ColorInitializationMode", "Direct Set"))
    put(PS, "InitializeParticle", "Color", link("User.Color", COLOR))
    put(PS, "InitializeParticle", "Sprite Size Mode", enum("/Niagara/Enums/ENiagara_SizeScaleMode.ENiagara_SizeScaleMode", "Random Uniform"))
    put(PS, "InitializeParticle", "Uniform Sprite Size Min", hlsl("User.Size * 0.7"))
    put(PS, "InitializeParticle", "Uniform Sprite Size Max", hlsl("User.Size * 1.3"))
    put(PS, "InitializeParticle", "Sprite Rotation Mode", enum("/Niagara/Enums/ENiagara_SpriteRotationMode.ENiagara_SpriteRotationMode", "Random"))
    # The material's random (and the spin below) come from Particles.MaterialRandom.
    put(PS, "InitializeParticle", "Write Material Random", b(True))

    # Where: a cylinder of radius User.Radius about the component's origin, from the ground (its bottom at the origin)
    # to 60 cm up the herd's z.
    shape = add_module(PS, "/Niagara/Modules/Spawn/Location/V2/ShapeLocation.ShapeLocation")
    put(PS, shape, "Shape Primitive", enum("/Niagara/Enums/Location/ENiagara_LocationShapes.ENiagara_LocationShapes", "Cylinder"))
    put(PS, shape, "Cylinder Radius", link("User.Radius", FLOAT))
    put(PS, shape, "Cylinder Height", f(60.0))
    put(PS, shape, "Cylinder Height Midpoint", f(0.0))

    # Thrown with the herd: User.Drift (world) times a random share of a quarter to two fifths.
    drift = add_module(PS, "/Niagara/Modules/Spawn/Velocity/AddVelocity.AddVelocity")
    put(PS, drift, "Velocity", link("User.Drift", VEC3))
    put(PS, drift, "Rotation Coordinate Space", enum("/Script/Niagara.ENiagaraCoordinateSpace", "World"))
    put(PS, drift, "Velocity Speed Scale", dyn("/Niagara/DynamicInputs/UniformRange/V2/RandomRangeFloat.RandomRangeFloat"))
    put(PS, drift, ["Velocity Speed Scale", "Minimum"], f(0.25))
    put(PS, drift, ["Velocity Speed Scale", "Maximum"], f(0.42))
    # Carried with the ground: the world turns under the herd (and the ship and camera ride it), so each puff starts with
    # the ground's own velocity, User.Carry, in full.
    carry = add_module(PS, "/Niagara/Modules/Spawn/Velocity/AddVelocity.AddVelocity")
    put(PS, carry, "Velocity", link("User.Carry", VEC3))
    put(PS, carry, "Rotation Coordinate Space", enum("/Script/Niagara.ENiagaraCoordinateSpace", "World"))
    # And kicked up: a gentle rise and spread in the herd's own frame (local: z up from the ground).
    kick = add_module(PS, "/Niagara/Modules/Spawn/Velocity/AddVelocity.AddVelocity")
    put(PS, kick, "Velocity", dyn("/Niagara/DynamicInputs/UniformRange/V2/RandomRangeVector.RandomRangeVector"))
    put(PS, kick, ["Velocity", "Minimum"], vec(-45.0, -45.0, 15.0))
    put(PS, kick, ["Velocity", "Maximum"], vec(45.0, 45.0, 60.0))
    put(PS, kick, "Rotation Coordinate Space", enum("/Script/Niagara.ENiagaraCoordinateSpace", "Local"))
    # The herd's up at the puff's birth (the component's z in the world) into dynamic material parameter 0, which the
    # material lights the puff by.
    herd_up = add_module(PS, "/Niagara/Modules/Update/Material/DynamicMaterialParameters.DynamicMaterialParameters")
    put(PS, herd_up, "Write Parameter Index 0", enum("/Niagara/Enums/Utility/ENiagara_DynamicMaterialParametersInputMode.ENiagara_DynamicMaterialParametersInputMode", "Vector + Float"))
    put(PS, herd_up, "Index 0 Vector 1", link("Engine.Owner.SystemZAxis", VEC3))

    # Particle update. The minimal template's particle state is an older version of the module, which the stack flags
    # with a version upgrade note; it is replaced by the current one (the same defaults: age, and kill at the end of
    # the life), still first in the stack.
    PU = "ParticleUpdateScript"
    call(NS, "RemoveModule", {"moduleToRemove": at(PU, "ParticleState")})
    add_module(PU, "/Niagara/Modules/Update/Lifetime/ParticleState.ParticleState")
    # Alpha over life: the birth colour's alpha scaled by a curve, a linear fade in to 1 over the first 15%, then
    # easing down through 0.72 at 55% to 0 at the end.
    fade = add_module(PU, "/Niagara/Modules/Update/Color/ScaleColor.ScaleColor")
    put(PU, fade, "Scale Alpha", dyn("/Niagara/DynamicInputs/ValueFromCurve/FloatFromCurve.FloatFromCurve"))
    put(PU, fade, ["Scale Alpha", "FloatCurve"], curve([
        (0.0, 0.0, 0.0, 0.0, "RCIM_Linear"),
        (0.15, 1.0, 0.0, 0.0, "RCIM_Cubic"),
        (0.55, 0.72, -1.1, -1.1, "RCIM_Cubic"),
        (1.0, 0.0, -1.4, -1.4, "RCIM_Cubic")]))
    # Size over life: from its birth size to three times it, fast at first (1 + 3.2 t - 1.2 t^2).
    grow = add_module(PU, "/Niagara/Modules/Update/Size/ScaleSpriteSize.ScaleSpriteSize")
    put(PU, grow, "Scale Sprite Size Mode", enum("/Niagara/Enums/SpriteRenderer/ENiagara_ScaleSpriteSize.ENiagara_ScaleSpriteSize", "Uniform Curve"))
    put(PU, grow, "Uniform Curve Sprite Scale", curve([
        (0.0, 1.0, 3.2, 3.2, "RCIM_Cubic"),
        (1.0, 3.0, 0.8, 0.8, "RCIM_Cubic")]))
    # A slow spin, its rate and sense the puff's own: at most 15 degrees a second.
    spin = add_module(PU, "/Niagara/Modules/Update/Orientation/SpriteRotationRate.SpriteRotationRate")
    put(PU, spin, "Rotation Rate", hlsl("(Particles.MaterialRandom - 0.5) * 30.0"))
    # Drag against the ground, not against empty space: each frame a puff's velocity over the turning ground (its
    # velocity less User.Carry) decays at 0.7 a second, so it slows to rest on the ground it rose from.
    rv = call(NS, "AddSetParametersModule", {"moduleLocationRef": at(PU), "parameters": [
        {"variable": {"name": "Particles.Velocity", "type": {"classStructOrEnum": {"refPath": VEC3}}}}]})
    drag = rv["moduleName"] if isinstance(rv, dict) and "moduleName" in rv else rv
    put(PU, drag, "Particles.Velocity", hlsl("User.Carry + (Particles.Velocity - User.Carry) * exp(-0.7 * Engine.DeltaTime)"))
    curl = add_module(PU, "/Niagara/Modules/Update/Forces/V2/CurlNoiseForce.CurlNoiseForce")
    put(PU, curl, "Noise Strength", f(40.0))
    put(PU, curl, "Noise Frequency", f(10.0))
    add_module(PU, "/Niagara/Modules/Solvers/SolveForcesAndVelocity.SolveForcesAndVelocity")

    # The sprite: facing the camera, sorted back to front, no shadow.
    call(NS, "SetRendererData", {"renderer": at(renderer=0), "rendererData": {"propertyValues": json.dumps({
        "Material": material["refPath"], "Alignment": "Unaligned", "FacingMode": "FaceCamera",
        "SortMode": "ViewDepth", "bCastShadows": False})}})
    rd = json.loads(call(NS, "GetRendererData", {"rendererRef": at(renderer=0)})["propertyValues"])
    got = rd["Material"]["refPath"] if isinstance(rd.get("Material"), dict) else rd.get("Material")
    if got != material["refPath"]:
        raise RuntimeError(f"the sprite renderer's material is {got}")


# ----------------------------------------------------------------------------------------------------------------------
# Checks.

# Stack issues that are expected and why. None so far: any issue fails the build.
EXPLAINED = {}


def check():
    cs = call(NS, "GetSystemCompileState", {"system": SYS_REF})
    print(f"  compile state: {cs['aggregateStatus']} (errors {cs['bHasErrors']}, warnings {cs['bHasWarnings']})")
    for s in cs.get("scripts", []):
        for ev in s.get("compileEvents", []):
            print(f"    {s['emitterName']}/{s['scriptName']} {ev['severity']}: {ev['message'][:300]}")
    si = call(NS, "GetStackIssues", {"system": SYS_REF})
    print(f"  stack issues: {si['numErrors']} errors, {si['numWarnings']} warnings, {si['numInfos']} infos")
    bad = []
    for i in si.get("issues", []):
        loc = i.get("location", {})
        where = f"{loc.get('emitterName')}/{loc.get('scriptName')}/{loc.get('moduleName')}"
        why = EXPLAINED.get(i["shortDescription"])
        print(f"    {i['severity']} {where}: {i['shortDescription']} - {i['longDescription'][:300]}" + (f"  [expected: {why}]" if why else ""))
        if not why:
            bad.append(i)
    if cs["bHasErrors"] or cs["aggregateStatus"] not in ("UpToDate", "UpToDateWithWarnings") or bad:
        raise RuntimeError("NS_HerdDust does not compile cleanly")


def build():
    for name in (SYS, MAT):
        path = f"{FOLDER}/{name}"
        if T(AS, "exists", {"path": path}).get("returnValue"):
            if not T(AS, "delete", {"path": path}).get("returnValue"):
                raise RuntimeError(f"could not delete {path}")
    print("==", MAT)
    material = build_material()
    print("==", SYS)
    build_system(material)
    check()
    r = T(AS, "save_assets", {"asset_paths": [f"{FOLDER}/{MAT}", f"{FOLDER}/{SYS}"]})
    if not isinstance(r, dict) or not r.get("returnValue"):
        raise RuntimeError(f"saving failed: {json.dumps(r, default=str)[:600]}")
    print("  saved", r.get("returnValue"))


if __name__ == "__main__":
    build()
