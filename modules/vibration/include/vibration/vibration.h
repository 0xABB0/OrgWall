#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <string/str8.h>
#include <allocator/allocator.fwd.h>
#include <collection.slotmap/slotmap.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Reactor Mel_Reactor;

typedef u32 Mel_Vib_Status;

#define MEL_VIB_SEVERITY_MASK 0x3u
#define MEL_VIB_OK            0u
#define MEL_VIB_WARNED        1u
#define MEL_VIB_ERROR         2u

#define MEL_VIB_WARN_AMPLITUDE_QUANTIZED    (1u << 2)
#define MEL_VIB_WARN_SHARPNESS_DROPPED      (1u << 3)
#define MEL_VIB_WARN_ENVELOPE_BAKED         (1u << 4)
#define MEL_VIB_WARN_PATTERN_TRUNCATED      (1u << 5)
#define MEL_VIB_WARN_COMPLETION_SYNTHESIZED (1u << 6)
#define MEL_VIB_WARN_PAUSE_QUANTIZED        (1u << 7)

#define MEL_VIB_RESULT_ABORTED     (1u << 8)
#define MEL_VIB_RESULT_DEVICE_LOST (1u << 9)

static inline bool mel_vib_failed(Mel_Vib_Status s) { return (s & MEL_VIB_SEVERITY_MASK) == MEL_VIB_ERROR; }
static inline bool mel_vib_warned(Mel_Vib_Status s) { return (s & MEL_VIB_SEVERITY_MASK) == MEL_VIB_WARNED; }

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Vib_Device;

#define MEL_VIB_DEVICE_NULL ((Mel_Vib_Device){ 0 })

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Vib_Playback;

#define MEL_VIB_PLAYBACK_NULL ((Mel_Vib_Playback){ 0 })

typedef struct
{
    bool present;
    bool amplitude;
    bool sharpness;
    f32  sharpness_min_hz;
    f32  sharpness_max_hz;
    bool envelopes;
    bool continuous;
    bool can_pause;
    bool pause_exact;
    bool completion_exact;
    u64  primitives;
    u32  actuator_count;
    u32  max_events;
    f32  max_duration_s;
    u32  max_envelope_points;
} Mel_Vib_Caps;

typedef struct
{
    str8         name;
    Mel_Vib_Caps caps;
} Mel_Vib_Descriptor;

typedef struct
{
    f32 t;
    f32 value;
} Mel_Vib_Breakpoint;

typedef struct
{
    const Mel_Vib_Breakpoint* points;
    u32                       count;
} Mel_Vib_Envelope;

typedef struct
{
    f32              at;
    f32              duration;
    f32              intensity;
    f32              sharpness;
    Mel_Vib_Envelope intensity_env;
    Mel_Vib_Envelope sharpness_env;
    u32              actuator_mask;
    u32              primitive;
    f32              primitive_scale;
} Mel_Vib_Event;

#define MEL_VIB_LOOP_FOREVER 0xFFFFFFFFu

typedef struct
{
    const Mel_Vib_Event* events;
    u32                  count;
    u32                  loop;
} Mel_Vib_Pattern;

static inline Mel_Vib_Event mel_vib_pulse(f32 amplitude, f32 sharpness, f32 duration_s)
{
    return (Mel_Vib_Event){ .at = 0.0f, .duration = duration_s, .intensity = amplitude, .sharpness = sharpness };
}

typedef void (*Mel_Vib_On_Complete)(Mel_Vib_Playback pb, Mel_Vib_Status status, void* user);

typedef struct
{
    Mel_Reactor*        reactor;
    Mel_Vib_On_Complete on_complete;
    void*               user;
} Mel_Vib_Play_Opt;

typedef struct
{
    Mel_Vib_Playback value;
    Mel_Vib_Status   status;
} Mel_Vib_Play_Result;

typedef struct
{
    Mel_Vib_Descriptor value;
    Mel_Vib_Status     status;
} Mel_Vib_Describe_Result;

void mel_vib_init(const Mel_Alloc* alloc, Mel_Reactor* reactor);
void mel_vib_shutdown(void);

u32 mel_vib_refresh(void);
u32 mel_vib_count(void);
u32 mel_vib_list(Mel_Vib_Device* out, u32 cap);

Mel_Vib_Describe_Result mel_vib_describe(Mel_Vib_Device d);
bool                    mel_vib_alive(Mel_Vib_Device d);
bool                    mel_vib_equal(Mel_Vib_Device a, Mel_Vib_Device b);

Mel_Vib_Play_Result mel_vib_play_opt(Mel_Vib_Device d, const Mel_Vib_Pattern* p, Mel_Vib_Play_Opt opt);
#define mel_vib_play(d, p, ...) mel_vib_play_opt((d), (p), (Mel_Vib_Play_Opt){ __VA_ARGS__ })

Mel_Vib_Status mel_vib_pause(Mel_Vib_Playback pb);
Mel_Vib_Status mel_vib_resume(Mel_Vib_Playback pb);
void           mel_vib_abort(Mel_Vib_Playback pb);
void           mel_vib_abort_all(Mel_Vib_Device d);

bool mel_vib_playing(Mel_Vib_Playback pb);
bool mel_vib_paused(Mel_Vib_Playback pb);

void* mel_vib_native(Mel_Vib_Device d);

#ifdef __cplusplus
}
#endif
