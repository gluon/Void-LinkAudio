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

    enum ParamId   {
        TEMPO_PARAM,        // BPM knob (20-999, default 120)
        TRANSPORT_PARAM,    // toggle button (0=stopped, 1=playing)
        NUM_PARAMS
    };
    enum InputId   { NUM_INPUTS };
    enum OutputId  {
        OUT_L_OUTPUT,
        OUT_R_OUTPUT,
        TEMPO_OUTPUT,       // bpm / 100 V (20 BPM = 0.2V, 999 BPM = 9.99V)
        PHASE_OUTPUT,       // phase * 10 / quantum V (0..10V over the quantum)
        TRANSPORT_OUTPUT,   // 0V stopped, 10V playing (gate)
        NUM_OUTPUTS
    };
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

    // ── Bidirectional sync state for tempo/transport params ─────────────────
    bool   mFirstProcess     = true;
    double mLastKnobTempo    = 120.0;
    bool   mLastKnobTransport = false;

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

        configOutput(OUT_L_OUTPUT,     "Audio L");
        configOutput(OUT_R_OUTPUT,     "Audio R");

        configOutput(TEMPO_OUTPUT,     "Tempo (bpm/100 V)");
        configOutput(PHASE_OUTPUT,     "Phase (0-10V over quantum)");
        configOutput(TRANSPORT_OUTPUT, "Transport (0V stopped, 10V playing)");

        configParam(TEMPO_PARAM, 20.0f, 999.0f, 120.0f, "Tempo", " BPM");
        configSwitch(TRANSPORT_PARAM, 0.0f, 1.0f, 0.0f, "Transport",
                     {"Stopped", "Playing"});

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
        // ── Capture session state once per process() ────────────────────────
        if (!manager) {
            outputs[OUT_L_OUTPUT].setVoltage(0.0f);
            outputs[OUT_R_OUTPUT].setVoltage(0.0f);
            outputs[TEMPO_OUTPUT].setVoltage(0.0f);
            outputs[PHASE_OUTPUT].setVoltage(0.0f);
            outputs[TRANSPORT_OUTPUT].setVoltage(0.0f);
            return;
        }

        auto&        la           = manager->linkAudio();
        auto         sessionState = la.captureAppSessionState();
        const auto   now          = la.clock().micros();
        const double quantum      = quantum_;
        const double sessionTempo = sessionState.tempo();
        const bool   sessionPlay  = sessionState.isPlaying();

        // ── Bidirectional tempo/transport sync ──────────────────────────────
        if (mFirstProcess) {
            params[TEMPO_PARAM].setValue(static_cast<float>(sessionTempo));
            params[TRANSPORT_PARAM].setValue(sessionPlay ? 1.0f : 0.0f);
            mLastKnobTempo     = sessionTempo;
            mLastKnobTransport = sessionPlay;
            mFirstProcess      = false;
        } else {
            const double knobTempo     = params[TEMPO_PARAM].getValue();
            const bool   knobTransport = params[TRANSPORT_PARAM].getValue() > 0.5f;

            if (std::abs(knobTempo - mLastKnobTempo) > 0.001) {
                manager->setTempo(knobTempo);
                mLastKnobTempo = knobTempo;
            }
            else if (std::abs(sessionTempo - mLastKnobTempo) > 0.001) {
                params[TEMPO_PARAM].setValue(static_cast<float>(sessionTempo));
                mLastKnobTempo = sessionTempo;
            }

            if (knobTransport != mLastKnobTransport) {
                manager->setIsPlaying(knobTransport);
                mLastKnobTransport = knobTransport;
            }
            else if (sessionPlay != mLastKnobTransport) {
                params[TRANSPORT_PARAM].setValue(sessionPlay ? 1.0f : 0.0f);
                mLastKnobTransport = sessionPlay;
            }
        }

        // ── Timing outputs (audio-rate CV) ──────────────────────────────────
        outputs[TEMPO_OUTPUT].setVoltage(
            static_cast<float>(sessionTempo / 100.0));
        const double phase = sessionState.phaseAtTime(now, quantum);
        outputs[PHASE_OUTPUT].setVoltage(
            static_cast<float>(phase * 10.0 / quantum));
        outputs[TRANSPORT_OUTPUT].setVoltage(sessionPlay ? 10.0f : 0.0f);

        // ── Audio output: drain one sample per channel ──────────────────────
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

