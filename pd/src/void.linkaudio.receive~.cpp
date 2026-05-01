// ============================================================================
// VoidLinkAudio - void.linkaudio.receive~ (Pure Data external)
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
// General Public License for more details, full text in LICENSE at
// the repo root, or at <https://www.gnu.org/licenses/gpl-2.0.html>.
//
// Built on top of Ableton Link Audio (GPL v2+, see ACKNOWLEDGEMENTS.md).
// ============================================================================

/*
    void.linkaudio.receive~  (Phase 3a: audio + timing signals)

    Subscribes to an Ableton Link Audio channel published on the LAN by Live,
    a TouchDesigner CHOP, a Max external, a VCV Rack module, an oF addon,
    or another Pd instance. Outputs received audio plus session timing
    (tempo / phase / transport) as audio-rate signals.

    Inlets:
        0 : (cold) messages

    Outlets:
        0 : (signal) audio L
        1 : (signal) audio R       - mirror of L when stream is mono
        2 : (signal) tempo~        - session BPM (slowly varying)
        3 : (signal) phase~        - sawtooth in [0, quantum)
        4 : (signal) transport~    - 0.0 or 1.0
        5 : info messages          - status key/value pairs

    Messages:
        enable <0|1>     - master enable (default 1). Disabling unsubscribes.
        channel <sym>    - name of the Link Audio channel to subscribe to.
                           Empty / no arg clears the target.
        frompeer <sym>   - optional peer-name filter when several peers
                           publish the same channel name. Empty = any.
        quantum <float>  - quantum used for phase mapping (default 4, range 1..16)
        tempo <float>    - SET session tempo (BPM). Propagates to all peers.
        transport <0|1>  - SET session transport. Propagates to all peers.
                           Requires startStopSync (enabled by manager ctor).
        bang             - retry: re-applies state. If a matching channel
                           appeared since last try, this will subscribe.
        info             - dump current status on the info outlet.

    Auto-status:
        The info outlet automatically emits a fresh dump whenever a meaningful
        state change is detected (subscribe state, peer count, or the list of
        available channels grows / shrinks). Editing settings does not spam
        the outlet.

    Timing signals:
        Captured once per perform() buffer via captureAudioSessionState()
        (audio-thread safe, lock-free), then advanced linearly across the
        buffer using beatsPerSample = tempo / 60 / sr. This is the same
        approach used by the Max / TD / VCV / VST hosts.

    Important note on sample rate:
        Pd vanilla does not do internal sample-rate conversion on Link Audio
        streams. If the published stream is at 48 kHz and Pd is running at
        44.1 kHz (or vice versa), the ring buffers will overflow / underflow
        continuously. Set Pd's audio sample rate to match the publishing
        host (typically 48 kHz for Live).

    Threading:
        - The Source callback (on_source_buffer) runs on a Link-managed
          thread. It writes float samples into SPSC ring buffers and is
          lock-free.
        - perform() runs on the Pd audio thread, drains the rings, and
          captures Link session state for the timing signals.
        - All other code (messages, polling tick) runs on the Pd main
          thread; safe to call into the Link app-thread API from there.
*/

#include "LinkAudioPlatform.h"   // must come before any <ableton/...> on Windows
#include "m_pd.h"

#include "LinkAudioManager.h"
#include "AudioRingBuffer.h"

#include <ableton/LinkAudio.hpp>

#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <string>


// ============================================================================
// State (heap-allocated, owned by the Pd object)
// ============================================================================

struct VoidLinkAudioReceiveImpl
{
    std::shared_ptr<LinkAudioManager>          manager;
    std::unique_ptr<ableton::LinkAudioSource>  source;

    AudioRingBuffer ringL{16384};
    AudioRingBuffer ringR{16384};

    std::atomic<uint32_t> streamSampleRate {48000};
    std::atomic<uint32_t> streamNumChannels{1};
    std::atomic<uint64_t> framesReceived   {0};
    std::atomic<uint64_t> framesDropped    {0};

    // Currently subscribed target (cached so apply_state can detect changes)
    std::string subscribedFromChannel;
    std::string subscribedFromPeer;

