// ============================================================================
// VoidLinkAudio - LinkAudioSend (TouchDesigner CHOP)
//
// Part of the VoidLinkAudio R&D project by Julien Bayle / Structure Void.
// https://julienbayle.net    https://structure-void.com
//
// Released under the MIT License - see LICENSE file at repo root.
// Provided AS IS, without warranty of any kind.
// ============================================================================

#include "LinkAudioSend.h"

#include <algorithm>
#include <cstring>

using namespace TD;

// ============================================================================
// Plugin entry points
// ============================================================================

extern "C"
{

DLLEXPORT
void FillCHOPPluginInfo(CHOP_PluginInfo* info)
{
    info->apiVersion = CHOPCPlusPlusAPIVersion;

    info->customOPInfo.opType ->setString("Linkaudiosend");
    info->customOPInfo.opLabel->setString("Link Audio Send");
    info->customOPInfo.opIcon ->setString("LAS");
    info->customOPInfo.authorName ->setString("Julien Bayle");
    info->customOPInfo.authorEmail->setString("contact@structure-void.com");

    info->customOPInfo.minInputs = 1;
    info->customOPInfo.maxInputs = 1;
}

DLLEXPORT
CHOP_CPlusPlusBase* CreateCHOPInstance(const OP_NodeInfo* info)
{
    return new LinkAudioSend(info);
}

DLLEXPORT
void DestroyCHOPInstance(CHOP_CPlusPlusBase* inst)
{
    delete static_cast<LinkAudioSend*>(inst);
}

} // extern "C"


// ============================================================================
// Construction / destruction
// ============================================================================

LinkAudioSend::LinkAudioSend(const OP_NodeInfo*)
{
    mManager = LinkAudioManager::acquire();
    mInt16Scratch.reserve(8192);  // 4096 frames * 2 channels
}

LinkAudioSend::~LinkAudioSend()
{
    destroySink();
}

void
LinkAudioSend::setupParameters(OP_ParameterManager* m, void*)
{
    Parameters::setup(m);
}


// ============================================================================
// Sink management
// ============================================================================

void
LinkAudioSend::ensureSink()
{
    if (mSink || !mManager) return;

    // Initial buffer size: 4096 frames * 2 channels = 8192 samples
    // (~85 ms at 48 kHz stereo). We'll bump dynamically if needed.
    constexpr std::size_t kInitialMaxSamples = 8192;

    auto& la = mManager->linkAudio();
    mSink.reset(new ableton::LinkAudioSink(
        la,
        mCurrentChannelName.empty() ? std::string("TD Send") : mCurrentChannelName,
        kInitialMaxSamples));
}

void
LinkAudioSend::destroySink()
{
    mSink.reset();
}


// ============================================================================
// CHOP callbacks
// ============================================================================

void
LinkAudioSend::getGeneralInfo(CHOP_GeneralInfo* info, const OP_Inputs*, void*)
{
    info->cookEveryFrame    = true;
    info->timeslice         = true;
    info->inputMatchIndex   = 0;
}

bool
LinkAudioSend::getOutputInfo(CHOP_OutputInfo* info, const OP_Inputs* inputs, void*)
{
    const bool        wantEnabled = Parameters::evalEnable(inputs);
    const std::string newPeerName = Parameters::evalPeerName(inputs);
    const std::string newChName   = Parameters::evalChannelName(inputs);
    mQuantum = Parameters::evalQuantum(inputs);

    if (mManager)
    {
        if (!newPeerName.empty() && newPeerName != mCurrentPeerName)
        {
            mManager->setPeerName(newPeerName);
            mCurrentPeerName = newPeerName;
        }

        if (wantEnabled != mEnabled)
        {
            auto& la = mManager->linkAudio();
            la.enable(wantEnabled);
            la.enableLinkAudio(wantEnabled);
            mEnabled = wantEnabled;

            if (!wantEnabled)
                destroySink();
        }
    }

    if (mEnabled)
    {
        ensureSink();

        if (mSink && newChName != mCurrentChannelName && !newChName.empty())
        {
            mSink->setName(newChName);
        }
    }
    mCurrentChannelName = newChName;

    // Match input attributes (numChannels, numSamples, sampleRate).
    return false;
}

void
LinkAudioSend::getChannelName(int32_t index, OP_String* name,
                                 const OP_Inputs*, void*)
{
    if (index == 0)
        name->setString("audioL");
    else if (index == 1)
        name->setString("audioR");
    else
        name->setString("audio");
}

