#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
#error "android-only translation unit"
#endif

#include <platform/android/jni.h>

#include <collection.array/array.h>

typedef struct
{
    i32                                request_code;
    Mel_Platform_Android_Permission_Cb cb;
    void*                              user;
} Permission_Listener;

static Mel_Array(Permission_Listener) g_listeners;

void mel_platform_android_permission_listen(const Mel_Alloc* alloc, i32 request_code, Mel_Platform_Android_Permission_Cb cb, void* user)
{
    if (cb == NULL)
        return;
    if (g_listeners.allocator == NULL)
        mel_array_init(&g_listeners, alloc);

    for (usize i = 0; i < g_listeners.count; i++)
    {
        Permission_Listener* l = &g_listeners.items[i];
        if (l->request_code == request_code && l->cb == cb && l->user == user)
            return;
    }

    Permission_Listener l = { .request_code = request_code, .cb = cb, .user = user };
    mel_array_push(&g_listeners, l);
}

void mel_platform_android_permission_unlisten(i32 request_code, Mel_Platform_Android_Permission_Cb cb, void* user)
{
    for (usize i = 0; i < g_listeners.count; i++)
    {
        Permission_Listener* l = &g_listeners.items[i];
        if (l->request_code == request_code && l->cb == cb && l->user == user)
        {
            mel_array_remove_unordered(&g_listeners, i);
            if (g_listeners.count == 0)
            {
                mel_array_free(&g_listeners);
                mel_array_init(&g_listeners, NULL);
            }
            return;
        }
    }
}

void mel_platform_android_permission_dispatch(i32 request_code, bool granted)
{
    for (usize i = 0; i < g_listeners.count; i++)
    {
        Permission_Listener* l = &g_listeners.items[i];
        if (l->request_code == request_code)
            l->cb(l->user, request_code, granted);
    }
}
