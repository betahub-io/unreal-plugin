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

## Filesystem: physical layer for anything ffmpeg touches

All plugin filesystem work that ffmpeg (an external process) also reads or writes must go
through `IPlatformFile::GetPlatformPhysical()`, **not** the wrapped
`FPlatformFileManager::GetPlatformFile()`. The wrapped layer is a virtualisation shim in
cook-in-editor and staged runs: it reports a redirected path while ffmpeg only ever sees real
disk, so the two disagree and files "exist" that ffmpeg cannot open. Use the physical layer
**consistently** across create / write / enumerate / read / delete; the wrapped layer is for
engine content I/O only.

## Testing

This plugin has a headless **Unreal Automation Test** suite. Tests live in
`Source/BetaHubBugReporter/Private/Tests/` and are guarded by `#if WITH_DEV_AUTOMATION_TESTS`, so they
compile into Development *editor* builds and are compiled out of Shipping/packaged builds.

Current tests (`BetaHub.VideoEncoder.*`, in `BH_VideoEncoderTest.cpp`):
- `HappyPath` — drives `BH_VideoEncoder` with a synthetic `FBH_FrameSource` through the real bundled
  ffmpeg, then `MergeSegments`, and asserts a valid mp4 is produced. Exercises the whole segment
  lifecycle end to end **without a GPU/rendering session** — the frames are synthesized, so it does not
  touch the back-buffer capture path.
- `FailFastUnwritableDir` — makes the segments directory unwritable and asserts recording refuses
  cleanly and promptly (a guard against the recording-stop freeze regression).

### Running the tests (headless, Windows)

Build the editor target for a version, then run the automation commandlet:

```
<UE_ROOT>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe <YourProject>.uproject \
  -ExecCmds="Automation RunTests BetaHub.VideoEncoder" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nullrhi -nosound -nopause -stdout -FullStdOutLogOutput
```

Success looks like `Test Completed. Result={Success}` per test, ending in `Automation Test Queue Empty`.
(The internal Windows build box and its exact paths/aliases are recorded in agent memory, not here.)

### Conventions & gotchas when adding tests here (learned the hard way)

- Tests must compile on **all** supported UE versions. Anything under
  `WITH_DEV_AUTOMATION_TESTS` builds in every editor build, so a version-specific API break in a test
  breaks the plugin build for source users. Verify across versions before committing.
- **`EAutomationTestFlags` mask moved in UE 5.4+**: `EAutomationTestFlags::ApplicationContextMask`
  became the standalone `EAutomationTestFlags_ApplicationContextMask`. Spell out the individual context
  enumerators instead (see `BH_AUTOMATION_TEST_FLAGS`) so it compiles on every version with no guard.
- **The automation framework fails any test that emits a `LogError`.** When a path legitimately logs an
  error (e.g. the fail-fast refusal), declare it with `AddExpectedError(...)`. Never lower the product's
  log level just to satisfy a test — fix the test, not the business logic.
- **`windows.h` leaks macros that collide with engine identifiers** — `DeleteFile` → `DeleteFileW`
  (notably 5.3, colliding with `IPlatformFile::DeleteFile`) and `Yield` (breaking
  `FPlatformProcess::Yield`). Prefer `#include "Windows/WindowsHWrapper.h"` over raw `<windows.h>`;
  it runs `PostWindowsApi.h`, which undoes them. Where raw inclusion is unavoidable, `#undef` after.

## Releases

`build.bat` on the Windows box packages all engine versions via `RunUAT BuildPlugin`. It is
**untracked on purpose** — it exists only on that machine, not in this repo.

```
build.bat <version>          compile the editor target (fast; needs a BetaHub_X_Y test project)
build.bat package all        normal release zips
build.bat fab all            Fab: strips ThirdParty (ffmpeg), Binaries, Build, Saved
```

Supported UE versions: **5.3, 5.4, 5.5, 5.6, 5.7, 5.8**, listed in the `VERSIONS` variable
near the top of that script.

## Verification available

Beyond the test suite, generated widgets can be checked against the originals three ways —
semantic `.T3D` diff, measured geometry, and a pixel diff of real Unreal renders. See
`tools/README.md`. Note that a render shows **design-time** state: `NativeConstruct` calls
`SetReportType(Bug)` and collapses several widgets, so the image shows a combination no
player ever sees.

```sh
python3 tools/tests/test_widget_tools.py
```
