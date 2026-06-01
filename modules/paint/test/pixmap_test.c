#include <test/test.h>

#include <allocator/heap.h>
#include <color/rgba8.h>
#include <math.geo/rect.h>

#include <paint/paint.h>

static inline mel_color8 sample(Mel_Pixmap_Pixels px, i32 x, i32 y) { return px.pixels[(usize)y * (usize)(px.stride / 4) + (usize)x]; }

MEL_TEST(paint, pixmap_geometry)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Pixmap   pm = mel_pixmap_create(a, 64, 64);
    Mel_Drawable d = mel_pixmap_drawable(pm);
    MEL_REQUIRE(mel_drawable_alive(d));

    Mel_Painter* p = mel_painter_begin(d);
    mel_painter_clear(p, mel_color8_rgba(0, 0, 255, 255));                               /* blue background      */
    mel_painter_fill_rect(p, mel_rect(16, 16, 32, 32), mel_color8_rgba(255, 0, 0, 255)); /* red center      */
    mel_painter_fill_rect(p, mel_rect(0, 0, 8, 8), mel_color8_rgba(0, 255, 0, 255));     /* green top-left  */
    mel_painter_end(p);

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
