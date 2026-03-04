# RecordReplayLibrary

A C++17 recording/replay library for timestamped channel data (video, audio, telemetry), with examples and an ImGui/SDL2 viewer.

## Features

- Timestamped message recording by channel
- Optional chunking/compression and file indexing
- Session replay via seek/read APIs
- Single-file recording mode (`.rec`) for easy copy/move
- Example apps:
  - `record_ffmpeg` (record FFmpeg video/audio test streams)
  - `record_multi_pattern` (record 4 video patterns in parallel)
  - `record_net_streams` (record multiple UDP/TCP payload streams + PEAK CAN stub)
  - `replay_ffplay` (pipe recorded video to FFplay)
  - `viewer` (inspect channels/messages/timeline)

## Build (Windows, Debug)

```powershell
cd c:\develop\RecordReplayLibrary\build
cmake ..
cmake --build . --config Debug
```

Binaries are generated in:

- `build\bin\Debug\`

## Quick Start

### 1) Record a test session

```powershell
& "C:\develop\RecordReplayLibrary\build\bin\Debug\record_ffmpeg.exe" "C:\develop\RecordReplayLibrary\build\recordings"
```

With current example config, output is a single file:

- `build\recordings\ffmpeg_testsrc.rec`

### 2) Replay with FFplay

```powershell
& "C:\develop\RecordReplayLibrary\build\bin\Debug\replay_ffplay.exe" "C:\develop\RecordReplayLibrary\build\recordings\ffmpeg_testsrc.rec"
```

### 3) Open in Viewer

```powershell
& "C:\develop\RecordReplayLibrary\build\bin\Debug\viewer.exe"
```

Open a `.rec` file directly from the UI.

### 4) Record UDP/TCP streams (and optional CAN stub)

```powershell
& "C:\develop\RecordReplayLibrary\build\bin\Debug\record_net_streams.exe" "C:\develop\RecordReplayLibrary\build\recordings" --session net_capture --udp 5000 --tcp 127.0.0.1:9000 --can-peak PCAN_USBBUS1 --duration 10
```

Notes:

- `record_net_streams` records payload bytes into separate channels per source.
- `--udp` and `--tcp` are repeatable for multiple streams.
- `--can-peak` is currently a stub hook (for future PEAK integration).

## Single-file vs Directory Sessions

`SessionConfig` supports two output modes:

- `SingleFile = true`
  - Writes directly to `OutputDir/SessionName.rec`
  - No session subdirectory
  - No `session.manifest`
  - Segment rotation is disabled
- `SingleFile = false` (default)
  - Writes a session directory with segment files
  - Writes `session.manifest`

## Data/Annotation Ordering Notes

- Frame timestamps should reflect capture time (set when a frame is read/created).
- `Annotate()` now flushes buffered channel accumulators first, so annotation record placement in file order matches prior writes.
- `SessionEnd` is a structural close record written by the writer during `Close()`, after index emission.

## Project Layout

- `include/recplay/` public headers
- `src/` library implementation
- `tests/` unit tests
- `examples/` recording/replay demos
- `apps/viewer/` SDL2 + ImGui inspection app
