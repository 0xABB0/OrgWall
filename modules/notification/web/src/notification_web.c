#include <notification/notification.h>
#include <notification/provider.h>

#include <allocator/allocator.h>
#include <log/log.h>

#include <emscripten.h>

#include <string.h>

#include "../../src/notification_internal.h"

EM_JS(int, mel_notif_js_available, (void), {
    return typeof Notification !== 'undefined' ? 1 : 0;
})

EM_JS(int, mel_notif_js_permission, (void), {
    if (typeof Notification === 'undefined')
        return 2;
    if (Notification.permission === 'granted')
        return 1;
    if (Notification.permission === 'denied')
        return 2;
    return 0;
})

EM_JS(void, mel_notif_js_request_permission, (void), {
    Notification.requestPermission().then(function(p) {
        var s = p === 'granted' ? 1 : (p === 'denied' ? 2 : 0);
        _mel_notif_web__on_permission(s);
    });
})

EM_JS(void, mel_notif_js_show, (u32 lo, u32 hi, const char* title, const char* body, const char* icon_url, const char* image_url, const u8* icon_rgba, u32 icon_w, u32 icon_h, int silent, double delay_ms, double interval_ms), {
    if (!Module.melNotif)
        Module.melNotif = { live: {}, timers: {} };
    var key = hi + ':' + lo;
    var opts = {};
    if (body)
        opts.body = UTF8ToString(body);
    if (icon_url)
        opts.icon = UTF8ToString(icon_url);
    else if (icon_rgba && icon_w > 0 && icon_h > 0)
    {
        var canvas = document.createElement('canvas');
        canvas.width = icon_w;
        canvas.height = icon_h;
        var ctx = canvas.getContext('2d');
        var img = ctx.createImageData(icon_w, icon_h);
        img.data.set(HEAPU8.subarray(icon_rgba, icon_rgba + icon_w * icon_h * 4));
        ctx.putImageData(img, 0, 0);
        opts.icon = canvas.toDataURL();
    }
    if (image_url)
        opts.image = UTF8ToString(image_url);
    opts.silent = silent !== 0;
    opts.tag = 'melody.' + key;
    var show = function() {
        try
        {
            var n = new Notification(title ? UTF8ToString(title) : "", opts);
            n.onclick = function() { _mel_notif_web__on_click(lo, hi); };
            n.onclose = function() { _mel_notif_web__on_close(lo, hi); };
            n.onshow = function() { _mel_notif_web__on_show(lo, hi); };
            Module.melNotif.live[key] = n;
        }
        catch (e)
        {
            console.error('melody notification: ' + e);
        }
    };
    var timers = [];
    if (interval_ms > 0)
        timers.push(setInterval(show, interval_ms));
    if (delay_ms > 0)
        timers.push(setTimeout(show, delay_ms));
    else if (interval_ms <= 0)
        show();
    if (timers.length)
        Module.melNotif.timers[key] = timers;
})

EM_JS(void, mel_notif_js_cancel, (u32 lo, u32 hi), {
    if (!Module.melNotif)
        return;
    var key = hi + ':' + lo;
    var n = Module.melNotif.live[key];
    if (n)
    {
        n.onclose = null;
        n.close();
        delete Module.melNotif.live[key];
    }
    var timers = Module.melNotif.timers[key];
    if (timers)
    {
        for (var i = 0; i < timers.length; i++)
        {
            clearTimeout(timers[i]);
            clearInterval(timers[i]);
        }
        delete Module.melNotif.timers[key];
    }
})

EM_JS(void, mel_notif_js_cancel_all, (void), {
    if (!Module.melNotif)
        return;
    for (var key in Module.melNotif.live)
    {
        var n = Module.melNotif.live[key];
        n.onclose = null;
        n.close();
    }
    for (var key in Module.melNotif.timers)
    {
        var timers = Module.melNotif.timers[key];
        for (var i = 0; i < timers.length; i++)
        {
            clearTimeout(timers[i]);
            clearInterval(timers[i]);
        }
    }
    Module.melNotif.live = {};
    Module.melNotif.timers = {};
})

EM_JS(double, mel_notif_js_now, (void), {
    return Date.now();
})

static Mel_Notif_Sink web_pending_sink;
static bool           web_sink_pending;

static const mel_notif_auth* web_map_permission(int p)
{
    if (p == 1)
        return &mel_notif_auth_granted;
    if (p == 2)
        return &mel_notif_auth_denied;
    return &mel_notif_auth_not_determined;
}

EMSCRIPTEN_KEEPALIVE void mel_notif_web__on_permission(int status)
{
    const mel_notif_auth* a = web_map_permission(status);
    mel_notif__dispatch_auth_changed(a);
    if (web_sink_pending)
    {
        web_sink_pending = false;
        web_pending_sink.on_auth(web_pending_sink.token, a);
    }
}

EMSCRIPTEN_KEEPALIVE void mel_notif_web__on_click(u32 lo, u32 hi)
{
    mel_notif__dispatch_activated(((u64)hi << 32) | lo, STR8_EMPTY, STR8_EMPTY, STR8_EMPTY);
}

