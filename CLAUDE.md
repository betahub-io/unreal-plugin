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
- `UniqueMergedNames` — records + merges twice and asserts the two merged files are distinct, coexist on
  disk, and carry the `Gameplay_<date>_<time>_<guid>` structure. Guards the per-report unique filename
  that background upload relies on (a second report must not clobber a file the first is still uploading);
  a revert to a timestamp-only name fails the structure check.

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

## Testing video capture: editor builds record only the editor window

`UBH_GameRecorder::CaptureBackBuffer` opens with a `#if WITH_EDITOR` block that captures a window
only when its title contains `Unreal Editor`, and returns for anything else. So in an **editor
build**:

- **Play In Editor works** — PIE renders into the `… - Unreal Editor` window.
- **A standalone game launched from the editor records nothing.** Its title is
  `YourProject (64-bit, PCD3D_SM6)`, so every presented frame is discarded one line into the
  capture callback.
- `UnrealEditor.exe <proj> -game` records nothing either, for the same reason.

Packaged builds compile the filter out (`WITH_EDITOR` is 0), which is why customers record fine in
standalone. **Use PIE or a packaged build to exercise the capture path — never `-game`.**

Downstream this looks identical to a broken GPU readback: the encoder starts, logs
`Waiting for the first valid frame...`, and produces no video. Since `aedcb57` a Warning after ~5s
names the rejected window, so check `Saved/Logs/<Project>.log` before suspecting the readback.

A healthy run logs `StartRecording called with FPS: 30` → `video segments directory ready` →
`Preferred FFmpeg options: ...`, then writes `<id>_%06d.mp4` into `Saved/BH_VideoSegments/`.

**Read `Saved/Logs/<Project>.log`, not a `> file` stdout redirect** — the redirect is heavily
buffered and loses everything if the process is force-killed. A second concurrent instance writes
`<Project>_2.log`.

## Releases

`build.bat` on the Windows box packages all engine versions via `RunUAT BuildPlugin`. It is
**untracked on purpose** — it exists only on that machine, not in this repo.

```
build.bat <version>          compile the editor target (fast; needs a BetaHub_X_Y test project)
build.bat package all        normal release zips
build.bat fab all            Fab: strips ThirdParty (ffmpeg), Binaries, Build, Saved
build.bat package 5.7 dirty  package the WORKING TREE for testing (zip suffixed -dirty)
```

Supported UE versions: **5.3, 5.4, 5.5, 5.6, 5.7, 5.8**, listed in the `VERSIONS` variable
near the top of that script.

**`package` and `fab` build from a clean `git archive HEAD`, not from your working tree.** A
published zip therefore always corresponds to a commit, and the script prints the short SHA it
used. If the tree is dirty it warns and lists the files, then builds HEAD anyway — your
uncommitted changes are *not* in that build. Add `dirty` as the third argument to package the
working tree instead; those zips are suffixed `-dirty` so a test build cannot be mistaken for a
release.

Two consequences worth knowing. `.gitattributes` `export-ignore` is honoured, so `tools/` and
`widgets/` contribute no files. And untracked paths cannot enter the build — which matters
because `OUTPUT_DIR` is `dist/` *inside* the plugin folder, and `RunUAT BuildPlugin` copies the
whole plugin folder into its HostProject: packaging a working tree with a populated `dist/`
nests each version's output inside the next. That once produced 135 GB and filled the disk.
Plain `build.bat <version>` still compiles the working tree, which is what you want while
iterating.

## Verification available

Beyond the test suite, generated widgets can be checked against the originals three ways —
semantic `.T3D` diff, measured geometry, and a pixel diff of real Unreal renders. See
`tools/README.md`. Note that a render shows **design-time** state: `NativeConstruct` calls
`SetReportType(Bug)` and collapses several widgets, so the image shows a combination no
player ever sees.

```sh
python3 tools/tests/test_widget_tools.py
```
