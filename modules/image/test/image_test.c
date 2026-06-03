#include <test/test.h>

#include <allocator/heap.h>

#include <collection.array/array.h>

#include <image/image.h>

MEL_TEST(image, packed_geometry)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image img;
    MEL_REQUIRE(mel_image_init(&img, &mel_image_rgba8, 64, 32, a));
    MEL_EXPECT_EQ(mel_image_plane_count(&img), 1);

    Mel_Image_Plane p = mel_image_plane(&img, 0);
    MEL_EXPECT_EQ(p.w, 64);
    MEL_EXPECT_EQ(p.h, 32);
    MEL_EXPECT_EQ(p.stride, 64 * 4);
    MEL_EXPECT_NOT_NULL(p.pixels);

    MEL_EXPECT_EQ(mel_image_byte_size(&mel_image_rgba8, 64, 32, 1), (usize)64 * 32 * 4);

    mel_image_free(&img);
    MEL_EXPECT_NULL(img.data);
}

MEL_TEST(image, aligned_stride)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image img;
    MEL_REQUIRE(mel_image_init_aligned(&img, &mel_image_gray8, 30, 4, 16, a));
    Mel_Image_Plane p = mel_image_plane(&img, 0);
    MEL_EXPECT_EQ(p.stride, 32);
    MEL_EXPECT_EQ(p.w, 30);
    mel_image_free(&img);
}

MEL_TEST(image, nv12_planes)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image img;
    MEL_REQUIRE(mel_image_init(&img, &mel_image_nv12, 64, 32, a));
    MEL_EXPECT_EQ(mel_image_plane_count(&img), 2);

    Mel_Image_Plane y = mel_image_plane(&img, 0);
    Mel_Image_Plane uv = mel_image_plane(&img, 1);
    MEL_EXPECT_EQ(y.w, 64);
    MEL_EXPECT_EQ(y.h, 32);
    MEL_EXPECT_EQ(y.stride, 64);
    MEL_EXPECT_EQ(uv.w, 32);
    MEL_EXPECT_EQ(uv.h, 16);
    MEL_EXPECT_EQ(uv.stride, 64);

    MEL_EXPECT_EQ(mel_image_byte_size(&mel_image_nv12, 64, 32, 1), (usize)64 * 32 + (usize)64 * 16);
    MEL_EXPECT_EQ((isize)(uv.pixels - y.pixels), (isize)64 * 32);

    mel_image_free(&img);
}

MEL_TEST(image, i420_planes)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image img;
    MEL_REQUIRE(mel_image_init(&img, &mel_image_i420, 8, 8, a));
    MEL_EXPECT_EQ(mel_image_plane_count(&img), 3);

    Mel_Image_Plane u = mel_image_plane(&img, 1);
    Mel_Image_Plane v = mel_image_plane(&img, 2);
    MEL_EXPECT_EQ(u.w, 4);
    MEL_EXPECT_EQ(u.h, 4);
    MEL_EXPECT_EQ((isize)(v.pixels - u.pixels), (isize)4 * 4);

    mel_image_free(&img);
}

MEL_TEST(image, gray_borrow_zero_copy)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image img;
    MEL_REQUIRE(mel_image_init(&img, &mel_image_nv12, 16, 16, a));

    Mel_Image_Plane y = mel_image_plane(&img, 0);
    y.pixels[0] = 123;
    y.pixels[1] = 200;

    mel_image_gray g = mel_image_gray_borrow(&img);
    MEL_EXPECT_EQ((const void*)g.pixels, (const void*)y.pixels);
    MEL_EXPECT_EQ(g.pixels[0], 123);
    MEL_EXPECT_EQ(g.pixels[1], 200);
    MEL_EXPECT_EQ(g.w, 16);
    MEL_EXPECT_EQ(g.stride, 16);

    mel_image_free(&img);
}

MEL_TEST(image, rgba8_to_gray_luma)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image img;
    MEL_REQUIRE(mel_image_init(&img, &mel_image_rgba8, 2, 1, a));
    Mel_Image_Plane p = mel_image_plane(&img, 0);
    p.pixels[0] = 255;
    p.pixels[1] = 0;
    p.pixels[2] = 0;
    p.pixels[3] = 255;
    p.pixels[4] = 0;
    p.pixels[5] = 255;
    p.pixels[6] = 0;
    p.pixels[7] = 255;

    Mel_Image g;
    MEL_REQUIRE(mel_image_to_gray(&img, a, &g));
    Mel_Image_Plane gp = mel_image_plane(&g, 0);
    MEL_EXPECT_EQ(gp.pixels[0], (u8)((255 * 77) >> 8));
    MEL_EXPECT_EQ(gp.pixels[1], (u8)((255 * 150) >> 8));

    mel_image_free(&g);
    mel_image_free(&img);
}

