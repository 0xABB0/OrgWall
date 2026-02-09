#include "anim.sprite.h"
#include "texture.atlas.h"

void mel_anim_sprite_init(Mel_Anim_Sprite* anim, Mel_Atlas_Handle atlas_handle, Mel_Anim_Timeline* timeline)
{
    assert(anim != nullptr);
    assert(timeline != nullptr);

    anim->atlas = atlas_handle;
    mel_anim_playback_start(&anim->playback, timeline);
}

void mel_anim_sprite_update(Mel_Anim_Sprite* anim, f32 dt)
{
    assert(anim != nullptr);
    mel_anim_playback_update(&anim->playback, dt);
}

Mel_Atlas_Region* mel_anim_sprite_region(Mel_Anim_Sprite* anim, Mel_Atlas_Pool* atlas_pool)
{
    assert(anim != nullptr);
    assert(atlas_pool != nullptr);

    Mel_Atlas_Entry* entry = mel_atlas_pool_get(atlas_pool, anim->atlas);
    if (!entry)
        return nullptr;

    u32 frame_idx = mel_anim_playback_frame_index(&anim->playback);
    if (frame_idx >= entry->region_count)
        return nullptr;

    return &entry->regions[frame_idx];
}

void mel_anim_sprite_get_uv(Mel_Anim_Sprite* anim, Mel_Atlas_Pool* atlas_pool,
                            f32* u0, f32* v0, f32* u1, f32* v1)
{
    assert(anim != nullptr);
    assert(atlas_pool != nullptr);

    Mel_Atlas_Entry* entry = mel_atlas_pool_get(atlas_pool, anim->atlas);
    if (!entry)
    {
        *u0 = *v0 = 0.0f;
        *u1 = *v1 = 1.0f;
        return;
    }

    u32 frame_idx = mel_anim_playback_frame_index(&anim->playback);
    if (frame_idx >= entry->region_count)
    {
        *u0 = *v0 = 0.0f;
        *u1 = *v1 = 1.0f;
        return;
    }

    mel_atlas_get_region_uv(entry, frame_idx, u0, v0, u1, v1);
}

Mel_Texture_Handle mel_anim_sprite_texture(Mel_Anim_Sprite* anim, Mel_Atlas_Pool* atlas_pool)
{
    assert(anim != nullptr);
    assert(atlas_pool != nullptr);

    Mel_Atlas_Entry* entry = mel_atlas_pool_get(atlas_pool, anim->atlas);
    if (!entry)
        return MEL_TEXTURE_HANDLE_NULL;

    return entry->texture;
}
