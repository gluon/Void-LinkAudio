# VoidLinkAudio — Pure Data

Source code and build instructions for the **VoidLinkAudio** externals for
Pure Data vanilla. If you just want to use precompiled binaries, grab them
from [Releases](../../releases) instead — this folder is for people who want
to read the code, modify it, and build their own.

To the best of our knowledge, this is the **first public Pd implementation
of Ableton Link Audio**.

## What's in here

```
pd/
├── README.md                                  This file
├── Makefile                                   pd-lib-builder driven
├── build.sh                                   universal-binary wrapper for macOS
├── src/
│   ├── void.linkaudio.send~.cpp               Publisher external
│   └── void.linkaudio.receive~.cpp            Subscriber external
└── help/
    ├── void.linkaudio.send~-help.pd           Help patch (right-click > Help)
    └── void.linkaudio.receive~-help.pd
```

The externals link against the shared C++ core in [`../core/`](../core/)
and the bundled [Ableton Link](https://github.com/Ableton/link) submodule
under `../thirdparty/link/`. The build itself is driven by
[pd-lib-builder](https://github.com/pure-data/pd-lib-builder), pulled in
as a submodule under `../thirdparty/pd-lib-builder/`.

## Object behaviour

### `void.linkaudio.send~`

- Inputs: 2 signal inlets (stereo audio to publish).
- Outputs: 3 signal outlets (`tempo~`, `phase~`, `transport~`) + 1 message
  outlet for status info.
- Messages: `channel <name>` (set channel name), `peer <name>` (set peer
  identity, process-wide), `enable <0|1>`, `quantum <n>`, `tempo <bpm>`
  (writes session tempo), `transport <0|1>` (writes session transport),
  `bang` (retry / re-apply state), `info` (request status dump).

### `void.linkaudio.receive~`

- Inputs: 1 message inlet (control only — receive~ has no audio input).
- Outputs: 2 signal outlets for L/R audio + 3 timing signal outlets
  (`tempo~`, `phase~`, `transport~`) + 1 message outlet for status.
- Messages: `channel <name>` (subscribe), `frompeer <name>` (peer filter,
  optional), `enable <0|1>`, `quantum <n>`, `tempo <bpm>`,
  `transport <0|1>`, `bang`, `info`.

`tempo` and `transport` messages are **writable** — they propagate the new
value to every Link peer on the LAN (Live, Max, TouchDesigner, VCV Rack,
etc.), and external changes from any peer are reflected back on the
respective signal outlets.

The status message outlet emits sequences of the form
`<key> <values...>` on meaningful state changes (subscribe state, peer
count, channel listing) and on demand via `info`. Pipe through `[route
publishing num_peers tempo channel ...]` to extract individual fields.

See the help patches `void.linkaudio.{send,receive}~-help.pd` for live
demos of all messages and outlets.

## Critical runtime requirement

Pd vanilla performs **no internal sample-rate conversion** on the Link
Audio path. If Pd runs at 44.1 kHz against a publisher at 48 kHz (Ableton
Live default), the receive ring buffer overflows continuously at the rate
ratio (~8 % per second per channel, compounding to ~13 % observed drops).

**Set Pd to 48 kHz** in `Media > Audio settings` for clean interop with
Live and the other VoidLinkAudio hosts (which all run at 48 kHz by default).

If you must keep Pd at a different sample rate for other reasons, expect
audible dropouts on the receive path until an internal SRC is added in a
later release.

## Required setup before building

You'll need:

- **Pure Data vanilla 0.56-2 or later** ([puredata.info](https://puredata.info))
- A C++17 compiler:
  - **macOS** — Apple Clang from Xcode 14+
  - **Windows** — MinGW-w64 gcc 13+ via MSYS2 (see Windows build below)
- The Ableton Link and pd-lib-builder submodules initialised:

  ```bash
  git submodule update --init --recursive
  ```

## Build (macOS)

```bash
cd pd
./build.sh
```

This produces `void.linkaudio.{send,receive}~.pd_darwin` as universal
binaries (arm64 + x86_64), targeting macOS 11.0.

The `build.sh` wrapper exists because `pd-lib-builder` overrides the
Makefile-level `arch =` directive internally. Passing `arch="..."` as a
command-line argument is the only reliable way to force a Mac universal
binary; the wrapper does that on Darwin and is a no-op elsewhere.

The default `PDDIR` in the Makefile points at the author's local Pd
install. Override if Pd is elsewhere:

```bash
make PDDIR=/Applications/Pd-0.56-2.app/Contents/Resources arch="arm64 x86_64"
```

## Build (Windows)

The build runs natively on Windows via **MSYS2 + MinGW-w64** (not WSL,
not MSVC). One-time setup:

```powershell
# Install MSYS2
winget install MSYS2.MSYS2

# In MSYS2 shell, install the toolchain:
pacman -S --needed base-devel mingw-w64-x86_64-toolchain

# Then add to PATH (User env vars), in this order:
#   C:\msys64\usr\bin
#   C:\msys64\mingw64\bin
```

Then build:

```powershell
cd pd
make PDDIR=C:/Pd
```

Where `C:/Pd` (note forward slashes, no quotes) is your Pd vanilla
install root — the folder containing `src/`, `bin/`, `doc/`. Output:
`void.linkaudio.{send,receive}~.dll`.

> ⚠️ If you have **GnuWin32 make 3.81** installed (older), make sure
> MSYS2's `make` (4.x) comes first in your PATH. The legacy GnuWin32
> make doesn't understand pd-lib-builder's modern Makefile syntax.

## Install

Pd vanilla loads externals from any folder in its **search path**. The
simplest setup is to add the `pd/` folder of this repo directly:

1. Open Pd.
2. **File > Preferences > Path** (or `Edit > Preferences > Path` depending
   on platform).
3. Add the path to this `pd/` folder (which contains both the externals
   and the `help/` subfolder).
4. Restart Pd.

Now `[void.linkaudio.send~]` and `[void.linkaudio.receive~]` are
instantiable in any patch, and right-click > Help opens the matching
help patch.

For a system-wide install instead, copy the externals into Pd's standard
externals folder:

- **macOS** — `~/Documents/Pd/externals/`
- **Windows** — `%APPDATA%\Pd\externals\` (typically `C:\Users\<you>\AppData\Roaming\Pd\externals\`)

## plugdata bonus

The Pd port automatically covers [plugdata](https://plugdata.org), Tim
Schoen's libpd-based VST3/AU/CLAP host + standalone application. One Pd
build effort yields:

- Pd vanilla (this repo)
- plugdata standalone (Mac, Win, Linux)
- plugdata-as-VST/AU/CLAP inside Live, Reaper, Bitwig, Logic, etc.

End-to-end validation in plugdata is on the v0.2.x roadmap.

## Code architecture (short)

Each external is a thin wrapper around the shared core:

- `LinkAudioManager` (in [`../core/`](../core/)) is a process-wide
  singleton that owns the Link session and exposes app-thread
  getters/setters for tempo and transport, plus a real-time-safe
  `captureAppSessionState()` for use inside the audio thread.
- `LinkAudio<Send|Receive>Stream` (also in core) owns a worker thread for
  Link API calls and a lock-free SPSC ring buffer for the audio path.
- The Pd external forwards `perform()` DSP callbacks into the stream's
  audio callbacks. Messages are dispatched through standard Pd
  `class_addmethod()` registration.

Threading model:

- Worker thread: runs Link callbacks, manages subscriptions, signals the
  audio thread when the topology changes.
- Audio thread (`perform()`): pulls/pushes samples from/to the ring
  buffer, advances tempo / phase / transport signal outputs from the
  captured session state. Never blocks. Never allocates.
- Main thread (Pd scheduler): periodic 250 ms clock retries
  subscribe/publish on connection loss, and emits status info messages
  on state changes.

If you read just one file, start with `void.linkaudio.send~.cpp` — the
rest of the moving parts are abstracted behind the core API.

## License

GPL-2.0-or-later — see the root [`LICENSE`](../LICENSE) file.