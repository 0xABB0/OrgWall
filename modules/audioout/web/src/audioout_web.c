#include <audioout/provider.h>

#include <core/platform.h>

#if !MEL_PLATFORM_EMSCRIPTEN
#error "audioout_web.c is for the emscripten runtime only"
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

#define MEL_AUDIOOUT_WEB_WORKLET_STACK (1u << 16)
#define MEL_AUDIOOUT_WEB_CHANNELS      2u

typedef struct
{
    Mel_AudioOut_Source src;
    bool                started;
} Web_Open;

typedef struct
{
    u32      count;
    Web_Open opens[];
} Open_List;

typedef struct
{
    u32                             id;
    str8                            stable_id;
    EMSCRIPTEN_WEBAUDIO_T           context;
    EMSCRIPTEN_AUDIO_WORKLET_NODE_T node;
    void*                           worklet_stack;
    f32*                            scratch;
    u32                             channels;
    u32                             quantum;
    u32                             samplerate;
    bool                            engine_running;
    _Atomic(void*)                  opens;
    _Atomic(u32)                    dropped;
    Mel_Array(void*) garbage;
} Playback;

typedef struct
{
    str8 stable_id;
    str8 name;
} Web_Device;

typedef Mel_Array(Web_Device) Device_Array;
typedef Mel_Array(Playback*) Playback_Array;

static struct
{
    bool                  registered;
    const Mel_Alloc*      alloc;
    Mel_AudioOut_Provider provider;
    Device_Array          devices;
    Playback_Array        playbacks;
    u32                   playback_seq;
    u32                   default_rate;
    bool                  has_sinkid;
} g_out;

EM_JS(void, audioout_web__js_init, (void), {
    if (globalThis.MelAOut)
        return;
    var S = { snap : [], rate : 0, sink : 0 };
    globalThis.MelAOut = S;
    if (typeof AudioContext != 'undefined')
    {
        if (typeof AudioContext.prototype.setSinkId == 'function')
            S.sink = 1;
        try
        {
            var probe = new AudioContext();
            S.rate = probe.sampleRate | 0;
            probe.close();
        }
        catch (e)
        {
        }
    }
    S.refresh = function()
    {
        if (!S.sink || typeof navigator == 'undefined' || !navigator.mediaDevices || !navigator.mediaDevices.enumerateDevices)
        {
            S.snap = [];
            _mel_audioout_web__on_enum(0);
            return;
        }
        navigator.mediaDevices.enumerateDevices()
            .then(function(list) {
                S.snap = list.filter(function(d) { return d.kind == 'audiooutput'; });
                _mel_audioout_web__on_enum(S.snap.length);
            })
            .catch(function() {
                S.snap = [];
                _mel_audioout_web__on_enum(0);
            });
    };
    if (S.sink && typeof navigator != 'undefined' && navigator.mediaDevices && navigator.mediaDevices.addEventListener)
        navigator.mediaDevices.addEventListener('devicechange', S.refresh);
});

EM_JS(void, audioout_web__js_shutdown, (void), {
    var S = globalThis.MelAOut;
    if (!S)
        return;
    if (typeof navigator != 'undefined' && navigator.mediaDevices && navigator.mediaDevices.removeEventListener)
        navigator.mediaDevices.removeEventListener('devicechange', S.refresh);
    globalThis.MelAOut = undefined;
});

EM_JS(void, audioout_web__js_refresh, (void), {
    if (globalThis.MelAOut)
        MelAOut.refresh();
});

EM_JS(int, audioout_web__js_rate, (void), { return globalThis.MelAOut ? MelAOut.rate : 0; });

EM_JS(int, audioout_web__js_sink_supported, (void), { return globalThis.MelAOut ? MelAOut.sink : 0; });

EM_JS(int, audioout_web__js_snap_id_len, (int idx), {
    var d = globalThis.MelAOut && MelAOut.snap[idx];
    return d && d.deviceId ? lengthBytesUTF8(d.deviceId) : 0;
});

EM_JS(void, audioout_web__js_snap_id, (int idx, char* buf, int cap), {
    var d = globalThis.MelAOut && MelAOut.snap[idx];
    stringToUTF8(d && d.deviceId ? d.deviceId : "", buf, cap);
});

EM_JS(int, audioout_web__js_snap_label_len, (int idx), {
    var d = globalThis.MelAOut && MelAOut.snap[idx];
    return d && d.label ? lengthBytesUTF8(d.label) : 0;
});

