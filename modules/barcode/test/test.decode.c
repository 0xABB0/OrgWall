#include <barcode/code128.h>
#include <barcode/code39.h>
#include <barcode/decode.h>
#include <barcode/ean.h>
#include <barcode/itf.h>
#include <barcode/matrix.h>
#include <barcode/qr.h>

#include <allocator/heap.h>
#include <image/image.h>
#include <test/test.h>

#include <string.h>

static bool mel__rasterize(const mel_barcode_matrix* m, i32 scale, i32 quiet, i32 rows, Mel_Image* out, const Mel_Alloc* a)
{
    i32 modules_w = m->width + 2 * quiet;
    i32 px_w = modules_w * scale;
    i32 px_h = rows;
    if (!mel_image_init(out, &mel_image_gray8, px_w, px_h, a))
        return false;

    Mel_Image_Plane p = mel_image_plane(out, 0);
    for (i32 y = 0; y < px_h; ++y)
    {
        u8* row = p.pixels + (usize)y * (usize)p.stride;
        for (i32 x = 0; x < px_w; ++x)
        {
            i32  mx = x / scale - quiet;
            bool dark = (mx >= 0 && mx < m->width) ? mel_barcode_matrix_get(m, mx, 0) : false;
            row[x] = dark ? 0 : 255;
        }
    }
    return true;
}

static void mel__roundtrip(const mel_barcode_matrix* m, const char* want_sym, const char* want_text, i32 scale)
{
    Mel_Image img;
    MEL_REQUIRE(mel__rasterize(m, scale, 10, 40, &img, mel_alloc_heap()));

    mel_image_gray gray = mel_image_gray_borrow(&img);
    MEL_REQUIRE(gray.pixels != NULL);

    mel_barcode_decode_result r;
    MEL_REQUIRE(mel_barcode_decode(&gray, &r, mel_alloc_heap()));
    MEL_REQUIRE(r.found);
    MEL_REQUIRE_NOT_NULL(r.text);
    MEL_REQUIRE_STR_EQ(r.symbology, want_sym);
    MEL_REQUIRE_STR_EQ(r.text, want_text);
    MEL_REQUIRE(r.x_end > r.x_start);

    mel_barcode_decode_result_free(&r, mel_alloc_heap());
    mel_image_free(&img);
}

MEL_TEST(barcode_decode, ean13_roundtrip)
{
    mel_barcode_matrix m;
    MEL_REQUIRE(mel_ean13_encode(&m, "5901234123457", 1, mel_alloc_heap()));
    mel__roundtrip(&m, "EAN-13", "5901234123457", 3);
    mel_barcode_matrix_free(&m);
}

MEL_TEST(barcode_decode, ean13_from_12_digits)
{
    mel_barcode_matrix m;
    MEL_REQUIRE(mel_ean13_encode(&m, "400638133393", 1, mel_alloc_heap()));
    mel__roundtrip(&m, "EAN-13", "4006381333931", 4);
    mel_barcode_matrix_free(&m);
}

MEL_TEST(barcode_decode, ean8_roundtrip)
{
    mel_barcode_matrix m;
    MEL_REQUIRE(mel_ean8_encode(&m, "9638507", 1, mel_alloc_heap()));
    mel__roundtrip(&m, "EAN-8", "96385074", 5);
    mel_barcode_matrix_free(&m);
}

MEL_TEST(barcode_decode, upca_decodes_as_ean13)
{
    mel_barcode_matrix m;
    MEL_REQUIRE(mel_upca_encode(&m, "03600029145", 1, mel_alloc_heap()));
    mel__roundtrip(&m, "EAN-13", "0036000291452", 4);
    mel_barcode_matrix_free(&m);
}

MEL_TEST(barcode_decode, code128_roundtrip)
{
    mel_barcode_matrix m;
    MEL_REQUIRE(mel_code128_encode(&m, "Melody42", 1, mel_alloc_heap()));
    mel__roundtrip(&m, "Code128", "Melody42", 3);
    mel_barcode_matrix_free(&m);
}

MEL_TEST(barcode_decode, code128_digit_run)
{
    mel_barcode_matrix m;
    MEL_REQUIRE(mel_code128_encode(&m, "12345678", 1, mel_alloc_heap()));
    mel__roundtrip(&m, "Code128", "12345678", 3);
    mel_barcode_matrix_free(&m);
}

