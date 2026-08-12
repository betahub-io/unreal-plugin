#!/usr/bin/env python3
"""Render a widget JSON source file to HTML — an approximate visual sketch.

    python3 tools/json_to_html.py widgets/BugReportForm.json -o /tmp/preview.html

THIS IS NOT A VERIFICATION TOOL. It is a fast iteration aid for seeing roughly
what a JSON edit produces without a 60-second Unreal round trip.

It approximates Slate's layout with CSS flexbox, and the two engines genuinely
differ - most visibly around CanvasPanel anchors, Slate's Fill/Auto sizing, and
text metrics. More importantly it is written from the same reading of the JSON
as the generator, so it can reproduce a misunderstanding rather than expose it.
Ground truth is BHWidgetGenLibrary.RenderWidgetToPng, which renders what Unreal
actually built.

Use this to catch gross mistakes - wrong container, missing widget, absurd
padding. Use the PNG render to decide anything looks correct.
"""

import argparse
import html
import json
import os
import sys


CANVAS_SIZE = (1280, 860)


def srgb(c):
    """Unreal stores linear colour; browsers expect sRGB. Skipping this makes
    everything markedly too dark (0.068 linear is mid-grey, not near-black)."""
    c = max(0.0, min(1.0, float(c)))
    v = 12.92 * c if c <= 0.0031308 else 1.055 * (c ** (1 / 2.4)) - 0.055
    return int(round(max(0.0, min(1.0, v)) * 255))


def color_of(value, default="transparent"):
    if not isinstance(value, dict):
        return default
    spec = value.get("SpecifiedColor", value)
    if not isinstance(spec, dict):
        return default
    if not any(k in spec for k in ("R", "G", "B", "A")):
        return default
    r, g, b = (srgb(spec.get(k, 0.0)) for k in ("R", "G", "B"))
    a = float(spec.get("A", 1.0))
    return "rgba(%d,%d,%d,%.3f)" % (r, g, b, a)


def margin_of(pad):
    if not isinstance(pad, dict):
        return None
    return "%gpx %gpx %gpx %gpx" % (
        pad.get("Top", 0), pad.get("Right", 0),
        pad.get("Bottom", 0), pad.get("Left", 0))


H_ALIGN = {"HAlign_Fill": "stretch", "HAlign_Left": "flex-start",
           "HAlign_Center": "center", "HAlign_Right": "flex-end"}
V_ALIGN = {"VAlign_Fill": "stretch", "VAlign_Top": "flex-start",
           "VAlign_Center": "center", "VAlign_Bottom": "flex-end"}


def slot_style(slot, parent_class):
    """CSS for how a child sits inside its parent."""
    if not slot:
        return [], []
    props = slot.get("properties") or {}
    style, notes = [], []

    m = margin_of(props.get("Padding"))
    if m:
        style.append("margin:%s" % m)

    size = props.get("Size")
    if isinstance(size, dict) and size.get("SizeRule") == "Fill":
        style.append("flex:%s 1 0" % size.get("Value", 1))
    else:
        style.append("flex:0 0 auto")

    h = H_ALIGN.get(props.get("HorizontalAlignment"))
    v = V_ALIGN.get(props.get("VerticalAlignment"))
    if parent_class == "VerticalBox":
        if h:
            style.append("align-self:%s" % h)
    elif parent_class == "HorizontalBox":
        if v:
            style.append("align-self:%s" % v)
    else:
        if h:
            style.append("justify-self:%s" % h)
        if v:
            style.append("align-self:%s" % v)

    if parent_class == "CanvasPanel":
        layout = props.get("LayoutData") or {}
        offsets = layout.get("Offsets") or {}
        anchors = layout.get("Anchors") or {}
        mn = anchors.get("Minimum") or {}
        style.append("position:absolute")
        style.append("left:%g%%" % (float(mn.get("X", 0)) * 100))
        style.append("top:%g%%" % (float(mn.get("Y", 0)) * 100))
        w = offsets.get("Right")
        hgt = offsets.get("Bottom")
        if w:
            style.append("width:%gpx" % w)
        if hgt:
            style.append("height:%gpx" % hgt)
        align = layout.get("Alignment") or {}
        style.append("transform:translate(%g%%,%g%%)"
                     % (-float(align.get("X", 0)) * 100,
                        -float(align.get("Y", 0)) * 100))
        notes.append("CanvasPanel anchors are approximated")

    return style, notes


