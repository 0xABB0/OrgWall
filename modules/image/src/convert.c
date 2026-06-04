#include <image/convert.h>

#include "format_internal.h"

#include <allocator/allocator.h>
#include <color/rgba.h>
#include <debug/assert.h>
#include <log/log.h>
#include <thread/once.h>

#include <string.h>

f32 mel_image__load_unorm8(const u8* p) { return (f32)p[0] * (1.0f / 255.0f); }

void mel_image__store_unorm8(u8* p, f32 v)
{
    f32 c = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    p[0] = (u8)(c * 255.0f + 0.5f);
}

f32 mel_image__load_unorm16(const u8* p)
{
    u16 v;
    memcpy(&v, p, sizeof(v));
    return (f32)v * (1.0f / 65535.0f);
}

void mel_image__store_unorm16(u8* p, f32 v)
{
    f32 c = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    u16 out = (u16)(c * 65535.0f + 0.5f);
    memcpy(p, &out, sizeof(out));
}

static f32 mel_image__half_to_f32(u16 h)
{
    u32 sign = (u32)(h & 0x8000u) << 16;
    u32 exp = (h >> 10) & 0x1Fu;
    u32 mant = h & 0x3FFu;
    u32 bits;

    if (exp == 0)
    {
        if (mant == 0)
        {
            bits = sign;
        }
        else
        {
            exp = 127 - 15 + 1;
            while ((mant & 0x400u) == 0)
            {
                mant <<= 1;
                exp--;
            }
            mant &= 0x3FFu;
            bits = sign | (exp << 23) | (mant << 13);
        }
    }
    else if (exp == 0x1Fu)
    {
        bits = sign | 0x7F800000u | (mant << 13);
    }
    else
    {
        bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
    }

    f32 out;
    memcpy(&out, &bits, sizeof(out));
    return out;
}

static u16 mel_image__f32_to_half(f32 f)
{
    u32 bits;
    memcpy(&bits, &f, sizeof(bits));

    u32 sign = (bits >> 16) & 0x8000u;
    i32 exp = (i32)((bits >> 23) & 0xFFu) - 127 + 15;
    u32 mant = bits & 0x7FFFFFu;

    if (((bits >> 23) & 0xFFu) == 0xFFu)
        return (u16)(sign | 0x7C00u | (mant ? 0x200u : 0u));

    if (exp >= 0x1F)
        return (u16)(sign | 0x7C00u);

    if (exp <= 0)
    {
        if (exp < -10)
            return (u16)sign;
        mant |= 0x800000u;
        u32 shift = (u32)(14 - exp);
        u32 half = (mant + (1u << (shift - 1)) + (((mant >> shift) & 1u) ? 0u : 0u)) >> shift;
        return (u16)(sign | half);
    }

    u32 half = (mant + 0x1000u) >> 13;
    if (half & 0x400u)
    {
        half = 0;
        exp++;
        if (exp >= 0x1F)
            return (u16)(sign | 0x7C00u);
    }
    return (u16)(sign | ((u32)exp << 10) | half);
}

f32 mel_image__load_f16(const u8* p)
{
    u16 v;
    memcpy(&v, p, sizeof(v));
    return mel_image__half_to_f32(v);
}

void mel_image__store_f16(u8* p, f32 v)
{
    u16 out = mel_image__f32_to_half(v);
    memcpy(p, &out, sizeof(out));
}

f32 mel_image__load_f32(const u8* p)
{
    f32 v;
    memcpy(&v, p, sizeof(v));
    return v;
}

void mel_image__store_f32(u8* p, f32 v) { memcpy(p, &v, sizeof(v)); }

float mel_image__tf_linear(float c) { return c; }

typedef struct
{
    u8  srgb_to_lin[256];
    u8  lin_to_srgb[256];
    f32 srgb_to_lin_f[256];
    f32 lin_to_srgb_f[256];
    u8  vr_to_full[256];
    u8  identity[256];
} mel_image__srgb_lut;

static mel_image__srgb_lut g_srgb;
static Mel_Once            g_srgb_once = MEL_ONCE_INIT;

static void mel_image__srgb_lut_build(void)
{
    for (i32 i = 0; i < 256; i++)
    {
        f32 e = (f32)i * (1.0f / 255.0f);
        f32 lf = mel_color_srgb_to_linear(e);
        f32 sf = mel_color_linear_to_srgb(e);
        i32 d = (i32)(lf * 255.0f + 0.5f);
        i32 u = (i32)(sf * 255.0f + 0.5f);

        g_srgb.srgb_to_lin[i] = (u8)(d < 0 ? 0 : (d > 255 ? 255 : d));
        g_srgb.lin_to_srgb[i] = (u8)(u < 0 ? 0 : (u > 255 ? 255 : u));
        g_srgb.srgb_to_lin_f[i] = lf;
        g_srgb.lin_to_srgb_f[i] = sf;

        f32 vf = ((f32)i - 16.0f) * (255.0f / 219.0f);
        i32 vi = (i32)(vf + 0.5f);
        g_srgb.vr_to_full[i] = (u8)(vi < 0 ? 0 : (vi > 255 ? 255 : vi));

        g_srgb.identity[i] = (u8)i;
    }
}

static void mel_image__srgb_lut_init(void) { mel_once(&g_srgb_once, mel_image__srgb_lut_build); }

static inline f32 mel_image__srgb_to_lin_f(f32 c)
{
    f32 cc = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
    return g_srgb.srgb_to_lin_f[(i32)(cc * 255.0f + 0.5f)];
}

static inline f32 mel_image__lin_to_srgb_f(f32 c)
{
    f32 cc = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
    return g_srgb.lin_to_srgb_f[(i32)(cc * 255.0f + 0.5f)];
}

