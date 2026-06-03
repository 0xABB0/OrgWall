#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>

#include <core/types.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>

#include <audio/audio.h>

#include <reactor/reactor.h>
#include <executor/executor.h>
#include <thread/thread.h>

#define HELLO_TAU       6.28318530717958647692
#define HELLO_SR        48000u
#define HELLO_CH        2u
#define HELLO_BLOCK     512u
#define HELLO_RING      4u
#define HELLO_MAXCH     2u
#define HELLO_MAXRATIO  4.0

typedef struct
{
    _Atomic(Mel_Reactor*) reactor;
    _Atomic(u32)          ready;
    Mel_Thread            thread;
} Hello_Reactor;

static f64 note_hz(i32 midi)
{
    return 440.0 * pow(2.0, (f64)(midi - 69) / 12.0);
}

static void envelope(f32* interleaved, u32 frames, u32 channels, u32 samplerate, f64 attack_s, f64 release_s)
{
    u32 atk = (u32)(attack_s * (f64)samplerate);
    u32 rel = (u32)(release_s * (f64)samplerate);
    if (atk > frames)
        atk = frames;
    if (rel > frames)
        rel = frames;
    for (u32 i = 0; i < frames; i++)
    {
        f32 g = 1.0f;
        if (i < atk)
            g = (f32)i / (f32)atk;
        if (i >= frames - rel)
        {
            f32 r = (f32)(frames - i) / (f32)rel;
            if (r < g)
                g = r;
        }
        for (u32 c = 0; c < channels; c++)
            interleaved[(usize)i * channels + c] *= g;
    }
}

static f32* tone_buffer(const Mel_Alloc* a, f64 freq, f64 seconds, u32 samplerate, u32 channels, f32 amp, u32* out_frames)
{
    u32  frames = (u32)(seconds * (f64)samplerate);
    f32* buf = mel_alloc(a, sizeof(f32) * (usize)frames * (usize)channels);
    for (u32 i = 0; i < frames; i++)
    {
        f64 t = (f64)i / (f64)samplerate;
        f64 fundamental = sin(HELLO_TAU * freq * t);
        f64 second = 0.5 * sin(HELLO_TAU * 2.0 * freq * t) * exp(-3.0 * t);
        f64 third = 0.25 * sin(HELLO_TAU * 3.0 * freq * t) * exp(-5.0 * t);
        f32 s = (f32)((fundamental + second + third) * (f64)amp);
        for (u32 c = 0; c < channels; c++)
            buf[(usize)i * channels + c] = s;
    }
    envelope(buf, frames, channels, samplerate, 0.006, 0.040);
    *out_frames = frames;
    return buf;
}

static Mel_Audio_Source* tone_source(const Mel_Alloc* a, f64 freq, f64 seconds, u32 samplerate, f32 amp)
{
    u32  frames = 0u;
    f32* buf = tone_buffer(a, freq, seconds, samplerate, HELLO_CH, amp, &frames);
    Mel_Audio_Source* src = mel_audio_pcm_from_float(a, buf, frames, HELLO_CH, samplerate, MEL_AUDIO_OWNERSHIP_OWNED);
    mel_dealloc(a, buf);
    return src;
}

static void sleep_seconds(f64 seconds)
{
    if (seconds <= 0.0)
        return;
    mel_thread_sleep((i64)(seconds * 1.0e9));
}

static bool reactor_started(Mel_Reactor* r, void* user)
{
    Hello_Reactor* h = (Hello_Reactor*)user;
    atomic_store_explicit(&h->reactor, r, memory_order_release);
    atomic_store_explicit(&h->ready, 1u, memory_order_release);
    return true;
}

static int reactor_thread_fn(void* user)
{
    Hello_Reactor* h = (Hello_Reactor*)user;
    mel_reactor_spawn(MEL_REACTOR_THREADED, reactor_started, h);
    return 0;
}

static void reactor_quit_post(void* user)
{
    mel_reactor_quit((Mel_Reactor*)user);
}

