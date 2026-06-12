#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <core/types.h>
#include <core/platform.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>

#include <gui/gui.h>
#include <gui/gui.h>

#include <audiomixer/audiomixer.h>

#include <vat/tick.h>
#include <vat/vat.h>
#include <executor/executor.h>
#include <time/nano.h>
#include <log/log.h>

#define HELLO_TAU           6.28318530717958647692
#define HELLO_SR            48000u
#define HELLO_CH            2u
#define HELLO_BLOCK         512u
#define HELLO_RING          4u
#define HELLO_MAXCH         2u
#define HELLO_MAXRATIO      4.0
#define HELLO_DOMAIN        "hello-audio"

#define HELLO_METER_HZ      30
#define HELLO_SEQ_HZ        20

#define HELLO_STRESS_TARGET 192u

typedef enum
{
    HELLO_MODE_DEMO = 0,
    HELLO_MODE_STRESS = 1,
} Hello_Mode;

typedef struct
{
    const Mel_Alloc* alloc;
    Mel_Vat*         vat;
    Mel_Mixer*       engine;
    Mel_Mixer_Caps   caps;

    Mel_Mixer_Source** demo_srcs;
    u32                demo_count;
    Mel_Mixer_Source** stress_srcs;
    u32                stress_count;

    Mel_Gui_Handle frame;
    Mel_Gui_Handle mode_label;
    Mel_Gui_Handle count_label;
    Mel_Gui_Handle canvas;

    Mel_Vat_Tick* meter_timer;
    Mel_Vat_Tick* seq_timer;
    Mel_Vat_Tick* auto_timer;

    Hello_Mode mode;
    bool       sequence_active;
    bool       started;
    u32        seq_step;
    f64        seq_elapsed;
    u32        stress_spawned;
    u32        rng;

    u32 active_voices;
    u32 peak_voices;

    f64  auto_seconds;
    f64  auto_elapsed;
    bool auto_quitting;
    bool torn_down;
} Hello_State;

static Hello_State g_app;

static f64 note_hz(i32 midi) { return 440.0 * pow(2.0, (f64)(midi - 69) / 12.0); }