    // User intent (set by messages)
    std::string fromChannel;       // wanted channel name (empty = no target)
    std::string fromPeer;          // optional peer-name filter (empty = any)
    bool        enabled         = true;
    int         pollIntervalMs  = 250;

    // Timing
    double      quantum         = 4.0;   // beats; range 1..16
    double      dspSampleRate   = 0.0;   // set by _dsp; used by perform

    // Tracking for auto-emit on meaningful state changes
    bool        lastSubscribed       = false;
    std::size_t lastNumPeers         = 0;
    std::size_t lastNumAudioChannels = 0;
    bool        firstStateApplied    = false;
};

typedef struct _voidlinkaudio_receive_tilde
{
    t_object  x_obj;
    t_outlet *x_info_outlet;
    t_clock  *x_poll_clock;

    VoidLinkAudioReceiveImpl *impl;
} t_voidlinkaudio_receive_tilde;

static t_class *voidlinkaudio_receive_tilde_class;


// ============================================================================
// Forward declarations
// ============================================================================

static void apply_state          (t_voidlinkaudio_receive_tilde *x);
static void try_subscribe        (t_voidlinkaudio_receive_tilde *x);
static void unsubscribe_internal (t_voidlinkaudio_receive_tilde *x);
static void emit_info            (t_voidlinkaudio_receive_tilde *x);
static void on_source_buffer     (t_voidlinkaudio_receive_tilde *x,
                                  ableton::LinkAudioSource::BufferHandle bh);
static void poll_tick            (t_voidlinkaudio_receive_tilde *x);


// ============================================================================
// Source callback - Link-managed thread, must be quick & lock-free.
// ============================================================================

static void
on_source_buffer(t_voidlinkaudio_receive_tilde *x,
                 ableton::LinkAudioSource::BufferHandle bh)
{
    if (!x || !x->impl) return;

    const auto& info = bh.info;
    if (info.numFrames == 0 || info.numChannels == 0 || bh.samples == nullptr)
        return;

    x->impl->streamSampleRate.store (info.sampleRate);
    x->impl->streamNumChannels.store(static_cast<uint32_t>(info.numChannels));

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
            scratchL[i] = static_cast<float>(bh.samples[base]) / 32768.0f;
            if (isStereo)
                scratchR[i] = static_cast<float>(bh.samples[base + 1]) / 32768.0f;
        }

        const std::size_t writtenL = x->impl->ringL.write(scratchL, n);
        if (writtenL < n)
            x->impl->framesDropped.fetch_add(n - writtenL);

        if (isStereo)
            x->impl->ringR.write(scratchR, n);

        srcOffset  += n;
        framesLeft -= n;
    }

    x->impl->framesReceived.fetch_add(totalFrames);
}


// ============================================================================
// Subscribe / unsubscribe
// ============================================================================

static void
try_subscribe(t_voidlinkaudio_receive_tilde *x)
{
    if (!x->impl || !x->impl->manager) return;

    auto channels = x->impl->manager->channels();
    std::optional<ableton::ChannelId> match;
    for (const auto& ch : channels)
    {
        if (ch.name != x->impl->subscribedFromChannel) continue;
        if (!x->impl->subscribedFromPeer.empty() && ch.peerName != x->impl->subscribedFromPeer)
            continue;
        match = ch.id;
        break;
    }

    if (!match)
        return;

    x->impl->ringL.reset();
    x->impl->ringR.reset();
    x->impl->framesReceived.store(0);
    x->impl->framesDropped.store(0);

    auto& la = x->impl->manager->linkAudio();
    x->impl->source.reset(new ableton::LinkAudioSource(
        la,
        *match,
        [x](ableton::LinkAudioSource::BufferHandle bh)
        {
            on_source_buffer(x, bh);
        }));
}

static void
unsubscribe_internal(t_voidlinkaudio_receive_tilde *x)
{
    if (!x->impl) return;
    x->impl->source.reset();
    x->impl->ringL.reset();
    x->impl->ringR.reset();
}


// ============================================================================
// State application (idempotent)
// ============================================================================

