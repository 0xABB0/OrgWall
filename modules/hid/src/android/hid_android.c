#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.array/array.h>

#include <hid/hid.h>
#include <hid/provider.h>
#include <hid/android/android.h>

#include "../hid_internal.h"

#include <errno.h>
#include <jni.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <platform/android/jni.h>

typedef struct
{
    u64  stable_id;
    int  native_id;
    int  fd;
    bool open;
} Android_Device;

typedef struct
{
    const Mel_Alloc* alloc;
    jclass           cls;
    jmethodID        m_enumerate;
    jmethodID        m_open_fd;
    jmethodID        m_close;
    jmethodID        m_vid;
    jmethodID        m_pid;
    jmethodID        m_string;
    bool             ready;
    Mel_Array(Android_Device*) devices;
} Android_Backend;

static Android_Backend g_android;

static JNIEnv* jni_env(void) { return mel_platform_android_env(); }

static Android_Device* find_device(u64 stable_id)
{
    for (usize i = 0; i < g_android.devices.count; i++)
        if (g_android.devices.items[i]->stable_id == stable_id)
            return g_android.devices.items[i];
    return NULL;
}

static void copy_jstring(JNIEnv* env, jstring js, char* out, usize cap)
{
    out[0] = '\0';
    if (!js)
        return;
    const char* s = (*env)->GetStringUTFChars(env, js, NULL);
    if (s)
    {
        strncpy(out, s, cap - 1);
        out[cap - 1] = '\0';
        (*env)->ReleaseStringUTFChars(env, js, s);
    }
}

static u32 android_enumerate(void* user, Mel_Hid_Raw* out, u32 cap)
{
    Android_Backend* be = user;
    if (!be->ready)
        return 0;
    JNIEnv* env = jni_env();
    if (!env)
        return 0;

    jintArray ids = (jintArray)(*env)->CallStaticObjectMethod(env, be->cls, be->m_enumerate);
    if (!ids)
        return 0;
    jsize n = (*env)->GetArrayLength(env, ids);
    jint* native_ids = (*env)->GetIntArrayElements(env, ids, NULL);

    u32 written = 0;
    for (jsize i = 0; i < n && written < cap; i++)
    {
        int native_id = native_ids[i];
        u64 id = (u64)0xB1u << 56 | (u64)(u32)native_id;

        Android_Device* ad = find_device(id);
        if (!ad)
        {
            ad = mel_alloc_type(be->alloc, Android_Device);
            memset(ad, 0, sizeof *ad);
            ad->stable_id = id;
            ad->native_id = native_id;
            ad->fd = MEL_HID_NO_FD;
            mel_array_push(&be->devices, ad);
        }

        Mel_Hid_Raw* r = &out[written++];
        memset(r, 0, sizeof *r);
        r->stable_id = id;
        r->desc.vendor_id = (u16)(*env)->CallStaticIntMethod(env, be->cls, be->m_vid, native_id);
        r->desc.product_id = (u16)(*env)->CallStaticIntMethod(env, be->cls, be->m_pid, native_id);
        r->desc.bus = MEL_HID_BUS_USB;
        jstring prod = (jstring)(*env)->CallStaticObjectMethod(env, be->cls, be->m_string, native_id);
        copy_jstring(env, prod, r->desc.product, MEL_HID_STRING_CAP);
        if (prod)
            (*env)->DeleteLocalRef(env, prod);
        snprintf(r->desc.path, MEL_HID_STRING_CAP, "usb:%d", native_id);
    }

    (*env)->ReleaseIntArrayElements(env, ids, native_ids, JNI_ABORT);
    (*env)->DeleteLocalRef(env, ids);
    return written;
}

static Mel_Hid_Status android_open(void* user, u64 stable_id, Mel_Hid_Channel* out_channel)
{
    Android_Backend* be = user;
    Android_Device*  ad = find_device(stable_id);
    if (!ad)
        return MEL_HID_ERROR | MEL_HID_DEVICE_LOST;
    JNIEnv* env = jni_env();
    if (!env)
        return MEL_HID_ERROR | MEL_HID_NO_BACKEND;
    jint raw_fd = (*env)->CallStaticIntMethod(env, be->cls, be->m_open_fd, ad->native_id);
    if (raw_fd < 0)
        return MEL_HID_ERROR | MEL_HID_ACCESS_DENIED;
    int fd = dup((int)raw_fd);
    if (fd < 0)
        return MEL_HID_ERROR | MEL_HID_DEVICE_LOST;
    ad->fd = fd;
    ad->open = true;
    *out_channel = (Mel_Hid_Channel){ .value = ad, .fd = fd, .bus = MEL_HID_BUS_USB };
    return MEL_HID_OK;
}

static void android_close(void* user, u64 stable_id, Mel_Hid_Channel channel)
{
    Android_Backend* be = user;
    Android_Device*  ad = channel.value;
    if (!ad || !ad->open)
        return;
    close(ad->fd);
    ad->fd = MEL_HID_NO_FD;
    ad->open = false;
    JNIEnv* env = jni_env();
    if (env)
        (*env)->CallStaticVoidMethod(env, be->cls, be->m_close, ad->native_id);
}

