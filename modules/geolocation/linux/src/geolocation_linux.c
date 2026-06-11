#include <geolocation/provider.h>

#include <debug/assert.h>
#include <log/log.h>
#include <time/nano.h>
#include <vat/vat.h>

#include "geolocation_dbus.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define GEO_GC_BUS       "org.freedesktop.GeoClue2"
#define GEO_GC_MANAGER   "/org/freedesktop/GeoClue2/Manager"
#define GEO_GC_MANAGER_I "org.freedesktop.GeoClue2.Manager"
#define GEO_GC_CLIENT_I  "org.freedesktop.GeoClue2.Client"
#define GEO_GC_LOC_I     "org.freedesktop.GeoClue2.Location"
#define GEO_PROPS_I      "org.freedesktop.DBus.Properties"
#define GEO_CALL_TIMEOUT 25000

#define GEO_GC_LEVEL_COUNTRY      1
#define GEO_GC_LEVEL_CITY         4
#define GEO_GC_LEVEL_NEIGHBORHOOD 5
#define GEO_GC_LEVEL_STREET       6
#define GEO_GC_LEVEL_EXACT        8

typedef u32 Geo_Gc_State;

#define GEO_GC_IDLE           0u
#define GEO_GC_GETTING_CLIENT 1u
#define GEO_GC_CONFIGURING    2u
#define GEO_GC_STARTING       3u
#define GEO_GC_RUNNING        4u

static Geo_DBus                     g_dbus;
static const Mel_Geo_Provider_Sink* g_sink;
static DBusConnection*              g_conn;
static Mel_Vat_Source*              g_source;
static Mel_Vat_Wakeable             g_wakeable;
static char                         g_client_path[256];
static Geo_Gc_State                 g_state;
static Mel_Geo_Demand               g_demand;
static bool                         g_want_stream;
static Mel_Future*                  g_auth_future;
static const mel_geo_auth*          g_auth_state = &mel_geo_auth_not_determined;
static Mel_Geo_Request*             g_pending;
static Mel_Geo_Fix                  g_last_fix;
static bool                         g_have_last;

static u32 geo_gc__level(f64 accuracy_m)
{
    if (accuracy_m <= 10.0)
        return GEO_GC_LEVEL_EXACT;
    if (accuracy_m <= 100.0)
        return GEO_GC_LEVEL_STREET;
    if (accuracy_m <= 1000.0)
        return GEO_GC_LEVEL_NEIGHBORHOOD;
    if (accuracy_m <= 10000.0)
        return GEO_GC_LEVEL_CITY;
    return GEO_GC_LEVEL_COUNTRY;
}

static void geo_gc__satisfy_pending(const Mel_Geo_Fix* fix)
{
    Mel_Geo_Request** pp = &g_pending;
    while (*pp != NULL)
    {
        Mel_Geo_Request* req = *pp;
        bool sharp = (fix->valid & MEL_GEO_VALID_HACC) && fix->horizontal_accuracy_m <= req->accuracy_m;
        if (sharp || !(fix->valid & MEL_GEO_VALID_HACC))
        {
            *pp = req->provider_next;
            req->provider_next = NULL;
            g_sink->on_request(req, fix, &mel_geo_ok);
        }
        else
            pp = &req->provider_next;
    }
}

static void geo_gc__fail_pending(const mel_geo_result* r)
{
    Mel_Geo_Request* req = g_pending;
    g_pending = NULL;
    while (req != NULL)
    {
        Mel_Geo_Request* next = req->provider_next;
        req->provider_next = NULL;
        g_sink->on_request(req, NULL, r);
        req = next;
    }
}

static void geo_gc__resolve_auth(const mel_geo_auth* auth)
{
    g_auth_state = auth;
    if (g_auth_future == NULL)
        return;
    Mel_Future* f = g_auth_future;
    g_auth_future = NULL;
    g_sink->on_auth(f, auth);
}

static void geo_gc__stopped(const mel_geo_result* why)
{
    g_state = GEO_GC_IDLE;
    g_client_path[0] = 0;
    if (why != NULL)
    {
        g_sink->on_stream_result(why);
        geo_gc__fail_pending(why);
        if (why == &mel_geo_denied)
            geo_gc__resolve_auth(&mel_geo_auth_denied);
    }
}

