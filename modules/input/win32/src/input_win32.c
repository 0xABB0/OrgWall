#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <imm.h>

#include <input/provider.h>
#include <input/win32/win32.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/slotmap.h>
#include <log/log.h>

#include "../../src/input_internal.h"

#define MEL_WIN32_KEYBOARD_ID 0x77696E6B6264ULL
#define MEL_WIN32_MOUSE_ID    0x77696E6D7365ULL
#define MEL_WIN32_PEN_ID      0x77696E70656EULL
#define MEL_WIN32_TOUCH_ID    0x77696E746368ULL

typedef struct
{
    HCURSOR cursor;
    bool    owned;
} Cursor_Slot;

static struct
{
    bool        init;
    HWND        hwnd;
    Mel_SlotMap cursors;
    f32         mouse_x, mouse_y;
    u32         buttons;
    bool        relative;
    bool        captured;
    bool        text_active;
    u32         pressed[(MEL_SCANCODE_COUNT + 31) / 32];
} g_win;

static void win_ensure(void)
{
    if (g_win.init)
        return;
    mel_slotmap_init(&g_win.cursors, mel_alloc_heap(), .item_size = sizeof(Cursor_Slot), .initial_capacity = 4);
    g_win.init = true;
}

static void win_set_pressed(Mel_Scancode sc, bool down)
{
    if (sc == MEL_SCANCODE_UNKNOWN || sc >= MEL_SCANCODE_COUNT)
        return;
    u32 w = (u32)sc >> 5, b = 1u << ((u32)sc & 31);
    if (down)
        g_win.pressed[w] |= b;
    else
        g_win.pressed[w] &= ~b;
}

