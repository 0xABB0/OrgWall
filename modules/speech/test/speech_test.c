#include <speech/provider.h>
#include <test/test.h>

#include <future/future.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>

#include <string.h>

#define MOCK_VOICE_FULL    0xF00DF001ull
#define MOCK_VOICE_LIMITED 0xF00DF002ull
#define MOCK_RECOGNIZER    0x5EC05EC1ull
#define MOCK_RECOGNIZER_NS 0x5EC05EC2ull

static bool mock_auth_granted;
static bool mock_voice_full_present;

static Mel_Speech_Sink mock_tts_sink;
static bool            mock_tts_active;
static f32             mock_tts_rate;
static f32             mock_tts_pitch;
static f32             mock_tts_volume;
static bool            mock_tts_want_ranges;
static u32             mock_tts_pause_calls;
static u32             mock_tts_resume_calls;
static u32             mock_tts_abort_calls;

static Mel_Speech_Sink mock_stt_sink;
static bool            mock_stt_active;
static bool            mock_stt_partials;
static u32             mock_stt_stop_calls;
static u32             mock_stt_abort_calls;

static u32 mock_enumerate_voices(void* user, const Mel_Alloc* alloc, Mel_Speech_Voice_Raw* out, u32 cap)
{
    (void)user;
    (void)alloc;
    u32 total = mock_voice_full_present ? 2 : 1;
    u32 n = 0;
    if (mock_voice_full_present && n < cap)
    {
        out[n].stable_id = MOCK_VOICE_FULL;
        out[n].name = S8("mock-full");
        out[n].language = S8("en-US");
        out[n].caps = (Mel_Speech_Voice_Caps){ .rate = true, .rate_min = 0.5f, .rate_max = 2.0f, .pitch = true, .volume = true, .ranges = true, .can_pause = true };
        n++;
    }
    if (n < cap)
    {
        out[n].stable_id = MOCK_VOICE_LIMITED;
        out[n].name = S8("mock-limited");
        out[n].language = S8("it-IT");
        out[n].caps = (Mel_Speech_Voice_Caps){ 0 };
        n++;
    }
    return total;
}

static u32 mock_enumerate_recognizers(void* user, const Mel_Alloc* alloc, Mel_Speech_Recognizer_Raw* out, u32 cap)
{
    (void)user;
    (void)alloc;
    if (cap >= 1)
    {
        out[0].stable_id = MOCK_RECOGNIZER;
        out[0].language = S8("en-US");
        out[0].caps = (Mel_Speech_Recognizer_Caps){ .on_device = true, .partials = true, .can_stop = true };
    }
    return 1;
}

static Mel_Speech_Status mock_speak(void* user, u64 stable_id, u64 token, const Mel_Speech_Speak_Lowered* lowered, Mel_Speech_Sink sink)
{
    (void)user;
    (void)stable_id;
    (void)token;
    mock_tts_sink = sink;
    mock_tts_active = true;
    mock_tts_rate = lowered->rate;
    mock_tts_pitch = lowered->pitch;
    mock_tts_volume = lowered->volume;
    mock_tts_want_ranges = lowered->want_ranges;
    return MEL_SPEECH_OK;
}

static void mock_speak_pause(void* user, u64 stable_id, u64 token)
{
    (void)user;
    (void)stable_id;
    (void)token;
    mock_tts_pause_calls++;
}

static void mock_speak_resume(void* user, u64 stable_id, u64 token)
{
    (void)user;
    (void)stable_id;
    (void)token;
    mock_tts_resume_calls++;
}

static void mock_speak_abort(void* user, u64 stable_id, u64 token)
{
    (void)user;
    (void)stable_id;
    (void)token;
    mock_tts_abort_calls++;
    mock_tts_active = false;
}

static const mel_speech_auth* mock_authorization(void* user)
{
    (void)user;
    return mock_auth_granted ? &mel_speech_auth_granted : &mel_speech_auth_denied;
}

static void mock_authorize(void* user, Mel_Speech_Sink sink)
{
    (void)user;
    if (sink.on_auth)
        sink.on_auth(sink.token, mock_auth_granted ? &mel_speech_auth_granted : &mel_speech_auth_denied);
}

