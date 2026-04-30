# VoidLinkAudio

> Multi-host integration of **Ableton Link Audio** — stream audio over a local
> network, sample-accurate, beat-synced, between any combination of TouchDesigner,
> Max/MSP, VCV Rack, openFrameworks, and Ableton Live.

> **Status: early R&D release (v0.1.0).** Built on top of Ableton's open-source
> [Link](https://github.com/Ableton/link) library (GPL-2.0-or-later). Link Audio
> is currently an alpha API — expect evolution.
>
> macOS Universal (Apple Silicon + Intel) and Windows x64 — both supported.

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

## Supported hosts

| Host                     | Platforms                             | Status                  |
|--------------------------|---------------------------------------|-------------------------|
| **Ableton Live 12.4+**   | native (Mac + Win)                    | works out of the box    |
| **Max / MSP**            | macOS Universal, Windows x64          | working                 |
| **TouchDesigner**        | macOS Universal, Windows x64          | working                 |
| **VCV Rack 2**           | macOS arm64 + x64, Windows x64        | working                 |
| **openFrameworks**       | macOS, Linux, Windows                 | working (separate repo) |

Cross-platform interop has been validated: audio passes between Win Max ↔ Mac
Max / Live / TouchDesigner / VCV Rack and back.

---

## Repository layout

| Path           | Host                | Notes                                  |
|----------------|---------------------|----------------------------------------|
| `core/`        | Shared C++          | LinkAudioManager + AudioRingBuffer     |
| `max/`         | Max / MSP externals | Full Max package (helpers, refpages)   |
| `td/`          | TouchDesigner CHOPs | Send + Receive CHOPs                   |
| `vcv/`         | VCV Rack module     | Send + Receive modules                 |
| `thirdparty/`  | Submodule           | [Ableton/link](https://github.com/Ableton/link) (GPL-2.0-or-later) |

**openFrameworks** support lives in its own repo, following the `ofxAddon`
convention:
**[gluon/ofxAbletonLinkAudio](https://github.com/gluon/ofxAbletonLinkAudio)**

**VST / AU / CLAP** plugins are distributed separately at
**[structure-void.com](https://structure-void.com)**.

---

## Quick start — precompiled binaries

The fastest way to use VoidLinkAudio. Pre-built, signed (Mac notarised, Win
signed) binaries are available in [Releases](../../releases):

- `VoidLinkAudio-Max-vX.Y.Z.zip` — Max package (Mac + Win in one)
- `VoidLinkAudio-TD-vX.Y.Z.zip` — TouchDesigner CHOPs (Mac + Win)
- `VoidLinkAudio-VCV-vX.Y.Z.zip` — VCV Rack plugin (Mac arm64/x64 + Win)

Each zip contains a README with install instructions specific to that host.

---

## Build from source

You'll need:

- **macOS 11+** (universal arm64+x86_64) or **Windows 10 x64**
- **CMake 3.22+**
- A **C++17** compiler (Apple Clang, MSVC 2022, or MinGW gcc 13+)
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

- **`LinkAudioReceive`** — subscribe to a remote channel; output L/R audio
- **`LinkAudioSend`** — publish a TD audio signal as a Link Audio channel

```bash
cd td/CHOP/LinkAudioReceive
cmake -B build -G Xcode
cmake --build build --config Release

cd ../LinkAudioSend
cmake -B build -G Xcode
cmake --build build --config Release
```

Outputs land under each CHOP's `plugin/Release/` folder. Drop the
`.plugin` bundles into your TD project's `Plugins/` folder, or symlink to:

```
~/Library/Application Support/Derivative/TouchDesigner099/Plugins/
```

### Max / MSP

Two MSP externals plus a full Max package (with help patchers, refpages,
vignette tooltip):

- **`void.linkaudio.receive~`** — receive audio from a Link Audio channel
- **`void.linkaudio.send~`** — publish 1 or 2 channels of audio

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

Two modules, **Void Link Audio Send** and **Void Link Audio Receive**, both
visible in the **Structure Void** browser category.

You'll need the [Rack SDK](https://vcvrack.com/manual/Building#Plugins) for
your target architecture.

```bash
cd vcv
RACK_DIR=/path/to/Rack-SDK make
```

For dual-arch macOS builds (arm64 + x64), see `vcv/README.md` for details.

> ⚠️ **Important**: VCV Rack's engine has no internal clock. You must have an
> Audio module in your VCV patch for VoidLinkAudio Send to publish at the
> correct sample rate. The Audio module doesn't need to be connected to anything.

### openFrameworks

See the dedicated repo:
**[gluon/ofxAbletonLinkAudio](https://github.com/gluon/ofxAbletonLinkAudio)**

It contains the addon source plus example projects (sender, receiver, ping-pong).

---

## License

VoidLinkAudio is released under **GPL-2.0-or-later** — see [`LICENSE`](LICENSE).

This project links statically against [Ableton Link](https://github.com/Ableton/link),
which is itself GPL-2.0-or-later. The viral GPL clause means any binary that
links against Link is also GPL.

A commercial (non-GPL) license for Link is available directly from Ableton:
<link-devs@ableton.com>.

See [`ACKNOWLEDGEMENTS.md`](ACKNOWLEDGEMENTS.md) for the complete attribution
list including TouchDesigner SDK, Max SDK, VCV Rack SDK, and asio-standalone.

---

## Acknowledgements

Built on top of [Ableton Link](https://github.com/Ableton/link), the
open-source synchronization library by **Ableton AG**. Link Audio is the audio
extension shipped with Ableton Link starting in Live 12.4.

This implementation is independent and is **not endorsed, certified, or
supported by Ableton**. "Ableton", "Live", and "Link" are trademarks of
Ableton AG.

---

## Author

**Julien Bayle** — Structure Void
[https://structure-void.com](https://structure-void.com) · [https://julienbayle.net](https://julienbayle.net)
Ableton Certified Trainer · Max Certified Trainer

---

*This is R&D code. It builds, runs, and has been validated in real bidirectional
sessions across multiple hosts on the same LAN — but it's not a commercial
product. No warranty, express or implied. Use, fork, ship, break. If you ship
something with it, a credit (and a postcard) is appreciated but not required.*