#include <gui.platform.android/gui.platform.android.h>
#include <gui.platform/gui.platform.h>
#include <gui/gui.h>

#include <allocator/allocator.h>
#include <allocator.heap/heap.h>
#include <collection.array/array.h>
#include <string/str8.h>

#include <string.h>

typedef struct {
    Mel_Atom                   atom;
    Mel_Gui_Android_Construct  cb;
} Mel_Android_Ctor;

static JavaVM*   mel__a_vm;
static jobject   mel__a_host;
static jclass    mel__a_host_class;

static jmethodID mel__a_attach;
static jmethodID mel__a_detach;
static jmethodID mel__a_set_text;
static jmethodID mel__a_get_text;
static jmethodID mel__a_set_window_pos;
static jmethodID mel__a_bind_handle;
static jmethodID mel__a_bind_edit_watcher;
static jmethodID mel__a_start_activity;
static jmethodID mel__a_get_activity;
static jmethodID mel__a_get_root;
static jmethodID mel__a_post;
static jmethodID mel__a_request_exit;

static jclass    mel__a_listener_class;
static jmethodID mel__a_listener_ctor;
static jmethodID mel__a_view_set_on_focus_change;

static Mel_Array(Mel_Android_Ctor) mel__a_ctors;
static bool mel__a_ctors_inited;

static void mel__a_ensure_ctors(void)
{
    if (!mel__a_ctors_inited) {
        mel_array_init(&mel__a_ctors, mel_alloc_heap());
        mel__a_ctors_inited = true;
    }
}

JNIEnv* mel_gui_android_env(void)
{
    JNIEnv* env = NULL;
    if (mel__a_vm == NULL) return NULL;
    if ((*mel__a_vm)->GetEnv(mel__a_vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        (*mel__a_vm)->AttachCurrentThread(mel__a_vm, &env, NULL);
    }
    return env;
}

jobject mel_gui_android_host(void)        { return mel__a_host; }
jclass  mel_gui_android_host_class(void)  { return mel__a_host_class; }

jstring mel_gui_android_utf16(JNIEnv* env, str8 s)
{
    if (env == NULL || s.data == NULL || s.len <= 0) return (*env)->NewString(env, NULL, 0);

    const Mel_Alloc* alloc = mel_alloc_heap();
    jchar* buf = mel_alloc(alloc, sizeof(jchar) * (usize)(s.len + 1));
    if (buf == NULL) return NULL;

    jsize out = 0;
    for (size i = 0; i < s.len; ) {
        u32 cp;
        u8 c = s.data[i];
        if (c < 0x80) {
            cp = c;
            i += 1;
        } else if ((c & 0xE0u) == 0xC0u && i + 1 < s.len) {
            cp = ((u32)(c & 0x1Fu) << 6) | (u32)(s.data[i + 1] & 0x3Fu);
            i += 2;
        } else if ((c & 0xF0u) == 0xE0u && i + 2 < s.len) {
            cp = ((u32)(c & 0x0Fu) << 12) | ((u32)(s.data[i + 1] & 0x3Fu) << 6) | (u32)(s.data[i + 2] & 0x3Fu);
            i += 3;
        } else if ((c & 0xF8u) == 0xF0u && i + 3 < s.len) {
            cp = ((u32)(c & 0x07u) << 18) | ((u32)(s.data[i + 1] & 0x3Fu) << 12)
               | ((u32)(s.data[i + 2] & 0x3Fu) << 6) | (u32)(s.data[i + 3] & 0x3Fu);
            i += 4;
        } else {
            cp = 0xFFFD;
            i += 1;
        }
        if (cp < 0x10000) {
            buf[out++] = (jchar)cp;
        } else {
            cp -= 0x10000;
            buf[out++] = (jchar)(0xD800 | (cp >> 10));
            buf[out++] = (jchar)(0xDC00 | (cp & 0x3FF));
        }
    }
    jstring r = (*env)->NewString(env, buf, out);
    mel_dealloc(alloc, buf);
    return r;
}

str8 mel_gui_android_str8(JNIEnv* env, jstring s, const Mel_Alloc* alloc)
{
    if (env == NULL || s == NULL) return STR8_EMPTY;

    jsize ulen = (*env)->GetStringLength(env, s);
    const jchar* chars = (*env)->GetStringChars(env, s, NULL);
    if (chars == NULL) return STR8_EMPTY;

    usize cap = (usize)ulen * 4u + 1u;
    u8* buf = mel_alloc(alloc, cap);
    if (buf == NULL) {
        (*env)->ReleaseStringChars(env, s, chars);
        return STR8_EMPTY;
    }

    usize out = 0;
    for (jsize i = 0; i < ulen; ) {
        u32 cp = chars[i++];
        if (cp >= 0xD800 && cp <= 0xDBFF && i < ulen) {
            u32 low = chars[i];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                i += 1;
            }
        }
        if (cp < 0x80) {
            buf[out++] = (u8)cp;
        } else if (cp < 0x800) {
            buf[out++] = (u8)(0xC0u | (cp >> 6));
            buf[out++] = (u8)(0x80u | (cp & 0x3Fu));
        } else if (cp < 0x10000) {
            buf[out++] = (u8)(0xE0u | (cp >> 12));
            buf[out++] = (u8)(0x80u | ((cp >> 6) & 0x3Fu));
            buf[out++] = (u8)(0x80u | (cp & 0x3Fu));
        } else {
            buf[out++] = (u8)(0xF0u | (cp >> 18));
            buf[out++] = (u8)(0x80u | ((cp >> 12) & 0x3Fu));
            buf[out++] = (u8)(0x80u | ((cp >> 6) & 0x3Fu));
            buf[out++] = (u8)(0x80u | (cp & 0x3Fu));
        }
    }
    (*env)->ReleaseStringChars(env, s, chars);
    return (str8){ .data = buf, .len = (size)out };
}