static Mel_Speech_Status mock_listen(void* user, u64 stable_id, u64 token, const Mel_Speech_Listen_Lowered* lowered, Mel_Speech_Sink sink)
{
    (void)user;
    (void)stable_id;
    (void)token;
    mock_stt_sink = sink;
    mock_stt_active = true;
    mock_stt_partials = lowered->partials;
    return MEL_SPEECH_OK;
}

static void mock_listen_stop(void* user, u64 stable_id, u64 token)
{
    (void)user;
    (void)stable_id;
    (void)token;
    mock_stt_stop_calls++;
}

static void mock_listen_abort(void* user, u64 stable_id, u64 token)
{
    (void)user;
    (void)stable_id;
    (void)token;
    mock_stt_abort_calls++;
    mock_stt_active = false;
}

static const Mel_Speech_Provider_Desc MOCK_DESC = {
    .name = "mock",
    .enumerate_voices = mock_enumerate_voices,
    .enumerate_recognizers = mock_enumerate_recognizers,
    .speak = mock_speak,
    .speak_pause = mock_speak_pause,
    .speak_resume = mock_speak_resume,
    .speak_abort = mock_speak_abort,
    .authorization = mock_authorization,
    .authorize = mock_authorize,
    .listen = mock_listen,
    .listen_stop = mock_listen_stop,
    .listen_abort = mock_listen_abort,
};

static u32 mock_ns_enumerate_recognizers(void* user, const Mel_Alloc* alloc, Mel_Speech_Recognizer_Raw* out, u32 cap)
{
    (void)user;
    (void)alloc;
    if (cap >= 1)
    {
        out[0].stable_id = MOCK_RECOGNIZER_NS;
        out[0].language = S8("de-DE");
        out[0].caps = (Mel_Speech_Recognizer_Caps){ 0 };
    }
    return 1;
}

static const Mel_Speech_Provider_Desc MOCK_NS_DESC = {
    .name = "mock-no-stop",
    .enumerate_recognizers = mock_ns_enumerate_recognizers,
    .listen = mock_listen,
    .listen_abort = mock_listen_abort,
};

void mel_speech__register_host_providers(void) {}

static void install_mock(void)
{
    mock_auth_granted = true;
    mock_voice_full_present = true;
    mock_tts_sink = (Mel_Speech_Sink){ 0 };
    mock_tts_active = false;
    mock_tts_pause_calls = 0;
    mock_tts_resume_calls = 0;
    mock_tts_abort_calls = 0;
    mock_stt_sink = (Mel_Speech_Sink){ 0 };
    mock_stt_active = false;
    mock_stt_stop_calls = 0;
    mock_stt_abort_calls = 0;
    mel_speech_init(mel_alloc_heap());
    mel_speech_provider_register(&MOCK_DESC);
    mel_speech_provider_register(&MOCK_NS_DESC);
    mel_speech_refresh();
}

static Mel_Speech_Voice voice_named(str8 name)
{
    Mel_Speech_Voice list[4];
    u32              n = mel_speech_voice_list(list, 4);
    for (u32 i = 0; i < n; i++)
    {
        Mel_Speech_Voice_Describe_Result r = mel_speech_voice_describe(list[i]);
        if (str8_equals(r.value.name, name))
            return list[i];
    }
    MEL_REQUIRE(false);
    return MEL_SPEECH_VOICE_NULL;
}

static Mel_Speech_Recognizer recognizer_lang(str8 language)
{
    Mel_Speech_Recognizer list[4];
    u32                   n = mel_speech_recognizer_list(list, 4);
    for (u32 i = 0; i < n; i++)
    {
        Mel_Speech_Recognizer_Describe_Result r = mel_speech_recognizer_describe(list[i]);
        if (str8_equals(r.value.language, language))
            return list[i];
    }
    MEL_REQUIRE(false);
    return MEL_SPEECH_RECOGNIZER_NULL;
}

typedef struct
{
    u32               calls;
    Mel_Speech_Status last;
} Done_Sink;

static void on_speak_done(Mel_Speech_Utterance u, Mel_Speech_Status status, void* user)
{
    (void)u;
    Done_Sink* d = (Done_Sink*)user;
    d->calls++;
    d->last = status;
}

