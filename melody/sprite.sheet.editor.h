#pragma once

#include "core.types.h"
#include "allocator.fwd.h"
#include "sprite.sheet.fwd.h"
#include "texture.pool.fwd.h"

typedef struct
{
    const Mel_Alloc* alloc;
    Mel_Texture_Pool* texture_pool;
    Mel_Spritesheet* spritesheet;
    i32 selected_frame;
    i32 selected_animation;
    i32 selected_anim_frame;

    char name_buffer[256];
    char texture_path_buffer[256];
    char anim_name_buffer[64];

    char event_sound_buffer[256];
    char event_tag_buffer[64];

    f32 preview_time;
    u32 preview_frame_idx;
    bool preview_playing;
    f32 zoom;
    bool show_grid;
    bool dirty;

    char save_path_buffer[256];

    bool dragging_frame;
    i32 drag_start_x;
    i32 drag_start_y;
    i32 drag_end_x;
    i32 drag_end_y;
} Mel_EdSpritesheet;

void mel_ed_spritesheet_init(Mel_EdSpritesheet* ed, const Mel_Alloc* alloc);
void mel_ed_spritesheet_shutdown(Mel_EdSpritesheet* ed);

void mel_ed_spritesheet_set(Mel_EdSpritesheet* ed, Mel_Spritesheet* sheet);
void mel_ed_spritesheet_draw(Mel_EdSpritesheet* ed, f32 dt);
