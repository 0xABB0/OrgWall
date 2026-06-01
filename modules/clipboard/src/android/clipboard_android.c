#include <clipboard/backend.h>

#include <platform/android/jni.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <log/log.h>

#include <string.h>

static jobject android_context(JNIEnv* env)
{
    jclass at = (*env)->FindClass(env, "android/app/ActivityThread");
    if (!at)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jmethodID cur = (*env)->GetStaticMethodID(env, at, "currentApplication", "()Landroid/app/Application;");
    if (!cur)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jobject app = (*env)->CallStaticObjectMethod(env, at, cur);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    return app;
}

static jobject android_clipboard(JNIEnv* env, jobject ctx)
{
    jclass ctx_cls = (*env)->FindClass(env, "android/content/Context");
    if (!ctx_cls)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jmethodID get = (*env)->GetMethodID(env, ctx_cls, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    if (!get)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jstring name = (*env)->NewStringUTF(env, "clipboard");
    jobject svc = (*env)->CallObjectMethod(env, ctx, get, name);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    return svc;
}

static jstring jstr_from_bytes(JNIEnv* env, str8 s)
{
    const Mel_Alloc* a = mel_alloc_heap();
    char*            c = (char*)mel_alloc(a, (usize)s.len + 1);
    if (!c)
        return (*env)->NewStringUTF(env, "");
    if (s.len)
        memcpy(c, s.data, (usize)s.len);
    c[s.len] = 0;
    jstring j = (*env)->NewStringUTF(env, c);
    mel_dealloc(a, c);
    return j;
}

static void emit_charseq(JNIEnv* env, Mel_Clip_Job* job, Mel_Clip_Format f, jobject cs)
{
    if (!cs)
        return;
    jclass    cs_cls = (*env)->GetObjectClass(env, cs);
    jmethodID to_str = (*env)->GetMethodID(env, cs_cls, "toString", "()Ljava/lang/String;");
    if (!to_str)
    {
        (*env)->ExceptionClear(env);
        return;
    }
    jstring js = (jstring)(*env)->CallObjectMethod(env, cs, to_str);
    if (!js)
        return;
    const char* chars = (*env)->GetStringUTFChars(env, js, NULL);
    if (chars)
    {
        mel_clip_job_emit(job, f, chars, strlen(chars));
        (*env)->ReleaseStringUTFChars(env, js, chars);
    }
}

void mel_clip__plat_read(Mel_Clip_Job* job)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env || (*env)->PushLocalFrame(env, 32) != 0)
    {
        mel_clip_job_resolve(job, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD);
        return;
    }
    Mel_Clip_Status st = 0;
    u32             emitted = 0;
    jobject         ctx = android_context(env);
    jobject         cm = ctx ? android_clipboard(env, ctx) : NULL;
    if (cm && mel_clip_job_wants(job, MEL_CLIP_FMT_TEXT))
    {
        jclass    cm_cls = (*env)->GetObjectClass(env, cm);
        jmethodID get = (*env)->GetMethodID(env, cm_cls, "getPrimaryClip", "()Landroid/content/ClipData;");
        jobject   clip = get ? (*env)->CallObjectMethod(env, cm, get) : NULL;
        if (clip)
        {
            jclass    cd_cls = (*env)->GetObjectClass(env, clip);
            jmethodID count = (*env)->GetMethodID(env, cd_cls, "getItemCount", "()I");
            jmethodID at = (*env)->GetMethodID(env, cd_cls, "getItemAt", "(I)Landroid/content/ClipData$Item;");
            jint      n = (count && at) ? (*env)->CallIntMethod(env, clip, count) : 0;
            for (jint i = 0; i < n; i++)
            {
                jobject item = (*env)->CallObjectMethod(env, clip, at, i);
                if (!item)
                    continue;
                jclass    it_cls = (*env)->GetObjectClass(env, item);
                jmethodID coerce = (*env)->GetMethodID(env, it_cls, "coerceToText", "(Landroid/content/Context;)Ljava/lang/CharSequence;");
                jobject   cs = coerce ? (*env)->CallObjectMethod(env, item, coerce, ctx) : NULL;
                if (cs)
                {
                    emit_charseq(env, job, MEL_CLIP_FMT_TEXT, cs);
                    emitted++;
                }
                else
                    (*env)->ExceptionClear(env);
            }
        }
    }
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
    if (emitted == 0)
        st |= MEL_CLIP_RESULT_EMPTY;
    (*env)->PopLocalFrame(env, NULL);
    mel_clip_job_resolve(job, (st & ~MEL_CLIP_SEVERITY_MASK) ? (st | MEL_CLIP_WARNED) : MEL_CLIP_OK);
}

static void android_set_clip(JNIEnv* env, jobject ctx, jobject cm, jobject clip)
{
    jclass    cm_cls = (*env)->GetObjectClass(env, cm);
    jmethodID set = (*env)->GetMethodID(env, cm_cls, "setPrimaryClip", "(Landroid/content/ClipData;)V");
    if (set)
    {
        (*env)->CallVoidMethod(env, cm, set, clip);
        if ((*env)->ExceptionCheck(env))
            (*env)->ExceptionClear(env);
    }
    (void)ctx;
}

