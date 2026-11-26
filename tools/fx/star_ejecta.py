"""Builds the star's ejecta: NS_StarEjecta, the plasma a star throws off, and M_StarEjecta, the additive glow it is drawn
with. AStarBody keeps one component of the system at its centre (absolute scale, so its local space is in cm about the
centre) and sets its user parameters; this script is the only place the assets are authored.

The user parameters (the contract with the game's C++):
    User.Radius      float, cm: the star's radius
    User.WindRate    float, puffs a second of the steady wind (0 none)
    User.WindColor   LinearColor: the wind's glow (HDR: its brightness is in the colour), its alpha the opacity
    User.EjectaRate  float, puffs a second of an ejection (the star sets it for a second or two when one goes, 0 between)
    User.EjectaPos   Vector, cm about the centre: where on the surface the ejection leaves
    User.EjectaColor LinearColor: the ejection's glow, as above

Two CPU emitters in the system's local space:
  Wind:   puffs born on the surface (a random point of the sphere pushed out to 1.02 radii), leaning toward the poles
          (the open field of the coronal holes, where the fast wind leaves), flying straight out at 0.03 to 0.06 radii a
          second and growing as they go, fading in and out over 30 to 50 s: a faint stream about the star out to two
          or three radii.
  Ejecta: puffs born in a ball of a fifth of a radius about User.EjectaPos, flying out from the centre at 0.06 to 0.12
          radii a second and spreading from where they left, so the cloud expands as it goes; white-hot at first,
          reddening, gone in 40 to 70 s.
Each sleeps beyond 1.5e13 cm (a tenth of an AU) from the camera and lets its puffs finish.

Both are drawn along their flight (velocity-aligned sprites). M_StarEjecta is unlit and additive: the particle's colour
times its alpha times a soft oval mask, narrow across the flight (1.6 to 1), so a puff is a streak of glow with no
edge.

Run with the editor open and no play session, both assets deleted first (with the editor closed: see build()):
    python3 tools/fx/star_ejecta.py
It makes both assets, checks the system's compile state and stack issues, and saves both.
"""
import json, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
import mcp
import uemat
from uemat import G, T, AS, MT

FOLDER = "/Game/Slonk/FX"
MAT = "M_StarEjecta"
SYS = "NS_StarEjecta"
SYS_REF = {"refPath": f"{FOLDER}/{SYS}.{SYS}"}
NS = "NiagaraToolsets.NiagaraToolset_System"
NI = "NiagaraToolsets.NiagaraToolset_Info"

MAT_CODE = r"""
// (The sprite turns its length along its flight: narrow across it, a streak.)
float2 c = UV * 2.0 - 1.0;
float m = saturate(1.0 - length(c * float2(1.6, 1.0)));
m = m * m * (3.0 - 2.0 * m);
m *= m;
return Col * saturate(A) * m;
"""


def strict(ts, tool, args):
    r = mcp.unwrap(mcp.call_tool("call_tool", {"toolset_name": ts, "tool_name": tool, "arguments": args}))
    if not isinstance(r, dict) or r.get("isError") or "error" in r or r.get("returnValue") is False:
        raise RuntimeError(f"{tool} failed: {json.dumps(r, default=str)[:1200]}")
    return r


def build_material():
    uemat.MATERIAL_FOLDER = FOLDER
    global T
    uemat.T = strict
    T = strict
    T(MT, "create_material", {"folder_path": FOLDER, "asset_name": MAT})
    g = G(MAT, {"BlendMode": "BLEND_Additive", "ShadingModel": "MSM_Unlit", "TwoSided": True, "bUsedWithNiagaraSprites": True})
    pc = g.node("ParticleColor")
    e = g.custom(MAT_CODE, ["Col", "A", "UV"], "CMOT_Float3", "ejecta puff")
    g.c(pc, e, "Col")
    g.c(pc, e, "A", "A")
    g.c(g.texcoord(0), e, "UV")
    g.out(e, "MP_EmissiveColor")
    r = T(MT, "recompile", {"material_or_function": g.mat})
    if isinstance(r, dict) and r.get("isError"):
        raise RuntimeError("M_StarEjecta failed to compile")
    print("  material compiled")
    return g.mat


LINKED = "/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked"
HLSL = "/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression"
DYNAMIC = "/Script/NiagaraEditor.NiagaraExt_StackInputData_DynamicInput"
DATA_IF = "/Script/NiagaraEditor.NiagaraExt_StackInputData_DataInterface"
ENUM = "/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum"
FLOAT = "/Script/Niagara.NiagaraFloat"
BOOL = "/Script/Niagara.NiagaraBool"
VEC3 = "/Script/CoreUObject.Vector3f"
POS = "/Script/Niagara.NiagaraPosition"
COLOR = "/Script/CoreUObject.LinearColor"