static u32 rng_next(u32* state)
{
    u32 x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static f32 rng_unit(u32* state) { return (f32)(rng_next(state) >> 8) / (f32)(1u << 24); }

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

static Mel_Mixer_Source* tone_source(const Mel_Alloc* a, f64 freq, f64 seconds, u32 samplerate, f32 amp)
{
    u32  frames = (u32)(seconds * (f64)samplerate);
    f32* buf = mel_alloc(a, sizeof(f32) * (usize)frames * (usize)HELLO_CH);
    for (u32 i = 0; i < frames; i++)
    {
        f64 t = (f64)i / (f64)samplerate;
        f64 fundamental = sin(HELLO_TAU * freq * t);
        f64 second = 0.5 * sin(HELLO_TAU * 2.0 * freq * t) * exp(-3.0 * t);
        f64 third = 0.25 * sin(HELLO_TAU * 3.0 * freq * t) * exp(-5.0 * t);
        f32 s = (f32)((fundamental + second + third) * (f64)amp);
        for (u32 c = 0; c < HELLO_CH; c++)
            buf[(usize)i * HELLO_CH + c] = s;
    }
    envelope(buf, frames, HELLO_CH, samplerate, 0.006, 0.040);
    Mel_Mixer_Source* src = mel_mixer_pcm_from_float(a, buf, frames, HELLO_CH, samplerate, MEL_MIXER_OWNERSHIP_OWNED);
    mel_dealloc(a, buf);
    return src;
}

static void build_demo_sources(Hello_State* s)
{
    static const i32 chord[] = { 60, 64, 67, 72 };
    static const i32 melody[] = { 72, 74, 76, 77, 79, 77, 76, 74, 72 };
    u32              chord_n = (u32)(sizeof(chord) / sizeof(chord[0]));
    u32              melody_n = (u32)(sizeof(melody) / sizeof(melody[0]));

    s->demo_count = chord_n + melody_n;
    s->demo_srcs = mel_alloc(s->alloc, sizeof(Mel_Mixer_Source*) * s->demo_count);
    for (u32 i = 0; i < chord_n; i++)
        s->demo_srcs[i] = tone_source(s->alloc, note_hz(chord[i]), 2.4, s->caps.samplerate, 0.32f);
    for (u32 i = 0; i < melody_n; i++)
        s->demo_srcs[chord_n + i] = tone_source(s->alloc, note_hz(melody[i]), 0.55, s->caps.samplerate, 0.34f);
}

static void build_stress_sources(Hello_State* s)
{
    static const i32 scale[] = { 48, 50, 52, 55, 57, 60, 62, 64, 67, 69, 72, 74 };
    s->stress_count = (u32)(sizeof(scale) / sizeof(scale[0]));
    s->stress_srcs = mel_alloc(s->alloc, sizeof(Mel_Mixer_Source*) * s->stress_count);
    for (u32 i = 0; i < s->stress_count; i++)
    {
        s->stress_srcs[i] = tone_source(s->alloc, note_hz(scale[i]), 2.0, s->caps.samplerate, 0.5f);
        mel_mixer_pcm_set_loop(s->stress_srcs[i], true, 0.0);
    }
}

static void free_sources(Hello_State* s, Mel_Mixer_Source** srcs, u32 count)
{
    if (srcs == NULL)
        return;
    for (u32 i = 0; i < count; i++)
        if (srcs[i] != NULL && srcs[i]->source_free != NULL)
            srcs[i]->source_free(srcs[i], s->alloc);
    mel_dealloc(s->alloc, srcs);
}

static void update_mode_label(Hello_State* s)
{
    if (mel_gui_handle_is_none(s->mode_label))
        return;
    char text[160];
    snprintf(text,
             sizeof text,
             "mode: %s   device: %uHz %uch  block %u  ring %u  latency %u",
             s->mode == HELLO_MODE_STRESS ? "STRESS" : "DEMO",
             s->caps.samplerate,
             s->caps.channels,
             s->caps.block_frames,
             s->caps.ring_blocks,
             s->caps.latency_frames);
    mel_gui_set_text(s->mode_label, str8_from_cstr(text));
}

static void update_count_label(Hello_State* s)
{
    if (mel_gui_handle_is_none(s->count_label))
        return;
    char text[128];
    snprintf(text, sizeof text, "voices: %u  (peak %u)   %s", s->active_voices, s->peak_voices, s->sequence_active ? "playing" : "idle");
    mel_gui_set_text(s->count_label, str8_from_cstr(text));
}

static void reset_sequence(Hello_State* s, Hello_Mode mode)
{
    if (s->engine != NULL)
        mel_mixer_stop_all(s->engine);
    mel_mixer_set_master_volume(s->engine, mode == HELLO_MODE_STRESS ? 0.5f : 0.8f);
    s->mode = mode;
    s->seq_step = 0u;
    s->seq_elapsed = 0.0;
    s->stress_spawned = 0u;
    s->sequence_active = true;
    update_mode_label(s);
    mel_log_info(HELLO_DOMAIN, "sequence start mode=%s", mode == HELLO_MODE_STRESS ? "stress" : "demo");
}

static void demo_step(Hello_State* s, f64 dt)
{
    static const f64 chord_at[] = { 0.0, 0.22, 0.44, 0.66 };
    static const f64 melody_base = 1.56;
    static const f64 melody_dt = 0.34;

    u32 chord_n = 4u;
    u32 melody_n = s->demo_count - chord_n;

    f64 prev = s->seq_elapsed;
    f64 now = prev + dt;
    s->seq_elapsed = now;

    for (u32 i = 0; i < chord_n; i++)
    {
        if (prev <= chord_at[i] && now > chord_at[i])
        {
            f32 pan = -0.4f + 0.27f * (f32)i;
            mel_mixer_play_ex(s->engine, s->demo_srcs[i], 0.9f, pan, false);
        }
    }
    for (u32 i = 0; i < melody_n; i++)
    {
        f64 at = melody_base + melody_dt * (f64)i;
        if (prev <= at && now > at)
        {
            f32 pan = (rng_unit(&s->rng) - 0.5f) * 0.6f;
            mel_mixer_play_ex(s->engine, s->demo_srcs[chord_n + i], 0.85f, pan, false);
        }
    }

    f64 fade_at = melody_base + melody_dt * (f64)melody_n + 0.5;
    if (prev <= fade_at && now > fade_at)
        mel_mixer_fade_master_volume(s->engine, 0.0f, 1.2);

    if (now > fade_at + 1.4)
        s->sequence_active = false;
}

static void stress_step(Hello_State* s, f64 dt)
{
    f64 prev = s->seq_elapsed;
    f64 now = prev + dt;
    s->seq_elapsed = now;

    f64 ramp = 6.0;
    f64 spawn_per_s = (f64)HELLO_STRESS_TARGET / ramp;
    u32 want = (u32)(now * spawn_per_s);
    if (want > HELLO_STRESS_TARGET)
        want = HELLO_STRESS_TARGET;

    while (s->stress_spawned < want)
    {
        Mel_Mixer_Source* src = s->stress_srcs[s->stress_spawned % s->stress_count];
        f32               pan = (rng_unit(&s->rng) - 0.5f) * 1.8f;
        f32               vol = 0.18f + 0.12f * rng_unit(&s->rng);
        Mel_Mixer_Voice   v = mel_mixer_play_ex(s->engine, src, 0.0f, pan, false);
        mel_mixer_set_play_speed(s->engine, v, 0.5 + 1.4 * (f64)rng_unit(&s->rng));
        mel_mixer_fade_volume(s->engine, v, vol, 0.8 + 1.5 * (f64)rng_unit(&s->rng));
        mel_mixer_oscillate_volume(s->engine, v, vol * 0.4f, vol, 2.0 + 4.0 * (f64)rng_unit(&s->rng));
        s->stress_spawned++;
    }

    f64 hold_until = ramp + 4.0;
    if (prev <= hold_until && now > hold_until)
        mel_mixer_fade_master_volume(s->engine, 0.0f, 1.5);
    if (now > hold_until + 1.7)
    {
        mel_mixer_stop_all(s->engine);
        s->sequence_active = false;
    }
}

static bool seq_tick(void* user)
{
    Hello_State* s = user;
    if (s->engine == NULL || !s->sequence_active)
        return true;
    f64 dt = 1.0 / (f64)HELLO_SEQ_HZ;
    if (s->mode == HELLO_MODE_STRESS)
        stress_step(s, dt);
    else
        demo_step(s, dt);
    return true;
}

static bool meter_tick(void* user)
{
    Hello_State* s = user;
    if (s->engine == NULL)
        return true;
    s->active_voices = mel_mixer_active_voice_count(s->engine);
    if (s->active_voices > s->peak_voices)
        s->peak_voices = s->active_voices;
    update_count_label(s);
    if (!mel_gui_handle_is_none(s->canvas))
        mel_gui_invalidate(s->canvas);
    return true;
}

static void teardown(Hello_State* s)
{
    if (s->torn_down)
        return;
    s->torn_down = true;

    if (s->meter_timer != NULL)
        mel_vat_tick_close(s->meter_timer);
    if (s->seq_timer != NULL)
        mel_vat_tick_close(s->seq_timer);
    if (s->auto_timer != NULL)
        mel_vat_tick_close(s->auto_timer);
    s->meter_timer = NULL;
    s->seq_timer = NULL;
    s->auto_timer = NULL;

    if (s->engine != NULL)
    {
        mel_mixer_destroy(s->engine);
        s->engine = NULL;
    }

    free_sources(s, s->demo_srcs, s->demo_count);
    free_sources(s, s->stress_srcs, s->stress_count);
    s->demo_srcs = NULL;
    s->stress_srcs = NULL;
    mel_log_info(HELLO_DOMAIN, "torn down (peak voices %u)", s->peak_voices);
}

static bool auto_tick(void* user)
{
    Hello_State* s = user;
    s->auto_elapsed += 1.0 / (f64)HELLO_SEQ_HZ;
    if (s->auto_elapsed >= s->auto_seconds && !s->auto_quitting)
    {
        s->auto_quitting = true;
        mel_log_info(HELLO_DOMAIN, "auto run complete at %.1fs, quitting", s->auto_elapsed);
        teardown(s);
        mel_vat_quit(s->vat);
        return false;
    }
    return true;
}

static void kick(Hello_State* s, Hello_Mode mode)
{
    s->started = true;
    reset_sequence(s, mode);
    meter_tick(s);
}

static void demo_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    kick((Hello_State*)user, HELLO_MODE_DEMO);
}

