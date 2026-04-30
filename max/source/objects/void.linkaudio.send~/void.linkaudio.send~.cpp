// ============================================================================
// VoidLinkAudio - void.linkaudio.send~ (Max/MSP external)
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

/*
    void.linkaudio.send~

    Publish audio to an Ableton Link Audio channel on the LAN, plus expose
    Link timing context as audio-rate signals.

    Inlets:
        1 or 2 signal inlets (depending on @stereo, default stereo)
        + the message inlet on inlet 0
    Outlets:
        3 signal outlets : tempo~, phase~, transport~
        1 dumpout (rightmost) for status messages and dict

    Attributes:
        @enable        0/1   - enable Link Audio (default 1)
        @stereo        0/1   - 1 = 2 inlets stereo, 0 = 1 inlet mono
                               (must be set as a creation argument; cannot
                               be changed at runtime because it changes the
                               number of signal inlets)
        @peer          sym   - local peer name advertised (default "Max")
        @channel       sym   - name of the Link Audio channel to publish on
                               (default "Max Out")
        @quantum       float - quantum used for beat / phase mapping (default 4)
        @tempo         float - session tempo in BPM (R/W, propagates to peers,
                               clipped to 20..999)
        @transport     0/1   - session transport state (R/W, propagates to peers
                               via startStopSync; reads isPlaying() live)
        @poll          0/1   - auto-retry publish + auto-status (default 1)
        @pollinterval  long  - polling interval in ms (default 250, range 50..60000)

    Messages:
        bang             - silent retry; re-applies state. State changes
                           will trigger an automatic dict dump.
        info             - explicit dict dump on the dumpout (route via
                           [dict.view] / [dict.unpack] / [route ...])
        poll <0|1>       - handled automatically via the attribute setter
        pollinterval <n> - handled automatically via the attribute setter

    Timing signals:
        tempo~     - session BPM, sample-accurate, slowly varying
        phase~     - sawtooth in [0, quantum), wraps modulo quantum
        transport~ - 0.0 or 1.0, follows session.isPlaying()

        These are derived from captureAudioSessionState() at the start of
        each perform buffer, then advanced linearly across the buffer using
        beatsPerSample = tempo / 60 / sampleRate. Quantum is read from the
        @quantum attribute (constant per session, not a signal).

    Auto-dump:
        Same as receive~: emits a fresh dict on dumpout when meaningful state
        changes (publishing flips on/off, peer count changes).

    Notes:
        - Sample format on the wire is int16 interleaved.
        - The Sink's BufferHandle is acquired/released directly inside the
          DSP perform routine. This matches what the TD CHOP does and is
          how Ableton's API is designed to be used (lock-free).
*/

#include "../../../../core/LinkAudioPlatform.h"   // Windows portability shim — must come first
#include "ext.h"
#include "ext_obex.h"
#include "ext_dictionary.h"
#include "ext_dictobj.h"
#include "z_dsp.h"

#include "LinkAudioManager.h"

#include <ableton/LinkAudio.hpp>

#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <string>


// ============================================================================
// Object struct
// ============================================================================

struct VoidLinkAudioOutImpl
{
    std::shared_ptr<LinkAudioManager>        manager;
    std::unique_ptr<ableton::LinkAudioSink>  sink;

    // Realtime stats (touched from the DSP perform thread)
    std::atomic<uint64_t> framesPublished  {0};
    std::atomic<uint64_t> framesNoBuffer   {0};
    std::atomic<uint64_t> framesCommitFail {0};

    // Cached "wanted" state — drives apply_state diffing.
    std::string  publishedChannel;
    std::string  currentPeerName;
    bool         enabled        = false;
    double       quantum        = 4.0;
    bool         stereo         = true;       // set once at _new from @stereo
    double       dspSampleRate  = 48000.0;    // updated in dsp64

    // Auto-dump tracking
    bool         lastPublishing       = false;
    std::size_t  lastNumPeers         = 0;
    std::size_t  lastNumAudioChannels = 0;
    bool         firstStateApplied    = false;

