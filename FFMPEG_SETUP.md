# FFmpeg Setup for Video Recording

The BetaHub Bug Reporter plugin uses FFmpeg to record gameplay video for bug reports. The plugin is built
for Windows (Win64).

- **GitHub release:** FFmpeg is included at `Plugins/BetaHubBugReporter/ThirdParty/FFmpeg/Windows/ffmpeg.exe`,
  and packaging your game copies it next to the game as `bh_ffmpeg.exe`. Nothing to install.
- **Fab:** the Fab build does not include FFmpeg, so install it yourself as below.

Without FFmpeg, the plugin still works for submitting bug reports with screenshots and logs, but video recording will be disabled.

## Installation

1. Download FFmpeg from the [official website](https://ffmpeg.org/download.html) or use a pre-built binary from [gyan.dev](https://www.gyan.dev/ffmpeg/builds/).
2. Place the `ffmpeg.exe` file in:
   - **For editor use:** `<YourProject>/Plugins/BetaHubBugReporter/ThirdParty/FFmpeg/Windows/ffmpeg.exe`
   - **For packaged builds:** copy it into each packaged build as `<PackagedGame>/<ProjectName>/Binaries/Win64/bh_ffmpeg.exe`, next to the packaged game (the GitHub release does this for you when you package)

## Verifying Installation

When the plugin starts, it searches for FFmpeg in the locations listed above. If FFmpeg is found, video recording will be enabled automatically. If not, you will see a warning in the Output Log:

```
Warning: FFmpeg not found. Video recording is disabled.
```

## Supported Encoders

The plugin automatically detects and uses the best available H.264 encoder in this priority order:

1. `h264_nvenc` (NVIDIA GPU)
2. `h264_amf` (AMD GPU)
3. `h264_videotoolbox` (macOS)
4. `h264_vaapi` (Linux VA-API)
5. `libx264` (CPU fallback)
