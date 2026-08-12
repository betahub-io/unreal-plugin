#!/usr/bin/env python3
"""Tests for the widget source tooling. Standard library only, no Unreal.

    python3 tools/tests/test_widget_tools.py

The slot-name collision test guards a real regression: slot names are unique
only within their owning widget, and resolving them globally produced a tree
with the right widget count but the wrong shape - the kind of bug that looks
fine until you read it closely.
"""

import copy
import json
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
REPO = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)

import t3d_to_json  # noqa: E402
import compare_widget_json as cmp_tool  # noqa: E402
import validate_widget_json as val_tool  # noqa: E402
import verify_widgets as verify_tool  # noqa: E402


def _obj(cls, name, parent_path, body=""):
    path = "/X/TestForm.TestForm:WidgetTree.%s" % name if parent_path is None \
        else "%s.%s" % (parent_path, name)
    return ('Begin Object Class=/Script/UMG.%s Name="%s" ExportPath='
            '"/Script/UMG.%s\'%s\'"\n%sEnd Object\n' % (cls, name, cls, path, body))


# Two HorizontalBoxes each owning a slot called HorizontalBoxSlot_0 - the exact
# shape that broke global slot lookup.
FIXTURE = """Begin Object Class=/Script/UMGEditor.WidgetBlueprint Name="TestForm" ExportPath="/Script/UMGEditor.WidgetBlueprint'/X/TestForm.TestForm'"
   Begin Object Class=/Script/UMG.WidgetTree Name="WidgetTree" ExportPath="/Script/UMG.WidgetTree'/X/TestForm.TestForm:WidgetTree'"
      Begin Object Class=/Script/UMG.VerticalBox Name="Root" ExportPath="/Script/UMG.VerticalBox'/X/TestForm.TestForm:WidgetTree.Root'"
         Begin Object Class=/Script/UMG.VerticalBoxSlot Name="VerticalBoxSlot_0" ExportPath="/Script/UMG.VerticalBoxSlot'/X/TestForm.TestForm:WidgetTree.Root.VerticalBoxSlot_0'"
         End Object
         Begin Object Class=/Script/UMG.VerticalBoxSlot Name="VerticalBoxSlot_1" ExportPath="/Script/UMG.VerticalBoxSlot'/X/TestForm.TestForm:WidgetTree.Root.VerticalBoxSlot_1'"
         End Object
      End Object
      Begin Object Class=/Script/UMG.HorizontalBox Name="BoxA" ExportPath="/Script/UMG.HorizontalBox'/X/TestForm.TestForm:WidgetTree.BoxA'"
         Begin Object Class=/Script/UMG.HorizontalBoxSlot Name="HorizontalBoxSlot_0" ExportPath="/Script/UMG.HorizontalBoxSlot'/X/TestForm.TestForm:WidgetTree.BoxA.HorizontalBoxSlot_0'"
         End Object
      End Object
      Begin Object Class=/Script/UMG.HorizontalBox Name="BoxB" ExportPath="/Script/UMG.HorizontalBox'/X/TestForm.TestForm:WidgetTree.BoxB'"
         Begin Object Class=/Script/UMG.HorizontalBoxSlot Name="HorizontalBoxSlot_0" ExportPath="/Script/UMG.HorizontalBoxSlot'/X/TestForm.TestForm:WidgetTree.BoxB.HorizontalBoxSlot_0'"
         End Object
      End Object
      Begin Object Class=/Script/UMG.TextBlock Name="LabelA" ExportPath="/Script/UMG.TextBlock'/X/TestForm.TestForm:WidgetTree.LabelA'"
      End Object
      Begin Object Class=/Script/UMG.TextBlock Name="LabelB" ExportPath="/Script/UMG.TextBlock'/X/TestForm.TestForm:WidgetTree.LabelB'"
      End Object
   End Object
   Begin Object Name="WidgetTree" ExportPath="/Script/UMG.WidgetTree'/X/TestForm.TestForm:WidgetTree'"
      Begin Object Name="Root" ExportPath="/Script/UMG.VerticalBox'/X/TestForm.TestForm:WidgetTree.Root'"
         Begin Object Name="VerticalBoxSlot_0" ExportPath="/Script/UMG.VerticalBoxSlot'/X/TestForm.TestForm:WidgetTree.Root.VerticalBoxSlot_0'"
            Content="/Script/UMG.HorizontalBox'TestForm:WidgetTree.BoxA'"
         End Object
         Begin Object Name="VerticalBoxSlot_1" ExportPath="/Script/UMG.VerticalBoxSlot'/X/TestForm.TestForm:WidgetTree.Root.VerticalBoxSlot_1'"
            Content="/Script/UMG.HorizontalBox'TestForm:WidgetTree.BoxB'"
         End Object
         Slots(0)="/Script/UMG.VerticalBoxSlot'VerticalBoxSlot_0'"
         Slots(1)="/Script/UMG.VerticalBoxSlot'VerticalBoxSlot_1'"
      End Object
      Begin Object Name="BoxA" ExportPath="/Script/UMG.HorizontalBox'/X/TestForm.TestForm:WidgetTree.BoxA'"
         Begin Object Name="HorizontalBoxSlot_0" ExportPath="/Script/UMG.HorizontalBoxSlot'/X/TestForm.TestForm:WidgetTree.BoxA.HorizontalBoxSlot_0'"
            Padding=(Left=1.000000,Top=2.000000)
            Content="/Script/UMG.TextBlock'TestForm:WidgetTree.LabelA'"
         End Object
         Slots(0)="/Script/UMG.HorizontalBoxSlot'HorizontalBoxSlot_0'"
      End Object
      Begin Object Name="BoxB" ExportPath="/Script/UMG.HorizontalBox'/X/TestForm.TestForm:WidgetTree.BoxB'"
         Begin Object Name="HorizontalBoxSlot_0" ExportPath="/Script/UMG.HorizontalBoxSlot'/X/TestForm.TestForm:WidgetTree.BoxB.HorizontalBoxSlot_0'"
            Content="/Script/UMG.TextBlock'TestForm:WidgetTree.LabelB'"
         End Object
         Slots(0)="/Script/UMG.HorizontalBoxSlot'HorizontalBoxSlot_0'"
      End Object
      Begin Object Name="LabelA" ExportPath="/Script/UMG.TextBlock'/X/TestForm.TestForm:WidgetTree.LabelA'"
         Text=NSLOCTEXT("", "KEYA", "Line one\\r\\nLine two")
         Font=(Size=20.000000,FontObject="/Script/Engine.Font'/Engine/EngineFonts/Roboto.Roboto'")
      End Object
      Begin Object Name="LabelB" ExportPath="/Script/UMG.TextBlock'/X/TestForm.TestForm:WidgetTree.LabelB'"
         Text=NSLOCTEXT("", "KEYB", "Quote \\" here")
         Visibility=Hidden
      End Object
      RootWidget="/Script/UMG.VerticalBox'TestForm:WidgetTree.Root'"
   End Object
   ParentClass="/Script/CoreUObject.Class'/Script/BetaHubBugReporter.BH_ReportFormWidget'"
End Object
"""