    // Flag to suppress propagation in tempo/transport attribute setters when
    // the setter is being called from the poll-tick mirror sync (i.e. the
    // change came FROM the session, we don't want to write it BACK).
    // Without this, the touch -> setter -> session -> touch loop would spin.
    bool         syncingFromSession   = false;
};

typedef struct _voidlinkaudiosend
{
    t_pxobject ob;       // MSP base (must be first)

    long      a_enable;
    long      a_stereo;          // creation-time only
    t_symbol *a_peer;
    t_symbol *a_channel;
    double    a_quantum;
    double    a_tempo;           // R/W: session tempo (BPM), 20..999
    long      a_transport;       // R/W: 0/1, mirrors session.isPlaying()
    long      a_poll;
    long      a_pollinterval;

    void     *dumpout;
    void     *poll_clock;

    VoidLinkAudioOutImpl *impl;
} t_voidlinkaudiosend;


// ============================================================================
// Forward declarations
// ============================================================================

static void *voidlinkaudiosend_new   (t_symbol *s, long argc, t_atom *argv);
static void  voidlinkaudiosend_free  (t_voidlinkaudiosend *x);
static void  voidlinkaudiosend_assist(t_voidlinkaudiosend *x, void *b, long m, long a, char *s);
static void  voidlinkaudiosend_bang  (t_voidlinkaudiosend *x);
static void  voidlinkaudiosend_info  (t_voidlinkaudiosend *x);
static void  voidlinkaudiosend_tick  (t_voidlinkaudiosend *x);

static void  voidlinkaudiosend_dsp64    (t_voidlinkaudiosend *x, t_object *dsp64,
                                        short *count, double samplerate,
                                        long maxvectorsize, long flags);
static void  voidlinkaudiosend_perform64(t_voidlinkaudiosend *x, t_object *dsp64,
                                        double **ins, long numins,
                                        double **outs, long numouts,
                                        long sampleframes, long flags, void *userparam);

static t_max_err voidlinkaudiosend_set_enable      (t_voidlinkaudiosend *x, t_object *attr, long argc, t_atom *argv);
static t_max_err voidlinkaudiosend_set_peer        (t_voidlinkaudiosend *x, t_object *attr, long argc, t_atom *argv);
static t_max_err voidlinkaudiosend_set_channel     (t_voidlinkaudiosend *x, t_object *attr, long argc, t_atom *argv);
static t_max_err voidlinkaudiosend_set_quantum     (t_voidlinkaudiosend *x, t_object *attr, long argc, t_atom *argv);
static t_max_err voidlinkaudiosend_set_tempo       (t_voidlinkaudiosend *x, t_object *attr, long argc, t_atom *argv);
static t_max_err voidlinkaudiosend_get_tempo       (t_voidlinkaudiosend *x, t_object *attr, long *argc, t_atom **argv);
static t_max_err voidlinkaudiosend_set_transport   (t_voidlinkaudiosend *x, t_object *attr, long argc, t_atom *argv);
static t_max_err voidlinkaudiosend_get_transport   (t_voidlinkaudiosend *x, t_object *attr, long *argc, t_atom **argv);
static t_max_err voidlinkaudiosend_set_poll        (t_voidlinkaudiosend *x, t_object *attr, long argc, t_atom *argv);
static t_max_err voidlinkaudiosend_set_pollinterval(t_voidlinkaudiosend *x, t_object *attr, long argc, t_atom *argv);

static void apply_state            (t_voidlinkaudiosend *x);
static void destroy_sink_internal  (t_voidlinkaudiosend *x);
static void poll_start             (t_voidlinkaudiosend *x);
static void poll_stop              (t_voidlinkaudiosend *x);

static t_class *s_voidlinkaudiosend_class = nullptr;