static void stress_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    kick((Hello_State*)user, HELLO_MODE_STRESS);
}

static void canvas_key_down(Mel_Gui_Handle h, Mel_Key key, void* user)
{
    (void)h;
    Hello_State* s = user;
    if (key == MEL_KEY_S)
        kick(s, HELLO_MODE_STRESS);
    else if (key == MEL_KEY_D || key == MEL_KEY_SPACE)
        kick(s, HELLO_MODE_DEMO);
}

static void canvas_pointer_down(Mel_Gui_Handle h, i32 x, i32 y, void* user)
{
    (void)h;
    (void)x;
    (void)y;
    Hello_State* s = user;
    kick(s, s->mode);
}

static void canvas_paint(Mel_Gui_Handle h, Mel_Painter* p, i32 w, i32 height, void* user)
{
    (void)h;
    Hello_State* s = user;

    mel_painter_clear(p, mel_color8_rgb(22, 26, 34));
    mel_painter_stroke_rect(p, mel_rect(1, 1, (f32)w - 2, (f32)height - 2), mel_color8_rgb(70, 84, 104), 1.5f);

    f32 cap = s->mode == HELLO_MODE_STRESS ? (f32)HELLO_STRESS_TARGET : 16.0f;
    f32 frac = cap > 0.0f ? (f32)s->active_voices / cap : 0.0f;
    if (frac > 1.0f)
        frac = 1.0f;

    f32 margin = 16.0f;
    f32 bar_x = margin;
    f32 bar_y = (f32)height * 0.5f;
    f32 bar_w = (f32)w - 2.0f * margin;
    f32 bar_h = (f32)height * 0.28f;

    mel_painter_fill_round_rect(p, mel_rect(bar_x, bar_y, bar_w, bar_h), 6.0f, mel_color8_rgb(34, 40, 52));
    if (frac > 0.0f)
    {
        u8 r = (u8)(90.0f + 150.0f * frac);
        u8 g = (u8)(200.0f - 120.0f * frac);
        mel_painter_fill_round_rect(p, mel_rect(bar_x, bar_y, bar_w * frac, bar_h), 6.0f, mel_color8_rgb(r, g, 120));
    }

    char head[96];
    snprintf(head, sizeof head, "%s  -  %u voices (peak %u)", s->mode == HELLO_MODE_STRESS ? "STRESS" : "DEMO", s->active_voices, s->peak_voices);
    mel_painter_draw_text(p, str8_from_cstr(head), mel_vec2(margin, 14.0f), mel_color8_rgb(220, 228, 240), 16.0f);

    const char* hint = s->started ? "D = demo   S = stress   click = replay" : "click or press D / S to play";
    mel_painter_draw_text(p, str8_from_cstr(hint), mel_vec2(margin, bar_y + bar_h + 12.0f), mel_color8_rgb(150, 168, 190), 13.0f);
}