void mel_image__packed_to_canonical(const mel_image_format* f, const Mel_Image* src, i32 y, mel_image_canon out)
{
    Mel_Image_Plane p = mel_image_plane(src, 0);
    const u8*       row = p.pixels + (usize)y * p.stride;
    i32             bps = f->bytes_per_sample;
    i32             bpp = f->bytes_per_pixel;
    i32             w = out.w;
    bool            lin = (f->to_linear == mel_image__tf_linear);
    bool            premul = f->premultiplied;
    f32 (*ld)(const u8*) = f->sample_load;
    float (*tl)(float) = f->to_linear;
    isize roff = f->off_r >= 0 ? (isize)f->off_r * bps : -1;
    isize goff = f->off_g >= 0 ? (isize)f->off_g * bps : -1;
    isize boff = f->off_b >= 0 ? (isize)f->off_b * bps : -1;
    isize aoff = f->off_a >= 0 ? (isize)f->off_a * bps : -1;

    for (i32 x = 0; x < w; x++)
    {
        const u8* px = row + (usize)x * bpp;
        f32       r = (roff >= 0) ? ld(px + roff) : 0.0f;
        f32       g = (goff >= 0) ? ld(px + goff) : 0.0f;
        f32       b = (boff >= 0) ? ld(px + boff) : 0.0f;
        f32       a = (aoff >= 0) ? ld(px + aoff) : 1.0f;

        if (!lin)
        {
            r = tl(r);
            g = tl(g);
            b = tl(b);
        }
        if (!premul)
        {
            r *= a;
            g *= a;
            b *= a;
        }
        out.row[x] = (mel_color){ r, g, b, a };
    }
}

void mel_image__packed_from_canonical(const mel_image_format* f, Mel_Image* dst, i32 y, mel_image_canon in)
{
    Mel_Image_Plane p = mel_image_plane(dst, 0);
    u8*             row = p.pixels + (usize)y * p.stride;
    i32             bps = f->bytes_per_sample;
    i32             bpp = f->bytes_per_pixel;
    i32             w = in.w;
    bool            lin = (f->to_encoded == mel_image__tf_linear);
    bool            premul = f->premultiplied;
    void (*st)(u8*, f32) = f->sample_store;
    float (*te)(float) = f->to_encoded;
    isize roff = f->off_r >= 0 ? (isize)f->off_r * bps : -1;
    isize goff = f->off_g >= 0 ? (isize)f->off_g * bps : -1;
    isize boff = f->off_b >= 0 ? (isize)f->off_b * bps : -1;
    isize aoff = f->off_a >= 0 ? (isize)f->off_a * bps : -1;

    for (i32 x = 0; x < w; x++)
    {
        mel_color c = in.row[x];
        f32       a = c.a;
        f32       r = c.r;
        f32       g = c.g;
        f32       b = c.b;

        if (!premul)
        {
            f32 inv = a > 0.0f ? 1.0f / a : 0.0f;
            r *= inv;
            g *= inv;
            b *= inv;
        }
        if (!lin)
        {
            r = te(r);
            g = te(g);
            b = te(b);
        }

        u8* px = row + (usize)x * bpp;
        if (roff >= 0)
            st(px + roff, r);
        if (goff >= 0)
            st(px + goff, g);
        if (boff >= 0)
            st(px + boff, b);
        if (aoff >= 0)
            st(px + aoff, a);
    }
}

typedef struct
{
    const u8* uv;
    i32       uv_stride;
    const u8* up;
    const u8* vp;
    i32       u_stride, v_stride;
    i32       ssx, ssy;
    i32       u_byte, v_byte;
    bool      semi;
} mel_image__yuv_chroma;

static mel_image__yuv_chroma mel_image__yuv_chroma_resolve(const mel_image_format* f, const Mel_Image* img)
{
    mel_image__yuv_chroma c = { 0 };
    c.semi = (f->plane_count == 2);
    c.u_byte = f->yuv.u_byte;
    c.v_byte = f->yuv.v_byte;
    if (c.semi)
    {
        Mel_Image_Plane uv = mel_image_plane(img, 1);
        c.uv = uv.pixels;
        c.uv_stride = uv.stride;
        c.ssx = uv.w < img->w ? 1 : 0;
        c.ssy = uv.h < img->h ? 1 : 0;
    }
    else
    {
        Mel_Image_Plane up = mel_image_plane(img, 1);
        Mel_Image_Plane vp = mel_image_plane(img, 2);
        c.up = up.pixels;
        c.vp = vp.pixels;
        c.u_stride = up.stride;
        c.v_stride = vp.stride;
        c.ssx = up.w < img->w ? 1 : 0;
        c.ssy = up.h < img->h ? 1 : 0;
    }
    return c;
}

