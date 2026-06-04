#pragma once

#include <input/input.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    Mel_Input_Device device;
    f32              x, y;
    f32              global_x, global_y;
    u32              buttons;
    bool             relative;
    bool             captured;
} Mel_Mouse_State;

Mel_Mouse_State mel_mouse_state(Mel_Input_Device d);

Mel_Input_Status mel_mouse_set_relative(Mel_Input_Device d, bool enable);
bool             mel_mouse_relative(Mel_Input_Device d);

Mel_Input_Status mel_mouse_capture(bool enable);
bool             mel_mouse_captured(void);

Mel_Input_Status mel_mouse_warp(f32 x, f32 y);
Mel_Input_Status mel_mouse_warp_global(f32 x, f32 y);

typedef struct
{
    i32 x, y, w, h;
} Mel_Mouse_Rect;

Mel_Input_Status mel_mouse_confine(Mel_Mouse_Rect rect);
Mel_Input_Status mel_mouse_unconfine(void);

typedef enum
{
    MEL_CURSOR_DEFAULT = 0,
    MEL_CURSOR_ARROW,
    MEL_CURSOR_IBEAM,
    MEL_CURSOR_WAIT,
    MEL_CURSOR_CROSSHAIR,
    MEL_CURSOR_WAIT_ARROW,
    MEL_CURSOR_RESIZE_NWSE,
    MEL_CURSOR_RESIZE_NESW,
    MEL_CURSOR_RESIZE_WE,
    MEL_CURSOR_RESIZE_NS,
    MEL_CURSOR_MOVE,
    MEL_CURSOR_NOT_ALLOWED,
    MEL_CURSOR_POINTER,
    MEL_CURSOR_HIDDEN,
} Mel_Cursor_Shape;

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Cursor;

#define MEL_CURSOR_NULL ((Mel_Cursor){ 0 })

typedef struct
{
    const u8* rgba;
    u32       width, height;
    f32       scale;
    u32       duration_ms;
} Mel_Cursor_Frame;

typedef struct
{
    const Mel_Cursor_Frame* frames;
    u32                     frame_count;
    f32                     hotspot_x, hotspot_y;
    f32                     accel_gain;
    f32                     accel_exponent;
    bool                    has_acceleration;
} Mel_Cursor_Opt;

Mel_Cursor mel_cursor_create_system(Mel_Cursor_Shape shape);

Mel_Cursor mel_cursor_create_opt(const Mel_Alloc* alloc, Mel_Cursor_Opt opt);
#define mel_cursor_create(alloc, ...) mel_cursor_create_opt((alloc), (Mel_Cursor_Opt){ __VA_ARGS__ })

void mel_cursor_destroy(Mel_Cursor c);

Mel_Input_Status mel_cursor_set(Mel_Cursor c);
Mel_Input_Status mel_cursor_show(bool visible);
bool             mel_cursor_visible(void);

#ifdef __cplusplus
}
#endif