static Mel_Reactor* reactor_bring_up(Hello_Reactor* h)
{
    atomic_store_explicit(&h->reactor, NULL, memory_order_relaxed);
    atomic_store_explicit(&h->ready, 0u, memory_order_relaxed);
    if (!mel_thread_spawn(&h->thread, reactor_thread_fn, h, .name = "hello-audio-reactor"))
        return NULL;
    while (atomic_load_explicit(&h->ready, memory_order_acquire) == 0u)
        mel_thread_yield();
    return (Mel_Reactor*)atomic_load_explicit(&h->reactor, memory_order_acquire);
}

static void reactor_tear_down(Hello_Reactor* h)
{
    Mel_Reactor* r = (Mel_Reactor*)atomic_load_explicit(&h->reactor, memory_order_acquire);
    if (r != NULL)
        mel_reactor_post(r, reactor_quit_post, r);
    mel_thread_join(&h->thread, NULL);
}

static Mel_Audio_Opt base_opt(Mel_Executor* exec, f32 master)
{
    return (Mel_Audio_Opt){
        .samplerate = HELLO_SR,
        .channels = HELLO_CH,
        .block_frames = HELLO_BLOCK,
        .ring_blocks = HELLO_RING,
        .master_volume = master,
        .resampler = NULL,
        .exec = exec,
        .max_voice_channels = HELLO_MAXCH,
        .max_voice_ratio = HELLO_MAXRATIO,
    };
}

static u32 rng_next(u32* state)
{
    u32 x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static f32 rng_unit(u32* state)
{
    return (f32)(rng_next(state) >> 8) / (f32)(1u << 24);
}

static Mel_Audio_Source** run_demo(const Mel_Alloc* a, Mel_Audio* eng, u32 device_rate, f64 auto_seconds, u32* out_count)
{
    static const i32 chord[] = { 60, 64, 67, 72 };
    static const i32 melody[] = { 72, 74, 76, 77, 79, 77, 76, 74, 72 };

    u32                count = 13u;
    Mel_Audio_Source** srcs = mel_alloc(a, sizeof(Mel_Audio_Source*) * count);
    Mel_Audio_Source** chord_src = srcs;
    Mel_Audio_Source** melody_src = srcs + 4;
    for (u32 i = 0; i < 4; i++)
        chord_src[i] = tone_source(a, note_hz(chord[i]), 2.4, device_rate, 0.32f);
    for (u32 i = 0; i < 9; i++)
        melody_src[i] = tone_source(a, note_hz(melody[i]), 0.55, device_rate, 0.34f);

    f64 budget = auto_seconds > 0.0 ? auto_seconds : 1.0e30;

    u32 rng = 0x1234u;
    f64 elapsed = 0.0;
    for (u32 i = 0; i < 4 && elapsed < budget; i++)
    {
        f32 pan = -0.4f + 0.27f * (f32)i;
        mel_audio_play_ex(eng, chord_src[i], 0.9f, pan, false);
        sleep_seconds(0.22);
        elapsed += 0.22;
    }

    sleep_seconds(0.9);
    elapsed += 0.9;

    for (u32 i = 0; i < 9 && elapsed < budget; i++)
    {
        f32 pan = (rng_unit(&rng) - 0.5f) * 0.6f;
        mel_audio_play_ex(eng, melody_src[i], 0.85f, pan, false);
        sleep_seconds(0.34);
        elapsed += 0.34;
    }

    sleep_seconds(0.5);
    mel_audio_fade_master_volume(eng, 0.0f, 1.2);
    sleep_seconds(1.3);

    *out_count = count;
    return srcs;
}

static Mel_Audio_Source** run_stress(const Mel_Alloc* a, Mel_Audio* eng, u32 device_rate, f64 auto_seconds, u32* out_count)
{
    static const i32 scale[] = { 48, 50, 52, 55, 57, 60, 62, 64, 67, 69, 72, 74 };
    u32 scale_n = (u32)(sizeof(scale) / sizeof(scale[0]));

    Mel_Audio_Source** srcs = mel_alloc(a, sizeof(Mel_Audio_Source*) * scale_n);
    for (u32 i = 0; i < scale_n; i++)
    {
        srcs[i] = tone_source(a, note_hz(scale[i]), 2.0, device_rate, 0.5f);
        mel_audio_pcm_set_loop(srcs[i], true, 0.0);
    }

    f64 total = auto_seconds > 0.0 ? auto_seconds : 10.0;
    u32 target = 192u;
    u32 spawned = 0u;
    u32 rng = 0xC0FFEEu;

    f64 ramp = total * 0.55;
    f64 step = ramp / (f64)target;
    f64 elapsed = 0.0;
    u32 tick = 0u;

    while (elapsed < total)
    {
        if (spawned < target && elapsed < ramp)
        {
            u32 batch = 3u;
            for (u32 k = 0; k < batch && spawned < target; k++)
            {
                Mel_Audio_Source* s = srcs[spawned % scale_n];
                f32  pan = (rng_unit(&rng) - 0.5f) * 1.8f;
                f32  vol = 0.18f + 0.12f * rng_unit(&rng);
                Mel_Audio_Voice v = mel_audio_play_ex(eng, s, 0.0f, pan, false);
                mel_audio_set_play_speed(eng, v, 0.5 + 1.4 * (f64)rng_unit(&rng));
                mel_audio_fade_volume(eng, v, vol, 0.8 + 1.5 * (f64)rng_unit(&rng));
                mel_audio_oscillate_volume(eng, v, vol * 0.4f, vol, 2.0 + 4.0 * (f64)rng_unit(&rng));
                spawned++;
            }
        }

        sleep_seconds(step * 3.0);
        elapsed += step * 3.0;

        if ((tick++ & 7u) == 0u)
        {
            u32 active = mel_audio_active_voice_count(eng);
            fprintf(stderr, "[stress] t=%5.1fs active_voices=%-4u spawned=%-4u\n", elapsed, active, spawned);
        }
    }

    fprintf(stderr, "[stress] fading swarm out\n");
    mel_audio_fade_master_volume(eng, 0.0f, 1.5);
    sleep_seconds(1.6);
    mel_audio_stop_all(eng);

    *out_count = scale_n;
    return srcs;
}

static bool mode_is_stress(int argc, char** argv)
{
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "stress") == 0)
            return true;
        if (strcmp(argv[i], "demo") == 0)
            return false;
    }
    const char* env = getenv("HELLO_AUDIO_MODE");
    if (env != NULL && strcmp(env, "stress") == 0)
        return true;
    return false;
}