void mel_image__yuv_to_canonical(const mel_image_format* f, const Mel_Image* src, i32 y, mel_image_canon out)
{
    mel_image_yuv m = f->yuv;
    i32           w = out.w;
    f32           kr = m.kr, kb = m.kb;
    f32           inv_kg = 1.0f / m.kg;
    f32           rc_coef = 2.0f * (1.0f - kr);
    f32           bc_coef = 2.0f * (1.0f - kb);
    f32           gv_coef = -kr * rc_coef * inv_kg;
    f32           gu_coef = -kb * bc_coef * inv_kg;

    mel_image__srgb_lut_init();

    Mel_Image_Plane       yp = mel_image_plane(src, 0);
    const u8*             yrow = yp.pixels + (usize)y * yp.stride;
    mel_image__yuv_chroma ch = mel_image__yuv_chroma_resolve(f, src);

    i32       cy = ch.ssy ? y >> 1 : y;
    const u8* uvrow = ch.semi ? ch.uv + (usize)cy * ch.uv_stride : NULL;
    const u8* urow = ch.semi ? NULL : ch.up + (usize)cy * ch.u_stride;
    const u8* vrow = ch.semi ? NULL : ch.vp + (usize)cy * ch.v_stride;

    for (i32 x = 0; x < w; x++)
    {
        i32 cx = ch.ssx ? x >> 1 : x;
        f32 Y = (f32)yrow[x];
        f32 U, V;
        if (ch.semi)
        {
            const u8* c = uvrow + (usize)cx * 2;
            U = (f32)c[ch.u_byte];
            V = (f32)c[ch.v_byte];
        }
        else
        {
            U = (f32)urow[cx];
            V = (f32)vrow[cx];
        }

        f32 yn, un, vn;
        if (m.full_range)
        {
            yn = Y * (1.0f / 255.0f);
            un = (U - 128.0f) * (1.0f / 255.0f);
            vn = (V - 128.0f) * (1.0f / 255.0f);
        }
        else
        {
            yn = (Y - 16.0f) * (1.0f / 219.0f);
            un = (U - 128.0f) * (1.0f / 224.0f);
            vn = (V - 128.0f) * (1.0f / 224.0f);
        }

        f32 r = yn + rc_coef * vn;
        f32 b = yn + bc_coef * un;
        f32 g = yn + gv_coef * vn + gu_coef * un;

        r = mel_image__srgb_to_lin_f(r);
        g = mel_image__srgb_to_lin_f(g);
        b = mel_image__srgb_to_lin_f(b);

        out.row[x] = (mel_color){ r, g, b, 1.0f };
    }
}

void mel_image__yuv_from_canonical(const mel_image_format* f, Mel_Image* dst, i32 y, mel_image_canon in)
{
    mel_image_yuv m = f->yuv;
    i32           w = in.w;
    f32           kr = m.kr, kg = m.kg, kb = m.kb;
    f32           inv_ub = 1.0f / (2.0f * (1.0f - kb));
    f32           inv_vr = 1.0f / (2.0f * (1.0f - kr));

    mel_image__srgb_lut_init();

    Mel_Image_Plane yp = mel_image_plane(dst, 0);
    u8*             yr = yp.pixels + (usize)y * yp.stride;

    mel_image__yuv_chroma ch = mel_image__yuv_chroma_resolve(f, dst);

    bool row_has_chroma = !(ch.ssy && (y & 1));
    i32  cy = ch.ssy ? y >> 1 : y;
    u8*  uvrow = (row_has_chroma && ch.semi) ? (u8*)ch.uv + (usize)cy * ch.uv_stride : NULL;
    u8*  urow = (row_has_chroma && !ch.semi) ? (u8*)ch.up + (usize)cy * ch.u_stride : NULL;
    u8*  vrow = (row_has_chroma && !ch.semi) ? (u8*)ch.vp + (usize)cy * ch.v_stride : NULL;

    for (i32 x = 0; x < w; x++)
    {
        mel_color c = in.row[x];
        f32       inv = c.a > 0.0f ? 1.0f / c.a : 0.0f;
        f32       r = mel_image__lin_to_srgb_f(c.r * inv);
        f32       g = mel_image__lin_to_srgb_f(c.g * inv);
        f32       b = mel_image__lin_to_srgb_f(c.b * inv);

        f32 yn = kr * r + kg * g + kb * b;
        f32 un = (b - yn) * inv_ub;
        f32 vn = (r - yn) * inv_vr;

        f32 Y, U, V;
        if (m.full_range)
        {
            Y = yn * 255.0f;
            U = un * 255.0f + 128.0f;
            V = vn * 255.0f + 128.0f;
        }
        else
        {
            Y = yn * 219.0f + 16.0f;
            U = un * 224.0f + 128.0f;
            V = vn * 224.0f + 128.0f;
        }

        yr[x] = (u8)(Y < 0.0f ? 0.0f : (Y > 255.0f ? 255.0f : Y + 0.5f));

        if (!row_has_chroma || (ch.ssx && (x & 1)))
            continue;

        u8  cu = (u8)(U < 0.0f ? 0.0f : (U > 255.0f ? 255.0f : U + 0.5f));
        u8  cv = (u8)(V < 0.0f ? 0.0f : (V > 255.0f ? 255.0f : V + 0.5f));
        i32 cx = ch.ssx ? x >> 1 : x;

        if (ch.semi)
        {
            u8* cp = uvrow + (usize)cx * 2;
            cp[ch.u_byte] = cu;
            cp[ch.v_byte] = cv;
        }
        else
        {
            urow[cx] = cu;
            vrow[cx] = cv;
        }
    }
}

void mel_image__packed_yuv_to_canonical(const mel_image_format* f, const Mel_Image* src, i32 y, mel_image_canon out)
{
    mel_image_yuv m = f->yuv;
    i32           w = out.w;
    f32           kr = m.kr, kb = m.kb, kg = m.kg;
    f32           inv_kg = 1.0f / kg;
    f32           rc_coef = 2.0f * (1.0f - kr);
    f32           bc_coef = 2.0f * (1.0f - kb);
    f32           gv_coef = -kr * rc_coef * inv_kg;
    f32           gu_coef = -kb * bc_coef * inv_kg;
    f32           yscale = m.full_range ? (1.0f / 255.0f) : (1.0f / 219.0f);
    f32           cscale = m.full_range ? (1.0f / 255.0f) : (1.0f / 224.0f);
    f32           ybias = m.full_range ? 0.0f : 16.0f;

    mel_assert((w & 1) == 0 && "packed 4:2:2 requires even width");
    mel_image__srgb_lut_init();

    Mel_Image_Plane p = mel_image_plane(src, 0);
    const u8*       row = p.pixels + (usize)y * p.stride;

    for (i32 x = 0; x < w; x += 2)
    {
        const u8* mp = row + (usize)x * 2;
        f32       un = ((f32)mp[m.pu_byte] - 128.0f) * cscale;
        f32       vn = ((f32)mp[m.pv_byte] - 128.0f) * cscale;
        f32       rc = rc_coef * vn;
        f32       bc = bc_coef * un;
        f32       gc = gv_coef * vn + gu_coef * un;

        for (i32 j = 0; j < 2; j++)
        {
            f32 yn = ((f32)mp[j ? m.y1_byte : m.y0_byte] - ybias) * yscale;
            f32 r = yn + rc;
            f32 b = yn + bc;
            f32 g = yn + gc;

            r = mel_image__srgb_to_lin_f(r);
            g = mel_image__srgb_to_lin_f(g);
            b = mel_image__srgb_to_lin_f(b);

            out.row[x + j] = (mel_color){ r, g, b, 1.0f };
        }
    }
}