static Mel_Hid_Io_Result android_write(void* user, Mel_Hid_Channel channel, const u8* data, usize len)
{
    (void)user;
    if (channel.fd < 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NOT_OPEN };
    ssize_t w = write(channel.fd, data, len);
    if (w < 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_DEVICE_LOST };
    return (Mel_Hid_Io_Result){ .bytes = (usize)w, .status = MEL_HID_OK };
}

static Mel_Hid_Io_Result android_read(void* user, Mel_Hid_Channel channel, u8* out, usize cap, i32 timeout_ms)
{
    (void)user;
    if (channel.fd < 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NOT_OPEN };
    if (timeout_ms != MEL_HID_TIMEOUT_POLL)
    {
        struct pollfd pfd = { .fd = channel.fd, .events = POLLIN };
        int           rc = poll(&pfd, 1, timeout_ms);
        if (rc == 0)
            return (Mel_Hid_Io_Result){ .status = MEL_HID_TIMED_OUT | MEL_HID_WARNED };
        if (rc < 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)))
            return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_DEVICE_LOST };
    }
    ssize_t r = read(channel.fd, out, cap);
    if (r < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return (Mel_Hid_Io_Result){ .status = MEL_HID_WOULD_BLOCK };
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_DEVICE_LOST };
    }
    return (Mel_Hid_Io_Result){ .bytes = (usize)r, .status = MEL_HID_OK };
}

static Mel_Hid_Io_Result android_get_feature(void* user, Mel_Hid_Channel channel, u8 report_id, u8* out, usize cap)
{
    (void)user;
    (void)channel;
    (void)report_id;
    (void)out;
    (void)cap;
    return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_UNSUPPORTED };
}

static Mel_Hid_Io_Result android_send_feature(void* user, Mel_Hid_Channel channel, const u8* data, usize len)
{
    (void)user;
    (void)channel;
    (void)data;
    (void)len;
    return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_UNSUPPORTED };
}

static Mel_Hid_Io_Result android_get_report_descriptor(void* user, Mel_Hid_Channel channel, u8* out, usize cap)
{
    (void)user;
    (void)channel;
    (void)out;
    (void)cap;
    return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_UNSUPPORTED };
}

static Mel_Hid_Io_Result android_get_string(void* user, Mel_Hid_Channel channel, u8 string_index, u8* out, usize cap)
{
    Android_Backend* be = user;
    Android_Device*  ad = channel.value;
    (void)string_index;
    if (!ad)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NOT_OPEN };
    JNIEnv* env = jni_env();
    if (!env)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NO_BACKEND };
    jstring js = (jstring)(*env)->CallStaticObjectMethod(env, be->cls, be->m_string, ad->native_id);
    char    buf[MEL_HID_STRING_CAP];
    copy_jstring(env, js, buf, sizeof buf);
    if (js)
        (*env)->DeleteLocalRef(env, js);
    usize len = strlen(buf);
    usize copy = len < cap ? len : cap;
    memcpy(out, buf, copy);
    Mel_Hid_Status st = MEL_HID_OK;
    if (copy < len)
        st |= MEL_HID_PARTIAL | MEL_HID_WARNED;
    return (Mel_Hid_Io_Result){ .bytes = copy, .status = st };
}

static void* android_native(void* user, Mel_Hid_Channel channel)
{
    (void)user;
    Android_Device* ad = channel.value;
    return ad ? (void*)(intptr_t)ad->fd : NULL;
}

void mel_hid__register_host_providers(const Mel_Alloc* alloc)
{
    g_android.alloc = alloc ? alloc : mel_alloc_heap();
    mel_array_init(&g_android.devices, g_android.alloc);

    JNIEnv* env = jni_env();
    if (env)
    {
        jclass local = (*env)->FindClass(env, "orgwall/melody/hid/MelodyHid");
        if (local)
        {
            g_android.cls = (jclass)(*env)->NewGlobalRef(env, local);
            (*env)->DeleteLocalRef(env, local);
            g_android.m_enumerate = (*env)->GetStaticMethodID(env, g_android.cls, "enumerate", "()[I");
            g_android.m_open_fd = (*env)->GetStaticMethodID(env, g_android.cls, "openFd", "(I)I");
            g_android.m_close = (*env)->GetStaticMethodID(env, g_android.cls, "closeDevice", "(I)V");
            g_android.m_vid = (*env)->GetStaticMethodID(env, g_android.cls, "vendorId", "(I)I");
            g_android.m_pid = (*env)->GetStaticMethodID(env, g_android.cls, "productId", "(I)I");
            g_android.m_string = (*env)->GetStaticMethodID(env, g_android.cls, "productName", "(I)Ljava/lang/String;");
            g_android.ready = g_android.cls && g_android.m_enumerate && g_android.m_open_fd;
        }
    }

    Mel_Hid_Provider_Desc desc = {
        .name = "android-usb",
        .user = &g_android,
        .enumerate = android_enumerate,
        .open = android_open,
        .close = android_close,
        .write = android_write,
        .read = android_read,
        .get_feature = android_get_feature,
        .send_feature = android_send_feature,
        .get_report_descriptor = android_get_report_descriptor,
        .get_string = android_get_string,
        .native = android_native,
    };
    mel_hid_provider_register(&desc);
}

int mel_hid_android_fd(Mel_Hid_Device d)
{
    void* n = mel_hid_native(d);
    return n ? (int)(intptr_t)n : MEL_HID_NO_FD;
}
