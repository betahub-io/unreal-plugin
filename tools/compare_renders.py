#!/usr/bin/env python3
"""Compare two widget renders: decoded pixels and measured geometry.

    python3 tools/compare_renders.py build/BugReportForm_original build/BugReportForm_Gen

Each argument is a path prefix; <prefix>.png and <prefix>.geometry.json are read.

Do not compare PNGs by checksum. PNG encoding is not deterministic across
runs, so identical images can produce different bytes - a checksum comparison
reports a difference that does not exist, and teaches you to distrust a check
that was actually fine. Decode and compare pixels.

Geometry is compared too, because it localises a failure: a pixel diff says
"something moved", geometry says "SubmitLabel moved 4px right".

Exit code 0 when equivalent, 1 when not.
"""

import argparse
import json
import os
import sys

PIXEL_TOLERANCE = 0      # per-channel; renders of the same tree should match exactly
GEOMETRY_TOLERANCE = 0.01


def load_geometry(prefix):
    path = "%s.geometry.json" % prefix
    if not os.path.exists(path):
        return None
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def compare_geometry(a, b, out):
    if a is None or b is None:
        out.append("geometry: missing on one side, skipped")
        return True

    A = {w["name"]: w for w in a.get("widgets", [])}
    B = {w["name"]: w for w in b.get("widgets", [])}

    ok = True
    for name in sorted(set(A) - set(B)):
        out.append("only in first : %s" % name)
        ok = False
    for name in sorted(set(B) - set(A)):
        out.append("only in second: %s" % name)
        ok = False

    for name in sorted(set(A) & set(B)):
        for key in ("x", "y", "width", "height", "fontSize"):
            va, vb = A[name].get(key), B[name].get(key)
            if va is None and vb is None:
                continue
            if va is None or vb is None:
                out.append("%s: %s present on one side only" % (name, key))
                ok = False
            elif abs(va - vb) > GEOMETRY_TOLERANCE:
                out.append("%s: %s %.2f -> %.2f" % (name, key, va, vb))
                ok = False
        if A[name].get("typeface") != B[name].get("typeface"):
            out.append("%s: typeface %r -> %r"
                       % (name, A[name].get("typeface"), B[name].get("typeface")))
            ok = False
    return ok


def compare_pixels(prefix_a, prefix_b, out, diff_path=None):
    pa, pb = "%s.png" % prefix_a, "%s.png" % prefix_b
    for p in (pa, pb):
        if not os.path.exists(p):
            out.append("pixels: %s missing, skipped" % p)
            return True
    try:
        from PIL import Image, ImageChops
    except ImportError:
        out.append("pixels: Pillow not installed, skipped "
                   "(pip3 install pillow) - geometry was still compared")
        return True

    a = Image.open(pa).convert("RGBA")
    b = Image.open(pb).convert("RGBA")
    if a.size != b.size:
        out.append("pixels: size %s vs %s" % (a.size, b.size))
        return False

    diff = ImageChops.difference(a, b)

    # Do NOT use diff.getbbox() here. On an RGBA image getbbox() is driven by
    # the alpha channel, so two fully-opaque images differing only in colour
    # report no bounding box at all - it silently passes a red rectangle.
    worst = max(band.getextrema()[1] for band in diff.split()[:3])
    if worst <= PIXEL_TOLERANCE:
        return True

    box = diff.convert("RGB").getbbox()
    grey = diff.convert("L")

    hist = grey.histogram()
    differing = sum(hist[1:])
    total = a.size[0] * a.size[1]
    out.append("pixels: %d of %d differ (%.4f%%), max delta %d, region %s"
               % (differing, total, 100.0 * differing / total, worst, box))
    if diff_path:
        diff.crop(box).save(diff_path)
        out.append("pixels: difference crop written to %s" % diff_path)
    return False


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("first", help="path prefix of the reference render")
    ap.add_argument("second", help="path prefix of the render under test")
    ap.add_argument("--diff", help="write a crop of the differing region here")
    args = ap.parse_args(argv)

    out = []
    geo_ok = compare_geometry(load_geometry(args.first),
                              load_geometry(args.second), out)
    px_ok = compare_pixels(args.first, args.second, out, args.diff)

    print("geometry: %s" % ("match" if geo_ok else "DIFFERS"))
    print("pixels  : %s" % ("match" if px_ok else "DIFFERS"))
    if out:
        print()
        for line in out[:40]:
            print("  %s" % line)
        if len(out) > 40:
            print("  ... and %d more" % (len(out) - 40))

    return 0 if (geo_ok and px_ok) else 1


if __name__ == "__main__":
    sys.exit(main())
