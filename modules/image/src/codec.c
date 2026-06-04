#include <image/codec.h>

#include "format_internal.h"

#include <allocator/allocator.h>
#include <collection/array.h>
#include <debug/assert.h>
#include <log/log.h>

#include <stb_image.h>
#include <stb_image_write.h>

#include <stdio.h>
#include <string.h>

typedef Mel_Array(const Mel_Image_Codec_Desc*) Mel_Image_Codec_Registry;

static struct
{
    Mel_Image_Codec_Registry registry;
    const Mel_Alloc*         alloc;
    bool                     ready;
} g_codec;

static const mel_image_format* mel_image__format_for_channels(i32 ch)
{
    if (ch == 1)
        return &mel_image_gray8;
    if (ch == 3)
        return &mel_image_rgb8;
    if (ch == 4)
        return &mel_image_rgba8;
    return NULL;
}

static bool mel_image__copy_stb(Mel_Image* out, const stbi_uc* src, i32 w, i32 h, i32 ch, const Mel_Alloc* a)
{
    const mel_image_format* f = mel_image__format_for_channels(ch);
    if (!f)
    {
        mel_log_error("image", "decode: unsupported channel count %d", ch);
        return false;
    }

    if (!mel_image_init(out, f, w, h, a))
        return false;

    Mel_Image_Plane dst = mel_image_plane(out, 0);
    usize           row = (usize)w * (usize)ch;
    if (dst.stride == (i32)row)
        memcpy(dst.pixels, src, row * (usize)h);
    else
        for (i32 y = 0; y < h; y++)
            memcpy(dst.pixels + (usize)y * dst.stride, src + (usize)y * row, row);
    return true;
}

static bool stb_decode(const u8* bytes, usize len, const Mel_Alloc* a, Mel_Image* out)
{
    if (!bytes || len == 0 || len > (usize)INT32_MAX)
        return false;

    int      w = 0, h = 0, ch = 0;
    stbi_uc* px = stbi_load_from_memory(bytes, (int)len, &w, &h, &ch, 0);
    if (!px)
    {
        mel_log_error("image", "decode: stbi_load_from_memory failed: %s", stbi_failure_reason());
        return false;
    }

    bool ok = mel_image__copy_stb(out, px, w, h, ch, a);
    stbi_image_free(px);
    return ok;
}