// Buffer sizing constant. The TD code uses 8192 but Max with smaller vector
// sizes (32 samples by default) calls perform much more often than TD's
// frame-rate cooks, so we give the sink more headroom to avoid
// "no buffer available" rejections. 32768 stereo samples = ~340ms at 48k
// which is plenty for any reasonable Live latency setting.
static constexpr std::size_t kInitialMaxSamples = 32768;


// ============================================================================
// Helper: pre-scan argv for @stereo, before attribute parsing.
//
// We need to know stereo-ness BEFORE creating signal inlets, because Max
// allocates inlets in dsp_setup() which happens in _new prior to
// attr_args_process(). So we manually scan argv for "@stereo" once.
// ============================================================================

static long
scan_stereo_arg(long argc, t_atom *argv, long defaultValue)
{
    for (long i = 0; i + 1 < argc; ++i)
    {
        if (atom_gettype(&argv[i]) == A_SYM)
        {
            t_symbol *s = atom_getsym(&argv[i]);
            if (s && s->s_name && strcmp(s->s_name, "@stereo") == 0)
            {
                return (atom_getlong(&argv[i + 1]) != 0) ? 1 : 0;
            }
        }
    }
    return defaultValue;
}


// ============================================================================
// Class registration
// ============================================================================

void ext_main(void *r)
{
    t_class *c = class_new("void.linkaudio.send~",
                           (method)voidlinkaudiosend_new,
                           (method)voidlinkaudiosend_free,
                           sizeof(t_voidlinkaudiosend),
                           NULL,
                           A_GIMME,
                           0);

    class_addmethod(c, (method)voidlinkaudiosend_dsp64,  "dsp64",  A_CANT, 0);
    class_addmethod(c, (method)voidlinkaudiosend_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)voidlinkaudiosend_bang,   "bang",   0);
    class_addmethod(c, (method)voidlinkaudiosend_info,   "info",   0);

    // ---- Attributes ----
    CLASS_ATTR_LONG (c, "enable",  0, t_voidlinkaudiosend, a_enable);
    CLASS_ATTR_STYLE_LABEL(c, "enable", 0, "onoff", "Enable Link Audio");
    CLASS_ATTR_ACCESSORS  (c, "enable", NULL, voidlinkaudiosend_set_enable);

    CLASS_ATTR_LONG (c, "stereo", 0, t_voidlinkaudiosend, a_stereo);
    CLASS_ATTR_STYLE_LABEL(c, "stereo", 0, "onoff", "Stereo (creation-time only)");
    // No accessor — read-only at runtime; only honored at creation.

    CLASS_ATTR_SYM  (c, "peer",    0, t_voidlinkaudiosend, a_peer);
    CLASS_ATTR_LABEL(c, "peer",    0, "Peer name (advertised on network)");
    CLASS_ATTR_ACCESSORS(c, "peer", NULL, voidlinkaudiosend_set_peer);

    CLASS_ATTR_SYM  (c, "channel", 0, t_voidlinkaudiosend, a_channel);
    CLASS_ATTR_LABEL(c, "channel", 0, "Channel name to publish");
    CLASS_ATTR_ACCESSORS(c, "channel", NULL, voidlinkaudiosend_set_channel);

    CLASS_ATTR_DOUBLE(c, "quantum", 0, t_voidlinkaudiosend, a_quantum);
    CLASS_ATTR_LABEL (c, "quantum", 0, "Quantum (beats)");
    CLASS_ATTR_FILTER_CLIP(c, "quantum", 1.0, 16.0);
    CLASS_ATTR_ACCESSORS  (c, "quantum", NULL, voidlinkaudiosend_set_quantum);

    // R/W mirror of session tempo. Setter pushes via setTempo() (commit to
    // app session state); getter reads back captureAppSessionState().tempo()
    // so external changes (e.g. Live or another peer raising the tempo) are
    // reflected when the attribute is queried via [getattr] or attrui.
    CLASS_ATTR_DOUBLE(c, "tempo", 0, t_voidlinkaudiosend, a_tempo);
    CLASS_ATTR_LABEL (c, "tempo", 0, "Tempo (BPM)");
    CLASS_ATTR_FILTER_CLIP(c, "tempo", 20.0, 999.0);
    CLASS_ATTR_ACCESSORS  (c, "tempo",
                           voidlinkaudiosend_get_tempo,
                           voidlinkaudiosend_set_tempo);

    // R/W mirror of session transport (isPlaying). Requires
    // enableStartStopSync(true) in the manager (which we do in the
    // constructor) for setIsPlaying() to actually propagate.
    CLASS_ATTR_LONG (c, "transport", 0, t_voidlinkaudiosend, a_transport);
    CLASS_ATTR_STYLE_LABEL(c, "transport", 0, "onoff", "Transport (play/stop)");
    CLASS_ATTR_ACCESSORS  (c, "transport",
                           voidlinkaudiosend_get_transport,
                           voidlinkaudiosend_set_transport);

    CLASS_ATTR_LONG (c, "poll",    0, t_voidlinkaudiosend, a_poll);
    CLASS_ATTR_STYLE_LABEL(c, "poll", 0, "onoff", "Auto-poll (publish retry + status)");
    CLASS_ATTR_ACCESSORS  (c, "poll", NULL, voidlinkaudiosend_set_poll);

    CLASS_ATTR_LONG (c, "pollinterval", 0, t_voidlinkaudiosend, a_pollinterval);
    CLASS_ATTR_LABEL(c, "pollinterval", 0, "Polling interval (ms)");
    CLASS_ATTR_FILTER_CLIP(c, "pollinterval", 50, 60000);
    CLASS_ATTR_ACCESSORS  (c, "pollinterval", NULL, voidlinkaudiosend_set_pollinterval);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    s_voidlinkaudiosend_class = c;

}


