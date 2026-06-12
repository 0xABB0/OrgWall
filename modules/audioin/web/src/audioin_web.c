#include <audioin/provider.h>

#include <core/platform.h>

#if !MEL_PLATFORM_EMSCRIPTEN
#error "audioin_web.c is for the emscripten runtime only"
#endif

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>

#include <emscripten.h>
#include <emscripten/em_js.h>
#include <emscripten/webaudio.h>

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#define MEL_AUDIOIN_WEB_WORKLET_STACK (1u << 16)

typedef struct
{
    u32              count;
    Mel_AudioIn_Sink sinks[];
} Sink_List;

typedef struct
{
    u32                             id;
    str8                            stable_id;
    EMSCRIPTEN_WEBAUDIO_T           context;
    EMSCRIPTEN_AUDIO_WORKLET_NODE_T node;
    void*                           worklet_stack;
    f32*                            interleave;
    u32                             interleave_channels;
    u32                             quantum;
    u32                             samplerate;
    Mel_AudioIn_Processing          requested;
    Mel_AudioIn_Processing          actual;
    bool                            actual_known;
    _Atomic(void*)                  sinks;
    _Atomic(u32)                    dropped;
    Mel_Array(void*) garbage;
} Capture;

typedef struct
{
    str8 stable_id;
    str8 name;
} Web_Device;

typedef Mel_Array(Web_Device) Device_Array;
typedef Mel_Array(Capture*) Capture_Array;
typedef Mel_Array(Mel_AudioIn_Sink) Auth_Sink_Array;

static struct
{
    bool                 registered;
    const Mel_Alloc*     alloc;
    Mel_AudioIn_Provider provider;
    Device_Array         devices;
    Capture_Array        captures;
    Auth_Sink_Array      auth_sinks;
    u32                  capture_seq;
    u32                  default_rate;
    bool                 auth_inflight;
} g_in;

EM_JS(void, audioin_web__js_init, (void), {
    if (globalThis.MelAIn)
        return;
    var S = { snap : [], open : {}, perm : "", permst : null, granted : false, rate : 0 };
    globalThis.MelAIn = S;
    try
    {
        var probe = new AudioContext();
        S.rate = probe.sampleRate | 0;
        probe.close();
    }
    catch (e)
    {
    }
    S.refresh = function()
    {
        if (typeof navigator == 'undefined' || !navigator.mediaDevices || !navigator.mediaDevices.enumerateDevices)
        {
            S.snap = [];
            _mel_audioin_web__on_enum(0);
            return;
        }
        navigator.mediaDevices.enumerateDevices()
            .then(function(list) {
                S.snap = list.filter(function(d) { return d.kind == 'audioinput'; });
                _mel_audioin_web__on_enum(S.snap.length);
            })
            .catch(function() {
                S.snap = [];
                _mel_audioin_web__on_enum(0);
            });
    };
    if (typeof navigator != 'undefined' && navigator.mediaDevices && navigator.mediaDevices.addEventListener)
        navigator.mediaDevices.addEventListener('devicechange', S.refresh);
    if (typeof navigator != 'undefined' && navigator.permissions && navigator.permissions.query)
    {
        navigator.permissions.query({ name : 'microphone' })
            .then(function(st) {
                S.permst = st;
                S.perm = st.state;
                st.onchange = function() { S.perm = st.state; };
            })
            .catch(function(){});
    }
});

EM_JS(void, audioin_web__js_shutdown, (void), {
    var S = globalThis.MelAIn;
    if (!S)
        return;
    if (typeof navigator != 'undefined' && navigator.mediaDevices && navigator.mediaDevices.removeEventListener)
        navigator.mediaDevices.removeEventListener('devicechange', S.refresh);
    if (S.permst)
        S.permst.onchange = null;
    globalThis.MelAIn = undefined;
});

EM_JS(void, audioin_web__js_refresh, (void), {
    if (globalThis.MelAIn)
        MelAIn.refresh();
});

EM_JS(int, audioin_web__js_rate, (void), { return globalThis.MelAIn ? MelAIn.rate : 0; });

EM_JS(int, audioin_web__js_snap_id_len, (int idx), {
    var d = globalThis.MelAIn && MelAIn.snap[idx];
    return d && d.deviceId ? lengthBytesUTF8(d.deviceId) : 0;
});

