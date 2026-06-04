#include <gamepad/provider.h>
#include <gamepad/android/android.h>

#include "../joystick_backend.h"

#include <platform/android/jni.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>

#include <string.h>

typedef struct
{
    i32  device_id;
    u64  stable_id;
    char name[128];
} And_Pad;

typedef struct
{
    const Mel_Alloc* alloc;
    Mel_Array(And_Pad) pads;
} And_Backend;

static And_Backend g_backend;

static And_Pad* pad_for(u64 stable_id)
{
    for (usize i = 0; i < g_backend.pads.count; i++)
        if (g_backend.pads.items[i].stable_id == stable_id)
            return &g_backend.pads.items[i];
    return NULL;
}

static u32 android_enumerate(void* user, Mel_Joystick_Raw* out, u32 cap)
{
    (void)user;
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return 0;
    mel_array_clear(&g_backend.pads);

    jclass id_cls = (*env)->FindClass(env, "android/view/InputDevice");
    if (!id_cls)
    {
        (*env)->ExceptionClear(env);
        return 0;
    }
    jmethodID get_ids = (*env)->GetStaticMethodID(env, id_cls, "getDeviceIds", "()[I");
    jmethodID get_dev = (*env)->GetStaticMethodID(env, id_cls, "getDevice", "(I)Landroid/view/InputDevice;");
    jmethodID get_sources = (*env)->GetMethodID(env, id_cls, "getSources", "()I");
    jmethodID get_name = (*env)->GetMethodID(env, id_cls, "getName", "()Ljava/lang/String;");
    jmethodID get_vid = (*env)->GetMethodID(env, id_cls, "getVendorId", "()I");
    jmethodID get_pid = (*env)->GetMethodID(env, id_cls, "getProductId", "()I");
    if (!get_ids || !get_dev || !get_sources)
    {
        (*env)->ExceptionClear(env);
        return 0;
    }

    jintArray ids = (jintArray)(*env)->CallStaticObjectMethod(env, id_cls, get_ids);
    if (!ids)
        return 0;
    jsize count = (*env)->GetArrayLength(env, ids);
    jint* elems = (*env)->GetIntArrayElements(env, ids, NULL);

    const jint SOURCE_GAMEPAD = 0x00000401;
    const jint SOURCE_JOYSTICK = 0x01000010;

    u32 n = 0;
    for (jsize i = 0; i < count && n < cap; i++)
    {
        jobject dev = (*env)->CallStaticObjectMethod(env, id_cls, get_dev, elems[i]);
        if (!dev)
            continue;
        jint sources = (*env)->CallIntMethod(env, dev, get_sources);
        if (((sources & SOURCE_GAMEPAD) != SOURCE_GAMEPAD) && ((sources & SOURCE_JOYSTICK) != SOURCE_JOYSTICK))
        {
            (*env)->DeleteLocalRef(env, dev);
            continue;
        }

        And_Pad pad;
        memset(&pad, 0, sizeof pad);
        pad.device_id = elems[i];
        pad.stable_id = (u64)(u32)elems[i];

        if (get_name)
        {
            jstring jname = (jstring)(*env)->CallObjectMethod(env, dev, get_name);
            if (jname)
            {
                const char* cn = (*env)->GetStringUTFChars(env, jname, NULL);
                if (cn)
                {
                    strncpy(pad.name, cn, sizeof pad.name - 1);
                    (*env)->ReleaseStringUTFChars(env, jname, cn);
                }
                (*env)->DeleteLocalRef(env, jname);
            }
        }

        mel_array_push(&g_backend.pads, pad);
        And_Pad* stored = &g_backend.pads.items[g_backend.pads.count - 1];

        Mel_Joystick_Descriptor desc;
        memset(&desc, 0, sizeof desc);
        desc.name = str8_from_cstr(stored->name);
        if (get_vid && get_pid)
        {
            desc.vendor_id = (u16)(*env)->CallIntMethod(env, dev, get_vid);
            desc.product_id = (u16)(*env)->CallIntMethod(env, dev, get_pid);
        }
        desc.guid = mel_guid_from_hidapi(3, desc.vendor_id, desc.product_id, 0, stored->name, 0, 0);
        desc.player_index = -1;
        desc.features.dual_motor_rumble = true;

        out[n].stable_id = stored->stable_id;
        out[n].desc = desc;
        n++;
        (*env)->DeleteLocalRef(env, dev);
    }

    (*env)->ReleaseIntArrayElements(env, ids, elems, JNI_ABORT);
    (*env)->DeleteLocalRef(env, ids);
    return n;
}

static bool android_poll(void* user, u64 stable_id, Mel_Joystick_State* out)
{
    (void)user;
    (void)out;
    And_Pad* pad = pad_for(stable_id);
    if (!pad)
        return false;
    static bool warned;
    if (!warned)
    {
        mel_log_warn("gamepad", "android poll has no MotionEvent/KeyEvent bridge; input state is unavailable (device_id=%d)", pad->device_id);
        warned = true;
    }
    return false;
}

static Mel_Joystick_Status android_rumble(void* user, u64 stable_id, Mel_Joystick_Rumble rumble)
{
    (void)user;
    And_Pad* pad = pad_for(stable_id);
    if (!pad)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_DEVICE_LOST;
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_NO_PROVIDER;

    jclass id_cls = (*env)->FindClass(env, "android/view/InputDevice");
    jmethodID get_dev = id_cls ? (*env)->GetStaticMethodID(env, id_cls, "getDevice", "(I)Landroid/view/InputDevice;") : NULL;
    jmethodID get_vib = id_cls ? (*env)->GetMethodID(env, id_cls, "getVibrator", "()Landroid/os/Vibrator;") : NULL;
    if (!get_dev || !get_vib)
    {
        (*env)->ExceptionClear(env);
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_UNSUPPORTED;
    }
    jobject dev = (*env)->CallStaticObjectMethod(env, id_cls, get_dev, pad->device_id);
    if (!dev)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_DEVICE_LOST;
    jobject vib = (*env)->CallObjectMethod(env, dev, get_vib);
    if (!vib)
    {
        (*env)->DeleteLocalRef(env, dev);
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_UNSUPPORTED;
    }

    jclass    vib_cls = (*env)->GetObjectClass(env, vib);
    jmethodID vibrate = (*env)->GetMethodID(env, vib_cls, "vibrate", "(J)V");
    jlong     ms = (jlong)(rumble.duration_s > 0.0f ? rumble.duration_s * 1000.0f : 200.0f);
    if (vibrate && (rumble.low_frequency > 0.0f || rumble.high_frequency > 0.0f))
        (*env)->CallVoidMethod(env, vib, vibrate, ms);
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, vib);
    (*env)->DeleteLocalRef(env, dev);
    return MEL_JOYSTICK_OK;
}

void mel_joystick__register_host_providers(const Mel_Alloc* alloc)
{
    g_backend.alloc = alloc ? alloc : mel_alloc_heap();
    mel_array_init(&g_backend.pads, g_backend.alloc);
    Mel_Joystick_Provider_Desc desc = {
        .name = "android-inputdevice",
        .enumerate = android_enumerate,
        .poll = android_poll,
        .rumble = android_rumble,
    };
    mel_joystick_provider_register(&desc);
}

i32 mel_joystick_android_device_id(Mel_Joystick j)
{
    u32 prov;
    u64 stable_id;
    if (!mel_joystick__lookup(j, &prov, &stable_id))
        return -1;
    And_Pad* pad = pad_for(stable_id);
    return pad ? pad->device_id : -1;
}