EM_JS(void, audioout_web__js_snap_label, (int idx, char* buf, int cap), {
    var d = globalThis.MelAOut && MelAOut.snap[idx];
    stringToUTF8(d && d.label ? d.label : "", buf, cap);
});

EM_JS(void, audioout_web__js_set_sink, (unsigned id, int ctx_handle, const char* device_id, int len), {
    var ctx = emscriptenGetAudioObject(ctx_handle);
    if (!ctx || typeof ctx.setSinkId != 'function')
    {
        _mel_audioout_web__on_sink(id, 0);
        return;
    }
    ctx.setSinkId(UTF8ToString(device_id, len)).then(function() { _mel_audioout_web__on_sink(id, 1); }).catch(function(e) { _mel_audioout_web__on_sink(id, 0); });
});

EM_JS(void, audioout_web__js_suspend, (int ctx_handle), {
    var ctx = emscriptenGetAudioObject(ctx_handle);
    if (ctx && ctx.state == 'running')
        ctx.suspend();
});

static Playback* playback_find(str8 stable_id)
{
    for (usize i = 0; i < g_out.playbacks.count; i++)
        if (str8_equals(g_out.playbacks.items[i]->stable_id, stable_id))
            return g_out.playbacks.items[i];
    return NULL;
}

static Playback* playback_by_id(u32 id)
{
    for (usize i = 0; i < g_out.playbacks.count; i++)
        if (g_out.playbacks.items[i]->id == id)
            return g_out.playbacks.items[i];
    return NULL;
}

static void playback_opens_swap(Playback* p, Open_List* nl)
{
    void* old = atomic_exchange_explicit(&p->opens, nl, memory_order_acq_rel);
    if (old)
        mel_array_push(&p->garbage, old);
}

static bool playback_open_add(Playback* p, Mel_AudioOut_Source src)
{
    Open_List* cur = atomic_load_explicit(&p->opens, memory_order_acquire);
    u32        count = cur ? cur->count : 0;
    Open_List* nl = mel_alloc(g_out.alloc, sizeof(Open_List) + sizeof(Web_Open) * ((usize)count + 1u));
    if (!nl)
        return false;
    for (u32 i = 0; i < count; i++)
        nl->opens[i] = cur->opens[i];
    nl->opens[count] = (Web_Open){ .src = src, .started = false };
    nl->count = count + 1u;
    playback_opens_swap(p, nl);
    return true;
}

static u32 playback_started_count(Playback* p)
{
    Open_List* ol = atomic_load_explicit(&p->opens, memory_order_acquire);
    u32        n = 0;
    if (ol)
        for (u32 i = 0; i < ol->count; i++)
            if (ol->opens[i].started)
                n++;
    return n;
}

static void playback_engine_sync(Playback* p)
{
    u32 started = playback_started_count(p);
    if (started > 0 && !p->engine_running)
    {
        p->engine_running = true;
        emscripten_resume_audio_context_sync(p->context);
        if (emscripten_audio_context_state(p->context) != AUDIO_CONTEXT_STATE_RUNNING)
            mel_log_warn("audioout", "web: AudioContext for %.*s held suspended by autoplay policy; audio starts after a user gesture", (int)p->stable_id.len, p->stable_id.data);
    }
    else if (started == 0 && p->engine_running)
    {
        p->engine_running = false;
        audioout_web__js_suspend(p->context);
    }
}

static void playback_destroy(Playback* p, bool lost)
{
    if (p->node > 0)
        emscripten_destroy_web_audio_node(p->node);
    if (p->context > 0)
        emscripten_destroy_audio_context(p->context);

    u32 dropped = atomic_load_explicit(&p->dropped, memory_order_relaxed);
    if (dropped > 0)
        mel_log_warn("audioout", "web: playback %.*s dropped %u blocks (render quantum exceeded the %u-frame scratch)", (int)p->stable_id.len, p->stable_id.data, dropped, p->quantum);

    Open_List* ol = atomic_exchange_explicit(&p->opens, NULL, memory_order_acq_rel);
    if (ol)
    {
        if (lost)
            for (u32 i = 0; i < ol->count; i++)
                if (ol->opens[i].src.on_lost)
                    ol->opens[i].src.on_lost(ol->opens[i].src.token);
        mel_dealloc(g_out.alloc, ol);
    }
    for (usize i = 0; i < p->garbage.count; i++)
        mel_dealloc(g_out.alloc, p->garbage.items[i]);
    mel_array_free(&p->garbage);

    if (p->scratch)
        mel_dealloc(g_out.alloc, p->scratch);
    if (p->worklet_stack)
        mel_dealloc(g_out.alloc, p->worklet_stack);
    if (p->stable_id.data)
        mel_dealloc(g_out.alloc, p->stable_id.data);

    for (usize i = 0; i < g_out.playbacks.count; i++)
        if (g_out.playbacks.items[i] == p)
        {
            mel_array_remove_unordered(&g_out.playbacks, i);
            break;
        }
    mel_dealloc(g_out.alloc, p);
}