MEL_TEST(image, to_gray_rejects_float)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image img;
    MEL_REQUIRE(mel_image_init(&img, &mel_image_rgba32f, 4, 4, a));
    Mel_Image g;
    MEL_EXPECT(!mel_image_to_gray(&img, a, &g));
    mel_image_free(&img);
}

MEL_TEST(image, wrap_borrows)
{
    u8 buf[16 * 16];
    for (i32 i = 0; i < 16 * 16; i++)
        buf[i] = (u8)i;

    Mel_Image_Plane pl = { buf, 16, 16, 16 };
    Mel_Image       img;
    MEL_REQUIRE(mel_image_wrap(&img, &mel_image_gray8, 16, 16, &pl, 1));
    MEL_EXPECT_NULL(img.alloc);

    mel_image_gray g = mel_image_gray_borrow(&img);
    MEL_EXPECT_EQ((const void*)g.pixels, (const void*)buf);
    MEL_EXPECT_EQ(g.pixels[5], 5);

    mel_image_free(&img);
}

MEL_TEST(image, convert_rgba8_bgra8_roundtrip)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_rgba8, 3, 2, a));
    Mel_Image_Plane sp = mel_image_plane(&src, 0);
    for (i32 i = 0; i < 3 * 2 * 4; i++)
        sp.pixels[i] = (u8)(i * 7 + 3);

    Mel_Image bgra;
    MEL_REQUIRE(mel_image_init(&bgra, &mel_image_bgra8, 3, 2, a));
    MEL_REQUIRE(mel_image_convert(&src, &bgra));

    Mel_Image_Plane bp = mel_image_plane(&bgra, 0);
    MEL_EXPECT_EQ(bp.pixels[0], sp.pixels[2]);
    MEL_EXPECT_EQ(bp.pixels[1], sp.pixels[1]);
    MEL_EXPECT_EQ(bp.pixels[2], sp.pixels[0]);
    MEL_EXPECT_EQ(bp.pixels[3], sp.pixels[3]);

    Mel_Image back;
    MEL_REQUIRE(mel_image_init(&back, &mel_image_rgba8, 3, 2, a));
    MEL_REQUIRE(mel_image_convert(&bgra, &back));

    Mel_Image_Plane rp = mel_image_plane(&back, 0);
    for (i32 i = 0; i < 3 * 2 * 4; i++)
        MEL_EXPECT_EQ(rp.pixels[i], sp.pixels[i]);

    mel_image_free(&back);
    mel_image_free(&bgra);
    mel_image_free(&src);
}

MEL_TEST(image, convert_premultiply_roundtrip)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_rgba8, 4, 1, a));
    Mel_Image_Plane sp = mel_image_plane(&src, 0);

    u8 px[16] = { 200, 100, 50, 255, 200, 100, 50, 128, 0, 255, 64, 200, 255, 255, 255, 1 };
    for (i32 i = 0; i < 16; i++)
        sp.pixels[i] = px[i];

    Mel_Image pm;
    MEL_REQUIRE(mel_image_init(&pm, &mel_image_rgba8_premul, 4, 1, a));
    MEL_REQUIRE(mel_image_convert(&src, &pm));

    Mel_Image back;
    MEL_REQUIRE(mel_image_init(&back, &mel_image_rgba8, 4, 1, a));
    MEL_REQUIRE(mel_image_convert(&pm, &back));

    Mel_Image_Plane bp = mel_image_plane(&back, 0);
    for (i32 p = 0; p < 4; p++)
    {
        u8 alpha = px[p * 4 + 3];
        if (alpha == 0)
            continue;
        for (i32 c = 0; c < 3; c++)
        {
            i32 d = (i32)bp.pixels[p * 4 + c] - (i32)px[p * 4 + c];
            MEL_EXPECT_LE(d < 0 ? -d : d, 1);
        }
        MEL_EXPECT_EQ(bp.pixels[p * 4 + 3], alpha);
    }

    mel_image_free(&back);
    mel_image_free(&pm);
    mel_image_free(&src);
}

