"""Check that a material builder still makes the material in the project, over the editor's MCP (localhost:8010).

A material edited in the editor (or by a one-off script) drifts from the builder in tools/planet_materials, and the
next run of the builder silently undoes the edit. This dumps a material graph as a description that two builds can
be compared by (nodes named by class and parameter name, description or index; their properties, code, pin wiring;
the material's outputs and settings) and compares two dumps.

  python matgraph.py dump <MaterialName> <out.json>
  python matgraph.py diff <a.json> <b.json>
  python matgraph.py check <builder.py> <MaterialName>    builds into <MaterialName>_Check, dumps both, diffs them

Run with the Slonk editor open and no play session. Each editor call takes about two seconds, so a dump of a graph
of fifty nodes takes a few minutes. `check` leaves the <MaterialName>_Check asset in Content/Slonk/Materials: delete
it (in the editor, or its .uasset with the editor closed).
"""
import importlib.util, json, os, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mcp
from uemat import M, MT, OB

# The properties worth comparing, by expression class (one call per node; a class not listed is compared by its wiring).
BYCLASS = {
    "MaterialExpressionScalarParameter": ["ParameterName", "DefaultValue", "bUseCustomPrimitiveData", "PrimitiveDataIndex"],
    "MaterialExpressionVectorParameter": ["ParameterName", "DefaultValue", "bUseCustomPrimitiveData", "PrimitiveDataIndex"],
    "MaterialExpressionTextureObjectParameter": ["ParameterName", "Texture", "SamplerType"],
    "MaterialExpressionCustom": ["Code", "OutputType", "Description", "Inputs", "AdditionalOutputs"],
    "MaterialExpressionTextureCoordinate": ["CoordinateIndex"],
    "MaterialExpressionConstant": ["R"],
    "MaterialExpressionConstant3Vector": ["Constant"],
    "MaterialExpressionComponentMask": ["R", "G", "B", "A"],
    "MaterialExpressionTransform": ["TransformSourceType", "TransformType"],
    "MaterialExpressionTransformPosition": ["TransformSourceType", "TransformType"],
    "MaterialExpressionPerInstanceCustomData": ["DataIndex", "ConstDefaultValue"],
    "MaterialExpressionPerInstanceCustomData3Vector": ["DataIndex", "ConstDefaultValue"],
}
MATPROPS = ["ShadingModel", "BlendMode", "TwoSided", "bTangentSpaceNormal", "OpacityMaskClipValue", "bUsedWithInstancedStaticMeshes",
            "bUsedWithStaticLighting", "MaterialDomain"]
OUTS = ["MP_BaseColor", "MP_Metallic", "MP_Specular", "MP_Roughness", "MP_EmissiveColor", "MP_Opacity", "MP_OpacityMask", "MP_Normal",
        "MP_WorldPositionOffset", "MP_SubsurfaceColor", "MP_AmbientOcclusion", "MP_PixelDepthOffset"]


def T(ts, tool, args):
    return mcp.unwrap(mcp.call_tool("call_tool", {"toolset_name": ts, "tool_name": tool, "arguments": args}))


def props(obj, names):
    """One call for all the names; if the class lacks one, one call each."""
    if not names:
        return {}
    r = T(OB, "get_properties", {"instance": obj, "properties": names})
    if isinstance(r, dict) and "returnValue" in r and not r.get("isError"):
        return json.loads(r["returnValue"])
    out = {}
    for n in names:
        r = T(OB, "get_properties", {"instance": obj, "properties": [n]})
        if isinstance(r, dict) and "returnValue" in r and not r.get("isError"):
            v = json.loads(r["returnValue"]).get(n)
            if v is not None:
                out[n] = v
    return out