def call(ts, tool, args):
    r = mcp.tool(ts, tool, args)
    if not isinstance(r, dict) or r.get("isError"):
        raise RuntimeError(f"{tool} failed: {json.dumps(r, default=str)[:1200]}")
    return r.get("returnValue")


def at(emitter, script="", module="", stack=(), renderer=-1):
    return {"system": SYS_REF, "emitterName": emitter, "scriptName": script, "moduleName": module,
            "rendererIndex": renderer, "inputNameStack": list(stack)}


def f(v): return (FLOAT, {"value": float(v)})
def b(v): return (BOOL, {"value": -1 if v else 0})
def link(name, typ): return (LINKED, {"linkedVariable": {"name": name, "type": {"classStructOrEnum": {"refPath": typ}}}})
def hlsl(expr): return (HLSL, {"hlslExpression": expr})
def dyn(asset): return (DYNAMIC, {"dynamicInputAsset": {"refPath": asset}})


_ENUMS = {}
def enum(path, display):
    if path not in _ENUMS:
        _ENUMS[path] = json.loads(call(NI, "UEnum_Info", {"enum": {"refPath": path}}))
    info = _ENUMS[path]
    for v in info["Values"]:
        if v["DisplayName"] == display:
            return (ENUM, {"enum": {"refPath": path}, "enumName": f"{info['Name']}::{v['Name']}", "displayName": display})
    raise KeyError(f"{display} not in {path}")


def curve(keys):
    ks = [{"interpMode": mode, "tangentMode": "RCTM_User", "tangentWeightMode": "RCTWM_WeightedNone", "time": t, "value": v,
           "arriveTangent": at_, "arriveTangentWeight": 0, "leaveTangent": lt, "leaveTangentWeight": 0}
          for t, v, at_, lt, mode in keys]
    return (DATA_IF, {"propertyValues": json.dumps({
        "Curve": {"keys": ks, "defaultValue": 3.4028234663852886e+38, "preInfinityExtrap": "RCCE_Constant", "postInfinityExtrap": "RCCE_Constant"},
        "bUseLUT": False, "bOptimizeLUT": False})})


def put(em, script, module, stack, value):
    if isinstance(stack, str):
        stack = [stack]
    call(NS, "SetStackInputData", {"stackInputRef": at(em, script, module, stack),
                                   "inputData": {"struct": {"refPath": value[0]}, "value": value[1]}})


def add_module(em, script, asset):
    return call(NS, "AddModule", {"moduleLocationRef": at(em, script), "moduleAsset": {"refPath": asset}})["moduleName"]


def set_params(em, script, pairs):
    """A Set Parameters module writing each (attribute, type, hlsl) in turn."""
    rv = call(NS, "AddSetParametersModule", {"moduleLocationRef": at(em, script), "parameters": [
        {"variable": {"name": n, "type": {"classStructOrEnum": {"refPath": t}}}} for n, t, _ in pairs]})
    mod = rv["moduleName"] if isinstance(rv, dict) and "moduleName" in rv else rv
    for n, t, expr in pairs:
        put(em, script, mod, n, hlsl(expr))
    return mod


def user_var(name, typ, value, desc):
    return {"name": name, "type": {"classStructOrEnum": {"refPath": typ}},
            "defaultValue": {"struct": {"refPath": typ}, "value": value}, "description": desc}


EU, PS, PU = "EmitterUpdateScript", "ParticleSpawnScript", "ParticleUpdateScript"