MEL_TEST(image, convert_srgb_linear_endpoints)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_rgba8_srgb, 3, 1, a));
    Mel_Image_Plane sp = mel_image_plane(&src, 0);
    u8              s3[12] = { 0, 0, 0, 255, 188, 188, 188, 255, 255, 255, 255, 255 };
    for (i32 i = 0; i < 12; i++)
        sp.pixels[i] = s3[i];

    Mel_Image lin;
    MEL_REQUIRE(mel_image_init(&lin, &mel_image_rgba8, 3, 1, a));
    MEL_REQUIRE(mel_image_convert(&src, &lin));

    Mel_Image_Plane lp = mel_image_plane(&lin, 0);
    MEL_EXPECT_EQ(lp.pixels[0], 0);
    MEL_EXPECT_EQ(lp.pixels[8], 255);
    i32 mid = lp.pixels[4];
    MEL_EXPECT_GT(mid, 110);
    MEL_EXPECT_LT(mid, 135);

    Mel_Image back;
    MEL_REQUIRE(mel_image_init(&back, &mel_image_rgba8_srgb, 3, 1, a));
    MEL_REQUIRE(mel_image_convert(&lin, &back));
    Mel_Image_Plane bp = mel_image_plane(&back, 0);
    MEL_EXPECT_EQ(bp.pixels[0], 0);
    MEL_EXPECT_EQ(bp.pixels[8], 255);
    i32 dm = (i32)bp.pixels[4] - 188;
    MEL_EXPECT_LE(dm < 0 ? -dm : dm, 1);

    mel_image_free(&back);
    mel_image_free(&lin);
    mel_image_free(&src);
}

MEL_TEST(image, convert_gray8_rgba8_replicate)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_gray8, 4, 1, a));
    Mel_Image_Plane sp = mel_image_plane(&src, 0);
    sp.pixels[0] = 0;
    sp.pixels[1] = 64;
    sp.pixels[2] = 200;
    sp.pixels[3] = 255;

    Mel_Image rgba;
    MEL_REQUIRE(mel_image_init(&rgba, &mel_image_rgba8, 4, 1, a));
    MEL_REQUIRE(mel_image_convert(&src, &rgba));

    Mel_Image_Plane rp = mel_image_plane(&rgba, 0);
    for (i32 x = 0; x < 4; x++)
    {
        u8 v = sp.pixels[x];
        MEL_EXPECT_EQ(rp.pixels[x * 4 + 0], v);
        MEL_EXPECT_EQ(rp.pixels[x * 4 + 1], v);
        MEL_EXPECT_EQ(rp.pixels[x * 4 + 2], v);
        MEL_EXPECT_EQ(rp.pixels[x * 4 + 3], 255);
    }

    mel_image_free(&rgba);
    mel_image_free(&src);
}

MEL_TEST(image, convert_nv12_rgba8_red)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_nv12, 2, 2, a));
    Mel_Image_Plane y = mel_image_plane(&src, 0);
    Mel_Image_Plane uv = mel_image_plane(&src, 1);
    for (i32 i = 0; i < 2 * 2; i++)
        y.pixels[(i / 2) * y.stride + (i % 2)] = 81;
    uv.pixels[0] = 90;
    uv.pixels[1] = 240;

    Mel_Image rgba;
    MEL_REQUIRE(mel_image_init(&rgba, &mel_image_rgba8, 2, 2, a));
    MEL_REQUIRE(mel_image_convert(&src, &rgba));

    Mel_Image_Plane rp = mel_image_plane(&rgba, 0);
    for (i32 p = 0; p < 4; p++)
    {
        const u8* px = rp.pixels + (usize)(p / 2) * rp.stride + (usize)(p % 2) * 4;
        MEL_EXPECT_GE(px[0], 250);
        MEL_EXPECT_LE(px[1], 4);
        MEL_EXPECT_LE(px[2], 4);
        MEL_EXPECT_EQ(px[3], 255);
    }

    Mel_Image gray;
    MEL_REQUIRE(mel_image_init(&gray, &mel_image_gray8, 2, 2, a));
    MEL_REQUIRE(mel_image_convert(&src, &gray));
    Mel_Image_Plane gp = mel_image_plane(&gray, 0);
    i32             gd = (i32)gp.pixels[0] - 76;
    MEL_EXPECT_LE(gd < 0 ? -gd : gd, 2);

    mel_image_free(&gray);
    mel_image_free(&rgba);
    mel_image_free(&src);
}

MEL_TEST(image, convert_size_mismatch_fails)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_rgba8, 4, 4, a));
    Mel_Image dst;
    MEL_REQUIRE(mel_image_init(&dst, &mel_image_rgba8, 4, 2, a));

    MEL_EXPECT(!mel_image_convert(&src, &dst));

    mel_image_free(&dst);
    mel_image_free(&src);
}

MEL_TEST(image, convert_identity_preserves_zero_alpha)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_rgba8, 2, 1, a));
    Mel_Image_Plane sp = mel_image_plane(&src, 0);
    u8              px[8] = { 200, 100, 50, 0, 10, 20, 30, 255 };
    for (i32 i = 0; i < 8; i++)
        sp.pixels[i] = px[i];

    Mel_Image dst;
    MEL_REQUIRE(mel_image_init(&dst, &mel_image_rgba8, 2, 1, a));
    MEL_REQUIRE(mel_image_convert(&src, &dst));

    Mel_Image_Plane dp = mel_image_plane(&dst, 0);
    for (i32 i = 0; i < 8; i++)
        MEL_EXPECT_EQ(dp.pixels[i], px[i]);

    mel_image_free(&dst);
    mel_image_free(&src);
}

