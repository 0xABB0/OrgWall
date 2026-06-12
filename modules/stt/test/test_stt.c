#include <test/test.h>

#include <stt/stt.h>
#include <stt/provider.h>

#include <audioin/audioin.h>
#include <audioin/permission.h>
#include <audioin/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <future/future.h>
#include <core/types.h>
#include <string/str8.h>

#include <string.h>

void mel_stt__register_host_providers(void) {}
void mel_audioin__register_host_providers(void) {}

typedef struct
{
    const mel_audioin_auth* auth;
    u32                     rates[1];
} Mock_Mic;

static Mock_Mic mock_mic;

static void mic_enumerate(void* user, Mel_AudioIn_Enum_Fn fn, void* fn_user)
{
    MEL_UNUSED(user);
    Mel_AudioIn_Raw raw = {
        .stable_id = S8("mock:mic"),
        .name = S8("Mock Mic"),
        .kind = &mel_audioin_builtin,
        .channels = 1,
        .samplerate = 16000,
        .samplerates = mock_mic.rates,
        .samplerate_count = 1,
        .caps = { .gain = false },
    };
    fn(&raw, fn_user);
}

static str8 mic_default_id(void* user)
{
    MEL_UNUSED(user);
    return S8("mock:mic");
}

static const mel_audioin_auth* mic_authorization(void* user)
{
    MEL_UNUSED(user);
    return mock_mic.auth;
}

static const Mel_AudioIn_Provider_Desc MOCK_MIC_DESC = {
    .name = "mock-mic",
    .enumerate = mic_enumerate,
    .default_id = mic_default_id,
    .authorization = mic_authorization,
};

typedef struct
{
    const mel_stt_auth*    auth;
    u32                    count;
    Mel_Stt_Recognizer_Raw recs[2];

    Mel_Stt_Sink   sink;
    Mel_Stt_Status listen_status;
    u32            listen_calls;
    u64            listen_stable_id;
    u64            listen_token;

    Mel_Stt_Listen_Lowered last;
    char                   device_id[64];
    u32                    device_id_len;
    bool                   vocab_present_at_call;

    u32 stop_calls;
    u32 abort_calls;
    u32 feed_calls;
    u32 fed_frames;

    Mel_Stt_Sink auth_sink;
    u32          auth_prompts;
} Mock_Stt;

static Mock_Stt mock_stt;

static u32 stt_enumerate(void* user, const Mel_Alloc* alloc, Mel_Stt_Recognizer_Raw* out, u32 cap)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    u32 n = mock_stt.count < cap ? mock_stt.count : cap;
    for (u32 i = 0; i < n; i++)
        out[i] = mock_stt.recs[i];
    return mock_stt.count;
}

static const mel_stt_auth* stt_authorization(void* user)
{
    MEL_UNUSED(user);
    return mock_stt.auth;
}

static void stt_authorize(void* user, Mel_Stt_Sink sink)
{
    MEL_UNUSED(user);
    mock_stt.auth_sink = sink;
    mock_stt.auth_prompts++;
}

static Mel_Stt_Status stt_listen(void* user, u64 stable_id, u64 token, const Mel_Stt_Listen_Lowered* lowered, Mel_Stt_Sink sink)
{
    MEL_UNUSED(user);
    mock_stt.listen_calls++;
    mock_stt.listen_stable_id = stable_id;
    mock_stt.listen_token = token;
    mock_stt.last = *lowered;
    mock_stt.device_id_len = lowered->device_stable_id.len < sizeof mock_stt.device_id ? (u32)lowered->device_stable_id.len : (u32)sizeof mock_stt.device_id;
    if (mock_stt.device_id_len > 0)
        memcpy(mock_stt.device_id, lowered->device_stable_id.data, mock_stt.device_id_len);
    mock_stt.vocab_present_at_call = lowered->vocabulary != NULL;
    if (!mel_stt_failed(mock_stt.listen_status))
        mock_stt.sink = sink;
    return mock_stt.listen_status;
}

static void stt_stop(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    mock_stt.stop_calls++;
}

static void stt_abort(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    mock_stt.abort_calls++;
}

