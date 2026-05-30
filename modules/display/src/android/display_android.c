#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
    #error "android-only translation unit"
#endif

#include <jni.h>
#include <string.h>

#include <platform/android/jni.h>

#include <display/android/android.h>

#include "../display_backend.h"

enum {
    MEL_ANDROID_STATE_UNKNOWN      = 0,
    MEL_ANDROID_STATE_OFF          = 1,
    MEL_ANDROID_STATE_ON           = 2,
    MEL_ANDROID_STATE_DOZE         = 3,
    MEL_ANDROID_STATE_DOZE_SUSPEND = 4,
    MEL_ANDROID_STATE_VR           = 5,
    MEL_ANDROID_STATE_ON_SUSPEND   = 6,
};

static struct {
    bool      resolved;
    bool      failed;
    jclass    cls;
    jclass    info_cls;
    jmethodID enumerate;
    jfieldID  f_id, f_name, f_w, f_h, f_dpi, f_state, f_wide, f_hdr;
    jfieldID  f_maxlum, f_minlum, f_avglum;
    jfieldID  f_modes_w, f_modes_h, f_modes_r;
} g;

static bool resolve(JNIEnv* env)
{
    if (g.resolved) return !g.failed;
    g.resolved = true;

    jclass local = (*env)->FindClass(env, "orgwall/melody/display/MelodyDisplay");
    if (!local) { (*env)->ExceptionClear(env); g.failed = true; return false; }
    g.cls = (*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);

    jclass info_local = (*env)->FindClass(env, "orgwall/melody/display/MelodyDisplay$Info");
    if (!info_local) { (*env)->ExceptionClear(env); g.failed = true; return false; }
    g.info_cls = (*env)->NewGlobalRef(env, info_local);
    (*env)->DeleteLocalRef(env, info_local);

    g.enumerate = (*env)->GetStaticMethodID(env, g.cls, "enumerate",
        "()[Lorgwall/melody/display/MelodyDisplay$Info;");

    g.f_id      = (*env)->GetFieldID(env, g.info_cls, "displayId", "I");
    g.f_name    = (*env)->GetFieldID(env, g.info_cls, "name", "Ljava/lang/String;");
    g.f_w       = (*env)->GetFieldID(env, g.info_cls, "widthPx", "I");
    g.f_h       = (*env)->GetFieldID(env, g.info_cls, "heightPx", "I");
    g.f_dpi     = (*env)->GetFieldID(env, g.info_cls, "densityDpi", "I");
    g.f_state   = (*env)->GetFieldID(env, g.info_cls, "state", "I");
    g.f_wide    = (*env)->GetFieldID(env, g.info_cls, "wideGamut", "Z");
    g.f_hdr     = (*env)->GetFieldID(env, g.info_cls, "hdr", "Z");
    g.f_maxlum  = (*env)->GetFieldID(env, g.info_cls, "maxLuminance", "F");
    g.f_minlum  = (*env)->GetFieldID(env, g.info_cls, "minLuminance", "F");
    g.f_avglum  = (*env)->GetFieldID(env, g.info_cls, "maxAvgLuminance", "F");
    g.f_modes_w = (*env)->GetFieldID(env, g.info_cls, "modeWidthPx", "[I");
    g.f_modes_h = (*env)->GetFieldID(env, g.info_cls, "modeHeightPx", "[I");
    g.f_modes_r = (*env)->GetFieldID(env, g.info_cls, "modeRefreshMhz", "[I");

    if (!g.enumerate || !g.f_id || !g.f_name || !g.f_w || !g.f_h || !g.f_dpi ||
        !g.f_state || !g.f_wide || !g.f_hdr || !g.f_maxlum || !g.f_minlum ||
        !g.f_avglum || !g.f_modes_w || !g.f_modes_h || !g.f_modes_r) {
        (*env)->ExceptionClear(env);
        g.failed = true;
        return false;
    }
    return true;
}

static Mel_Display_State map_state(jint s)
{
    switch (s) {
        case MEL_ANDROID_STATE_ON:           return MEL_DISPLAY_STATE_ACTIVE;
        case MEL_ANDROID_STATE_OFF:          return MEL_DISPLAY_STATE_POWERED_OFF;
        case MEL_ANDROID_STATE_DOZE:
        case MEL_ANDROID_STATE_DOZE_SUSPEND: return MEL_DISPLAY_STATE_DIMMED;
        case MEL_ANDROID_STATE_ON_SUSPEND:   return MEL_DISPLAY_STATE_IDLE;
        default:                             return MEL_DISPLAY_STATE_ACTIVE;
    }
}

static void copy_name(JNIEnv* env, jstring js, char* out)
{
    out[0] = 0;
    if (!js) return;
    const char* c = (*env)->GetStringUTFChars(env, js, NULL);
    if (!c) return;
    strncpy(out, c, MEL_DISPLAY_NAME_CAP - 1);
    out[MEL_DISPLAY_NAME_CAP - 1] = 0;
    (*env)->ReleaseStringUTFChars(env, js, c);
}

