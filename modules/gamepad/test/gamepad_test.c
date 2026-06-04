#include <test/test.h>

#include <gamepad/joystick.h>
#include <gamepad/events.h>
#include <gamepad/provider.h>
#include <gamepad/gamepad.h>
#include <gamepad/protocol.h>
#include <guid/guid.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <string/str8.h>

#include <string.h>

#include "../src/joystick_backend.h"

static void noop_host_register(void) {}

MEL_TEST(guid, vidpid_round_trip)
{
    Mel_Guid g = mel_guid_from_vidpid(0x045E, 0x028E, 0x0114);
    u16      v = 0, p = 0, ver = 0;
    MEL_REQUIRE(mel_guid_vidpid(g, &v, &p, &ver));
    MEL_EXPECT_EQ(v, 0x045Eu);
    MEL_EXPECT_EQ(p, 0x028Eu);
    MEL_EXPECT_EQ(ver, 0x0114u);
}

MEL_TEST(guid, string_round_trip)
{
    Mel_Guid g = mel_guid_from_vidpid(0x054C, 0x05C4, 0x0001);
    char     buf[64];
    size     n = mel_guid_to_string(g, buf, sizeof buf);
    MEL_EXPECT_EQ(n, 32);

    Mel_Guid back = MEL_GUID_ZERO;
    MEL_REQUIRE(mel_guid_from_string((str8){ (u8*)buf, n }, &back));
    MEL_EXPECT(mel_guid_equal(g, back));
}

MEL_TEST(guid, zero_has_no_vidpid)
{
    MEL_EXPECT(mel_guid_is_zero(MEL_GUID_ZERO));
    MEL_EXPECT(!mel_guid_vidpid(MEL_GUID_ZERO, NULL, NULL, NULL));
}

MEL_TEST(joystick, dead_handle_is_loud_not_fatal)
{
    Mel_Joystick bogus = { .h = { .index = 9999, .generation = 7 } };
    MEL_EXPECT(!mel_joystick_alive(bogus));
    Mel_Joystick_Describe_Result r = mel_joystick_describe(bogus);
    MEL_EXPECT(mel_joystick_failed(r.status));
    MEL_EXPECT((r.status & MEL_JOYSTICK_INVALID_HANDLE) != 0u);
}

MEL_TEST(joystick, virtual_device_appears_through_spine)
{
    mel_joystick__set_host_register(noop_host_register);
    mel_joystick_init(mel_alloc_heap());

    Mel_Joystick_Descriptor desc;
    memset(&desc, 0, sizeof desc);
    desc.axis_count = 6;
    desc.button_count = MEL_GAMEPAD_BUTTON_COUNT;
    desc.guid = mel_guid_from_vidpid(0x045E, 0x028E, 0x0001);
    desc.features.dual_motor_rumble = true;
    desc.features.rgb_led = true;

    Mel_Joystick_Virtual v = mel_joystick_virtual_create(.desc = desc);
    MEL_REQUIRE(v.stable_id != 0);

    u32 before = mel_joystick_count();
    mel_joystick_refresh();
    MEL_EXPECT_GE(mel_joystick_count(), before + 1u);

    Mel_Joystick list[16];
    u32          n = mel_joystick_list(list, 16);
    MEL_REQUIRE_GE(n, 1u);

    bool found = false;
    for (u32 i = 0; i < n; i++)
    {
        Mel_Joystick_Describe_Result r = mel_joystick_describe(list[i]);
        if (r.status == MEL_JOYSTICK_OK && r.value.axis_count == 6 && r.value.features.rgb_led)
        {
            found = true;
            Mel_Joystick_Status rs = mel_joystick_rumble(list[i], (Mel_Joystick_Rumble){ .low_frequency = 0.5f, .high_frequency = 0.25f });
            MEL_EXPECT(!mel_joystick_failed(rs));
            Mel_Joystick_Rumble last = mel_joystick_virtual_last_rumble(v);
            MEL_EXPECT_FLOAT_EQ(last.low_frequency, 0.5f, 0.001f);

            Mel_Joystick_Status ls = mel_joystick_led(list[i], (Mel_Joystick_Led){ .red = 10, .green = 20, .blue = 30 });
            MEL_EXPECT(!mel_joystick_failed(ls));
            MEL_EXPECT_EQ(mel_joystick_virtual_last_led(v).green, 20);
        }
    }
    MEL_EXPECT(found);

    mel_joystick_virtual_destroy(v);
    mel_joystick_refresh();
    mel_joystick_shutdown();
}

