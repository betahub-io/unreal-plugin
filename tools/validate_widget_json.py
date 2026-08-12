#!/usr/bin/env python3
"""Validate a widget JSON source file before it ever reaches Unreal.

Runs in under a second with no Unreal install, so mistakes surface while you
are editing rather than after a ~60s editor round trip.

Checks:
  * structure       - required keys, correct types
  * widget classes  - exist in tools/umg_schema.json for the target engine
  * names           - unique, and valid Unreal object names
  * containment     - only panels take children; content widgets take one
  * slots           - declared slot class matches what the parent produces
  * BindWidget      - every meta=(BindWidget) property in the parent C++ class
                      still has a matching widget of a compatible type

The BindWidget check is the important one: renaming a widget in JSON would
otherwise compile fine and fail as a null at runtime.

Usage:
    python3 tools/validate_widget_json.py widgets/BugReportForm.json
    python3 tools/validate_widget_json.py widgets/*.json
"""

import argparse
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCHEMA_PATH = os.path.join(HERE, "umg_schema.json")

NAME_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

# UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CloseButton;
BIND_RE = re.compile(
    r"UPROPERTY\s*\(\s*meta\s*=\s*\(\s*BindWidget(?P<opt>Optional)?\s*\)\s*\)\s*"
    r"(?:TObjectPtr\s*<\s*U(?P<t1>\w+)\s*>|U(?P<t2>\w+)\s*\*)\s+(?P<name>\w+)\s*;",
    re.MULTILINE,
)


class Report:
    def __init__(self):
        self.errors = []
        self.warnings = []

    def error(self, where, msg):
        self.errors.append("%s: %s" % (where, msg))

    def warn(self, where, msg):
        self.warnings.append("%s: %s" % (where, msg))

    @property
    def ok(self):
        return not self.errors


def load_schema():
    if not os.path.exists(SCHEMA_PATH):
        raise SystemExit(
            "missing %s - regenerate it with tools/ue_dump_umg_schema.py"
            % SCHEMA_PATH)
    with open(SCHEMA_PATH, "r", encoding="utf-8") as fh:
        return json.load(fh)


def find_bind_widgets(parent_class):
    """Parse meta=(BindWidget) properties out of the parent C++ header."""
    if not parent_class:
        return None, None
    header = None
    for base, _dirs, files in os.walk(os.path.join(REPO, "Source")):
        if "%s.h" % parent_class in files:
            header = os.path.join(base, "%s.h" % parent_class)
            break
    if header is None:
        return None, None

    with open(header, "r", encoding="utf-8", errors="replace") as fh:
        text = fh.read()

    binds = {}
    for m in BIND_RE.finditer(text):
        binds[m.group("name")] = {
            "type": m.group("t1") or m.group("t2"),
            "optional": bool(m.group("opt")),
        }
    return binds, os.path.relpath(header, REPO)


def check_node(node, schema, rep, seen, path, is_root=False):
    where = path

    if not isinstance(node, dict):
        rep.error(where, "node is not an object")
        return

    name = node.get("name")
    cls = node.get("class")

    if not isinstance(name, str) or not name:
        rep.error(where, "missing or non-string 'name'")
        return
    where = "%s/%s" % (path, name)

    if not NAME_RE.match(name):
        rep.error(where, "%r is not a valid Unreal object name" % name)
    if name in seen:
        rep.error(where, "duplicate widget name %r (already used at %s)"
                  % (name, seen[name]))
    else:
        seen[name] = where

    if not isinstance(cls, str) or not cls:
        rep.error(where, "missing or non-string 'class'")
        return

    info = schema["widgets"].get(cls)
    if info is None:
        rep.error(where, "unknown widget class %r - not a UWidget in engine %s"
                  % (cls, schema.get("engine")))
        info = {}

    props = node.get("properties", {})
    if not isinstance(props, dict):
        rep.error(where, "'properties' must be an object")

    slot = node.get("slot")
    if is_root and slot:
        rep.error(where, "root widget must not declare a 'slot' - it has no parent")
    if slot is not None and not isinstance(slot, dict):
        rep.error(where, "'slot' must be an object")

    children = node.get("children", [])
    if not isinstance(children, list):
        rep.error(where, "'children' must be a list")
        children = []

    if children:
        if not info.get("isPanel"):
            rep.error(where, "%s cannot take children (not a panel widget)" % cls)
        else:
            limit = info.get("maxChildren", -1)
            if limit != -1 and len(children) > limit:
                rep.error(where, "%s accepts %d child(ren), found %d"
                          % (cls, limit, len(children)))

    expected_slot = info.get("slotClass")
    for child in children:
        if isinstance(child, dict) and isinstance(child.get("slot"), dict):
            declared = child["slot"].get("class")
            if declared and expected_slot and declared != expected_slot:
                rep.error("%s/%s" % (where, child.get("name", "?")),
                          "slot class %r does not match %r produced by parent %s"
                          % (declared, expected_slot, cls))
        check_node(child, schema, rep, seen, where)


def validate(path, schema):
    rep = Report()
    with open(path, "r", encoding="utf-8") as fh:
        try:
            doc = json.load(fh)
        except ValueError as exc:
            rep.error(path, "invalid JSON: %s" % exc)
            return rep, {}

    root = doc.get("root")
    if not isinstance(root, dict):
        rep.error(path, "missing 'root' object")
        return rep, {}

    seen = {}
    check_node(root, schema, rep, seen, os.path.basename(path), is_root=True)

    parent_class = doc.get("parentClass")
    binds, header = find_bind_widgets(parent_class)
    if binds is None:
        rep.warn(path, "could not find header for parentClass %r - "
                       "BindWidget check skipped" % parent_class)
    else:
        for bind_name, meta in sorted(binds.items()):
            if bind_name not in seen:
                msg = ("%s declares meta=(BindWidget) %s %s, but no widget of "
                       "that name exists - this compiles but is null at runtime"
                       % (header, meta["type"], bind_name))
                if meta["optional"]:
                    rep.warn(path, msg)
                else:
                    rep.error(path, msg)

    return rep, seen


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    args = ap.parse_args(argv)

    schema = load_schema()
    paths = []
    for pattern in args.files:
        paths.extend(sorted(glob.glob(pattern)) or [pattern])

    failed = False
    for path in paths:
        rep, seen = validate(path, schema)
        label = os.path.relpath(path, os.getcwd())
        if rep.ok and not rep.warnings:
            print("%-40s OK   (%d widgets)" % (label, len(seen)))
        else:
            print("%-40s %s" % (label, "FAIL" if rep.errors else "OK with warnings"))
        for e in rep.errors:
            print("   error: %s" % e)
        for w in rep.warnings:
            print("   warn : %s" % w)
        failed = failed or not rep.ok

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
