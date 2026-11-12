#!/usr/bin/env python3
"""Tests for the Slate-faithful HTML preview.

    python3 tools/tests/test_preview.py

Two groups:

- Resolution (standard library only): class defaults, partial structs, font
  metrics, refusal of what the preview cannot model.
- Fidelity (needs Chrome/Chromium; skipped with a reason otherwise): lays out
  both shipped widgets at two draw sizes and compares every widget's rect with
  the geometry UE 5.3 itself computed in a real render (fixtures/*.geometry.json,
  produced by ue_render_widgets.py), plus fixtures/FontProbe, which covers the
  plugin's own fonts. The phase-1 bar was 90% of widgets within
  2px; today every widget is within 2px, so anything less is a regression.

Refreshing the fixtures means re-rendering on UE 5.3 - see tools/README.md.
"""

import copy
import json
import math
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
REPO = os.path.dirname(TOOLS)
FIXTURES = os.path.join(HERE, "fixtures")
sys.path.insert(0, TOOLS)

import json_to_html  # noqa: E402
import measure_preview  # noqa: E402
import umg_resolve  # noqa: E402

TOLERANCE_PX = 2.0
REQUIRED_RATIO = 1.0
WIDGETS = ("BugReportForm", "BugReportFormPopup")
SIZES = ((1280, 860), (1920, 1080))
CHROME = measure_preview.find_chrome()


def node(cls, name="W", props=None, slot=None, children=None):
    n = {"name": name, "class": cls, "properties": props or {}}
    if slot:
        n["slot"] = slot
    if children:
        n["children"] = children
    return n


class ResolutionTests(unittest.TestCase):

    def test_partial_struct_keeps_the_class_default_fields(self):
        # The .T3D export dropped Top/Bottom because they equal the ButtonSlot
        # default (2) - reading them as 0 shrinks every button by 4px.
        slot = umg_resolve.resolve_slot(
            {"class": "ButtonSlot", "properties": {"Padding": {"Left": 2.0, "Right": 2.0}}})
        self.assertEqual(slot["padding"], [2.0, 2.0, 2.0, 2.0])

    def test_absent_slot_values_come_from_the_engine(self):
        slot = umg_resolve.resolve_slot({"class": "BorderSlot", "properties": {}})
        self.assertEqual(slot["padding"], [4.0, 2.0, 4.0, 2.0])
        self.assertEqual((slot["hAlign"], slot["vAlign"]), ("fill", "fill"))
        overlay = umg_resolve.resolve_slot({"class": "OverlaySlot", "properties": {}})
        self.assertEqual((overlay["hAlign"], overlay["vAlign"]), ("left", "top"))

    def test_both_enum_spellings_resolve(self):
        self.assertEqual(umg_resolve.canon("HAlign_Center", umg_resolve.H_ALIGN, "a"), "center")
        self.assertEqual(umg_resolve.canon({"__enum__": "H_ALIGN_CENTER"},
                                           umg_resolve.H_ALIGN, "a"), "center")

    def test_text_without_font_gets_umg_default_bold_24(self):
        text = umg_resolve.resolve_node(node("TextBlock", props={"Text": {"__text__": "Hi"}}))
        self.assertTrue(text["font"]["file"].endswith("Roboto-Bold.ttf"))
        self.assertEqual(text["font"]["ppem"], 32.0)
        self.assertEqual(text["shadow"], [1.0, 1.0])

    def test_line_heights_match_unreal(self):
        # Heights Unreal reported for these sizes, which include the 1px shadow.
        for points, unreal_height in ((36, 57), (24, 39), (20, 33), (18, 29), (16, 26)):
            font = umg_resolve.resolve_font({"Size": float(points), "TypefaceFontName": "Bold"})
            self.assertEqual(font["lineHeight"] + 1, unreal_height, "%dpt" % points)

    def test_fractional_pixel_sizes_round_like_freetype(self):
        # 20pt is 26.67px; Roboto forces integer ppem, so Slate lays out at 27.
        font = umg_resolve.resolve_font({"Size": 20.0, "TypefaceFontName": "Bold"})
        self.assertEqual(font["ppem"], 27.0)

    def test_unknown_widget_class_is_refused(self):
        with self.assertRaises(umg_resolve.UnsupportedWidget):
            umg_resolve.resolve_node(node("WebBrowser"))

    def test_font_without_preview_file_is_refused(self):
        with self.assertRaises(umg_resolve.UnsupportedWidget):
            umg_resolve.resolve_font({"Size": 12.0, "TypefaceFontName": "Black"})

    def test_render_transform_origin_matches_unreal_dump(self):
        # BugReportCheckBox: layout rect (419, 144.42, 18, 19), scale 2 about the
        # centre; Unreal reports its top-left at (410, 134.92).
        transform, pivot = umg_resolve.render_transform(
            node("CheckBox", props={"RenderTransform": {"Scale": {"X": 2.0, "Y": 2.0}}}))
        x, y = measure_preview.transformed_origin(
            {"x": 419.0, "y": 144.42, "width": 18.0, "height": 19.0}, transform, pivot)
        self.assertAlmostEqual(x, 410.0)
        self.assertAlmostEqual(y, 134.92)

    def test_preview_embeds_everything_it_needs(self):
        with open(os.path.join(REPO, "widgets", "BugReportFormPopup.json")) as fh:
            page = json_to_html.build_preview(json.load(fh), 640, 480)
        self.assertIn("@font-face", page)
        self.assertIn("bhPreviewReady", page)
        self.assertNotIn("</script><", page.split("BH_PREVIEW")[1].split("\n")[0])


