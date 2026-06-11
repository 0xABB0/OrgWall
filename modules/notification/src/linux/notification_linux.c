#include <notification/notification.h>
#include <notification/provider.h>
#include <notification/linux/linux.h>

#include <allocator/allocator.h>
#include <collection/array.h>
#include <collection/slotmap.h>
#include <log/log.h>

#include <dbus/dbus.h>

#include <string.h>
#include <time.h>

#include "../notification_internal.h"

#define NOTIFY_BUS   "org.freedesktop.Notifications"
#define NOTIFY_PATH  "/org/freedesktop/Notifications"
#define NOTIFY_IFACE "org.freedesktop.Notifications"

typedef struct
{
    u64           token;
    dbus_uint32_t id;
} Id_Map;

typedef struct
{
    u64 token;
    u64 due_unix_ms;
    u64 interval_ms;
} Pending;

typedef struct
{
    DBusConnection* conn;
    bool            conn_failed;
    bool            filter_added;
    Mel_Array(Id_Map) ids;
    Mel_Array(Pending) pending;
    bool arrays_init;
} Linux_State;

static Linux_State lx;

static u64 now_unix_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (u64)ts.tv_sec * 1000u + (u64)ts.tv_nsec / 1000000u;
}

static void ensure_arrays(void)
{
    if (lx.arrays_init)
        return;
    mel_array_init(&lx.ids, mel_notif__alloc());
    mel_array_init(&lx.pending, mel_notif__alloc());
    lx.arrays_init = true;
}

static Id_Map* id_by_token(u64 token)
{
    for (usize i = 0; i < lx.ids.count; i++)
        if (lx.ids.items[i].token == token)
            return &lx.ids.items[i];
    return NULL;
}

static u64 token_by_id(dbus_uint32_t id)
{
    for (usize i = 0; i < lx.ids.count; i++)
        if (lx.ids.items[i].id == id)
            return lx.ids.items[i].token;
    return 0;
}

static void id_remove(u64 token)
{
    for (usize i = 0; i < lx.ids.count; i++)
    {
        if (lx.ids.items[i].token == token)
        {
            lx.ids.items[i] = lx.ids.items[lx.ids.count - 1];
            lx.ids.count--;
            return;
        }
    }
}

static void pending_remove(u64 token)
{
    for (usize i = 0; i < lx.pending.count; i++)
    {
        if (lx.pending.items[i].token == token)
        {
            lx.pending.items[i] = lx.pending.items[lx.pending.count - 1];
            lx.pending.count--;
            return;
        }
    }
}