void mel_gui_android_str8_free(JNIEnv* env, str8 s, const Mel_Alloc* alloc)
{
    (void)env;
    if (s.data != NULL) mel_dealloc(alloc, s.data);
}

bool mel_gui_android_attach(JavaVM* vm, JNIEnv* env, jobject host)
{
    if (vm == NULL || env == NULL || host == NULL) return false;

    if (mel__a_host != NULL)       (*env)->DeleteGlobalRef(env, mel__a_host);
    if (mel__a_host_class != NULL) (*env)->DeleteGlobalRef(env, mel__a_host_class);

    mel__a_vm = vm;
    mel__a_host = (*env)->NewGlobalRef(env, host);

    jclass local = (*env)->GetObjectClass(env, host);
    mel__a_host_class = (*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);

    mel__a_attach            = (*env)->GetMethodID(env, mel__a_host_class, "attach",                "(Landroid/view/View;Landroid/view/View;IIII)V");
    mel__a_detach            = (*env)->GetMethodID(env, mel__a_host_class, "detach",                "(Landroid/view/View;)V");
    mel__a_set_text          = (*env)->GetMethodID(env, mel__a_host_class, "setText",               "(Landroid/view/View;Ljava/lang/String;)V");
    mel__a_get_text          = (*env)->GetMethodID(env, mel__a_host_class, "getText",               "(Landroid/view/View;)Ljava/lang/String;");
    mel__a_set_window_pos    = (*env)->GetMethodID(env, mel__a_host_class, "setWindowPos",          "(Landroid/view/View;IIIII)V");
    mel__a_bind_handle       = (*env)->GetMethodID(env, mel__a_host_class, "bindNativeHandle",      "(Landroid/view/View;J)V");
    mel__a_bind_edit_watcher = (*env)->GetMethodID(env, mel__a_host_class, "bindEditWatcher",       "(Landroid/widget/EditText;Landroid/text/TextWatcher;)V");
    mel__a_start_activity    = (*env)->GetMethodID(env, mel__a_host_class, "scheduleStartActivity", "(Ljava/lang/String;)V");
    mel__a_get_activity      = (*env)->GetMethodID(env, mel__a_host_class, "getActivity",           "()Landroid/app/Activity;");
    mel__a_get_root          = (*env)->GetMethodID(env, mel__a_host_class, "getRoot",               "()Landroid/view/View;");
    mel__a_post              = (*env)->GetMethodID(env, mel__a_host_class, "post",                  "(JIJJ)V");
    mel__a_request_exit      = (*env)->GetMethodID(env, mel__a_host_class, "requestExit",           "()V");

    jclass listener_local = (*env)->FindClass(env, "orgwall/melody/platform/MelListener");
    if (listener_local != NULL) {
        mel__a_listener_class = (*env)->NewGlobalRef(env, listener_local);
        (*env)->DeleteLocalRef(env, listener_local);
        mel__a_listener_ctor = (*env)->GetMethodID(env, mel__a_listener_class, "<init>", "(J)V");
    }

    return mel__a_attach != NULL && mel__a_set_text != NULL && mel__a_get_text != NULL
        && mel__a_set_window_pos != NULL
        && mel__a_bind_handle != NULL && mel__a_bind_edit_watcher != NULL
        && mel__a_start_activity != NULL && mel__a_get_activity != NULL
        && mel__a_get_root != NULL && mel__a_post != NULL && mel__a_request_exit != NULL
        && mel__a_listener_class != NULL && mel__a_listener_ctor != NULL;
}

