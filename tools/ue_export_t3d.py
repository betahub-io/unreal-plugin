"""Export WidgetBlueprints to .T3D text using Unreal's own exporter.

Runs inside the editor. Read-only: it loads assets and writes text files,
never saves or modifies a .uasset.

    UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript \
        -script="tools/ue_export_t3d.py" -EnablePlugins=PythonScriptPlugin \
        -unattended -nosplash -nopause -stdout

Configuration is read from bh_export_config.json sitting next to this script:

    {"assets": ["/BetaHubBugReporter/BugReportForm"], "outDir": "C:/..."}

Both keys are optional. A config file is used rather than environment
variables because WSL does not forward its environment to Windows
processes unless WSLENV is set up, so env vars silently do nothing here.

Note: results are reported through a written file and log_error, because
unreal.log at Display verbosity does not reach commandlet stdout.
"""

import os
import traceback

import unreal

DEFAULT_ASSETS = [
    "/BetaHubBugReporter/BugReportForm",
    "/BetaHubBugReporter/BugReportFormPopup",
]

DEFAULT_OUT_DIR = r"C:/Users/upsoft/bh_widget_dump/export"

_config = {}
_config_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "bh_export_config.json")
if os.path.exists(_config_path):
    import json
    with open(_config_path, "r", encoding="utf-8") as _fh:
        _config = json.load(_fh)

assets = [a for a in _config.get("assets") or [] if a.strip()] or DEFAULT_ASSETS
out_dir = _config.get("outDir") or DEFAULT_OUT_DIR
report_path = os.path.join(out_dir, "export_report.txt")

lines = []


def log(msg):
    lines.append(str(msg))


ok = True
try:
    os.makedirs(out_dir, exist_ok=True)
    tools = unreal.AssetToolsHelpers.get_asset_tools()

    for path in assets:
        try:
            if not unreal.EditorAssetLibrary.does_asset_exist(path):
                raise RuntimeError("asset does not exist")
            # export_assets takes PATH STRINGS, not loaded objects.
            tools.export_assets([path], out_dir)
            log("exported %s" % path)
        except Exception:
            ok = False
            log("FAILED %s:\n%s" % (path, traceback.format_exc()))

    produced = []
    for base, _dirs, files in os.walk(out_dir):
        for f in files:
            if f.lower().endswith(".t3d"):
                full = os.path.join(base, f)
                produced.append((full, os.path.getsize(full)))
    log("")
    for full, size in produced:
        log("%s (%d bytes)" % (full, size))
    if not produced:
        ok = False
        log("no .T3D files produced")

except Exception:
    ok = False
    log("TOP-LEVEL FAILURE:\n%s" % traceback.format_exc())

log("")
log("BH_EXPORT_RESULT: %s" % ("OK" if ok else "FAILED"))

with open(report_path, "w", encoding="utf-8") as fh:
    fh.write("\n".join(lines))

# log_error is the only Python verbosity that reaches commandlet stdout.
unreal.log_error("BH_EXPORT_RESULT: %s" % ("OK" if ok else "FAILED"))