// Static panel labels, drawn in NVG (NanoSVG used by VCV does not render <text>).
struct ReceivePanelLabels : Widget {
    void draw(const DrawArgs& args) override {
        std::shared_ptr<window::Font> font =
            APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        if (!font || !font->handle) return;

        nvgFontFaceId(args.vg, font->handle);

        auto label = [&](float x, float y, float size, NVGcolor color, int letterSpacing,
                         const char* text) {
            nvgFontSize  (args.vg, size);
            nvgTextLetterSpacing(args.vg, (float)letterSpacing);
            nvgTextAlign (args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor (args.vg, color);
            nvgText(args.vg, x, y, text, nullptr);
        };

        const NVGcolor cTitle  = nvgRGB(0xcc, 0xcc, 0xcc);
        const NVGcolor cBanner = nvgRGB(0x0a, 0x0a, 0x0a);
        const NVGcolor cMain   = nvgRGB(0xaa, 0xaa, 0xaa);
        const NVGcolor cSub    = nvgRGB(0x88, 0x88, 0x88);

        // Title
        label(45.0f, 32.0f, 11.0f, cTitle, 2, "VOID LINK");

        // Banner module name
        label(45.0f, 51.0f, 11.0f, cBanner, 3, "RECEIVE");

        // Status label
        label(45.0f, 100.0f, 6.0f, cSub, 1, "SUBSCRIBED");

        // Tempo / State labels (above knob and switch)
        label(45.0f, 144.0f, 7.0f, cMain, 2, "TEMPO");
        label(45.0f, 194.0f, 7.0f, cMain, 2, "STATE");

        // Timing output labels (3 columns)
        label(18.0f, 248.0f, 7.0f, cSub, 0, "TEMPO");
        label(45.0f, 248.0f, 7.0f, cSub, 0, "PHASE");
        label(72.0f, 248.0f, 7.0f, cSub, 0, "STATE");

        // Audio outputs label
        label(45.0f, 298.0f, 7.0f, cMain, 2, "FROM LINK");
        label(27.0f, 307.0f, 5.0f, cSub, 0, "L");
        label(63.0f, 307.0f, 5.0f, cSub, 0, "R");
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

        // Static panel labels (drawn in NVG since NanoSVG does not render <text>).
        ReceivePanelLabels* labels = new ReceivePanelLabels;
        labels->box.pos  = Vec(0, 0);
        labels->box.size = Vec(90, 380);
        addChild(labels);

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // ========================================================================
        // Layout positions match VoidLinkAudioReceive.svg exactly.
        // SVG provides the labels and separator lines; C++ provides the widgets.
        // ========================================================================

        // Status LEDs at y=80 (label "SUBSCRIBED" in SVG at y=100).
        addChild(createLightCentered<MediumLight<GreenLight>>(
            Vec(box.size.x * 0.5f - 6.0f, 80.0f),
            module, VoidLinkAudioReceive::STATUS_GREEN_LIGHT));
        addChild(createLightCentered<SmallLight<RedLight>>(
            Vec(box.size.x * 0.5f + 6.0f, 80.0f),
            module, VoidLinkAudioReceive::STATUS_RED_LIGHT));

        // Peer count display at y=117.
        ReceivePeerCountDisplay* peerDisplay = new ReceivePeerCountDisplay;
        peerDisplay->module    = module;
        peerDisplay->box.pos   = Vec(0, 109);
        peerDisplay->box.size  = Vec(box.size.x, 16);
        addChild(peerDisplay);

        // Tempo knob at y=175 (SVG separator at y=138, label "TEMPO" at y=153).
        addParam(createParamCentered<RoundLargeBlackKnob>(
            Vec(box.size.x * 0.5f, 170.0f), module, VoidLinkAudioReceive::TEMPO_PARAM));

        // Transport switch at y=225 (SVG label "STATE" at y=210).
        addParam(createParamCentered<BefacoSwitch>(
            Vec(box.size.x * 0.5f, 215.0f), module, VoidLinkAudioReceive::TRANSPORT_PARAM));

        // 3 timing jacks at y=280 (SVG separator at y=248, labels at y=263).
        // X positions match SVG label columns: 18, 45, 72.
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(18.0f, 265.0f), module, VoidLinkAudioReceive::TEMPO_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(45.0f, 265.0f), module, VoidLinkAudioReceive::PHASE_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(72.0f, 265.0f), module, VoidLinkAudioReceive::TRANSPORT_OUTPUT));

        // 2 audio jacks at y=348 (SVG separator at y=305, label "FROM LINK"
        // at y=320, "L"/"R" at y=333). X positions match SVG: 27, 63.
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(27.0f, 325.0f), module, VoidLinkAudioReceive::OUT_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(63.0f, 325.0f), module, VoidLinkAudioReceive::OUT_R_OUTPUT));
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
