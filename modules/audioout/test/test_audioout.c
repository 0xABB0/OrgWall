#include <test/test.h>

#include <audioout/audioout.h>
#include <audioout/events.h>
#include <audioout/os.h>
#include <audioout/provider.h>

#include "../src/audioout_internal.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <core/types.h>
#include <executor/executor.h>
#include <string/str8.h>

#include <string.h>

void mel_audioout__register_host_providers(void) {}

typedef struct
{
    const char* stable_id;
    const char* name;
    u32         channels;
    u32         samplerate;
    bool        volume_cap;
    bool        mute_cap;
} Mock_Device;

typedef struct
{
    Mock_Device devices[8];
    u32         count;
    u32         default_idx;
    u32         rates[2];
    f32         volume;
    bool        muted;
    bool        set_volume_called;
    bool        set_muted_called;
} Mock_State;

static Mock_State mock1;

static str8 cstr(const char* s) { return (str8){ (u8*)s, (size)strlen(s) }; }

static void mock_enumerate(void* user, Mel_AudioOut_Enum_Fn fn, void* fn_user)
{
    Mock_State* m = user;
    for (u32 i = 0; i < m->count; i++)
    {
        Mel_AudioOut_Raw raw = {
            .stable_id = cstr(m->devices[i].stable_id),
            .name = cstr(m->devices[i].name),
            .kind = &mel_audioout_builtin,
            .channels = m->devices[i].channels,
            .samplerate = m->devices[i].samplerate,
            .samplerates = m->rates,
            .samplerate_count = 2,
            .caps = { .volume = m->devices[i].volume_cap, .mute = m->devices[i].mute_cap },
            .volume = m->volume,
            .muted = m->muted,
        };
        if (!fn(&raw, fn_user))
            return;
    }
}

static str8 mock_default_id(void* user)
{
    Mock_State* m = user;
    if (m->default_idx >= m->count)
        return STR8_EMPTY;
    return cstr(m->devices[m->default_idx].stable_id);
}

static f32 mock_volume(void* user, str8 stable_id)
{
    MEL_UNUSED(stable_id);
    Mock_State* m = user;
    return m->volume;
}

static Mel_AudioOut_Status mock_set_volume(void* user, str8 stable_id, f32 volume)
{
    MEL_UNUSED(stable_id);
    Mock_State* m = user;
    m->volume = volume;
    m->set_volume_called = true;
    return MEL_AUDIOOUT_OK;
}

static bool mock_muted(void* user, str8 stable_id)
{
    MEL_UNUSED(stable_id);
    Mock_State* m = user;
    return m->muted;
}

static Mel_AudioOut_Status mock_set_muted(void* user, str8 stable_id, bool muted)
{
    MEL_UNUSED(stable_id);
    Mock_State* m = user;
    m->muted = muted;
    m->set_muted_called = true;
    return MEL_AUDIOOUT_OK;
}

static const Mel_AudioOut_Provider_Desc MOCK_DESC1 = {
    .name = "mock1",
    .user = &mock1,
    .enumerate = mock_enumerate,
    .default_id = mock_default_id,
    .volume = mock_volume,
    .set_volume = mock_set_volume,
    .muted = mock_muted,
    .set_muted = mock_set_muted,
};

static void mock_reset(Mock_State* m)
{
    memset(m, 0, sizeof *m);
    m->rates[0] = 44100;
    m->rates[1] = 48000;
    m->volume = 0.5f;
    m->default_idx = 0;
}

static Mel_AudioOut_Provider install(void)
{
    mock_reset(&mock1);
    mock1.devices[0] = (Mock_Device){ "mock:speakers", "Speakers", 2, 48000, true, true };
    mock1.devices[1] = (Mock_Device){ "mock:hdmi", "HDMI Out", 8, 48000, false, false };
    mock1.count = 2;
    mel_audioout_init(mel_alloc_heap(), NULL);
    Mel_AudioOut_Provider p = mel_audioout_provider_register(&MOCK_DESC1);
    mel_audioout_refresh();
    return p;
}

