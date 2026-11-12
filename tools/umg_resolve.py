#!/usr/bin/env python3
"""Resolve a widget JSON source into a fully specified layout tree.

The widget JSON holds only what differs from the class defaults - Unreal's
.T3D export omits the rest, down to single struct fields. This module fills
every gap from tools/umg_defaults.json (dumped from the engine by
ue_dump_umg_defaults.py) and turns the result into the plain tree that
tools/preview/slate_layout.js lays out: canonical enums, merged paddings,
fonts with the metrics Slate itself would compute.

Nothing here guesses. A widget class, enum or font the preview cannot model
raises UnsupportedWidget instead of rendering something plausible.
"""

import json
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
DEFAULTS_PATH = os.path.join(HERE, "umg_defaults.json")
# The plugin's own fonts: the manifest ue_import_fonts.py builds Content/Fonts from.
FONT_MANIFEST = os.path.join(REPO, "widgets", "fonts", "fonts.json")

# Slate rasterises fonts at 96 DPI while FSlateFontInfo.Size is in points.
SLATE_FONT_DPI = 96.0
POINTS_PER_INCH = 72.0
# OpenType head.flags bit 3: "force ppem to integer values". FreeType honours it
# and so the pixel size Slate lays out with is rounded.
HEAD_FLAG_INTEGER_PPEM = 1 << 3
# LetterSpacing is in thousandths of the font's point size (SlateTextShaper.cpp).
LETTER_SPACING_UNITS = 1000.0

ENGINE_DEFAULT_FONT = "/Engine/EngineFonts/Roboto.Roboto"
# The engine's own Roboto (Engine/Content/Slate/Fonts), so advances match Slate's.
ENGINE_FONT_FILES = {
    (ENGINE_DEFAULT_FONT, "Regular"): "tools/preview_fonts/Roboto-Regular.ttf",
    (ENGINE_DEFAULT_FONT, "Bold"): "tools/preview_fonts/Roboto-Bold.ttf",
}

_font_files = None


def font_files():
    """(font object path, typeface) -> TTF path relative to the repo."""
    global _font_files
    if _font_files is None:
        files = dict(ENGINE_FONT_FILES)
        with open(FONT_MANIFEST, "r", encoding="utf-8") as fh:
            manifest = json.load(fh)
        base = os.path.relpath(os.path.dirname(FONT_MANIFEST), REPO)
        for entry in manifest["fonts"]:
            asset = entry["asset"]
            obj = "%s.%s" % (asset, asset.rsplit("/", 1)[-1])
            for typeface, ttf in entry["typefaces"].items():
                files[(obj, typeface)] = os.path.join(base, ttf)
        _font_files = files
    return _font_files

# Editable text reserves room for the caret on both sides of its text
# (FSlateEditableTextLayout::CacheDesiredSize): CaretWidthPercent of the font's
# max height, clamped (FTextEditHelper::CalculateCaretWidth).
CARET_WIDTH_PERCENT = 0.08
CARET_WIDTH_MIN = 1.0
CARET_WIDTH_MAX = 2.0
# Hint text is the text colour at this opacity (SlateEditableTextLayout.cpp).
HINT_TEXT_OPACITY = 0.35

# Default style images are engine textures, so the preview cannot read their
# colour from the asset. These are measured from the textures in
# Engine/Content/Slate/Common and from UE 5.3 renders.
TEXTBOX_IMAGE_ALPHA = 204 / 255.0      # TextBox.png body
TEXTBOX_IMAGE_RADIUS = 3.0             # TextBox.png corner fade
CHECKBOX_UNCHECKED_ALPHA = 179 / 255.0  # CheckBox.png body
CHECKBOX_CHECKMARK_ALPHA = 64 / 255.0   # CheckBox_Checked_Hovered.png tick
CHECKBOX_IMAGE_INSET = 1.0             # both images leave a 1px transparent rim
CHECKBOX_IMAGE_RADIUS = 2.0
# Check images are tinted by the inherited foreground; renders show #C0C0C0.
INHERITED_FOREGROUND = "rgb(192,192,192)"

