"""Dump UMG's widget class catalogue to JSON so validation can run offline.

Runs inside the editor, once per engine version. The result
(tools/umg_schema.json) lets tools/validate_widget_json.py check widget
classes, container rules and slot types on a developer machine in under a
second, instead of after a ~60s editor round trip.

    UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript \
        -script="tools/ue_dump_umg_schema.py" -EnablePlugins=PythonScriptPlugin \
        -unattended -nosplash -nopause -stdout

Writes to bh_umg_schema.json beside this script.

Slot classes are discovered empirically: build a throwaway panel, add a
throwaway child, and read back what slot Unreal created. That is more
trustworthy than inferring from class names.
"""

import json
import os
import traceback

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "bh_umg_schema.json")

lines = []


def log(m):
    lines.append(str(m))


def widget_classes():
    """Every unreal.* name that is a UWidget subclass."""
    found = {}
    for name in dir(unreal):
        if name.startswith("_"):
            continue
        try:
            obj = getattr(unreal, name)
        except Exception:
            continue
        try:
            if isinstance(obj, type) and issubclass(obj, unreal.Widget):
                found[name] = obj
        except Exception:
            continue
    return found


schema = {"engine": None, "widgets": {}}

try:
    schema["engine"] = unreal.SystemLibrary.get_engine_version()
    classes = widget_classes()
    log("found %d widget classes" % len(classes))

    # A cheap, always-constructible child for probing slot types.
    probe_parent = None

    for name, cls in sorted(classes.items()):
        entry = {
            "isPanel": False,
            "isContent": False,
            "maxChildren": 0,
            "slotClass": None,
        }
        try:
            entry["isPanel"] = issubclass(cls, unreal.PanelWidget)
        except Exception:
            pass
        try:
            entry["isContent"] = issubclass(cls, unreal.ContentWidget)
        except Exception:
            pass

        if entry["isPanel"]:
            # ContentWidget (Button, Border, ...) holds exactly one child.
            entry["maxChildren"] = 1 if entry["isContent"] else -1  # -1 = unbounded
            try:
                panel = unreal.new_object(cls)
                child = unreal.new_object(unreal.Spacer)
                slot = panel.add_child(child)
                if slot is not None:
                    entry["slotClass"] = slot.get_class().get_name()
            except Exception as exc:
                log("slot probe failed for %s: %s" % (name, exc))

        schema["widgets"][name] = entry

    panels = [n for n, e in schema["widgets"].items() if e["isPanel"]]
    with_slots = [n for n in panels if schema["widgets"][n]["slotClass"]]
    log("panels: %d, of which slot class resolved: %d" % (len(panels), len(with_slots)))

except Exception:
    log("FAILED:\n%s" % traceback.format_exc())

schema["_log"] = lines

with open(OUT, "w", encoding="utf-8") as fh:
    json.dump(schema, fh, indent=2, sort_keys=True)

unreal.log_error("BH_SCHEMA: wrote %s (%d classes)"
                 % (OUT, len(schema.get("widgets", {}))))
