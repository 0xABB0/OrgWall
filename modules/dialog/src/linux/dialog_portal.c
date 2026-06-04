#include "dialog_linux.h"

#include <dialog/backend.h>
#include <window/window.h>
#include <log/log.h>

#include <allocator/allocator.h>

#include <dbus/dbus.h>

#include <stdio.h>
#include <string.h>

#define PORTAL_BUS    "org.freedesktop.portal.Desktop"
#define PORTAL_PATH   "/org/freedesktop/portal/desktop"
#define PORTAL_IFACE  "org.freedesktop.portal.FileChooser"
#define REQUEST_IFACE "org.freedesktop.portal.Request"

typedef struct
{
    u64   token;
    char* request_path;
    bool  done;
} Portal_Call;

static char* uri_to_path(const Mel_Alloc* a, const char* uri)
{
    const char* p = uri;
    if (strncmp(uri, "file://", 7) == 0)
    {
        p = uri + 7;
        const char* slash = strchr(p, '/');
        if (slash)
            p = slash;
    }
    usize n = strlen(p);
    char* out = (char*)mel_alloc(a, n + 1);
    if (!out)
        return NULL;
    usize w = 0;
    for (usize i = 0; i < n; i++)
    {
        if (p[i] == '%' && i + 2 < n)
        {
            char hi = p[i + 1], lo = p[i + 2];
            int  hv = (hi >= '0' && hi <= '9') ? hi - '0' : (hi | 0x20) - 'a' + 10;
            int  lv = (lo >= '0' && lo <= '9') ? lo - '0' : (lo | 0x20) - 'a' + 10;
            if (hv >= 0 && hv < 16 && lv >= 0 && lv < 16)
            {
                out[w++] = (char)(hv * 16 + lv);
                i += 2;
                continue;
            }
        }
        out[w++] = p[i];
    }
    out[w] = 0;
    return out;
}

static void dict_bool(DBusMessageIter* dict, const char* key, dbus_bool_t value)
{
    DBusMessageIter entry, variant;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static void dict_string(DBusMessageIter* dict, const char* key, const char* value)
{
    DBusMessageIter entry, variant;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static void dict_filters(DBusMessageIter* dict, Mel_Dialog_Job* job)
{
    u32 fc = mel_dialog_job_filter_count(job);
    if (fc == 0)
        return;
    DBusMessageIter entry, variant, arr, filt, plist, pentry;
    const char*     key = "filters";
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "a(sa(us))", &variant);
    dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "(sa(us))", &arr);
    for (u32 i = 0; i < fc; i++)
    {
        dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &filt);
        const char* label = mel_dialog_job_filter_label(job, i);
        const char* lbl = label ? label : "Files";
        dbus_message_iter_append_basic(&filt, DBUS_TYPE_STRING, &lbl);
        dbus_message_iter_open_container(&filt, DBUS_TYPE_ARRAY, "(us)", &plist);
        u32 pc = mel_dialog_job_filter_pattern_count(job, i);
        for (u32 p = 0; p < pc; p++)
        {
            const char* pat = mel_dialog_job_filter_pattern(job, i, p);
            if (!pat)
                continue;
            dbus_message_iter_open_container(&plist, DBUS_TYPE_STRUCT, NULL, &pentry);
            dbus_uint32_t kind = 0;
            char          glob[280];
            const char*   ext = strrchr(pat, '.');
            if (strchr(pat, '*'))
                snprintf(glob, sizeof glob, "%s", pat);
            else if (ext && ext[1])
                snprintf(glob, sizeof glob, "*%s", ext);
            else
                snprintf(glob, sizeof glob, "*.%s", pat);
            const char* g = glob;
            dbus_message_iter_append_basic(&pentry, DBUS_TYPE_UINT32, &kind);
            dbus_message_iter_append_basic(&pentry, DBUS_TYPE_STRING, &g);
            dbus_message_iter_close_container(&plist, &pentry);
        }
        dbus_message_iter_close_container(&filt, &plist);
        dbus_message_iter_close_container(&arr, &filt);
    }
    dbus_message_iter_close_container(&variant, &arr);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static void parse_response(Mel_Dialog_Job* job, DBusMessage* sig)
{
    const Mel_Alloc* a = mel_dialog_job_alloc(job);
    DBusMessageIter  it;
    if (!dbus_message_iter_init(sig, &it))
    {
        mel_dialog_job_resolve(job, MEL_DIALOG_OK | MEL_DIALOG_CANCELLED);
        return;
    }
    dbus_uint32_t response = 1;
    dbus_message_iter_get_basic(&it, &response);
    if (response != 0)
    {
        mel_dialog_job_resolve(job, MEL_DIALOG_OK | MEL_DIALOG_CANCELLED);
        return;
    }
    dbus_message_iter_next(&it);

    DBusMessageIter results;
    dbus_message_iter_recurse(&it, &results);
    u32 emitted = 0;
    while (dbus_message_iter_get_arg_type(&results) == DBUS_TYPE_DICT_ENTRY)
    {
        DBusMessageIter entry, var;
        dbus_message_iter_recurse(&results, &entry);
        const char* key = NULL;
        dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);
        dbus_message_iter_recurse(&entry, &var);
        if (key && strcmp(key, "uris") == 0)
        {
            DBusMessageIter uris;
            dbus_message_iter_recurse(&var, &uris);
            while (dbus_message_iter_get_arg_type(&uris) == DBUS_TYPE_STRING)
            {
                const char* uri = NULL;
                dbus_message_iter_get_basic(&uris, &uri);
                if (uri)
                {
                    char* path = uri_to_path(a, uri);
                    if (path)
                    {
                        mel_dialog_job_emit_path(job, path);
                        mel_dealloc(a, path);
                        emitted++;
                    }
                }
                dbus_message_iter_next(&uris);
            }
        }
        dbus_message_iter_next(&results);
    }
    mel_dialog_job_resolve(job, emitted ? MEL_DIALOG_OK : (MEL_DIALOG_OK | MEL_DIALOG_CANCELLED));
}

