// ============================================================================
// VoidLinkAudio - VoidLinkAudioSend module (VCV Rack 2 plugin)
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
#include <ableton/LinkAudio.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ============================================================================
// VoidLinkAudioSend
//
// Publishes stereo audio to a Link Audio channel.
//
// Threading model:
//
//   - process() (audio thread, hard real-time):
//       Reads inputs, accumulates into staging buffer, calls publishStaging()
//       once the buffer is full. NO calls into Link's session state, NO
//       channel discovery, NO subscribe/unsubscribe. Only sink->commit() which
//       is documented as lock-free.
//
//   - workerThread (background, message-thread analog):
//       Owns all the "heavy" Link API calls: setPeerName, enable/disable,
//       sink construction/destruction. Wakes up either when stateDirty is
//       set by the UI (right-click menu edits) or every 200ms for periodic
//       reconciliation (e.g. detecting new peers).
//
// This mirrors what the Max and TD versions do via t_clock / cooking
// thread. VCV does not provide such a thread, so we run our own.
// ============================================================================

namespace {

constexpr std::size_t  kCommitFrames       = 256;
constexpr std::size_t  kInitialMaxSamples  = 32768;

inline int16_t floatToInt16Clamped(float v) {
    if (v >= 1.0f)  return 32767;
    if (v <= -1.0f) return -32768;
    return static_cast<int16_t>(v * 32768.0f);
}

} // namespace

struct VoidLinkAudioSend : Module {

    enum ParamId   {
        TEMPO_PARAM,        // BPM knob (20-999, default 120)
        TRANSPORT_PARAM,    // toggle button (0=stopped, 1=playing)
        NUM_PARAMS
    };
    enum InputId   { IN_L_INPUT, IN_R_INPUT, NUM_INPUTS };
    enum OutputId  {
        TEMPO_OUTPUT,       // bpm / 100 V (20 BPM = 0.2V, 999 BPM = 9.99V)
        PHASE_OUTPUT,       // phase * 10 / quantum V (0..10V over the quantum)
        TRANSPORT_OUTPUT,   // 0V stopped, 10V playing (gate)
        NUM_OUTPUTS
    };
    enum LightId   {
        STATUS_GREEN_LIGHT,    // RGB-style: lit green when publishing with peers
        STATUS_RED_LIGHT,      // RGB-style: lit red on drops
        NUM_LIGHTS
    };

    // ── Configuration (read by worker, written by UI) ───────────────────────
    // Protected by stateMutex. UI sets, worker reads.
    std::mutex      stateMutex;
    std::string     channelName_   = "Main";
    std::string     localPeerName_ = "VCVRack";
    bool            enabled_       = true;
    double          quantum_       = 4.0;

    // Triggered by UI to ask the worker to reconcile.
    std::atomic<bool> stateDirty {true};

    // ── Link Audio plumbing — owned by the worker thread ────────────────────
    // sink_ is read by audio thread (atomic load of the pointer-equivalent
    // would be ideal, but unique_ptr is fine since it is only modified by
    // the worker thread, and audio thread reads under the conservative
    // assumption that it might be null).
    std::shared_ptr<LinkAudioManager>       manager;
    std::unique_ptr<ableton::LinkAudioSink> sink;
    std::string                             publishedChannel;
    std::string                             currentPeerName;
    bool                                    workerEnabled = false;

    // ── Staging buffer (audio thread only) ──────────────────────────────────
    std::vector<int16_t> staging;
    std::size_t          stagingFrames = 0;

    // ── Stats (read by widget, written by audio + worker) ───────────────────
    std::atomic<uint64_t> framesPublished  {0};
    std::atomic<uint64_t> framesNoBuffer   {0};
    std::atomic<uint64_t> framesCommitFail {0};

    // For visual drop indicator: latched value that decays.
    std::atomic<uint64_t> lastSeenCommitFail {0};
    float                 dropFlashTimer = 0.0f;  // seconds remaining on the red LED

    // Cached snapshot for the widget (no Link call from main thread).
    std::atomic<std::size_t> peerCount {0};
    std::atomic<bool>        isPublishing {false};

