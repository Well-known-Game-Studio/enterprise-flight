"""Builds M_Hull (traffic ships and stations) and M_HullWindows (the additive overlay on the player's Enterprise):
plated hull colour with procedural lit windows whose on/off pattern drifts over time, each window strictly on or off.

Windows are placed by zones in the vessel's local space, set per vessel through ASpaceVessel::ApplyWindowParams:
a saucer disc, a hull box, a bridge ring, and two annulus zones (a ring of the wheel stations, or with major
radius 0 a vertical cylinder such as a station's shaft or pod) so every long section of a station carries deck
lights, not only the one the saucer zone covers.

Run with the editor open and no play session: python3 tools/ship_materials/hull_material.py
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
from uemat import *

# P, N: object-space position / normal (cm). The mesh->pawn transform (MeshScale, MeshRelLoc, AxisX/Y/Z) is identity
# for M_Hull; the Enterprise overlay uses it so zones can be given in pawn space. Returns (win*bright, win, h, 0).
WINDOW_FN = """
float3 Pp = MeshRelLoc + MeshScale * (AxisX * P.x + AxisY * P.y + AxisZ * P.z);
float3 Nn = normalize(AxisX * N.x + AxisY * N.y + AxisZ * N.z);
float sideHull = 1.0 - smoothstep(0.72, 0.92, abs(Nn.z));            // side-facing plating
float sideRim = 1.0 - smoothstep(0.90, 0.985, abs(Nn.z));            // a saucer rim is a bevel: only the flat plates are excluded
float3 d = Pp - SaucerCenter;
float r = length(d.xy);
float inS = (1.0 - smoothstep(SaucerRadius, SaucerRadius * 1.08, r)) * (1.0 - smoothstep(SaucerThickness * 1.1, SaucerThickness * 1.3, abs(d.z)));
float inH = (1.0 - smoothstep(HullMaxY * 0.9, HullMaxY, abs(Pp.y))) * (1.0 - smoothstep(HullMaxZ * 0.9, HullMaxZ, Pp.z))
          * smoothstep(HullMinZ, HullMinZ * 0.9, Pp.z) * step(HullMinX, Pp.x) * step(Pp.x, HullMaxX);
