#include <test/test.h>

#include <core/compiler.h>

#include <allocator/heap.h>
#include <color/rgba8.h>
#include <image/image.h>
#include <math.geo/rect.h>

#include <paint/paint.h>

#include <stdlib.h>

MEL_CONSTRUCTOR static void paint_test_nofork(void) { setenv("MEL_TEST_NOFORK", "1", 1); }

static inline mel_color8 sample(Mel_Pixmap_Pixels px, i32 x, i32 y) { return px.pixels[(usize)y * (usize)(px.stride / 4) + (usize)x]; }

MEL_TEST(paint, pixmap_geometry)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Pixmap   pm = mel_pixmap_create(a, 64, 64);
    Mel_Drawable d = mel_pixmap_drawable(pm);
    MEL_REQUIRE(mel_drawable_alive(d));

    Mel_Painter p = mel_painter_begin(d);
    mel_painter_clear(&p, mel_color8_rgba(0, 0, 255, 255));                               /* blue background */
    mel_painter_fill_rect(&p, mel_rect(16, 16, 32, 32), mel_color8_rgba(255, 0, 0, 255)); /* red center      */
    mel_painter_fill_rect(&p, mel_rect(0, 0, 8, 8), mel_color8_rgba(0, 255, 0, 255));     /* green top-left  */
    mel_painter_end(&p);

    Mel_Pixmap_Pixels px = mel_pixmap_pixels(pm);
    MEL_REQUIRE(px.pixels != NULL);
    MEL_EXPECT_EQ(px.w, 64);
    MEL_EXPECT_EQ(px.h, 64);

    /* y-down origin: (0,0) is the top-left, so it lands in memory row 0. */
    mel_color8 tl = sample(px, 1, 1);
    MEL_EXPECT_EQ(tl.r, 0);
    MEL_EXPECT_EQ(tl.g, 255);
    MEL_EXPECT_EQ(tl.b, 0);
    MEL_EXPECT_EQ(tl.a, 255);

    mel_color8 mid = sample(px, 32, 32);
    MEL_EXPECT_EQ(mid.r, 255);
    MEL_EXPECT_EQ(mid.g, 0);
    MEL_EXPECT_EQ(mid.b, 0);

    /* untouched far corner keeps the background */
    mel_color8 br = sample(px, 63, 63);
    MEL_EXPECT_EQ(br.r, 0);
    MEL_EXPECT_EQ(br.g, 0);
    MEL_EXPECT_EQ(br.b, 255);

    mel_pixmap_destroy(pm);
    MEL_EXPECT(!mel_drawable_alive(d));
}

/* Two painters live at once — impossible with a single global painter; the
 * second begin would clobber the first. Guards against that regression. */
MEL_TEST(paint, two_painters_compose)
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Pixmap a = mel_pixmap_create(alloc, 16, 16);
    Mel_Pixmap b = mel_pixmap_create(alloc, 16, 16);

    Mel_Painter pa = mel_painter_begin(mel_pixmap_drawable(a));
    Mel_Painter pb = mel_painter_begin(mel_pixmap_drawable(b));
    mel_painter_clear(&pa, mel_color8_rgba(255, 0, 0, 255));
    mel_painter_clear(&pb, mel_color8_rgba(0, 255, 0, 255));
    mel_painter_fill_rect(&pa, mel_rect(4, 4, 8, 8), mel_color8_rgba(0, 0, 255, 255));
    mel_painter_end(&pb);
    mel_painter_end(&pa);

    Mel_Pixmap_Pixels pxa = mel_pixmap_pixels(a);
    Mel_Pixmap_Pixels pxb = mel_pixmap_pixels(b);
    MEL_EXPECT_EQ(sample(pxa, 1, 1).r, 255); /* A background red          */
    MEL_EXPECT_EQ(sample(pxa, 6, 6).b, 255); /* A blue square             */
    MEL_EXPECT_EQ(sample(pxb, 1, 1).g, 255); /* B green, not clobbered by pa */
    MEL_EXPECT_EQ(sample(pxb, 6, 6).g, 255);

    mel_pixmap_destroy(a);
    mel_pixmap_destroy(b);
}

static inline void put_rgba(Mel_Image_Plane pl, i32 x, i32 y, mel_color8 c)
{
    u8* px = pl.pixels + (usize)y * (usize)pl.stride + (usize)x * 4;
    px[0] = c.r;
    px[1] = c.g;
    px[2] = c.b;
    px[3] = c.a;
}

