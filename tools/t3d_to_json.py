#!/usr/bin/env python3
"""Convert an Unreal .T3D export of a WidgetBlueprint into the JSON source format.

Runs anywhere - no Unreal install required. The .T3D is produced by
tools/ue_export_t3d.py running inside the editor.

T3D layout, for reference:

    Begin Object Class=... Name="X" ExportPath="..."   <- declaration pass
    ...
    Begin Object Name="X" ExportPath="..."             <- definition pass
       Property=Value
    End Object

Widgets sit flat under WidgetTree. Hierarchy is by reference, not nesting:
a panel lists Slots(0..n); each slot has Content (the child) and Parent.
Each widget's own Slot points back at the slot holding it.

Usage:
    python3 tools/t3d_to_json.py BugReportForm.T3D -o widgets/BugReportForm.json
"""

import argparse
import json
import re
import sys

# Properties that encode structure. They are rebuilt from the hierarchy on
# generation, so carrying them in the source format would be duplicate truth.
STRUCTURAL = {"Slot", "Slots", "Parent", "Content", "RootWidget"}

# Editor-only bookkeeping that says nothing about how the widget looks.
EDITOR_NOISE = {"bExpandedInDesigner", "DisplayLabel"}

BEGIN_RE = re.compile(r'^\s*Begin Object\s+(?P<attrs>.*)$')
END_RE = re.compile(r'^\s*End Object\s*$')
PROP_RE = re.compile(r'^\s*(?P<key>[A-Za-z_][A-Za-z0-9_]*)(?:\((?P<idx>\d+)\))?=(?P<val>.*)$')
ATTR_RE = re.compile(r'(\w+)=(?:"([^"]*)"|(\S+))')
NSLOCTEXT_RE = re.compile(r'^NSLOCTEXT\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)$')
OBJREF_RE = re.compile(r"^(?P<cls>[\w./]+)'(?P<path>[^']*)'$")


class Block:
    __slots__ = ("cls", "name", "export_path", "props", "children")

    def __init__(self, cls, name, export_path):
        self.cls = cls
        self.name = name
        self.export_path = export_path
        self.props = {}
        self.children = []

    def __repr__(self):
        return "<Block %s %s>" % (self.cls, self.name)


# --------------------------------------------------------------------------
# value parsing
# --------------------------------------------------------------------------

def split_top_level(s):
    """Split "A=1,B=(C=2,D=3)" on commas that are not inside () or quotes."""
    parts, depth, in_str, cur = [], 0, False, []
    i = 0
    while i < len(s):
        ch = s[i]
        if in_str:
            cur.append(ch)
            if ch == "\\" and i + 1 < len(s):
                cur.append(s[i + 1])
                i += 2
                continue
            if ch == '"':
                in_str = False
        elif ch == '"':
            in_str = True
            cur.append(ch)
        elif ch == "(":
            depth += 1
            cur.append(ch)
        elif ch == ")":
            depth -= 1
            cur.append(ch)
        elif ch == "," and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
        i += 1
    if cur:
        parts.append("".join(cur))
    return [p.strip() for p in parts if p.strip()]


_ESCAPES = {"\\": "\\", '"': '"', "'": "'", "n": "\n", "r": "\r",
            "t": "\t", "0": "\0"}


def unescape(s):
    """Decode T3D string escapes. Without this, \\r\\n survives as a literal
    backslash-r and gets escaped again on the next export."""
    if "\\" not in s:
        return s
    out, i = [], 0
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            nxt = s[i + 1]
            out.append(_ESCAPES.get(nxt, nxt))
            i += 2
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def parse_value(raw):
    """Parse one T3D property value into a JSON-friendly Python value."""
    s = raw.strip().rstrip(",")
    if not s:
        return ""

    m = NSLOCTEXT_RE.match(s)
    if m:
        # Namespace and key are localisation bookkeeping; the literal is the
        # only part a human edits.
        return {"__text__": unescape(m.group(3)), "__key__": m.group(2)}

    if s.startswith("(") and s.endswith(")"):
        inner = s[1:-1].strip()
        if not inner:
            return {}
        parts = split_top_level(inner)
        if all("=" in p and p.index("=") > 0 and PROP_RE.match(p) for p in parts):
            out = {}
            for p in parts:
                k, _, v = p.partition("=")
                out[k.strip()] = parse_value(v)
            return out
        return [parse_value(p) for p in parts]

    if s.startswith('"') and s.endswith('"') and len(s) >= 2:
        s = s[1:-1]
        # Object references are usually emitted wrapped in quotes, so the
        # ref check has to happen after unquoting, not before.
        m = OBJREF_RE.match(s)
        if m:
            return {"__ref__": m.group("path"), "__class__": m.group("cls")}
        return unescape(s)

    if s in ("True", "true"):
        return True
    if s in ("False", "false"):
        return False

    m = OBJREF_RE.match(s)
    if m:
        return {"__ref__": m.group("path"), "__class__": m.group("cls")}

    try:
        if re.fullmatch(r"[+-]?\d+", s):
            return int(s)
        if re.fullmatch(r"[+-]?(\d+\.\d*|\.\d+|\d+)([eE][+-]?\d+)?", s):
            return float(s)
    except ValueError:
        pass

    return s  # bare token: enum literal such as HAlign_Fill


# --------------------------------------------------------------------------
# T3D parsing
# --------------------------------------------------------------------------

