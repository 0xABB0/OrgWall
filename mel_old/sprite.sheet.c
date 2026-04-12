#include "sprite.sheet.h"
#include "allocator.h"

#include <assert.h>

static void mel__sheet_grow(Mel_Sprite_Sheet* sheet)
{
    u32 new_cap = sheet->frame_capacity == 0 ? 8 : sheet->frame_capacity * 2;
    usize new_size = sizeof(Mel_Rect) * new_cap;
    if (sheet->frames == NULL)
        sheet->frames = mel_alloc(sheet->alloc, new_size);
    else
        sheet->frames = mel_realloc(sheet->alloc, sheet->frames, new_size);
    sheet->frame_capacity = new_cap;
}

void mel_sprite_sheet_init(Mel_Sprite_Sheet* sheet, const Mel_Alloc* alloc)
{
    assert(sheet != NULL);
    assert(alloc != NULL);

    *sheet = (Mel_Sprite_Sheet){0};
    sheet->alloc = alloc;
}

void mel_sprite_sheet_destroy(Mel_Sprite_Sheet* sheet)
{
    assert(sheet != NULL);

    if (sheet->frames)
        mel_dealloc(sheet->alloc, sheet->frames);

    *sheet = (Mel_Sprite_Sheet){0};
}

void mel_sprite_sheet_push_frame(Mel_Sprite_Sheet* sheet, Mel_Rect uv)
{
    assert(sheet != NULL);

    if (sheet->frame_count >= sheet->frame_capacity)
        mel__sheet_grow(sheet);

    sheet->frames[sheet->frame_count++] = uv;
}

void mel_sprite_sheet_from_grid_opt(Mel_Sprite_Sheet* sheet, Mel_Sprite_Sheet_Grid_Opt opt)
{
    assert(sheet != NULL);
    assert(opt.cols > 0);
    assert(opt.rows > 0);

    u32 total = opt.frame_count > 0 ? opt.frame_count : opt.cols * opt.rows;
    f32 fw = 1.0f / (f32)opt.cols;
    f32 fh = 1.0f / (f32)opt.rows;

    u32 i = 0;
    for (u32 row = 0; row < opt.rows && i < total; row++)
    {
        for (u32 col = 0; col < opt.cols && i < total; col++, i++)
        {
            mel_sprite_sheet_push_frame(sheet,
                mel_rect((f32)col * fw, (f32)row * fh, fw, fh));
        }
    }
}

Mel_Rect mel_sprite_sheet_frame(const Mel_Sprite_Sheet* sheet, u32 frame_index)
{
    assert(sheet != NULL);
    assert(frame_index < sheet->frame_count);

    return sheet->frames[frame_index];
}
