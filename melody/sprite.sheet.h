#pragma once

#include "core.types.h"
#include "sprite.sheet.fwd.h"
#include "math.geo.rect.h"
#include "allocator.fwd.h"

struct Mel_Sprite_Sheet {
    Mel_Rect* frames;
    u32 frame_count;
    u32 frame_capacity;
    const Mel_Alloc* alloc;
};

void mel_sprite_sheet_init(Mel_Sprite_Sheet* sheet, const Mel_Alloc* alloc);
void mel_sprite_sheet_destroy(Mel_Sprite_Sheet* sheet);

void mel_sprite_sheet_push_frame(Mel_Sprite_Sheet* sheet, Mel_Rect uv);

typedef struct {
    u32 cols;
    u32 rows;
    u32 frame_count;
} Mel_Sprite_Sheet_Grid_Opt;

void mel_sprite_sheet_from_grid_opt(Mel_Sprite_Sheet* sheet, Mel_Sprite_Sheet_Grid_Opt opt);
#define mel_sprite_sheet_from_grid(sheet, ...) \
    mel_sprite_sheet_from_grid_opt((sheet), (Mel_Sprite_Sheet_Grid_Opt){__VA_ARGS__})

Mel_Rect mel_sprite_sheet_frame(const Mel_Sprite_Sheet* sheet, u32 frame_index);