static void
apply_state(t_voidlinkaudio_receive_tilde *x)
{
    if (!x->impl || !x->impl->manager) return;

    auto& la = x->impl->manager->linkAudio();

    if (!x->impl->enabled)
        unsubscribe_internal(x);

    if (x->impl->fromChannel != x->impl->subscribedFromChannel
        || x->impl->fromPeer != x->impl->subscribedFromPeer)
    {
        unsubscribe_internal(x);
        x->impl->subscribedFromChannel = x->impl->fromChannel;
        x->impl->subscribedFromPeer    = x->impl->fromPeer;
    }

    if (x->impl->enabled
        && !x->impl->source
        && !x->impl->subscribedFromChannel.empty())
    {
        try_subscribe(x);
    }

    const bool        nowSubscribed       = (x->impl->source != nullptr);
    const std::size_t nowNumPeers         = la.numPeers();
    const std::size_t nowNumAudioChannels = la.channels().size();

    const bool stateChanged =
        !x->impl->firstStateApplied
        || nowSubscribed       != x->impl->lastSubscribed
        || nowNumPeers         != x->impl->lastNumPeers
        || nowNumAudioChannels != x->impl->lastNumAudioChannels;

    if (stateChanged)
    {
        x->impl->lastSubscribed       = nowSubscribed;
        x->impl->lastNumPeers         = nowNumPeers;
        x->impl->lastNumAudioChannels = nowNumAudioChannels;
        x->impl->firstStateApplied    = true;
        emit_info(x);
    }
}


// ============================================================================
// Info emission
// ============================================================================

static void
emit_info(t_voidlinkaudio_receive_tilde *x)
{
    if (!x->impl || !x->impl->manager || !x->x_info_outlet) return;

    auto& la = x->impl->manager->linkAudio();
    const auto state    = la.captureAppSessionState();
    const auto now      = la.clock().micros();
    const auto channels = la.channels();

    t_outlet *out = x->x_info_outlet;
    t_atom   a;

    SETFLOAT(&a, x->impl->enabled ? 1 : 0);
    outlet_anything(out, gensym("enabled"), 1, &a);

    SETFLOAT(&a, la.isLinkAudioEnabled() ? 1 : 0);
    outlet_anything(out, gensym("audio_enabled"), 1, &a);

    SETFLOAT(&a, static_cast<t_float>(la.numPeers()));
    outlet_anything(out, gensym("num_peers"), 1, &a);

    SETFLOAT(&a, x->impl->source ? 1 : 0);
    outlet_anything(out, gensym("subscribed"), 1, &a);

    SETSYMBOL(&a, gensym(x->impl->fromChannel.c_str()));
    outlet_anything(out, gensym("fromchannel"), 1, &a);

    SETSYMBOL(&a, gensym(x->impl->fromPeer.c_str()));
    outlet_anything(out, gensym("frompeer"), 1, &a);

    SETFLOAT(&a, static_cast<t_float>(x->impl->streamSampleRate.load()));
    outlet_anything(out, gensym("stream_sample_rate"), 1, &a);

    SETFLOAT(&a, static_cast<t_float>(x->impl->streamNumChannels.load()));
    outlet_anything(out, gensym("stream_num_channels"), 1, &a);

    SETFLOAT(&a, static_cast<t_float>(x->impl->framesReceived.load()));
    outlet_anything(out, gensym("frames_received"), 1, &a);

    SETFLOAT(&a, static_cast<t_float>(x->impl->framesDropped.load()));
    outlet_anything(out, gensym("frames_dropped"), 1, &a);

    SETFLOAT(&a, static_cast<t_float>(state.tempo()));
    outlet_anything(out, gensym("tempo"), 1, &a);

    SETFLOAT(&a, static_cast<t_float>(state.beatAtTime(now, x->impl->quantum)));
    outlet_anything(out, gensym("beat"), 1, &a);

    SETFLOAT(&a, static_cast<t_float>(state.phaseAtTime(now, x->impl->quantum)));
    outlet_anything(out, gensym("phase"), 1, &a);

    SETFLOAT(&a, static_cast<t_float>(x->impl->quantum));
    outlet_anything(out, gensym("quantum"), 1, &a);

    SETFLOAT(&a, state.isPlaying() ? 1 : 0);
    outlet_anything(out, gensym("transport"), 1, &a);

    SETFLOAT(&a, static_cast<t_float>(channels.size()));
    outlet_anything(out, gensym("num_audio_channels"), 1, &a);

    for (const auto& ch : channels)
    {
        t_atom av[2];
        SETSYMBOL(&av[0], gensym(ch.name.c_str()));
        SETSYMBOL(&av[1], gensym(ch.peerName.c_str()));
        outlet_anything(out, gensym("channel"), 2, av);
    }
}


