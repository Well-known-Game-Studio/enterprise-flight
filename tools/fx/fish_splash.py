"""Builds the fish splash: NS_FishSplash, the one-shot burst of water a fish throws up where it leaves the sea or dives
back in, and M_FishSplash, the self-lit translucent material it is drawn with. The game spawns the system (a pooled
component) at the point on the sea's surface where the fish breaks it, its z the sea's up there, sets its user
parameters and lets it run: it bursts once and finishes by itself; this script is the only place the asset is authored.

The user parameters (the contract with the game's C++):
    User.Size    float, cm: the fish's length, typically 30-120; the splash scales with it (a 60 cm fish throws a crown
                 1-1.5 m high)
    User.Color   LinearColor: the foam's colour, from the world's sea and its solvent; its alpha the opacity
    User.Carry   Vector, world cm/s: the ground's own world velocity there from the planet's spin (0 while the spin is
                 held), given to every droplet in full and the velocity its drag is measured against, exactly as
                 NS_HerdDust's User.Carry
    User.Entry   float: 0 a fish leaving the water, 1 a fish diving back in (an entry throws a taller, narrower crown)
    User.Night   float 0..1: 1 at night, when the splash is dimmer

A one-shot: the system's state does not loop (once, 2.5 s, letting its emitters finish), and each emitter's life cycle
is its own, once, its burst at its birth, and complete when its last particle dies, so the whole system finishes by
itself within ~2 s and a pooled component can be activated again. Three CPU emitters, simulating in world space (a
droplet flies on where it was thrown while the world turns), each culling itself past 1.5 km from the camera (killed
after its particles finish, so a splash spawned out of range completes at once rather than sleeping forever):

  Droplets  25 to 60 beads of water (User.Size * 0.55, clamped), thrown up and out in a crown. Born on a ring of
            0.18 * User.Size about the point (on the surface), each is thrown in a hollow cone about the component's
            z, 14 to 32 degrees from it (an entry 5 to 16: narrower), at 0.62 to 1.04 times sqrt(2 g h), where h, the
            crown's top, is 2.1 * User.Size (an entry 3.0: taller; a 60 cm fish ~1.2 m, diving ~1.8 m), with a little
            scatter. Its life is its flight back down to the surface (twice its speed up the component's z over g),
            clamped to 0.6 to 2.2 s (a big entry's crown lands before its droplets go). Gravity 980 cm/s^2 down the component's -z; a light drag (its velocity over the
            turning ground, less User.Carry, decaying at 0.4 a second). A droplet starts 0.07 to 0.15 times User.Size,
            shrinks to 0.45 of that over its life and fades out over its last 40%.
  Foam      a ring of foam on the surface: 3 to 6 soft sprites (User.Size / 15, clamped) lying flat on the sea (facing
            the component's z, their own x along its x), 4 cm up it, scattered within 0.12 * User.Size of the point,
            1.3 to 1.9 times User.Size across, spreading to 2.4 times that (fast at first) and fading over 1.5 to 2 s.
            The material draws each as a ragged band whose middle opens as it ages.
  Spray     a soft mist puff or two (2), 0.8 to 1.2 times User.Size, born in the splash's lower third and thrown up at
            a quarter to half the droplets' speed, slowed by a strong drag against the ground (2.5 a second), growing
            to 2.3 times their size and fading over 1 to 1.6 s, at a quarter of User.Color's opacity.

All take User.Carry in full at birth (the world turns under the splash, and the ship and camera ride it) and measure
their drag against it; gravity acts along the component's -z, and since the ground's velocity is constant over the
splash's short life that is gravity relative to the moving ground too. Each particle's colour is User.Color at its
birth, its colour and opacity scaled by 1 - 0.6 * User.Night. The sea's up there (the component's z) goes to the
material as dynamic parameter 0's RGB and the particle's kind (0 droplet, 1 foam, 2 spray) as its fourth value. At
most 64 + 8 + 4 particles a splash (FixedCount).

M_FishSplash is translucent and unlit, and lights itself as M_HerdDust does: the sky atmosphere's sun (its way, and its
light where the particle is, through the air above it: SkyAtmosphereLightIlluminance at the pixel's own world
position, never the light 'on ground', which on these worlds is a permanent sunset) on a soft wrap round the particle
plus a quarter of it from the sky, as the luminance a surface of the particle's colour would have (light over pi), so
foam of a white colour reads white in the sun. A droplet is a round bead shaded as a ball (its normal from where the
pixel is on it, in the world, toward the camera in its middle) and glows a little when seen against the sun; foam is
a ragged ring lit as the flat sea is (the sea's up); spray is a soft puff shaded as a ball of mist. Its opacity is the
particle's alpha times the kind's mask times a depth fade (droplets 10 cm, foam 2 cm, spray 80 cm). The light falls
back to 1 from overhead only where there is no sky atmosphere's sun at all (the material's own preview), not where the
sun's light is merely nil (night), so a night splash is dark.

Run with the editor open and no play session:  python3 tools/fx/fish_splash.py
It deletes and recreates both assets, checks the system's compile state and stack issues, and saves both.
"""
import json, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
import mcp
import uemat
from uemat import G, T, AS, MT