// ============================================================================
// Instance lifecycle
// ============================================================================

static void *
voidlinkaudiosend_new(t_symbol *s, long argc, t_atom *argv)
{
    t_voidlinkaudiosend *x = (t_voidlinkaudiosend *)object_alloc(s_voidlinkaudiosend_class);
    if (!x)
        return NULL;

    // Decide inlet count BEFORE dsp_setup() — by pre-scanning argv for @stereo.
    const long stereoArg = scan_stereo_arg(argc, argv, /*default=*/1);
    const long numInlets = (stereoArg ? 2 : 1);

    dsp_setup((t_pxobject *)x, (short)numInlets);

    // Outlet layout (left to right):
    //   [tempo~] [phase~] [transport~] [dumpout]
    // Order of outlet_new calls matters: rightmost first, leftmost last.
    x->dumpout = outlet_new((t_object *)x, NULL);     // dumpout (rightmost)
    outlet_new((t_object *)x, "signal");              // transport~
    outlet_new((t_object *)x, "signal");              // phase~
    outlet_new((t_object *)x, "signal");              // tempo~ (leftmost)

    // Default attribute values
    x->a_enable       = 1;
    x->a_stereo       = stereoArg;
    x->a_peer         = gensym("Max");
    x->a_channel      = gensym("Max Out");
    x->a_quantum      = 4.0;
    x->a_tempo        = 120.0;
    x->a_transport    = 0;
    x->a_poll         = 1;
    x->a_pollinterval = 250;

    // Heap-construct C++ impl
    x->impl = new (std::nothrow) VoidLinkAudioOutImpl();
    if (!x->impl)
    {
        object_error((t_object *)x, "void.linkaudio.send~: out of memory");
        return NULL;
    }

    x->impl->manager = LinkAudioManager::acquire();
    x->impl->quantum = x->a_quantum;
    x->impl->stereo  = (stereoArg != 0);

    x->poll_clock = clock_new((t_object *)x, (method)voidlinkaudiosend_tick);

    // Parse remaining @attributes (excluding @stereo which we already handled)
    attr_args_process(x, (short)argc, argv);

    apply_state(x);

    // Initial mirror sync: pull current session tempo/transport into our
    // attribute storage so a freshly-instantiated object reflects the live
    // session state. attrui observers may not be bound yet here, so we
    // don't call object_attr_touch — that happens shortly via the first
    // poll tick.
    if (x->impl->manager)
    {
        x->a_tempo     = x->impl->manager->tempo();
        x->a_transport = x->impl->manager->isPlaying() ? 1 : 0;
    }

    // Start polling if enabled. Schedule the first tick fast (50ms) instead
    // of waiting a full pollinterval, so attrui observers attached after
    // the patcher load get notified quickly via object_attr_touch.
    if (x->a_poll && x->poll_clock)
        clock_fdelay(x->poll_clock, 50.0);

    return x;
}