def emitter(name, material, rate_var, life_min, life_max, color_var, size_expr, place_exprs, alpha_keys, grow_keys):
    call(NS, "AddEmitter", {"system": SYS_REF, "emitterName": name,
                            "templateEmitter": {"refPath": "/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"}})
    call(NS, "SetEmitterData", {"emitter": at(name), "emitterData": {"propertyValues": json.dumps({
        "SimTarget": "CPUSim", "bLocalSpace": True, "CalculateBoundsMode": "Dynamic",
        "AllocationMode": "FixedCount", "PreAllocationCount": 1500})}})
    put(name, EU, "EmitterState", "Scalability Mode", enum("/Niagara/Enums/ENiagaraEmitterScalabilityMode_Limited.ENiagaraEmitterScalabilityMode_Limited", "Self"))
    put(name, EU, "EmitterState", "Enable Distance Culling", b(True))
    put(name, EU, "EmitterState", "Module.bUseMaxDistance", b(True))
    put(name, EU, "EmitterState", "MaxDistance", f(1.5e13))
    put(name, EU, "EmitterState", "Max Distance Response", enum("/Script/Niagara.ENiagaraExecutionStateManagement", "Sleep and Let Particles Finish"))
    sr = add_module(name, EU, "/Niagara/Modules/Emitter/SpawnRate.SpawnRate")
    put(name, EU, sr, "SpawnRate", link(rate_var, FLOAT))

    put(name, PS, "InitializeParticle", "Lifetime Mode", enum("/Niagara/Enums/ENiagara_LifetimeMode.ENiagara_LifetimeMode", "Random"))
    put(name, PS, "InitializeParticle", "Lifetime Min", f(life_min))
    put(name, PS, "InitializeParticle", "Lifetime Max", f(life_max))
    put(name, PS, "InitializeParticle", "Color Mode", enum("/Niagara/Enums/ENiagara_ColorInitializationMode.ENiagara_ColorInitializationMode", "Direct Set"))
    put(name, PS, "InitializeParticle", "Color", link(color_var, COLOR))
    put(name, PS, "InitializeParticle", "Sprite Size Mode", enum("/Niagara/Enums/ENiagara_SizeScaleMode.ENiagara_SizeScaleMode", "Random Uniform"))
    put(name, PS, "InitializeParticle", "Uniform Sprite Size Min", hlsl(size_expr[0]))
    put(name, PS, "InitializeParticle", "Uniform Sprite Size Max", hlsl(size_expr[1]))
    put(name, PS, "InitializeParticle", "Sprite Rotation Mode", enum("/Niagara/Enums/ENiagara_SpriteRotationMode.ENiagara_SpriteRotationMode", "Random"))
    put(name, PS, "InitializeParticle", "Write Material Random", b(True))
    # A random point of a unit ball, which the next module places and sends on its way.
    shape = add_module(name, PS, "/Niagara/Modules/Spawn/Location/V2/ShapeLocation.ShapeLocation")
    put(name, PS, shape, "Shape Primitive", enum("/Niagara/Enums/Location/ENiagara_LocationShapes.ENiagara_LocationShapes", "Sphere"))
    put(name, PS, shape, "Sphere Radius", f(1.0))
    set_params(name, PS, place_exprs)

    call(NS, "RemoveModule", {"moduleToRemove": at(name, PU, "ParticleState")})
    add_module(name, PU, "/Niagara/Modules/Update/Lifetime/ParticleState.ParticleState")
    fade = add_module(name, PU, "/Niagara/Modules/Update/Color/ScaleColor.ScaleColor")
    put(name, PU, fade, "Scale Alpha", dyn("/Niagara/DynamicInputs/ValueFromCurve/FloatFromCurve.FloatFromCurve"))
    put(name, PU, fade, ["Scale Alpha", "FloatCurve"], curve(alpha_keys))
    grow = add_module(name, PU, "/Niagara/Modules/Update/Size/ScaleSpriteSize.ScaleSpriteSize")
    put(name, PU, grow, "Scale Sprite Size Mode", enum("/Niagara/Enums/SpriteRenderer/ENiagara_ScaleSpriteSize.ENiagara_ScaleSpriteSize", "Uniform Curve"))
    put(name, PU, grow, "Uniform Curve Sprite Scale", curve(grow_keys))
    add_module(name, PU, "/Niagara/Modules/Solvers/SolveForcesAndVelocity.SolveForcesAndVelocity")

    call(NS, "SetRendererData", {"renderer": at(name, renderer=0), "rendererData": {"propertyValues": json.dumps({
        "Material": material["refPath"], "Alignment": "VelocityAligned", "FacingMode": "FaceCamera", "SortMode": "None", "bCastShadows": False})}})


