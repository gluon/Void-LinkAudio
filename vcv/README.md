# VoidLinkAudio — VCV Rack

Source code and build instructions for the **VoidLinkAudio** VCV Rack 2
plugin. If you just want to use precompiled binaries, grab them from
[Releases](../../releases) instead — this folder is for people who want
to read the code, modify it, and build their own.

## What's in here

```
vcv/
├── README.md                         This file
├── Makefile                          Plugin Makefile (includes $(RACK_DIR)/plugin.mk)
├── plugin.json                       Manifest read by Rack at load
├── src/
│   ├── plugin.hpp                    Forward declarations + Model* externs
│   ├── plugin.cpp                    init() — registers both Models with Rack
│   ├── VoidLinkAudioSend.cpp         Send module (audio path, threading, panel)
│   └── VoidLinkAudioReceive.cpp      Receive module
└── res/
    ├── VoidLinkAudioSend.svg         Panel graphics (used by Rack)
    └── VoidLinkAudioReceive.svg
```

The plugin links against the shared C++ core in [`../core/`](../core/)
and the bundled [Ableton Link](https://github.com/Ableton/link) submodule
under `../thirdparty/link/`. There is no separate copy inside `vcv/`.

## Modules at a glance

Two modules under the **Structure Void** browser category in Rack:

- **Void Link Audio Send** — publish stereo audio onto the network as a
  named Link Audio channel.
- **Void Link Audio Receive** — subscribe to a Link Audio channel from
  any peer (Live 12.4+, Max, TouchDesigner, etc.) and output it as audio.

Each panel has:

- A **`TEMPO`** knob (20–999 BPM) — turn it to change the session tempo;
  it also follows external changes from any other peer (Live, Max, etc.).
- A **`STATE`** switch — toggles the shared Link transport on/off.
- Three CV outputs:
  - **`TEMPO`** — bpm / 100, in volts (e.g. 1.20 V at 120 BPM).
  - **`PHASE`** — 0–10 V ramp over the quantum.
  - **`STATE`** — 0 V stopped, 10 V playing (a simple gate).
- Two audio jacks (stereo inputs on Send, stereo outputs on Receive).
- A LED + peer count display (green when active, red on drops).

The channel name, peer name, and (for Receive) source peer filter are
editable via right-click menu.

Tempo and Transport are bidirectional — the knob/switch values sync with
every other Link peer on the LAN.

## Critical runtime requirement

VCV Rack's engine has **no internal clock**: `process()` is driven by the
audio callback of the **Audio module** in your patch. Without an Audio
module, the engine falls back to a software timer that runs at ~40 kHz
instead of the expected sample rate, which causes cyclic buffer underruns
when consumers (like Ableton Live) are strict on sample-rate alignment.

**You must have an Audio module in your VCV patch** for VoidLinkAudio Send
to publish at the correct rate. The Audio module does not need to be
connected to anything — its presence alone wires the engine to the audio
backend and fixes the timing.

## Required setup before building

You need:

- The [VCV Rack 2 SDK](https://vcvrack.com/manual/Building#Plugins) for
  your target architecture
- `make`, a C++17 compiler (clang on Mac, MinGW gcc on Windows, gcc on Linux)
- The Ableton Link submodule initialised:

  ```bash
  git submodule update --init --recursive
  ```

The Rack SDK is downloaded separately from
<https://vcvrack.com/downloads> — pick the SDK that matches your platform
(`Rack-SDK-<version>-mac-arm64`, `Rack-SDK-<version>-mac-x64`,
`Rack-SDK-<version>-win-x64`, or `Rack-SDK-<version>-lin-x64`).

## Build

```bash
cd vcv
RACK_DIR=/path/to/Rack-SDK make
```

This produces `plugin.dylib` (Mac) / `plugin.so` (Linux) / `plugin.dll`
(Windows) and packages it as `dist/VoidLinkAudio-<version>-<arch>.vcvplugin`.

For dual-arch macOS (arm64 + x86_64), build twice with each SDK and ship
two `.vcvplugin` files. The cross-compile is driven by the SDK choice;
both files install side-by-side in Rack's user plugins folder.

## Install

```bash
cd vcv
RACK_DIR=/path/to/Rack-SDK make install
```

This drops the `.vcvplugin` into Rack's user plugins folder:

- **macOS arm64** — `~/Library/Application Support/Rack2/plugins-mac-arm64/`
- **macOS x86_64** — `~/Library/Application Support/Rack2/plugins-mac-x64/`
- **Windows** — `%LOCALAPPDATA%\Rack2\plugins-win-x64\`
- **Linux** — `~/.Rack2/plugins-lin-x64/`

Restart Rack. Search for "Void Link" in the module browser, drop both
modules in a rack, **add an Audio module to the patch** (see "Critical
runtime requirement" above), and connect them.

## SVG panel workflow

VCV uses NanoSVG which **does not render `<text>` elements**. Static
labels on the panel ("VOID LINK", "TEMPO", "STATE", etc.) are drawn at
runtime in the C++ widget via NanoVG, on top of a minimal background SVG.

If you want to redesign the panel:

- Edit `res/VoidLinkAudio<Send|Receive>.svg` for the static graphics
  (background, accent border, separator lines, logo paths).
- Edit `SendPanelLabels` / `ReceivePanelLabels` widgets in
  `src/VoidLinkAudio<Send|Receive>.cpp` for text labels (font size,
  position, color).

The font used for labels is the bundled `ShareTechMono-Regular.ttf` from
the Rack SDK.

## Code architecture (short)

Each module is a thin wrapper around the shared core:

- `LinkAudioManager` (in `../core/`) is a process-wide singleton that owns
  the Link session and exposes app-thread getters/setters for tempo and
  transport, plus a real-time-safe `captureAppSessionState()` for use
  inside `process()`.
- `LinkAudio<Send|Receive>Stream` (also in core) owns a worker thread for
  Link API calls and a lock-free SPSC ring buffer for the audio path.
- The VCV module's `process()` captures session state once, mirrors it to
  the knob/switch params (anti-feedback via cached `mLastKnobTempo`), and
  drives the three timing CV outputs. Audio inputs/outputs are routed
  through the ring buffer.

Threading model:

- Worker thread: runs Link callbacks, manages subscriptions.
- Audio thread (`process()`): pulls/pushes samples from/to the ring
  buffer, computes timing CV outputs from the captured session state.
  Never blocks. Never allocates.
- UI thread (Rack): displays the cached peer count and LED state.

If you read just one file, start with `VoidLinkAudioSend.cpp` — the rest
of the moving parts are abstracted behind the core API.

## License

GPL-2.0-or-later — see the root [`LICENSE`](../LICENSE) file.