MEL_TEST(image, to_rgba_convenience)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_gray8, 3, 1, a));
    Mel_Image_Plane sp = mel_image_plane(&src, 0);
    sp.pixels[0] = 10;
    sp.pixels[1] = 128;
    sp.pixels[2] = 250;

    Mel_Image rgba;
    MEL_REQUIRE(mel_image_to_rgba(&src, a, &rgba));
    Mel_Image_Plane rp = mel_image_plane(&rgba, 0);
    for (i32 x = 0; x < 3; x++)
    {
        u8 v = sp.pixels[x];
        MEL_EXPECT_EQ(rp.pixels[x * 4 + 0], v);
        MEL_EXPECT_EQ(rp.pixels[x * 4 + 3], 255);
    }

    mel_image_free(&rgba);
    mel_image_free(&src);
}

MEL_TEST(image, convert_canonical_fallback)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_rgba32f, 2, 1, a));
    Mel_Image_Plane sp = mel_image_plane(&src, 0);
    f32             px[8] = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.0f, 0.5f };
    memcpy(sp.pixels, px, sizeof(px));

    Mel_Image out;
    MEL_REQUIRE(mel_image_convert_new(&src, &mel_image_rgba16f, a, &out));

    Mel_Image back;
    MEL_REQUIRE(mel_image_convert_new(&out, &mel_image_rgba32f, a, &back));
    Mel_Image_Plane bp = mel_image_plane(&back, 0);
    f32             r[8];
    memcpy(r, bp.pixels, sizeof(r));
    MEL_EXPECT_FLOAT_EQ(r[0], 1.0f, 0.01);
    MEL_EXPECT_FLOAT_EQ(r[7], 0.5f, 0.01);

    mel_image_free(&back);
    mel_image_free(&out);
    mel_image_free(&src);
}

MEL_TEST(image, roi_offset)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image img;
    MEL_REQUIRE(mel_image_init(&img, &mel_image_rgba8, 8, 8, a));
    Mel_Image_Plane p = mel_image_plane(&img, 0);

    Mel_Image_Plane r = mel_image_plane_roi(p, 2, 3, 3, 2);
    MEL_EXPECT_EQ(r.w, 3);
    MEL_EXPECT_EQ(r.h, 2);
    MEL_EXPECT_EQ(r.bpp, 4);
    MEL_EXPECT_EQ(r.stride, p.stride);
    MEL_EXPECT_EQ((isize)(r.pixels - p.pixels), (isize)3 * p.stride + 2 * 4);

    Mel_Image_Plane clip = mel_image_plane_roi(p, 6, 6, 10, 10);
    MEL_EXPECT_EQ(clip.w, 2);
    MEL_EXPECT_EQ(clip.h, 2);

    mel_image_gray gv = { (const u8*)p.pixels, p.stride, 8, 8 };
    mel_image_gray gr = mel_image_gray_roi(gv, 1, 1, 4, 4);
    MEL_EXPECT_EQ(gr.w, 4);
    MEL_EXPECT_EQ(gr.h, 4);
    MEL_EXPECT_EQ((isize)(gr.pixels - gv.pixels), (isize)1 * gv.stride + 1);

    mel_image_free(&img);
}

MEL_TEST(image, blit_same_format)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_rgba8, 4, 4, a));
    Mel_Image_Plane sp = mel_image_plane(&src, 0);
    for (i32 y = 0; y < 4; y++)
        for (i32 x = 0; x < 4; x++)
        {
            u8* q = sp.pixels + (usize)y * sp.stride + (usize)x * 4;
            q[0] = (u8)(x * 10 + y);
            q[1] = (u8)(x);
            q[2] = (u8)(y);
            q[3] = 255;
        }

    Mel_Image dst;
    MEL_REQUIRE(mel_image_init(&dst, &mel_image_rgba8, 4, 4, a));
    MEL_REQUIRE(mel_image_blit(&dst, 0, 0, &src, 1, 1, 2, 2));

    Mel_Image_Plane dp = mel_image_plane(&dst, 0);
    for (i32 y = 0; y < 2; y++)
        for (i32 x = 0; x < 2; x++)
        {
            const u8* s = sp.pixels + (usize)(y + 1) * sp.stride + (usize)(x + 1) * 4;
            const u8* d = dp.pixels + (usize)y * dp.stride + (usize)x * 4;
            for (i32 c = 0; c < 4; c++)
                MEL_EXPECT_EQ(d[c], s[c]);
        }
    MEL_EXPECT_EQ(dp.pixels[(usize)2 * dp.stride], 0);

    mel_image_free(&dst);
    mel_image_free(&src);
}

