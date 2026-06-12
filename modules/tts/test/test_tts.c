#include <test/test.h>

#include <tts/tts.h>
#include <tts/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/slotmap.h>
#include <string/str8.h>

#include <string.h>

void mel_tts__register_host_providers(void) {}

#define MOCK_RENDER_FRAMES 4u

typedef struct
{
    u64                stable_id;
    const char*        name;
    const char*        language;
    const char*        viseme_set;
    Mel_Tts_Voice_Caps caps;
} Mock_Voice;

typedef struct
{
    Mock_Voice voices[8];
    u32        count;

    u32             speak_calls;
    u32             render_calls;
    u32             pause_calls;
    u32             resume_calls;
    u32             abort_calls;
    u64             last_stable_id;
    u64             last_token;
    Mel_Tts_Lowered last_lowered;
    Mel_Tts_Sink    sink;
    bool            complete_inline;
    bool            render_inline;
    Mel_Tts_Status  refuse;
    f32             render_buf[MOCK_RENDER_FRAMES];
} Mock_State;

static Mock_State mock1;
static Mock_State mock2;

static str8 cstr(const char* s) { return s ? (str8){ (u8*)s, (size)strlen(s) } : STR8_EMPTY; }

static u32 mock_enumerate(void* user, const Mel_Alloc* alloc, Mel_Tts_Voice_Raw* out, u32 cap)
{
    MEL_UNUSED(alloc);
    Mock_State* m = user;
    u32         n = m->count < cap ? m->count : cap;
    for (u32 i = 0; i < n; i++)
        out[i] = (Mel_Tts_Voice_Raw){
            .stable_id = m->voices[i].stable_id,
            .name = cstr(m->voices[i].name),
            .language = cstr(m->voices[i].language),
            .viseme_set = cstr(m->voices[i].viseme_set),
            .caps = m->voices[i].caps,
        };
    return m->count;
}

static Mel_Tts_Status mock_speak(void* user, u64 stable_id, u64 token, const Mel_Tts_Lowered* lowered, Mel_Tts_Sink sink)
{
    Mock_State* m = user;
    m->speak_calls++;
    m->last_stable_id = stable_id;
    m->last_token = token;
    m->last_lowered = *lowered;
    if (m->refuse != 0)
        return m->refuse;
    m->sink = sink;
    if (m->complete_inline)
    {
        if (lowered->want_ranges && sink.on_range)
            sink.on_range(sink.token, (Mel_Tts_Range){ .offset = 0, .length = 5 });
        if (lowered->want_visemes && sink.on_viseme)
            sink.on_viseme(sink.token, (Mel_Tts_Viseme){ .viseme = 7, .range = { .offset = 0, .length = 5 } });
        sink.on_done(sink.token, MEL_TTS_OK);
    }
    return MEL_TTS_OK;
}

static void mock_pause(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    Mock_State* m = user;
    m->pause_calls++;
}

static void mock_resume(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    Mock_State* m = user;
    m->resume_calls++;
}

static void mock_abort(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(stable_id);
    Mock_State* m = user;
    m->abort_calls++;
    m->last_token = token;
}

static Mel_Tts_Status mock_render(void* user, u64 stable_id, u64 token, const Mel_Tts_Lowered* lowered, Mel_Tts_Sink sink)
{
    Mock_State* m = user;
    m->render_calls++;
    m->last_stable_id = stable_id;
    m->last_token = token;
    m->last_lowered = *lowered;
    if (m->refuse != 0)
        return m->refuse;
    m->sink = sink;
    if (m->render_inline)
    {
        for (u32 i = 0; i < MOCK_RENDER_FRAMES; i++)
            m->render_buf[i] = 0.5f;
        Mel_Tts_Render pcm = { .frames = m->render_buf, .frame_count = MOCK_RENDER_FRAMES, .sample_rate = 22050, .channels = 1 };
        sink.on_render(sink.token, &pcm, MEL_TTS_OK);
        for (u32 i = 0; i < MOCK_RENDER_FRAMES; i++)
            m->render_buf[i] = -1.0f;
    }
    return MEL_TTS_OK;
}

