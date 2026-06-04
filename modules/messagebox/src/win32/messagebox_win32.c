#include <messagebox/backend.h>
#include <log/log.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>

#include <string.h>

bool mel_msgbox__plat_available(void) { return true; }

static wchar_t* wide_from_str8(const Mel_Alloc* a, str8 s)
{
    if (s.len <= 0 || !s.data)
    {
        wchar_t* empty = (wchar_t*)mel_alloc(a, sizeof(wchar_t));
        if (empty)
            empty[0] = 0;
        return empty;
    }
    int n = MultiByteToWideChar(CP_UTF8, 0, (const char*)s.data, (int)s.len, NULL, 0);
    if (n <= 0)
    {
        wchar_t* empty = (wchar_t*)mel_alloc(a, sizeof(wchar_t));
        if (empty)
            empty[0] = 0;
        return empty;
    }
    wchar_t* w = (wchar_t*)mel_alloc(a, sizeof(wchar_t) * (usize)(n + 1));
    if (!w)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, (const char*)s.data, (int)s.len, w, n);
    w[n] = 0;
    return w;
}

Mel_Msgbox_Status mel_msgbox__plat_show(const Mel_Msgbox_Request* req, i32* out_chosen_id)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Msgbox_Status warn = 0;

    HWND parent = (HWND)req->native_parent;

    wchar_t* wtitle = wide_from_str8(a, req->title);
    wchar_t* wmsg = wide_from_str8(a, req->message);

    TASKDIALOG_BUTTON* tbuttons = (TASKDIALOG_BUTTON*)mel_alloc(a, sizeof(TASKDIALOG_BUTTON) * (usize)req->button_count);
    wchar_t**          wlabels = (wchar_t**)mel_alloc(a, sizeof(wchar_t*) * (usize)req->button_count);
    if (!wtitle || !wmsg || !tbuttons || !wlabels)
    {
        mel_log_error("messagebox", "win32 dialog: out of memory");
        if (wtitle) mel_dealloc(a, wtitle);
        if (wmsg) mel_dealloc(a, wmsg);
        if (tbuttons) mel_dealloc(a, tbuttons);
        if (wlabels) mel_dealloc(a, wlabels);
        *out_chosen_id = req->escape_id;
        return MEL_MSGBOX_ERROR;
    }

    for (u32 i = 0; i < req->button_count; i++)
    {
        str8 label = req->buttons[i].label.len > 0 ? req->buttons[i].label : (str8){ (u8*)"OK", 2 };
        wlabels[i] = wide_from_str8(a, label);
        tbuttons[i].nButtonID = (int)(i + 1);
        tbuttons[i].pszButtonText = wlabels[i] ? wlabels[i] : L"OK";
    }

    int default_button = (int)req->button_count > 0 ? 1 : 0;
    for (u32 i = 0; i < req->button_count; i++)
        if (req->buttons[i].id == req->default_id)
            default_button = (int)(i + 1);

    TASKDIALOGCONFIG cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.cbSize = sizeof cfg;
    cfg.hwndParent = parent;
    cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    if (req->right_to_left)
        cfg.dwFlags |= TDF_RTL_LAYOUT;
    cfg.pszWindowTitle = (wtitle && wtitle[0]) ? wtitle : L"";
    cfg.pszMainInstruction = (wtitle && wtitle[0]) ? wtitle : wmsg;
    cfg.pszContent = (wtitle && wtitle[0]) ? wmsg : L"";
    cfg.cButtons = req->button_count;
    cfg.pButtons = tbuttons;
    cfg.nDefaultButton = default_button;

    switch (req->severity)
    {
        case MEL_MSGBOX_SEVERITY_WARN:  cfg.pszMainIcon = TD_WARNING_ICON; break;
        case MEL_MSGBOX_SEVERITY_ERROR: cfg.pszMainIcon = TD_ERROR_ICON; break;
        default:                        cfg.pszMainIcon = TD_INFORMATION_ICON; break;
    }

    int     pressed = 0;
    HRESULT hr = TaskDialogIndirect(&cfg, &pressed, NULL, NULL);

    i32 chosen = req->escape_id;
    Mel_Msgbox_Status st = MEL_MSGBOX_OK;
    if (SUCCEEDED(hr))
    {
        if (pressed >= 1 && (u32)pressed <= req->button_count)
            chosen = req->buttons[pressed - 1].id;
        else
            st |= MEL_MSGBOX_RESULT_DISMISSED;
    }
    else
    {
        mel_log_error("messagebox", "TaskDialogIndirect failed: hr=0x%08lx", (unsigned long)hr);
        st = MEL_MSGBOX_ERROR;
    }

    if (req->accent.has_value || req->text.has_value || req->background.has_value)
        warn |= MEL_MSGBOX_WARN_COLOR_DROPPED;

    for (u32 i = 0; i < req->button_count; i++)
        if (wlabels[i])
            mel_dealloc(a, wlabels[i]);
    mel_dealloc(a, wlabels);
    mel_dealloc(a, tbuttons);
    mel_dealloc(a, wtitle);
    mel_dealloc(a, wmsg);

    *out_chosen_id = chosen;
    return st | warn | (warn && (st & MEL_MSGBOX_SEVERITY_MASK) == MEL_MSGBOX_OK ? MEL_MSGBOX_WARNED : MEL_MSGBOX_OK);
}
