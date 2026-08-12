#!/usr/bin/env python3
"""Semantic diff between two widget JSON documents.

This is the phase-1 gate. Rather than text-diffing two .T3D exports - where
auto-generated slot object names and object ordering differ harmlessly and
swamp the real signal - both sides are converted to the JSON source format
first, which already discards that noise, then compared structurally.

Usage:
    python3 tools/compare_widget_json.py original.json regenerated.json
    python3 tools/compare_widget_json.py a.T3D b.T3D          # converts first

Exit code 0 when equivalent, 1 when not.
"""

import argparse
import json
import math
import os
import sys

FLOAT_TOLERANCE = 1e-4

# Present in an export but not meaningful for equivalence.
IGNORED_PROPS = {"bExpandedInDesigner", "DisplayLabel"}


def load(path):
    if path.lower().endswith(".t3d"):
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from t3d_to_json import convert
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            return convert(fh.read())
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def close_enough(a, b):
    if isinstance(a, bool) or isinstance(b, bool):
        return a is b
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return math.isclose(float(a), float(b), rel_tol=0, abs_tol=FLOAT_TOLERANCE)
    return False


def diff_value(path, a, b, out):
    if isinstance(a, dict) and isinstance(b, dict):
        for key in sorted(set(a) | set(b)):
            if key in IGNORED_PROPS:
                continue
            sub = "%s.%s" % (path, key)
            if key not in a:
                out.append("+ %s = %r  (only in regenerated)" % (sub, b[key]))
            elif key not in b:
                out.append("- %s = %r  (missing from regenerated)" % (sub, a[key]))
            else:
                diff_value(sub, a[key], b[key], out)
        return

    if isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            out.append("~ %s list length %d vs %d" % (path, len(a), len(b)))
        for i in range(min(len(a), len(b))):
            diff_value("%s[%d]" % (path, i), a[i], b[i], out)
        return

    if a == b or close_enough(a, b):
        return
    out.append("~ %s: %r -> %r" % (path, a, b))


def diff_node(path, a, b, out):
    if a.get("name") != b.get("name"):
        out.append("~ %s name: %r -> %r" % (path, a.get("name"), b.get("name")))
    if a.get("class") != b.get("class"):
        out.append("~ %s class: %r -> %r" % (path, a.get("class"), b.get("class")))

    diff_value("%s.properties" % path, a.get("properties") or {},
               b.get("properties") or {}, out)

    a_slot, b_slot = a.get("slot"), b.get("slot")
    if bool(a_slot) != bool(b_slot):
        out.append("~ %s slot present: %s -> %s" % (path, bool(a_slot), bool(b_slot)))
    elif a_slot and b_slot:
        if a_slot.get("class") != b_slot.get("class"):
            out.append("~ %s slot class: %r -> %r"
                       % (path, a_slot.get("class"), b_slot.get("class")))
        diff_value("%s.slot.properties" % path, a_slot.get("properties") or {},
                   b_slot.get("properties") or {}, out)

    a_kids = a.get("children") or []
    b_kids = b.get("children") or []
    if len(a_kids) != len(b_kids):
        out.append("~ %s child count %d vs %d  (%s | %s)"
                   % (path, len(a_kids), len(b_kids),
                      ", ".join(k.get("name", "?") for k in a_kids),
                      ", ".join(k.get("name", "?") for k in b_kids)))
    for i in range(min(len(a_kids), len(b_kids))):
        child_path = "%s/%s" % (path, a_kids[i].get("name", i))
        diff_node(child_path, a_kids[i], b_kids[i], out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("original")
    ap.add_argument("regenerated")
    ap.add_argument("--max", type=int, default=60,
                    help="max differences to print (default 60)")
    args = ap.parse_args(argv)

    a, b = load(args.original), load(args.regenerated)
    out = []
    diff_node(a.get("root", {}).get("name", "root"),
              a.get("root", {}), b.get("root", {}), out)

    a_stats = a.get("_stats", {})
    b_stats = b.get("_stats", {})
    print("original    : %s widgets" % a_stats.get("widgetsReachable", "?"))
    print("regenerated : %s widgets" % b_stats.get("widgetsReachable", "?"))
    print()

    if not out:
        print("GATE: PASS - regenerated tree is equivalent to the original")
        return 0

    print("GATE: FAIL - %d difference(s)" % len(out))
    for line in out[:args.max]:
        print("  %s" % line)
    if len(out) > args.max:
        print("  ... and %d more" % (len(out) - args.max))
    return 1


if __name__ == "__main__":
    sys.exit(main())