    // ── Bidirectional sync state for tempo/transport params ─────────────────
    // Both fields are owned by the audio thread (process()) and used to
    // detect "user changed the knob/button" vs "session changed externally".
    // mFirstProcess: on the very first cook, we snapshot the live session
    //   into the params silently (so opening a patch doesn't clobber Live's
    //   current tempo with the saved patch knob value).
    bool   mFirstProcess     = true;
    double mLastKnobTempo    = 120.0;    // last tempo we either pushed or mirrored
    bool   mLastKnobTransport = false;

    // ── Worker thread ────────────────────────────────────────────────────────
    std::thread             worker;
    std::atomic<bool>       workerStop {false};
    std::condition_variable workerCv;
    std::mutex              workerCvMutex;

    VoidLinkAudioSend() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        configInput (IN_L_INPUT,  "Audio L");
        configInput (IN_R_INPUT,  "Audio R");

        configOutput(TEMPO_OUTPUT,     "Tempo (bpm/100 V)");
        configOutput(PHASE_OUTPUT,     "Phase (0-10V over quantum)");
        configOutput(TRANSPORT_OUTPUT, "Transport (0V stopped, 10V playing)");

        configParam(TEMPO_PARAM, 20.0f, 999.0f, 120.0f, "Tempo", " BPM");
        configSwitch(TRANSPORT_PARAM, 0.0f, 1.0f, 0.0f, "Transport",
                     {"Stopped", "Playing"});

        staging.resize(kCommitFrames * 2);  // stereo interleaved

        manager = LinkAudioManager::acquire();