MEL_TEST(image, blit_rgba8_gray8)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_rgba8, 2, 2, a));
    Mel_Image_Plane sp = mel_image_plane(&src, 0);
    u8              px[16] = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 128, 128, 128, 255 };
    for (i32 y = 0; y < 2; y++)
        for (i32 x = 0; x < 2; x++)
            memcpy(sp.pixels + (usize)y * sp.stride + (usize)x * 4, px + (y * 2 + x) * 4, 4);

    Mel_Image dst;
    MEL_REQUIRE(mel_image_init(&dst, &mel_image_gray8, 2, 2, a));
    MEL_REQUIRE(mel_image_blit(&dst, 0, 0, &src, 0, 0, 2, 2));

    Mel_Image_Plane dp = mel_image_plane(&dst, 0);
    MEL_EXPECT_EQ(dp.pixels[0], (u8)((255 * 77) >> 8));
    MEL_EXPECT_EQ(dp.pixels[1], (u8)((255 * 150) >> 8));
    MEL_EXPECT_EQ(dp.pixels[dp.stride], (u8)((255 * 29) >> 8));
    MEL_EXPECT_EQ(dp.pixels[dp.stride + 1], (u8)((128 * 77 + 128 * 150 + 128 * 29) >> 8));

    mel_image_free(&dst);
    mel_image_free(&src);
}

MEL_TEST(image, blit_rgb8_rgba8_canonical)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_rgb8, 2, 2, a));
    Mel_Image_Plane sp = mel_image_plane(&src, 0);
    for (i32 y = 0; y < 2; y++)
        for (i32 x = 0; x < 2; x++)
        {
            u8* q = sp.pixels + (usize)y * sp.stride + (usize)x * 3;
            q[0] = (u8)(x * 30 + 10);
            q[1] = (u8)(y * 30 + 20);
            q[2] = (u8)(x * 17 + y * 11 + 5);
        }

    Mel_Image dst;
    MEL_REQUIRE(mel_image_init(&dst, &mel_image_rgba8, 2, 2, a));
    MEL_REQUIRE(mel_image_blit(&dst, 0, 0, &src, 0, 0, 2, 2));

    Mel_Image_Plane dp = mel_image_plane(&dst, 0);
    for (i32 y = 0; y < 2; y++)
        for (i32 x = 0; x < 2; x++)
        {
            const u8* s = sp.pixels + (usize)y * sp.stride + (usize)x * 3;
            const u8* d = dp.pixels + (usize)y * dp.stride + (usize)x * 4;
            for (i32 c = 0; c < 3; c++)
            {
                i32 diff = (i32)d[c] - (i32)s[c];
                MEL_EXPECT_LE(diff < 0 ? -diff : diff, 1);
            }
            MEL_EXPECT_EQ(d[3], 255);
        }

    mel_image_free(&dst);
    mel_image_free(&src);
}

MEL_TEST(image, resize_new_half)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_gray8, 4, 4, a));
    Mel_Image_Plane sp = mel_image_plane(&src, 0);
    for (i32 y = 0; y < 4; y++)
        for (i32 x = 0; x < 4; x++)
            sp.pixels[(usize)y * sp.stride + x] = (u8)(y * 40 + x * 10);

    Mel_Image dst;
    MEL_REQUIRE(mel_image_resize_new(&src, 2, 2, &mel_image_filter_box, a, &dst));
    MEL_EXPECT_EQ(dst.w, 2);
    MEL_EXPECT_EQ(dst.h, 2);

    Mel_Image_Plane dp = mel_image_plane(&dst, 0);
    for (i32 dy = 0; dy < 2; dy++)
        for (i32 dx = 0; dx < 2; dx++)
        {
            u32 acc = 0;
            for (i32 yy = 0; yy < 2; yy++)
                for (i32 xx = 0; xx < 2; xx++)
                    acc += sp.pixels[(usize)(dy * 2 + yy) * sp.stride + (dx * 2 + xx)];
            MEL_EXPECT_EQ(dp.pixels[(usize)dy * dp.stride + dx], (u8)((acc + 2) / 4));
        }

    mel_image_free(&dst);
    mel_image_free(&src);
}

