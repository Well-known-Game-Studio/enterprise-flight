"""Bake the tileable PBR texture sets the planet terrain material samples (cliff, talus, soil, scrub).

Runs headless in Blender (5.x):
    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python tools/planet_materials/bake_terrain_textures.py -- \
        --out <dir> [--size 2048] [--sets cliff,talus,soil,scrub]

Four ground sets plus the water (normal + foam only). Every set is a procedural Cycles material evaluated on a 4-D torus (u and v each map to a circle, so the noise
wraps seamlessly at the tile edge) and baked from a unit plane into two packed images:
    T_<Set>_BC.png   RGB base colour, sRGB (the terrain material tints it per biome / stratum); A height
    T_<Set>_NR.png   RG tangent-space normal xy (+Y is +V as Blender bakes it; the material rebuilds z and its
                     own frame), B ambient occlusion, A roughness (linear)
Import with tools/planet_materials/import_terrain_textures.py.
"""
import bpy, math, os, sys, subprocess

# ---------------------------------------------------------------------------------------------------------- args
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
def arg(name, default):
    if name in argv:
        return argv[argv.index(name) + 1]
    return default
OUT = arg("--out", os.path.join(os.getcwd(), "tex_out"))
SIZE = int(arg("--size", "2048"))
SETS = arg("--sets", "cliff,talus,soil,scrub,water").split(",")
os.makedirs(OUT, exist_ok=True)

TWO_PI = 2.0 * math.pi

# --------------------------------------------------------------------------------------------------------- scene
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = 1
scene.cycles.use_denoising = False
scene.render.bake.margin = 0
scene.render.bake.use_clear = True
scene.render.bake.use_selected_to_active = False
scene.view_settings.view_transform = "Standard"

bpy.ops.mesh.primitive_plane_add(size=1.0)
plane = bpy.context.active_object
plane.name = "BakePlane"