MEL_TEST(speech, enumerate_and_describe)
{
    install_mock();
    MEL_EXPECT_EQ(mel_speech_voice_count(), (u32)2);
    MEL_EXPECT_EQ(mel_speech_recognizer_count(), (u32)2);

    Mel_Speech_Voice                 full = voice_named(S8("mock-full"));
    Mel_Speech_Voice_Describe_Result vd = mel_speech_voice_describe(full);
    MEL_EXPECT_EQ(vd.status & MEL_SPEECH_SEVERITY_MASK, (Mel_Speech_Status)MEL_SPEECH_OK);
    MEL_EXPECT_EQ_STR8(vd.value.language, S8("en-US"));
    MEL_EXPECT(vd.value.caps.ranges);
    MEL_EXPECT(mel_speech_voice_alive(full));

    Mel_Speech_Recognizer                 rec = recognizer_lang(S8("en-US"));
    Mel_Speech_Recognizer_Describe_Result rd = mel_speech_recognizer_describe(rec);
    MEL_EXPECT_EQ(rd.status & MEL_SPEECH_SEVERITY_MASK, (Mel_Speech_Status)MEL_SPEECH_OK);
    MEL_EXPECT(rd.value.caps.partials);
    MEL_EXPECT(rd.value.caps.on_device);
    mel_speech_shutdown();
}

MEL_TEST(speech, describe_dead_handle_fails_loudly)
{
    install_mock();
    Mel_Speech_Voice_Describe_Result r = mel_speech_voice_describe(MEL_SPEECH_VOICE_NULL);
    MEL_EXPECT(mel_speech_failed(r.status));
    MEL_EXPECT(r.status & MEL_SPEECH_RESULT_NO_DEVICE);
    mel_speech_shutdown();
}

MEL_TEST(speech, authorize_future_resolves_granted)
{
    install_mock();
    MEL_EXPECT(mel_speech_auth_is_granted(mel_speech_authorization()));

    Mel_Future* f = mel_speech_authorize(mel_alloc_heap());
    MEL_REQUIRE(f != NULL);
    MEL_EXPECT(mel_future_resolved(f));
    const mel_speech_auth* a = mel_speech_future_auth(f);
    MEL_EXPECT(a == &mel_speech_auth_granted);
    mel_speech_future_free(f);
    mel_speech_shutdown();
}

MEL_TEST(speech, authorize_future_denied)
{
    install_mock();
    mock_auth_granted = false;
    Mel_Future* f = mel_speech_authorize(mel_alloc_heap());
    MEL_REQUIRE(f != NULL);
    MEL_EXPECT(mel_future_resolved(f));
    MEL_EXPECT(!mel_speech_auth_is_granted(mel_speech_future_auth(f)));
    MEL_EXPECT(mel_future_status_failed(mel_future_status(f)));
    mel_speech_future_free(f);
    mel_speech_shutdown();
}

MEL_TEST(speech, speak_completes)
{
    install_mock();
    Mel_Speech_Voice        v = voice_named(S8("mock-full"));
    Done_Sink               done = { 0 };
    Mel_Speech_Speak_Result r = mel_speech_speak(v, S8("hello world"), .on_complete = on_speak_done, .user = &done);
    MEL_EXPECT_EQ(r.status, (Mel_Speech_Status)MEL_SPEECH_OK);
    MEL_EXPECT(mel_speech_speaking(r.value));
    MEL_EXPECT(mock_tts_active);

    mock_tts_sink.on_speak_done(mock_tts_sink.token, MEL_SPEECH_OK);
    MEL_EXPECT_EQ(done.calls, (u32)1);
    MEL_EXPECT_EQ(done.last, (Mel_Speech_Status)MEL_SPEECH_OK);
    MEL_EXPECT(!mel_speech_speaking(r.value));
    mel_speech_shutdown();
}