def build_system(material):
    call(NS, "CreateNiagaraSystem", {"assetName": SYS, "assetPath": FOLDER,
                                     "templateSystem": {"refPath": "/Niagara/DefaultAssets/DefaultSystem.DefaultSystem"}})
    for em in call(NS, "GetSystemSummary", {"system": SYS_REF})["emitters"]:
        call(NS, "RemoveEmitter", {"emitterToRemove": at(em["emitterName"])})
    call(NS, "AddUserVariables", {"system": SYS_REF, "variablesToAdd": [
        user_var("User.Radius", FLOAT, {"value": 6.96e10}, "The star's radius, cm."),
        user_var("User.WindRate", FLOAT, {"value": 0.0}, "Wind puffs a second; 0 none."),
        user_var("User.WindColor", COLOR, {"r": 1.0, "g": 0.7, "b": 0.4, "a": 0.3}, "The wind's glow (HDR), alpha the opacity."),
        user_var("User.EjectaRate", FLOAT, {"value": 0.0}, "Ejection puffs a second while one goes; 0 between."),
        user_var("User.EjectaPos", VEC3, {"x": 6.96e10, "y": 0.0, "z": 0.0}, "Where on the surface it leaves, cm about the centre."),
        user_var("User.EjectaColor", COLOR, {"r": 1.0, "g": 0.85, "b": 0.7, "a": 0.25}, "The ejection's glow (HDR), alpha the opacity."),
    ]})
    rise = [(0.0, 0.0, 0.0, 0.0, "RCIM_Linear"), (0.12, 1.0, 0.0, 0.0, "RCIM_Cubic"), (0.6, 0.6, -1.0, -1.0, "RCIM_Cubic"), (1.0, 0.0, -1.2, -1.2, "RCIM_Cubic")]
    # The wind: from the surface, leaning poleward (the point of the ball squeezed toward the axis before it is put
    # on the surface), straight out.
    emitter("Wind", material, "User.WindRate", 30.0, 50.0, "User.WindColor",
            ("User.Radius * 0.04", "User.Radius * 0.08"),
            [("Particles.Position", POS, "normalize(Particles.Position * float3(0.55, 0.55, 1.6) + float3(0.0, 0.0, 0.0001)) * User.Radius * 1.02"),
             ("Particles.Velocity", VEC3, "normalize(Particles.Position) * User.Radius * (0.03 + 0.03 * Particles.MaterialRandom)")],
            rise, [(0.0, 1.0, 2.0, 2.0, "RCIM_Cubic"), (1.0, 3.0, 1.0, 1.0, "RCIM_Cubic")])
    # The ejection: a ball of a fifth of a radius about the point it leaves from, flying out from the centre and
    # spreading from the point.
    emitter("Ejecta", material, "User.EjectaRate", 40.0, 70.0, "User.EjectaColor",
            ("User.Radius * 0.12", "User.Radius * 0.28"),
            [("Particles.Velocity", VEC3, "normalize(User.EjectaPos) * User.Radius * (0.06 + 0.06 * Particles.MaterialRandom) + Particles.Position * User.Radius * 0.03"),
             ("Particles.Position", POS, "User.EjectaPos * 1.01 + Particles.Position * User.Radius * 0.2")],
            [(0.0, 0.0, 0.0, 0.0, "RCIM_Linear"), (0.05, 1.0, 0.0, 0.0, "RCIM_Cubic"), (0.5, 0.55, -1.0, -1.0, "RCIM_Cubic"), (1.0, 0.0, -1.0, -1.0, "RCIM_Cubic")],
            [(0.0, 1.0, 3.0, 3.0, "RCIM_Cubic"), (1.0, 5.0, 2.0, 2.0, "RCIM_Cubic")])
    # The ejection reddens as it cools.
    tint = add_module("Ejecta", PU, "/Niagara/Modules/Update/Color/ScaleColor.ScaleColor")
    put("Ejecta", PU, tint, "Scale RGB", hlsl("lerp(float3(1.0, 1.0, 1.0), float3(1.0, 0.35, 0.25), Particles.NormalizedAge)"))


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
        print(f"    {i['severity']} {loc.get('emitterName')}/{loc.get('scriptName')}/{loc.get('moduleName')}: {i['shortDescription']} - {i['longDescription'][:300]}")
        if i["shortDescription"] not in EXPLAINED:
            bad.append(i)
    if cs["bHasErrors"] or cs["aggregateStatus"] not in ("UpToDate", "UpToDateWithWarnings") or bad:
        raise RuntimeError("NS_StarEjecta does not compile cleanly")


def build():
    # (Deleting a Niagara system and making it again under the same name in one editor session leaves the old graph's
    # nodes about, and the new asset saves broken: the editor ensures on its pins, and loading the saved system crashes
    # the game. So the assets are deleted on disk with the editor closed, and this script only ever makes them new.)
    for name in (SYS, MAT):
        path = f"{FOLDER}/{name}"
        if T(AS, "exists", {"path": path}).get("returnValue"):
            raise RuntimeError(f"{path} exists: close the editor, delete unreal/Content/Slonk/FX/{name}.uasset, and run again")
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
