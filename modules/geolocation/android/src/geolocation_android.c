#include <geolocation/provider.h>

#include <allocator/allocator.h>
#include <debug/assert.h>
#include <log/log.h>
#include <platform/android/android.h>
#include <platform/android/jni.h>
#include <string/str8.h>
#include <time/nano.h>

#include <string.h>

#define GEO_AND_RESULT_OK          0
#define GEO_AND_RESULT_DENIED      1
#define GEO_AND_RESULT_UNAVAILABLE 2
#define GEO_AND_RESULT_LOST        3
#define GEO_AND_RESULT_EXHAUSTED   7

typedef struct
{
    bool fused;
} Geo_And_Backend;

static Geo_And_Backend g_framework = { false };
static Geo_And_Backend g_fused = { true };

static const Mel_Geo_Provider_Sink* g_sink;

static jclass    g_cls_geo;
static jclass    g_cls_fused;
static jmethodID g_mid_granted;
static jmethodID g_mid_background_granted;
static jmethodID g_mid_geocoder_present;
static jmethodID g_mid_last_known;
static jmethodID g_mid_geocode_forward;
static jmethodID g_mid_geocode_reverse;
static jmethodID g_mid_fw_stream_start;
static jmethodID g_mid_fw_stream_update;
static jmethodID g_mid_fw_stream_stop;
static jmethodID g_mid_fw_request;
static jmethodID g_mid_fw_cancel;
static jmethodID g_mid_fu_available;
static jmethodID g_mid_fu_stream_start;
static jmethodID g_mid_fu_stream_update;
static jmethodID g_mid_fu_stream_stop;
static jmethodID g_mid_fu_request;
static jmethodID g_mid_fu_cancel;
static jmethodID g_mid_fu_region_add;
static jmethodID g_mid_fu_region_remove;

static const mel_geo_result* geo_and__result(jint code)
{
    if (code == GEO_AND_RESULT_OK)
        return &mel_geo_ok;
    if (code == GEO_AND_RESULT_DENIED)
        return &mel_geo_denied;
    if (code == GEO_AND_RESULT_LOST)
        return &mel_geo_lost;
    if (code == GEO_AND_RESULT_EXHAUSTED)
        return &mel_geo_exhausted;
    return &mel_geo_unavailable;
}

static bool geo_and__check(JNIEnv* env)
{
    if (!(*env)->ExceptionCheck(env))
        return true;
    (*env)->ExceptionClear(env);
    return false;
}

static void JNICALL geo_and__on_fix(JNIEnv* env, jclass cls, jboolean stream, jlong req_ptr, jdouble lat, jdouble lon,
                                    jdouble alt, jdouble hacc, jdouble vacc, jdouble speed, jdouble speed_acc,
                                    jdouble bearing, jdouble bearing_acc, jlong utc_ms, jint valid)
{
    (void)env;
    (void)cls;
    Mel_Geo_Fix fix = {
        .latitude_deg = lat,
        .longitude_deg = lon,
        .altitude_m = alt,
        .horizontal_accuracy_m = hacc,
        .vertical_accuracy_m = vacc,
        .speed_mps = speed,
        .speed_accuracy_mps = speed_acc,
        .course_deg = bearing,
        .course_accuracy_deg = bearing_acc,
        .utc_unix_ms = (u64)utc_ms,
        .monotonic_ns = mel_nanos_since_unspecified_epoch(),
        .valid = (u32)valid | MEL_GEO_VALID_MONOTONIC,
    };
    if (stream)
        g_sink->on_fix(&fix);
    else
        g_sink->on_request((Mel_Geo_Request*)(intptr_t)req_ptr, &fix, &mel_geo_ok);
}

static void JNICALL geo_and__on_stream_result(JNIEnv* env, jclass cls, jint code)
{
    (void)env;
    (void)cls;
    g_sink->on_stream_result(geo_and__result(code));
}

static void JNICALL geo_and__on_request_failed(JNIEnv* env, jclass cls, jlong req_ptr, jint code)
{
    (void)env;
    (void)cls;
    g_sink->on_request((Mel_Geo_Request*)(intptr_t)req_ptr, NULL, geo_and__result(code));
}