MEL_TEST(speech, speak_lowering_clamps_and_warns)
{
    install_mock();
    Mel_Speech_Voice        v = voice_named(S8("mock-full"));
    Mel_Speech_Speak_Result r = mel_speech_speak(v, S8("fast"), .rate = 5.0f);
    MEL_EXPECT(mel_speech_warned(r.status));
    MEL_EXPECT(r.status & MEL_SPEECH_WARN_RATE_CLAMPED);
    MEL_EXPECT_EQ(mock_tts_rate, 2.0f);
    mel_speech_speak_abort(r.value);

    Mel_Speech_Voice        lim = voice_named(S8("mock-limited"));
    Mel_Speech_Speak_Result r2 = mel_speech_speak(lim, S8("flat"), .rate = 1.5f, .pitch = 1.2f, .volume = 0.5f);
    MEL_EXPECT(mel_speech_warned(r2.status));
    MEL_EXPECT(r2.status & MEL_SPEECH_WARN_RATE_CLAMPED);
    MEL_EXPECT(r2.status & MEL_SPEECH_WARN_PITCH_DROPPED);
    MEL_EXPECT(r2.status & MEL_SPEECH_WARN_VOLUME_DROPPED);
    MEL_EXPECT_EQ(mock_tts_rate, 0.0f);
    MEL_EXPECT_EQ(mock_tts_pitch, 0.0f);
    MEL_EXPECT_EQ(mock_tts_volume, 0.0f);
    mel_speech_shutdown();
}

MEL_TEST(speech, speak_empty_text_fails_loudly)
{
    install_mock();
    Mel_Speech_Voice        v = voice_named(S8("mock-full"));
    Mel_Speech_Speak_Result r = mel_speech_speak(v, STR8_EMPTY);
    MEL_EXPECT(mel_speech_failed(r.status));
    MEL_EXPECT(!mock_tts_active);
    mel_speech_shutdown();
}

MEL_TEST(speech, speak_dead_voice_fails_loudly)
{
    install_mock();
    Mel_Speech_Speak_Result r = mel_speech_speak(MEL_SPEECH_VOICE_NULL, S8("ghost"));
    MEL_EXPECT(mel_speech_failed(r.status));
    MEL_EXPECT(r.status & MEL_SPEECH_RESULT_NO_DEVICE);
    mel_speech_shutdown();
}

typedef struct
{
    u32              calls;
    Mel_Speech_Range last;
} Range_Sink;

static void on_range(Mel_Speech_Utterance u, Mel_Speech_Range range, void* user)
{
    (void)u;
    Range_Sink* rs = (Range_Sink*)user;
    rs->calls++;
    rs->last = range;
}

MEL_TEST(speech, ranges_delivered_when_supported)
{
    install_mock();
    Mel_Speech_Voice        v = voice_named(S8("mock-full"));
    Range_Sink              rs = { 0 };
    Mel_Speech_Speak_Result r = mel_speech_speak(v, S8("hello world"), .on_range = on_range, .user = &rs);
    MEL_EXPECT_EQ(r.status, (Mel_Speech_Status)MEL_SPEECH_OK);
    MEL_EXPECT(mock_tts_want_ranges);

    mock_tts_sink.on_range(mock_tts_sink.token, (Mel_Speech_Range){ .offset = 6, .length = 5 });
    MEL_EXPECT_EQ(rs.calls, (u32)1);
    MEL_EXPECT_EQ(rs.last.offset, (usize)6);
    MEL_EXPECT_EQ(rs.last.length, (usize)5);
    mel_speech_speak_abort(r.value);
    mel_speech_shutdown();
}

MEL_TEST(speech, ranges_dropped_when_unsupported)
{
    install_mock();
    Mel_Speech_Voice        v = voice_named(S8("mock-limited"));
    Range_Sink              rs = { 0 };
    Mel_Speech_Speak_Result r = mel_speech_speak(v, S8("hello"), .on_range = on_range, .user = &rs);
    MEL_EXPECT(mel_speech_warned(r.status));
    MEL_EXPECT(r.status & MEL_SPEECH_WARN_RANGES_DROPPED);
    MEL_EXPECT(!mock_tts_want_ranges);
    mel_speech_speak_abort(r.value);
    mel_speech_shutdown();
}