static bool playback_process(int num_inputs, const AudioSampleFrame* inputs, int num_outputs, AudioSampleFrame* outputs, int num_params, const AudioParamFrame* params, void* user)
{
    MEL_UNUSED(num_inputs);
    MEL_UNUSED(inputs);
    MEL_UNUSED(num_params);
    MEL_UNUSED(params);
    Playback* p = (Playback*)user;

    for (int o = 0; o < num_outputs; o++)
        memset(outputs[o].data, 0, sizeof(f32) * (usize)outputs[o].numberOfChannels * (usize)outputs[o].samplesPerChannel);

    if (num_outputs < 1)
        return true;
    AudioSampleFrame* out = &outputs[0];
    u32               channels = (u32)out->numberOfChannels;
    u32               frames = (u32)out->samplesPerChannel;
    if (channels == 0u || frames == 0u)
        return true;
    if (p->scratch == NULL || frames > p->quantum)
    {
        atomic_fetch_add_explicit(&p->dropped, 1u, memory_order_relaxed);
        return true;
    }

    Open_List* ol = atomic_load_explicit(&p->opens, memory_order_acquire);
    if (!ol)
        return true;

    u32 mix = channels < p->channels ? channels : p->channels;
    for (u32 i = 0; i < ol->count; i++)
    {
        if (!ol->opens[i].started)
            continue;
        u32 got = ol->opens[i].src.pull(ol->opens[i].src.token, p->scratch, frames);
        if (got > frames)
            got = frames;
        for (u32 ch = 0; ch < mix; ch++)
        {
            f32* plane = out->data + (usize)ch * frames;
            for (u32 f = 0; f < got; f++)
                plane[f] += p->scratch[(usize)f * p->channels + ch];
        }
    }
    return true;
}

static void playback_processor_created(EMSCRIPTEN_WEBAUDIO_T context, bool success, void* user)
{
    Playback* p = playback_by_id((u32)(uintptr_t)user);
    if (!p)
        return;
    if (!success)
    {
        mel_log_error("audioout", "web: AudioWorkletProcessor creation failed for %.*s; output lost", (int)p->stable_id.len, p->stable_id.data);
        playback_destroy(p, true);
        return;
    }

    int                                     output_channels[1] = { (int)p->channels };
    EmscriptenAudioWorkletNodeCreateOptions opts = {
        .numberOfInputs = 0,
        .numberOfOutputs = 1,
        .outputChannelCounts = output_channels,
    };
    p->node = emscripten_create_wasm_audio_worklet_node(context, "mel-audioout-sink", &opts, playback_process, p);
    if (p->node <= 0)
    {
        mel_log_error("audioout", "web: AudioWorkletNode creation failed for %.*s; output lost", (int)p->stable_id.len, p->stable_id.data);
        playback_destroy(p, true);
        return;
    }
    emscripten_audio_node_connect(p->node, context, 0, 0);
    mel_log_info("audioout", "web: output ready for %.*s (%u Hz, quantum %u, %u ch)", (int)p->stable_id.len, p->stable_id.data, p->samplerate, p->quantum, p->channels);
    playback_engine_sync(p);
}

static void playback_worklet_ready(EMSCRIPTEN_WEBAUDIO_T context, bool success, void* user)
{
    Playback* p = playback_by_id((u32)(uintptr_t)user);
    if (!p)
        return;
    if (!success)
    {
        mel_log_error("audioout", "web: AudioWorklet thread creation failed for %.*s; output lost", (int)p->stable_id.len, p->stable_id.data);
        playback_destroy(p, true);
        return;
    }
    WebAudioWorkletProcessorCreateOptions popts = {
        .name = "mel-audioout-sink",
    };
    emscripten_create_wasm_audio_worklet_processor_async(context, &popts, playback_processor_created, user);
}

