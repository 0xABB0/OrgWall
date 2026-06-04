#pragma once

#include <core/types.h>
#include <string/str8.h>
#include <allocator/allocator.fwd.h>
#include <collection.slotmap/slotmap.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Executor Mel_Executor;
typedef struct Mel_Event    Mel_Event;

typedef u32 Mel_Input_Status;

#define MEL_INPUT_SEVERITY_MASK       0x3u
#define MEL_INPUT_OK                  0u
#define MEL_INPUT_WARNED              1u
#define MEL_INPUT_ERROR               2u

#define MEL_INPUT_INVALID_HANDLE      (1u << 2)
#define MEL_INPUT_UNSUPPORTED         (1u << 3)
#define MEL_INPUT_DEGRADED            (1u << 4)
#define MEL_INPUT_NO_PROVIDER         (1u << 5)
#define MEL_INPUT_AREA_IGNORED        (1u << 6)
#define MEL_INPUT_CAPTURE_EMULATED    (1u << 7)
#define MEL_INPUT_WARP_UNAVAILABLE    (1u << 8)
#define MEL_INPUT_CONFINE_UNAVAILABLE (1u << 9)
#define MEL_INPUT_CURSOR_QUANTIZED    (1u << 10)
#define MEL_INPUT_IME_SYNTHESIZED     (1u << 11)

static inline bool mel_input_status_failed(Mel_Input_Status s) { return (s & MEL_INPUT_SEVERITY_MASK) == MEL_INPUT_ERROR; }
static inline bool mel_input_status_warned(Mel_Input_Status s) { return (s & MEL_INPUT_SEVERITY_MASK) == MEL_INPUT_WARNED; }

enum
{
    MEL_INPUT_CAP_KEYBOARD = 1u << 0,
    MEL_INPUT_CAP_MOUSE = 1u << 1,
    MEL_INPUT_CAP_TOUCH = 1u << 2,
    MEL_INPUT_CAP_PEN = 1u << 3,
    MEL_INPUT_CAP_TEXT = 1u << 4,
    MEL_INPUT_CAP_IME = 1u << 5,
    MEL_INPUT_CAP_RELATIVE = 1u << 6,
    MEL_INPUT_CAP_CAPTURE = 1u << 7,
    MEL_INPUT_CAP_WARP = 1u << 8,
    MEL_INPUT_CAP_CONFINE = 1u << 9,
    MEL_INPUT_CAP_CURSOR = 1u << 10,
    MEL_INPUT_CAP_PRESSURE = 1u << 11,
    MEL_INPUT_CAP_TILT = 1u << 12,
    MEL_INPUT_CAP_HOVER = 1u << 13,
    MEL_INPUT_CAP_ROTATION = 1u << 14,
    MEL_INPUT_CAP_ERASER = 1u << 15,
    MEL_INPUT_CAP_VIRTUAL = 1u << 16,
};

typedef struct
{
    str8 name;
    u32  caps;

    u32 vendor_id;
    u32 product_id;

    u32 key_count;
    u32 button_count;
    u32 touch_point_max;
    u32 pen_button_count;

    bool touch_direct;
    bool touch_indirect;

    f32 pressure_max;
    f32 hover_distance_max;
} Mel_Input_Device_Descriptor;

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Input_Device;

#define MEL_INPUT_DEVICE_NULL ((Mel_Input_Device){ 0 })

typedef struct
{
    Mel_Input_Device_Descriptor value;
    Mel_Input_Status            status;
} Mel_Input_Describe_Result;

void mel_input_init(const Mel_Alloc* alloc);
void mel_input_init_ex(const Mel_Alloc* alloc, Mel_Executor* exec);
void mel_input_shutdown(void);

u32 mel_input_refresh(void);
u32 mel_input_count(void);
u32 mel_input_list(Mel_Input_Device* out, u32 cap);

Mel_Input_Describe_Result mel_input_describe(Mel_Input_Device d);
bool                      mel_input_alive(Mel_Input_Device d);
bool                      mel_input_equal(Mel_Input_Device a, Mel_Input_Device b);

void* mel_input_native(Mel_Input_Device d);

Mel_Event* mel_input_event_channel(void);

#ifdef __cplusplus
}
#endif
