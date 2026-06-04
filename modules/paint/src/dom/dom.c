#include "../paint_internal.h"

#include <debug/assert.h>
#include <log/log.h>

#include <paint/painter.h>

#include <emscripten.h>
#include <stdint.h>
#include <string.h>

static inline int id_of(Mel_Painter* p) { return (int)(intptr_t)p->native; }

static inline unsigned packrgba(mel_color8 c) { return ((unsigned)c.r << 24) | ((unsigned)c.g << 16) | ((unsigned)c.b << 8) | (unsigned)c.a; }

EM_JS(void, mel_paint_dom__clear, (int id, unsigned rgba), {
    const el = MelWeb.els[id];
    if (!el)
        return;
    const c = el.getContext('2d');
    c.fillStyle = 'rgba(' + ((rgba >>> 24) & 255) + ',' + ((rgba >>> 16) & 255) + ',' + ((rgba >>> 8) & 255) + ',' + ((rgba & 255) / 255) + ')';
    c.fillRect(0, 0, el.width, el.height);
});
EM_JS(void, mel_paint_dom__fill_rect, (int id, float x, float y, float w, float h, unsigned rgba), {
    const el = MelWeb.els[id];
    if (!el)
        return;
    const c = el.getContext('2d');
    c.fillStyle = 'rgba(' + ((rgba >>> 24) & 255) + ',' + ((rgba >>> 16) & 255) + ',' + ((rgba >>> 8) & 255) + ',' + ((rgba & 255) / 255) + ')';
    c.fillRect(x, y, w, h);
});
EM_JS(void, mel_paint_dom__fill_ellipse, (int id, float x, float y, float w, float h, unsigned rgba), {
    const el = MelWeb.els[id];
    if (!el)
        return;
    const c = el.getContext('2d');
    c.fillStyle = 'rgba(' + ((rgba >>> 24) & 255) + ',' + ((rgba >>> 16) & 255) + ',' + ((rgba >>> 8) & 255) + ',' + ((rgba & 255) / 255) + ')';
    c.beginPath();
    c.ellipse(x + w / 2, y + h / 2, w / 2, h / 2, 0, 0, Math.PI * 2);
    c.fill();
});
EM_JS(void, mel_paint_dom__stroke_rect, (int id, float x, float y, float w, float h, unsigned rgba, float width), {
    const el = MelWeb.els[id];
    if (!el)
        return;
    const c = el.getContext('2d');
    c.strokeStyle = 'rgba(' + ((rgba >>> 24) & 255) + ',' + ((rgba >>> 16) & 255) + ',' + ((rgba >>> 8) & 255) + ',' + ((rgba & 255) / 255) + ')';
    c.lineWidth = width;
    c.strokeRect(x, y, w, h);
});
EM_JS(void, mel_paint_dom__line, (int id, float ax, float ay, float bx, float by, unsigned rgba, float width), {
    const el = MelWeb.els[id];
    if (!el)
        return;
    const c = el.getContext('2d');
    c.strokeStyle = 'rgba(' + ((rgba >>> 24) & 255) + ',' + ((rgba >>> 16) & 255) + ',' + ((rgba >>> 8) & 255) + ',' + ((rgba & 255) / 255) + ')';
    c.lineWidth = width;
    c.lineCap = 'round';
    c.beginPath();
    c.moveTo(ax, ay);
    c.lineTo(bx, by);
    c.stroke();
});
EM_JS(void, mel_paint_dom__round_rect, (int id, float x, float y, float w, float h, float r, unsigned rgba), {
    const el = MelWeb.els[id];
    if (!el)
        return;
    const c = el.getContext('2d');
    c.fillStyle = 'rgba(' + ((rgba >>> 24) & 255) + ',' + ((rgba >>> 16) & 255) + ',' + ((rgba >>> 8) & 255) + ',' + ((rgba & 255) / 255) + ')';
    c.beginPath();
    if (c.roundRect)
        c.roundRect(x, y, w, h, r);
    else
        c.rect(x, y, w, h);
    c.fill();
});
EM_JS(void, mel_paint_dom__image, (int id, const unsigned char* px, int sw, int sh, int stride, float dx, float dy, float dw, float dh), {
    const el = MelWeb.els[id];
    if (!el)
        return;
    const c = el.getContext('2d');
    let cache = MelWeb.imgCache;
    if (!cache)
        cache = MelWeb.imgCache = {};
    let e = cache[id];
    if (!e || e.sw !== sw || e.sh !== sh)
    {
        const off = document.createElement('canvas');
        off.width = sw;
        off.height = sh;
        e = cache[id] = { sw : sw, sh : sh, off : off, octx : off.getContext('2d'), idata : null };
        e.idata = e.octx.createImageData(sw, sh);
    }
    const idata = e.idata;
    for (let y = 0; y < sh; ++y)
    {
        const srow = px + y * stride;
        const drow = y * sw * 4;
        idata.data.set(HEAPU8.subarray(srow, srow + sw * 4), drow);
    }
    e.octx.putImageData(idata, 0, 0);
    c.drawImage(e.off, dx, dy, dw, dh);
});
EM_JS(void, mel_paint_dom__text, (int id, const char* s, float x, float y, unsigned rgba, float size), {
    const el = MelWeb.els[id];
    if (!el)
        return;
    const c = el.getContext('2d');
    c.fillStyle = 'rgba(' + ((rgba >>> 24) & 255) + ',' + ((rgba >>> 16) & 255) + ',' + ((rgba >>> 8) & 255) + ',' + ((rgba & 255) / 255) + ')';
    c.font = size + 'px system-ui, sans-serif';
    c.textBaseline = 'top';
    c.fillText(UTF8ToString(s), x, y);
});

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