EMSCRIPTEN_KEEPALIVE void mel_audioout_web__on_sink(unsigned id, int ok)
{
    Playback* p = playback_by_id((u32)id);
    if (!p)
        return;
    if (ok)
    {
        mel_log_info("audioout", "web: setSinkId bound %.*s", (int)p->stable_id.len, p->stable_id.data);
        return;
    }
    mel_log_error("audioout", "web: setSinkId rejected for %.*s (device gone or not allowed); output lost", (int)p->stable_id.len, p->stable_id.data);
    playback_destroy(p, true);
}

static void devices_free(Device_Array* arr)
{
    for (usize i = 0; i < arr->count; i++)
    {
        if (arr->items[i].stable_id.data)
            mel_dealloc(g_out.alloc, arr->items[i].stable_id.data);
        if (arr->items[i].name.data)
            mel_dealloc(g_out.alloc, arr->items[i].name.data);
    }
    mel_array_free(arr);
}

static bool devices_contain(const Device_Array* arr, str8 stable_id)
{
    for (usize i = 0; i < arr->count; i++)
        if (str8_equals(arr->items[i].stable_id, stable_id))
            return true;
    return false;
}

EMSCRIPTEN_KEEPALIVE void mel_audioout_web__on_enum(int count)
{
    if (!g_out.registered || !g_out.has_sinkid)
        return;

    Device_Array fresh;
    mel_array_init(&fresh, g_out.alloc);
    for (int i = 0; i < count; i++)
    {
        Web_Device d = { 0 };
        int        id_len = audioout_web__js_snap_id_len(i);
        if (id_len > 0)
        {
            u8* data = mel_alloc(g_out.alloc, (usize)id_len + 5);
            if (!data)
                continue;
            memcpy(data, "web:", 4);
            audioout_web__js_snap_id(i, (char*)data + 4, id_len + 1);
            d.stable_id = (str8){ data, (size)id_len + 4 };
        }
        else if (!devices_contain(&fresh, S8("web:default")))
            d.stable_id = str8_dup(S8("web:default"), g_out.alloc);
        else
            d.stable_id = str8_fmt(g_out.alloc, "web:pending-%d", i);

        int label_len = audioout_web__js_snap_label_len(i);
        if (label_len > 0)
        {
            u8* data = mel_alloc(g_out.alloc, (usize)label_len + 1);
            if (data)
            {
                audioout_web__js_snap_label(i, (char*)data, label_len + 1);
                d.name = (str8){ data, (size)label_len };
            }
        }
        mel_array_push(&fresh, d);
    }

    bool changed = fresh.count != g_out.devices.count;
    for (usize i = 0; !changed && i < fresh.count; i++)
    {
        bool found = false;
        for (usize j = 0; j < g_out.devices.count && !found; j++)
            found = str8_equals(fresh.items[i].stable_id, g_out.devices.items[j].stable_id) && str8_equals(fresh.items[i].name, g_out.devices.items[j].name);
        changed = !found;
    }

    devices_free(&g_out.devices);
    g_out.devices = fresh;

    if (count == 0)
        mel_log_warn("audioout", "web: enumerateDevices yielded zero audio outputs (unsupported context or no devices)");
    if (changed)
        mel_audioout_provider_notify(g_out.provider);
}

static void web_enumerate(void* user, Mel_AudioOut_Enum_Fn fn, void* fn_user)
{
    MEL_UNUSED(user);
    for (usize i = 0; i < g_out.devices.count; i++)
    {
        Web_Device*      d = &g_out.devices.items[i];
        u32              rate = g_out.default_rate;
        Mel_AudioOut_Raw raw = {
            .stable_id = d->stable_id,
            .name = d->name,
            .kind = &mel_audioout_unknown,
            .channels = MEL_AUDIOOUT_WEB_CHANNELS,
            .samplerate = rate,
            .samplerates = rate > 0 ? &rate : NULL,
            .samplerate_count = rate > 0 ? 1u : 0u,
            .caps = { .volume = false, .mute = false },
        };
        if (!fn(&raw, fn_user))
            break;
    }
    if (g_out.has_sinkid)
        audioout_web__js_refresh();
}

