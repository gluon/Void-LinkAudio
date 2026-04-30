// ============================================================================
// VoidLinkAudio - LinkAudioReceive (TouchDesigner CHOP)
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

#ifndef __LinkAudioReceive__
#define __LinkAudioReceive__

#include "CHOP_CPlusPlusBase.h"
#include "Parameters.h"
#include "LinkAudioManager.h"
#include "AudioRingBuffer.h"

#include <ableton/LinkAudio.hpp>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace TD;

// ============================================================================
// LinkAudioReceive (Stage 2A — naive audio receive, mono OR stereo)
//
// Output channel count matches the incoming stream:
//   - mono stream  -> 1 output channel
//   - stereo stream -> 2 output channels (deinterleaved from int16 LRLR...)
//
// Note: ableton::ChannelId is an alias for ableton::link::NodeId (top-level
// alias in ApiConfig.hpp), distinct from ableton::link_audio::ChannelId
// (internal serialization wrapper). We use ableton::ChannelId.
// ============================================================================

class LinkAudioReceive : public CHOP_CPlusPlusBase
{
public:
    LinkAudioReceive(const OP_NodeInfo* info);
    virtual ~LinkAudioReceive();

    void getGeneralInfo (CHOP_GeneralInfo*, const OP_Inputs*, void*) override;
    bool getOutputInfo  (CHOP_OutputInfo*, const OP_Inputs*, void*) override;
    void getChannelName (int32_t index, OP_String* name,
                         const OP_Inputs*, void*) override;
    void execute        (CHOP_Output*, const OP_Inputs*, void*) override;
    void setupParameters(OP_ParameterManager*, void*) override;

    int32_t getNumInfoCHOPChans(void* reserved) override;
    void    getInfoCHOPChan(int32_t index, OP_InfoCHOPChan* chan,
                            void* reserved) override;

    bool getInfoDATSize   (OP_InfoDATSize* infoSize, void* reserved) override;
    void getInfoDATEntries(int32_t index, int32_t nEntries,
                           OP_InfoDATEntries* entries, void* reserved) override;

private:
    void onSourceBuffer(ableton::LinkAudioSource::BufferHandle bh);

    std::optional<ableton::ChannelId>
        findChannelId(const std::string& chName,
                      const std::string& peerFilter) const;
    void subscribe(const ableton::ChannelId& id);
    void unsubscribe();

    std::shared_ptr<LinkAudioManager> mManager;

    // One ring buffer per channel, max 2 channels.
    AudioRingBuffer  mRingL{16384};
    AudioRingBuffer  mRingR{16384};

    std::unique_ptr<ableton::LinkAudioSource> mSource;

    std::atomic<uint32_t> mStreamSampleRate {48000};
    std::atomic<uint32_t> mStreamNumChannels{1};
    std::atomic<uint64_t> mStreamFramesReceived{0};
    std::atomic<uint64_t> mStreamFramesDropped {0};

    std::string mSubscribedFromChannel;
    std::string mSubscribedFromPeer;

    bool        mEnabled = false;
    double      mQuantum = 4.0;

    // Push-on-change state for the R/W Tempo and Transport params.
    // We only push the user-facing param value to the session when it
    // actually changes. External session changes (Live, other peers) are
    // NOT mirrored back into the params — they show up in the Info CHOP.
    // mFirstParamCheck = true so the very first cook just snapshots the
    // current param values without clobbering session state.
    bool        mFirstParamCheck   = true;
    double      mLastUserTempo     = 120.0;
    int         mLastUserTransport = 0;

    std::size_t mCachedNumPeers      = 0;
    std::size_t mCachedNumChannels   = 0;
    double      mCachedTempo         = 0.0;
    double      mCachedBeat          = 0.0;
    double      mCachedPhase         = 0.0;
    bool        mCachedIsPlaying     = false;
    bool        mCachedAudioEnabled  = false;
    std::vector<LinkAudioManager::Channel> mChannelsSnapshot;
};

#endif // __LinkAudioReceive__
