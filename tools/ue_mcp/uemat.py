"""Material graph helpers over the editor's MCP material toolset (localhost:8010). Used by the builders in
tools/planet_materials; needs the Slonk editor open with the MCP plugin listening, and no play session running.

Usage from a builder:  from uemat import *   then   g = G("M_Name", {...}); ...; g.finish()
"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mcp

SC = "editor_toolset.toolsets.scene.SceneTools"
AC = "editor_toolset.toolsets.actor.ActorTools"
OB = "editor_toolset.toolsets.object.ObjectTools"
AS = "editor_toolset.toolsets.asset.AssetTools"
EA = "EditorToolset.EditorAppToolset"
MT = "editor_toolset.toolsets.material.MaterialTools"
MATERIAL_FOLDER = "/Game/Slonk/Materials"


def T(ts, tool, args):
    r = mcp.unwrap(mcp.call_tool("call_tool", {"toolset_name": ts, "tool_name": tool, "arguments": args}))
    if isinstance(r, dict) and (r.get("isError") or "error" in r):
        print("  !!", tool, json.dumps(r, default=str)[:600])
    return r


def setp(obj, values):
    return T(OB, "set_properties", {"instance": obj, "values": json.dumps(values)})


def getp(obj, names):
    r = T(OB, "get_properties", {"instance": obj, "properties": names})
    return json.loads(r["returnValue"]) if isinstance(r, dict) and "returnValue" in r else {}


def M(name):
    return {"refPath": f"{MATERIAL_FOLDER}/{name}.{name}"}


def add(mat, cls, x=0, y=0):
    r = T(MT, "add_expression", {"material_or_function": mat, "expression_class": {"refPath": "/Script/Engine." + cls}, "x": x, "y": y})
    return r["returnValue"] if isinstance(r, dict) and "returnValue" in r else r


def conn(a, aout, b, bin):
    return T(MT, "connect_expressions", {"from_expression": a, "from_output_name": aout, "to_expression": b, "to_input_name": bin})


def out(a, aout, prop):
    return T(MT, "connect_to_output", {"expression": a, "output_name": aout, "material_property": prop})


def finish(mat):
    print("  recompile", T(MT, "recompile", {"material_or_function": mat}))
    print("  save", T(AS, "save_assets", {"asset_paths": [mat["refPath"].split(".")[0]]}))


def wipe(name):
    """Create the material if missing, else delete every expression so the builder starts clean."""
    path = f"{MATERIAL_FOLDER}/{name}"
    if not T(AS, "exists", {"path": path}).get("returnValue"):
        T(MT, "create_material", {"folder_path": MATERIAL_FOLDER, "asset_name": name})
    else:
        m = M(name)
        for e in T(MT, "get_expressions", {"material_or_function": m}).get("returnValue", []):
            T(MT, "delete_expression", {"material_or_function": m, "expression": e})


def mask(g, src, r=False, gg=False, b=False, a=False):
    n = g.node("ComponentMask", {"R": r, "G": gg, "B": b, "A": a}); g.c(src, n, ""); return n


class G:
    """Tiny graph builder."""

    def __init__(self, name, props):
        self.mat = M(name); self.name = name; self.n = 0
        print("==", name, "props", setp(self.mat, props))

    def node(self, cls, props=None):
        self.n += 1
        e = add(self.mat, "MaterialExpression" + cls, -220 * (self.n % 8), 140 * (self.n // 8))
        if props:
            r = setp(e, props)
            if not (isinstance(r, dict) and r.get("returnValue")):
                print("  !! props", cls, props, r)
        return e

    def c(self, a, b, pin, aout=""):
        r = conn(a, aout, b, pin)
        if isinstance(r, dict) and r.get("isError"):
            print("  !! connect", a["refPath"].split(":")[-1], "->", b["refPath"].split(":")[-1], pin,
                  T(MT, "get_expression_input_names", {"expression": b}))
        return b

    def scalar(self, name, v, cpd=None):
        p = {"ParameterName": name, "DefaultValue": v}
        if cpd is not None:
            p.update({"bUseCustomPrimitiveData": True, "PrimitiveDataIndex": cpd})
        return self.node("ScalarParameter", p)

    def vec(self, name, rgb):
        return self.node("VectorParameter", {"ParameterName": name, "DefaultValue": {"R": rgb[0], "G": rgb[1], "B": rgb[2], "A": 1}})

    def const(self, v):
        return self.node("Constant", {"R": v})

    def texcoord(self, index):
        return self.node("TextureCoordinate", {"CoordinateIndex": index})

    def custom(self, code, inputs, outtype="CMOT_Float3", desc="", extra_outputs=()):
        """Custom HLSL node. `inputs` are pin names used in the code; `extra_outputs` are (name, type) pairs that the
        code assigns to, available as extra output pins. The Inputs array has to be grown with the exact struct
        shape first, then filled (a quirk of setting struct arrays over the MCP)."""
        e = self.node("Custom")
        def el(n): return {"inputName": n, "input": {"expression": "None", "outputIndex": 0, "inputName": "None", "mask": 0, "maskR": 0, "maskG": 0, "maskB": 0, "maskA": 0}}
        r = setp(e, {"Inputs": [el("None") for _ in inputs]})
        if not (isinstance(r, dict) and r.get("returnValue")): print("  !! grow", r)
        props = {"Inputs": [el(n) for n in inputs], "Code": code, "OutputType": outtype, "Description": desc}
        if extra_outputs:
            props["AdditionalOutputs"] = [{"outputName": n, "outputType": t} for n, t in extra_outputs]
        r = setp(e, props)
        if not (isinstance(r, dict) and r.get("returnValue")): print("  !! custom props", r)
        if extra_outputs:
            got = getp(e, ["AdditionalOutputs"]).get("AdditionalOutputs")
            print("  custom outputs:", json.dumps(got)[:300])
        return e

    def op2(self, cls, a, b, pa="A", pb="B"):
        n = self.node(cls); self.c(a, n, pa); self.c(b, n, pb); return n
    def mul(self, a, b): return self.op2("Multiply", a, b)
    def add_(self, a, b): return self.op2("Add", a, b)
    def sub(self, a, b): return self.op2("Subtract", a, b)
    def dot(self, a, b): return self.op2("DotProduct", a, b)
    def lerp(self, a, b, alpha):
        n = self.node("LinearInterpolate"); self.c(a, n, "A"); self.c(b, n, "B"); self.c(alpha, n, "Alpha"); return n
    def smooth(self, lo, hi, v):
        n = self.node("SmoothStep"); self.c(lo, n, "Min"); self.c(hi, n, "Max"); self.c(v, n, "Value"); return n
    def un(self, cls, a, pin=""):
        n = self.node(cls); self.c(a, n, pin); return n
    def noise(self, pos, levels=6, scale=1.0, lo=0.0, hi=1.0, fn="NOISEFUNCTION_SimplexTex", turb=False, levelscale=2.0):
        n = self.node("Noise", {"NoiseFunction": fn, "Levels": levels, "Scale": scale, "OutputMin": lo, "OutputMax": hi, "bTurbulence": turb, "LevelScale": levelscale, "Quality": 1})
        self.c(pos, n, "World Position"); return n

    def out(self, e, prop, aout=""):
        r = out(e, aout, prop); print("  out", prop, r)

    def finish(self):
        finish(self.mat)