EM_JS(void, audioin_web__js_snap_id, (int idx, char* buf, int cap), {
    var d = globalThis.MelAIn && MelAIn.snap[idx];
    stringToUTF8(d && d.deviceId ? d.deviceId : "", buf, cap);
});

EM_JS(int, audioin_web__js_snap_label_len, (int idx), {
    var d = globalThis.MelAIn && MelAIn.snap[idx];
    return d && d.label ? lengthBytesUTF8(d.label) : 0;
});

EM_JS(void, audioin_web__js_snap_label, (int idx, char* buf, int cap), {
    var d = globalThis.MelAIn && MelAIn.snap[idx];
    stringToUTF8(d && d.label ? d.label : "", buf, cap);
});

EM_JS(int, audioin_web__js_auth_state, (void), {
    var S = globalThis.MelAIn;
    if (!S)
        return 0;
    if (S.granted || S.perm == 'granted')
        return 1;
    if (S.perm == 'denied')
        return 2;
    return 0;
});

EM_JS(void, audioin_web__js_authorize, (void), {
    var S = globalThis.MelAIn;
    if (typeof navigator == 'undefined' || !navigator.mediaDevices || !navigator.mediaDevices.getUserMedia)
    {
        _mel_audioin_web__on_auth(2);
        return;
    }
    navigator.mediaDevices.getUserMedia({ audio : true })
        .then(function(stream) {
            stream.getTracks().forEach(function(t) { t.stop(); });
            S.granted = true;
            S.refresh();
            _mel_audioin_web__on_auth(1);
        })
        .catch(function(e) {
            var denied = e && (e.name == 'NotAllowedError' || e.name == 'SecurityError');
            _mel_audioin_web__on_auth(denied ? 0 : 2);
        });
});

EM_JS(int, audioin_web__js_open, (unsigned id, const char* device_id, int len, int ec, int ns, int agc), {
    var S = globalThis.MelAIn;
    if (!S || typeof navigator == 'undefined' || !navigator.mediaDevices || !navigator.mediaDevices.getUserMedia)
        return 0;
    var did = UTF8ToString(device_id, len);
    S.open[id] = { stream : null, src : null };
    navigator.mediaDevices.getUserMedia({ audio : { deviceId : { exact : did }, echoCancellation : !!ec, noiseSuppression : !!ns, autoGainControl : !!agc } })
        .then(function(stream) {
            var st = S.open[id];
            if (!st)
            {
                stream.getTracks().forEach(function(t) { t.stop(); });
                return;
            }
            S.granted = true;
            st.stream = stream;
            var tracks = stream.getAudioTracks();
            var settings = (tracks.length && tracks[0].getSettings) ? tracks[0].getSettings() : {};
            var ch = settings.channelCount | 0;
            var flag = function(v) { return typeof v == 'undefined' ? -1 : (v ? 1 : 0); };
            tracks.forEach(function(t) { t.addEventListener('ended', function() { _mel_audioin_web__on_ended(id); }); });
            S.refresh();
            _mel_audioin_web__on_media(id, 1, ch, flag(settings.echoCancellation), flag(settings.noiseSuppression), flag(settings.autoGainControl));
        })
        .catch(function(e) {
            delete S.open[id];
            _mel_audioin_web__on_media(id, 0, 0, -1, -1, -1);
        });
    return 1;
});

EM_JS(int, audioin_web__js_connect, (unsigned id, int ctx_handle, int node_handle), {
    var S = globalThis.MelAIn;
    var st = S && S.open[id];
    if (!st || !st.stream)
        return 0;
    var ctx = emscriptenGetAudioObject(ctx_handle);
    var node = emscriptenGetAudioObject(node_handle);
    if (!ctx || !node)
        return 0;
    st.src = ctx.createMediaStreamSource(st.stream);
    st.src.connect(node);
    return 1;
});

EM_JS(void, audioin_web__js_close, (unsigned id), {
    var S = globalThis.MelAIn;
    var st = S && S.open[id];
    if (!st)
        return;
    if (st.src)
        st.src.disconnect();
    if (st.stream)
        st.stream.getTracks().forEach(function(t) { t.stop(); });
    delete S.open[id];
});