static void mock_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
}

static const Mel_Tts_Provider_Desc MOCK_DESC1 = {
    .name = "mock1",
    .user = &mock1,
    .enumerate_voices = mock_enumerate,
    .speak = mock_speak,
    .pause = mock_pause,
    .resume = mock_resume,
    .abort = mock_abort,
    .render = mock_render,
    .shutdown = mock_shutdown,
};

static const Mel_Tts_Provider_Desc MOCK_DESC2 = {
    .name = "mock2",
    .user = &mock2,
    .enumerate_voices = mock_enumerate,
    .speak = mock_speak,
    .abort = mock_abort,
};

static const Mel_Tts_Voice_Caps FULL_CAPS = {
    .rate = true,
    .rate_min = 0.5f,
    .rate_max = 2.0f,
    .pitch = true,
    .volume = true,
    .ranges = true,
    .can_pause = true,
    .render = true,
    .ssml = true,
    .visemes = true,
};

static void mock_reset(Mock_State* m) { memset(m, 0, sizeof *m); }

static void install(void)
{
    mock_reset(&mock1);
    mock1.voices[0] = (Mock_Voice){ .stable_id = 1, .name = "Full", .language = "en-US", .viseme_set = "mock-visemes", .caps = FULL_CAPS };
    mock1.voices[1] = (Mock_Voice){ .stable_id = 2, .name = "Plain", .language = "it-IT", .caps = { 0 } };
    mock1.count = 2;
    mel_tts_init(mel_alloc_heap());
    mel_tts_provider_register(&MOCK_DESC1);
    mel_tts_refresh();
}

static Mel_Tts_Voice find_voice(str8 name)
{
    Mel_Tts_Voice list[8];
    u32           n = mel_tts_voice_list(list, 8);
    for (u32 i = 0; i < n; i++)
    {
        Mel_Tts_Voice_Describe_Result d = mel_tts_voice_describe(list[i]);
        if (!mel_tts_failed(d.status) && str8_equals(d.value.name, name))
            return list[i];
    }
    return MEL_TTS_VOICE_NULL;
}

typedef struct
{
    u32            complete_calls;
    Mel_Tts_Status complete_status;
    u32            range_calls;
    Mel_Tts_Range  last_range;
    u32            viseme_calls;
    Mel_Tts_Viseme last_viseme;
    u32            render_calls;
    Mel_Tts_Status render_status;
    bool           pcm_was_null;
    u32            frame_count;
    u32            sample_rate;
    u32            channels;
    f32            captured[MOCK_RENDER_FRAMES];
} Tally;

static void on_complete(Mel_Tts_Utterance u, Mel_Tts_Status status, void* user)
{
    MEL_UNUSED(u);
    Tally* t = user;
    t->complete_calls++;
    t->complete_status = status;
}

static void on_range(Mel_Tts_Utterance u, Mel_Tts_Range range, void* user)
{
    MEL_UNUSED(u);
    Tally* t = user;
    t->range_calls++;
    t->last_range = range;
}

static void on_viseme(Mel_Tts_Utterance u, Mel_Tts_Viseme viseme, void* user)
{
    MEL_UNUSED(u);
    Tally* t = user;
    t->viseme_calls++;
    t->last_viseme = viseme;
}

static void on_render(Mel_Tts_Utterance u, const Mel_Tts_Render* pcm, Mel_Tts_Status status, void* user)
{
    MEL_UNUSED(u);
    Tally* t = user;
    t->render_calls++;
    t->render_status = status;
    if (pcm == NULL)
    {
        t->pcm_was_null = true;
        return;
    }
    t->frame_count = pcm->frame_count;
    t->sample_rate = pcm->sample_rate;
    t->channels = pcm->channels;
    for (u32 i = 0; i < pcm->frame_count && i < MOCK_RENDER_FRAMES; i++)
        t->captured[i] = pcm->frames[i];
}

