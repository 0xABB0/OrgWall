#include "../paint_internal.h"

#include <debug/assert.h>

#include <paint/native_android.h>
#include <paint/painter.h>

#include <string.h>

static jmethodID m_drawRect, m_drawOval, m_drawLine, m_drawRoundRect, m_drawText;
static jmethodID m_setColor, m_setStyle, m_setStrokeWidth, m_setTextSize, m_ascent;
static jobject   s_style_fill, s_style_stroke;
static bool      s_ready;

static bool ensure(JNIEnv* env)
{
    if (s_ready)
        return true;

    jclass canvas = (*env)->FindClass(env, "android/graphics/Canvas");
    jclass paint = (*env)->FindClass(env, "android/graphics/Paint");
    jclass style = (*env)->FindClass(env, "android/graphics/Paint$Style");
    if (!canvas || !paint || !style)
        return false;

    m_drawRect = (*env)->GetMethodID(env, canvas, "drawRect", "(FFFFLandroid/graphics/Paint;)V");
    m_drawOval = (*env)->GetMethodID(env, canvas, "drawOval", "(FFFFLandroid/graphics/Paint;)V");
    m_drawLine = (*env)->GetMethodID(env, canvas, "drawLine", "(FFFFLandroid/graphics/Paint;)V");
    m_drawRoundRect = (*env)->GetMethodID(env, canvas, "drawRoundRect", "(FFFFFFLandroid/graphics/Paint;)V");
    m_drawText = (*env)->GetMethodID(env, canvas, "drawText", "(Ljava/lang/String;FFLandroid/graphics/Paint;)V");

    m_setColor = (*env)->GetMethodID(env, paint, "setColor", "(I)V");
    m_setStyle = (*env)->GetMethodID(env, paint, "setStyle", "(Landroid/graphics/Paint$Style;)V");
    m_setStrokeWidth = (*env)->GetMethodID(env, paint, "setStrokeWidth", "(F)V");
    m_setTextSize = (*env)->GetMethodID(env, paint, "setTextSize", "(F)V");
    m_ascent = (*env)->GetMethodID(env, paint, "ascent", "()F");

    jfieldID f_fill = (*env)->GetStaticFieldID(env, style, "FILL", "Landroid/graphics/Paint$Style;");
    jfieldID f_stroke = (*env)->GetStaticFieldID(env, style, "STROKE", "Landroid/graphics/Paint$Style;");
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return false;
    }

    jobject fill = (*env)->GetStaticObjectField(env, style, f_fill);
    jobject stroke = (*env)->GetStaticObjectField(env, style, f_stroke);
    s_style_fill = (*env)->NewGlobalRef(env, fill);
    s_style_stroke = (*env)->NewGlobalRef(env, stroke);

    (*env)->DeleteLocalRef(env, canvas);
    (*env)->DeleteLocalRef(env, paint);
    (*env)->DeleteLocalRef(env, style);
    (*env)->DeleteLocalRef(env, fill);
    (*env)->DeleteLocalRef(env, stroke);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return false;
    }

    s_ready = m_drawRect && m_drawOval && m_drawLine && m_drawRoundRect && m_drawText && m_setColor && m_setStyle && m_setStrokeWidth && m_setTextSize && m_ascent && s_style_fill && s_style_stroke;
    return s_ready;
}

static inline Mel_Paint_Android_Native* nat(Mel_Painter* p) { return (Mel_Paint_Android_Native*)p->native; }

static inline jint argb(mel_color8 k) { return (jint)(((u32)k.a << 24) | ((u32)k.r << 16) | ((u32)k.g << 8) | (u32)k.b); }

static void set1l(JNIEnv* e, jobject o, jmethodID m, jobject v)
{
    jvalue a = { .l = v };
    (*e)->CallVoidMethodA(e, o, m, &a);
}
static void set1i(JNIEnv* e, jobject o, jmethodID m, jint v)
{
    jvalue a = { .i = v };
    (*e)->CallVoidMethodA(e, o, m, &a);
}
static void set1f(JNIEnv* e, jobject o, jmethodID m, jfloat v)
{
    jvalue a = { .f = v };
    (*e)->CallVoidMethodA(e, o, m, &a);
}

static void fill_style(JNIEnv* e, jobject paint, mel_color8 k)
{
    set1l(e, paint, m_setStyle, s_style_fill);
    set1i(e, paint, m_setColor, argb(k));
}

static void stroke_style(JNIEnv* e, jobject paint, mel_color8 k, f32 width)
{
    set1l(e, paint, m_setStyle, s_style_stroke);
    set1f(e, paint, m_setStrokeWidth, (jfloat)width);
    set1i(e, paint, m_setColor, argb(k));
}