# ExpandableArea's collapsed header is wider than the dumped defaults explain:
# measured 21px on UE 5.3 (BugReportForm.ExpandableArea_227) against 18px from
# HeaderPadding plus the arrow image.
EXPANDABLE_HEADER_EXTRA_WIDTH = 3.0
# The NoBorder button style around the header pads 1px below its content.
EXPANDABLE_HEADER_EXTRA_HEIGHT = 1.0

H_ALIGN = {"halignfill": "fill", "halignleft": "left",
           "haligncenter": "center", "halignright": "right"}
V_ALIGN = {"valignfill": "fill", "valigntop": "top",
           "valigncenter": "center", "valignbottom": "bottom"}
VISIBILITY = {"visible": "visible", "collapsed": "collapsed", "hidden": "hidden",
              "hittestinvisible": "visible", "selfhittestinvisible": "visible"}
SIZE_RULE = {"automatic": "auto", "fill": "fill"}
JUSTIFY = {"left": "left", "center": "center", "right": "right"}
CHECKED = {"unchecked": False, "checked": True, "undetermined": False}

PANEL_KINDS = {"CanvasPanel": "canvas", "VerticalBox": "vbox",
               "HorizontalBox": "hbox", "Overlay": "overlay",
               "ScrollBox": "scrollbox", "UniformGridPanel": "ugrid"}
SINGLE_CHILD_KINDS = {"Border": "border", "Button": "button",
                      "SizeBox": "sizebox", "CheckBox": "checkbox",
                      "BackgroundBlur": "blur"}
LEAF_KINDS = {"TextBlock": "text", "Spacer": "spacer", "Image": "image",
              "MultiLineEditableTextBox": "textbox",
              "EditableTextBox": "textbox", "ExpandableArea": "expandable",
              "EditableText": "edittext", "MultiLineEditableText": "edittext"}
SUPPORTED = set(PANEL_KINDS) | set(SINGLE_CHILD_KINDS) | set(LEAF_KINDS)


class UnsupportedWidget(ValueError):
    """The preview cannot model this faithfully, so it refuses."""


_defaults = None


def defaults():
    global _defaults
    if _defaults is None:
        with open(DEFAULTS_PATH, "r", encoding="utf-8") as fh:
            _defaults = json.load(fh)["classes"]
    return _defaults


def merge(base, override):
    """Overlay a partial value on its default. Missing struct fields keep the
    default - they are absent because they *equal* it, not because they are 0."""
    if isinstance(base, dict) and isinstance(override, dict) \
            and "__enum__" not in base and "__ref__" not in override:
        out = dict(base)
        for k, v in override.items():
            out[k] = merge(base.get(k), v)
        return out
    return override if override is not None else base


def prop(cls, props, name, *, chain=("Widget",)):
    """A property's effective value: the JSON value merged over the class default."""
    d = defaults()
    base = None
    for c in (cls,) + tuple(chain):
        if name in d.get(c, {}):
            base = d[c][name]
            break
    value = merge(base, (props or {}).get(name))
    if value is None:
        raise UnsupportedWidget("%s.%s has no value and no engine default - add it to "
                                "ue_dump_umg_defaults.py and re-dump" % (cls, name))
    return value


def canon(value, table, what):
    """Enum value (JSON 'HAlign_Fill' or dumped {'__enum__': 'H_ALIGN_FILL'}) -> preview token."""
    raw = value.get("__enum__") if isinstance(value, dict) else value
    key = str(raw).replace("_", "").lower()
    if key not in table:
        raise UnsupportedWidget("unknown %s value %r" % (what, raw))
    return table[key]


def margin(m):
    return [float(m.get(k, 0.0)) for k in ("Left", "Top", "Right", "Bottom")]


def vec2(v):
    return [float(v.get("X", 0.0)), float(v.get("Y", 0.0))]


# --------------------------------------------------------------------------
# colour
# --------------------------------------------------------------------------

def _to_srgb(c):
    c = max(0.0, min(1.0, float(c)))
    v = 12.92 * c if c <= 0.0031308 else 1.055 * (c ** (1 / 2.4)) - 0.055
    return int(round(max(0.0, min(1.0, v)) * 255))