MEL_TEST(audioout, enumerates_and_describes)
{
    install();
    MEL_EXPECT_EQ(mel_audioout_count(), 2u);

    Mel_AudioOut spk = mel_audioout_find(S8("mock:speakers"));
    MEL_REQUIRE(mel_audioout_alive(spk));

    Mel_AudioOut_Describe_Result d = mel_audioout_describe(spk, mel_alloc_heap());
    MEL_EXPECT(!mel_audioout_status_failed(d.status));
    MEL_EXPECT_EQ_STR8(d.value.name, S8("Speakers"));
    MEL_EXPECT_EQ_STR8(d.value.stable_id, S8("mock:speakers"));
    MEL_EXPECT(d.value.kind == &mel_audioout_builtin);
    MEL_EXPECT_EQ(d.value.channels, 2u);
    MEL_EXPECT_EQ(d.value.samplerates.count, (usize)2);
    MEL_EXPECT(d.value.caps.volume);
    mel_audioout_describe_free(&d);

    mel_audioout_shutdown();
}

MEL_TEST(audioout, find_unknown_is_null)
{
    install();
    MEL_EXPECT(!mel_audioout_alive(mel_audioout_find(S8("mock:ghost"))));
    mel_audioout_shutdown();
}

MEL_TEST(audioout, reconciliation_keeps_surviving_handles)
{
    install();
    Mel_AudioOut spk = mel_audioout_find(S8("mock:speakers"));
    Mel_AudioOut hdmi = mel_audioout_find(S8("mock:hdmi"));

    mock1.devices[1] = (Mock_Device){ "mock:usb-dac", "USB DAC", 2, 96000, true, true };
    mel_audioout_refresh();

    MEL_EXPECT_EQ(mel_audioout_count(), 2u);
    MEL_EXPECT(mel_audioout_alive(spk));
    MEL_EXPECT(!mel_audioout_alive(hdmi));
    MEL_EXPECT(mel_audioout_equal(mel_audioout_find(S8("mock:speakers")), spk));
    MEL_EXPECT(mel_audioout_alive(mel_audioout_find(S8("mock:usb-dac"))));

    mel_audioout_shutdown();
}

typedef struct
{
    u32 added;
    u32 removed;
    u32 changed;
    u32 default_changed;
} Event_Tally;

static void on_event(const Mel_AudioOut_Event* ev, void* user)
{
    Event_Tally* t = user;
    if (ev->added)
        t->added++;
    if (ev->removed)
        t->removed++;
    if (ev->changed)
        t->changed++;
    if (ev->default_changed)
        t->default_changed++;
}

MEL_TEST(audioout, default_tracking_fires_event)
{
    Mel_AudioOut_Provider p = install();
    Mel_AudioOut          spk = mel_audioout_find(S8("mock:speakers"));
    MEL_EXPECT(mel_audioout_equal(mel_audioout_default(), spk));

    Event_Tally              tally = { 0 };
    Mel_AudioOut_Hotplug_Sub sub = mel_audioout_subscribe(NULL, on_event, &tally);

    mock1.default_idx = 1;
    mel_audioout_provider_notify(p);
    MEL_EXPECT(mel_audioout_equal(mel_audioout_default(), mel_audioout_find(S8("mock:hdmi"))));
    MEL_EXPECT_EQ(tally.default_changed, 1u);

    mel_audioout_unsubscribe(sub);
    mel_audioout_shutdown();
}

