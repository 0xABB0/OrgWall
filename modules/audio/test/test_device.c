#include <test/test.h>

#include <audio/audio.h>
#include <audioout/audioout.h>
#include <audioout/events.h>
#include <audioout/provider.h>

#include "../../audiopolicy/src/audiopolicy_internal.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <core/types.h>
#include <event/event.h>
#include <executor/executor.h>
#include <string/str8.h>
#include <thread/thread.h>

#include <string.h>

#define SR    48000u
#define CH    2u
#define BLOCK 128u

void mel_audioout__register_host_providers(void) {}

static Mel_AudioPolicy_Status policy_apply(const Mel_AudioPolicy* requested, Mel_AudioPolicy* in_force)
{
    *in_force = *requested;
    return MEL_AUDIOPOLICY_OK;
}

static const Mel_AudioPolicy_Backend POLICY_BACKEND = { .apply = policy_apply };

const Mel_AudioPolicy_Backend* mel_audiopolicy__backend(void) { return &POLICY_BACKEND; }

typedef struct
{
    const char*         id;
    const char*         name;
    bool                present;
    u32                 opened;
    u32                 opens;
    u32                 closes;
    Mel_AudioOut_Source src;
} Mock_Device;

typedef struct
{
    Mock_Device a;
    Mock_Device b;
    u32         default_b;
} Mock_State;

static Mock_State mock;

static str8 cstr(const char* s) { return (str8){ (u8*)s, (size)strlen(s) }; }

static Mock_Device* mock_find(str8 stable_id)
{
    if (mock.a.present && str8_equals(stable_id, cstr(mock.a.id)))
        return &mock.a;
    if (mock.b.present && str8_equals(stable_id, cstr(mock.b.id)))
        return &mock.b;
    return NULL;
}

static void mock_enumerate(void* user, Mel_AudioOut_Enum_Fn fn, void* fn_user)
{
    MEL_UNUSED(user);
    u32          rates[1] = { SR };
    Mock_Device* devs[2] = { &mock.a, &mock.b };
    for (u32 i = 0; i < 2u; i++)
    {
        if (!devs[i]->present)
            continue;
        Mel_AudioOut_Raw raw = {
            .stable_id = cstr(devs[i]->id),
            .name = cstr(devs[i]->name),
            .kind = &mel_audioout_builtin,
            .channels = CH,
            .samplerate = SR,
            .samplerates = rates,
            .samplerate_count = 1,
        };
        if (!fn(&raw, fn_user))
            return;
    }
}

static str8 mock_default_id(void* user)
{
    MEL_UNUSED(user);
    if (mock.default_b && mock.b.present)
        return cstr(mock.b.id);
    if (mock.a.present)
        return cstr(mock.a.id);
    if (mock.b.present)
        return cstr(mock.b.id);
    return STR8_EMPTY;
}

static Mel_AudioOut_Status mock_open(void* user, str8 stable_id, Mel_AudioOut_Format req, Mel_AudioOut_Open_Opt opt, Mel_AudioOut_Granted* granted, Mel_AudioOut_Source src)
{
    MEL_UNUSED(user);
    MEL_UNUSED(opt);
    Mock_Device* d = mock_find(stable_id);
    if (d == NULL)
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_NO_DEVICE;
    d->src = src;
    d->opened++;
    d->opens++;
    granted->format.samplerate = req.samplerate;
    granted->format.channels = req.channels;
    granted->format.block_frames = BLOCK;
    granted->exclusive = false;
    granted->os_timestamps = false;
    granted->latency_frames = 100u;
    return MEL_AUDIOOUT_OK;
}

static void mock_start(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
}

static void mock_stop(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
}

static void mock_close(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(token);
    Mock_Device* d = mock_find(stable_id);
    if (d != NULL)
    {
        d->opened--;
        d->closes++;
    }
}

static const Mel_AudioOut_Provider_Desc MOCK_DESC = {
    .name = "mock",
    .enumerate = mock_enumerate,
    .default_id = mock_default_id,
    .open = mock_open,
    .start = mock_start,
    .stop = mock_stop,
    .close = mock_close,
};

static void install(void)
{
    memset(&mock, 0, sizeof mock);
    mock.a = (Mock_Device){ .id = "mock:a", .name = "Mock A", .present = true };
    mock.b = (Mock_Device){ .id = "mock:b", .name = "Mock B", .present = true };
    mel_audioout_init(mel_alloc_heap(), NULL);
    mel_audioout_provider_register(&MOCK_DESC);
    mel_audioout_refresh();
}

