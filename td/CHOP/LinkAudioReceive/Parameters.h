// ============================================================================
// VoidLinkAudio - LinkAudioReceive (TouchDesigner CHOP)
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
// Parameter names / labels (Receive)
//
// Sémantique:
//   - From Peer    : the Peer Name of the SEND we want to subscribe to
//                    (= the value that peer set in its own "Peer Name" param).
//                    Required to disambiguate when several peers publish a
//                    channel with the same name.
//   - From Channel : the channel name to subscribe to on that peer.
//
// Internal short names ("Frompeer", "Fromchannel") are what TD writes in
// .toe files and what Python expressions reference.
// ============================================================================

constexpr static char EnableName[]        = "Enable";
constexpr static char EnableLabel[]       = "Enable";

constexpr static char FromPeerName[]      = "Frompeer";
constexpr static char FromPeerLabel[]     = "From Peer";

constexpr static char FromChannelName[]   = "Fromchannel";
constexpr static char FromChannelLabel[]  = "From Channel";

constexpr static char QuantumName[]       = "Quantum";
constexpr static char QuantumLabel[]      = "Quantum";


// ============================================================================
// Parameter accessors
// ============================================================================

class Parameters
{
public:
    static void setup(TD::OP_ParameterManager*);

    static bool        evalEnable      (const TD::OP_Inputs*);
    static std::string evalFromPeer    (const TD::OP_Inputs*);
    static std::string evalFromChannel (const TD::OP_Inputs*);
    static double      evalQuantum     (const TD::OP_Inputs*);
};