def container_style(cls, props):
    style, notes = [], []
    if cls == "VerticalBox":
        style += ["display:flex", "flex-direction:column"]
    elif cls == "HorizontalBox":
        style += ["display:flex", "flex-direction:row"]
    elif cls == "Overlay":
        style += ["display:grid", "grid-template-areas:'stack'",
                  "justify-items:stretch", "align-items:stretch"]
    elif cls == "CanvasPanel":
        style += ["position:relative",
                  "width:%dpx" % CANVAS_SIZE[0],
                  "height:%dpx" % CANVAS_SIZE[1]]
        notes.append("CanvasPanel given a %dx%d stage; in game it fills the viewport"
                     % CANVAS_SIZE)
    elif cls in ("Border", "Button"):
        style += ["display:flex", "flex-direction:column",
                  "align-items:stretch", "justify-content:flex-start"]

    if cls == "Border":
        style.append("background:%s" % color_of(props.get("BrushColor"), "transparent"))
        m = margin_of(props.get("Padding"))
        if m:
            style.append("padding:%s" % m)
    if cls == "Button":
        ws = props.get("WidgetStyle") or {}
        normal = ws.get("Normal") if isinstance(ws, dict) else None
        tint = (normal or {}).get("TintColor") if isinstance(normal, dict) else None
        style.append("background:%s" % color_of(tint, "rgba(128,128,128,1)"))
        style.append("border-radius:4px")
        notes.append("Button styling is approximated from Normal tint only")
    return style, notes


def text_of(value):
    if isinstance(value, dict) and "__text__" in value:
        return value["__text__"]
    if isinstance(value, str):
        return value
    return ""


def render_node(node, parent_class, notes, depth=0):
    cls = node.get("class", "?")
    name = node.get("name", "?")
    props = node.get("properties") or {}

    style, s_notes = slot_style(node.get("slot"), parent_class)
    c_style, c_notes = container_style(cls, props)
    style += c_style
    notes.extend("%s: %s" % (name, n) for n in s_notes + c_notes)

    if parent_class == "Overlay":
        style.append("grid-area:stack")

    vis = props.get("Visibility")
    if vis in ("Collapsed",):
        style.append("display:none")
    elif vis in ("Hidden",):
        style.append("visibility:hidden")

    inner = ""
    if cls == "TextBlock":
        font = props.get("Font") or {}
        size = font.get("Size", 12)
        col = color_of(props.get("ColorAndOpacity"), "rgba(255,255,255,1)")
        style += ["font-size:%gpx" % size, "color:%s" % col,
                  "white-space:pre-wrap"]
        inner = html.escape(text_of(props.get("Text")))
    elif cls == "CheckBox":
        style += ["width:16px", "height:16px", "border:1px solid #999",
                  "border-radius:2px", "background:rgba(255,255,255,.15)",
                  "flex:0 0 auto"]
        notes.append("%s: CheckBox drawn as a plain box" % name)
    elif cls == "MultiLineEditableTextBox":
        hint = html.escape(text_of(props.get("HintText")))
        style += ["background:rgba(255,255,255,.9)", "color:#555",
                  "min-height:60px", "padding:6px", "border-radius:3px",
                  "font-size:13px", "white-space:pre-wrap", "text-align:left",
                  "display:block"]
        inner = hint
    elif cls == "Spacer":
        size = props.get("Size") or {}
        style += ["width:%gpx" % size.get("X", 0), "height:%gpx" % size.get("Y", 0)]
    elif cls == "ExpandableArea":
        style += ["min-width:8px", "min-height:8px"]

    children = node.get("children") or []
    kids = "".join(render_node(c, cls, notes, depth + 1) for c in children)

    return ('<div class="w" data-name="%s" data-class="%s" style="%s" '
            'title="%s [%s]">%s%s</div>'
            % (html.escape(name), html.escape(cls), ";".join(style),
               html.escape("%s [%s]" % (name, cls)), html.escape(cls),
               inner, kids))