static Mel_Stt_Status stt_feed(void* user, u64 stable_id, u64 token, const f32* frames, u32 frame_count)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    MEL_UNUSED(frames);
    mock_stt.feed_calls++;
    mock_stt.fed_frames += frame_count;
    return MEL_STT_OK;
}

static const Mel_Stt_Provider_Desc MOCK_STT_DESC = {
    .name = "mock-stt",
    .enumerate_recognizers = stt_enumerate,
    .authorization = stt_authorization,
    .authorize = stt_authorize,
    .listen = stt_listen,
    .stop = stt_stop,
    .abort = stt_abort,
    .feed = stt_feed,
};

typedef struct
{
    u32            results;
    u32            finals;
    char           last_text[64];
    u32            last_text_len;
    u32            completes;
    Mel_Stt_Status last_complete;
} Cb_State;

static Cb_State cb;

static void on_result(Mel_Stt_Session s, const Mel_Stt_Result* result, void* user)
{
    MEL_UNUSED(s);
    MEL_UNUSED(user);
    cb.results++;
    if (result->final)
        cb.finals++;
    cb.last_text_len = result->text.len < sizeof cb.last_text ? (u32)result->text.len : (u32)sizeof cb.last_text;
    if (cb.last_text_len > 0)
        memcpy(cb.last_text, result->text.data, cb.last_text_len);
}

static void on_complete(Mel_Stt_Session s, Mel_Stt_Status status, void* user)
{
    MEL_UNUSED(s);
    MEL_UNUSED(user);
    cb.completes++;
    cb.last_complete = status;
}

static Mel_Stt_Recognizer_Caps full_caps(void)
{
    return (Mel_Stt_Recognizer_Caps){
        .on_device = true,
        .require_on_device = true,
        .partials = true,
        .can_stop = true,
        .feed = true,
        .device_select = true,
        .vocabulary = true,
        .punctuation = true,
        .profanity_filter = true,
    };
}

static void fixture_reset(void)
{
    memset(&mock_mic, 0, sizeof mock_mic);
    memset(&mock_stt, 0, sizeof mock_stt);
    memset(&cb, 0, sizeof cb);
    mock_mic.auth = &mel_audioin_auth_granted;
    mock_mic.rates[0] = 16000;
    mock_stt.auth = &mel_stt_auth_granted;
    mock_stt.listen_status = MEL_STT_OK;
    mock_stt.count = 1;
    mock_stt.recs[0] = (Mel_Stt_Recognizer_Raw){ .stable_id = 1, .language = S8("en-US"), .caps = full_caps() };
    mock_stt.recs[1] = (Mel_Stt_Recognizer_Raw){ .stable_id = 2, .language = S8("it-IT"), .caps = full_caps() };
}

static Mel_Stt_Recognizer fixture_up(void)
{
    mel_audioin_init(mel_alloc_heap(), NULL);
    mel_audioin_provider_register(&MOCK_MIC_DESC);
    mel_audioin_refresh();
    mel_stt_init(mel_alloc_heap());
    mel_stt_provider_register(&MOCK_STT_DESC);
    mel_stt_refresh();
    Mel_Stt_Recognizer r = MEL_STT_RECOGNIZER_NULL;
    mel_stt_recognizer_list(&r, 1);
    return r;
}

static void fixture_down(void)
{
    mel_stt_shutdown();
    mel_audioin_shutdown();
}

MEL_TEST(stt, enumeration_and_describe)
{
    fixture_reset();
    mock_stt.count = 2;
    fixture_up();

    MEL_EXPECT_EQ(mel_stt_recognizer_count(), 2u);
    Mel_Stt_Recognizer recs[2];
    MEL_REQUIRE_EQ(mel_stt_recognizer_list(recs, 2), 2u);
    MEL_EXPECT(mel_stt_recognizer_alive(recs[0]));
    MEL_EXPECT(mel_stt_recognizer_alive(recs[1]));
    MEL_EXPECT(!mel_stt_recognizer_equal(recs[0], recs[1]));

    Mel_Stt_Recognizer_Describe_Result d0 = mel_stt_recognizer_describe(recs[0]);
    MEL_REQUIRE(!mel_stt_failed(d0.status));
    MEL_EXPECT_EQ_STR8(d0.value.language, S8("en-US"));
    MEL_EXPECT(d0.value.caps.partials);
    MEL_EXPECT(d0.value.caps.feed);

    Mel_Stt_Recognizer_Describe_Result d1 = mel_stt_recognizer_describe(recs[1]);
    MEL_REQUIRE(!mel_stt_failed(d1.status));
    MEL_EXPECT_EQ_STR8(d1.value.language, S8("it-IT"));

    Mel_Stt_Recognizer_Describe_Result dead = mel_stt_recognizer_describe(MEL_STT_RECOGNIZER_NULL);
    MEL_EXPECT(mel_stt_failed(dead.status));
    MEL_EXPECT(dead.status & MEL_STT_RESULT_NO_DEVICE);
    MEL_EXPECT(!mel_stt_recognizer_alive(MEL_STT_RECOGNIZER_NULL));

    fixture_down();
}

