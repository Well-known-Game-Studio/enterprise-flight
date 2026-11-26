"""Builds M_PhaserBeam, the material of a phaser beam (PhaserBeam.cpp): unlit, additive, two-sided, its colour the
beam's own.

APhaserBeam sets BeamColor on its instance (the ship's orange by default). Until this builder the material had its
colour as a constant and no parameter at all, so the setting did nothing; the only beams drawn in it are the
ship's, which are that orange, so nothing looked wrong. (A Borg cube's cutting beam is an APhaserBeam too, but in
M_BorgBeam, which has its own BeamColor.) The glow is the colour thirty times over, as before.

Run with the editor open and no play session:  python3 tools/ship_materials/phaser_beam_material.py
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ue_mcp"))
from uemat import *

NAME = "M_PhaserBeam"


def build():
    wipe(NAME)
    g = G(NAME, {"ShadingModel": "MSM_Unlit", "BlendMode": "BLEND_Additive", "TwoSided": True})
    color = g.vec("BeamColor", (1.0, 0.55, 0.15))
    glow = g.node("Multiply"); g.c(color, glow, "A", "RGB"); g.c(g.const(30.0), glow, "B")
    g.out(glow, "MP_EmissiveColor")
    g.out(g.const(0.85), "MP_Opacity")
    g.finish()


if __name__ == "__main__":
    build()