PAGE = """<!doctype html>
<meta charset="utf-8">
<title>%(title)s — approximate preview</title>
<style>
  body {{ margin:0; background:#2b2f33; color:#ddd;
          font-family:-apple-system,Segoe UI,Roboto,sans-serif; }}
  .banner {{ background:#7a4a12; color:#ffe9cf; padding:8px 14px;
             font-size:13px; line-height:1.5; }}
  .banner b {{ color:#fff; }}
  .stage {{ padding:24px; display:flex; justify-content:center; }}
  .root {{ box-shadow:0 6px 30px rgba(0,0,0,.5); }}
  .w {{ box-sizing:border-box; }}
  .notes {{ padding:12px 24px 32px; font-size:12px; color:#9aa; }}
  .notes li {{ margin:2px 0; }}
  body.outline .w {{ outline:1px dashed rgba(120,200,255,.35); }}
  .toggle {{ padding:6px 24px; font-size:12px; }}
</style>
<div class="banner">
  <b>Approximate sketch, not verification.</b> Slate layout is emulated with CSS
  and the two differ — CanvasPanel anchors, Fill/Auto sizing and text metrics
  especially. This is also written from the same reading of the JSON as the
  generator, so it can repeat a misunderstanding rather than reveal one.
  Confirm anything that matters with the Unreal PNG render.
</div>
<div class="toggle">
  <label><input type="checkbox" onchange="document.body.classList.toggle('outline')"> outline widgets</label>
</div>
<div class="stage"><div class="root">%(body)s</div></div>
<div class="notes"><b>Approximations applied:</b><ul>%(notes)s</ul></div>
"""


def convert(doc):
    notes = []
    body = render_node(doc["root"], None, notes)
    seen, ordered = set(), []
    for n in notes:
        if n not in seen:
            seen.add(n)
            ordered.append(n)
    note_html = "".join("<li>%s</li>" % html.escape(n) for n in ordered) \
        or "<li>none</li>"
    return PAGE.replace("{{", "{").replace("}}", "}") % {
        "title": html.escape(doc.get("asset", "widget")),
        "body": body,
        "notes": note_html,
    }


EXACT_PAGE = """<!doctype html>
<meta charset="utf-8">
<title>%(title)s — exact</title>
<style>
  body {{ margin:0; background:#2b2f33; color:#ddd;
          font-family:-apple-system,Segoe UI,Roboto,sans-serif; }}
  .banner {{ background:#14503a; color:#d6ffe9; padding:8px 14px; font-size:13px; }}
  .banner b {{ color:#fff; }}
  .stage {{ position:relative; margin:24px auto; width:%(w)dpx; height:%(h)dpx;
            outline:1px solid #444; }}
  .w {{ position:absolute; box-sizing:border-box; }}
  body.outline .w {{ outline:1px dashed rgba(120,200,255,.45); }}
  .toggle {{ padding:6px 24px; font-size:12px; }}
</style>
<div class="banner">
  <b>Exact.</b> Every rectangle below is the geometry Unreal computed during a real
  arrange pass — positions and sizes are measured, not emulated. Colours and text come
  from the JSON source.
</div>
<div class="toggle">
  <label><input type="checkbox" onchange="document.body.classList.toggle('outline')"> outline widgets</label>
</div>
<div class="stage">%(body)s</div>
"""


