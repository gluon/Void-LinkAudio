// ============================================================================
// VoidLinkAudio - LinkAudioReceive (TouchDesigner CHOP)
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

    // ---- From Peer ----
    // The Peer Name of the SEND we subscribe to. Without this, if two peers
    // publish a channel with the same name, the first one found wins. Set
    // this to the exact Peer Name announced by the source (e.g. "VCVRack",
    // "Live", "Max"). Leave empty only if you trust there is exactly one
    // publisher of From Channel on the network.
    {
        OP_StringParameter p;
        p.name         = FromPeerName;
        p.label        = FromPeerLabel;
        p.page         = "Link Audio";
        p.defaultValue = "";
        m->appendString(p);
    }

    // ---- From Channel ----
    // Name of the audio channel published by the source peer (matches the
    // "Channel Name" field in the Send CHOP/external).
    {
        OP_StringParameter p;
        p.name         = FromChannelName;
        p.label        = FromChannelLabel;
        p.page         = "Link Audio";
        p.defaultValue = "";
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
Parameters::evalFromPeer(const OP_Inputs* in)
{
    const char* s = in->getParString(FromPeerName);
    return s ? std::string(s) : std::string();
}

std::string
Parameters::evalFromChannel(const OP_Inputs* in)
{
    const char* s = in->getParString(FromChannelName);
    return s ? std::string(s) : std::string();
}

double
Parameters::evalQuantum(const OP_Inputs* in)
{
    return in->getParDouble(QuantumName);
}