void mel_image__packed_yuv_from_canonical(const mel_image_format* f, Mel_Image* dst, i32 y, mel_image_canon in)
{
    mel_image_yuv m = f->yuv;
    i32           w = in.w;
    f32           kr = m.kr, kg = m.kg, kb = m.kb;
    f32           inv_ub = 1.0f / (2.0f * (1.0f - kb));
    f32           inv_vr = 1.0f / (2.0f * (1.0f - kr));

    mel_assert((w & 1) == 0 && "packed 4:2:2 requires even width");
    mel_image__srgb_lut_init();

    Mel_Image_Plane p = mel_image_plane(dst, 0);
    u8*             row = p.pixels + (usize)y * p.stride;

    for (i32 x = 0; x < w; x++)
    {
        mel_color c = in.row[x];
        f32       inv = c.a > 0.0f ? 1.0f / c.a : 0.0f;
        f32       r = mel_image__lin_to_srgb_f(c.r * inv);
        f32       g = mel_image__lin_to_srgb_f(c.g * inv);
        f32       b = mel_image__lin_to_srgb_f(c.b * inv);

        f32 yn = kr * r + kg * g + kb * b;
        f32 un = (b - yn) * inv_ub;
        f32 vn = (r - yn) * inv_vr;

        f32 yscale = m.full_range ? 255.0f : 219.0f;
        f32 cscale = m.full_range ? 255.0f : 224.0f;
        f32 ybias = m.full_range ? 0.0f : 16.0f;
        f32 Y = yn * yscale + ybias;
        f32 U = un * cscale + 128.0f;
        f32 V = vn * cscale + 128.0f;

        u8* mp = row + (usize)(x & ~1) * 2;
        mp[(x & 1) ? m.y1_byte : m.y0_byte] = (u8)(Y < 0.0f ? 0.0f : (Y > 255.0f ? 255.0f : Y + 0.5f));

        if (x & 1)
            continue;

        mp[m.pu_byte] = (u8)(U < 0.0f ? 0.0f : (U > 255.0f ? 255.0f : U + 0.5f));
        mp[m.pv_byte] = (u8)(V < 0.0f ? 0.0f : (V > 255.0f ? 255.0f : V + 0.5f));
    }
}

static void k_packed_yuv_to_planar_yuv(const Mel_Image* src, Mel_Image* dst)
{
    Mel_Image_Plane sp = mel_image_plane(src, 0);
    Mel_Image_Plane dy = mel_image_plane(dst, 0);
    i32             w = src->w, h = src->h;
    mel_image_yuv   sm = src->format->yuv;
    mel_image_yuv   dm = dst->format->yuv;

    mel_assert((w & 1) == 0 && "packed 4:2:2 requires even width");
    mel_assert(sm.full_range == dm.full_range && "yuv->yuv transcode requires matching range");

    for (i32 y = 0; y < h; y++)
    {
        const u8* restrict sp0 = sp.pixels + (usize)y * sp.stride + sm.y0_byte;
        u8* restrict dr = dy.pixels + (usize)y * dy.stride;
        for (i32 x = 0; x < w; x++)
        {
            dr[x] = sp0[0];
            sp0 += 2;
        }
    }

    bool semi = (dst->format->plane_count == 2);
    i32  cw = (w + 1) / 2;

    Mel_Image_Plane up = mel_image_plane(dst, 1);
    Mel_Image_Plane vp = semi ? up : mel_image_plane(dst, 2);
    i32             ssy = up.h < h ? 1 : 0;
    i32             ch = up.h;

    for (i32 cy = 0; cy < ch; cy++)
    {
        i32 y0 = ssy ? (cy * 2) : cy;
        i32 y1 = (ssy && y0 + 1 < h) ? y0 + 1 : y0;
        const u8* restrict r0 = sp.pixels + (usize)y0 * sp.stride;
        const u8* restrict r1 = sp.pixels + (usize)y1 * sp.stride;
        u8* restrict urow = up.pixels + (usize)cy * up.stride;
        u8* restrict vrow = semi ? urow : (vp.pixels + (usize)cy * vp.stride);

        for (i32 cx = 0; cx < cw; cx++)
        {
            const u8* m0 = r0 + (usize)cx * 4;
            const u8* m1 = r1 + (usize)cx * 4;
            u32       u = (u32)m0[sm.pu_byte] + (u32)m1[sm.pu_byte];
            u32       v = (u32)m0[sm.pv_byte] + (u32)m1[sm.pv_byte];
            u8        cu = (u8)((u + 1) >> 1);
            u8        cv = (u8)((v + 1) >> 1);
            if (semi)
            {
                u8* cp = urow + (usize)cx * 2;
                cp[dm.u_byte] = cu;
                cp[dm.v_byte] = cv;
            }
            else
            {
                urow[cx] = cu;
                vrow[cx] = cv;
            }
        }
    }
}