static void geo_gc__location_fetched(DBusPendingCall* pending, void* user)
{
    (void)user;
    DBusMessage* reply = g_dbus.pending_call_steal_reply(pending);
    g_dbus.pending_call_unref(pending);
    if (reply == NULL)
        return;
    if (g_dbus.message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR)
    {
        g_dbus.message_unref(reply);
        return;
    }

    Mel_Geo_Fix fix = { .monotonic_ns = mel_nanos_since_unspecified_epoch(), .valid = MEL_GEO_VALID_MONOTONIC };

    DBusMessageIter it, dict;
    if (!g_dbus.message_iter_init(reply, &it) || g_dbus.message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY)
    {
        g_dbus.message_unref(reply);
        return;
    }
    g_dbus.message_iter_recurse(&it, &dict);
    while (g_dbus.message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY)
    {
        DBusMessageIter entry, var;
        g_dbus.message_iter_recurse(&dict, &entry);
        const char* key = NULL;
        g_dbus.message_iter_get_basic(&entry, &key);
        g_dbus.message_iter_next(&entry);
        g_dbus.message_iter_recurse(&entry, &var);
        int type = g_dbus.message_iter_get_arg_type(&var);
        if (type == DBUS_TYPE_DOUBLE)
        {
            double v = 0.0;
            g_dbus.message_iter_get_basic(&var, &v);
            if (strcmp(key, "Latitude") == 0)
            {
                fix.latitude_deg = v;
                fix.valid |= MEL_GEO_VALID_POSITION;
            }
            else if (strcmp(key, "Longitude") == 0)
                fix.longitude_deg = v;
            else if (strcmp(key, "Accuracy") == 0 && v >= 0.0)
            {
                fix.horizontal_accuracy_m = v;
                fix.valid |= MEL_GEO_VALID_HACC;
            }
            else if (strcmp(key, "Altitude") == 0 && v > -1e8)
            {
                fix.altitude_m = v;
                fix.valid |= MEL_GEO_VALID_ALTITUDE;
            }
            else if (strcmp(key, "Speed") == 0 && v >= 0.0)
            {
                fix.speed_mps = v;
                fix.valid |= MEL_GEO_VALID_SPEED;
            }
            else if (strcmp(key, "Heading") == 0 && v >= 0.0)
            {
                fix.course_deg = v;
                fix.valid |= MEL_GEO_VALID_COURSE;
            }
        }
        else if (type == DBUS_TYPE_STRUCT && strcmp(key, "Timestamp") == 0)
        {
            DBusMessageIter ts;
            g_dbus.message_iter_recurse(&var, &ts);
            dbus_uint64_t sec = 0, usec = 0;
            if (g_dbus.message_iter_get_arg_type(&ts) == DBUS_TYPE_UINT64)
            {
                g_dbus.message_iter_get_basic(&ts, &sec);
                g_dbus.message_iter_next(&ts);
                if (g_dbus.message_iter_get_arg_type(&ts) == DBUS_TYPE_UINT64)
                    g_dbus.message_iter_get_basic(&ts, &usec);
                fix.utc_unix_ms = (u64)sec * 1000u + (u64)usec / 1000u;
                fix.valid |= MEL_GEO_VALID_UTC;
            }
        }
        g_dbus.message_iter_next(&dict);
    }
    g_dbus.message_unref(reply);

    if (!(fix.valid & MEL_GEO_VALID_POSITION))
        return;
    g_last_fix = fix;
    g_have_last = true;
    geo_gc__resolve_auth(g_auth_state == &mel_geo_auth_not_determined ? &mel_geo_auth_granted_in_use : g_auth_state);
    if (g_want_stream)
        g_sink->on_fix(&fix);
    geo_gc__satisfy_pending(&fix);
}

static void geo_gc__fetch_location(const char* location_path)
{
    DBusMessage* msg = g_dbus.message_new_method_call(GEO_GC_BUS, location_path, GEO_PROPS_I, "GetAll");
    const char*  iface = GEO_GC_LOC_I;
    g_dbus.message_append_args(msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID);
    DBusPendingCall* pending = NULL;
    if (g_dbus.connection_send_with_reply(g_conn, msg, &pending, GEO_CALL_TIMEOUT) && pending != NULL)
        g_dbus.pending_call_set_notify(pending, geo_gc__location_fetched, NULL, NULL);
    g_dbus.message_unref(msg);
    g_dbus.connection_flush(g_conn);
}

