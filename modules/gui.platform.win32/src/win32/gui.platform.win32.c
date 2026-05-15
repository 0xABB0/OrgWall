#include <gui.platform.win32/gui.platform.win32.h>
#include <gui.platform/gui.platform.h>
#include <gui/gui.h>

#include <allocator/allocator.h>
#include <allocator.heap/heap.h>
#include <collection.array/array.h>
#include <string/string.str8.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>

#define MEL_WIN32_MAIN_CLASS L"MelodyMainWindow"

typedef struct {
    Mel_Atom                  atom;
    Mel_Gui_Win32_Construct   cb;
} Mel_Win32_Ctor;

static HINSTANCE                 mel__w_hinstance;
static HWND                      mel__w_root;
static Mel_Array(Mel_Win32_Ctor) mel__w_ctors;
static bool                      mel__w_ctors_inited;
static bool                      mel__w_exit_requested;
static str8                      mel__w_pending_activity;

static u64 mel__w_pack_handle(Mel_Gui_Handle h)
{
    return ((u64)h.handle.generation << 32) | (u64)h.handle.index;
}

static Mel_Gui_Handle mel__w_unpack_handle(u64 v)
{
    return (Mel_Gui_Handle){ .handle = { .index = (u32)(v & 0xFFFFFFFFu), .generation = (u32)(v >> 32) } };
}

static Mel_Gui_Handle mel__w_handle_from_hwnd(HWND hwnd)
{
    LONG_PTR raw = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    return mel__w_unpack_handle((u64)raw);
}

static void mel__w_bind_handle(HWND hwnd, Mel_Gui_Handle h)
{
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)mel__w_pack_handle(h));
}

static void mel__w_ensure_ctors(void)
{
    if (!mel__w_ctors_inited) {
        mel_array_init(&mel__w_ctors, mel_alloc_heap());
        mel__w_ctors_inited = true;
    }
}

bool mel_gui_win32_register_constructor(Mel_Atom atom, Mel_Gui_Win32_Construct cb)
{
    if (atom == MEL_ATOM_NONE || cb == NULL) return false;
    mel__w_ensure_ctors();
    for (usize i = 0; i < mel__w_ctors.count; i++) {
        if (mel__w_ctors.items[i].atom == atom) {
            mel__w_ctors.items[i].cb = cb;
            return true;
        }
    }
    mel_array_push(&mel__w_ctors, ((Mel_Win32_Ctor){ .atom = atom, .cb = cb }));
    return true;
}

static Mel_Gui_Win32_Construct mel__w_lookup_ctor(Mel_Atom atom)
{
    if (!mel__w_ctors_inited) return NULL;
    for (usize i = 0; i < mel__w_ctors.count; i++) {
        if (mel__w_ctors.items[i].atom == atom) return mel__w_ctors.items[i].cb;
    }
    return NULL;
}

HWND      mel_gui_win32_root(void)      { return mel__w_root; }
HINSTANCE mel_gui_win32_hinstance(void) { return mel__w_hinstance; }

wchar_t* mel_gui_win32_str8_to_wide(str8 s)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    if (s.data == NULL || s.len <= 0) {
        wchar_t* empty = mel_alloc(alloc, sizeof(wchar_t));
        if (empty) empty[0] = 0;
        return empty;
    }
    int needed = MultiByteToWideChar(CP_UTF8, 0, (const char*)s.data, (int)s.len, NULL, 0);
    if (needed < 0) needed = 0;
    wchar_t* out = mel_alloc(alloc, sizeof(wchar_t) * (usize)(needed + 1));
    if (out == NULL) return NULL;
    if (needed > 0) MultiByteToWideChar(CP_UTF8, 0, (const char*)s.data, (int)s.len, out, needed);
    out[needed] = 0;
    return out;
}

void mel_gui_win32_wide_free(wchar_t* w)
{
    if (w != NULL) mel_dealloc(mel_alloc_heap(), w);
}

str8 mel_gui_win32_wide_to_str8(const wchar_t* w, int wlen)
{
    if (w == NULL || wlen <= 0) return STR8_EMPTY;
    int needed = WideCharToMultiByte(CP_UTF8, 0, w, wlen, NULL, 0, NULL, NULL);
    if (needed <= 0) return STR8_EMPTY;
    const Mel_Alloc* alloc = mel_alloc_heap();
    u8* buf = mel_alloc(alloc, (usize)needed);
    if (buf == NULL) return STR8_EMPTY;
    WideCharToMultiByte(CP_UTF8, 0, w, wlen, (char*)buf, needed, NULL, NULL);
    return (str8){ .data = buf, .len = (size)needed };
}

void mel_gui_win32_str8_free(str8 s)
{
    if (s.data != NULL) mel_dealloc(mel_alloc_heap(), s.data);
}

