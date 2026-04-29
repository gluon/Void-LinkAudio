# VoidLinkAudio — VCV Rack plugin

VCV Rack 2 plugin bringing Ableton Link Audio to networked modular synthesis.

## Status

**Phase 1 — bootstrap**: skeleton with two passthrough modules (`VoidLinkAudioSend`, `VoidLinkAudioReceive`). Audio routes locally for testing the build, the install, and the rack panel. No network yet.

**Phase 2 — networked**: hook the modules to the shared `core/` (`LinkAudioManager` + ring buffer + Receiver/Publisher façades), so Send actually publishes and Receive actually subscribes over Link Audio. Finalised in 2.0.5: bidirectional audio validated against TouchDesigner, Max/MSP, and Ableton Live; UI cleaned up to 2 jacks per module; SVG labels rendered via path-only (NanoSVG ignores `<text>`).

**Phase 3 — universal binary + automation**: build script that produces a fat arm64+x86_64 `.dylib` ready to ship as a single `.vcvplugin`.

## Critical runtime requirement

VCV Rack's engine has no internal timer: `process()` is driven by the CoreAudio callback of the **Audio module** in your patch. Without an Audio module, the engine falls back to a software timer that runs at ~40 kHz instead of the expected 48 kHz, which causes cyclic buffer underruns when consumers (Ableton Live, etc.) are strict on sample-rate alignment.

**You must have an Audio module in your VCV patch** for VoidLinkAudio Send to publish at the correct rate. The Audio module does not need to be connected to anything — its presence alone wires the engine to CoreAudio and fixes the timing.

## Layout

```
vcv/
├── Makefile                Plugin Makefile, includes $(RACK_DIR)/plugin.mk
├── plugin.json             Manifest read by Rack at load
├── README.md               This file
├── src/
│   ├── plugin.hpp          Forward declarations and Model* externs
│   ├── plugin.cpp          init() — registers the two Models with Rack
│   ├── VoidLinkAudioSend.cpp
│   └── VoidLinkAudioReceive.cpp
└── res/
    ├── VoidLinkAudioSend.source.svg     Editable source (with <text>)
    ├── VoidLinkAudioSend.svg            Generated, paths only (used by Rack)
    ├── VoidLinkAudioReceive.source.svg  Editable source (with <text>)
    └── VoidLinkAudioReceive.svg         Generated, paths only (used by Rack)
```

## Build

The Rack SDK must be available somewhere. We keep both architectures side by side so we can later produce a universal-binary `.vcvplugin`:

```
~/DATA/WORK/_DEV/VCVRack/Rack-SDK-2.6.6-arm64/
~/DATA/WORK/_DEV/VCVRack/Rack-SDK-2.6.6-mac-x64/
```

Build for the current arch (mac arm64 here):

```bash
cd vcv/
RACK_DIR=~/DATA/WORK/_DEV/VCVRack/Rack-SDK-2.6.6-arm64 make
```

Install the signed `.vcvplugin` into Rack's user plugin folder:

```bash
RACK_DIR=~/DATA/WORK/_DEV/VCVRack/Rack-SDK-2.6.6-arm64 make install
```

This drops a `.vcvplugin` into the user plugin folder (typically `~/Library/Application Support/Rack2/plugins-mac-arm64/`).

Launch Rack, search for "Void Link" in the module browser, drop both modules in a rack, **add an Audio module to the patch** (see "Critical runtime requirement" above), and connect them.

## SVG panel workflow

NanoSVG (the SVG renderer used by Rack) ignores `<text>` tags. Any text on the panel must be converted to paths before Rack can display it.

We keep two files per module:

- `<Module>.source.svg` — editable source with regular `<text>` elements. Edit this when you want to change a label.
- `<Module>.svg` — generated, paths-only version. This is the file Rack reads.

To regenerate `.svg` from `.source.svg`:

1. Copy `<Module>.source.svg` to `<Module>.svg`.
2. Open `<Module>.svg` in Inkscape.
3. Edit > Select All in All Layers (Ctrl+Alt+A).
4. Path > Object to Path (Ctrl+Shift+C).
5. File > Save As > **Plain SVG** (overwriting `<Module>.svg`).

Caveat: Inkscape's "Object to Path" sometimes emits `style="fill:#xxxxxx"` for the converted glyphs instead of the `fill="#xxxxxx"` attribute. If you change the colour scheme of the panel, check both forms.

## Universal binary (planned, Phase 3)

Two builds, one with each SDK, then `lipo` the two `.dylib` together. A helper script will be added in `vcv/build.sh` once Phase 2 is stable. For now just stick to whichever arch matches your Rack install.