static Capture* capture_find(str8 stable_id)
{
    for (usize i = 0; i < g_in.captures.count; i++)
        if (str8_equals(g_in.captures.items[i]->stable_id, stable_id))
            return g_in.captures.items[i];
    return NULL;
}

static Capture* capture_by_id(u32 id)
{
    for (usize i = 0; i < g_in.captures.count; i++)
        if (g_in.captures.items[i]->id == id)
            return g_in.captures.items[i];
    return NULL;
}

static void capture_sinks_swap(Capture* c, Sink_List* nl)
{
    void* old = atomic_exchange_explicit(&c->sinks, nl, memory_order_acq_rel);
    if (old)
        mel_array_push(&c->garbage, old);
}

static bool capture_sink_add(Capture* c, Mel_AudioIn_Sink sink)
{
    Sink_List* cur = atomic_load_explicit(&c->sinks, memory_order_acquire);
    u32        count = cur ? cur->count : 0;
    Sink_List* nl = mel_alloc(g_in.alloc, sizeof(Sink_List) + sizeof(Mel_AudioIn_Sink) * ((usize)count + 1u));
    if (!nl)
        return false;
    for (u32 i = 0; i < count; i++)
        nl->sinks[i] = cur->sinks[i];
    nl->sinks[count] = sink;
    nl->count = count + 1u;
    capture_sinks_swap(c, nl);
    return true;
}

static void capture_destroy(Capture* c, bool lost)
{
    audioin_web__js_close(c->id);
    if (c->node > 0)
        emscripten_destroy_web_audio_node(c->node);
    if (c->context > 0)
        emscripten_destroy_audio_context(c->context);

    u32 dropped = atomic_load_explicit(&c->dropped, memory_order_relaxed);
    if (dropped > 0)
        mel_log_warn("audioin", "web: capture %.*s dropped %u blocks (input exceeded %u allocated channels)", (int)c->stable_id.len, c->stable_id.data, dropped, c->interleave_channels);

    Sink_List* sl = atomic_exchange_explicit(&c->sinks, NULL, memory_order_acq_rel);
    if (sl)
    {
        if (lost)
            for (u32 i = 0; i < sl->count; i++)
                if (sl->sinks[i].on_lost)
                    sl->sinks[i].on_lost(sl->sinks[i].token);
        mel_dealloc(g_in.alloc, sl);
    }
    for (usize i = 0; i < c->garbage.count; i++)
        mel_dealloc(g_in.alloc, c->garbage.items[i]);
    mel_array_free(&c->garbage);

    if (c->interleave)
        mel_dealloc(g_in.alloc, c->interleave);
    if (c->worklet_stack)
        mel_dealloc(g_in.alloc, c->worklet_stack);
    if (c->stable_id.data)
        mel_dealloc(g_in.alloc, c->stable_id.data);

    for (usize i = 0; i < g_in.captures.count; i++)
        if (g_in.captures.items[i] == c)
        {
            mel_array_remove_unordered(&g_in.captures, i);
            break;
        }
    mel_dealloc(g_in.alloc, c);
}

static bool capture_process(int num_inputs, const AudioSampleFrame* inputs, int num_outputs, AudioSampleFrame* outputs, int num_params, const AudioParamFrame* params, void* user)
{
    MEL_UNUSED(num_params);
    MEL_UNUSED(params);
    Capture* c = (Capture*)user;

    for (int o = 0; o < num_outputs; o++)
        memset(outputs[o].data, 0, sizeof(f32) * (usize)outputs[o].numberOfChannels * (usize)outputs[o].samplesPerChannel);

    if (num_inputs < 1)
        return true;
    const AudioSampleFrame* in = &inputs[0];
    u32                     channels = (u32)in->numberOfChannels;
    u32                     frames = (u32)in->samplesPerChannel;
    if (channels == 0u || frames == 0u)
        return true;
    if (c->interleave == NULL || channels > c->interleave_channels || frames > c->quantum)
    {
        atomic_fetch_add_explicit(&c->dropped, 1u, memory_order_relaxed);
        return true;
    }

    for (u32 ch = 0; ch < channels; ch++)
    {
        const f32* plane = in->data + (usize)ch * frames;
        for (u32 i = 0; i < frames; i++)
            c->interleave[(usize)i * channels + ch] = plane[i];
    }

    Sink_List* sl = atomic_load_explicit(&c->sinks, memory_order_acquire);
    if (sl)
        for (u32 i = 0; i < sl->count; i++)
            if (sl->sinks[i].on_frames)
                sl->sinks[i].on_frames(sl->sinks[i].token, c->interleave, frames, c->samplerate, channels, 0u);
    return true;
}