MEL_TEST(audioout, volume_mute_caps_gating)
{
    install();
    Mel_AudioOut spk = mel_audioout_find(S8("mock:speakers"));
    Mel_AudioOut hdmi = mel_audioout_find(S8("mock:hdmi"));

    Mel_AudioOut_Status st = mel_audioout_set_volume(hdmi, 0.3f);
    MEL_EXPECT(mel_audioout_status_failed(st));
    MEL_EXPECT(st & MEL_AUDIOOUT_RESULT_UNSUPPORTED);
    MEL_EXPECT(!mock1.set_volume_called);

    st = mel_audioout_set_volume(spk, 0.3f);
    MEL_EXPECT(!mel_audioout_status_failed(st));
    MEL_EXPECT(mock1.set_volume_called);
    MEL_EXPECT_FLOAT_EQ(mel_audioout_volume(spk), 0.3f, 0.0f);

    MEL_EXPECT(!mel_audioout_muted(spk));
    st = mel_audioout_set_muted(spk, true);
    MEL_EXPECT(!mel_audioout_status_failed(st));
    MEL_EXPECT(mock1.set_muted_called);
    MEL_EXPECT(mel_audioout_muted(spk));

    st = mel_audioout_set_muted(hdmi, true);
    MEL_EXPECT(mel_audioout_status_failed(st));

    mel_audioout_shutdown();
}

MEL_TEST(audioout, external_volume_change_fires_changed)
{
    Mel_AudioOut_Provider    p = install();
    Event_Tally              tally = { 0 };
    Mel_AudioOut_Hotplug_Sub sub = mel_audioout_subscribe(NULL, on_event, &tally);

    mock1.volume = 0.9f;
    mel_audioout_provider_notify(p);
    MEL_EXPECT_EQ(tally.changed, 2u);

    mock1.muted = true;
    mel_audioout_provider_notify(p);
    MEL_EXPECT_EQ(tally.changed, 4u);

    mel_audioout_provider_notify(p);
    MEL_EXPECT_EQ(tally.changed, 4u);

    mel_audioout_unsubscribe(sub);
    mel_audioout_shutdown();
}

typedef struct
{
    f32 fill;
    u32 frames_limit;
    u32 pulls;
    u32 lost;
} Pull_State;

static void pull_on_lost(void* token)
{
    Pull_State* ps = token;
    ps->lost++;
}

static u32 pull_constant(void* token, f32* dst, u32 frames)
{
    Pull_State* ps = token;
    u32         give = frames < ps->frames_limit ? frames : ps->frames_limit;
    for (u32 i = 0; i < give * 2u; i++)
        dst[i] = ps->fill;
    ps->pulls++;
    return give;
}

