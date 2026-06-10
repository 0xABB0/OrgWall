#pragma once

#include "../gui_internal.h"

#include <layout/linear.h>

#include <jni.h>

#include <paint/painter.h>
#include <paint/native_android.h>

/* Cross-widget JNI handles. The View/TextView/ViewGroup ids are framework
 * methods we drive w->native (the cached View jobject) with directly — there is
 * no generic MelGui denominator and no handle->view map on the Java side. */
typedef struct
{
    float density;

    jmethodID view_setVisibility;     /* (I)V */
    jmethodID view_setEnabled;        /* (Z)V */
    jmethodID view_requestFocus;      /* ()Z  */
    jmethodID view_invalidate;        /* ()V  */
    jmethodID view_setLayoutParams;   /* (Landroid/view/ViewGroup$LayoutParams;)V */
    jmethodID view_getParent;         /* ()Landroid/view/ViewParent; */
    jmethodID view_setTag;            /* (Ljava/lang/Object;)V */
    jmethodID view_getTag;            /* ()Ljava/lang/Object; */
    jmethodID view_measure;           /* (II)V */
    jmethodID view_getMeasuredWidth;  /* ()I */
    jmethodID view_getMeasuredHeight; /* ()I */

    jclass    vg_cls;        /* android/view/ViewGroup */
    jmethodID vg_addView;    /* (Landroid/view/View;)V */
    jmethodID vg_removeView; /* (Landroid/view/View;)V */

    jclass    lp_cls;        /* android/widget/FrameLayout$LayoutParams */
    jmethodID lp_ctor;       /* (II)V */
    jfieldID  lp_leftMargin; /* I */
    jfieldID  lp_topMargin;  /* I */

    jclass    llp_cls;          /* android/widget/LinearLayout$LayoutParams */
    jmethodID llp_ctor;         /* (IIF)V */
    jfieldID  llp_gravity;      /* I */
    jfieldID  llp_leftMargin;   /* I */
    jfieldID  llp_topMargin;    /* I */
    jfieldID  llp_rightMargin;  /* I */
    jfieldID  llp_bottomMargin; /* I */

    jclass    tv_cls;     /* android/widget/TextView */
    jmethodID tv_setText; /* (Ljava/lang/CharSequence;)V */
    jmethodID tv_getText; /* ()Ljava/lang/CharSequence; */

    jmethodID obj_toString; /* java/lang/Object.toString ()Ljava/lang/String; */

    jclass    mel_cls;              /* orgwall/melody/platform/MelGui */
    jmethodID mel_presentFrame;     /* (Landroid/view/View;)V */
    jmethodID mel_back;             /* ()Z */
    jmethodID mel_setActivityTitle; /* (Ljava/lang/String;)V */
    jmethodID mel_installFocus;     /* (Landroid/view/View;JJJ)V */
} Mel_Gui_Android;

/* A user callback round-trips through Java as a jlong stored on the listener;
 * the JNI trampoline casts it back to the matching signature and invokes it. */
typedef void (*Mel_Cb_Void)(Mel_Gui_Handle, void*);
typedef void (*Mel_Cb_Bool)(Mel_Gui_Handle, bool, void*);
typedef void (*Mel_Cb_I32)(Mel_Gui_Handle, i32, void*);
typedef void (*Mel_Cb_Point)(Mel_Gui_Handle, i32, i32, void*);
typedef void (*Mel_Cb_Str8)(Mel_Gui_Handle, str8, void*);
typedef void (*Mel_Cb_Paint)(Mel_Gui_Handle, Mel_Painter*, i32, i32, void*);
typedef void (*Mel_Cb_Resize)(Mel_Gui_Handle, i32, i32, void*);
typedef void (*Mel_Cb_Insets)(Mel_Gui_Handle, const Mel_Frame_Insets*, void*);

Mel_Gui_Android* mel_gui__android(void);
JNIEnv*          mel_gui__android_env(void);
void             mel_gui__android_set_density(float density);
bool             mel_gui__android_register(JNIEnv* env, jclass cls);
jstring          mel_gui__android_jstring(JNIEnv* env, str8 s);
jlong            mel_gui__android_pack(Mel_Gui_Handle h);
Mel_Gui_Handle   mel_gui__android_unpack(jlong p);
int              mel_gui__android_dp2px(i32 dp);
i32              mel_gui__android_px2dp(int px);

/* LinearLayout.LayoutParams (a local ref) for `child` under a container lowered
 * to `lin`: main-axis size from fixed/preferred (else weight-driven 0, else
 * wrap), cross-axis stretch via MATCH_PARENT, gravity from the resolved cross
 * align, margins from the layoutable. */
jobject mel_gui__android_linear_params(JNIEnv* env, const Mel_Linear_Layout* lin, const Mel_Gui_Node* child);

/* Make `view` a child of n->parent's View at n's bounds, then pin it as
 * n->native (a global ref). */
void mel_gui__android_attach(Mel_Gui_Node* n, jobject view);

/* Install the shared focus listener when either focus slot is set. */
void mel_gui__android_install_focus(JNIEnv* env, jobject view, Mel_Gui_Handle h, Mel_Gui_Focus_Cb focus);

bool mel_gui__android_layout_register_jni(JNIEnv* env);
bool mel_gui__android_style_register_jni(JNIEnv* env);
bool mel_gui__android_frame_register_jni(JNIEnv* env);
bool mel_gui__android_dialog_register_jni(JNIEnv* env);
bool mel_gui__android_panel_register_jni(JNIEnv* env);
bool mel_gui__android_groupbox_register_jni(JNIEnv* env);
bool mel_gui__android_scrollview_register_jni(JNIEnv* env);
bool mel_gui__android_tabview_register_jni(JNIEnv* env);
bool mel_gui__android_splitter_register_jni(JNIEnv* env);
bool mel_gui__android_label_register_jni(JNIEnv* env);
bool mel_gui__android_button_register_jni(JNIEnv* env);
bool mel_gui__android_checkbox_register_jni(JNIEnv* env);
bool mel_gui__android_textfield_register_jni(JNIEnv* env);
bool mel_gui__android_slider_register_jni(JNIEnv* env);
bool mel_gui__android_canvas_register_jni(JNIEnv* env);
bool mel_gui__android_gpu_view_register_jni(JNIEnv* env);
