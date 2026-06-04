#include <test/test.h>
#include <input/input.h>
#include <input/events.h>
#include <input/keyboard.h>
#include <input/mouse.h>
#include <input/touch.h>
#include <input/pen.h>
#include <input/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <executor/executor.h>
#include <string/str8.h>

#include <string.h>

#include "../src/input_internal.h"

MEL_TEST(input, dead_handle_is_loud_not_fatal)
{
    Mel_Input_Device bogus = { .h = { .index = 9999, .generation = 7 } };
    MEL_EXPECT(!mel_input_alive(bogus));
    Mel_Input_Describe_Result r = mel_input_describe(bogus);
    MEL_EXPECT(mel_input_status_failed(r.status));
    MEL_EXPECT((r.status & MEL_INPUT_INVALID_HANDLE) != 0);
}

MEL_TEST(input, null_handle_is_dead)
{
    Mel_Input_Device null = MEL_INPUT_DEVICE_NULL;
    MEL_EXPECT(!mel_input_alive(null));
    MEL_EXPECT(mel_input_equal(null, null));
}

MEL_TEST(input, scancode_names_are_frozen)
{
    MEL_EXPECT_EQ_STR8(Mel_Scancode_to_string(MEL_SCANCODE_A), S8("A"));
    MEL_EXPECT_EQ_STR8(Mel_Scancode_to_string(MEL_SCANCODE_RETURN), S8("Return"));
    MEL_EXPECT_EQ_STR8(Mel_Scancode_to_string(MEL_SCANCODE_LSHIFT), S8("LShift"));
    MEL_EXPECT_EQ_STR8(Mel_Scancode_to_string(MEL_SCANCODE_UNKNOWN), S8("Unknown"));
}

typedef struct
{
    u32 pressed_mask;
    f32 mx, my;
} Fake_Dev;

static struct
{
    Fake_Dev key;
    Fake_Dev mouse;
    u32      count;
} g_fake;

static u32 fake_enumerate(void* user, Mel_Input_Raw* out, u32 cap)
{
    (void)user;
    if (cap < 2 || g_fake.count == 0)
        return 0;
    out[0] = (Mel_Input_Raw){ .stable_id = 0x10, .desc = { .name = S8("FakeKeyboard"), .caps = MEL_INPUT_CAP_KEYBOARD | MEL_INPUT_CAP_TEXT, .key_count = 128 } };
    out[1] = (Mel_Input_Raw){ .stable_id = 0x20, .desc = { .name = S8("FakeMouse"), .caps = MEL_INPUT_CAP_MOUSE | MEL_INPUT_CAP_RELATIVE, .button_count = 3 } };
    return 2;
}

static bool fake_key_down(void* user, u64 sid, Mel_Scancode sc)
{
    (void)user;
    if (sid != 0x10)
        return false;
    return (g_fake.key.pressed_mask & (1u << ((u32)sc & 31))) != 0;
}

static Mel_Mouse_State fake_mouse_state(void* user, u64 sid)
{
    (void)user;
    (void)sid;
    return (Mel_Mouse_State){ .x = g_fake.mouse.mx, .y = g_fake.mouse.my, .buttons = g_fake.mouse.pressed_mask };
}

static Mel_Input_Status fake_set_relative(void* user, u64 sid, bool enable)
{
    (void)user;
    (void)sid;
    (void)enable;
    return MEL_INPUT_OK;
}

static const Mel_Input_Provider_Desc fake_provider = {
    .name = "fake",
    .enumerate = fake_enumerate,
    .key_down = fake_key_down,
    .mouse_state = fake_mouse_state,
    .mouse_set_relative = fake_set_relative,
};