jobject mel_gui_android_activity(JNIEnv* env)
{
    if (mel__a_host == NULL || mel__a_get_activity == NULL) return NULL;
    return (*env)->CallObjectMethod(env, mel__a_host, mel__a_get_activity);
}

jlong mel_gui_android_pack_handle(Mel_Gui_Handle h)
{
    return (jlong)((u64)h.handle.generation << 32 | (u64)h.handle.index);
}

jobject mel_gui_android_new_listener(JNIEnv* env, Mel_Gui_Handle h)
{
    if (mel__a_listener_class == NULL || mel__a_listener_ctor == NULL) return NULL;
    return (*env)->NewObject(env, mel__a_listener_class, mel__a_listener_ctor, mel_gui_android_pack_handle(h));
}

void mel_gui_android_bind_edit_watcher(JNIEnv* env, jobject edit, jobject watcher)
{
    if (mel__a_host == NULL || mel__a_bind_edit_watcher == NULL) return;
    (*env)->CallVoidMethod(env, mel__a_host, mel__a_bind_edit_watcher, edit, watcher);
}

void mel_gui_android_install_focus_listener(JNIEnv* env, jobject view, jobject listener)
{
    if (view == NULL || listener == NULL) return;
    if (mel__a_view_set_on_focus_change == NULL) {
        jclass view_class = (*env)->FindClass(env, "android/view/View");
        if (view_class == NULL) return;
        mel__a_view_set_on_focus_change = (*env)->GetMethodID(env, view_class, "setOnFocusChangeListener",
            "(Landroid/view/View$OnFocusChangeListener;)V");
        (*env)->DeleteLocalRef(env, view_class);
    }
    (*env)->CallVoidMethod(env, view, mel__a_view_set_on_focus_change, listener);
}

bool mel_gui_platform_init(void)  { return true; }
void mel_gui_platform_shutdown(void) {}

bool mel_gui_android_register_constructor(Mel_Atom atom, Mel_Gui_Android_Construct cb)
{
    if (atom == MEL_ATOM_NONE || cb == NULL) return false;
    mel__a_ensure_ctors();
    for (usize i = 0; i < mel__a_ctors.count; i++) {
        if (mel__a_ctors.items[i].atom == atom) {
            mel__a_ctors.items[i].cb = cb;
            return true;
        }
    }
    mel_array_push(&mel__a_ctors, ((Mel_Android_Ctor){ .atom = atom, .cb = cb }));
    return true;
}