MEL_TEST(joystick, trigger_rumble_warns_when_unsupported)
{
    mel_joystick__set_host_register(noop_host_register);
    mel_joystick_init(mel_alloc_heap());

    Mel_Joystick_Descriptor desc;
    memset(&desc, 0, sizeof desc);
    desc.axis_count = 6;
    desc.button_count = 4;
    desc.features.dual_motor_rumble = true;

    Mel_Joystick_Virtual v = mel_joystick_virtual_create(.desc = desc);
    mel_joystick_refresh();

    Mel_Joystick list[16];
    u32          n = mel_joystick_list(list, 16);
    bool         found = false;
    for (u32 i = 0; i < n; i++)
    {
        Mel_Joystick_Describe_Result r = mel_joystick_describe(list[i]);
        if (r.status == MEL_JOYSTICK_OK && !r.value.features.trigger_rumble && r.value.features.dual_motor_rumble)
        {
            found = true;
            Mel_Joystick_Status rs = mel_joystick_rumble(list[i], (Mel_Joystick_Rumble){ .left_trigger = 0.7f });
            MEL_EXPECT((rs & MEL_JOYSTICK_TRIGGER_RUMBLE_OFF) != 0u);
        }
    }
    MEL_EXPECT(found);

    mel_joystick_virtual_destroy(v);
    mel_joystick_refresh();
    mel_joystick_shutdown();
}

MEL_TEST(gamepad, db_parses_bundled)
{
    Mel_Gamepad_Db* db = mel_gamepad_db_create(mel_alloc_heap(), .platform_filter = S8("Mac OS X"));
    MEL_REQUIRE_NOT_NULL(db);
    u32 added = mel_gamepad_db_load_bundled(db);
    MEL_EXPECT_GE(added, 1u);
    MEL_EXPECT_EQ(mel_gamepad_db_count(db), added);
    mel_gamepad_db_destroy(db);
}

MEL_TEST(gamepad, db_filters_by_platform)
{
    Mel_Gamepad_Db* mac = mel_gamepad_db_create(mel_alloc_heap(), .platform_filter = S8("Mac OS X"));
    Mel_Gamepad_Db* lin = mel_gamepad_db_create(mel_alloc_heap(), .platform_filter = S8("Linux"));
    mel_gamepad_db_load_bundled(mac);
    mel_gamepad_db_load_bundled(lin);
    MEL_EXPECT_NEQ(mel_gamepad_db_count(mac), mel_gamepad_db_count(lin));
    mel_gamepad_db_destroy(mac);
    mel_gamepad_db_destroy(lin);
}

