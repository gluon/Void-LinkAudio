// ============================================================================
// VoidLinkAudio - LinkAudioPlatform (platform compatibility shim)
//
// Part of the VoidLinkAudio R&D project by Julien Bayle / Structure Void.
//
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Julien Bayle / Structure Void
//
// VoidLinkAudio is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details — full text in LICENSE at
// the repo root, or at <https://www.gnu.org/licenses/gpl-2.0.html>.
//
// Must be included before any <ableton/...> header.
// ============================================================================

#pragma once

#ifdef _WIN32
  // Order matters: <winsock2.h> must come before <windows.h> on MSVC,
  // otherwise <windows.h> pulls a stale <winsock.h>.
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif

  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <stdlib.h>

  // <objbase.h> (transitively pulled by <windows.h>) defines `interface`
  // as a macro for COM. Ableton Link's API uses `interface` as a parameter
  // name and template parameter — collides with the macro under MSVC.
  #ifdef interface
    #undef interface
  #endif

  // MSVC's <winsock2.h> doesn't define htonll/ntohll despite they being
  // standard on macOS and Linux. Link's discovery byte-stream serialization
  // requires them. Provide a portable shim using MSVC intrinsics.
  #ifndef htonll
    #define htonll(x) _byteswap_uint64(x)
  #endif
  #ifndef ntohll
    #define ntohll(x) _byteswap_uint64(x)
  #endif

  // Required Windows libraries for Link's networking (linker pragma).
  #pragma comment(lib, "ws2_32.lib")
  #pragma comment(lib, "iphlpapi.lib")
  #pragma comment(lib, "winmm.lib")
#endif