EMSCRIPTEN_KEEPALIVE void mel_notif_web__on_close(u32 lo, u32 hi)
{
    mel_notif__dispatch_dismissed(((u64)hi << 32) | lo);
}

EMSCRIPTEN_KEEPALIVE void mel_notif_web__on_show(u32 lo, u32 hi)
{
    mel_notif__dispatch_presented(((u64)hi << 32) | lo);
}

static bool web_supported(void* user)
{
    MEL_UNUSED(user);
    return mel_notif_js_available() != 0;
}

static Mel_Notif_Caps web_caps(void* user)
{
    MEL_UNUSED(user);
    return MEL_NOTIF_CAP_ICON | MEL_NOTIF_CAP_ATTACHMENT | MEL_NOTIF_CAP_SCHEDULE | MEL_NOTIF_CAP_REPEAT | MEL_NOTIF_CAP_UPDATE | MEL_NOTIF_CAP_AUTH;
}

static const mel_notif_auth* web_authorization(void* user)
{
    MEL_UNUSED(user);
    return web_map_permission(mel_notif_js_permission());
}

static void web_authorize(void* user, Mel_Notif_Sink sink)
{
    MEL_UNUSED(user);
    web_pending_sink = sink;
    web_sink_pending = true;
    mel_notif_js_request_permission();
}

static const char* cstr(str8 s)
{
    if (s.len == 0 || s.data == NULL)
        return NULL;
    return str8_to_cstr_alloc(s, mel_notif__alloc());
}

static void cstr_free(const char* s)
{
    if (s != NULL)
        mel_dealloc(mel_notif__alloc(), (void*)s);
}

static Mel_Notif_Status web_post(void* user, const Mel_Notif_Lowered* lw)
{
    MEL_UNUSED(user);
    const Mel_Notif_Content* c = lw->content;
    const Mel_Alloc*         a = mel_notif__alloc();
    Mel_Notif_Status         warn = 0;

    if (web_authorization(NULL) != &mel_notif_auth_granted)
    {
        mel_log_error("notification", "post without granted permission; call mel_notif_authorize first");
        return MEL_NOTIF_ERROR | MEL_NOTIF_ERR_NOT_AUTHORIZED;
    }

    str8 body_s = STR8_EMPTY;
    const char* body_c;
    if (c->subtitle.len > 0)
    {
        body_s = str8_fmt_alloc(a, "%.*s\n%.*s", (int)c->subtitle.len, c->subtitle.data, (int)c->body.len, c->body.data);
        body_c = cstr(body_s);
    }
    else
        body_c = cstr(c->body);
    const char* title_c = cstr(c->title);
    const char* icon_c = cstr(c->icon.path);
    const char* image_c = cstr(c->attachment.path);

    if (c->attachment.rgba != NULL && image_c == NULL)
        warn |= MEL_NOTIF_WARN_IMAGE_DROPPED;
    if (c->action_count > 0)
        warn |= MEL_NOTIF_WARN_ACTIONS_DROPPED;
    if (c->sound_path.len > 0)
        warn |= MEL_NOTIF_WARN_SOUND_DROPPED;
    if (c->progress.present)
        warn |= MEL_NOTIF_WARN_PROGRESS_DROPPED;
    if (c->has_badge)
        warn |= MEL_NOTIF_WARN_BADGE_DROPPED;

    f64 delay_ms = 0;
    f64 interval_ms = 0;
    if (lw->scheduled)
    {
        if (lw->trigger.at_unix_ms > 0)
        {
            f64 delta = (f64)lw->trigger.at_unix_ms - mel_notif_js_now();
            delay_ms = delta > 0 ? delta : 1;
        }
        interval_ms = (f64)lw->trigger.interval_ms;
    }

    mel_notif_js_show((u32)(lw->token & 0xffffffffu), (u32)(lw->token >> 32), title_c, body_c, icon_c, image_c, c->icon.rgba, c->icon.width, c->icon.height, c->silent ? 1 : 0, delay_ms, interval_ms);

    cstr_free(title_c);
    cstr_free(body_c);
    cstr_free(icon_c);
    cstr_free(image_c);
    if (body_s.data != NULL)
        mel_dealloc(a, body_s.data);
    return warn != 0 ? (MEL_NOTIF_WARNED | warn) : MEL_NOTIF_OK;
}

static void web_cancel(void* user, u64 token)
{
    MEL_UNUSED(user);
    mel_notif_js_cancel((u32)(token & 0xffffffffu), (u32)(token >> 32));
}

static void web_cancel_all(void* user)
{
    MEL_UNUSED(user);
    mel_notif_js_cancel_all();
}

void mel_notif__register_host_providers(void)
{
    static const Mel_Notif_Provider_Desc desc = {
        .name = "web-notification-api",
        .supported = web_supported,
        .caps = web_caps,
        .authorization = web_authorization,
        .authorize = web_authorize,
        .post = web_post,
        .update = web_post,
        .cancel = web_cancel,
        .cancel_all = web_cancel_all,
    };
    mel_notif_provider_register(&desc);
}