static DBusHandlerResult on_signal(DBusConnection* conn, DBusMessage* msg, void* user)
{
    MEL_UNUSED(conn);
    MEL_UNUSED(user);
    if (dbus_message_is_signal(msg, NOTIFY_IFACE, "ActionInvoked"))
    {
        dbus_uint32_t id = 0;
        const char*   key = NULL;
        if (dbus_message_get_args(msg, NULL, DBUS_TYPE_UINT32, &id, DBUS_TYPE_STRING, &key, DBUS_TYPE_INVALID) && key != NULL)
        {
            u64  token = token_by_id(id);
            str8 action = strcmp(key, "default") == 0 ? STR8_EMPTY : (str8){ (u8*)key, (size)strlen(key) };
            mel_notif__dispatch_activated(token, action, STR8_EMPTY, STR8_EMPTY);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_signal(msg, NOTIFY_IFACE, "NotificationClosed"))
    {
        dbus_uint32_t id = 0, reason = 0;
        if (dbus_message_get_args(msg, NULL, DBUS_TYPE_UINT32, &id, DBUS_TYPE_UINT32, &reason, DBUS_TYPE_INVALID))
        {
            u64 token = token_by_id(id);
            if (token != 0 && (reason == 1 || reason == 2))
                mel_notif__dispatch_dismissed(token);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusConnection* ensure_conn(void)
{
    if (lx.conn != NULL || lx.conn_failed)
        return lx.conn;
    DBusError err;
    dbus_error_init(&err);
    DBusConnection* conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (conn == NULL || dbus_error_is_set(&err))
    {
        if (dbus_error_is_set(&err))
        {
            mel_log_warn("notification", "session bus unavailable: %s", err.message != NULL ? err.message : "?");
            dbus_error_free(&err);
        }
        lx.conn_failed = true;
        return NULL;
    }
    dbus_connection_set_exit_on_disconnect(conn, FALSE);
    dbus_bus_add_match(conn, "type='signal',interface='" NOTIFY_IFACE "',member='ActionInvoked'", NULL);
    dbus_bus_add_match(conn, "type='signal',interface='" NOTIFY_IFACE "',member='NotificationClosed'", NULL);
    dbus_connection_add_filter(conn, on_signal, NULL, NULL);
    dbus_connection_flush(conn);
    lx.filter_added = true;
    lx.conn = conn;
    return conn;
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

static void hint_u8(DBusMessageIter* dict, const char* key, unsigned char value)
{
    DBusMessageIter entry, variant;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "y", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BYTE, &value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static void hint_bool(DBusMessageIter* dict, const char* key, dbus_bool_t value)
{
    DBusMessageIter entry, variant;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static void hint_string(DBusMessageIter* dict, const char* key, const char* value)
{
    DBusMessageIter entry, variant;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static void hint_image_data(DBusMessageIter* dict, const Mel_Notif_Image* img)
{
    DBusMessageIter entry, variant, st, arr;
    const char*     key = "image-data";
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "(iiibiiay)", &variant);
    dbus_message_iter_open_container(&variant, DBUS_TYPE_STRUCT, NULL, &st);
    dbus_int32_t w = (dbus_int32_t)img->width;
    dbus_int32_t h = (dbus_int32_t)img->height;
    dbus_int32_t stride = w * 4;
    dbus_bool_t  has_alpha = TRUE;
    dbus_int32_t bits = 8;
    dbus_int32_t channels = 4;
    dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &w);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &h);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &stride);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_BOOLEAN, &has_alpha);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &bits);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &channels);
    dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "y", &arr);
    int n = w * h * 4;
    dbus_message_iter_append_fixed_array(&arr, DBUS_TYPE_BYTE, &img->rgba, n);
    dbus_message_iter_close_container(&st, &arr);
    dbus_message_iter_close_container(&variant, &st);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static Mel_Notif_Status send_notify(const Mel_Notif_Lowered* lw)
{
    DBusConnection* conn = ensure_conn();
    if (conn == NULL)
        return MEL_NOTIF_ERROR | MEL_NOTIF_ERR_BACKEND_FAIL;
    ensure_arrays();

    const Mel_Notif_Content* c = lw->content;
    const Mel_Alloc*         a = mel_notif__alloc();
    Mel_Notif_Status         warn = 0;

    DBusMessage* call = dbus_message_new_method_call(NOTIFY_BUS, NOTIFY_PATH, NOTIFY_IFACE, "Notify");
    if (call == NULL)
        return MEL_NOTIF_ERROR | MEL_NOTIF_ERR_BACKEND_FAIL;

    const char* icon_c = cstr(c->icon.path);
    const char* title_c = cstr(c->title);
    str8        body_s = c->subtitle.len > 0 ? str8_fmt_alloc(a, "%.*s\n%.*s", (int)c->subtitle.len, c->subtitle.data, (int)c->body.len, c->body.data) : STR8_EMPTY;
    const char* body_c = c->subtitle.len > 0 ? cstr(body_s) : cstr(c->body);

    DBusMessageIter args;
    dbus_message_iter_init_append(call, &args);
    const char* app_name = "";
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &app_name);
    Id_Map*       existing = id_by_token(lw->token);
    dbus_uint32_t replaces = existing != NULL ? existing->id : 0;
    dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &replaces);
    const char* app_icon = icon_c != NULL ? icon_c : "";
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &app_icon);
    const char* summary = title_c != NULL ? title_c : "";
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &summary);
    const char* body = body_c != NULL ? body_c : "";
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &body);

    DBusMessageIter actions;
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "s", &actions);
    const char* def_key = "default";
    const char* def_label = "Open";
    dbus_message_iter_append_basic(&actions, DBUS_TYPE_STRING, &def_key);
    dbus_message_iter_append_basic(&actions, DBUS_TYPE_STRING, &def_label);
    for (u32 i = 0; i < c->action_count; i++)
    {
        const char* k = cstr(c->actions[i].id);
        const char* l = cstr(c->actions[i].label);
        const char* ks = k != NULL ? k : "";
        const char* ls = l != NULL ? l : "";
        dbus_message_iter_append_basic(&actions, DBUS_TYPE_STRING, &ks);
        dbus_message_iter_append_basic(&actions, DBUS_TYPE_STRING, &ls);
        cstr_free(k);
        cstr_free(l);
        if ((c->actions[i].flags & MEL_NOTIF_ACTION_TEXT_INPUT) != 0)
            warn |= MEL_NOTIF_WARN_REPLY_DROPPED;
    }
    dbus_message_iter_close_container(&args, &actions);

    DBusMessageIter hints;
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &hints);
    hint_u8(&hints, "urgency", 1);
    const char* sound_c = NULL;
    if (c->silent)
        hint_bool(&hints, "suppress-sound", TRUE);
    else if (c->sound_path.len > 0)
    {
        sound_c = cstr(c->sound_path);
        hint_string(&hints, "sound-file", sound_c);
    }
    const char* image_c = NULL;
    if (c->attachment.path.len > 0)
    {
        image_c = cstr(c->attachment.path);
        hint_string(&hints, "image-path", image_c);
    }
    else if (c->attachment.rgba != NULL)
        hint_image_data(&hints, &c->attachment);
    dbus_message_iter_close_container(&args, &hints);

    dbus_int32_t expire = -1;
    dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &expire);

    DBusError err;
    dbus_error_init(&err);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, call, 2000, &err);
    dbus_message_unref(call);
    cstr_free(icon_c);
    cstr_free(title_c);
    cstr_free(body_c);
    cstr_free(sound_c);
    cstr_free(image_c);
    if (body_s.data != NULL)
        mel_dealloc(a, body_s.data);

    if (reply == NULL || dbus_error_is_set(&err))
    {
        mel_log_error("notification", "Notify failed: %s", dbus_error_is_set(&err) ? err.message : "?");
        if (dbus_error_is_set(&err))
            dbus_error_free(&err);
        if (reply != NULL)
            dbus_message_unref(reply);
        return MEL_NOTIF_ERROR | MEL_NOTIF_ERR_BACKEND_FAIL;
    }
    dbus_uint32_t id = 0;
    dbus_message_get_args(reply, NULL, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID);
    dbus_message_unref(reply);

    if (existing != NULL)
        existing->id = id;
    else
    {
        Id_Map m = { .token = lw->token, .id = id };
        mel_array_push(&lx.ids, m);
    }

    if (c->progress.present)
        warn |= MEL_NOTIF_WARN_PROGRESS_DROPPED;
    if (c->has_badge)
        warn |= MEL_NOTIF_WARN_BADGE_DROPPED;
    return warn != 0 ? (MEL_NOTIF_WARNED | warn) : MEL_NOTIF_OK;
}

