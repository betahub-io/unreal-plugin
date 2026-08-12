# Widget source tooling — developer only

The UMG widgets in `Content/` are generated from JSON in `widgets/`. The JSON is the
source of truth; the `.uasset` files are build output that happens to be committed
because Unreal cannot load anything else.

**None of this ships.** It is developer tooling. See `widgetgen/README.md` for why the
helper module's manifest is deliberately not called `.uplugin`.

## Why

`.uasset` files are binary: unreviewable in pull requests, unmergeable, and silently
version-locked. All three assets are UE 5.3 format; opening and saving one in 5.4+
upgrades it and breaks every 5.3 user, with nothing to catch it. Commit `0503b80`
("Recreate widgets for Unreal 5.3") is what that looks like when it happens.

## Everyday edit

```sh
# 1. edit the source
$EDITOR widgets/BugReportForm.json

# 2. check it locally - about a second, no Unreal needed
python3 tools/validate_widget_json.py widgets/BugReportForm.json

# 3. regenerate on the build box (UE 5.3), then verify nothing unintended moved
#    see "Regenerating" below
```

## The pieces

| Tool | Runs | Purpose |
|---|---|---|
| `validate_widget_json.py` | anywhere | Structure, widget classes, containment, slot types, and `meta=(BindWidget)` names. Run this first — it is instant. |
| `ue_export_t3d.py` | UE editor | Exports a `.uasset` to Unreal's own `.T3D` text format. |
| `t3d_to_json.py` | anywhere | Converts `.T3D` to the JSON source format. |
| `ue_build_widgets.py` | **UE 5.3 only** | One editor run: generate, export T3D, render both sides with geometry. The everyday command. |
| `ue_generate_widgets.py` | **UE 5.3 only** | Generate only. Also importable — `ue_build_widgets` calls its `run()`. |
| `ue_render_widgets.py` | **UE 5.3 only** | Render + geometry only, for assets you did not just generate. |
| `compare_renders.py` | anywhere | Pixel + geometry diff of two renders. Needs Pillow for the pixel half. |
| `verify_widgets.py` | anywhere | **Pre-release check**: asset format version, and drift between `widgets/*.json` and the real `.uasset`. |
| `ue_verify_widgets.py` | **UE 5.3 only** | Engine half of the check: blueprints compile, every required `BindWidget` resolves. Commandlet, no Slate needed. |
| `compare_widget_json.py` | anywhere | Semantic diff of two widget trees. The regeneration gate. |
| `json_to_html.py` | anywhere | Approximate visual sketch for fast iteration. **Not verification** — see below. |
| `ue_dump_umg_schema.py` | UE editor | Regenerates `umg_schema.json`. Only needed when changing engine version. |
| `widgetgen/` | UE 5.3 | Editor-only C++ module reaching what Python cannot. |

## Regenerating

Must run under **UE 5.3**. Generating on a later engine produces assets 5.3 cannot load —
the exact bug this tooling exists to prevent. `ue_generate_widgets.py` enforces this.

One-time setup on the build box:

```sh
./tools/widgetgen/install.sh "/path/to/HostProject"
# then build the host project's editor target
```

Then, per regeneration, write `bh_generate_config.json` next to the script:

```json
{"json": "…/BugReportForm.json",
 "seed": "/BetaHubBugReporter/BugReportForm",
 "target": "/Game/BH_Gen/BugReportForm_Gen",
 "exportDir": "…/gen"}
```

and run the editor commandlet:

```
UnrealEditor-Cmd.exe HostProject.uproject -run=pythonscript \
  -script=".../ue_generate_widgets.py" -EnablePlugins=PythonScriptPlugin \
  -unattended -nosplash -nopause -stdout
```

Then export the result and diff it against the source to confirm only what you intended
changed:

```sh
python3 tools/t3d_to_json.py generated.T3D -o /tmp/check.json
python3 tools/compare_widget_json.py widgets/BugReportForm.json /tmp/check.json
```

## The edit loop

About 15 seconds from a JSON edit to seeing the real thing:

```sh
python3 tools/validate_widget_json.py widgets/BugReportForm.json   # ~1s, catches most mistakes
# then one editor run on the box: generate + export + render both sides
python3 tools/compare_renders.py build/BugReportForm_original build/BugReportForm_Gen
python3 tools/json_to_html.py widgets/BugReportForm.json \
        --geometry build/BugReportForm_Gen.geometry.json -o /tmp/preview.html
```

What the loop does **not** cover, so you know when to open the editor yourself:

- The render is **design-time state**, not what a player sees. `NativeConstruct` calls
  `SetReportType(Bug)`, which collapses several widgets, so the PNG shows a combination
  no user ever gets.
- Default visual state only — no hover, pressed, focused or disabled.
- One draw size. No DPI scaling or viewport-size behaviour.
- Layout and appearance only, never behaviour.

## Seeing the widget

Two levels, and the difference matters:

```sh
# fast sketch - approximate, instant, no Unreal
python3 tools/json_to_html.py widgets/BugReportForm.json -o /tmp/preview.html
open /tmp/preview.html
```

The HTML emulates Slate layout with CSS. It is good for catching gross mistakes — wrong
container, missing widget, absurd padding — and useless for deciding that something is
correct, for two reasons: CSS and Slate genuinely differ (CanvasPanel anchors, Fill/Auto
sizing, text metrics), and it is written from the same reading of the JSON as the
generator, so it can reproduce a misunderstanding instead of exposing it.

Ground truth is `BHWidgetGenLibrary.RenderWidgetToPng`, which renders what Unreal actually
built. Use that to confirm anything that matters, and to diff an original against a
regenerated asset.

Note: headless browsers block `file://`. To screenshot the preview, serve it first
(`python3 -m http.server` in the output directory) — a normal browser opens the file fine.

## Before a release

```sh
python3 tools/verify_widgets.py --emit-config /tmp/bh_verify_config.json
# copy that next to the tools on the build box, run the engine half as a
# commandlet, bring back its .T3D exports, then:
python3 tools/verify_widgets.py --exports <dir-with-the-T3D-exports>
```

Four things get checked, and each has been confirmed to actually fail when it should:

| Check | Catches |
|---|---|
| `FileVersionUE5 == 1009` | an asset saved by a newer engine, which UE 5.3 cannot load |
| JSON vs `.uasset` drift | someone editing one without the other |
| Blueprints compile | a broken asset |
| Required `BindWidget` resolves | a rename that compiles and then nulls at runtime |

## Tests

```sh
python3 tools/tests/test_widget_tools.py
```

## Things worth knowing before you touch this

- **Generation needs a seed asset.** `RootWidget` cannot be written from Python, so the
  generator duplicates an existing asset and rebuilds everything beneath its root. The
  seed's root name and class must match the JSON's.
- **There are two widget trees and both resolve by path.** `Asset.Name_C:WidgetTree` is
  compiled output, regenerated on load — writing there is silently discarded and the save
  still reports success. Only `Asset.Name:WidgetTree` is authoritative.
- **Renaming a widget can break the plugin at runtime**, because `meta=(BindWidget)` binds
  by name and a mismatch is a null rather than a build error. The validator checks this.
- **Nothing here catches a visual regression.** The gate compares everything Unreal's
  exporter emits, but "compiles clean and looks wrong" needs a human with the editor open.
- **`unreal.log` at Display level never reaches commandlet stdout.** Write to a file, or
  use `log_error`.
