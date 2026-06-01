#include <test/test.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <color/rgba8.h>
#include <math.geo/rect.h>

#include <paint/paint.h>

#include <string.h>

#include <CoreGraphics/CoreGraphics.h>

static inline mel_color8 sample(const u8* buf, i32 stride, i32 x, i32 y)
{
    const mel_color8* px = (const mel_color8*)buf;
    return px[(usize)y * (usize)(stride / 4) + (usize)x];
}

MEL_TEST(paint, borrow_draws_into_external_context)
{
    const Mel_Alloc* a = mel_alloc_heap();

    i32   w = 32, h = 32;
    i32   stride = w * 4;
    usize size = (usize)stride * (usize)h;
    u8*   buf = (u8*)mel_alloc(a, size);
    memset(buf, 0, size);

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef    cg = CGBitmapContextCreate(buf, (size_t)w, (size_t)h, 8, (size_t)stride, cs, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(cs);
    CGContextTranslateCTM(cg, 0, h);
    CGContextScaleCTM(cg, 1, -1);

    Mel_Drawable d = mel_drawable_borrow(cg, w, h);
    MEL_REQUIRE(mel_drawable_alive(d));

    Mel_Painter p = mel_painter_begin(d);
    mel_painter_clear(&p, mel_color8_rgba(0, 0, 255, 255));
    mel_painter_fill_rect(&p, mel_rect(8, 8, 8, 8), mel_color8_rgba(255, 0, 0, 255));
    mel_painter_end(&p);

    mel_color8 tl = sample(buf, stride, 0, 0);
    MEL_EXPECT_EQ(tl.b, 255);
    MEL_EXPECT_EQ(tl.r, 0);

    mel_color8 mid = sample(buf, stride, 10, 10);
    MEL_EXPECT_EQ(mid.r, 255);
    MEL_EXPECT_EQ(mid.b, 0);

    mel_drawable_release(d);
    MEL_EXPECT(!mel_drawable_alive(d));

    mel_color8 after = sample(buf, stride, 10, 10);
    MEL_EXPECT_EQ(after.r, 255);

    CGContextRelease(cg);
    mel_dealloc(a, buf);
}
