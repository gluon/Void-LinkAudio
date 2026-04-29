// ============================================================================
// VoidLinkAudio - LinkAudioReceive (TouchDesigner CHOP)
//
// Part of the VoidLinkAudio R&D project by Julien Bayle / Structure Void.
// https://julienbayle.net    https://structure-void.com
//
// Released under the MIT License - see LICENSE file at repo root.
// Provided AS IS, without warranty of any kind.
// ============================================================================

#include "LinkAudioReceive.h"

extern "C"
{

DLLEXPORT
void FillCHOPPluginInfo(CHOP_PluginInfo* info)
{
    info->apiVersion = CHOPCPlusPlusAPIVersion;

    info->customOPInfo.opType ->setString("Linkaudioreceive");
    info->customOPInfo.opLabel->setString("Link Audio Receive");
    info->customOPInfo.opIcon ->setString("LAR");
    info->customOPInfo.authorName ->setString("Julien Bayle");
    info->customOPInfo.authorEmail->setString("contact@structure-void.com");

    info->customOPInfo.minInputs = 0;
    info->customOPInfo.maxInputs = 0;
}

DLLEXPORT
CHOP_CPlusPlusBase* CreateCHOPInstance(const OP_NodeInfo* info)
{
    return new LinkAudioReceive(info);
}

DLLEXPORT
void DestroyCHOPInstance(CHOP_CPlusPlusBase* inst)
{
    delete static_cast<LinkAudioReceive*>(inst);
}

} // extern "C"


// ============================================================================
// Construction / destruction
// ============================================================================

LinkAudioReceive::LinkAudioReceive(const OP_NodeInfo*)
{
    mManager = LinkAudioManager::acquire();
}

LinkAudioReceive::~LinkAudioReceive()
{
    mSource.reset();
}

void
LinkAudioReceive::setupParameters(OP_ParameterManager* m, void*)
{
    Parameters::setup(m);
}


// ============================================================================
// Subscribe / unsubscribe
// ============================================================================

std::optional<ableton::ChannelId>
LinkAudioReceive::findChannelId(const std::string& chName,
                               const std::string& peerFilter) const
{
    if (chName.empty() || !mManager)
        return std::nullopt;

    auto channels = mManager->channels();
    for (const auto& ch : channels)
    {
        if (ch.name != chName)
            continue;
        if (!peerFilter.empty() && ch.peerName != peerFilter)
            continue;
        return ch.id;
    }
    return std::nullopt;
}

void
LinkAudioReceive::subscribe(const ableton::ChannelId& id)
{
    unsubscribe();

    if (!mManager)
        return;

    mRingL.reset();
    mRingR.reset();
    mStreamFramesReceived.store(0);
    mStreamFramesDropped.store(0);

    ableton::LinkAudio& la = mManager->linkAudio();

    mSource.reset(new ableton::LinkAudioSource(
        la,
        id,
        [this](ableton::LinkAudioSource::BufferHandle bh)
        {
            onSourceBuffer(bh);
        }));
}

void
LinkAudioReceive::unsubscribe()
{
    mSource.reset();
    mRingL.reset();
    mRingR.reset();
}


// ============================================================================
// Source callback (Link thread!)
//
// Stream is interleaved int16 (LRLRLR for stereo, LLL for mono).
// We deinterleave into our two SPSC ring buffers (R untouched if mono).
// ============================================================================

void
LinkAudioReceive::onSourceBuffer(ableton::LinkAudioSource::BufferHandle bh)
{
    const auto& info = bh.info;

    if (info.numFrames == 0 || info.numChannels == 0 || bh.samples == nullptr)
        return;

    mStreamSampleRate.store (info.sampleRate);
    mStreamNumChannels.store(static_cast<uint32_t>(info.numChannels));

    constexpr std::size_t kBatch = 512;
    float scratchL[kBatch];
    float scratchR[kBatch];

    const std::size_t totalFrames = info.numFrames;
    const std::size_t stride      = info.numChannels;
    const bool        isStereo    = (stride >= 2);

    std::size_t framesLeft = totalFrames;
    std::size_t srcOffset  = 0;

    while (framesLeft > 0)
    {
        const std::size_t n = (framesLeft < kBatch) ? framesLeft : kBatch;

        for (std::size_t i = 0; i < n; ++i)
        {
            const std::size_t base = (srcOffset + i) * stride;
            const int16_t L = bh.samples[base];
            scratchL[i] = static_cast<float>(L) / 32768.0f;

            if (isStereo)
            {
                const int16_t R = bh.samples[base + 1];
                scratchR[i] = static_cast<float>(R) / 32768.0f;
            }
        }

        const std::size_t writtenL = mRingL.write(scratchL, n);
        if (writtenL < n)
            mStreamFramesDropped.fetch_add(n - writtenL);

        if (isStereo)
        {
            // We don't separately count R drops; if L dropped, R likely did too.
            mRingR.write(scratchR, n);
        }

        srcOffset  += n;
        framesLeft -= n;
    }

    mStreamFramesReceived.fetch_add(totalFrames);
}


