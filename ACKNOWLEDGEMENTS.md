# Acknowledgements

VoidLinkAudio depends on, or interoperates with, software developed
by other parties. This file lists those dependencies and the terms
under which their work is used.

## Ableton Link / Link Audio

This software is built on top of **Ableton Link Audio**, the audio
extension shipped with Ableton Link. Link Audio is open-source software
developed by **Ableton AG** and released under the MIT license.

This implementation is independent and is **not endorsed, certified,
or supported by Ableton**. "Ableton", "Live", "Link", and related
marks are trademarks of Ableton AG.

References:
- Ableton Link source:     https://github.com/Ableton/link
- Link concepts overview:  https://ableton.github.io/link/

The Link source is bundled in this repository as a git submodule under
`thirdparty/link/`. Its license terms are reproduced verbatim below.

```
Copyright 2016, Ableton AG, Berlin. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## ASIO standalone

Bundled with Ableton Link as a sub-submodule. Used by Link for
networking primitives. Boost Software License 1.0.

## TouchDesigner SDK headers

`CHOP_CPlusPlusBase.h` and `CPlusPlus_Common.h` are owned by
**Derivative Inc.** and bundled under their **Shared Use License**.
See the headers themselves for terms.

## Max SDK

Owned by **Cycling '74**. Used under the standard Max SDK license.
SDK headers are not redistributed in this repository; they are pulled
from a local Max SDK installation at build time.

## VCV Rack SDK

Owned by **VCV / Andrew Belt**. Plugin development kit. The Rack SDK
is fetched at build time, not bundled.

## NanoSVG / NanoVG

Used internally by VCV Rack to render plugin panels. Zlib license,
Mikko Mononen.

---

VoidLinkAudio is part of Structure Void's open R&D.
https://structure-void.com  -  https://julienbayle.net