static Mel_Scancode win_scancode_from_vk(WPARAM vk, LPARAM lparam)
{
    UINT scan = (lparam >> 16) & 0xFF;
    bool ext = (lparam & (1 << 24)) != 0;
    switch (vk)
    {
    case 'A':
        return MEL_SCANCODE_A;
    case 'B':
        return MEL_SCANCODE_B;
    case 'C':
        return MEL_SCANCODE_C;
    case 'D':
        return MEL_SCANCODE_D;
    case 'E':
        return MEL_SCANCODE_E;
    case 'F':
        return MEL_SCANCODE_F;
    case 'G':
        return MEL_SCANCODE_G;
    case 'H':
        return MEL_SCANCODE_H;
    case 'I':
        return MEL_SCANCODE_I;
    case 'J':
        return MEL_SCANCODE_J;
    case 'K':
        return MEL_SCANCODE_K;
    case 'L':
        return MEL_SCANCODE_L;
    case 'M':
        return MEL_SCANCODE_M;
    case 'N':
        return MEL_SCANCODE_N;
    case 'O':
        return MEL_SCANCODE_O;
    case 'P':
        return MEL_SCANCODE_P;
    case 'Q':
        return MEL_SCANCODE_Q;
    case 'R':
        return MEL_SCANCODE_R;
    case 'S':
        return MEL_SCANCODE_S;
    case 'T':
        return MEL_SCANCODE_T;
    case 'U':
        return MEL_SCANCODE_U;
    case 'V':
        return MEL_SCANCODE_V;
    case 'W':
        return MEL_SCANCODE_W;
    case 'X':
        return MEL_SCANCODE_X;
    case 'Y':
        return MEL_SCANCODE_Y;
    case 'Z':
        return MEL_SCANCODE_Z;
    case '0':
        return MEL_SCANCODE_0;
    case '1':
        return MEL_SCANCODE_1;
    case '2':
        return MEL_SCANCODE_2;
    case '3':
        return MEL_SCANCODE_3;
    case '4':
        return MEL_SCANCODE_4;
    case '5':
        return MEL_SCANCODE_5;
    case '6':
        return MEL_SCANCODE_6;
    case '7':
        return MEL_SCANCODE_7;
    case '8':
        return MEL_SCANCODE_8;
    case '9':
        return MEL_SCANCODE_9;
    case VK_RETURN:
        return ext ? MEL_SCANCODE_KP_ENTER : MEL_SCANCODE_RETURN;
    case VK_ESCAPE:
        return MEL_SCANCODE_ESCAPE;
    case VK_BACK:
        return MEL_SCANCODE_BACKSPACE;
    case VK_TAB:
        return MEL_SCANCODE_TAB;
    case VK_SPACE:
        return MEL_SCANCODE_SPACE;
    case VK_CAPITAL:
        return MEL_SCANCODE_CAPSLOCK;
    case VK_LSHIFT:
        return MEL_SCANCODE_LSHIFT;
    case VK_RSHIFT:
        return MEL_SCANCODE_RSHIFT;
    case VK_SHIFT:
        return scan == 0x36 ? MEL_SCANCODE_RSHIFT : MEL_SCANCODE_LSHIFT;
    case VK_LCONTROL:
        return MEL_SCANCODE_LCTRL;
    case VK_RCONTROL:
        return MEL_SCANCODE_RCTRL;
    case VK_CONTROL:
        return ext ? MEL_SCANCODE_RCTRL : MEL_SCANCODE_LCTRL;
    case VK_LMENU:
        return MEL_SCANCODE_LALT;
    case VK_RMENU:
        return MEL_SCANCODE_RALT;
    case VK_MENU:
        return ext ? MEL_SCANCODE_RALT : MEL_SCANCODE_LALT;
    case VK_LWIN:
        return MEL_SCANCODE_LGUI;
    case VK_RWIN:
        return MEL_SCANCODE_RGUI;
    case VK_LEFT:
        return MEL_SCANCODE_LEFT;
    case VK_RIGHT:
        return MEL_SCANCODE_RIGHT;
    case VK_UP:
        return MEL_SCANCODE_UP;
    case VK_DOWN:
        return MEL_SCANCODE_DOWN;
    case VK_HOME:
        return MEL_SCANCODE_HOME;
    case VK_END:
        return MEL_SCANCODE_END;
    case VK_PRIOR:
        return MEL_SCANCODE_PAGEUP;
    case VK_NEXT:
        return MEL_SCANCODE_PAGEDOWN;
    case VK_INSERT:
        return MEL_SCANCODE_INSERT;
    case VK_DELETE:
        return MEL_SCANCODE_DELETE;
    case VK_F1:
        return MEL_SCANCODE_F1;
    case VK_F2:
        return MEL_SCANCODE_F2;
    case VK_F3:
        return MEL_SCANCODE_F3;
    case VK_F4:
        return MEL_SCANCODE_F4;
    case VK_F5:
        return MEL_SCANCODE_F5;
    case VK_F6:
        return MEL_SCANCODE_F6;
    case VK_F7:
        return MEL_SCANCODE_F7;
    case VK_F8:
        return MEL_SCANCODE_F8;
    case VK_F9:
        return MEL_SCANCODE_F9;
    case VK_F10:
        return MEL_SCANCODE_F10;
    case VK_F11:
        return MEL_SCANCODE_F11;
    case VK_F12:
        return MEL_SCANCODE_F12;
    default:
        return MEL_SCANCODE_UNKNOWN;
    }
}

