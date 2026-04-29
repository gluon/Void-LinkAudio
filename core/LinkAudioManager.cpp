// ============================================================================
// VoidLinkAudio - LinkAudioManager (shared Link Audio manager - implementation)
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

#include "LinkAudioManager.h"

std::weak_ptr<LinkAudioManager> LinkAudioManager::sInstance;
std::mutex                      LinkAudioManager::sInstanceMutex;

LinkAudioManager::LinkAudioManager(double initialBpm, std::string peerName)
    : mLinkAudio(initialBpm, std::move(peerName))
{
    // Enable both Link tempo sync and Link Audio channel sharing.
    mLinkAudio.enable(true);
    mLinkAudio.enableLinkAudio(true);
}

LinkAudioManager::~LinkAudioManager()
{
    // Tear down cleanly. enableLinkAudio(false) stops audio announcements;
    // enable(false) leaves the Link session.
    mLinkAudio.enableLinkAudio(false);
    mLinkAudio.enable(false);
}

std::shared_ptr<LinkAudioManager>
LinkAudioManager::acquire()
{
    std::lock_guard<std::mutex> lock(sInstanceMutex);
    auto sp = sInstance.lock();
    if (!sp)
    {
        // Use new because constructor is private; std::make_shared
        // doesn't have access.
        sp = std::shared_ptr<LinkAudioManager>(
            new LinkAudioManager(120.0, "TouchDesigner"));
        sInstance = sp;
    }
    return sp;
}

std::size_t
LinkAudioManager::numPeers() const
{
    return mLinkAudio.numPeers();
}

std::vector<LinkAudioManager::Channel>
LinkAudioManager::channels()
{
    return mLinkAudio.channels();
}

void
LinkAudioManager::setPeerName(const std::string& name)
{
    mLinkAudio.setPeerName(name);
}

std::string
LinkAudioManager::peerName() const
{
    return mLinkAudio.peerName();
}