class ParserTests(unittest.TestCase):
    def setUp(self):
        self.doc = t3d_to_json.convert(FIXTURE)
        self.root = self.doc["root"]

    def kids(self, node):
        return {c["name"]: c for c in node.get("children", [])}

    def test_root_identified(self):
        self.assertEqual(self.root["name"], "Root")
        self.assertEqual(self.root["class"], "VerticalBox")

    def test_all_widgets_reachable(self):
        self.assertEqual(self.doc["_stats"]["widgetsReachable"],
                         self.doc["_stats"]["widgetsInTree"])

    def test_slot_names_are_resolved_per_widget(self):
        """Regression: BoxA and BoxB both own a HorizontalBoxSlot_0."""
        top = self.kids(self.root)
        self.assertEqual(sorted(top), ["BoxA", "BoxB"])
        self.assertEqual(list(self.kids(top["BoxA"])), ["LabelA"])
        self.assertEqual(list(self.kids(top["BoxB"])), ["LabelB"])

    def test_no_widget_appears_twice(self):
        seen = []

        def walk(n):
            seen.append(n["name"])
            for c in n.get("children", []):
                walk(c)

        walk(self.root)
        self.assertEqual(len(seen), len(set(seen)), "duplicated widgets: %s" % seen)

    def test_localised_text_keeps_key(self):
        label = self.kids(self.kids(self.root)["BoxA"])["LabelA"]
        self.assertEqual(label["properties"]["Text"]["__key__"], "KEYA")

    def test_string_escapes_decoded(self):
        label = self.kids(self.kids(self.root)["BoxA"])["LabelA"]
        self.assertEqual(label["properties"]["Text"]["__text__"],
                         "Line one\r\nLine two")

    def test_escaped_quote_decoded(self):
        label = self.kids(self.kids(self.root)["BoxB"])["LabelB"]
        self.assertEqual(label["properties"]["Text"]["__text__"], 'Quote " here')

    def test_object_reference_captured(self):
        label = self.kids(self.kids(self.root)["BoxA"])["LabelA"]
        font = label["properties"]["Font"]
        self.assertEqual(font["FontObject"]["__ref__"],
                         "/Engine/EngineFonts/Roboto.Roboto")
        self.assertEqual(font["Size"], 20.0)

    def test_enum_kept_as_token(self):
        label = self.kids(self.kids(self.root)["BoxB"])["LabelB"]
        self.assertEqual(label["properties"]["Visibility"], "Hidden")

    def test_slot_properties_attached_to_child(self):
        label = self.kids(self.kids(self.root)["BoxA"])["LabelA"]
        self.assertEqual(label["slot"]["class"], "HorizontalBoxSlot")
        self.assertEqual(label["slot"]["properties"]["Padding"],
                         {"Left": 1.0, "Top": 2.0})

    def test_structural_props_not_leaked(self):
        for banned in ("Slot", "Slots", "Content", "Parent"):
            self.assertNotIn(banned, self.root["properties"])


