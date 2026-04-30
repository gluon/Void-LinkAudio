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

#include "Parameters.h"
#include "CPlusPlus_Common.h"

using namespace TD;

void
Parameters::setup(OP_ParameterManager* m)
{
    // ---- Enable ----
    {
        OP_NumericParameter p;
        p.name             = EnableName;
        p.label            = EnableLabel;
        p.page             = "Link Audio";
        p.defaultValues[0] = 1;
        p.minValues[0]     = 0;
        p.maxValues[0]     = 1;
        p.minSliders[0]    = 0;
        p.maxSliders[0]    = 1;
        p.clampMins[0]     = true;
        p.clampMaxes[0]    = true;
        m->appendInt(p);
    }

    // ---- Peer Name ----
    // The identity this peer announces on the Link Audio network. Other
    // peers see this string and reference it in their "From Peer" field
    // to subscribe to channels published here. Pick something distinctive
    // (e.g. "TouchDesigner-Stage", "TD-Mac1") if multiple TDs are on the
    // same network.
    {
        OP_StringParameter p;
        p.name         = PeerNameName;
        p.label        = PeerNameLabel;
        p.page         = "Link Audio";
        p.defaultValue = "TouchDesigner";
        m->appendString(p);
    }

    // ---- Channel Name ----
    // The name of the audio channel this CHOP publishes on the network.
    // Receivers will reference this in their "From Channel" field. If you
    // run several Send CHOPs in the same project, use distinct names.
    {
        OP_StringParameter p;
        p.name         = ChannelNameName;
        p.label        = ChannelNameLabel;
        p.page         = "Link Audio";
        p.defaultValue = "TD Send";
        m->appendString(p);
    }

    // ---- Quantum ----
    {
        OP_NumericParameter p;
        p.name             = QuantumName;
        p.label            = QuantumLabel;
        p.page             = "Link Audio";
        p.defaultValues[0] = 4.0;
        p.minValues[0]     = 1.0;
        p.maxValues[0]     = 16.0;
        p.minSliders[0]    = 1.0;
        p.maxSliders[0]    = 16.0;
        p.clampMins[0]     = true;
        m->appendFloat(p);
    }

    // ---- Tempo (R/W mirror of the session BPM) ----
    // Setting this param pushes the new tempo to the shared Link session,
    // which propagates to all peers. The CHOP only pushes when this param
    // actually changes; external tempo changes (e.g. Live) are NOT mirrored
    // back into this param (params represent user intent, the live session
    // value is exposed via the Info CHOP `tempo` channel).
    {
        OP_NumericParameter p;
        p.name             = TempoName;
        p.label            = TempoLabel;
        p.page             = "Link Audio";
        p.defaultValues[0] = 120.0;
        p.minValues[0]     = 20.0;
        p.maxValues[0]     = 999.0;
        p.minSliders[0]    = 60.0;
        p.maxSliders[0]    = 200.0;
        p.clampMins[0]     = true;
        p.clampMaxes[0]    = true;
        m->appendFloat(p);
    }

    // ---- Transport (R/W mirror of session play/stop) ----
    // Toggle: 1 = playing, 0 = stopped. Propagation between peers requires
    // Start/Stop Sync, which the LinkAudioManager enables internally.
    // Same push-on-change semantics as Tempo; live state via Info CHOP
    // `transport` channel.
    {
        OP_NumericParameter p;
        p.name             = TransportName;
        p.label            = TransportLabel;
        p.page             = "Link Audio";
        p.defaultValues[0] = 0;
        p.minValues[0]     = 0;
        p.maxValues[0]     = 1;
        p.minSliders[0]    = 0;
        p.maxSliders[0]    = 1;
        p.clampMins[0]     = true;
        p.clampMaxes[0]    = true;
        m->appendInt(p);
    }
}

bool
Parameters::evalEnable(const OP_Inputs* in)
{
    return in->getParInt(EnableName) != 0;
}

std::string
Parameters::evalPeerName(const OP_Inputs* in)
{
    const char* s = in->getParString(PeerNameName);
    return s ? std::string(s) : std::string();
}

std::string
Parameters::evalChannelName(const OP_Inputs* in)
{
    const char* s = in->getParString(ChannelNameName);
    return s ? std::string(s) : std::string();
}

double
Parameters::evalQuantum(const OP_Inputs* in)
{
    return in->getParDouble(QuantumName);
}

double
Parameters::evalTempo(const OP_Inputs* in)
{
    return in->getParDouble(TempoName);
}

bool
Parameters::evalTransport(const OP_Inputs* in)
{
    return in->getParInt(TransportName) != 0;
}
