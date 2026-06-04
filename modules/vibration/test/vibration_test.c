#include <test/test.h>

#include <vibration/vibration.h>
#include <vibration/ffb.h>
#include <vibration/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <string/str8.h>

#include <string.h>

#include "../src/vibration_internal.h"

static void noop_host_register(void) {}

typedef struct
{
    u64                stable_id;
    Mel_Vib_FF_Caps    caps;
    Mel_Vib_FF_Lowered last_upload;
    Mel_Vib_FF_Lowered last_update;
    u32                start_loop;
    int                start_count;
    int                stop_count;
    int                pause_count;
    int                resume_count;
    int                release_count;
    f32                last_gain;
    bool               last_autocenter;
    f32                last_autocenter_strength;
    bool               support_pause;
    int                submit_count;
} Mock_Dev;

static Mock_Dev g_mock;

static u32 mock_enumerate(void* user, Mel_Vib_Raw* out, u32 cap)
{
    MEL_UNUSED(user);
    if (cap >= 1)
    {
        memset(&out[0], 0, sizeof out[0]);
        out[0].stable_id = g_mock.stable_id;
        out[0].name = str8_from_cstr("mock wheel");
        out[0].caps.present = true;
        out[0].caps.amplitude = true;
        out[0].caps.continuous = true;
        out[0].caps.can_pause = true;
        out[0].caps.actuator_count = 1;
    }
    return 1;
}

static bool mock_open(void* user, u64 stable_id, Mel_Vib_Descriptor* out)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    memset(out, 0, sizeof *out);
    out->caps.present = true;
    return true;
}

static void mock_close(void* user, u64 stable_id)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
}

static Mel_Vib_Status mock_submit(void* user, u64 stable_id, u64 token, const Mel_Vib_Lowered* low, Mel_Vib_Completion comp)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    MEL_UNUSED(low);
    MEL_UNUSED(comp);
    g_mock.submit_count++;
    return MEL_VIB_OK;
}

static bool mock_ff_query(void* user, u64 stable_id, Mel_Vib_FF_Caps* out)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    *out = g_mock.caps;
    return true;
}

static Mel_Vib_Status mock_ff_upload(void* user, u64 stable_id, u64 token, const Mel_Vib_FF_Lowered* low)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    g_mock.last_upload = *low;
    return MEL_VIB_OK;
}

static Mel_Vib_Status mock_ff_update(void* user, u64 stable_id, u64 token, const Mel_Vib_FF_Lowered* low)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    g_mock.last_update = *low;
    return MEL_VIB_OK;
}

static Mel_Vib_Status mock_ff_start(void* user, u64 stable_id, u64 token, u32 loop)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    g_mock.start_count++;
    g_mock.start_loop = loop;
    return MEL_VIB_OK;
}

static Mel_Vib_Status mock_ff_stop(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    g_mock.stop_count++;
    return MEL_VIB_OK;
}

static Mel_Vib_Status mock_ff_pause(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    g_mock.pause_count++;
    return MEL_VIB_OK;
}

static Mel_Vib_Status mock_ff_resume(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    g_mock.resume_count++;
    return MEL_VIB_OK;
}

static void mock_ff_release(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    g_mock.release_count++;
}

static Mel_Vib_Status mock_ff_set_gain(void* user, u64 stable_id, f32 gain)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    g_mock.last_gain = gain;
    return MEL_VIB_OK;
}

static Mel_Vib_Status mock_ff_set_autocenter(void* user, u64 stable_id, bool enabled, f32 strength)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    g_mock.last_autocenter = enabled;
    g_mock.last_autocenter_strength = strength;
    return MEL_VIB_OK;
}

static void mock_reset_caps_full(void)
{
    memset(&g_mock.caps, 0, sizeof g_mock.caps);
    g_mock.caps.present = true;
    g_mock.caps.effects = MEL_VIB_FF_EFFECT_RUMBLE | MEL_VIB_FF_EFFECT_CONSTANT | MEL_VIB_FF_EFFECT_RAMP | MEL_VIB_FF_EFFECT_PERIODIC | MEL_VIB_FF_EFFECT_CONDITION;
    g_mock.caps.waveforms = MEL_VIB_FF_WAVE_SINE | MEL_VIB_FF_WAVE_SQUARE | MEL_VIB_FF_WAVE_TRIANGLE | MEL_VIB_FF_WAVE_SAWTOOTH_UP | MEL_VIB_FF_WAVE_SAWTOOTH_DOWN;
    g_mock.caps.conditions = MEL_VIB_FF_COND_SPRING | MEL_VIB_FF_COND_DAMPER | MEL_VIB_FF_COND_INERTIA | MEL_VIB_FF_COND_FRICTION;
    g_mock.caps.direction_axes = 3;
    g_mock.caps.gain = true;
    g_mock.caps.autocenter = true;
    g_mock.caps.autocenter_continuous = true;
    g_mock.caps.envelope = true;
    g_mock.caps.max_effects = 8;
    g_mock.caps.min_frequency_hz = 1.0f;
    g_mock.caps.max_frequency_hz = 50.0f;
}