MEL_TEST(speech, pause_resume_roundtrip)
{
    install_mock();
    Mel_Speech_Voice        v = voice_named(S8("mock-full"));
    Mel_Speech_Speak_Result r = mel_speech_speak(v, S8("long text"));
    MEL_EXPECT_EQ(mel_speech_speak_pause(r.value) & MEL_SPEECH_SEVERITY_MASK, (Mel_Speech_Status)MEL_SPEECH_OK);
    MEL_EXPECT(mel_speech_speak_paused(r.value));
    MEL_EXPECT(!mel_speech_speaking(r.value));
    MEL_EXPECT_EQ(mock_tts_pause_calls, (u32)1);

    MEL_EXPECT_EQ(mel_speech_speak_resume(r.value) & MEL_SPEECH_SEVERITY_MASK, (Mel_Speech_Status)MEL_SPEECH_OK);
    MEL_EXPECT(mel_speech_speaking(r.value));
    MEL_EXPECT_EQ(mock_tts_resume_calls, (u32)1);
    mel_speech_speak_abort(r.value);
    mel_speech_shutdown();
}

MEL_TEST(speech, pause_unsupported_fails_loudly)
{
    install_mock();
    Mel_Speech_Voice        v = voice_named(S8("mock-limited"));
    Mel_Speech_Speak_Result r = mel_speech_speak(v, S8("flat"));
    Mel_Speech_Status       s = mel_speech_speak_pause(r.value);
    MEL_EXPECT(mel_speech_failed(s));
    MEL_EXPECT(s & MEL_SPEECH_RESULT_UNSUPPORTED);
    MEL_EXPECT_EQ(mock_tts_pause_calls, (u32)0);
    mel_speech_speak_abort(r.value);
    mel_speech_shutdown();
}

MEL_TEST(speech, abort_resolves_and_late_completion_is_ignored)
{
    install_mock();
    Mel_Speech_Voice        v = voice_named(S8("mock-full"));
    Done_Sink               done = { 0 };
    Mel_Speech_Speak_Result r = mel_speech_speak(v, S8("doomed"), .on_complete = on_speak_done, .user = &done);
    Mel_Speech_Sink         late = mock_tts_sink;

    mel_speech_speak_abort(r.value);
    MEL_EXPECT_EQ(done.calls, (u32)1);
    MEL_EXPECT(done.last & MEL_SPEECH_RESULT_ABORTED);
    MEL_EXPECT_EQ(mock_tts_abort_calls, (u32)1);
    MEL_EXPECT(!mel_speech_speaking(r.value));

    late.on_speak_done(late.token, MEL_SPEECH_OK);
    MEL_EXPECT_EQ(done.calls, (u32)1);
    mel_speech_shutdown();
}

typedef struct
{
    u32               calls;
    u32               finals;
    char              last_text[64];
    f32               last_confidence;
    u32               done_calls;
    Mel_Speech_Status done_last;
} Listen_Ctx;

static void on_result(Mel_Speech_Session s, const Mel_Speech_Result* result, void* user)
{
    (void)s;
    Listen_Ctx* ctx = (Listen_Ctx*)user;
    ctx->calls++;
    if (result->final)
        ctx->finals++;
    usize n = (usize)result->text.len < sizeof ctx->last_text - 1 ? (usize)result->text.len : sizeof ctx->last_text - 1;
    memcpy(ctx->last_text, result->text.data, n);
    ctx->last_text[n] = 0;
    ctx->last_confidence = result->confidence;
}

static void ctx_on_listen_done(Mel_Speech_Session s, Mel_Speech_Status status, void* user)
{
    (void)s;
    Listen_Ctx* ctx = (Listen_Ctx*)user;
    ctx->done_calls++;
    ctx->done_last = status;
}