MEL_TEST(stt, composed_auth_most_restrictive)
{
    fixture_reset();
    fixture_up();

    MEL_EXPECT_EQ(mel_stt_authorization(), &mel_stt_auth_granted);
    MEL_EXPECT(mel_stt_auth_is_granted(mel_stt_authorization()));

    mock_mic.auth = &mel_audioin_auth_denied;
    MEL_EXPECT_EQ(mel_stt_authorization(), &mel_stt_auth_denied);

    mock_mic.auth = &mel_audioin_auth_granted;
    mock_stt.auth = &mel_stt_auth_denied;
    MEL_EXPECT_EQ(mel_stt_authorization(), &mel_stt_auth_denied);

    mock_stt.auth = &mel_stt_auth_not_determined;
    MEL_EXPECT_EQ(mel_stt_authorization(), &mel_stt_auth_not_determined);

    mock_stt.auth = &mel_stt_auth_restricted;
    mock_mic.auth = &mel_audioin_auth_not_determined;
    MEL_EXPECT_EQ(mel_stt_authorization(), &mel_stt_auth_restricted);

    mock_stt.auth = &mel_stt_auth_restricted;
    mock_mic.auth = &mel_audioin_auth_denied;
    MEL_EXPECT_EQ(mel_stt_authorization(), &mel_stt_auth_denied);

    fixture_down();
}

MEL_TEST(stt, authorize_future_resolves_composed)
{
    fixture_reset();
    mock_stt.auth = &mel_stt_auth_not_determined;
    fixture_up();

    Mel_Future* f = mel_stt_authorize(mel_alloc_heap());
    MEL_REQUIRE_NOT_NULL(f);
    MEL_EXPECT_EQ(mock_stt.auth_prompts, 1u);
    MEL_EXPECT(!mel_future_resolved(f));

    mock_stt.auth = &mel_stt_auth_granted;
    mock_stt.auth_sink.on_auth(mock_stt.auth_sink.token, mock_stt.auth);
    MEL_REQUIRE(mel_future_resolved(f));
    MEL_EXPECT_EQ(mel_stt_future_auth(f), &mel_stt_auth_granted);
    MEL_EXPECT(mel_stt_auth_is_granted(mel_stt_future_auth(f)));
    mel_stt_future_free(f);

    mock_mic.auth = &mel_audioin_auth_denied;
    mock_stt.auth = &mel_stt_auth_not_determined;
    f = mel_stt_authorize(mel_alloc_heap());
    MEL_REQUIRE_NOT_NULL(f);
    mock_stt.auth = &mel_stt_auth_granted;
    mock_stt.auth_sink.on_auth(mock_stt.auth_sink.token, mock_stt.auth);
    MEL_REQUIRE(mel_future_resolved(f));
    MEL_EXPECT_EQ(mel_stt_future_auth(f), &mel_stt_auth_denied);
    MEL_EXPECT(mel_future_status_failed(mel_future_status(f)));
    mel_stt_future_free(f);

    fixture_down();
}

MEL_TEST(stt, listen_requires_on_result)
{
    fixture_reset();
    Mel_Stt_Recognizer r = fixture_up();

    Mel_Stt_Listen_Result res = mel_stt_listen(r, .on_complete = on_complete);
    MEL_EXPECT(mel_stt_failed(res.status));
    MEL_EXPECT_EQ(mock_stt.listen_calls, 0u);

    fixture_down();
}

