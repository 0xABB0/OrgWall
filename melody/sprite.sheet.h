#ifndef MEL_ASSETS_SPRITESHEET_H
#define MEL_ASSETS_SPRITESHEET_H

#include "core.types.h"
#include "allocator.fwd.h"
#include "gpu.texture.fwd.h"
#include "string.str8.fwd.h"

typedef struct Mel_SpriteFrame
{
    u32 x;
    u32 y;
    u32 width;
    u32 height;
    i32 offset_x;
    i32 offset_y;
} Mel_SpriteFrame;

#define MEL_FRAME_EVENT_SOUND   (1 << 0)
#define MEL_FRAME_EVENT_HITBOX  (1 << 1)
#define MEL_FRAME_EVENT_TAG     (1 << 2)

typedef struct Mel_FrameEvent
{
    u32 flags;

    str8 sound_path;
    f32 sound_volume;

    i32 hitbox_x;
    i32 hitbox_y;
    u32 hitbox_width;
    u32 hitbox_height;

    str8* tags;
    u32 tag_count;
} Mel_FrameEvent;

typedef struct Mel_Animation
{
    str8 name;
    u32* frame_indices;
    f32* frame_durations;
    Mel_FrameEvent* frame_events;
    u32 frame_count;
    f32 default_duration;
    bool loop;
} Mel_Animation;

typedef struct Mel_Spritesheet
{
    const Mel_Alloc* alloc;
    str8 name;
    str8 texture_path;
    Mel_SpriteFrame* frames;
    u32 frame_count;
    Mel_Animation* animations;
    u32 animation_count;
    Mel_Gpu_Texture* texture;
    u32 texture_width;
    u32 texture_height;
} Mel_Spritesheet;

typedef struct Mel_AnimationPlayer
{
    Mel_Spritesheet* sheet;
    Mel_Animation* current;
    u32 current_frame;
    f32 timer;
    bool playing;
    bool finished;
} Mel_AnimationPlayer;

typedef struct Mel_Assets Mel_Assets;

bool mel_spritesheet_load(Mel_Spritesheet* sheet, const Mel_Alloc* alloc, Mel_Assets* assets, str8 path);
bool mel_spritesheet_save(Mel_Spritesheet* sheet, str8 path);
void mel_spritesheet_free(Mel_Spritesheet* sheet);

Mel_Animation* mel_spritesheet_find_animation(Mel_Spritesheet* sheet, str8 name);
Mel_SpriteFrame* mel_spritesheet_get_frame(Mel_Spritesheet* sheet, u32 index);
void mel_spritesheet_get_frame_uv(Mel_Spritesheet* sheet, u32 index, f32* u0, f32* v0, f32* u1, f32* v1);

void mel_animation_player_init(Mel_AnimationPlayer* player, Mel_Spritesheet* sheet);
void mel_animation_player_play(Mel_AnimationPlayer* player, str8 name);
void mel_animation_player_update(Mel_AnimationPlayer* player, f32 dt);
Mel_SpriteFrame* mel_animation_player_current_frame(Mel_AnimationPlayer* player);
Mel_FrameEvent* mel_animation_player_current_event(Mel_AnimationPlayer* player);

f32 mel_animation_get_frame_duration(Mel_Animation* anim, u32 frame_idx);

void mel_frame_event_free(Mel_FrameEvent* event, const Mel_Alloc* alloc);
void mel_frame_event_add_tag(Mel_FrameEvent* event, const Mel_Alloc* alloc, str8 tag);
bool mel_frame_event_has_tag(Mel_FrameEvent* event, str8 tag);

Mel_Animation* mel_spritesheet_add_animation(Mel_Spritesheet* sheet, str8 name);
void mel_animation_add_frame(Mel_Animation* anim, const Mel_Alloc* alloc, u32 frame_index, f32 duration);
void mel_animation_set_frame_event(Mel_Animation* anim, const Mel_Alloc* alloc, u32 anim_frame_idx, Mel_FrameEvent event);

#endif
