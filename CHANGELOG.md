# Changelog

## 1.4.0 - 2025-12-22

### Fixed

- Editor freeze/deadlock during bug report submission caused by blocking async call on game thread

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