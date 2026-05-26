#pragma once

#include "../gui_internal.h"

#include <emscripten.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The web backend mirrors the win32 one: every widget is a real native object —
 * here a DOM element — created through EM_JS, with the element's integer id (an
 * index into a JS-side registry) stored in node->native. Container widgets put
 * their content host id in node->content so children parent into the right box.
 * Per-element callback state lives in a C-side Mel_Web_Ctl indexed by id; DOM
 * event listeners call back into the exported mel_web__ev_* dispatchers, which
 * fire the stored capability callbacks. The painter wraps a <canvas> id and each
 * op draws straight onto its 2D context. */

struct Mel_Painter { int canvas; i32 w, h; };

typedef enum {
    MEL_WEB_TEXT = 0, // text via textContent (label, button, groupbox legend)
    MEL_WEB_INPUT,    // text via .value (textfield)
    MEL_WEB_FRAME,    // text via document.title
} Mel_Web_Kind;

typedef struct {
    bool                used;
    Mel_Gui_Handle      handle;
    Mel_Web_Kind        kind;
    Mel_Gui_Pointer_Cb  pointer;
    Mel_Gui_Focus_Cb    focus;
    Mel_Gui_Keyboard_Cb keyboard;
    Mel_CheckBox_On     checkbox;
    Mel_Slider_On       slider;
    Mel_TextField_On    textfield;
    Mel_Canvas_On       canvas;
    // on_select doubles for tabview selection and dialog result (same shape).
    void              (*on_select)(Mel_Gui_Handle h, i32 index, void* user);
    int                 aux0, aux1, aux2; // widget scratch (tabview bar/count/selected)
} Mel_Web_Ctl;

/* --- registry / lookup (backend.c) --- */
int          mel_web__id_of(Mel_Gui_Node* n);
int          mel_web__parent_id(Mel_Gui_Node* n);
Mel_Web_Ctl* mel_web__ctl(int id);
Mel_Web_Ctl* mel_web__ctl_new(int id, Mel_Gui_Handle h);

static inline const char* mel_web__cstr(str8 s, char* buf, int cap) {
    int n = (s.data && s.len > 0) ? (int)s.len : 0;
    if (n > cap - 1) n = cap - 1;
    if (n > 0) memcpy(buf, s.data, (size_t)n);
    buf[n] = 0;
    return buf;
}

static inline u32 mel_web__rgba(Mel_Color c) {
    return ((u32)c.r << 24) | ((u32)c.g << 16) | ((u32)c.b << 8) | (u32)c.a;
}

/* --- DOM element ops (EM_JS, backend.c) --- */
int  mel_web__el_create(const char* tag);
void mel_web__el_append(int parent, int child); // parent 0 -> #mel-root
void mel_web__el_class(int id, const char* cls);
void mel_web__el_bounds(int id, i32 x, i32 y, i32 w, i32 h);
void mel_web__el_text(int id, const char* s);
int  mel_web__el_get_text(int id, char* buf, int cap);
void mel_web__el_set_value(int id, const char* s);
int  mel_web__el_get_value(int id, char* buf, int cap);
void mel_web__el_title(const char* s);
void mel_web__el_visible(int id, int visible);
void mel_web__el_enabled(int id, int enabled);
void mel_web__el_focus(int id);
void mel_web__el_destroy(int id);

/* event-listener wiring */
void mel_web__on_click(int id);
void mel_web__on_input(int id);
void mel_web__on_check(int id);
void mel_web__on_slider(int id);
void mel_web__on_focus(int id);
void mel_web__on_pointer(int id);
void mel_web__on_key(int id);

/* widget-specific */
void mel_web__slider_setup(int id, i32 lo, i32 hi, i32 value);
void mel_web__slider_set(int id, i32 value);
i32  mel_web__slider_value(int id);
void mel_web__checkbox_set(int id, int checked);
int  mel_web__checkbox_get(int id);
void mel_web__checkbox_label(int id, const char* s);

/* canvas painter (canvas.c) */
void mel_web__canvas_size(int id, i32 w, i32 h);
void mel_web__paint_clear(int id, u32 rgba);
void mel_web__paint_fill_rect(int id, f32 x, f32 y, f32 w, f32 h, u32 rgba);
void mel_web__paint_fill_ellipse(int id, f32 x, f32 y, f32 w, f32 h, u32 rgba);
void mel_web__paint_stroke_rect(int id, f32 x, f32 y, f32 w, f32 h, u32 rgba, f32 width);
void mel_web__paint_line(int id, f32 ax, f32 ay, f32 bx, f32 by, u32 rgba, f32 width);
void mel_web__paint_round_rect(int id, f32 x, f32 y, f32 w, f32 h, f32 radius, u32 rgba);
void mel_web__paint_text(int id, const char* s, f32 x, f32 y, u32 rgba, f32 size);

void mel_web__canvas_repaint(Mel_Gui_Node* n);