MEL_TEST(image, wrap_plane_roi_resize)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image img;
    MEL_REQUIRE(mel_image_init(&img, &mel_image_gray8, 8, 8, a));
    Mel_Image_Plane p = mel_image_plane(&img, 0);
    for (i32 y = 0; y < 8; y++)
        for (i32 x = 0; x < 8; x++)
            p.pixels[(usize)y * p.stride + x] = (u8)(y * 16 + x);

    Mel_Image_Plane roi = mel_image_plane_roi(p, 2, 2, 4, 4);
    MEL_EXPECT_EQ(roi.w, 4);
    MEL_EXPECT_EQ(roi.h, 4);
    MEL_EXPECT_EQ(roi.stride, p.stride);

    Mel_Image view;
    MEL_REQUIRE(mel_image_wrap_plane(&view, &mel_image_gray8, &roi));
    MEL_EXPECT_NULL(view.alloc);
    MEL_EXPECT_EQ(view.w, 4);
    MEL_EXPECT_EQ(view.h, 4);

    Mel_Image_Plane vp = mel_image_plane(&view, 0);
    MEL_EXPECT_EQ(vp.stride, p.stride);
    MEL_EXPECT_EQ(vp.pixels[0], p.pixels[(usize)2 * p.stride + 2]);

    Mel_Image dst;
    MEL_REQUIRE(mel_image_resize_new(&view, 2, 2, &mel_image_filter_box, a, &dst));
    Mel_Image_Plane dp = mel_image_plane(&dst, 0);
    for (i32 dy = 0; dy < 2; dy++)
        for (i32 dx = 0; dx < 2; dx++)
        {
            u32 acc = 0;
            for (i32 yy = 0; yy < 2; yy++)
                for (i32 xx = 0; xx < 2; xx++)
                    acc += p.pixels[(usize)(2 + dy * 2 + yy) * p.stride + (2 + dx * 2 + xx)];
            MEL_EXPECT_EQ(dp.pixels[(usize)dy * dp.stride + dx], (u8)((acc + 2) / 4));
        }

    mel_image_free(&dst);
    mel_image_free(&img);
}

MEL_TEST(image, resize_2x_nearest)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_rgba8, 2, 2, a));
    Mel_Image_Plane sp = mel_image_plane(&src, 0);
    u8              v[4] = { 10, 40, 70, 100 };
    for (i32 y = 0; y < 2; y++)
        for (i32 x = 0; x < 2; x++)
        {
            u8* q = sp.pixels + (usize)y * sp.stride + (usize)x * 4;
            q[0] = q[1] = q[2] = v[y * 2 + x];
            q[3] = 255;
        }

    Mel_Image dst;
    MEL_REQUIRE(mel_image_init(&dst, &mel_image_rgba8, 4, 4, a));
    MEL_REQUIRE(mel_image_resize(&src, &dst, &mel_image_filter_nearest));

    Mel_Image_Plane dp = mel_image_plane(&dst, 0);
    for (i32 y = 0; y < 4; y++)
        for (i32 x = 0; x < 4; x++)
        {
            u8        want = v[(y / 2) * 2 + (x / 2)];
            const u8* q = dp.pixels + (usize)y * dp.stride + (usize)x * 4;
            MEL_EXPECT_EQ(q[0], want);
            MEL_EXPECT_EQ(q[3], 255);
        }

    mel_image_free(&dst);
    mel_image_free(&src);
}

MEL_TEST(image, resize_half_box_average)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_gray8, 4, 4, a));
    Mel_Image_Plane sp = mel_image_plane(&src, 0);
    for (i32 y = 0; y < 4; y++)
        for (i32 x = 0; x < 4; x++)
            sp.pixels[(usize)y * sp.stride + x] = (u8)(y * 40 + x * 10);

    Mel_Image dst;
    MEL_REQUIRE(mel_image_init(&dst, &mel_image_gray8, 2, 2, a));
    MEL_REQUIRE(mel_image_resize(&src, &dst, &mel_image_filter_box));

    Mel_Image_Plane dp = mel_image_plane(&dst, 0);
    for (i32 dy = 0; dy < 2; dy++)
        for (i32 dx = 0; dx < 2; dx++)
        {
            u32 acc = 0;
            for (i32 yy = 0; yy < 2; yy++)
                for (i32 xx = 0; xx < 2; xx++)
                    acc += sp.pixels[(usize)(dy * 2 + yy) * sp.stride + (dx * 2 + xx)];
            u8 want = (u8)((acc + 2) / 4);
            MEL_EXPECT_EQ(dp.pixels[(usize)dy * dp.stride + dx], want);
        }

    mel_image_free(&dst);
    mel_image_free(&src);
}

MEL_TEST(image, resize_bilinear_integer)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_gray8, 2, 1, a));
    Mel_Image_Plane sp = mel_image_plane(&src, 0);
    sp.pixels[0] = 0;
    sp.pixels[1] = 200;

    Mel_Image dst;
    MEL_REQUIRE(mel_image_init(&dst, &mel_image_gray8, 4, 1, a));
    MEL_REQUIRE(mel_image_resize(&src, &dst, &mel_image_filter_bilinear));

    Mel_Image_Plane dp = mel_image_plane(&dst, 0);
    MEL_EXPECT_EQ(dp.pixels[0], 0);
    MEL_EXPECT_EQ(dp.pixels[3], 200);
    MEL_EXPECT_GT(dp.pixels[1], 0);
    MEL_EXPECT_LT(dp.pixels[1], dp.pixels[2]);

    mel_image_free(&dst);
    mel_image_free(&src);
}