static void screen_on_destroy(Mel_Gui_Handle frame, void* arg)
{
    (void)frame;
    (void)arg;
    teardown(&g_app);
}

static Hello_Mode initial_mode(void)
{
    const char* env = getenv("HELLO_AUDIO_MODE");
    if (env != NULL && strcmp(env, "stress") == 0)
        return HELLO_MODE_STRESS;
    return HELLO_MODE_DEMO;
}

static void build_main(Mel_Gui_Handle frame, void* user)
{
    Hello_State* s = user;
    s->frame = frame;

    mel_gui_set_text(frame, S8("Melody Audio"));
    mel_gui_set_layout(frame, mel_column_layout(.spacing = 8, .margin = 16, .cross_align = MEL_ALIGN_STRETCH));

    s->mode_label = mel_label_create(frame, .text = S8(""), .layoutable = { .preferred_h = 26 });
    s->count_label = mel_label_create(frame, .text = S8("voices: 0"), .layoutable = { .preferred_h = 24 });

    mel_button_create(frame, .text = S8("Play Demo (C-major arpeggio + scale)"), .pointer.on_click = demo_clicked, .user = s, .layoutable = { .preferred_h = 40 });
    mel_button_create(frame, .text = S8("Play Stress (ramp to ~192 voices)"), .pointer.on_click = stress_clicked, .user = s, .layoutable = { .preferred_h = 40 });

    s->canvas = mel_canvas_create(frame, .on_.on_paint = canvas_paint, .pointer.on_pointer_down = canvas_pointer_down, .keyboard.on_key_down = canvas_key_down, .user = s, .layoutable = { .preferred_h = 180, .weight = 1 });

    update_mode_label(s);
    update_count_label(s);
    mel_gui_set_focus(s->canvas);
}

