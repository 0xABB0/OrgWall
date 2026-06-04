#pragma once

#include <core/types.h>
#include <string/str8.fwd.h>
#include <allocator/allocator.fwd.h>
#include <collection/slotmap.fwd.h>
#include <guid/guid.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Executor Mel_Executor;

typedef u32 Mel_Joystick_Status;

#define MEL_JOYSTICK_SEVERITY_MASK 0x3u
#define MEL_JOYSTICK_OK            0u
#define MEL_JOYSTICK_WARNED        1u
#define MEL_JOYSTICK_ERROR         2u

#define MEL_JOYSTICK_INVALID_HANDLE     (1u << 2)
#define MEL_JOYSTICK_DEVICE_LOST        (1u << 3)
#define MEL_JOYSTICK_UNSUPPORTED        (1u << 4)
#define MEL_JOYSTICK_RUMBLE_QUANTIZED   (1u << 5)
#define MEL_JOYSTICK_TRIGGER_RUMBLE_OFF (1u << 6)
#define MEL_JOYSTICK_LED_UNSUPPORTED    (1u << 7)
#define MEL_JOYSTICK_EFFECT_REJECTED    (1u << 8)
#define MEL_JOYSTICK_NO_PROVIDER        (1u << 9)

static inline bool mel_joystick_failed(Mel_Joystick_Status s) { return (s & MEL_JOYSTICK_SEVERITY_MASK) == MEL_JOYSTICK_ERROR; }
static inline bool mel_joystick_warned(Mel_Joystick_Status s) { return (s & MEL_JOYSTICK_SEVERITY_MASK) == MEL_JOYSTICK_WARNED; }

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Joystick;

#define MEL_JOYSTICK_NULL ((Mel_Joystick){ 0 })

typedef struct
{
    bool wireless;
    bool charging;
    bool has_battery;
    f32  battery_level;
} Mel_Joystick_Power;

typedef struct
{
    bool dual_motor_rumble;
    bool trigger_rumble;
    bool player_led;
    bool rgb_led;
    bool manufacturer_effects;

    bool gyro;
    bool accel;
    bool touchpad;
    u32  touchpad_finger_cap;
    f32  touchpad_width_px;
    f32  touchpad_height_px;

    bool steam_input;
} Mel_Joystick_Features;

typedef struct
{
    str8 name;
    str8 serial;

    Mel_Guid guid;

    u16 vendor_id;
    u16 product_id;
    u16 version;
    u16 firmware_version;

    u32 axis_count;
    u32 button_count;
    u32 hat_count;
    u32 ball_count;

    i32 player_index;

    Mel_Joystick_Power    power;
    Mel_Joystick_Features features;
} Mel_Joystick_Descriptor;

typedef struct
{
    Mel_Joystick_Descriptor value;
    Mel_Joystick_Status     status;
} Mel_Joystick_Describe_Result;

#define MEL_JOYSTICK_HAT_CENTERED  0x00u
#define MEL_JOYSTICK_HAT_UP        0x01u
#define MEL_JOYSTICK_HAT_RIGHT     0x02u
#define MEL_JOYSTICK_HAT_DOWN      0x04u
#define MEL_JOYSTICK_HAT_LEFT      0x08u
#define MEL_JOYSTICK_HAT_RIGHTUP   (MEL_JOYSTICK_HAT_RIGHT | MEL_JOYSTICK_HAT_UP)
#define MEL_JOYSTICK_HAT_RIGHTDOWN (MEL_JOYSTICK_HAT_RIGHT | MEL_JOYSTICK_HAT_DOWN)
#define MEL_JOYSTICK_HAT_LEFTUP    (MEL_JOYSTICK_HAT_LEFT | MEL_JOYSTICK_HAT_UP)
#define MEL_JOYSTICK_HAT_LEFTDOWN  (MEL_JOYSTICK_HAT_LEFT | MEL_JOYSTICK_HAT_DOWN)

typedef struct
{
    i16 x;
    i16 y;
} Mel_Joystick_Ball;

typedef struct
{
    u32  finger_id;
    bool down;
    f32  x;
    f32  y;
    f32  pressure;
} Mel_Joystick_Touch;

typedef struct
{
    f32 x;
    f32 y;
    f32 z;
} Mel_Joystick_Vec3;

typedef struct
{
    const i16*               axes;
    u32                      axis_count;
    const u8*                buttons;
    u32                      button_count;
    const u8*                hats;
    u32                      hat_count;
    const Mel_Joystick_Ball* balls;
    u32                      ball_count;

    const Mel_Joystick_Touch* touches;
    u32                       touch_count;

    bool              has_gyro;
    Mel_Joystick_Vec3 gyro_rad_s;
    bool              has_accel;
    Mel_Joystick_Vec3 accel_m_s2;
    u64               sensor_timestamp_ns;
} Mel_Joystick_State;

typedef struct
{
    Mel_Joystick_State value;
    Mel_Joystick_Status status;
} Mel_Joystick_State_Result;

void mel_joystick_init(const Mel_Alloc* alloc);
void mel_joystick_init_ex(const Mel_Alloc* alloc, Mel_Executor* exec);
void mel_joystick_shutdown(void);

u32 mel_joystick_refresh(void);
u32 mel_joystick_count(void);
u32 mel_joystick_list(Mel_Joystick* out, u32 cap);

Mel_Joystick_Describe_Result mel_joystick_describe(Mel_Joystick j);
bool                         mel_joystick_alive(Mel_Joystick j);
bool                         mel_joystick_equal(Mel_Joystick a, Mel_Joystick b);

Mel_Joystick_State_Result mel_joystick_poll(Mel_Joystick j);

typedef struct
{
    f32 low_frequency;
    f32 high_frequency;
    f32 left_trigger;
    f32 right_trigger;
    f32 duration_s;
} Mel_Joystick_Rumble;

Mel_Joystick_Status mel_joystick_rumble(Mel_Joystick j, Mel_Joystick_Rumble r);

typedef struct
{
    u8 red;
    u8 green;
    u8 blue;
} Mel_Joystick_Led;

Mel_Joystick_Status mel_joystick_led(Mel_Joystick j, Mel_Joystick_Led led);
Mel_Joystick_Status mel_joystick_player_index(Mel_Joystick j, i32 player_index);

Mel_Joystick_Status mel_joystick_effect(Mel_Joystick j, const void* data, usize size);

void* mel_joystick_steam_input_handle(Mel_Joystick j);

void* mel_joystick_native(Mel_Joystick j);

#ifdef __cplusplus
}
#endif
