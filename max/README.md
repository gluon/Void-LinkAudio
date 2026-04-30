# VoidLinkAudio — Max / MSP

Source code and build instructions for the **VoidLinkAudio** Max externals
and Max package. If you just want to use precompiled binaries, grab them
from [Releases](../../releases) instead — this folder is for people who
want to read the code, modify it, and build their own.

## What's in here

```
max/
├── README.md                                       This file
├── source/
│   └── objects/
│       ├── void.linkaudio.send~/
│       │   ├── void.linkaudio.send~.cpp            MSP external (audio path, threading)
│       │   └── CMakeLists.txt
│       └── void.linkaudio.receive~/
│           ├── void.linkaudio.receive~.cpp
│           └── CMakeLists.txt
├── package/                                        Drop-in Max package
│   ├── README.md                                   Object documentation (peers, channels, attrs)
│   ├── docs/                                       maxref.xml + vignette tooltip
│   ├── extras/VOID_LinkAudio.maxpat                Overview patcher
│   ├── help/                                       Help patchers per object
│   └── externals/                                  .mxo / .mxe64 binaries (after build)
└── externals/                                      Build output (alternative location)
```

The externals link against the shared C++ core in [`../core/`](../core/)
and the bundled [Ableton Link](https://github.com/Ableton/link) submodule
under `../thirdparty/link/`. There is no separate copy inside `max/`.

## Object behaviour

### `void.linkaudio.send~`

- Inputs: 1 or 2 audio signals to publish.
- Outputs: `tempo~`, `phase~`, `transport~` (Link session timing) plus a
  status `dumpout` (`t_dictionary` reflecting peers, channel state,
  traffic counters, transport).
- Attributes (R/W): `@channel`, `@peer`, `@enable`, `@stereo`, `@tempo`,
  `@transport`.

### `void.linkaudio.receive~`

- Outputs: `L`, `R` audio + `tempo~`, `phase~`, `transport~` + status `dumpout`.
- Attributes (R/W): `@frompeer`, `@fromchannel`, `@enable`, `@tempo`,
  `@transport`.

Tempo and Transport attributes are bidirectional — changing them on a Max
object propagates to every Link peer on the LAN (Live, TouchDesigner, VCV,
etc.), and incoming changes from other peers are reflected back on the
attributes and outlets.

See [`package/README.md`](package/README.md) for the full object
documentation, attribute list, dumpout dictionary structure, and patching
patterns.

## Required setup before building

You'll need:

- The [Max SDK 9](https://github.com/Cycling74/max-sdk) (8.2.0 or later)
- CMake 3.22+
- A C++17 compiler (Apple Clang, MSVC 2022, or MinGW gcc 13+)
- The Ableton Link submodule initialised:

  ```bash
  git submodule update --init --recursive
  ```

Set the Max SDK path:

```bash
export MAX_SDK_PATH=/path/to/your/max-sdk
```

On Windows, set the env var via `[Environment]::SetEnvironmentVariable(...)`
or in your shell profile.

## Build (macOS)

```bash
cd max
cmake -B build -G Xcode
cmake --build build --config Release
```

Outputs land at `max/externals/*.mxo`. They are universal binaries
(arm64 + x86_64) when built on Apple Silicon with Xcode.

## Build (Windows)

```bash
cd max
cmake -B build-win -G "Visual Studio 17 2022" -A x64
cmake --build build-win --config Release
```

Outputs land at `max/externals/*.mxe64`.

## Install

Symlink (or copy) the `package/` folder into your Max packages folder:

**macOS:**

```bash
ln -s "$(pwd)/package" "$HOME/Documents/Max 9/Packages/VoidLinkAudio"
```

**Windows (PowerShell):**

```powershell
New-Item -ItemType SymbolicLink `
  -Path "$env:USERPROFILE\Documents\Max 9\Packages\VoidLinkAudio" `
  -Target "$pwd\package"
```

Drop the freshly-built externals from `max/externals/` into the package's
`externals/` folder (`.mxo` for Mac, `.mxe64` for Windows), and restart Max.

## Code architecture (short)

Each external is a thin wrapper around the shared core:

- `LinkAudioManager` (in `../core/`) is a process-wide singleton that owns
  the Link session and exposes app-thread getters/setters for tempo and
  transport, plus a real-time-safe `captureAppSessionState()` for use
  inside the audio thread.
- `LinkAudio<Send|Receive>Stream` (also in core) owns a worker thread for
  Link API calls (subscribe/publish/peer setup) and a lock-free SPSC ring
  buffer for the audio path.
- The Max external forwards MSP `perform64()` callbacks into the stream's
  audio callbacks. Attributes are routed via standard Max getters/setters.

Threading model:

- Worker thread: runs Link callbacks, manages subscriptions, signals the
  audio thread when the topology changes.
- Audio thread: pulls/pushes samples from/to the ring buffer in
  `perform64()`. Never blocks. Never allocates.
- Main thread (Max UI): reads the cached state for `dumpout` dictionary
  and applies attribute changes via the worker thread.

If you read just one file, start with `void.linkaudio.send~.cpp` —
the rest of the moving parts are abstracted behind the core API.

## License

GPL-2.0-or-later — see the root [`LICENSE`](../LICENSE) file.