def linear_color(value):
    """FLinearColor or FSlateColor -> [r, g, b, a] in linear space."""
    spec = value.get("SpecifiedColor", value) if isinstance(value, dict) else {}
    return [float(spec.get(k, 1.0 if k == "A" else 0.0)) for k in ("R", "G", "B", "A")]


def css_color(lin, tint=None):
    r, g, b, a = lin
    if tint:
        r, g, b, a = r * tint[0], g * tint[1], b * tint[2], a * tint[3]
    return "rgba(%d,%d,%d,%.3f)" % (_to_srgb(r), _to_srgb(g), _to_srgb(b), a)


def brush_paint(brush, tint=None):
    """What the preview draws for an FSlateBrush: fill, corner radii, outline."""
    draw_as = str(brush.get("DrawAs", "Image"))
    if draw_as == "NoDrawType":
        return None
    fill = css_color(linear_color(brush.get("TintColor", {})), tint)
    paint = {"fill": fill, "radius": [0, 0, 0, 0], "outline": None}
    if draw_as == "RoundedBox":
        o = brush.get("OutlineSettings") or {}
        r = o.get("CornerRadii") or {}
        paint["radius"] = [float(r.get(k, 0.0)) for k in ("X", "Y", "Z", "W")]
        if float(o.get("Width", 0.0)) > 0:
            paint["outline"] = {"width": float(o["Width"]),
                                "color": css_color(linear_color(o.get("Color", {})))}
    return paint


# --------------------------------------------------------------------------
# fonts
# --------------------------------------------------------------------------

_font_cache = {}


def _tables(path):
    with open(path, "rb") as fh:
        data = fh.read()
    count = struct.unpack(">H", data[4:6])[0]
    tables = {}
    for i in range(count):
        tag, _, off, length = struct.unpack(">4sIII", data[12 + 16 * i:28 + 16 * i])
        tables[tag.decode("latin-1")] = data[off:off + length]
    return tables


def font_metrics(path):
    """unitsPerEm, hhea ascent/descent/lineGap, the integer-ppem flag, and whether
    the face has a legacy 'kern' table. `path` is relative to the repo."""
    if path not in _font_cache:
        t = _tables(os.path.join(REPO, path))
        head, hhea = t["head"], t["hhea"]
        ascent, descent, line_gap = struct.unpack(">hhh", hhea[4:10])
        _font_cache[path] = {
            "unitsPerEm": struct.unpack(">H", head[18:20])[0],
            "flags": struct.unpack(">H", head[16:18])[0],
            "ascent": ascent, "descent": descent, "lineGap": line_gap,
            "kernTable": "kern" in t,
        }
    return _font_cache[path]


def _round_half_up(v):
    return int(v + 0.5)


def resolve_font(font):
    """FSlateFontInfo -> what layout needs. Mirrors FreeType + FSlateFontRenderer:
    ppem from points at 96 DPI (rounded for integer-ppem fonts), and a line height
    of round(face height * scale).

    Latin text takes Slate's KerningOnly shaping path (ETextShapingMethod::Auto):
    kerning comes from the legacy 'kern' table alone, never from GPOS, so a face
    without one is laid out unkerned. Letter spacing is LetterSpacing/1000 of the
    point size, truncated to whole pixels, between glyphs."""
    ref = font.get("FontObject")
    obj = ref.get("__ref__") if isinstance(ref, dict) else ENGINE_DEFAULT_FONT
    typeface = font.get("TypefaceFontName") or "Regular"
    path = font_files().get((obj, typeface))
    if path is None:
        raise UnsupportedWidget("no preview font for %s typeface %r - add it to "
                                "widgets/fonts/fonts.json" % (obj, typeface))
    m = font_metrics(path)
    points = float(font.get("Size", 24.0))
    ppem = points * SLATE_FONT_DPI / POINTS_PER_INCH
    if m["flags"] & HEAD_FLAG_INTEGER_PPEM:
        ppem = float(_round_half_up(ppem))
    face_height = m["ascent"] - m["descent"] + m["lineGap"]
    return {
        "file": path,
        "family": "bh-" + os.path.splitext(os.path.basename(path))[0],
        "ppem": ppem,
        "lineHeight": _round_half_up(face_height * ppem / m["unitsPerEm"]),
        "kerning": m["kernTable"],
        "letterSpacing": int(float(font.get("LetterSpacing", 0)) * points
                             / LETTER_SPACING_UNITS),
        "outline": float((font.get("OutlineSettings") or {}).get("OutlineSize", 0)),
    }