static int geo_gc__filter(DBusConnection* conn, DBusMessage* msg, void* user)
{
    (void)conn;
    (void)user;
    if (g_dbus.message_is_signal(msg, GEO_GC_CLIENT_I, "LocationUpdated"))
    {
        const char* old_path = NULL;
        const char* new_path = NULL;
        if (g_dbus.message_get_args(msg, NULL, DBUS_TYPE_OBJECT_PATH, &old_path, DBUS_TYPE_OBJECT_PATH, &new_path, DBUS_TYPE_INVALID))
            geo_gc__fetch_location(new_path);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void geo_gc__set_prop_u32(const char* prop, u32 value)
{
    DBusMessage*    msg = g_dbus.message_new_method_call(GEO_GC_BUS, g_client_path, GEO_PROPS_I, "Set");
    const char*     iface = GEO_GC_CLIENT_I;
    DBusMessageIter it, var;
    g_dbus.message_iter_init_append(msg, &it);
    g_dbus.message_iter_append_basic(&it, DBUS_TYPE_STRING, &iface);
    g_dbus.message_iter_append_basic(&it, DBUS_TYPE_STRING, &prop);
    g_dbus.message_iter_open_container(&it, DBUS_TYPE_VARIANT, "u", &var);
    g_dbus.message_iter_append_basic(&var, DBUS_TYPE_UINT32, &value);
    g_dbus.message_iter_close_container(&it, &var);
    g_dbus.connection_send(g_conn, msg, NULL);
    g_dbus.message_unref(msg);
}

static void geo_gc__set_prop_str(const char* prop, const char* value)
{
    DBusMessage*    msg = g_dbus.message_new_method_call(GEO_GC_BUS, g_client_path, GEO_PROPS_I, "Set");
    const char*     iface = GEO_GC_CLIENT_I;
    DBusMessageIter it, var;
    g_dbus.message_iter_init_append(msg, &it);
    g_dbus.message_iter_append_basic(&it, DBUS_TYPE_STRING, &iface);
    g_dbus.message_iter_append_basic(&it, DBUS_TYPE_STRING, &prop);
    g_dbus.message_iter_open_container(&it, DBUS_TYPE_VARIANT, "s", &var);
    g_dbus.message_iter_append_basic(&var, DBUS_TYPE_STRING, &value);
    g_dbus.message_iter_close_container(&it, &var);
    g_dbus.connection_send(g_conn, msg, NULL);
    g_dbus.message_unref(msg);
}

static void geo_gc__configure(void)
{
    char desktop_id[64];
    snprintf(desktop_id, sizeof desktop_id, "melody-%d", (int)getpid());
    geo_gc__set_prop_str("DesktopId", desktop_id);
    geo_gc__set_prop_u32("RequestedAccuracyLevel", geo_gc__level(g_demand.accuracy_m));
    geo_gc__set_prop_u32("DistanceThreshold", g_demand.min_distance_m > 0.0 ? (u32)g_demand.min_distance_m : 0u);
    u32 time_threshold_s = g_demand.min_interval_ns > 0 ? (u32)(g_demand.min_interval_ns / 1000000000ll) : 0u;
    geo_gc__set_prop_u32("TimeThreshold", time_threshold_s);
    g_dbus.connection_flush(g_conn);
}

static void geo_gc__started(DBusPendingCall* pending, void* user)
{
    (void)user;
    DBusMessage* reply = g_dbus.pending_call_steal_reply(pending);
    g_dbus.pending_call_unref(pending);
    if (reply == NULL)
        return;
    bool error = g_dbus.message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR;
    const char* error_name = error ? g_dbus.message_get_error_name(reply) : NULL;
    bool denied = error_name != NULL && strstr(error_name, "AccessDenied") != NULL;
    g_dbus.message_unref(reply);
    if (error)
    {
        mel_log_warn("geo", "geoclue client start failed: %s", error_name != NULL ? error_name : "unknown");
        geo_gc__stopped(denied ? &mel_geo_denied : &mel_geo_unavailable);
        return;
    }
    g_state = GEO_GC_RUNNING;
    g_sink->on_stream_result(&mel_geo_ok);
    geo_gc__resolve_auth(&mel_geo_auth_granted_in_use);
}

static void geo_gc__client_got(DBusPendingCall* pending, void* user)
{
    (void)user;
    DBusMessage* reply = g_dbus.pending_call_steal_reply(pending);
    g_dbus.pending_call_unref(pending);
    if (reply == NULL || g_dbus.message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR)
    {
        if (reply != NULL)
            g_dbus.message_unref(reply);
        geo_gc__stopped(&mel_geo_unavailable);
        return;
    }
    const char* path = NULL;
    if (!g_dbus.message_get_args(reply, NULL, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID) || path == NULL)
    {
        g_dbus.message_unref(reply);
        geo_gc__stopped(&mel_geo_unavailable);
        return;
    }
    snprintf(g_client_path, sizeof g_client_path, "%s", path);
    g_dbus.message_unref(reply);

    g_state = GEO_GC_CONFIGURING;
    geo_gc__configure();

    char rule[512];
    snprintf(rule, sizeof rule, "type='signal',interface='%s',member='LocationUpdated',path='%s'", GEO_GC_CLIENT_I, g_client_path);
    DBusMessage* add = g_dbus.message_new_method_call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "AddMatch");
    const char*  rp = rule;
    g_dbus.message_append_args(add, DBUS_TYPE_STRING, &rp, DBUS_TYPE_INVALID);
    g_dbus.connection_send(g_conn, add, NULL);
    g_dbus.message_unref(add);

    g_state = GEO_GC_STARTING;
    DBusMessage*     start = g_dbus.message_new_method_call(GEO_GC_BUS, g_client_path, GEO_GC_CLIENT_I, "Start");
    DBusPendingCall* p = NULL;
    if (g_dbus.connection_send_with_reply(g_conn, start, &p, GEO_CALL_TIMEOUT) && p != NULL)
        g_dbus.pending_call_set_notify(p, geo_gc__started, NULL, NULL);
    g_dbus.message_unref(start);
    g_dbus.connection_flush(g_conn);
}

static void geo_gc__kick(void)
{
    if (g_state != GEO_GC_IDLE)
        return;
    g_state = GEO_GC_GETTING_CLIENT;
    DBusMessage*     msg = g_dbus.message_new_method_call(GEO_GC_BUS, GEO_GC_MANAGER, GEO_GC_MANAGER_I, "GetClient");
    DBusPendingCall* pending = NULL;
    if (g_dbus.connection_send_with_reply(g_conn, msg, &pending, GEO_CALL_TIMEOUT) && pending != NULL)
        g_dbus.pending_call_set_notify(pending, geo_gc__client_got, NULL, NULL);
    else
        geo_gc__stopped(&mel_geo_unavailable);
    g_dbus.message_unref(msg);
    g_dbus.connection_flush(g_conn);
}

static void geo_gc__client_stop(void)
{
    if (g_state == GEO_GC_IDLE || g_client_path[0] == 0)
    {
        g_state = GEO_GC_IDLE;
        return;
    }
    DBusMessage* stop = g_dbus.message_new_method_call(GEO_GC_BUS, g_client_path, GEO_GC_CLIENT_I, "Stop");
    g_dbus.connection_send(g_conn, stop, NULL);
    g_dbus.message_unref(stop);
    g_dbus.connection_flush(g_conn);
    geo_gc__stopped(NULL);
}

static void geo_gc__maybe_idle(void)
{
    if (!g_want_stream && g_pending == NULL && g_auth_future == NULL)
        geo_gc__client_stop();
}

static void geo_gc__source_wakeables(Mel_Vat_Source* source, Mel_Vat_Wakeable** out, usize* count)
{
    (void)source;
    *out = &g_wakeable;
    *count = 1;
}

static bool geo_gc__source_drain(Mel_Vat_Source* source, u32 budget)
{
    (void)source;
    (void)budget;
    if (g_conn == NULL)
        return false;
    g_dbus.connection_read_write(g_conn, 0);
    while (g_dbus.connection_dispatch(g_conn) == DBUS_DISPATCH_DATA_REMAINS)
        ;
    return false;
}

static const Mel_Vat_Source_Vtbl GEO_GC_SOURCE_VT = {
    .wakeables = geo_gc__source_wakeables,
    .drain = geo_gc__source_drain,
};

static bool geo_gc_available(void* user)
{
    (void)user;
    if (g_conn != NULL)
        return true;
    if (!geo_dbus_load(&g_dbus))
        return false;
    DBusError err;
    g_dbus.error_init(&err);
    g_conn = g_dbus.bus_get_private(DBUS_BUS_SYSTEM, &err);
    if ((err.name != NULL))
    {
        g_dbus.error_free(&err);
        g_conn = NULL;
        return false;
    }
    g_dbus.connection_set_exit_on_disconnect(g_conn, 0);
    g_dbus.connection_add_filter(g_conn, geo_gc__filter, NULL, NULL);
    return true;
}

static void geo_gc_attach(void* user, Mel_Vat* vat, const Mel_Geo_Provider_Sink* sink)
{
    (void)user;
    g_sink = sink;
    int fd = -1;
    if (g_dbus.connection_get_unix_fd(g_conn, &fd) && fd >= 0)
    {
        g_wakeable = (Mel_Vat_Wakeable){ .handle = fd, .events = MEL_VAT_WAKE_IN };
        g_source = mel_vat_source_open(vat, &GEO_GC_SOURCE_VT, NULL);
    }
    else
        mel_log_error("geo", "geoclue: no pollable dbus fd; updates will not be delivered");
}

static void geo_gc_detach(void* user)
{
    (void)user;
    geo_gc__client_stop();
    if (g_source != NULL)
    {
        mel_vat_source_close(g_source);
        g_source = NULL;
    }
    if (g_conn != NULL)
    {
        g_dbus.connection_remove_filter(g_conn, geo_gc__filter, NULL);
        g_dbus.connection_close(g_conn);
        g_dbus.connection_unref(g_conn);
        g_conn = NULL;
    }
    g_sink = NULL;
}

static Mel_Geo_Caps geo_gc_caps(void* user)
{
    (void)user;
    return (Mel_Geo_Caps){ .fixes = true };
}

static const mel_geo_auth* geo_gc_authorization(void* user)
{
    (void)user;
    return g_auth_state;
}

static void geo_gc_authorize(void* user, const mel_geo_scope* scope, Mel_Future* future)
{
    (void)user;
    (void)scope;
    if (g_auth_state != &mel_geo_auth_not_determined || g_state == GEO_GC_RUNNING)
    {
        g_sink->on_auth(future, g_auth_state == &mel_geo_auth_not_determined ? &mel_geo_auth_granted_in_use : g_auth_state);
        return;
    }
    mel_assert_msg("an authorize is already pending", g_auth_future == NULL);
    g_auth_future = future;
    geo_gc__kick();
}

static const mel_geo_result* geo_gc_last_known(void* user, Mel_Geo_Fix* out)
{
    (void)user;
    if (!g_have_last)
        return &mel_geo_unavailable;
    *out = g_last_fix;
    return &mel_geo_ok;
}

static void geo_gc_request(void* user, Mel_Geo_Request* req)
{
    (void)user;
    req->provider_next = g_pending;
    g_pending = req;
    geo_gc__kick();
}

static void geo_gc_request_cancel(void* user, Mel_Geo_Request* req)
{
    (void)user;
    for (Mel_Geo_Request** pp = &g_pending; *pp != NULL; pp = &(*pp)->provider_next)
        if (*pp == req)
        {
            *pp = req->provider_next;
            break;
        }
    req->provider_next = NULL;
    geo_gc__maybe_idle();
}

static const mel_geo_result* geo_gc_stream_start(void* user, const Mel_Geo_Demand* d)
{
    (void)user;
    g_demand = *d;
    g_want_stream = true;
    if (g_state == GEO_GC_RUNNING)
        geo_gc__configure();
    else
        geo_gc__kick();
    return &mel_geo_ok;
}

static void geo_gc_stream_update(void* user, const Mel_Geo_Demand* d)
{
    (void)user;
    g_demand = *d;
    if (g_state == GEO_GC_RUNNING)
        geo_gc__configure();
}

static void geo_gc_stream_stop(void* user)
{
    (void)user;
    g_want_stream = false;
    geo_gc__maybe_idle();
}

void mel_geo__register_host_providers(void)
{
    static Mel_Geo_Provider_Node node;
    node.desc = (Mel_Geo_Provider_Desc){
        .name = "linux-geoclue2",
        .available = geo_gc_available,
        .attach = geo_gc_attach,
        .detach = geo_gc_detach,
        .caps = geo_gc_caps,
        .authorization = geo_gc_authorization,
        .authorize = geo_gc_authorize,
        .last_known = geo_gc_last_known,
        .request = geo_gc_request,
        .request_cancel = geo_gc_request_cancel,
        .stream_start = geo_gc_stream_start,
        .stream_update = geo_gc_stream_update,
        .stream_stop = geo_gc_stream_stop,
    };
    mel_geo_provider_register_host(&node);
}