static DBusHandlerResult on_signal(DBusConnection* conn, DBusMessage* msg, void* user)
{
    Portal_Call* call = (Portal_Call*)user;
    (void)conn;
    if (dbus_message_is_signal(msg, REQUEST_IFACE, "Response") && call->request_path && dbus_message_has_path(msg, call->request_path))
    {
        Mel_Dialog_Job* job = mel_dialog__job_from_token(call->token);
        if (job)
            parse_response(job, msg);
        call->done = true;
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

bool mel_dialog__portal_run(Mel_Dialog_Job* job)
{
    const Mel_Alloc* a = mel_dialog_job_alloc(job);
    DBusError        err;
    dbus_error_init(&err);
    DBusConnection* conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!conn || dbus_error_is_set(&err))
    {
        if (dbus_error_is_set(&err))
            dbus_error_free(&err);
        return false;
    }
    dbus_connection_set_exit_on_disconnect(conn, FALSE);

    u32         request = mel_dialog_job_request(job);
    const char* method = (request & MEL_DIALOG_REQUEST_SAVE_FILE) ? "SaveFile" : "OpenFile";
    DBusMessage* call = dbus_message_new_method_call(PORTAL_BUS, PORTAL_PATH, PORTAL_IFACE, method);
    if (!call)
        return false;

    char token_str[64];
    snprintf(token_str, sizeof token_str, "mel_dialog_%llu", (unsigned long long)mel_dialog_job_token(job));

    DBusMessageIter args;
    dbus_message_iter_init_append(call, &args);
    const char* parent = "";
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &parent);
    const char* title = mel_dialog_job_title(job);
    const char* t = title ? title : ((request & MEL_DIALOG_REQUEST_OPEN_DIR) ? "Select Folder" : "Select File");
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &t);

    DBusMessageIter dict;
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dict);
    dict_string(&dict, "handle_token", token_str);
    if (request & MEL_DIALOG_REQUEST_MULTI)
        dict_bool(&dict, "multiple", TRUE);
    if (request & MEL_DIALOG_REQUEST_OPEN_DIR)
        dict_bool(&dict, "directory", TRUE);
    if (request & MEL_DIALOG_REQUEST_SAVE_FILE)
    {
        const char* name = mel_dialog_job_default_name(job);
        if (name)
            dict_string(&dict, "current_name", name);
    }
    if (!(request & MEL_DIALOG_REQUEST_OPEN_DIR))
        dict_filters(&dict, job);
    dbus_message_iter_close_container(&args, &dict);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, call, 10000, &err);
    dbus_message_unref(call);
    if (!reply || dbus_error_is_set(&err))
    {
        if (dbus_error_is_set(&err))
            dbus_error_free(&err);
        if (reply)
            dbus_message_unref(reply);
        return false;
    }

    const char* req_path = NULL;
    DBusError   perr;
    dbus_error_init(&perr);
    if (!dbus_message_get_args(reply, &perr, DBUS_TYPE_OBJECT_PATH, &req_path, DBUS_TYPE_INVALID) || !req_path)
    {
        dbus_message_unref(reply);
        if (dbus_error_is_set(&perr))
            dbus_error_free(&perr);
        return false;
    }

    Portal_Call* pc = mel_alloc_type(a, Portal_Call);
    memset(pc, 0, sizeof *pc);
    pc->token = mel_dialog_job_token(job);
    pc->request_path = (char*)mel_alloc(a, strlen(req_path) + 1);
    strcpy(pc->request_path, req_path);
    dbus_message_unref(reply);

    char rule[512];
    snprintf(rule, sizeof rule, "type='signal',interface='%s',path='%s'", REQUEST_IFACE, pc->request_path);
    dbus_bus_add_match(conn, rule, &err);
    dbus_connection_flush(conn);
    dbus_connection_add_filter(conn, on_signal, pc, NULL);

    while (!pc->done && dbus_connection_read_write_dispatch(conn, 200))
        ;

    dbus_connection_remove_filter(conn, on_signal, pc);
    dbus_bus_remove_match(conn, rule, NULL);
    if (dbus_error_is_set(&err))
        dbus_error_free(&err);
    mel_dealloc(a, pc->request_path);
    mel_dealloc(a, pc);
    return true;
}
