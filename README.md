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

## Configuration

All you need to do is to go to your *Player Settings* and in the *BetaHub Bug Reported* section under the *Plugins* category, set your Project ID. You can find your Project ID in your project *General Settings* page.

There, you can also configure your shortcut key to open the bug reporter window.

For full docs and guides, please visit the [BetaHub Documentation](https://www.betahub.io/docs/integration-guides/#unreal-plugin-integration).
