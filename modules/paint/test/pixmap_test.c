#include <test/test.h>

#include <core/compiler.h>

#include <allocator/heap.h>
#include <color/rgba8.h>
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

MEL_TEST(paint, pixmap_image_wraps_into_convert)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Pixmap pm = mel_pixmap_create(a, 8, 4);
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