static Mel_Vib_Provider_Desc mock_desc(void)
{
    Mel_Vib_Provider_Desc d;
    memset(&d, 0, sizeof d);
    d.name = "mock-ffb";
    d.enumerate = mock_enumerate;
    d.open = mock_open;
    d.close = mock_close;
    d.submit = mock_submit;
    d.ff_query = mock_ff_query;
    d.ff_upload = mock_ff_upload;
    d.ff_update = mock_ff_update;
    d.ff_start = mock_ff_start;
    d.ff_stop = mock_ff_stop;
    d.ff_pause = mock_ff_pause;
    d.ff_resume = mock_ff_resume;
    d.ff_release = mock_ff_release;
    d.ff_set_gain = mock_ff_set_gain;
    d.ff_set_autocenter = mock_ff_set_autocenter;
    return d;
}

static Mel_Vib_Device find_mock_device(void)
{
    Mel_Vib_Device list[8];
    u32            n = mel_vib_list(list, 8);
    for (u32 i = 0; i < n; i++)
    {
        Mel_Vib_Describe_Result r = mel_vib_describe(list[i]);
        if (r.status == MEL_VIB_OK && str8_equals(r.value.name, str8_from_cstr("mock wheel")))
            return list[i];
    }
    return MEL_VIB_DEVICE_NULL;
}

static Mel_Vib_Device setup_mock(Mel_Vib_Provider* out_prov)
{
    memset(&g_mock, 0, sizeof g_mock);
    g_mock.stable_id = 0xFEED1234u;
    mock_reset_caps_full();

    mel_vib__set_host_register(noop_host_register);
    mel_vib_init(mel_alloc_heap(), NULL);
    Mel_Vib_Provider_Desc d = mock_desc();
    Mel_Vib_Provider      p = mel_vib_provider_register(&d);
    if (out_prov)
        *out_prov = p;
    mel_vib_refresh();
    return find_mock_device();
}

MEL_TEST(ffb, dead_device_is_loud_not_fatal)
{
    Mel_Vib_Device bogus = { .h = { .index = 9999, .generation = 7 } };
    MEL_EXPECT(!mel_vib_alive(bogus));
    MEL_EXPECT(!mel_vib_ff_supported(bogus));
    Mel_Vib_FF_Caps_Result r = mel_vib_ff_caps(bogus);
    MEL_EXPECT(mel_vib_failed(r.status));
}

MEL_TEST(ffb, caps_round_trip_and_supported)
{
    Mel_Vib_Provider prov;
    Mel_Vib_Device   d = setup_mock(&prov);
    MEL_REQUIRE(mel_vib_alive(d));

    MEL_EXPECT(mel_vib_ff_supported(d));
    Mel_Vib_FF_Caps_Result r = mel_vib_ff_caps(d);
    MEL_REQUIRE_EQ(r.status, (Mel_Vib_Status)MEL_VIB_OK);
    MEL_EXPECT(r.value.present);
    MEL_EXPECT((r.value.effects & MEL_VIB_FF_EFFECT_PERIODIC) != 0u);
    MEL_EXPECT((r.value.conditions & MEL_VIB_FF_COND_SPRING) != 0u);
    MEL_EXPECT_EQ(r.value.direction_axes, 3u);

    mel_vib_provider_unregister(prov);
    mel_vib_shutdown();
}

