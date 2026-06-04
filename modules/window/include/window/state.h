#pragma once

#include <core/types.h>
#include <string/str8.h>
#include <future/future.h>

#include <window/window.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef u32 Mel_Window_Status;

#define MEL_WINDOW_SEVERITY_MASK 0x3u
#define MEL_WINDOW_OK            0u
#define MEL_WINDOW_WARNED        1u
#define MEL_WINDOW_ERROR         2u

#define MEL_WINDOW_INVALID_HANDLE   (1u << 2)
#define MEL_WINDOW_UNAVAILABLE      (1u << 3)
#define MEL_WINDOW_REJECTED         (1u << 4)
#define MEL_WINDOW_CLAMPED          (1u << 5)
#define MEL_WINDOW_BACKEND_DEFERRED (1u << 6)
#define MEL_WINDOW_CANCELLED        (1u << 7)

static inline bool mel_window_status_failed(Mel_Window_Status s) { return (s & MEL_WINDOW_SEVERITY_MASK) == MEL_WINDOW_ERROR; }
static inline bool mel_window_status_warned(Mel_Window_Status s) { return (s & MEL_WINDOW_SEVERITY_MASK) == MEL_WINDOW_WARNED; }
static inline bool mel_window_status_ok(Mel_Window_Status s) { return (s & MEL_WINDOW_SEVERITY_MASK) == MEL_WINDOW_OK; }
static inline bool mel_window_status_unavailable(Mel_Window_Status s) { return (s & MEL_WINDOW_UNAVAILABLE) != 0u; }
static inline bool mel_window_status_clamped(Mel_Window_Status s) { return (s & MEL_WINDOW_CLAMPED) != 0u; }

enum
{
    MEL_WINDOW_STATE_SHOWN       = 1u << 0,
    MEL_WINDOW_STATE_HIDDEN      = 1u << 1,
    MEL_WINDOW_STATE_MINIMIZED   = 1u << 2,
    MEL_WINDOW_STATE_MAXIMIZED   = 1u << 3,
    MEL_WINDOW_STATE_FULLSCREEN  = 1u << 4,
    MEL_WINDOW_STATE_FOCUSED     = 1u << 5,
    MEL_WINDOW_STATE_OCCLUDED    = 1u << 6,
    MEL_WINDOW_STATE_BORDERLESS  = 1u << 7,
    MEL_WINDOW_STATE_RESIZABLE   = 1u << 8,
    MEL_WINDOW_STATE_ALWAYS_TOP  = 1u << 9,
    MEL_WINDOW_STATE_MOUSE_GRAB  = 1u << 10,
    MEL_WINDOW_STATE_KEY_GRAB    = 1u << 11,
    MEL_WINDOW_STATE_MODAL       = 1u << 12,
    MEL_WINDOW_STATE_TRANSPARENT = 1u << 13,
};

enum
{
    MEL_WINDOW_FULLSCREEN_OFF       = 0u,
    MEL_WINDOW_FULLSCREEN_DESKTOP   = 1u << 0,
    MEL_WINDOW_FULLSCREEN_EXCLUSIVE = 1u << 1,
};

enum
{
    MEL_WINDOW_PROGRESS_NONE          = 0u,
    MEL_WINDOW_PROGRESS_INDETERMINATE = 1u << 0,
    MEL_WINDOW_PROGRESS_NORMAL        = 1u << 1,
    MEL_WINDOW_PROGRESS_PAUSED        = 1u << 2,
    MEL_WINDOW_PROGRESS_ERROR         = 1u << 3,
};

enum
{
    MEL_WINDOW_FLASH_CANCEL      = 0u,
    MEL_WINDOW_FLASH_BRIEF       = 1u << 0,
    MEL_WINDOW_FLASH_UNTIL_FOCUS = 1u << 1,
};

enum
{
    MEL_WINDOW_HIT_NORMAL        = 0u,
    MEL_WINDOW_HIT_DRAGGABLE     = 1u << 0,
    MEL_WINDOW_HIT_RESIZE_TOP    = 1u << 1,
    MEL_WINDOW_HIT_RESIZE_BOTTOM = 1u << 2,
    MEL_WINDOW_HIT_RESIZE_LEFT   = 1u << 3,
    MEL_WINDOW_HIT_RESIZE_RIGHT  = 1u << 4,
};

enum
{
    MEL_WINDOW_PIXEL_UNKNOWN       = 0u,
    MEL_WINDOW_PIXEL_BGRA8         = 1u << 0,
    MEL_WINDOW_PIXEL_RGBA8         = 1u << 1,
    MEL_WINDOW_PIXEL_RGB10A2       = 1u << 2,
    MEL_WINDOW_PIXEL_RGBA16F       = 1u << 3,
    MEL_WINDOW_PIXEL_PREMULTIPLIED = 1u << 8,
    MEL_WINDOW_PIXEL_SRGB          = 1u << 9,
};

typedef struct
{
    i32 x, y, w, h;
} Mel_Window_Rect;

typedef struct
{
    u32 width_px, height_px;
    u32 refresh_mhz;
    u32 format_flags;
} Mel_Window_Video_Mode;

typedef u32 (*Mel_Window_Hit_Test)(Mel_Window w, i32 x, i32 y, void* user);

