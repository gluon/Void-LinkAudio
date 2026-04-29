// ============================================================================
// VoidLinkAudio - LinkAudioSend (TouchDesigner CHOP)
//
// Part of the VoidLinkAudio R&D project by Julien Bayle / Structure Void.
// https://julienbayle.net    https://structure-void.com
//
// Released under the MIT License - see LICENSE file at repo root.
// Provided AS IS, without warranty of any kind.
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


class Parameters
{
public:
    static void setup(TD::OP_ParameterManager*);

    static bool        evalEnable      (const TD::OP_Inputs*);
    static std::string evalPeerName    (const TD::OP_Inputs*);
    static std::string evalChannelName (const TD::OP_Inputs*);
    static double      evalQuantum     (const TD::OP_Inputs*);
};