static Mel_Audio_Opt opt(void)
{
    return (Mel_Audio_Opt){
        .samplerate = SR,
        .channels = CH,
        .block_frames = BLOCK,
        .ring_blocks = 4u,
        .master_volume = 1.0f,
        .max_voice_channels = CH,
        .max_voice_ratio = 4.0,
    };
}

static Mel_Audio_Source* const_source(const Mel_Alloc* a, f32 value)
{
    f32* buf = mel_alloc(a, sizeof(f32) * SR);
    for (u32 i = 0; i < SR; i++)
        buf[i] = value;
    Mel_Audio_Source* s = mel_audio_pcm_from_float(a, buf, SR, 1u, SR, MEL_AUDIO_OWNERSHIP_OWNED);
    mel_dealloc(a, buf);
    mel_audio_pcm_set_loop(s, true, 0.0);
    return s;
}

static bool pull_until_audible(Mock_Device* d, f32* dst, u32 frames)
{
    for (u32 tries = 0; tries < 200u; tries++)
    {
        u32 got = d->src.pull(d->src.token, dst, frames);
        if (got == frames)
        {
            bool nonzero = false;
            for (u32 i = 0; i < frames * CH; i++)
                if (dst[i] != 0.0f)
                    nonzero = true;
            if (nonzero)
                return true;
        }
        mel_thread_sleep(2000000);
    }
    return false;
}

typedef struct
{
    Mel_Audio_Device_Event last;
    u32                    count;
} Event_Tally;

static void on_device_event(const void* payload, void* user)
{
    Event_Tally* t = user;
    t->last = *(const Mel_Audio_Device_Event*)payload;
    t->count++;
}

MEL_TEST(device, create_without_audioout_init_fails_loudly)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Audio*       eng = mel_audio_create(a, opt());
    MEL_EXPECT(eng == NULL);
}

MEL_TEST(device, create_pinned_plays_through_the_provider)
{
    install();
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Audio_Opt o = opt();
    o.device = mel_audioout_find(S8("mock:a"));
    Mel_Audio* eng = mel_audio_create(a, o);
    MEL_REQUIRE_NOT_NULL(eng);
    MEL_EXPECT(mock.a.opened);
    MEL_EXPECT(mel_audioout_equal(mel_audio_device(eng), mel_audioout_find(S8("mock:a"))));
    MEL_EXPECT_EQ(mel_audio_device_status(eng) & MEL_AUDIO_SEVERITY_MASK, (u32)MEL_AUDIO_OK);

    Mel_Audio_Caps caps = mel_audio_caps(eng);
    MEL_EXPECT_EQ(caps.latency_frames, 100u + 4u * BLOCK);

    Mel_Audio_Source* src = const_source(a, 0.5f);
    mel_audio_play(eng, src);

    f32 dst[BLOCK * CH];
    MEL_EXPECT(pull_until_audible(&mock.a, dst, BLOCK));

    mel_audio_destroy(eng);
    MEL_EXPECT(!mock.a.opened);
    src->source_free(src, a);
    mel_audioout_shutdown();
}

MEL_TEST(device, set_device_voices_survive)
{
    install();
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Audio_Opt o = opt();
    o.device = mel_audioout_find(S8("mock:a"));
    Mel_Audio* eng = mel_audio_create(a, o);
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Audio_Source* src = const_source(a, 0.5f);
    Mel_Audio_Voice   v = mel_audio_play(eng, src);

    f32 dst[BLOCK * CH];
    MEL_EXPECT(pull_until_audible(&mock.a, dst, BLOCK));

    Mel_Audio_Status st = mel_audio_set_device(eng, mel_audioout_find(S8("mock:b")));
    MEL_EXPECT(!mel_audio_failed(st));
    MEL_EXPECT(mock.b.opened);
    MEL_EXPECT(!mock.a.opened);
    MEL_EXPECT(mel_audio_voice_valid(eng, v));
    MEL_EXPECT(pull_until_audible(&mock.b, dst, BLOCK));

    mel_audio_destroy(eng);
    src->source_free(src, a);
    mel_audioout_shutdown();
}