static u32 win_modifiers(void)
{
    u32 m = 0;
    if (GetKeyState(VK_LSHIFT) & 0x8000)
        m |= MEL_INPUT_MOD_LSHIFT;
    if (GetKeyState(VK_RSHIFT) & 0x8000)
        m |= MEL_INPUT_MOD_RSHIFT;
    if (GetKeyState(VK_LCONTROL) & 0x8000)
        m |= MEL_INPUT_MOD_LCTRL;
    if (GetKeyState(VK_RCONTROL) & 0x8000)
        m |= MEL_INPUT_MOD_RCTRL;
    if (GetKeyState(VK_LMENU) & 0x8000)
        m |= MEL_INPUT_MOD_LALT;
    if (GetKeyState(VK_RMENU) & 0x8000)
        m |= MEL_INPUT_MOD_RALT;
    if (GetKeyState(VK_LWIN) & 0x8000)
        m |= MEL_INPUT_MOD_LGUI;
    if (GetKeyState(VK_RWIN) & 0x8000)
        m |= MEL_INPUT_MOD_RGUI;
    if (GetKeyState(VK_CAPITAL) & 1)
        m |= MEL_INPUT_MOD_CAPS;
    if (GetKeyState(VK_NUMLOCK) & 1)
        m |= MEL_INPUT_MOD_NUM;
    if (GetKeyState(VK_SCROLL) & 1)
        m |= MEL_INPUT_MOD_SCROLL;
    return m;
}

static u32 win_enumerate(void* user, Mel_Input_Raw* out, u32 cap)
{
    (void)user;
    if (cap < 3)
        return 0;
    u32 n = 0;
    out[n++] = (Mel_Input_Raw){ .stable_id = MEL_WIN32_KEYBOARD_ID, .desc = { .name = S8("Keyboard"), .caps = MEL_INPUT_CAP_KEYBOARD | MEL_INPUT_CAP_TEXT | MEL_INPUT_CAP_IME, .key_count = MEL_SCANCODE_COUNT } };
    out[n++] = (Mel_Input_Raw){ .stable_id = MEL_WIN32_MOUSE_ID,
                                .desc = { .name = S8("Mouse"), .caps = MEL_INPUT_CAP_MOUSE | MEL_INPUT_CAP_RELATIVE | MEL_INPUT_CAP_CAPTURE | MEL_INPUT_CAP_WARP | MEL_INPUT_CAP_CONFINE | MEL_INPUT_CAP_CURSOR, .button_count = 5 } };
    out[n++] = (Mel_Input_Raw){
        .stable_id = MEL_WIN32_PEN_ID,
        .desc = { .name = S8("Pen"), .caps = MEL_INPUT_CAP_PEN | MEL_INPUT_CAP_PRESSURE | MEL_INPUT_CAP_TILT | MEL_INPUT_CAP_HOVER | MEL_INPUT_CAP_ERASER, .pen_button_count = 2, .pressure_max = 1024.0f, .hover_distance_max = 1.0f }
    };
    if (GetSystemMetrics(SM_MAXIMUMTOUCHES) > 0 && cap >= 4)
        out[n++] = (Mel_Input_Raw){ .stable_id = MEL_WIN32_TOUCH_ID,
                                    .desc = { .name = S8("Touchscreen"), .caps = MEL_INPUT_CAP_TOUCH | MEL_INPUT_CAP_PRESSURE, .touch_point_max = (u32)GetSystemMetrics(SM_MAXIMUMTOUCHES), .touch_direct = true, .pressure_max = 1024.0f } };
    return n;
}

static bool win_key_down(void* user, u64 sid, Mel_Scancode sc)
{
    (void)user;
    (void)sid;
    if (sc == MEL_SCANCODE_UNKNOWN || sc >= MEL_SCANCODE_COUNT)
        return false;
    return (g_win.pressed[(u32)sc >> 5] & (1u << ((u32)sc & 31))) != 0;
}

static u32 win_mods(void* user, u64 sid)
{
    (void)user;
    (void)sid;
    return win_modifiers();
}

static Mel_Mouse_State win_mouse_state(void* user, u64 sid)
{
    (void)user;
    (void)sid;
    POINT pt = { 0, 0 };
    GetCursorPos(&pt);
    return (Mel_Mouse_State){ .x = g_win.mouse_x, .y = g_win.mouse_y, .global_x = (f32)pt.x, .global_y = (f32)pt.y, .buttons = g_win.buttons, .relative = g_win.relative, .captured = g_win.captured };
}

