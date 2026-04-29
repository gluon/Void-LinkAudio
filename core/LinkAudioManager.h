// ============================================================================
// VoidLinkAudio - LinkAudioManager (shared Link Audio manager - interface)
//
// Part of the VoidLinkAudio R&D project by Julien Bayle / Structure Void.
// https://julienbayle.net    https://structure-void.com
//
// Shared core: this file is compiled into every host (TouchDesigner CHOPs,
// Max externals, VCV Rack modules, VST3/AU plugins, openFrameworks addon).
//
// Released under the MIT License - see LICENSE file at repo root.
// Built on top of Ableton Link Audio (see ACKNOWLEDGEMENTS.md).
// Provided AS IS, without warranty of any kind.
// ============================================================================

#pragma once

// ============================================================================
// LinkAudioManager
//
// Owns the single shared ableton::LinkAudio instance for this plugin.
// Acquired via shared_ptr from CHOP instances; the underlying LinkAudio
// is destroyed when the last CHOP that referenced it is destroyed.
//
// Thread-safety:
//   - acquire() is thread-safe.
//   - linkAudio() returns a reference; calls into LinkAudio itself are
//     governed by the per-method thread-safety contract documented in
//     LinkAudio.hpp.
// ============================================================================

#include <ableton/LinkAudio.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

class LinkAudioManager
{
public:
    using Channel = ableton::LinkAudio::Channel;

    /// Acquire (or create) the process-wide instance for this plugin.
    /// Returned shared_ptr keeps it alive; release by letting it go out of scope.
    static std::shared_ptr<LinkAudioManager> acquire();

    ~LinkAudioManager();

    // Direct access to the underlying LinkAudio instance.
    ableton::LinkAudio& linkAudio() { return mLinkAudio; }

    // Convenience wrappers
    std::size_t          numPeers()        const;
    std::vector<Channel> channels();
    void                 setPeerName(const std::string& name);
    std::string          peerName()        const;

    // Non-copyable / non-movable
    LinkAudioManager(const LinkAudioManager&)            = delete;
    LinkAudioManager& operator=(const LinkAudioManager&) = delete;

private:
    LinkAudioManager(double initialBpm, std::string peerName);

    ableton::LinkAudio mLinkAudio;

    // Process-wide weak instance, locked through sInstanceMutex.
    static std::weak_ptr<LinkAudioManager> sInstance;
    static std::mutex                      sInstanceMutex;
};