MEL_TEST(device, set_device_failure_keeps_previous_binding)
{
    install();
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Audio_Opt o = opt();
    o.device = mel_audioout_find(S8("mock:a"));
    Mel_Audio* eng = mel_audio_create(a, o);
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Audio_Status st = mel_audio_set_device(eng, MEL_AUDIOOUT_NULL);
    MEL_EXPECT(!mel_audio_failed(st));
    st = mel_audio_set_device(eng, mel_audioout_find(S8("mock:a")));
    MEL_EXPECT(!mel_audio_failed(st));

    Mel_AudioOut dead = mel_audioout_find(S8("mock:b"));
    mock.b.present = false;
    mel_audioout_refresh();
    MEL_EXPECT(!mel_audioout_alive(dead));

    st = mel_audio_set_device(eng, dead);
    MEL_EXPECT(mel_audio_failed(st));
    MEL_EXPECT(st & MEL_AUDIO_RESULT_NO_DEVICE);
    MEL_EXPECT(mock.a.opened);
    MEL_EXPECT(mel_audioout_equal(mel_audio_device(eng), mel_audioout_find(S8("mock:a"))));

    mel_audio_destroy(eng);
    mel_audioout_shutdown();
}

MEL_TEST(device, pinned_loss_holds_until_rebind)
{
    install();
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Audio_Opt o = opt();
    o.device = mel_audioout_find(S8("mock:a"));
    Mel_Audio* eng = mel_audio_create(a, o);
    MEL_REQUIRE_NOT_NULL(eng);

    Event_Tally tally = { 0 };
    mel_event_subscribe_push(mel_audio_device_events(eng), mel_executor_inline(), on_device_event, &tally);

    mock.a.present = false;
    mel_audioout_refresh();

    MEL_EXPECT_EQ(tally.count, 1u);
    MEL_EXPECT(tally.last.lost);
    Mel_Audio_Status st = mel_audio_device_status(eng);
    MEL_EXPECT(mel_audio_failed(st));
    MEL_EXPECT(st & MEL_AUDIO_RESULT_DEVICE_LOST);

    st = mel_audio_set_device(eng, mel_audioout_find(S8("mock:b")));
    MEL_EXPECT(!mel_audio_failed(st));
    MEL_EXPECT_EQ(mel_audio_device_status(eng) & MEL_AUDIO_SEVERITY_MASK, (u32)MEL_AUDIO_OK);
    MEL_EXPECT(mock.b.opened);

    mel_audio_destroy(eng);
    mel_audioout_shutdown();
}

MEL_TEST(device, follow_mode_migrates_on_default_change)
{
    install();
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Audio* eng = mel_audio_create(a, opt());
    MEL_REQUIRE_NOT_NULL(eng);
    MEL_EXPECT(mock.a.opened);

    Event_Tally tally = { 0 };
    mel_event_subscribe_push(mel_audio_device_events(eng), mel_executor_inline(), on_device_event, &tally);

    mock.default_b = 1u;
    mel_audioout_refresh();

    MEL_EXPECT_EQ(tally.count, 1u);
    MEL_EXPECT(tally.last.default_changed);
    MEL_EXPECT(!mock.a.opened);
    MEL_EXPECT(mock.b.opened);
    MEL_EXPECT(mel_audioout_equal(mel_audio_device(eng), mel_audioout_find(S8("mock:b"))));

    mel_audio_destroy(eng);
    mel_audioout_shutdown();
}

MEL_TEST(device, interruption_holds_and_resumes)
{
    install();
    const Mel_Alloc* a = mel_alloc_heap();
    mel_audiopolicy_init(mel_alloc_heap(), NULL);

    Mel_Audio_Opt o = opt();
    o.device = mel_audioout_find(S8("mock:a"));
    Mel_Audio* eng = mel_audio_create(a, o);
    MEL_REQUIRE_NOT_NULL(eng);

    Event_Tally tally = { 0 };
    mel_event_subscribe_push(mel_audio_device_events(eng), mel_executor_inline(), on_device_event, &tally);

    mel_audiopolicy__emit(&(Mel_AudioPolicy_Event){ .interruption_began = true });

    MEL_EXPECT_EQ(tally.count, 1u);
    MEL_EXPECT(tally.last.interrupted);
    MEL_EXPECT(!mock.a.opened);
    Mel_Audio_Status st = mel_audio_device_status(eng);
    MEL_EXPECT(mel_audio_failed(st));
    MEL_EXPECT(st & MEL_AUDIO_RESULT_INTERRUPTED);

    mel_audiopolicy__emit(&(Mel_AudioPolicy_Event){ .interruption_ended = true, .should_resume = true });

    MEL_EXPECT_EQ(tally.count, 2u);
    MEL_EXPECT(tally.last.resumed);
    MEL_EXPECT(mock.a.opened);
    MEL_EXPECT_EQ(mel_audio_device_status(eng) & MEL_AUDIO_SEVERITY_MASK, (u32)MEL_AUDIO_OK);

    mel_audio_destroy(eng);
    mel_audiopolicy_shutdown();
    mel_audioout_shutdown();
}
