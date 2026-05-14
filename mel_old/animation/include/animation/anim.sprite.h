#pragma once

#include <core/types.h>
#include "anim.sprite.fwd.h"
#include "anim.clip.fwd.h"
#include <allocator/fwd.h>

struct Mel_Sprite_Anim_Frame {
    u32 frame_index;
    f32 duration;
};

struct Mel_Sprite_Anim_Event {
    u32 after_frame;
    u64 event_hash;
    u64 event_property;
};

struct Mel_Sprite_Anim_Def {
    u64 name_hash;
    u64 property_hash;
    bool is_looping;

    Mel_Sprite_Anim_Frame* frames;
    u32 frame_count;
    u32 frame_capacity;

    Mel_Sprite_Anim_Event* events;
    u32 event_count;
    u32 event_capacity;

    const Mel_Alloc* alloc;
};

void mel_sprite_anim_def_init(Mel_Sprite_Anim_Def* def, u64 name_hash, u64 property_hash, const Mel_Alloc* alloc);
void mel_sprite_anim_def_destroy(Mel_Sprite_Anim_Def* def);

void mel_sprite_anim_def_push_frame(Mel_Sprite_Anim_Def* def, u32 frame_index, f32 duration);
void mel_sprite_anim_def_insert_frame(Mel_Sprite_Anim_Def* def, u32 position, u32 frame_index, f32 duration);
void mel_sprite_anim_def_remove_frame(Mel_Sprite_Anim_Def* def, u32 position);
void mel_sprite_anim_def_set_frame(Mel_Sprite_Anim_Def* def, u32 position, u32 frame_index, f32 duration);

void mel_sprite_anim_def_add_event(Mel_Sprite_Anim_Def* def, u32 after_frame, u64 event_hash, u64 event_property);
void mel_sprite_anim_def_remove_event(Mel_Sprite_Anim_Def* def, u32 index);

Mel_Anim_Clip mel_sprite_anim_def_compile(const Mel_Sprite_Anim_Def* def, const Mel_Alloc* alloc);