void mel_clip__plat_write(Mel_Clip_Job* job)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env || (*env)->PushLocalFrame(env, 32) != 0)
    {
        mel_clip_job_resolve(job, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD);
        return;
    }
    Mel_Clip_Status st = 0;
    jobject         ctx = android_context(env);
    jobject         cm = ctx ? android_clipboard(env, ctx) : NULL;
    if (cm)
    {
        str8 text = STR8_EMPTY, html = STR8_EMPTY;
        u32  reps = mel_clip_job_rep_count(job, 0);
        for (u32 r = 0; r < reps; r++)
        {
            Mel_Clip_Rep rep = mel_clip_job_rep(job, 0, r);
            if (rep.format == MEL_CLIP_FMT_TEXT)
                text = rep.bytes;
            else if (rep.format == MEL_CLIP_FMT_HTML)
                html = rep.bytes;
            else
                st |= MEL_CLIP_WARN_REPRESENTATION_DROPPED;
        }
        jclass  cd_cls = (*env)->FindClass(env, "android/content/ClipData");
        jstring label = (*env)->NewStringUTF(env, "melody");
        jobject clip = NULL;
        if (cd_cls && !str8_is_empty(html))
        {
            jmethodID nh = (*env)->GetStaticMethodID(env, cd_cls, "newHtmlText", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;Ljava/lang/String;)Landroid/content/ClipData;");
            jstring   jt = jstr_from_bytes(env, str8_is_empty(text) ? html : text);
            jstring   jh = jstr_from_bytes(env, html);
            if (nh)
                clip = (*env)->CallStaticObjectMethod(env, cd_cls, nh, label, jt, jh);
        }
        if (!clip && cd_cls)
        {
            (*env)->ExceptionClear(env);
            jmethodID np = (*env)->GetStaticMethodID(env, cd_cls, "newPlainText", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/ClipData;");
            jstring   jt = jstr_from_bytes(env, text);
            if (np)
                clip = (*env)->CallStaticObjectMethod(env, cd_cls, np, label, jt);
        }
        if (clip)
            android_set_clip(env, ctx, cm, clip);
        else
        {
            (*env)->ExceptionClear(env);
            st |= MEL_CLIP_ERROR;
        }
    }
    else
        st |= MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD;
    (*env)->PopLocalFrame(env, NULL);
    Mel_Clip_Status sev = (st & MEL_CLIP_SEVERITY_MASK) == MEL_CLIP_ERROR ? st : ((st & ~MEL_CLIP_SEVERITY_MASK) ? (st | MEL_CLIP_WARNED) : MEL_CLIP_OK);
    mel_clip_job_resolve(job, sev);
}

void mel_clip__plat_clear(Mel_Clip_Job* job)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env || (*env)->PushLocalFrame(env, 16) != 0)
    {
        mel_clip_job_resolve(job, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD);
        return;
    }
    jobject ctx = android_context(env);
    jobject cm = ctx ? android_clipboard(env, ctx) : NULL;
    if (cm)
    {
        jclass    cm_cls = (*env)->GetObjectClass(env, cm);
        jmethodID clr = (*env)->GetMethodID(env, cm_cls, "clearPrimaryClip", "()V");
        if (clr)
        {
            (*env)->CallVoidMethod(env, cm, clr);
            if ((*env)->ExceptionCheck(env))
                (*env)->ExceptionClear(env);
        }
        else
        {
            (*env)->ExceptionClear(env);
            jclass    cd_cls = (*env)->FindClass(env, "android/content/ClipData");
            jmethodID np = cd_cls ? (*env)->GetStaticMethodID(env, cd_cls, "newPlainText", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/ClipData;") : NULL;
            jstring   empty = (*env)->NewStringUTF(env, "");
            jobject   clip = np ? (*env)->CallStaticObjectMethod(env, cd_cls, np, empty, empty) : NULL;
            if (clip)
                android_set_clip(env, ctx, cm, clip);
        }
    }
    (*env)->PopLocalFrame(env, NULL);
    mel_clip_job_resolve(job, MEL_CLIP_OK);
}

void mel_clip__plat_query(Mel_Clip_Job* job)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env || (*env)->PushLocalFrame(env, 32) != 0)
    {
        mel_clip_job_resolve(job, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD);
        return;
    }
    jobject ctx = android_context(env);
    jobject cm = ctx ? android_clipboard(env, ctx) : NULL;
    if (cm)
    {
        jclass    cm_cls = (*env)->GetObjectClass(env, cm);
        jmethodID gd = (*env)->GetMethodID(env, cm_cls, "getPrimaryClipDescription", "()Landroid/content/ClipDescription;");
        jobject   desc = gd ? (*env)->CallObjectMethod(env, cm, gd) : NULL;
        if (desc)
        {
            jclass    d_cls = (*env)->GetObjectClass(env, desc);
            jmethodID cnt = (*env)->GetMethodID(env, d_cls, "getMimeTypeCount", "()I");
            jmethodID mt = (*env)->GetMethodID(env, d_cls, "getMimeType", "(I)Ljava/lang/String;");
            jint      n = (cnt && mt) ? (*env)->CallIntMethod(env, desc, cnt) : 0;
            for (jint i = 0; i < n; i++)
            {
                jstring     js = (jstring)(*env)->CallObjectMethod(env, desc, mt, i);
                const char* c = js ? (*env)->GetStringUTFChars(env, js, NULL) : NULL;
                if (c)
                {
                    mel_clip_job_emit_format(job, mel_clip_format_register((str8){ (u8*)c, (size)strlen(c) }));
                    (*env)->ReleaseStringUTFChars(env, js, c);
                }
            }
        }
    }
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
    (*env)->PopLocalFrame(env, NULL);
    mel_clip_job_resolve(job, MEL_CLIP_OK);
}

bool mel_clip__plat_available(void) { return true; }

u64 mel_clip__plat_sequence(void) { return 0; }

void* mel_clip__plat_native(void) { return NULL; }