static void JNICALL geo_and__on_region(JNIEnv* env, jclass cls, jlong region_ptr, jboolean entered)
{
    (void)env;
    (void)cls;
    g_sink->on_region((Mel_Geo_Region*)(intptr_t)region_ptr, entered == JNI_TRUE);
}

static void JNICALL geo_and__on_region_result(JNIEnv* env, jclass cls, jlong region_ptr, jint code)
{
    (void)env;
    (void)cls;
    g_sink->on_region_result((Mel_Geo_Region*)(intptr_t)region_ptr, geo_and__result(code));
}

static str8 geo_and__str(JNIEnv* env, jobjectArray arr, jsize idx, const Mel_Alloc* alloc)
{
    jstring s = (jstring)(*env)->GetObjectArrayElement(env, arr, idx);
    if (s == NULL)
        return (str8){ 0 };
    const char* utf = (*env)->GetStringUTFChars(env, s, NULL);
    str8        out = { 0 };
    if (utf != NULL)
    {
        out = str8_dup_alloc((str8){ (u8*)utf, strlen(utf) }, alloc);
        (*env)->ReleaseStringUTFChars(env, s, utf);
    }
    (*env)->DeleteLocalRef(env, s);
    return out;
}

static void JNICALL geo_and__on_geocode(JNIEnv* env, jclass cls, jlong geo_ptr, jint code, jint count,
                                        jobjectArray strings, jdoubleArray coords)
{
    (void)cls;
    Mel_Geo_Geocode* g = (Mel_Geo_Geocode*)(intptr_t)geo_ptr;
    if (!mel_geo_provider_geocode_claim(g))
        return;
    if (code != GEO_AND_RESULT_OK)
    {
        g_sink->on_geocode(g, geo_and__result(code));
        return;
    }
    for (jint i = 0; i < count && (u32)i < g->max_results; i++)
    {
        jdouble ll[2] = { 0 };
        (*env)->GetDoubleArrayRegion(env, coords, i * 2, 2, ll);
        Mel_Geo_Place place = {
            .name = geo_and__str(env, strings, i * 7 + 0, g->alloc),
            .thoroughfare = geo_and__str(env, strings, i * 7 + 1, g->alloc),
            .locality = geo_and__str(env, strings, i * 7 + 2, g->alloc),
            .admin_area = geo_and__str(env, strings, i * 7 + 3, g->alloc),
            .postal_code = geo_and__str(env, strings, i * 7 + 4, g->alloc),
            .country = geo_and__str(env, strings, i * 7 + 5, g->alloc),
            .country_code = geo_and__str(env, strings, i * 7 + 6, g->alloc),
        };
        if (ll[0] == ll[0] && ll[1] == ll[1])
        {
            place.latitude_deg = ll[0];
            place.longitude_deg = ll[1];
            place.valid |= MEL_GEO_VALID_POSITION;
        }
        mel_array_push(&g->places, place);
    }
    g_sink->on_geocode(g, &mel_geo_ok);
}

static const JNINativeMethod GEO_AND_NATIVES[] = {
    { "nativeOnFix", "(ZJDDDDDDDDDJI)V", (void*)geo_and__on_fix },
    { "nativeOnStreamResult", "(I)V", (void*)geo_and__on_stream_result },
    { "nativeOnRequestFailed", "(JI)V", (void*)geo_and__on_request_failed },
    { "nativeOnRegion", "(JZ)V", (void*)geo_and__on_region },
    { "nativeOnRegionResult", "(JI)V", (void*)geo_and__on_region_result },
    { "nativeOnGeocode", "(JII[Ljava/lang/String;[D)V", (void*)geo_and__on_geocode },
};

