#!/usr/bin/env python3
"""UMG markup: a hand- and agent-writable source that compiles to widget JSON.

    python3 tools/umg_markup.py compile widgets/BugReportScreen.umg.xml -o widgets/BugReportScreen.json
    python3 tools/umg_markup.py decompile widgets/BugReportForm.json -o /tmp/BugReportForm.umg.xml
    python3 tools/umg_markup.py check          # every *.umg.xml matches its committed .json

The JSON in widgets/ stays what the UE 5.3 generator consumes; this is a layer
in front of it, the way UXML/USS sit in front of Unity's prefab. The
vocabulary is UMG's own, so what you write is what Unreal builds:

    <Widget asset="Demo" parent="UserWidget" styles="demo.umg.css">
      <Overlay name="Root">
        <Border class="panel" Padding="24 16" slot.HorizontalAlignment="Center">
          <TextBlock name="Title" class="title" key="Demo.Title">REPORT A BUG</TextBlock>
        </Border>
      </Overlay>
    </Widget>

Elements are UMG classes. Attributes are properties with the names the .T3D
export uses, dotted into structs (Font.Size, WidgetStyle.Normal.TintColor);
`slot.` addresses the slot the parent gives the widget. Reserved attributes:
`name` (needed for BindWidget, otherwise generated), `class` (style classes),
`key` (localisation key of the text content; without one the text is
culture-invariant).

Values are typed by the engine default of the property (tools/umg_defaults.json):

    margins   "10" | "10 20" | "1 2 3 4"   - FMargin(all), (horizontal, vertical),
                                            (left, top, right, bottom)
    vectors   "2" | "1 0.7"
    colours   "#RRGGBB" | "#RRGGBBAA"      - sRGB, stored linear as Unreal does
              "linear(r g b a)"            - exact linear values
    enums     "Center" for HAlign_Center; full names work too
    sizes     "Auto" | "Fill" | "Fill 2"   - box slot Size
    objects   "ref(/Script/Engine.Font'/Engine/EngineFonts/Roboto.Roboto')"

Setting one field (slot.Padding.Bottom="10") leaves the others absent, which
means *default* - exactly as in the JSON.

The stylesheet (*.umg.css) holds tokens and rules:

    :root   { --accent: #FF7A45; --gap: 12; }
    TextBlock     { Font.Size: 20; }
    .title        { Font.Size: 36; ColorAndOpacity: var(--accent); }
    #Title        { Font.TypefaceFontName: Bold; }

Selectors are a type, .classes and/or #name, compounded but never nested.
Precedence: #name over .class over type, then later rules; attributes on the
element win over every rule. var() works in rules and attributes alike.
"""

import argparse
import difflib
import glob
import json
import os
import re
import sys
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape as xml_escape

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import umg_resolve  # noqa: E402

SCHEMA_PATH = os.path.join(HERE, "umg_schema.json")
PROPERTIES_PATH = os.path.join(HERE, "umg_properties.json")
WIDGETS_DIR = os.path.join(REPO, "widgets")
MARKUP_SUFFIX = ".umg.xml"
SCHEMA_ID = "betahub-umg-widget/1"
SLOT_PREFIX = "slot."
KEY_SUFFIX = ".key"
RESERVED = {"name", "class", "key"}
# Widgets whose element content is their Text.
TEXT_CONTENT = {c: "Text" for c in ("TextBlock", "RichTextBlock", "EditableText",
                                     "MultiLineEditableText", "EditableTextBox",
                                     "MultiLineEditableTextBox")}
# FText properties. Not in the dumped defaults, so typed here.
TEXT_PROPERTIES = {"Text", "HintText"}
# Enum literals by property name, as .T3D writes them. Short forms resolve by suffix.
ENUMS = {
    "HorizontalAlignment": ["HAlign_Fill", "HAlign_Left", "HAlign_Center", "HAlign_Right"],
    "VerticalAlignment": ["VAlign_Fill", "VAlign_Top", "VAlign_Center", "VAlign_Bottom"],
    "Visibility": ["Visible", "Collapsed", "Hidden", "HitTestInvisible", "SelfHitTestInvisible"],
    "Justification": ["Left", "Center", "Right"],
    "CheckedState": ["Unchecked", "Checked", "Undetermined"],
    "Orientation": ["Orient_Horizontal", "Orient_Vertical"],
    "SizeRule": ["Automatic", "Fill"],
}
SIZE_AUTO_WORDS = {"auto", "automatic"}
MARGIN_FIELDS = ("Left", "Top", "Right", "Bottom")
VEC4_FIELDS = ("X", "Y", "Z", "W")
# Widgets that keep their content padding and alignment twice: on themselves and
# on their child's slot. The editor keeps the two equal (UBorder::OnSlotAdded
# copies one way, SynchronizeProperties applies the widget's own), so the
# compiler fills whichever side is missing from the other.
MIRRORED = {"Border": "BorderSlot", "BackgroundBlur": "BackgroundBlurSlot"}
MIRRORED_PROPS = ("Padding", "HorizontalAlignment", "VerticalAlignment")
COLOR_FIELDS = ("R", "G", "B", "A")
SPECIFIED_RULE = "UseColor_Specified"
LINE_WRAP = 100

