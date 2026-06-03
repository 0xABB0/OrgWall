#include "img_golden.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>

#include <log/log.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEL_GOLDEN_DIR "modules/gpu/test/golden"

static void mel_golden__golden_path(char* out, usize cap, const char* name) { snprintf(out, cap, "%s/%s.ppm", MEL_GOLDEN_DIR, name); }

static void mel_golden__artifact_path(char* out, usize cap, const char* name, const char* backend, const char* kind) { snprintf(out, cap, "%s/%s.%s.%s.ppm", MEL_GOLDEN_DIR, name, backend, kind); }

bool mel_golden_update_requested(void)
{
    const char* v = getenv("MEL_GPU_GOLDEN_UPDATE");
    return v && v[0] == '1' && v[1] == '\0';
}

static void mel_golden__write_ppm_rgb(const char* path, const u8* rgba, u32 w, u32 h)
{
    FILE* f = fopen(path, "wb");
    if (!f)
    {
        mel_log_error("gpu", "golden: cannot open %s for write (%s)", path, strerror(errno));
        return;
    }
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (u32 i = 0; i < w * h; i++)
        fwrite(rgba + (usize)i * 4, 1, 3, f);
    fclose(f);
}

static u8* mel_golden__load_ppm_rgb(const Mel_Alloc* a, const char* path, u32* out_w, u32* out_h)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;

    char magic[3] = { 0 };
    if (fscanf(f, "%2s", magic) != 1 || strcmp(magic, "P6") != 0)
    {
        mel_log_error("gpu", "golden: %s is not a binary P6 PPM (magic '%s')", path, magic);
        fclose(f);
        return NULL;
    }

    u32 w = 0, h = 0, maxval = 0;
    if (fscanf(f, "%u %u %u", &w, &h, &maxval) != 3)
    {
        mel_log_error("gpu", "golden: %s header malformed (want 'w h maxval')", path);
        fclose(f);
        return NULL;
    }
    if (maxval != 255)
    {
        mel_log_error("gpu", "golden: %s maxval %u unsupported (want 255)", path, maxval);
        fclose(f);
        return NULL;
    }
    if (fgetc(f) == EOF)
    {
        mel_log_error("gpu", "golden: %s truncated before pixel data", path);
        fclose(f);
        return NULL;
    }

    usize rgb_bytes = (usize)w * h * 3;
    u8*   rgb = mel_alloc(a, rgb_bytes);
    if (fread(rgb, 1, rgb_bytes, f) != rgb_bytes)
    {
        mel_log_error("gpu", "golden: %s pixel payload short of %ux%u*3 bytes", path, w, h);
        mel_dealloc(a, rgb);
        fclose(f);
        return NULL;
    }
    fclose(f);

    *out_w = w;
    *out_h = h;
    return rgb;
}

bool mel_golden_check(const char* backend, const char* name, const u8* produced_rgba, u32 width, u32 height, Mel_Golden_Tolerance tol, const char* file, int line)
{
    char golden_path[512];
    mel_golden__golden_path(golden_path, sizeof golden_path, name);

    if (mel_golden_update_requested())
    {
        mel_golden__write_ppm_rgb(golden_path, produced_rgba, width, height);
        mel_log_warn("gpu", "golden: MEL_GPU_GOLDEN_UPDATE=1 -> rewrote reference %s (%ux%u, backend=%s); not asserting", golden_path, width, height, backend);
        return true;
    }

    const Mel_Alloc* a = mel_alloc_heap();
    u32              gw = 0, gh = 0;
    u8*              golden_rgb = mel_golden__load_ppm_rgb(a, golden_path, &gw, &gh);
    if (!golden_rgb)
    {
        mel_test_fail(file, line, "golden[%s] '%s': reference %s missing or unreadable; regenerate with MEL_GPU_GOLDEN_UPDATE=1", backend, name, golden_path);
        return false;
    }

    if (gw != width || gh != height)
    {
        mel_test_fail(file, line, "golden[%s] '%s': dimension mismatch produced %ux%u vs reference %ux%u (%s)", backend, name, width, height, gw, gh, golden_path);
        mel_dealloc(a, golden_rgb);
        return false;
    }

    u32 pixel_count = width * height;
    u32 offending = 0;
    u8  max_delta = 0;
    i32 first_off = -1;

    for (u32 i = 0; i < pixel_count; i++)
    {
        const u8* g = golden_rgb + (usize)i * 3;
        const u8* p = produced_rgba + (usize)i * 4;
        u8        pixel_max = 0;
        for (u32 c = 0; c < 3; c++)
        {
            i32 d = (i32)p[c] - (i32)g[c];
            u8  ad = (u8)(d < 0 ? -d : d);
            if (ad > pixel_max)
                pixel_max = ad;
        }
        if (pixel_max > max_delta)
            max_delta = pixel_max;
        if (pixel_max > tol.max_channel_delta)
        {
            offending++;
            if (first_off < 0)
                first_off = (i32)i;
        }
    }

    f32 frac = pixel_count ? (f32)offending / (f32)pixel_count : 0.0f;
    if (frac <= tol.max_fraction_exceeding)
    {
        mel_dealloc(a, golden_rgb);
        return true;
    }

    char produced_path[512], diff_path[512];
    mel_golden__artifact_path(produced_path, sizeof produced_path, name, backend, "produced");
    mel_golden__artifact_path(diff_path, sizeof diff_path, name, backend, "diff");
    mel_golden__write_ppm_rgb(produced_path, produced_rgba, width, height);

    u8* diff_rgb = mel_alloc(a, (usize)pixel_count * 3);
    for (u32 i = 0; i < pixel_count; i++)
    {
        const u8* g = golden_rgb + (usize)i * 3;
        const u8* p = produced_rgba + (usize)i * 4;
        for (u32 c = 0; c < 3; c++)
        {
            i32 d = (i32)p[c] - (i32)g[c];
            u32 ad = (u32)(d < 0 ? -d : d) * 4u;
            diff_rgb[(usize)i * 3 + c] = (u8)(ad > 255u ? 255u : ad);
        }
    }
    FILE* df = fopen(diff_path, "wb");
    if (df)
    {
        fprintf(df, "P6\n%u %u\n255\n", width, height);
        fwrite(diff_rgb, 1, (usize)pixel_count * 3, df);
        fclose(df);
    }
    mel_dealloc(a, diff_rgb);

    u32       fx = (u32)first_off % width;
    u32       fy = (u32)first_off / width;
    const u8* eg = golden_rgb + (usize)first_off * 3;
    const u8* ap = produced_rgba + (usize)first_off * 4;

    mel_test_fail(file,
                  line,
                  "golden[%s] '%s': %u/%u pixels exceed delta>%u (%.4f > allowed %.4f); max channel delta %u; "
                  "first offender (%u,%u) expected RGB(%u,%u,%u) actual RGBA(%u,%u,%u,%u); produced=%s diff=%s",
                  backend,
                  name,
                  offending,
                  pixel_count,
                  tol.max_channel_delta,
                  (double)frac,
                  (double)tol.max_fraction_exceeding,
                  max_delta,
                  fx,
                  fy,
                  eg[0],
                  eg[1],
                  eg[2],
                  ap[0],
                  ap[1],
                  ap[2],
                  ap[3],
                  produced_path,
                  diff_path);

    mel_dealloc(a, golden_rgb);
    return false;
}