static void rect4(JNIEnv* e, jobject canvas, jmethodID m, Mel_Rect r, jobject paint)
{
    jvalue a[5] = { { .f = r.x }, { .f = r.y }, { .f = r.x + r.w }, { .f = r.y + r.h }, { .l = paint } };
    (*e)->CallVoidMethodA(e, canvas, m, a);
}

static jstring jstr(JNIEnv* e, str8 text)
{
    char buf[1024];
    int  n = (text.data && text.len > 0) ? (int)text.len : 0;
    if (n > (int)sizeof buf - 1)
        n = (int)sizeof buf - 1;
    if (n > 0)
        memcpy(buf, text.data, (size_t)n);
    buf[n] = 0;
    return (*e)->NewStringUTF(e, buf);
}

Mel_Painter mel_painter_begin(Mel_Drawable dh)
{
    Paint_Drawable* d = mel_paint__get(dh);
    mel_assert(!d->painting);
    d->painting = true;
    return (Mel_Painter){ .native = d->native, .owner = dh, .w = d->w, .h = d->h };
}

void mel_painter_end(Mel_Painter* p)
{
    mel_paint__get(p->owner)->painting = false;
    p->native = NULL;
}

void mel_painter_clear(Mel_Painter* p, mel_color8 k)
{
    JNIEnv* e = nat(p)->env;
    if (!ensure(e))
        return;
    fill_style(e, nat(p)->paint, k);
    rect4(e, nat(p)->canvas, m_drawRect, mel_rect(0, 0, (f32)p->w, (f32)p->h), nat(p)->paint);
}

void mel_painter_fill_rect(Mel_Painter* p, Mel_Rect r, mel_color8 k)
{
    JNIEnv* e = nat(p)->env;
    if (!ensure(e))
        return;
    fill_style(e, nat(p)->paint, k);
    rect4(e, nat(p)->canvas, m_drawRect, r, nat(p)->paint);
}

void mel_painter_fill_ellipse(Mel_Painter* p, Mel_Rect r, mel_color8 k)
{
    JNIEnv* e = nat(p)->env;
    if (!ensure(e))
        return;
    fill_style(e, nat(p)->paint, k);
    rect4(e, nat(p)->canvas, m_drawOval, r, nat(p)->paint);
}

void mel_painter_stroke_rect(Mel_Painter* p, Mel_Rect r, mel_color8 k, f32 width)
{
    JNIEnv* e = nat(p)->env;
    if (!ensure(e))
        return;
    stroke_style(e, nat(p)->paint, k, width);
    rect4(e, nat(p)->canvas, m_drawRect, r, nat(p)->paint);
}

void mel_painter_draw_line(Mel_Painter* p, Mel_Vec2 a, Mel_Vec2 b, mel_color8 k, f32 width)
{
    JNIEnv* e = nat(p)->env;
    if (!ensure(e))
        return;
    stroke_style(e, nat(p)->paint, k, width);
    jvalue v[5] = { { .f = a.x }, { .f = a.y }, { .f = b.x }, { .f = b.y }, { .l = nat(p)->paint } };
    (*e)->CallVoidMethodA(e, nat(p)->canvas, m_drawLine, v);
}

void mel_painter_fill_round_rect(Mel_Painter* p, Mel_Rect r, f32 radius, mel_color8 k)
{
    JNIEnv* e = nat(p)->env;
    if (!ensure(e))
        return;
    fill_style(e, nat(p)->paint, k);
    jvalue v[7] = { { .f = r.x }, { .f = r.y }, { .f = r.x + r.w }, { .f = r.y + r.h }, { .f = radius }, { .f = radius }, { .l = nat(p)->paint } };
    (*e)->CallVoidMethodA(e, nat(p)->canvas, m_drawRoundRect, v);
}

void mel_painter_draw_text(Mel_Painter* p, str8 text, Mel_Vec2 pos, mel_color8 k, f32 size)
{
    JNIEnv* e = nat(p)->env;
    if (!ensure(e))
        return;
    fill_style(e, nat(p)->paint, k);
    set1f(e, nat(p)->paint, m_setTextSize, (jfloat)size);
    jfloat  ascent = (*e)->CallFloatMethodA(e, nat(p)->paint, m_ascent, NULL);
    jstring s = jstr(e, text);
    jvalue  v[4] = { { .l = s }, { .f = pos.x }, { .f = pos.y - ascent }, { .l = nat(p)->paint } };
    (*e)->CallVoidMethodA(e, nat(p)->canvas, m_drawText, v);
    (*e)->DeleteLocalRef(e, s);
}
