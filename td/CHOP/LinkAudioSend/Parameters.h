// ============================================================================
// VoidLinkAudio - LinkAudioSend / Parameters (TouchDesigner CHOP)
//
// Part of the VoidLinkAudio R&D project by Julien Bayle / Structure Void.
// https://julienbayle.net    https://structure-void.com
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
// Built on top of Ableton Link Audio (GPL v2+, see ACKNOWLEDGEMENTS.md).
// ============================================================================

#pragma once

#include <string>

namespace TD
{
    class OP_Inputs;
    class OP_ParameterManager;
}

// ============================================================================
// Parameter names / labels (Send)
//
// Sémantique:
//   - Peer Name    : how this peer announces itself on the Link Audio
//                    network. Other Receive nodes use this string in their
//                    "From Peer" field to disambiguate.
//   - Channel Name : the name of the audio channel this peer publishes.
//                    Other Receive nodes use this in their "From Channel".
// ============================================================================

constexpr static char EnableName[]       = "Enable";
constexpr static char EnableLabel[]      = "Enable";

constexpr static char PeerNameName[]     = "Peername";
constexpr static char PeerNameLabel[]    = "Peer Name";

constexpr static char ChannelNameName[]  = "Channelname";
constexpr static char ChannelNameLabel[] = "Channel Name";

constexpr static char QuantumName[]      = "Quantum";
constexpr static char QuantumLabel[]     = "Quantum";

// R/W mirrors of the shared Link session state. Setting these in the
// CHOP propagates to all peers (Live, Max, other TDs, etc.). Reading
// the live session value is done via the Info CHOP (`tempo`,
// `transport` channels).
constexpr static char TempoName[]        = "Tempo";
constexpr static char TempoLabel[]       = "Tempo";

constexpr static char TransportName[]    = "Transport";
constexpr static char TransportLabel[]   = "Transport";


class Parameters
{
public:
    static void setup(TD::OP_ParameterManager*);

    static bool        evalEnable      (const TD::OP_Inputs*);
    static std::string evalPeerName    (const TD::OP_Inputs*);
    static std::string evalChannelName (const TD::OP_Inputs*);
    static double      evalQuantum     (const TD::OP_Inputs*);
    static double      evalTempo       (const TD::OP_Inputs*);
    static bool        evalTransport   (const TD::OP_Inputs*);
};
