#!/usr/bin/env python3
"""Render a widget JSON source file to an HTML preview.

    python3 tools/json_to_html.py widgets/BugReportForm.json -o /tmp/preview.html
    python3 tools/json_to_html.py widgets/BugReportForm.json --size 1920x1080 -o /tmp/p.html

Default mode lays the widget out with a re-implementation of Slate's own layout
passes (tools/preview/slate_layout.js), fed by tools/umg_resolve.py, which fills
in every class default from tools/umg_defaults.json and gives text the engine's
own Roboto fonts and Slate's font metrics. Its geometry is checked against real
UE 5.3 renders by tools/tests/test_preview.py; tools/measure_preview.py reports
the same numbers for any render you have.

Widgets the preview cannot model raise an error naming the class, rather than
being drawn approximately.

With --geometry it instead places every widget at the rect Unreal measured
during a real render (ue_render_widgets.py) - exact by construction, but it
needs an editor run first.
"""

import argparse
import base64
import html
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import umg_resolve  # noqa: E402

LAYOUT_JS = os.path.join(HERE, "preview", "slate_layout.js")
DEFAULT_SIZE = (1280, 860)


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


def text_of(value):
    if isinstance(value, dict) and "__text__" in value:
        return value["__text__"]
    if isinstance(value, str):
        return value
    return ""


PAGE = """<!doctype html>
<meta charset="utf-8">
<title>%(title)s - preview %(w)dx%(h)d</title>
<style>
%(fonts)s
  body { margin:0; background:#2b2f33; color:#ddd;
         font-family:-apple-system,Segoe UI,sans-serif; }
  .banner { background:#1c3f5e; color:#d8ecff; padding:8px 14px; font-size:13px; }
  .banner b { color:#fff; }
  .toggle { padding:6px 24px; font-size:12px; }
  #bh-stage { position:relative; margin:12px 24px 32px; overflow:hidden;
              background:#000; outline:1px solid #444; }
  .bh-w { position:absolute; box-sizing:border-box; }
  .bh-text { position:absolute; inset:0; overflow:visible; }
  .bh-glyph { position:absolute; white-space:pre; font-kerning:none; }
  .bh-check { position:absolute; box-sizing:border-box; }
  .bh-tick { position:absolute; left:32%%; top:8%%; width:28%%; height:58%%;
             border:solid; border-width:0 2.5px 2.5px 0; transform:rotate(40deg); }
  body.outline .bh-w { outline:1px dashed rgba(120,200,255,.45); }
  /* #bare: just the widget at its draw size, for screenshots and pixel diffs */
  body.bare { background:#000; }
  body.bare .banner, body.bare .toggle { display:none; }
  body.bare #bh-stage { margin:0; outline:none; }
</style>
<div class="banner"><b>Slate layout preview.</b> Layout follows UE 5.3's own algorithm with
  every class default filled in from the engine; glyph advances come from the browser.
  Default visual state only: no hover, focus or runtime changes.</div>
<div class="toggle"><label><input type="checkbox"
  onchange="document.body.classList.toggle('outline')"> outline widgets</label></div>
<div id="bh-stage"></div>
<script>if (location.hash === "#bare") document.body.classList.add("bare");</script>
<script>window.BH_PREVIEW = %(config)s;</script>
<script>
%(layout_js)s
</script>
"""


def font_faces(fonts):
    rules = []
    for family, filename in sorted(fonts.items()):
        with open(os.path.join(umg_resolve.REPO, filename), "rb") as fh:
            data = base64.b64encode(fh.read()).decode("ascii")
        rules.append("  @font-face { font-family:'%s'; src:url(data:font/ttf;base64,%s); }"
                     % (family, data))
    return "\n".join(rules)


def build_preview(doc, width, height):
    """Self-contained HTML: fonts, layout engine and resolved tree inline."""
    tree = umg_resolve.resolve(doc)
    config = json.dumps({"tree": tree, "width": width, "height": height})
    with open(LAYOUT_JS, "r", encoding="utf-8") as fh:
        layout_js = fh.read()
    return PAGE % {
        "title": html.escape(tree["asset"]),
        "w": width, "h": height,
        "fonts": font_faces(tree["fonts"]),
        # "</" would end the script element early.
        "config": config.replace("</", "<\\/"),
        "layout_js": layout_js,
    }


def parse_size(text):
    try:
        w, h = (int(v) for v in text.lower().split("x"))
    except ValueError:
        raise argparse.ArgumentTypeError("size must look like 1280x860")
    if w <= 0 or h <= 0:
        raise argparse.ArgumentTypeError("size must be positive")
    return w, h


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
    ap.add_argument("-s", "--size", type=parse_size, default=DEFAULT_SIZE,
                    help="draw size WxH, as the widget would get on screen "
                         "(default %dx%d)" % DEFAULT_SIZE)
    ap.add_argument("-g", "--geometry",
                    help="geometry JSON from ue_render_widgets.py: place every widget "
                         "at the rect Unreal measured instead of laying it out")
    args = ap.parse_args(argv)

    with open(args.json_file, "r", encoding="utf-8") as fh:
        doc = json.load(fh)

    if args.geometry:
        with open(args.geometry, "r", encoding="utf-8") as fh:
            page = render_exact(doc, json.load(fh))
    else:
        page = build_preview(doc, *args.size)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(page)
        print("wrote %s" % args.out, file=sys.stderr)
    else:
        print(page)
    return 0


if __name__ == "__main__":
    sys.exit(main())