MEL_TEST(ffb, upload_start_status_stop_lifecycle)
{
    Mel_Vib_Provider prov;
    Mel_Vib_Device   d = setup_mock(&prov);
    MEL_REQUIRE(mel_vib_alive(d));

    Mel_Vib_FF_Effect fx;
    memset(&fx, 0, sizeof fx);
    fx.effect = MEL_VIB_FF_EFFECT_PERIODIC;
    fx.duration_s = 1.0f;
    fx.periodic.waveform = MEL_VIB_FF_WAVE_SINE;
    fx.periodic.magnitude = 0.8f;
    fx.periodic.frequency_hz = 10.0f;
    fx.direction = mel_vib_ff_dir_cartesian(1.0f, 0.0f, 0.0f);

    Mel_Vib_FF_Upload_Result up = mel_vib_ff_upload(d, &fx);
    MEL_REQUIRE(!mel_vib_failed(up.status));
    MEL_REQUIRE(mel_vib_ff_alive(up.value));
    MEL_EXPECT_EQ(g_mock.last_upload.effect.periodic.waveform, (u32)MEL_VIB_FF_WAVE_SINE);

    Mel_Vib_FF_State_Result s0 = mel_vib_ff_status(up.value);
    MEL_REQUIRE_EQ(s0.status, (Mel_Vib_Status)MEL_VIB_OK);
    MEL_EXPECT(s0.value.active);
    MEL_EXPECT(!s0.value.playing);

    MEL_REQUIRE(!mel_vib_failed(mel_vib_ff_start(up.value, 3)));
    MEL_EXPECT_EQ(g_mock.start_count, 1);
    MEL_EXPECT_EQ(g_mock.start_loop, 3u);

    Mel_Vib_FF_State_Result s1 = mel_vib_ff_status(up.value);
    MEL_EXPECT(s1.value.playing);
    MEL_EXPECT_EQ(s1.value.loops_remaining, 3u);

    MEL_REQUIRE(!mel_vib_failed(mel_vib_ff_pause(up.value)));
    MEL_EXPECT_EQ(g_mock.pause_count, 1);
    Mel_Vib_FF_State_Result s2 = mel_vib_ff_status(up.value);
    MEL_EXPECT(s2.value.paused);
    MEL_EXPECT(!s2.value.playing);

    MEL_REQUIRE(!mel_vib_failed(mel_vib_ff_resume(up.value)));
    MEL_EXPECT_EQ(g_mock.resume_count, 1);
    MEL_EXPECT(mel_vib_ff_status(up.value).value.playing);

    MEL_REQUIRE(!mel_vib_failed(mel_vib_ff_stop(up.value)));
    MEL_EXPECT_EQ(g_mock.stop_count, 1);
    MEL_EXPECT(!mel_vib_ff_status(up.value).value.playing);

    mel_vib_ff_release(up.value);
    MEL_EXPECT_EQ(g_mock.release_count, 1);
    MEL_EXPECT(!mel_vib_ff_alive(up.value));

    mel_vib_provider_unregister(prov);
    mel_vib_shutdown();
}

MEL_TEST(ffb, condition_dropped_when_unsupported)
{
    Mel_Vib_Provider prov;
    Mel_Vib_Device   d = setup_mock(&prov);
    g_mock.caps.effects &= ~(u64)MEL_VIB_FF_EFFECT_CONDITION;
    g_mock.caps.conditions = 0;

    Mel_Vib_FF_Condition cond;
    memset(&cond, 0, sizeof cond);
    cond.kind = MEL_VIB_FF_COND_SPRING;
    cond.right_coeff = 0.5f;
    cond.left_coeff = 0.5f;

    Mel_Vib_FF_Effect fx;
    memset(&fx, 0, sizeof fx);
    fx.effect = MEL_VIB_FF_EFFECT_CONDITION;
    fx.conditions = &cond;
    fx.condition_count = 1;

    Mel_Vib_FF_Upload_Result up = mel_vib_ff_upload(d, &fx);
    MEL_REQUIRE(!mel_vib_failed(up.status));
    MEL_EXPECT(mel_vib_warned(up.status));
    MEL_EXPECT((up.status & MEL_VIB_FF_WARN_CONDITION_DROPPED) != 0u);
    MEL_EXPECT_EQ(g_mock.last_upload.effect.effect, (u32)MEL_VIB_FF_EFFECT_RUMBLE);

    mel_vib_ff_release(up.value);
    mel_vib_provider_unregister(prov);
    mel_vib_shutdown();
}

