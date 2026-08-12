"""Generate, export and render widgets in a single editor session.

Doing this as one run rather than three matters twice over: it removes two
editor boots (~7s each), and it makes it impossible to render a stale asset
left over from an earlier generate.

MUST run under UE 5.3, and needs Slate for the render pass, so it has to boot
the editor rather than run as a commandlet:

    UnrealEditor-Cmd.exe <project>.uproject \
        -ExecutePythonScript="tools/ue_build_widgets.py" \
        -EnablePlugins=PythonScriptPlugin -AllowCommandletRendering \
        -unattended -nosplash -nopause -stdout

Reads bh_build_config.json from its own directory:

    {"widgets": [{"json": "C:/.../BugReportForm.json",
                  "seed": "/BetaHubBugReporter/BugReportForm",
                  "target": "/Game/BH_Gen/BugReportForm_Gen"}],
     "outDir": "C:/.../build",
     "width": 1280, "height": 860,
     "renderOriginals": true}

Per widget it writes <name>.T3D, <name>.png and <name>.geometry.json, plus the
same for the seed asset when renderOriginals is set - which is what makes an
original-vs-generated comparison possible without a second trip.
"""

import json
import os
import sys
import traceback

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

CONFIG_PATH = os.path.join(HERE, "bh_build_config.json")

import ue_generate_widgets as generator  # noqa: E402

report = []
failures = []


def log(m):
    report.append(str(m))


def fail(m):
    failures.append(str(m))
    report.append("FAILURE: %s" % m)


def mark(m):
    unreal.log_error("BH_BUILD: %s" % m)
    log(">>> %s" % m)


def render(asset_path, out_dir, width, height, label):
    """Render + geometry for one asset. Returns True on success."""
    helper = getattr(unreal, "BHWidgetGenLibrary", None)
    if helper is None:
        fail("BHWidgetGenLibrary not loaded - run tools/widgetgen/install.sh "
             "and rebuild the host project's editor target")
        return False

    try:
        blueprint = unreal.load_asset(asset_path)
        if blueprint is None:
            fail("%s: asset not found" % asset_path)
            return False
        widget_class = blueprint.generated_class()
        if widget_class is None:
            fail("%s: no generated class" % asset_path)
            return False

        ok, err = helper.render_widget_and_dump_geometry(
            widget_class, width, height, out_dir,
            "%s.png" % label, "%s.geometry.json" % label)
        if not ok:
            fail("%s: render failed: %s" % (asset_path, err))
            return False
        log("rendered %s -> %s.png" % (asset_path, label))
        return True
    except Exception:
        fail("%s:\n%s" % (asset_path, traceback.format_exc()))
        return False


def export_t3d(asset_path, out_dir):
    try:
        unreal.AssetToolsHelpers.get_asset_tools().export_assets(
            [asset_path], out_dir)
        log("exported T3D for %s" % asset_path)
        return True
    except Exception:
        fail("%s: T3D export failed:\n%s" % (asset_path, traceback.format_exc()))
        return False


try:
    with open(CONFIG_PATH, "r", encoding="utf-8") as fh:
        cfg = json.load(fh)

    widgets = cfg.get("widgets") or []
    out_dir = cfg["outDir"]
    width = int(cfg.get("width", 1280))
    height = int(cfg.get("height", 860))
    render_originals = bool(cfg.get("renderOriginals", True))

    engine = unreal.SystemLibrary.get_engine_version()
    log("engine : %s" % engine)
    if not engine.startswith("5.3.") and not cfg.get("allowAnyEngine"):
        raise RuntimeError(
            "refusing to build on engine %s - widgets must be generated under "
            "UE 5.3 or the output cannot be loaded by 5.3 users" % engine)

    os.makedirs(out_dir, exist_ok=True)

    for spec in widgets:
        target = spec["target"]
        label = target.rsplit("/", 1)[-1]
        seed = spec["seed"]

        mark("GENERATE %s" % label)
        gen_cfg = dict(spec)
        gen_cfg["exportDir"] = out_dir
        ok, problems = generator.run(gen_cfg)
        for p in problems:
            fail("%s: %s" % (label, p))
        if not ok:
            fail("%s: generation failed, skipping render" % label)
            continue

        mark("RENDER %s" % label)
        render(target, out_dir, width, height, label)

        if render_originals:
            mark("RENDER original %s" % seed)
            seed_label = "%s_original" % seed.rsplit("/", 1)[-1]
            render(seed, out_dir, width, height, seed_label)
            export_t3d(seed, os.path.join(out_dir, "original_t3d"))

except Exception:
    fail("FATAL:\n%s" % traceback.format_exc())

log("")
log("failures: %d" % len(failures))
for f in failures:
    log("  %s" % f)

result = "OK" if not failures else "PROBLEMS"
log("BH_BUILD_RESULT: %s" % result)

try:
    target_dir = locals().get("out_dir") or HERE
    os.makedirs(target_dir, exist_ok=True)
    with open(os.path.join(target_dir, "build_report.txt"), "w",
              encoding="utf-8") as fh:
        fh.write("\n".join(report))
except Exception:
    pass

unreal.log_error("BH_BUILD_RESULT: %s (%d failures)" % (result, len(failures)))