static void win_register_rawinput(bool enable)
{
    RAWINPUTDEVICE rid = { .usUsagePage = 0x01, .usUsage = 0x02, .dwFlags = enable ? RIDEV_INPUTSINK : RIDEV_REMOVE, .hwndTarget = enable ? g_win.hwnd : NULL };
    RegisterRawInputDevices(&rid, 1, sizeof rid);
}

static Mel_Input_Status win_set_relative(void* user, u64 sid, bool enable)
{
    (void)user;
    (void)sid;
    g_win.relative = enable;
    win_register_rawinput(enable);
    ShowCursor(enable ? FALSE : TRUE);
    return MEL_INPUT_OK;
}

static Mel_Input_Status win_capture(void* user, bool enable)
{
    (void)user;
    g_win.captured = enable;
    if (enable && g_win.hwnd)
        SetCapture(g_win.hwnd);
    else
        ReleaseCapture();
    return MEL_INPUT_OK;
}

static Mel_Input_Status win_warp(void* user, u64 sid, f32 x, f32 y, bool global)
{
    (void)user;
    (void)sid;
    POINT pt = { (LONG)x, (LONG)y };
    if (!global && g_win.hwnd)
        ClientToScreen(g_win.hwnd, &pt);
    SetCursorPos(pt.x, pt.y);
    return MEL_INPUT_OK;
}

static Mel_Input_Status win_confine(void* user, const Mel_Mouse_Rect* rect)
{
    (void)user;
    if (rect == NULL)
    {
        ClipCursor(NULL);
        return MEL_INPUT_OK;
    }
    RECT r = { rect->x, rect->y, rect->x + rect->w, rect->y + rect->h };
    if (g_win.hwnd)
    {
        POINT tl = { r.left, r.top }, br = { r.right, r.bottom };
        ClientToScreen(g_win.hwnd, &tl);
        ClientToScreen(g_win.hwnd, &br);
        r = (RECT){ tl.x, tl.y, br.x, br.y };
    }
    ClipCursor(&r);
    return MEL_INPUT_OK;
}

static LPCWSTR win_idc(Mel_Cursor_Shape shape)
{
    switch (shape)
    {
    case MEL_CURSOR_IBEAM:
        return IDC_IBEAM;
    case MEL_CURSOR_WAIT:
        return IDC_WAIT;
    case MEL_CURSOR_CROSSHAIR:
        return IDC_CROSS;
    case MEL_CURSOR_WAIT_ARROW:
        return IDC_APPSTARTING;
    case MEL_CURSOR_RESIZE_NWSE:
        return IDC_SIZENWSE;
    case MEL_CURSOR_RESIZE_NESW:
        return IDC_SIZENESW;
    case MEL_CURSOR_RESIZE_WE:
        return IDC_SIZEWE;
    case MEL_CURSOR_RESIZE_NS:
        return IDC_SIZENS;
    case MEL_CURSOR_MOVE:
        return IDC_SIZEALL;
    case MEL_CURSOR_NOT_ALLOWED:
        return IDC_NO;
    case MEL_CURSOR_POINTER:
        return IDC_HAND;
    default:
        return IDC_ARROW;
    }
}

static Mel_Cursor win_cursor_system(void* user, Mel_Cursor_Shape shape)
{
    (void)user;
    win_ensure();
    Cursor_Slot        slot = { .cursor = LoadCursorW(NULL, win_idc(shape)), .owned = false };
    Mel_SlotMap_Handle h = mel_slotmap_insert(&g_win.cursors, &slot);
    return (Mel_Cursor){ h };
}

