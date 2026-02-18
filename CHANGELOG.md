# Changelog

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