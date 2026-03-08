#pragma once

#include "core.types.h"
#include "allocator.fwd.h"
#include "texture.pool.fwd.h"
#include "sprite.sheet.fwd.h"

typedef struct
{
    const Mel_Alloc* alloc;
    Mel_Texture_Pool* texture_pool;
    Mel_Sprite_Sheet* spritesheet;
} Mel_EdSpritesheet;

void mel_ed_spritesheet_init(Mel_EdSpritesheet* ed, const Mel_Alloc* alloc);
void mel_ed_spritesheet_shutdown(Mel_EdSpritesheet* ed);
void mel_ed_spritesheet_draw(Mel_EdSpritesheet* ed, f32 dt);
