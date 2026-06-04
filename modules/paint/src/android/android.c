#include "../paint_internal.h"

#include <debug/assert.h>
#include <log/log.h>

#include <paint/native_android.h>
#include <paint/painter.h>

#include <string.h>

static jmethodID m_drawRect, m_drawOval, m_drawLine, m_drawRoundRect, m_drawText;
static jmethodID m_setColor, m_setStyle, m_setStrokeWidth, m_setTextSize, m_ascent;
static jobject   s_style_fill, s_style_stroke;
static bool      s_ready;

static jclass    c_bitmap, c_config, c_rect, c_rectf;
static jmethodID m_createBitmap, m_copyPixels, m_drawBitmap, m_setPremultiplied;
static jmethodID m_rect_ctor, m_rectf_ctor;
static jobject   s_config_argb;
static bool      s_img_ready;

static jobject s_bitmap;
static i32     s_bitmap_w, s_bitmap_h;
static bool    s_bitmap_premul;
static jobject s_src_rect;

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

static bool ensure_img(JNIEnv* env)
{
    if (s_img_ready)
        return true;

    jclass bitmap = (*env)->FindClass(env, "android/graphics/Bitmap");
    jclass config = (*env)->FindClass(env, "android/graphics/Bitmap$Config");
    jclass rect = (*env)->FindClass(env, "android/graphics/Rect");
    jclass rectf = (*env)->FindClass(env, "android/graphics/RectF");
    jclass canvas = (*env)->FindClass(env, "android/graphics/Canvas");
    if (!bitmap || !config || !rect || !rectf || !canvas)
        return false;

    m_createBitmap = (*env)->GetStaticMethodID(env, bitmap, "createBitmap", "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");
    m_copyPixels = (*env)->GetMethodID(env, bitmap, "copyPixelsFromBuffer", "(Ljava/nio/Buffer;)V");
    m_setPremultiplied = (*env)->GetMethodID(env, bitmap, "setPremultiplied", "(Z)V");
    m_drawBitmap = (*env)->GetMethodID(env, canvas, "drawBitmap", "(Landroid/graphics/Bitmap;Landroid/graphics/Rect;Landroid/graphics/RectF;Landroid/graphics/Paint;)V");
    m_rect_ctor = (*env)->GetMethodID(env, rect, "<init>", "(IIII)V");
    m_rectf_ctor = (*env)->GetMethodID(env, rectf, "<init>", "(FFFF)V");

    jfieldID f_argb = (*env)->GetStaticFieldID(env, config, "ARGB_8888", "Landroid/graphics/Bitmap$Config;");
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return false;
    }

    jobject argb = (*env)->GetStaticObjectField(env, config, f_argb);
    s_config_argb = (*env)->NewGlobalRef(env, argb);
    c_bitmap = (jclass)(*env)->NewGlobalRef(env, bitmap);
    c_config = (jclass)(*env)->NewGlobalRef(env, config);
    c_rect = (jclass)(*env)->NewGlobalRef(env, rect);
    c_rectf = (jclass)(*env)->NewGlobalRef(env, rectf);

    (*env)->DeleteLocalRef(env, bitmap);
    (*env)->DeleteLocalRef(env, config);
    (*env)->DeleteLocalRef(env, rect);
    (*env)->DeleteLocalRef(env, rectf);
    (*env)->DeleteLocalRef(env, canvas);
    (*env)->DeleteLocalRef(env, argb);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return false;
    }

    s_img_ready = m_createBitmap && m_copyPixels && m_setPremultiplied && m_drawBitmap && m_rect_ctor && m_rectf_ctor && s_config_argb && c_bitmap && c_rect && c_rectf;
    return s_img_ready;
}