VAR_RE = re.compile(r"var\(\s*(--[A-Za-z0-9_-]+)\s*\)")
REF_RE = re.compile(r"^ref\(\s*(?P<cls>[\w./]+)'(?P<path>[^']+)'\s*\)$")
LINEAR_RE = re.compile(r"^linear\((?P<body>[^)]*)\)$")
HEX_RE = re.compile(r"^#(?P<hex>[0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$")
NUM_RE = re.compile(r"^[+-]?(\d+\.\d*|\.\d+|\d+)([eE][+-]?\d+)?$")
SELECTOR_RE = re.compile(r"^(?P<type>[A-Za-z_]\w*)?(?P<rest>(?:[.#][A-Za-z_][\w-]*)*)$")
PROP_RE = re.compile(r"^[A-Za-z_][\w.]*$")
TOKEN_RE = re.compile(r"^--[A-Za-z0-9_-]+$")
MAX_TOKEN_DEPTH = 16
# Tokens a theme supplies at runtime (FBH_ThemeColors). Their :root values are the
# compile-time defaults; every property set from one is re-coloured when the theme changes.
THEME_PREFIX = "--bh-"
THEME_TOKENS = {"ground", "surface", "surface-hi", "line", "text", "muted", "accent", "on-accent",
                "ok", "warn", "err", "field", "field-hi", "bar", "scrim", "radius"}
WHOLE_VAR_RE = re.compile(r"^\s*var\(\s*(--[A-Za-z0-9_-]+)\s*\)\s*$")
# Every source file of the plugin carries it (Fab requires a copyright notice).
COPYRIGHT = "// Copyright (c) 2024-2026 Upsoft sp. z o. o."
DEFAULT_STYLES_OUT = os.path.join(REPO, "Source", "BetaHubBugReporter", "Private", "Generated",
                                  "BH_WidgetStyles.inl")


class MarkupError(ValueError):
    pass


_schema = None
_properties = None


def schema():
    global _schema
    if _schema is None:
        with open(SCHEMA_PATH, "r", encoding="utf-8") as fh:
            _schema = json.load(fh)["widgets"]
    return _schema


def properties():
    """Reflected properties per class and struct (ue_dump_umg_properties.py)."""
    global _properties
    if _properties is None:
        with open(PROPERTIES_PATH, "r", encoding="utf-8") as fh:
            _properties = json.load(fh)
    return _properties


def _where_else(cls, name):
    """Struct paths under `cls` that end in a field called `name`, for hints."""
    props = properties()
    hits = []
    for top, info in props["classes"].get(cls, {}).items():
        struct = info.get("struct")
        if struct and name in props["structs"].get(struct, {}):
            hits.append("%s.%s" % (top, name))
    return hits


def check_path(cls, parts, where):
    """Refuse a property path the class does not have, with a suggestion."""
    props = properties()
    fields = props["classes"].get(cls)
    if fields is None:
        raise MarkupError("%s: no property list for %s - re-run ue_dump_umg_properties.py"
                          % (where, cls))
    owner = cls
    for i, part in enumerate(parts):
        if part not in fields:
            hint = difflib.get_close_matches(part, list(fields), n=3)
            if i == 0:
                hint = _where_else(cls, part) + hint
            raise MarkupError("%s: %s has no property %s%s"
                              % (where, owner, part,
                                 " (did you mean %s?)" % " or ".join(hint) if hint else ""))
        struct = fields[part].get("struct")
        if i + 1 < len(parts):
            if struct is None:
                raise MarkupError("%s: %s.%s is not a struct" % (where, owner, part))
            owner, fields = struct, props["structs"][struct]


# --------------------------------------------------------------------------
# values
# --------------------------------------------------------------------------

def default_for(cls, parts):
    """The engine default at a dotted path, or None when unknown."""
    d = umg_resolve.defaults()
    for c in (cls, "Widget"):
        value = d.get(c, {}).get(parts[0])
        if value is None:
            continue
        for p in parts[1:]:
            if not isinstance(value, dict) or p not in value:
                return None
            value = value[p]
        return value
    return None