MEL_TEST(ffb, waveform_approximated_and_frequency_clamped)
{
    Mel_Vib_Provider prov;
    Mel_Vib_Device   d = setup_mock(&prov);
    g_mock.caps.waveforms = MEL_VIB_FF_WAVE_SINE;

    Mel_Vib_FF_Effect fx;
    memset(&fx, 0, sizeof fx);
    fx.effect = MEL_VIB_FF_EFFECT_PERIODIC;
    fx.periodic.waveform = MEL_VIB_FF_WAVE_SAWTOOTH_UP;
    fx.periodic.magnitude = 1.0f;
    fx.periodic.frequency_hz = 999.0f;

    Mel_Vib_FF_Upload_Result up = mel_vib_ff_upload(d, &fx);
    MEL_REQUIRE(!mel_vib_failed(up.status));
    MEL_EXPECT((up.status & MEL_VIB_FF_WARN_WAVEFORM_APPROX) != 0u);
    MEL_EXPECT((up.status & MEL_VIB_FF_WARN_FREQUENCY_CLAMPED) != 0u);
    MEL_EXPECT_EQ(g_mock.last_upload.effect.periodic.waveform, (u32)MEL_VIB_FF_WAVE_SINE);
    MEL_EXPECT_FLOAT_EQ(g_mock.last_upload.effect.periodic.frequency_hz, 50.0f, 0.01f);

    mel_vib_ff_release(up.value);
    mel_vib_provider_unregister(prov);
    mel_vib_shutdown();
}

MEL_TEST(ffb, cartesian_direction_flattened_on_one_axis_device)
{
    Mel_Vib_Provider prov;
    Mel_Vib_Device   d = setup_mock(&prov);
    g_mock.caps.direction_axes = 1;

    Mel_Vib_FF_Effect fx;
    memset(&fx, 0, sizeof fx);
    fx.effect = MEL_VIB_FF_EFFECT_CONSTANT;
    fx.constant.magnitude = 0.7f;
    fx.direction = mel_vib_ff_dir_cartesian(0.5f, 0.5f, 0.0f);

    Mel_Vib_FF_Upload_Result up = mel_vib_ff_upload(d, &fx);
    MEL_REQUIRE(!mel_vib_failed(up.status));
    MEL_EXPECT((up.status & MEL_VIB_FF_WARN_AXES_REDUCED) != 0u);
    MEL_EXPECT((up.status & MEL_VIB_FF_WARN_DIRECTION_FLATTENED) != 0u);

    mel_vib_ff_release(up.value);
    mel_vib_provider_unregister(prov);
    mel_vib_shutdown();
}

MEL_TEST(ffb, gain_and_autocenter_forwarded)
{
    Mel_Vib_Provider prov;
    Mel_Vib_Device   d = setup_mock(&prov);

    MEL_REQUIRE(!mel_vib_failed(mel_vib_ff_set_gain(d, 0.5f)));
    MEL_EXPECT_FLOAT_EQ(g_mock.last_gain, 0.5f, 0.001f);

    MEL_REQUIRE(!mel_vib_failed(mel_vib_ff_set_autocenter(d, true)));
    MEL_EXPECT(g_mock.last_autocenter);

    MEL_REQUIRE(!mel_vib_failed(mel_vib_ff_set_autocenter_strength(d, 0.25f)));
    MEL_EXPECT_FLOAT_EQ(g_mock.last_autocenter_strength, 0.25f, 0.001f);

    mel_vib_provider_unregister(prov);
    mel_vib_shutdown();
}

MEL_TEST(ffb, gain_quantized_when_device_lacks_gain)
{
    Mel_Vib_Provider prov;
    Mel_Vib_Device   d = setup_mock(&prov);
    g_mock.caps.gain = false;

    Mel_Vib_Status st = mel_vib_ff_set_gain(d, 0.5f);
    MEL_EXPECT(mel_vib_warned(st));
    MEL_EXPECT((st & MEL_VIB_FF_WARN_GAIN_QUANTIZED) != 0u);

    mel_vib_provider_unregister(prov);
    mel_vib_shutdown();
}

