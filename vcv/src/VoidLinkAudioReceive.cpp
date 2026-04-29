// ============================================================================
// VoidLinkAudio - VoidLinkAudioReceive module (VCV Rack 2 plugin)
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

#include "plugin.hpp"

#include "LinkAudioManager.h"
#include "AudioRingBuffer.h"
#include <ableton/LinkAudio.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// ============================================================================
// VoidLinkAudioReceive
//
// Same threading model as Send: a worker thread owns all heavy Link API
// calls. process() only reads ring buffers and sets atomic stats. The
// LinkAudioSource's callback runs on a Link-managed thread; it pushes
// converted samples into the SPSC ring buffers, which is lock-free.
// ============================================================================

namespace {

constexpr std::size_t kRingSize = 16384;

} // namespace

struct VoidLinkAudioReceive : Module {

    enum ParamId   { NUM_PARAMS };
    enum InputId   { NUM_INPUTS };
    enum OutputId  { OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };
    enum LightId   {
        STATUS_GREEN_LIGHT,
        STATUS_RED_LIGHT,
        NUM_LIGHTS
    };

    // ── Configuration (UI <-> worker) ────────────────────────────────────────
    std::mutex     stateMutex;
    std::string    fromChannelName_   = "Main";
    std::string    fromPeerName_    = "";
    bool           enabled_       = true;
    double         quantum_       = 4.0;

    std::atomic<bool> stateDirty {true};

    // ── Link Audio plumbing (worker-owned) ──────────────────────────────────
    std::shared_ptr<LinkAudioManager>         manager;
    std::unique_ptr<ableton::LinkAudioSource> source;
    std::string                               subscribedFromChannel;
    std::string                               subscribedFromPeer;
    bool                                      workerEnabled = false;

    // ── Ring buffers (audio thread <-> Link callback thread, SPSC) ──────────
    AudioRingBuffer ringL{kRingSize};
    AudioRingBuffer ringR{kRingSize};

    // ── Stats ────────────────────────────────────────────────────────────────
    std::atomic<uint32_t> streamSampleRate {48000};
    std::atomic<uint32_t> streamNumChannels{1};
    std::atomic<uint64_t> framesReceived   {0};
    std::atomic<uint64_t> framesDropped    {0};

    std::atomic<std::size_t> peerCount    {0};
    std::atomic<bool>        isSubscribed {false};

    // Drop indicator latch.
    std::atomic<uint64_t> lastSeenDropped {0};
    float                 dropFlashTimer = 0.0f;

    // ── Worker thread ────────────────────────────────────────────────────────
    std::thread             worker;
    std::atomic<bool>       workerStop {false};
    std::condition_variable workerCv;
    std::mutex              workerCvMutex;

    VoidLinkAudioReceive() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        configOutput(OUT_L_OUTPUT, "Audio L");
        configOutput(OUT_R_OUTPUT, "Audio R");

        manager = LinkAudioManager::acquire();