static void
voidlinkaudiosend_free(t_voidlinkaudiosend *x)
{
    dsp_free((t_pxobject *)x);

    if (x->poll_clock)
    {
        clock_unset(x->poll_clock);
        object_free(x->poll_clock);
        x->poll_clock = nullptr;
    }

    if (x->impl)
    {
        destroy_sink_internal(x);
        x->impl->manager.reset();
        delete x->impl;
        x->impl = nullptr;
    }
}

static void
voidlinkaudiosend_assist(t_voidlinkaudiosend *x, void *b, long io, long index, char *s)
{
    if (io == ASSIST_INLET)
    {
        if (index == 0)
            snprintf(s, 256, "(signal) audio L  /  messages: bang, info, attributes");
        else
            snprintf(s, 256, "(signal) audio R");
    }
    else
    {
        switch (index)
        {
            case 0: snprintf(s, 256, "(signal) tempo (BPM)");           break;
            case 1: snprintf(s, 256, "(signal) phase [0, quantum)");    break;
            case 2: snprintf(s, 256, "(signal) transport (0/1)");       break;
            case 3: snprintf(s, 256, "dumpout: status / dict");         break;
            default: snprintf(s, 256, "outlet");                        break;
        }
    }
}


// ============================================================================
// Attribute setters
// ============================================================================

static t_max_err
voidlinkaudiosend_set_enable(t_voidlinkaudiosend *x, t_object *attr, long argc, t_atom *argv)
{
    if (argc && argv)
        x->a_enable = (atom_getlong(argv) != 0) ? 1 : 0;
    if (x->impl) apply_state(x);
    return MAX_ERR_NONE;
}

static t_max_err
voidlinkaudiosend_set_peer(t_voidlinkaudiosend *x, t_object *attr, long argc, t_atom *argv)
{
    if (argc && argv)
        x->a_peer = atom_getsym(argv);
    if (x->impl) apply_state(x);
    return MAX_ERR_NONE;
}

static t_max_err
voidlinkaudiosend_set_channel(t_voidlinkaudiosend *x, t_object *attr, long argc, t_atom *argv)
{
    if (argc && argv)
        x->a_channel = atom_getsym(argv);
    if (x->impl) apply_state(x);
    return MAX_ERR_NONE;
}

static t_max_err
voidlinkaudiosend_set_quantum(t_voidlinkaudiosend *x, t_object *attr, long argc, t_atom *argv)
{
    if (argc && argv)
        x->a_quantum = atom_getfloat(argv);
    if (x->a_quantum < 1.0)  x->a_quantum = 1.0;
    if (x->a_quantum > 16.0) x->a_quantum = 16.0;
    if (x->impl) x->impl->quantum = x->a_quantum;
    return MAX_ERR_NONE;
}

// ----- Tempo ---------------------------------------------------------------
//
// Setter: store locally + push to session via manager (which uses
// captureAppSessionState/setTempo/commit). When called as part of the
// poll-tick mirror sync, skip propagation to avoid a feedback loop.
//
// Getter: always read live from the session, so [getattr tempo] / attrui /
// pattr show the actual current session tempo, not just whatever this
// object last wrote.