MEL_TEST(speech, listen_delivers_partial_and_final)
{
    install_mock();
    Mel_Speech_Recognizer    rec = recognizer_lang(S8("en-US"));
    Listen_Ctx               ctx = { 0 };
    Mel_Speech_Listen_Result r = mel_speech_listen(rec, .partials = true, .on_result = on_result, .on_complete = ctx_on_listen_done, .user = &ctx);
    MEL_EXPECT_EQ(r.status, (Mel_Speech_Status)MEL_SPEECH_OK);
    MEL_EXPECT(mel_speech_listening(r.value));
    MEL_EXPECT(mock_stt_partials);

    Mel_Speech_Result partial = { .text = S8("hel"), .final = false, .confidence = 0.0f };
    mock_stt_sink.on_result(mock_stt_sink.token, &partial);
    MEL_EXPECT_EQ(ctx.calls, (u32)1);
    MEL_EXPECT_EQ(ctx.finals, (u32)0);
    MEL_EXPECT(strcmp(ctx.last_text, "hel") == 0);

    Mel_Speech_Result final = { .text = S8("hello"), .final = true, .confidence = 0.9f };
    mock_stt_sink.on_result(mock_stt_sink.token, &final);
    MEL_EXPECT_EQ(ctx.finals, (u32)1);
    MEL_EXPECT(strcmp(ctx.last_text, "hello") == 0);

    mock_stt_sink.on_listen_done(mock_stt_sink.token, MEL_SPEECH_OK);
    MEL_EXPECT_EQ(ctx.done_calls, (u32)1);
    MEL_EXPECT_EQ(ctx.done_last, (Mel_Speech_Status)MEL_SPEECH_OK);
    MEL_EXPECT(!mel_speech_listening(r.value));
    mel_speech_shutdown();
}

MEL_TEST(speech, listen_busy_recognizer_fails_loudly)
{
    install_mock();
    Mel_Speech_Recognizer    rec = recognizer_lang(S8("en-US"));
    Listen_Ctx               ctx = { 0 };
    Mel_Speech_Listen_Result a = mel_speech_listen(rec, .on_result = on_result, .user = &ctx);
    MEL_EXPECT_EQ(a.status, (Mel_Speech_Status)MEL_SPEECH_OK);

    Mel_Speech_Listen_Result b = mel_speech_listen(rec, .on_result = on_result, .user = &ctx);
    MEL_EXPECT(mel_speech_failed(b.status));
    MEL_EXPECT(b.status & MEL_SPEECH_RESULT_BUSY);

    mel_speech_listen_abort(a.value);
    Mel_Speech_Listen_Result c = mel_speech_listen(rec, .on_result = on_result, .user = &ctx);
    MEL_EXPECT_EQ(c.status, (Mel_Speech_Status)MEL_SPEECH_OK);
    mel_speech_listen_abort(c.value);
    mel_speech_shutdown();
}

MEL_TEST(speech, listen_without_on_result_fails_loudly)
{
    install_mock();
    Mel_Speech_Recognizer    rec = recognizer_lang(S8("en-US"));
    Mel_Speech_Listen_Result r = mel_speech_listen(rec);
    MEL_EXPECT(mel_speech_failed(r.status));
    MEL_EXPECT(!mock_stt_active);
    mel_speech_shutdown();
}

MEL_TEST(speech, listen_partials_dropped_when_unsupported)
{
    install_mock();
    Mel_Speech_Recognizer    rec = recognizer_lang(S8("de-DE"));
    Listen_Ctx               ctx = { 0 };
    Mel_Speech_Listen_Result r = mel_speech_listen(rec, .partials = true, .on_result = on_result, .user = &ctx);
    MEL_EXPECT(mel_speech_warned(r.status));
    MEL_EXPECT(r.status & MEL_SPEECH_WARN_PARTIALS_DROPPED);
    MEL_EXPECT(!mock_stt_partials);
    mel_speech_listen_abort(r.value);
    mel_speech_shutdown();
}

MEL_TEST(speech, listen_stop_graceful_then_provider_completes)
{
    install_mock();
    Mel_Speech_Recognizer    rec = recognizer_lang(S8("en-US"));
    Listen_Ctx               ctx = { 0 };
    Mel_Speech_Listen_Result r = mel_speech_listen(rec, .on_result = on_result, .on_complete = ctx_on_listen_done, .user = &ctx);
    MEL_EXPECT_EQ(mel_speech_listen_stop(r.value) & MEL_SPEECH_SEVERITY_MASK, (Mel_Speech_Status)MEL_SPEECH_OK);
    MEL_EXPECT_EQ(mock_stt_stop_calls, (u32)1);
    MEL_EXPECT(mel_speech_listening(r.value));

    mock_stt_sink.on_listen_done(mock_stt_sink.token, MEL_SPEECH_OK);
    MEL_EXPECT_EQ(ctx.done_calls, (u32)1);
    MEL_EXPECT(!mel_speech_listening(r.value));
    mel_speech_shutdown();
}