MEL_TEST(paint, draw_image_blits_pattern)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_rgba8, 4, 4, a));
    Mel_Image_Plane sp = mel_image_plane(&src, 0);

    for (i32 y = 0; y < 4; ++y)
        for (i32 x = 0; x < 4; ++x)
            put_rgba(sp, x, y, mel_color8_rgba(40, 40, 40, 255));
    put_rgba(sp, 0, 0, mel_color8_rgba(255, 0, 0, 255));   /* top-left      red   */
    put_rgba(sp, 3, 0, mel_color8_rgba(0, 255, 0, 255));   /* top-right     green */
    put_rgba(sp, 0, 3, mel_color8_rgba(0, 0, 255, 255));   /* bottom-left   blue  */
    put_rgba(sp, 3, 3, mel_color8_rgba(255, 255, 0, 255)); /* bottom-right  yellow*/

    Mel_Pixmap  pm = mel_pixmap_create(a, 16, 16);
    Mel_Painter p = mel_painter_begin(mel_pixmap_drawable(pm));
    mel_painter_clear(&p, mel_color8_rgba(0, 0, 0, 255));
    mel_painter_draw_image(&p, &src, mel_rect(4, 4, 4, 4), a); /* 1:1, upright */
    mel_painter_end(&p);

    Mel_Pixmap_Pixels px = mel_pixmap_pixels(pm);

    mel_color8 tl = sample(px, 4, 4); /* dst top-left -> src (0,0) red */
    MEL_EXPECT_EQ(tl.r, 255);
    MEL_EXPECT_EQ(tl.g, 0);
    MEL_EXPECT_EQ(tl.b, 0);

    mel_color8 tr = sample(px, 7, 4); /* dst top-right -> src (3,0) green */
    MEL_EXPECT_EQ(tr.r, 0);
    MEL_EXPECT_EQ(tr.g, 255);
    MEL_EXPECT_EQ(tr.b, 0);

    mel_color8 bl = sample(px, 4, 7); /* dst bottom-left -> src (0,3) blue */
    MEL_EXPECT_EQ(bl.r, 0);
    MEL_EXPECT_EQ(bl.g, 0);
    MEL_EXPECT_EQ(bl.b, 255);

    mel_color8 brc = sample(px, 7, 7); /* dst bottom-right -> src (3,3) yellow */
    MEL_EXPECT_EQ(brc.r, 255);
    MEL_EXPECT_EQ(brc.g, 255);
    MEL_EXPECT_EQ(brc.b, 0);

    mel_color8 mid = sample(px, 5, 5); /* interior fill */
    MEL_EXPECT_EQ(mid.r, 40);
    MEL_EXPECT_EQ(mid.g, 40);
    MEL_EXPECT_EQ(mid.b, 40);

    mel_color8 outside = sample(px, 1, 1); /* untouched background */
    MEL_EXPECT_EQ(outside.r, 0);
    MEL_EXPECT_EQ(outside.g, 0);
    MEL_EXPECT_EQ(outside.b, 0);

    mel_image_free(&src);
    mel_pixmap_destroy(pm);
}

MEL_TEST(paint, draw_image_gray8_path)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_gray8, 4, 4, a));
    Mel_Image_Plane sp = mel_image_plane(&src, 0);
    for (i32 y = 0; y < 4; ++y)
        for (i32 x = 0; x < 4; ++x)
            sp.pixels[(usize)y * (usize)sp.stride + (usize)x] = 128;

    Mel_Pixmap  pm = mel_pixmap_create(a, 8, 8);
    Mel_Painter p = mel_painter_begin(mel_pixmap_drawable(pm));
    mel_painter_clear(&p, mel_color8_rgba(0, 0, 0, 255));
    mel_painter_draw_image(&p, &src, mel_rect(0, 0, 4, 4), a);
    mel_painter_end(&p);

    Mel_Pixmap_Pixels px = mel_pixmap_pixels(pm);
    mel_color8        g = sample(px, 1, 1);
    MEL_EXPECT_EQ(g.r, g.g);
    MEL_EXPECT_EQ(g.g, g.b);
    MEL_EXPECT_EQ(g.r, 128);

    mel_image_free(&src);
    mel_pixmap_destroy(pm);
}