typedef struct
{
    u32             flags;
    Mel_Window_Rect bounds_px;
    Mel_Window_Rect safe_area_px;
    f32             opacity;
    f32             scale;
    u32             pixel_format_flags;
    u32             progress_state;
    f32             progress_value;
} Mel_Window_State_Snapshot;

typedef struct
{
    const u8* data;
    usize     size;
} Mel_Window_Icc_Profile;

typedef struct
{
    Mel_Window_State_Snapshot value;
    Mel_Window_Status         status;
} Mel_Window_State_Result;

typedef struct
{
    Mel_Window_Video_Mode value;
    Mel_Window_Status     status;
} Mel_Window_Video_Mode_Result;

typedef struct
{
    void* pixels;
    i32   width_px, height_px;
    i32   stride_bytes;
    u32   format_flags;
} Mel_Window_Surface;

typedef struct
{
    Mel_Window_Surface value;
    Mel_Window_Status  status;
} Mel_Window_Surface_Result;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Window_Op;

#define MEL_WINDOW_OP_NULL ((Mel_Window_Op){ 0, 0 })

static inline bool mel_window_op_valid(Mel_Window_Op op) { return op.index != 0 || op.generation != 0; }

typedef struct
{
    Mel_Window_Icc_Profile value;
    Mel_Window_Status      status;
} Mel_Window_Icc_Result;

Mel_Window_Status mel_window_set_min_size(Mel_Window w, i32 min_w, i32 min_h);
Mel_Window_Status mel_window_set_max_size(Mel_Window w, i32 max_w, i32 max_h);
Mel_Window_Status mel_window_set_aspect_ratio(Mel_Window w, f32 min_ratio, f32 max_ratio);

Mel_Window_Status            mel_window_set_fullscreen(Mel_Window w, u32 fullscreen_flags);
Mel_Window_Status            mel_window_set_fullscreen_mode(Mel_Window w, Mel_Window_Video_Mode mode);
Mel_Window_Video_Mode_Result mel_window_get_fullscreen_mode(Mel_Window w);

Mel_Window_Status mel_window_set_opacity(Mel_Window w, f32 opacity);
f32               mel_window_get_opacity(Mel_Window w);

Mel_Window_Status mel_window_set_always_on_top(Mel_Window w, bool on);
Mel_Window_Status mel_window_set_borderless(Mel_Window w, bool borderless);
Mel_Window_Status mel_window_set_resizable(Mel_Window w, bool resizable);

Mel_Window_Status mel_window_set_icon(Mel_Window w, const u8* rgba, i32 width, i32 height);

Mel_Window_Status mel_window_set_modal(Mel_Window w, bool modal);
Mel_Window_Status mel_window_set_parent(Mel_Window w, Mel_Window parent);
Mel_Window        mel_window_get_parent(Mel_Window w);

Mel_Window_Status mel_window_set_hit_test(Mel_Window w, Mel_Window_Hit_Test cb, void* user);
Mel_Window_Status mel_window_set_shape(Mel_Window w, const u8* alpha_mask, i32 width, i32 height);

Mel_Window_Status mel_window_set_mouse_grab(Mel_Window w, bool grab);
Mel_Window_Status mel_window_set_keyboard_grab(Mel_Window w, bool grab);
Mel_Window_Status mel_window_set_mouse_rect(Mel_Window w, Mel_Window_Rect rect_px);

Mel_Window_Status mel_window_set_progress_state(Mel_Window w, u32 progress_state);
Mel_Window_Status mel_window_set_progress_value(Mel_Window w, f32 value);

Mel_Window_State_Result mel_window_query_state(Mel_Window w);
Mel_Window_Rect         mel_window_safe_area(Mel_Window w);

void mel_window_get_position(Mel_Window w, i32* out_x, i32* out_y);
u32  mel_window_pixel_format(Mel_Window w);

Mel_Window_Status mel_window_maximize(Mel_Window w);
Mel_Window_Status mel_window_minimize(Mel_Window w);
Mel_Window_Status mel_window_restore(Mel_Window w);
Mel_Window_Status mel_window_raise(Mel_Window w);
Mel_Window_Status mel_window_flash(Mel_Window w, u32 flash_flags);

Mel_Window mel_window_by_id(u64 native_id);
u32        mel_window_enumerate_all(Mel_Window* out, u32 cap);

Mel_Window_Surface_Result mel_window_get_surface(Mel_Window w);
Mel_Window_Status         mel_window_present_surface(Mel_Window w);

typedef struct
{
    Mel_Reactor*   reactor;
    Mel_Executor*  deliver;
    Mel_Window_Op* out_op;
} Mel_Window_Icc_Opt;

Mel_Window_Icc_Result mel_window_icc_profile(Mel_Window w);

Mel_Future* mel_window_fetch_icc_opt(Mel_Window w, Mel_Window_Icc_Opt opt);
#define mel_window_fetch_icc(w, ...) mel_window_fetch_icc_opt((w), (Mel_Window_Icc_Opt){ __VA_ARGS__ })

const Mel_Window_Icc_Result* mel_window_icc_future_result(Mel_Future* f);
void                         mel_window_icc_future_release(Mel_Future* f);

bool mel_window_cancel(Mel_Window_Op op);

#ifdef __cplusplus
}
#endif
