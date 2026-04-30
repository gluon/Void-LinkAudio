// ============================================================================
// VoidLinkAudio - void.linkaudio.receive~ (Max/MSP external)
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
    void.linkaudio.receive~

    Receive audio from an Ableton Link Audio channel published on the LAN,
    plus expose Link timing context as audio-rate signals.

    Inlets:
        none (signal)
        1 message inlet (default)
    Outlets:
        2 signal outlets : audioL, audioR
        3 signal outlets : tempo~, phase~, transport~
        1 dumpout (rightmost) for status messages

    Attributes:
        @enable        0/1   - enable Link Audio (default 1)
        @peer          sym   - local peer name advertised (default "Max")
        @channel       sym   - name of the Link Audio channel to subscribe to
        @filter        sym   - optional peer name filter when matching channels
        @quantum       float - quantum used for beat / phase mapping (default 4)
        @poll          0/1   - auto-retry subscribe + auto-status (default 1)
        @pollinterval  long  - polling interval in ms (default 250, range 50..60000)
        @tempo         float - session tempo BPM (R/W; set propagates to session)
        @transport     0/1   - session transport (R/W; set propagates to session)

    Messages:
        bang             - silent retry; re-applies state (auto-subscribes
                           if a matching channel is now available). State
                           changes will trigger an automatic dict dump.
        info             - explicit dict dump on the dumpout (route via
                           [dict.view] / [dict.unpack] / [route ...])
        tempo <float>    - shorthand for [tempo $1( via attribute setter
        transport <0|1>  - shorthand for [transport $1( via attribute setter
        poll <0|1>       - handled automatically via the attribute setter
        pollinterval <n> - handled automatically via the attribute setter

    Auto-dump:
        The dumpout emits a fresh status dict whenever a meaningful state
        change is detected (subscribed flips, peer count changes, or the
        list of available channels grows/shrinks). Editing attributes in
        the inspector does NOT spam the dumpout.

    Timing signals:
        tempo~     - session BPM, sample-accurate, slowly varying
        phase~     - sawtooth in [0, quantum), wraps modulo quantum
        transport~ - 0.0 or 1.0, follows session.isPlaying()

        These are derived from captureAudioSessionState() at the start of
        each perform buffer, then advanced linearly across the buffer using
        beatsPerSample = tempo / 60 / sampleRate. Quantum is read from the
        @quantum attribute (constant per session, not a signal).

    Notes:
        - Output channel count for audio is fixed at 2 (audioL / audioR).
          Mono streams are mirrored on R. This matches the TD CHOP "Q1b" choice.
        - Sample format on the wire is int16 interleaved; we convert to
          float64 in the perform routine.
        - The Source callback runs on a Link-managed thread, so we push
          into SPSC ring buffers and drain them from perform64.
*/

#include "../../../../core/LinkAudioPlatform.h"   // Windows portability shim - must come first
#include "ext.h"
#include "ext_obex.h"
#include "ext_dictionary.h"
#include "ext_dictobj.h"
#include "z_dsp.h"

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
#include <vector>


// ============================================================================
// Object struct
//
// MUST start with t_pxobject. Plain C-style struct - Max allocates it via
// object_alloc(), so anything non-trivially-constructible must be placed
// in a separate "impl" struct on the heap, constructed in *_new and
// destroyed in *_free.
// ============================================================================

struct VoidLinkAudioInImpl
{
    std::shared_ptr<LinkAudioManager>          manager;
    std::unique_ptr<ableton::LinkAudioSource>  source;

    AudioRingBuffer ringL{16384};
    AudioRingBuffer ringR{16384};

    std::atomic<uint32_t> streamSampleRate {48000};
    std::atomic<uint32_t> streamNumChannels{1};
    std::atomic<uint64_t> framesReceived   {0};
    std::atomic<uint64_t> framesDropped    {0};

    // Cached current "wanted" target so we can detect changes on each dsp pass.
    std::string subscribedFromChannel;
    std::string subscribedFromPeer;

    bool        enabled         = false;
    double      quantum         = 4.0;

    // DSP sample rate, set by dsp64 callback. Used by perform64 to compute
    // beatsPerSample for the timing signal outlets. dsp64 and perform64
    // never run concurrently in MSP, no atomic needed.
    double      dspSampleRate   = 0.0;

    // Flag to suppress propagation in tempo/transport attribute setters when
    // the setter is being called from the poll-tick mirror sync (i.e. the
    // change came FROM the session, we don't want to write it BACK).
    // Without this, the touch -> setter -> session -> touch loop would spin.
    bool        syncingFromSession = false;

    // Tracking for auto re-dump on meaningful state change.
    // We only dump when one of these changes between successive apply_state()
    // calls - typing in the inspector won't spam the dumpout.
    bool        lastSubscribed         = false;
    std::size_t lastNumPeers           = 0;
    std::size_t lastNumAudioChannels   = 0;
    bool        firstStateApplied      = false;
};

typedef struct _voidlinkaudioreceive
{
    t_pxobject ob;       // MSP base (must be first)

    // Native Max attribute storage - these mirror impl state for Max's
    // attribute system to read/write directly.
    long      a_enable;
    t_symbol *a_fromchannel;
    t_symbol *a_frompeer;
    double    a_quantum;
    long      a_poll;            // 0/1 - auto retry + status polling
    long      a_pollinterval;    // ms - polling interval
    double    a_tempo;           // BPM - mirror only; getter reads live session
    long      a_transport;       // 0/1 - mirror only; getter reads live session

    void     *dumpout;           // right outlet for status messages
    void     *poll_clock;        // t_clock for periodic apply_state() retries

    VoidLinkAudioInImpl *impl;   // heap-allocated C++ state
} t_voidlinkaudioreceive;


// ============================================================================
// Forward declarations
// ============================================================================

static void *voidlinkaudioreceive_new   (t_symbol *s, long argc, t_atom *argv);
static void  voidlinkaudioreceive_free  (t_voidlinkaudioreceive *x);
static void  voidlinkaudioreceive_assist(t_voidlinkaudioreceive *x, void *b, long m, long a, char *s);
static void  voidlinkaudioreceive_bang  (t_voidlinkaudioreceive *x);
static void  voidlinkaudioreceive_info  (t_voidlinkaudioreceive *x);
static void  voidlinkaudioreceive_tick  (t_voidlinkaudioreceive *x);

static void  voidlinkaudioreceive_dsp64    (t_voidlinkaudioreceive *x, t_object *dsp64,
                                       short *count, double samplerate,
                                       long maxvectorsize, long flags);
static void  voidlinkaudioreceive_perform64(t_voidlinkaudioreceive *x, t_object *dsp64,
                                       double **ins, long numins,
                                       double **outs, long numouts,
                                       long sampleframes, long flags, void *userparam);

// Attribute setters
static t_max_err voidlinkaudioreceive_set_enable      (t_voidlinkaudioreceive *x, t_object *attr, long argc, t_atom *argv);
static t_max_err voidlinkaudioreceive_set_fromchannel (t_voidlinkaudioreceive *x, t_object *attr, long argc, t_atom *argv);
static t_max_err voidlinkaudioreceive_set_frompeer    (t_voidlinkaudioreceive *x, t_object *attr, long argc, t_atom *argv);
static t_max_err voidlinkaudioreceive_set_quantum     (t_voidlinkaudioreceive *x, t_object *attr, long argc, t_atom *argv);
static t_max_err voidlinkaudioreceive_set_poll        (t_voidlinkaudioreceive *x, t_object *attr, long argc, t_atom *argv);
static t_max_err voidlinkaudioreceive_set_pollinterval(t_voidlinkaudioreceive *x, t_object *attr, long argc, t_atom *argv);
static t_max_err voidlinkaudioreceive_set_tempo       (t_voidlinkaudioreceive *x, t_object *attr, long argc, t_atom *argv);
static t_max_err voidlinkaudioreceive_get_tempo       (t_voidlinkaudioreceive *x, t_object *attr, long *argc, t_atom **argv);
static t_max_err voidlinkaudioreceive_set_transport   (t_voidlinkaudioreceive *x, t_object *attr, long argc, t_atom *argv);
static t_max_err voidlinkaudioreceive_get_transport   (t_voidlinkaudioreceive *x, t_object *attr, long *argc, t_atom **argv);

// Internal helpers
static void apply_state             (t_voidlinkaudioreceive *x);
static void try_subscribe           (t_voidlinkaudioreceive *x);
static void unsubscribe_internal    (t_voidlinkaudioreceive *x);
static void on_source_buffer        (t_voidlinkaudioreceive *x,
                                     ableton::LinkAudioSource::BufferHandle bh);
static void poll_start              (t_voidlinkaudioreceive *x);
static void poll_stop               (t_voidlinkaudioreceive *x);

static t_class *s_voidlinkaudioreceive_class = nullptr;


// ============================================================================
// Class registration
// ============================================================================

void ext_main(void *r)
{
    t_class *c = class_new("void.linkaudio.receive~",
                           (method)voidlinkaudioreceive_new,
                           (method)voidlinkaudioreceive_free,
                           sizeof(t_voidlinkaudioreceive),
                           NULL,
                           A_GIMME,
                           0);

    class_addmethod(c, (method)voidlinkaudioreceive_dsp64,  "dsp64",  A_CANT, 0);
    class_addmethod(c, (method)voidlinkaudioreceive_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)voidlinkaudioreceive_bang,   "bang",   0);
    class_addmethod(c, (method)voidlinkaudioreceive_info,   "info",   0);

    // NOTE: We deliberately do NOT register custom "poll" / "tempo" / "transport"
    // methods. Since they're CLASS_ATTR_*, Max automatically handles
    // [tempo 128(, [transport 1(, etc. via the attribute setter.

    // ---- Attributes ----
    CLASS_ATTR_LONG (c, "enable",  0, t_voidlinkaudioreceive, a_enable);
    CLASS_ATTR_STYLE_LABEL(c, "enable", 0, "onoff", "Enable Link Audio");
    CLASS_ATTR_ACCESSORS  (c, "enable", NULL, voidlinkaudioreceive_set_enable);

    CLASS_ATTR_SYM  (c, "fromchannel", 0, t_voidlinkaudioreceive, a_fromchannel);
    CLASS_ATTR_LABEL(c, "fromchannel", 0, "From Channel (channel name to subscribe to)");
    CLASS_ATTR_ACCESSORS(c, "fromchannel", NULL, voidlinkaudioreceive_set_fromchannel);

    CLASS_ATTR_SYM  (c, "frompeer",  0, t_voidlinkaudioreceive, a_frompeer);
    CLASS_ATTR_LABEL(c, "frompeer", 0, "From Peer (peer name of the source SEND - essential to disambiguate)");
    CLASS_ATTR_ACCESSORS(c, "frompeer", NULL, voidlinkaudioreceive_set_frompeer);

    CLASS_ATTR_DOUBLE(c, "quantum", 0, t_voidlinkaudioreceive, a_quantum);
    CLASS_ATTR_LABEL (c, "quantum", 0, "Quantum (beats)");
    CLASS_ATTR_FILTER_CLIP(c, "quantum", 1.0, 16.0);
    CLASS_ATTR_ACCESSORS  (c, "quantum", NULL, voidlinkaudioreceive_set_quantum);

    CLASS_ATTR_LONG (c, "poll",    0, t_voidlinkaudioreceive, a_poll);
    CLASS_ATTR_STYLE_LABEL(c, "poll", 0, "onoff", "Auto-poll (subscribe retry + status)");
    CLASS_ATTR_ACCESSORS  (c, "poll", NULL, voidlinkaudioreceive_set_poll);

    CLASS_ATTR_LONG (c, "pollinterval", 0, t_voidlinkaudioreceive, a_pollinterval);
    CLASS_ATTR_LABEL(c, "pollinterval", 0, "Polling interval (ms)");
    CLASS_ATTR_FILTER_CLIP(c, "pollinterval", 50, 60000);
    CLASS_ATTR_ACCESSORS  (c, "pollinterval", NULL, voidlinkaudioreceive_set_pollinterval);

    // Tempo and transport mirror Link session state. Custom getters read
    // the live session; custom setters propagate to the session via the
    // shared LinkAudioManager (capture/commit pattern).
    CLASS_ATTR_DOUBLE(c, "tempo", 0, t_voidlinkaudioreceive, a_tempo);
    CLASS_ATTR_LABEL (c, "tempo", 0, "Tempo (BPM, session-wide)");
    CLASS_ATTR_FILTER_CLIP(c, "tempo", 20.0, 999.0);
    CLASS_ATTR_ACCESSORS  (c, "tempo", voidlinkaudioreceive_get_tempo, voidlinkaudioreceive_set_tempo);

    CLASS_ATTR_LONG  (c, "transport", 0, t_voidlinkaudioreceive, a_transport);
    CLASS_ATTR_STYLE_LABEL(c, "transport", 0, "onoff", "Transport (session-wide; requires startStopSync peers)");
    CLASS_ATTR_ACCESSORS  (c, "transport", voidlinkaudioreceive_get_transport, voidlinkaudioreceive_set_transport);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    s_voidlinkaudioreceive_class = c;

}


// ============================================================================
// Instance lifecycle
// ============================================================================

static void *
voidlinkaudioreceive_new(t_symbol *s, long argc, t_atom *argv)
{
    t_voidlinkaudioreceive *x = (t_voidlinkaudioreceive *)object_alloc(s_voidlinkaudioreceive_class);
    if (!x)
        return NULL;

    // No signal inlet (this is a pure receiver). Just outlets.
    // Order of outlet_new calls matters: rightmost first, leftmost last.
    //
    // Final layout (left to right):
    //   [audioL] [audioR] [tempo~] [phase~] [transport~] [dumpout]
    x->dumpout = outlet_new((t_object *)x, NULL);     // dumpout (rightmost)
    outlet_new((t_object *)x, "signal");              // transport~
    outlet_new((t_object *)x, "signal");              // phase~
    outlet_new((t_object *)x, "signal");              // tempo~
    outlet_new((t_object *)x, "signal");              // R signal
    outlet_new((t_object *)x, "signal");              // L signal (leftmost)

    // Default attribute values (before parsing args)
    x->a_enable       = 1;
    x->a_fromchannel  = gensym("");
    x->a_frompeer     = gensym("");
    x->a_quantum      = 4.0;
    x->a_poll         = 1;
    x->a_pollinterval = 250;
    x->a_tempo        = 120.0;   // mirror; getter reads live session
    x->a_transport    = 0;       // mirror; getter reads live session

    // Heap-construct the C++ impl
    x->impl = new (std::nothrow) VoidLinkAudioInImpl();
    if (!x->impl)
    {
        object_error((t_object *)x, "void.linkaudio.receive~: out of memory");
        return NULL;
    }

    // Acquire shared LinkAudio manager (process-wide singleton, ref-counted)
    x->impl->manager = LinkAudioManager::acquire();
    x->impl->quantum = x->a_quantum;

    // Create the polling clock (not started yet - apply_state() will start it
    // if @poll is on after attribute parsing).
    x->poll_clock = clock_new((t_object *)x, (method)voidlinkaudioreceive_tick);

    // Parse @attribute-style args from the box (peer foo channel bar etc.)
    attr_args_process(x, (short)argc, argv);

    // Apply current attributes to the underlying Link state.
    apply_state(x);

    // Initial mirror sync: pull current session tempo/transport into our
    // attribute storage so a freshly-instantiated object reflects the live
    // session state (e.g. if Live is already at 140 BPM, attrui shows 140
    // as soon as the patcher finishes loading rather than waiting one full
    // pollinterval). We don't call object_attr_touch here because attrui
    // observers may not be bound yet at this point in patcher load.
    if (x->impl->manager)
    {
        x->a_tempo     = x->impl->manager->tempo();
        x->a_transport = x->impl->manager->isPlaying() ? 1 : 0;
    }

    // Start polling if enabled. Schedule the first tick fast (50ms) instead
    // of waiting a full pollinterval, so attrui observers attached after the
    // patcher load completes get notified quickly via object_attr_touch.
    if (x->a_poll && x->poll_clock)
        clock_fdelay(x->poll_clock, 50.0);

    return x;
}

static void
voidlinkaudioreceive_free(t_voidlinkaudioreceive *x)
{
    // dsp_free first to detach from MSP graph
    dsp_free((t_pxobject *)x);

    // Stop and free the polling clock
    if (x->poll_clock)
    {
        clock_unset(x->poll_clock);
        object_free(x->poll_clock);
        x->poll_clock = nullptr;
    }

    if (x->impl)
    {
        unsubscribe_internal(x);
        // Release shared_ptr to manager; the actual LinkAudio object goes
        // away when the last instance releases it.
        x->impl->manager.reset();
        delete x->impl;
        x->impl = nullptr;
    }
}

static void
voidlinkaudioreceive_assist(t_voidlinkaudioreceive *x, void *b, long io, long index, char *s)
{
    if (io == ASSIST_INLET)
    {
        snprintf(s, 256, "messages: bang, info, tempo, transport, attributes");
    }
    else
    {
        switch (index)
        {
            case 0: snprintf(s, 256, "(signal) audio L"); break;
            case 1: snprintf(s, 256, "(signal) audio R"); break;
            case 2: snprintf(s, 256, "(signal) tempo (BPM)"); break;
            case 3: snprintf(s, 256, "(signal) phase [0, quantum)"); break;
            case 4: snprintf(s, 256, "(signal) transport (0/1)"); break;
            case 5: snprintf(s, 256, "dumpout: status / dict / channels"); break;
            default: snprintf(s, 256, "outlet"); break;
        }
    }
}


// ============================================================================
// Attribute setters - store value, then re-apply state if needed
// ============================================================================

static t_max_err
voidlinkaudioreceive_set_enable(t_voidlinkaudioreceive *x, t_object *attr, long argc, t_atom *argv)
{
    if (argc && argv)
        x->a_enable = (atom_getlong(argv) != 0) ? 1 : 0;
    if (x->impl) apply_state(x);
    return MAX_ERR_NONE;
}

static t_max_err
voidlinkaudioreceive_set_fromchannel(t_voidlinkaudioreceive *x, t_object *attr, long argc, t_atom *argv)
{
    if (argc && argv)
        x->a_fromchannel = atom_getsym(argv);
    if (x->impl) apply_state(x);
    return MAX_ERR_NONE;
}

static t_max_err
voidlinkaudioreceive_set_frompeer(t_voidlinkaudioreceive *x, t_object *attr, long argc, t_atom *argv)
{
    if (argc && argv)
        x->a_frompeer = atom_getsym(argv);
    if (x->impl) apply_state(x);
    return MAX_ERR_NONE;
}

static t_max_err
voidlinkaudioreceive_set_quantum(t_voidlinkaudioreceive *x, t_object *attr, long argc, t_atom *argv)
{
    if (argc && argv)
        x->a_quantum = atom_getfloat(argv);
    if (x->a_quantum < 1.0)  x->a_quantum = 1.0;
    if (x->a_quantum > 16.0) x->a_quantum = 16.0;
    if (x->impl) x->impl->quantum = x->a_quantum;
    return MAX_ERR_NONE;
}

static t_max_err
voidlinkaudioreceive_set_poll(t_voidlinkaudioreceive *x, t_object *attr, long argc, t_atom *argv)
{
    if (argc && argv)
        x->a_poll = (atom_getlong(argv) != 0) ? 1 : 0;
    if (x->poll_clock)
    {
        if (x->a_poll) poll_start(x);
        else           poll_stop(x);
    }
    return MAX_ERR_NONE;
}

static t_max_err
voidlinkaudioreceive_set_pollinterval(t_voidlinkaudioreceive *x, t_object *attr, long argc, t_atom *argv)
{
    if (argc && argv)
        x->a_pollinterval = atom_getlong(argv);
    if (x->a_pollinterval < 50)    x->a_pollinterval = 50;
    if (x->a_pollinterval > 60000) x->a_pollinterval = 60000;
    // No need to restart the clock - next clock_fdelay in tick() picks up
    // the new interval naturally.
    return MAX_ERR_NONE;
}

// ---- tempo (R/W; setter propagates to session, getter reads live session) --

static t_max_err
voidlinkaudioreceive_set_tempo(t_voidlinkaudioreceive *x, t_object *attr, long argc, t_atom *argv)
{
    if (!argc || !argv) return MAX_ERR_NONE;
    double bpm = atom_getfloat(argv);
    if (bpm < 20.0)  bpm = 20.0;
    if (bpm > 999.0) bpm = 999.0;
    x->a_tempo = bpm;
    // Skip propagation when the setter is being called as part of the
    // poll-tick mirror sync (the value came from the session itself).
    if (x->impl && x->impl->manager && !x->impl->syncingFromSession)
        x->impl->manager->setTempo(bpm);
    return MAX_ERR_NONE;
}

static t_max_err
voidlinkaudioreceive_get_tempo(t_voidlinkaudioreceive *x, t_object *attr, long *argc, t_atom **argv)
{
    char alloc;
    if (atom_alloc(argc, argv, &alloc) != MAX_ERR_NONE) return MAX_ERR_GENERIC;
    double bpm = 120.0;
    if (x->impl && x->impl->manager)
        bpm = x->impl->manager->tempo();
    atom_setfloat(*argv, bpm);
    return MAX_ERR_NONE;
}

// ---- transport (R/W; setter propagates to session, getter reads live session)

static t_max_err
voidlinkaudioreceive_set_transport(t_voidlinkaudioreceive *x, t_object *attr, long argc, t_atom *argv)
{
    if (!argc || !argv) return MAX_ERR_NONE;
    long val = (atom_getlong(argv) != 0) ? 1 : 0;
    x->a_transport = val;
    // Skip propagation when the setter is being called as part of the
    // poll-tick mirror sync (the value came from the session itself).
    if (x->impl && x->impl->manager && !x->impl->syncingFromSession)
        x->impl->manager->setIsPlaying(val != 0);
    return MAX_ERR_NONE;
}

static t_max_err
voidlinkaudioreceive_get_transport(t_voidlinkaudioreceive *x, t_object *attr, long *argc, t_atom **argv)
{
    char alloc;
    if (atom_alloc(argc, argv, &alloc) != MAX_ERR_NONE) return MAX_ERR_GENERIC;
    long val = 0;
    if (x->impl && x->impl->manager)
        val = x->impl->manager->isPlaying() ? 1 : 0;
    atom_setlong(*argv, val);
    return MAX_ERR_NONE;
}


// ============================================================================
// Polling clock
// ============================================================================

static void
poll_start(t_voidlinkaudioreceive *x)
{
    if (!x->poll_clock) return;
    // clock_fdelay schedules a one-shot callback after `ms`. Re-armed in tick.
    clock_fdelay(x->poll_clock, (double)x->a_pollinterval);
}

static void
poll_stop(t_voidlinkaudioreceive *x)
{
    if (!x->poll_clock) return;
    clock_unset(x->poll_clock);
}

static void
voidlinkaudioreceive_tick(t_voidlinkaudioreceive *x)
{
    if (!x || !x->impl) return;

    // Mirror-sync: if the session tempo/transport changed externally
    // (e.g. another peer or Live), update our local attribute storage so
    // attrui / pattr / [getattr] / inspector all reflect the new value.
    // Use the syncingFromSession flag to suppress the setter's propagation
    // back to the session (otherwise touch -> setter -> session -> touch loop).
    if (x->impl->manager)
    {
        const double sessionTempo = x->impl->manager->tempo();
        const long   sessionXport = x->impl->manager->isPlaying() ? 1 : 0;

        if (std::abs(sessionTempo - x->a_tempo) > 0.001)
        {
            x->impl->syncingFromSession = true;
            x->a_tempo = sessionTempo;
            object_attr_touch((t_object *)x, gensym("tempo"));
            x->impl->syncingFromSession = false;
        }
        if (sessionXport != x->a_transport)
        {
            x->impl->syncingFromSession = true;
            x->a_transport = sessionXport;
            object_attr_touch((t_object *)x, gensym("transport"));
            x->impl->syncingFromSession = false;
        }
    }

    // Re-apply state. apply_state() is idempotent and will only re-attempt
    // subscribe if not currently subscribed and a channel target is defined.
    apply_state(x);

    // Re-arm if polling is still on.
    if (x->a_poll && x->poll_clock)
        clock_fdelay(x->poll_clock, (double)x->a_pollinterval);
}


// ============================================================================
// State application - drives Link enable/disable + subscribe/unsubscribe
// ============================================================================

static void
apply_state(t_voidlinkaudioreceive *x)
{
    if (!x->impl || !x->impl->manager)
        return;

    auto& la = x->impl->manager->linkAudio();

    // Enable / disable
    const bool wantEnabled = (x->a_enable != 0);
    if (wantEnabled != x->impl->enabled)
    {
        la.enable(wantEnabled);
        la.enableLinkAudio(wantEnabled);
        x->impl->enabled = wantEnabled;
        if (!wantEnabled)
            unsubscribe_internal(x);
    }

    // Subscribe target change?
    const std::string newFromChannel = (x->a_fromchannel && x->a_fromchannel->s_name) ? std::string(x->a_fromchannel->s_name) : std::string();
    const std::string newFromPeer    = (x->a_frompeer  && x->a_frompeer->s_name)  ? std::string(x->a_frompeer->s_name)  : std::string();

    if (newFromChannel != x->impl->subscribedFromChannel || newFromPeer != x->impl->subscribedFromPeer)
    {
        unsubscribe_internal(x);
        x->impl->subscribedFromChannel = newFromChannel;
        x->impl->subscribedFromPeer    = newFromPeer;
    }

    if (x->impl->enabled && !x->impl->source && !x->impl->subscribedFromChannel.empty())
    {
        try_subscribe(x);
    }

    // Auto re-dump if any meaningful state has changed since the last call.
    // We compare against last-seen values stored in the impl. The first call
    // always emits a dump (so an observer wired up at creation gets the
    // initial snapshot once peers/channels are discovered).
    const bool        nowSubscribed         = (x->impl->source != nullptr);
    const std::size_t nowNumPeers           = la.numPeers();
    const std::size_t nowNumAudioChannels   = la.channels().size();

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
        voidlinkaudioreceive_info(x);
    }
}

static void
try_subscribe(t_voidlinkaudioreceive *x)
{
    if (!x->impl || !x->impl->manager) return;

    auto channels = x->impl->manager->channels();
    std::optional<ableton::ChannelId> match;
    for (const auto& ch : channels)
    {
        if (ch.name != x->impl->subscribedFromChannel) continue;
        if (!x->impl->subscribedFromPeer.empty() && ch.peerName != x->impl->subscribedFromPeer) continue;
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

    // Constructor accesses LinkAudio's private mController; passing the
    // concrete reference (not a templated proxy) makes friendship work.
    x->impl->source.reset(new ableton::LinkAudioSource(
        la,
        *match,
        [x](ableton::LinkAudioSource::BufferHandle bh)
        {
            on_source_buffer(x, bh);
        }));
}

static void
unsubscribe_internal(t_voidlinkaudioreceive *x)
{
    if (!x->impl) return;
    x->impl->source.reset();
    x->impl->ringL.reset();
    x->impl->ringR.reset();
}


// ============================================================================
// Source callback - Link-managed thread, must be quick & lock-free
// ============================================================================

static void
on_source_buffer(t_voidlinkaudioreceive *x,
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
// DSP
// ============================================================================

static void
voidlinkaudioreceive_dsp64(t_voidlinkaudioreceive *x, t_object *dsp64, short *count,
                      double samplerate, long maxvectorsize, long flags)
{
    // Cache sample rate for the timing-signal computation in perform64.
    if (x->impl) x->impl->dspSampleRate = samplerate;

    object_method(dsp64, gensym("dsp_add64"),
                  x, voidlinkaudioreceive_perform64, 0, NULL);
}

static void
voidlinkaudioreceive_perform64(t_voidlinkaudioreceive *x, t_object *dsp64,
                          double **ins, long numins,
                          double **outs, long numouts,
                          long sampleframes, long flags, void *userparam)
{
    // Outlet layout (left to right):
    //   0:audioL  1:audioR  2:tempo~  3:phase~  4:transport~
    double *outL         = (numouts > 0) ? outs[0] : nullptr;
    double *outR         = (numouts > 1) ? outs[1] : nullptr;
    double *outTempo     = (numouts > 2) ? outs[2] : nullptr;
    double *outPhase     = (numouts > 3) ? outs[3] : nullptr;
    double *outTransport = (numouts > 4) ? outs[4] : nullptr;

    const long n = sampleframes;

    // ---- Audio output ------------------------------------------------------
    //
    // If not enabled or no source, output silence on the audio outlets.
    // Timing outlets keep working regardless of subscribe state - they
    // reflect the local Link session, which exists as soon as @enable is on.
    const bool audioActive = x->impl && x->impl->enabled && x->impl->source;

    if (!audioActive)
    {
        if (outL) std::memset(outL, 0, sizeof(double) * n);
        if (outR) std::memset(outR, 0, sizeof(double) * n);
    }
    else
    {
        // Drain ringL into outL (float -> double).
        if (outL)
        {
            constexpr std::size_t kBatch = 256;
            float tmp[kBatch];
            long  done = 0;
            while (done < n)
            {
                const std::size_t want = static_cast<std::size_t>(
                    (n - done < (long)kBatch) ? (n - done) : (long)kBatch);
                const std::size_t got = x->impl->ringL.read(tmp, want);
                for (std::size_t i = 0; i < got; ++i)
                    outL[done + i] = static_cast<double>(tmp[i]);
                for (std::size_t i = got; i < want; ++i)
                    outL[done + i] = 0.0;
                done += static_cast<long>(want);
            }
        }

        // Drain ringR into outR if stream is stereo, otherwise mirror outL.
        if (outR)
        {
            const bool streamIsStereo = (x->impl->streamNumChannels.load() >= 2);
            if (streamIsStereo)
            {
                constexpr std::size_t kBatch = 256;
                float tmp[kBatch];
                long  done = 0;
                while (done < n)
                {
                    const std::size_t want = static_cast<std::size_t>(
                        (n - done < (long)kBatch) ? (n - done) : (long)kBatch);
                    const std::size_t got = x->impl->ringR.read(tmp, want);
                    for (std::size_t i = 0; i < got; ++i)
                        outR[done + i] = static_cast<double>(tmp[i]);
                    for (std::size_t i = got; i < want; ++i)
                        outR[done + i] = 0.0;
                    done += static_cast<long>(want);
                }
            }
            else if (outL)
            {
                std::memcpy(outR, outL, sizeof(double) * n);
            }
        }
    }

    // ---- Timing signals ----------------------------------------------------
    //
    // These follow the local Link session and are independent of the audio
    // subscribe state. We capture once per buffer (lock-free on the audio
    // thread) and advance linearly using beatsPerSample.
    if (!x->impl || !x->impl->manager || x->impl->dspSampleRate <= 0.0)
    {
        if (outTempo)     std::memset(outTempo,     0, sizeof(double) * n);
        if (outPhase)     std::memset(outPhase,     0, sizeof(double) * n);
        if (outTransport) std::memset(outTransport, 0, sizeof(double) * n);
        return;
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

    for (long i = 0; i < n; ++i)
    {
        const double beat  = beatBegin + beatsPerSample * static_cast<double>(i);
        double       phase = std::fmod(beat, quantum);
        if (phase < 0.0) phase += quantum;

        if (outTempo)     outTempo[i]     = tempoBpm;
        if (outPhase)     outPhase[i]     = phase;
        if (outTransport) outTransport[i] = playingVal;
    }
}


// ============================================================================
// bang / info
//
// bang : silent retry - re-applies state, may trigger subscribe.
//        If the resulting state differs from the previous, the auto-dump
//        in apply_state() will emit a fresh dict on the dumpout.
// info : explicit dict dump on the dumpout (route via [dict.view])
// ============================================================================

static void
voidlinkaudioreceive_bang(t_voidlinkaudioreceive *x)
{
    if (!x->impl) return;
    apply_state(x);
}

static void
voidlinkaudioreceive_info(t_voidlinkaudioreceive *x)
{
    if (!x->impl || !x->impl->manager) return;

    auto& la = x->impl->manager->linkAudio();
    const auto state    = la.captureAppSessionState();
    const auto now      = la.clock().micros();
    const auto channels = la.channels();

    // Build dict
    t_dictionary *d = dictionary_new();
    if (!d) return;

    dictionary_appendlong  (d, gensym("enabled"),             x->impl->enabled ? 1 : 0);
    dictionary_appendlong  (d, gensym("audio_enabled"),       la.isLinkAudioEnabled() ? 1 : 0);
    dictionary_appendlong  (d, gensym("num_peers"),           (t_atom_long)la.numPeers());
    dictionary_appendlong  (d, gensym("subscribed"),          x->impl->source ? 1 : 0);
    dictionary_appendsym   (d, gensym("fromchannel"),         x->a_fromchannel ? x->a_fromchannel : gensym(""));
    dictionary_appendsym   (d, gensym("frompeer"),            x->a_frompeer  ? x->a_frompeer  : gensym(""));
    dictionary_appendlong  (d, gensym("stream_sample_rate"),  (t_atom_long)x->impl->streamSampleRate.load());
    dictionary_appendlong  (d, gensym("stream_num_channels"), (t_atom_long)x->impl->streamNumChannels.load());
    dictionary_appendlong  (d, gensym("frames_received"),     (t_atom_long)x->impl->framesReceived.load());
    dictionary_appendlong  (d, gensym("frames_dropped"),      (t_atom_long)x->impl->framesDropped.load());
    dictionary_appendfloat (d, gensym("tempo"),               state.tempo());
    dictionary_appendfloat (d, gensym("beat"),                state.beatAtTime(now, x->impl->quantum));
    dictionary_appendfloat (d, gensym("phase"),               state.phaseAtTime(now, x->impl->quantum));
    dictionary_appendfloat (d, gensym("quantum"),             x->impl->quantum);
    dictionary_appendlong  (d, gensym("transport"),           state.isPlaying() ? 1 : 0);
    dictionary_appendlong  (d, gensym("poll"),                x->a_poll);
    dictionary_appendlong  (d, gensym("pollinterval"),        x->a_pollinterval);
    dictionary_appendlong  (d, gensym("num_audio_channels"),  (t_atom_long)channels.size());

    // Build a sub-dict for the channels list, so user can iterate.
    // Each key is "<index>" -> array of [channel_name, peer_name].
    t_dictionary *channelsDict = dictionary_new();
    if (channelsDict)
    {
        long idx = 0;
        for (const auto& ch : channels)
        {
            t_atom av[2];
            atom_setsym(&av[0], gensym(ch.name.c_str()));
            atom_setsym(&av[1], gensym(ch.peerName.c_str()));
            char keybuf[16];
            snprintf(keybuf, sizeof(keybuf), "%ld", idx++);
            dictionary_appendatoms(channelsDict, gensym(keybuf), 2, av);
        }
        dictionary_appenddictionary(d, gensym("channels"), (t_object *)channelsDict);
    }

    // Register the dict and emit a dictionary message that [dict.view] can handle.
    t_symbol *dictName = nullptr;
    t_dictionary *registered = dictobj_register(d, &dictName);
    if (registered && dictName)
    {
        t_atom a;
        atom_setsym(&a, dictName);
        outlet_anything(x->dumpout, gensym("dictionary"), 1, &a);
        dictobj_release(registered);
    }
    else
    {
        // Fallback: free locally if registration failed
        object_free(d);
    }
}