static bool stb_probe_png(const u8* b, usize n) { return n >= 8 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G' && b[4] == '\r' && b[5] == '\n' && b[6] == 0x1A && b[7] == '\n'; }
static bool stb_probe_jpeg(const u8* b, usize n) { return n >= 3 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF; }
static bool stb_probe_bmp(const u8* b, usize n) { return n >= 2 && b[0] == 'B' && b[1] == 'M'; }
static bool stb_probe_gif(const u8* b, usize n) { return n >= 6 && b[0] == 'G' && b[1] == 'I' && b[2] == 'F' && b[3] == '8' && (b[4] == '7' || b[4] == '9') && b[5] == 'a'; }
static bool stb_probe_psd(const u8* b, usize n) { return n >= 4 && b[0] == '8' && b[1] == 'B' && b[2] == 'P' && b[3] == 'S'; }
static bool stb_probe_hdr(const u8* b, usize n) { return (n >= 11 && memcmp(b, "#?RADIANCE\n", 11) == 0) || (n >= 7 && memcmp(b, "#?RGBE\n", 7) == 0); }
static bool stb_probe_tga(const u8* b, usize n)
{
    if (n < 18)
        return false;
    u8 it = b[2];
    if (it != 1 && it != 2 && it != 3 && it != 9 && it != 10 && it != 11)
        return false;
    u8 bpp = b[16];
    return bpp == 8 || bpp == 15 || bpp == 16 || bpp == 24 || bpp == 32;
}

typedef struct
{
    const mel_image_format* fmt;
    i32                     comp;
} mel_image__encode_view;

static mel_image__encode_view mel_image__encode_source(const Mel_Image* img)
{
    const mel_image_format* f = img->format;
    mel_image__encode_view  v = { NULL, 0 };
    if (f == &mel_image_gray8 || f == &mel_image_r8)
    {
        v.fmt = &mel_image_gray8;
        v.comp = 1;
    }
    else if (f == &mel_image_rgb8)
    {
        v.fmt = &mel_image_rgb8;
        v.comp = 3;
    }
    else if (f == &mel_image_rgba8 || f == &mel_image_rgba8_srgb || f == &mel_image_rgba8_premul || f == &mel_image_bgra8)
    {
        v.fmt = &mel_image_rgba8;
        v.comp = 4;
    }
    else
    {
        mel_log_error("image", "encode: format %s not writable, convert to rgba8/rgb8/gray8 first", img->format->name);
    }
    return v;
}

static bool mel_image__encode_pack(const Mel_Image* img, const Mel_Alloc* a, mel_image__encode_view view, u8** out_pixels, bool* out_owned)
{
    *out_pixels = NULL;
    *out_owned = false;

    if (!view.fmt)
        return false;

    if (img->format == view.fmt && !img->wrapped)
    {
        Mel_Image_Plane p = mel_image_plane(img, 0);
        if (p.stride == img->w * view.comp)
        {
            *out_pixels = p.pixels;
            return true;
        }
    }

    if (img->format == view.fmt)
    {
        Mel_Image_Plane p = mel_image_plane(img, 0);
        usize           row = (usize)img->w * (usize)view.comp;
        u8*             buf = (u8*)mel_alloc(a, row * (usize)img->h);
        if (!buf)
            return false;
        if (p.stride == (i32)row)
            memcpy(buf, p.pixels, row * (usize)img->h);
        else
            for (i32 y = 0; y < img->h; y++)
                memcpy(buf + (usize)y * row, p.pixels + (usize)y * p.stride, row);
        *out_pixels = buf;
        *out_owned = true;
        return true;
    }

    Mel_Image tmp;
    if (!mel_image_convert_new(img, view.fmt, a, &tmp))
        return false;

    *out_pixels = mel_image_plane(&tmp, 0).pixels;
    *out_owned = true;
    return true;
}

typedef struct
{
    Mel_Image_Write_Fn write_fn;
    void*              user;
} mel_image__stb_sink;

static void mel_image__stb_write(void* context, void* data, int size)
{
    mel_image__stb_sink* s = (mel_image__stb_sink*)context;
    s->write_fn(s->user, data, (usize)size);
}

static bool stb_encode_png(const Mel_Image* img, Mel_Image_Write_Fn write_fn, void* user, const Mel_Alloc* a)
{
    mel_image__encode_view view = mel_image__encode_source(img);
    u8*                    px = NULL;
    bool                   owned = false;
    if (!mel_image__encode_pack(img, a, view, &px, &owned))
        return false;

    mel_image__stb_sink sink = { write_fn, user };
    int                 ok = stbi_write_png_to_func(mel_image__stb_write, &sink, img->w, img->h, view.comp, px, img->w * view.comp);
    if (owned)
        mel_dealloc(a, px);
    return ok != 0;
}

static bool stb_encode_bmp(const Mel_Image* img, Mel_Image_Write_Fn write_fn, void* user, const Mel_Alloc* a)
{
    mel_image__encode_view view = mel_image__encode_source(img);
    u8*                    px = NULL;
    bool                   owned = false;
    if (!mel_image__encode_pack(img, a, view, &px, &owned))
        return false;

    mel_image__stb_sink sink = { write_fn, user };
    int                 ok = stbi_write_bmp_to_func(mel_image__stb_write, &sink, img->w, img->h, view.comp, px);
    if (owned)
        mel_dealloc(a, px);
    return ok != 0;
}

static bool stb_encode_jpeg(const Mel_Image* img, Mel_Image_Write_Fn write_fn, void* user, const Mel_Alloc* a)
{
    mel_image__encode_view view = mel_image__encode_source(img);
    u8*                    px = NULL;
    bool                   owned = false;
    if (!mel_image__encode_pack(img, a, view, &px, &owned))
        return false;

    mel_image__stb_sink sink = { write_fn, user };
    int                 ok = stbi_write_jpg_to_func(mel_image__stb_write, &sink, img->w, img->h, view.comp, px, 90);
    if (owned)
        mel_dealloc(a, px);
    return ok != 0;
}

static const Mel_Image_Codec_Desc mel_image__codec_png = { "png", stb_probe_png, stb_decode, stb_encode_png };
static const Mel_Image_Codec_Desc mel_image__codec_jpeg = { "jpeg", stb_probe_jpeg, stb_decode, stb_encode_jpeg };
static const Mel_Image_Codec_Desc mel_image__codec_bmp = { "bmp", stb_probe_bmp, stb_decode, stb_encode_bmp };
static const Mel_Image_Codec_Desc mel_image__codec_gif = { "gif", stb_probe_gif, stb_decode, NULL };
static const Mel_Image_Codec_Desc mel_image__codec_tga = { "tga", stb_probe_tga, stb_decode, NULL };
static const Mel_Image_Codec_Desc mel_image__codec_psd = { "psd", stb_probe_psd, stb_decode, NULL };
static const Mel_Image_Codec_Desc mel_image__codec_hdr = { "hdr", stb_probe_hdr, stb_decode, NULL };

void mel_image_codec_register(const Mel_Image_Codec_Desc* codec)
{
    mel_assert(g_codec.ready);
    mel_assert(codec && codec->name);
    mel_array_push(&g_codec.registry, codec);
}

void mel_image_codec_init(const Mel_Alloc* a)
{
    mel_assert(a);
    if (g_codec.ready)
        return;

    mel_array_init(&g_codec.registry, a);
    g_codec.alloc = a;
    g_codec.ready = true;

    mel_image_codec_register(&mel_image__codec_png);
    mel_image_codec_register(&mel_image__codec_jpeg);
    mel_image_codec_register(&mel_image__codec_bmp);
    mel_image_codec_register(&mel_image__codec_gif);
    mel_image_codec_register(&mel_image__codec_tga);
    mel_image_codec_register(&mel_image__codec_psd);
    mel_image_codec_register(&mel_image__codec_hdr);
}

void mel_image_codec_shutdown(void)
{
    if (!g_codec.ready)
        return;
    mel_array_free(&g_codec.registry);
    g_codec.alloc = NULL;
    g_codec.ready = false;
}

const Mel_Image_Codec_Desc* mel_image_codec_find(const char* name)
{
    if (!g_codec.ready || !name)
        return NULL;
    for (usize i = 0; i < g_codec.registry.count; i++)
        if (strcmp(g_codec.registry.items[i]->name, name) == 0)
            return g_codec.registry.items[i];
    return NULL;
}

const Mel_Image_Codec_Desc* mel_image_codec_probe(const u8* bytes, usize len)
{
    if (!g_codec.ready || !bytes || len == 0)
        return NULL;
    for (usize i = 0; i < g_codec.registry.count; i++)
    {
        const Mel_Image_Codec_Desc* c = g_codec.registry.items[i];
        if (c->probe && c->probe(bytes, len))
            return c;
    }
    return NULL;
}

bool mel_image_load(Mel_Image* out, const u8* bytes, usize len, const Mel_Alloc* a)
{
    if (!out || !bytes || len == 0 || !a)
        return false;
    if (!g_codec.ready)
    {
        mel_log_error("image", "load: codec registry not initialised (call mel_image_codec_init)");
        return false;
    }

    const Mel_Image_Codec_Desc* c = mel_image_codec_probe(bytes, len);
    if (!c || !c->decode)
    {
        mel_log_error("image", "load: no codec recognised the byte stream");
        return false;
    }
    return c->decode(bytes, len, a, out);
}

bool mel_image_load_file(Mel_Image* out, const char* path, const Mel_Alloc* a)
{
    if (!out || !path || !a)
        return false;

    FILE* fp = fopen(path, "rb");
    if (!fp)
    {
        mel_log_error("image", "load_file: cannot open %s", path);
        return false;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return false;
    }
    long size = ftell(fp);
    if (size <= 0)
    {
        fclose(fp);
        mel_log_error("image", "load_file: empty or unseekable %s", path);
        return false;
    }
    rewind(fp);

    u8* buf = (u8*)mel_alloc(a, (usize)size);
    if (!buf)
    {
        fclose(fp);
        return false;
    }

    usize got = fread(buf, 1, (usize)size, fp);
    fclose(fp);
    if (got != (usize)size)
    {
        mel_dealloc(a, buf);
        mel_log_error("image", "load_file: short read %s", path);
        return false;
    }

    bool ok = mel_image_load(out, buf, (usize)size, a);
    mel_dealloc(a, buf);
    return ok;
}

static const char* mel_image__ext(const char* path)
{
    const char* dot = NULL;
    for (const char* p = path; *p; p++)
        if (*p == '.')
            dot = p;
    return dot ? dot + 1 : NULL;
}

static bool mel_image__ext_eq(const char* ext, const char* lower)
{
    for (; *ext && *lower; ext++, lower++)
    {
        char e = *ext;
        if (e >= 'A' && e <= 'Z')
            e = (char)(e - 'A' + 'a');
        if (e != *lower)
            return false;
    }
    return *ext == *lower;
}

static const char* mel_image__codec_for_ext(const char* ext)
{
    if (!ext)
        return NULL;
    if (mel_image__ext_eq(ext, "png"))
        return "png";
    if (mel_image__ext_eq(ext, "jpg") || mel_image__ext_eq(ext, "jpeg"))
        return "jpeg";
    if (mel_image__ext_eq(ext, "bmp"))
        return "bmp";
    return NULL;
}

typedef struct
{
    FILE* fp;
    bool  ok;
} mel_image__file_sink;

static void mel_image__file_write(void* user, const void* bytes, usize len)
{
    mel_image__file_sink* s = (mel_image__file_sink*)user;
    if (!s->ok)
        return;
    if (fwrite(bytes, 1, len, s->fp) != len)
        s->ok = false;
}

bool mel_image_save_as(const Mel_Image* img, const char* path, const char* codec_name, const Mel_Alloc* a)
{
    if (!img || !img->format || !path || !codec_name || !a)
        return false;

    const Mel_Image_Codec_Desc* c = mel_image_codec_find(codec_name);
    if (!c || !c->encode)
    {
        mel_log_error("image", "save: codec '%s' has no encoder", codec_name);
        return false;
    }

    FILE* fp = fopen(path, "wb");
    if (!fp)
    {
        mel_log_error("image", "save: cannot open %s for write", path);
        return false;
    }

    mel_image__file_sink sink = { fp, true };
    bool                 ok = c->encode(img, mel_image__file_write, &sink, a);
    if (fclose(fp) != 0)
        ok = false;
    return ok && sink.ok;
}

bool mel_image_save(const Mel_Image* img, const char* path, const Mel_Alloc* a)
{
    const char* codec_name = mel_image__codec_for_ext(mel_image__ext(path ? path : ""));
    if (!codec_name)
    {
        mel_log_error("image", "save: no codec maps to extension of %s", path ? path : "(null)");
        return false;
    }
    return mel_image_save_as(img, path, codec_name, a);
}

static void mel_image__bytes_write(void* user, const void* bytes, usize len)
{
    Mel_Image_Bytes* out = (Mel_Image_Bytes*)user;
    usize            need = out->count + len;
    mel_array_reserve(out, need);
    memcpy(out->items + out->count, bytes, len);
    out->count = need;
}

bool mel_image_encode(const Mel_Image* img, const char* codec_name, Mel_Image_Bytes* out, const Mel_Alloc* a)
{
    if (!img || !img->format || !codec_name || !out || !a)
        return false;

    const Mel_Image_Codec_Desc* c = mel_image_codec_find(codec_name);
    if (!c || !c->encode)
    {
        mel_log_error("image", "encode: codec '%s' has no encoder", codec_name);
        return false;
    }
    mel_array_clear(out);
    return c->encode(img, mel_image__bytes_write, out, a);
}