static Mel_Cursor win_cursor_custom(void* user, const Mel_Cursor_Opt* opt)
{
    (void)user;
    win_ensure();
    if (opt->frame_count == 0 || opt->frames[0].rgba == NULL)
        return MEL_CURSOR_NULL;
    const Mel_Cursor_Frame* f = &opt->frames[0];
    HBITMAP                 color = CreateBitmap((int)f->width, (int)f->height, 1, 32, f->rgba);
    HBITMAP                 mask = CreateBitmap((int)f->width, (int)f->height, 1, 1, NULL);
    ICONINFO                ii = { .fIcon = FALSE, .xHotspot = (DWORD)opt->hotspot_x, .yHotspot = (DWORD)opt->hotspot_y, .hbmMask = mask, .hbmColor = color };
    HCURSOR                 cur = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    if (!cur)
        return MEL_CURSOR_NULL;
    Cursor_Slot        slot = { .cursor = cur, .owned = true };
    Mel_SlotMap_Handle h = mel_slotmap_insert(&g_win.cursors, &slot);
    return (Mel_Cursor){ h };
}

static void win_cursor_destroy(void* user, Mel_Cursor c)
{
    (void)user;
    if (!g_win.init)
        return;
    Cursor_Slot* s = (Cursor_Slot*)mel_slotmap_get(&g_win.cursors, c.h);
    if (s && s->owned && s->cursor)
        DestroyCursor(s->cursor);
    mel_slotmap_remove(&g_win.cursors, c.h);
}

static Mel_Input_Status win_cursor_set(void* user, Mel_Cursor c)
{
    (void)user;
    Cursor_Slot* s = (Cursor_Slot*)mel_slotmap_get(&g_win.cursors, c.h);
    if (!s || !s->cursor)
        return MEL_INPUT_ERROR | MEL_INPUT_INVALID_HANDLE;
    SetCursor(s->cursor);
    return MEL_INPUT_OK;
}

static Mel_Input_Status win_cursor_show(void* user, bool visible)
{
    (void)user;
    ShowCursor(visible ? TRUE : FALSE);
    return MEL_INPUT_OK;
}

static Mel_Input_Status win_text_start(void* user, const Mel_Input_Text_Opt* opt)
{
    (void)user;
    (void)opt;
    g_win.text_active = true;
    if (g_win.hwnd)
    {
        HIMC imc = ImmGetContext(g_win.hwnd);
        if (imc)
        {
            ImmSetOpenStatus(imc, TRUE);
            ImmReleaseContext(g_win.hwnd, imc);
        }
    }
    return MEL_INPUT_OK;
}

static void win_text_stop(void* user)
{
    (void)user;
    g_win.text_active = false;
}

static Mel_Input_Status win_text_set_area(void* user, Mel_Input_Rect area)
{
    (void)user;
    if (!g_win.hwnd)
        return MEL_INPUT_WARNED | MEL_INPUT_AREA_IGNORED;
    HIMC imc = ImmGetContext(g_win.hwnd);
    if (!imc)
        return MEL_INPUT_WARNED | MEL_INPUT_AREA_IGNORED;
    CANDIDATEFORM cf = { .dwIndex = 0, .dwStyle = CFS_CANDIDATEPOS, .ptCurrentPos = { area.x, area.y + area.h } };
    ImmSetCandidateWindow(imc, &cf);
    COMPOSITIONFORM comp = { .dwStyle = CFS_POINT, .ptCurrentPos = { area.x, area.y } };
    ImmSetCompositionWindow(imc, &comp);
    ImmReleaseContext(g_win.hwnd, imc);
    return MEL_INPUT_OK;
}

static Mel_Input_Status win_osk(void* user)
{
    (void)user;
    return MEL_INPUT_WARNED | MEL_INPUT_UNSUPPORTED;
}

static Mel_Input_Provider_Desc g_desc;