static bool status_ok(Mel_Tts_Status s) { return (s & MEL_TTS_SEVERITY_MASK) == MEL_TTS_OK; }

MEL_TEST(tts, enumerates_and_describes)
{
    install();
    MEL_EXPECT_EQ(mel_tts_voice_count(), 2u);

    Mel_Tts_Voice full = find_voice(S8("Full"));
    MEL_REQUIRE(mel_tts_voice_alive(full));

    Mel_Tts_Voice_Describe_Result d = mel_tts_voice_describe(full);
    MEL_REQUIRE(!mel_tts_failed(d.status));
    MEL_EXPECT_EQ_STR8(d.value.name, S8("Full"));
    MEL_EXPECT_EQ_STR8(d.value.language, S8("en-US"));
    MEL_EXPECT_EQ_STR8(d.value.viseme_set, S8("mock-visemes"));
    MEL_EXPECT(d.value.caps.rate);
    MEL_EXPECT_FLOAT_EQ(d.value.caps.rate_min, 0.5f, 0.0f);
    MEL_EXPECT_FLOAT_EQ(d.value.caps.rate_max, 2.0f, 0.0f);
    MEL_EXPECT(d.value.caps.can_pause);
    MEL_EXPECT(d.value.caps.render);
    MEL_EXPECT(d.value.caps.ssml);
    MEL_EXPECT(d.value.caps.visemes);

    Mel_Tts_Voice plain = find_voice(S8("Plain"));
    MEL_REQUIRE(mel_tts_voice_alive(plain));
    d = mel_tts_voice_describe(plain);
    MEL_REQUIRE(!mel_tts_failed(d.status));
    MEL_EXPECT(!d.value.caps.rate);
    MEL_EXPECT(!d.value.caps.render);
    MEL_EXPECT(str8_is_empty(d.value.viseme_set));

    d = mel_tts_voice_describe(MEL_TTS_VOICE_NULL);
    MEL_EXPECT(mel_tts_failed(d.status));
    MEL_EXPECT(d.status & MEL_TTS_RESULT_LOST);

    mel_tts_shutdown();
}

MEL_TEST(tts, lowering_names_every_loss)
{
    install();
    mock1.complete_inline = true;
    Mel_Tts_Voice plain = find_voice(S8("Plain"));
    Tally         t = { 0 };

    Mel_Tts_Speak_Result r = mel_tts_speak(plain, S8("hello"), .rate = 2.0f, .pitch = 1.5f, .volume = 0.5f, .on_range = on_range, .on_viseme = on_viseme, .on_complete = on_complete, .user = &t);
    MEL_REQUIRE(!mel_tts_failed(r.status));
    MEL_EXPECT(mel_tts_warned(r.status));
    MEL_EXPECT(r.status & MEL_TTS_WARN_RATE_CLAMPED);
    MEL_EXPECT(r.status & MEL_TTS_WARN_PITCH_DROPPED);
    MEL_EXPECT(r.status & MEL_TTS_WARN_VOLUME_DROPPED);
    MEL_EXPECT(r.status & MEL_TTS_WARN_RANGES_DROPPED);
    MEL_EXPECT(r.status & MEL_TTS_WARN_VISEMES_DROPPED);

    MEL_EXPECT_EQ(mock1.speak_calls, 1u);
    MEL_EXPECT_FLOAT_EQ(mock1.last_lowered.rate, 0.0f, 0.0f);
    MEL_EXPECT_FLOAT_EQ(mock1.last_lowered.pitch, 0.0f, 0.0f);
    MEL_EXPECT_FLOAT_EQ(mock1.last_lowered.volume, 0.0f, 0.0f);
    MEL_EXPECT(!mock1.last_lowered.want_ranges);
    MEL_EXPECT(!mock1.last_lowered.want_visemes);
    MEL_EXPECT_EQ(t.range_calls, 0u);
    MEL_EXPECT_EQ(t.viseme_calls, 0u);
    MEL_EXPECT_EQ(t.complete_calls, 1u);
    MEL_EXPECT(status_ok(t.complete_status));

    mel_tts_shutdown();
}