// ============================================================================
// Polling clock
// ============================================================================

static void
poll_tick(t_voidlinkaudio_receive_tilde *x)
{
    if (!x || !x->impl) return;
    apply_state(x);
    if (x->x_poll_clock)
        clock_delay(x->x_poll_clock, static_cast<double>(x->impl->pollIntervalMs));
}


// ============================================================================
// Messages
// ============================================================================

static void
voidlinkaudio_receive_tilde_enable(t_voidlinkaudio_receive_tilde *x, t_floatarg f)
{
    if (!x->impl) return;
    x->impl->enabled = (f != 0.0f);
    apply_state(x);
}

static void
voidlinkaudio_receive_tilde_channel(t_voidlinkaudio_receive_tilde *x, t_symbol *s)
{
    if (!x->impl) return;
    x->impl->fromChannel = (s && s->s_name) ? s->s_name : "";
    apply_state(x);
}

static void
voidlinkaudio_receive_tilde_frompeer(t_voidlinkaudio_receive_tilde *x, t_symbol *s)
{
    if (!x->impl) return;
    x->impl->fromPeer = (s && s->s_name) ? s->s_name : "";
    apply_state(x);
}

static void
voidlinkaudio_receive_tilde_quantum(t_voidlinkaudio_receive_tilde *x, t_floatarg f)
{
    if (!x->impl) return;
    double q = static_cast<double>(f);
    if (q < 1.0)  q = 1.0;
    if (q > 16.0) q = 16.0;
    x->impl->quantum = q;
}

// SET session tempo. Propagates to all Link peers (Live, etc.).
// app-thread; capture/commit pattern is inside manager->setTempo().
static void
voidlinkaudio_receive_tilde_tempo(t_voidlinkaudio_receive_tilde *x, t_floatarg f)
{
    if (!x->impl || !x->impl->manager) return;
    double bpm = static_cast<double>(f);
    if (bpm < 20.0)  bpm = 20.0;
    if (bpm > 999.0) bpm = 999.0;
    x->impl->manager->setTempo(bpm);
}

// SET session transport. Requires startStopSync (enabled in manager ctor).
static void
voidlinkaudio_receive_tilde_transport(t_voidlinkaudio_receive_tilde *x, t_floatarg f)
{
    if (!x->impl || !x->impl->manager) return;
    x->impl->manager->setIsPlaying(f != 0.0f);
}

static void
voidlinkaudio_receive_tilde_bang(t_voidlinkaudio_receive_tilde *x)
{
    apply_state(x);
}

static void
voidlinkaudio_receive_tilde_info(t_voidlinkaudio_receive_tilde *x)
{
    emit_info(x);
}


// ============================================================================
// DSP
// ============================================================================

