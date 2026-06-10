#include "win32.h"

static void tab_display_rect(HWND tabctl, RECT* out)
{
    GetClientRect(tabctl, out);
    TabCtrl_AdjustRect(tabctl, FALSE, out);
}

static void tab_arrange(Mel_Gui_Handle container)
{
    Mel_Gui_Node* node = mel_gui__node(container);
    if (!node || !node->native)
        return;

    HWND tabctl = (HWND)node->native;
    RECT rc;
    tab_display_rect(tabctl, &rc);

    for (Mel_Gui_Handle ch = mel_gui__first_child(container); !mel_gui_handle_is_none(ch); ch = mel_gui__next_sibling(ch))
    {
        Mel_Gui_Node* c = mel_gui__node(ch);
        if (!c)
            continue;
        c->x = rc.left;
        c->y = rc.top;
        c->width = rc.right - rc.left;
        c->height = rc.bottom - rc.top;
        mel_gui__push_bounds(c->self);
        if (c->layout || c->is_scroll_host)
            mel_gui__layout_arrange(c->self);
    }
}

static LRESULT CALLBACK tabview_parent_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref)
{
    Mel_Win32_TabView* tv = (Mel_Win32_TabView*)ref;
    if (msg == WM_NOTIFY && tv)
    {
        LPNMHDR nh = (LPNMHDR)lp;
        if (nh->hwndFrom == tv->tabctl && nh->code == (UINT)TCN_SELCHANGE)
        {
            i32 sel = (i32)TabCtrl_GetCurSel(tv->tabctl);
            for (i32 i = 0; i < tv->page_count; i++)
            {
                ShowWindow(tv->pages[i], i == sel ? SW_SHOW : SW_HIDE);
            }
            tv->selected = sel;
            if (tv->on_select)
                tv->on_select(tv->base.handle, sel, mel_gui_user(tv->base.handle));
            return 0;
        }
    }
    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, tabview_parent_subclass, id);
    return DefSubclassProc(hwnd, msg, wp, lp);
}

Mel_Gui_Handle mel_tabview_create_opt(Mel_Gui_Handle parent, Mel_TabView_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden, &o.layoutable, NULL);
    Mel_Gui_Node*  n = mel_gui__node(h);
    if (!n)
        return h;
    n->container_arrange = tab_arrange;

    HWND par = mel_gui__win32_parent_hwnd(n);
    if (!par)
        return h;

    HWND tabctl = CreateWindowExW(0, WC_TABCONTROLW, NULL, mel_gui__win32_child_style(n, o.disabled) | WS_CLIPSIBLINGS, n->x, n->y, n->width, n->height, par, NULL, current_hinst, NULL);
    n->native = tabctl;
    if (!tabctl)
        return h;

    Mel_Win32_TabView* tv = (Mel_Win32_TabView*)mel_gui__win32_alloc_ctl(tabctl, sizeof *tv, h);
    if (tv)
    {
        tv->base.focus = o.focus;
        tv->on_select = o.on_select;
        tv->tabctl = tabctl;
        tv->selected = -1;
        SetWindowSubclass(par, tabview_parent_subclass, (UINT_PTR)tabctl, (DWORD_PTR)tv);
    }
    if (mel_tabview_style_any(&o.style))
        mel_tabview_set_style_opt(h, o.style);
    return h;
}

Mel_Gui_Handle mel_tab_create_opt(Mel_Gui_Handle tabview, Mel_Tab_Opt o)
{
    Mel_Gui_Node* tn = mel_gui__node(tabview);
    if (!tn || !tn->native)
        return MEL_GUI_HANDLE_NONE;
    HWND               tabctl = (HWND)tn->native;
    Mel_Win32_TabView* tv = (Mel_Win32_TabView*)mel_gui__win32_ctl(tabctl);
    if (!tv || tv->page_count >= MEL_TABVIEW_MAX_PAGES)
        return MEL_GUI_HANDLE_NONE;

    Mel_Gui_Handle h = mel_gui__node_new(tabview, 0, 0, 0, 0, o.id, o.user, false, &o.layoutable, o.layout);
    Mel_Gui_Node*  n = mel_gui__node(h);
    if (!n)
        return h;

    i32 index = tv->page_count;

    wchar_t wbuf[512];
    mel_gui__win32_widen(o.title, wbuf, 512);
    TCITEMW item = { 0 };
    item.mask = TCIF_TEXT;
    item.pszText = wbuf;
    TabCtrl_InsertItem(tabctl, index, &item);

    RECT rc;
    tab_display_rect(tabctl, &rc);
    bool hidden = index != 0;
    HWND page = mel_gui__win32_make_container(tabctl, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, h, (Mel_Gui_Pointer_Cb){ 0 }, (Mel_Gui_Focus_Cb){ 0 }, (Mel_Gui_Keyboard_Cb){ 0 }, hidden, false);

    n->native = page;
    n->content = page;
    n->x = rc.left;
    n->y = rc.top;
    n->width = rc.right - rc.left;
    n->height = rc.bottom - rc.top;

    tv->pages[index] = page;
    tv->page_count++;
    if (tv->selected < 0)
        tv->selected = 0;
    if (mel_tab_style_any(&o.style))
        mel_tab_set_style_opt(h, o.style);
    return h;
}

void mel_tabview_select(Mel_Gui_Handle tabview, i32 index)
{
    Mel_Gui_Node* n = mel_gui__node(tabview);
    if (!n || !n->native)
        return;
    HWND               tabctl = (HWND)n->native;
    Mel_Win32_TabView* tv = (Mel_Win32_TabView*)mel_gui__win32_ctl(tabctl);
    if (!tv || index < 0 || index >= tv->page_count)
        return;
    TabCtrl_SetCurSel(tabctl, index);
    for (i32 i = 0; i < tv->page_count; i++)
    {
        ShowWindow(tv->pages[i], i == index ? SW_SHOW : SW_HIDE);
    }
    tv->selected = index;
    if (tv->on_select)
        tv->on_select(tv->base.handle, index, mel_gui_user(tv->base.handle));
}

i32 mel_tabview_selected(Mel_Gui_Handle tabview)
{
    Mel_Gui_Node* n = mel_gui__node(tabview);
    if (!n || !n->native)
        return -1;
    Mel_Win32_TabView* tv = (Mel_Win32_TabView*)mel_gui__win32_ctl((HWND)n->native);
    return tv ? tv->selected : -1;
}
