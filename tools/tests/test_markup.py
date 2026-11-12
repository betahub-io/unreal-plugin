#!/usr/bin/env python3
"""Tests for the UMG markup compiler (tools/umg_markup.py). Standard library only.

    python3 tools/tests/test_markup.py

The round-trip tests are the expressiveness gate: both shipped widgets go
JSON -> markup -> JSON and must come back identical, so anything the JSON
format can say, the markup can say too.
"""

import json
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
REPO = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)

import compare_widget_json  # noqa: E402
import umg_markup  # noqa: E402

SHIPPED = ("BugReportForm", "BugReportFormPopup")


def compile_text(body, styles=None, asset="Test", parent="UserWidget"):
    markup = '<Widget asset="%s" parent="%s">%s</Widget>' % (asset, parent, body)
    return umg_markup.compile_markup(markup, styles=styles)


def first_child(doc):
    return doc["root"]["children"][0]


class RoundTripTests(unittest.TestCase):

    def test_shipped_widgets_round_trip_exactly(self):
        for name in SHIPPED:
            with self.subTest(widget=name):
                with open(os.path.join(REPO, "widgets", name + ".json")) as fh:
                    original = json.load(fh)
                markup = umg_markup.decompile(original)
                rebuilt = umg_markup.compile_markup(markup)
                diffs = []
                compare_widget_json.diff_node("root", original["root"], rebuilt["root"], diffs)
                self.assertEqual(diffs, [])
                self.assertEqual(rebuilt, original)

    def test_decompiled_markup_is_shorter_than_the_json(self):
        with open(os.path.join(REPO, "widgets", "BugReportForm.json")) as fh:
            original = json.load(fh)
        markup = umg_markup.decompile(original)
        self.assertLess(markup.count("\n"), json.dumps(original, indent=2).count("\n") / 2)


class CommittedSourceTests(unittest.TestCase):

    def test_compiled_json_matches_its_markup(self):
        for directory in (os.path.join(REPO, "widgets"), os.path.join(HERE, "fixtures")):
            with self.subTest(directory=directory):
                self.assertEqual(umg_markup.check(directory), [])


