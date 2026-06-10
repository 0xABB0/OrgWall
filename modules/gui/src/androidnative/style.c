#include "android.h"

static jclass    s_cls;
static jmethodID s_applyText;            /* (Landroid/view/View;ZIFIZLjava/lang/String;)V */
static jmethodID s_applySurface;         /* (Landroid/view/View;ZIZIFFIIII)V */
static jmethodID s_applySlider;          /* (Landroid/view/View;ZIZI)V */
static jmethodID s_applyCheckTint;       /* (Landroid/view/View;I)V */
static jmethodID s_applyGroupBoxTitle;   /* (Landroid/view/View;ZIFIZLjava/lang/String;)V */
static jmethodID s_applySplitterDivider; /* (Landroid/view/View;I)V */

bool mel_gui__android_style_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelStyle");
    if (!cls)
        return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_applyText = (*env)->GetStaticMethodID(env, s_cls, "applyText", "(Landroid/view/View;ZIFIZLjava/lang/String;)V");
    s_applySurface = (*env)->GetStaticMethodID(env, s_cls, "applySurface", "(Landroid/view/View;ZIZIFFIIII)V");
    s_applySlider = (*env)->GetStaticMethodID(env, s_cls, "applySlider", "(Landroid/view/View;ZIZI)V");
    s_applyCheckTint = (*env)->GetStaticMethodID(env, s_cls, "applyCheckTint", "(Landroid/view/View;I)V");
    s_applyGroupBoxTitle = (*env)->GetStaticMethodID(env, s_cls, "applyGroupBoxTitle", "(Landroid/view/View;ZIFIZLjava/lang/String;)V");
    s_applySplitterDivider = (*env)->GetStaticMethodID(env, s_cls, "applySplitterDivider", "(Landroid/view/View;I)V");
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return false;
    }
    return s_applyText && s_applySurface && s_applySlider && s_applyCheckTint && s_applyGroupBoxTitle && s_applySplitterDivider;
}

static jint argb(mel_color8 c) { return (jint)(((u32)c.a << 24) | ((u32)c.r << 16) | ((u32)c.g << 8) | (u32)c.b); }

static jobject style_view(Mel_Gui_Handle h, JNIEnv** env_out)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native)
        return NULL;
    JNIEnv* env = mel_gui__android_env();
    if (!env)
        return NULL;
    *env_out = env;
    return (jobject)n->native;
}

static void apply_text(JNIEnv* env, jobject view, jmethodID mid, const Mel_Font* font, Mel_Style_Color fg)
{
    jstring family = mel_gui__android_jstring(env, font->family);
    (*env)->CallStaticVoidMethod(env, s_cls, mid, view, (jboolean)fg.set, argb(fg.color), (jfloat)font->size, (jint)font->weight, (jboolean)font->italic, family);
    (*env)->DeleteLocalRef(env, family);
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
}

static void apply_surface(JNIEnv* env, jobject view, const Mel_Style_Surface* s)
{
    /* A border needs both a width and a color; style.h names width 0 as "no
     * border", and inventing a color would be a silent default. */
    bool has_border = s->border_color.set && s->border_width > 0;

    (*env)->CallStaticVoidMethod(env,
                                 s_cls,
                                 s_applySurface,
                                 view,
                                 (jboolean)s->bg.set,
                                 argb(s->bg.color),
                                 (jboolean)has_border,
                                 argb(s->border_color.color),
                                 (jfloat)s->border_width,
                                 (jfloat)s->corner_radius,
                                 (jint)mel_gui__android_dp2px(s->padding_l),
                                 (jint)mel_gui__android_dp2px(s->padding_t),
                                 (jint)mel_gui__android_dp2px(s->padding_r),
                                 (jint)mel_gui__android_dp2px(s->padding_b));
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
}

void mel_label_set_style_opt(Mel_Gui_Handle h, Mel_Label_Style style)
{
    JNIEnv* env;
    jobject view = style_view(h, &env);
    if (!view)
        return;
    if (mel_font_any(&style.font) || style.fg.set)
        apply_text(env, view, s_applyText, &style.font, style.fg);
    if (mel_style_surface_any(&style.surface))
        apply_surface(env, view, &style.surface);
}

void mel_button_set_style_opt(Mel_Gui_Handle h, Mel_Button_Style style)
{
    JNIEnv* env;
    jobject view = style_view(h, &env);
    if (!view)
        return;
    if (mel_font_any(&style.font) || style.fg.set)
        apply_text(env, view, s_applyText, &style.font, style.fg);
    if (mel_style_surface_any(&style.surface))
        apply_surface(env, view, &style.surface);
}