static bool geo_and__ensure_jni(void)
{
    if (g_cls_geo != NULL)
        return true;
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL)
        return false;

    jclass geo = mel_platform_android_find_class(env, "orgwall/melody/geolocation/MelodyGeo");
    if (!geo_and__check(env) || geo == NULL)
    {
        mel_log_error("geo", "MelodyGeo Java companion not found");
        return false;
    }
    if ((*env)->RegisterNatives(env, geo, GEO_AND_NATIVES, (jint)(sizeof GEO_AND_NATIVES / sizeof GEO_AND_NATIVES[0])) != 0)
    {
        geo_and__check(env);
        mel_log_error("geo", "MelodyGeo native registration failed");
        return false;
    }
    g_cls_geo = (*env)->NewGlobalRef(env, geo);

    g_mid_granted = (*env)->GetStaticMethodID(env, g_cls_geo, "granted", "()Z");
    g_mid_background_granted = (*env)->GetStaticMethodID(env, g_cls_geo, "backgroundGranted", "()Z");
    g_mid_geocoder_present = (*env)->GetStaticMethodID(env, g_cls_geo, "geocoderPresent", "()Z");
    g_mid_last_known = (*env)->GetStaticMethodID(env, g_cls_geo, "lastKnown", "()[D");
    g_mid_geocode_forward = (*env)->GetStaticMethodID(env, g_cls_geo, "geocodeForward", "(JLjava/lang/String;I)V");
    g_mid_geocode_reverse = (*env)->GetStaticMethodID(env, g_cls_geo, "geocodeReverse", "(JDDI)V");
    g_mid_fw_stream_start = (*env)->GetStaticMethodID(env, g_cls_geo, "streamStart", "(DJF)I");
    g_mid_fw_stream_update = (*env)->GetStaticMethodID(env, g_cls_geo, "streamUpdate", "(DJF)V");
    g_mid_fw_stream_stop = (*env)->GetStaticMethodID(env, g_cls_geo, "streamStop", "()V");
    g_mid_fw_request = (*env)->GetStaticMethodID(env, g_cls_geo, "requestSingle", "(JD)I");
    g_mid_fw_cancel = (*env)->GetStaticMethodID(env, g_cls_geo, "cancelSingle", "(J)V");
    if (!geo_and__check(env))
        return false;

    jclass fused = mel_platform_android_find_class(env, "orgwall/melody/geolocation/MelodyGeoFused");
    if (geo_and__check(env) && fused != NULL)
    {
        g_cls_fused = (*env)->NewGlobalRef(env, fused);
        g_mid_fu_available = (*env)->GetStaticMethodID(env, g_cls_fused, "available", "()Z");
        g_mid_fu_stream_start = (*env)->GetStaticMethodID(env, g_cls_fused, "streamStart", "(DJF)I");
        g_mid_fu_stream_update = (*env)->GetStaticMethodID(env, g_cls_fused, "streamUpdate", "(DJF)V");
        g_mid_fu_stream_stop = (*env)->GetStaticMethodID(env, g_cls_fused, "streamStop", "()V");
        g_mid_fu_request = (*env)->GetStaticMethodID(env, g_cls_fused, "requestSingle", "(JDJ)I");
        g_mid_fu_cancel = (*env)->GetStaticMethodID(env, g_cls_fused, "cancelSingle", "(J)V");
        g_mid_fu_region_add = (*env)->GetStaticMethodID(env, g_cls_fused, "regionAdd", "(JDDFZZ)I");
        g_mid_fu_region_remove = (*env)->GetStaticMethodID(env, g_cls_fused, "regionRemove", "(J)V");
        geo_and__check(env);
    }
    return true;
}

static bool geo_and__call_bool(jclass cls, jmethodID mid)
{
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL || cls == NULL || mid == NULL)
        return false;
    jboolean r = (*env)->CallStaticBooleanMethod(env, cls, mid);
    if (!geo_and__check(env))
        return false;
    return r == JNI_TRUE;
}

static bool geo_and__available_framework(void* user)
{
    (void)user;
    return geo_and__ensure_jni();
}

static bool geo_and__available_fused(void* user)
{
    (void)user;
    if (!geo_and__ensure_jni() || g_cls_fused == NULL)
        return false;
    return geo_and__call_bool(g_cls_fused, g_mid_fu_available);
}

static void geo_and__attach(void* user, Mel_Vat* vat, const Mel_Geo_Provider_Sink* sink)
{
    (void)user;
    (void)vat;
    g_sink = sink;
}

static void geo_and__detach(void* user)
{
    (void)user;
    g_sink = NULL;
}