# --------------------------------------------------------------------------
# tree
# --------------------------------------------------------------------------

def text_of(value):
    if isinstance(value, dict):
        return value.get("__text__", "")
    return value if isinstance(value, str) else ""


def render_transform(node):
    """(RenderTransform, RenderTransformPivot) with defaults applied."""
    props = node.get("properties") or {}
    cls = node.get("class", "Widget")
    return prop(cls, props, "RenderTransform"), prop(cls, props, "RenderTransformPivot")


def resolve_slot(slot):
    if not slot:
        return None
    cls = slot["class"]
    props = slot.get("properties") or {}
    d = defaults().get(cls)
    if d is None:
        raise UnsupportedWidget("slot class %s has no engine defaults" % cls)
    out = {"class": cls}
    if cls == "CanvasPanelSlot":
        layout = prop(cls, props, "LayoutData", chain=())
        a = layout["Anchors"]
        out["canvas"] = {
            "offsets": margin(layout["Offsets"]),
            "anchors": vec2(a["Minimum"]) + vec2(a["Maximum"]),
            "alignment": vec2(layout["Alignment"]),
            "autoSize": bool(prop(cls, props, "bAutoSize", chain=())),
            "zOrder": int(prop(cls, props, "ZOrder", chain=())),
        }
        return out
    if cls == "UniformGridSlot":
        # No padding of its own: the panel's SlotPadding applies to every cell.
        out["row"] = int(prop(cls, props, "Row", chain=()))
        out["column"] = int(prop(cls, props, "Column", chain=()))
    else:
        out["padding"] = margin(prop(cls, props, "Padding", chain=()))
    out["hAlign"] = canon(prop(cls, props, "HorizontalAlignment", chain=()), H_ALIGN, "alignment")
    out["vAlign"] = canon(prop(cls, props, "VerticalAlignment", chain=()), V_ALIGN, "alignment")
    if "Size" in d or "Size" in props:
        size = merge(d.get("Size", {"SizeRule": "Automatic", "Value": 1.0}), props.get("Size"))
        out["sizeRule"] = canon(size.get("SizeRule", "Automatic"), SIZE_RULE, "size rule")
        out["sizeValue"] = float(size.get("Value", 1.0))
    return out


def resolve_node(node):
    cls = node["class"]
    if cls not in SUPPORTED:
        raise UnsupportedWidget("%s (%s): the preview has no layout model for this class"
                                % (node.get("name"), cls))
    props = node.get("properties") or {}
    transform, pivot = render_transform(node)
    out = {
        "name": node["name"],
        "class": cls,
        "kind": PANEL_KINDS.get(cls) or SINGLE_CHILD_KINDS.get(cls) or LEAF_KINDS[cls],
        "visibility": canon(prop(cls, props, "Visibility"), VISIBILITY, "visibility"),
        "transform": {"scale": vec2(transform["Scale"]), "shear": vec2(transform["Shear"]),
                      "angle": float(transform["Angle"]),
                      "translation": vec2(transform["Translation"])},
        "pivot": vec2(pivot),
        "slot": resolve_slot(node.get("slot")),
        "children": [resolve_node(c) for c in node.get("children") or []],
    }
    if out["kind"] in SINGLE_CHILD_KINDS.values() and len(out["children"]) > 1:
        raise UnsupportedWidget("%s (%s) holds %d children; it takes one"
                                % (node["name"], cls, len(out["children"])))
    _RESOLVERS.get(cls, lambda *_: None)(out, cls, props)
    return out