static void mel__w_dispatch_command(WPARAM wp, LPARAM lp)
{
    HWND child = (HWND)lp;
    if (child == NULL) return;
    Mel_Gui_Handle h = mel__w_handle_from_hwnd(child);
    if (mel_gui_handle_is_none(h)) return;

    WORD notify = HIWORD(wp);
    wchar_t cls[64];
    GetClassNameW(child, cls, 64);

    if (lstrcmpiW(cls, L"BUTTON") == 0) {
        if (notify == BN_CLICKED) {
            LONG style = GetWindowLongW(child, GWL_STYLE);
            if ((style & BS_TYPEMASK) == BS_AUTOCHECKBOX || (style & BS_TYPEMASK) == BS_CHECKBOX) {
                LRESULT st = SendMessageW(child, BM_GETCHECK, 0, 0);
                mel_gui_send_message(h, MEL_GUI_MSG_VALUE_CHANGED, (Mel_Gui_WParam)(st == BST_CHECKED ? 1 : 0), 0);
            } else {
                mel_gui_send_message(h, MEL_GUI_MSG_CLICK, 0, 0);
            }
        }
        return;
    }
    if (lstrcmpiW(cls, L"EDIT") == 0) {
        if (notify == EN_CHANGE) {
            int wlen = GetWindowTextLengthW(child);
            wchar_t* wbuf = mel_alloc(mel_alloc_heap(), sizeof(wchar_t) * (usize)(wlen + 1));
            if (wbuf == NULL) return;
            GetWindowTextW(child, wbuf, wlen + 1);
            str8 text = mel_gui_win32_wide_to_str8(wbuf, wlen);
            mel_dealloc(mel_alloc_heap(), wbuf);
            mel_gui_send_message(h, MEL_GUI_MSG_TEXT_CHANGED,
                (Mel_Gui_WParam)(usize)text.len, (Mel_Gui_LParam)(intptr_t)text.data);
            mel_gui_win32_str8_free(text);
        }
        return;
    }
}

static void mel__w_dispatch_scroll(HWND child)
{
    if (child == NULL) return;
    Mel_Gui_Handle h = mel__w_handle_from_hwnd(child);
    if (mel_gui_handle_is_none(h)) return;
    LRESULT pos = SendMessageW(child, TBM_GETPOS, 0, 0);
    mel_gui_send_message(h, MEL_GUI_MSG_VALUE_CHANGED, (Mel_Gui_WParam)(i64)pos, 0);
}

