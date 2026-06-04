#pragma once

#include <vibration/vibration.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define MEL_VIB_FF_WARN_DIRECTION_FLATTENED (1u << 10)
#define MEL_VIB_FF_WARN_CONDITION_DROPPED   (1u << 11)
#define MEL_VIB_FF_WARN_WAVEFORM_APPROX     (1u << 12)
#define MEL_VIB_FF_WARN_ENVELOPE_DROPPED    (1u << 13)
#define MEL_VIB_FF_WARN_GAIN_QUANTIZED      (1u << 14)
#define MEL_VIB_FF_WARN_AUTOCENTER_ABSENT   (1u << 15)
#define MEL_VIB_FF_WARN_RAMP_APPROX         (1u << 16)
#define MEL_VIB_FF_WARN_AXES_REDUCED        (1u << 17)
#define MEL_VIB_FF_WARN_FREQUENCY_CLAMPED   (1u << 18)
#define MEL_VIB_FF_WARN_AUTOCENTER_QUANTIZED (1u << 19)

enum
{
    MEL_VIB_FF_WAVE_SINE = 1u << 0,
    MEL_VIB_FF_WAVE_SQUARE = 1u << 1,
    MEL_VIB_FF_WAVE_TRIANGLE = 1u << 2,
    MEL_VIB_FF_WAVE_SAWTOOTH_UP = 1u << 3,
    MEL_VIB_FF_WAVE_SAWTOOTH_DOWN = 1u << 4,
};

enum
{
    MEL_VIB_FF_COND_SPRING = 1u << 0,
    MEL_VIB_FF_COND_DAMPER = 1u << 1,
    MEL_VIB_FF_COND_INERTIA = 1u << 2,
    MEL_VIB_FF_COND_FRICTION = 1u << 3,
};

enum
{
    MEL_VIB_FF_EFFECT_RUMBLE = 1u << 0,
    MEL_VIB_FF_EFFECT_CONSTANT = 1u << 1,
    MEL_VIB_FF_EFFECT_RAMP = 1u << 2,
    MEL_VIB_FF_EFFECT_PERIODIC = 1u << 3,
    MEL_VIB_FF_EFFECT_CONDITION = 1u << 4,
};

enum
{
    MEL_VIB_FF_DIR_POLAR = 0,
    MEL_VIB_FF_DIR_CARTESIAN = 1,
    MEL_VIB_FF_DIR_SPHERICAL = 2,
    MEL_VIB_FF_DIR_STEERING_AXIS = 3,
};

typedef struct
{
    u32 encoding;
    f32 a;
    f32 b;
    f32 c;
} Mel_Vib_FF_Direction;

#define MEL_VIB_FF_DURATION_INFINITE 0.0f

static inline Mel_Vib_FF_Direction mel_vib_ff_dir_polar(f32 radians) { return (Mel_Vib_FF_Direction){ .encoding = MEL_VIB_FF_DIR_POLAR, .a = radians }; }

static inline Mel_Vib_FF_Direction mel_vib_ff_dir_cartesian(f32 x, f32 y, f32 z) { return (Mel_Vib_FF_Direction){ .encoding = MEL_VIB_FF_DIR_CARTESIAN, .a = x, .b = y, .c = z }; }

static inline Mel_Vib_FF_Direction mel_vib_ff_dir_spherical(f32 azimuth, f32 elevation) { return (Mel_Vib_FF_Direction){ .encoding = MEL_VIB_FF_DIR_SPHERICAL, .a = azimuth, .b = elevation }; }

static inline Mel_Vib_FF_Direction mel_vib_ff_dir_steering(f32 signed_axis) { return (Mel_Vib_FF_Direction){ .encoding = MEL_VIB_FF_DIR_STEERING_AXIS, .a = signed_axis }; }

typedef struct
{
    f32 attack_s;
    f32 attack_level;
    f32 fade_s;
    f32 fade_level;
} Mel_Vib_FF_Envelope;

