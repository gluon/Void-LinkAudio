# LinkAudioInCHOP — Stage 1

A TouchDesigner Custom CHOP that joins an Ableton Link Audio session and
exposes its tempo/beat/phase state plus discovery of audio channels
present on the local network.

**Stage 1 scope:** discovery + transport. No audio yet.
The plugin is announced on the Link session, sees other Link Audio peers,
lists their published audio channels, and outputs the live transport
state of the session. Audio streaming (subscribing to a channel) is
Stage 2.

## Output

| channel    | description                                |
| ---------- | ------------------------------------------ |
| beat       | Current beat number (continuous, modulo Q) |
| phase      | Phase position within quantum, [0, Q)      |
| tempo      | Session tempo in BPM                       |
| isPlaying  | 1 when transport is playing, else 0        |

## Info CHOP

`enabled`, `audio_enabled`, `num_peers`, `num_audio_channels`,
`tempo`, `beat`, `phase`.

## Info DAT

One row per discovered audio channel on the network:
`channel_name`, `peer_name`, `channel_id`.

## Parameters

- **Enable** — toggles Link + Link Audio on/off
- **Peer Name** — local identifier shown to other peers
- **Channel ID** — (Stage 2) the channel to subscribe to
- **Quantum** — quantum used for beat/phase mapping (default 4)

## Required setup before building

1. From the parent repo root, ensure the Link submodule is initialized
   recursively (Link bundles `asio-standalone` as a sub-submodule):

   ```bash
   git submodule update --init --recursive
   ```

2. Copy the TouchDesigner SDK headers into this folder. They are local
   per CHOP by convention:

   ```
   CPlusPlus_Common.h
   CHOP_CPlusPlusBase.h
   ```

   On macOS these live under
   `/Applications/TouchDesigner.app/Contents/Resources/Samples/CPlusPlus/`.
   Copying them from an existing working CHOP project (e.g. your
   VOIDSharedMemoryReadCHOP) is the easiest way and guarantees the
   API version matches your installed TD.

## Build (macOS)

From this directory:

```bash
cmake -B build -G Xcode
cmake --build build --config Release
```

Or for a quick command-line build:

```bash
cmake -B build
cmake --build build --config Release
```

The plugin is produced at:

```
plugin/LinkAudioInCHOP.plugin
```

## Loading in TouchDesigner

1. Drop the `.plugin` bundle in your project's `Plugins/` folder
   (or any custom-OP-discoverable location).
2. Restart TouchDesigner. On first load TD asks you to authorize the
   binary — accept.
3. The operator appears as **Link Audio In** in the CHOP create menu.

## Validating Stage 1

- Enable the CHOP. With Live 12.4 (public beta or later) running on
  the same network with Link Audio enabled, you should see
  `num_peers >= 1` in the Info CHOP.
- Enable a Link Audio sink in Live (e.g. an audio track set to send
  via Link Audio). Its channel should appear in the Info DAT.
- The `beat`/`phase`/`tempo`/`isPlaying` channels should follow Live's
  transport.

When all of the above is verified, we are ready for Stage 2 (audio
streaming via `LinkAudioSource`).