static void fill_modes(JNIEnv* env, jobject info, Mel_Display_Descriptor* d)
{
    d->refresh_mode_count = 0;
    jintArray wj = (*env)->GetObjectField(env, info, g.f_modes_w);
    jintArray hj = (*env)->GetObjectField(env, info, g.f_modes_h);
    jintArray rj = (*env)->GetObjectField(env, info, g.f_modes_r);
    if (!wj || !hj || !rj) return;

    jsize n = (*env)->GetArrayLength(env, wj);
    if (n > MEL_DISPLAY_MAX_MODES) n = MEL_DISPLAY_MAX_MODES;

    jint wbuf[MEL_DISPLAY_MAX_MODES], hbuf[MEL_DISPLAY_MAX_MODES], rbuf[MEL_DISPLAY_MAX_MODES];
    (*env)->GetIntArrayRegion(env, wj, 0, n, wbuf);
    (*env)->GetIntArrayRegion(env, hj, 0, n, hbuf);
    (*env)->GetIntArrayRegion(env, rj, 0, n, rbuf);

    for (jsize i = 0; i < n; i++) {
        d->refresh_modes[d->refresh_mode_count++] = (Mel_Display_Mode){
            .width_px    = (u32)wbuf[i],
            .height_px   = (u32)hbuf[i],
            .refresh_mhz = (u32)rbuf[i],
            .interlaced  = false,
        };
    }

    (*env)->DeleteLocalRef(env, wj);
    (*env)->DeleteLocalRef(env, hj);
    (*env)->DeleteLocalRef(env, rj);
}

u32 mel_display__enumerate(const Mel_Alloc* alloc, Mel_Display_Raw* out, u32 cap)
{
    (void)alloc;
    JNIEnv* env = mel_platform_android_env();
    if (!env || !resolve(env)) return 0;

    jobjectArray arr = (*env)->CallStaticObjectMethod(env, g.cls, g.enumerate);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return 0; }
    if (!arr) return 0;

    jsize count = (*env)->GetArrayLength(env, arr);
    u32 n = 0;
    for (jsize i = 0; i < count && n < cap; i++) {
        jobject info = (*env)->GetObjectArrayElement(env, arr, i);
        if (!info) continue;

        Mel_Display_Raw* r = &out[n++];
        memset(r, 0, sizeof *r);

        jint id = (*env)->GetIntField(env, info, g.f_id);
        r->stable_id = (u64)(u32)id;

        Mel_Display_Descriptor* d = &r->desc;

        jstring name = (*env)->GetObjectField(env, info, g.f_name);
        copy_name(env, name, d->name);
        if (name) (*env)->DeleteLocalRef(env, name);

        d->connector = (id == 0) ? MEL_DISPLAY_CONNECTOR_INTERNAL
                                 : MEL_DISPLAY_CONNECTOR_UNKNOWN;

        d->native_resolution.width_px  = (u32)(*env)->GetIntField(env, info, g.f_w);
        d->native_resolution.height_px = (u32)(*env)->GetIntField(env, info, g.f_h);

        jint dpi = (*env)->GetIntField(env, info, g.f_dpi);
        d->scale_factor = dpi > 0 ? (f32)dpi / 160.0f : 1.0f;

        d->state = map_state((*env)->GetIntField(env, info, g.f_state));

        fill_modes(env, info, d);

        Mel_Display_Hdr* hdr = &d->hdr;
        bool is_hdr = (*env)->GetBooleanField(env, info, g.f_hdr);
        f32 maxl = (*env)->GetFloatField(env, info, g.f_maxlum);
        hdr->has_luminance = is_hdr && maxl > 0.0f;
        if (hdr->has_luminance) {
            hdr->peak_luminance_nits = maxl;
            hdr->avg_luminance_nits  = (*env)->GetFloatField(env, info, g.f_avglum);
            hdr->min_luminance_nits  = (*env)->GetFloatField(env, info, g.f_minlum);
        }
        hdr->has_edr = false;

        u32 cs = 0;
        hdr->supported_color_spaces[cs++] = MEL_COLOR_SPACE_SRGB;
        if ((*env)->GetBooleanField(env, info, g.f_wide))
            hdr->supported_color_spaces[cs++] = MEL_COLOR_SPACE_DISPLAY_P3;
        hdr->supported_color_space_count = cs;

        hdr->mastering_primaries_support = is_hdr ? MEL_DISPLAY_MASTERING_STATIC
                                                  : MEL_DISPLAY_MASTERING_NONE;
        hdr->tone_mapping_owner = MEL_DISPLAY_TONEMAP_DISPLAY;
        hdr->active             = is_hdr;

        (*env)->DeleteLocalRef(env, info);
    }

    (*env)->DeleteLocalRef(env, arr);
    return n;
}

int mel_display_android_display_id(Mel_Display d)
{
    u64 id;
    return mel_display__stable_id(d, &id) ? (int)(u32)id : -1;
}
