#include <stdio.h>

#include <core/platform.h>
#include <app/app.h>
#include <gui/gui.h>

#include <allocator/heap.h>
#include <barcode/barcode.h>

typedef bool (*Encode_Fn)(mel_barcode_matrix* out, const char* text, const Mel_Alloc* allocator);

static bool enc_qr(mel_barcode_matrix* m, const char* t, const Mel_Alloc* a) { return mel_qr_encode(m, t, (mel_qr_opt){ .ecc = mel_qr_ecc_m(), .version = 0, .mask = -1 }, a); }
static bool enc_code128(mel_barcode_matrix* m, const char* t, const Mel_Alloc* a) { return mel_code128_encode(m, t, 40, a); }
static bool enc_code39(mel_barcode_matrix* m, const char* t, const Mel_Alloc* a) { return mel_code39_encode(m, t, 40, (mel_code39_opt){ 0 }, a); }
static bool enc_ean13(mel_barcode_matrix* m, const char* t, const Mel_Alloc* a) { return mel_ean13_encode(m, t, 40, a); }
static bool enc_ean8(mel_barcode_matrix* m, const char* t, const Mel_Alloc* a) { return mel_ean8_encode(m, t, 40, a); }
static bool enc_upca(mel_barcode_matrix* m, const char* t, const Mel_Alloc* a) { return mel_upca_encode(m, t, 40, a); }
static bool enc_itf(mel_barcode_matrix* m, const char* t, const Mel_Alloc* a) { return mel_itf_encode(m, t, 40, (mel_itf_opt){ .pad_odd = true }, a); }

typedef struct
{
    const char* name;
    const char* hint;
    Encode_Fn   fn;
} Symbology;

static const Symbology SYMS[] = {
    { "QR", "any text", enc_qr },
    { "Code 128", "any ASCII", enc_code128 },
    { "Code 39", "A-Z 0-9 - . $ / + % space", enc_code39 },
    { "EAN-13", "12 or 13 digits", enc_ean13 },
    { "EAN-8", "7 or 8 digits", enc_ean8 },
    { "UPC-A", "11 or 12 digits", enc_upca },
    { "ITF", "digits (even)", enc_itf },
};
#define SYM_COUNT ((i32)(sizeof(SYMS) / sizeof(SYMS[0])))

typedef struct
{
    Mel_Gui_Handle     edit;
    Mel_Gui_Handle     status;
    Mel_Gui_Handle     canvas;
    mel_barcode_matrix matrix;
    bool               has_matrix;
} App_State;

static App_State g_app;

static void on_encode(Mel_Gui_Handle h, void* user)
{
    (void)h;
    const Symbology* s = user;

    char text[512];
    text[0] = 0;
    mel_gui_get_text(g_app.edit, text, sizeof text);
    text[sizeof text - 1] = 0;

    if (g_app.has_matrix)
    {
        mel_barcode_matrix_free(&g_app.matrix);
        g_app.has_matrix = false;
    }

    mel_barcode_matrix m;
    char               status[256];
    if (s->fn(&m, text, mel_alloc_heap()))
    {
        g_app.matrix = m;
        g_app.has_matrix = true;
        snprintf(status, sizeof status, "%s  —  %d x %d modules", s->name, m.width, m.height);
    }
    else
    {
        snprintf(status, sizeof status, "%s cannot encode this input  (needs %s)", s->name, s->hint);
    }
    mel_gui_set_text(g_app.status, str8_from_cstr(status));
    mel_gui_invalidate(g_app.canvas);
}

static void on_paint(Mel_Gui_Handle h, Mel_Painter* p, i32 w, i32 ht, void* user)
{
    (void)h;
    (void)user;
    mel_painter_clear(p, mel_color8_rgb(255, 255, 255));

    if (!g_app.has_matrix)
    {
        mel_painter_draw_text(p, S8("Type text, then pick an encoding above."), mel_vec2(16, 16), mel_color8_rgb(140, 140, 140), 14.0f);
        return;
    }

    mel_barcode_matrix* m = &g_app.matrix;
    i32                 qz = m->quiet_zone > 0 ? m->quiet_zone : (m->height == 1 ? 10 : 4);
    f32                 total_w = (f32)(m->width + 2 * qz);
    f32                 total_h = (f32)(m->height + 2 * qz);

    f32 pad = 20.0f;
    f32 cell = ((f32)w - 2 * pad) / total_w;
    f32 cell_h = ((f32)ht - 2 * pad) / total_h;
    if (cell_h < cell)
    {
        cell = cell_h;
    }
    if (cell < 1.0f)
    {
        cell = 1.0f;
    }

    f32 origin_x = ((f32)w - cell * total_w) / 2.0f;
    f32 origin_y = ((f32)ht - cell * total_h) / 2.0f;

    mel_color8 dark = mel_color8_rgb(0, 0, 0);
    for (i32 r = 0; r < m->height; ++r)
    {
        for (i32 c = 0; c < m->width; ++c)
        {
            if (mel_barcode_matrix_get(m, c, r))
            {
                f32 x = origin_x + (f32)(c + qz) * cell;
                f32 y = origin_y + (f32)(r + qz) * cell;
                mel_painter_fill_rect(p, mel_rect(x, y, cell + 0.5f, cell + 0.5f), dark);
            }
        }
    }
}

static void on_destroy(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    if (g_app.has_matrix)
    {
        mel_barcode_matrix_free(&g_app.matrix);
        g_app.has_matrix = false;
    }
}

static void build_main(Mel_Gui_Handle frame, void* user)
{
    (void)user;
    mel_gui_set_layout(frame, mel_column_layout(.spacing = 8, .margin = 16, .cross_align = MEL_ALIGN_STRETCH));

    mel_label_create(frame, .text = S8("Barcode Studio"), .layoutable = { .preferred_h = 28 });

    g_app.edit = mel_textfield_create(frame, .text = S8("HELLO WORLD"), .layoutable = { .preferred_h = 36 });

    for (i32 i = 0; i < SYM_COUNT; ++i)
    {
        mel_button_create(frame, .text = str8_from_cstr(SYMS[i].name), .pointer.on_click = on_encode, .user = (void*)&SYMS[i], .layoutable = { .preferred_h = 32 });
    }

    g_app.status = mel_label_create(frame, .text = S8("Pick an encoding to render the input below."), .layoutable = { .preferred_h = 24 });

    g_app.canvas = mel_canvas_create(frame, .on_.on_paint = on_paint, .lifecycle.on_destroy = on_destroy, .layoutable = { .preferred_h = 320, .weight = 1 });
}

void mel_app_setup(Mel_Reactor* reactor)
{
    mel_gui_init(reactor);
    mel_app_register_screen(S8("main"), build_main, NULL);
    mel_app_present(S8("main"), NULL);
}
