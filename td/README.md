# VoidLinkAudio — TouchDesigner

Source code and build instructions for the **VoidLinkAudio** TouchDesigner
Custom CHOPs. If you just want to use precompiled binaries, grab them from
[Releases](../../../releases) instead — this folder is for people who want
to read the code, modify it, and build their own.

## What's in here

Two independent Custom CHOP plugins, one per directory:

```
td/
├── README.md                          This file
└── CHOP/
    ├── LinkAudioSend/                 Publish audio to a Link Audio channel
    │   ├── LinkAudioSend.cpp / .h     CHOP class (audio path, threading)
    │   ├── Parameters.cpp / .h        Parameter declarations + R/W glue
    │   ├── CMakeLists.txt
    │   └── plugin/Release/            Build output (.plugin / .dll)
    └── LinkAudioReceive/               Subscribe to a Link Audio channel
        ├── LinkAudioReceive.cpp / .h
        ├── Parameters.cpp / .h
        ├── CMakeLists.txt
        └── plugin/Release/
```

Both CHOPs link against the shared C++ core in [`../core/`](../core/) and
the bundled [Ableton Link](https://github.com/Ableton/link) submodule
under `../thirdparty/link/`. There is no separate copy of the core or of
Link inside `td/`.

## Operator behaviour

### `LinkAudioSend`

- Inputs: an audio CHOP (1 or 2 channels).
- Publishes a named Link Audio channel on the LAN under a configurable
  peer name.
- Info CHOP exposes `num_peers`, `tempo`, `beat`, `phase`, `transport`,
  plus per-stream counters (`frames_published`, `frames_dropped`).
- Parameters: `Channel Name`, `Peer Name`, `Enabled`, `Tempo` (R/W),
  `Transport` (R/W).

### `LinkAudioReceive`

- Outputs: 1 or 2 channels of received audio.
- Subscribes to `(From Peer, From Channel)`. Auto-resubscribes when a
  matching publisher reappears.
- Info CHOP exposes the same shared timing fields plus
  `frames_received` and `frames_dropped`.
- Parameters: `From Peer Name`, `From Channel Name`, `Enabled`, `Tempo`
  (R/W), `Transport` (R/W).

Tempo and Transport are bidirectional — changing them on a TD CHOP
propagates to every Link peer on the LAN (Live, Max, VCV, etc.), and
vice-versa.

## Required setup before building

The CHOPs need the TouchDesigner SDK headers locally in each CHOP folder.
This is the TD convention — they're not redistributed here, you copy them
from your local TouchDesigner install:

```
CPlusPlus_Common.h
CHOP_CPlusPlusBase.h
```

On macOS:

```
/Applications/TouchDesigner.app/Contents/Resources/Samples/CPlusPlus/
```

On Windows: under your TouchDesigner install folder.

Copy these two headers into **both** `CHOP/LinkAudioSend/` and
`CHOP/LinkAudioReceive/`.

Also make sure the Ableton Link submodule is initialised:

```bash
git submodule update --init --recursive
```

## Build (macOS)

```bash
cd td/CHOP/LinkAudioReceive
cmake -B build -G Xcode
cmake --build build --config Release

cd ../LinkAudioSend
cmake -B build -G Xcode
cmake --build build --config Release
```

Outputs land at `plugin/Release/LinkAudio*.plugin`. They are universal
binaries (arm64 + x86_64) when CMake is invoked on Apple Silicon with the
Xcode generator.

## Build (Windows)

You'll need Visual Studio 2022 with the Desktop C++ workload, CMake 3.22+,
and the Ableton Link submodule.

```bash
cd td/CHOP/LinkAudioReceive
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

cd ..\LinkAudioSend
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Outputs land at `plugin/Release/LinkAudio*.dll`.

## Install

The simplest install is to drop the built `.plugin` (Mac) or `.dll`
(Windows) bundles into your TD project's `Plugins/` folder. TD picks
them up at start.

For a system-wide install:

- **macOS**: `~/Library/Application Support/Derivative/TouchDesigner099/Plugins/`
- **Windows**: `%APPDATA%\Derivative\TouchDesigner\Plugins\`

Restart TouchDesigner. The operators appear as **Link Audio Send** and
**Link Audio Receive** in the CHOP Add Operator dialog.

## Code architecture (short)

Each CHOP is a thin wrapper around the shared core:

- `LinkAudioManager` (in `../core/`) is a process-wide singleton that owns
  the Link session and exposes app-thread getters/setters for tempo and
  transport, plus a real-time-safe `captureAppSessionState()` for use
  inside the audio thread.
- `LinkAudio<Send|Receive>Stream` (also in core) owns a worker thread for
  Link API calls (subscribe/publish/peer setup) and a lock-free SPSC ring
  buffer for the audio path.
- The CHOP forwards TD audio in/out from `getOutputInfo()` and `execute()`
  into the stream's audio callbacks. TD parameters are routed via
  `Parameters.cpp` into stream attributes.

Threading model:

- Worker thread: runs Link callbacks, manages subscriptions, signals the
  audio thread when the topology changes.
- Audio thread: pulls samples from / pushes samples into the ring buffer
  during the TD audio callback. Never blocks. Never allocates.
- Main thread (TD UI): reads the cached state for Info CHOP fields and
  applies parameter changes via the worker thread.

If you read just one file, start with `LinkAudio<Send|Receive>.cpp` —
the rest of the moving parts are abstracted behind the core API.

## License

GPL-2.0-or-later — see the root [`LICENSE`](../LICENSE) file.