static Mel_Geo_Caps geo_and__caps(void* user)
{
    Geo_And_Backend* be = user;
    return (Mel_Geo_Caps){
        .fixes = true,
        .regions_native = be->fused && g_mid_fu_region_add != NULL,
        .geocoding = geo_and__call_bool(g_cls_geo, g_mid_geocoder_present),
        .background = geo_and__call_bool(g_cls_geo, g_mid_background_granted),
    };
}

static const mel_geo_auth* geo_and__authorization(void* user)
{
    (void)user;
    if (!geo_and__call_bool(g_cls_geo, g_mid_granted))
        return &mel_geo_auth_not_determined;
    if (geo_and__call_bool(g_cls_geo, g_mid_background_granted))
        return &mel_geo_auth_granted_always;
    return &mel_geo_auth_granted_in_use;
}

static struct
{
    Mel_Task    task;
    Mel_Future* platform_future;
    Mel_Future* caller;
    bool        want_background;
    bool        background_stage;
} g_auth;

static void geo_and__auth_step(Mel_Task* t)
{
    (void)t;
    const Mel_Platform_Permission_Outcome* o = mel_platform_android_permission_outcome(g_auth.platform_future);
    bool granted = o != NULL && (o->result & MEL_PLATFORM_PERMISSION_GRANTED) != 0u;
    mel_platform_android_permission_free(g_auth.platform_future);
    g_auth.platform_future = NULL;

    if (g_auth.background_stage)
    {
        Mel_Future* caller = g_auth.caller;
        g_auth.caller = NULL;
        g_sink->on_auth(caller, granted ? &mel_geo_auth_granted_always : &mel_geo_auth_granted_in_use);
        return;
    }
    if (!granted)
    {
        Mel_Future* caller = g_auth.caller;
        g_auth.caller = NULL;
        g_sink->on_auth(caller, &mel_geo_auth_denied);
        return;
    }
    if (g_auth.want_background && mel_platform_android_sdk_version() >= 29)
    {
        g_auth.background_stage = true;
        g_auth.platform_future = mel_platform_android_request_permission("android.permission.ACCESS_BACKGROUND_LOCATION",
                                                                         .deliver = mel_executor_inline());
        mel_task_init(&g_auth.task, geo_and__auth_step);
        mel_future_then(g_auth.platform_future, &g_auth.task, mel_executor_inline());
        return;
    }
    Mel_Future* caller = g_auth.caller;
    g_auth.caller = NULL;
    g_sink->on_auth(caller, &mel_geo_auth_granted_in_use);
}

static void geo_and__authorize(void* user, const mel_geo_scope* scope, Mel_Future* future)
{
    (void)user;
    const mel_geo_auth* now = geo_and__authorization(NULL);
    if (mel_geo_auth_is_granted(now) && (scope == &mel_geo_scope_in_use || now == &mel_geo_auth_granted_always))
    {
        g_sink->on_auth(future, now);
        return;
    }
    mel_assert_msg("an authorize is already pending", g_auth.caller == NULL);
    g_auth.caller = future;
    g_auth.want_background = scope == &mel_geo_scope_always;
    g_auth.background_stage = mel_geo_auth_is_granted(now);
    const char* perm = g_auth.background_stage ? "android.permission.ACCESS_BACKGROUND_LOCATION"
                                               : "android.permission.ACCESS_FINE_LOCATION";
    g_auth.platform_future = mel_platform_android_request_permission(perm, .deliver = mel_executor_inline());
    mel_task_init(&g_auth.task, geo_and__auth_step);
    mel_future_then(g_auth.platform_future, &g_auth.task, mel_executor_inline());
}

static const mel_geo_result* geo_and__last_known(void* user, Mel_Geo_Fix* out)
{
    (void)user;
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL || g_mid_last_known == NULL)
        return &mel_geo_unavailable;
    jdoubleArray arr = (jdoubleArray)(*env)->CallStaticObjectMethod(env, g_cls_geo, g_mid_last_known);
    if (!geo_and__check(env) || arr == NULL)
        return &mel_geo_unavailable;
    jdouble v[12] = { 0 };
    (*env)->GetDoubleArrayRegion(env, arr, 0, 12, v);
    (*env)->DeleteLocalRef(env, arr);
    *out = (Mel_Geo_Fix){
        .latitude_deg = v[0],
        .longitude_deg = v[1],
        .altitude_m = v[2],
        .horizontal_accuracy_m = v[3],
        .vertical_accuracy_m = v[4],
        .speed_mps = v[5],
        .speed_accuracy_mps = v[6],
        .course_deg = v[7],
        .course_accuracy_deg = v[8],
        .utc_unix_ms = (u64)v[9],
        .monotonic_ns = mel_nanos_since_unspecified_epoch() - (u64)(v[10] * 1000000.0),
        .valid = (u32)v[11] | MEL_GEO_VALID_MONOTONIC,
    };
    return &mel_geo_ok;
}