static void pump_pending(void)
{
    if (!lx.arrays_init)
        return;
    u64 now = now_unix_ms();
    for (usize i = 0; i < lx.pending.count;)
    {
        Pending* p = &lx.pending.items[i];
        if (p->due_unix_ms > now)
        {
            i++;
            continue;
        }
        Mel_SlotMap_Handle h = mel_slotmap_handle_unpack64(p->token);
        Notif_Slot*        ns = mel_notif__slot(h);
        if (ns != NULL)
        {
            Mel_Notif_Lowered lw = { .token = p->token, .content = &ns->content, .trigger = ns->trigger, .scheduled = false };
            send_notify(&lw);
        }
        if (ns != NULL && p->interval_ms > 0)
        {
            p->due_unix_ms = now + p->interval_ms;
            i++;
        }
        else
        {
            lx.pending.items[i] = lx.pending.items[lx.pending.count - 1];
            lx.pending.count--;
        }
    }
}

void mel_notif_linux_pump(void)
{
    if (lx.conn != NULL)
        dbus_connection_read_write_dispatch(lx.conn, 0);
    pump_pending();
}

static bool linux_supported(void* user)
{
    MEL_UNUSED(user);
    return ensure_conn() != NULL;
}

static Mel_Notif_Caps linux_caps(void* user)
{
    MEL_UNUSED(user);
    return MEL_NOTIF_CAP_ACTIONS | MEL_NOTIF_CAP_ICON | MEL_NOTIF_CAP_ATTACHMENT | MEL_NOTIF_CAP_SOUND | MEL_NOTIF_CAP_SCHEDULE | MEL_NOTIF_CAP_REPEAT | MEL_NOTIF_CAP_UPDATE;
}

