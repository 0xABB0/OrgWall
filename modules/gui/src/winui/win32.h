#pragma once

#include "../gui_internal.h"

#include <platform/win32/win32_globals.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#define MEL_REFLECT(m) ((UINT)(WM_APP + (m)))

struct Mel_Painter { HDC dc; i32 w, h; };

/* Per-HWND state. A pointer to one of these lives in GWLP_USERDATA — this is
 * where a control's callbacks live (the win32 analogue of "inside the View").
 * Every control struct starts with Mel_Win32_Ctl so the shared focus/key
 * dispatch can read the handle + focus + keyboard slots generically. */
typedef struct {
    Mel_Gui_Handle      handle;
    Mel_Gui_Focus_Cb    focus;
    Mel_Gui_Keyboard_Cb keyboard;
} Mel_Win32_Ctl;

typedef struct { Mel_Win32_Ctl base; Mel_Gui_Pointer_Cb pointer; }                    Mel_Win32_Button;
typedef struct { Mel_Win32_Ctl base; Mel_CheckBox_On    on_;     }                    Mel_Win32_CheckBox;
typedef struct { Mel_Win32_Ctl base; Mel_Slider_On      on_;     }                    Mel_Win32_Slider;
typedef struct { Mel_Win32_Ctl base; Mel_TextField_On   on_;     }                    Mel_Win32_TextField;
typedef struct { Mel_Win32_Ctl base; Mel_Gui_Pointer_Cb pointer; Mel_Canvas_On on_; } Mel_Win32_Canvas;
typedef struct { Mel_Win32_Ctl base; Mel_Gui_Pointer_Cb pointer; }                    Mel_Win32_Panel;
typedef struct {
    Mel_Win32_Ctl base;
    HWND          inner;
    i32           content_w, content_h;
    i32           pos_x, pos_y;
} Mel_Win32_Scroll;

typedef struct {
    Mel_Win32_Ctl        base;
    Mel_Gui_Lifecycle_Cb lifecycle;
    DWORD                style, ex_style;
    BOOL                 has_menu;
    i32                  min_w, min_h, max_w, max_h;
    u8                   initial_state;
    bool                 first_show_done;
} Mel_Win32_Frame;

/* The dialog ctl embeds a frame as its first member so the toplevel branch of
 * mel_gui_set_bounds (which casts to Mel_Win32_Frame) reads valid style/menu. */
typedef struct {
    Mel_Win32_Frame frame;
    Mel_Dialog_On   on_;
    Mel_Gui_Handle  owner;
    i32             result;
    bool            result_set;
} Mel_Win32_Dialog;

#define MEL_TABVIEW_MAX_PAGES 32
typedef struct {
    Mel_Win32_Ctl base;
    void (*on_select)(Mel_Gui_Handle h, i32 index, void* user);
    HWND          tabctl;
    HWND          pages[MEL_TABVIEW_MAX_PAGES];
    i32           page_count;
    i32           selected;
} Mel_Win32_TabView;

#define MEL_SPLITTER_MAX_PANES 16
typedef struct {
    Mel_Win32_Ctl base;
    bool          vertical;
    HWND          panes[MEL_SPLITTER_MAX_PANES];
    i32           sizes[MEL_SPLITTER_MAX_PANES];
    i32           mins[MEL_SPLITTER_MAX_PANES];
    i32           pane_count;
    i32           drag_index;
    bool          dragging;
} Mel_Win32_Splitter;

int            mel_gui__win32_widen (str8 s, wchar_t* buf, int cap);
size           mel_gui__win32_narrow(const wchar_t* w, int wlen, char* buf, size cap);

Mel_Win32_Ctl* mel_gui__win32_ctl   (HWND hwnd);
Mel_Gui_Handle mel_gui__win32_handle_of(HWND hwnd);
void*          mel_gui__win32_alloc_ctl(HWND hwnd, usize size, Mel_Gui_Handle h);
void           mel_gui__win32_free_ctl (HWND hwnd);

HWND           mel_gui__win32_parent_hwnd (Mel_Gui_Node* n);
DWORD          mel_gui__win32_child_style (Mel_Gui_Node* n, bool disabled);
bool           mel_gui__win32_subclass_common(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

void           mel_gui__win32_ensure_container_class(void);
HWND           mel_gui__win32_make_container(HWND parent, i32 x, i32 y, i32 w, i32 h,
                                             Mel_Gui_Handle handle, Mel_Gui_Pointer_Cb pointer,
                                             Mel_Gui_Focus_Cb focus, Mel_Gui_Keyboard_Cb keyboard,
                                             bool hidden, bool disabled);