class Graph:
    """Small helper over a material node tree: sockets or python floats everywhere."""

    def __init__(self, mat):
        self.nt = mat.node_tree
        self.nt.nodes.clear()
        self.x = 0

    def node(self, kind, **props):
        n = self.nt.nodes.new(kind)
        for k, v in props.items():
            setattr(n, k, v)
        self.x += 1
        n.location = (self.x * 40 % 2000, (self.x * 40) // 2000 * -300)
        return n

    def link(self, src, dst):
        if isinstance(src, (int, float)):
            dst.default_value = src
        elif isinstance(src, tuple):
            dst.default_value = src if len(src) == len(dst.default_value) else (*src, 1.0)
        else:
            self.nt.links.new(src, dst)

    def math(self, op, a, b=None, c=None, clamp=False):
        n = self.node("ShaderNodeMath", operation=op, use_clamp=clamp)
        self.link(a, n.inputs[0])
        if b is not None:
            self.link(b, n.inputs[1])
        if c is not None:
            self.link(c, n.inputs[2])
        return n.outputs[0]

    def add(self, a, b): return self.math("ADD", a, b)
    def sub(self, a, b): return self.math("SUBTRACT", a, b)
    def mul(self, a, b): return self.math("MULTIPLY", a, b)
    def mad(self, a, b, c): return self.math("MULTIPLY_ADD", a, b, c)
    def smooth(self, lo, hi, x):
        # smoothstep via Map Range (smoothstep interpolation)
        n = self.node("ShaderNodeMapRange", interpolation_type="SMOOTHSTEP", clamp=True)
        self.link(x, n.inputs["Value"]); self.link(lo, n.inputs["From Min"]); self.link(hi, n.inputs["From Max"])
        return n.outputs["Result"]
    def remap(self, x, lo, hi, a, b):
        n = self.node("ShaderNodeMapRange", clamp=True)
        self.link(x, n.inputs["Value"]); self.link(lo, n.inputs["From Min"]); self.link(hi, n.inputs["From Max"])
        self.link(a, n.inputs["To Min"]); self.link(b, n.inputs["To Max"])
        return n.outputs["Result"]
    def mix(self, fac, a, b):
        n = self.node("ShaderNodeMix", data_type="RGBA", clamp_factor=True)
        self.link(fac, n.inputs["Factor"]); self.link(a, n.inputs[6]); self.link(b, n.inputs[7])
        return n.outputs[2]
    def mixf(self, fac, a, b):
        n = self.node("ShaderNodeMix", data_type="FLOAT", clamp_factor=True)
        self.link(fac, n.inputs["Factor"]); self.link(a, n.inputs[2]); self.link(b, n.inputs[3])
        return n.outputs[0]
    def scale_rgb(self, col, s):
        n = self.node("ShaderNodeMix", data_type="RGBA", blend_type="MULTIPLY", clamp_factor=True)
        self.link(1.0, n.inputs["Factor"]); self.link(col, n.inputs[6])
        if isinstance(s, (int, float)):
            self.link((s, s, s, 1.0), n.inputs[7])
        elif isinstance(s, tuple):
            self.link(s, n.inputs[7])
        else:
            # scalar socket as grey
            c = self.node("ShaderNodeCombineColor")
            self.link(s, c.inputs[0]); self.link(s, c.inputs[1]); self.link(s, c.inputs[2])
            self.link(c.outputs[0], n.inputs[7])
        return n.outputs[2]
    def combine(self, r, g, b):
        c = self.node("ShaderNodeCombineColor")
        self.link(r, c.inputs[0]); self.link(g, c.inputs[1]); self.link(b, c.inputs[2])
        return c.outputs[0]

    # ---- the tileable domain: (u, v) -> point on a 4-D torus. Period 1 in noise units along each circle, so a
    # noise "Scale" of S puts about S features across the tile. ru/rv < 1 squash the circle (anisotropic noise);
    # ou/ov offset the angle so two lookups decorrelate.
    def torus(self, ru=1.0, rv=1.0, ou=0.0, ov=0.0):
        uv = self.node("ShaderNodeUVMap").outputs["UV"]
        sep = self.node("ShaderNodeSeparateXYZ"); self.link(uv, sep.inputs[0])
        au = self.mad(sep.outputs[0], TWO_PI, ou * TWO_PI)
        av = self.mad(sep.outputs[1], TWO_PI, ov * TWO_PI)
        k = 1.0 / TWO_PI
        x = self.mul(self.math("COSINE", au), ru * k)
        y = self.mul(self.math("SINE", au), ru * k)
        z = self.mul(self.math("COSINE", av), rv * k)
        w = self.mul(self.math("SINE", av), rv * k)
        comb = self.node("ShaderNodeCombineXYZ")
        self.link(x, comb.inputs[0]); self.link(y, comb.inputs[1]); self.link(z, comb.inputs[2])
        return comb.outputs[0], w

    def noise(self, scale, detail=4.0, rough=0.5, ru=1.0, rv=1.0, ou=0.0, ov=0.0, distortion=0.0):
        vec, w = self.torus(ru, rv, ou, ov)
        n = self.node("ShaderNodeTexNoise", noise_dimensions="4D", normalize=True)
        self.link(vec, n.inputs["Vector"]); self.link(w, n.inputs["W"])
        self.link(scale, n.inputs["Scale"]); self.link(detail, n.inputs["Detail"]); self.link(rough, n.inputs["Roughness"])
        self.link(distortion, n.inputs["Distortion"])
        return n.outputs["Factor"]

    def voronoi(self, scale, feature="F1", randomness=1.0, ru=1.0, rv=1.0, ou=0.0, ov=0.0, distance="EUCLIDEAN", exponent=2.0, normalize=True):
        """Returns (Distance, Color) sockets; Color is a per-cell random colour (a hash)."""
        vec, w = self.torus(ru, rv, ou, ov)
        n = self.node("ShaderNodeTexVoronoi", voronoi_dimensions="4D", feature=feature, distance=distance, normalize=normalize)
        self.link(vec, n.inputs["Vector"]); self.link(w, n.inputs["W"])
        self.link(scale, n.inputs["Scale"]); self.link(randomness, n.inputs["Randomness"])
        if "Exponent" in n.inputs: self.link(exponent, n.inputs["Exponent"])
        col = n.outputs.get("Color")
        if col is None:  # DISTANCE_TO_EDGE has no Color: take the cell hash from an F1 lookup of the same cells
            f1 = self.node("ShaderNodeTexVoronoi", voronoi_dimensions="4D", feature="F1", distance=distance, normalize=True)
            self.link(vec, f1.inputs["Vector"]); self.link(w, f1.inputs["W"])
            self.link(scale, f1.inputs["Scale"]); self.link(randomness, f1.inputs["Randomness"])
            if "Exponent" in f1.inputs: self.link(exponent, f1.inputs["Exponent"])
            col = f1.outputs["Color"]
        return n.outputs["Distance"], col

    def channel(self, col, i):
        s = self.node("ShaderNodeSeparateColor"); self.link(col, s.inputs[0])
        return s.outputs[i]


# ---------------------------------------------------------------------------------------------------- the sets
# Each builder returns dict(height=socket 0..1, albedo=colour socket, rough=socket, ao=socket, bump_distance)

def build_cliff(g):
    """Bedded sedimentary rock seen face-on: beds run along u (horizontal on a wall), joints cut across them."""
    # beds: mostly a function of v (one slow noise around the v circle) bent a little along u; 9 or so per tile
    bedv = g.noise(9.0, detail=1.0, rough=0.5, ru=0.0, rv=1.0)
    bend = g.noise(3.0, detail=2.0, rough=0.5, ou=0.4)
    beds = g.add(bedv, g.mul(g.sub(bend, 0.5), 0.25))
    bedstep = g.smooth(0.45, 0.55, beds)                      # recessed (soft) vs standing (hard) beds
    # laminae: fine horizontal lines within each bed
    lam = g.noise(60.0, detail=1.0, rough=0.5, ru=0.0, rv=1.0, ov=0.3)
    lam = g.mul(g.smooth(0.55, 0.7, lam), 0.08)
    # bed boundary seam
    seam = g.sub(1.0, g.mul(g.math("ABSOLUTE", g.sub(g.smooth(0.46, 0.54, beds), 0.5)), 2.0))
    seam = g.smooth(0.85, 1.0, seam)
    # joints: tall boxy blocks (Chebychev cells squashed along v), a thin crack on every edge
    jd, jc = g.voronoi(7.0, feature="DISTANCE_TO_EDGE", ru=1.0, rv=0.3, distance="CHEBYCHEV", randomness=0.7)
    joint = g.smooth(0.0, 0.02, jd)                           # 0 in the crack
    blockvar = g.channel(jc, 0)                               # per-block tone
    # weathering: fine grain, pits, and a patchy dark varnish
    grain = g.noise(50.0, detail=6.0, rough=0.6, ou=0.3)
    pits = g.smooth(0.72, 0.85, g.noise(28.0, detail=2.0, ou=0.7, ov=0.2))
    varnish = g.smooth(0.45, 0.75, g.noise(10.0, detail=3.0, rough=0.6, ou=0.15, ov=0.6))
    h = g.mad(bedstep, 0.45, 0.35)
    h = g.add(h, lam)
    h = g.add(h, g.mul(grain, 0.1))
    h = g.sub(h, g.mul(pits, 0.12))
    h = g.add(h, g.mul(g.sub(blockvar, 0.5), 0.1))
    h = g.mul(h, g.mad(joint, 0.5, 0.5))
    h = g.math("MULTIPLY", h, 1.0, clamp=True)
    tone = g.mad(bedstep, 0.3, 0.6)
    tone = g.add(tone, g.mul(g.sub(grain, 0.5), 0.2))
    tone = g.add(tone, g.mul(g.sub(blockvar, 0.5), 0.15))
    tone = g.sub(tone, g.mul(lam, 1.5))
    tone = g.mixf(seam, tone, g.mul(tone, 0.45))
    tone = g.mixf(pits, tone, g.mul(tone, 0.6))
    tone = g.mixf(varnish, tone, g.mul(tone, 0.7))
    tone = g.mixf(joint, 0.2, tone)
    albedo = g.scale_rgb((0.80, 0.74, 0.68, 1.0), tone)
    rough = g.mad(g.sub(1.0, h), 0.12, 0.82)
    ao = g.mad(h, 0.45, 0.55)
    ao = g.mul(ao, g.mad(joint, 0.5, 0.5))
    return dict(height=h, albedo=albedo, rough=rough, ao=ao, bump=0.05)


def build_talus(g):
    """Angular rock fragments lying on a slope, two sizes, sand and grit between them."""
    def fragments(scale, ou, radius):
        # raw (unnormalised) distances: the cell spacing is 1/scale, the fragment radius a fraction of it
        cd, cc = g.voronoi(scale, feature="F1", distance="MINKOWSKI", ou=ou, randomness=1.0, exponent=3.0, normalize=False)
        ed, _ = g.voronoi(scale, feature="DISTANCE_TO_EDGE", distance="MINKOWSKI", ou=ou, randomness=1.0, exponent=3.0, normalize=False)
        # distances come out in cell units (the cell spacing is about 1)
        size = g.mad(g.channel(cc, 0), radius * 0.5, radius * 0.75)      # per-fragment size
        rock = g.sub(1.0, g.smooth(g.mul(size, 0.8), size, cd))           # 1 on a fragment, 0 beyond its edge
        rock = g.mul(rock, g.smooth(0.0, 0.03, ed))                       # and a hairline between touching ones
        tx = g.sub(g.channel(cc, 1), 0.5)
        dome = g.sub(1.0, g.math("POWER", g.math("DIVIDE", cd, size), 1.6))  # convex top
        dome = g.math("MULTIPLY", dome, 1.0, clamp=True)
        facet = g.noise(scale * 3.0, detail=1.0, rough=0.4, ou=ou + 0.5)
        frag = g.mad(dome, 0.6, 0.3)
        frag = g.add(frag, g.mul(cd, g.mul(tx, 1.5)))                      # tilt
        frag = g.add(frag, g.mul(g.sub(facet, 0.5), 0.3))
        frag = g.math("MULTIPLY", frag, 1.0, clamp=True)
        return rock, frag, cc
    big, bigh, bigc = fragments(5.0, 0.0, 0.7)
    small, smallh, smallc = fragments(15.0, 0.37, 0.6)
    grain = g.noise(80.0, detail=5.0, rough=0.6, ou=0.1, ov=0.6)
    h = g.mad(grain, 0.12, 0.04)
    h = g.mixf(small, h, g.mad(smallh, 0.4, 0.15))
    h = g.mixf(big, h, g.mad(bigh, 0.6, 0.35))
    h = g.add(h, g.mul(grain, 0.04))
    h = g.math("MULTIPLY", h, 1.0, clamp=True)
    def rockcol(cc, base):
        tone = g.mad(g.channel(cc, 2), 0.35, 0.7)                     # mild per-fragment tone: from a distance
        tone = g.add(tone, g.mul(g.sub(grain, 0.5), 0.35))            # strong variation reads as leopard spots
        return g.scale_rgb(base, tone)
    bigcol = rockcol(bigc, (0.52, 0.46, 0.41, 1.0))
    smallcol = rockcol(smallc, (0.46, 0.42, 0.38, 1.0))
    gritcol = g.scale_rgb((0.42, 0.34, 0.26, 1.0), g.mad(grain, 0.5, 0.5))
    albedo = g.mix(small, gritcol, smallcol)
    albedo = g.mix(big, albedo, bigcol)
    rough = g.mixf(big, 0.95, g.mad(g.sub(1.0, grain), 0.1, 0.8))
    ao = g.mad(h, 0.7, 0.3)
    return dict(height=h, albedo=albedo, rough=rough, ao=ao, bump=0.12)


def build_soil(g):
    """Dry sandy ground with ripples and scattered pebbles."""
    grain = g.noise(40.0, detail=8.0, rough=0.65)
    broad = g.noise(12.0, detail=3.0, rough=0.5, ou=0.4)      # finer than the tile: the hashed cells keep one mean
    # sand ripples: a sine along v, bent by a slow noise
    uv = g.node("ShaderNodeUVMap").outputs["UV"]
    sep = g.node("ShaderNodeSeparateXYZ"); g.link(uv, sep.inputs[0])
    warp = g.noise(3.0, detail=2.0, rough=0.5, ou=0.8)
    phase = g.mad(sep.outputs[1], TWO_PI * 26.0, g.mul(g.sub(warp, 0.5), 6.0))
    ripple = g.mad(g.math("SINE", phase), 0.5, 0.5)
    ripple = g.mul(ripple, g.smooth(0.3, 0.7, g.noise(8.0, detail=2.0, rough=0.5, ou=0.55)))  # ripples in patches
    # pebbles: a fraction of voronoi cells carry a rounded stone
    pd, pc = g.voronoi(18.0, feature="F1", ou=0.6)
    gate = g.smooth(0.6, 0.64, g.channel(pc, 0))
    size = g.mad(g.channel(pc, 1), 0.14, 0.12)
    peb = g.mul(g.sub(1.0, g.smooth(g.mul(size, 0.6), size, pd)), gate)
    pebh = g.mul(peb, peb)                                    # domed
    h = g.mad(grain, 0.3, 0.35)
    h = g.add(h, g.mul(g.sub(broad, 0.5), 0.12))
    h = g.add(h, g.mul(ripple, 0.03))
    h = g.mixf(peb, h, g.mad(pebh, 0.45, 0.45))
    h = g.math("MULTIPLY", h, 1.0, clamp=True)
    sandtone = g.mad(grain, 0.3, 0.75)
    sandtone = g.add(sandtone, g.mul(g.sub(broad, 0.5), 0.2))
    sandtone = g.add(sandtone, g.mul(g.sub(ripple, 0.5), 0.05))
    sandcol = g.scale_rgb((0.78, 0.66, 0.50, 1.0), sandtone)
    pebcol = g.scale_rgb((0.45, 0.40, 0.36, 1.0), g.mad(g.channel(pc, 2), 0.7, 0.5))
    albedo = g.mix(peb, sandcol, pebcol)
    rough = g.mixf(peb, 0.95, 0.8)
    ao = g.mad(h, 0.25, 0.75)
    return dict(height=h, albedo=albedo, rough=rough, ao=ao, bump=0.03)


def build_scrub(g):
    """Dry grass tufts and low scrub over dark soil, for the wet ground along the rivers."""
    grain = g.noise(50.0, detail=6.0, rough=0.6)
    broad = g.noise(11.0, detail=3.0, rough=0.5, ou=0.5)
    td, tc = g.voronoi(13.0, feature="F1", ou=0.3)
    gate = g.smooth(0.3, 0.4, g.channel(tc, 0))
    edge = g.noise(60.0, detail=3.0, rough=0.7, ou=0.9)
    tuft = g.sub(1.0, g.smooth(0.12, 0.34, g.add(td, g.mul(g.sub(edge, 0.5), 0.2))))
    tuft = g.mul(tuft, gate)
    blade = g.noise(120.0, detail=2.0, rough=0.5, ru=0.25, ov=0.4)  # streaky blades
    h = g.mad(grain, 0.2, 0.25)
    h = g.add(h, g.mul(g.sub(broad, 0.5), 0.15))
    h = g.add(h, g.mul(tuft, g.mad(blade, 0.4, 0.4)))
    h = g.math("MULTIPLY", h, 1.0, clamp=True)
    soilcol = g.scale_rgb((0.36, 0.29, 0.20, 1.0), g.mad(grain, 0.5, 0.6))
    grasstone = g.mad(g.channel(tc, 1), 0.5, 0.6)
    grasstone = g.add(grasstone, g.mul(g.sub(blade, 0.5), 0.4))
    dry = g.scale_rgb((0.55, 0.50, 0.26, 1.0), grasstone)
    green = g.scale_rgb((0.30, 0.40, 0.14, 1.0), grasstone)
    grasscol = g.mix(g.channel(tc, 2), dry, green)
    albedo = g.mix(tuft, soilcol, grasscol)
    rough = g.mixf(tuft, 0.9, 0.75)
    ao = g.mad(h, 0.4, 0.6)
    return dict(height=h, albedo=albedo, rough=rough, ao=ao, bump=0.04)


def build_water(g):
    """Wind waves for the sea surface: a few directional trains with integer periods across the tile (so they
    wrap), choppy and bent by noise, plus a foam mask on the crests. Only the normal is used."""
    uv = g.node("ShaderNodeUVMap").outputs["UV"]
    sep = g.node("ShaderNodeSeparateXYZ"); g.link(uv, sep.inputs[0])
    u, v = sep.outputs[0], sep.outputs[1]
    warp = g.noise(3.0, detail=2.0, rough=0.5, ou=0.2)
    h = None
    # (cycles along u, cycles along v, amplitude, phase)
    for cu, cv, amp, ph in ((7, 3, 0.30, 0.0), (4, -6, 0.22, 1.1), (11, 5, 0.12, 2.3), (-9, 8, 0.10, 0.7), (16, -3, 0.06, 3.0), (5, 14, 0.05, 1.9)):
        phase = g.add(g.add(g.mul(u, TWO_PI * cu), g.mul(v, TWO_PI * cv)), ph)
        phase = g.add(phase, g.mul(g.sub(warp, 0.5), 2.5))
        # a sharpened crest: (1 - |sin|)^1.5 style, so the trains are not pure sines
        crest = g.math("POWER", g.mad(g.math("SINE", phase), 0.5, 0.5), 1.6)
        term = g.mul(crest, amp)
        h = term if h is None else g.add(h, term)
    ripple = g.noise(40.0, detail=5.0, rough=0.6, ou=0.7)
    h = g.add(h, g.mul(g.sub(ripple, 0.5), 0.1))
    h = g.math("MULTIPLY", h, 1.0, clamp=True)
    foam = g.smooth(0.62, 0.8, g.add(h, g.mul(g.sub(g.noise(25.0, detail=3.0, rough=0.5, ou=0.4), 0.5), 0.2)))
    albedo = g.scale_rgb((0.5, 0.5, 0.5, 1.0), 1.0)
    return dict(height=h, albedo=albedo, rough=foam, ao=1.0, bump=0.06)


BUILDERS = dict(cliff=build_cliff, talus=build_talus, soil=build_soil, scrub=build_scrub, water=build_water)


# --------------------------------------------------------------------------------------------------------- bake
def bake(mat, g, image, kind, colour_socket=None, normal_socket=None):
    out = g.node("ShaderNodeOutputMaterial")
    if kind == "EMIT":
        em = g.node("ShaderNodeEmission")
        g.link(colour_socket, em.inputs["Color"]); g.link(1.0, em.inputs["Strength"])
        g.link(em.outputs[0], out.inputs["Surface"])
    else:
        bsdf = g.node("ShaderNodeBsdfPrincipled")
        g.link(normal_socket, bsdf.inputs["Normal"])
        g.link(bsdf.outputs[0], out.inputs["Surface"])
    g.nt.nodes.active = out
    tex = g.node("ShaderNodeTexImage"); tex.image = image
    g.nt.nodes.active = tex
    bpy.context.view_layer.objects.active = plane
    plane.select_set(True)
    if kind == "EMIT":
        bpy.ops.object.bake(type="EMIT", margin=0, use_clear=True)
    else:
        bpy.ops.object.bake(type="NORMAL", normal_space="TANGENT", margin=0, use_clear=True)
    g.nt.links.remove(out.inputs["Surface"].links[0])


def new_image(name, data):
    img = bpy.data.images.new(name, SIZE, SIZE, alpha=True, float_buffer=True)
    img.colorspace_settings.name = "Non-Color" if data else "sRGB"
    return img


def save(img, path):
    img.filepath_raw = path
    img.file_format = "PNG"
    bpy.context.scene.render.image_settings.color_depth = "8"
    img.save()


for name in SETS:
    builder = BUILDERS[name]
    mat = bpy.data.materials.new("M_" + name)
    mat.use_nodes = True
    plane.data.materials.clear()
    plane.data.materials.append(mat)
    g = Graph(mat)
    s = builder(g)
    Set = name.capitalize()
    parts = {}
    # base colour
    img = new_image("BC", data=False)
    bake(mat, g, img, "EMIT", colour_socket=s["albedo"])
    save(img, os.path.join(OUT, f"_{Set}_bc.png")); parts["bc"] = img
    # ao, roughness, height (linear)
    img = new_image("ORM", data=True)
    bake(mat, g, img, "EMIT", colour_socket=g.combine(s["ao"], s["rough"], s["height"]))
    save(img, os.path.join(OUT, f"_{Set}_orm.png")); parts["orm"] = img
    # normal from the height through a bump node
    bump = g.node("ShaderNodeBump")
    g.link(s["height"], bump.inputs["Height"]); g.link(1.0, bump.inputs["Strength"]); g.link(s["bump"], bump.inputs["Distance"])
    img = new_image("N", data=True)
    bake(mat, g, img, "NORMAL", normal_socket=bump.outputs["Normal"])
    save(img, os.path.join(OUT, f"_{Set}_n.png")); parts["n"] = img
    # pack with ImageMagick from the saved parts (Blender's own save converts colour spaces on the way out, so
    # reading its buffers back double-encodes): BC = colour + height, NR = normal xy + ao + roughness
    o = os.path.join(OUT, "")
    def chan(src, c, dst):
        subprocess.run(["magick", src, "-channel", c, "-separate", "+channel", dst], check=True)
    chan(f"{o}_{Set}_orm.png", "B", f"{o}_{Set}_h.png")
    subprocess.run(["magick", f"{o}_{Set}_bc.png", f"{o}_{Set}_h.png", "-alpha", "off", "-compose", "CopyOpacity", "-composite", f"{o}T_{Set}_BC.png"], check=True)
    chan(f"{o}_{Set}_n.png", "R", f"{o}_{Set}_nx.png"); chan(f"{o}_{Set}_n.png", "G", f"{o}_{Set}_ny.png")
    chan(f"{o}_{Set}_orm.png", "R", f"{o}_{Set}_ao.png"); chan(f"{o}_{Set}_orm.png", "G", f"{o}_{Set}_ro.png")
    subprocess.run(["magick", f"{o}_{Set}_nx.png", f"{o}_{Set}_ny.png", f"{o}_{Set}_ao.png", f"{o}_{Set}_ro.png",
                    "-channel", "RGBA", "-combine", "-define", "png:color-type=6", f"{o}T_{Set}_NR.png"], check=True)
    for part in ("bc", "orm", "n", "h", "nx", "ny", "ao", "ro"):
        os.remove(f"{o}_{Set}_{part}.png")
    if name == "water":
        os.remove(f"{o}T_{Set}_BC.png")   # the sea is coloured in the material; NR is normal xy, 1, foam
    print(f"BAKED {name}", flush=True)
print("ALL BAKED", flush=True)