static str8 web_default_id(void* user)
{
    MEL_UNUSED(user);
    for (usize i = 0; i < g_out.devices.count; i++)
        if (str8_equals(g_out.devices.items[i].stable_id, S8("web:default")))
            return g_out.devices.items[i].stable_id;
    return g_out.devices.count > 0 ? g_out.devices.items[0].stable_id : STR8_EMPTY;
}

static Mel_AudioOut_Status web_open(void* user, str8 stable_id, Mel_AudioOut_Format req, Mel_AudioOut_Format* granted, Mel_AudioOut_Source src)
{
    MEL_UNUSED(user);
    assert(granted != NULL);
    assert(src.pull != NULL);

    Playback* existing = playback_find(stable_id);
    if (existing)
    {
        if (!playback_open_add(existing, src))
            return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
        granted->samplerate = existing->samplerate;
        granted->channels = existing->channels;
        granted->block_frames = existing->quantum;
        return MEL_AUDIOOUT_OK;
    }

    if (str8_starts_with(stable_id, S8("web:pending-")))
    {
        mel_log_error("audioout", "web: open %.*s: deviceId withheld until media consent reveals it", (int)stable_id.len, stable_id.data);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_NO_DEVICE;
    }
    if (!str8_starts_with(stable_id, S8("web:")))
    {
        mel_log_error("audioout", "web: open %.*s: not a web provider id", (int)stable_id.len, stable_id.data);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_NO_DEVICE;
    }
    bool is_default = str8_equals(stable_id, S8("web:default"));
    if (!is_default && !g_out.has_sinkid)
    {
        mel_log_error("audioout", "web: open %.*s: AudioContext.setSinkId unavailable; only web:default is openable", (int)stable_id.len, stable_id.data);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }

    Playback* p = mel_alloc_type(g_out.alloc, Playback);
    if (!p)
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    memset(p, 0, sizeof *p);
    p->id = ++g_out.playback_seq;
    p->stable_id = str8_dup(stable_id, g_out.alloc);
    p->channels = MEL_AUDIOOUT_WEB_CHANNELS;
    mel_array_init(&p->garbage, g_out.alloc);
    atomic_store_explicit(&p->opens, NULL, memory_order_relaxed);

    p->context = emscripten_create_audio_context(NULL);
    if (p->context <= 0)
    {
        mel_log_error("audioout", "web: AudioContext creation failed for %.*s", (int)stable_id.len, stable_id.data);
        mel_dealloc(g_out.alloc, p->stable_id.data);
        mel_array_free(&p->garbage);
        mel_dealloc(g_out.alloc, p);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }
    int rate = emscripten_audio_context_sample_rate(p->context);
    int quantum = emscripten_audio_context_quantum_size(p->context);
    if (rate <= 0 || quantum <= 0)
    {
        mel_log_error("audioout", "web: AudioContext reported rate %d / quantum %d for %.*s", rate, quantum, (int)stable_id.len, stable_id.data);
        emscripten_destroy_audio_context(p->context);
        mel_dealloc(g_out.alloc, p->stable_id.data);
        mel_array_free(&p->garbage);
        mel_dealloc(g_out.alloc, p);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }
    p->samplerate = (u32)rate;
    p->quantum = (u32)quantum;

    p->scratch = mel_alloc(g_out.alloc, sizeof(f32) * (usize)p->quantum * p->channels);
    p->worklet_stack = mel_calloc(g_out.alloc, MEL_AUDIOOUT_WEB_WORKLET_STACK);
    if (!p->scratch || !p->worklet_stack || !playback_open_add(p, src))
    {
        mel_log_error("audioout", "web: playback allocation failed for %.*s", (int)stable_id.len, stable_id.data);
        mel_array_push(&g_out.playbacks, p);
        playback_destroy(p, false);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }
    mel_array_push(&g_out.playbacks, p);

    if (!is_default)
        audioout_web__js_set_sink(p->id, p->context, (const char*)p->stable_id.data + 4, (int)(p->stable_id.len - 4));
    emscripten_start_wasm_audio_worklet_thread_async(p->context, p->worklet_stack, MEL_AUDIOOUT_WEB_WORKLET_STACK, playback_worklet_ready, (void*)(uintptr_t)p->id);

    granted->samplerate = p->samplerate;
    granted->channels = p->channels;
    granted->block_frames = p->quantum;
    if (req.samplerate != p->samplerate || req.channels != p->channels || req.block_frames != p->quantum)
        mel_log_info("audioout", "web: open %.*s granted %u Hz %u ch quantum %u (requested %u Hz %u ch block %u)", (int)stable_id.len, stable_id.data, p->samplerate, p->channels, p->quantum, req.samplerate, req.channels, req.block_frames);
    return MEL_AUDIOOUT_OK;
}

