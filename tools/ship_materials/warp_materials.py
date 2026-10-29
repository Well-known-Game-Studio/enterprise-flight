"""Builds M_WarpStreak (the classic view's streak cylinders) and M_PP_Warp (the warp post-process: the classic
radial streak blur, and the per-pixel relativistic view with aberration, Doppler shift and the synthesised sky).

Transwarp (warp factor above 9.9) has a look of its own in both, driven by the `Transwarp` 0..1 parameter the pawn
sets: the classic streaks turn to cycling rainbow colours and the radial blur splits into a prism with a rainbow
pinwheel round the vanishing point; the relativistic view pushes the beta clamp up (in C++), shifts the Doppler
colours past the visible band and adds the forward headlight of beamed light.

Run with the editor open and no play session: python3 tools/ship_materials/warp_materials.py [streak] [pp]
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
from uemat import *
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stars_material import STAR_FN

# ---- M_WarpStreak: additive unlit; a per-instance rainbow hue at transwarp, ends faded along the cylinder.
STREAK_CODE = """
float3 base = StreakColor * Intensity;
float hue = frac(Rnd + T * 0.25);
float3 rgb = saturate(abs(frac(hue + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);
float3 c = lerp(base, (rgb * 0.85 + 0.15) * Intensity * 1.3, Rainbow);
float endfade = smoothstep(0.0, 0.12, V) * (1.0 - smoothstep(0.88, 1.0, V));
return c * endfade;
"""


def streak():
    wipe("M_WarpStreak")
    g = G("M_WarpStreak", {"ShadingModel": "MSM_Unlit", "BlendMode": "BLEND_Additive", "TwoSided": True, "bUsedWithInstancedStaticMeshes": True})
    e = g.custom(STREAK_CODE, ["StreakColor", "Intensity", "Rainbow", "Rnd", "T", "V"], "CMOT_Float3", "streak colour")
    g.c(g.vec("StreakColor", (0.55, 0.75, 1.0)), e, "StreakColor")
    g.c(g.scalar("Intensity", 21.25), e, "Intensity")          # the old constants: (0.55, 0.75, 1) x 25 x 0.85
    g.c(g.scalar("Rainbow", 0.0), e, "Rainbow")
    g.c(g.node("PerInstanceRandom"), e, "Rnd")
    g.c(g.node("Time"), e, "T")
    g.c(mask(g, g.texcoord(0), gg=True), e, "V")
    g.out(e, "MP_EmissiveColor")
    g.finish(); return g


# ---- M_PP_Warp
# Classic: a radial zoom blur toward the vanishing point (away from it when travelling backwards), the hull masked
# out by depth. At transwarp the three channels sweep different fractions (a prism along every streak), the streaks
# take a rainbow hue by their bearing round the vanishing point, and a pulsing prismatic ring circles it.
CLASSIC_CODE = """
float2 fwd = ForwardUV.xy;
float2 d = UV - fwd;
float depthHere = SceneTextureLookup(Parameters, ClampSceneTextureUV(ViewportUVToSceneTextureUV(UV, 1), 1), 1, false).r;
float farHere = smoothstep(NearExclude, FarInclude, depthHere);
float sgn = 1.0 - 2.0 * Behind;
float amt = Blur * Strength * sgn * 0.5 * farHere * (1.0 + 0.6 * Transwarp);
float3 acc = float3(0.0, 0.0, 0.0);
float3 mx = float3(0.0, 0.0, 0.0);
float3 here = SceneTextureLookup(Parameters, ClampSceneTextureUV(ViewportUVToSceneTextureUV(UV, 14), 14), 14, false).rgb;
if (Strength <= 0.001 || farHere <= 0.001) { return here; }
const int taps = 8;
float split = 0.35 * Transwarp;
for (int i = 0; i < taps; ++i)
{
    float f = i / (float)taps;
    float3 s;
    s.r = SceneTextureLookup(Parameters, ClampSceneTextureUV(ViewportUVToSceneTextureUV(UV - d * amt * f * (1.0 - split), 14), 14), 14, false).r;
    s.g = SceneTextureLookup(Parameters, ClampSceneTextureUV(ViewportUVToSceneTextureUV(UV - d * amt * f, 14), 14), 14, false).g;
    s.b = SceneTextureLookup(Parameters, ClampSceneTextureUV(ViewportUVToSceneTextureUV(UV - d * amt * f * (1.0 + split), 14), 14), 14, false).b;
    acc += s;
    mx = max(mx, s);
}
float3 classic = lerp(acc / (float)taps, mx, MaxBlend);
if (Transwarp > 0.001)
{
    float aspect = ResolvedView.ViewSizeAndInvSize.x * ResolvedView.ViewSizeAndInvSize.w;
    float2 dd = d * float2(aspect, 1.0);
    float r = length(dd);
    float ang = atan2(dd.y, dd.x);
    float hue = frac(ang / 6.2831853 + T * 0.12);
    float3 rainbow = saturate(abs(frac(hue + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);
    // The streaks themselves take the hue of their bearing: a slowly turning pinwheel of colour.
    classic *= lerp(float3(1.0, 1.0, 1.0), rainbow * 1.4 + 0.35, 0.6 * Transwarp);
    // A prismatic ring sweeping out from the vanishing point.
    float ringR = frac(T * 0.45) * 0.9;
    float ring = exp(-pow((r - ringR) / 0.035, 2.0)) * (1.0 - ringR / 0.9);
    classic += rainbow * ring * 1.2 * Transwarp * farHere;
}
return classic;
"""

# Relativistic: everything beyond our own hull is taken from the frame where the aberrated line of sight lands,
# and synthesised (stars, the sun) where it lands outside the frame; then Doppler-tinted and beamed.
REL_CODE = """
float2 ndc = float2(UV.x * 2.0 - 1.0, 1.0 - UV.y * 2.0);
float2 th = float2(1.0 / ResolvedView.ViewToClip[0][0], 1.0 / ResolvedView.ViewToClip[1][1]);
float3 dp = normalize(float3(ndc * th, 1.0));            // line of sight of this pixel, view space (x right, y up, z fwd)
float3 here = SceneTextureLookup(Parameters, ClampSceneTextureUV(ViewportUVToSceneTextureUV(UV, 14), 14), 14, false).rgb;
float depthHere = SceneTextureLookup(Parameters, ClampSceneTextureUV(ViewportUVToSceneTextureUV(UV, 1), 1), 1, false).r;
float farHere = smoothstep(NearExclude, FarInclude, depthHere);   // 0 on the hull / bridge, 1 on the universe
float b = clamp(Beta, 0.0, 0.995);
if (farHere <= 0.001 || b <= 0.001) { return here; }
float3 a = normalize(TravelView);
float g = rsqrt(1.0 - b * b);
float cp = clamp(dot(dp, a), -1.0, 1.0);
float D = 1.0 / (g * (1.0 - b * cp));                     // Doppler factor along this line of sight
float c0 = (cp - b) / (1.0 - b * cp);                      // rest-frame direction the light actually came from
float3 perp = dp - a * cp;
float pl = length(perp);
perp = pl > 1e-5 ? perp / pl : float3(0.0, 0.0, 0.0);
float3 d0 = a * c0 + perp * sqrt(saturate(1.0 - c0 * c0));
d0 = normalize(lerp(dp, d0, Contract));
float3 col = float3(0.0, 0.0, 0.0);
float ok = 0.0;
if (d0.z > 0.02)
{
    float2 n2 = d0.xy / d0.z / th;
    float2 suv = float2(n2.x * 0.5 + 0.5, 0.5 - n2.y * 0.5);
    if (all(suv > 0.001) && all(suv < 0.999))
    {
        float2 px = 1.5 * View.ViewSizeAndInvSize.zw;
        float ds = SceneTextureLookup(Parameters, ClampSceneTextureUV(ViewportUVToSceneTextureUV(suv, 1), 1), 1, false).r;
        ds = min(ds, SceneTextureLookup(Parameters, ClampSceneTextureUV(ViewportUVToSceneTextureUV(suv + float2(px.x, 0.0), 1), 1), 1, false).r);
        ds = min(ds, SceneTextureLookup(Parameters, ClampSceneTextureUV(ViewportUVToSceneTextureUV(suv - float2(px.x, 0.0), 1), 1), 1, false).r);
        ds = min(ds, SceneTextureLookup(Parameters, ClampSceneTextureUV(ViewportUVToSceneTextureUV(suv + float2(0.0, px.y), 1), 1), 1, false).r);
        ds = min(ds, SceneTextureLookup(Parameters, ClampSceneTextureUV(ViewportUVToSceneTextureUV(suv - float2(0.0, px.y), 1), 1), 1, false).r);
        if (ds > FarInclude) { col = SceneTextureLookup(Parameters, ClampSceneTextureUV(ViewportUVToSceneTextureUV(suv, 14), 14), 14, false).rgb; ok = 1.0; }
    }
}
if (ok < 0.5)
{
    float3 d = normalize(CamRight * d0.x + CamUp * d0.y + CamFwd * d0.z);
""" + STAR_FN + """
    col = scol;
    float3 sv = normalize(SunView);
    float ang = acos(clamp(dot(d0, sv), -1.0, 1.0));
    float r = max(SunAngle, 1e-5);
    float rg = max(SunGlowAngle, r * 1.5);
    float disc = 1.0 - smoothstep(r * 0.92, r * 1.04, ang);
    float glow = exp(-ang / rg * 3.0) * (1.0 - disc);
    col += SunColor * disc + SunGlowColor * glow;
}
// Doppler: blue ahead, red behind. At transwarp the shift runs past the visible band: light ahead goes violet
// and fades to ultraviolet black at the very centre, light behind reddens to nothing.
float X = clamp(log2(D), -3.0, 3.0 + Transwarp) * Dop * (1.0 + 0.5 * Transwarp);
float3 tint = X > 0.0 ? lerp(float3(1,1,1), float3(0.5, 0.7, 2.0), min(X * 0.5, 1.0))
                      : lerp(float3(1,1,1), float3(2.0, 0.45, 0.2), min(-X * 0.5, 1.0));
float bright = X > 0.0 ? 1.0 + X * 1.2 : pow(2.0, X * 0.5);
if (Transwarp > 0.001)
{
    float beyondUV = saturate((X - 2.2) / 1.6) * Transwarp;        // past violet: into the ultraviolet, dark
    float beyondIR = saturate((-X - 1.6) / 1.2) * Transwarp;       // past red: into the infrared, dark
    tint = lerp(tint, float3(0.45, 0.2, 0.9), beyondUV);
    bright *= (1.0 - 0.85 * beyondUV) * (1.0 - 0.9 * beyondIR);
    // Beamed light: the forward headlight of the whole sky's radiation piled into a few degrees.
    float fwdAng = acos(cp);
    float halo = exp(-pow(fwdAng / (0.11 + 0.08 * (1.0 - b)), 2.0));
    float core = exp(-pow(fwdAng / 0.035, 2.0));
    col += (float3(0.6, 0.4, 1.0) * halo * 1.8 + float3(0.95, 0.9, 1.0) * core * 4.0) * Transwarp;
}
col *= tint * bright;
return lerp(here, col, farHere);
"""


def warp_pp():
    wipe("M_PP_Warp")
    g = G("M_PP_Warp", {"MaterialDomain": "MD_PostProcess", "BlendableLocation": "BL_SceneColorBeforeBloom", "BlendablePriority": 0})
    uv = g.node("TextureCoordinate")
    strength = g.scalar("Strength", 0.0); mode = g.scalar("Mode", 0.0); behind = g.scalar("Behind", 0.0)
    beta = g.scalar("Beta", 0.0); blur = g.scalar("BlurStrength", 0.6); contract = g.scalar("ContractStrength", 1.0); dop = g.scalar("DopplerStrength", 0.85); flash = g.scalar("Flash", 0.0)
    transwarp = g.scalar("Transwarp", 0.0)
    near = g.scalar("NearExcludeCm", 150000.0); far = g.scalar("FarIncludeCm", 400000.0)
    time_ = g.node("Time")
    cl = g.custom(CLASSIC_CODE, ["UV", "ForwardUV", "Strength", "Behind", "Blur", "MaxBlend", "NearExclude", "FarInclude", "Transwarp", "T"], "CMOT_Float3", "classic streak blur")
    g.c(uv, cl, "UV"); g.c(g.vec("ForwardUV", (0.5, 0.5, 0.0)), cl, "ForwardUV"); g.c(strength, cl, "Strength"); g.c(behind, cl, "Behind")
    g.c(blur, cl, "Blur"); g.c(g.scalar("StreakMaxBlend", 0.75), cl, "MaxBlend"); g.c(near, cl, "NearExclude"); g.c(far, cl, "FarInclude")
    g.c(transwarp, cl, "Transwarp"); g.c(time_, cl, "T")
    inputs = ["UV", "TravelView", "Beta", "Contract", "Dop", "NearExclude", "FarInclude", "SkyDepth", "SunView", "SunAngle", "SunGlowAngle", "SunColor", "SunGlowColor", "CamRight", "CamUp", "CamFwd", "Transwarp"]
    rel = g.custom(REL_CODE, inputs, "CMOT_Float3", "relativistic view")
    g.c(uv, rel, "UV")
    g.c(g.vec("TravelView", (0, 0, 1)), rel, "TravelView")
    g.c(beta, rel, "Beta"); g.c(contract, rel, "Contract"); g.c(dop, rel, "Dop")
    g.c(near, rel, "NearExclude"); g.c(far, rel, "FarInclude")
    g.c(g.scalar("SkyDepthCm", 2.5e11), rel, "SkyDepth")
    g.c(g.vec("SunView", (0, 0, 1)), rel, "SunView"); g.c(g.scalar("SunAngle", 0.0), rel, "SunAngle")
    g.c(g.scalar("SunGlowAngle", 0.0), rel, "SunGlowAngle")
    g.c(g.vec("SunColor", (80.0, 69.0, 50.0)), rel, "SunColor"); g.c(g.vec("SunGlowColor", (16.0, 11.5, 6.4)), rel, "SunGlowColor")
    g.c(g.vec("CamRight", (0, 1, 0)), rel, "CamRight"); g.c(g.vec("CamUp", (0, 0, 1)), rel, "CamUp"); g.c(g.vec("CamFwd", (1, 0, 0)), rel, "CamFwd")
    g.c(transwarp, rel, "Transwarp")
    out_ = g.lerp(cl, rel, mode)
    out_ = g.add_(out_, g.mul(g.vec("FlashColor", (0.8, 0.9, 1.4)), g.mul(flash, g.const(6.0))))
    # SceneTextureLookup only exists in the generated shader when a SceneTexture node is in the graph: keep the
    # colour and depth nodes connected at weight zero so the HLSL above can sample them.
    st = g.node("SceneTexture", {"SceneTextureId": "PPI_PostProcessInput0"}); sd = g.node("SceneTexture", {"SceneTextureId": "PPI_SceneDepth"})
    zero = g.mul(g.add_(mask(g, st, r=True, gg=True, b=True), mask(g, sd, r=True)), g.const(0.0))
    out_ = g.add_(out_, zero)
    g.out(out_, "MP_EmissiveColor")
    g.finish(); return g


if __name__ == "__main__":
    which = sys.argv[1:] or ["streak", "pp"]
    if "streak" in which: streak()
    if "pp" in which: warp_pp()