void mel_input__register_host_providers(void)
{
    g_desc = (Mel_Input_Provider_Desc){
        .name = "win32-rawinput",
        .enumerate = win_enumerate,
        .key_down = win_key_down,
        .modifiers = win_mods,
        .mouse_state = win_mouse_state,
        .mouse_set_relative = win_set_relative,
        .mouse_capture = win_capture,
        .mouse_warp = win_warp,
        .mouse_confine = win_confine,
        .cursor_create_system = win_cursor_system,
        .cursor_create_custom = win_cursor_custom,
        .cursor_destroy = win_cursor_destroy,
        .cursor_set = win_cursor_set,
        .cursor_show = win_cursor_show,
        .text_start = win_text_start,
        .text_stop = win_text_stop,
        .text_set_area = win_text_set_area,
        .osk_show = win_osk,
        .osk_hide = win_osk,
    };
    mel_input_provider_register(&g_desc);
}

void mel_input_win32_set_hwnd(void* hwnd)
{
    win_ensure();
    g_win.hwnd = (HWND)hwnd;
}

static void win_emit_button(Mel_Input_Sink* sink, u32 mask, bool down)
{
    if (down)
        g_win.buttons |= mask;
    else
        g_win.buttons &= ~mask;
    Mel_Input_Mouse_Event me = { .x = g_win.mouse_x, .y = g_win.mouse_y, .buttons = g_win.buttons, .button_changed = mask, .button_down = down };
    mel_input_sink_mouse(sink, MEL_WIN32_MOUSE_ID, &me);
}