static void web_set_started(str8 stable_id, void* token, bool started)
{
    Playback* p = playback_find(stable_id);
    if (!p)
        return;
    Open_List* cur = atomic_load_explicit(&p->opens, memory_order_acquire);
    u32        count = cur ? cur->count : 0;
    Open_List* nl = mel_alloc(g_out.alloc, sizeof(Open_List) + sizeof(Web_Open) * (usize)count);
    if (!nl)
    {
        mel_log_error("audioout", "web: start/stop bookkeeping allocation failed for %.*s", (int)stable_id.len, stable_id.data);
        return;
    }
    for (u32 i = 0; i < count; i++)
    {
        nl->opens[i] = cur->opens[i];
        if (nl->opens[i].src.token == token)
            nl->opens[i].started = started;
    }
    nl->count = count;
    playback_opens_swap(p, nl);
    playback_engine_sync(p);
}

static void web_start(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    web_set_started(stable_id, token, true);
}

static void web_stop(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    web_set_started(stable_id, token, false);
}

static void web_close(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    Playback* p = playback_find(stable_id);
    if (!p)
        return;

    Open_List* cur = atomic_load_explicit(&p->opens, memory_order_acquire);
    u32        count = cur ? cur->count : 0;
    Open_List* nl = mel_alloc(g_out.alloc, sizeof(Open_List) + sizeof(Web_Open) * (usize)count);
    if (!nl)
        return;
    u32 kept = 0;
    for (u32 i = 0; i < count; i++)
        if (cur->opens[i].src.token != token)
            nl->opens[kept++] = cur->opens[i];
    nl->count = kept;

    if (kept == 0)
    {
        mel_dealloc(g_out.alloc, nl);
        playback_destroy(p, false);
        return;
    }
    playback_opens_swap(p, nl);
    playback_engine_sync(p);
}

static void* web_native(void* user, str8 stable_id)
{
    MEL_UNUSED(user);
    return playback_find(stable_id);
}

static void web_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    while (g_out.playbacks.count > 0)
        playback_destroy(g_out.playbacks.items[g_out.playbacks.count - 1], true);
    devices_free(&g_out.devices);
    mel_array_free(&g_out.playbacks);
    audioout_web__js_shutdown();
    memset(&g_out, 0, sizeof g_out);
}

void mel_audioout__register_host_providers(void)
{
    g_out.alloc = mel_alloc_heap();
    mel_array_init(&g_out.devices, g_out.alloc);
    mel_array_init(&g_out.playbacks, g_out.alloc);
    g_out.playback_seq = 0;

    audioout_web__js_init();
    g_out.has_sinkid = audioout_web__js_sink_supported() != 0;
    int rate = audioout_web__js_rate();
    g_out.default_rate = rate > 0 ? (u32)rate : 0u;
    if (g_out.default_rate == 0)
        mel_log_warn("audioout", "web: AudioContext unavailable; descriptor samplerate unknown until open");

    if (g_out.has_sinkid)
        mel_log_info("audioout", "web: AudioContext.setSinkId available; enumerating audiooutput devices");
    else
    {
        mel_log_warn("audioout", "web: AudioContext.setSinkId unavailable; exposing the user-agent default output only");
        Web_Device d = {
            .stable_id = str8_dup(S8("web:default"), g_out.alloc),
            .name = str8_dup(S8("Default"), g_out.alloc),
        };
        mel_array_push(&g_out.devices, d);
    }

    static const Mel_AudioOut_Provider_Desc desc = {
        .name = "web-mediadevices",
        .enumerate = web_enumerate,
        .default_id = web_default_id,
        .open = web_open,
        .start = web_start,
        .stop = web_stop,
        .close = web_close,
        .native = web_native,
        .shutdown = web_shutdown,
    };
    g_out.provider = mel_audioout_provider_register(&desc);
    g_out.registered = true;
    if (g_out.has_sinkid)
        audioout_web__js_refresh();
}