static void capture_processor_created(EMSCRIPTEN_WEBAUDIO_T context, bool success, void* user)
{
    Capture* c = capture_by_id((u32)(uintptr_t)user);
    if (!c)
        return;
    if (!success)
    {
        mel_log_error("audioin", "web: AudioWorkletProcessor creation failed for %.*s; stream lost", (int)c->stable_id.len, c->stable_id.data);
        capture_destroy(c, true);
        return;
    }

    int                                     output_channels[1] = { 1 };
    EmscriptenAudioWorkletNodeCreateOptions opts = {
        .numberOfInputs = 1,
        .numberOfOutputs = 1,
        .outputChannelCounts = output_channels,
    };
    c->node = emscripten_create_wasm_audio_worklet_node(context, "mel-audioin-capture", &opts, capture_process, c);
    if (c->node <= 0)
    {
        mel_log_error("audioin", "web: AudioWorkletNode creation failed for %.*s; stream lost", (int)c->stable_id.len, c->stable_id.data);
        capture_destroy(c, true);
        return;
    }
    emscripten_audio_node_connect(c->node, context, 0, 0);
    emscripten_resume_audio_context_sync(context);
    if (!audioin_web__js_connect(c->id, context, c->node))
    {
        mel_log_error("audioin", "web: MediaStream source connection failed for %.*s; stream lost", (int)c->stable_id.len, c->stable_id.data);
        capture_destroy(c, true);
        return;
    }
    mel_log_info("audioin", "web: capture running for %.*s (%u Hz, quantum %u, %u ch buffer)", (int)c->stable_id.len, c->stable_id.data, c->samplerate, c->quantum, c->interleave_channels);
}

static void capture_worklet_ready(EMSCRIPTEN_WEBAUDIO_T context, bool success, void* user)
{
    Capture* c = capture_by_id((u32)(uintptr_t)user);
    if (!c)
        return;
    if (!success)
    {
        mel_log_error("audioin", "web: AudioWorklet thread creation failed for %.*s; stream lost", (int)c->stable_id.len, c->stable_id.data);
        capture_destroy(c, true);
        return;
    }
    WebAudioWorkletProcessorCreateOptions popts = {
        .name = "mel-audioin-capture",
    };
    emscripten_create_wasm_audio_worklet_processor_async(context, &popts, capture_processor_created, user);
}

EMSCRIPTEN_KEEPALIVE void mel_audioin_web__on_media(unsigned id, int ok, int channels, int ec, int ns, int agc)
{
    Capture* c = capture_by_id((u32)id);
    if (!c)
        return;
    if (!ok)
    {
        mel_log_error("audioin", "web: getUserMedia rejected for %.*s (permission denied, device gone, or busy); stream lost", (int)c->stable_id.len, c->stable_id.data);
        capture_destroy(c, true);
        return;
    }
    c->actual = (Mel_AudioIn_Processing){ .echo_cancellation = ec == 1, .noise_suppression = ns == 1, .auto_gain = agc == 1 };
    c->actual_known = true;
    if (ec < 0 || ns < 0 || agc < 0)
        mel_log_warn("audioin", "web: track settings omit processing flags for %.*s; unknown reported as off", (int)c->stable_id.len, c->stable_id.data);
    mel_log_info("audioin",
                 "web: %.*s settings resolved: ec=%d ns=%d agc=%d (requested %d/%d/%d)",
                 (int)c->stable_id.len,
                 c->stable_id.data,
                 ec == 1,
                 ns == 1,
                 agc == 1,
                 c->requested.echo_cancellation,
                 c->requested.noise_suppression,
                 c->requested.auto_gain);
    c->interleave_channels = channels > 0 ? (u32)channels : 2u;
    c->interleave = mel_alloc(g_in.alloc, sizeof(f32) * (usize)c->quantum * c->interleave_channels);
    c->worklet_stack = mel_calloc(g_in.alloc, MEL_AUDIOIN_WEB_WORKLET_STACK);
    if (!c->interleave || !c->worklet_stack)
    {
        mel_log_error("audioin", "web: capture buffer allocation failed for %.*s; stream lost", (int)c->stable_id.len, c->stable_id.data);
        capture_destroy(c, true);
        return;
    }
    emscripten_start_wasm_audio_worklet_thread_async(c->context, c->worklet_stack, MEL_AUDIOIN_WEB_WORKLET_STACK, capture_worklet_ready, (void*)(uintptr_t)c->id);
}

