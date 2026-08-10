# BetaHub Unreal Plugin — agent notes

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

- Tests must compile on **all** supported UE versions (5.3–5.7). Anything under
  `WITH_DEV_AUTOMATION_TESTS` builds in every editor build, so a version-specific API break in a test
  breaks the plugin build for source users. Verify across versions before committing.
- **`EAutomationTestFlags` mask moved in UE 5.4+**: `EAutomationTestFlags::ApplicationContextMask`
  became the standalone `EAutomationTestFlags_ApplicationContextMask`. Spell out the individual context
  enumerators instead (see `BH_AUTOMATION_TEST_FLAGS`) so it compiles on 5.3–5.7 with no version guard.
- **The automation framework fails any test that emits a `LogError`.** When a path legitimately logs an
  error (e.g. the fail-fast refusal), declare it with `AddExpectedError(...)`. Never lower the product's
  log level just to satisfy a test — fix the test, not the business logic.
- **`windows.h` leaks `DeleteFile` → `DeleteFileW`** on some versions (notably 5.3), colliding with
  `IPlatformFile::DeleteFile`. `#undef DeleteFile` after the includes.

## Build & platforms

`build.bat` (Windows) drives builds. Usage: `build.bat <command> <version|all>`.
- `build.bat <version>` — compile the editor target for that UE version (fast; use for test/compile checks).
- `build.bat package <version|all>` — package via `RunUAT BuildPlugin`.
- `build.bat fab <version|all>` — Fab-marketplace package (strips ffmpeg/Binaries/ThirdParty).

Supported UE versions: **5.3, 5.4, 5.5, 5.6, 5.7**. All plugin filesystem work that ffmpeg (an external
process) also reads/writes must go through `IPlatformFile::GetPlatformPhysical()`, not the wrapped
`FPlatformFileManager::GetPlatformFile()`, so the plugin and ffmpeg share one real-disk view.
