// ============================================================================
// VoidLinkAudio - LinkAudioSend (TouchDesigner CHOP)
//
// Part of the VoidLinkAudio R&D project by Julien Bayle / Structure Void.
// https://julienbayle.net    https://structure-void.com
//
// Released under the MIT License - see LICENSE file at repo root.
// Provided AS IS, without warranty of any kind.
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
