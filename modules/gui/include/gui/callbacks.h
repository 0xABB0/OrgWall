#pragma once

#include <core/types.h>

#include "handle.h"
#include "input.h"

typedef struct {
    void (*on_show)   (Mel_Gui_Handle h, void* user);
    void (*on_hide)   (Mel_Gui_Handle h, void* user);
    void (*on_enable) (Mel_Gui_Handle h, bool enabled, void* user);
    void (*on_destroy)(Mel_Gui_Handle h, void* user);
    void (*on_resize) (Mel_Gui_Handle h, i32 w, i32 h_, void* user);
    bool (*on_close)  (Mel_Gui_Handle h, void* user);
} Mel_Gui_Lifecycle_Cb;

typedef struct {
    void (*on_focus_in) (Mel_Gui_Handle h, void* user);
    void (*on_focus_out)(Mel_Gui_Handle h, void* user);
} Mel_Gui_Focus_Cb;

typedef struct {
    void (*on_pointer_down) (Mel_Gui_Handle h, i32 x, i32 y, void* user);
    void (*on_pointer_up)   (Mel_Gui_Handle h, i32 x, i32 y, void* user);
    void (*on_pointer_move) (Mel_Gui_Handle h, i32 x, i32 y, void* user);
    void (*on_pointer_enter)(Mel_Gui_Handle h, void* user);
    void (*on_pointer_leave)(Mel_Gui_Handle h, void* user);
    void (*on_click)        (Mel_Gui_Handle h, void* user);
    void (*on_double_click) (Mel_Gui_Handle h, void* user);
} Mel_Gui_Pointer_Cb;

typedef struct {
    void (*on_key_down)(Mel_Gui_Handle h, Mel_Key key, void* user);
    void (*on_key_up)  (Mel_Gui_Handle h, Mel_Key key, void* user);
    void (*on_char)    (Mel_Gui_Handle h, u32 codepoint, void* user);
} Mel_Gui_Keyboard_Cb;

void mel_gui_set_lifecycle_cb(Mel_Gui_Handle h, const Mel_Gui_Lifecycle_Cb* cb);
void mel_gui_set_focus_cb    (Mel_Gui_Handle h, const Mel_Gui_Focus_Cb* cb);
void mel_gui_set_pointer_cb  (Mel_Gui_Handle h, const Mel_Gui_Pointer_Cb* cb);
void mel_gui_set_keyboard_cb (Mel_Gui_Handle h, const Mel_Gui_Keyboard_Cb* cb);
