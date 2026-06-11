#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <tray/provider.h>
#include <tray/win32/win32.h>
#include <allocator/allocator.h>
#include <collection/array.h>
#include <log/log.h>

#include "../../src/tray_internal.h"

#define MEL_TRAY_WM_NOTIFY (WM_APP + 0x31)
#define MEL_TRAY_CMD_BASE  0x1000

typedef struct
{
    u64   token;
    UINT  uid;
    HMENU menu;
    HICON icon;
} Win_Tray;

typedef struct
{
    u64   token;
    HMENU menu;
} Win_Menu;

typedef struct
{
    u64   token;
    HMENU parent;
    UINT  cmd;
    bool  separator;
} Win_Item;

static struct
{
    bool             ready;
    const Mel_Alloc* alloc;
    HWND             msg_window;
    UINT             next_uid;
    UINT             next_cmd;
    Mel_Array(Win_Tray) trays;
    Mel_Array(Win_Menu) menus;
    Mel_Array(Win_Item) items;
} w;

static Win_Tray* tray_by_token(u64 token)
{
    for (usize i = 0; i < w.trays.count; i++)
        if (w.trays.items[i].token == token)
            return &w.trays.items[i];
    return NULL;
}

static Win_Tray* tray_by_uid(UINT uid)
{
    for (usize i = 0; i < w.trays.count; i++)
        if (w.trays.items[i].uid == uid)
            return &w.trays.items[i];
    return NULL;
}

static Win_Menu* menu_by_token(u64 token)
{
    for (usize i = 0; i < w.menus.count; i++)
        if (w.menus.items[i].token == token)
            return &w.menus.items[i];
    return NULL;
}

static Win_Item* item_by_token(u64 token)
{
    for (usize i = 0; i < w.items.count; i++)
        if (w.items.items[i].token == token)
            return &w.items.items[i];
    return NULL;
}

static Win_Item* item_by_cmd(UINT cmd)
{
    for (usize i = 0; i < w.items.count; i++)
        if (w.items.items[i].cmd == cmd)
            return &w.items.items[i];
    return NULL;
}

static WCHAR* wide_from(str8 s, const Mel_Alloc* alloc)
{
    if (s.len == 0 || s.data == NULL)
    {
        WCHAR* z = mel_alloc(alloc, sizeof(WCHAR));
        z[0] = 0;
        return z;
    }
    int    n = MultiByteToWideChar(CP_UTF8, 0, (const char*)s.data, (int)s.len, NULL, 0);
    WCHAR* buf = mel_alloc(alloc, (usize)(n + 1) * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, (const char*)s.data, (int)s.len, buf, n);
    buf[n] = 0;
    return buf;
}