MEL_TEST(paint, draw_image_scaled_2x)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Image src;
    MEL_REQUIRE(mel_image_init(&src, &mel_image_rgba8, 2, 2, a));
    Mel_Image_Plane sp = mel_image_plane(&src, 0);
    put_rgba(sp, 0, 0, mel_color8_rgba(255, 0, 0, 255));
    put_rgba(sp, 1, 0, mel_color8_rgba(0, 255, 0, 255));
    put_rgba(sp, 0, 1, mel_color8_rgba(0, 0, 255, 255));
    put_rgba(sp, 1, 1, mel_color8_rgba(255, 255, 0, 255));

    Mel_Pixmap  pm = mel_pixmap_create(a, 8, 8);
    Mel_Painter p = mel_painter_begin(mel_pixmap_drawable(pm));
    mel_painter_clear(&p, mel_color8_rgba(0, 0, 0, 255));
    mel_painter_draw_image(&p, &src, mel_rect(0, 0, 8, 8), a);
    mel_painter_end(&p);

    Mel_Pixmap_Pixels px = mel_pixmap_pixels(pm);
    MEL_EXPECT_EQ(sample(px, 1, 1).r, 255);
    MEL_EXPECT_EQ(sample(px, 6, 1).g, 255);
    MEL_EXPECT_EQ(sample(px, 1, 6).b, 255);
    MEL_EXPECT_EQ(sample(px, 6, 6).r, 255);
    MEL_EXPECT_EQ(sample(px, 6, 6).g, 255);

    mel_image_free(&src);
    mel_pixmap_destroy(pm);
}

MEL_TEST(paint, draw_image_wrapped_bgra8_converts)
{
    const Mel_Alloc* a = mel_alloc_heap();

    u8              buf[4 * 4 * 4];
    Mel_Image_Plane plane = { .pixels = buf, .stride = 4 * 4, .w = 4, .h = 4, .bpp = 4 };
    for (i32 y = 0; y < 4; ++y)
        for (i32 x = 0; x < 4; ++x)
        {
            u8* q = buf + (usize)y * 16 + (usize)x * 4;
            q[0] = 200;
            q[1] = 50;
            q[2] = 10;
            q[3] = 255;
        }

    Mel_Image src;
    MEL_REQUIRE(mel_image_wrap_plane(&src, &mel_image_bgra8, &plane));
    MEL_EXPECT_NULL(src.alloc);

    Mel_Pixmap  pm = mel_pixmap_create(a, 8, 8);
    Mel_Painter p = mel_painter_begin(mel_pixmap_drawable(pm));
    mel_painter_clear(&p, mel_color8_rgba(0, 0, 0, 255));
    mel_painter_draw_image(&p, &src, mel_rect(0, 0, 4, 4), a);
    mel_painter_end(&p);

    Mel_Pixmap_Pixels px = mel_pixmap_pixels(pm);
    mel_color8        c = sample(px, 1, 1);
    MEL_EXPECT_EQ(c.r, 10);
    MEL_EXPECT_EQ(c.g, 50);
    MEL_EXPECT_EQ(c.b, 200);

    mel_image_free(&src);
    mel_pixmap_destroy(pm);
}

MEL_TEST(paint, pixmap_image_wraps_into_convert)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Pixmap  pm = mel_pixmap_create(a, 8, 4);
    Mel_Painter p = mel_painter_begin(mel_pixmap_drawable(pm));
    mel_painter_clear(&p, mel_color8_rgba(255, 0, 0, 255));
    mel_painter_end(&p);

    Mel_Image view;
    MEL_REQUIRE(mel_pixmap_image(pm, &view));
    MEL_EXPECT_NULL(view.alloc);
    MEL_EXPECT_EQ(view.w, 8);
    MEL_EXPECT_EQ(view.h, 4);

    Mel_Pixmap_Pixels px = mel_pixmap_pixels(pm);
    Mel_Image_Plane   vp = mel_image_plane(&view, 0);
    MEL_EXPECT_EQ((const void*)vp.pixels, (const void*)px.pixels);
    MEL_EXPECT_EQ(vp.stride, px.stride);

    Mel_Image straight;
    MEL_REQUIRE(mel_image_init(&straight, &mel_image_rgba8, 8, 4, a));
    MEL_REQUIRE(mel_image_convert(&view, &straight));
    Mel_Image_Plane qp = mel_image_plane(&straight, 0);
    MEL_EXPECT_EQ(qp.pixels[0], 255);
    MEL_EXPECT_EQ(qp.pixels[1], 0);
    MEL_EXPECT_EQ(qp.pixels[2], 0);
    MEL_EXPECT_EQ(qp.pixels[3], 255);

    mel_image_free(&straight);
    mel_image_free(&view);
    mel_pixmap_destroy(pm);
}
