# Changelog

## UNRELEASED

### Added

- Added the ability to set maximum recording width and height; recordings are scaled down if the viewport exceeds these dimensions.
- Video segments now use random names to avoid file conflicts when multiple game instances are running.

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