static t_max_err
voidlinkaudiosend_set_tempo(t_voidlinkaudiosend *x, t_object *attr, long argc, t_atom *argv)
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
voidlinkaudiosend_get_tempo(t_voidlinkaudiosend *x, t_object *attr, long *argc, t_atom **argv)
{
    char alloc;
    if (atom_alloc(argc, argv, &alloc) != MAX_ERR_NONE) return MAX_ERR_GENERIC;

    double bpm = x->a_tempo;
    if (x->impl && x->impl->manager)
        bpm = x->impl->manager->tempo();

    atom_setfloat(*argv, bpm);
    return MAX_ERR_NONE;
}

// ----- Transport -----------------------------------------------------------

static t_max_err
voidlinkaudiosend_set_transport(t_voidlinkaudiosend *x, t_object *attr, long argc, t_atom *argv)
{
    if (!argc || !argv) return MAX_ERR_NONE;
    long val = (atom_getlong(argv) != 0) ? 1 : 0;
    x->a_transport = val;
    if (x->impl && x->impl->manager && !x->impl->syncingFromSession)
        x->impl->manager->setIsPlaying(val != 0);
    return MAX_ERR_NONE;
}

static t_max_err
voidlinkaudiosend_get_transport(t_voidlinkaudiosend *x, t_object *attr, long *argc, t_atom **argv)
{
    char alloc;
    if (atom_alloc(argc, argv, &alloc) != MAX_ERR_NONE) return MAX_ERR_GENERIC;

    long val = x->a_transport;
    if (x->impl && x->impl->manager)
        val = x->impl->manager->isPlaying() ? 1 : 0;

    atom_setlong(*argv, val);
    return MAX_ERR_NONE;
}

static t_max_err
voidlinkaudiosend_set_poll(t_voidlinkaudiosend *x, t_object *attr, long argc, t_atom *argv)
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
voidlinkaudiosend_set_pollinterval(t_voidlinkaudiosend *x, t_object *attr, long argc, t_atom *argv)
{
    if (argc && argv)
        x->a_pollinterval = atom_getlong(argv);
    if (x->a_pollinterval < 50)    x->a_pollinterval = 50;
    if (x->a_pollinterval > 60000) x->a_pollinterval = 60000;
    return MAX_ERR_NONE;
}


// ============================================================================
// Polling clock
// ============================================================================

static void
poll_start(t_voidlinkaudiosend *x)
{
    if (!x->poll_clock) return;
    clock_fdelay(x->poll_clock, (double)x->a_pollinterval);
}

static void
poll_stop(t_voidlinkaudiosend *x)
{
    if (!x->poll_clock) return;
    clock_unset(x->poll_clock);
}

static void
voidlinkaudiosend_tick(t_voidlinkaudiosend *x)
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

    apply_state(x);
    if (x->a_poll && x->poll_clock)
        clock_fdelay(x->poll_clock, (double)x->a_pollinterval);
}


// ============================================================================
// Sink lifecycle
// ============================================================================

static void
destroy_sink_internal(t_voidlinkaudiosend *x)
{
    if (!x->impl) return;
    x->impl->sink.reset();
}


// ============================================================================
// State application
// ============================================================================

static void
apply_state(t_voidlinkaudiosend *x)
{
    if (!x->impl || !x->impl->manager)
        return;

    auto& la = x->impl->manager->linkAudio();

    // Peer name (advertised on network)
    const std::string newPeer = (x->a_peer && x->a_peer->s_name)
                                ? std::string(x->a_peer->s_name)
                                : std::string();
    if (!newPeer.empty() && newPeer != x->impl->currentPeerName)
    {
        x->impl->manager->setPeerName(newPeer);
        x->impl->currentPeerName = newPeer;
    }

    // Enable / disable
    const bool wantEnabled = (x->a_enable != 0);
    if (wantEnabled != x->impl->enabled)
    {
        la.enable(wantEnabled);
        la.enableLinkAudio(wantEnabled);
        x->impl->enabled = wantEnabled;
        if (!wantEnabled)
            destroy_sink_internal(x);
    }

    // Channel name change → recreate sink
    const std::string newCh = (x->a_channel && x->a_channel->s_name)
                              ? std::string(x->a_channel->s_name)
                              : std::string();
    if (newCh != x->impl->publishedChannel)
    {
        destroy_sink_internal(x);
        x->impl->publishedChannel = newCh;
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

    // Auto re-dump on meaningful state change
    const bool        nowPublishing        = (x->impl->sink != nullptr);
    const std::size_t nowNumPeers          = la.numPeers();
    const std::size_t nowNumAudioChannels  = la.channels().size();

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
        voidlinkaudiosend_info(x);
    }
}


