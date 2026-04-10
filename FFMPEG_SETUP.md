# FFmpeg Setup for Video Recording

The BetaHub Bug Reporter plugin uses FFmpeg to record gameplay video for bug reports. FFmpeg is **not bundled** with the plugin and must be installed separately.

Without FFmpeg, the plugin still works for submitting bug reports with screenshots and logs, but video recording will be disabled.

## Installation

### Windows

1. Download FFmpeg from the [official website](https://ffmpeg.org/download.html) or use a pre-built binary from [gyan.dev](https://www.gyan.dev/ffmpeg/builds/).
2. Place the `ffmpeg.exe` file in one of the following locations:
   - **For editor use:** `<YourProject>/Plugins/BetaHubBugReporter/ThirdParty/FFmpeg/Windows/ffmpeg.exe`
   - **For packaged builds:** `<YourProject>/Binaries/Win64/bh_ffmpeg.exe`

### macOS

1. Install FFmpeg via [Homebrew](https://brew.sh/): `brew install ffmpeg`
2. Alternatively, place the `ffmpeg` binary in one of the following locations:
   - **For editor use:** `<YourProject>/Plugins/BetaHubBugReporter/ThirdParty/FFmpeg/Mac/ffmpeg`
   - **For packaged builds:** `<YourProject>/Binaries/Mac/bh_ffmpeg`

### Linux

1. Install FFmpeg via your package manager (e.g., `sudo apt install ffmpeg`).
2. Alternatively, place the `ffmpeg` binary in one of the following locations:
   - **For editor use:** `<YourProject>/Plugins/BetaHubBugReporter/ThirdParty/FFmpeg/Linux/ffmpeg`
   - **For packaged builds:** `<YourProject>/Binaries/Linux/bh_ffmpeg`

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