static t_int *
voidlinkaudio_receive_tilde_perform(t_int *w)
{
    t_voidlinkaudio_receive_tilde *x = (t_voidlinkaudio_receive_tilde *)(w[1]);
    t_sample *outL         = (t_sample *)(w[2]);
    t_sample *outR         = (t_sample *)(w[3]);
    t_sample *outTempo     = (t_sample *)(w[4]);
    t_sample *outPhase     = (t_sample *)(w[5]);
    t_sample *outTransport = (t_sample *)(w[6]);
    int       n            = static_cast<int>(w[7]);

    // ---- Audio out ---------------------------------------------------------
    const bool audioActive = x->impl && x->impl->enabled && x->impl->source;

    if (!audioActive)
    {
        for (int i = 0; i < n; ++i) { outL[i] = 0; outR[i] = 0; }
    }
    else
    {
        // Drain ringL
        constexpr std::size_t kBatch = 256;
        float tmp[kBatch];
        {
            int done = 0;
            while (done < n)
            {
                const std::size_t want = static_cast<std::size_t>(
                    (n - done < (int)kBatch) ? (n - done) : (int)kBatch);
                const std::size_t got = x->impl->ringL.read(tmp, want);
                for (std::size_t i = 0;   i < got;  ++i) outL[done + i] = static_cast<t_sample>(tmp[i]);
                for (std::size_t i = got; i < want; ++i) outL[done + i] = 0;
                done += static_cast<int>(want);
            }
        }
        // Drain ringR or mirror outL
        const bool streamIsStereo = (x->impl->streamNumChannels.load() >= 2);
        if (streamIsStereo)
        {
            int done = 0;
            while (done < n)
            {
                const std::size_t want = static_cast<std::size_t>(
                    (n - done < (int)kBatch) ? (n - done) : (int)kBatch);
                const std::size_t got = x->impl->ringR.read(tmp, want);
                for (std::size_t i = 0;   i < got;  ++i) outR[done + i] = static_cast<t_sample>(tmp[i]);
                for (std::size_t i = got; i < want; ++i) outR[done + i] = 0;
                done += static_cast<int>(want);
            }
        }
        else
        {
            std::memcpy(outR, outL, sizeof(t_sample) * n);
        }
    }

    // ---- Timing signals ----------------------------------------------------
    //
    // These follow the local Link session and are independent of the
    // subscribe state (they keep working even with no audio source).
    // We capture once per buffer (audio-thread safe, lock-free) and
    // advance linearly using beatsPerSample.

    if (!x->impl || !x->impl->manager || x->impl->dspSampleRate <= 0.0)
    {
        for (int i = 0; i < n; ++i) { outTempo[i] = 0; outPhase[i] = 0; outTransport[i] = 0; }
        return (w + 8);
    }

    auto& la = x->impl->manager->linkAudio();
    auto state = la.captureAudioSessionState();

    const double tempoBpm    = state.tempo();
    const double quantum     = x->impl->quantum;
    const double playingVal  = state.isPlaying() ? 1.0 : 0.0;

    const auto   microsBegin = la.clock().micros();
    const double beatBegin   = state.beatAtTime(microsBegin, quantum);

    const double sr             = x->impl->dspSampleRate;
    const double beatsPerSample = tempoBpm / 60.0 / sr;

    for (int i = 0; i < n; ++i)
    {
        const double beat  = beatBegin + beatsPerSample * static_cast<double>(i);
        double       phase = std::fmod(beat, quantum);
        if (phase < 0.0) phase += quantum;

        outTempo[i]     = static_cast<t_sample>(tempoBpm);
        outPhase[i]     = static_cast<t_sample>(phase);
        outTransport[i] = static_cast<t_sample>(playingVal);
    }

    return (w + 8);
}

static void
voidlinkaudio_receive_tilde_dsp(t_voidlinkaudio_receive_tilde *x, t_signal **sp)
{
    if (x->impl) x->impl->dspSampleRate = (double)sp[0]->s_sr;

    // sp[0..4] = outL, outR, outTempo, outPhase, outTransport
    dsp_add(voidlinkaudio_receive_tilde_perform, 7,
            x,
            sp[0]->s_vec, sp[1]->s_vec,
            sp[2]->s_vec, sp[3]->s_vec, sp[4]->s_vec,
            (t_int)sp[0]->s_n);
}


// ============================================================================
// Instance lifecycle
// ============================================================================