        worker = std::thread([this] { workerLoop(); });
    }

    ~VoidLinkAudioSend() override {
        // Stop worker first so it does not touch the sink while we tear down.
        workerStop.store(true);
        workerCv.notify_all();
        if (worker.joinable()) worker.join();

        // Then release Link resources in dependency order.
        sink.reset();
        manager.reset();
    }

    // Called by worker thread only.
    void applyStateLocked() {
        if (!manager) return;
        auto& la = manager->linkAudio();

        // Snapshot UI state under mutex.
        std::string wantChannel, wantPeer;
        bool wantEnabled;
        double wantQuantum;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            wantChannel  = channelName_;
            wantPeer     = localPeerName_;
            wantEnabled  = enabled_;
            wantQuantum  = quantum_;
        }
        (void)wantQuantum;  // captured directly in audio thread via member

        // Peer name.
        if (!wantPeer.empty() && wantPeer != currentPeerName) {
            manager->setPeerName(wantPeer);
            currentPeerName = wantPeer;
        }

        // Enable / disable.
        if (wantEnabled != workerEnabled) {
            la.enable(wantEnabled);
            la.enableLinkAudio(wantEnabled);
            workerEnabled = wantEnabled;
            if (!wantEnabled) {
                sink.reset();
                publishedChannel.clear();
            }
        }

        if (!workerEnabled) {
            isPublishing.store(false);
            peerCount.store(la.numPeers());
            return;
        }

        // Channel name change → recreate sink.
        if (wantChannel != publishedChannel) {
            sink.reset();
            publishedChannel = wantChannel;
        }

        if (!sink && !publishedChannel.empty()) {
            sink.reset(new ableton::LinkAudioSink(
                la, publishedChannel, kInitialMaxSamples));
            framesPublished.store(0);
            framesNoBuffer.store(0);
            framesCommitFail.store(0);
        }

        // Update widget snapshots.
        isPublishing.store(sink != nullptr);
        peerCount.store(la.numPeers());
    }

    void workerLoop() {
        using namespace std::chrono_literals;
        while (!workerStop.load()) {
            applyStateLocked();
            stateDirty.store(false);

            // Wait either for a dirty signal or a periodic timeout (200ms).
            std::unique_lock<std::mutex> lk(workerCvMutex);
            workerCv.wait_for(lk, 200ms, [this] {
                return workerStop.load() || stateDirty.load();
            });
        }
    }

    // Notify the worker after a UI change.
    void notifyDirty() {
        stateDirty.store(true);
        workerCv.notify_all();
    }

    // Publish the accumulated staging buffer. Audio thread only.
    void publishStaging(double sampleRate) {
        if (!sink || stagingFrames == 0) return;

        auto& la = manager->linkAudio();

        ableton::LinkAudioSink::BufferHandle bh(*sink);
        if (!bh) {
            framesNoBuffer.fetch_add(stagingFrames);
            stagingFrames = 0;
            return;
        }

        const std::size_t numFrames    = stagingFrames;
        const std::size_t totalSamples = numFrames * 2;

        if (totalSamples > bh.maxNumSamples) {
            framesCommitFail.fetch_add(numFrames);
            stagingFrames = 0;
            return;
        }

        std::memcpy(bh.samples, staging.data(),
                    sizeof(int16_t) * totalSamples);

        // captureAppSessionState + clock.micros are documented as
        // realtime-safe on the audio thread.
        const auto   state              = la.captureAppSessionState();
        const auto   now                = la.clock().micros();
        const double quantum            = quantum_;
        const double beatsAtBufferBegin = state.beatAtTime(now, quantum);

                const bool ok = bh.commit(state,
                                          beatsAtBufferBegin,
                                          quantum,
                                          numFrames,
                                          /*numChannels=*/2,
                                          sampleRate);

        if (ok) framesPublished.fetch_add(numFrames);
        else    framesCommitFail.fetch_add(numFrames);

        stagingFrames = 0;
    }

    void process(const ProcessArgs& args) override {
        // ── Capture session state once per process() ────────────────────────
        // Used both for the audio publish path (publishStaging) and for the
        // timing outputs / param sync below. captureAppSessionState() is
        // documented as realtime-safe.
        if (!manager) {
            // Idle: just clear outputs and bail.
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
        // First cook: snapshot session into params silently so we don't
        // clobber Live's tempo with the saved patch value.
        if (mFirstProcess) {
            params[TEMPO_PARAM].setValue(static_cast<float>(sessionTempo));
            params[TRANSPORT_PARAM].setValue(sessionPlay ? 1.0f : 0.0f);
            mLastKnobTempo     = sessionTempo;
            mLastKnobTransport = sessionPlay;
            mFirstProcess      = false;
        } else {
            const double knobTempo     = params[TEMPO_PARAM].getValue();
            const bool   knobTransport = params[TRANSPORT_PARAM].getValue() > 0.5f;

            // User moved the knob? Push to session.
            if (std::abs(knobTempo - mLastKnobTempo) > 0.001) {
                manager->setTempo(knobTempo);
                mLastKnobTempo = knobTempo;
            }
            // Else session changed externally (e.g. Live)? Mirror to knob.
            else if (std::abs(sessionTempo - mLastKnobTempo) > 0.001) {
                params[TEMPO_PARAM].setValue(static_cast<float>(sessionTempo));
                mLastKnobTempo = sessionTempo;
            }

            // User clicked transport button?
            if (knobTransport != mLastKnobTransport) {
                manager->setIsPlaying(knobTransport);
                mLastKnobTransport = knobTransport;
            }
            // Else session transport changed externally?
            else if (sessionPlay != mLastKnobTransport) {
                params[TRANSPORT_PARAM].setValue(sessionPlay ? 1.0f : 0.0f);
                mLastKnobTransport = sessionPlay;
            }
        }

        // ── Timing outputs (audio-rate CV) ──────────────────────────────────
        // TEMPO     : bpm / 100 V (range 0.2V-9.99V over Live's 20-999 BPM)
        // PHASE     : phase * 10 / quantum V (0..10V ramp over the quantum)
        // TRANSPORT : 0V stopped, 10V playing (gate)
        outputs[TEMPO_OUTPUT].setVoltage(
            static_cast<float>(sessionTempo / 100.0));
        const double phase = sessionState.phaseAtTime(now, quantum);
        outputs[PHASE_OUTPUT].setVoltage(
            static_cast<float>(phase * 10.0 / quantum));
        outputs[TRANSPORT_OUTPUT].setVoltage(sessionPlay ? 10.0f : 0.0f);

        // ── Audio publish path ──────────────────────────────────────────────
        // Read inputs (mono fallback: if R is not connected, mirror L).
        const float inL = inputs[IN_L_INPUT].getVoltage() * 0.1f;
        const float inR = inputs[IN_R_INPUT].isConnected()
                              ? inputs[IN_R_INPUT].getVoltage() * 0.1f
                              : inL;

        // Stage one stereo frame.
        if (sink && stagingFrames < kCommitFrames) {
            const std::size_t idx = stagingFrames * 2;
            staging[idx + 0] = floatToInt16Clamped(inL);
            staging[idx + 1] = floatToInt16Clamped(inR);
            ++stagingFrames;
        }

        if (stagingFrames >= kCommitFrames) {
            publishStaging(args.sampleRate);
        }

        // ── Visual feedback ──────────────────────────────────────────────────
        // Status LED: green when publishing AND peers connected.
        const bool publishing = isPublishing.load();
        const std::size_t nPeers = peerCount.load();
        const float greenBrightness = (publishing && nPeers > 0) ? 0.9f
                                    : (publishing ? 0.35f : 0.1f);
        lights[STATUS_GREEN_LIGHT].setBrightness(greenBrightness);

        // Drop indicator: flash red for ~250ms when commit fails detected.
        const uint64_t curFails = framesCommitFail.load();
        if (curFails > lastSeenCommitFail.load()) {
            lastSeenCommitFail.store(curFails);
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
        json_object_set_new(root, "channelName",   json_string(channelName_.c_str()));
        json_object_set_new(root, "localPeerName", json_string(localPeerName_.c_str()));
        json_object_set_new(root, "enabled",       json_boolean(enabled_));
        json_object_set_new(root, "quantum",       json_real(quantum_));
        return root;
    }

    void dataFromJson(json_t* root) override {
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (json_t* s = json_object_get(root, "channelName"))   channelName_ = json_string_value(s);
            if (json_t* s = json_object_get(root, "localPeerName")) localPeerName_ = json_string_value(s);
            if (json_t* b = json_object_get(root, "enabled"))       enabled_ = json_boolean_value(b);
            if (json_t* d = json_object_get(root, "quantum"))       quantum_ = json_real_value(d);
        }
        notifyDirty();
    }
};

