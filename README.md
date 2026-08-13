# BetaHub Unreal Plugin

## How it works

The plugin will start a *Background Service* that will record gameplay session as a video, including all the Unreal Engine logs. Then, it listens for the shortcut key (F12 by default) to open the bug reporter window. The user can then type in the bug description and submit it to the BetaHub platform.

## Installation

### From Fab

The plugin is available on the [Fab marketplace](https://www.fab.com/pl/listings/9c76232c-34f6-433c-ad7e-2c4005837e66). Install it directly from Fab into your Unreal Engine project.

### From GitHub

1. Navigate to your Unreal project directory.
2. Open the `Plugins` directory, or create it if it doesn't exist.
3. Unpack the contents of the GitHub repository into the `Plugins/BetaHubBugReporter` directory.
4. Restart Unreal Editor.
5. Confirm any prompts or warnings that may appear.

## Compatibility

This plugin is fully compatible with Unreal Engine versions **5.3**, **5.4**, **5.5**, **5.6**, and **5.7**. If you're looking for Unreal 4 support, there's a [separate repo for that](https://github.com/betahub-io/unreal-4-plugin).
All features have been tested and verified on these versions.

## FFmpeg Installation

The plugin requires FFmpeg for video recording. Place the FFmpeg executable in the following location based on your platform:

- **Windows:** `Plugins/BetaHubBugReporter/ThirdParty/FFmpeg/Windows/ffmpeg.exe`
- **Mac:** `Plugins/BetaHubBugReporter/ThirdParty/FFmpeg/Mac/ffmpeg`
- **Linux:** `Plugins/BetaHubBugReporter/ThirdParty/FFmpeg/Linux/ffmpeg`

For packaged builds, place the executable (renamed to `bh_ffmpeg` or `bh_ffmpeg.exe`) in `Binaries/<Platform>/`.

## Video Encoder

The plugin can encode the gameplay video with either the bundled FFmpeg (CPU) or a GPU hardware encoder. Choose it under *Project Settings → Plugins → BetaHub Bug Reporter → **Video Encoder Backend***:

| Setting | What it does | Requirements |
| --- | --- | --- |
| **FFmpeg** (default) | Encodes on the CPU with the bundled FFmpeg. | None — works on every GPU and platform. |
| **Hardware** | Encodes on the GPU (NVENC/AMF), keeping encoding off the CPU. | Windows, an NVIDIA or AMD GPU, and a plugin build with hardware-encode support (see below). |
| **Auto** | Uses the hardware encoder when it is available, otherwise FFmpeg. | Same as Hardware; silently uses FFmpeg when hardware isn't available. |

**Automatic fallback:** *Hardware* and *Auto* always fall back to FFmpeg if the GPU encoder can't start (no supported GPU, driver session limit reached, or the plugin was built without hardware support). The chosen backend is logged at the start of each recording under the `LogBetaHub` category.

**Enabling hardware encode (experimental):** hardware encoding is built on Unreal's Experimental *AVCodecs* plugins and is compiled in only when the plugin is built from source with the `BETAHUB_HWENCODE=1` environment variable set, with the *AVCodecs* (and *NVCodecs*/*AMFCodecs*) plugins enabled in your project. Without that, the *Hardware*/*Auto* settings fall back to FFmpeg.

## Configuration

All you need to do is to go to your *Player Settings* and in the *BetaHub Bug Reported* section under the *Plugins* category, set your Project ID. You can find your Project ID in your project *General Settings* page.

There, you can also configure your shortcut key to open the bug reporter window.

For full docs and guides, please visit the [BetaHub Documentation](https://betahub.io/docs/permalinks/unreal-plugin).

## Custom Fields (C++)

You can attach your own game-specific data (player level, current scene, build number, etc.) to a bug report. Custom fields are **C++ only** — they are passed per submission to `SubmitReportWithMedia` and are not exposed in the bundled report widget or via Blueprint.

```cpp
#include "BH_BugReport.h"

UBH_PluginSettings* Settings = GetMutableDefault<UBH_PluginSettings>();
UBH_BugReport* BugReport = NewObject<UBH_BugReport>();

TMap<FString, FBH_CustomFieldValue> CustomFields;
CustomFields.Add("player_level", FBH_CustomFieldValue::FromString(FString::FromInt(PlayerLevel)));
CustomFields.Add("tags", FBH_CustomFieldValue::FromArray({ "Bug", "UI" })); // multi-select

TArray<FBH_MediaFile> Videos, Screenshots, Logs;

BugReport->SubmitReportWithMedia(
    Settings, nullptr,
    TEXT("Description"), TEXT("Steps to reproduce"),
    Videos, Screenshots, Logs,
    []() {}, [](const FString& Error) {},
    TEXT(""), TEXT(""), CustomFields);
```

Use `FromString()` for text/single-select/boolean fields and `FromArray()` for multi-select fields. Field keys are snake_case identifiers.

**Project setup:** plain text fields auto-create on first submission, so no setup is needed for them. **Single-select, multi-select, and boolean fields must be pre-created** in your BetaHub project settings — and for select fields, every submitted value must match a predefined option, otherwise the whole report is rejected. See the [custom fields documentation](https://betahub.io/docs/integrations/game-engines/#custom-fields) for details.

## Development Branch

This repository has an actively maintained `dev` branch which is often more up-to-date than `master`. If you encounter any issues on `master`, we recommend checking whether the problem has already been addressed in the `dev` branch:

👉 https://github.com/betahub-io/unreal-plugin/tree/dev

The `dev` branch is regularly merged into `master` after we ensure that it does not introduce regressions for users who simply clone the repository and use `master` as-is.
