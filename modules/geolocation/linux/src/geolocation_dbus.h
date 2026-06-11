#pragma once

#include <core/types.h>

typedef struct DBusConnection  DBusConnection;
typedef struct DBusMessage     DBusMessage;
typedef struct DBusPendingCall DBusPendingCall;

typedef u32 dbus_bool_t;
typedef u32 dbus_uint32_t;
typedef u64 dbus_uint64_t;

typedef struct
{
    _Alignas(8) char opaque[128];
} DBusMessageIter;

typedef struct
{
    const char* name;
    const char* message;
    void*       pad[6];
} DBusError;

#define DBUS_BUS_SYSTEM                     1
#define DBUS_MESSAGE_TYPE_ERROR             3
#define DBUS_TYPE_INVALID                   0
#define DBUS_TYPE_STRING                    ((int)'s')
#define DBUS_TYPE_OBJECT_PATH               ((int)'o')
#define DBUS_TYPE_ARRAY                     ((int)'a')
#define DBUS_TYPE_VARIANT                   ((int)'v')
#define DBUS_TYPE_DICT_ENTRY                ((int)'e')
#define DBUS_TYPE_STRUCT                    ((int)'r')
#define DBUS_TYPE_DOUBLE                    ((int)'d')
#define DBUS_TYPE_UINT32                    ((int)'u')
#define DBUS_TYPE_UINT64                    ((int)'t')
#define DBUS_HANDLER_RESULT_HANDLED         0
#define DBUS_HANDLER_RESULT_NOT_YET_HANDLED 1
#define DBUS_DISPATCH_DATA_REMAINS          0

typedef int (*DBusHandleMessageFunction)(DBusConnection* conn, DBusMessage* msg, void* user);
typedef void (*DBusPendingCallNotifyFunction)(DBusPendingCall* pending, void* user);
typedef void (*DBusFreeFunction)(void* memory);

typedef struct
{
    DBusConnection* (*bus_get_private)(int type, DBusError* err);
    void (*error_init)(DBusError* err);
    void (*error_free)(DBusError* err);

    void (*connection_set_exit_on_disconnect)(DBusConnection* conn, dbus_bool_t v);
    dbus_bool_t (*connection_add_filter)(DBusConnection* conn, DBusHandleMessageFunction fn, void* user, DBusFreeFunction free_fn);
    void (*connection_remove_filter)(DBusConnection* conn, DBusHandleMessageFunction fn, void* user);
    dbus_bool_t (*connection_get_unix_fd)(DBusConnection* conn, int* fd);
    dbus_bool_t (*connection_read_write)(DBusConnection* conn, int timeout_ms);
    int (*connection_dispatch)(DBusConnection* conn);
    dbus_bool_t (*connection_send)(DBusConnection* conn, DBusMessage* msg, dbus_uint32_t* serial);
    dbus_bool_t (*connection_send_with_reply)(DBusConnection* conn, DBusMessage* msg, DBusPendingCall** pending, int timeout_ms);
    void (*connection_flush)(DBusConnection* conn);
    void (*connection_close)(DBusConnection* conn);
    void (*connection_unref)(DBusConnection* conn);

    DBusMessage* (*message_new_method_call)(const char* bus, const char* path, const char* iface, const char* method);
    void (*message_unref)(DBusMessage* msg);
    int (*message_get_type)(DBusMessage* msg);
    const char* (*message_get_error_name)(DBusMessage* msg);
    dbus_bool_t (*message_get_args)(DBusMessage* msg, DBusError* err, int first_type, ...);
    dbus_bool_t (*message_append_args)(DBusMessage* msg, int first_type, ...);
    dbus_bool_t (*message_is_signal)(DBusMessage* msg, const char* iface, const char* member);

    dbus_bool_t (*message_iter_init)(DBusMessage* msg, DBusMessageIter* it);
    void (*message_iter_recurse)(DBusMessageIter* it, DBusMessageIter* sub);
    int (*message_iter_get_arg_type)(DBusMessageIter* it);
    void (*message_iter_get_basic)(DBusMessageIter* it, void* out);
    dbus_bool_t (*message_iter_next)(DBusMessageIter* it);
    void (*message_iter_init_append)(DBusMessage* msg, DBusMessageIter* it);
    dbus_bool_t (*message_iter_append_basic)(DBusMessageIter* it, int type, const void* value);
    dbus_bool_t (*message_iter_open_container)(DBusMessageIter* it, int type, const char* sig, DBusMessageIter* sub);
    dbus_bool_t (*message_iter_close_container)(DBusMessageIter* it, DBusMessageIter* sub);

    dbus_bool_t (*pending_call_set_notify)(DBusPendingCall* pending, DBusPendingCallNotifyFunction fn, void* user, DBusFreeFunction free_fn);
    DBusMessage* (*pending_call_steal_reply)(DBusPendingCall* pending);
    void (*pending_call_unref)(DBusPendingCall* pending);
} Geo_DBus;

bool geo_dbus_load(Geo_DBus* out);
