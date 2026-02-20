#pragma once

#include "core.types.h"
#include "anim.sprite.fwd.h"
#include "anim.mixer.h"
#include "anim.clip.fwd.h"
#include "allocator.fwd.h"

#define MEL_ANIM_PROP_SPRITE_FRAME 0x1ULL

struct Mel_Anim_Sprite {
    Mel_Anim_Mixer mixer;
};

void mel_anim_sprite_init(Mel_Anim_Sprite* sprite, const Mel_Alloc* alloc);
void mel_anim_sprite_destroy(Mel_Anim_Sprite* sprite);
void mel_anim_sprite_play(Mel_Anim_Sprite* sprite, Mel_Anim_Clip* clip,
                          f32 mix_duration);
void mel_anim_sprite_update(Mel_Anim_Sprite* sprite, f32 dt);
u32  mel_anim_sprite_frame_index(const Mel_Anim_Sprite* sprite);

Mel_Anim_Clip mel_anim_sprite_clip(const Mel_Alloc* alloc,
                                    u64 name_hash,
                                    const u32* frame_indices,
                                    const f32* frame_durations,
                                    u32 frame_count, bool loop);