static void k_packed_yuv_gray8(const Mel_Image* src, Mel_Image* dst)
{
    Mel_Image_Plane sp = mel_image_plane(src, 0);
    Mel_Image_Plane d = mel_image_plane(dst, 0);
    i32             w = src->w, h = src->h;
    mel_image_yuv   m = src->format->yuv;
    mel_assert((w & 1) == 0 && "packed 4:2:2 requires even width");
    mel_image__srgb_lut_init();
    const u8* tab = m.full_range ? g_srgb.identity : g_srgb.vr_to_full;
    for (i32 y = 0; y < h; y++)
    {
        const u8* restrict sr = sp.pixels + (usize)y * sp.stride + m.y0_byte;
        u8* restrict dr = d.pixels + (usize)y * d.stride;
        for (i32 x = 0; x < w; x++)
        {
            dr[x] = tab[*sr];
            sr += 2;
        }
    }
}

static void k_packed_yuv_rgba8(const Mel_Image* src, Mel_Image* dst)
{
    Mel_Image_Plane         sp = mel_image_plane(src, 0);
    Mel_Image_Plane         d = mel_image_plane(dst, 0);
    i32                     w = src->w, h = src->h;
    const mel_image_format* f = src->format;
    mel_image_yuv           m = f->yuv;
    f32                     kr = m.kr, kb = m.kb, kg = m.kg;
    f32                     inv_kg = 1.0f / kg;
    f32                     rc_coef = 2.0f * (1.0f - kr);
    f32                     bc_coef = 2.0f * (1.0f - kb);
    f32                     gv_coef = -kr * rc_coef * inv_kg;
    f32                     gu_coef = -kb * bc_coef * inv_kg;
    f32                     yscale = m.full_range ? 1.0f : (255.0f / 219.0f);
    f32                     cscale = m.full_range ? 1.0f : (255.0f / 224.0f);
    f32                     ybias = m.full_range ? 0.0f : 16.0f;
    bool                    dst_linear = (dst->format->to_linear == mel_image__tf_linear);

    mel_assert((w & 1) == 0 && "packed 4:2:2 requires even width");
    mel_image__srgb_lut_init();
    const u8* tab = dst_linear ? g_srgb.srgb_to_lin : g_srgb.identity;

    for (i32 y = 0; y < h; y++)
    {
        const u8* restrict sr = sp.pixels + (usize)y * sp.stride;
        u8* restrict dr = d.pixels + (usize)y * d.stride;
        for (i32 x = 0; x < w; x += 2)
        {
            const u8* mp = sr + (usize)x * 2;
            f32       un = ((f32)mp[m.pu_byte] - 128.0f) * cscale;
            f32       vn = ((f32)mp[m.pv_byte] - 128.0f) * cscale;
            f32       rc = rc_coef * vn;
            f32       bc = bc_coef * un;
            f32       gc = gv_coef * vn + gu_coef * un;

            for (i32 j = 0; j < 2; j++)
            {
                f32 yn = ((f32)mp[j ? m.y1_byte : m.y0_byte] - ybias) * yscale;
                f32 r = yn + rc;
                f32 b = yn + bc;
                f32 g = yn + gc;

                u8 ru = (u8)(r < 0.0f ? 0.0f : (r > 255.0f ? 255.0f : r + 0.5f));
                u8 gu = (u8)(g < 0.0f ? 0.0f : (g > 255.0f ? 255.0f : g + 0.5f));
                u8 bu = (u8)(b < 0.0f ? 0.0f : (b > 255.0f ? 255.0f : b + 0.5f));

                u8* q = dr + (usize)(x + j) * 4;
                q[0] = tab[ru];
                q[1] = tab[gu];
                q[2] = tab[bu];
                q[3] = 255;
            }
        }
    }
}

static void k_rgba8_bgra8(const Mel_Image* src, Mel_Image* dst)
{
    Mel_Image_Plane s = mel_image_plane(src, 0);
    Mel_Image_Plane d = mel_image_plane(dst, 0);
    i32             w = src->w, h = src->h;
    for (i32 y = 0; y < h; y++)
    {
        const u8* restrict sr = s.pixels + (usize)y * s.stride;
        u8* restrict dr = d.pixels + (usize)y * d.stride;
        for (i32 x = 0; x < w; x++)
        {
            const u8* p = sr + (usize)x * 4;
            u8*       q = dr + (usize)x * 4;
            q[0] = p[2];
            q[1] = p[1];
            q[2] = p[0];
            q[3] = p[3];
        }
    }
}

static void k_gray8_rgba8(const Mel_Image* src, Mel_Image* dst)
{
    Mel_Image_Plane s = mel_image_plane(src, 0);
    Mel_Image_Plane d = mel_image_plane(dst, 0);
    i32             w = src->w, h = src->h;
    for (i32 y = 0; y < h; y++)
    {
        const u8* restrict sr = s.pixels + (usize)y * s.stride;
        u8* restrict dr = d.pixels + (usize)y * d.stride;
        for (i32 x = 0; x < w; x++)
        {
            u8  v = sr[x];
            u8* q = dr + (usize)x * 4;
            q[0] = v;
            q[1] = v;
            q[2] = v;
            q[3] = 255;
        }
    }
}

static void k_rgba8_gray8(const Mel_Image* src, Mel_Image* dst)
{
    Mel_Image_Plane s = mel_image_plane(src, 0);
    Mel_Image_Plane d = mel_image_plane(dst, 0);
    i32             w = src->w, h = src->h;
    for (i32 y = 0; y < h; y++)
    {
        const u8* restrict sr = s.pixels + (usize)y * s.stride;
        u8* restrict dr = d.pixels + (usize)y * d.stride;
        for (i32 x = 0; x < w; x++)
        {
            const u8* p = sr + (usize)x * 4;
            u32       R = p[0], G = p[1], B = p[2];
            dr[x] = (u8)((R * 77 + G * 150 + B * 29) >> 8);
        }
    }
}