// Bridge module: a ring of small panes round the superstructure at the saucer's centre (BridgeRadius 0 = none).
float inBr = step(1.0, BridgeRadius) * (1.0 - smoothstep(BridgeRadius, BridgeRadius * 1.06, r)) * step(BridgeMinZ, Pp.z) * step(Pp.z, BridgeMaxZ);
inS *= 1.0 - inBr;
// Annulus zones about the vessel's own axis: RingA/B = (centre z, major radius, half width), RingAH/BH = half
// height. Major 0 is a vertical cylinder (a shaft, a column, a pod cluster); width 0 switches the zone off.
float rr = length(Pp.xy);
float inR1 = RingA.z > 0.0 ? (1.0 - smoothstep(RingA.z, RingA.z * 1.1, abs(rr - RingA.y))) * (1.0 - smoothstep(RingAH, RingAH * 1.1, abs(Pp.z - RingA.x))) : 0.0;
float inR2 = RingB.z > 0.0 ? (1.0 - smoothstep(RingB.z, RingB.z * 1.1, abs(rr - RingB.y))) * (1.0 - smoothstep(RingBH, RingBH * 1.1, abs(Pp.z - RingB.x))) : 0.0;
float inRing = saturate(inR1 + inR2) * (1.0 - saturate(inS + inBr));
float zone = saturate(inS + inH + inBr + inRing);
// Keep-out boxes (pawn space): the deflector housing and dish, and the torpedo bay. A box with Min > Max is off.
float inA = all(Pp > ExcludeAMin) && all(Pp < ExcludeAMax) ? 1.0 : 0.0;
float inB = all(Pp > ExcludeBMin) && all(Pp < ExcludeBMax) ? 1.0 : 0.0;
zone *= (1.0 - inA) * (1.0 - inB);
float side = max(max(sideHull * inH, sideRim * inS), max(sideHull * inBr, sideHull * inRing));
float arc = atan2(d.y, d.x) * lerp(SaucerRadius, BridgeRadius, inBr);   // arc length round the saucer (or the bridge)
float arcR = atan2(Pp.y, Pp.x) * max(rr, 1.0);                           // arc length round the vessel's axis
float u = lerp(lerp(Pp.x, arc, saturate(inS + inBr)), arcR, inRing);
// A saucer rim is a thin bevel: finer cells there so two rows of smaller panes fit, and a few more of them lit.
// The bridge is smaller again, with most of its panes lit.
float cu = u / lerp(lerp(Pitch, Pitch * 0.65, inS), Pitch * 0.45, inBr);
float cz = Pp.z / lerp(lerp(Deck, Deck * 0.7, inS), Deck * 0.6, inBr);
float2 cell = floor(float2(cu, cz));
float fu = frac(cu), fz = frac(cz);
// Panes PaneW of the pitch wide and PaneH of the deck tall: a quarter and a fifth on a ship, like the refit's;
// stations use wider, taller panes so their decks read as lit rows from kilometres away.
float hw = PaneW * 0.5, hz = PaneH * 0.5;
float wu = smoothstep(0.5 - hw - 0.05, 0.5 - hw, fu) * (1.0 - smoothstep(0.5 + hw, 0.5 + hw + 0.05, fu));
float wz = smoothstep(0.5 - hz - 0.05, 0.5 - hz, fz) * (1.0 - smoothstep(0.5 + hz, 0.5 + hz + 0.05, fz));
float h = frac(sin(dot(cell, float2(12.9898, 78.233))) * 43758.5453);          // this window's identity
// Each window keeps its state for a Period, then re-rolls; the roll times are staggered per window, so the
// pattern drifts across the hull instead of all switching at once. A window is either on or off.
float epoch = floor(T / max(Period, 1.0) + h * 7.0);
float h2 = frac(sin(dot(cell + epoch * 37.0 + 11.0, float2(26.651, 61.137))) * 24634.6345);
float lit = h2 < lerp(OnFraction, 0.85, inBr) ? 1.0 : 0.0;
float win = wu * wz * lit * side * zone;
float bright = 0.55 + 0.6 * h;
return float4(win * bright, win, h, 0.0);
"""
INPUTS = ["P", "N", "T", "MeshScale", "MeshRelLoc", "AxisX", "AxisY", "AxisZ", "SaucerCenter", "SaucerRadius", "SaucerThickness",
          "HullMaxY", "HullMaxZ", "HullMinZ", "HullMinX", "HullMaxX", "Pitch", "Deck", "OnFraction", "Period", "ExcludeAMin", "ExcludeAMax",
          "ExcludeBMin", "ExcludeBMax", "BridgeRadius", "BridgeMinZ", "BridgeMaxZ", "RingA", "RingB", "RingAH", "RingBH", "PaneW", "PaneH"]


def window_node(g, lp, nl):
    e = g.custom(WINDOW_FN, INPUTS, "CMOT_Float4", "procedural windows")
    g.c(lp, e, "P"); g.c(nl, e, "N"); g.c(g.node("Time"), e, "T")
    g.c(g.scalar("MeshScale", 1.0), e, "MeshScale"); g.c(g.vec("MeshRelLoc", (0, 0, 0)), e, "MeshRelLoc")
    g.c(g.vec("AxisX", (1, 0, 0)), e, "AxisX"); g.c(g.vec("AxisY", (0, 1, 0)), e, "AxisY"); g.c(g.vec("AxisZ", (0, 0, 1)), e, "AxisZ")
    g.c(g.vec("WindowSaucerCenter", (0, 0, 0)), e, "SaucerCenter"); g.c(g.scalar("WindowSaucerRadius", 0.0), e, "SaucerRadius")
    g.c(g.scalar("WindowSaucerThickness", 1.0), e, "SaucerThickness")
    g.c(g.scalar("WindowHullMaxY", 0.0), e, "HullMaxY"); g.c(g.scalar("WindowHullMaxZ", 0.0), e, "HullMaxZ")
    g.c(g.scalar("WindowHullMinZ", -1.0e9), e, "HullMinZ"); g.c(g.scalar("WindowHullMinX", -1.0e9), e, "HullMinX"); g.c(g.scalar("WindowHullMaxX", 1.0e9), e, "HullMaxX")
    g.c(g.scalar("WindowPitch", 600.0), e, "Pitch"); g.c(g.scalar("WindowDeck", 400.0), e, "Deck")
    g.c(g.scalar("WindowOnFraction", 0.5), e, "OnFraction"); g.c(g.scalar("WindowPeriod", 30.0), e, "Period")
    g.c(g.vec("WindowExcludeAMin", (1, 1, 1)), e, "ExcludeAMin"); g.c(g.vec("WindowExcludeAMax", (0, 0, 0)), e, "ExcludeAMax")
    g.c(g.vec("WindowExcludeBMin", (1, 1, 1)), e, "ExcludeBMin"); g.c(g.vec("WindowExcludeBMax", (0, 0, 0)), e, "ExcludeBMax")
    g.c(g.scalar("BridgeRadius", 0.0), e, "BridgeRadius"); g.c(g.scalar("BridgeMinZ", 0.0), e, "BridgeMinZ"); g.c(g.scalar("BridgeMaxZ", 0.0), e, "BridgeMaxZ")
    g.c(g.vec("WindowRingA", (0, 0, 0)), e, "RingA"); g.c(g.vec("WindowRingB", (0, 0, 0)), e, "RingB")
    g.c(g.scalar("WindowRingAHeight", 0.0), e, "RingAH"); g.c(g.scalar("WindowRingBHeight", 0.0), e, "RingBH")
    g.c(g.scalar("WindowPaneW", 0.2), e, "PaneW"); g.c(g.scalar("WindowPaneH", 0.12), e, "PaneH")
    return e


def object_pos_normal(g):
    lp0 = g.node("PreSkinnedPosition"); lp = g.node("VertexInterpolator"); g.c(lp0, lp, "VS")
    nw = g.node("VertexNormalWS"); nl = g.node("Transform", {"TransformSourceType": "TRANSFORMSOURCE_World", "TransformType": "TRANSFORM_Local"}); g.c(nw, nl, "")
    return lp, nl


def hull():
    wipe("M_Hull")
    g = G("M_Hull", {"ShadingModel": "MSM_DefaultLit", "BlendMode": "BLEND_Opaque", "bUsedWithInstancedStaticMeshes": True, "bTangentSpaceNormal": False})
    lp, nl = object_pos_normal(g)
    q = g.mul(lp, g.scalar("PanelScale", 0.01))
    vn = g.node("VectorNoise", {"NoiseFunction": "VNF_VoronoiALU", "Quality": 2}); g.c(g.mul(q, g.const(6.0)), vn, "World Position")
    cellpos = mask(g, vn, r=True, gg=True, b=True)
    hsh = g.un("Frac", g.mul(g.un("Sine", g.dot(cellpos, g.node("Constant3Vector", {"Constant": {"R": 12.9898, "G": 78.233, "B": 37.719, "A": 1}}))), g.const(43758.5453)))
    dm = g.node("ComponentMask", {"R": False, "G": False, "B": False, "A": True}); g.c(vn, dm, "")
    seam = g.smooth(g.const(0.0), g.const(0.06), dm)
    grime = g.noise(g.mul(q, g.const(20.0)), levels=3, levelscale=2.2, fn="NOISEFUNCTION_GradientALU")
    tint = g.add_(g.const(0.88), g.mul(hsh, g.const(0.24)))
    col = g.mul(g.mul(g.vec("HullColor", (0.62, 0.64, 0.68)), tint), g.add_(g.const(0.85), g.mul(grime, g.const(0.3))))
    col = g.mul(col, g.lerp(g.const(0.7), g.const(1.0), seam))
    w = window_node(g, lp, nl)
    wb = mask(g, w, r=True); win = mask(g, w, gg=True)
    em = g.mul(g.mul(g.vec("WindowColor", (1.0, 0.86, 0.62)), g.scalar("WindowGlow", 25.0)), wb)
    g.out(g.add_(col, g.mul(win, g.const(0.35))), "MP_BaseColor")
    g.out(em, "MP_EmissiveColor")
    g.out(g.add_(g.scalar("Roughness", 0.45), g.mul(grime, g.const(0.25))), "MP_Roughness")
    g.out(g.scalar("Metallic", 0.25), "MP_Metallic")
    g.out(g.scalar("Specular", 0.5), "MP_Specular")
    g.finish(); return g


# Emissive patches for the player's hull, in pawn space: colour (rgb) x intensity (a), shown on faces turned toward Dir.
PATCH_FN = """
float3 Pp = MeshRelLoc + MeshScale * (AxisX * P.x + AxisY * P.y + AxisZ * P.z);
float3 Nn = normalize(AxisX * N.x + AxisY * N.y + AxisZ * N.z);
float3 col = float3(0.0, 0.0, 0.0);
float inA = all(Pp > AMin) && all(Pp < AMax) ? 1.0 : 0.0;
col += AColor * AIntensity * inA * smoothstep(AFacing, AFacing + 0.2, dot(Nn, normalize(ADir)));
float inB = all(Pp > BMin) && all(Pp < BMax) ? 1.0 : 0.0;
col += BColor * BIntensity * inB * smoothstep(BFacing, BFacing + 0.2, dot(Nn, normalize(BDir)));
float inC = all(Pp > CMin) && all(Pp < CMax) ? 1.0 : 0.0;
col += CColor * CIntensity * inC * smoothstep(CFacing, CFacing + 0.2, dot(Nn, normalize(CDir)));
return col;
"""
PATCH_INPUTS = ["P", "N", "MeshScale", "MeshRelLoc", "AxisX", "AxisY", "AxisZ", "AMin", "AMax", "AColor", "AIntensity", "ADir", "AFacing",
                "BMin", "BMax", "BColor", "BIntensity", "BDir", "BFacing", "CMin", "CMax", "CColor", "CIntensity", "CDir", "CFacing"]


def patch_node(g, lp, nl):
    e = g.custom(PATCH_FN, PATCH_INPUTS, "CMOT_Float3", "emissive patches")
    g.c(lp, e, "P"); g.c(nl, e, "N")
    g.c(g.scalar("MeshScale", 1.0), e, "MeshScale"); g.c(g.vec("MeshRelLoc", (0, 0, 0)), e, "MeshRelLoc")
    g.c(g.vec("AxisX", (1, 0, 0)), e, "AxisX"); g.c(g.vec("AxisY", (0, 1, 0)), e, "AxisY"); g.c(g.vec("AxisZ", (0, 0, 1)), e, "AxisZ")
    for k in "ABC":
        g.c(g.vec(f"Patch{k}Min", (1, 1, 1)), e, f"{k}Min"); g.c(g.vec(f"Patch{k}Max", (0, 0, 0)), e, f"{k}Max")
        g.c(g.vec(f"Patch{k}Color", (1, 1, 1)), e, f"{k}Color"); g.c(g.scalar(f"Patch{k}Intensity", 0.0), e, f"{k}Intensity")
        g.c(g.vec(f"Patch{k}Dir", (1, 0, 0)), e, f"{k}Dir"); g.c(g.scalar(f"Patch{k}Facing", 0.0), e, f"{k}Facing")
    return e


def overlay():
    wipe("M_HullWindows")
    g = G("M_HullWindows", {"ShadingModel": "MSM_Unlit", "BlendMode": "BLEND_Additive", "TwoSided": False})
    lp, nl = object_pos_normal(g)
    w = window_node(g, lp, nl)
    em = g.mul(g.mul(g.vec("WindowColor", (1.0, 0.86, 0.62)), g.scalar("WindowGlow", 22.0)), mask(g, w, r=True))
    em = g.add_(em, patch_node(g, lp, nl))
    g.out(em, "MP_EmissiveColor")
    g.finish(); return g


if __name__ == "__main__":
    hull()
    overlay()