void
LinkAudioSend::execute(CHOP_Output* output, const OP_Inputs* inputs, void*)
{
    // Refresh cached info
    if (mManager)
    {
        auto& la = mManager->linkAudio();
        const auto state = la.captureAppSessionState();
        const auto now   = la.clock().micros();

        mCachedBeat         = state.beatAtTime (now, mQuantum);
        mCachedPhase        = state.phaseAtTime(now, mQuantum);
        mCachedTempo        = state.tempo();
        mCachedIsPlaying    = state.isPlaying();
        mCachedNumPeers     = la.numPeers();
        mCachedAudioEnabled = la.isLinkAudioEnabled();
        mChannelsSnapshot   = la.channels();
        mCachedNumChannels  = mChannelsSnapshot.size();
    }

    // -------- Pass-through input -> output --------
    const OP_CHOPInput* in =
        (inputs->getNumInputs() > 0) ? inputs->getInputCHOP(0) : nullptr;

    const int outChans   = output->numChannels;
    const int outSamples = output->numSamples;

    if (in && in->numChannels > 0 && in->numSamples > 0)
    {
        const int copyChans   = std::min(outChans,   in->numChannels);
        const int copySamples = std::min(outSamples, in->numSamples);
        for (int c = 0; c < copyChans; ++c)
        {
            std::memcpy(output->channels[c],
                        in->channelData[c],
                        copySamples * sizeof(float));
            if (copySamples < outSamples)
                std::memset(output->channels[c] + copySamples, 0,
                            (outSamples - copySamples) * sizeof(float));
        }
        for (int c = copyChans; c < outChans; ++c)
            std::memset(output->channels[c], 0, outSamples * sizeof(float));
    }
    else
    {
        for (int c = 0; c < outChans; ++c)
            std::memset(output->channels[c], 0, outSamples * sizeof(float));
    }

    // -------- Send to Link Audio --------
    if (!mEnabled || !mSink || !mManager || !in ||
        in->numChannels == 0 || in->numSamples <= 0)
    {
        return;
    }

    auto& la = mManager->linkAudio();

    // Mono if input has 1 channel, stereo if 2 or more (clamp at 2 — Link
    // Audio's commit() only accepts 1 or 2).
    const std::size_t numCh = (in->numChannels >= 2) ? 2 : 1;

    const std::size_t numFrames  = static_cast<std::size_t>(in->numSamples);
    const uint32_t    sampleRate = static_cast<uint32_t>(in->sampleRate);

    const std::size_t totalSamples = numFrames * numCh;

    if (totalSamples > mSink->maxNumSamples())
    {
        mSink->requestMaxNumSamples(totalSamples * 2);
    }

    ableton::LinkAudioSink::BufferHandle bh(*mSink);
    if (!bh)
    {
        mFramesNoBuffer.fetch_add(numFrames);
        return;
    }

    if (totalSamples > bh.maxNumSamples)
    {
        mFramesCommitFail.fetch_add(numFrames);
        return;
    }

    // Float32 -> int16 conversion, interleaving LR if stereo
    const float* srcL = in->channelData[0];
    const float* srcR = (numCh == 2) ? in->channelData[1] : nullptr;

    auto floatToInt16 = [](float v) -> int16_t
    {
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        return static_cast<int16_t>(v * 32767.0f);
    };

    if (numCh == 1)
    {
        for (std::size_t i = 0; i < numFrames; ++i)
            bh.samples[i] = floatToInt16(srcL[i]);
    }
    else
    {
        // Interleave LRLRLR...
        for (std::size_t i = 0; i < numFrames; ++i)
        {
            bh.samples[i * 2]     = floatToInt16(srcL[i]);
            bh.samples[i * 2 + 1] = floatToInt16(srcR[i]);
        }
    }

    // Capture Link state and commit
    const auto state = la.captureAppSessionState();
    const auto now   = la.clock().micros();
    const double beatsAtBufferBegin = state.beatAtTime(now, mQuantum);

    const bool ok = bh.commit(state,
                              beatsAtBufferBegin,
                              mQuantum,
                              numFrames,
                              numCh,
                              sampleRate);

    if (ok)
        mFramesSent.fetch_add(numFrames);
    else
        mFramesCommitFail.fetch_add(numFrames);
}


// ============================================================================
// Info CHOP / Info DAT
// ============================================================================

int32_t
LinkAudioSend::getNumInfoCHOPChans(void*)
{
    return 11;
}

void
LinkAudioSend::getInfoCHOPChan(int32_t index, OP_InfoCHOPChan* chan, void*)
{
    switch (index)
    {
    case 0:
        chan->name->setString("enabled");
        chan->value = mEnabled ? 1.0 : 0.0;
        break;
    case 1:
        chan->name->setString("audio_enabled");
        chan->value = mCachedAudioEnabled ? 1.0 : 0.0;
        break;
    case 2:
        chan->name->setString("num_peers");
        chan->value = static_cast<double>(mCachedNumPeers);
        break;
    case 3:
        chan->name->setString("num_audio_channels");
        chan->value = static_cast<double>(mCachedNumChannels);
        break;
    case 4:
        chan->name->setString("sink_active");
        chan->value = mSink ? 1.0 : 0.0;
        break;
    case 5:
        chan->name->setString("frames_sent");
        chan->value = static_cast<double>(mFramesSent.load());
        break;
    case 6:
        chan->name->setString("frames_no_listener");
        chan->value = static_cast<double>(mFramesNoBuffer.load());
        break;
    case 7:
        chan->name->setString("frames_commit_fail");
        chan->value = static_cast<double>(mFramesCommitFail.load());
        break;
    case 8:
        chan->name->setString("tempo");
        chan->value = mCachedTempo;
        break;
    case 9:
        chan->name->setString("beat");
        chan->value = mCachedBeat;
        break;
    case 10:
        chan->name->setString("phase");
        chan->value = mCachedPhase;
        break;
    }
}

bool
LinkAudioSend::getInfoDATSize(OP_InfoDATSize* infoSize, void*)
{
    const int rows = 1 + static_cast<int>(mChannelsSnapshot.size());
    infoSize->rows     = rows;
    infoSize->cols     = 2;
    infoSize->byColumn = false;
    return true;
}

void
LinkAudioSend::getInfoDATEntries(int32_t index, int32_t,
                                    OP_InfoDATEntries* entries, void*)
{
    if (index == 0)
    {
        entries->values[0]->setString("channel_name");
        entries->values[1]->setString("peer_name");
        return;
    }

    const int idx = index - 1;
    if (idx < 0 || idx >= static_cast<int>(mChannelsSnapshot.size()))
        return;

    const auto& ch = mChannelsSnapshot[idx];
    entries->values[0]->setString(ch.name.c_str());
    entries->values[1]->setString(ch.peerName.c_str());
}