static void k_straight_premul(const Mel_Image* src, Mel_Image* dst)
{
    Mel_Image_Plane s = mel_image_plane(src, 0);
    Mel_Image_Plane d = mel_image_plane(dst, 0);
    i32             w = src->w, h = src->h;
    for (i32 y = 0; y < h; y++)
    {
        const u8* restrict sr = s.pixels + (usize)y * s.stride;
        u8* restrict dr = d.pixels + (usize)y * d.stride;
        for (i32 x = 0; x < w; x++)
        {
            const u8* p = sr + (usize)x * 4;
            u8*       q = dr + (usize)x * 4;
            u32       a = p[3];
            q[0] = (u8)((p[0] * a + 127) / 255);
            q[1] = (u8)((p[1] * a + 127) / 255);
            q[2] = (u8)((p[2] * a + 127) / 255);
            q[3] = (u8)a;
        }
    }
}

static void k_premul_straight(const Mel_Image* src, Mel_Image* dst)
{
    Mel_Image_Plane s = mel_image_plane(src, 0);
    Mel_Image_Plane d = mel_image_plane(dst, 0);
    i32             w = src->w, h = src->h;
    for (i32 y = 0; y < h; y++)
    {
        const u8* restrict sr = s.pixels + (usize)y * s.stride;
        u8* restrict dr = d.pixels + (usize)y * d.stride;
        for (i32 x = 0; x < w; x++)
        {
            const u8* p = sr + (usize)x * 4;
            u8*       q = dr + (usize)x * 4;
            u32       a = p[3];
            if (a == 0)
            {
                q[0] = q[1] = q[2] = q[3] = 0;
                continue;
            }
            q[0] = (u8)((p[0] * 255 + a / 2) / a);
            q[1] = (u8)((p[1] * 255 + a / 2) / a);
            q[2] = (u8)((p[2] * 255 + a / 2) / a);
            q[3] = (u8)a;
        }
    }
}

static void k_srgb_lin8(const Mel_Image* src, Mel_Image* dst)
{
    mel_image__srgb_lut_init();
    Mel_Image_Plane s = mel_image_plane(src, 0);
    Mel_Image_Plane d = mel_image_plane(dst, 0);
    i32             w = src->w, h = src->h;
    for (i32 y = 0; y < h; y++)
    {
        const u8* restrict sr = s.pixels + (usize)y * s.stride;
        u8* restrict dr = d.pixels + (usize)y * d.stride;
        for (i32 x = 0; x < w; x++)
        {
            const u8* p = sr + (usize)x * 4;
            u8*       q = dr + (usize)x * 4;
            q[0] = g_srgb.srgb_to_lin[p[0]];
            q[1] = g_srgb.srgb_to_lin[p[1]];
            q[2] = g_srgb.srgb_to_lin[p[2]];
            q[3] = p[3];
        }
    }
}

static void k_lin_srgb8(const Mel_Image* src, Mel_Image* dst)
{
    mel_image__srgb_lut_init();
    Mel_Image_Plane s = mel_image_plane(src, 0);
    Mel_Image_Plane d = mel_image_plane(dst, 0);
    i32             w = src->w, h = src->h;
    for (i32 y = 0; y < h; y++)
    {
        const u8* restrict sr = s.pixels + (usize)y * s.stride;
        u8* restrict dr = d.pixels + (usize)y * d.stride;
        for (i32 x = 0; x < w; x++)
        {
            const u8* p = sr + (usize)x * 4;
            u8*       q = dr + (usize)x * 4;
            q[0] = g_srgb.lin_to_srgb[p[0]];
            q[1] = g_srgb.lin_to_srgb[p[1]];
            q[2] = g_srgb.lin_to_srgb[p[2]];
            q[3] = p[3];
        }
    }
}

static void k_yuv_rgba8(const Mel_Image* src, Mel_Image* dst)
{
    Mel_Image_Plane         yp = mel_image_plane(src, 0);
    Mel_Image_Plane         d = mel_image_plane(dst, 0);
    i32                     w = src->w, h = src->h;
    const mel_image_format* f = src->format;
    mel_image_yuv           m = f->yuv;
    mel_image__yuv_chroma   ch = mel_image__yuv_chroma_resolve(f, src);
    f32                     kr = m.kr, kb = m.kb, kg = m.kg;
    f32                     inv_kg = 1.0f / kg;
    f32                     rc_coef = 2.0f * (1.0f - kr);
    f32                     bc_coef = 2.0f * (1.0f - kb);
    f32                     gv_coef = -kr * rc_coef * inv_kg;
    f32                     gu_coef = -kb * bc_coef * inv_kg;
    f32                     yscale = m.full_range ? 1.0f : (255.0f / 219.0f);
    f32                     cscale = m.full_range ? 1.0f : (255.0f / 224.0f);
    f32                     ybias = m.full_range ? 0.0f : 16.0f;
    bool                    dst_linear = (dst->format->to_linear == mel_image__tf_linear);

    mel_image__srgb_lut_init();
    const u8* tab = dst_linear ? g_srgb.srgb_to_lin : g_srgb.identity;

    for (i32 y = 0; y < h; y++)
    {
        const u8* restrict yrow = yp.pixels + (usize)y * yp.stride;
        u8* restrict dr = d.pixels + (usize)y * d.stride;
        i32       cy = ch.ssy ? y >> 1 : y;
        const u8* uvrow = ch.semi ? ch.uv + (usize)cy * ch.uv_stride : NULL;
        const u8* urow = ch.semi ? NULL : ch.up + (usize)cy * ch.u_stride;
        const u8* vrow = ch.semi ? NULL : ch.vp + (usize)cy * ch.v_stride;

        for (i32 x = 0; x < w; x++)
        {
            i32 cx = ch.ssx ? x >> 1 : x;
            f32 U, V;
            if (ch.semi)
            {
                const u8* c = uvrow + (usize)cx * 2;
                U = (f32)c[ch.u_byte];
                V = (f32)c[ch.v_byte];
            }
            else
            {
                U = (f32)urow[cx];
                V = (f32)vrow[cx];
            }

            f32 yn = ((f32)yrow[x] - ybias) * yscale;
            f32 un = (U - 128.0f) * cscale;
            f32 vn = (V - 128.0f) * cscale;

            f32 r = yn + rc_coef * vn;
            f32 b = yn + bc_coef * un;
            f32 g = yn + gv_coef * vn + gu_coef * un;

            u8 ru = (u8)(r < 0.0f ? 0.0f : (r > 255.0f ? 255.0f : r + 0.5f));
            u8 gu = (u8)(g < 0.0f ? 0.0f : (g > 255.0f ? 255.0f : g + 0.5f));
            u8 bu = (u8)(b < 0.0f ? 0.0f : (b > 255.0f ? 255.0f : b + 0.5f));

            u8* q = dr + (usize)x * 4;
            q[0] = tab[ru];
            q[1] = tab[gu];
            q[2] = tab[bu];
            q[3] = 255;
        }
    }
}

