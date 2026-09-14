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

This plugin is fully compatible with Unreal Engine versions **5.3**, **5.4**, **5.5**, **5.6**, **5.7**, and **5.8**. If you're looking for Unreal 4 support, there's a [separate repo for that](https://github.com/betahub-io/unreal-4-plugin).
All features have been tested and verified on these versions.

## FFmpeg Installation

The plugin requires FFmpeg for video recording. Place the FFmpeg executable in the following location based on your platform:

- **Windows:** `Plugins/BetaHubBugReporter/ThirdParty/FFmpeg/Windows/ffmpeg.exe`
- **Mac:** `Plugins/BetaHubBugReporter/ThirdParty/FFmpeg/Mac/ffmpeg`
- **Linux:** `Plugins/BetaHubBugReporter/ThirdParty/FFmpeg/Linux/ffmpeg`

For packaged builds, place the executable (renamed to `bh_ffmpeg` or `bh_ffmpeg.exe`) in `Binaries/<Platform>/`.

## Video Encoder

The plugin records by copying each frame off the GPU asynchronously, scaling it down on the GPU, and encoding it with the bundled FFmpeg (CPU). This is the only encoder in a standard build, and it works on every GPU and platform — there is nothing to configure.

Frame capture never blocks the render thread waiting for the GPU, so recording costs a few percent of framerate rather than stalling every captured frame.

**Hardware (GPU) encoding — experimental, off by default.** An optional NVENC/AMF backend exists but is compiled out unless the plugin is built from source with the `BETAHUB_HWENCODE=1` environment variable set *and* Unreal's Experimental *AVCodecs* (plus *NVCodecs*/*AMFCodecs*) plugins enabled in your project. Only such a build shows the *Video Encoder Backend* setting under *Project Settings → Plugins → BetaHub Bug Reporter*, offering:

| Setting | What it does | Requirements |
| --- | --- | --- |
| **FFmpeg** (default) | Encodes on the CPU with the bundled FFmpeg. | None — works on every GPU and platform. |
| **Hardware** | Encodes on the GPU (NVENC/AMF), keeping encoding off the CPU. | Windows, an NVIDIA or AMD GPU. |
| **Auto** | Uses the hardware encoder when it is available, otherwise FFmpeg. | Same as Hardware; silently uses FFmpeg when hardware isn't available. |

*Hardware* and *Auto* always fall back to FFmpeg if the GPU encoder can't start (no supported GPU, or the driver's encode-session limit is already used up by streaming software). The chosen backend is logged at the start of each recording under the `LogBetaHub` category.

The setting is hidden in standard builds because every option there would resolve to FFmpeg anyway.

## Configuration

All you need to do is to go to your *Player Settings* and in the *BetaHub Bug Reported* section under the *Plugins* category, set your Project ID. You can find your Project ID in your project *General Settings* page.

There, you can also configure your shortcut key to open the bug reporter window.

For full docs and guides, please visit the [BetaHub Documentation](https://betahub.io/docs/permalinks/unreal-plugin).

## Submission mode

The *Media Upload Mode* setting under *Project Settings → Plugins → BetaHub Bug Reporter* controls when
the report form confirms a submission and lets the player return to the game:

| Setting | What it does |
| --- | --- |
| **Upload in background** (default) | The form confirms and closes as soon as the draft is created on BetaHub; the screenshot/video upload and the publish then finish in the background. The player is back in the game in about one round-trip instead of waiting out the whole submission, which can take several seconds on projects with heavy server-side processing. |
| **Wait for upload** | The form stays open on *Submitting…* until the media has fully uploaded and the report is published, then shows the result. This was the behavior before 1.7. |

Background mode is safe against a lost connection: if the client's own publish call never lands (the game
is closed or crashes right after submitting), BetaHub auto-publishes the draft shortly afterwards, so a
report is never left stranded. **Updating from 1.6.x switches you to background mode automatically** — set
*Media Upload Mode* to *Wait for upload* if you want the old blocking behavior back.

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

## Getting Help

**Found a problem? Tell us on [Discord](https://discord.gg/g2wpRtG).** That's where our team is, and
it's the fastest way to get an answer.

It helps a lot if you include:

- your Unreal Engine version and platform,
- the plugin version (see `BetaHubBugReporter.uplugin`),
- the `LogBetaHub` lines from your project's `Saved/Logs/<Project>.log` — video recording in
  particular logs a clear reason when it can't record.

### Workaround: roll back to a previous release

If a new release breaks something for you, install the previous one from
[GitHub Releases](https://github.com/betahub-io/unreal-plugin/releases) while we sort it out. Each
release is packaged per engine version, so pick the zip matching your engine (for example
`BetaHubPlugin-<version>-Unreal_5.4.zip`). Replace the contents of
`Plugins/BetaHubBugReporter` with the older build and restart the editor.

Please still let us know on Discord that you had to roll back, and which version works — that tells
us what broke and when.

`master` is the maintained branch: it carries every released fix, and each release is built and
tested against all supported engine versions before it ships.