typedef struct
{
    f32 magnitude;
} Mel_Vib_FF_Constant;

typedef struct
{
    f32 start;
    f32 end;
} Mel_Vib_FF_Ramp;

typedef struct
{
    u32 waveform;
    f32 magnitude;
    f32 frequency_hz;
    f32 offset;
    f32 phase;
} Mel_Vib_FF_Periodic;

typedef struct
{
    u32 kind;
    f32 right_coeff;
    f32 left_coeff;
    f32 right_saturation;
    f32 left_saturation;
    f32 deadband;
    f32 center;
} Mel_Vib_FF_Condition;

typedef struct
{
    u32                         effect;
    f32                         duration_s;
    f32                         start_delay_s;
    u32                         loop;
    Mel_Vib_FF_Direction        direction;
    Mel_Vib_FF_Envelope         envelope;
    Mel_Vib_FF_Constant         constant;
    Mel_Vib_FF_Ramp             ramp;
    Mel_Vib_FF_Periodic         periodic;
    const Mel_Vib_FF_Condition* conditions;
    u32                         condition_count;
} Mel_Vib_FF_Effect;

typedef struct
{
    bool present;
    u64  effects;
    u64  waveforms;
    u64  conditions;
    u32  direction_axes;
    bool gain;
    bool autocenter;
    bool autocenter_continuous;
    bool envelope;
    u32  max_effects;
    f32  min_frequency_hz;
    f32  max_frequency_hz;
} Mel_Vib_FF_Caps;

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Vib_FF_Slot;

#define MEL_VIB_FF_SLOT_NULL ((Mel_Vib_FF_Slot){ 0 })

typedef struct
{
    bool active;
    bool playing;
    bool paused;
    u32  loops_remaining;
} Mel_Vib_FF_State;

typedef struct
{
    Mel_Vib_FF_Caps value;
    Mel_Vib_Status  status;
} Mel_Vib_FF_Caps_Result;

typedef struct
{
    Mel_Vib_FF_Slot value;
    Mel_Vib_Status  status;
} Mel_Vib_FF_Upload_Result;

typedef struct
{
    Mel_Vib_FF_State value;
    Mel_Vib_Status   status;
} Mel_Vib_FF_State_Result;

Mel_Vib_FF_Caps_Result mel_vib_ff_caps(Mel_Vib_Device d);
bool                   mel_vib_ff_supported(Mel_Vib_Device d);

Mel_Vib_FF_Upload_Result mel_vib_ff_upload(Mel_Vib_Device d, const Mel_Vib_FF_Effect* effect);
Mel_Vib_Status           mel_vib_ff_update(Mel_Vib_FF_Slot s, const Mel_Vib_FF_Effect* effect);
Mel_Vib_Status           mel_vib_ff_start(Mel_Vib_FF_Slot s, u32 loop);
Mel_Vib_Status           mel_vib_ff_stop(Mel_Vib_FF_Slot s);
Mel_Vib_Status           mel_vib_ff_pause(Mel_Vib_FF_Slot s);
Mel_Vib_Status           mel_vib_ff_resume(Mel_Vib_FF_Slot s);
void                     mel_vib_ff_release(Mel_Vib_FF_Slot s);

Mel_Vib_FF_State_Result mel_vib_ff_status(Mel_Vib_FF_Slot s);
bool                    mel_vib_ff_alive(Mel_Vib_FF_Slot s);

Mel_Vib_Status mel_vib_ff_set_gain(Mel_Vib_Device d, f32 gain);
Mel_Vib_Status mel_vib_ff_set_autocenter(Mel_Vib_Device d, bool enabled);
Mel_Vib_Status mel_vib_ff_set_autocenter_strength(Mel_Vib_Device d, f32 strength);
void           mel_vib_ff_stop_all(Mel_Vib_Device d);

#ifdef __cplusplus
}
#endif