MEL_TEST(stt, device_door_gated_by_caps)
{
    fixture_reset();
    mock_stt.recs[0].caps.device_select = false;
    Mel_Stt_Recognizer r = fixture_up();
    Mel_AudioIn        dev = mel_audioin_find(S8("mock:mic"));
    MEL_REQUIRE(mel_audioin_alive(dev));

    Mel_Stt_Listen_Result res = mel_stt_listen(r, .device = dev, .on_result = on_result, .on_complete = on_complete);
    MEL_EXPECT(mel_stt_failed(res.status));
    MEL_EXPECT(res.status & MEL_STT_RESULT_UNSUPPORTED);
    MEL_EXPECT_EQ(mock_stt.listen_calls, 0u);

    fixture_down();
}

MEL_TEST(stt, device_door_lowers_stable_id)
{
    fixture_reset();
    Mel_Stt_Recognizer r = fixture_up();
    Mel_AudioIn        dev = mel_audioin_find(S8("mock:mic"));
    MEL_REQUIRE(mel_audioin_alive(dev));

    Mel_Stt_Listen_Result res = mel_stt_listen(r, .device = dev, .on_result = on_result, .on_complete = on_complete);
    MEL_REQUIRE(!mel_stt_failed(res.status));
    MEL_EXPECT_EQ(mock_stt.listen_calls, 1u);
    MEL_EXPECT_EQ(mock_stt.listen_stable_id, 1ull);
    MEL_EXPECT(!mock_stt.last.feed);
    MEL_EXPECT_EQ(mock_stt.device_id_len, (u32)lengthof("mock:mic"));
    MEL_EXPECT(memcmp(mock_stt.device_id, "mock:mic", mock_stt.device_id_len) == 0);

    mel_stt_abort(res.value);
    fixture_down();
}

MEL_TEST(stt, feed_door_format_violations_loud)
{
    fixture_reset();
    Mel_Stt_Recognizer r = fixture_up();
    Mel_AudioIn        dev = mel_audioin_find(S8("mock:mic"));

    Mel_Stt_Listen_Result res = mel_stt_listen(r, .feed = true, .on_result = on_result);
    MEL_EXPECT(mel_stt_failed(res.status));
    MEL_EXPECT(res.status & MEL_STT_RESULT_UNSUPPORTED);

    res = mel_stt_listen(r, .feed_sample_rate = 16000, .on_result = on_result);
    MEL_EXPECT(mel_stt_failed(res.status));
    MEL_EXPECT(res.status & MEL_STT_RESULT_UNSUPPORTED);

    res = mel_stt_listen(r, .feed = true, .feed_sample_rate = 16000, .device = dev, .on_result = on_result);
    MEL_EXPECT(mel_stt_failed(res.status));
    MEL_EXPECT(res.status & MEL_STT_RESULT_UNSUPPORTED);

    MEL_EXPECT_EQ(mock_stt.listen_calls, 0u);

    mock_stt.recs[0].caps.feed = false;
    mel_stt_refresh();
    res = mel_stt_listen(r, .feed = true, .feed_sample_rate = 16000, .on_result = on_result);
    MEL_EXPECT(mel_stt_failed(res.status));
    MEL_EXPECT(res.status & MEL_STT_RESULT_UNSUPPORTED);
    MEL_EXPECT_EQ(mock_stt.listen_calls, 0u);

    fixture_down();
}

MEL_TEST(stt, fed_door_skips_consent)
{
    fixture_reset();
    mock_mic.auth = &mel_audioin_auth_denied;
    mock_stt.auth = &mel_stt_auth_denied;
    Mel_Stt_Recognizer r = fixture_up();

    Mel_Stt_Listen_Result res = mel_stt_listen(r, .feed = true, .feed_sample_rate = 16000, .on_result = on_result, .on_complete = on_complete);
    MEL_REQUIRE(!mel_stt_failed(res.status));
    MEL_EXPECT(mock_stt.last.feed);
    MEL_EXPECT_EQ(mock_stt.last.feed_sample_rate, 16000u);

    f32 buf[64] = { 0 };
    MEL_EXPECT(!mel_stt_failed(mel_stt_feed(res.value, buf, 64)));
    MEL_EXPECT_EQ(mock_stt.feed_calls, 1u);
    MEL_EXPECT_EQ(mock_stt.fed_frames, 64u);

    mel_stt_abort(res.value);
    fixture_down();
}

