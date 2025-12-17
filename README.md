# BetaHub Unreal Plugin

## How it works

The plugin will start a *Background Service* that will record gameplay session as a video, including all the Unreal Engine logs. Then, it listens for the shortcut key (F12 by default) to open the bug reporter window. The user can then type in the bug description and submit it to the BetaHub platform.

## Installation

### From GitHub

1. Navigate to your Unreal project directory.
2. Open the `Plugins` directory, or create it if it doesn't exist.
3. Unpack the contents of the GitHub repository into the `Plugins/BetaHubBugReporter` directory.
4. Restart Unreal Editor.
5. Confirm any prompts or warnings that may appear.

## Compatibility

This plugin is fully compatible with Unreal Engine versions **5.3**, **5.4**, **5.5**, and **5.6**. If you're looking for Unreal 4 support, there's a [separate repo for that](https://github.com/betahub-io/unreal-4-plugin).
All features have been tested and verified on these versions.

## Configuration

All you need to do is to go to your *Player Settings* and in the *BetaHub Bug Reported* section under the *Plugins* category, set your Project ID. You can find your Project ID in your project *General Settings* page.

There, you can also configure your shortcut key to open the bug reporter window.

For full docs and guides, please visit the [BetaHub Documentation](https://www.betahub.io/docs/integration-guides/#unreal-plugin-integration).

## Development Branch

This repository has an actively maintained `dev` branch which is often more up-to-date than `master`. If you encounter any issues on `master`, we recommend checking whether the problem has already been addressed in the `dev` branch:

👉 https://github.com/betahub-io/unreal-plugin/tree/dev

The `dev` branch is regularly merged into `master` after we ensure that it does not introduce regressions for users who simply clone the repository and use `master` as-is.