MEL_TEST(ffb, autocenter_quantized_when_device_lacks_continuous)
{
    Mel_Vib_Provider prov;
    Mel_Vib_Device   d = setup_mock(&prov);
    g_mock.caps.autocenter_continuous = false;

    Mel_Vib_Status st = mel_vib_ff_set_autocenter_strength(d, 0.3f);
    MEL_REQUIRE(!mel_vib_failed(st));
    MEL_EXPECT(mel_vib_warned(st));
    MEL_EXPECT((st & MEL_VIB_FF_WARN_AUTOCENTER_QUANTIZED) != 0u);
    MEL_EXPECT(g_mock.last_autocenter);

    Mel_Vib_Status on = mel_vib_ff_set_autocenter_strength(d, 1.0f);
    MEL_EXPECT((on & MEL_VIB_FF_WARN_AUTOCENTER_QUANTIZED) == 0u);
    Mel_Vib_Status off = mel_vib_ff_set_autocenter_strength(d, 0.0f);
    MEL_EXPECT((off & MEL_VIB_FF_WARN_AUTOCENTER_QUANTIZED) == 0u);

    g_mock.caps.autocenter_continuous = true;
    Mel_Vib_Status cont = mel_vib_ff_set_autocenter_strength(d, 0.3f);
    MEL_EXPECT((cont & MEL_VIB_FF_WARN_AUTOCENTER_QUANTIZED) == 0u);

    mel_vib_provider_unregister(prov);
    mel_vib_shutdown();
}

MEL_TEST(ffb, honest_absent_without_ffb_provider)
{
    memset(&g_mock, 0, sizeof g_mock);
    g_mock.stable_id = 0xABCDu;
    mock_reset_caps_full();

    mel_vib__set_host_register(noop_host_register);
    mel_vib_init(mel_alloc_heap(), NULL);
    Mel_Vib_Provider_Desc d = mock_desc();
    d.ff_query = NULL;
    d.ff_upload = NULL;
    Mel_Vib_Provider p = mel_vib_provider_register(&d);
    mel_vib_refresh();
    Mel_Vib_Device dev = find_mock_device();
    MEL_REQUIRE(mel_vib_alive(dev));

    MEL_EXPECT(!mel_vib_ff_supported(dev));
    Mel_Vib_FF_Caps_Result caps = mel_vib_ff_caps(dev);
    MEL_EXPECT_EQ(caps.status, (Mel_Vib_Status)MEL_VIB_OK);
    MEL_EXPECT(!caps.value.present);

    Mel_Vib_FF_Effect fx;
    memset(&fx, 0, sizeof fx);
    fx.effect = MEL_VIB_FF_EFFECT_RUMBLE;
    fx.constant.magnitude = 1.0f;
    Mel_Vib_FF_Upload_Result up = mel_vib_ff_upload(dev, &fx);
    MEL_EXPECT(mel_vib_failed(up.status));
    MEL_EXPECT(!mel_vib_ff_alive(up.value));

    mel_vib_provider_unregister(p);
    mel_vib_shutdown();
}

MEL_TEST(vibration, existing_timeline_play_still_works)
{
    Mel_Vib_Provider prov;
    Mel_Vib_Device   d = setup_mock(&prov);
    MEL_REQUIRE(mel_vib_alive(d));

    Mel_Vib_Event       ev = mel_vib_pulse(0.6f, 0.5f, 0.05f);
    Mel_Vib_Pattern     pat = { .events = &ev, .count = 1, .loop = 0 };
    Mel_Vib_Play_Result pr = mel_vib_play(d, &pat);
    MEL_EXPECT(!mel_vib_failed(pr.status));
    MEL_EXPECT_EQ(g_mock.submit_count, 1);

    mel_vib_abort(pr.value);
    mel_vib_provider_unregister(prov);
    mel_vib_shutdown();
}

MEL_TEST(ffb, stop_all_halts_playing_effects)
{
    Mel_Vib_Provider prov;
    Mel_Vib_Device   d = setup_mock(&prov);

    Mel_Vib_FF_Effect fx;
    memset(&fx, 0, sizeof fx);
    fx.effect = MEL_VIB_FF_EFFECT_RUMBLE;
    fx.constant.magnitude = 1.0f;

    Mel_Vib_FF_Upload_Result up = mel_vib_ff_upload(d, &fx);
    MEL_REQUIRE(!mel_vib_failed(up.status));
    MEL_REQUIRE(!mel_vib_failed(mel_vib_ff_start(up.value, 0)));
    int before = g_mock.stop_count;

    mel_vib_ff_stop_all(d);
    MEL_EXPECT_EQ(g_mock.stop_count, before + 1);
    MEL_EXPECT(!mel_vib_ff_status(up.value).value.playing);

    mel_vib_ff_release(up.value);
    mel_vib_provider_unregister(prov);
    mel_vib_shutdown();
}
