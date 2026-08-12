# BetaHub Unreal Plugin — working notes

The repository root **is** the shipped plugin: `BetaHubBugReporter.uplugin` sits beside
`Source/` and `Content/`. Anything added at the top level is inside the plugin folder.

## UMG widgets are generated, not hand-edited

`Content/BugReportForm.uasset` and `Content/BugReportFormPopup.uasset` are **build output**.
The source of truth is `widgets/*.json`. They are committed only because Unreal cannot load
anything else.

```sh
$EDITOR widgets/BugReportForm.json
python3 tools/validate_widget_json.py widgets/BugReportForm.json   # ~1s, no Unreal
# then regenerate on the build box — see tools/README.md
```

Editing the `.uasset` directly in the Unreal Editor is not wrong, but the JSON must be
re-exported afterwards or the next regeneration silently reverts the change.

Full workflow, tool list and gotchas: **`tools/README.md`**.

### Non-negotiable constraints

- **Generate under UE 5.3 only.** Later engines write a format UE 5.3 cannot load, which
  breaks every 5.3 customer. `tools/ue_generate_widgets.py` refuses other versions. This has
  already happened once — commit `0503b80`, "Recreate widgets for Unreal 5.3".
- **`tools/` and `widgets/` are developer-only and must never ship.** They are excluded by
  `RunUAT BuildPlugin` (which copies only the plugin's standard folders plus
  `Config/FilterPlugin.ini`) and by `.gitattributes` for source archives. **Never add a
  second `*.uplugin` to this repo** — that is what would make the dev module discoverable
  and shippable. Its manifest is deliberately stored as `.uplugin.in`.
- **Renaming a widget can break the plugin at runtime.** `meta=(BindWidget)` binds by name;
  a mismatch is a null at runtime, not a build error. The validator checks this — run it.

### Traps that cost real time

- **Two widget trees exist and both resolve by path.** `Asset.Name_C:WidgetTree` is compiled
  output regenerated on load — writes there are silently discarded and the save still
  reports success. Only `Asset.Name:WidgetTree` is authoritative.
- **The `.T3D` export omits defaults**, so a property left at its class default is simply
  absent from the JSON. Six of thirteen text widgets carry no `Font` at all; their real size
  is UMG's default 24 Bold. Never infer an absent property's value — measure it.
- **Never conclude anything about widget generation from a non-5.3 engine.** UE 5.7 added
  `WidgetVariableNameToGuidMap`, which makes programmatic widget creation trip compiler
  ensures. UE 5.3 has no such map and is unaffected.

## Releases

`build.bat` in this directory packages all engine versions via `RunUAT BuildPlugin`:

```
build.bat package all     # normal release zips
build.bat fab all         # Fab: strips ThirdParty (ffmpeg), Binaries, Build, Saved
```

Versions are listed in the `VERSIONS` variable near the top of that script.

## Verification available

Beyond the test suite, generated widgets can be checked against the originals three ways —
semantic `.T3D` diff, measured geometry, and a pixel diff of real Unreal renders. See
`tools/README.md`. Note that a render shows **design-time** state: `NativeConstruct` calls
`SetReportType(Bug)` and collapses several widgets, so the image shows a combination no
player ever sees.

```sh
python3 tools/tests/test_widget_tools.py
```