// ============================================================================
// Widgets
// ============================================================================

// Widget showing the live peer count number, drawn directly in NVG.
struct PeerCountDisplay : Widget {
    VoidLinkAudioSend* module = nullptr;

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
struct SendPanelLabels : Widget {
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
        label(45.0f, 51.0f, 11.0f, cBanner, 3, "SEND");

        // Status label
        label(45.0f, 100.0f, 6.0f, cSub, 1, "PUBLISHING");

        // Tempo / State labels (above knob and switch)
        label(45.0f, 144.0f, 7.0f, cMain, 2, "TEMPO");
        label(45.0f, 194.0f, 7.0f, cMain, 2, "STATE");

        // Timing output labels (3 columns)
        label(18.0f, 248.0f, 7.0f, cSub, 0, "TEMPO");
        label(45.0f, 248.0f, 7.0f, cSub, 0, "PHASE");
        label(72.0f, 248.0f, 7.0f, cSub, 0, "STATE");

        // Audio inputs label
        label(45.0f, 298.0f, 7.0f, cMain, 2, "TO LINK");
        label(27.0f, 307.0f, 5.0f, cSub, 0, "L");
        label(63.0f, 307.0f, 5.0f, cSub, 0, "R");
    }
};

// Editable text menu item.
struct SendTextEditMenuItem : ui::MenuItem {
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

struct VoidLinkAudioSendWidget : ModuleWidget {