FOLDER = "/Game/Slonk/FX"
MAT = "M_FishSplash"
SYS = "NS_FishSplash"
SYS_REF = {"refPath": f"{FOLDER}/{SYS}.{SYS}"}
NS = "NiagaraToolsets.NiagaraToolset_System"
NI = "NiagaraToolsets.NiagaraToolset_Info"

# ----------------------------------------------------------------------------------------------------------------------
# The material.

MAT_CODE = r"""
// The sprite's middle at 0, its edges at 1.
float2 c = UV * 2.0 - 1.0;
float r0 = length(c);
float ang = atan2(c.y, c.x);
float ph = Rnd * 6.2831853;
// The sea's up there (the emitter writes its frame's z into the dynamic parameter). Without it (the material's own
// preview; its default is 0) the card's normal.
float3 up = dot(Up, Up) > 0.01 ? normalize(Up) : Parameters.TangentToWorld[2];
// The pixel's offset from the particle's middle in the world, in sprite radii: in the sprite's own plane however the
// renderer turned it, so the shading needs no assumption about the sprite's tangent axes.
float3 w = Off / max(0.5 * max(Sz.x, Sz.y), 0.01);
float3 v = dot(V, V) > 1e-6 ? normalize(V) : up;
float m = 0.0;
float3 n = up;
Fade = 10.0;
if (Kind < 0.5)
{
    // A droplet: a bead of water, round, its rim soft over a fifth of its radius, shaded as a ball (its normal from
    // the pixel's place on it, toward the camera in its middle).
    m = saturate((1.0 - r0) * 4.0);
    m = m * m * (3.0 - 2.0 * m);
    float3 wp = w - dot(w, v) * v;
    n = normalize(wp + sqrt(saturate(1.0 - dot(wp, wp))) * v);
    Fade = 10.0;
}
else if (Kind < 1.5)
{
    // Foam: a ragged band of foam lying on the sea, its middle opening and the band thinning as it spreads (the
    // particle's normalized age), flecked with bubbles, and lit as the flat sea is.
    float rim = 1.0 + 0.09 * sin(ang * 5.0 + ph) + 0.06 * sin(ang * 9.0 - ph * 1.7);
    float r = r0 * 1.12 / rim;
    float mid = lerp(0.42, 0.7, Age);
    float hw = lerp(0.36, 0.17, Age);
    float band = saturate(1.0 - abs(r - mid) / hw);
    band = band * band * (3.0 - 2.0 * band);
    float fleck = 0.5 + 0.5 * sin(c.x * 11.0 + ph * 3.0) * sin(c.y * 10.3 - ph * 2.1);
    float fleck2 = 0.5 + 0.5 * sin((c.x + c.y) * 17.0 - ph) * sin((c.x - c.y) * 15.0 + ph * 1.3);
    m = band * lerp(0.55, 1.0, fleck) * lerp(0.75, 1.0, fleck2) * saturate((1.0 - r) * 6.0);
    n = up;
    Fade = 2.0;
}
else
{
    // Spray: a soft puff of mist, ragged at its rim, shaded as a ball of mist lit from above (the sea's up tilted
    // toward the puff's rims).
    float rim = 1.0 + 0.11 * sin(ang * 3.0 + ph) + 0.07 * sin(ang * 5.0 - ph * 2.3);
    float r = r0 * 1.18 / rim;
    m = saturate(1.0 - r);
    m = m * m * (3.0 - 2.0 * m);
    m *= m;
    n = normalize(up + w * 0.6);
    Fade = 80.0;
}
Opacity = saturate(A * m);
// Lit as the sea beside it is: by the sky atmosphere's sun (its way and its light where the particle is, the air's
// colour through it included), a soft wrap round it, and a quarter of it from the sky on the shaded side, as luminance
// a surface of the particle's colour would have (light over pi). Without a sky atmosphere's sun at all (no light
// direction: the material's preview) a light of 1 from overhead; a sun whose light is nil (night) leaves it dark.
bool sun = dot(L, L) > 1e-6;
float3 l = sun ? normalize(L) : up;
float3 e = sun ? E : float3(1.0, 1.0, 1.0);
float lit = saturate(dot(n, l) * 0.6 + 0.4);
float3 col = Col * e * (0.25 + 0.75 * lit) * 0.31830989;
// Water throws the sun forward: a droplet, or the mist, seen against the sun glows with it.
float fwd = pow(saturate(dot(-v, l)), 8.0) * (Kind < 0.5 ? 0.6 : (Kind > 1.5 ? 0.35 : 0.0));
return col + Col * e * fwd * 0.31830989;
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
    # Two-sided: the foam lies flat on the sea, facing its up, and must not be culled whichever way the renderer winds it.
    g = G(MAT, {"BlendMode": "BLEND_Translucent", "ShadingModel": "MSM_Unlit", "TwoSided": True,
                "bUsedWithNiagaraSprites": True})
    pc = g.node("ParticleColor")
    rnd = g.node("ParticleRandom")
    uv = g.texcoord(0)
    # Dynamic parameter 0: the sea's up in the world (its RGB) and the particle's kind (its fourth value: 0 droplet,
    # 1 foam, 2 spray), from the emitters. Its default 0, so without the emitter (the material's own preview) the
    # normal falls back to the card's and the sprite is drawn as a droplet.
    dyn = g.node("DynamicParameter", {"DefaultValue": {"R": 0.0, "G": 0.0, "B": 0.0, "A": 0.0}})
    # Its per-value outputs (Param1..Param4) can only be connected by name once its output names have been asked for
    # (until then connecting "Param4" fails; "RGB" works either way).
    T(MT, "get_expression_output_names", {"expression": dyn})
    age = g.node("ParticleRelativeTime")
    # The pixel's offset from the particle's middle, in the world (the difference of two large-world positions, small).
    wpos = g.node("WorldPosition")
    ppos = g.node("ParticlePositionWS")
    off = g.node("Subtract")
    g.c(wpos, off, "A", "XYZ")
    g.c(ppos, off, "B")
    size = g.node("ParticleSize")
    cam = g.node("CameraVectorWS")
    # The sky atmosphere's sun (light 0): its way, and its light where the particle is, through the air above that
    # point (the node's world position left to the pixel's own). Not the light 'on ground': that is the engine's one
    # global value, reckoned with the atmosphere's +Z as up, which on these worlds is a permanent sunset.
    sun_dir = g.node("SkyAtmosphereLightDirection")
    sun_lux = g.node("SkyAtmosphereLightIlluminance")
    e = g.custom(MAT_CODE, ["Col", "A", "UV", "Rnd", "Up", "Kind", "Age", "Off", "Sz", "V", "L", "E"], "CMOT_Float3",
                 "fish splash particle", extra_outputs=(("Opacity", "CMOT_Float1"), ("Fade", "CMOT_Float1")))
    # The particle colour node's main output is its RGB; the alpha is its own pin.
    g.c(pc, e, "Col")
    g.c(pc, e, "A", "A")
    g.c(uv, e, "UV")
    g.c(rnd, e, "Rnd")
    g.c(dyn, e, "Up", "RGB")
    g.c(dyn, e, "Kind", "Param4")
    g.c(age, e, "Age")
    g.c(off, e, "Off")
    g.c(size, e, "Sz")
    g.c(cam, e, "V")
    g.c(sun_dir, e, "L")
    g.c(sun_lux, e, "E")
    # Every pin wired, and to the output meant (the toolset's own report of a pin's output name is not to be trusted:
    # read the custom node's inputs themselves, their expressions and output indices).
    want = {"Col": (pc, 0), "A": (pc, 4), "UV": (uv, 0), "Rnd": (rnd, 0), "Up": (dyn, 4), "Kind": (dyn, 3), "Age": (age, 0),
            "Off": (off, 0), "Sz": (size, 0), "V": (cam, 0), "L": (sun_dir, 0), "E": (sun_lux, 0)}
    got = {i["inputName"]: i["input"] for i in uemat.getp(e, ["Inputs"])["Inputs"]}
    for pin, (src, index) in want.items():
        inp = got.get(pin, {})
        src_got = inp.get("expression", {}).get("refPath") if isinstance(inp.get("expression"), dict) else None
        if src_got != src["refPath"] or inp.get("outputIndex") != index:
            raise RuntimeError(f"the material's pin {pin} is wired to {src_got} output {inp.get('outputIndex')}, "
                               f"not {src['refPath']} output {index}")
    # The depth fade over the kind's own distance, so droplets and puffs do not cut into the sea and the foam, lying on
    # it, is not faded away.
    fade = g.node("DepthFade")
    g.c(e, fade, "Opacity", "Opacity")
    g.c(e, fade, "FadeDistance", "Fade")
    g.out(e, "MP_EmissiveColor")
    g.out(fade, "MP_Opacity")
    r = T(MT, "recompile", {"material_or_function": g.mat})
    if isinstance(r, dict) and r.get("isError"):
        raise RuntimeError("M_FishSplash failed to compile")
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
INT = "/Script/Niagara.NiagaraInt32"
BOOL = "/Script/Niagara.NiagaraBool"
VEC3 = "/Script/CoreUObject.Vector3f"
COLOR = "/Script/CoreUObject.LinearColor"

RANDOM_FLOAT = "/Niagara/DynamicInputs/UniformRange/V2/RandomRangeFloat.RandomRangeFloat"
RANDOM_VECTOR = "/Niagara/DynamicInputs/UniformRange/V2/RandomRangeVector.RandomRangeVector"
FLOAT_CURVE = "/Niagara/DynamicInputs/ValueFromCurve/FloatFromCurve.FloatFromCurve"

E_LIFE_CYCLE = "/Niagara/Enums/ENiagaraEmitterLifeCycleMode.ENiagaraEmitterLifeCycleMode"
E_INACTIVE = "/Niagara/Enums/ENiagaraInactiveMode.ENiagaraInactiveMode"
E_SYS_INACTIVE = "/Niagara/Enums/ENiagaraSystemInactiveMode.ENiagaraSystemInactiveMode"
E_LOOP = "/Niagara/Enums/ENiagara_EmitterStateOptions.ENiagara_EmitterStateOptions"
E_SCALABILITY = "/Niagara/Enums/ENiagaraEmitterScalabilityMode_Limited.ENiagaraEmitterScalabilityMode_Limited"
E_EXEC_STATE = "/Script/Niagara.ENiagaraExecutionStateManagement"
E_LIFETIME = "/Niagara/Enums/ENiagara_LifetimeMode.ENiagara_LifetimeMode"
E_COLOR_MODE = "/Niagara/Enums/ENiagara_ColorInitializationMode.ENiagara_ColorInitializationMode"
E_SIZE_MODE = "/Niagara/Enums/ENiagara_SizeScaleMode.ENiagara_SizeScaleMode"
E_ROTATION_MODE = "/Niagara/Enums/ENiagara_SpriteRotationMode.ENiagara_SpriteRotationMode"
E_SHAPE = "/Niagara/Enums/Location/ENiagara_LocationShapes.ENiagara_LocationShapes"
E_OFFSET_MODE = "/Niagara/Enums/Transforms/ENiagara_OffsetMode.ENiagara_OffsetMode"
E_VELOCITY_MODE = "/Niagara/Enums/Utility/ENiagara_VelocityMode.ENiagara_VelocityMode"
E_SPACE = "/Script/Niagara.ENiagaraCoordinateSpace"
E_DYN_PARAM = "/Niagara/Enums/Utility/ENiagara_DynamicMaterialParametersInputMode.ENiagara_DynamicMaterialParametersInputMode"
E_SCALE_SIZE = "/Niagara/Enums/SpriteRenderer/ENiagara_ScaleSpriteSize.ENiagara_ScaleSpriteSize"


def call(ts, tool, args):
    r = mcp.tool(ts, tool, args)
    if not isinstance(r, dict) or r.get("isError"):
        raise RuntimeError(f"{tool} failed: {json.dumps(r, default=str)[:1200]}")
    return r.get("returnValue")


def at(emitter, script="", module="", stack=(), renderer=-1):
    return {"system": SYS_REF, "emitterName": emitter, "scriptName": script, "moduleName": module,
            "rendererIndex": renderer, "inputNameStack": list(stack)}


def f(v): return (FLOAT, {"value": float(v)})
def i32(v): return (INT, {"value": int(v)})
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
    """A float curve data interface from (time, value, arrive tangent, leave tangent, interp) keys, as NS_HerdDust's:
    user tangents (auto tangents are only worked out by the curve editor), sampled directly rather than through a
    lookup table (which the toolset's copy never rebuilds)."""
    ks = [{"interpMode": mode, "tangentMode": "RCTM_User", "tangentWeightMode": "RCTWM_WeightedNone", "time": t, "value": v,
           "arriveTangent": at_, "arriveTangentWeight": 0, "leaveTangent": lt, "leaveTangentWeight": 0}
          for t, v, at_, lt, mode in keys]
    return (DATA_IF, {"propertyValues": json.dumps({
        "Curve": {"keys": ks, "defaultValue": 3.4028234663852886e+38, "preInfinityExtrap": "RCCE_Constant", "postInfinityExtrap": "RCCE_Constant"},
        "bUseLUT": False, "bOptimizeLUT": False})})


class Emitter:
    """One emitter of the system, by name: its stack inputs, modules and renderer."""

    def __init__(self, name):
        self.name = name

    def put(self, script, module, stack, value):
        if isinstance(stack, str):
            stack = [stack]
        call(NS, "SetStackInputData", {"stackInputRef": at(self.name, script, module, stack),
                                       "inputData": {"struct": {"refPath": value[0]}, "value": value[1]}})

    def add(self, script, asset):
        return call(NS, "AddModule", {"moduleLocationRef": at(self.name, script), "moduleAsset": {"refPath": asset}})["moduleName"]

    def set_params(self, script, params):
        """A SetParameters module writing each (variable, type, value) of params, in order."""
        rv = call(NS, "AddSetParametersModule", {"moduleLocationRef": at(self.name, script), "parameters": [
            {"variable": {"name": n, "type": {"classStructOrEnum": {"refPath": t}}}} for n, t, _ in params]})
        mod = rv["moduleName"] if isinstance(rv, dict) and "moduleName" in rv else rv
        for n, _, value in params:
            self.put(script, mod, n, value)
        return mod

    def renderer(self, material, props):
        call(NS, "SetRendererData", {"renderer": at(self.name, renderer=0), "rendererData": {"propertyValues": json.dumps(
            dict({"Material": material["refPath"], "SortMode": "ViewDepth", "bCastShadows": False}, **props))}})
        rd = json.loads(call(NS, "GetRendererData", {"rendererRef": at(self.name, renderer=0)})["propertyValues"])
        got = rd["Material"]["refPath"] if isinstance(rd.get("Material"), dict) else rd.get("Material")
        if got != material["refPath"]:
            raise RuntimeError(f"{self.name}'s sprite renderer's material is {got}")
        for k, v in props.items():
            if rd.get(k) != v:
                raise RuntimeError(f"{self.name}'s sprite renderer's {k} is {rd.get(k)}, not {v}")


def user_var(name, typ, value, desc):
    return {"name": name, "type": {"classStructOrEnum": {"refPath": typ}},
            "defaultValue": {"struct": {"refPath": typ}, "value": value}, "description": desc}


EU, PS, PU = "EmitterUpdateScript", "ParticleSpawnScript", "ParticleUpdateScript"

# The droplets' launch, shared by the spray: the speed up the sea's z that throws a droplet to the crown's top,
# sqrt(2 g h), h = 2.1 * User.Size for a fish leaving the water and 3.0 * User.Size for one diving back in.
V_UP = "sqrt(1960.0 * User.Size * lerp(2.1, 3.0, User.Entry))"
# The colour at birth: User.Color, dimmed at night (its colour and its opacity).
COLOR_NIGHT = "User.Color * (1.0 - 0.6 * User.Night)"


def new_emitter(name, max_particles, material, renderer_props):
    """A CPU, world-space emitter from the minimal template, with a fixed allocation that bounds its count; its life
    cycle its own: once, its burst at its birth, complete when its last particle dies; culled past 1.5 km (killed after
    its particles finish, so a splash spawned out of range completes at once); and the minimal template's particle
    state replaced by the current version (the stack flags the template's older one with a version upgrade note).
    Its sprite renderer is set up first, before any module, so every later change compiles the system with it (see
    build_system for the one compile the last emitter still needed)."""
    call(NS, "AddEmitter", {"system": SYS_REF, "emitterName": name,
                            "templateEmitter": {"refPath": "/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"}})
    em = Emitter(name)
    call(NS, "SetEmitterData", {"emitter": at(name), "emitterData": {"propertyValues": json.dumps({
        "SimTarget": "CPUSim", "bLocalSpace": False, "CalculateBoundsMode": "Dynamic",
        "AllocationMode": "FixedCount", "PreAllocationCount": max_particles})}})
    em.renderer(material, renderer_props)
    em.put(EU, "EmitterState", "Life Cycle Mode", enum(E_LIFE_CYCLE, "Self"))
    em.put(EU, "EmitterState", "Loop Behavior", enum(E_LOOP, "Once"))
    em.put(EU, "EmitterState", "Loop Duration", f(0.5))
    em.put(EU, "EmitterState", "Inactive Response", enum(E_INACTIVE, "Complete (Let Particles Finish then Kill Emitter)"))
    em.put(EU, "EmitterState", "Scalability Mode", enum(E_SCALABILITY, "Self"))
    em.put(EU, "EmitterState", "Enable Distance Culling", b(True))
    em.put(EU, "EmitterState", "Module.bUseMaxDistance", b(True))
    em.put(EU, "EmitterState", "MaxDistance", f(150000.0))
    em.put(EU, "EmitterState", "Max Distance Response", enum(E_EXEC_STATE, "Kill After Particles Finish"))
    call(NS, "RemoveModule", {"moduleToRemove": at(name, PU, "ParticleState")})
    em.add(PU, "/Niagara/Modules/Update/Lifetime/ParticleState.ParticleState")
    return em


def burst(em, count):
    """The emitter's one burst, at its birth: count particles (an int expression)."""
    m = em.add(EU, "/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous")
    em.put(EU, m, "Spawn Count", hlsl(count) if isinstance(count, str) else i32(count))
    em.put(EU, m, "Spawn Time", f(0.0))


def initialize(em, life, size_min, size_max, color=COLOR_NIGHT):
    """Its life (a fixed value or a (min, max) range), its colour at birth, a random uniform size between two
    expressions, a random rotation and the material random."""
    IP = "InitializeParticle"
    if isinstance(life, tuple):
        em.put(PS, IP, "Lifetime Mode", enum(E_LIFETIME, "Random"))
        em.put(PS, IP, "Lifetime Min", f(life[0]))
        em.put(PS, IP, "Lifetime Max", f(life[1]))
    else:
        em.put(PS, IP, "Lifetime Mode", enum(E_LIFETIME, "Direct Set"))
        em.put(PS, IP, "Lifetime", f(life))
    em.put(PS, IP, "Color Mode", enum(E_COLOR_MODE, "Direct Set"))
    em.put(PS, IP, "Color", hlsl(color))
    em.put(PS, IP, "Sprite Size Mode", enum(E_SIZE_MODE, "Random Uniform"))
    em.put(PS, IP, "Uniform Sprite Size Min", hlsl(size_min))
    em.put(PS, IP, "Uniform Sprite Size Max", hlsl(size_max))
    em.put(PS, IP, "Sprite Rotation Mode", enum(E_ROTATION_MODE, "Random"))
    em.put(PS, IP, "Write Material Random", b(True))


def carry(em):
    """The ground's own velocity, User.Carry, in full (in the world)."""
    m = em.add(PS, "/Niagara/Modules/Spawn/Velocity/AddVelocity.AddVelocity")
    em.put(PS, m, "Velocity", link("User.Carry", VEC3))
    em.put(PS, m, "Rotation Coordinate Space", enum(E_SPACE, "World"))


def kick(em, lo, hi):
    """A random velocity in the sea's own frame (local: z up from the sea), between two vector expressions."""
    m = em.add(PS, "/Niagara/Modules/Spawn/Velocity/AddVelocity.AddVelocity")
    em.put(PS, m, "Velocity", dyn(RANDOM_VECTOR))
    em.put(PS, m, ["Velocity", "Minimum"], hlsl(lo))
    em.put(PS, m, ["Velocity", "Maximum"], hlsl(hi))
    em.put(PS, m, "Rotation Coordinate Space", enum(E_SPACE, "Local"))


def material_params(em, kind):
    """The sea's up at the particle's birth (the component's z in the world) and its kind into dynamic material
    parameter 0, which the material lights it by and draws it as."""
    m = em.add(PS, "/Niagara/Modules/Update/Material/DynamicMaterialParameters.DynamicMaterialParameters")
    em.put(PS, m, "Write Parameter Index 0", enum(E_DYN_PARAM, "Vector + Float"))
    em.put(PS, m, "Index 0 Vector 1", link("Engine.Owner.SystemZAxis", VEC3))
    em.put(PS, m, "Module.Index0Float3Write", b(True))
    em.put(PS, m, "Index 0 Param 4", f(kind))


def fade_and_size(em, alpha_keys, size_keys):
    """Alpha over life (the birth colour's alpha scaled by a curve) and size over life (a uniform curve)."""
    m = em.add(PU, "/Niagara/Modules/Update/Color/ScaleColor.ScaleColor")
    em.put(PU, m, "Scale Alpha", dyn(FLOAT_CURVE))
    em.put(PU, m, ["Scale Alpha", "FloatCurve"], curve(alpha_keys))
    m = em.add(PU, "/Niagara/Modules/Update/Size/ScaleSpriteSize.ScaleSpriteSize")
    em.put(PU, m, "Scale Sprite Size Mode", enum(E_SCALE_SIZE, "Uniform Curve"))
    em.put(PU, m, "Uniform Curve Sprite Scale", curve(size_keys))


def spin(em, deg_per_s):
    """A slow spin, its rate and sense the particle's own: at most deg_per_s."""
    m = em.add(PU, "/Niagara/Modules/Update/Orientation/SpriteRotationRate.SpriteRotationRate")
    em.put(PU, m, "Rotation Rate", hlsl(f"(Particles.MaterialRandom - 0.5) * {2.0 * deg_per_s:.1f}"))


def drag(em, rate):
    """Drag against the ground, not against empty space: each frame a particle's velocity over the turning ground (its
    velocity less User.Carry) decays at rate a second."""
    em.set_params(PU, [("Particles.Velocity", VEC3, hlsl(
        f"User.Carry + (Particles.Velocity - User.Carry) * exp(-{rate} * Engine.DeltaTime)"))])


def solve(em):
    em.add(PU, "/Niagara/Modules/Solvers/SolveForcesAndVelocity.SolveForcesAndVelocity")


# ----------------------------------------------------------------------------------------------------------------------
# The system.

def build_droplets(material):
    em = new_emitter("Droplets", 64, material, {"Alignment": "Unaligned", "FacingMode": "FaceCamera", "SortOrderHint": 2})
    burst(em, "(int)clamp(User.Size * 0.55, 25.0, 60.0)")
    # The life is set below, from the throw; the size 0.07 to 0.15 of the fish's length.
    initialize(em, 1.0, "User.Size * 0.07", "User.Size * 0.15")
    # Born on a ring about the point, on the surface (most on its rim, some within it).
    shape = em.add(PS, "/Niagara/Modules/Spawn/Location/V2/ShapeLocation.ShapeLocation")
    em.put(PS, shape, "Shape Primitive", enum(E_SHAPE, "Ring / Disc"))
    em.put(PS, shape, "Ring Radius", hlsl("User.Size * 0.18"))
    em.put(PS, shape, "Disc Coverage", f(0.3))
    # Thrown up and out in a crown: in a hollow cone about the sea's up (local z), between 14 and 32 degrees from it
    # (an entry's narrower, 5 to 16), at 0.62 to 1.04 of the speed that throws a droplet to the crown's top.
    out = em.add(PS, "/Niagara/Modules/Spawn/Velocity/AddVelocity.AddVelocity")
    em.put(PS, out, "Velocity Mode", enum(E_VELOCITY_MODE, "In Cone"))
    em.put(PS, out, "Cone Axis", vec(0.0, 0.0, 1.0))
    em.put(PS, out, "Cone Angle", hlsl("lerp(32.0, 16.0, User.Entry)"))
    em.put(PS, out, "Inner Cone Angle", hlsl("lerp(14.0, 5.0, User.Entry)"))
    em.put(PS, out, "Rotation Coordinate Space", enum(E_SPACE, "Local"))
    # (In a cone the module has no speed scale: the speed itself is the random range.)
    em.put(PS, out, "Velocity Speed", dyn(RANDOM_FLOAT))
    em.put(PS, out, ["Velocity Speed", "Minimum"], hlsl(f"0.62 * {V_UP}"))
    em.put(PS, out, ["Velocity Speed", "Maximum"], hlsl(f"1.04 * {V_UP}"))
    # With a little scatter, so the crown is not a perfect cone.
    kick(em, f"float3(-0.05, -0.05, -0.03) * {V_UP}", f"float3(0.05, 0.05, 0.03) * {V_UP}")
    carry(em)
    # Its life: its flight back down to the surface (its speed up the sea's z over the ground, twice, over g).
    em.set_params(PS, [("Particles.Lifetime", FLOAT, hlsl(
        "clamp(2.0 * dot(Particles.Velocity - User.Carry, Engine.Owner.SystemZAxis) / 980.0, 0.6, 2.2)"))])
    material_params(em, 0)
    # Fading out over the last 40% of its life, shrinking to 0.45 of its size.
    fade_and_size(em, [(0.0, 1.0, 0.0, 0.0, "RCIM_Cubic"), (0.6, 1.0, 0.0, 0.0, "RCIM_Cubic"),
                       (1.0, 0.0, -2.5, -2.5, "RCIM_Cubic")],
                  [(0.0, 1.0, -0.2, -0.2, "RCIM_Cubic"), (1.0, 0.45, -0.9, -0.9, "RCIM_Cubic")])
    # Gravity down the sea's -z (local: the component's frame).
    g = em.add(PU, "/Niagara/Modules/Update/Forces/GravityForce.GravityForce")
    em.put(PU, g, "Gravity", vec(0.0, 0.0, -980.0))
    em.put(PU, g, "Coordinate Space", enum(E_SPACE, "Local"))
    drag(em, 0.4)
    solve(em)



def build_foam(material):
    em = new_emitter("Foam", 8, material, {"Alignment": "CustomAlignment", "FacingMode": "CustomFacingVector", "SortOrderHint": 0})
    burst(em, "(int)clamp(User.Size / 15.0, 3.0, 6.0)")
    initialize(em, (1.5, 2.0), "User.Size * 1.3", "User.Size * 1.9")
    # Scattered within 0.12 of the fish's length of the point, 4 cm up the sea's z so it lies on the sea, not in it.
    shape = em.add(PS, "/Niagara/Modules/Spawn/Location/V2/ShapeLocation.ShapeLocation")
    em.put(PS, shape, "Shape Primitive", enum(E_SHAPE, "Ring / Disc"))
    em.put(PS, shape, "Ring Radius", hlsl("User.Size * 0.12"))
    em.put(PS, shape, "Disc Coverage", f(1.0))
    em.put(PS, shape, "Offset Mode", enum(E_OFFSET_MODE, "Default"))
    em.put(PS, shape, "Offset", vec(0.0, 0.0, 4.0))
    em.put(PS, shape, "Offset Coordinate Space", enum(E_SPACE, "Local"))
    carry(em)
    # Lying flat on the sea: facing the sea's up, its own x along the component's x (its rotation turns it from there).
    em.set_params(PS, [("Particles.SpriteFacing", VEC3, link("Engine.Owner.SystemZAxis", VEC3)),
                       ("Particles.SpriteAlignment", VEC3, link("Engine.Owner.SystemXAxis", VEC3))])
    material_params(em, 1)
    # In quickly, holding, then fading; spreading to 2.4 times its size, fast at first.
    fade_and_size(em, [(0.0, 0.0, 0.0, 0.0, "RCIM_Linear"), (0.07, 1.0, 0.0, 0.0, "RCIM_Cubic"),
                       (0.45, 0.8, -0.8, -0.8, "RCIM_Cubic"), (1.0, 0.0, -1.6, -1.6, "RCIM_Cubic")],
                  [(0.0, 1.0, 3.0, 3.0, "RCIM_Cubic"), (1.0, 2.4, 0.4, 0.4, "RCIM_Cubic")])
    spin(em, 8.0)
    solve(em)



def build_spray(material):
    em = new_emitter("Spray", 4, material, {"Alignment": "Unaligned", "FacingMode": "FaceCamera", "SortOrderHint": 1})
    burst(em, 2)
    initialize(em, (1.0, 1.6), "User.Size * 0.8", "User.Size * 1.2",
               color="User.Color * float4(1.0, 1.0, 1.0, 0.25) * (1.0 - 0.6 * User.Night)")
    # Born in the splash's lower third, about the point.
    shape = em.add(PS, "/Niagara/Modules/Spawn/Location/V2/ShapeLocation.ShapeLocation")
    em.put(PS, shape, "Shape Primitive", enum(E_SHAPE, "Cylinder"))
    em.put(PS, shape, "Cylinder Radius", hlsl("User.Size * 0.15"))
    em.put(PS, shape, "Cylinder Height", hlsl("User.Size * 0.4"))
    em.put(PS, shape, "Cylinder Height Midpoint", f(0.0))
    # Thrown up at a quarter to half the droplets' speed, a little to the side.
    kick(em, f"float3(-0.12, -0.12, 0.25) * {V_UP}", f"float3(0.12, 0.12, 0.45) * {V_UP}")
    carry(em)
    material_params(em, 2)
    fade_and_size(em, [(0.0, 0.0, 0.0, 0.0, "RCIM_Linear"), (0.12, 1.0, 0.0, 0.0, "RCIM_Cubic"),
                       (0.5, 0.6, -1.1, -1.1, "RCIM_Cubic"), (1.0, 0.0, -0.8, -0.8, "RCIM_Cubic")],
                  [(0.0, 1.0, 2.6, 2.6, "RCIM_Cubic"), (1.0, 2.3, 0.5, 0.5, "RCIM_Cubic")])
    spin(em, 20.0)
    drag(em, 2.5)
    solve(em)



def build_system(material):
    call(NS, "CreateNiagaraSystem", {"assetName": SYS, "assetPath": FOLDER,
                                     "templateSystem": {"refPath": "/Niagara/DefaultAssets/DefaultSystem.DefaultSystem"}})
    # The template's own emitter goes; ours start from the minimal one (initialize particle, particle state, a sprite).
    for em in call(NS, "GetSystemSummary", {"system": SYS_REF})["emitters"]:
        call(NS, "RemoveEmitter", {"emitterToRemove": at(em["emitterName"])})

    call(NS, "AddUserVariables", {"system": SYS_REF, "variablesToAdd": [
        user_var("User.Size", FLOAT, {"value": 60.0}, "The fish's length, cm: the splash scales with it."),
        user_var("User.Color", COLOR, {"r": 0.85, "g": 0.9, "b": 0.95, "a": 0.9},
                 "The foam's colour, from the world's sea and its solvent; its alpha the opacity."),
        user_var("User.Carry", VEC3, {"x": 0.0, "y": 0.0, "z": 0.0},
                 "The ground's own world velocity there (the planet's spin), world cm/s: every particle takes it in full "
                 "at birth, and its drag is measured against it."),
        user_var("User.Entry", FLOAT, {"value": 0.0},
                 "0 a fish leaving the water, 1 a fish diving back in (a taller, narrower crown)."),
        user_var("User.Night", FLOAT, {"value": 0.0}, "0 by day, 1 at night: the splash is dimmer."),
    ]})

    build_foam(material)
    build_spray(material)
    build_droplets(material)

    # The system does not loop: once, for 2.5 s, then it lets its emitters finish (each completes by itself well before).
    # Set last, after the emitters: the system's compile after the last emitter's last change left that emitter drawing
    # nothing (the droplets, built last, never appeared in the editor, though the compile state was clean and there were
    # no stack issues) until the system compiled once more; these changes are that compile.
    SU = "SystemUpdateScript"
    put_system(SU, "SystemState", "Loop Behavior", enum(E_LOOP, "Once"))
    put_system(SU, "SystemState", "Loop Duration", f(2.5))
    put_system(SU, "SystemState", "Inactive Response", enum(E_SYS_INACTIVE, "Complete (Let Emitters Finish then Kill The System)"))


def put_system(script, module, stack, value):
    if isinstance(stack, str):
        stack = [stack]
    call(NS, "SetStackInputData", {"stackInputRef": at("", script, module, stack),
                                   "inputData": {"struct": {"refPath": value[0]}, "value": value[1]}})


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
        raise RuntimeError("NS_FishSplash does not compile cleanly")


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
