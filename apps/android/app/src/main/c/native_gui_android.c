#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <android/log.h>
#include <gui.control/gui.control.h>
#include <gui.platform.android/gui.platform.android.h>

#define MEL_ANDROID_LOG_TAG "MelodyGui"
#define MEL_ANDROID_LOG(...) __android_log_print(ANDROID_LOG_INFO, MEL_ANDROID_LOG_TAG, __VA_ARGS__)
#define DEMO_CLASS_COUNTER_BUTTON S8("demo.counter_button")
#define DEMO_CLASS_OPEN_DETAILS_BUTTON S8("demo.open_details_button")

typedef struct Main_State {
    Mel_Gui_Handle window;
    Mel_Gui_Handle status;
    Mel_Gui_Handle edit;
    Mel_Gui_Handle button;
    Mel_Gui_Handle checkbox;
    Mel_Gui_Handle slider_label;
    i32 clicks;
    i32 slider;
    bool checked;
    char edit_text[128];
} Main_State;

typedef struct Details_State {
    Mel_Gui_Handle window;
    Mel_Gui_Handle status;
    i32 taps;
} Details_State;

typedef void (*Build_Activity_Fn)(JNIEnv* env, jobject host);

typedef struct Native_Activity_Desc {
    const char* name;
    Build_Activity_Fn build;
} Native_Activity_Desc;

static JavaVM* g_vm;
static bool g_classes_registered;
static Main_State g_main;
static Details_State g_details;

static void update_main_status(void)
{
    char text[256];
    snprintf(
        text,
        sizeof(text),
        "C saw: clicks=%d, checked=%s, slider=%d, edit=\"%s\"",
        g_main.clicks,
        g_main.checked ? "yes" : "no",
        g_main.slider,
        g_main.edit_text);
    mel_gui_set_text(g_main.status, str8_from_cstr(text));
}

static void update_details_status(void)
{
    char text[128];
    snprintf(text, sizeof(text), "Details Activity has its own C state: taps=%d", g_details.taps);
    mel_gui_set_text(g_details.status, str8_from_cstr(text));
}

static Mel_Gui_Result main_window_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam wparam, Mel_Gui_LParam lparam)
{
    MEL_UNUSED(h);
    MEL_UNUSED(lparam);

    if (msg == MEL_GUI_MSG_COMMAND) {
        Mel_Gui_Handle child = (Mel_Gui_Handle)(intptr_t)wparam;
        MEL_ANDROID_LOG("main activity command from id=%u", mel_gui_id(child));
        update_main_status();
        return 1;
    }

    return 0;
}

static Mel_Gui_Result details_window_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam wparam, Mel_Gui_LParam lparam)
{
    MEL_UNUSED(h);
    MEL_UNUSED(wparam);
    MEL_UNUSED(lparam);

    if (msg == MEL_GUI_MSG_COMMAND) {
        update_details_status();
        return 1;
    }

    return 0;
}

static Mel_Gui_Result counter_button_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam wparam, Mel_Gui_LParam lparam)
{
    MEL_UNUSED(wparam);
    MEL_UNUSED(lparam);

    if (msg == MEL_GUI_MSG_CLICK) {
        g_main.clicks += 1;
        mel_gui_set_text(h, S8("Handled by class proc"));
        update_main_status();
        return 1;
    }

    return 0;
}

static Mel_Gui_Result open_details_button_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam wparam, Mel_Gui_LParam lparam)
{
    MEL_UNUSED(h);
    MEL_UNUSED(wparam);
    MEL_UNUSED(lparam);

    if (msg == MEL_GUI_MSG_CLICK) {
        mel_gui_android_start_activity(S8("details"));
        return 1;
    }

    return 0;
}

static Mel_Gui_Result details_button_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam wparam, Mel_Gui_LParam lparam)
{
    MEL_UNUSED(h);
    MEL_UNUSED(wparam);
    MEL_UNUSED(lparam);

    if (msg == MEL_GUI_MSG_CLICK) {
        g_details.taps += 1;
        update_details_status();
        return 1;
    }

    return 0;
}