def _border(out, cls, props):
    out["desiredSizeScale"] = vec2(prop(cls, props, "DesiredSizeScale"))
    tint = linear_color(prop(cls, props, "BrushColor"))
    out["paint"] = brush_paint(prop(cls, props, "Background"), tint)
    # UBorder mirrors Padding/alignment onto its BorderSlot; the slot is what
    # SBorder lays out with. Without a child there is no slot to read.
    if not out["children"]:
        out["contentPadding"] = margin(prop(cls, props, "Padding"))


def _blur(out, cls, props):
    # Laid out like a Border (SBackgroundBlur is a compound widget with one slot).
    out["desiredSizeScale"] = [1.0, 1.0]
    if not out["children"]:
        out["contentPadding"] = margin(prop(cls, props, "Padding"))
    r = prop(cls, props, "CornerRadius")
    out["paint"] = {"blur": float(prop(cls, props, "BlurStrength")),
                    "radius": [float(r.get(k, 0.0)) for k in ("X", "Y", "Z", "W")]}


def _ugrid(out, cls, props):
    out["slotPadding"] = margin(prop(cls, props, "SlotPadding"))
    out["minSlot"] = [float(prop(cls, props, "MinDesiredSlotWidth")),
                      float(prop(cls, props, "MinDesiredSlotHeight"))]


def _button(out, cls, props):
    style = prop(cls, props, "WidgetStyle")
    out["stylePadding"] = margin(style["NormalPadding"])
    out["emptySize"] = vec2(style["Normal"].get("ImageSize", {}))
    tint = linear_color(prop(cls, props, "BackgroundColor"))
    out["paint"] = brush_paint(style["Normal"], tint)


def _text(out, cls, props):
    font = prop(cls, props, "Font")
    out["text"] = text_of(props.get("Text"))
    out["font"] = resolve_font(font)
    out["shadow"] = [abs(v) for v in vec2(prop(cls, props, "ShadowOffset"))]
    out["margin"] = margin(prop(cls, props, "Margin"))
    out["justify"] = canon(prop(cls, props, "Justification"), JUSTIFY, "justification")
    out["autoWrap"] = bool(prop(cls, props, "AutoWrapText"))
    out["minDesiredWidth"] = float(prop(cls, props, "MinDesiredWidth"))
    out["lineHeightPercentage"] = float(prop(cls, props, "LineHeightPercentage"))
    out["color"] = css_color(linear_color(prop(cls, props, "ColorAndOpacity")))


def _checkbox(out, cls, props):
    style = prop(cls, props, "WidgetStyle")
    out["imageSize"] = vec2(style["UncheckedImage"]["ImageSize"])
    out["contentPadding"] = margin(style["Padding"])
    out["checked"] = canon(prop(cls, props, "CheckedState"), CHECKED, "checked state")
    out["paint"] = {"tint": INHERITED_FOREGROUND, "uncheckedAlpha": CHECKBOX_UNCHECKED_ALPHA,
                    "checkmarkAlpha": CHECKBOX_CHECKMARK_ALPHA,
                    "inset": CHECKBOX_IMAGE_INSET, "radius": CHECKBOX_IMAGE_RADIUS}


def _edit_fields(out, cls, props, text_style, multiline):
    """What FSlateEditableTextLayout lays out with, for any editable text."""
    out["font"] = resolve_font(text_style["Font"])
    out["caretWidth"] = min(max(CARET_WIDTH_PERCENT * out["font"]["lineHeight"],
                                CARET_WIDTH_MIN), CARET_WIDTH_MAX)
    out["shadow"] = [abs(v) for v in vec2(text_style.get("ShadowOffset", {}))]
    out["text"] = text_of(props.get("Text"))
    out["hint"] = text_of(props.get("HintText"))
    out["multiline"] = multiline
    color = linear_color(text_style.get("ColorAndOpacity", {}))
    out["color"] = css_color(color)
    out["hintColor"] = css_color(color[:3] + [HINT_TEXT_OPACITY])
    out["justify"] = canon(prop(cls, props, "Justification"), JUSTIFY, "justification")