static HICON icon_from(Mel_Tray_Image img)
{
    if (img.rgba == NULL || img.width == 0 || img.height == 0)
        return NULL;
    BITMAPV5HEADER bi = { 0 };
    bi.bV5Size = sizeof bi;
    bi.bV5Width = (LONG)img.width;
    bi.bV5Height = -(LONG)img.height;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC     hdc = GetDC(NULL);
    void*   bits = NULL;
    HBITMAP color = CreateDIBSection(hdc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, hdc);
    if (color == NULL || bits == NULL)
        return NULL;

    const u8* src = img.rgba;
    u8*       dst = bits;
    for (u32 i = 0; i < img.width * img.height; i++)
    {
        dst[i * 4 + 0] = src[i * 4 + 2];
        dst[i * 4 + 1] = src[i * 4 + 1];
        dst[i * 4 + 2] = src[i * 4 + 0];
        dst[i * 4 + 3] = src[i * 4 + 3];
    }

    HBITMAP  mask = CreateBitmap((int)img.width, (int)img.height, 1, 1, NULL);
    ICONINFO ii = { .fIcon = TRUE, .hbmMask = mask, .hbmColor = color };
    HICON    icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == MEL_TRAY_WM_NOTIFY)
    {
        UINT      uid = (UINT)HIWORD(lp);
        UINT      ev = (UINT)LOWORD(lp);
        Win_Tray* wt = tray_by_uid(uid);
        if (wt == NULL)
            return 0;
        if (ev == WM_LBUTTONUP)
            mel_tray__dispatch_activate(wt->token, MEL_TRAY_BUTTON_LEFT);
        else if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU)
        {
            mel_tray__dispatch_activate(wt->token, MEL_TRAY_BUTTON_RIGHT);
            if (wt->menu != NULL)
            {
                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                mel_tray__dispatch_menu(wt->token, MEL_TRAY_EVENT_MENU_OPENED);
                TrackPopupMenu(wt->menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                PostMessageW(hwnd, WM_NULL, 0, 0);
                mel_tray__dispatch_menu(wt->token, MEL_TRAY_EVENT_MENU_CLOSED);
            }
        }
        return 0;
    }
    if (msg == WM_COMMAND)
    {
        UINT      cmd = (UINT)LOWORD(wp);
        Win_Item* it = item_by_cmd(cmd);
        if (it != NULL)
            mel_tray__dispatch_item_clicked(it->token);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool ensure_window(void)
{
    if (w.msg_window != NULL)
        return true;
    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hi;
    wc.lpszClassName = L"MelodyTrayWindow";
    RegisterClassW(&wc);
    w.msg_window = CreateWindowExW(0, L"MelodyTrayWindow", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hi, NULL);
    if (w.msg_window == NULL)
    {
        mel_log_error("tray", "win32 CreateWindowExW (message window) failed");
        return false;
    }
    return true;
}

static bool win_supported(void* user)
{
    (void)user;
    return true;
}

static Mel_Tray_Status win_create(void* user, const Mel_Tray_Lowered* lowered)
{
    (void)user;
    if (!ensure_window())
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;

    Win_Tray  wt = { .token = lowered->token, .uid = ++w.next_uid, .menu = NULL, .icon = NULL };
    Win_Menu* wm = menu_by_token(lowered->menu_token);
    if (wm != NULL)
        wt.menu = wm->menu;
    wt.icon = icon_from(lowered->image);

    NOTIFYICONDATAW nid = { 0 };
    nid.cbSize = sizeof nid;
    nid.hWnd = w.msg_window;
    nid.uID = wt.uid;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = MEL_TRAY_WM_NOTIFY;
    nid.hIcon = wt.icon != NULL ? wt.icon : LoadIcon(NULL, IDI_APPLICATION);
    if (lowered->tooltip.len > 0)
    {
        WCHAR* tip = wide_from(lowered->tooltip, w.alloc);
        lstrcpynW(nid.szTip, tip, (int)(sizeof nid.szTip / sizeof(WCHAR)));
        mel_dealloc(w.alloc, tip);
    }
    if (!Shell_NotifyIconW(NIM_ADD, &nid))
    {
        mel_log_error("tray", "win32 Shell_NotifyIconW(NIM_ADD) failed");
        if (wt.icon != NULL)
            DestroyIcon(wt.icon);
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    }
    if (!lowered->visible)
    {
        nid.uFlags = NIF_STATE;
        nid.dwState = NIS_HIDDEN;
        nid.dwStateMask = NIS_HIDDEN;
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
    mel_array_push(&w.trays, wt);
    return MEL_TRAY_OK;
}

static void win_destroy(void* user, u64 token)
{
    (void)user;
    Win_Tray* wt = tray_by_token(token);
    if (wt == NULL)
        return;
    NOTIFYICONDATAW nid = { .cbSize = sizeof nid, .hWnd = w.msg_window, .uID = wt->uid };
    Shell_NotifyIconW(NIM_DELETE, &nid);
    if (wt->icon != NULL)
        DestroyIcon(wt->icon);
    usize idx = (usize)(wt - w.trays.items);
    mel_array_remove_unordered(&w.trays, idx);
}

static Mel_Tray_Status win_set_image(void* user, u64 token, Mel_Tray_Image image)
{
    (void)user;
    Win_Tray* wt = tray_by_token(token);
    if (wt == NULL)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    HICON old = wt->icon;
    wt->icon = icon_from(image);
    NOTIFYICONDATAW nid = { .cbSize = sizeof nid, .hWnd = w.msg_window, .uID = wt->uid, .uFlags = NIF_ICON };
    nid.hIcon = wt->icon != NULL ? wt->icon : LoadIcon(NULL, IDI_APPLICATION);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
    if (old != NULL)
        DestroyIcon(old);
    return MEL_TRAY_OK;
}

static Mel_Tray_Status win_set_tooltip(void* user, u64 token, str8 tooltip)
{
    (void)user;
    Win_Tray* wt = tray_by_token(token);
    if (wt == NULL)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    NOTIFYICONDATAW nid = { .cbSize = sizeof nid, .hWnd = w.msg_window, .uID = wt->uid, .uFlags = NIF_TIP };
    WCHAR*          tip = wide_from(tooltip, w.alloc);
    lstrcpynW(nid.szTip, tip, (int)(sizeof nid.szTip / sizeof(WCHAR)));
    mel_dealloc(w.alloc, tip);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
    return MEL_TRAY_OK;
}

static Mel_Tray_Status win_set_title(void* user, u64 token, str8 title)
{
    (void)user;
    (void)token;
    (void)title;
    return MEL_TRAY_WARNED | MEL_TRAY_WARN_TITLE_DROPPED;
}

static Mel_Tray_Status win_set_visible(void* user, u64 token, bool visible)
{
    (void)user;
    Win_Tray* wt = tray_by_token(token);
    if (wt == NULL)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    NOTIFYICONDATAW nid = { .cbSize = sizeof nid, .hWnd = w.msg_window, .uID = wt->uid, .uFlags = NIF_STATE };
    nid.dwState = visible ? 0 : NIS_HIDDEN;
    nid.dwStateMask = NIS_HIDDEN;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
    return MEL_TRAY_OK;
}

static Mel_Tray_Status win_menu_create(void* user, u64 menu_token)
{
    (void)user;
    Win_Menu wm = { .token = menu_token, .menu = CreatePopupMenu() };
    if (wm.menu == NULL)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    mel_array_push(&w.menus, wm);
    return MEL_TRAY_OK;
}

static void win_menu_destroy(void* user, u64 menu_token)
{
    (void)user;
    Win_Menu* wm = menu_by_token(menu_token);
    if (wm == NULL)
        return;
    DestroyMenu(wm->menu);
    usize idx = (usize)(wm - w.menus.items);
    mel_array_remove_unordered(&w.menus, idx);
}

static Mel_Tray_Status win_item_add(void* user, const Mel_Tray_Item_Lowered* lowered)
{
    (void)user;
    Win_Menu* parent = menu_by_token(lowered->parent_menu_token);
    if (parent == NULL)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;

    Win_Item      it = { .token = lowered->token, .parent = parent->menu, .cmd = 0, .separator = false };
    MENUITEMINFOW mii = { .cbSize = sizeof mii };
    if ((lowered->flags & MEL_TRAY_ITEM_SEPARATOR) != 0)
    {
        it.separator = true;
        mii.fMask = MIIM_FTYPE;
        mii.fType = MFT_SEPARATOR;
    }
    else
    {
        it.cmd = MEL_TRAY_CMD_BASE + (++w.next_cmd);
        WCHAR* label = wide_from(lowered->label, w.alloc);
        mii.fMask = MIIM_STRING | MIIM_STATE | MIIM_FTYPE;
        mii.fType = MFT_STRING;
        mii.dwTypeData = label;
        mii.fState = ((lowered->flags & MEL_TRAY_ITEM_ENABLED) != 0 ? MFS_ENABLED : MFS_DISABLED);
        if ((lowered->flags & MEL_TRAY_ITEM_CHECKBOX) != 0 && (lowered->flags & MEL_TRAY_ITEM_CHECKED) != 0)
            mii.fState |= MFS_CHECKED;
        if (lowered->submenu_token != 0)
        {
            Win_Menu* sub = menu_by_token(lowered->submenu_token);
            if (sub != NULL)
            {
                mii.fMask |= MIIM_SUBMENU;
                mii.hSubMenu = sub->menu;
            }
        }
        else
        {
            mii.fMask |= MIIM_ID;
            mii.wID = it.cmd;
        }
        InsertMenuItemW(parent->menu, (UINT)lowered->at, TRUE, &mii);
        mel_dealloc(w.alloc, label);
        mel_array_push(&w.items, it);
        return MEL_TRAY_OK;
    }
    InsertMenuItemW(parent->menu, (UINT)lowered->at, TRUE, &mii);
    mel_array_push(&w.items, it);
    return MEL_TRAY_OK;
}

static void win_item_remove(void* user, u64 token)
{
    (void)user;
    Win_Item* it = item_by_token(token);
    if (it == NULL)
        return;
    if (it->cmd != 0)
        DeleteMenu(it->parent, it->cmd, MF_BYCOMMAND);
    usize idx = (usize)(it - w.items.items);
    mel_array_remove_unordered(&w.items, idx);
}

static Mel_Tray_Status win_item_set_label(void* user, u64 token, str8 label)
{
    (void)user;
    Win_Item* it = item_by_token(token);
    if (it == NULL || it->separator)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    WCHAR*        wlabel = wide_from(label, w.alloc);
    MENUITEMINFOW mii = { .cbSize = sizeof mii, .fMask = MIIM_STRING, .dwTypeData = wlabel };
    SetMenuItemInfoW(it->parent, it->cmd, FALSE, &mii);
    mel_dealloc(w.alloc, wlabel);
    return MEL_TRAY_OK;
}

static Mel_Tray_Status win_item_set_flags(void* user, u64 token, Mel_Tray_Item_Flags flags)
{
    (void)user;
    Win_Item* it = item_by_token(token);
    if (it == NULL || it->separator)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    MENUITEMINFOW mii = { .cbSize = sizeof mii, .fMask = MIIM_STATE };
    mii.fState = ((flags & MEL_TRAY_ITEM_ENABLED) != 0 ? MFS_ENABLED : MFS_DISABLED);
    if ((flags & MEL_TRAY_ITEM_CHECKBOX) != 0 && (flags & MEL_TRAY_ITEM_CHECKED) != 0)
        mii.fState |= MFS_CHECKED;
    SetMenuItemInfoW(it->parent, it->cmd, FALSE, &mii);
    return MEL_TRAY_OK;
}

static void* win_native(void* user, u64 token)
{
    (void)user;
    Win_Tray* wt = tray_by_token(token);
    return wt != NULL ? (void*)wt->menu : NULL;
}

void mel_tray__register_host_providers(void)
{
    if (!w.ready)
    {
        w.alloc = mel_tray__alloc();
        mel_array_init(&w.trays, w.alloc);
        mel_array_init(&w.menus, w.alloc);
        mel_array_init(&w.items, w.alloc);
        w.next_uid = 0;
        w.next_cmd = 0;
        w.ready = true;
    }
    static const Mel_Tray_Provider_Desc desc = {
        .name = "win32-shellnotifyicon",
        .supported = win_supported,
        .create = win_create,
        .destroy = win_destroy,
        .set_image = win_set_image,
        .set_tooltip = win_set_tooltip,
        .set_title = win_set_title,
        .set_visible = win_set_visible,
        .menu_create = win_menu_create,
        .menu_destroy = win_menu_destroy,
        .item_add = win_item_add,
        .item_remove = win_item_remove,
        .item_set_label = win_item_set_label,
        .item_set_flags = win_item_set_flags,
        .native = win_native,
    };
    mel_tray_provider_register(&desc);
}

HWND mel_tray_win32_message_window(Mel_Tray t)
{
    (void)t;
    return w.msg_window;
}

HMENU mel_tray_win32_menu(Mel_Tray t) { return (HMENU)mel_tray_native(t); }

UINT mel_tray_win32_icon_id(Mel_Tray t)
{
    Win_Tray* wt = tray_by_token(mel_slotmap_handle_pack64(t.h));
    return wt != NULL ? wt->uid : 0;
}