MEL_TEST(tts, lowering_clamps_rate_to_caps)
{
    install();
    mock1.complete_inline = true;
    Mel_Tts_Voice full = find_voice(S8("Full"));
    Tally         t = { 0 };

    Mel_Tts_Speak_Result r = mel_tts_speak(full, S8("fast"), .rate = 3.0f, .on_complete = on_complete, .user = &t);
    MEL_EXPECT(mel_tts_warned(r.status));
    MEL_EXPECT(r.status & MEL_TTS_WARN_RATE_CLAMPED);
    MEL_EXPECT_FLOAT_EQ(mock1.last_lowered.rate, 2.0f, 0.0f);

    r = mel_tts_speak(full, S8("slow"), .rate = 0.1f, .on_complete = on_complete, .user = &t);
    MEL_EXPECT(mel_tts_warned(r.status));
    MEL_EXPECT(r.status & MEL_TTS_WARN_RATE_CLAMPED);
    MEL_EXPECT_FLOAT_EQ(mock1.last_lowered.rate, 0.5f, 0.0f);

    r = mel_tts_speak(full, S8("fine"), .rate = 1.5f, .on_complete = on_complete, .user = &t);
    MEL_EXPECT(status_ok(r.status));
    MEL_EXPECT_FLOAT_EQ(mock1.last_lowered.rate, 1.5f, 0.0f);

    r = mel_tts_speak(full, S8("native"), .on_complete = on_complete, .user = &t);
    MEL_EXPECT(status_ok(r.status));
    MEL_EXPECT_FLOAT_EQ(mock1.last_lowered.rate, 0.0f, 0.0f);

    MEL_EXPECT_EQ(t.complete_calls, 4u);
    mel_tts_shutdown();
}

MEL_TEST(tts, ranges_and_visemes_delivered_when_capped)
{
    install();
    mock1.complete_inline = true;
    Mel_Tts_Voice full = find_voice(S8("Full"));
    Tally         t = { 0 };

    Mel_Tts_Speak_Result r = mel_tts_speak(full, S8("hello"), .on_range = on_range, .on_viseme = on_viseme, .on_complete = on_complete, .user = &t);
    MEL_EXPECT(status_ok(r.status));
    MEL_EXPECT(mock1.last_lowered.want_ranges);
    MEL_EXPECT(mock1.last_lowered.want_visemes);
    MEL_EXPECT_EQ(t.range_calls, 1u);
    MEL_EXPECT_EQ(t.last_range.length, (usize)5);
    MEL_EXPECT_EQ(t.viseme_calls, 1u);
    MEL_EXPECT_EQ(t.last_viseme.viseme, 7u);
    MEL_EXPECT_EQ(t.complete_calls, 1u);

    mel_tts_shutdown();
}

MEL_TEST(tts, ssml_unsupported_fails_loud)
{
    install();
    Mel_Tts_Voice plain = find_voice(S8("Plain"));
    Tally         t = { 0 };

    Mel_Tts_Speak_Result r = mel_tts_speak(plain, S8("<speak>hi</speak>"), .ssml = true, .on_complete = on_complete, .user = &t);
    MEL_EXPECT(mel_tts_failed(r.status));
    MEL_EXPECT(r.status & MEL_TTS_RESULT_UNSUPPORTED);
    MEL_EXPECT_EQ(mock1.speak_calls, 0u);
    MEL_EXPECT_EQ(t.complete_calls, 0u);
    MEL_EXPECT(!mel_tts_speaking(r.value));

    mel_tts_shutdown();
}