def _edittext(out, cls, props):
    style = prop(cls, props, "WidgetStyle")
    multiline = cls == "MultiLineEditableText"
    _edit_fields(out, cls, props, style, multiline)
    out["margin"] = margin(prop(cls, props, "Margin")) if multiline else [0.0] * 4
    out["autoWrap"] = bool(prop(cls, props, "AutoWrapText")) if multiline else False
    out["lineHeightPercentage"] = (float(prop(cls, props, "LineHeightPercentage"))
                                   if multiline else 1.0)
    out["minDesiredWidth"] = 0.0 if multiline else float(prop(cls, props, "MinimumDesiredWidth"))


def _textbox(out, cls, props):
    style = prop(cls, props, "WidgetStyle")
    text_style = style["TextStyle"]
    out["padding"] = margin(style["Padding"])
    multiline = cls == "MultiLineEditableTextBox"
    _edit_fields(out, cls, props, text_style, multiline)
    out["margin"] = [0.0] * 4
    out["autoWrap"] = bool(prop(cls, props, "AutoWrapText")) if multiline else False
    out["lineHeightPercentage"] = 1.0
    out["minDesiredWidth"] = 0.0
    background = linear_color(style["BackgroundColor"])
    image_tint = linear_color(style["BackgroundImageNormal"].get("TintColor", {}))
    fill = [b * t for b, t in zip(background, image_tint)]
    fill[3] *= TEXTBOX_IMAGE_ALPHA
    out["paint"] = {"fill": css_color(fill), "radius": [TEXTBOX_IMAGE_RADIUS] * 4,
                    "outline": None}


def _sizebox(out, cls, props):
    sizes = {}
    for name in ("WidthOverride", "HeightOverride", "MinDesiredWidth",
                 "MinDesiredHeight", "MaxDesiredWidth", "MaxDesiredHeight"):
        if props.get("bOverride_" + name):
            sizes[name] = float(prop(cls, props, name))
    out["sizes"] = sizes


def _spacer(out, cls, props):
    out["size"] = vec2(prop(cls, props, "Size"))


def _image(out, cls, props):
    brush = prop(cls, props, "Brush")
    out["size"] = vec2(brush["ImageSize"])
    out["paint"] = brush_paint(brush, linear_color(prop(cls, props, "ColorAndOpacity")))


def _scrollbox(out, cls, props):
    if canon(prop(cls, props, "Orientation"), {"orientvertical": "vertical",
                                               "orienthorizontal": "horizontal"},
             "orientation") != "vertical":
        raise UnsupportedWidget("%s: only vertical ScrollBox is modelled" % out["name"])


def _expandable(out, cls, props):
    style = prop(cls, props, "Style")
    header = margin(prop(cls, props, "HeaderPadding"))
    image = vec2(style["CollapsedImage"]["ImageSize"])
    if props.get("bIsExpanded"):
        raise UnsupportedWidget("%s: an expanded ExpandableArea is not modelled" % out["name"])
    out["size"] = [header[0] + header[2] + image[0] + EXPANDABLE_HEADER_EXTRA_WIDTH,
                   header[1] + header[3] + image[1] + EXPANDABLE_HEADER_EXTRA_HEIGHT]


_RESOLVERS = {
    "Border": _border, "Button": _button, "TextBlock": _text,
    "CheckBox": _checkbox, "MultiLineEditableTextBox": _textbox,
    "EditableTextBox": _textbox, "SizeBox": _sizebox, "Spacer": _spacer,
    "Image": _image, "ScrollBox": _scrollbox, "ExpandableArea": _expandable,
    "BackgroundBlur": _blur, "UniformGridPanel": _ugrid,
    "EditableText": _edittext, "MultiLineEditableText": _edittext,
}


def resolve(doc):
    """Widget JSON document -> layout tree plus the font files it needs."""
    root = resolve_node(doc["root"])
    fonts = {}

    def collect(n):
        f = n.get("font")
        if f:
            fonts[f["family"]] = f["file"]
        for c in n["children"]:
            collect(c)

    collect(root)
    return {"asset": doc.get("asset", "widget"), "root": root, "fonts": fonts}