static Mel_Gui_Result checkbox_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam wparam, Mel_Gui_LParam lparam)
{
    MEL_UNUSED(h);
    MEL_UNUSED(lparam);

    if (msg == MEL_GUI_MSG_VALUE_CHANGED) {
        g_main.checked = wparam != 0;
        update_main_status();
        return 1;
    }

    return 0;
}

static Mel_Gui_Result slider_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam wparam, Mel_Gui_LParam lparam)
{
    MEL_UNUSED(h);
    MEL_UNUSED(lparam);

    if (msg == MEL_GUI_MSG_VALUE_CHANGED) {
        g_main.slider = (i32)wparam;
        char text[32];
        snprintf(text, sizeof(text), "%d", g_main.slider);
        mel_gui_set_text(g_main.slider_label, str8_from_cstr(text));
        update_main_status();
        return 1;
    }

    return 0;
}

static Mel_Gui_Result edit_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam wparam, Mel_Gui_LParam lparam)
{
    MEL_UNUSED(h);
    MEL_UNUSED(wparam);

    if (msg == MEL_GUI_MSG_TEXT_CHANGED) {
        str8* text = (str8*)(intptr_t)lparam;
        if (text != NULL) {
            size n = str8_to_buf(*text, g_main.edit_text, (size)sizeof(g_main.edit_text) - 1);
            g_main.edit_text[n] = 0;
        }
        update_main_status();
        return 1;
    }

    return 0;
}

static void register_demo_classes(void)
{
    if (g_classes_registered) return;

    mel_gui_register_class(&(Mel_Gui_Class_Desc){
        .name = DEMO_CLASS_COUNTER_BUTTON,
        .base_name = MEL_GUI_CLASS_BUTTON,
        .proc = counter_button_proc,
    });
    mel_gui_register_class(&(Mel_Gui_Class_Desc){
        .name = DEMO_CLASS_OPEN_DETAILS_BUTTON,
        .base_name = MEL_GUI_CLASS_BUTTON,
        .proc = open_details_button_proc,
    });

    g_classes_registered = true;
}

static bool attach_activity(JNIEnv* env, jobject host)
{
    if (!mel_gui_android_attach(g_vm, env, host)) return false;
    if (!mel_gui_init()) return false;

    register_demo_classes();
    return true;
}

static void build_main_activity(JNIEnv* env, jobject host)
{
    if (!attach_activity(env, host)) return;

    memset(&g_main, 0, sizeof(g_main));
    strcpy(g_main.edit_text, "native ui");
    g_main.slider = 65;

    g_main.window = mel_gui_create_window(S8("Melody Main Activity"), 0, 0, main_window_proc, &g_main);
    mel_gui_create_child(g_main.window, MEL_GUI_CLASS_LABEL, S8("Main Activity"), 1, 24, 20, 360, 48);
    mel_gui_create_child(g_main.window, MEL_GUI_CLASS_LABEL, S8("This screen is selected and built from C."), 2, 24, 76, 360, 46);
    mel_gui_create_child(g_main.window, DEMO_CLASS_OPEN_DETAILS_BUTTON, S8("Open C-defined Details Activity"), 3, 24, 126, 310, 52);
    g_main.edit = mel_gui_create_child_proc(g_main.window, MEL_GUI_CLASS_EDIT, S8("native ui"), 10, 24, 198, 320, 54, edit_proc, NULL);
    g_main.button = mel_gui_create_child(g_main.window, DEMO_CLASS_COUNTER_BUTTON, S8("Tap custom class"), 11, 24, 272, 210, 52);
    g_main.checkbox = mel_gui_create_child_proc(g_main.window, MEL_GUI_CLASS_CHECKBOX, S8("Own checkbox proc"), 12, 24, 344, 340, 52, checkbox_proc, NULL);
    mel_gui_create_child(g_main.window, MEL_GUI_CLASS_LABEL, S8("Slider value"), 13, 24, 416, 120, 42);
    mel_gui_create_child_proc(g_main.window, MEL_GUI_CLASS_SLIDER, S8(""), 14, 138, 416, 170, 42, slider_proc, NULL);
    g_main.slider_label = mel_gui_create_child(g_main.window, MEL_GUI_CLASS_LABEL, S8("65"), 15, 318, 416, 56, 42);
    g_main.status = mel_gui_create_child(g_main.window, MEL_GUI_CLASS_LABEL, S8(""), 16, 24, 488, 360, 80);

    update_main_status();
    MEL_ANDROID_LOG("built C-defined main activity");
}