int main(int argc, char** argv)
{
    const Mel_Alloc* a = mel_alloc_heap();

    bool stress = mode_is_stress(argc, argv);

    f64         auto_seconds = 0.0;
    const char* auto_env = getenv("HELLO_AUDIO_AUTO");
    if (auto_env != NULL)
        auto_seconds = atof(auto_env);

    Hello_Reactor h;
    Mel_Reactor*  reactor = reactor_bring_up(&h);
    if (reactor == NULL)
    {
        fprintf(stderr, "hello-audio: failed to bring up reactor\n");
        return 1;
    }
    Mel_Executor* exec = mel_reactor_executor(reactor);

    f32        master = stress ? 0.5f : 0.8f;
    Mel_Audio* eng = mel_audio_create(a, reactor, base_opt(exec, master));
    if (eng == NULL)
    {
        fprintf(stderr, "hello-audio: no audio device (mel_audio_create returned NULL)\n");
        reactor_tear_down(&h);
        return 1;
    }

    Mel_Audio_Caps caps = mel_audio_caps(eng);
    fprintf(stderr, "hello-audio: device %uHz %uch block %u ring %u latency %u frames; mode=%s\n",
            caps.samplerate, caps.channels, caps.block_frames, caps.ring_blocks, caps.latency_frames,
            stress ? "stress" : "demo");

    u32                src_count = 0u;
    Mel_Audio_Source** srcs = stress ? run_stress(a, eng, caps.samplerate, auto_seconds, &src_count)
                                     : run_demo(a, eng, caps.samplerate, auto_seconds, &src_count);

    fprintf(stderr, "hello-audio: %u voices active at teardown\n", mel_audio_active_voice_count(eng));

    mel_audio_destroy(eng);

    for (u32 i = 0; i < src_count; i++)
        srcs[i]->source_free(srcs[i], a);
    mel_dealloc(a, srcs);

    reactor_tear_down(&h);
    return 0;
}
