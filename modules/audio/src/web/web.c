#include <audio/backend.h>

#include <core/types.h>
#include <allocator/allocator.h>
#include <event/event.h>
#include <log/log.h>

#include <emscripten/webaudio.h>
#include <emscripten/em_js.h>

#include <stdatomic.h>
#include <string.h>

#define MEL_AUDIO_WEB_QUANTUM       128
#define MEL_AUDIO_WEB_WORKLET_STACK (1u << 16)

#define MEL_AUDIO__DEVICE_EVENT_DEVICECHANGE 1u

typedef struct
{
    Mel_Audio_Ring*                 ring;
    const Mel_Alloc*                alloc;
    void*                           worklet_stack;
    EMSCRIPTEN_WEBAUDIO_T           context;
    EMSCRIPTEN_AUDIO_WORKLET_NODE_T node;
    u32                             channels;
    u32                             quantum;
    f32*                            deinterleave;
    usize                           deinterleave_samples;
    _Atomic(u32)                    underruns;
    u32                             opened;
    u32                             started;
    _Atomic(Mel_Event*)             device_events;
    u32                             devicechange_listening;
} Mel_Audio_Web;

static Mel_Audio_Web g_web;

EMSCRIPTEN_KEEPALIVE void mel_audio_web__on_devicechange(void)
{
    Mel_Event* ev = atomic_load_explicit(&g_web.device_events, memory_order_acquire);
    if (ev != NULL)
    {
        u32 code = MEL_AUDIO__DEVICE_EVENT_DEVICECHANGE;
        mel_event_fire(ev, &code);
    }
}

EM_JS(int, mel_audio_web__add_devicechange_listener, (void), {
    if (typeof navigator === 'undefined' || !navigator.mediaDevices)
        return 0;
    if (Module['_mel_audio_web_devicechange_handler'])
        return 1;
    var handler = function() { _mel_audio_web__on_devicechange(); };
    Module['_mel_audio_web_devicechange_handler'] = handler;
    navigator.mediaDevices.addEventListener('devicechange', handler);
    return 1;
});

EM_JS(void, mel_audio_web__remove_devicechange_listener, (void), {
    if (typeof navigator === 'undefined' || !navigator.mediaDevices)
        return;
    var handler = Module['_mel_audio_web_devicechange_handler'];
    if (handler) {
        navigator.mediaDevices.removeEventListener('devicechange', handler);
        Module['_mel_audio_web_devicechange_handler'] = undefined;
    }
});

bool mel_audio_backend_open(Mel_Audio_Opt req, Mel_Audio_Caps* granted, const Mel_Alloc* a)
{
    assert(granted != NULL);
    assert(a != NULL);
    assert(req.samplerate > 0u);
    assert(req.channels >= 1u);

    if (g_web.opened)
    {
        mel_log_error("audio.web", "backend already open; one AudioContext per process");
        return false;
    }

    g_web = (Mel_Audio_Web){ 0 };
    g_web.alloc = a;
    g_web.channels = req.channels;

    EmscriptenWebAudioCreateAttributes attr = {
        .latencyHint = "interactive",
        .sampleRate = req.samplerate,
        .renderSizeHint = AUDIO_CONTEXT_RENDER_SIZE_DEFAULT,
    };

    g_web.context = emscripten_create_audio_context(&attr);
    if (g_web.context <= 0)
    {
        mel_log_error("audio.web", "emscripten_create_audio_context failed (requested %uHz %uch)", req.samplerate, req.channels);
        return false;
    }

    int got_rate = emscripten_audio_context_sample_rate(g_web.context);
    int got_quantum = emscripten_audio_context_quantum_size(g_web.context);
    if (got_rate <= 0)
        got_rate = (int)req.samplerate;
    if (got_quantum <= 0)
        got_quantum = MEL_AUDIO_WEB_QUANTUM;

    if ((u32)got_rate != req.samplerate)
        mel_log_warn("audio.web", "device granted %dHz, requested %uHz (Web Audio chooses the hardware rate)", got_rate, req.samplerate);
    if ((u32)got_quantum != req.block_frames)
        mel_log_warn("audio.web", "device block fixed at %d frames (Web Audio render quantum), requested %u", got_quantum, req.block_frames);

    g_web.quantum = (u32)got_quantum;

    assert(req.ring_blocks > 0u);
    u32 ring_blocks = req.ring_blocks;
    *granted = (Mel_Audio_Caps){
        .samplerate = (u32)got_rate,
        .channels = req.channels,
        .block_frames = (u32)got_quantum,
        .ring_blocks = ring_blocks,
        .latency_frames = ring_blocks * (u32)got_quantum,
    };

    g_web.opened = 1u;
    mel_log_info("audio.web", "AudioContext opened: %dHz %uch quantum %d (latency ~%u frames)", got_rate, req.channels, got_quantum, granted->latency_frames);
    return true;
}