static void geo_and__request(void* user, Mel_Geo_Request* req)
{
    Geo_And_Backend* be = user;
    JNIEnv*          env = mel_platform_android_env();
    if (env == NULL)
    {
        g_sink->on_request(req, NULL, &mel_geo_unavailable);
        return;
    }
    jint code;
    if (be->fused)
    {
        jlong max_age_ms = req->max_age_ns > 0 ? req->max_age_ns / 1000000 : 0;
        code = (*env)->CallStaticIntMethod(env, g_cls_fused, g_mid_fu_request, (jlong)(intptr_t)req, (jdouble)req->accuracy_m, max_age_ms);
    }
    else
        code = (*env)->CallStaticIntMethod(env, g_cls_geo, g_mid_fw_request, (jlong)(intptr_t)req, (jdouble)req->accuracy_m);
    if (!geo_and__check(env))
        code = GEO_AND_RESULT_UNAVAILABLE;
    if (code != GEO_AND_RESULT_OK)
        g_sink->on_request(req, NULL, geo_and__result(code));
}

static void geo_and__request_cancel(void* user, Mel_Geo_Request* req)
{
    Geo_And_Backend* be = user;
    JNIEnv*          env = mel_platform_android_env();
    if (env == NULL)
        return;
    (*env)->CallStaticVoidMethod(env, be->fused ? g_cls_fused : g_cls_geo, be->fused ? g_mid_fu_cancel : g_mid_fw_cancel,
                                 (jlong)(intptr_t)req);
    geo_and__check(env);
}

static const mel_geo_result* geo_and__stream_start(void* user, const Mel_Geo_Demand* d)
{
    Geo_And_Backend* be = user;
    JNIEnv*          env = mel_platform_android_env();
    if (env == NULL)
        return &mel_geo_unavailable;
    jlong interval_ms = d->min_interval_ns > 0 ? d->min_interval_ns / 1000000 : 0;
    jint  code = (*env)->CallStaticIntMethod(env, be->fused ? g_cls_fused : g_cls_geo,
                                             be->fused ? g_mid_fu_stream_start : g_mid_fw_stream_start,
                                             (jdouble)d->accuracy_m, interval_ms, (jfloat)d->min_distance_m);
    if (!geo_and__check(env))
        return &mel_geo_unavailable;
    return geo_and__result(code);
}

static void geo_and__stream_update(void* user, const Mel_Geo_Demand* d)
{
    Geo_And_Backend* be = user;
    JNIEnv*          env = mel_platform_android_env();
    if (env == NULL)
        return;
    jlong interval_ms = d->min_interval_ns > 0 ? d->min_interval_ns / 1000000 : 0;
    (*env)->CallStaticVoidMethod(env, be->fused ? g_cls_fused : g_cls_geo,
                                 be->fused ? g_mid_fu_stream_update : g_mid_fw_stream_update,
                                 (jdouble)d->accuracy_m, interval_ms, (jfloat)d->min_distance_m);
    geo_and__check(env);
}

static void geo_and__stream_stop(void* user)
{
    Geo_And_Backend* be = user;
    JNIEnv*          env = mel_platform_android_env();
    if (env == NULL)
        return;
    (*env)->CallStaticVoidMethod(env, be->fused ? g_cls_fused : g_cls_geo,
                                 be->fused ? g_mid_fu_stream_stop : g_mid_fw_stream_stop);
    geo_and__check(env);
}

