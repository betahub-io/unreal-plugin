"""Engine-side half of the pre-release widget check.

Asserts, for each widget asset:
  * the blueprint compiles
  * every required meta=(BindWidget) property has a widget of that exact name
    and compatible type in the authored tree - the same rule the widget
    compiler applies, checked against the real asset rather than the header
  * exports .T3D so the local half can diff it against widgets/*.json

Runs as a plain commandlet - no Slate or RHI needed, so it is fast:

    UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript \
        -script="tools/ue_verify_widgets.py" \
        -EnablePlugins=PythonScriptPlugin -unattended -nosplash -nopause -stdout

Reads bh_verify_config.json from its own directory - produce it with
`python3 tools/verify_widgets.py --emit-config`, so both halves agree on
which bindings are required.
"""

import json
import os
import traceback

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(HERE, "bh_verify_config.json")

report = []
problems = []


def log(m):
    report.append(str(m))


def problem(m):
    problems.append(str(m))
    report.append("PROBLEM: %s" % m)


def mark(m):
    unreal.log_error("BH_VERIFY: %s" % m)
    log(">>> %s" % m)


out_dir = HERE
try:
    with open(CONFIG_PATH, "r", encoding="utf-8") as fh:
        cfg = json.load(fh)
    out_dir = cfg.get("outDir") or HERE
    os.makedirs(out_dir, exist_ok=True)

    engine = unreal.SystemLibrary.get_engine_version()
    log("engine: %s" % engine)
    if not engine.startswith("5.3."):
        problem("verification must run under UE 5.3 (got %s) - a newer engine "
                "would report success for assets 5.3 cannot load" % engine)

    for spec in cfg.get("widgets", []):
        asset_path = spec["asset"]
        name = asset_path.rsplit("/", 1)[-1]
        mark("CHECK %s" % name)

        try:
            blueprint = unreal.load_asset(asset_path)
            if blueprint is None:
                problem("%s: asset not found" % asset_path)
                continue

            unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)

            widget_class = blueprint.generated_class()
            if widget_class is None:
                problem("%s: no generated class after compile" % name)
                continue

            # Apply the rule the widget compiler itself applies: a widget of
            # this name and a compatible type must exist in the AUTHORED tree.
            # Checking a constructed instance would be more direct, but binding
            # happens inside CreateWidget's initialise, which needs a world and
            # Slate - and this check needs neither.
            bindings = spec.get("bindWidgets") or {}
            tree_path = "%s.%s:WidgetTree" % (asset_path, name)
            for prop_name, expected_type in sorted(bindings.items()):
                widget = unreal.load_object(None, "%s.%s" % (tree_path, prop_name))
                if widget is None:
                    problem("%s: BindWidget %s (%s) has no widget of that name - "
                            "compiles, then null at runtime"
                            % (name, prop_name, expected_type))
                    continue
                actual = widget.get_class().get_name()
                if actual != expected_type:
                    problem("%s: BindWidget %s expects %s but the tree has %s"
                            % (name, prop_name, expected_type, actual))
            if bindings:
                log("%s: checked %d required bindings" % (name, len(bindings)))

            unreal.AssetToolsHelpers.get_asset_tools().export_assets(
                [asset_path], out_dir)
            log("%s: exported .T3D" % name)

        except Exception:
            problem("%s:\n%s" % (name, traceback.format_exc()))

except Exception:
    problem("FATAL:\n%s" % traceback.format_exc())

log("")
log("problems: %d" % len(problems))
for p in problems:
    log("  %s" % p)

result = "PASS" if not problems else "FAIL"
log("BH_VERIFY_RESULT: %s" % result)

try:
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "verify_report.txt"), "w",
              encoding="utf-8") as fh:
        fh.write("\n".join(report))
except Exception:
    pass

unreal.log_error("BH_VERIFY_RESULT: %s (%d problems)" % (result, len(problems)))
