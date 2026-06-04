#pragma once

#include <gamepad/joystick.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    u64                     stable_id;
    Mel_Joystick_Descriptor desc;
} Mel_Joystick_Raw;

typedef struct
{
    const char* name;
    void*       user;

    u32  (*enumerate)(void* user, Mel_Joystick_Raw* out, u32 cap);
    bool (*open)(void* user, u64 stable_id, Mel_Joystick_Descriptor* out);
    void (*close)(void* user, u64 stable_id);

    bool (*poll)(void* user, u64 stable_id, Mel_Joystick_State* out);

    Mel_Joystick_Status (*rumble)(void* user, u64 stable_id, Mel_Joystick_Rumble rumble);
    Mel_Joystick_Status (*led)(void* user, u64 stable_id, Mel_Joystick_Led led);
    Mel_Joystick_Status (*set_player_index)(void* user, u64 stable_id, i32 player_index);
    Mel_Joystick_Status (*effect)(void* user, u64 stable_id, const void* data, usize size);

    void* (*steam_input_handle)(void* user, u64 stable_id);
    void* (*native)(void* user, u64 stable_id);
} Mel_Joystick_Provider_Desc;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Joystick_Provider;

Mel_Joystick_Provider mel_joystick_provider_register(const Mel_Joystick_Provider_Desc* desc);
void                  mel_joystick_provider_unregister(Mel_Joystick_Provider p);

void mel_joystick__register_host_providers(void);

typedef struct
{
    Mel_Joystick_Descriptor desc;
} Mel_Joystick_Virtual_Opt;

typedef struct
{
    u64 stable_id;
} Mel_Joystick_Virtual;

#define MEL_JOYSTICK_VIRTUAL_NULL ((Mel_Joystick_Virtual){ 0 })

Mel_Joystick_Virtual mel_joystick_virtual_create_opt(Mel_Joystick_Virtual_Opt opt);
#define mel_joystick_virtual_create(...) mel_joystick_virtual_create_opt((Mel_Joystick_Virtual_Opt){ __VA_ARGS__ })

void mel_joystick_virtual_destroy(Mel_Joystick_Virtual v);

void mel_joystick_virtual_set_state(Mel_Joystick_Virtual v, const Mel_Joystick_State* state);

Mel_Joystick_Rumble mel_joystick_virtual_last_rumble(Mel_Joystick_Virtual v);
Mel_Joystick_Led    mel_joystick_virtual_last_led(Mel_Joystick_Virtual v);

#ifdef __cplusplus
}
#endif