def parse_t3d(text):
    """Return the root Blocks, with declaration and definition passes merged."""
    roots, stack = [], []
    by_path = {}

    for line in text.splitlines():
        if END_RE.match(line):
            if stack:
                stack.pop()
            continue

        m = BEGIN_RE.match(line)
        if m:
            attrs = dict(
                (k, v1 if v1 is not None and v1 != "" else v2)
                for k, v1, v2 in ATTR_RE.findall(m.group("attrs"))
            )
            path = attrs.get("ExportPath", "")
            existing = by_path.get(path)
            if existing is not None:
                block = existing            # definition pass for a known object
            else:
                block = Block(attrs.get("Class", ""), attrs.get("Name", ""), path)
                if path:
                    by_path[path] = block
                if stack:
                    stack[-1].children.append(block)
                else:
                    roots.append(block)
            stack.append(block)
            continue

        if not stack:
            continue

        m = PROP_RE.match(line)
        if m:
            key, idx, val = m.group("key"), m.group("idx"), m.group("val")
            parsed = parse_value(val)
            if idx is not None:
                stack[-1].props.setdefault(key, []).append(parsed)
            else:
                stack[-1].props[key] = parsed

    return roots, by_path


# --------------------------------------------------------------------------
# hierarchy reconstruction
# --------------------------------------------------------------------------

def short_name(ref):
    """'BugReportForm:WidgetTree.CloseButton' -> 'CloseButton'"""
    if isinstance(ref, dict):
        ref = ref.get("__ref__", "")
    return str(ref).rsplit(".", 1)[-1].rsplit(":", 1)[-1]


def clean_props(block, keep_editor_noise):
    out = {}
    for k, v in block.props.items():
        if k in STRUCTURAL:
            continue
        if not keep_editor_noise and k in EDITOR_NOISE:
            continue
        out[k] = v
    return out


def build_node(widget, widgets_by_name, keep_noise, seen):
    if widget.name in seen:
        raise ValueError("cycle in widget tree at %r" % widget.name)
    seen = seen | {widget.name}

    node = {
        "name": widget.name,
        "class": widget.cls.rsplit(".", 1)[-1],
        "properties": clean_props(widget, keep_noise),
    }

    # Slot names are unique only within their owning widget - HorizontalBox_150
    # and HorizontalBox_151 both contain a "HorizontalBoxSlot_0". Resolve them
    # against this widget's own children, never a global table.
    own_slots = {s.name: s for s in widget.children}

    slot_refs = widget.props.get("Slots") or []
    children = []
    for ref in slot_refs:
        slot = own_slots.get(short_name(ref))
        if slot is None:
            raise ValueError("%s references slot %r that it does not own"
                             % (widget.name, ref))
        content = slot.props.get("Content")
        if content is None:
            continue
        child = widgets_by_name.get(short_name(content))
        if child is None:
            raise ValueError("slot %s references missing widget %r"
                             % (slot.name, content))
        child_node = build_node(child, widgets_by_name, keep_noise, seen)
        child_node["slot"] = {
            "class": slot.cls.rsplit(".", 1)[-1],
            "properties": clean_props(slot, keep_noise),
        }
        children.append(child_node)

    if children:
        node["children"] = children
    return node


def convert(text, keep_noise=False):
    roots, _by_path = parse_t3d(text)
    if not roots:
        raise ValueError("no objects found - is this a .T3D export?")

    blueprint = roots[0]
    tree = next((c for c in blueprint.children if c.name == "WidgetTree"), None)
    if tree is None:
        raise ValueError("no WidgetTree in export")

    widgets_by_name = {w.name: w for w in tree.children}
    slot_count = sum(len(w.children) for w in tree.children)

    root_ref = tree.props.get("RootWidget")
    if root_ref is None:
        raise ValueError("WidgetTree has no RootWidget")
    root = widgets_by_name.get(short_name(root_ref))
    if root is None:
        raise ValueError("RootWidget %r not found among widgets" % (root_ref,))

    parent_class = blueprint.props.get("ParentClass")

    doc = {
        "$schema": "betahub-umg-widget/1",
        "asset": blueprint.name,
        "parentClass": short_name(parent_class) if parent_class else None,
        "root": build_node(root, widgets_by_name, keep_noise, frozenset()),
    }

    reachable = _count(doc["root"])
    doc["_stats"] = {
        "widgetsReachable": reachable,
        "widgetsInTree": len(widgets_by_name),
        "slots": slot_count,
    }
    return doc


def _count(node):
    return 1 + sum(_count(c) for c in node.get("children", []))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("t3d", help="path to the .T3D export")
    ap.add_argument("-o", "--out", help="output .json (default: stdout)")
    ap.add_argument("--keep-editor-noise", action="store_true",
                    help="retain bExpandedInDesigner / DisplayLabel")
    args = ap.parse_args(argv)

    with open(args.t3d, "r", encoding="utf-8", errors="replace") as fh:
        doc = convert(fh.read(), keep_noise=args.keep_editor_noise)

    stats = doc["_stats"]
    if stats["widgetsReachable"] != stats["widgetsInTree"]:
        print("WARNING: %d widgets in tree but only %d reachable from root - "
              "orphans will be dropped"
              % (stats["widgetsInTree"], stats["widgetsReachable"]),
              file=sys.stderr)

    text = json.dumps(doc, indent=2, sort_keys=False, ensure_ascii=False)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(text + "\n")
        print("wrote %s  (%d widgets, %d slots)"
              % (args.out, stats["widgetsReachable"], stats["slots"]),
              file=sys.stderr)
    else:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