void mel_app_setup(Mel_Vat* root)
{
    Hello_State* s = &g_app;
    memset(s, 0, sizeof *s);
    s->alloc = mel_alloc_heap();
    s->vat = root;
    s->rng = 0xC0FFEEu;
    s->mode = initial_mode();

    const char* auto_env = getenv("HELLO_AUDIO_AUTO");
    if (auto_env != NULL)
        s->auto_seconds = atof(auto_env);

    mel_gui_init(root);

    Mel_Executor* exec = mel_vat_executor(root);
    Mel_Mixer_Opt opt = {
        .samplerate = HELLO_SR,
        .channels = HELLO_CH,
        .block_frames = HELLO_BLOCK,
        .ring_blocks = HELLO_RING,
        .master_volume = s->mode == HELLO_MODE_STRESS ? 0.5f : 0.8f,
        .resampler = NULL,
        .exec = exec,
        .max_voice_channels = HELLO_MAXCH,
        .max_voice_ratio = HELLO_MAXRATIO,
    };

    s->engine = mel_mixer_create(s->alloc, opt);
    if (s->engine == NULL)
    {
        mel_log_fatal(HELLO_DOMAIN, "mel_mixer_create returned NULL (no audio device)");
        abort();
    }
    s->caps = mel_mixer_caps(s->engine);
    mel_log_info(HELLO_DOMAIN, "device %uHz %uch block %u ring %u latency %u", s->caps.samplerate, s->caps.channels, s->caps.block_frames, s->caps.ring_blocks, s->caps.latency_frames);

    build_demo_sources(s);
    build_stress_sources(s);

    s->meter_timer = mel_vat_tick_open(root, s->alloc, (i64)MEL_NANOS_PER_SEC / HELLO_METER_HZ, meter_tick, s);
    if (s->meter_timer == NULL)
    {
        mel_log_fatal(HELLO_DOMAIN, "failed to create meter timer");
        abort();
    }

    s->seq_timer = mel_vat_tick_open(root, s->alloc, (i64)MEL_NANOS_PER_SEC / HELLO_SEQ_HZ, seq_tick, s);
    if (s->seq_timer == NULL)
    {
        mel_log_fatal(HELLO_DOMAIN, "failed to create sequencer timer");
        abort();
    }

    mel_app_register_screen(S8("main"), .build = build_main, .user = s, .on_destroy = screen_on_destroy);
    mel_app_present(S8("main"), NULL);

    kick(s, s->mode);

    if (s->auto_seconds > 0.0)
    {
        s->auto_timer = mel_vat_tick_open(root, s->alloc, (i64)MEL_NANOS_PER_SEC / HELLO_SEQ_HZ, auto_tick, s);
        if (s->auto_timer == NULL)
        {
            mel_log_fatal(HELLO_DOMAIN, "failed to create auto timer");
            abort();
        }
    }
}