class ValueTests(unittest.TestCase):

    def test_margin_shorthands_follow_fmargin_constructors(self):
        doc = compile_text('<VerticalBox><TextBlock slot.Padding="10"/>'
                           '<TextBlock slot.Padding="10 20"/>'
                           '<TextBlock slot.Padding="1 2 3 4"/></VerticalBox>')
        pads = [c["slot"]["properties"]["Padding"] for c in doc["root"]["children"]]
        self.assertEqual(pads[0], {"Left": 10.0, "Top": 10.0, "Right": 10.0, "Bottom": 10.0})
        self.assertEqual(pads[1], {"Left": 10.0, "Top": 20.0, "Right": 10.0, "Bottom": 20.0})
        self.assertEqual(pads[2], {"Left": 1.0, "Top": 2.0, "Right": 3.0, "Bottom": 4.0})

    def test_dotted_field_stays_partial(self):
        # A single field must not drag the others in as zeros - absent means default.
        doc = compile_text('<VerticalBox><TextBlock slot.Padding.Bottom="10"/></VerticalBox>')
        self.assertEqual(first_child(doc)["slot"]["properties"]["Padding"], {"Bottom": 10.0})

    def test_hex_colour_is_converted_to_linear(self):
        doc = compile_text('<Border BrushColor="#FF7A45"/>')
        c = doc["root"]["properties"]["BrushColor"]
        self.assertAlmostEqual(c["R"], 1.0)
        self.assertAlmostEqual(c["G"], 0.194618, places=5)
        self.assertAlmostEqual(c["B"], 0.059511, places=5)
        self.assertEqual(c["A"], 1.0)

    def test_slate_colour_gets_specified_rule(self):
        doc = compile_text('<TextBlock ColorAndOpacity="#000000"/>')
        self.assertEqual(doc["root"]["properties"]["ColorAndOpacity"],
                         {"SpecifiedColor": {"R": 0.0, "G": 0.0, "B": 0.0, "A": 1.0},
                          "ColorUseRule": "UseColor_Specified"})

    def test_short_enum_names_expand(self):
        doc = compile_text('<VerticalBox><TextBlock slot.HorizontalAlignment="Center" '
                           'slot.VerticalAlignment="Bottom"/></VerticalBox>')
        slot = first_child(doc)["slot"]["properties"]
        self.assertEqual(slot["HorizontalAlignment"], "HAlign_Center")
        self.assertEqual(slot["VerticalAlignment"], "VAlign_Bottom")

    def test_size_shorthand(self):
        doc = compile_text('<VerticalBox><Spacer slot.Size="Fill"/>'
                           '<Spacer slot.Size="Fill 2"/></VerticalBox>')
        kids = doc["root"]["children"]
        self.assertEqual(kids[0]["slot"]["properties"]["Size"], {"SizeRule": "Fill"})
        self.assertEqual(kids[1]["slot"]["properties"]["Size"], {"SizeRule": "Fill", "Value": 2.0})

    def test_text_content_and_key(self):
        doc = compile_text('<TextBlock key="K1">Hello\nthere</TextBlock>')
        self.assertEqual(doc["root"]["properties"]["Text"],
                         {"__text__": "Hello\nthere", "__key__": "K1"})

    def test_text_without_key_has_none(self):
        doc = compile_text('<TextBlock>Hi</TextBlock>')
        self.assertEqual(doc["root"]["properties"]["Text"], {"__text__": "Hi"})

    def test_slot_class_comes_from_the_parent(self):
        doc = compile_text('<Overlay><TextBlock/></Overlay>')
        self.assertEqual(first_child(doc)["slot"]["class"], "OverlaySlot")

    def test_unnamed_widgets_get_stable_names(self):
        a = compile_text('<VerticalBox><TextBlock/><TextBlock/></VerticalBox>')
        b = compile_text('<VerticalBox><TextBlock/><TextBlock/></VerticalBox>')
        names = [c["name"] for c in a["root"]["children"]]
        self.assertEqual(len(set(names)), 2)
        self.assertEqual(names, [c["name"] for c in b["root"]["children"]])


class MirrorTests(unittest.TestCase):

    def test_border_padding_reaches_the_child_slot(self):
        doc = compile_text('<Border Padding="12" HorizontalAlignment="Center"><TextBlock/></Border>')
        slot = first_child(doc)["slot"]["properties"]
        self.assertEqual(slot["Padding"], doc["root"]["properties"]["Padding"])
        self.assertEqual(slot["HorizontalAlignment"], "HAlign_Center")

    def test_child_slot_padding_reaches_the_border(self):
        doc = compile_text('<Border><TextBlock slot.Padding="8"/></Border>')
        self.assertEqual(doc["root"]["properties"]["Padding"]["Left"], 8.0)

    def test_contradicting_sides_are_refused(self):
        with self.assertRaises(umg_markup.MarkupError):
            compile_text('<Border Padding="12"><TextBlock slot.Padding="8"/></Border>')

    def test_corner_radii_shorthand(self):
        doc = compile_text('<Border Background.OutlineSettings.CornerRadii="10"/>')
        self.assertEqual(doc["root"]["properties"]["Background"]["OutlineSettings"]["CornerRadii"],
                         {"X": 10.0, "Y": 10.0, "Z": 10.0, "W": 10.0})