void mel_textfield_set_style_opt(Mel_Gui_Handle h, Mel_TextField_Style style)
{
    JNIEnv* env;
    jobject view = style_view(h, &env);
    if (!view)
        return;
    if (mel_font_any(&style.font) || style.fg.set)
        apply_text(env, view, s_applyText, &style.font, style.fg);
    if (mel_style_surface_any(&style.surface))
        apply_surface(env, view, &style.surface);
}

void mel_checkbox_set_style_opt(Mel_Gui_Handle h, Mel_CheckBox_Style style)
{
    JNIEnv* env;
    jobject view = style_view(h, &env);
    if (!view)
        return;
    if (mel_font_any(&style.font) || style.fg.set)
        apply_text(env, view, s_applyText, &style.font, style.fg);
    if (style.tint.set)
    {
        (*env)->CallStaticVoidMethod(env, s_cls, s_applyCheckTint, view, argb(style.tint.color));
        if ((*env)->ExceptionCheck(env))
            (*env)->ExceptionClear(env);
    }
    if (mel_style_surface_any(&style.surface))
        apply_surface(env, view, &style.surface);
}

void mel_slider_set_style_opt(Mel_Gui_Handle h, Mel_Slider_Style style)
{
    JNIEnv* env;
    jobject view = style_view(h, &env);
    if (!view)
        return;
    if (style.track.set || style.thumb.set)
    {
        (*env)->CallStaticVoidMethod(env, s_cls, s_applySlider, view, (jboolean)style.track.set, argb(style.track.color), (jboolean)style.thumb.set, argb(style.thumb.color));
        if ((*env)->ExceptionCheck(env))
            (*env)->ExceptionClear(env);
    }
    if (mel_style_surface_any(&style.surface))
        apply_surface(env, view, &style.surface);
}

void mel_groupbox_set_style_opt(Mel_Gui_Handle h, Mel_GroupBox_Style style)
{
    JNIEnv* env;
    jobject view = style_view(h, &env);
    if (!view)
        return;
    if (mel_font_any(&style.title_font) || style.title_fg.set)
        apply_text(env, view, s_applyGroupBoxTitle, &style.title_font, style.title_fg);
    if (mel_style_surface_any(&style.surface))
        apply_surface(env, view, &style.surface);
}

void mel_splitter_set_style_opt(Mel_Gui_Handle h, Mel_Splitter_Style style)
{
    JNIEnv* env;
    jobject view = style_view(h, &env);
    if (!view)
        return;
    if (style.divider.set)
    {
        (*env)->CallStaticVoidMethod(env, s_cls, s_applySplitterDivider, view, argb(style.divider.color));
        if ((*env)->ExceptionCheck(env))
            (*env)->ExceptionClear(env);
    }
    if (mel_style_surface_any(&style.surface))
        apply_surface(env, view, &style.surface);
}

static void set_surface_style(Mel_Gui_Handle h, const Mel_Style_Surface* surface)
{
    JNIEnv* env;
    jobject view = style_view(h, &env);
    if (!view)
        return;
    if (mel_style_surface_any(surface))
        apply_surface(env, view, surface);
}

void mel_panel_set_style_opt(Mel_Gui_Handle h, Mel_Panel_Style style) { set_surface_style(h, &style.surface); }
void mel_canvas_set_style_opt(Mel_Gui_Handle h, Mel_Canvas_Style style) { set_surface_style(h, &style.surface); }
void mel_scrollview_set_style_opt(Mel_Gui_Handle h, Mel_ScrollView_Style style) { set_surface_style(h, &style.surface); }
void mel_frame_set_style_opt(Mel_Gui_Handle h, Mel_Frame_Style style) { set_surface_style(h, &style.surface); }
void mel_dialog_set_style_opt(Mel_Gui_Handle h, Mel_Dialog_Style style) { set_surface_style(h, &style.surface); }
void mel_tabview_set_style_opt(Mel_Gui_Handle h, Mel_TabView_Style style) { set_surface_style(h, &style.surface); }
void mel_tab_set_style_opt(Mel_Gui_Handle h, Mel_Tab_Style style) { set_surface_style(h, &style.surface); }
void mel_splitpane_set_style_opt(Mel_Gui_Handle h, Mel_SplitPane_Style style) { set_surface_style(h, &style.surface); }