EMSCRIPTEN_KEEPALIVE void mel_audioin_web__on_ended(unsigned id)
{
    Capture* c = capture_by_id((u32)id);
    if (!c)
        return;
    mel_log_warn("audioin", "web: MediaStreamTrack ended for %.*s; stream lost", (int)c->stable_id.len, c->stable_id.data);
    capture_destroy(c, true);
}

static void devices_free(Device_Array* arr)
{
    for (usize i = 0; i < arr->count; i++)
    {
        if (arr->items[i].stable_id.data)
            mel_dealloc(g_in.alloc, arr->items[i].stable_id.data);
        if (arr->items[i].name.data)
            mel_dealloc(g_in.alloc, arr->items[i].name.data);
    }
    mel_array_free(arr);
}

EMSCRIPTEN_KEEPALIVE void mel_audioin_web__on_enum(int count)
{
    if (!g_in.registered)
        return;

    Device_Array fresh;
    mel_array_init(&fresh, g_in.alloc);
    for (int i = 0; i < count; i++)
    {
        Web_Device d = { 0 };
        int        id_len = audioin_web__js_snap_id_len(i);
        if (id_len > 0)
        {
            u8* data = mel_alloc(g_in.alloc, (usize)id_len + 5);
            if (!data)
                continue;
            memcpy(data, "web:", 4);
            audioin_web__js_snap_id(i, (char*)data + 4, id_len + 1);
            d.stable_id = (str8){ data, (size)id_len + 4 };
        }
        else
            d.stable_id = str8_fmt(g_in.alloc, "web:pending-%d", i);

        int label_len = audioin_web__js_snap_label_len(i);
        if (label_len > 0)
        {
            u8* data = mel_alloc(g_in.alloc, (usize)label_len + 1);
            if (data)
            {
                audioin_web__js_snap_label(i, (char*)data, label_len + 1);
                d.name = (str8){ data, (size)label_len };
            }
        }
        mel_array_push(&fresh, d);
    }

    bool changed = fresh.count != g_in.devices.count;
    for (usize i = 0; !changed && i < fresh.count; i++)
    {
        bool found = false;
        for (usize j = 0; j < g_in.devices.count && !found; j++)
            found = str8_equals(fresh.items[i].stable_id, g_in.devices.items[j].stable_id) && str8_equals(fresh.items[i].name, g_in.devices.items[j].name);
        changed = !found;
    }

    devices_free(&g_in.devices);
    g_in.devices = fresh;

    if (count == 0)
        mel_log_warn("audioin", "web: enumerateDevices yielded zero audio inputs (unsupported context or no devices)");
    if (changed)
        mel_audioin_provider_notify(g_in.provider);
}

EMSCRIPTEN_KEEPALIVE void mel_audioin_web__on_auth(int code)
{
    g_in.auth_inflight = false;
    const mel_audioin_auth* a = code == 1 ? &mel_audioin_auth_granted : code == 0 ? &mel_audioin_auth_denied : &mel_audioin_auth_not_determined;
    if (code != 1)
        mel_log_warn("audioin", "web: microphone probe resolved %s", mel_audioin_auth_name(a));

    Auth_Sink_Array pending = g_in.auth_sinks;
    mel_array_init(&g_in.auth_sinks, g_in.alloc);
    for (usize i = 0; i < pending.count; i++)
        pending.items[i].on_auth(pending.items[i].token, a);
    mel_array_free(&pending);
}