def dump(name, path):
    mat = M(name)
    exprs = T(MT, "get_expressions", {"material_or_function": mat}).get("returnValue", [])
    info, ids = {}, {}
    for e in exprs:
        ref = e["refPath"]
        cls = ref.split(":")[-1].rsplit("_", 1)[0]
        p = props(e, BYCLASS.get(cls, []))
        key = cls
        for k in ("ParameterName", "Description"):
            if p.get(k):
                key += ":" + str(p[k])
                break
        else:
            for k in ("PrimitiveDataIndex", "DataIndex", "CoordinateIndex"):
                if k in p:
                    key += f":{k}={p[k]}"
        if p.get("Code"):
            key += ":" + p["Code"].strip().splitlines()[0][:60]
        base, n = key, 2
        while key in ids.values():
            key = f"{base}#{n}"
            n += 1
        ids[ref] = key
        info[ref] = p
    nodes = {}
    for e in exprs:
        ref = e["refPath"]
        wires = T(MT, "get_expression_inputs", {"material_or_function": mat, "expression": e}).get("returnValue", [])
        w = {}
        for i, src in enumerate(wires or []):
            ex = (src or {}).get("expression")
            if isinstance(ex, dict) and ex.get("refPath"):
                w[src.get("input_name") or f"#{i}"] = [ids.get(ex["refPath"], ex["refPath"]), src.get("output_name", "")]
        p = dict(info[ref])
        if "Code" in p:
            p["Code"] = p["Code"].replace("\r\n", "\n")
        if "Inputs" in p:
            # The pin names and masks; what feeds each pin is in "inputs", by node name.
            p["Inputs"] = [[i.get("inputName"), {m: i.get("input", {}).get(m) for m in ("mask", "maskR", "maskG", "maskB", "maskA", "outputIndex")}] for i in p["Inputs"]]
        nodes[ids[ref]] = {"props": p, "inputs": w}
    outs = {}
    for o in OUTS:
        r = T(MT, "get_property_input", {"material": mat, "material_property": o}).get("returnValue") or {}
        ex = r.get("expression")
        if isinstance(ex, dict) and ex.get("refPath"):
            outs[o] = [ids.get(ex["refPath"], ex["refPath"]), r.get("output_name", "")]
    data = {"material": props(mat, MATPROPS), "outputs": outs, "nodes": dict(sorted(nodes.items()))}
    json.dump(data, open(path, "w", encoding="utf-8"), indent=1, sort_keys=True)
    print(name, len(nodes), "nodes ->", path)


def diff(pa, pb):
    a, b = (json.load(open(f, encoding="utf-8")) for f in (pa, pb))
    n = 0
    for sect in ("material", "outputs"):
        if a[sect] != b[sect]:
            n += 1
            print("DIFF", sect, "\n  a:", a[sect], "\n  b:", b[sect])
    for k in sorted(set(a["nodes"]) | set(b["nodes"])):
        if k not in a["nodes"] or k not in b["nodes"]:
            n += 1
            print("ONLY IN", "a" if k in a["nodes"] else "b", k[:100])
            continue
        na, nb = a["nodes"][k], b["nodes"][k]
        for f in ("props", "inputs"):
            for q in sorted(set(na[f]) | set(nb[f])):
                if na[f].get(q) != nb[f].get(q):
                    n += 1
                    print("DIFF", k[:70], f, q, "\n  a:", str(na[f].get(q))[:300], "\n  b:", str(nb[f].get(q))[:300])
    print(n, "differences;", len(a["nodes"]), "and", len(b["nodes"]), "nodes")
    return n


def check(builder, name):
    spec = importlib.util.spec_from_file_location("builder", builder)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    mod.NAME = name + "_Check"
    mod.build()
    tmp = tempfile.gettempdir()
    pa, pb = os.path.join(tmp, f"{name}.asset.json"), os.path.join(tmp, f"{name}.built.json")
    dump(name, pa)
    dump(mod.NAME, pb)
    return diff(pa, pb)


if __name__ == "__main__":
    op = sys.argv[1]
    if op == "dump": dump(sys.argv[2], sys.argv[3])
    elif op == "diff": sys.exit(1 if diff(sys.argv[2], sys.argv[3]) else 0)
    elif op == "check": sys.exit(1 if check(sys.argv[2], sys.argv[3]) else 0)
    else: print(__doc__)
