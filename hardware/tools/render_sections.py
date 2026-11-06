#!/usr/bin/env python3
"""Cut the assembled model into labelled cross-section images.

Needs: pip install trimesh networkx matplotlib
Run:   python3 hardware/tools/render_sections.py
Writes hardware/model/renders/section_*.png from hardware/model/bodies/*.stl.
"""
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch, PathPatch
from matplotlib.path import Path as MplPath
import numpy as np
import trimesh

MODEL = Path(__file__).resolve().parents[1] / "model"
PARTS = {
    "enclosure": ("#9fb4cc", "enclosure"),
    "encoder_ec11": ("#8c8c92", "EC11 encoder"),
    "battery_14500": ("#d4ad3c", "14500 battery"),
    "lilygo_board_placeholder": ("#2f8050", "LilyGo board (slab stand-in)"),
    "speaker_3525": ("#26262c", "3525 speaker"),
    "amp_max98357": ("#3c5ac0", "MAX98357 amp"),
    "tp4057_board": ("#c23232", "TP4057 board (stand-in)"),
    "tp4057_sokkel_v6": ("#e0761c", "TP4057 sokkel V6"),
}
# name, axis cut across, position (mm), horizontal axis, vertical axis, what the viewer faces
SECTIONS = [
    ("y030", 1, 30.0, 0, 2, "looking from the knob end towards the USB end"),
    ("y060", 1, 60.0, 0, 2, "looking from the knob end towards the USB end"),
    ("y076", 1, 76.0, 0, 2, "looking from the knob end towards the USB end"),
    ("x113", 0, 113.0, 1, 2, "looking from +X towards -X"),
]
AXES = "XYZ"


def outline(mesh, axis, pos, h, v):
    """Closed outlines of the mesh on the plane, as a fillable path (or None)."""
    origin, normal = np.zeros(3), np.zeros(3)
    origin[axis], normal[axis] = pos, 1.0
    section = mesh.section(plane_origin=origin, plane_normal=normal)
    if section is None:
        return None
    loops = [np.asarray(loop)[:, [h, v]] for loop in section.discrete]
    verts, codes = [], []
    for i, pts in enumerate(loops):
        # Matplotlib fills by winding, so wind each loop by how deeply it nests:
        # that keeps cavities and holes empty.
        depth = sum(MplPath(o).contains_point(pts[0]) for j, o in enumerate(loops) if j != i)
        x, y = pts[:, 0], pts[:, 1]
        counter_clockwise = np.dot(x[:-1], y[1:]) - np.dot(x[1:], y[:-1]) > 0
        if counter_clockwise == bool(depth % 2):
            pts = pts[::-1]
        verts += pts.tolist()
        codes += [MplPath.MOVETO] + [MplPath.LINETO] * (len(pts) - 2) + [MplPath.CLOSEPOLY]
    return MplPath(verts, codes) if verts else None


def main():
    meshes = {name: trimesh.load(MODEL / "bodies" / f"{name}.stl") for name in PARTS}
    for label, axis, pos, h, v, facing in SECTIONS:
        fig, ax = plt.subplots(figsize=(9, 7), dpi=150)
        legend = []
        for name, (colour, title) in PARTS.items():
            path = outline(meshes[name], axis, pos, h, v)
            if path is None:
                continue
            ax.add_patch(PathPatch(path, facecolor=colour, edgecolor="#1d2228", lw=0.6, alpha=0.9))
            legend.append(Patch(facecolor=colour, edgecolor="#1d2228", label=title))
        ax.autoscale_view()
        ax.set_aspect("equal")
        ax.margins(0.08)
        ax.grid(True, lw=0.3, color="#c9ced5")
        ax.set_axisbelow(True)
        ax.set_xlabel(f"{AXES[h]} (mm)")
        ax.set_ylabel(f"{AXES[v]} (mm)")
        ax.set_title(f"Globo section at {AXES[axis]} = {pos:g} mm, {facing}", fontsize=10)
        ax.legend(handles=legend, loc="upper left", bbox_to_anchor=(1.01, 1), frameon=False, fontsize=8)
        fig.savefig(MODEL / "renders" / f"section_{label}.png", bbox_inches="tight")
        plt.close(fig)
        print(label, [p.get_label() for p in legend])


if __name__ == "__main__":
    main()