MEL_TEST(image, orient_dihedral)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_gray8, 3, 2, a));
    Mel_Image_Plane sp = mel_image_plane(&src, 0);
    u8              v[6] = { 1, 2, 3, 4, 5, 6 };
    for (i32 y = 0; y < 2; y++)
        for (i32 x = 0; x < 3; x++)
            sp.pixels[(usize)y * sp.stride + x] = v[y * 3 + x];

    Mel_Image r90;
    MEL_REQUIRE(mel_image_orient(&src, a, (Mel_Image_Orient){ 1, false }, &r90));
    MEL_EXPECT_EQ(r90.w, 2);
    MEL_EXPECT_EQ(r90.h, 3);
    Mel_Image_Plane p90 = mel_image_plane(&r90, 0);
    MEL_EXPECT_EQ(p90.pixels[0], 4);
    MEL_EXPECT_EQ(p90.pixels[1], 1);
    MEL_EXPECT_EQ(p90.pixels[(usize)2 * p90.stride + 1], 3);

    Mel_Image r180;
    MEL_REQUIRE(mel_image_orient(&src, a, (Mel_Image_Orient){ 2, false }, &r180));
    MEL_EXPECT_EQ(r180.w, 3);
    MEL_EXPECT_EQ(r180.h, 2);
    Mel_Image_Plane p180 = mel_image_plane(&r180, 0);
    MEL_EXPECT_EQ(p180.pixels[0], 6);
    MEL_EXPECT_EQ(p180.pixels[2], 4);
    MEL_EXPECT_EQ(p180.pixels[(usize)1 * p180.stride + 2], 1);

    Mel_Image r270;
    MEL_REQUIRE(mel_image_orient(&src, a, (Mel_Image_Orient){ 3, false }, &r270));
    Mel_Image_Plane p270 = mel_image_plane(&r270, 0);
    MEL_EXPECT_EQ(p270.pixels[0], 3);
    MEL_EXPECT_EQ(p270.pixels[1], 6);
    MEL_EXPECT_EQ(p270.pixels[(usize)2 * p270.stride], 1);

    Mel_Image flip;
    MEL_REQUIRE(mel_image_orient(&src, a, (Mel_Image_Orient){ 0, true }, &flip));
    MEL_EXPECT_EQ(flip.w, 3);
    MEL_EXPECT_EQ(flip.h, 2);
    Mel_Image_Plane pf = mel_image_plane(&flip, 0);
    MEL_EXPECT_EQ(pf.pixels[0], 3);
    MEL_EXPECT_EQ(pf.pixels[2], 1);
    MEL_EXPECT_EQ(pf.pixels[(usize)1 * pf.stride], 6);

    mel_image_free(&flip);
    mel_image_free(&r270);
    mel_image_free(&r180);
    mel_image_free(&r90);
    mel_image_free(&src);
}

static void codec_fill_rgba(Mel_Image* img)
{
    Mel_Image_Plane p = mel_image_plane(img, 0);
    for (i32 y = 0; y < img->h; y++)
        for (i32 x = 0; x < img->w; x++)
        {
            u8* q = p.pixels + (usize)y * p.stride + (usize)x * 4;
            q[0] = (u8)(x * 17 + y * 3);
            q[1] = (u8)(x * 5 + y * 31);
            q[2] = (u8)(x * 11 + y * 7 + 64);
            q[3] = 255;
        }
}

MEL_TEST(image, codec_png_roundtrip_exact)
{
    const Mel_Alloc* a = mel_alloc_heap();
    mel_image_codec_init(a);

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_rgba8, 7, 5, a));
    codec_fill_rgba(&src);

    Mel_Image_Bytes enc;
    mel_array_init(&enc, a);
    MEL_REQUIRE(mel_image_encode(&src, "png", &enc, a));
    MEL_EXPECT_GT(enc.count, (usize)0);

    Mel_Image dec;
    MEL_REQUIRE(mel_image_load(&dec, enc.items, enc.count, a));
    MEL_EXPECT_EQ(dec.w, 7);
    MEL_EXPECT_EQ(dec.h, 5);
    MEL_EXPECT_EQ((const void*)dec.format, (const void*)&mel_image_rgba8);

    Mel_Image_Plane sp = mel_image_plane(&src, 0);
    Mel_Image_Plane dp = mel_image_plane(&dec, 0);
    for (i32 y = 0; y < 5; y++)
        for (i32 x = 0; x < 7 * 4; x++)
            MEL_EXPECT_EQ(dp.pixels[(usize)y * dp.stride + x], sp.pixels[(usize)y * sp.stride + x]);

    mel_image_free(&dec);
    mel_array_free(&enc);
    mel_image_free(&src);
    mel_image_codec_shutdown();
}

