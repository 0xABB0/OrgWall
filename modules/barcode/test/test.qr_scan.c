#include <barcode/qr.h>
#include <barcode/qr_detect.h>

#include <allocator/heap.h>
#include <image/image.h>
#include <test/test.h>

#include <math.h>
#include <string.h>

static u8* mel__render_matrix(const mel_barcode_matrix* m, i32 scale, i32 quiet, i32* out_w, i32* out_h, const Mel_Alloc* a)
{
    i32 dim = m->width;
    i32 px = (dim + 2 * quiet) * scale;
    u8* buf = mel_alloc(a, (usize)px * (usize)px);
    if (buf == NULL)
    {
        return NULL;
    }
    memset(buf, 255, (usize)px * (usize)px);
    for (i32 r = 0; r < dim; ++r)
    {
        for (i32 c = 0; c < dim; ++c)
        {
            if (!mel_barcode_matrix_get(m, c, r))
            {
                continue;
            }
            i32 x0 = (c + quiet) * scale;
            i32 y0 = (r + quiet) * scale;
            for (i32 dy = 0; dy < scale; ++dy)
            {
                u8* row = buf + (usize)(y0 + dy) * (usize)px + (usize)x0;
                memset(row, 0, (usize)scale);
            }
        }
    }
    *out_w = px;
    *out_h = px;
    return buf;
}

static u8* mel__rotate(const u8* src, i32 sw, i32 sh, f32 angle, i32* out_w, i32* out_h, const Mel_Alloc* a)
{
    f32 ca = cosf(angle);
    f32 sa = sinf(angle);
    i32 pad = (i32)((f32)(sw + sh) * 0.25f) + 4;
    i32 dw = sw + 2 * pad;
    i32 dh = sh + 2 * pad;
    u8* dst = mel_alloc(a, (usize)dw * (usize)dh);
    if (dst == NULL)
    {
        return NULL;
    }
    memset(dst, 255, (usize)dw * (usize)dh);

    f32 cx = (f32)dw / 2.0f;
    f32 cy = (f32)dh / 2.0f;
    f32 scx = (f32)sw / 2.0f;
    f32 scy = (f32)sh / 2.0f;
    for (i32 y = 0; y < dh; ++y)
    {
        for (i32 x = 0; x < dw; ++x)
        {
            f32 rx = (f32)x - cx;
            f32 ry = (f32)y - cy;
            f32 sx = ca * rx + sa * ry + scx;
            f32 sy = -sa * rx + ca * ry + scy;
            i32 ix = (i32)(sx + 0.5f);
            i32 iy = (i32)(sy + 0.5f);
            if (ix < 0 || ix >= sw || iy < 0 || iy >= sh)
            {
                continue;
            }
            dst[(usize)y * (usize)dw + (usize)x] = src[(usize)iy * (usize)sw + (usize)ix];
        }
    }
    *out_w = dw;
    *out_h = dh;
    return dst;
}

static void mel__scan_check_dim(const char* data, mel_qr_opt opt, i32 scale, i32 expect_dim)
{
    const Mel_Alloc* a = mel_alloc_heap();

    mel_barcode_matrix m;
    MEL_REQUIRE(mel_qr_encode(&m, data, opt, a));
    if (expect_dim > 0)
    {
        MEL_REQUIRE_EQ(m.width, expect_dim);
        MEL_REQUIRE_EQ(m.height, expect_dim);
    }

    i32 w = 0, h = 0;
    u8* buf = mel__render_matrix(&m, scale, 4, &w, &h, a);
    MEL_REQUIRE(buf != NULL);

    mel_image_gray g = { buf, w, w, h };

    mel_qr_decoded d;
    MEL_REQUIRE(mel_qr_decode_image(&g, &d, a));
    MEL_REQUIRE_EQ((i32)d.len, (i32)strlen(data));
    MEL_REQUIRE_STR_EQ(d.text, data);

    mel_qr_decoded_free(&d, a);
    mel_dealloc(a, buf);
    mel_barcode_matrix_free(&m);
}

static void mel__scan_check(const char* data, mel_qr_opt opt, i32 scale) { mel__scan_check_dim(data, opt, scale, 0); }

MEL_TEST(barcode_qr_scan, axis_aligned_numeric_v1) { mel__scan_check("01234567", (mel_qr_opt){ .ecc = mel_qr_ecc_m(), .version = 1, .mask = -1 }, 8); }

MEL_TEST(barcode_qr_scan, axis_aligned_alnum_v1) { mel__scan_check("HELLO WORLD", (mel_qr_opt){ .ecc = mel_qr_ecc_q(), .version = 1, .mask = -1 }, 6); }

MEL_TEST(barcode_qr_scan, axis_aligned_byte_v2) { mel__scan_check_dim("Melody scanner!!", (mel_qr_opt){ .ecc = mel_qr_ecc_m(), .version = 2, .mask = -1 }, 6, 25); }