MEL_TEST(stt, mic_doors_denied_unconsented)
{
    fixture_reset();
    mock_mic.auth = &mel_audioin_auth_denied;
    Mel_Stt_Recognizer r = fixture_up();
    Mel_AudioIn        dev = mel_audioin_find(S8("mock:mic"));

    Mel_Stt_Listen_Result res = mel_stt_listen(r, .on_result = on_result, .on_complete = on_complete);
    MEL_EXPECT(mel_stt_failed(res.status));
    MEL_EXPECT(res.status & MEL_STT_RESULT_DENIED);

    res = mel_stt_listen(r, .device = dev, .on_result = on_result, .on_complete = on_complete);
    MEL_EXPECT(mel_stt_failed(res.status));
    MEL_EXPECT(res.status & MEL_STT_RESULT_DENIED);

    MEL_EXPECT_EQ(mock_stt.listen_calls, 0u);

    mock_mic.auth = &mel_audioin_auth_granted;
    mock_stt.auth = &mel_stt_auth_not_determined;
    res = mel_stt_listen(r, .on_result = on_result, .on_complete = on_complete);
    MEL_EXPECT(mel_stt_failed(res.status));
    MEL_EXPECT(res.status & MEL_STT_RESULT_DENIED);

    fixture_down();
}

MEL_TEST(stt, tuning_lowered_with_warnings)
{
    fixture_reset();
    mock_stt.recs[0].caps.partials = false;
    mock_stt.recs[0].caps.vocabulary = false;
    mock_stt.recs[0].caps.punctuation = false;
    mock_stt.recs[0].caps.profanity_filter = false;
    Mel_Stt_Recognizer r = fixture_up();

    str8                  vocab[] = { S8("Melody"), S8("audioin") };
    Mel_Stt_Listen_Result res = mel_stt_listen(r, .partials = true, .vocabulary = vocab, .vocabulary_count = 2, .punctuation = true, .profanity_filter = true, .on_result = on_result, .on_complete = on_complete);
    MEL_REQUIRE(!mel_stt_failed(res.status));
    MEL_EXPECT(mel_stt_warned(res.status));
    MEL_EXPECT(res.status & MEL_STT_WARN_PARTIALS_DROPPED);
    MEL_EXPECT(res.status & MEL_STT_WARN_VOCABULARY_DROPPED);
    MEL_EXPECT(res.status & MEL_STT_WARN_PUNCTUATION_DROPPED);
    MEL_EXPECT(res.status & MEL_STT_WARN_PROFANITY_DROPPED);

    MEL_EXPECT(!mock_stt.last.partials);
    MEL_EXPECT_EQ(mock_stt.last.vocabulary_count, 0u);
    MEL_EXPECT(!mock_stt.vocab_present_at_call);
    MEL_EXPECT(!mock_stt.last.punctuation);
    MEL_EXPECT(!mock_stt.last.profanity_filter);

    mel_stt_abort(res.value);
    fixture_down();
}

MEL_TEST(stt, tuning_passes_through_when_capped)
{
    fixture_reset();
    Mel_Stt_Recognizer r = fixture_up();

    str8                  vocab[] = { S8("Melody") };
    Mel_Stt_Listen_Result res = mel_stt_listen(r, .partials = true, .vocabulary = vocab, .vocabulary_count = 1, .punctuation = true, .profanity_filter = true, .require_on_device = true, .on_result = on_result, .on_complete = on_complete);
    MEL_REQUIRE(!mel_stt_failed(res.status));
    MEL_EXPECT(!mel_stt_warned(res.status));
    MEL_EXPECT(mock_stt.last.partials);
    MEL_EXPECT_EQ(mock_stt.last.vocabulary_count, 1u);
    MEL_EXPECT(mock_stt.vocab_present_at_call);
    MEL_EXPECT(mock_stt.last.punctuation);
    MEL_EXPECT(mock_stt.last.profanity_filter);
    MEL_EXPECT(mock_stt.last.require_on_device);

    mel_stt_abort(res.value);
    fixture_down();
}

