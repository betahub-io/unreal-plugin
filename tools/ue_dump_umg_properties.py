"""Dump every reflected property of the UMG widget and slot classes.

tools/umg_markup.py checks property paths against the result, so a misspelt or
misplaced property (LetterSpacing on a TextBlock instead of Font.LetterSpacing)
fails at compile time on any machine, not after a generator run on the box.

Runs inside the editor, once per engine version, after umg_schema.json (whose
class list it reads):

    UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript \
        -script="tools/ue_dump_umg_properties.py" -EnablePlugins=PythonScriptPlugin \
        -unattended -nosplash -nopause -stdout

Writes bh_umg_properties.json beside this script; copy it to
tools/umg_properties.json. Needs BHWidgetGenLibrary (tools/widgetgen).
"""

import json
import os
import traceback

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
SCHEMA = os.path.join(HERE, "umg_schema.json")
OUT = os.path.join(HERE, "bh_umg_properties.json")

lines = []
try:
    with open(SCHEMA, "r", encoding="utf-8") as fh:
        widgets = json.load(fh)["widgets"]
    names = set(widgets) | {w["slotClass"] for w in widgets.values() if w.get("slotClass")}
    names.add("Widget")
    classes = []
    for name in sorted(names):
        cls = getattr(unreal, name, None)
        if cls is None:
            lines.append("no unreal.%s" % name)
            continue
        classes.append(cls.static_class())
    described = json.loads(unreal.BHWidgetGenLibrary.describe_properties_json(classes))
    described["engine"] = unreal.SystemLibrary.get_engine_version()
    with open(OUT, "w", encoding="utf-8") as fh:
        json.dump(described, fh, indent=1, sort_keys=True)
    lines.append("classes %d, structs %d" % (len(described["classes"]),
                                             len(described["structs"])))
    lines.append("BH_PROPERTIES_RESULT: OK")
except Exception:
    lines.append("BH_PROPERTIES_RESULT: FAILED\n%s" % traceback.format_exc())

with open(os.path.join(HERE, "bh_umg_properties_report.txt"), "w", encoding="utf-8") as fh:
    fh.write("\n".join(lines))
unreal.log_error(lines[-1].splitlines()[0])