MEL_TEST(barcode_decode, code39_roundtrip)
{
    mel_barcode_matrix m;
    MEL_REQUIRE(mel_code39_encode(&m, "CODE39", 1, (mel_code39_opt){ 0 }, mel_alloc_heap()));
    mel__roundtrip(&m, "Code39", "CODE39", 3);
    mel_barcode_matrix_free(&m);
}

MEL_TEST(barcode_decode, itf_roundtrip)
{
    mel_barcode_matrix m;
    MEL_REQUIRE(mel_itf_encode(&m, "1234567890", 1, (mel_itf_opt){ 0 }, mel_alloc_heap()));
    mel__roundtrip(&m, "ITF", "1234567890", 3);
    mel_barcode_matrix_free(&m);
}

MEL_TEST(barcode_decode, blank_image_does_not_decode)
{
    Mel_Image img;
    MEL_REQUIRE(mel_image_init(&img, &mel_image_gray8, 200, 40, mel_alloc_heap()));
    Mel_Image_Plane p = mel_image_plane(&img, 0);
    for (i32 y = 0; y < p.h; ++y)
        memset(p.pixels + (usize)y * (usize)p.stride, 255, (usize)p.w);

    mel_image_gray gray = mel_image_gray_borrow(&img);
    mel_barcode_decode_result r;
    MEL_REQUIRE(!mel_barcode_decode(&gray, &r, mel_alloc_heap()));
    MEL_REQUIRE(!r.found);
    mel_image_free(&img);
}

static bool mel__rasterize_2d(const mel_barcode_matrix* m, i32 scale, i32 quiet, Mel_Image* out, const Mel_Alloc* a)
{
    i32 dim = m->width;
    i32 px = (dim + 2 * quiet) * scale;
    if (!mel_image_init(out, &mel_image_gray8, px, px, a))
        return false;

    Mel_Image_Plane p = mel_image_plane(out, 0);
    for (i32 y = 0; y < px; ++y)
    {
        u8* row = p.pixels + (usize)y * (usize)p.stride;
        for (i32 x = 0; x < px; ++x)
        {
            i32  mx = x / scale - quiet;
            i32  my = y / scale - quiet;
            bool dark = (mx >= 0 && mx < dim && my >= 0 && my < dim) ? mel_barcode_matrix_get(m, mx, my) : false;
            row[x] = dark ? 0 : 255;
        }
    }
    return true;
}

static void mel__qr_roundtrip(const char* data, mel_qr_opt opt, i32 scale)
{
    const Mel_Alloc* a = mel_alloc_heap();

    mel_barcode_matrix m;
    MEL_REQUIRE(mel_qr_encode(&m, data, opt, a));

    Mel_Image img;
    MEL_REQUIRE(mel__rasterize_2d(&m, scale, 4, &img, a));

    mel_image_gray gray = mel_image_gray_borrow(&img);
    MEL_REQUIRE(gray.pixels != NULL);

    mel_barcode_decode_result r;
    MEL_REQUIRE(mel_barcode_decode(&gray, &r, a));
    MEL_REQUIRE(r.found);
    MEL_REQUIRE_NOT_NULL(r.text);
    MEL_REQUIRE_STR_EQ(r.symbology, "QR");
    MEL_REQUIRE_STR_EQ(r.text, data);
    MEL_REQUIRE_EQ(r.text_len, (i32)strlen(data));

    mel_barcode_decode_result_free(&r, a);
    mel_image_free(&img);
    mel_barcode_matrix_free(&m);
}

MEL_TEST(barcode_decode, qr_numeric_through_unified) { mel__qr_roundtrip("01234567", (mel_qr_opt){ .ecc = mel_qr_ecc_m(), .version = 1, .mask = -1 }, 8); }

MEL_TEST(barcode_decode, qr_alnum_through_unified) { mel__qr_roundtrip("HELLO WORLD", (mel_qr_opt){ .ecc = mel_qr_ecc_q(), .version = 1, .mask = -1 }, 6); }

MEL_TEST(barcode_decode, qr_byte_through_unified) { mel__qr_roundtrip("Melody scanner", (mel_qr_opt){ .ecc = mel_qr_ecc_m(), .version = 0, .mask = -1 }, 6); }