        worker = std::thread([this] { workerLoop(); });
    }

    ~VoidLinkAudioReceive() override {
        workerStop.store(true);
        workerCv.notify_all();
        if (worker.joinable()) worker.join();

        // Tear down source before manager — its callback can still fire
        // briefly during destruction.
        source.reset();
        manager.reset();
    }

    // ── Source callback — runs on a Link-managed thread, lock-free ──────────
    void onSourceBuffer(ableton::LinkAudioSource::BufferHandle bh) {
        const auto& info = bh.info;
        if (info.numFrames == 0 || info.numChannels == 0 || bh.samples == nullptr)
            return;

        streamSampleRate.store(info.sampleRate);
        streamNumChannels.store(static_cast<uint32_t>(info.numChannels));

        constexpr std::size_t kBatch = 512;
        float scratchL[kBatch];
        float scratchR[kBatch];

        const std::size_t totalFrames = info.numFrames;
        const std::size_t stride      = info.numChannels;
        const bool        isStereo    = (stride >= 2);

        std::size_t framesLeft = totalFrames;
        std::size_t srcOffset  = 0;

        while (framesLeft > 0) {
            const std::size_t n = (framesLeft < kBatch) ? framesLeft : kBatch;

            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t base = (srcOffset + i) * stride;
                scratchL[i] = static_cast<float>(bh.samples[base]) / 32768.0f;
                if (isStereo)
                    scratchR[i] = static_cast<float>(bh.samples[base + 1]) / 32768.0f;
            }

            const std::size_t writtenL = ringL.write(scratchL, n);
            if (writtenL < n)
                framesDropped.fetch_add(n - writtenL);

            if (isStereo) ringR.write(scratchR, n);
            else          ringR.write(scratchL, n);  // mirror L on R

            srcOffset  += n;
            framesLeft -= n;
        }

        framesReceived.fetch_add(totalFrames);
    }

    // ── Worker thread API ───────────────────────────────────────────────────
    void trySubscribe() {
        auto channels = manager->channels();
        std::optional<ableton::ChannelId> match;
        for (const auto& ch : channels) {
            if (ch.name != subscribedFromChannel) continue;
            if (!subscribedFromPeer.empty() && ch.peerName != subscribedFromPeer) continue;
            match = ch.id;
            break;
        }
        if (!match) return;

        ringL.reset();
        ringR.reset();
        framesReceived.store(0);
        framesDropped.store(0);

        auto& la = manager->linkAudio();
        source.reset(new ableton::LinkAudioSource(
            la, *match,
            [this](ableton::LinkAudioSource::BufferHandle bh) {
                onSourceBuffer(bh);
            }));
    }

    void unsubscribeInternal() {
        source.reset();
        ringL.reset();
        ringR.reset();
    }

    void applyStateLocked() {
        if (!manager) return;
        auto& la = manager->linkAudio();

        std::string wantFromChannel, wantFromPeer;
        bool wantEnabled;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            wantFromChannel = fromChannelName_;
            wantFromPeer    = fromPeerName_;
            wantEnabled     = enabled_;
        }

        if (wantEnabled != workerEnabled) {
            la.enable(wantEnabled);
            la.enableLinkAudio(wantEnabled);
            workerEnabled = wantEnabled;
            if (!wantEnabled) {
                unsubscribeInternal();
                subscribedFromChannel.clear();
                subscribedFromPeer.clear();
            }
        }

        if (!workerEnabled) {
            isSubscribed.store(false);
            peerCount.store(la.numPeers());
            return;
        }

        const bool channelChanged = (wantFromChannel != subscribedFromChannel);
        const bool fromPeerChanged = (wantFromPeer != subscribedFromPeer);
        if (channelChanged || fromPeerChanged) {
            unsubscribeInternal();
            subscribedFromChannel = wantFromChannel;
            subscribedFromPeer    = wantFromPeer;
        }

        if (!source && !subscribedFromChannel.empty()) {
            trySubscribe();
        }

        isSubscribed.store(source != nullptr);
        peerCount.store(la.numPeers());
    }

    void workerLoop() {
        using namespace std::chrono_literals;
        while (!workerStop.load()) {
            applyStateLocked();
            stateDirty.store(false);

            std::unique_lock<std::mutex> lk(workerCvMutex);
            workerCv.wait_for(lk, 200ms, [this] {
                return workerStop.load() || stateDirty.load();
            });
        }
    }

    void notifyDirty() {
        stateDirty.store(true);
        workerCv.notify_all();
    }

    void process(const ProcessArgs& args) override {
        // Drain one sample per channel per process() call.
        float sL = 0.0f, sR = 0.0f;
        if (isSubscribed.load()) {
            ringL.read(&sL, 1);
            ringR.read(&sR, 1);
        }

        outputs[OUT_L_OUTPUT].setVoltage(sL * 10.0f);
        outputs[OUT_R_OUTPUT].setVoltage(sR * 10.0f);

        // ── Visual feedback ──────────────────────────────────────────────────
        const bool subscribed = isSubscribed.load();
        const std::size_t nPeers = peerCount.load();
        const float greenBrightness = (subscribed && nPeers > 0) ? 0.9f
                                    : (subscribed ? 0.35f : 0.1f);
        lights[STATUS_GREEN_LIGHT].setBrightness(greenBrightness);

        const uint64_t curDropped = framesDropped.load();
        if (curDropped > lastSeenDropped.load()) {
            lastSeenDropped.store(curDropped);
            dropFlashTimer = 0.25f;
        }
        if (dropFlashTimer > 0.0f) {
            dropFlashTimer -= args.sampleTime;
            lights[STATUS_RED_LIGHT].setBrightness(0.9f);
        } else {
            lights[STATUS_RED_LIGHT].setBrightness(0.0f);
        }
    }

    // ── State save/restore ──────────────────────────────────────────────────
    json_t* dataToJson() override {
        std::lock_guard<std::mutex> lock(stateMutex);
        json_t* root = json_object();
        json_object_set_new(root, "fromChannel", json_string(fromChannelName_.c_str()));
        json_object_set_new(root, "fromPeer",    json_string(fromPeerName_.c_str()));
        json_object_set_new(root, "enabled",       json_boolean(enabled_));
        json_object_set_new(root, "quantum",       json_real(quantum_));
        return root;
    }

    void dataFromJson(json_t* root) override {
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (json_t* s = json_object_get(root, "fromChannel")) fromChannelName_ = json_string_value(s);
            if (json_t* s = json_object_get(root, "fromPeer"))    fromPeerName_    = json_string_value(s);
            if (json_t* b = json_object_get(root, "enabled"))       enabled_ = json_boolean_value(b);
            if (json_t* d = json_object_get(root, "quantum"))       quantum_ = json_real_value(d);
        }
        notifyDirty();
    }
};

// ============================================================================
// Widgets
// ============================================================================

struct ReceivePeerCountDisplay : Widget {
    VoidLinkAudioReceive* module = nullptr;

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        if (!module) return;

        std::shared_ptr<window::Font> font =
            APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        if (!font || !font->handle) return;

        const std::size_t n = module->peerCount.load();
        const std::string txt = std::to_string(n) + (n == 1 ? " peer" : " peers");

        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize  (args.vg, 10.0f);
        nvgTextAlign (args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor (args.vg, nvgRGB(0xcc, 0xcc, 0xcc));
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, txt.c_str(), nullptr);
    }
};