static Mel_Gui_Android_Construct mel__a_lookup_ctor(Mel_Atom atom)
{
    if (!mel__a_ctors_inited) return NULL;
    for (usize i = 0; i < mel__a_ctors.count; i++) {
        if (mel__a_ctors.items[i].atom == atom) return mel__a_ctors.items[i].cb;
    }
    return NULL;
}

void* mel_gui_platform_create(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc, Mel_Atom platform_class)
{
    if (mel__a_host == NULL) return NULL;

    Mel_Gui_Android_Construct cb = mel__a_lookup_ctor(platform_class);
    if (cb == NULL) return NULL;

    JNIEnv* env = mel_gui_android_env();
    jobject local = cb(env, mel__a_host, mel__a_host_class, h, desc);
    if (local == NULL) return NULL;

    jobject global = (*env)->NewGlobalRef(env, local);

    jobject parent_view;
    bool parent_is_root = false;
    if (!mel_gui_handle_is_none(desc->parent)) {
        parent_view = (jobject)mel_gui_platform_native(desc->parent);
    } else {
        parent_view = (*env)->CallObjectMethod(env, mel__a_host, mel__a_get_root);
        parent_is_root = true;
    }

    jlong packed = mel_gui_android_pack_handle(h);
    (*env)->CallVoidMethod(env, mel__a_host, mel__a_attach, parent_view, local, desc->x, desc->y, desc->w, desc->h);
    (*env)->CallVoidMethod(env, mel__a_host, mel__a_bind_handle, local, packed);

    if (parent_is_root) {
        (*env)->DeleteLocalRef(env, parent_view);
    }
    (*env)->DeleteLocalRef(env, local);
    return global;
}

void mel_gui_platform_destroy(Mel_Gui_Handle h)
{
    if (mel__a_host == NULL) return;
    jobject view = (jobject)mel_gui_platform_native(h);
    if (view == NULL) return;

    JNIEnv* env = mel_gui_android_env();
    (*env)->CallVoidMethod(env, mel__a_host, mel__a_detach, view);
    (*env)->DeleteGlobalRef(env, view);
    mel_gui_platform_bind_native(h, NULL);
}

bool mel_gui_platform_set_window_pos(Mel_Gui_Handle h, i32 x, i32 y, i32 w, i32 hgt, u32 flags)
{
    if (mel__a_host == NULL) return false;
    jobject view = (jobject)mel_gui_platform_native(h);
    if (view == NULL) return false;
    JNIEnv* env = mel_gui_android_env();
    (*env)->CallVoidMethod(env, mel__a_host, mel__a_set_window_pos, view, x, y, w, hgt, (jint)flags);
    return true;
}

bool mel_gui_platform_set_text(Mel_Gui_Handle h, str8 text)
{
    if (mel__a_host == NULL) return false;
    jobject view = (jobject)mel_gui_platform_native(h);
    if (view == NULL) return false;
    JNIEnv* env = mel_gui_android_env();
    jstring j = mel_gui_android_utf16(env, text);
    (*env)->CallVoidMethod(env, mel__a_host, mel__a_set_text, view, j);
    (*env)->DeleteLocalRef(env, j);
    return true;
}

bool mel_gui_platform_post_message(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    if (mel__a_host == NULL || mel__a_post == NULL) return false;
    JNIEnv* env = mel_gui_android_env();
    jlong packed = mel_gui_android_pack_handle(h);
    (*env)->CallVoidMethod(env, mel__a_host, mel__a_post, packed, (jint)msg, (jlong)(i64)w, (jlong)l);
    return true;
}