i64 mel_input_win32_wndproc(void* hwnd, u32 msg, u64 wparam, i64 lparam, bool* handled)
{
    Mel_Input_Sink* sink = mel_input__sink();
    if (handled)
        *handled = false;
    if (sink == NULL)
        return 0;
    LPARAM lp = (LPARAM)lparam;
    WPARAM wp = (WPARAM)wparam;
    switch (msg)
    {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
    {
        bool         down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
        Mel_Scancode sc = win_scancode_from_vk(wp, lp);
        win_set_pressed(sc, down);
        Mel_Input_Key_Event ke = { .scancode = sc, .modifiers = win_modifiers(), .down = down, .repeat = down && (lp & (1 << 30)) };
        mel_input_sink_key(sink, MEL_WIN32_KEYBOARD_ID, &ke);
        if (handled)
            *handled = true;
        return 0;
    }
    case WM_CHAR:
    {
        Mel_Input_Key_Event ke = { .keycode = (Mel_Keycode)wp, .modifiers = win_modifiers(), .down = true };
        mel_input_sink_key(sink, MEL_WIN32_KEYBOARD_ID, &ke);
        if (handled)
            *handled = true;
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        f32                   nx = (f32)GET_X_LPARAM(lp), ny = (f32)GET_Y_LPARAM(lp);
        Mel_Input_Mouse_Event me = { .x = nx, .y = ny, .dx = nx - g_win.mouse_x, .dy = ny - g_win.mouse_y, .buttons = g_win.buttons };
        g_win.mouse_x = nx;
        g_win.mouse_y = ny;
        mel_input_sink_mouse(sink, MEL_WIN32_MOUSE_ID, &me);
        return 0;
    }
    case WM_LBUTTONDOWN:
        win_emit_button(sink, MEL_INPUT_MOUSE_BUTTON_LEFT, true);
        return 0;
    case WM_LBUTTONUP:
        win_emit_button(sink, MEL_INPUT_MOUSE_BUTTON_LEFT, false);
        return 0;
    case WM_RBUTTONDOWN:
        win_emit_button(sink, MEL_INPUT_MOUSE_BUTTON_RIGHT, true);
        return 0;
    case WM_RBUTTONUP:
        win_emit_button(sink, MEL_INPUT_MOUSE_BUTTON_RIGHT, false);
        return 0;
    case WM_MBUTTONDOWN:
        win_emit_button(sink, MEL_INPUT_MOUSE_BUTTON_MIDDLE, true);
        return 0;
    case WM_MBUTTONUP:
        win_emit_button(sink, MEL_INPUT_MOUSE_BUTTON_MIDDLE, false);
        return 0;
    case WM_MOUSEWHEEL:
    {
        Mel_Input_Mouse_Event me = { .x = g_win.mouse_x, .y = g_win.mouse_y, .wheel_y = (f32)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA, .buttons = g_win.buttons };
        mel_input_sink_mouse(sink, MEL_WIN32_MOUSE_ID, &me);
        return 0;
    }
    case WM_MOUSEHWHEEL:
    {
        Mel_Input_Mouse_Event me = { .x = g_win.mouse_x, .y = g_win.mouse_y, .wheel_x = (f32)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA, .buttons = g_win.buttons };
        mel_input_sink_mouse(sink, MEL_WIN32_MOUSE_ID, &me);
        return 0;
    }
    case WM_INPUT:
    {
        if (!g_win.relative)
            return 0;
        UINT size = 0;
        GetRawInputData((HRAWINPUT)lp, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER));
        if (size == 0 || size > 256)
            return 0;
        BYTE buf[256];
        if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) != size)
            return 0;
        RAWINPUT* ri = (RAWINPUT*)buf;
        if (ri->header.dwType == RIM_TYPEMOUSE && (ri->data.mouse.usFlags & MOUSE_MOVE_RELATIVE) == 0)
        {
            Mel_Input_Mouse_Event me = { .dx = (f32)ri->data.mouse.lLastX, .dy = (f32)ri->data.mouse.lLastY, .buttons = g_win.buttons, .relative = true };
            mel_input_sink_mouse(sink, MEL_WIN32_MOUSE_ID, &me);
        }
        return 0;
    }
    case WM_POINTERUPDATE:
    case WM_POINTERDOWN:
    case WM_POINTERUP:
    {
        UINT32             pid = GET_POINTERID_WPARAM(wp);
        POINTER_INPUT_TYPE ptype = PT_POINTER;
        if (!GetPointerType(pid, &ptype))
            return 0;
        if (ptype == PT_PEN)
        {
            POINTER_PEN_INFO pen;
            if (!GetPointerPenInfo(pid, &pen))
                return 0;
            f32                 phase = msg == WM_POINTERDOWN ? MEL_INPUT_PEN_DOWN : (msg == WM_POINTERUP ? MEL_INPUT_PEN_UP : MEL_INPUT_PEN_MOVE);
            Mel_Input_Pen_Event pe = {
                .phase = (u32)phase,
                .x = (f32)pen.pointerInfo.ptPixelLocation.x,
                .y = (f32)pen.pointerInfo.ptPixelLocation.y,
                .pressure = (f32)pen.pressure,
                .tilt_x = (f32)pen.tiltX,
                .tilt_y = (f32)pen.tiltY,
                .rotation = (f32)pen.rotation,
                .eraser = (pen.penFlags & PEN_FLAG_ERASER) != 0,
                .buttons = (pen.penFlags & PEN_FLAG_BARREL) ? 1u : 0u,
                .in_proximity = (pen.pointerInfo.pointerFlags & POINTER_FLAG_INRANGE) != 0,
            };
            mel_input_sink_pen(sink, MEL_WIN32_PEN_ID, &pe);
        }
        else if (ptype == PT_TOUCH)
        {
            POINTER_TOUCH_INFO ti;
            if (!GetPointerTouchInfo(pid, &ti))
                return 0;
            u32                   phase = msg == WM_POINTERDOWN ? MEL_INPUT_TOUCH_DOWN : (msg == WM_POINTERUP ? MEL_INPUT_TOUCH_UP : MEL_INPUT_TOUCH_MOVE);
            Mel_Input_Touch_Event te = {
                .finger_id = pid,
                .phase = phase,
                .x = (f32)ti.pointerInfo.ptPixelLocation.x,
                .y = (f32)ti.pointerInfo.ptPixelLocation.y,
                .pressure = (f32)ti.pressure,
                .direct = true,
            };
            mel_input_sink_touch(sink, MEL_WIN32_TOUCH_ID, &te);
        }
        return 0;
    }
    default:
        return 0;
    }
}