// ============================================================================
// CHOP callbacks
// ============================================================================

void
LinkAudioReceive::getGeneralInfo(CHOP_GeneralInfo* info, const OP_Inputs*, void*)
{
    info->cookEveryFrame = true;
    info->timeslice      = true;
}

bool
LinkAudioReceive::getOutputInfo(CHOP_OutputInfo* info, const OP_Inputs* inputs, void*)
{
    const bool        wantEnabled  = Parameters::evalEnable(inputs);
    const std::string newFromPeer    = Parameters::evalFromPeer(inputs);
    const std::string newFromChannel = Parameters::evalFromChannel(inputs);
    mQuantum = Parameters::evalQuantum(inputs);

    if (mManager)
    {
        if (wantEnabled != mEnabled)
        {
            auto& la = mManager->linkAudio();
            la.enable(wantEnabled);
            la.enableLinkAudio(wantEnabled);
            mEnabled = wantEnabled;

            if (!wantEnabled)
                unsubscribe();
        }
    }

    const bool targetChanged = (newFromChannel != mSubscribedFromChannel) ||
                               (newFromPeer    != mSubscribedFromPeer);

    if (targetChanged)
    {
        unsubscribe();
        mSubscribedFromChannel = newFromChannel;
        mSubscribedFromPeer    = newFromPeer;
    }

    if (mEnabled && !mSource && !mSubscribedFromChannel.empty() && mManager)
    {
        if (auto chId = findChannelId(mSubscribedFromChannel, mSubscribedFromPeer))
        {
            subscribe(*chId);
        }
    }

    // Output declaration: match stream channel count (1 or 2).
    // If not yet receiving, default to mono until we know.
    const uint32_t streamChans = mStreamNumChannels.load();
    info->numChannels = (streamChans >= 2) ? 2 : 1;
    info->sampleRate  = static_cast<float>(mStreamSampleRate.load());
    info->startIndex  = 0;
    return true;
}

void
LinkAudioReceive::getChannelName(int32_t index, OP_String* name,
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
LinkAudioReceive::execute(CHOP_Output* output, const OP_Inputs*, void*)
{
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

    if (output->numChannels < 1 || output->numSamples <= 0)
        return;

    const std::size_t want = static_cast<std::size_t>(output->numSamples);

    auto fillFromRing = [&](AudioRingBuffer& rb, float* dst)
    {
        const std::size_t got = rb.read(dst, want);
        if (got < want)
            std::memset(dst + got, 0, (want - got) * sizeof(float));
    };

    if (mSource)
    {
        // Channel 0 (L)
        fillFromRing(mRingL, output->channels[0]);

        // Channel 1 (R) if present
        if (output->numChannels >= 2)
        {
            // If incoming stream is mono, mirror L into R for compatibility
            const bool streamIsStereo = (mStreamNumChannels.load() >= 2);
            if (streamIsStereo)
            {
                fillFromRing(mRingR, output->channels[1]);
            }
            else
            {
                std::memcpy(output->channels[1], output->channels[0],
                            want * sizeof(float));
            }
        }
    }
    else
    {
        for (int c = 0; c < output->numChannels; ++c)
            std::memset(output->channels[c], 0, want * sizeof(float));
    }
}


// ============================================================================
// Info CHOP / Info DAT
// ============================================================================

int32_t
LinkAudioReceive::getNumInfoCHOPChans(void*)
{
    return 12;
}

void
LinkAudioReceive::getInfoCHOPChan(int32_t index, OP_InfoCHOPChan* chan, void*)
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
        chan->name->setString("subscribed");
        chan->value = mSource ? 1.0 : 0.0;
        break;
    case 5:
        chan->name->setString("stream_sample_rate");
        chan->value = static_cast<double>(mStreamSampleRate.load());
        break;
    case 6:
        chan->name->setString("stream_num_channels");
        chan->value = static_cast<double>(mStreamNumChannels.load());
        break;
    case 7:
        chan->name->setString("ring_buffer_avail");
        chan->value = static_cast<double>(mRingL.available());
        break;
    case 8:
        chan->name->setString("frames_dropped");
        chan->value = static_cast<double>(mStreamFramesDropped.load());
        break;
    case 9:
        chan->name->setString("tempo");
        chan->value = mCachedTempo;
        break;
    case 10:
        chan->name->setString("beat");
        chan->value = mCachedBeat;
        break;
    case 11:
        chan->name->setString("phase");
        chan->value = mCachedPhase;
        break;
    }
}

bool
LinkAudioReceive::getInfoDATSize(OP_InfoDATSize* infoSize, void*)
{
    const int rows = 1 + static_cast<int>(mChannelsSnapshot.size());
    infoSize->rows     = rows;
    infoSize->cols     = 2;
    infoSize->byColumn = false;
    return true;
}

void
LinkAudioReceive::getInfoDATEntries(int32_t index, int32_t,
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