def font_style(rect, json_font):
    """Prefer the font Unreal resolved. Six of thirteen text widgets carry no
    Font in the asset at all - the export only emits non-defaults - so reading
    it from the JSON alone silently renders them at the wrong size."""
    size = None
    weight = 400
    if isinstance(rect, dict) and rect.get("fontSize"):
        size = rect["fontSize"]
        if str(rect.get("typeface", "")).lower().startswith("bold"):
            weight = 700
    elif isinstance(json_font, dict) and json_font.get("Size"):
        size = json_font["Size"]
    if size is None:
        size = 24  # UMG's default TextBlock size, not an arbitrary guess
    return ["font-size:%gpx" % size, "font-weight:%d" % weight]


def render_exact(doc, geometry):
    """Position every widget from Unreal's measured geometry."""
    by_name = {}

    def index(n):
        by_name[n["name"]] = n
        for c in n.get("children", []):
            index(c)

    index(doc["root"])

    rects = geometry.get("widgets") or []
    if not rects:
        raise SystemExit("geometry file contains no widgets")

    origin_x = min(r["x"] for r in rects)
    origin_y = min(r["y"] for r in rects)

    parts = []
    for r in rects:
        node = by_name.get(r["name"], {})
        props = node.get("properties") or {}
        cls = r.get("class", node.get("class", "?"))
        style = ["left:%gpx" % (r["x"] - origin_x), "top:%gpx" % (r["y"] - origin_y),
                 "width:%gpx" % r["width"], "height:%gpx" % r["height"]]
        inner = ""

        if cls == "Border":
            style.append("background:%s" % color_of(props.get("BrushColor")))
        elif cls == "Button":
            ws = props.get("WidgetStyle") or {}
            normal = ws.get("Normal") if isinstance(ws, dict) else None
            tint = (normal or {}).get("TintColor") if isinstance(normal, dict) else None
            style += ["background:%s" % color_of(tint, "rgba(128,128,128,1)"),
                      "border-radius:4px"]
        elif cls == "TextBlock":
            style += font_style(r, props.get("Font"))
            style += ["color:%s" % color_of(props.get("ColorAndOpacity"),
                                            "rgba(255,255,255,1)"),
                      "white-space:pre-wrap", "line-height:1.15"]
            inner = html.escape(text_of(props.get("Text")))
        elif cls == "CheckBox":
            style += ["border:1px solid #999", "border-radius:2px",
                      "background:rgba(255,255,255,.15)", "color:#fff",
                      "display:flex", "align-items:center",
                      "justify-content:center", "font-size:%gpx" % (r["height"] * .8)]
            if props.get("CheckedState") == "Checked":
                inner = "&#10003;"

        elif cls == "MultiLineEditableTextBox":
            style += font_style(r, None)
            style += ["background:rgba(255,255,255,.9)", "color:#777",
                      "padding:6px", "border-radius:3px",
                      "white-space:pre-wrap", "overflow:hidden"]
            inner = html.escape(text_of(props.get("HintText")))

        if not r.get("visible", True):
            style.append("opacity:.25")

        parts.append('<div class="w" style="%s" title="%s">%s</div>'
                     % (";".join(style),
                        html.escape("%s [%s]  %gx%g at %g,%g"
                                    % (r["name"], cls, r["width"], r["height"],
                                       r["x"], r["y"])),
                        inner))

    return EXACT_PAGE.replace("{{", "{").replace("}}", "}") % {
        "title": html.escape(doc.get("asset", "widget")),
        "w": int(geometry.get("drawWidth", 1280)),
        "h": int(geometry.get("drawHeight", 860)),
        "body": "".join(parts),
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("json_file")
    ap.add_argument("-o", "--out", help="output .html (default: stdout)")
    ap.add_argument("-g", "--geometry",
                    help="geometry JSON from ue_render_widgets.py. With this the "
                         "output is exact rather than approximate.")
    args = ap.parse_args(argv)

    with open(args.json_file, "r", encoding="utf-8") as fh:
        doc = json.load(fh)

    if args.geometry:
        with open(args.geometry, "r", encoding="utf-8") as fh:
            page = render_exact(doc, json.load(fh))
    else:
        page = convert(doc)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(page)
        print("wrote %s" % args.out, file=sys.stderr)
    else:
        print(page)
    return 0


if __name__ == "__main__":
    sys.exit(main())
