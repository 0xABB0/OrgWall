#include <gamepad/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.array/array.h>
#include <log/log.h>

#include <string.h>

typedef struct
{
    u64                     stable_id;
    bool                    active;
    Mel_Joystick_Descriptor desc;
    Mel_Joystick_State      state;
    Mel_Array(i16)          axes;
    Mel_Array(u8)           buttons;
    Mel_Array(u8)           hats;
    Mel_Joystick_Rumble     last_rumble;
    Mel_Joystick_Led        last_led;
} Virtual_Device;

typedef struct
{
    bool             initialized;
    const Mel_Alloc* alloc;
    Mel_Array(Virtual_Device) devices;
    u64                       next_id;
} Virtual_Registry;

static Virtual_Registry vr;

static Virtual_Device* virtual_find(u64 stable_id)
{
    for (usize i = 0; i < vr.devices.count; i++)
        if (vr.devices.items[i].active && vr.devices.items[i].stable_id == stable_id)
            return &vr.devices.items[i];
    return NULL;
}

static u32 virtual_enumerate(void* user, Mel_Joystick_Raw* out, u32 cap)
{
    MEL_UNUSED(user);
    u32 n = 0;
    for (usize i = 0; i < vr.devices.count && n < cap; i++)
    {
        Virtual_Device* d = &vr.devices.items[i];
        if (!d->active)
            continue;
        out[n].stable_id = d->stable_id;
        out[n].desc = d->desc;
        n++;
    }
    return n;
}

static bool virtual_poll(void* user, u64 stable_id, Mel_Joystick_State* out)
{
    MEL_UNUSED(user);
    Virtual_Device* d = virtual_find(stable_id);
    if (!d)
        return false;
    *out = d->state;
    out->axes = d->axes.items;
    out->axis_count = (u32)d->axes.count;
    out->buttons = d->buttons.items;
    out->button_count = (u32)d->buttons.count;
    out->hats = d->hats.items;
    out->hat_count = (u32)d->hats.count;
    return true;
}

static Mel_Joystick_Status virtual_rumble(void* user, u64 stable_id, Mel_Joystick_Rumble rumble)
{
    MEL_UNUSED(user);
    Virtual_Device* d = virtual_find(stable_id);
    if (!d)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_DEVICE_LOST;
    d->last_rumble = rumble;
    return MEL_JOYSTICK_OK;
}

static Mel_Joystick_Status virtual_led(void* user, u64 stable_id, Mel_Joystick_Led led)
{
    MEL_UNUSED(user);
    Virtual_Device* d = virtual_find(stable_id);
    if (!d)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_DEVICE_LOST;
    d->last_led = led;
    return MEL_JOYSTICK_OK;
}

static Mel_Joystick_Status virtual_set_player_index(void* user, u64 stable_id, i32 player_index)
{
    MEL_UNUSED(user);
    Virtual_Device* d = virtual_find(stable_id);
    if (!d)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_DEVICE_LOST;
    d->desc.player_index = player_index;
    return MEL_JOYSTICK_OK;
}

static void virtual_ensure(const Mel_Alloc* alloc)
{
    if (vr.initialized)
        return;
    vr.alloc = alloc ? alloc : mel_alloc_heap();
    mel_array_init(&vr.devices, vr.alloc);
    vr.next_id = 0x76697274u;
    vr.initialized = true;

    Mel_Joystick_Provider_Desc desc = {
        .name = "virtual",
        .enumerate = virtual_enumerate,
        .poll = virtual_poll,
        .rumble = virtual_rumble,
        .led = virtual_led,
        .set_player_index = virtual_set_player_index,
    };
    mel_joystick_provider_register(&desc);
}

Mel_Joystick_Virtual mel_joystick_virtual_create_opt(Mel_Joystick_Virtual_Opt opt)
{
    virtual_ensure(NULL);
    Virtual_Device d;
    memset(&d, 0, sizeof d);
    d.active = true;
    d.stable_id = vr.next_id++;
    d.desc = opt.desc;
    mel_array_init(&d.axes, vr.alloc);
    mel_array_init(&d.buttons, vr.alloc);
    mel_array_init(&d.hats, vr.alloc);
    for (u32 i = 0; i < opt.desc.axis_count; i++)
        mel_array_push(&d.axes, (i16)0);
    for (u32 i = 0; i < opt.desc.button_count; i++)
        mel_array_push(&d.buttons, (u8)0);
    for (u32 i = 0; i < opt.desc.hat_count; i++)
        mel_array_push(&d.hats, (u8)MEL_JOYSTICK_HAT_CENTERED);
    mel_array_push(&vr.devices, d);
    mel_log_info("gamepad", "virtual joystick created stable_id=%llu", (unsigned long long)d.stable_id);
    return (Mel_Joystick_Virtual){ .stable_id = d.stable_id };
}

void mel_joystick_virtual_destroy(Mel_Joystick_Virtual v)
{
    Virtual_Device* d = virtual_find(v.stable_id);
    if (!d)
        return;
    mel_array_free(&d->axes);
    mel_array_free(&d->buttons);
    mel_array_free(&d->hats);
    d->active = false;
}

void mel_joystick_virtual_set_state(Mel_Joystick_Virtual v, const Mel_Joystick_State* state)
{
    Virtual_Device* d = virtual_find(v.stable_id);
    if (!d || !state)
        return;
    d->state = *state;
    mel_array_clear(&d->axes);
    for (u32 i = 0; i < state->axis_count; i++)
        mel_array_push(&d->axes, state->axes[i]);
    mel_array_clear(&d->buttons);
    for (u32 i = 0; i < state->button_count; i++)
        mel_array_push(&d->buttons, state->buttons[i]);
    mel_array_clear(&d->hats);
    for (u32 i = 0; i < state->hat_count; i++)
        mel_array_push(&d->hats, state->hats[i]);
}

Mel_Joystick_Rumble mel_joystick_virtual_last_rumble(Mel_Joystick_Virtual v)
{
    Virtual_Device* d = virtual_find(v.stable_id);
    return d ? d->last_rumble : (Mel_Joystick_Rumble){ 0 };
}

Mel_Joystick_Led mel_joystick_virtual_last_led(Mel_Joystick_Virtual v)
{
    Virtual_Device* d = virtual_find(v.stable_id);
    return d ? d->last_led : (Mel_Joystick_Led){ 0 };
}