MEL_TEST(speech, listen_stop_synthesized_when_provider_cannot_stop)
{
    install_mock();
    Mel_Speech_Recognizer    rec = recognizer_lang(S8("de-DE"));
    Listen_Ctx               ctx = { 0 };
    Mel_Speech_Listen_Result r = mel_speech_listen(rec, .on_result = on_result, .on_complete = ctx_on_listen_done, .user = &ctx);
    Mel_Speech_Status        s = mel_speech_listen_stop(r.value);
    MEL_EXPECT(mel_speech_warned(s));
    MEL_EXPECT(s & MEL_SPEECH_WARN_STOP_SYNTHESIZED);
    MEL_EXPECT_EQ(ctx.done_calls, (u32)1);
    MEL_EXPECT(ctx.done_last & MEL_SPEECH_RESULT_ABORTED);
    MEL_EXPECT(!mel_speech_listening(r.value));
    mel_speech_shutdown();
}

MEL_TEST(speech, listen_abort_resolves_aborted)
{
    install_mock();
    Mel_Speech_Recognizer    rec = recognizer_lang(S8("en-US"));
    Listen_Ctx               ctx = { 0 };
    Mel_Speech_Listen_Result r = mel_speech_listen(rec, .on_result = on_result, .on_complete = ctx_on_listen_done, .user = &ctx);
    mel_speech_listen_abort(r.value);
    MEL_EXPECT_EQ(ctx.done_calls, (u32)1);
    MEL_EXPECT(ctx.done_last & MEL_SPEECH_RESULT_ABORTED);
    MEL_EXPECT_EQ(mock_stt_abort_calls, (u32)1);
    MEL_EXPECT(!mel_speech_listening(r.value));
    mel_speech_shutdown();
}

MEL_TEST(speech, refresh_lost_voice_resolves_active_utterance)
{
    install_mock();
    Mel_Speech_Voice        v = voice_named(S8("mock-full"));
    Done_Sink               done = { 0 };
    Mel_Speech_Speak_Result r = mel_speech_speak(v, S8("vanishing"), .on_complete = on_speak_done, .user = &done);
    MEL_EXPECT(mel_speech_speaking(r.value));

    mock_voice_full_present = false;
    mel_speech_refresh();
    MEL_EXPECT_EQ(mel_speech_voice_count(), (u32)1);
    MEL_EXPECT(!mel_speech_voice_alive(v));
    MEL_EXPECT_EQ(done.calls, (u32)1);
    MEL_EXPECT(mel_speech_failed(done.last));
    MEL_EXPECT(done.last & MEL_SPEECH_RESULT_LOST);
    mel_speech_shutdown();
}

MEL_TEST(speech, shutdown_aborts_active_work)
{
    install_mock();
    Mel_Speech_Voice         v = voice_named(S8("mock-full"));
    Mel_Speech_Recognizer    rec = recognizer_lang(S8("en-US"));
    Done_Sink                sdone = { 0 };
    Listen_Ctx               ctx = { 0 };
    Mel_Speech_Speak_Result  sr = mel_speech_speak(v, S8("cut short"), .on_complete = on_speak_done, .user = &sdone);
    Mel_Speech_Listen_Result lr = mel_speech_listen(rec, .on_result = on_result, .on_complete = ctx_on_listen_done, .user = &ctx);
    (void)sr;
    (void)lr;

    mel_speech_shutdown();
    MEL_EXPECT_EQ(sdone.calls, (u32)1);
    MEL_EXPECT(sdone.last & MEL_SPEECH_RESULT_ABORTED);
    MEL_EXPECT_EQ(ctx.done_calls, (u32)1);
    MEL_EXPECT(ctx.done_last & MEL_SPEECH_RESULT_ABORTED);
    MEL_EXPECT_EQ(mel_speech_voice_count(), (u32)0);
}
