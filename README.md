# VoidLinkAudio

> Multi-host integration of **Ableton Link Audio** — stream audio over a local
> network, sample-accurate, beat-synced, between any combination of TouchDesigner,
> Max/MSP, VCV Rack, openFrameworks, and Ableton Live.

> **Status: early research release.** Built on top of Ableton's open-source
> [Link](https://github.com/Ableton/link) library (MIT). APIs may evolve.
> macOS-first; Windows builds coming.

---

## What is Link Audio

[Ableton Link](https://www.ableton.com/en/link/) has been around since 2016 — a
tempo / beat / phase synchronization layer over LAN. Hundreds of apps support
it. **Link Audio**, introduced in Ableton Live 12.4 (public release **May 5,
2026**), extends Link to also stream **audio** between peers.

In addition to the shared timeline:

- Each peer can publish one or more named **channels** (audio streams).
- Each peer can subscribe to channels published by other peers.
- Audio is exchanged on the local network with sample-accurate timing.

**VoidLinkAudio** brings this functionality into creative tools that don't have
it natively yet — and lets them all interoperate with each other and with Live.

---

## What's in this repo

| Path           | Host                | Status                                |
|----------------|---------------------|---------------------------------------|
| `td/`          | TouchDesigner CHOPs | Working on macOS                      |
| `max/`         | Max / MSP externals | Working on macOS, package included    |
| `vcv/`         | VCV Rack module     | Work in progress                      |
| `core/`        | Shared C++          | LinkAudioManager + AudioRingBuffer    |
| `thirdparty/`  | Submodule           | Ableton/link (MIT)                    |

**openFrameworks** support lives in its own repo, following the `ofxAddon`
convention:
**[gluon/ofxAbletonLinkAudio](https://github.com/gluon/ofxAbletonLinkAudio)**

**VST / AU / CLAP** plugins are distributed separately at
**[structure-void.com](https://structure-void.com)**.

---

## Quick start

### Use precompiled binaries

Pre-built releases (signed/notarised on macOS, signed on Windows) will be
available in the [Releases](../../releases) tab. Drop them into your host's
plugin folder and you're done.

### Build from source

You'll need:

- **macOS 11+** (universal arm64+x86_64) or **Windows 10 x64**
- **CMake 3.22+**
- A **C++17** compiler (Apple Clang or MSVC 2022)
- Host SDKs (see per-host sections below)

Clone with submodules:

```bash
git clone --recursive https://github.com/gluon/Void-LinkAudio.git
cd Void-LinkAudio
```

If you cloned without `--recursive`:

```bash
git submodule update --init --recursive
```

---

### TouchDesigner

Two custom CHOPs:

- **`LinkAudioInCHOP`** — subscribe to a remote channel; output L/R audio
- **`LinkAudioOutCHOP`** — publish a TD audio signal as a Link Audio channel

```bash
cd td/CHOP/LinkAudioInCHOP
cmake -B build -G Xcode
cmake --build build --config Release

cd ../LinkAudioOutCHOP
cmake -B build -G Xcode
cmake --build build --config Release
```

Outputs land under each CHOP's `plugin/Release/` folder. Drop the
`.plugin` bundles into your TD project's `Plugins/` folder, or symlink to:

```
~/Library/Application Support/Derivative/TouchDesigner099/Plugins/
```

### Max / MSP

Two MSP externals plus a full Max package:

- **`void.linkaudio.in~`** — receive audio from a Link Audio channel
- **`void.linkaudio.out~`** — publish 1 or 2 channels of audio

You'll need the [Max SDK](https://github.com/Cycling74/max-sdk) (8.2.0+).

```bash
export MAX_SDK_PATH=/path/to/your/max-sdk
cd max
cmake -B build -G Xcode
cmake --build build --config Release
```

The package (with `maxref`, help patchers, overview patcher) lives at
`max/package/`. Symlink it into your Max packages folder:

```bash
ln -s "$(pwd)/package" "$HOME/Documents/Max 9/Packages/VoidLinkAudio"
```

then drop the freshly-built `.mxo` files from `max/externals/` into the
package's `externals/` folder, and restart Max.

### VCV Rack

*Work in progress — instructions will follow once the module is stabilised.*

### openFrameworks

See the dedicated repo:
**[gluon/ofxAbletonLinkAudio](https://github.com/gluon/ofxAbletonLinkAudio)**

It contains the addon source plus example projects (sender, receiver, ping-pong).

---

## Windows builds

Windows builds are coming. The CMake setup includes the right Win32 link flags
(`ws2_32`, `iphlpapi`, `winmm`) and platform defines, and the externals
cross-compile cleanly from an ARM Mac via Parallels + Visual Studio 2022.
Runtime testing on real x64 Windows is in progress.

---

## License

This repository is licensed under the **MIT License** — see [LICENSE](LICENSE).

It depends on (as a git submodule) the
[Ableton/link](https://github.com/Ableton/link) library, which is also MIT-licensed.

The TouchDesigner SDK headers vendored under `td/` are property of
**Derivative Inc.** and used under their Shared Use License.

The Max SDK is property of **Cycling '74** — only headers are required, and
they are not redistributed in this repo (you supply your own via the
`MAX_SDK_PATH` variable).

---

## Acknowledgements

See [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md).

---

## Author

**Julien Bayle** — *Structure Void*
[structure-void.com](https://structure-void.com) ·
[julienbayle.net](http://julienbayle.net)
Ableton Certified Trainer · Max Certified Trainer

---

> Independent research project. Not affiliated with or endorsed by Ableton AG,
> Cycling '74, or Derivative Inc.
