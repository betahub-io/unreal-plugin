"""Render widget blueprints to PNG and dump the geometry Unreal computed.

Requires the BetaHubWidgetGen developer module (tools/widgetgen) and a real
RHI - do NOT pass -nullrhi.

    UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript \
        -script="tools/ue_render_widgets.py" -EnablePlugins=PythonScriptPlugin \
        -unattended -nosplash -nopause -stdout

Reads bh_render_config.json from its own directory:

    {"assets": ["/BetaHubBugReporter/BugReportForm",
                "/Game/BH_Gen/BugReportForm_Gen"],
     "width": 1280, "height": 860,
     "outDir": "C:/.../renders"}

Output per asset: <name>.png and <name>.geometry.json.

The geometry is the point: almost no widget stores its own size, so this is
the only way to know where things actually ended up.
"""

import json
import os
import traceback

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(HERE, "bh_render_config.json")

lines = []
problems = []


def log(m):
    lines.append(str(m))


def problem(m):
    problems.append(str(m))
    lines.append("PROBLEM: %s" % m)


cfg = {}
out_dir = HERE
try:
    with open(CONFIG_PATH, "r", encoding="utf-8") as fh:
        cfg = json.load(fh)

    assets = cfg.get("assets") or []
    width = int(cfg.get("width", 1280))
    height = int(cfg.get("height", 860))
    out_dir = cfg.get("outDir") or HERE

    helper = getattr(unreal, "BHWidgetGenLibrary", None)
    if helper is None:
        raise RuntimeError(
            "BHWidgetGenLibrary not loaded - run tools/widgetgen/install.sh "
            "against this project and rebuild its editor target")
    if not hasattr(helper, "render_widget_and_dump_geometry"):
        raise RuntimeError(
            "BHWidgetGenLibrary is loaded but has no "
            "render_widget_and_dump_geometry - the module is stale, rebuild it")

    os.makedirs(out_dir, exist_ok=True)
    log("engine  : %s" % unreal.SystemLibrary.get_engine_version())
    log("drawSize: %dx%d" % (width, height))

    for asset_path in assets:
        name = asset_path.rsplit("/", 1)[-1]
        try:
            blueprint = unreal.load_asset(asset_path)
            if blueprint is None:
                raise RuntimeError("asset not found")
            widget_class = blueprint.generated_class()
            if widget_class is None:
                raise RuntimeError("no generated class")

            ok, err = helper.render_widget_and_dump_geometry(
                widget_class, width, height, out_dir,
                "%s.png" % name, "%s.geometry.json" % name)

            if ok:
                log("rendered %s" % asset_path)
            else:
                problem("%s: %s" % (asset_path, err))
        except Exception:
            problem("%s:\n%s" % (asset_path, traceback.format_exc()))

except Exception:
    problem("FATAL:\n%s" % traceback.format_exc())

log("")
log("problems: %d" % len(problems))
for p in problems:
    log("  %s" % p)

result = "OK" if not problems else "PROBLEMS"
log("BH_RENDER_RESULT: %s" % result)

try:
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "render_report.txt"), "w",
              encoding="utf-8") as fh:
        fh.write("\n".join(lines))
except Exception:
    pass

unreal.log_error("BH_RENDER_RESULT: %s (%d problems)" % (result, len(problems)))