static void *
voidlinkaudio_receive_tilde_new(t_symbol *s, int argc, t_atom *argv)
{
    (void)s;

    t_voidlinkaudio_receive_tilde *x =
        (t_voidlinkaudio_receive_tilde *)pd_new(voidlinkaudio_receive_tilde_class);
    if (!x) return nullptr;

    // Outlets layout: 5 signal + 1 message
    outlet_new(&x->x_obj, &s_signal);   // 0: audio L
    outlet_new(&x->x_obj, &s_signal);   // 1: audio R
    outlet_new(&x->x_obj, &s_signal);   // 2: tempo~
    outlet_new(&x->x_obj, &s_signal);   // 3: phase~
    outlet_new(&x->x_obj, &s_signal);   // 4: transport~
    x->x_info_outlet = outlet_new(&x->x_obj, &s_anything);

    x->impl = new (std::nothrow) VoidLinkAudioReceiveImpl();
    if (!x->impl)
    {
        pd_error(x, "void.linkaudio.receive~: out of memory");
        return nullptr;
    }

    x->impl->manager = LinkAudioManager::acquire();
    if (x->impl->manager)
        x->impl->manager->setPeerName("Pd");

    // Optional creation args: [void.linkaudio.receive~ <channel> [peer] [quantum]]
    if (argc >= 1 && argv[0].a_type == A_SYMBOL)
        x->impl->fromChannel = atom_getsymbol(&argv[0])->s_name;
    if (argc >= 2 && argv[1].a_type == A_SYMBOL)
        x->impl->fromPeer = atom_getsymbol(&argv[1])->s_name;
    if (argc >= 3 && argv[2].a_type == A_FLOAT)
    {
        double q = static_cast<double>(atom_getfloat(&argv[2]));
        if (q < 1.0)  q = 1.0;
        if (q > 16.0) q = 16.0;
        x->impl->quantum = q;
    }

    x->x_poll_clock = clock_new(x, (t_method)poll_tick);

    apply_state(x);

    if (x->x_poll_clock)
        clock_delay(x->x_poll_clock, 50.0);

    return x;
}

static void
voidlinkaudio_receive_tilde_free(t_voidlinkaudio_receive_tilde *x)
{
    if (x->x_poll_clock)
    {
        clock_unset(x->x_poll_clock);
        clock_free(x->x_poll_clock);
        x->x_poll_clock = nullptr;
    }

    if (x->impl)
    {
        unsubscribe_internal(x);
        x->impl->manager.reset();
        delete x->impl;
        x->impl = nullptr;
    }
}


// ============================================================================
// Class registration
//
// 'void.linkaudio.receive~' -> setup_void0x2elinkaudio0x2ereceive_tilde
// ============================================================================

extern "C" void setup_void0x2elinkaudio0x2ereceive_tilde(void)
{
    voidlinkaudio_receive_tilde_class = class_new(
        gensym("void.linkaudio.receive~"),
        (t_newmethod)voidlinkaudio_receive_tilde_new,
        (t_method)voidlinkaudio_receive_tilde_free,
        sizeof(t_voidlinkaudio_receive_tilde),
        CLASS_DEFAULT,
        A_GIMME, 0);

    class_addmethod(voidlinkaudio_receive_tilde_class,
                    (t_method)voidlinkaudio_receive_tilde_dsp,
                    gensym("dsp"), A_CANT, 0);

    class_addmethod(voidlinkaudio_receive_tilde_class,
                    (t_method)voidlinkaudio_receive_tilde_enable,
                    gensym("enable"), A_DEFFLOAT, 0);

    class_addmethod(voidlinkaudio_receive_tilde_class,
                    (t_method)voidlinkaudio_receive_tilde_channel,
                    gensym("channel"), A_DEFSYMBOL, 0);

    class_addmethod(voidlinkaudio_receive_tilde_class,
                    (t_method)voidlinkaudio_receive_tilde_frompeer,
                    gensym("frompeer"), A_DEFSYMBOL, 0);

    class_addmethod(voidlinkaudio_receive_tilde_class,
                    (t_method)voidlinkaudio_receive_tilde_quantum,
                    gensym("quantum"), A_DEFFLOAT, 0);

    class_addmethod(voidlinkaudio_receive_tilde_class,
                    (t_method)voidlinkaudio_receive_tilde_tempo,
                    gensym("tempo"), A_DEFFLOAT, 0);

    class_addmethod(voidlinkaudio_receive_tilde_class,
                    (t_method)voidlinkaudio_receive_tilde_transport,
                    gensym("transport"), A_DEFFLOAT, 0);

    class_addbang(voidlinkaudio_receive_tilde_class,
                  voidlinkaudio_receive_tilde_bang);

    class_addmethod(voidlinkaudio_receive_tilde_class,
                    (t_method)voidlinkaudio_receive_tilde_info,
                    gensym("info"), A_NULL);
}