struct ReceiveTextEditMenuItem : ui::MenuItem {
    std::string                      placeholder;
    std::function<std::string()>     getter;
    std::function<void(std::string)> setter;

    struct TF : ui::TextField {
        std::function<void(std::string)> setter;
        void onSelectKey(const SelectKeyEvent& e) override {
            if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
                if (setter) setter(text);
                ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
                if (overlay) overlay->requestDelete();
                e.consume(this);
                return;
            }
            ui::TextField::onSelectKey(e);
        }
    };

    ui::Menu* createChildMenu() override {
        ui::Menu* m = new ui::Menu;
        TF* tf = new TF;
        tf->box.size.x = 180.0f;
        tf->placeholder = placeholder;
        tf->setText(getter());
        tf->setter = setter;
        m->addChild(tf);
        APP->event->setSelectedWidget(tf);
        return m;
    }
};

struct VoidLinkAudioReceiveWidget : ModuleWidget {

    VoidLinkAudioReceiveWidget(VoidLinkAudioReceive* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/VoidLinkAudioReceive.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addChild(createLightCentered<MediumLight<GreenLight>>(
            Vec(box.size.x * 0.5f - 6.0f, 90.0f),
            module, VoidLinkAudioReceive::STATUS_GREEN_LIGHT));
        addChild(createLightCentered<SmallLight<RedLight>>(
            Vec(box.size.x * 0.5f + 6.0f, 90.0f),
            module, VoidLinkAudioReceive::STATUS_RED_LIGHT));

        ReceivePeerCountDisplay* peerDisplay = new ReceivePeerCountDisplay;
        peerDisplay->module    = module;
        peerDisplay->box.pos   = Vec(0, 140);
        peerDisplay->box.size  = Vec(box.size.x, 16);
        addChild(peerDisplay);

        const float xL = box.size.x * 0.30f;
        const float xR = box.size.x * 0.70f;

        addOutput(createOutputCentered<PJ301MPort>(
            Vec(xL, 280.0f), module, VoidLinkAudioReceive::OUT_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(xR, 280.0f), module, VoidLinkAudioReceive::OUT_R_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        VoidLinkAudioReceive* m = dynamic_cast<VoidLinkAudioReceive*>(this->module);
        if (!m) return;

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Link Audio"));

        // Channel
        {
            std::string current;
            { std::lock_guard<std::mutex> lk(m->stateMutex); current = m->fromChannelName_; }

            ReceiveTextEditMenuItem* it = new ReceiveTextEditMenuItem;
            it->text          = "From Channel: " + current;
            it->rightText     = RIGHT_ARROW;
            it->placeholder   = "Main";
            it->getter        = [m]() {
                std::lock_guard<std::mutex> lk(m->stateMutex); return m->fromChannelName_;
            };
            it->setter        = [m](std::string v) {
                { std::lock_guard<std::mutex> lk(m->stateMutex); m->fromChannelName_ = v; }
                m->notifyDirty();
            };
            menu->addChild(it);
        }

        // From Peer
        {
            std::string current;
            { std::lock_guard<std::mutex> lk(m->stateMutex); current = m->fromPeerName_; }

            ReceiveTextEditMenuItem* it = new ReceiveTextEditMenuItem;
            it->text          = "From Peer: " + (current.empty() ? std::string("(any, ambiguous)") : current);
            it->rightText     = RIGHT_ARROW;
            it->placeholder   = "exact source peer name";
            it->getter        = [m]() {
                std::lock_guard<std::mutex> lk(m->stateMutex); return m->fromPeerName_;
            };
            it->setter        = [m](std::string v) {
                { std::lock_guard<std::mutex> lk(m->stateMutex); m->fromPeerName_ = v; }
                m->notifyDirty();
            };
            menu->addChild(it);
        }

        menu->addChild(createCheckMenuItem(
            "Enabled", "",
            [m]() {
                std::lock_guard<std::mutex> lk(m->stateMutex); return m->enabled_;
            },
            [m]() {
                { std::lock_guard<std::mutex> lk(m->stateMutex); m->enabled_ = !m->enabled_; }
                m->notifyDirty();
            }));

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Status"));
        menu->addChild(createMenuLabel("  Peers on network: " + std::to_string(m->peerCount.load())));
        menu->addChild(createMenuLabel(m->isSubscribed.load() ? "  Subscribed: yes" : "  Subscribed: no"));
        if (m->isSubscribed.load()) {
            menu->addChild(createMenuLabel("  Stream: "
                + std::to_string(m->streamSampleRate.load()) + " Hz / "
                + std::to_string(m->streamNumChannels.load()) + " ch"));
            menu->addChild(createMenuLabel("  Frames received: " + std::to_string(m->framesReceived.load())));
            menu->addChild(createMenuLabel("  Frames dropped: "  + std::to_string(m->framesDropped.load())));
        }
    }
};

Model* modelVoidLinkAudioReceive = createModel<VoidLinkAudioReceive, VoidLinkAudioReceiveWidget>("VoidLinkAudioReceive");