static void k_yuv_gray8(const Mel_Image* src, Mel_Image* dst)
{
    Mel_Image_Plane yp = mel_image_plane(src, 0);
    Mel_Image_Plane d = mel_image_plane(dst, 0);
    i32             w = src->w, h = src->h;
    bool            full = src->format->yuv.full_range;
    if (!full)
        mel_image__srgb_lut_init();
    for (i32 y = 0; y < h; y++)
    {
        const u8* sr = yp.pixels + (usize)y * yp.stride;
        u8*       dr = d.pixels + (usize)y * d.stride;
        if (full)
        {
            memcpy(dr, sr, (usize)w);
            continue;
        }
        for (i32 x = 0; x < w; x++)
            dr[x] = g_srgb.vr_to_full[sr[x]];
    }
}

typedef struct
{
    const mel_image_format* src;
    const mel_image_format* dst;
    mel_image_kernel        fn;
} mel_image_kernel_entry;

mel_image_kernel mel_image__find_kernel(const mel_image_format* s, const mel_image_format* d)
{
    static const mel_image_kernel_entry table[] = {
        { &mel_image_rgba8, &mel_image_bgra8, k_rgba8_bgra8 },
        { &mel_image_bgra8, &mel_image_rgba8, k_rgba8_bgra8 },
        { &mel_image_gray8, &mel_image_rgba8, k_gray8_rgba8 },
        { &mel_image_rgba8, &mel_image_gray8, k_rgba8_gray8 },
        { &mel_image_rgba8, &mel_image_rgba8_premul, k_straight_premul },
        { &mel_image_rgba8_premul, &mel_image_rgba8, k_premul_straight },
        { &mel_image_rgba8_srgb, &mel_image_rgba8, k_srgb_lin8 },
        { &mel_image_rgba8, &mel_image_rgba8_srgb, k_lin_srgb8 },
        { &mel_image_nv12, &mel_image_rgba8, k_yuv_rgba8 },
        { &mel_image_nv12_full, &mel_image_rgba8, k_yuv_rgba8 },
        { &mel_image_nv21, &mel_image_rgba8, k_yuv_rgba8 },
        { &mel_image_i420, &mel_image_rgba8, k_yuv_rgba8 },
        { &mel_image_i422, &mel_image_rgba8, k_yuv_rgba8 },
        { &mel_image_i444, &mel_image_rgba8, k_yuv_rgba8 },
        { &mel_image_nv12, &mel_image_rgba8_srgb, k_yuv_rgba8 },
        { &mel_image_nv12_full, &mel_image_rgba8_srgb, k_yuv_rgba8 },
        { &mel_image_nv21, &mel_image_rgba8_srgb, k_yuv_rgba8 },
        { &mel_image_i420, &mel_image_rgba8_srgb, k_yuv_rgba8 },
        { &mel_image_i422, &mel_image_rgba8_srgb, k_yuv_rgba8 },
        { &mel_image_i444, &mel_image_rgba8_srgb, k_yuv_rgba8 },
        { &mel_image_nv12, &mel_image_gray8, k_yuv_gray8 },
        { &mel_image_nv12_full, &mel_image_gray8, k_yuv_gray8 },
        { &mel_image_nv21, &mel_image_gray8, k_yuv_gray8 },
        { &mel_image_i420, &mel_image_gray8, k_yuv_gray8 },
        { &mel_image_i422, &mel_image_gray8, k_yuv_gray8 },
        { &mel_image_i444, &mel_image_gray8, k_yuv_gray8 },
        { &mel_image_yuyv, &mel_image_rgba8, k_packed_yuv_rgba8 },
        { &mel_image_uyvy, &mel_image_rgba8, k_packed_yuv_rgba8 },
        { &mel_image_yuyv, &mel_image_rgba8_srgb, k_packed_yuv_rgba8 },
        { &mel_image_uyvy, &mel_image_rgba8_srgb, k_packed_yuv_rgba8 },
        { &mel_image_yuyv, &mel_image_gray8, k_packed_yuv_gray8 },
        { &mel_image_uyvy, &mel_image_gray8, k_packed_yuv_gray8 },
        { &mel_image_yuyv, &mel_image_i420, k_packed_yuv_to_planar_yuv },
        { &mel_image_uyvy, &mel_image_i420, k_packed_yuv_to_planar_yuv },
        { &mel_image_yuyv, &mel_image_i422, k_packed_yuv_to_planar_yuv },
        { &mel_image_uyvy, &mel_image_i422, k_packed_yuv_to_planar_yuv },
        { &mel_image_yuyv, &mel_image_nv12, k_packed_yuv_to_planar_yuv },
        { &mel_image_uyvy, &mel_image_nv12, k_packed_yuv_to_planar_yuv },
        { &mel_image_yuyv, &mel_image_nv21, k_packed_yuv_to_planar_yuv },
        { &mel_image_uyvy, &mel_image_nv21, k_packed_yuv_to_planar_yuv },
    };
    for (usize i = 0; i < sizeof(table) / sizeof(table[0]); i++)
        if (table[i].src == s && table[i].dst == d)
            return table[i].fn;
    return NULL;
}