MEL_TEST(image, codec_bmp_roundtrip_exact)
{
    const Mel_Alloc* a = mel_alloc_heap();
    mel_image_codec_init(a);

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_rgba8, 6, 4, a));
    codec_fill_rgba(&src);

    Mel_Image_Bytes enc;
    mel_array_init(&enc, a);
    MEL_REQUIRE(mel_image_encode(&src, "bmp", &enc, a));
    MEL_REQUIRE_GE(enc.count, (usize)2);
    MEL_EXPECT_EQ(enc.items[0], (u8)'B');
    MEL_EXPECT_EQ(enc.items[1], (u8)'M');

    Mel_Image dec;
    MEL_REQUIRE(mel_image_load(&dec, enc.items, enc.count, a));
    MEL_EXPECT_EQ(dec.w, 6);
    MEL_EXPECT_EQ(dec.h, 4);

    Mel_Image_Plane sp = mel_image_plane(&src, 0);
    Mel_Image_Plane dp = mel_image_plane(&dec, 0);
    for (i32 y = 0; y < 4; y++)
        for (i32 x = 0; x < 6; x++)
        {
            const u8* s = sp.pixels + (usize)y * sp.stride + (usize)x * 4;
            const u8* d = dp.pixels + (usize)y * dp.stride + (usize)x * dp.bpp;
            MEL_EXPECT_EQ(d[0], s[0]);
            MEL_EXPECT_EQ(d[1], s[1]);
            MEL_EXPECT_EQ(d[2], s[2]);
        }

    mel_image_free(&dec);
    mel_array_free(&enc);
    mel_image_free(&src);
    mel_image_codec_shutdown();
}

MEL_TEST(image, codec_jpeg_roundtrip_tolerance)
{
    const Mel_Alloc* a = mel_alloc_heap();
    mel_image_codec_init(a);

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_rgb8, 16, 16, a));
    Mel_Image_Plane sp = mel_image_plane(&src, 0);
    for (i32 y = 0; y < 16; y++)
        for (i32 x = 0; x < 16; x++)
        {
            u8* q = sp.pixels + (usize)y * sp.stride + (usize)x * 3;
            q[0] = (u8)(x * 16);
            q[1] = (u8)(y * 16);
            q[2] = 128;
        }

    Mel_Image_Bytes enc;
    mel_array_init(&enc, a);
    MEL_REQUIRE(mel_image_encode(&src, "jpeg", &enc, a));
    MEL_REQUIRE_GE(enc.count, (usize)3);
    MEL_EXPECT_EQ(enc.items[0], (u8)0xFF);
    MEL_EXPECT_EQ(enc.items[1], (u8)0xD8);

    Mel_Image dec;
    MEL_REQUIRE(mel_image_load(&dec, enc.items, enc.count, a));
    MEL_EXPECT_EQ(dec.w, 16);
    MEL_EXPECT_EQ(dec.h, 16);
    MEL_EXPECT_EQ((const void*)dec.format, (const void*)&mel_image_rgb8);

    Mel_Image_Plane dp = mel_image_plane(&dec, 0);
    for (i32 y = 0; y < 16; y++)
        for (i32 x = 0; x < 16; x++)
        {
            const u8* s = sp.pixels + (usize)y * sp.stride + (usize)x * 3;
            const u8* d = dp.pixels + (usize)y * dp.stride + (usize)x * 3;
            for (i32 c = 0; c < 3; c++)
            {
                i32 diff = (i32)d[c] - (i32)s[c];
                MEL_EXPECT_LE(diff < 0 ? -diff : diff, 24);
            }
        }

    mel_image_free(&dec);
    mel_array_free(&enc);
    mel_image_free(&src);
    mel_image_codec_shutdown();
}

MEL_TEST(image, codec_probe_and_unknown)
{
    const Mel_Alloc* a = mel_alloc_heap();
    mel_image_codec_init(a);

    MEL_EXPECT_NOT_NULL((void*)mel_image_codec_find("png"));
    MEL_EXPECT_NOT_NULL((void*)mel_image_codec_find("gif"));
    MEL_EXPECT_NULL((void*)mel_image_codec_find("webp"));

    u8        garbage[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    Mel_Image dec;
    MEL_EXPECT(!mel_image_load(&dec, garbage, sizeof(garbage), a));

    mel_image_codec_shutdown();
}

MEL_TEST(image, codec_load_before_init_fails)
{
    const Mel_Alloc* a = mel_alloc_heap();

    u8        bytes[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    Mel_Image dec;
    MEL_EXPECT(!mel_image_load(&dec, bytes, sizeof(bytes), a));
}
