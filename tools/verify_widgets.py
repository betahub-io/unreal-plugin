#!/usr/bin/env python3
"""Pre-release check for the generated widget assets.

Two halves, because they need different things:

  local  (this script)  asset format version, and drift between the committed
                        widgets/*.json and what the .uasset actually contains
  engine (ue_verify_widgets.py)  blueprints compile, and every
                        meta=(BindWidget) property resolves to a real widget

Run the local half first - it needs no Unreal and catches the two failures
that actually ship: an asset saved by the wrong engine, and JSON that has
drifted away from the asset it is supposed to describe.

    python3 tools/verify_widgets.py                       # version check only
    python3 tools/verify_widgets.py --exports <dir>       # + drift check
    python3 tools/verify_widgets.py --emit-config <file>  # config for the engine half

Exit code 0 when everything passes.
"""

import argparse
import glob
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

# UE 5.3 writes FileVersionUE5 1009. A higher number means the asset was saved
# by a newer engine and UE 5.3 can no longer load it - the exact regression
# this whole pipeline exists to prevent.
EXPECTED_UE5_VERSION = 1009

sys.path.insert(0, HERE)
import t3d_to_json  # noqa: E402
import compare_widget_json as cmp_tool  # noqa: E402
from validate_widget_json import find_bind_widgets  # noqa: E402


def asset_versions(path):
    """Read (FileVersionUE4, FileVersionUE5) straight out of the .uasset header."""
    with open(path, "rb") as fh:
        data = fh.read(64)
    tag, legacy = struct.unpack_from("<Ii", data, 0)
    if tag != 0x9E2A83C1:
        raise ValueError("not a .uasset (bad magic 0x%08X)" % tag)
    off = 8
    if legacy != -4:
        off += 4
    ue4, ue5 = struct.unpack_from("<ii", data, off)
    return ue4, ue5


def check_versions(problems):
    assets = sorted(glob.glob(os.path.join(REPO, "Content", "*.uasset")))
    if not assets:
        problems.append("no assets found under Content/")
        return
    for path in assets:
        name = os.path.basename(path)
        try:
            _ue4, ue5 = asset_versions(path)
        except Exception as exc:
            problems.append("%s: %s" % (name, exc))
            continue
        if ue5 != EXPECTED_UE5_VERSION:
            problems.append(
                "%s: FileVersionUE5 is %d, expected %d - this asset was saved by a "
                "newer engine and UE 5.3 users cannot load it"
                % (name, ue5, EXPECTED_UE5_VERSION))
        else:
            print("  %-32s format %d  ok" % (name, ue5))


def check_drift(exports_dir, problems):
    """Compare each committed widgets/*.json against the asset it describes."""
    sources = sorted(glob.glob(os.path.join(REPO, "widgets", "*.json")))
    if not sources:
        problems.append("no sources found under widgets/")
        return

    for src in sources:
        name = os.path.splitext(os.path.basename(src))[0]
        matches = glob.glob(os.path.join(exports_dir, "**", "%s.T3D" % name),
                            recursive=True)
        if not matches:
            problems.append("%s: no %s.T3D under %s - was the export run?"
                            % (name, name, exports_dir))
            continue

        with open(matches[0], "r", encoding="utf-8", errors="replace") as fh:
            from_asset = t3d_to_json.convert(fh.read())
        with open(src, "r", encoding="utf-8") as fh:
            committed = json.load(fh)

        diffs = []
        cmp_tool.diff_node(name, committed.get("root", {}),
                           from_asset.get("root", {}), diffs)
        if diffs:
            problems.append(
                "%s: committed JSON and the .uasset have drifted apart "
                "(%d differences) - someone edited one without the other"
                % (name, len(diffs)))
            for line in diffs[:8]:
                problems.append("    %s" % line)
            if len(diffs) > 8:
                problems.append("    ... and %d more" % (len(diffs) - 8))
        else:
            print("  %-32s matches its .uasset" % (name + ".json"))


def emit_config(path):
    """Write the config the engine-side half needs, including the BindWidget
    names parsed out of the C++ headers so both halves agree on the list."""
    entries = []
    for src in sorted(glob.glob(os.path.join(REPO, "widgets", "*.json"))):
        with open(src, "r", encoding="utf-8") as fh:
            doc = json.load(fh)
        parent = doc.get("parentClass")
        binds, _header = find_bind_widgets(parent)
        entries.append({
            "asset": "/BetaHubBugReporter/%s" % doc.get("asset"),
            "parentClass": parent,
            "bindWidgets": {n: m["type"] for n, m in (binds or {}).items()
                            if not m["optional"]},
        })
    cfg = {"widgets": entries,
           "outDir": "C:/Users/upsoft/bh_widget_dump/verify"}
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(cfg, fh, indent=2)
    print("wrote %s (%d widgets, %d required bindings)"
          % (path, len(entries), sum(len(e["bindWidgets"]) for e in entries)))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exports", help="directory holding .T3D exports of the "
                                      "current assets, for the drift check")
    ap.add_argument("--emit-config", help="write the engine-half config here")
    args = ap.parse_args(argv)

    if args.emit_config:
        emit_config(args.emit_config)
        return 0

    problems = []

    print("asset format version:")
    check_versions(problems)

    if args.exports:
        print("\ndrift between widgets/*.json and Content/*.uasset:")
        check_drift(args.exports, problems)
    else:
        print("\ndrift check skipped (no --exports given)")

    print()
    if problems:
        print("VERIFY: FAIL")
        for p in problems:
            print("  %s" % p)
        return 1
    print("VERIFY: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