MEL_TEST(gamepad, binding_and_read_through_mapping)
{
    mel_joystick__set_host_register(noop_host_register);
    mel_joystick_init(mel_alloc_heap());

    Mel_Gamepad_Db* db = mel_gamepad_db_create(mel_alloc_heap(), .platform_filter = S8("Mac OS X"));
    mel_gamepad_db_load_line(db, S8("030000005e0400008e02000000007200,Test 360,a:b0,b:b1,x:b2,y:b3,leftx:a0,lefttrigger:a2,dpup:h0.1,platform:Mac OS X,"));
    mel_gamepad_set_db(db);

    Mel_Guid guid;
    MEL_REQUIRE(mel_guid_from_string(S8("030000005e0400008e02000000007200"), &guid));

    Mel_Joystick_Descriptor desc;
    memset(&desc, 0, sizeof desc);
    desc.guid = guid;
    desc.axis_count = 6;
    desc.button_count = 4;
    desc.hat_count = 1;

    Mel_Joystick_Virtual v = mel_joystick_virtual_create(.desc = desc);
    mel_joystick_refresh();

    Mel_Joystick list[16];
    u32          n = mel_joystick_list(list, 16);
    Mel_Joystick target = MEL_JOYSTICK_NULL;
    for (u32 i = 0; i < n; i++)
    {
        Mel_Joystick_Describe_Result r = mel_joystick_describe(list[i]);
        if (r.status == MEL_JOYSTICK_OK && mel_guid_equal(r.value.guid, guid))
            target = list[i];
    }
    MEL_REQUIRE(mel_joystick_alive(target));
    MEL_EXPECT(mel_gamepad_supported(target));

    Mel_Gamepad_Binding bind;
    MEL_REQUIRE(mel_gamepad_button_binding(target, MEL_GAMEPAD_BUTTON_SOUTH, &bind));
    MEL_EXPECT_EQ(bind.kind, MEL_GAMEPAD_BIND_BUTTON);
    MEL_EXPECT_EQ(bind.index, 0u);

    MEL_REQUIRE(mel_gamepad_axis_binding(target, MEL_GAMEPAD_AXIS_LEFT_X, &bind));
    MEL_EXPECT_EQ(bind.kind, MEL_GAMEPAD_BIND_AXIS);

    i16 axes[6] = { 16000, 0, 24000, 0, 0, 0 };
    u8  buttons[4] = { 1, 0, 0, 0 };
    u8  hats[1] = { MEL_JOYSTICK_HAT_UP };
    Mel_Joystick_State st;
    memset(&st, 0, sizeof st);
    st.axes = axes;
    st.axis_count = 6;
    st.buttons = buttons;
    st.button_count = 4;
    st.hats = hats;
    st.hat_count = 1;
    mel_joystick_virtual_set_state(v, &st);

    Mel_Gamepad_Frame frame = mel_gamepad_read(target);
    MEL_REQUIRE(frame.valid);
    MEL_EXPECT(frame.down[MEL_GAMEPAD_BUTTON_SOUTH]);
    MEL_EXPECT(frame.down[MEL_GAMEPAD_BUTTON_DPAD_UP]);
    MEL_EXPECT(frame.axis[MEL_GAMEPAD_AXIS_LEFT_X] > 0.4f);
    MEL_EXPECT(frame.axis[MEL_GAMEPAD_AXIS_LEFT_TRIGGER] > 0.6f);

    mel_gamepad_set_db(NULL);
    mel_gamepad_db_destroy(db);
    mel_joystick_virtual_destroy(v);
    mel_joystick_refresh();
    mel_joystick_shutdown();
}

MEL_TEST(gamepad, regional_labels)
{
    MEL_EXPECT_EQ_STR8(mel_gamepad_button_label(MEL_GAMEPAD_BUTTON_SOUTH, MEL_GAMEPAD_FACE_LABELS_AB), S8("A"));
    MEL_EXPECT_EQ_STR8(mel_gamepad_button_label(MEL_GAMEPAD_BUTTON_SOUTH, MEL_GAMEPAD_FACE_LABELS_SONY), S8("Cross"));
    MEL_EXPECT_EQ_STR8(mel_gamepad_button_label(MEL_GAMEPAD_BUTTON_EAST, MEL_GAMEPAD_FACE_LABELS_SONY), S8("Circle"));
    MEL_EXPECT_EQ_STR8(mel_gamepad_button_label(MEL_GAMEPAD_BUTTON_NORTH, MEL_GAMEPAD_FACE_LABELS_NINTENDO), S8("X"));
}

MEL_TEST(gamepad, protocol_enum_reflection)
{
    MEL_EXPECT_EQ_STR8(Mel_Gamepad_Button_to_string(MEL_GAMEPAD_BUTTON_SOUTH), S8("South"));
    MEL_EXPECT_EQ_STR8(Mel_Gamepad_Axis_to_string(MEL_GAMEPAD_AXIS_LEFT_TRIGGER), S8("LeftTrigger"));
    MEL_EXPECT_EQ_STR8(Mel_Scancode_to_string(MEL_SCANCODE_A), S8("A"));
}

