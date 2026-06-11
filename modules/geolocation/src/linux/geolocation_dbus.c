#include "geolocation_dbus.h"

#include <dlfcn.h>

bool geo_dbus_load(Geo_DBus* out)
{
    static void* lib;
    if (lib == NULL)
    {
        lib = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
        if (lib == NULL)
            lib = dlopen("libdbus-1.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (lib == NULL)
        return false;

#define GEO_DBUS_SYM(field, name)              \
    do                                         \
    {                                          \
        *(void**)&out->field = dlsym(lib, name); \
        if (out->field == NULL)                \
            return false;                      \
    } while (0)

    GEO_DBUS_SYM(bus_get_private, "dbus_bus_get_private");
    GEO_DBUS_SYM(error_init, "dbus_error_init");
    GEO_DBUS_SYM(error_free, "dbus_error_free");
    GEO_DBUS_SYM(connection_set_exit_on_disconnect, "dbus_connection_set_exit_on_disconnect");
    GEO_DBUS_SYM(connection_add_filter, "dbus_connection_add_filter");
    GEO_DBUS_SYM(connection_remove_filter, "dbus_connection_remove_filter");
    GEO_DBUS_SYM(connection_get_unix_fd, "dbus_connection_get_unix_fd");
    GEO_DBUS_SYM(connection_read_write, "dbus_connection_read_write");
    GEO_DBUS_SYM(connection_dispatch, "dbus_connection_dispatch");
    GEO_DBUS_SYM(connection_send, "dbus_connection_send");
    GEO_DBUS_SYM(connection_send_with_reply, "dbus_connection_send_with_reply");
    GEO_DBUS_SYM(connection_flush, "dbus_connection_flush");
    GEO_DBUS_SYM(connection_close, "dbus_connection_close");
    GEO_DBUS_SYM(connection_unref, "dbus_connection_unref");
    GEO_DBUS_SYM(message_new_method_call, "dbus_message_new_method_call");
    GEO_DBUS_SYM(message_unref, "dbus_message_unref");
    GEO_DBUS_SYM(message_get_type, "dbus_message_get_type");
    GEO_DBUS_SYM(message_get_error_name, "dbus_message_get_error_name");
    GEO_DBUS_SYM(message_get_args, "dbus_message_get_args");
    GEO_DBUS_SYM(message_append_args, "dbus_message_append_args");
    GEO_DBUS_SYM(message_is_signal, "dbus_message_is_signal");
    GEO_DBUS_SYM(message_iter_init, "dbus_message_iter_init");
    GEO_DBUS_SYM(message_iter_recurse, "dbus_message_iter_recurse");
    GEO_DBUS_SYM(message_iter_get_arg_type, "dbus_message_iter_get_arg_type");
    GEO_DBUS_SYM(message_iter_get_basic, "dbus_message_iter_get_basic");
    GEO_DBUS_SYM(message_iter_next, "dbus_message_iter_next");
    GEO_DBUS_SYM(message_iter_init_append, "dbus_message_iter_init_append");
    GEO_DBUS_SYM(message_iter_append_basic, "dbus_message_iter_append_basic");
    GEO_DBUS_SYM(message_iter_open_container, "dbus_message_iter_open_container");
    GEO_DBUS_SYM(message_iter_close_container, "dbus_message_iter_close_container");
    GEO_DBUS_SYM(pending_call_set_notify, "dbus_pending_call_set_notify");
    GEO_DBUS_SYM(pending_call_steal_reply, "dbus_pending_call_steal_reply");
    GEO_DBUS_SYM(pending_call_unref, "dbus_pending_call_unref");

#undef GEO_DBUS_SYM
    return true;
}
