// ============================================================================
// VoidLinkAudio - void.linkaudio.send~ (Pure Data external)
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
    void.linkaudio.send~  (Phase 3b)

    Publishes audio to an Ableton Link Audio channel on the LAN, plus exposes
    Link timing context as audio-rate signals.

    Inlets:
        0 : (signal) audio L  + messages
        1 : (signal) audio R

    Outlets:
        0 : (signal) tempo~     - session BPM
        1 : (signal) phase~     - sawtooth in [0, quantum)
        2 : (signal) transport~ - 0.0 or 1.0
        3 : info messages       - status key/value pairs + channel listing

    Messages:
        enable <0|1>     - master enable (default 1). Disabling drops the sink.
        channel <sym>    - name of the Link Audio channel to publish on
                           (default "Pd Out"). Empty clears the publish target.
        peer <sym>       - peer name advertised on the network (default "Pd").
                           Affects the WHOLE process — every other VoidLinkAudio
                           external in this Pd will share this peer name, since
                           the underlying Link session is process-wide.
        quantum <float>  - quantum used for beat / phase mapping (default 4,
                           range 1..16).
        tempo <float>    - SET session tempo (BPM). Propagates to all peers.
        transport <0|1>  - SET session transport. Propagates to all peers.
                           Requires startStopSync (enabled in manager ctor).
        bang             - retry: re-applies state.
        info             - dump current status on the info outlet.

    Stereo only (Phase 3b):
        Phase 3b ships stereo only (2 signal inlets). Mono support comes via
        a creation argument in a later phase. To send a mono signal for now,
        wire the same source to both inlets, or use [throw~] / [send~] to
        duplicate.

    Sample rate matters:
        The published wire format includes the local Pd sample rate. If Pd
        runs at 44.1 kHz and the receiving host expects 48 kHz (typical for
        Live), the receiver may drop frames or resample. For lossless
        interop with Live, set Pd to 48 kHz (Media > Audio settings).

    Threading:
        - perform() runs on the Pd audio thread. It acquires a BufferHandle
          from the LinkAudioSink, fills it with int16 samples, captures the
          AppSessionState, and commits — all lock-free per Ableton's API.
        - bh.commit() is mandatory. Without it the publisher's frame counter
          rises locally but no peer ever receives anything (the most
          time-consuming bug of the whole project).
        - All other code (messages, polling tick) runs on the Pd main
          thread; safe to call into the Link app-thread API from there.
*/

#include "LinkAudioPlatform.h"   // must come before any <ableton/...> on Windows
#include "m_pd.h"

#include "LinkAudioManager.h"

#include <ableton/LinkAudio.hpp>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>


// ============================================================================
// State (heap-allocated, owned by the Pd object)
// ============================================================================

struct VoidLinkAudioSendImpl
{
    std::shared_ptr<LinkAudioManager>        manager;
    std::unique_ptr<ableton::LinkAudioSink>  sink;

    // RT stats (touched only from the audio thread; reads are best-effort
    // from the message thread for emit_info())
    std::atomic<uint64_t> framesPublished {0};
    std::atomic<uint64_t> framesNoBuffer  {0};
    std::atomic<uint64_t> framesCommitFail{0};

    // Cached "wanted" state — drives apply_state diffing
    std::string  publishedChannel;
    std::string  currentPeerName;

    // User intent (set by messages)
    std::string  channel        = "Pd Out";
    std::string  peer           = "Pd";
    bool         enabled        = true;
    int          pollIntervalMs = 250;
    double       quantum        = 4.0;

    // Set by _dsp; used by perform
    double       dspSampleRate  = 48000.0;

    // Auto-emit info tracking
    bool         lastPublishing       = false;
    std::size_t  lastNumPeers         = 0;
    std::size_t  lastNumAudioChannels = 0;
    bool         firstStateApplied    = false;
};

typedef struct _voidlinkaudio_send_tilde
{
    t_object  x_obj;
    t_float   x_f;            // dummy: holds float-as-signal value if a
                              // float message arrives at inlet 0 (signal inlet)
    t_inlet  *x_signal_in_R;  // handle for the second signal inlet, freed in _free
    t_outlet *x_info_outlet;
    t_clock  *x_poll_clock;

    VoidLinkAudioSendImpl *impl;
} t_voidlinkaudio_send_tilde;

static t_class *voidlinkaudio_send_tilde_class;

// Sink internal buffer size. 32768 stereo samples = ~340 ms at 48 kHz —
// matches the Max external. Plenty of headroom for any reasonable Live
// latency.
static constexpr std::size_t kInitialMaxSamples = 32768;


// ============================================================================
// Forward declarations
// ============================================================================