    VoidLinkAudioSendWidget(VoidLinkAudioSend* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/VoidLinkAudioSend.svg")));

        // Static panel labels (drawn in NVG since NanoSVG does not render <text>).
        SendPanelLabels* labels = new SendPanelLabels;
        labels->box.pos  = Vec(0, 0);
        labels->box.size = Vec(90, 380);
        addChild(labels);

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // ========================================================================
        // Layout positions match VoidLinkAudioSend.svg exactly.
        // SVG provides the labels and separator lines; C++ provides the widgets.
        // ========================================================================

        // Status LEDs at y=80 (label "PUBLISHING" in SVG at y=100).
        addChild(createLightCentered<MediumLight<GreenLight>>(
            Vec(box.size.x * 0.5f - 6.0f, 80.0f),
            module, VoidLinkAudioSend::STATUS_GREEN_LIGHT));
        addChild(createLightCentered<SmallLight<RedLight>>(
            Vec(box.size.x * 0.5f + 6.0f, 80.0f),
            module, VoidLinkAudioSend::STATUS_RED_LIGHT));

        // Peer count display at y=117.
        PeerCountDisplay* peerDisplay = new PeerCountDisplay;
        peerDisplay->module    = module;
        peerDisplay->box.pos   = Vec(0, 109);
        peerDisplay->box.size  = Vec(box.size.x, 16);
        addChild(peerDisplay);

        // Tempo knob at y=175 (SVG separator at y=138, label "TEMPO" at y=153).
        addParam(createParamCentered<RoundLargeBlackKnob>(
            Vec(box.size.x * 0.5f, 170.0f), module, VoidLinkAudioSend::TEMPO_PARAM));

        // Transport switch at y=225 (SVG label "STATE" at y=210).
        addParam(createParamCentered<BefacoSwitch>(
            Vec(box.size.x * 0.5f, 215.0f), module, VoidLinkAudioSend::TRANSPORT_PARAM));

        // 3 timing jacks at y=280 (SVG separator at y=248, labels at y=263).
        // X positions match SVG label columns: 18, 45, 72.
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(18.0f, 265.0f), module, VoidLinkAudioSend::TEMPO_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(45.0f, 265.0f), module, VoidLinkAudioSend::PHASE_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(72.0f, 265.0f), module, VoidLinkAudioSend::TRANSPORT_OUTPUT));

        // 2 audio jacks at y=348 (SVG separator at y=305, label "TO LINK"
        // at y=320, "L"/"R" at y=333). X positions match SVG: 27, 63.
        addInput(createInputCentered<PJ301MPort>(
            Vec(27.0f, 325.0f), module, VoidLinkAudioSend::IN_L_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            Vec(63.0f, 325.0f), module, VoidLinkAudioSend::IN_R_INPUT));

    }

    void appendContextMenu(Menu* menu) override {
        VoidLinkAudioSend* m = dynamic_cast<VoidLinkAudioSend*>(this->module);
        if (!m) return;

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Link Audio"));

        // Channel name
        {
            std::string current;
            { std::lock_guard<std::mutex> lk(m->stateMutex); current = m->channelName_; }

            SendTextEditMenuItem* it = new SendTextEditMenuItem;
            it->text          = "Channel: " + current;
            it->rightText     = RIGHT_ARROW;
            it->placeholder   = "Main";
            it->getter        = [m]() {
                std::lock_guard<std::mutex> lk(m->stateMutex);
                return m->channelName_;
            };
            it->setter        = [m](std::string v) {
                { std::lock_guard<std::mutex> lk(m->stateMutex); m->channelName_ = v; }
                m->notifyDirty();
            };
            menu->addChild(it);
        }

        // Local peer
        {
            std::string current;
            { std::lock_guard<std::mutex> lk(m->stateMutex); current = m->localPeerName_; }

            SendTextEditMenuItem* it = new SendTextEditMenuItem;
            it->text          = "Local peer: " + current;
            it->rightText     = RIGHT_ARROW;
            it->placeholder   = "VCVRack";
            it->getter        = [m]() {
                std::lock_guard<std::mutex> lk(m->stateMutex);
                return m->localPeerName_;
            };
            it->setter        = [m](std::string v) {
                { std::lock_guard<std::mutex> lk(m->stateMutex); m->localPeerName_ = v; }
                m->notifyDirty();
            };
            menu->addChild(it);
        }

        // Enabled toggle
        menu->addChild(createCheckMenuItem(
            "Enabled", "",
            [m]() {
                std::lock_guard<std::mutex> lk(m->stateMutex);
                return m->enabled_;
            },
            [m]() {
                { std::lock_guard<std::mutex> lk(m->stateMutex); m->enabled_ = !m->enabled_; }
                m->notifyDirty();
            }));

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Status"));
        menu->addChild(createMenuLabel("  Peers on network: " + std::to_string(m->peerCount.load())));
        menu->addChild(createMenuLabel(m->isPublishing.load() ? "  Publishing: yes" : "  Publishing: no"));
        menu->addChild(createMenuLabel("  Frames published: " + std::to_string(m->framesPublished.load())));
        menu->addChild(createMenuLabel("  Frames dropped: "   + std::to_string(m->framesCommitFail.load())));
    }
};

Model* modelVoidLinkAudioSend = createModel<VoidLinkAudioSend, VoidLinkAudioSendWidget>("VoidLinkAudioSend");