@unittest.skipUnless(CHROME, "no Chrome/Chromium found - set BH_CHROME to run fidelity tests")
class FidelityTests(unittest.TestCase):

    def check(self, widget, size, source_dir=os.path.join(REPO, "widgets")):
        with open(os.path.join(source_dir, widget + ".json")) as fh:
            doc = json.load(fh)
        fixture = os.path.join(FIXTURES, "%s.%dx%d.geometry.json" % ((widget,) + size))
        with open(fixture) as fh:
            geometry = json.load(fh)
        with tempfile.TemporaryDirectory() as tmp:
            page = os.path.join(tmp, "preview.html")
            with open(page, "w", encoding="utf-8") as fh:
                fh.write(json_to_html.build_preview(doc, *size))
            rows = measure_preview.compare(doc, geometry,
                                           measure_preview.collect_rects(page, CHROME))
        within, total = measure_preview.summarise(rows, TOLERANCE_PX)
        worst = sorted(rows, key=lambda r: -r[0])[:3]
        self.assertGreater(total, 0)
        self.assertGreaterEqual(
            within / total, REQUIRED_RATIO,
            "%s %dx%d: %d of %d within %gpx; worst %s"
            % (widget, size[0], size[1], within, total, TOLERANCE_PX,
               [(r[1], round(r[0], 1)) for r in worst]))

    def test_bug_report_form_small(self):
        self.check("BugReportForm", SIZES[0])

    def test_bug_report_form_large(self):
        self.check("BugReportForm", SIZES[1])

    def test_popup_small(self):
        self.check("BugReportFormPopup", SIZES[0])

    def test_popup_large(self):
        self.check("BugReportFormPopup", SIZES[1])

    def test_direction_b_layout_full_hd(self):
        # Every widget class the new full-screen widget uses, plus text that wraps
        # only from the second frame on.
        self.check("LayoutProbe", (1920, 1080), source_dir=FIXTURES)

    def test_direction_b_layout_steam_deck(self):
        self.check("LayoutProbe", (1280, 800), source_dir=FIXTURES)

    def test_plugin_fonts(self):
        # Barlow, Barlow Semi Condensed and JetBrains Mono at fractional pixel sizes
        # with letter spacing: none has a legacy kern table, so Slate does not kern
        # them although a browser would.
        self.check("FontProbe", SIZES[0], source_dir=FIXTURES)

    def test_a_layout_error_is_caught(self):
        # Guard the guard: a 10px padding change must fail the comparison.
        with open(os.path.join(REPO, "widgets", "BugReportFormPopup.json")) as fh:
            doc = json.load(fh)
        broken = copy.deepcopy(doc)
        broken["root"]["children"][0]["children"][0]["slot"]["properties"]["Padding"] = {
            "Left": 20.0, "Top": 20.0, "Right": 20.0, "Bottom": 20.0}
        with open(os.path.join(FIXTURES, "BugReportFormPopup.1280x860.geometry.json")) as fh:
            geometry = json.load(fh)
        with tempfile.TemporaryDirectory() as tmp:
            page = os.path.join(tmp, "preview.html")
            with open(page, "w", encoding="utf-8") as fh:
                fh.write(json_to_html.build_preview(broken, 1280, 860))
            rows = measure_preview.compare(doc, geometry,
                                           measure_preview.collect_rects(page, CHROME))
        within, total = measure_preview.summarise(rows, TOLERANCE_PX)
        self.assertLess(within, total)



class CompareTests(unittest.TestCase):
    """compare() without a browser: rects are given."""

    DOC = {"root": {"name": "Root", "class": "Overlay", "children": [
        {"name": "Shown", "class": "Border"},
        {"name": "Folded", "class": "VerticalBox", "children": [{"name": "Inside", "class": "TextBlock"}]}]}}

    def geometry(self, *widgets):
        return {"widgets": [dict(name=n, **{"class": c}, x=x, y=y, width=w, height=h) for n, c, x, y, w, h in widgets]}

    def test_collapsed_in_both_is_not_a_mismatch(self):
        # Unreal arranges the children of a collapsed widget at 0x0; the preview draws nothing.
        geometry = self.geometry(("Shown", "Border", 0, 0, 10, 10), ("Inside", "TextBlock", 0, 0, 0, 0))
        rows = measure_preview.compare(self.DOC, geometry, {"Shown": {"x": 0, "y": 0, "width": 10, "height": 10}})
        self.assertEqual([r[1] for r in rows], ["Shown"])

    def test_drawn_by_unreal_but_missing_is_a_mismatch(self):
        geometry = self.geometry(("Shown", "Border", 0, 0, 10, 10))
        rows = measure_preview.compare(self.DOC, geometry, {})
        self.assertEqual(rows[0][0], math.inf)

if __name__ == "__main__":
    unittest.main(verbosity=2)