MEL_TEST(stt, require_on_device_hard_fail_when_uncapped)
{
    fixture_reset();
    mock_stt.recs[0].caps.require_on_device = false;
    Mel_Stt_Recognizer r = fixture_up();

    Mel_Stt_Listen_Result res = mel_stt_listen(r, .require_on_device = true, .on_result = on_result, .on_complete = on_complete);
    MEL_EXPECT(mel_stt_failed(res.status));
    MEL_EXPECT(res.status & MEL_STT_RESULT_UNSUPPORTED);
    MEL_EXPECT_EQ(mock_stt.listen_calls, 0u);

    fixture_down();
}

MEL_TEST(stt, one_live_session_per_recognizer)
{
    fixture_reset();
    Mel_Stt_Recognizer r = fixture_up();

    Mel_Stt_Listen_Result first = mel_stt_listen(r, .on_result = on_result, .on_complete = on_complete);
    MEL_REQUIRE(!mel_stt_failed(first.status));

    Mel_Stt_Listen_Result second = mel_stt_listen(r, .on_result = on_result, .on_complete = on_complete);
    MEL_EXPECT(mel_stt_failed(second.status));
    MEL_EXPECT(second.status & MEL_STT_RESULT_BUSY);

    mel_stt_abort(first.value);
    Mel_Stt_Listen_Result third = mel_stt_listen(r, .on_result = on_result, .on_complete = on_complete);
    MEL_EXPECT(!mel_stt_failed(third.status));

    mel_stt_abort(third.value);
    fixture_down();
}

MEL_TEST(stt, stop_graceful_drains)
{
    fixture_reset();
    Mel_Stt_Recognizer r = fixture_up();

    Mel_Stt_Listen_Result res = mel_stt_listen(r, .partials = true, .on_result = on_result, .on_complete = on_complete);
    MEL_REQUIRE(!mel_stt_failed(res.status));

    Mel_Stt_Status st = mel_stt_stop(res.value);
    MEL_EXPECT(!mel_stt_failed(st));
    MEL_EXPECT(!mel_stt_warned(st));
    MEL_EXPECT_EQ(mock_stt.stop_calls, 1u);
    MEL_EXPECT(mel_stt_listening(res.value));

    Mel_Stt_Result partial = { .text = S8("hello"), .final = false, .confidence = 0.0f };
    mock_stt.sink.on_result(mock_stt.sink.token, &partial);
    Mel_Stt_Result final = { .text = S8("hello world"), .final = true, .confidence = 0.9f };
    mock_stt.sink.on_result(mock_stt.sink.token, &final);
    mock_stt.sink.on_done(mock_stt.sink.token, MEL_STT_OK);

    MEL_EXPECT_EQ(cb.results, 2u);
    MEL_EXPECT_EQ(cb.finals, 1u);
    MEL_EXPECT_EQ(cb.last_text_len, (u32)lengthof("hello world"));
    MEL_EXPECT(memcmp(cb.last_text, "hello world", cb.last_text_len) == 0);
    MEL_EXPECT_EQ(cb.completes, 1u);
    MEL_EXPECT(!mel_stt_failed(cb.last_complete));
    MEL_EXPECT(!mel_stt_listening(res.value));

    MEL_EXPECT(mel_stt_failed(mel_stt_stop(res.value)));

    fixture_down();
}

MEL_TEST(stt, stop_synthesized_when_no_drain)
{
    fixture_reset();
    mock_stt.recs[0].caps.can_stop = false;
    Mel_Stt_Recognizer r = fixture_up();

    Mel_Stt_Listen_Result res = mel_stt_listen(r, .on_result = on_result, .on_complete = on_complete);
    MEL_REQUIRE(!mel_stt_failed(res.status));

    Mel_Stt_Status st = mel_stt_stop(res.value);
    MEL_EXPECT(mel_stt_warned(st));
    MEL_EXPECT(st & MEL_STT_WARN_STOP_SYNTHESIZED);
    MEL_EXPECT_EQ(mock_stt.stop_calls, 0u);
    MEL_EXPECT_EQ(mock_stt.abort_calls, 1u);
    MEL_EXPECT_EQ(cb.completes, 1u);
    MEL_EXPECT(!mel_stt_failed(cb.last_complete));
    MEL_EXPECT(cb.last_complete & MEL_STT_RESULT_ABORTED);
    MEL_EXPECT(!mel_stt_listening(res.value));

    fixture_down();
}