def srgb_to_linear(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def parse_color(raw):
    m = HEX_RE.match(raw)
    if m:
        h = m.group("hex")
        rgb = [srgb_to_linear(int(h[i:i + 2], 16)) for i in (0, 2, 4)]
        alpha = int(h[6:8], 16) / 255.0 if len(h) == 8 else 1.0
        return dict(zip(COLOR_FIELDS, [round(v, 6) for v in rgb] + [round(alpha, 6)]))
    m = LINEAR_RE.match(raw)
    if m:
        vals = m.group("body").replace(",", " ").split()
        if len(vals) != 4:
            raise MarkupError("linear() takes r g b a, got %r" % raw)
        return dict(zip(COLOR_FIELDS, [float(v) for v in vals]))
    return None


def numbers(raw, counts, what):
    parts = raw.split()
    if len(parts) not in counts or not all(NUM_RE.match(p) for p in parts):
        raise MarkupError("%s expects %s numbers, got %r"
                          % (what, " or ".join(map(str, counts)), raw))
    return [float(p) for p in parts]


def is_margin(v):
    return isinstance(v, dict) and set(v) <= set(MARGIN_FIELDS) and len(v) == 4


def is_vec2(v):
    return isinstance(v, dict) and set(v) == {"X", "Y"}


def is_vec4(v):
    return isinstance(v, dict) and set(v) == set(VEC4_FIELDS)


def is_color(v):
    return isinstance(v, dict) and set(v) == set(COLOR_FIELDS)


def is_slate_color(v):
    return isinstance(v, dict) and "SpecifiedColor" in v


def is_size(v):
    return isinstance(v, dict) and "SizeRule" in v


def expand_enum(prop, raw, where):
    literals = ENUMS.get(prop)
    if literals is None:
        return raw
    if raw in literals:
        return raw
    want = raw.replace("_", "").lower()
    hits = [l for l in literals if l.replace("_", "").lower().endswith(want)]
    if len(hits) != 1:
        raise MarkupError("%s: %r is not a %s (one of %s)"
                          % (where, raw, prop, ", ".join(literals)))
    return hits[0]


def parse_literal(raw):
    """A value whose property has no known default: infer from its spelling."""
    color = parse_color(raw)
    if color is not None:
        return color
    m = REF_RE.match(raw)
    if m:
        return {"__ref__": m.group("path"), "__class__": m.group("cls")}
    if raw in ("true", "false"):
        return raw == "true"
    if NUM_RE.match(raw):
        return float(raw) if any(c in raw for c in ".eE") else int(raw)
    return raw


def parse_value(cls, parts, raw, where):
    """Markup text -> JSON value for the property at `parts` on class `cls`."""
    raw = raw.strip()
    prop = parts[-1]
    if len(parts) == 1 and prop in TEXT_PROPERTIES:
        return {"__text__": raw}
    default = default_for(cls, parts)

    m = REF_RE.match(raw)
    if m:
        return {"__ref__": m.group("path"), "__class__": m.group("cls")}
    color = parse_color(raw)
    if color is not None:
        if is_slate_color(default):
            return {"SpecifiedColor": color, "ColorUseRule": SPECIFIED_RULE}
        return color

    if default is None:
        if prop in ENUMS:
            return expand_enum(prop, raw, where)
        return parse_literal(raw)
    if isinstance(default, dict):
        if "__enum__" in default:
            return expand_enum(prop, raw, where)
        if "__ref__" in default:
            return {"__ref__": raw, "__class__": default["__class__"]}
        if is_margin(default):
            v = numbers(raw, (1, 2, 4), where)
            if len(v) == 1:
                v = v * 4
            elif len(v) == 2:
                v = [v[0], v[1], v[0], v[1]]
            return dict(zip(MARGIN_FIELDS, v))
        if is_vec2(default):
            v = numbers(raw, (1, 2), where)
            return {"X": v[0], "Y": v[-1]}
        if is_vec4(default):
            v = numbers(raw, (1, 4), where)
            return dict(zip(VEC4_FIELDS, v * 4 if len(v) == 1 else v))
        if is_size(default):
            words = raw.split()
            if len(words) == 1 and words[0].lower() in SIZE_AUTO_WORDS:
                return {"SizeRule": "Automatic"}
            if words and words[0].lower() == "fill" and len(words) <= 2:
                out = {"SizeRule": "Fill"}
                if len(words) == 2:
                    out["Value"] = numbers(words[1], (1,), where)[0]
                return out
            raise MarkupError("%s: size is Auto, Fill or 'Fill <ratio>', got %r" % (where, raw))
        raise MarkupError("%s is a struct (%s); set its fields as %s.<Field>"
                          % (where, ", ".join(sorted(default)), ".".join(parts)))
    if isinstance(default, bool):
        if raw not in ("true", "false"):
            raise MarkupError("%s expects true or false, got %r" % (where, raw))
        return raw == "true"
    if isinstance(default, float):
        return numbers(raw, (1,), where)[0]
    if isinstance(default, int):
        if not re.match(r"^[+-]?\d+$", raw):
            raise MarkupError("%s expects an integer, got %r" % (where, raw))
        return int(raw)
    return raw


def set_path(target, parts, value, where):
    node = target
    for p in parts[:-1]:
        existing = node.get(p)
        if existing is None:
            existing = node[p] = {}
        elif not isinstance(existing, dict) or "__text__" in existing or "__ref__" in existing:
            raise MarkupError("%s: %s is not a struct" % (where, p))
        node = existing
    node[parts[-1]] = value


# --------------------------------------------------------------------------
# stylesheet
# --------------------------------------------------------------------------

def parse_styles(text):
    """-> (tokens, rules). rules: [(specificity, order, selector, [(prop, raw)])]."""
    text = re.sub(r"/\*.*?\*/", "", text or "", flags=re.S)
    tokens, rules = {}, []
    pos = 0
    for m in re.finditer(r"([^{}]+)\{([^{}]*)\}", text):
        if text[pos:m.start()].strip():
            raise MarkupError("stylesheet: cannot parse %r" % text[pos:m.start()].strip()[:60])
        pos = m.end()
        selectors = [s.strip() for s in m.group(1).split(",")]
        decls = []
        for d in m.group(2).split(";"):
            if not d.strip():
                continue
            if ":" not in d:
                raise MarkupError("stylesheet: %r is not 'property: value'" % d.strip())
            prop, _, raw = d.partition(":")
            decls.append((prop.strip(), raw.strip()))
        if selectors == [":root"]:
            for prop, raw in decls:
                if not TOKEN_RE.match(prop):
                    raise MarkupError("stylesheet: :root holds --tokens only, got %r" % prop)
                tokens[prop] = raw
            continue
        for prop, _ in decls:
            if not PROP_RE.match(prop):
                raise MarkupError("stylesheet: bad property name %r" % prop)
        for sel in selectors:
            sm = SELECTOR_RE.match(sel)
            if not sel or not sm:
                raise MarkupError("stylesheet: unsupported selector %r (type, .class, #name only)"
                                  % sel)
            parts = re.findall(r"[.#][A-Za-z_][\w-]*", sm.group("rest"))
            selector = {"type": sm.group("type"),
                        "classes": {p[1:] for p in parts if p[0] == "."},
                        "ids": {p[1:] for p in parts if p[0] == "#"}}
            spec = (len(selector["ids"]), len(selector["classes"]),
                    1 if selector["type"] else 0)
            rules.append((spec, len(rules), selector, decls))
    if text[pos:].strip():
        raise MarkupError("stylesheet: cannot parse %r" % text[pos:].strip()[:60])
    return tokens, rules


def resolve_tokens(raw, tokens, where, depth=0):
    if depth > MAX_TOKEN_DEPTH:
        raise MarkupError("%s: tokens refer to each other in a loop" % where)

    def sub(m):
        name = m.group(1)
        if name not in tokens:
            raise MarkupError("%s: undefined token %s" % (where, name))
        return resolve_tokens(tokens[name], tokens, where, depth + 1)

    return VAR_RE.sub(sub, raw)


def theme_token(raw, tokens, where, depth=0):
    """The theme token a value *is* (alias chains followed), or None. A theme token
    inside a larger value cannot be re-applied at runtime, so it is refused."""
    if depth > MAX_TOKEN_DEPTH:
        raise MarkupError("%s: tokens refer to each other in a loop" % where)
    m = WHOLE_VAR_RE.match(raw)
    if m:
        name = m.group(1)
        if name.startswith(THEME_PREFIX):
            token = name[len(THEME_PREFIX):]
            if token not in THEME_TOKENS:
                raise MarkupError("%s: %s is not a theme token (%s)"
                                  % (where, name, ", ".join(sorted(THEME_TOKENS))))
            return token
        if name not in tokens:
            raise MarkupError("%s: undefined token %s" % (where, name))
        return theme_token(tokens[name], tokens, where, depth + 1)
    for name in VAR_RE.findall(raw):
        if name.startswith(THEME_PREFIX) or (name in tokens
                                             and theme_token(tokens[name], tokens, where, depth + 1)):
            raise MarkupError("%s: theme token %s must be the whole value, not part of %r"
                              % (where, name, raw))
    return None


def matches(selector, cls, name, classes):
    return ((selector["type"] is None or selector["type"] == cls)
            and selector["classes"] <= classes
            and all(i == name for i in selector["ids"]))


# --------------------------------------------------------------------------
# compile
# --------------------------------------------------------------------------

class Compiler:

    def __init__(self, asset, tokens, rules):
        self.asset = asset
        self.tokens = tokens
        self.rules = sorted(rules, key=lambda r: (r[0], r[1]))
        # Classes of typed rules (Border.focused) are states the game switches at runtime.
        self.state_classes = {c for _, _, sel, _ in rules if sel["type"] and not sel["ids"] for c in sel["classes"]}
        self.used = set()
        self.counters = {}
        # widget name -> {property path: theme token}, for runtime theming
        self.bindings = {}
        # widget name -> style classes from the markup
        self.base_classes = {}

    def reserve_names(self, el):
        for e in el.iter():
            name = e.get("name")
            if name is None:
                continue
            if name in self.used:
                raise MarkupError("widget name %r is used twice" % name)
            self.used.add(name)

    def auto_name(self, cls):
        while True:
            n = self.counters.get(cls, 0)
            self.counters[cls] = n + 1
            name = "%s_%d" % (cls, n)
            if name not in self.used:
                self.used.add(name)
                return name

    def node(self, el, parent_cls):
        cls = el.tag
        info = schema().get(cls)
        if info is None:
            raise MarkupError("<%s>: not a UMG widget class in tools/umg_schema.json" % cls)
        name = el.get("name") or self.auto_name(cls)
        where = "%s (%s)" % (name, cls)
        classes = set((el.get("class") or "").split())
        baked = sorted(classes & self.state_classes)
        if baked:
            raise MarkupError("%s: %s is a runtime state (a Type.class rule); switch it from code - "
                              "baked into the asset it could never be switched off" % (where, ", ".join(baked)))

        decls = []
        for _, _, selector, rule_decls in self.rules:
            if matches(selector, cls, name, classes):
                decls.extend(rule_decls)
        for attr, raw in el.attrib.items():
            if attr not in RESERVED:
                decls.append((attr, raw))

        props, slot_props = {}, {}
        slot_cls = schema()[parent_cls]["slotClass"] if parent_cls else None
        if classes:
            self.base_classes[name] = sorted(classes)
        bound = self.bindings.setdefault(name, {})
        for path, raw in decls:
            token = theme_token(raw, self.tokens, where)
            for key in [k for k in bound if k == path or k.startswith(path + ".")]:
                del bound[key]
            if token:
                bound[path] = token
            raw = resolve_tokens(raw, self.tokens, where)
            if path.startswith(SLOT_PREFIX):
                if slot_cls is None:
                    raise MarkupError("%s: the root widget has no slot (%s)" % (where, path))
                parts = path[len(SLOT_PREFIX):].split(".")
                check_path(slot_cls, parts, where)
                set_path(slot_props, parts, parse_value(slot_cls, parts, raw, where + " " + path),
                         where)
            elif path.endswith(KEY_SUFFIX) and path[:-len(KEY_SUFFIX)] in TEXT_PROPERTIES:
                prop = path[:-len(KEY_SUFFIX)]
                if not isinstance(props.get(prop), dict):
                    raise MarkupError("%s: %s set before %s" % (where, path, prop))
                props[prop]["__key__"] = raw
            else:
                parts = path.split(".")
                check_path(cls, parts, where)
                set_path(props, parts, parse_value(cls, parts, raw, where + " " + path), where)

        text = el.text if el.text is not None and el.text.strip() else None
        if text is not None:
            prop = TEXT_CONTENT.get(cls)
            if prop is None:
                raise MarkupError("%s: only %s take text content" % (where, ", ".join(TEXT_CONTENT)))
            props[prop] = {"__text__": text}
        if el.get("key") is not None:
            prop = TEXT_CONTENT.get(cls)
            if prop is None or prop not in props:
                raise MarkupError("%s: key= needs text content" % where)
            props[prop]["__key__"] = el.get("key")

        kids = list(el)
        for k in kids:
            if k.tail and k.tail.strip():
                raise MarkupError("%s: stray text %r between children" % (where, k.tail.strip()))
        max_children = info["maxChildren"]
        if max_children == 0 and kids:
            raise MarkupError("%s takes no children" % where)
        if max_children > 0 and len(kids) > max_children:
            raise MarkupError("%s takes at most %d child" % (where, max_children))

        if not bound:
            del self.bindings[name]
        out = {"name": name, "class": cls, "properties": props}
        if kids:
            out["children"] = [self.node(k, cls) for k in kids]
            if cls in MIRRORED:
                mirror(out, where)
        if slot_cls:
            out["slot"] = {"class": slot_cls, "properties": slot_props}
        return out


def mirror(node, where):
    """Make a Border's own Padding/alignment and its child's slot agree."""
    props = node["properties"]
    slot = node["children"][0]["slot"]["properties"]
    for key in MIRRORED_PROPS:
        if key in props and key in slot:
            if props[key] != slot[key]:
                raise MarkupError("%s: %s is %r on the widget but %r on its child's slot - "
                                  "set it in one place" % (where, key, props[key], slot[key]))
        elif key in props:
            slot[key] = props[key]
        elif key in slot:
            props[key] = slot[key]


def _count(node):
    return 1 + sum(_count(c) for c in node.get("children", []))


def _slots(node):
    return (1 if node.get("slot") else 0) + sum(_slots(c) for c in node.get("children", []))


def compile_markup(markup, styles=None, base_dir=None):
    return _compile(markup, styles, base_dir)[0]


def _compile(markup, styles=None, base_dir=None):
    """-> (widget JSON, the Compiler that built it, the stylesheet's id or None)."""
    try:
        top = ET.fromstring(markup)
    except ET.ParseError as exc:
        raise MarkupError("markup is not well-formed XML: %s" % exc)
    if top.tag != "Widget":
        raise MarkupError("the document element must be <Widget>, not <%s>" % top.tag)
    for attr in ("asset", "parent"):
        if not top.get(attr):
            raise MarkupError("<Widget> needs %s=" % attr)
    sheet = None
    if top.get("styles"):
        sheet = os.path.basename(top.get("styles")).split(".")[0]
    if styles is None and top.get("styles"):
        if base_dir is None:
            raise MarkupError("styles=%r needs the markup's directory" % top.get("styles"))
        with open(os.path.join(base_dir, top.get("styles")), "r", encoding="utf-8") as fh:
            styles = fh.read()
    roots = list(top)
    if len(roots) != 1:
        raise MarkupError("<Widget> holds exactly one root widget, found %d" % len(roots))

    tokens, rules = parse_styles(styles)
    compiler = Compiler(top.get("asset"), tokens, rules)
    compiler.reserve_names(roots[0])
    root = compiler.node(roots[0], None)
    count = _count(root)
    doc = {
        "$schema": SCHEMA_ID,
        "asset": top.get("asset"),
        "parentClass": top.get("parent"),
        "root": root,
        "_stats": {"widgetsReachable": count, "widgetsInTree": count, "slots": _slots(root)},
    }
    return doc, compiler, sheet


# --------------------------------------------------------------------------
# runtime styles
# --------------------------------------------------------------------------

def to_t3d(value, nested=False):
    """A JSON property value as the text FProperty::ImportText reads."""
    if isinstance(value, dict) and "__text__" in value:
        literal = (value["__text__"].replace("\\", "\\\\").replace('"', '\\"')
                   .replace("\r", "\\r").replace("\n", "\\n"))
        if value.get("__key__"):
            return 'NSLOCTEXT("", "%s", "%s")' % (value["__key__"], literal)
        return 'INVTEXT("%s")' % literal
    if isinstance(value, dict) and "__ref__" in value:
        ref = "%s'%s'" % (value["__class__"], value["__ref__"])
        return '"%s"' % ref if nested else ref
    if isinstance(value, dict):
        return "(%s)" % ",".join("%s=%s" % (k, to_t3d(v, True)) for k, v in value.items())
    if isinstance(value, bool):
        return "True" if value else "False"
    if isinstance(value, float):
        return "%f" % value
    if isinstance(value, int):
        return str(value)
    return str(value)


def runtime_rules(sheet, styles):
    """Rules a game can switch on at runtime: a type plus at least one class, no #name.
    The type makes each value typeable now, so the game only imports finished text."""
    tokens, rules = parse_styles(styles)
    out = []
    for spec, order, selector, decls in rules:
        if not selector["type"] or not selector["classes"] or selector["ids"]:
            continue
        cls = selector["type"]
        where = "%s: %s.%s" % (sheet, cls, ".".join(sorted(selector["classes"])))
        if cls not in schema():
            raise MarkupError("%s: not a UMG widget class" % where)
        out_decls = []
        for path, raw in decls:
            if path.startswith(SLOT_PREFIX):
                raise MarkupError("%s: a runtime rule cannot set %s - the slot depends on the parent"
                                  % (where, path))
            parts = path.split(".")
            check_path(cls, parts, where)
            token = theme_token(raw, tokens, where)
            if token:
                out_decls.append((path, None, token))
            else:
                value = parse_value(cls, parts, resolve_tokens(raw, tokens, where), where + " " + path)
                out_decls.append((path, to_t3d(value), None))
        out.append({"sheet": sheet, "type": cls, "classes": sorted(selector["classes"]),
                    "specificity": spec[0] * 10000 + spec[1] * 100 + spec[2], "order": order,
                    "decls": out_decls})
    return out


def collect_runtime_styles(markup_paths):
    """Theme bindings, markup classes and runtime rules of a set of markup files."""
    assets, rules, sheets_seen = [], [], set()
    for path in sorted(markup_paths):
        base_dir = os.path.dirname(os.path.abspath(path))
        with open(path, "r", encoding="utf-8") as fh:
            markup = fh.read()
        doc, compiler, sheet = _compile(markup, base_dir=base_dir)
        assets.append({"asset": doc["asset"], "sheet": sheet,
                       "bindings": sorted((w, p, t) for w, paths in compiler.bindings.items()
                                          for p, t in paths.items()),
                       "classes": sorted(compiler.base_classes.items())})
        if sheet and sheet not in sheets_seen:
            sheets_seen.add(sheet)
            styles_path = os.path.join(base_dir, ET.fromstring(markup).get("styles"))
            with open(styles_path, "r", encoding="utf-8") as fh:
                rules.extend(runtime_rules(sheet, fh.read()))
    return assets, rules


def cpp_string(s):
    if s is None:
        return "nullptr"
    return 'TEXT("%s")' % s.replace("\\", "\\\\").replace('"', '\\"')


def render_styles_inl(assets, rules):
    """C++ tables for FBH_WidgetStyler (Private/BH_WidgetStyler.cpp includes them)."""
    lines = [COPYRIGHT,
             "// Generated by tools/umg_markup.py styles from widgets/*.umg.xml and their stylesheets.",
             "// Do not edit: change the markup or stylesheet and regenerate.",
             "", "static const FBH_StyleDecl GBH_StyleDecls[] = {"]
    first, decl_index = [], 0
    for rule in rules:
        first.append(decl_index)
        for path, value, token in rule["decls"]:
            lines.append("    {%s, %s, %s}," % (cpp_string(path), cpp_string(value), cpp_string(token)))
            decl_index += 1
    lines += ["    {nullptr, nullptr, nullptr},", "};", "", "static const FBH_StyleRule GBH_StyleRules[] = {"]
    for rule, start in zip(rules, first):
        lines.append("    {%s, %s, %s, %d, %d, %d, %d}," % (
            cpp_string(rule["sheet"]), cpp_string(rule["type"]), cpp_string(" ".join(rule["classes"])),
            rule["specificity"], rule["order"], start, len(rule["decls"])))
    lines += ["    {nullptr, nullptr, nullptr, 0, 0, 0, 0},", "};", "",
              "static const FBH_ThemeBinding GBH_ThemeBindings[] = {"]
    for a in assets:
        for widget, path, token in a["bindings"]:
            lines.append("    {%s, %s, %s, %s}," % (cpp_string(a["asset"]), cpp_string(widget),
                                                    cpp_string(path), cpp_string(token)))
    lines += ["    {nullptr, nullptr, nullptr, nullptr},", "};", "",
              "static const FBH_BaseClasses GBH_BaseClasses[] = {"]
    for a in assets:
        for widget, classes in a["classes"]:
            lines.append("    {%s, %s, %s}," % (cpp_string(a["asset"]), cpp_string(widget),
                                                cpp_string(" ".join(classes))))
    lines += ["    {nullptr, nullptr, nullptr},", "};", "",
              "static const FBH_AssetSheet GBH_AssetSheets[] = {"]
    for a in assets:
        lines.append("    {%s, %s}," % (cpp_string(a["asset"]), cpp_string(a["sheet"])))
    lines += ["    {nullptr, nullptr},", "};", ""]
    return "\n".join(lines)


def styles_inl(directory):
    paths = glob.glob(os.path.join(directory, "*" + MARKUP_SUFFIX))
    return render_styles_inl(*collect_runtime_styles(paths))


def compile_file(path):
    with open(path, "r", encoding="utf-8") as fh:
        return compile_markup(fh.read(), base_dir=os.path.dirname(os.path.abspath(path)))


# --------------------------------------------------------------------------
# decompile
# --------------------------------------------------------------------------

def fmt_num(v):
    return repr(float(v)) if isinstance(v, float) else str(v)


def fmt_short(v):
    """20.0 -> '20'. Only offered where the property is typed float, and kept
    only if it compiles back to the same value."""
    return str(int(v)) if isinstance(v, float) and v.is_integer() else fmt_num(v)


def fmt_color(c, fmt):
    return "linear(%s)" % " ".join(fmt(c[k]) for k in COLOR_FIELDS)


def short_enum(prop, value):
    literals = ENUMS.get(prop, [])
    if value not in literals:
        return None
    tail = value.split("_")[-1]
    hits = [l for l in literals if l.replace("_", "").lower().endswith(tail.lower())]
    return tail if hits == [value] else None


def shorthand(cls, parts, value):
    """A single string that compiles back to exactly `value`, or None.
    Candidates run from most to least compact; the first that round-trips wins."""
    candidates = []
    for fmt in (fmt_short, fmt_num):
        if is_margin(value):
            l, t, r, b = (value[k] for k in MARGIN_FIELDS)
            if l == t == r == b:
                candidates.append(fmt(l))
            elif l == r and t == b:
                candidates.append("%s %s" % (fmt(l), fmt(t)))
            candidates.append(" ".join(fmt(v) for v in (l, t, r, b)))
        if is_vec2(value):
            if value["X"] == value["Y"]:
                candidates.append(fmt(value["X"]))
            candidates.append("%s %s" % (fmt(value["X"]), fmt(value["Y"])))
        if is_vec4(value):
            vals = [value[k] for k in VEC4_FIELDS]
            if len(set(vals)) == 1:
                candidates.append(fmt(vals[0]))
            candidates.append(" ".join(fmt(v) for v in vals))
        if is_color(value):
            candidates.append(fmt_color(value, fmt))
        if is_slate_color(value) and is_color(value["SpecifiedColor"]):
            candidates.append(fmt_color(value["SpecifiedColor"], fmt))
        if is_size(value):
            words = [value["SizeRule"]] + ([fmt(value["Value"])] if "Value" in value else [])
            candidates.append(" ".join(words))
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            candidates.append(fmt(value))
    if isinstance(value, dict) and "__ref__" in value:
        candidates.append("ref(%s'%s')" % (value["__class__"], value["__ref__"]))
    if isinstance(value, bool):
        candidates.append("true" if value else "false")
    elif isinstance(value, str):
        short = short_enum(parts[-1], value)
        if short:
            candidates.append(short)
        candidates.append(value)
    for c in candidates:
        try:
            if parse_value(cls, parts, c, "decompile") == value:
                return c
        except MarkupError:
            continue
    return None


def flatten(cls, parts, value, out):
    """(path, text) pairs that compile back to `value` at `parts`."""
    if len(parts) == 1 and parts[0] in TEXT_PROPERTIES and isinstance(value, dict):
        out.append((parts[0], value["__text__"]))
        if "__key__" in value:
            out.append((parts[0] + KEY_SUFFIX, value["__key__"]))
        return
    s = shorthand(cls, parts, value)
    if s is not None:
        out.append((".".join(parts), s))
        return
    if isinstance(value, dict) and not any(k.startswith("__") for k in value) and value:
        for k, v in value.items():
            flatten(cls, parts + [k], v, out)
        return
    raise MarkupError("cannot express %s.%s = %r in markup" % (cls, ".".join(parts), value))


def attr_escape(s):
    return xml_escape(s, {'"': "&quot;", "\n": "&#10;", "\r": "&#13;", "\t": "&#9;"})


def text_escape(s):
    return xml_escape(s, {"\r": "&#13;"})


def decompile_node(node, depth, lines):
    cls = node["class"]
    pad = "  " * depth
    attrs = [("name", node["name"])]
    props = dict(node.get("properties") or {})
    content = None
    text_prop = TEXT_CONTENT.get(cls)
    if text_prop and isinstance(props.get(text_prop), dict) and "__text__" in props[text_prop]:
        text = props.pop(text_prop)
        if text["__text__"].strip():
            content = text["__text__"]
            if "__key__" in text:
                attrs.append(("key", text["__key__"]))
        else:
            props[text_prop] = text
    pairs = []
    for k, v in props.items():
        flatten(cls, [k], v, pairs)
    slot = node.get("slot")
    if slot:
        slot_pairs = []
        for k, v in (slot.get("properties") or {}).items():
            flatten(slot["class"], [k], v, slot_pairs)
        pairs += [(SLOT_PREFIX + p, v) for p, v in slot_pairs]
    attrs += pairs

    rendered = ['%s="%s"' % (k, attr_escape(v)) for k, v in attrs]
    head = "%s<%s %s" % (pad, cls, " ".join(rendered))
    if len(head) > LINE_WRAP:
        head = ("%s<%s %s" % (pad, cls, rendered[0])
                + "".join("\n%s    %s" % (pad, r) for r in rendered[1:]))
    kids = node.get("children") or []
    if content is not None:
        lines.append("%s>%s</%s>" % (head, text_escape(content), cls))
    elif kids:
        lines.append(head + ">")
        for k in kids:
            decompile_node(k, depth + 1, lines)
        lines.append("%s</%s>" % (pad, cls))
    else:
        lines.append(head + "/>")


def decompile(doc):
    """Widget JSON -> markup. Verified: the result compiles back to `doc`."""
    lines = ['<?xml version="1.0" encoding="utf-8"?>',
             '<Widget asset="%s" parent="%s">' % (attr_escape(doc["asset"]),
                                                  attr_escape(doc.get("parentClass") or ""))]
    decompile_node(doc["root"], 1, lines)
    lines.append("</Widget>")
    markup = "\n".join(lines) + "\n"
    if compile_markup(markup)["root"] != doc["root"]:
        raise MarkupError("decompiled markup does not reproduce %s - report this" % doc["asset"])
    return markup


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def json_path_for(markup_path):
    return markup_path[:-len(MARKUP_SUFFIX)] + ".json"


def write_json(doc, path):
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(doc, fh, indent=2, ensure_ascii=False)
        fh.write("\n")


def check(directory, styles_out=None):
    """Every markup source compiles to exactly its committed JSON, and the generated
    style tables match the markup."""
    stale = []
    if styles_out and glob.glob(os.path.join(directory, "*" + MARKUP_SUFFIX)):
        expected = styles_inl(directory)
        if not os.path.exists(styles_out):
            stale.append("%s is missing - run: umg_markup.py styles" % styles_out)
        else:
            with open(styles_out, "r", encoding="utf-8") as fh:
                if fh.read() != expected:
                    stale.append("%s is out of date - run: umg_markup.py styles" % styles_out)
    for path in sorted(glob.glob(os.path.join(directory, "*" + MARKUP_SUFFIX))):
        target = json_path_for(path)
        compiled = compile_file(path)
        if not os.path.exists(target):
            stale.append("%s: no %s" % (path, os.path.basename(target)))
            continue
        with open(target, "r", encoding="utf-8") as fh:
            if json.load(fh) != compiled:
                stale.append("%s: %s is out of date" % (path, os.path.basename(target)))
    return stale


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("compile", help="markup -> widget JSON")
    c.add_argument("markup")
    c.add_argument("-o", "--out", help="default: the .json beside the markup")
    d = sub.add_parser("decompile", help="widget JSON -> markup")
    d.add_argument("json")
    d.add_argument("-o", "--out", help="default: stdout")
    k = sub.add_parser("check", help="every *.umg.xml matches its committed .json")
    k.add_argument("--dir", default=WIDGETS_DIR)
    st = sub.add_parser("styles", help="runtime style tables for the C++ styler")
    st.add_argument("--dir", default=WIDGETS_DIR)
    st.add_argument("-o", "--out", default=DEFAULT_STYLES_OUT)
    args = ap.parse_args(argv)

    try:
        if args.cmd == "compile":
            if not args.markup.endswith(MARKUP_SUFFIX) and not args.out:
                raise MarkupError("name the markup *%s or pass -o" % MARKUP_SUFFIX)
            out = args.out or json_path_for(args.markup)
            write_json(compile_file(args.markup), out)
            print("wrote %s" % out, file=sys.stderr)
        elif args.cmd == "decompile":
            with open(args.json, "r", encoding="utf-8") as fh:
                markup = decompile(json.load(fh))
            if args.out:
                with open(args.out, "w", encoding="utf-8") as fh:
                    fh.write(markup)
                print("wrote %s" % args.out, file=sys.stderr)
            else:
                sys.stdout.write(markup)
        elif args.cmd == "styles":
            os.makedirs(os.path.dirname(args.out), exist_ok=True)
            with open(args.out, "w", encoding="utf-8") as fh:
                fh.write(styles_inl(args.dir))
            print("wrote %s" % args.out, file=sys.stderr)
        else:
            stale = check(args.dir, DEFAULT_STYLES_OUT if args.dir == WIDGETS_DIR else None)
            for s in stale:
                print(s)
            if stale:
                return 1
            print("all markup up to date", file=sys.stderr)
    except MarkupError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