static void web_enumerate(void* user, Mel_AudioIn_Enum_Fn fn, void* fn_user)
{
    MEL_UNUSED(user);
    for (usize i = 0; i < g_in.devices.count; i++)
    {
        Web_Device*     d = &g_in.devices.items[i];
        u32             rate = g_in.default_rate;
        Mel_AudioIn_Raw raw = {
            .stable_id = d->stable_id,
            .name = d->name,
            .kind = &mel_audioin_unknown,
            .channels = 1,
            .samplerate = rate,
            .samplerates = rate > 0 ? &rate : NULL,
            .samplerate_count = rate > 0 ? 1u : 0u,
            .caps = { .gain = false },
        };
        if (!fn(&raw, fn_user))
            break;
    }
    audioin_web__js_refresh();
}

static str8 web_default_id(void* user)
{
    MEL_UNUSED(user);
    for (usize i = 0; i < g_in.devices.count; i++)
        if (str8_equals(g_in.devices.items[i].stable_id, S8("web:default")))
            return g_in.devices.items[i].stable_id;
    return g_in.devices.count > 0 ? g_in.devices.items[0].stable_id : STR8_EMPTY;
}

static bool processing_equal(Mel_AudioIn_Processing a, Mel_AudioIn_Processing b) { return a.echo_cancellation == b.echo_cancellation && a.noise_suppression == b.noise_suppression && a.auto_gain == b.auto_gain; }

static Mel_AudioIn_Status web_open(void* user, str8 stable_id, Mel_AudioIn_Sink sink, Mel_AudioIn_Open_Opt opt, Mel_AudioIn_Granted* granted)
{
    MEL_UNUSED(user);
    if (granted)
        *granted = (Mel_AudioIn_Granted){ 0 };
    if (opt.exclusive)
        mel_log_warn("audioin", "web: exclusive capture requested for %.*s; the web has no exclusive surface, granted shared", (int)stable_id.len, stable_id.data);
    Capture* existing = capture_find(stable_id);
    if (existing)
    {
        if (!processing_equal(opt.processing, existing->requested))
            mel_log_warn("audioin", "web: open %.*s: processing options differ from the live stream; existing stream settings win", (int)stable_id.len, stable_id.data);
        if (!capture_sink_add(existing, sink))
            return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
        if (granted && existing->actual_known)
            granted->processing = existing->actual;
        return MEL_AUDIOIN_OK;
    }

    if (str8_starts_with(stable_id, S8("web:pending-")))
    {
        mel_log_error("audioin", "web: open %.*s: deviceId withheld until consent; call mel_audioin_authorize first", (int)stable_id.len, stable_id.data);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_DENIED;
    }
    if (!str8_starts_with(stable_id, S8("web:")))
    {
        mel_log_error("audioin", "web: open %.*s: not a web provider id", (int)stable_id.len, stable_id.data);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_NO_DEVICE;
    }

    Capture* c = mel_alloc_type(g_in.alloc, Capture);
    if (!c)
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    memset(c, 0, sizeof *c);
    c->id = ++g_in.capture_seq;
    c->stable_id = str8_dup(stable_id, g_in.alloc);
    mel_array_init(&c->garbage, g_in.alloc);
    atomic_store_explicit(&c->sinks, NULL, memory_order_relaxed);

    c->context = emscripten_create_audio_context(NULL);
    if (c->context <= 0)
    {
        mel_log_error("audioin", "web: AudioContext creation failed for %.*s", (int)stable_id.len, stable_id.data);
        mel_dealloc(g_in.alloc, c->stable_id.data);
        mel_dealloc(g_in.alloc, c);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }
    int rate = emscripten_audio_context_sample_rate(c->context);
    int quantum = emscripten_audio_context_quantum_size(c->context);
    if (rate <= 0 || quantum <= 0)
    {
        mel_log_error("audioin", "web: AudioContext reported rate %d / quantum %d for %.*s", rate, quantum, (int)stable_id.len, stable_id.data);
        emscripten_destroy_audio_context(c->context);
        mel_dealloc(g_in.alloc, c->stable_id.data);
        mel_dealloc(g_in.alloc, c);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }
    c->samplerate = (u32)rate;
    c->quantum = (u32)quantum;
    c->requested = opt.processing;

    if (!capture_sink_add(c, sink))
    {
        emscripten_destroy_audio_context(c->context);
        mel_dealloc(g_in.alloc, c->stable_id.data);
        mel_dealloc(g_in.alloc, c);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }
    mel_array_push(&g_in.captures, c);

    if (!audioin_web__js_open(c->id, (const char*)c->stable_id.data + 4, (int)(c->stable_id.len - 4), opt.processing.echo_cancellation ? 1 : 0, opt.processing.noise_suppression ? 1 : 0, opt.processing.auto_gain ? 1 : 0))
    {
        mel_log_error("audioin", "web: getUserMedia unavailable; cannot open %.*s", (int)stable_id.len, stable_id.data);
        capture_destroy(c, false);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }
    mel_log_info("audioin", "web: open %.*s: processing grants resolve asynchronously; reported off until track settings arrive", (int)stable_id.len, stable_id.data);
    return MEL_AUDIOIN_OK;
}