class CompareTests(unittest.TestCase):
    def setUp(self):
        self.a = t3d_to_json.convert(FIXTURE)

    def diff(self, b):
        out = []
        cmp_tool.diff_node("root", self.a["root"], b["root"], out)
        return out

    def test_identical_is_clean(self):
        self.assertEqual(self.diff(copy.deepcopy(self.a)), [])

    def test_detects_text_change(self):
        b = copy.deepcopy(self.a)
        b["root"]["children"][0]["children"][0]["properties"]["Text"]["__text__"] = "x"
        self.assertEqual(len(self.diff(b)), 1)

    def test_detects_removed_property(self):
        b = copy.deepcopy(self.a)
        del b["root"]["children"][0]["children"][0]["properties"]["Font"]
        self.assertTrue(self.diff(b))

    def test_detects_child_count_change(self):
        b = copy.deepcopy(self.a)
        b["root"]["children"].pop()
        self.assertTrue(self.diff(b))

    def test_float_tolerance(self):
        b = copy.deepcopy(self.a)
        slot = b["root"]["children"][0]["children"][0]["slot"]
        slot["properties"]["Padding"]["Left"] = 1.000001
        self.assertEqual(self.diff(b), [])

    def test_float_beyond_tolerance_detected(self):
        b = copy.deepcopy(self.a)
        slot = b["root"]["children"][0]["children"][0]["slot"]
        slot["properties"]["Padding"]["Left"] = 1.5
        self.assertEqual(len(self.diff(b)), 1)


class ValidatorTests(unittest.TestCase):
    """Runs against the real committed sources, so drift in either is caught."""

    @classmethod
    def setUpClass(cls):
        cls.schema = val_tool.load_schema()
        cls.widgets_dir = os.path.join(REPO, "widgets")

    def real_files(self):
        return [os.path.join(self.widgets_dir, f)
                for f in sorted(os.listdir(self.widgets_dir))
                if f.endswith(".json") and not f.startswith("_")]

    def test_committed_sources_validate(self):
        for path in self.real_files():
            rep, _seen = val_tool.validate(path, self.schema)
            self.assertTrue(rep.ok, "%s: %s" % (path, rep.errors))

    def test_schema_knows_the_classes_in_use(self):
        for path in self.real_files():
            with open(path, "r", encoding="utf-8") as fh:
                doc = json.load(fh)

            def walk(n):
                self.assertIn(n["class"], self.schema["widgets"],
                              "%s uses unknown class %s" % (path, n["class"]))
                for c in n.get("children", []):
                    walk(c)

            walk(doc["root"])

    def test_bindwidget_properties_are_found(self):
        binds, header = val_tool.find_bind_widgets("BH_ReportFormWidget")
        self.assertIsNotNone(header, "parent header not located")
        self.assertIn("SubmitLabel", binds)
        self.assertIn("SuggestionCheckBox", binds)
        self.assertEqual(binds["SubmitLabel"]["type"], "TextBlock")


class AssetVersionTests(unittest.TestCase):
    """The version check is the last line of defence against shipping an asset
    UE 5.3 cannot load, so it gets tested against fabricated headers rather
    than only against files that happen to be correct."""

    @staticmethod
    def header(ue4, ue5, legacy=-8, tag=0x9E2A83C1):
        import struct
        return (struct.pack("<Ii", tag, legacy) + struct.pack("<i", 0)
                + struct.pack("<ii", ue4, ue5) + b"\x00" * 32)

    def write(self, blob):
        import tempfile
        fh = tempfile.NamedTemporaryFile(suffix=".uasset", delete=False)
        fh.write(blob)
        fh.close()
        self.addCleanup(os.unlink, fh.name)
        return fh.name

    def test_reads_a_53_header(self):
        path = self.write(self.header(522, 1009))
        self.assertEqual(verify_tool.asset_versions(path), (522, 1009))

    def test_reads_a_newer_header(self):
        path = self.write(self.header(522, 1012))
        _ue4, ue5 = verify_tool.asset_versions(path)
        self.assertNotEqual(ue5, verify_tool.EXPECTED_UE5_VERSION)

    def test_rejects_a_non_uasset(self):
        path = self.write(b"not an unreal asset at all, no magic here......")
        with self.assertRaises(ValueError):
            verify_tool.asset_versions(path)

    def test_real_assets_are_at_the_53_floor(self):
        import glob
        assets = glob.glob(os.path.join(REPO, "Content", "*.uasset"))
        self.assertTrue(assets, "no assets found to check")
        for path in assets:
            _ue4, ue5 = verify_tool.asset_versions(path)
            self.assertEqual(ue5, verify_tool.EXPECTED_UE5_VERSION,
                             "%s was saved by a newer engine" % path)


if __name__ == "__main__":
    unittest.main(verbosity=2)