// ============================================================================
// DSP
// ============================================================================

static inline int16_t
float_to_int16_clamped(double v)
{
    if (v >  1.0) v =  1.0;
    if (v < -1.0) v = -1.0;
    return static_cast<int16_t>(v * 32767.0);
}

static void
voidlinkaudiosend_dsp64(t_voidlinkaudiosend *x, t_object *dsp64, short *count,
                       double samplerate, long maxvectorsize, long flags)
{
    // Stash the current sample rate so the perform routine can pass it to
    // bh.commit(). Cast to double-as-pointer would be ugly, so we tuck it
    // into the impl.
    if (x->impl)
        x->impl->dspSampleRate = samplerate;

    // Z_NO_INPLACE is critical here: this object has 1-2 signal inlets
    // (audio to publish) AND 3 signal outlets (tempo~, phase~, transport~).
    // Without this flag, MSP is free to alias outs[i] onto ins[j] memory,
    // and our perform routine writes timing values into outs[] BEFORE
    // copying ins[] into the network buffer. With aliasing on, that
    // overwrites the audio inputs, so we publish tempo/phase to the network
    // instead of the actual audio. Receive~ doesn't have this issue because
    // it has 0 signal inlets.
    object_method(dsp64, gensym("dsp_add64"),
                  x, voidlinkaudiosend_perform64, Z_NO_INPLACE, NULL);
}