static void web_close(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    Capture* c = capture_find(stable_id);
    if (!c)
        return;

    Sink_List* cur = atomic_load_explicit(&c->sinks, memory_order_acquire);
    u32        count = cur ? cur->count : 0;
    Sink_List* nl = mel_alloc(g_in.alloc, sizeof(Sink_List) + sizeof(Mel_AudioIn_Sink) * (usize)count);
    if (!nl)
        return;
    u32 kept = 0;
    for (u32 i = 0; i < count; i++)
        if (cur->sinks[i].token != token)
            nl->sinks[kept++] = cur->sinks[i];
    nl->count = kept;

    if (kept == 0)
    {
        mel_dealloc(g_in.alloc, nl);
        capture_destroy(c, false);
        return;
    }
    capture_sinks_swap(c, nl);
}

static const mel_audioin_auth* web_authorization(void* user)
{
    MEL_UNUSED(user);
    int code = audioin_web__js_auth_state();
    if (code == 1)
        return &mel_audioin_auth_granted;
    if (code == 2)
        return &mel_audioin_auth_denied;
    return &mel_audioin_auth_not_determined;
}

static void web_authorize(void* user, Mel_AudioIn_Sink sink)
{
    MEL_UNUSED(user);
    if (sink.on_auth == NULL)
        return;
    mel_array_push(&g_in.auth_sinks, sink);
    if (!g_in.auth_inflight)
    {
        g_in.auth_inflight = true;
        audioin_web__js_authorize();
    }
}

static void* web_native(void* user, str8 stable_id)
{
    MEL_UNUSED(user);
    return capture_find(stable_id);
}

static void web_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    while (g_in.captures.count > 0)
        capture_destroy(g_in.captures.items[g_in.captures.count - 1], true);

    if (g_in.auth_sinks.count > 0)
    {
        mel_log_warn("audioin", "web: %u authorize probe(s) unresolved at shutdown", (u32)g_in.auth_sinks.count);
        for (usize i = 0; i < g_in.auth_sinks.count; i++)
            g_in.auth_sinks.items[i].on_auth(g_in.auth_sinks.items[i].token, &mel_audioin_auth_not_determined);
    }
    mel_array_free(&g_in.auth_sinks);
    devices_free(&g_in.devices);
    mel_array_free(&g_in.captures);
    audioin_web__js_shutdown();
    memset(&g_in, 0, sizeof g_in);
}

void mel_audioin__register_host_providers(void)
{
    g_in.alloc = mel_alloc_heap();
    mel_array_init(&g_in.devices, g_in.alloc);
    mel_array_init(&g_in.captures, g_in.alloc);
    mel_array_init(&g_in.auth_sinks, g_in.alloc);
    g_in.capture_seq = 0;
    g_in.auth_inflight = false;

    audioin_web__js_init();
    int rate = audioin_web__js_rate();
    g_in.default_rate = rate > 0 ? (u32)rate : 0u;
    if (g_in.default_rate == 0)
        mel_log_warn("audioin", "web: AudioContext unavailable; descriptor samplerate unknown until open");

    static const Mel_AudioIn_Provider_Desc desc = {
        .name = "web-mediadevices",
        .enumerate = web_enumerate,
        .default_id = web_default_id,
        .open = web_open,
        .close = web_close,
        .authorization = web_authorization,
        .authorize = web_authorize,
        .native = web_native,
        .shutdown = web_shutdown,
    };
    g_in.provider = mel_audioin_provider_register(&desc);
    g_in.registered = true;
    audioin_web__js_refresh();
}