MEL_TEST(tts, render_unsupported_fails_loud)
{
    install();
    Mel_Tts_Voice plain = find_voice(S8("Plain"));
    Tally         t = { 0 };

    Mel_Tts_Speak_Result r = mel_tts_render(plain, S8("hi"), .on_render = on_render, .user = &t);
    MEL_EXPECT(mel_tts_failed(r.status));
    MEL_EXPECT(r.status & MEL_TTS_RESULT_UNSUPPORTED);
    MEL_EXPECT_EQ(mock1.render_calls, 0u);
    MEL_EXPECT_EQ(t.render_calls, 0u);

    Mel_Tts_Voice full = find_voice(S8("Full"));
    r = mel_tts_render(full, S8("hi"), .user = &t);
    MEL_EXPECT(mel_tts_failed(r.status));
    MEL_EXPECT_EQ(mock1.render_calls, 0u);

    r = mel_tts_render(full, S8("<speak>hi</speak>"), .ssml = true, .on_render = on_render, .user = &t);
    MEL_EXPECT(status_ok(r.status) || mel_tts_warned(r.status));
    MEL_EXPECT_EQ(mock1.render_calls, 1u);
    MEL_EXPECT(mock1.last_lowered.ssml);
    mel_tts_abort(r.value);

    mel_tts_shutdown();
}

MEL_TEST(tts, empty_text_fails_loud)
{
    install();
    Mel_Tts_Voice full = find_voice(S8("Full"));
    Tally         t = { 0 };

    Mel_Tts_Speak_Result r = mel_tts_speak(full, STR8_EMPTY, .on_complete = on_complete, .user = &t);
    MEL_EXPECT(mel_tts_failed(r.status));
    MEL_EXPECT_EQ(mock1.speak_calls, 0u);

    r = mel_tts_render(full, STR8_EMPTY, .on_render = on_render, .user = &t);
    MEL_EXPECT(mel_tts_failed(r.status));
    MEL_EXPECT_EQ(mock1.render_calls, 0u);

    mel_tts_shutdown();
}

MEL_TEST(tts, speak_completes_exactly_once)
{
    install();
    Mel_Tts_Voice full = find_voice(S8("Full"));
    Tally         t = { 0 };

    Mel_Tts_Speak_Result r = mel_tts_speak(full, S8("hello"), .on_complete = on_complete, .user = &t);
    MEL_REQUIRE(status_ok(r.status));
    MEL_EXPECT(mel_tts_speaking(r.value));
    MEL_EXPECT_EQ(t.complete_calls, 0u);

    mock1.sink.on_done(mock1.sink.token, MEL_TTS_OK);
    MEL_EXPECT_EQ(t.complete_calls, 1u);
    MEL_EXPECT(status_ok(t.complete_status));
    MEL_EXPECT(!mel_tts_speaking(r.value));
    MEL_EXPECT(!mel_tts_paused(r.value));

    mock1.sink.on_done(mock1.sink.token, MEL_TTS_ERROR | MEL_TTS_RESULT_AUDIO);
    MEL_EXPECT_EQ(t.complete_calls, 1u);

    MEL_EXPECT(mel_tts_failed(mel_tts_pause(r.value)));

    mel_tts_shutdown();
}

MEL_TEST(tts, provider_error_terminal_propagates)
{
    install();
    Mel_Tts_Voice full = find_voice(S8("Full"));
    Tally         t = { 0 };

    Mel_Tts_Speak_Result r = mel_tts_speak(full, S8("hello"), .on_complete = on_complete, .user = &t);
    MEL_REQUIRE(status_ok(r.status));

    mock1.sink.on_done(mock1.sink.token, MEL_TTS_ERROR | MEL_TTS_RESULT_NETWORK);
    MEL_EXPECT_EQ(t.complete_calls, 1u);
    MEL_EXPECT(mel_tts_failed(t.complete_status));
    MEL_EXPECT(t.complete_status & MEL_TTS_RESULT_NETWORK);

    mel_tts_shutdown();
}

