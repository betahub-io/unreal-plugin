# Changelog

## UNRELEASED

### Added
- Can now set maximum recording width and height, will is scaled down if the viewport is larger.

### Fixes

- Fix ocasional freezes on Windows when viewport is being destroed, as write pipe could freeze the game.
- Fix video recorder recording non-main windows when multiple windows are present.
- Fix occasional crash when capturing a screenshot.

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