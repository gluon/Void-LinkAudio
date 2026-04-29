# Acknowledgements

VoidLinkAudio depends on, or interoperates with, software developed
by other parties. This file lists those dependencies and the terms
under which their work is used.

## Ableton Link / Link Audio

This software is built on top of **Ableton Link Audio**, the audio
extension shipped with Ableton Link. Link is open-source software
developed by **Ableton AG** and released under the **GNU General
Public License v2.0 or later** (GPL-2.0-or-later).

Because VoidLinkAudio links statically against Link, the entire
combined work is itself distributed under GPL-2.0-or-later. This is
why this project is GPL-licensed (see [`LICENSE`](LICENSE) at the
repo root).

A commercial (non-GPL) license for Link is available directly from
Ableton: <link-devs@ableton.com>.

This implementation is independent and is **not endorsed, certified,
or supported by Ableton**. "Ableton", "Live", "Link", and related
marks are trademarks of Ableton AG.

References:
- Ableton Link source:     https://github.com/Ableton/link
- Link concepts overview:  https://ableton.github.io/link/
- Link license:            https://github.com/Ableton/link/blob/master/LICENSE.md

The Link source is bundled in this repository as a git submodule under
`thirdparty/link/`. Its full GPL v2 license text is reproduced in
`thirdparty/link/GNU-GPL-v2.0.md`.

## ASIO standalone

Bundled with Ableton Link as a sub-submodule under
`thirdparty/link/modules/asio-standalone/`. Used by Link for networking
primitives (this is the standalone Boost ASIO library, not the audio
ASIO driver protocol). Boost Software License 1.0.

## TouchDesigner SDK headers

`CHOP_CPlusPlusBase.h` and `CPlusPlus_Common.h` are owned by
**Derivative Inc.** and bundled under their **Shared Use License**.
See the headers themselves for terms.

## Max SDK

Owned by **Cycling '74**. Used under the standard Max SDK license.
SDK headers are not redistributed in this repository; they are pulled
from a local Max SDK installation at build time.

## VCV Rack SDK

Owned by **VCV / Andrew Belt**. The Rack SDK is GPL-3.0-or-later
(compatible with this project's GPL-2.0-or-later licensing). The SDK
is fetched at build time, not bundled.

## NanoSVG / NanoVG

Used internally by VCV Rack to render plugin panels. Zlib license,
Mikko Mononen.

---

VoidLinkAudio is part of Structure Void's open R&D.
https://structure-void.com  -  https://julienbayle.net