MEL_TEST(audioout, publish_negotiates_and_sums)
{
    install();
    const Mel_Alloc* a = mel_alloc_heap();
    Event_Tally      tally = { 0 };
    mel_audioout_subscribe(NULL, on_event, &tally);

    u32 before = mel_audioout_count();

    Mel_AudioOut_Publish_Result pub = mel_audioout_publish(a,
                                                           (Mel_AudioOut_Publish_Opt){
                                                               .name = S8("App Sink"),
                                                               .channels = 2,
                                                               .samplerate = 48000,
                                                               .ring_capacity_frames = 256,
                                                           });
    MEL_REQUIRE(!mel_audioout_status_failed(pub.status));
    MEL_EXPECT(mel_audioout_status_warned(pub.status));
    MEL_EXPECT(pub.status & MEL_AUDIOOUT_WARN_LOCAL_ONLY);
    MEL_EXPECT(!mel_audioout_publish_os_visible(pub.published));
    MEL_EXPECT_EQ(mel_audioout_count(), before + 1u);
    MEL_EXPECT_EQ(tally.added, 1u);
    MEL_REQUIRE(mel_audioout_alive(pub.device));

    Mel_AudioOut_Describe_Result d = mel_audioout_describe(pub.device, a);
    MEL_EXPECT(d.value.kind == &mel_audioout_virtual);
    mel_audioout_describe_free(&d);

    Pull_State p1 = { .fill = 0.25f, .frames_limit = 64u };
    Pull_State p2 = { .fill = 0.5f, .frames_limit = 32u };

    Mel_AudioOut_Format   req = { .samplerate = 44100, .channels = 1, .block_frames = 512 };
    Mel_AudioOut_Open_Opt oopt = { 0 };
    Mel_AudioOut_Granted  granted = { 0 };
    Mel_AudioOut_Source   s1 = { .pull = pull_constant, .on_lost = pull_on_lost, .token = &p1 };
    Mel_AudioOut_Source   s2 = { .pull = pull_constant, .on_lost = pull_on_lost, .token = &p2 };
    MEL_EXPECT(!mel_audioout_status_failed(mel_audioout__open(pub.device, req, oopt, &granted, s1)));
    MEL_EXPECT_EQ(granted.format.samplerate, 48000u);
    MEL_EXPECT_EQ(granted.format.channels, 2u);
    MEL_EXPECT_EQ(granted.format.block_frames, 256u);
    MEL_EXPECT(!granted.exclusive);

    Mel_AudioOut_Granted granted2 = { 0 };
    MEL_EXPECT(!mel_audioout_status_failed(mel_audioout__open(pub.device, req, oopt, &granted2, s2)));

    f32 dst[64 * 2];
    MEL_EXPECT_EQ(mel_audioout_publish_read(pub.published, dst, 64u), 0u);

    mel_audioout__start(pub.device, &p1);
    mel_audioout__start(pub.device, &p2);

    u32 got = mel_audioout_publish_read(pub.published, dst, 64u);
    MEL_EXPECT_EQ(got, 64u);
    MEL_EXPECT_FLOAT_EQ(dst[0], 0.75f, 1e-6f);
    MEL_EXPECT_FLOAT_EQ(dst[31 * 2], 0.75f, 1e-6f);
    MEL_EXPECT_FLOAT_EQ(dst[32 * 2], 0.25f, 1e-6f);
    MEL_EXPECT_FLOAT_EQ(dst[63 * 2 + 1], 0.25f, 1e-6f);

    mel_audioout__stop(pub.device, &p1);
    got = mel_audioout_publish_read(pub.published, dst, 64u);
    MEL_EXPECT_EQ(got, 32u);
    MEL_EXPECT_FLOAT_EQ(dst[0], 0.5f, 1e-6f);

    mel_audioout__close(pub.device, &p1);
    got = mel_audioout_publish_read(pub.published, dst, 64u);
    MEL_EXPECT_EQ(got, 32u);

    mel_audioout_unpublish(pub.published);
    MEL_EXPECT_EQ(mel_audioout_count(), before);
    MEL_EXPECT_EQ(tally.removed, 1u);
    MEL_EXPECT(!mel_audioout_alive(pub.device));
    MEL_EXPECT_EQ(p1.lost, 0u);
    MEL_EXPECT_EQ(p2.lost, 1u);

    mel_audioout_shutdown();
}

MEL_TEST(audioout, open_on_dead_handle_is_lost)
{
    install();
    Mel_AudioOut          ghost = mel_audioout_find(S8("mock:ghost"));
    Mel_AudioOut_Format   req = { .samplerate = 48000, .channels = 2, .block_frames = 256 };
    Mel_AudioOut_Open_Opt oopt = { 0 };
    Mel_AudioOut_Granted  granted = { 0 };
    Pull_State            ps = { .fill = 0.f, .frames_limit = 16u };

    Mel_AudioOut_Source src = { .pull = pull_constant, .on_lost = pull_on_lost, .token = &ps };
    Mel_AudioOut_Status st = mel_audioout__open(ghost, req, oopt, &granted, src);
    MEL_EXPECT(mel_audioout_status_failed(st));
    MEL_EXPECT(st & MEL_AUDIOOUT_RESULT_LOST);

    mel_audioout_shutdown();
}

MEL_TEST(audioout, refresh_without_change_is_silent)
{
    install();
    Event_Tally              tally = { 0 };
    Mel_AudioOut_Hotplug_Sub sub = mel_audioout_subscribe(NULL, on_event, &tally);

    mel_audioout_refresh();
    MEL_EXPECT_EQ(tally.added, 0u);
    MEL_EXPECT_EQ(tally.removed, 0u);
    MEL_EXPECT_EQ(tally.changed, 0u);
    MEL_EXPECT_EQ(tally.default_changed, 0u);

    mel_audioout_unsubscribe(sub);
    mel_audioout_shutdown();
}