void mel_painter_clear(Mel_Painter* p, mel_color8 k) { mel_paint_dom__clear(id_of(p), packrgba(k)); }
void mel_painter_fill_rect(Mel_Painter* p, Mel_Rect r, mel_color8 k) { mel_paint_dom__fill_rect(id_of(p), r.x, r.y, r.w, r.h, packrgba(k)); }
void mel_painter_fill_ellipse(Mel_Painter* p, Mel_Rect r, mel_color8 k) { mel_paint_dom__fill_ellipse(id_of(p), r.x, r.y, r.w, r.h, packrgba(k)); }
void mel_painter_stroke_rect(Mel_Painter* p, Mel_Rect r, mel_color8 k, f32 width) { mel_paint_dom__stroke_rect(id_of(p), r.x, r.y, r.w, r.h, packrgba(k), width); }
void mel_painter_draw_line(Mel_Painter* p, Mel_Vec2 a, Mel_Vec2 b, mel_color8 k, f32 width) { mel_paint_dom__line(id_of(p), a.x, a.y, b.x, b.y, packrgba(k), width); }
void mel_painter_fill_round_rect(Mel_Painter* p, Mel_Rect r, f32 radius, mel_color8 k) { mel_paint_dom__round_rect(id_of(p), r.x, r.y, r.w, r.h, radius, packrgba(k)); }

void mel_painter_draw_image(Mel_Painter* p, const Mel_Image* img, Mel_Rect dst, const Mel_Alloc* scratch_alloc)
{
    mel_assert(p && img && img->format);

    Mel_Image        scratch = { 0 };
    const Mel_Image* src = img;

    if (img->format != &mel_image_rgba8 && img->format != &mel_image_rgba8_premul)
    {
        if (!scratch_alloc)
        {
            mel_log_fatal("paint", "mel_painter_draw_image: format '%s' needs conversion but no scratch allocator given", mel_image_format_name(img->format));
            return;
        }
        if (!mel_image_init(&scratch, &mel_image_rgba8, img->w, img->h, scratch_alloc) || !mel_image_convert_scratch(img, &scratch, scratch_alloc))
        {
            mel_log_error("paint", "mel_painter_draw_image: convert '%s' -> rgba8 failed", mel_image_format_name(img->format));
            if (scratch.format)
                mel_image_free(&scratch);
            return;
        }
        src = &scratch;
    }

    Mel_Image_Plane plane = mel_image_plane(src, 0);
    mel_paint_dom__image(id_of(p), plane.pixels, plane.w, plane.h, plane.stride, dst.x, dst.y, dst.w, dst.h);

    if (scratch.format)
        mel_image_free(&scratch);
}

void mel_painter_draw_text(Mel_Painter* p, str8 text, Mel_Vec2 pos, mel_color8 k, f32 size)
{
    char b[1024];
    int  n = (text.data && text.len > 0) ? (int)text.len : 0;
    if (n > (int)sizeof b - 1)
        n = (int)sizeof b - 1;
    if (n > 0)
        memcpy(b, text.data, (size_t)n);
    b[n] = 0;
    mel_paint_dom__text(id_of(p), b, pos.x, pos.y, packrgba(k), size);
}
