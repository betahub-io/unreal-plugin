#!/usr/bin/env python3
"""Measure an HTML widget preview against the geometry Unreal really computed.

    python3 tools/measure_preview.py preview.html widgets/BugReportForm.json \
        tools/tests/fixtures/BugReportForm.1280x860.geometry.json

The preview is loaded in headless Chrome. Every element carrying `data-name` is
measured with render transforms switched off, relative to the root widget, and
then transformed the way Unreal's geometry dump reports it: position after the
widget's RenderTransform, size before it. That is what makes the two directly
comparable.

Exit status is 0 when at least --min-ratio of the widgets Unreal drew lie within
--tolerance pixels on x, y, width and height.

Chrome is found via --chrome, $BH_CHROME, a Playwright browser cache, or a
system install.
"""

import argparse
import glob
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import umg_resolve  # noqa: E402

DEFAULT_TOLERANCE_PX = 2.0
DEFAULT_MIN_RATIO = 0.9
CHROME_TIMEOUT_S = 60
VIRTUAL_TIME_BUDGET_MS = 5000
RESULT_ID = "bh-measure-result"
# Per-channel differences (0-255) to report: small ones are antialiasing and
# glyph hinting, large ones are something drawn differently.
PIXEL_THRESHOLDS = (16, 64)

CHROME_CANDIDATES = [
    "~/Library/Caches/ms-playwright/chromium_headless_shell-*/chrome-headless-shell-*/chrome-headless-shell",
    "~/.cache/ms-playwright/chromium_headless_shell-*/chrome-headless-shell-*/chrome-headless-shell",
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Chromium.app/Contents/MacOS/Chromium",
]
CHROME_ON_PATH = ["chromium", "chromium-browser", "google-chrome", "chrome"]

# Appended to the page. Waits for the preview to finish laying out, switches
# render transforms off, and reports every widget's layout rect relative to the
# root widget.
COLLECTOR = """
<style>.bh-measuring [data-name]{transform:none !important}</style>
<script>
(function () {
  const ready = window.bhPreviewReady || document.fonts.ready;
  Promise.resolve(ready).then(function () {
    document.body.classList.add('bh-measuring');
    const all = document.querySelectorAll('[data-name]');
    const out = {};
    if (all.length) {
      const r0 = all[0].getBoundingClientRect();
      all.forEach(function (e) {
        const r = e.getBoundingClientRect();
        out[e.dataset.name] = {x: r.left - r0.left, y: r.top - r0.top,
                               width: r.width, height: r.height};
      });
    }
    const pre = document.createElement('pre');
    pre.id = '%s';
    pre.textContent = JSON.stringify(out);
    document.body.appendChild(pre);
  });
})();
</script>
""" % RESULT_ID


def find_chrome(explicit=None):
    """Path to a Chrome/Chromium binary, or None."""
    if explicit:
        return explicit
    if os.environ.get("BH_CHROME"):
        return os.environ["BH_CHROME"]
    for pattern in CHROME_CANDIDATES:
        hits = sorted(glob.glob(os.path.expanduser(pattern)))
        if hits:
            return hits[-1]
    for name in CHROME_ON_PATH:
        path = shutil.which(name)
        if path:
            return path
    return None


def collect_rects(html_path, chrome):
    """Layout rects of every [data-name] element, keyed by widget name."""
    with open(html_path, "r", encoding="utf-8") as fh:
        page = fh.read()
    with tempfile.TemporaryDirectory() as tmp:
        probe = os.path.join(tmp, "probe.html")
        with open(probe, "w", encoding="utf-8") as fh:
            fh.write(page + COLLECTOR)
        proc = subprocess.run(
            [chrome, "--headless", "--disable-gpu", "--hide-scrollbars",
             "--allow-file-access-from-files", "--window-size=2400,1600",
             "--virtual-time-budget=%d" % VIRTUAL_TIME_BUDGET_MS,
             "--dump-dom", "file://" + probe],
            capture_output=True, text=True, timeout=CHROME_TIMEOUT_S)
    m = re.search(r'<pre id="%s">(.*?)</pre>' % RESULT_ID, proc.stdout, re.S)
    if not m:
        raise RuntimeError("preview never reported its layout (chrome exit %d): %s"
                           % (proc.returncode, proc.stderr.strip()[-500:]))
    raw = m.group(1).replace("&quot;", '"').replace("&lt;", "<") \
        .replace("&gt;", ">").replace("&amp;", "&")
    return json.loads(raw)


def transformed_origin(rect, transform, pivot):
    """Where Unreal reports a widget's top-left: its layout origin pushed through
    the RenderTransform about the pivot. Slate applies scale, then shear, then
    rotation, then translation."""
    w, h = rect["width"], rect["height"]
    px, py = pivot["X"] * w, pivot["Y"] * h
    x, y = -px, -py
    sx, sy = transform["Scale"]["X"], transform["Scale"]["Y"]
    x, y = x * sx, y * sy
    shx = math.tan(math.radians(transform["Shear"]["X"]))
    shy = math.tan(math.radians(transform["Shear"]["Y"]))
    x, y = x + y * shx, y + x * shy
    a = math.radians(transform["Angle"])
    x, y = x * math.cos(a) - y * math.sin(a), x * math.sin(a) + y * math.cos(a)
    x += transform["Translation"]["X"] + px
    y += transform["Translation"]["Y"] + py
    return rect["x"] + x, rect["y"] + y