MEL_TEST(tts, provider_synchronous_refusal_returns_error)
{
    install();
    mock1.refuse = MEL_TTS_ERROR | MEL_TTS_RESULT_BUSY;
    Mel_Tts_Voice full = find_voice(S8("Full"));
    Tally         t = { 0 };

    Mel_Tts_Speak_Result r = mel_tts_speak(full, S8("hello"), .on_complete = on_complete, .user = &t);
    MEL_EXPECT(mel_tts_failed(r.status));
    MEL_EXPECT(r.status & MEL_TTS_RESULT_BUSY);
    MEL_EXPECT_EQ(t.complete_calls, 0u);
    MEL_EXPECT(!mel_tts_speaking(r.value));

    mel_tts_shutdown();
}

MEL_TEST(tts, abort_is_idempotent_and_swallows_late_terminal)
{
    install();
    Mel_Tts_Voice full = find_voice(S8("Full"));
    Tally         t = { 0 };

    Mel_Tts_Speak_Result r = mel_tts_speak(full, S8("hello"), .on_complete = on_complete, .user = &t);
    MEL_REQUIRE(status_ok(r.status));

    mel_tts_abort(r.value);
    MEL_EXPECT_EQ(mock1.abort_calls, 1u);
    MEL_EXPECT_EQ(t.complete_calls, 1u);
    MEL_EXPECT(status_ok(t.complete_status));
    MEL_EXPECT(t.complete_status & MEL_TTS_RESULT_ABORTED);

    mock1.sink.on_done(mock1.sink.token, MEL_TTS_OK);
    MEL_EXPECT_EQ(t.complete_calls, 1u);

    mel_tts_abort(r.value);
    MEL_EXPECT_EQ(mock1.abort_calls, 1u);
    MEL_EXPECT_EQ(t.complete_calls, 1u);

    mel_tts_shutdown();
}

MEL_TEST(tts, abort_all_resolves_per_voice)
{
    install();
    Mel_Tts_Voice full = find_voice(S8("Full"));
    Mel_Tts_Voice plain = find_voice(S8("Plain"));
    Tally         tf = { 0 };
    Tally         tp = { 0 };

    Mel_Tts_Speak_Result rf1 = mel_tts_speak(full, S8("one"), .on_complete = on_complete, .user = &tf);
    Mel_Tts_Speak_Result rf2 = mel_tts_speak(full, S8("two"), .on_complete = on_complete, .user = &tf);
    Mel_Tts_Speak_Result rp = mel_tts_speak(plain, S8("three"), .on_complete = on_complete, .user = &tp);
    MEL_REQUIRE(status_ok(rf1.status));
    MEL_REQUIRE(status_ok(rf2.status));
    MEL_REQUIRE(status_ok(rp.status));

    mel_tts_abort_all(full);
    MEL_EXPECT_EQ(tf.complete_calls, 2u);
    MEL_EXPECT(tf.complete_status & MEL_TTS_RESULT_ABORTED);
    MEL_EXPECT_EQ(tp.complete_calls, 0u);
    MEL_EXPECT(mel_tts_speaking(rp.value));

    mel_tts_shutdown();
    MEL_EXPECT_EQ(tp.complete_calls, 1u);
}

MEL_TEST(tts, pause_resume_capability_gated)
{
    install();
    Mel_Tts_Voice full = find_voice(S8("Full"));
    Tally         t = { 0 };

    Mel_Tts_Speak_Result r = mel_tts_speak(full, S8("hello"), .on_complete = on_complete, .user = &t);
    MEL_REQUIRE(status_ok(r.status));

    MEL_EXPECT(status_ok(mel_tts_pause(r.value)));
    MEL_EXPECT_EQ(mock1.pause_calls, 1u);
    MEL_EXPECT(mel_tts_paused(r.value));
    MEL_EXPECT(!mel_tts_speaking(r.value));

    MEL_EXPECT(status_ok(mel_tts_pause(r.value)));
    MEL_EXPECT_EQ(mock1.pause_calls, 1u);

    MEL_EXPECT(status_ok(mel_tts_resume(r.value)));
    MEL_EXPECT_EQ(mock1.resume_calls, 1u);
    MEL_EXPECT(mel_tts_speaking(r.value));
    MEL_EXPECT(!mel_tts_paused(r.value));

    Mel_Tts_Voice        plain = find_voice(S8("Plain"));
    Mel_Tts_Speak_Result rp = mel_tts_speak(plain, S8("hi"), .on_complete = on_complete, .user = &t);
    MEL_REQUIRE(status_ok(rp.status));
    Mel_Tts_Status st = mel_tts_pause(rp.value);
    MEL_EXPECT(mel_tts_failed(st));
    MEL_EXPECT(st & MEL_TTS_RESULT_UNSUPPORTED);
    MEL_EXPECT_EQ(mock1.pause_calls, 1u);

    mel_tts_shutdown();
}