static const char* MEL__QR_SCAN_PAYLOAD[7] = {
    "01234567", "Melody scanner!!", "abcdefghijklmnopqrstuvwxyz0123", "QR VERSION FOUR PAYLOAD TEXT 123456", "Version five carries a comfortably long byte payload string here", "ABCDEFGHIJ", "ABCDEFGHIJ",
};

static void mel__scan_version(i32 version, i32 scale)
{
    i32 expect_dim = version * 4 + 17;
    mel__scan_check_dim(MEL__QR_SCAN_PAYLOAD[version - 1], (mel_qr_opt){ .ecc = mel_qr_ecc_m(), .version = version, .mask = -1 }, scale, expect_dim);
    for (i32 mask = 0; mask < 8; ++mask)
    {
        mel__scan_check_dim(MEL__QR_SCAN_PAYLOAD[version - 1], (mel_qr_opt){ .ecc = mel_qr_ecc_m(), .version = version, .mask = mask }, scale, expect_dim);
    }
}

MEL_TEST(barcode_qr_scan, all_versions_v1) { mel__scan_version(1, 6); }
MEL_TEST(barcode_qr_scan, all_versions_v2) { mel__scan_version(2, 6); }
MEL_TEST(barcode_qr_scan, all_versions_v3) { mel__scan_version(3, 5); }
MEL_TEST(barcode_qr_scan, all_versions_v4) { mel__scan_version(4, 5); }
MEL_TEST(barcode_qr_scan, all_versions_v5) { mel__scan_version(5, 5); }
MEL_TEST(barcode_qr_scan, all_versions_v6) { mel__scan_version(6, 5); }
MEL_TEST(barcode_qr_scan, all_versions_v7) { mel__scan_version(7, 5); }

MEL_TEST(barcode_qr_scan, v2_all_masks_small_scale)
{
    for (i32 mask = 0; mask < 8; ++mask)
    {
        for (i32 scale = 4; scale <= 8; ++scale)
        {
            mel__scan_check_dim("Melody scanner!!", (mel_qr_opt){ .ecc = mel_qr_ecc_m(), .version = 2, .mask = mask }, scale, 25);
        }
    }
}

MEL_TEST(barcode_qr_scan, axis_aligned_v3_multiblock)
{
    char payload[33];
    for (i32 i = 0; i < 32; ++i)
    {
        payload[i] = 'a';
    }
    payload[32] = '\0';
    mel__scan_check(payload, (mel_qr_opt){ .ecc = mel_qr_ecc_q(), .version = 0, .mask = -1 }, 5);
}

MEL_TEST(barcode_qr_scan, axis_aligned_v7_alignment) { mel__scan_check("ABCDEFGHIJ", (mel_qr_opt){ .ecc = mel_qr_ecc_m(), .version = 7, .mask = -1 }, 5); }

MEL_TEST(barcode_qr_scan, all_masks)
{
    for (i32 mask = 0; mask < 8; ++mask)
    {
        mel__scan_check("MASK SCAN 0123", (mel_qr_opt){ .ecc = mel_qr_ecc_m(), .version = 0, .mask = mask }, 6);
    }
}

MEL_TEST(barcode_qr_scan, rotated_small_angle)
{
    const Mel_Alloc* a = mel_alloc_heap();

    mel_barcode_matrix m;
    MEL_REQUIRE(mel_qr_encode(&m, "ROTATED QR 42", (mel_qr_opt){ .ecc = mel_qr_ecc_q(), .version = 0, .mask = -1 }, a));

    i32 w = 0, h = 0;
    u8* buf = mel__render_matrix(&m, 10, 6, &w, &h, a);
    MEL_REQUIRE(buf != NULL);

    i32 rw = 0, rh = 0;
    u8* rot = mel__rotate(buf, w, h, 0.13f, &rw, &rh, a);
    MEL_REQUIRE(rot != NULL);

    mel_image_gray g = { rot, rw, rw, rh };

    mel_qr_decoded d;
    MEL_REQUIRE(mel_qr_decode_image(&g, &d, a));
    MEL_REQUIRE_STR_EQ(d.text, "ROTATED QR 42");

    mel_qr_decoded_free(&d, a);
    mel_dealloc(a, rot);
    mel_dealloc(a, buf);
    mel_barcode_matrix_free(&m);
}

MEL_TEST(barcode_qr_scan, blank_image_fails)
{
    const Mel_Alloc* a = mel_alloc_heap();
    i32              w = 80, h = 80;
    u8*              buf = mel_alloc(a, (usize)w * (usize)h);
    MEL_REQUIRE(buf != NULL);
    memset(buf, 255, (usize)w * (usize)h);

    mel_image_gray g = { buf, w, w, h };
    mel_qr_decoded d;
    MEL_REQUIRE(!mel_qr_decode_image(&g, &d, a));

    mel_dealloc(a, buf);
}