static jobject ensure_bitmap(JNIEnv* env, i32 w, i32 h, bool premul)
{
    if (s_bitmap && s_bitmap_w == w && s_bitmap_h == h)
    {
        if (s_bitmap_premul != premul)
        {
            jvalue pv = { .z = (jboolean)premul };
            (*env)->CallVoidMethodA(env, s_bitmap, m_setPremultiplied, &pv);
            s_bitmap_premul = premul;
        }
        return s_bitmap;
    }

    if (s_bitmap)
    {
        (*env)->DeleteGlobalRef(env, s_bitmap);
        s_bitmap = NULL;
        s_bitmap_w = 0;
        s_bitmap_h = 0;
    }
    if (s_src_rect)
    {
        (*env)->DeleteGlobalRef(env, s_src_rect);
        s_src_rect = NULL;
    }

    jvalue  a[3] = { { .i = (jint)w }, { .i = (jint)h }, { .l = s_config_argb } };
    jobject local = (*env)->CallStaticObjectMethodA(env, c_bitmap, m_createBitmap, a);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    if (!local)
        return NULL;

    s_bitmap = (*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);
    s_bitmap_w = w;
    s_bitmap_h = h;

    jvalue pv = { .z = (jboolean)premul };
    (*env)->CallVoidMethodA(env, s_bitmap, m_setPremultiplied, &pv);
    s_bitmap_premul = premul;

    jvalue  rsrc[4] = { { .i = 0 }, { .i = 0 }, { .i = (jint)w }, { .i = (jint)h } };
    jobject rect = (*env)->NewObjectA(env, c_rect, m_rect_ctor, rsrc);
    if (rect)
    {
        s_src_rect = (*env)->NewGlobalRef(env, rect);
        (*env)->DeleteLocalRef(env, rect);
    }
    return s_bitmap;
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

static Mel_Image        s_conv;
static const Mel_Alloc* s_conv_alloc;

void mel_painter_draw_image(Mel_Painter* p, const Mel_Image* img, Mel_Rect dst, const Mel_Alloc* scratch_alloc)
{
    mel_assert(p && img && img->format);

    JNIEnv* e = nat(p)->env;
    if (!ensure_img(e))
    {
        mel_log_error("paint", "mel_painter_draw_image: android JNI resolution failed");
        return;
    }

    const Mel_Image* src = img;
    Mel_Image_Plane  plane = mel_image_plane(src, 0);
    bool             premul = img->format == &mel_image_rgba8_premul;
    bool             direct = (img->format == &mel_image_rgba8 || premul) && plane.stride == plane.w * 4;

    if (!direct)
    {
        if (!scratch_alloc)
        {
            mel_log_fatal("paint", "mel_painter_draw_image: format '%s' needs conversion but no scratch allocator given", mel_image_format_name(img->format));
            MEL_BREAKPOINT();
            return;
        }
        if (s_conv.format && (s_conv.w != img->w || s_conv.h != img->h || s_conv_alloc != scratch_alloc))
        {
            mel_image_free(&s_conv);
            s_conv = (Mel_Image){ 0 };
        }
        if (!s_conv.format && !mel_image_init(&s_conv, &mel_image_rgba8, img->w, img->h, scratch_alloc))
        {
            mel_log_error("paint", "mel_painter_draw_image: scratch rgba8 init failed (%dx%d)", img->w, img->h);
            return;
        }
        s_conv_alloc = scratch_alloc;
        if (!mel_image_convert_scratch(img, &s_conv, scratch_alloc))
        {
            mel_log_error("paint", "mel_painter_draw_image: convert '%s' -> rgba8 failed", mel_image_format_name(img->format));
            return;
        }
        src = &s_conv;
        plane = mel_image_plane(src, 0);
    }

    jobject bitmap = ensure_bitmap(e, plane.w, plane.h, true);
    if (!bitmap || !s_src_rect)
    {
        mel_log_error("paint", "mel_painter_draw_image: Bitmap.createBitmap failed (%dx%d)", plane.w, plane.h);
        return;
    }

    jobject buf = (*e)->NewDirectByteBuffer(e, plane.pixels, (jlong)plane.stride * (jlong)plane.h);
    if (!buf)
    {
        mel_log_error("paint", "mel_painter_draw_image: NewDirectByteBuffer failed");
        return;
    }

    set1l(e, bitmap, m_copyPixels, buf);
    if ((*e)->ExceptionCheck(e))
    {
        (*e)->ExceptionClear(e);
        mel_log_error("paint", "mel_painter_draw_image: copyPixelsFromBuffer raised");
        (*e)->DeleteLocalRef(e, buf);
        return;
    }

    jvalue  rdst[4] = { { .f = dst.x }, { .f = dst.y }, { .f = dst.x + dst.w }, { .f = dst.y + dst.h } };
    jobject dst_rect = (*e)->NewObjectA(e, c_rectf, m_rectf_ctor, rdst);
    if (!dst_rect)
    {
        mel_log_error("paint", "mel_painter_draw_image: RectF alloc failed");
        (*e)->DeleteLocalRef(e, buf);
        return;
    }

    jvalue draw[4] = { { .l = bitmap }, { .l = s_src_rect }, { .l = dst_rect }, { .l = nat(p)->paint } };
    (*e)->CallVoidMethodA(e, nat(p)->canvas, m_drawBitmap, draw);

    (*e)->DeleteLocalRef(e, dst_rect);
    (*e)->DeleteLocalRef(e, buf);

    if ((*e)->ExceptionCheck(e))
    {
        (*e)->ExceptionClear(e);
        mel_log_error("paint", "mel_painter_draw_image: drawBitmap raised");
    }
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