MEL_TEST(tts, pause_without_provider_entry_fails_loud)
{
    install();
    mock_reset(&mock2);
    mock2.voices[0] = (Mock_Voice){ .stable_id = 1, .name = "Limited", .language = "en-GB", .caps = { .can_pause = true } };
    mock2.count = 1;
    mel_tts_provider_register(&MOCK_DESC2);
    mel_tts_refresh();

    Mel_Tts_Voice limited = find_voice(S8("Limited"));
    MEL_REQUIRE(mel_tts_voice_alive(limited));
    Tally t = { 0 };

    Mel_Tts_Speak_Result r = mel_tts_speak(limited, S8("hi"), .on_complete = on_complete, .user = &t);
    MEL_REQUIRE(status_ok(r.status));

    Mel_Tts_Status st = mel_tts_pause(r.value);
    MEL_EXPECT(mel_tts_failed(st));
    MEL_EXPECT(st & MEL_TTS_RESULT_UNSUPPORTED);
    MEL_EXPECT(mel_tts_speaking(r.value));

    mel_tts_shutdown();
}

MEL_TEST(tts, render_delivers_borrowed_pcm_exactly_once)
{
    install();
    mock1.render_inline = true;
    Mel_Tts_Voice full = find_voice(S8("Full"));
    Tally         t = { 0 };

    Mel_Tts_Speak_Result r = mel_tts_render(full, S8("hi"), .rate = 1.2f, .on_render = on_render, .user = &t);
    MEL_EXPECT(status_ok(r.status));
    MEL_EXPECT_EQ(mock1.render_calls, 1u);
    MEL_EXPECT_FLOAT_EQ(mock1.last_lowered.rate, 1.2f, 0.0f);

    MEL_EXPECT_EQ(t.render_calls, 1u);
    MEL_EXPECT(status_ok(t.render_status));
    MEL_EXPECT(!t.pcm_was_null);
    MEL_EXPECT_EQ(t.frame_count, MOCK_RENDER_FRAMES);
    MEL_EXPECT_EQ(t.sample_rate, 22050u);
    MEL_EXPECT_EQ(t.channels, 1u);
    for (u32 i = 0; i < MOCK_RENDER_FRAMES; i++)
        MEL_EXPECT_FLOAT_EQ(t.captured[i], 0.5f, 0.0f);
    for (u32 i = 0; i < MOCK_RENDER_FRAMES; i++)
        MEL_EXPECT_FLOAT_EQ(mock1.render_buf[i], -1.0f, 0.0f);

    mock1.sink.on_render(mock1.sink.token, NULL, MEL_TTS_ERROR | MEL_TTS_RESULT_AUDIO);
    MEL_EXPECT_EQ(t.render_calls, 1u);

    mel_tts_shutdown();
}

MEL_TEST(tts, render_abort_resolves_null_pcm)
{
    install();
    Mel_Tts_Voice full = find_voice(S8("Full"));
    Tally         t = { 0 };

    Mel_Tts_Speak_Result r = mel_tts_render(full, S8("hi"), .on_render = on_render, .user = &t);
    MEL_REQUIRE(status_ok(r.status));
    MEL_EXPECT_EQ(t.render_calls, 0u);

    mel_tts_abort(r.value);
    MEL_EXPECT_EQ(mock1.abort_calls, 1u);
    MEL_EXPECT_EQ(t.render_calls, 1u);
    MEL_EXPECT(t.pcm_was_null);
    MEL_EXPECT(status_ok(t.render_status));
    MEL_EXPECT(t.render_status & MEL_TTS_RESULT_ABORTED);

    f32            late[2] = { 0.1f, 0.2f };
    Mel_Tts_Render pcm = { .frames = late, .frame_count = 2, .sample_rate = 8000, .channels = 1 };
    mock1.sink.on_render(mock1.sink.token, &pcm, MEL_TTS_OK);
    MEL_EXPECT_EQ(t.render_calls, 1u);

    mel_tts_shutdown();
}