static void mel_image__copy_identical(const Mel_Image* src, Mel_Image* dst)
{
    i32 planes = src->format->plane_count;
    for (i32 k = 0; k < planes; k++)
    {
        Mel_Image_Plane s = mel_image_plane(src, k);
        Mel_Image_Plane d = mel_image_plane(dst, k);
        i32             row = s.stride < d.stride ? s.stride : d.stride;
        for (i32 y = 0; y < s.h; y++)
            memcpy(d.pixels + (usize)y * d.stride, s.pixels + (usize)y * s.stride, (usize)row);
    }
}

static bool mel_image__convert_canonical(const Mel_Image* src, Mel_Image* dst, const Mel_Alloc* a)
{
    if (!src->format->to_canonical || !dst->format->from_canonical)
    {
        mel_log_error("image", "convert: no path %s -> %s", src->format->name, dst->format->name);
        return false;
    }
    if (!a)
    {
        mel_log_error("image", "convert: %s -> %s needs scratch but no allocator available", src->format->name, dst->format->name);
        return false;
    }

    i32        w = src->w;
    mel_color* row = (mel_color*)mel_alloc(a, (usize)w * sizeof(mel_color));
    if (!row)
    {
        mel_log_error("image", "convert: scratch row OOM (%d px)", w);
        return false;
    }

    mel_image_canon canon = { row, w };
    for (i32 y = 0; y < src->h; y++)
    {
        src->format->to_canonical(src->format, src, y, canon);
        dst->format->from_canonical(dst->format, dst, y, canon);
    }

    mel_dealloc(a, row);
    return true;
}

static bool mel_image__convert_check(const Mel_Image* src, Mel_Image* dst)
{
    if (!src || !dst || !src->format || !dst->format)
        return false;
    if (src->w != dst->w || src->h != dst->h)
    {
        mel_log_error("image", "convert: size mismatch src %dx%d dst %dx%d", src->w, src->h, dst->w, dst->h);
        return false;
    }
    return true;
}

bool mel_image_convert_scratch(const Mel_Image* src, Mel_Image* dst, const Mel_Alloc* scratch)
{
    if (!mel_image__convert_check(src, dst))
        return false;

    if (src->format == dst->format)
    {
        mel_image__copy_identical(src, dst);
        return true;
    }

    mel_image_kernel k = mel_image__find_kernel(src->format, dst->format);
    if (k)
    {
        k(src, dst);
        return true;
    }

    const Mel_Alloc* a = scratch ? scratch : (dst->alloc ? dst->alloc : src->alloc);
    return mel_image__convert_canonical(src, dst, a);
}

bool mel_image_convert_via_canonical(const Mel_Image* src, Mel_Image* dst, const Mel_Alloc* scratch)
{
    if (!mel_image__convert_check(src, dst))
        return false;
    const Mel_Alloc* a = scratch ? scratch : (dst->alloc ? dst->alloc : src->alloc);
    return mel_image__convert_canonical(src, dst, a);
}

bool mel_image_convert(const Mel_Image* src, Mel_Image* dst)
{
    if (!mel_image__convert_check(src, dst))
        return false;

    if (src->format == dst->format)
    {
        mel_image__copy_identical(src, dst);
        return true;
    }

    mel_image_kernel k = mel_image__find_kernel(src->format, dst->format);
    if (k)
    {
        k(src, dst);
        return true;
    }

    const Mel_Alloc* a = dst->alloc ? dst->alloc : src->alloc;
    if (!a)
    {
        mel_log_error("image", "convert: %s -> %s needs scratch but both images are non-owning (wrapped); use mel_image_convert_scratch", src->format->name, dst->format->name);
        return false;
    }
    return mel_image__convert_canonical(src, dst, a);
}

bool mel_image_convert_new(const Mel_Image* src, const mel_image_format* fmt, const Mel_Alloc* a, Mel_Image* out)
{
    if (!src || !src->format || !fmt || !a || !out)
        return false;
    if (!mel_image__init_uninit(out, fmt, src->w, src->h, a))
        return false;
    if (!mel_image_convert(src, out))
    {
        mel_image_free(out);
        return false;
    }
    return true;
}

bool mel_image_to_rgba(const Mel_Image* src, const Mel_Alloc* a, Mel_Image* out) { return mel_image_convert_new(src, &mel_image_rgba8, a, out); }

bool mel_image_premultiply(const Mel_Image* src, const Mel_Alloc* a, Mel_Image* out) { return mel_image_convert_new(src, &mel_image_rgba8_premul, a, out); }

bool mel_image_unpremultiply(const Mel_Image* src, const Mel_Alloc* a, Mel_Image* out) { return mel_image_convert_new(src, &mel_image_rgba8, a, out); }
