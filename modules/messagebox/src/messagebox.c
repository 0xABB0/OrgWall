#include <messagebox/messagebox.h>
#include <messagebox/backend.h>

#include <window/window.h>
#include <log/log.h>

static const Mel_Msgbox_Button MEL_MSGBOX_OK_BUTTON = { .label = { 0 }, .id = 0 };

static void* resolve_native_parent(Mel_Window parent, Mel_Msgbox_Status* warn)
{
    if (mel_window_is_none(parent))
        return NULL;
    if (!mel_window_alive(parent))
    {
        *warn |= MEL_MSGBOX_WARN_PARENT_DROPPED;
        return NULL;
    }
    void* native = mel_window_native(parent);
    if (!native)
        *warn |= MEL_MSGBOX_WARN_PARENT_DROPPED;
    return native;
}

static Mel_Msgbox_Status merge_severity(Mel_Msgbox_Status base, Mel_Msgbox_Status warn)
{
    Mel_Msgbox_Status merged = base | warn;
    if ((merged & MEL_MSGBOX_SEVERITY_MASK) == MEL_MSGBOX_OK && warn != 0)
        merged |= MEL_MSGBOX_WARNED;
    return merged;
}

static Mel_Msgbox_Result run(const Mel_Msgbox_Button* buttons, u32 button_count, Mel_Msgbox_Opt opt)
{
    Mel_Msgbox_Result r = { .chosen_id = 0, .status = MEL_MSGBOX_OK };

    if (button_count == 0 || !buttons)
    {
        mel_log_error("messagebox", "show with no buttons");
        r.status = MEL_MSGBOX_ERROR;
        return r;
    }

    i32 default_id = opt.has_default_id ? opt.default_id : buttons[0].id;
    i32 escape_id = opt.has_escape_id ? opt.escape_id : buttons[button_count - 1].id;

    if (!mel_msgbox__plat_available())
    {
        mel_log_error("messagebox", "no native dialog backend on this platform");
        r.status = MEL_MSGBOX_ERROR | MEL_MSGBOX_RESULT_NO_BACKEND;
        r.chosen_id = escape_id;
        return r;
    }

    Mel_Msgbox_Status warn = 0;

    Mel_Msgbox_Request req = {
        .title         = opt.title,
        .message       = opt.message,
        .severity      = opt.severity,
        .buttons       = buttons,
        .button_count  = button_count,
        .default_id    = default_id,
        .escape_id     = escape_id,
        .accent        = opt.accent,
        .text          = opt.text,
        .background    = opt.background,
        .right_to_left = opt.right_to_left,
        .native_parent = resolve_native_parent(opt.parent, &warn),
    };

    if (str8_is_empty(opt.title))
        warn |= MEL_MSGBOX_WARN_TITLE_SYNTHESIZED;

    i32               chosen = req.default_id;
    Mel_Msgbox_Status st = mel_msgbox__plat_show(&req, &chosen);

    r.chosen_id = chosen;
    r.status = merge_severity(st, warn);
    return r;
}

bool mel_msgbox_available(void) { return mel_msgbox__plat_available(); }

Mel_Msgbox_Status mel_msgbox_alert_opt(str8 title, str8 message, Mel_Msgbox_Opt opt)
{
    opt.title = title;
    opt.message = message;
    Mel_Msgbox_Result r = run(&MEL_MSGBOX_OK_BUTTON, 1, opt);
    return r.status;
}

Mel_Msgbox_Result mel_msgbox_show_opt(Mel_Msgbox_Opt opt)
{
    if (opt.button_count == 0 || !opt.buttons)
        return run(&MEL_MSGBOX_OK_BUTTON, 1, opt);
    return run(opt.buttons, opt.button_count, opt);
}
