# VOID Link Audio

Ableton Link Audio receiver and publisher for Max.

Stream audio between Max, Ableton Live, TouchDesigner, VCV Rack, and any other Link Audio host on the local network — sample-accurate, beat-synced, stereo.

---

## Objects

- **`void.linkaudio.send~`** — publish one or two channels of audio onto the network under a user-defined channel name.
- **`void.linkaudio.receive~`** — subscribe to an audio channel published on the network and output the received signal on its left and right outlets.

Both objects expose a status `t_dictionary` on their dumpout that always reflects the current state (peers, channels, traffic counters, Link timing).

---

## How peers and channels are addressed

```
   Max instance A                        Max instance B
   (or any Link Audio host)              (or any Link Audio host)
   ─────────────────────                 ─────────────────────────

   void.linkaudio.send~                  void.linkaudio.receive~
       @peer    "Max-Stage1"  ─────►         @frompeer    "Max-Stage1"
       @channel "drums"       ─────►         @fromchannel "drums"
```

A **Send** advertises one channel (`@channel`) under one peer identity (`@peer`).
A **Receive** picks one channel from one peer by their pair (`@frompeer`, `@fromchannel`).

`@frompeer` is **essential** when several peers publish a channel with the same name — without it, the first match on the network wins.

Channel names are case-sensitive. Within one Max process, the local peer name is shared (Link Audio uses one peer identity per process).

---

## Installation

Drop this folder into your Max user packages directory:

- **macOS**: `~/Documents/Max 9/Packages/`
- **Windows**: `Documents\Max 9\Packages\`

Restart Max. The objects will appear in the package browser and become available for use.

---

## Quick start

```
[cycle~ 220]    [cycle~ 330]
       \           /
       [*~ 0.2]  [*~ 0.2]
            \   /
[void.linkaudio.send~ @stereo 1 @channel "Max Out"]
```

In Ableton Live: Preferences → Link → Audio → On. Set a track's Audio From to `Max / Max Out`. Turn on monitoring. You should hear the two sine waves.

For the receive direction, see `extras/VOID_LinkAudio.maxpat` for an overview patch.

---

## Tip — Live drops on first connect

When Max is already publishing and Live then opens with the channel pre-bound, Live may show heavy buffer underruns at first. Toggling `@enable 0` then `@enable 1` on the Send (or sending a `bang`) typically resets the connection cleanly. This is a startup-state quirk on Live's side, not a streaming bug.

---

## Compatibility

- macOS 11.0+ (Apple Silicon and Intel)
- Windows 10+ (x64)
- Max 9.0 or later

---

## Author

Julien Bayle — Structure Void

[https://structure-void.com](https://structure-void.com)

---

## Acknowledgements

Built on top of [Ableton Link](https://github.com/Ableton/link), the open-source synchronization library by **Ableton AG** (MIT license). Link Audio is the audio extension shipped with Ableton Link starting from Live 12.4.

This implementation is independent and is **not endorsed, certified, or supported by Ableton**. "Ableton", "Live", and "Link" are trademarks of Ableton AG.

References:
- [Ableton Link source on GitHub](https://github.com/Ableton/link)
- [Link concepts overview](https://ableton.github.io/link/)

VOID Link Audio is released under the MIT License (see LICENSE at the repo root).