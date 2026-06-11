#include <messagebox/backend.h>
#include <log/log.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>

#include <emscripten.h>

#include <string.h>

EM_JS(void, mel_msgbox_js_alert, (const char* title, const char* msg), {
    var t = title ? UTF8ToString(title) : "";
    var m = msg ? UTF8ToString(msg) : "";
    window.alert(t.length ? (t + "\n\n" + m) : m);
})

EM_JS(int, mel_msgbox_js_confirm, (const char* title, const char* msg), {
    var t = title ? UTF8ToString(title) : "";
    var m = msg ? UTF8ToString(msg) : "";
    return window.confirm(t.length ? (t + "\n\n" + m) : m) ? 1 : 0;
})

bool mel_msgbox__plat_available(void) { return true; }

static char* cstr_dup(const Mel_Alloc* a, str8 s)
{
    char* c = (char*)mel_alloc(a, (usize)(s.len > 0 ? s.len : 0) + 1);
    if (!c)
        return NULL;
    if (s.len > 0 && s.data)
        memcpy(c, s.data, (usize)s.len);
    c[s.len > 0 ? s.len : 0] = 0;
    return c;
}

Mel_Msgbox_Status mel_msgbox__plat_show(const Mel_Msgbox_Request* req, i32* out_chosen_id)
{
    const Mel_Alloc* a = mel_alloc_heap();
    char*            title = cstr_dup(a, req->title);
    char*            message = cstr_dup(a, req->message);

    Mel_Msgbox_Status warn = 0;
    if (req->accent.has_value || req->text.has_value || req->background.has_value)
        warn |= MEL_MSGBOX_WARN_COLOR_DROPPED;
    if (req->right_to_left)
        warn |= MEL_MSGBOX_WARN_RTL_DROPPED;
    if (req->native_parent)
        warn |= MEL_MSGBOX_WARN_PARENT_DROPPED;

    i32 chosen;
    Mel_Msgbox_Status st = MEL_MSGBOX_OK;

    if (req->button_count <= 1)
    {
        mel_msgbox_js_alert(title, message);
        chosen = req->button_count == 1 ? req->buttons[0].id : req->default_id;
    }
    else
    {
        bool collapsed = req->button_count > 2;
        if (collapsed)
            warn |= MEL_MSGBOX_WARN_BUTTONS_COLLAPSED;
        int ok = mel_msgbox_js_confirm(title, message);
        chosen = ok ? req->default_id : req->escape_id;
        if (!ok && collapsed)
            st |= MEL_MSGBOX_RESULT_DISMISSED;
    }

    if (title)
        mel_dealloc(a, title);
    if (message)
        mel_dealloc(a, message);

    *out_chosen_id = chosen;
    return st | warn | (warn && (st & MEL_MSGBOX_SEVERITY_MASK) == MEL_MSGBOX_OK ? MEL_MSGBOX_WARNED : MEL_MSGBOX_OK);
}