static Mel_Notif_Status linux_post(void* user, const Mel_Notif_Lowered* lw)
{
    MEL_UNUSED(user);
    mel_notif_linux_pump();
    if (lw->scheduled)
    {
        ensure_arrays();
        u64     due = lw->trigger.at_unix_ms != 0 ? lw->trigger.at_unix_ms : now_unix_ms() + lw->trigger.interval_ms;
        Pending p = { .token = lw->token, .due_unix_ms = due, .interval_ms = lw->trigger.interval_ms };
        mel_array_push(&lx.pending, p);
        return MEL_NOTIF_OK;
    }
    return send_notify(lw);
}

static void linux_cancel(void* user, u64 token)
{
    MEL_UNUSED(user);
    mel_notif_linux_pump();
    pending_remove(token);
    Id_Map* m = id_by_token(token);
    if (m == NULL)
        return;
    DBusConnection* conn = ensure_conn();
    if (conn != NULL)
    {
        DBusMessage* call = dbus_message_new_method_call(NOTIFY_BUS, NOTIFY_PATH, NOTIFY_IFACE, "CloseNotification");
        if (call != NULL)
        {
            dbus_message_append_args(call, DBUS_TYPE_UINT32, &m->id, DBUS_TYPE_INVALID);
            dbus_connection_send(conn, call, NULL);
            dbus_connection_flush(conn);
            dbus_message_unref(call);
        }
    }
    id_remove(token);
}

static void linux_cancel_all(void* user)
{
    if (!lx.arrays_init)
        return;
    while (lx.ids.count > 0)
        linux_cancel(user, lx.ids.items[0].token);
    lx.pending.count = 0;
}

static void linux_shutdown(void* user)
{
    MEL_UNUSED(user);
    if (lx.conn != NULL)
    {
        if (lx.filter_added)
            dbus_connection_remove_filter(lx.conn, on_signal, NULL);
        dbus_connection_unref(lx.conn);
    }
    if (lx.arrays_init)
    {
        mel_array_free(&lx.ids);
        mel_array_free(&lx.pending);
    }
    memset(&lx, 0, sizeof lx);
}

void mel_notif__register_host_providers(void)
{
    static const Mel_Notif_Provider_Desc desc = {
        .name = "linux-fdo-notifications",
        .supported = linux_supported,
        .caps = linux_caps,
        .post = linux_post,
        .update = linux_post,
        .cancel = linux_cancel,
        .cancel_all = linux_cancel_all,
        .shutdown = linux_shutdown,
    };
    mel_notif_provider_register(&desc);
}