static LRESULT CALLBACK mel__w_root_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_COMMAND:
            if (lp != 0) mel__w_dispatch_command(wp, lp);
            return 0;
        case WM_HSCROLL:
        case WM_VSCROLL:
            if (lp != 0) mel__w_dispatch_scroll((HWND)lp);
            return 0;
        case WM_CLOSE: {
            Mel_Gui_Handle h = mel__w_handle_from_hwnd(hwnd);
            if (!mel_gui_handle_is_none(h)) {
                mel_gui_send_message(h, MEL_GUI_MSG_CLOSE, 0, 0);
            } else {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void mel__w_install_default_font(HWND hwnd)
{
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (font != NULL) SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, MAKELPARAM(TRUE, 0));
}

static BOOL CALLBACK mel__w_set_font_proc(HWND child, LPARAM lp)
{
    SendMessageW(child, WM_SETFONT, (WPARAM)lp, MAKELPARAM(TRUE, 0));
    return TRUE;
}

void mel_gui_win32_run(void (*setup)(void))
{
    mel__w_hinstance = GetModuleHandleW(NULL);

    INITCOMMONCONTROLSEX icc = { .dwSize = sizeof(icc), .dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {
        .cbSize        = sizeof(wc),
        .style         = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc   = mel__w_root_proc,
        .hInstance     = mel__w_hinstance,
        .hCursor       = LoadCursor(NULL, IDC_ARROW),
        .hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1),
        .lpszClassName = MEL_WIN32_MAIN_CLASS,
    };
    RegisterClassExW(&wc);

    mel__w_root = CreateWindowExW(
        0,
        MEL_WIN32_MAIN_CLASS, L"Melody",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 480, 720,
        NULL, NULL, mel__w_hinstance, NULL);
    if (mel__w_root == NULL) return;

    ShowWindow(mel__w_root, SW_SHOW);
    UpdateWindow(mel__w_root);

    if (setup != NULL) setup();
    mel_gui_dispatch_app_message(MEL_GUI_MSG_APP_CREATE, 0, 0);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (mel__w_pending_activity.data != NULL) {
            str8 owned = mel__w_pending_activity;
            mel__w_pending_activity = STR8_EMPTY;
            mel_gui_destroy_all_roots();
            mel_gui_app_build_activity(owned);
            mel_gui_win32_str8_free(owned);
        }
        if (!IsDialogMessageW(mel__w_root, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (mel__w_exit_requested) {
            mel__w_exit_requested = false;
            DestroyWindow(mel__w_root);
        }
    }

    mel_gui_dispatch_app_message(MEL_GUI_MSG_APP_DESTROY, 0, 0);
    mel_gui_destroy_all_roots();
    mel_gui_shutdown();
}

bool mel_gui_platform_init(void)     { return true; }
void mel_gui_platform_shutdown(void) {}

void* mel_gui_platform_create(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc, Mel_Atom platform_class)
{
    Mel_Gui_Win32_Construct cb = mel__w_lookup_ctor(platform_class);
    if (cb == NULL) return NULL;

    HWND parent_hwnd = mel__w_root;
    if (!mel_gui_handle_is_none(desc->parent)) {
        void* p = mel_gui_platform_native(desc->parent);
        if (p != NULL) parent_hwnd = (HWND)p;
    }

    HWND child = cb(h, desc, parent_hwnd);
    if (child == NULL) return NULL;

    mel__w_bind_handle(child, h);
    mel__w_install_default_font(child);

    if (desc->x != MEL_GUI_DEFAULT_POSITION && child != mel__w_root) {
        SetWindowPos(child, NULL, desc->x, desc->y, desc->w, desc->h, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (child != mel__w_root) ShowWindow(child, SW_SHOW);

    return (void*)child;
}

void mel_gui_platform_destroy(Mel_Gui_Handle h)
{
    void* native = mel_gui_platform_native(h);
    if (native == NULL) return;
    HWND hwnd = (HWND)native;
    if (hwnd != mel__w_root) {
        DestroyWindow(hwnd);
    } else {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }
    mel_gui_platform_bind_native(h, NULL);
}

bool mel_gui_platform_set_window_pos(Mel_Gui_Handle h, i32 x, i32 y, i32 w, i32 hgt, u32 flags)
{
    void* native = mel_gui_platform_native(h);
    if (native == NULL) return false;
    HWND hwnd = (HWND)native;

    UINT swp = SWP_NOZORDER | SWP_NOACTIVATE;
    if (flags & MEL_GUI_SWP_NOMOVE) swp |= SWP_NOMOVE;
    if (flags & MEL_GUI_SWP_NOSIZE) swp |= SWP_NOSIZE;
    if ((swp & (SWP_NOMOVE | SWP_NOSIZE)) != (SWP_NOMOVE | SWP_NOSIZE)) {
        SetWindowPos(hwnd, NULL, x, y, w, hgt, swp);
    }
    if (flags & MEL_GUI_SWP_SHOW)    ShowWindow(hwnd, SW_SHOW);
    if (flags & MEL_GUI_SWP_HIDE)    ShowWindow(hwnd, SW_HIDE);
    if (flags & MEL_GUI_SWP_ENABLE)  EnableWindow(hwnd, TRUE);
    if (flags & MEL_GUI_SWP_DISABLE) EnableWindow(hwnd, FALSE);
    return true;
}

bool mel_gui_platform_set_text(Mel_Gui_Handle h, str8 text)
{
    void* native = mel_gui_platform_native(h);
    if (native == NULL) return false;
    HWND hwnd = (HWND)native;
    wchar_t* w = mel_gui_win32_str8_to_wide(text);
    if (w == NULL) return false;
    SetWindowTextW(hwnd, w);
    mel_gui_win32_wide_free(w);
    return true;
}

#define MEL_WIN32_POST_MSG (WM_APP + 0x4711)

bool mel_gui_platform_post_message(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    if (mel__w_root == NULL) return false;
    typedef struct { Mel_Gui_Handle h; Mel_Gui_Msg msg; Mel_Gui_WParam w; Mel_Gui_LParam l; } Mel_Win32_Post;
    Mel_Win32_Post* p = mel_alloc(mel_alloc_heap(), sizeof(*p));
    if (p == NULL) return false;
    *p = (Mel_Win32_Post){ .h = h, .msg = msg, .w = w, .l = l };
    return PostMessageW(mel__w_root, MEL_WIN32_POST_MSG, 0, (LPARAM)p) != 0;
}

void mel_gui_platform_request_exit(void)
{
    mel__w_exit_requested = true;
    if (mel__w_root != NULL) PostMessageW(mel__w_root, WM_NULL, 0, 0);
}

bool mel_gui_app_start_activity(str8 activity_name)
{
    if (mel__w_root == NULL) return false;
    const Mel_Alloc* alloc = mel_alloc_heap();
    u8* buf = mel_alloc(alloc, (usize)activity_name.len);
    if (buf == NULL) return false;
    memcpy(buf, activity_name.data, (usize)activity_name.len);
    mel__w_pending_activity = (str8){ .data = buf, .len = activity_name.len };
    PostMessageW(mel__w_root, WM_NULL, 0, 0);
    return true;
}