MEL_TEST(tts, refresh_loss_resolves_live_utterances_lost)
{
    install();
    Mel_Tts_Voice full = find_voice(S8("Full"));
    Mel_Tts_Voice plain = find_voice(S8("Plain"));
    Tally         t = { 0 };

    Mel_Tts_Speak_Result r = mel_tts_speak(full, S8("hello"), .on_complete = on_complete, .user = &t);
    MEL_REQUIRE(status_ok(r.status));

    mock1.voices[0] = mock1.voices[1];
    mock1.count = 1;
    mel_tts_refresh();

    MEL_EXPECT_EQ(mel_tts_voice_count(), 1u);
    MEL_EXPECT(!mel_tts_voice_alive(full));
    MEL_EXPECT(mel_tts_voice_alive(plain));
    MEL_EXPECT(mel_tts_voice_equal(find_voice(S8("Plain")), plain));

    MEL_EXPECT_EQ(t.complete_calls, 1u);
    MEL_EXPECT(mel_tts_failed(t.complete_status));
    MEL_EXPECT(t.complete_status & MEL_TTS_RESULT_LOST);
    MEL_EXPECT(!mel_tts_speaking(r.value));

    mock1.sink.on_done(mock1.sink.token, MEL_TTS_OK);
    MEL_EXPECT_EQ(t.complete_calls, 1u);

    mel_tts_shutdown();
}

MEL_TEST(tts, refresh_keys_by_provider_and_stable_id)
{
    install();
    mock_reset(&mock2);
    mock2.voices[0] = (Mock_Voice){ .stable_id = 1, .name = "Twin", .language = "fr-FR", .caps = { 0 } };
    mock2.count = 1;
    mel_tts_provider_register(&MOCK_DESC2);
    mel_tts_refresh();

    MEL_EXPECT_EQ(mel_tts_voice_count(), 3u);
    MEL_EXPECT(mel_tts_voice_alive(find_voice(S8("Full"))));
    MEL_EXPECT(mel_tts_voice_alive(find_voice(S8("Twin"))));

    Mel_Tts_Voice list[8];
    MEL_EXPECT_EQ(mel_tts_voice_list(list, 8), 3u);

    mel_tts_shutdown();
}

MEL_TEST(tts, shutdown_aborts_live_work)
{
    install();
    Mel_Tts_Voice full = find_voice(S8("Full"));
    Tally         ts = { 0 };
    Tally         tr = { 0 };

    Mel_Tts_Speak_Result rs = mel_tts_speak(full, S8("hello"), .on_complete = on_complete, .user = &ts);
    Mel_Tts_Speak_Result rr = mel_tts_render(full, S8("hello"), .on_render = on_render, .user = &tr);
    MEL_REQUIRE(status_ok(rs.status));
    MEL_REQUIRE(status_ok(rr.status));

    mel_tts_shutdown();

    MEL_EXPECT_EQ(mock1.abort_calls, 2u);
    MEL_EXPECT_EQ(ts.complete_calls, 1u);
    MEL_EXPECT(status_ok(ts.complete_status));
    MEL_EXPECT(ts.complete_status & MEL_TTS_RESULT_ABORTED);
    MEL_EXPECT_EQ(tr.render_calls, 1u);
    MEL_EXPECT(tr.pcm_was_null);
    MEL_EXPECT(tr.render_status & MEL_TTS_RESULT_ABORTED);
    MEL_EXPECT_EQ(mel_tts_voice_count(), 0u);
}