static bool mel_audio_web__process(int num_inputs, const AudioSampleFrame* inputs, int num_outputs, AudioSampleFrame* outputs, int num_params, const AudioParamFrame* params, void* user)
{
    MEL_UNUSED(num_inputs);
    MEL_UNUSED(inputs);
    MEL_UNUSED(num_params);
    MEL_UNUSED(params);

    Mel_Audio_Web* w = (Mel_Audio_Web*)user;

    if (num_outputs < 1)
        return true;

    AudioSampleFrame* out = &outputs[0];
    u32               channels = (u32)out->numberOfChannels;
    u32               frames = (u32)out->samplesPerChannel;
    if (channels == 0u || frames == 0u)
        return true;

    u32   want = frames * channels;
    usize need = (usize)want;
    if (need > w->deinterleave_samples || w->deinterleave == NULL)
    {
        for (u32 c = 0; c < channels; c++)
            memset(out->data + (usize)c * frames, 0, sizeof(f32) * (usize)frames);
        return true;
    }

    u32 got = mel_audio_ring_read(w->ring, w->deinterleave, want);
    if (got < want)
    {
        atomic_fetch_add_explicit(&w->underruns, 1u, memory_order_relaxed);
    }

    for (u32 c = 0; c < channels; c++)
    {
        f32* plane = out->data + (usize)c * frames;
        for (u32 i = 0; i < frames; i++)
            plane[i] = w->deinterleave[(usize)i * channels + c];
    }

    return true;
}

static void mel_audio_web__node_created(EMSCRIPTEN_WEBAUDIO_T context, bool success, void* user)
{
    Mel_Audio_Web* w = (Mel_Audio_Web*)user;
    if (!success)
    {
        mel_log_error("audio.web", "AudioWorkletProcessor creation failed");
        return;
    }

    int                                     output_channels[1] = { (int)w->channels };
    EmscriptenAudioWorkletNodeCreateOptions opts = {
        .numberOfInputs = 0,
        .numberOfOutputs = 1,
        .outputChannelCounts = output_channels,
    };

    w->node = emscripten_create_wasm_audio_worklet_node(context, "mel-audio-mixer", &opts, mel_audio_web__process, w);
    if (w->node <= 0)
    {
        mel_log_error("audio.web", "emscripten_create_wasm_audio_worklet_node failed");
        return;
    }

    emscripten_audio_node_connect(w->node, context, 0, 0);
    emscripten_resume_audio_context_sync(context);
    mel_log_info("audio.web", "AudioWorklet node connected; output running");
}

static void mel_audio_web__worklet_ready(EMSCRIPTEN_WEBAUDIO_T context, bool success, void* user)
{
    Mel_Audio_Web* w = (Mel_Audio_Web*)user;
    if (!success)
    {
        mel_log_error("audio.web", "AudioWorklet thread creation failed");
        return;
    }

    WebAudioWorkletProcessorCreateOptions popts = {
        .name = "mel-audio-mixer",
        .numAudioParams = 0,
        .audioParamDescriptors = NULL,
    };
    emscripten_create_wasm_audio_worklet_processor_async(context, &popts, mel_audio_web__node_created, w);
}

void mel_audio_backend_start(Mel_Audio_Ring* ring)
{
    assert(ring != NULL);
    assert(g_web.opened);
    assert(!g_web.started);

    g_web.ring = ring;
    g_web.started = 1u;

    g_web.deinterleave_samples = (usize)g_web.quantum * (usize)g_web.channels;
    g_web.deinterleave = mel_calloc(g_web.alloc, sizeof(f32) * g_web.deinterleave_samples);
    if (g_web.deinterleave == NULL)
    {
        mel_log_error("audio.web", "deinterleave scratch allocation failed (%zu samples)", g_web.deinterleave_samples);
        g_web.deinterleave_samples = 0u;
        return;
    }

    g_web.worklet_stack = mel_calloc(g_web.alloc, MEL_AUDIO_WEB_WORKLET_STACK);
    if (g_web.worklet_stack == NULL)
    {
        mel_log_error("audio.web", "worklet stack allocation failed (%u bytes)", MEL_AUDIO_WEB_WORKLET_STACK);
        return;
    }

    emscripten_start_wasm_audio_worklet_thread_async(g_web.context, g_web.worklet_stack, MEL_AUDIO_WEB_WORKLET_STACK, mel_audio_web__worklet_ready, &g_web);
}

void mel_audio_backend_stop(void)
{
    if (!g_web.started)
        return;

    u32 underruns = atomic_load_explicit(&g_web.underruns, memory_order_relaxed);
    if (underruns > 0u)
        mel_log_warn("audio.web", "device thread observed %u ring underruns over the session", underruns);

    if (g_web.node > 0)
    {
        emscripten_destroy_web_audio_node(g_web.node);
        g_web.node = 0;
    }
    g_web.ring = NULL;
    g_web.started = 0u;
}

void mel_audio_backend_close(const Mel_Alloc* a)
{
    assert(a != NULL);
    if (!g_web.opened)
        return;

    if (g_web.started)
        mel_audio_backend_stop();

    if (g_web.context > 0)
    {
        emscripten_destroy_audio_context(g_web.context);
        g_web.context = 0;
    }

    if (g_web.deinterleave != NULL)
    {
        mel_dealloc(a, g_web.deinterleave);
        g_web.deinterleave = NULL;
        g_web.deinterleave_samples = 0u;
    }
    if (g_web.worklet_stack != NULL)
    {
        mel_dealloc(a, g_web.worklet_stack);
        g_web.worklet_stack = NULL;
    }

    g_web.opened = 0u;
}

void mel_audio_backend_set_device_event(Mel_Event* ev)
{
    atomic_store_explicit(&g_web.device_events, ev, memory_order_release);

    if (ev != NULL)
    {
        if (g_web.devicechange_listening)
            return;
        if (mel_audio_web__add_devicechange_listener() != 0)
            g_web.devicechange_listening = 1u;
        return;
    }

    if (g_web.devicechange_listening)
    {
        mel_audio_web__remove_devicechange_listener();
        g_web.devicechange_listening = 0u;
    }
}