static const mel_geo_result* geo_and__region_add(void* user, Mel_Geo_Region* r)
{
    (void)user;
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL || g_mid_fu_region_add == NULL)
        return &mel_geo_unavailable;
    jint code = (*env)->CallStaticIntMethod(env, g_cls_fused, g_mid_fu_region_add, (jlong)(intptr_t)r,
                                            (jdouble)r->latitude_deg, (jdouble)r->longitude_deg, (jfloat)r->radius_m,
                                            (jboolean)r->notify_enter, (jboolean)r->notify_exit);
    if (!geo_and__check(env))
        return &mel_geo_unavailable;
    return geo_and__result(code);
}

static void geo_and__region_remove(void* user, Mel_Geo_Region* r)
{
    (void)user;
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL || g_mid_fu_region_remove == NULL)
        return;
    (*env)->CallStaticVoidMethod(env, g_cls_fused, g_mid_fu_region_remove, (jlong)(intptr_t)r);
    geo_and__check(env);
}

static void geo_and__geocode(Mel_Geo_Geocode* g, bool reverse)
{
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL)
    {
        if (mel_geo_provider_geocode_claim(g))
            g_sink->on_geocode(g, &mel_geo_unavailable);
        return;
    }
    if (reverse)
        (*env)->CallStaticVoidMethod(env, g_cls_geo, g_mid_geocode_reverse, (jlong)(intptr_t)g, (jdouble)g->latitude_deg,
                                     (jdouble)g->longitude_deg, (jint)g->max_results);
    else
    {
        char stack_query[512];
        char* q = stack_query;
        if (g->query.len + 1 > sizeof stack_query)
            q = mel_alloc(g->alloc, g->query.len + 1);
        memcpy(q, g->query.data, g->query.len);
        q[g->query.len] = 0;
        jstring js = (*env)->NewStringUTF(env, q);
        if (q != stack_query)
            mel_dealloc(g->alloc, q);
        (*env)->CallStaticVoidMethod(env, g_cls_geo, g_mid_geocode_forward, (jlong)(intptr_t)g, js, (jint)g->max_results);
        (*env)->DeleteLocalRef(env, js);
    }
    if (!geo_and__check(env) && mel_geo_provider_geocode_claim(g))
        g_sink->on_geocode(g, &mel_geo_unavailable);
}

static void geo_and__geocode_forward(void* user, Mel_Geo_Geocode* g)
{
    (void)user;
    geo_and__geocode(g, false);
}

static void geo_and__geocode_reverse(void* user, Mel_Geo_Geocode* g)
{
    (void)user;
    geo_and__geocode(g, true);
}

void mel_geo__register_host_providers(void)
{
    static Mel_Geo_Provider_Node fused_node;
    fused_node.desc = (Mel_Geo_Provider_Desc){
        .name = "android-fused",
        .user = &g_fused,
        .available = geo_and__available_fused,
        .attach = geo_and__attach,
        .detach = geo_and__detach,
        .caps = geo_and__caps,
        .authorization = geo_and__authorization,
        .authorize = geo_and__authorize,
        .last_known = geo_and__last_known,
        .request = geo_and__request,
        .request_cancel = geo_and__request_cancel,
        .stream_start = geo_and__stream_start,
        .stream_update = geo_and__stream_update,
        .stream_stop = geo_and__stream_stop,
        .region_add = geo_and__region_add,
        .region_remove = geo_and__region_remove,
        .geocode_forward = geo_and__geocode_forward,
        .geocode_reverse = geo_and__geocode_reverse,
    };
    mel_geo_provider_register_host(&fused_node);

    static Mel_Geo_Provider_Node framework_node;
    framework_node.desc = (Mel_Geo_Provider_Desc){
        .name = "android-locationmanager",
        .user = &g_framework,
        .available = geo_and__available_framework,
        .attach = geo_and__attach,
        .detach = geo_and__detach,
        .caps = geo_and__caps,
        .authorization = geo_and__authorization,
        .authorize = geo_and__authorize,
        .last_known = geo_and__last_known,
        .request = geo_and__request,
        .request_cancel = geo_and__request_cancel,
        .stream_start = geo_and__stream_start,
        .stream_update = geo_and__stream_update,
        .stream_stop = geo_and__stream_stop,
        .geocode_forward = geo_and__geocode_forward,
        .geocode_reverse = geo_and__geocode_reverse,
    };
    mel_geo_provider_register_host(&framework_node);
}