def compare(widget_doc, geometry, rects):
    """One row per widget Unreal drew: (delta, name, class, unreal, preview).
    delta is the worst of |dx|, |dy|, |dw|, |dh|; infinite when the preview has
    no such widget."""
    nodes = {}

    def index(n):
        nodes[n["name"]] = n
        for c in n.get("children") or []:
            index(c)

    index(widget_doc["root"])

    rows = []
    for w in geometry["widgets"]:
        if not w.get("visible", True):
            continue
        ue = (w["x"], w["y"], w["width"], w["height"])
        r = rects.get(w["name"])
        if r is None:
            rows.append((math.inf, w["name"], w["class"], ue, None))
            continue
        node = nodes.get(w["name"], {"class": w["class"]})
        transform, pivot = umg_resolve.render_transform(node)
        x, y = transformed_origin(r, transform, pivot)
        mine = (x, y, r["width"], r["height"])
        delta = max(abs(a - b) for a, b in zip(ue, mine))
        rows.append((delta, w["name"], w["class"], ue, mine))
    return rows


def summarise(rows, tolerance):
    within = sum(1 for r in rows if r[0] <= tolerance)
    return within, len(rows)


def screenshot(html_path, width, height, out_png, chrome):
    """The widget alone at its draw size, via the preview's #bare mode."""
    proc = subprocess.run(
        [chrome, "--headless", "--disable-gpu", "--hide-scrollbars",
         "--allow-file-access-from-files", "--window-size=%d,%d" % (width, height),
         "--virtual-time-budget=%d" % VIRTUAL_TIME_BUDGET_MS,
         "--screenshot=" + out_png, "file://%s#bare" % os.path.abspath(html_path)],
        capture_output=True, text=True, timeout=CHROME_TIMEOUT_S)
    if not os.path.exists(out_png):
        raise RuntimeError("screenshot failed (chrome exit %d): %s"
                           % (proc.returncode, proc.stderr.strip()[-500:]))


def pixel_diff(png_a, png_b, threshold):
    """Share of pixels whose largest channel difference exceeds threshold."""
    try:
        from PIL import Image, ImageChops
    except ImportError:
        raise SystemExit("--png needs Pillow (pip install Pillow)")
    a = Image.open(png_a).convert("RGB")
    b = Image.open(png_b).convert("RGB")
    if a.size != b.size:
        raise SystemExit("size mismatch: preview %s vs render %s" % (a.size, b.size))
    channels = ImageChops.difference(a, b).split()
    worst = channels[0]
    for c in channels[1:]:
        worst = ImageChops.lighter(worst, c)
    over = worst.point(lambda v: 255 if v > threshold else 0).histogram()[255]
    return over / float(a.size[0] * a.size[1])


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("preview_html")
    ap.add_argument("widget_json")
    ap.add_argument("geometry_json")
    ap.add_argument("--tolerance", type=float, default=DEFAULT_TOLERANCE_PX)
    ap.add_argument("--min-ratio", type=float, default=DEFAULT_MIN_RATIO)
    ap.add_argument("--chrome")
    ap.add_argument("--worst", type=int, default=8, help="offenders to list")
    ap.add_argument("--png", help="Unreal's render of the same widget: also report "
                                  "how many pixels differ (needs Pillow)")
    ap.add_argument("--screenshot", help="where to keep the preview screenshot "
                                         "taken for --png (default: discarded)")
    args = ap.parse_args(argv)

    chrome = find_chrome(args.chrome)
    if not chrome:
        raise SystemExit("no Chrome/Chromium found - pass --chrome or set BH_CHROME")
    with open(args.widget_json, "r", encoding="utf-8") as fh:
        doc = json.load(fh)
    with open(args.geometry_json, "r", encoding="utf-8") as fh:
        geometry = json.load(fh)

    rows = compare(doc, geometry, collect_rects(args.preview_html, chrome))
    within, total = summarise(rows, args.tolerance)
    print("%d of %d widgets within %gpx" % (within, total, args.tolerance))
    fmt = lambda r: "(%s)" % ", ".join("%.1f" % v for v in r) if r else "missing"
    for delta, name, cls, ue, mine in sorted(rows, key=lambda r: -r[0])[:args.worst]:
        if delta <= args.tolerance:
            break
        print("  %7.1f  %-30s %-26s unreal %s  preview %s"
              % (delta, name, cls, fmt(ue), fmt(mine)))

    if args.png:
        with tempfile.TemporaryDirectory() as tmp:
            shot = args.screenshot or os.path.join(tmp, "preview.png")
            screenshot(args.preview_html, geometry["drawWidth"], geometry["drawHeight"],
                       shot, chrome)
            for threshold in PIXEL_THRESHOLDS:
                print("pixels differing by more than %3d: %.2f%%"
                      % (threshold, 100 * pixel_diff(shot, args.png, threshold)))
    return 0 if total and within / total >= args.min_ratio else 1


if __name__ == "__main__":
    sys.exit(main())
