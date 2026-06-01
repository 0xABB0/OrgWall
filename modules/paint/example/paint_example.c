#include <stdio.h>

#include <allocator/heap.h>
#include <color/rgba8.h>
#include <math.geo/rect.h>
#include <math.vector/vec2.h>
#include <string/str8.h>

#include <paint/paint.h>

static void write_ppm(const char* path, Mel_Pixmap_Pixels px)
{
    FILE* f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", px.w, px.h);
    i32 sw = px.stride / 4;
    for (i32 y = 0; y < px.h; y++)
        for (i32 x = 0; x < px.w; x++)
        {
            mel_color8 c = px.pixels[(usize)y * (usize)sw + (usize)x];
            u8         rgb[3] = { c.r, c.g, c.b };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

static int check(const char* what, mel_color8 got, u8 r, u8 g, u8 b)
{
    if (got.r == r && got.g == g && got.b == b)
        return 0;
    printf("  FAIL %-16s got %3u,%3u,%3u expected %3u,%3u,%3u\n", what, got.r, got.g, got.b, r, g, b);
    return 1;
}

int main(void)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Pixmap   pm = mel_pixmap_create(a, 128, 96);
    Mel_Drawable d = mel_pixmap_drawable(pm);

    Mel_Painter p = mel_painter_begin(d);
    mel_painter_clear(&p, mel_color8_rgba(32, 32, 40, 255));
    mel_painter_fill_rect(&p, mel_rect(8, 24, 48, 48), mel_color8_rgba(220, 40, 40, 255));
    mel_painter_fill_ellipse(&p, mel_rect(64, 24, 48, 48), mel_color8_rgba(40, 200, 90, 255));
    mel_painter_stroke_rect(&p, mel_rect(8, 24, 104, 48), mel_color8_rgba(80, 140, 255, 255), 2);
    mel_painter_draw_line(&p, (Mel_Vec2){ .x = 8, .y = 88 }, (Mel_Vec2){ .x = 120, .y = 88 }, mel_color8_rgba(255, 210, 60, 255), 3);
    mel_painter_fill_round_rect(&p, mel_rect(40, 40, 48, 28), 8, mel_color8_rgba(255, 255, 255, 90));
    mel_painter_draw_text(&p, S8("Melody paint"), (Mel_Vec2){ .x = 10, .y = 4 }, mel_color8_rgba(240, 240, 240, 255), 14);
    mel_painter_end(&p);

    Mel_Pixmap_Pixels px = mel_pixmap_pixels(pm);
    i32               sw = px.stride / 4;
    int               fails = 0;

    /* (2,2) is untouched background — proves y-down origin maps to memory row 0. */
    fails += check("bg corner", px.pixels[2 * sw + 2], 32, 32, 40);
    /* (20,40) is the red rect interior, clear of every later shape. */
    fails += check("red interior", px.pixels[40 * sw + 20], 220, 40, 40);
    /* (88,48) is the green ellipse center. */
    fails += check("ellipse center", px.pixels[48 * sw + 88], 40, 200, 90);

    const char* out = "/tmp/paint-example.ppm";
    write_ppm(out, px);
    printf("paint-example: %dx%d, %d check(s) failed; wrote %s\n", px.w, px.h, fails, out);

    mel_pixmap_destroy(pm);
    printf("paint-example: drawable alive after destroy = %d (expect 0)\n", (int)mel_drawable_alive(d));
    return fails ? 1 : 0;
}