size mel_gui_platform_get_text(Mel_Gui_Handle h, char* buf, size cap)
{
    if (mel__a_host == NULL || mel__a_get_text == NULL) return 0;
    jobject view = (jobject)mel_gui_platform_native(h);
    if (view == NULL) return 0;
    JNIEnv* env = mel_gui_android_env();
    jstring jstr = (*env)->CallObjectMethod(env, mel__a_host, mel__a_get_text, view);
    if (jstr == NULL) return 0;

    str8 text = mel_gui_android_str8(env, jstr, mel_alloc_heap());
    (*env)->DeleteLocalRef(env, jstr);

    size n = text.len;
    if (buf != NULL && cap > 0) {
        size to_copy = (n < cap - 1) ? n : (cap - 1);
        if (to_copy > 0 && text.data != NULL) memcpy(buf, text.data, (usize)to_copy);
        buf[to_copy] = 0;
    }
    mel_gui_android_str8_free(env, text, mel_alloc_heap());
    return n;
}

bool mel_gui_platform_invalidate(Mel_Gui_Handle h)
{
    jobject view = (jobject)mel_gui_platform_native(h);
    if (view == NULL) return false;
    JNIEnv* env = mel_gui_android_env();
    static jmethodID jmid;
    if (jmid == NULL) {
        jclass view_class = (*env)->FindClass(env, "android/view/View");
        if (view_class == NULL) return false;
        jmid = (*env)->GetMethodID(env, view_class, "invalidate", "()V");
        (*env)->DeleteLocalRef(env, view_class);
        if (jmid == NULL) return false;
    }
    (*env)->CallVoidMethod(env, view, jmid);
    return true;
}

bool mel_gui_platform_invalidate_rect(Mel_Gui_Handle h, i32 x, i32 y, i32 w, i32 hgt)
{
    jobject view = (jobject)mel_gui_platform_native(h);
    if (view == NULL) return false;
    JNIEnv* env = mel_gui_android_env();
    static jmethodID jmid;
    if (jmid == NULL) {
        jclass view_class = (*env)->FindClass(env, "android/view/View");
        if (view_class == NULL) return false;
        jmid = (*env)->GetMethodID(env, view_class, "invalidate", "(IIII)V");
        (*env)->DeleteLocalRef(env, view_class);
        if (jmid == NULL) return false;
    }
    (*env)->CallVoidMethod(env, view, jmid, (jint)x, (jint)y, (jint)(x + w), (jint)(y + hgt));
    return true;
}

bool mel_gui_platform_set_focus(Mel_Gui_Handle h)
{
    if (mel__a_host == NULL) return false;
    JNIEnv* env = mel_gui_android_env();
    jclass view_class = (*env)->FindClass(env, "android/view/View");
    if (view_class == NULL) return false;

    bool ok = false;
    if (mel_gui_handle_is_none(h)) {
        jobject root = (*env)->CallObjectMethod(env, mel__a_host, mel__a_get_root);
        if (root != NULL) {
            jmethodID clear_focus = (*env)->GetMethodID(env, view_class, "clearFocus", "()V");
            (*env)->CallVoidMethod(env, root, clear_focus);
            (*env)->DeleteLocalRef(env, root);
            ok = true;
        }
    } else {
        jobject view = (jobject)mel_gui_platform_native(h);
        if (view != NULL) {
            jmethodID request_focus = (*env)->GetMethodID(env, view_class, "requestFocus", "()Z");
            ok = (*env)->CallBooleanMethod(env, view, request_focus) == JNI_TRUE;
        }
    }
    (*env)->DeleteLocalRef(env, view_class);
    return ok;
}

void mel_gui_platform_request_exit(void)
{
    if (mel__a_host == NULL || mel__a_request_exit == NULL) return;
    JNIEnv* env = mel_gui_android_env();
    (*env)->CallVoidMethod(env, mel__a_host, mel__a_request_exit);
}

bool mel_gui_app_start_activity(str8 activity_name)
{
    if (mel__a_host == NULL || mel__a_start_activity == NULL) return false;
    JNIEnv* env = mel_gui_android_env();
    jstring j = mel_gui_android_utf16(env, activity_name);
    (*env)->CallVoidMethod(env, mel__a_host, mel__a_start_activity, j);
    (*env)->DeleteLocalRef(env, j);
    return true;
}
