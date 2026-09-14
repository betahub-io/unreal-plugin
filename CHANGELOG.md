# Changelog

## 1.7.0 - 2026-09-14

### Changed

- **Bug reports now upload in the background by default.** The form confirms and closes as soon as the draft is created on BetaHub, and the media upload and publish finish detached, so reporters return to the game in about one round-trip instead of waiting out the full submission - which on projects with heavy server-side processing could take ~15 seconds. **Action for existing integrators updating from 1.6.x: this is a behavior change you get automatically.** If you need the old behavior - the form staying open on "Submitting..." until everything has uploaded and published - set **Project Settings -> BetaHub -> Media Upload Mode** to **Wait for upload**. A backend safety-net republishes the draft if the client's publish call is lost, so a report is never left as an unpublished draft.

### Added

- **Media Upload Mode** setting (Project Settings -> BetaHub): **Upload in background** (default) or **Wait for upload** (the pre-1.7 blocking behavior). Mirrors the Unity plugin's upload modes.
- `UBH_BugReport::SubmitReportWithMedia` gained two optional trailing parameters, both back-compatible: an `OnDraftCreated` callback (fired when the draft is created, before upload/publish) and a `TempFilesToCleanup` list the submit chain deletes when the submission finishes.

### Fixed

- Auto-captured screenshots and recorded video clips now use unique per-report filenames. With background upload a second report can be submitted while the first is still uploading; the old fixed screenshot name and second-resolution video name could let the second report overwrite a file the first was still sending.
- Cleanup of a report's temporary media (screenshot and recorded clip) now runs in the submit chain rather than in the form, so it happens reliably even though the form closes before the background upload finishes.

## 1.6.1 - 2026-08-17

### Fixed

- Submitting a report without video (a screenshot-only or logs-only bug report, or a suggestion) left gameplay recording stopped and never resumed it. The next report then captured the *previous* report's frozen screenshot and its leftover video segments, while the report the player was actually filing ended up with stale or missing media. Recording now resumes after every submission - success or failure - so each report captures its own moment.

## 1.6.0 - 2026-08-13

### Changed

- Video recording no longer stalls the render thread. Frames were previously read back from the GPU synchronously, which forced a pipeline flush on every captured frame. Capture now uses an asynchronous readback ring and scales the frame down on the GPU before reading it back. On our test hardware the render-thread stall went from ~6.5 ms per capture to ~0.01 ms, and the cost of recording to game framerate dropped from ~18% to ~6%.
- Recorded video is smoother. The old synchronous readback was silently throttling capture to roughly half the configured frame rate, so a recording set to 30 FPS was really being written at about 13. It now reaches the configured rate.

### Fixed

- The game could hang permanently when recording stopped. If FFmpeg exited early - a missing or unwritable output directory, a crash, an antivirus block - the plugin kept a copy of FFmpeg's own input pipe handle open, so the pipe never registered as broken. Writes to it blocked forever and took the game thread down with them when it waited for the encoder to finish. Reachable from ordinary actions including resizing the viewport.
- Video recording failed silently in packaged and cook-in-editor builds where the engine redirects file paths. The plugin and FFmpeg had different views of where the video segments were: the plugin reported files as present that FFmpeg could not open, so the merge step produced no video and no clear error. All segment file operations now go through the physical filesystem, which FFmpeg also sees.
- Recording now refuses to start, with a clear log message, when the video segments directory cannot be written to, instead of failing later during the merge.
- Video merge failures caused by unchecked file writes and relative paths.

### Added

- Optional **Debug Prefill Form** setting (Project Settings -> BetaHub -> Debug). When enabled, the feedback form opens with sample text already filled in, so a submission can be tested without typing anything. The sample text is prefixed `[BetaHub debug prefill]` so test reports are recognisable, and it only replaces fields you have not typed in yourself. Has no effect in Shipping builds - the code is compiled out - but a report submitted with it enabled is still a real report.

## 1.5.5 - 2026-05-26

### Fixed

- Submit button stayed clickable while a submission was in progress, so clicking the "Submitting..." button again sent duplicate reports. The button is now disabled and guarded against re-entry until the submission completes.
- Submit button state is now reset whenever the form is shown, so a form widget instance that is reused (rather than recreated) no longer stays stuck on "Submitting...".

## 1.5.4 - 2026-04-01

### Fixed

- Packaged build initialization blocked when viewport does not use a separate render target (GetRenderTargetTexture returning null)
- Plugin content assets (widget blueprints) not cooked in UE 5.4+ due to EngineVersion mismatch in .uplugin

## 1.5.3 - 2026-02-18

### Fixed

- PlayerController null error when opening bug report widget, caused by event chain not firing when local players already exist at startup
- Old input component not cleaned up when PlayerController changes (e.g. during level transitions)

## 1.5.2 - 2026-02-16

### Added

- CPU profiling instrumentation via `stat BetaHub` console command, allowing developers to measure the plugin's recording cost across game thread, render thread, and background threads

## 1.5.1 - 2026-02-16

### Fixed

- Video encoder crash on PIE (Play In Editor) exit caused by use-after-free when GC destroys the frame buffer
- Re-entrancy issue where FlushRenderingCommands() could pump queued tasks that restart recording during StopRecording()

## 1.5.0 - 2026-02-10

### Changed

- **Breaking:** Project Token (ProjectToken) is now required for all submissions. Anonymous authentication fallback has been removed. If the token is not configured, submissions will be blocked with a user-facing error popup.

## 1.4.0 - 2025-12-22

### Added

- Suggestions/feature request submission support with optional screenshot attachment
- Report type selector (Bug Report / Suggestion) in the feedback form

### Fixed

- Editor freeze/deadlock during bug report submission caused by blocking async call on game thread
- Cursor state not properly restored when popup is displayed after form submission

## 1.2.0 - 2025-08-01

### Added

- Release management support with new project token configuration
- Enhanced HTTP error handling for improved reliability during bug report submission

### Fixed

- Screenshots not uploading properly during bug report submission
- Memory problems causing application crashes and instability
- Deprecated warnings and compilation issues for better engine compatibility

### Changed

- Improved performance with asynchronous ReadPixels execution
- Enhanced pointer handling using modern Unreal Engine TObjectPtr types
- Updated documentation with Unreal Engine compatibility information

## 1.1.0 - 2024-12-04

### Added

- Added the ability to set maximum recording width and height; recordings are scaled down if the viewport exceeds these dimensions.
- Video segments now use random names to avoid file conflicts when multiple game instances are running.
- Added blueprint callable functions to fetch release information from the project releases endpoint:
  - `FetchAllReleases`: Fetches all releases.
  - `FetchLatestRelease`: Fetches the latest release.
  - `FetchReleaseById`: Fetches release information by release ID.

### Fixed

- Fixed occasional freezes on Windows when the viewport is destroyed, caused by the write pipe freezing the game.
- Fixed the video recorder incorrectly capturing non-main windows when multiple windows are open.
- Fixed occasional crashes when capturing screenshots.

## 1.0.4

### Fixes

- Fix getting proper player reference when in multiplayer session
- Fix freeze when closing game
- Fix invalid handle exception when closing pipes
- Add stopping service when GameInstanceSubsystem is deinitialized
- Change format for MaxRecordingDuration to integer

## 1.0.3

### Fixes

- Compilation errors on shipping builds
- Crashes on shipping builds

## 1.0.2

### Fixes

- Random crash on viewport change, potential crash even if the viewport size is not changed.