MEL_TEST(joystick, event_spine_added_removed)
{
    mel_joystick__set_host_register(noop_host_register);
    mel_joystick_init(mel_alloc_heap());

    Mel_Joystick_Descriptor desc;
    memset(&desc, 0, sizeof desc);
    desc.axis_count = 6;
    desc.button_count = MEL_GAMEPAD_BUTTON_COUNT;

    Mel_Joystick_Virtual v = mel_joystick_virtual_create(.desc = desc);
    MEL_REQUIRE(v.stable_id != 0);

    mel_joystick_refresh();

    Mel_Joystick_Event evs[32];
    u32                ne = mel_joystick_poll_events(evs, 32);
    bool               added = false;
    Mel_Joystick       added_handle = MEL_JOYSTICK_NULL;
    for (u32 i = 0; i < ne; i++)
        if (evs[i].kind == MEL_JOYSTICK_EVENT_ADDED)
        {
            added = true;
            added_handle = evs[i].joystick;
        }
    MEL_EXPECT(added);
    MEL_REQUIRE(mel_joystick_alive(added_handle));

    mel_joystick_virtual_destroy(v);
    mel_joystick_refresh();
    ne = mel_joystick_poll_events(evs, 32);
    bool removed = false;
    for (u32 i = 0; i < ne; i++)
        if (evs[i].kind == MEL_JOYSTICK_EVENT_REMOVED)
            removed = true;
    MEL_EXPECT(removed);

    mel_joystick_shutdown();
}

MEL_TEST(joystick, enumerate_grows_past_initial_cap)
{
    mel_joystick__set_host_register(noop_host_register);
    mel_joystick_init(mel_alloc_heap());

    Mel_Joystick_Descriptor desc;
    memset(&desc, 0, sizeof desc);
    desc.axis_count = 2;
    desc.button_count = 4;

    const u32             made = 20;
    Mel_Joystick_Virtual* vs = (Mel_Joystick_Virtual*)mel_alloc(mel_alloc_heap(), sizeof(Mel_Joystick_Virtual) * made);
    MEL_REQUIRE_NOT_NULL(vs);
    for (u32 i = 0; i < made; i++)
        vs[i] = mel_joystick_virtual_create(.desc = desc);

    mel_joystick_refresh();
    MEL_EXPECT_GE(mel_joystick_count(), made);

    for (u32 i = 0; i < made; i++)
        mel_joystick_virtual_destroy(vs[i]);
    mel_joystick_refresh();
    mel_dealloc(mel_alloc_heap(), vs);
    mel_joystick_shutdown();
}

MEL_TEST(joystick, descriptor_name_survives_refresh)
{
    mel_joystick__set_host_register(noop_host_register);
    mel_joystick_init(mel_alloc_heap());

    char namebuf[32];
    strncpy(namebuf, "WanderingName", sizeof namebuf - 1);
    namebuf[sizeof namebuf - 1] = '\0';

    Mel_Joystick_Descriptor desc;
    memset(&desc, 0, sizeof desc);
    desc.axis_count = 2;
    desc.button_count = 4;
    desc.name = str8_from_cstr(namebuf);

    Mel_Joystick_Virtual v = mel_joystick_virtual_create(.desc = desc);
    mel_joystick_refresh();

    memset(namebuf, 'Z', sizeof namebuf);

    Mel_Joystick list[16];
    u32          n = mel_joystick_list(list, 16);
    bool         matched = false;
    for (u32 i = 0; i < n; i++)
    {
        Mel_Joystick_Describe_Result r = mel_joystick_describe(list[i]);
        if (r.status == MEL_JOYSTICK_OK && str8_equals(r.value.name, S8("WanderingName")))
            matched = true;
    }
    MEL_EXPECT(matched);

    mel_joystick_virtual_destroy(v);
    mel_joystick_refresh();
    mel_joystick_shutdown();
}