static void apply_state            (t_voidlinkaudio_send_tilde *x);
static void destroy_sink_internal  (t_voidlinkaudio_send_tilde *x);
static void emit_info              (t_voidlinkaudio_send_tilde *x);
static void poll_tick              (t_voidlinkaudio_send_tilde *x);


// ============================================================================
// Sink lifecycle
// ============================================================================

static void
destroy_sink_internal(t_voidlinkaudio_send_tilde *x)
{
    if (!x->impl) return;
    x->impl->sink.reset();
}


// ============================================================================
// State application (idempotent)
// ============================================================================

static void
apply_state(t_voidlinkaudio_send_tilde *x)
{
    if (!x->impl || !x->impl->manager) return;

    auto& la = x->impl->manager->linkAudio();

    // Peer name advertised on the network. Process-wide: setting this on
    // any VoidLinkAudio external affects every other external sharing the
    // same Link Audio manager.
    if (!x->impl->peer.empty() && x->impl->peer != x->impl->currentPeerName)
    {
        x->impl->manager->setPeerName(x->impl->peer);
        x->impl->currentPeerName = x->impl->peer;
    }

    // If user disabled this instance, drop the sink.
    if (!x->impl->enabled)
        destroy_sink_internal(x);

    // Channel name change → recreate sink
    if (x->impl->channel != x->impl->publishedChannel)
    {
        destroy_sink_internal(x);
        x->impl->publishedChannel = x->impl->channel;
    }

    // Create sink if needed
    if (x->impl->enabled && !x->impl->sink && !x->impl->publishedChannel.empty())
    {
        x->impl->sink.reset(new ableton::LinkAudioSink(
            la,
            x->impl->publishedChannel,
            kInitialMaxSamples));
        x->impl->framesPublished.store(0);
        x->impl->framesNoBuffer.store(0);
        x->impl->framesCommitFail.store(0);
    }

    // Auto-emit info on meaningful state change
    const bool        nowPublishing       = (x->impl->sink != nullptr);
    const std::size_t nowNumPeers         = la.numPeers();
    const std::size_t nowNumAudioChannels = la.channels().size();

    const bool stateChanged =
        !x->impl->firstStateApplied
        || nowPublishing       != x->impl->lastPublishing
        || nowNumPeers         != x->impl->lastNumPeers
        || nowNumAudioChannels != x->impl->lastNumAudioChannels;

    if (stateChanged)
    {
        x->impl->lastPublishing       = nowPublishing;
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
emit_info(t_voidlinkaudio_send_tilde *x)
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

    SETFLOAT(&a, x->impl->sink ? 1 : 0);
    outlet_anything(out, gensym("publishing"), 1, &a);

    SETSYMBOL(&a, gensym(x->impl->channel.c_str()));
    outlet_anything(out, gensym("channel"), 1, &a);

    SETSYMBOL(&a, gensym(x->impl->peer.c_str()));
    outlet_anything(out, gensym("peer"), 1, &a);

    SETFLOAT(&a, static_cast<t_float>(x->impl->framesPublished.load()));
    outlet_anything(out, gensym("frames_published"), 1, &a);

    SETFLOAT(&a, static_cast<t_float>(x->impl->framesNoBuffer.load()));
    outlet_anything(out, gensym("frames_no_buffer"), 1, &a);

    SETFLOAT(&a, static_cast<t_float>(x->impl->framesCommitFail.load()));
    outlet_anything(out, gensym("frames_commit_fail"), 1, &a);

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
poll_tick(t_voidlinkaudio_send_tilde *x)
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
voidlinkaudio_send_tilde_enable(t_voidlinkaudio_send_tilde *x, t_floatarg f)
{
    if (!x->impl) return;
    x->impl->enabled = (f != 0.0f);
    apply_state(x);
}

static void
voidlinkaudio_send_tilde_channel(t_voidlinkaudio_send_tilde *x, t_symbol *s)
{
    if (!x->impl) return;
    x->impl->channel = (s && s->s_name) ? s->s_name : "";
    apply_state(x);
}

static void
voidlinkaudio_send_tilde_peer(t_voidlinkaudio_send_tilde *x, t_symbol *s)
{
    if (!x->impl) return;
    x->impl->peer = (s && s->s_name) ? s->s_name : "Pd";
    apply_state(x);
}

static void
voidlinkaudio_send_tilde_quantum(t_voidlinkaudio_send_tilde *x, t_floatarg f)
{
    if (!x->impl) return;
    double q = static_cast<double>(f);
    if (q < 1.0)  q = 1.0;
    if (q > 16.0) q = 16.0;
    x->impl->quantum = q;
}

static void
voidlinkaudio_send_tilde_tempo(t_voidlinkaudio_send_tilde *x, t_floatarg f)
{
    if (!x->impl || !x->impl->manager) return;
    double bpm = static_cast<double>(f);
    if (bpm < 20.0)  bpm = 20.0;
    if (bpm > 999.0) bpm = 999.0;
    x->impl->manager->setTempo(bpm);
}

static void
voidlinkaudio_send_tilde_transport(t_voidlinkaudio_send_tilde *x, t_floatarg f)
{
    if (!x->impl || !x->impl->manager) return;
    x->impl->manager->setIsPlaying(f != 0.0f);
}

static void
voidlinkaudio_send_tilde_bang(t_voidlinkaudio_send_tilde *x)
{
    apply_state(x);
}

static void
voidlinkaudio_send_tilde_info(t_voidlinkaudio_send_tilde *x)
{
    emit_info(x);
}


// ============================================================================
// DSP
// ============================================================================

static inline int16_t
float_to_int16_clamped(t_sample v)
{
    if (v >  1.0f) v =  1.0f;
    if (v < -1.0f) v = -1.0f;
    return static_cast<int16_t>(v * 32767.0f);
}

static t_int *
voidlinkaudio_send_tilde_perform(t_int *w)
{
    t_voidlinkaudio_send_tilde *x = (t_voidlinkaudio_send_tilde *)(w[1]);
    const t_sample *inL          = (const t_sample *)(w[2]);
    const t_sample *inR          = (const t_sample *)(w[3]);
    t_sample       *outTempo     = (t_sample *)(w[4]);
    t_sample       *outPhase     = (t_sample *)(w[5]);
    t_sample       *outTransport = (t_sample *)(w[6]);
    int             n            = static_cast<int>(w[7]);

    // No manager yet -> silence everything and bail.
    if (!x->impl || !x->impl->manager || x->impl->dspSampleRate <= 0.0)
    {
        for (int i = 0; i < n; ++i)
        {
            outTempo[i]     = 0;
            outPhase[i]     = 0;
            outTransport[i] = 0;
        }
        return (w + 8);
    }

    auto& la = x->impl->manager->linkAudio();

    // ========================================================================
    // STEP 1: Audio publish path.
    //
    // Read inL/inR FIRST, before writing to outs[]. Pd may in-place process
    // (alias outs[i] onto ins[j] memory) under specific topologies. By
    // committing the audio publish before touching outs[], we guarantee the
    // network sees the actual audio inputs, not whatever happened to be in
    // outs[] on the previous block.
    // ========================================================================

    if (x->impl->enabled && x->impl->sink)
    {
        constexpr std::size_t numCh        = 2;        // stereo only Phase 3b
        const std::size_t     totalSamples = static_cast<std::size_t>(n) * numCh;

        ableton::LinkAudioSink::BufferHandle bh(*x->impl->sink);

        if (!bh)
        {
            x->impl->framesNoBuffer.fetch_add(n);
        }
        else if (totalSamples > bh.maxNumSamples)
        {
            x->impl->framesCommitFail.fetch_add(n);
        }
        else
        {
            // Interleave L/R into int16
            for (int i = 0; i < n; ++i)
            {
                bh.samples[2 * i + 0] = float_to_int16_clamped(inL[i]);
                bh.samples[2 * i + 1] = float_to_int16_clamped(inR[i]);
            }

            // Commit with full Link timing context. Mandatory — without it
            // local counters rise but no peer ever receives anything.
            const auto   appState           = la.captureAppSessionState();
            const auto   nowApp             = la.clock().micros();
            const double beatsAtBufferBegin = appState.beatAtTime(nowApp, x->impl->quantum);

            const bool ok = bh.commit(appState,
                                      beatsAtBufferBegin,
                                      x->impl->quantum,
                                      static_cast<std::size_t>(n),
                                      numCh,
                                      x->impl->dspSampleRate);

            if (ok) x->impl->framesPublished.fetch_add(n);
            else    x->impl->framesCommitFail.fetch_add(n);
        }
    }

    // ========================================================================
    // STEP 2: Fill timing outlets.
    //
    // Safe to do now; we've already finished reading ins[] above.
    // ========================================================================

    auto state = la.captureAudioSessionState();

    const double tempoBpm    = state.tempo();
    const double quantum     = (x->impl->quantum > 0.0) ? x->impl->quantum : 4.0;
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
voidlinkaudio_send_tilde_dsp(t_voidlinkaudio_send_tilde *x, t_signal **sp)
{
    if (x->impl) x->impl->dspSampleRate = (double)sp[0]->s_sr;

    // sp[0..1] = inL, inR ; sp[2..4] = outTempo, outPhase, outTransport
    dsp_add(voidlinkaudio_send_tilde_perform, 7,
            x,
            sp[0]->s_vec, sp[1]->s_vec,
            sp[2]->s_vec, sp[3]->s_vec, sp[4]->s_vec,
            (t_int)sp[0]->s_n);
}


// ============================================================================
// Instance lifecycle
// ============================================================================

static void *
voidlinkaudio_send_tilde_new(t_symbol *s, int argc, t_atom *argv)
{
    (void)s;

    t_voidlinkaudio_send_tilde *x =
        (t_voidlinkaudio_send_tilde *)pd_new(voidlinkaudio_send_tilde_class);
    if (!x) return nullptr;

    // Inlets: the first signal inlet is automatic (created by pd_new because
    // the class was registered with CLASS_MAINSIGNALIN). Add the second one.
    x->x_signal_in_R = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);

    // Outlets: 3 signal + 1 message
    outlet_new(&x->x_obj, &s_signal);   // 0: tempo~
    outlet_new(&x->x_obj, &s_signal);   // 1: phase~
    outlet_new(&x->x_obj, &s_signal);   // 2: transport~
    x->x_info_outlet = outlet_new(&x->x_obj, &s_anything);

    x->impl = new (std::nothrow) VoidLinkAudioSendImpl();
    if (!x->impl)
    {
        pd_error(x, "void.linkaudio.send~: out of memory");
        return nullptr;
    }

    x->impl->manager = LinkAudioManager::acquire();

    // Optional creation args:
    //   [void.linkaudio.send~ <channel> [peer] [quantum]]
    if (argc >= 1 && argv[0].a_type == A_SYMBOL)
        x->impl->channel = atom_getsymbol(&argv[0])->s_name;
    if (argc >= 2 && argv[1].a_type == A_SYMBOL)
        x->impl->peer = atom_getsymbol(&argv[1])->s_name;
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
voidlinkaudio_send_tilde_free(t_voidlinkaudio_send_tilde *x)
{
    if (x->x_poll_clock)
    {
        clock_unset(x->x_poll_clock);
        clock_free(x->x_poll_clock);
        x->x_poll_clock = nullptr;
    }

    if (x->x_signal_in_R)
    {
        inlet_free(x->x_signal_in_R);
        x->x_signal_in_R = nullptr;
    }

    if (x->impl)
    {
        destroy_sink_internal(x);
        x->impl->manager.reset();
        delete x->impl;
        x->impl = nullptr;
    }
}


// ============================================================================
// Class registration
//
// 'void.linkaudio.send~' -> setup_void0x2elinkaudio0x2esend_tilde
// ============================================================================

extern "C" void setup_void0x2elinkaudio0x2esend_tilde(void)
{
    voidlinkaudio_send_tilde_class = class_new(
        gensym("void.linkaudio.send~"),
        (t_newmethod)voidlinkaudio_send_tilde_new,
        (t_method)voidlinkaudio_send_tilde_free,
        sizeof(t_voidlinkaudio_send_tilde),
        CLASS_DEFAULT,
        A_GIMME, 0);

    // CLASS_MAINSIGNALIN: tells Pd that the first inlet is a signal inlet
    // and that x_f is the dummy float to use when a float message arrives
    // there. Standard pattern for tilde objects with signal inlets.
    CLASS_MAINSIGNALIN(voidlinkaudio_send_tilde_class,
                       t_voidlinkaudio_send_tilde, x_f);

    class_addmethod(voidlinkaudio_send_tilde_class,
                    (t_method)voidlinkaudio_send_tilde_dsp,
                    gensym("dsp"), A_CANT, 0);

    class_addmethod(voidlinkaudio_send_tilde_class,
                    (t_method)voidlinkaudio_send_tilde_enable,
                    gensym("enable"), A_DEFFLOAT, 0);

    class_addmethod(voidlinkaudio_send_tilde_class,
                    (t_method)voidlinkaudio_send_tilde_channel,
                    gensym("channel"), A_DEFSYMBOL, 0);

    class_addmethod(voidlinkaudio_send_tilde_class,
                    (t_method)voidlinkaudio_send_tilde_peer,
                    gensym("peer"), A_DEFSYMBOL, 0);

    class_addmethod(voidlinkaudio_send_tilde_class,
                    (t_method)voidlinkaudio_send_tilde_quantum,
                    gensym("quantum"), A_DEFFLOAT, 0);

    class_addmethod(voidlinkaudio_send_tilde_class,
                    (t_method)voidlinkaudio_send_tilde_tempo,
                    gensym("tempo"), A_DEFFLOAT, 0);

    class_addmethod(voidlinkaudio_send_tilde_class,
                    (t_method)voidlinkaudio_send_tilde_transport,
                    gensym("transport"), A_DEFFLOAT, 0);

    class_addbang(voidlinkaudio_send_tilde_class,
                  voidlinkaudio_send_tilde_bang);

    class_addmethod(voidlinkaudio_send_tilde_class,
                    (t_method)voidlinkaudio_send_tilde_info,
                    gensym("info"), A_NULL);
}