class RuntimeStyleTests(unittest.TestCase):

    SHEET = """
        :root { --bh-accent: #FF7A59; --bh-radius: 10; --brand: var(--bh-accent); --gap: 4; }
        Border.focused { Background.TintColor: var(--bh-accent); Padding: 15 12;
                         Background.OutlineSettings.CornerRadii: var(--bh-radius); }
        .plain { Padding: 1; }
    """

    def write(self, tmp, markup, sheet=None):
        with open(os.path.join(tmp, "s.umg.css"), "w") as fh:
            fh.write(sheet or self.SHEET)
        with open(os.path.join(tmp, "T.umg.xml"), "w") as fh:
            fh.write('<Widget asset="T" parent="UserWidget" styles="s.umg.css">%s</Widget>' % markup)

    def test_theme_bindings_follow_aliases(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.write(tmp, '<Border name="Box" class="plain" BrushColor="var(--brand)"/>')
            assets, rules = umg_markup.collect_runtime_styles([os.path.join(tmp, "T.umg.xml")])
        self.assertEqual(assets[0]["bindings"], [("Box", "BrushColor", "accent")])
        self.assertEqual(assets[0]["classes"], [("Box", ["plain"])])
        self.assertEqual(assets[0]["sheet"], "s")

    def test_later_literal_drops_the_binding(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.write(tmp, '<Border name="Box" BrushColor="var(--bh-accent)"/>',
                       self.SHEET + "Border { BrushColor: #000000; }")
            assets, _ = umg_markup.collect_runtime_styles([os.path.join(tmp, "T.umg.xml")])
        # The inline attribute comes last, so the token wins and stays bound.
        self.assertEqual(assets[0]["bindings"], [("Box", "BrushColor", "accent")])

    def test_runtime_rules_are_typed_and_finished(self):
        rules = umg_markup.runtime_rules("s", self.SHEET)
        self.assertEqual(len(rules), 1)  # .plain has no type, so it is compile-time only
        decls = rules[0]["decls"]
        self.assertIn(("Background.TintColor", None, "accent"), decls)
        self.assertIn(("Padding", "(Left=15.000000,Top=12.000000,Right=15.000000,Bottom=12.000000)", None), decls)
        self.assertIn(("Background.OutlineSettings.CornerRadii", None, "radius"), decls)

    def test_theme_token_inside_a_value_is_refused(self):
        with self.assertRaises(umg_markup.MarkupError):
            umg_markup.runtime_rules("s", ":root { --bh-radius: 10; } Border.x { Padding: 0 var(--bh-radius); }")

    def test_runtime_state_class_in_markup_is_refused(self):
        # Border.focused is a state the game switches; baked into the asset it could never be
        # switched off, because the runtime only knows the asset's own values.
        with self.assertRaises(umg_markup.MarkupError) as ctx:
            compile_text('<Border name="Box" class="plain focused"/>', styles=self.SHEET)
        self.assertIn("focused", str(ctx.exception))

    def test_runtime_rules_cannot_set_slot_properties(self):
        with self.assertRaises(umg_markup.MarkupError):
            umg_markup.runtime_rules("s", "TextBlock.x { slot.Padding: 4; }")

    def test_unknown_theme_token_is_refused(self):
        with self.assertRaises(umg_markup.MarkupError):
            compile_text('<Border BrushColor="var(--bh-nope)"/>', styles=":root { --bh-nope: #000000; }")

    def test_tables_render_and_stale_tables_are_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.write(tmp, '<Border name="Box" BrushColor="var(--bh-accent)"/>')
            inl = umg_markup.styles_inl(tmp)
            self.assertIn('{TEXT("T"), TEXT("Box"), TEXT("BrushColor"), TEXT("accent")},', inl)
            self.assertIn('{TEXT("s"), TEXT("Border"), TEXT("focused"), 101,', inl)
            out = os.path.join(tmp, "styles.inl")
            with open(out, "w") as fh:
                fh.write(inl)
            with open(os.path.join(tmp, "T.json"), "w") as fh:
                json.dump(umg_markup.compile_file(os.path.join(tmp, "T.umg.xml")), fh)
            self.assertEqual(umg_markup.check(tmp, out), [])
            with open(out, "w") as fh:
                fh.write("// stale")
            self.assertEqual(len(umg_markup.check(tmp, out)), 1)


class StyleTests(unittest.TestCase):

    STYLES = """
        /* tokens */
        :root { --accent: #FF7A45; --gap: 12; }
        TextBlock { Font.Size: 20; }
        .title { Font.Size: 36; ColorAndOpacity: var(--accent); }
        #Special { Font.Size: 48; }
        .spaced { slot.Padding: 0 0 0 var(--gap); }
    """

    def test_type_class_id_precedence_then_inline(self):
        doc = compile_text('<VerticalBox>'
                           '<TextBlock name="A"/>'
                           '<TextBlock name="B" class="title"/>'
                           '<TextBlock name="Special" class="title"/>'
                           '<TextBlock name="D" class="title" Font.Size="10"/>'
                           '</VerticalBox>', styles=self.STYLES)
        sizes = [c["properties"]["Font"]["Size"] for c in doc["root"]["children"]]
        self.assertEqual(sizes, [20.0, 36.0, 48.0, 10.0])

    def test_tokens_resolve_in_styles_and_attributes(self):
        doc = compile_text('<VerticalBox><TextBlock class="spaced" '
                           'ColorAndOpacity="var(--accent)"/></VerticalBox>', styles=self.STYLES)
        kid = first_child(doc)
        self.assertEqual(kid["slot"]["properties"]["Padding"]["Bottom"], 12.0)
        self.assertAlmostEqual(kid["properties"]["ColorAndOpacity"]["SpecifiedColor"]["G"],
                               0.194618, places=5)

    def test_styles_file_is_found_next_to_the_markup(self):
        with tempfile.TemporaryDirectory() as tmp:
            with open(os.path.join(tmp, "t.umg.css"), "w") as fh:
                fh.write(".big { Font.Size: 40; }")
            path = os.path.join(tmp, "T.umg.xml")
            with open(path, "w") as fh:
                fh.write('<Widget asset="T" parent="UserWidget" styles="t.umg.css">'
                         '<TextBlock class="big"/></Widget>')
            doc = umg_markup.compile_file(path)
        self.assertEqual(doc["root"]["properties"]["Font"]["Size"], 40.0)


class ErrorTests(unittest.TestCase):

    def assertRefused(self, body, styles=None):
        with self.assertRaises(umg_markup.MarkupError):
            compile_text(body, styles=styles)

    def test_undefined_token(self):
        self.assertRefused('<Border BrushColor="var(--nope)"/>')

    def test_scalar_for_a_struct_without_shorthand(self):
        self.assertRefused('<TextBlock Font="Big"/>')

    def test_bad_margin(self):
        self.assertRefused('<VerticalBox><TextBlock slot.Padding="1 2 3"/></VerticalBox>')

    def test_text_on_a_widget_without_text(self):
        self.assertRefused('<VerticalBox>hello</VerticalBox>')

    def test_slot_attribute_on_the_root(self):
        self.assertRefused('<TextBlock slot.Padding="1"/>')

    def test_children_under_a_leaf(self):
        self.assertRefused('<TextBlock><TextBlock/></TextBlock>')

    def test_unknown_enum_value(self):
        self.assertRefused('<VerticalBox><TextBlock slot.HorizontalAlignment="Middle"/></VerticalBox>')

    def test_unparseable_style(self):
        self.assertRefused('<TextBlock/>', styles=".a { Font.Size 20 }")

    def test_misplaced_property_points_to_where_it_lives(self):
        with self.assertRaises(umg_markup.MarkupError) as ctx:
            compile_text('<TextBlock LetterSpacing="20"/>')
        self.assertIn("Font.LetterSpacing", str(ctx.exception))

    def test_misspelt_struct_field(self):
        with self.assertRaises(umg_markup.MarkupError) as ctx:
            compile_text('<TextBlock Font.Szie="20"/>')
        self.assertIn("Size", str(ctx.exception))

    def test_misspelt_slot_property(self):
        self.assertRefused('<VerticalBox><TextBlock slot.Paddding="1"/></VerticalBox>')

    def test_duplicate_names(self):
        self.assertRefused('<VerticalBox><TextBlock name="X"/><TextBlock name="X"/></VerticalBox>')


if __name__ == "__main__":
    unittest.main(verbosity=2)
