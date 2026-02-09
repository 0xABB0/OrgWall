#pragma once

#include "types.h"
#include "anim.sprite.fwd.h"
#include "anim.timeline.h"
#include "texture.atlas.fwd.h"
#include "texture.pool.fwd.h"

struct Mel_Anim_Sprite {
    Mel_Atlas_Handle atlas;
    Mel_Anim_Playback playback;
};

void              mel_anim_sprite_init(Mel_Anim_Sprite* anim, Mel_Atlas_Handle atlas_handle, Mel_Anim_Timeline* timeline);
void              mel_anim_sprite_update(Mel_Anim_Sprite* anim, f32 dt);
Mel_Atlas_Region* mel_anim_sprite_region(Mel_Anim_Sprite* anim, Mel_Atlas_Pool* atlas_pool);
void              mel_anim_sprite_get_uv(Mel_Anim_Sprite* anim, Mel_Atlas_Pool* atlas_pool,
                                         f32* u0, f32* v0, f32* u1, f32* v1);
Mel_Texture_Handle mel_anim_sprite_texture(Mel_Anim_Sprite* anim, Mel_Atlas_Pool* atlas_pool);
