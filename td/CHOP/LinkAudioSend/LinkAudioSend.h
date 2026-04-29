// ============================================================================
// VoidLinkAudio - LinkAudioSend (TouchDesigner CHOP)
//
// Part of the VoidLinkAudio R&D project by Julien Bayle / Structure Void.
// https://julienbayle.net    https://structure-void.com
//
// Released under the MIT License - see LICENSE file at repo root.
// Provided AS IS, without warranty of any kind.
// ============================================================================

#ifndef __LinkAudioSend__
#define __LinkAudioSend__

#include "CHOP_CPlusPlusBase.h"
#include "Parameters.h"
#include "LinkAudioManager.h"

#include <ableton/LinkAudio.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

using namespace TD;

// ============================================================================
// LinkAudioSend
//
// Takes an audio CHOP as input and publishes its channel-0 (mono) over
// Link Audio. Other peers running Link Audio (Live 12.4+, another TD with
// our In CHOP, etc.) can subscribe to it.
//
// Output: pass-through of the input (so the user can chain).
// ============================================================================

class LinkAudioSend : public CHOP_CPlusPlusBase
{
public:
    LinkAudioSend(const OP_NodeInfo* info);
    virtual ~LinkAudioSend();

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
    void ensureSink();
    void destroySink();

    std::shared_ptr<LinkAudioManager>      mManager;
    std::unique_ptr<ableton::LinkAudioSink> mSink;

    // Scratch buffer to convert float32 -> int16 before commit().
    // Resized lazily based on incoming numFrames.
    std::vector<int16_t> mInt16Scratch;

    // Param state
    bool        mEnabled = false;
    std::string mCurrentPeerName;
    std::string mCurrentChannelName;
    double      mQuantum = 4.0;

    // Stats
    std::atomic<uint64_t> mFramesSent      {0};
    std::atomic<uint64_t> mFramesNoBuffer  {0};   // sink not connected -> drop
    std::atomic<uint64_t> mFramesCommitFail{0};   // commit() returned false

    // Cached for Info CHOP / DAT
    std::size_t mCachedNumPeers     = 0;
    std::size_t mCachedNumChannels  = 0;
    double      mCachedTempo        = 0.0;
    double      mCachedBeat         = 0.0;
    double      mCachedPhase        = 0.0;
    bool        mCachedIsPlaying    = false;
    bool        mCachedAudioEnabled = false;
    std::vector<LinkAudioManager::Channel> mChannelsSnapshot;
};

#endif // __LinkAudioSend__