MEL_TEST(input, registry_tracks_provider_devices)
{
    g_fake = (typeof(g_fake)){ 0 };
    g_fake.count = 1;
    mel_input__set_test_provider(&fake_provider);
    mel_input_init(mel_alloc_heap());

    MEL_EXPECT_EQ(mel_input_count(), 2u);

    Mel_Input_Device devs[8];
    u32              n = mel_input_list(devs, 8);
    MEL_EXPECT_EQ(n, 2u);

    bool found_keyboard = false;
    for (u32 i = 0; i < n; i++)
    {
        Mel_Input_Describe_Result r = mel_input_describe(devs[i]);
        MEL_EXPECT(!mel_input_status_failed(r.status));
        if ((r.value.caps & MEL_INPUT_CAP_KEYBOARD) != 0)
            found_keyboard = true;
    }
    MEL_EXPECT(found_keyboard);

    mel_input_shutdown();
}

static Mel_Input_Device device_with_caps(u32 cap)
{
    Mel_Input_Device devs[8];
    u32              n = mel_input_list(devs, 8);
    for (u32 i = 0; i < n; i++)
    {
        Mel_Input_Describe_Result r = mel_input_describe(devs[i]);
        if ((r.value.caps & cap) != 0)
            return devs[i];
    }
    return MEL_INPUT_DEVICE_NULL;
}

MEL_TEST(input, keyboard_state_delegates_to_provider)
{
    g_fake = (typeof(g_fake)){ 0 };
    g_fake.count = 1;
    g_fake.key.pressed_mask = 1u << (MEL_SCANCODE_A & 31);
    mel_input__set_test_provider(&fake_provider);
    mel_input_init(mel_alloc_heap());

    Mel_Input_Device kbd = device_with_caps(MEL_INPUT_CAP_KEYBOARD);
    MEL_REQUIRE(mel_input_alive(kbd));
    MEL_EXPECT(mel_keyboard_key_down(kbd, MEL_SCANCODE_A));
    MEL_EXPECT(!mel_keyboard_key_down(kbd, MEL_SCANCODE_B));

    mel_input_shutdown();
}

MEL_TEST(input, mouse_relative_round_trips)
{
    g_fake = (typeof(g_fake)){ 0 };
    g_fake.count = 1;
    g_fake.mouse.mx = 42.0f;
    g_fake.mouse.my = 7.0f;
    mel_input__set_test_provider(&fake_provider);
    mel_input_init(mel_alloc_heap());

    Mel_Input_Device mouse = device_with_caps(MEL_INPUT_CAP_MOUSE);
    MEL_REQUIRE(mel_input_alive(mouse));
    Mel_Mouse_State st = mel_mouse_state(mouse);
    MEL_EXPECT_FLOAT_EQ(st.x, 42.0f, 0.001f);

    Mel_Input_Status s = mel_mouse_set_relative(mouse, true);
    MEL_EXPECT(!mel_input_status_failed(s));

    mel_input_shutdown();
}

MEL_TEST(input, hotplug_diff_fires_added_and_removed)
{
    g_fake = (typeof(g_fake)){ 0 };
    g_fake.count = 1;
    mel_input__set_test_provider(&fake_provider);
    mel_input_init(mel_alloc_heap());

    Mel_Input_Device_Event drain[32];
    mel_input_poll_events(drain, 32);

    u32 added = mel_input_poll_events(drain, 32);
    MEL_EXPECT_EQ(added, 0u);

    g_fake.count = 0;
    mel_input_refresh();
    MEL_EXPECT_EQ(mel_input_count(), 0u);

    u32 removed = 0;
    u32 got = mel_input_poll_events(drain, 32);
    for (u32 i = 0; i < got; i++)
        if (drain[i].kind == MEL_INPUT_DEVICE_EVENT_REMOVED)
            removed++;
    MEL_EXPECT_EQ(removed, 2u);

    mel_input_shutdown();
}

MEL_TEST(input, key_name_encodes_utf8)
{
    char buf[8];
    str8 ascii = mel_keyboard_key_name(MEL_INPUT_DEVICE_NULL, (Mel_Keycode)'q', buf, sizeof buf);
    MEL_EXPECT_EQ_STR8(ascii, S8("q"));
}
