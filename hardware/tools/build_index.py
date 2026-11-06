#!/usr/bin/env python3
"""Regenerate hardware/index.json: every 3D file, measured, in one place.

Standard library only, so it runs anywhere:  python3 hardware/tools/build_index.py

Reads binary STL and 3MF directly (triangles, bounding box, volume). STEP files
are listed with their size; open them with CadQuery or FreeCAD for geometry.
Roles and status live in hardware/tools/annotations.json, so re-exporting a
part only means running this again.
"""
import hashlib
import json
import re
import struct
import zipfile
from pathlib import Path

HW = Path(__file__).resolve().parents[1]
SKIP_DIRS = {"tools"}
FORMATS = {".stl": "stl", ".3mf": "3mf", ".step": "step", ".stp": "step", ".f3d": "fusion"}


def measure(tris):
    """tris: iterable of 9-float tuples. Returns count, bbox and volume."""
    lo, hi = [float("inf")] * 3, [float("-inf")] * 3
    count, vol = 0, 0.0
    for ax, ay, az, bx, by, bz, cx, cy, cz in tris:
        count += 1
        for i, v in enumerate((ax, ay, az, bx, by, bz, cx, cy, cz)):
            axis = i % 3
            if v < lo[axis]:
                lo[axis] = v
            if v > hi[axis]:
                hi[axis] = v
        vol += ax * (by * cz - bz * cy) - ay * (bx * cz - bz * cx) + az * (bx * cy - by * cx)
    return {
        "triangles": count,
        "bbox_min_mm": [round(v, 3) for v in lo],
        "bbox_max_mm": [round(v, 3) for v in hi],
        "size_mm": [round(b - a, 3) for a, b in zip(lo, hi)],
        "volume_cm3": round(abs(vol) / 6000, 3),
    }


def stl_tris(path):
    data = path.read_bytes()
    count = struct.unpack_from("<I", data, 80)[0]
    if 84 + 50 * count != len(data):
        raise ValueError("not a binary STL")
    for off in range(84, len(data), 50):
        yield struct.unpack_from("<9f", data, off + 12)


def threemf_tris(path):
    """All objects in the file, with no build transforms applied."""
    with zipfile.ZipFile(path) as z:
        model = z.read("3D/3dmodel.model").decode("utf-8", "replace")
    for mesh in re.findall(r"<mesh>(.*?)</mesh>", model, re.S):
        verts = [
            tuple(float(n) for n in v)
            for v in re.findall(r'<vertex\s+x="([^"]+)"\s+y="([^"]+)"\s+z="([^"]+)"', mesh)
        ]
        for a, b, c in re.findall(r'<triangle\s+v1="(\d+)"\s+v2="(\d+)"\s+v3="(\d+)"', mesh):
            yield verts[int(a)] + verts[int(b)] + verts[int(c)]


def describe(path):
    rel = path.relative_to(HW).as_posix()
    data = path.read_bytes()
    entry = {
        "path": rel,
        "format": FORMATS[path.suffix.lower()],
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }
    if entry["format"] == "stl":
        entry.update(measure(stl_tris(path)))
    elif entry["format"] == "3mf":
        entry.update(measure(threemf_tris(path)))
    return entry


def annotate(entry, rules):
    """First matching rule wins; later fields never overwrite measurements."""
    for rule in rules:
        if re.search(rule["match"], entry["path"]):
            for key, value in rule.items():
                if key != "match":
                    entry.setdefault(key, value)
            break
    return entry


def main():
    notes = json.loads((HW / "tools/annotations.json").read_text())
    files = [
        annotate(describe(p), notes["rules"])
        for p in sorted(HW.rglob("*"))
        if p.is_file()
        and p.suffix.lower() in FORMATS
        and not SKIP_DIRS & set(p.relative_to(HW).parts)
    ]
    index = {key: value for key, value in notes.items() if key != "rules"}
    index["files"] = files
    (HW / "index.json").write_text(json.dumps(index, indent=2) + "\n")
    print(f"index.json: {len(files)} files")


if __name__ == "__main__":
    main()