static void
voidlinkaudiosend_perform64(t_voidlinkaudiosend *x, t_object *dsp64,
                           double **ins, long numins,
                           double **outs, long numouts,
                           long sampleframes, long flags, void *userparam)
{
    const long n = sampleframes;

    // Outlet layout (left to right): tempo~, phase~, transport~
    double *outTempo     = (numouts > 0) ? outs[0] : nullptr;
    double *outPhase     = (numouts > 1) ? outs[1] : nullptr;
    double *outTransport = (numouts > 2) ? outs[2] : nullptr;

    // No manager / no DSP rate yet -> silence the timing outlets and bail.
    if (!x->impl || !x->impl->manager || x->impl->dspSampleRate <= 0.0)
    {
        if (outTempo)     std::memset(outTempo,     0, sizeof(double) * n);
        if (outPhase)     std::memset(outPhase,     0, sizeof(double) * n);
        if (outTransport) std::memset(outTransport, 0, sizeof(double) * n);
        return;
    }

    auto& la = x->impl->manager->linkAudio();

    // ========================================================================
    // STEP 1: Audio publish path.
    //
    // We do this FIRST, before writing anything to outs[], because in MSP
    // the runtime is allowed to alias an output buffer onto an input buffer
    // when in-place processing is permitted. Even though we set Z_NO_INPLACE
    // in dsp64, reading ins[] first + writing outs[] second is the
    // structurally correct order: it guarantees the published audio is the
    // actual audio inputs, not whatever we happened to put into outs[].
    // ========================================================================

    if (x->impl->enabled && x->impl->sink)
    {
        const bool        isStereo     = x->impl->stereo && (numins >= 2);
        const std::size_t numCh        = isStereo ? 2 : 1;
        const std::size_t totalSamples = static_cast<std::size_t>(n) * numCh;

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
            const double *srcL = (numins >= 1) ? ins[0] : nullptr;
            const double *srcR = (isStereo && numins >= 2) ? ins[1] : nullptr;

            if (srcL)
            {
                if (!isStereo)
                {
                    for (long i = 0; i < n; ++i)
                        bh.samples[i] = float_to_int16_clamped(srcL[i]);
                }
                else
                {
                    for (long i = 0; i < n; ++i)
                    {
                        bh.samples[2 * i + 0] = float_to_int16_clamped(srcL[i]);
                        bh.samples[2 * i + 1] = float_to_int16_clamped(srcR[i]);
                    }
                }

                // Commit the buffer with full Link timing context. bh.commit()
                // requires an app-thread session state, so we capture
                // AppSessionState here.
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
    }

    // ========================================================================
    // STEP 2: Fill timing outlets.
    //
    // Safe to do now; we've already finished reading ins[] above. Even if
    // outs[i] secretly shared memory with ins[j], the audio is already
    // serialized into the sink buffer and committed.
    // ========================================================================

    const auto   audioState  = la.captureAudioSessionState();
    const auto   microsBegin = la.clock().micros();

    const double tempoBpm       = audioState.tempo();
    const double quantum        = (x->impl->quantum > 0.0) ? x->impl->quantum : 4.0;
    const double beatBegin      = audioState.beatAtTime(microsBegin, quantum);
    const double sr             = x->impl->dspSampleRate;
    const double beatsPerSample = tempoBpm / 60.0 / sr;
    const double playingVal     = audioState.isPlaying() ? 1.0 : 0.0;

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
// ============================================================================

static void
voidlinkaudiosend_bang(t_voidlinkaudiosend *x)
{
    if (!x->impl) return;
    apply_state(x);
}

static void
voidlinkaudiosend_info(t_voidlinkaudiosend *x)
{
    if (!x->impl || !x->impl->manager) return;

    auto& la = x->impl->manager->linkAudio();
    const auto state    = la.captureAppSessionState();
    const auto now      = la.clock().micros();
    const auto channels = la.channels();

    t_dictionary *d = dictionary_new();
    if (!d) return;

    dictionary_appendlong  (d, gensym("enabled"),             x->impl->enabled ? 1 : 0);
    dictionary_appendlong  (d, gensym("audio_enabled"),       la.isLinkAudioEnabled() ? 1 : 0);
    dictionary_appendlong  (d, gensym("num_peers"),           (t_atom_long)la.numPeers());
    dictionary_appendlong  (d, gensym("publishing"),          x->impl->sink ? 1 : 0);
    dictionary_appendlong  (d, gensym("stereo"),              x->impl->stereo ? 1 : 0);
    dictionary_appendsym   (d, gensym("channel"),             x->a_channel ? x->a_channel : gensym(""));
    dictionary_appendsym   (d, gensym("peer"),                x->a_peer    ? x->a_peer    : gensym(""));
    dictionary_appendlong  (d, gensym("frames_published"),    (t_atom_long)x->impl->framesPublished.load());
    dictionary_appendlong  (d, gensym("frames_no_buffer"),    (t_atom_long)x->impl->framesNoBuffer.load());
    dictionary_appendlong  (d, gensym("frames_commit_fail"),  (t_atom_long)x->impl->framesCommitFail.load());
    dictionary_appendfloat (d, gensym("tempo"),               state.tempo());
    dictionary_appendlong  (d, gensym("transport"),           state.isPlaying() ? 1 : 0);
    dictionary_appendfloat (d, gensym("beat"),                state.beatAtTime(now, x->impl->quantum));
    dictionary_appendfloat (d, gensym("phase"),               state.phaseAtTime(now, x->impl->quantum));
    dictionary_appendfloat (d, gensym("quantum"),             x->impl->quantum);
    dictionary_appendlong  (d, gensym("poll"),                x->a_poll);
    dictionary_appendlong  (d, gensym("pollinterval"),        x->a_pollinterval);
    dictionary_appendlong  (d, gensym("num_audio_channels"),  (t_atom_long)channels.size());

    // Sub-dict listing all channels visible on the network (incl. ours)
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
        object_free(d);
    }
}