static void build_details_activity(JNIEnv* env, jobject host)
{
    if (!attach_activity(env, host)) return;

    memset(&g_details, 0, sizeof(g_details));

    g_details.window = mel_gui_create_window(S8("Melody Details Activity"), 0, 0, details_window_proc, &g_details);
    mel_gui_create_child(g_details.window, MEL_GUI_CLASS_LABEL, S8("Details Activity"), 1, 24, 20, 360, 48);
    mel_gui_create_child(g_details.window, MEL_GUI_CLASS_LABEL, S8("Different Java Activity, different C builder."), 2, 24, 76, 360, 46);
    mel_gui_create_child_proc(g_details.window, MEL_GUI_CLASS_BUTTON, S8("Tap details-local proc"), 21, 24, 144, 250, 52, details_button_proc, NULL);
    g_details.status = mel_gui_create_child(g_details.window, MEL_GUI_CLASS_LABEL, S8(""), 22, 24, 218, 360, 80);

    update_details_status();
    MEL_ANDROID_LOG("built C-defined details activity");
}

static const Native_Activity_Desc g_activities[] = {
    { "main", build_main_activity },
    { "details", build_details_activity },
};

static void jstring_to_buf(JNIEnv* env, jstring value, char* buf, size_t buf_size)
{
    if (buf_size == 0) return;
    buf[0] = 0;
    if (value == NULL) return;

    const char* chars = (*env)->GetStringUTFChars(env, value, NULL);
    if (chars != NULL) {
        snprintf(buf, buf_size, "%s", chars);
        (*env)->ReleaseStringUTFChars(env, value, chars);
    }
}

static const Native_Activity_Desc* find_activity(const char* name)
{
    for (size_t i = 0; i < sizeof(g_activities) / sizeof(g_activities[0]); i += 1) {
        if (strcmp(g_activities[i].name, name) == 0) return &g_activities[i];
    }
    return NULL;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
    MEL_UNUSED(reserved);
    g_vm = vm;
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL Java_orgwall_melody_NativeActivity_nativeBuildActivity(
    JNIEnv* env, jclass cls, jobject host, jstring activity_name)
{
    MEL_UNUSED(cls);

    char name[64];
    jstring_to_buf(env, activity_name, name, sizeof(name));

    const Native_Activity_Desc* activity = find_activity(name);
    if (activity == NULL) {
        MEL_ANDROID_LOG("unknown native activity '%s'", name);
        activity = find_activity("main");
    }

    if (activity != NULL) activity->build(env, host);
}

JNIEXPORT void JNICALL Java_orgwall_melody_NativeActivity_nativeResumeActivity(
    JNIEnv* env, jclass cls, jobject host, jstring activity_name)
{
    MEL_UNUSED(cls);

    char name[64];
    jstring_to_buf(env, activity_name, name, sizeof(name));

    if (attach_activity(env, host)) {
        MEL_ANDROID_LOG("resumed C-defined activity '%s'", name);
    }
}

JNIEXPORT void JNICALL Java_orgwall_melody_NativeGuiHost_nativeDispatchClick(JNIEnv* env, jclass cls, jlong handle)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    mel_gui_android_dispatch_click((Mel_Gui_Handle)(intptr_t)handle);
}

JNIEXPORT void JNICALL Java_orgwall_melody_NativeGuiHost_nativeDispatchValueChanged(
    JNIEnv* env, jclass cls, jlong handle, jint value)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    Mel_Gui_Handle h = (Mel_Gui_Handle)(intptr_t)handle;
    mel_gui_android_dispatch_value_changed(h, value);
}

JNIEXPORT void JNICALL Java_orgwall_melody_NativeGuiHost_nativeDispatchTextChanged(
    JNIEnv* env, jclass cls, jlong handle, jstring value)
{
    MEL_UNUSED(cls);
    char text_buf[128] = {0};
    jstring_to_buf(env, value, text_buf, sizeof(text_buf));

    Mel_Gui_Handle h = (Mel_Gui_Handle)(intptr_t)handle;
    mel_gui_android_dispatch_text_changed(h, str8_from_cstr(text_buf));
}