MEL_TEST(stt, feed_after_terminal_rejected)
{
    fixture_reset();
    Mel_Stt_Recognizer r = fixture_up();

    Mel_Stt_Listen_Result unfed = mel_stt_listen(r, .on_result = on_result, .on_complete = on_complete);
    MEL_REQUIRE(!mel_stt_failed(unfed.status));
    f32 buf[8] = { 0 };
    MEL_EXPECT(mel_stt_failed(mel_stt_feed(unfed.value, buf, 8)));
    mel_stt_abort(unfed.value);

    Mel_Stt_Listen_Result fed = mel_stt_listen(r, .feed = true, .feed_sample_rate = 16000, .on_result = on_result, .on_complete = on_complete);
    MEL_REQUIRE(!mel_stt_failed(fed.status));
    MEL_EXPECT(!mel_stt_failed(mel_stt_feed(fed.value, buf, 8)));

    mel_stt_abort(fed.value);
    Mel_Stt_Status st = mel_stt_feed(fed.value, buf, 8);
    MEL_EXPECT(mel_stt_failed(st));
    MEL_EXPECT(st & MEL_STT_RESULT_LOST);
    MEL_EXPECT_EQ(mock_stt.feed_calls, 1u);

    fixture_down();
}

MEL_TEST(stt, abort_idempotent_swallows_late_terminals)
{
    fixture_reset();
    Mel_Stt_Recognizer r = fixture_up();

    Mel_Stt_Listen_Result res = mel_stt_listen(r, .on_result = on_result, .on_complete = on_complete);
    MEL_REQUIRE(!mel_stt_failed(res.status));
    Mel_Stt_Sink sink = mock_stt.sink;

    mel_stt_abort(res.value);
    MEL_EXPECT_EQ(cb.completes, 1u);
    MEL_EXPECT(cb.last_complete & MEL_STT_RESULT_ABORTED);
    MEL_EXPECT_EQ(mock_stt.abort_calls, 1u);

    mel_stt_abort(res.value);
    MEL_EXPECT_EQ(cb.completes, 1u);
    MEL_EXPECT_EQ(mock_stt.abort_calls, 1u);

    sink.on_done(sink.token, MEL_STT_ERROR | MEL_STT_RESULT_AUDIO);
    Mel_Stt_Result late = { .text = S8("late"), .final = true, .confidence = 0.0f };
    sink.on_result(sink.token, &late);
    MEL_EXPECT_EQ(cb.completes, 1u);
    MEL_EXPECT_EQ(cb.results, 0u);

    fixture_down();
}

MEL_TEST(stt, refresh_loss_resolves_sessions_lost)
{
    fixture_reset();
    Mel_Stt_Recognizer r = fixture_up();

    Mel_Stt_Listen_Result res = mel_stt_listen(r, .on_result = on_result, .on_complete = on_complete);
    MEL_REQUIRE(!mel_stt_failed(res.status));

    mock_stt.count = 0;
    MEL_EXPECT_EQ(mel_stt_refresh(), 0u);
    MEL_EXPECT(!mel_stt_recognizer_alive(r));
    MEL_EXPECT_EQ(cb.completes, 1u);
    MEL_EXPECT(mel_stt_failed(cb.last_complete));
    MEL_EXPECT(cb.last_complete & MEL_STT_RESULT_LOST);
    MEL_EXPECT(!mel_stt_listening(res.value));

    fixture_down();
}

MEL_TEST(stt, shutdown_aborts_live_sessions)
{
    fixture_reset();
    Mel_Stt_Recognizer r = fixture_up();

    Mel_Stt_Listen_Result res = mel_stt_listen(r, .on_result = on_result, .on_complete = on_complete);
    MEL_REQUIRE(!mel_stt_failed(res.status));

    mel_stt_shutdown();
    MEL_EXPECT_EQ(cb.completes, 1u);
    MEL_EXPECT(cb.last_complete & MEL_STT_RESULT_ABORTED);
    MEL_EXPECT_EQ(mock_stt.abort_calls, 1u);

    mel_audioin_shutdown();
}
