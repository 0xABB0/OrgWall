#include "sprite.sheet.h"
#include "gpu.texture.h"
#include "string.str8.h"
#include "vfs.h"
#include "allocator.h"
#include <cjson/cJSON.h>
#include <SDL3/SDL.h>
#include <string.h>

bool mel_spritesheet_load(Mel_Spritesheet* sheet, const Mel_Alloc* alloc, Mel_Vfs* vfs, str8 path)
{
    assert(sheet != nullptr);
    assert(alloc != nullptr);
    assert(vfs != nullptr);
    assert(!str8_is_empty(path));

    str8 text = mel_vfs_read_text_alloc(vfs, path, alloc);
    if (!text.data)
    {
        SDL_Log("Failed to read spritesheet: %.*s", (int)path.len, path.data);
        return false;
    }

    cJSON* root = cJSON_Parse((char*)text.data);
    mel_dealloc(alloc, text.data);

    if (!root)
    {
        SDL_Log("Failed to parse spritesheet JSON: %.*s", (int)path.len, path.data);
        return false;
    }

    *sheet = (Mel_Spritesheet){0};
    sheet->alloc = alloc;

    cJSON* name = cJSON_GetObjectItem(root, "name");
    if (name && cJSON_IsString(name))
    {
        sheet->name = str8_dup(str8_from_cstr(name->valuestring), alloc);
    }

    cJSON* texture = cJSON_GetObjectItem(root, "texture");
    if (texture && cJSON_IsString(texture))
    {
        sheet->texture_path = str8_dup(str8_from_cstr(texture->valuestring), alloc);
    }

    cJSON* tex_width = cJSON_GetObjectItem(root, "texture_width");
    if (tex_width && cJSON_IsNumber(tex_width))
    {
        sheet->texture_width = (u32)tex_width->valueint;
    }

    cJSON* tex_height = cJSON_GetObjectItem(root, "texture_height");
    if (tex_height && cJSON_IsNumber(tex_height))
    {
        sheet->texture_height = (u32)tex_height->valueint;
    }

    cJSON* frames = cJSON_GetObjectItem(root, "frames");
    if (frames && cJSON_IsArray(frames))
    {
        sheet->frame_count = (u32)cJSON_GetArraySize(frames);
        sheet->frames = (Mel_SpriteFrame*)mel_calloc(alloc, sheet->frame_count * sizeof(Mel_SpriteFrame));

        u32 i = 0;
        cJSON* frame;
        cJSON_ArrayForEach(frame, frames)
        {
            cJSON* x = cJSON_GetObjectItem(frame, "x");
            cJSON* y = cJSON_GetObjectItem(frame, "y");
            cJSON* w = cJSON_GetObjectItem(frame, "width");
            cJSON* h = cJSON_GetObjectItem(frame, "height");
            cJSON* ox = cJSON_GetObjectItem(frame, "offset_x");
            cJSON* oy = cJSON_GetObjectItem(frame, "offset_y");

            if (x && cJSON_IsNumber(x)) sheet->frames[i].x = (u32)x->valueint;
            if (y && cJSON_IsNumber(y)) sheet->frames[i].y = (u32)y->valueint;
            if (w && cJSON_IsNumber(w)) sheet->frames[i].width = (u32)w->valueint;
            if (h && cJSON_IsNumber(h)) sheet->frames[i].height = (u32)h->valueint;
            if (ox && cJSON_IsNumber(ox)) sheet->frames[i].offset_x = ox->valueint;
            if (oy && cJSON_IsNumber(oy)) sheet->frames[i].offset_y = oy->valueint;

            i++;
        }
    }

    cJSON* animations = cJSON_GetObjectItem(root, "animations");
    if (animations && cJSON_IsArray(animations))
    {
        sheet->animation_count = (u32)cJSON_GetArraySize(animations);
        sheet->animations = (Mel_Animation*)mel_calloc(alloc, sheet->animation_count * sizeof(Mel_Animation));

        u32 i = 0;
        cJSON* anim;
        cJSON_ArrayForEach(anim, animations)
        {
            cJSON* anim_name = cJSON_GetObjectItem(anim, "name");
            if (anim_name && cJSON_IsString(anim_name))
            {
                sheet->animations[i].name = str8_dup(str8_from_cstr(anim_name->valuestring), alloc);
            }

            cJSON* duration = cJSON_GetObjectItem(anim, "default_duration");
            if (duration && cJSON_IsNumber(duration))
            {
                sheet->animations[i].default_duration = (f32)duration->valuedouble;
            }
            else
            {
                sheet->animations[i].default_duration = 0.1f;
            }

            cJSON* loop = cJSON_GetObjectItem(anim, "loop");
            sheet->animations[i].loop = !loop || cJSON_IsTrue(loop);

            cJSON* frame_indices = cJSON_GetObjectItem(anim, "frames");
            if (frame_indices && cJSON_IsArray(frame_indices))
            {
                sheet->animations[i].frame_count = (u32)cJSON_GetArraySize(frame_indices);
                sheet->animations[i].frame_indices = (u32*)mel_calloc(
                    alloc, sheet->animations[i].frame_count * sizeof(u32));

                u32 j = 0;
                cJSON* idx;
                cJSON_ArrayForEach(idx, frame_indices)
                {
                    if (cJSON_IsNumber(idx))
                    {
                        sheet->animations[i].frame_indices[j] = (u32)idx->valueint;
                    }
                    j++;
                }
            }

            cJSON* durations = cJSON_GetObjectItem(anim, "frame_durations");
            if (durations && cJSON_IsArray(durations))
            {
                sheet->animations[i].frame_durations = (f32*)mel_calloc(
                    alloc, sheet->animations[i].frame_count * sizeof(f32));

                u32 j = 0;
                cJSON* dur;
                cJSON_ArrayForEach(dur, durations)
                {
                    if (cJSON_IsNumber(dur) && j < sheet->animations[i].frame_count)
                    {
                        sheet->animations[i].frame_durations[j] = (f32)dur->valuedouble;
                    }
                    j++;
                }
            }

            cJSON* events = cJSON_GetObjectItem(anim, "frame_events");
            if (events && cJSON_IsArray(events))
            {
                sheet->animations[i].frame_events = (Mel_FrameEvent*)mel_calloc(
                    alloc, sheet->animations[i].frame_count * sizeof(Mel_FrameEvent));

                u32 j = 0;
                cJSON* ev;
                cJSON_ArrayForEach(ev, events)
                {
                    if (j >= sheet->animations[i].frame_count) break;
                    if (!cJSON_IsObject(ev)) { j++; continue; }

                    Mel_FrameEvent* event = &sheet->animations[i].frame_events[j];

                    cJSON* sound = cJSON_GetObjectItem(ev, "sound");
                    if (sound && cJSON_IsString(sound))
                    {
                        event->sound_path = str8_dup(str8_from_cstr(sound->valuestring), alloc);
                        event->flags |= MEL_FRAME_EVENT_SOUND;
                    }

                    cJSON* vol = cJSON_GetObjectItem(ev, "volume");
                    if (vol && cJSON_IsNumber(vol))
                    {
                        event->sound_volume = (f32)vol->valuedouble;
                    }
                    else
                    {
                        event->sound_volume = 1.0f;
                    }

                    cJSON* hx = cJSON_GetObjectItem(ev, "hitbox_x");
                    cJSON* hy = cJSON_GetObjectItem(ev, "hitbox_y");
                    cJSON* hw = cJSON_GetObjectItem(ev, "hitbox_width");
                    cJSON* hh = cJSON_GetObjectItem(ev, "hitbox_height");
                    if (hw && hh)
                    {
                        event->hitbox_x = hx ? hx->valueint : 0;
                        event->hitbox_y = hy ? hy->valueint : 0;
                        event->hitbox_width = (u32)hw->valueint;
                        event->hitbox_height = (u32)hh->valueint;
                        event->flags |= MEL_FRAME_EVENT_HITBOX;
                    }

                    cJSON* tags = cJSON_GetObjectItem(ev, "tags");
                    if (tags && cJSON_IsArray(tags))
                    {
                        event->tag_count = (u32)cJSON_GetArraySize(tags);
                        event->tags = (str8*)mel_calloc(alloc, event->tag_count * sizeof(str8));
                        u32 t = 0;
                        cJSON* tag;
                        cJSON_ArrayForEach(tag, tags)
                        {
                            if (cJSON_IsString(tag))
                            {
                                event->tags[t] = str8_dup(str8_from_cstr(tag->valuestring), alloc);
                            }
                            t++;
                        }
                        if (event->tag_count > 0)
                        {
                            event->flags |= MEL_FRAME_EVENT_TAG;
                        }
                    }

                    j++;
                }
            }

            i++;
        }
    }

    cJSON_Delete(root);

    SDL_Log("Loaded spritesheet: %.*s (%u frames, %u animations)",
            !str8_is_empty(sheet->name) ? (int)sheet->name.len : (int)path.len,
            !str8_is_empty(sheet->name) ? (char*)sheet->name.data : (char*)path.data,
            sheet->frame_count, sheet->animation_count);

    return true;
}

bool mel_spritesheet_save(Mel_Spritesheet* sheet, Mel_Vfs* vfs, str8 path)
{
    assert(sheet != nullptr);
    assert(vfs != nullptr);
    assert(!str8_is_empty(path));

    cJSON* root = cJSON_CreateObject();

    if (!str8_is_empty(sheet->name))
    {
        char name_buf[256];
        str8_to_buf(sheet->name, name_buf, sizeof(name_buf));
        cJSON_AddStringToObject(root, "name", name_buf);
    }
    if (!str8_is_empty(sheet->texture_path))
    {
        char tex_buf[512];
        str8_to_buf(sheet->texture_path, tex_buf, sizeof(tex_buf));
        cJSON_AddStringToObject(root, "texture", tex_buf);
    }

    cJSON_AddNumberToObject(root, "texture_width", sheet->texture_width);
    cJSON_AddNumberToObject(root, "texture_height", sheet->texture_height);

    if (sheet->frame_count > 0)
    {
        cJSON* frames = cJSON_AddArrayToObject(root, "frames");
        for (u32 i = 0; i < sheet->frame_count; i++)
        {
            Mel_SpriteFrame* f = &sheet->frames[i];
            cJSON* frame = cJSON_CreateObject();
            cJSON_AddNumberToObject(frame, "x", f->x);
            cJSON_AddNumberToObject(frame, "y", f->y);
            cJSON_AddNumberToObject(frame, "width", f->width);
            cJSON_AddNumberToObject(frame, "height", f->height);
            if (f->offset_x != 0) cJSON_AddNumberToObject(frame, "offset_x", f->offset_x);
            if (f->offset_y != 0) cJSON_AddNumberToObject(frame, "offset_y", f->offset_y);
            cJSON_AddItemToArray(frames, frame);
        }
    }

    if (sheet->animation_count > 0)
    {
        cJSON* animations = cJSON_AddArrayToObject(root, "animations");
        for (u32 i = 0; i < sheet->animation_count; i++)
        {
            Mel_Animation* a = &sheet->animations[i];
            cJSON* anim = cJSON_CreateObject();

            if (!str8_is_empty(a->name))
            {
                char anim_name_buf[256];
                str8_to_buf(a->name, anim_name_buf, sizeof(anim_name_buf));
                cJSON_AddStringToObject(anim, "name", anim_name_buf);
            }
            cJSON_AddNumberToObject(anim, "default_duration", a->default_duration);
            cJSON_AddBoolToObject(anim, "loop", a->loop);

            if (a->frame_count > 0)
            {
                cJSON* indices = cJSON_AddArrayToObject(anim, "frames");
                for (u32 j = 0; j < a->frame_count; j++)
                {
                    cJSON_AddItemToArray(indices, cJSON_CreateNumber(a->frame_indices[j]));
                }

                if (a->frame_durations)
                {
                    cJSON* durations = cJSON_AddArrayToObject(anim, "frame_durations");
                    for (u32 j = 0; j < a->frame_count; j++)
                    {
                        cJSON_AddItemToArray(durations, cJSON_CreateNumber(a->frame_durations[j]));
                    }
                }

                if (a->frame_events)
                {
                    cJSON* events = cJSON_AddArrayToObject(anim, "frame_events");
                    for (u32 j = 0; j < a->frame_count; j++)
                    {
                        Mel_FrameEvent* ev = &a->frame_events[j];
                        cJSON* event_obj = cJSON_CreateObject();

                        if (ev->flags & MEL_FRAME_EVENT_SOUND)
                        {
                            if (!str8_is_empty(ev->sound_path))
                            {
                                char sound_buf[512];
                                str8_to_buf(ev->sound_path, sound_buf, sizeof(sound_buf));
                                cJSON_AddStringToObject(event_obj, "sound", sound_buf);
                            }
                            cJSON_AddNumberToObject(event_obj, "volume", ev->sound_volume);
                        }

                        if (ev->flags & MEL_FRAME_EVENT_HITBOX)
                        {
                            cJSON_AddNumberToObject(event_obj, "hitbox_x", ev->hitbox_x);
                            cJSON_AddNumberToObject(event_obj, "hitbox_y", ev->hitbox_y);
                            cJSON_AddNumberToObject(event_obj, "hitbox_width", ev->hitbox_width);
                            cJSON_AddNumberToObject(event_obj, "hitbox_height", ev->hitbox_height);
                        }

                        if ((ev->flags & MEL_FRAME_EVENT_TAG) && ev->tag_count > 0)
                        {
                            cJSON* tags = cJSON_AddArrayToObject(event_obj, "tags");
                            for (u32 t = 0; t < ev->tag_count; t++)
                            {
                                if (!str8_is_empty(ev->tags[t]))
                                {
                                    char tag_buf[256];
                                    str8_to_buf(ev->tags[t], tag_buf, sizeof(tag_buf));
                                    cJSON_AddItemToArray(tags, cJSON_CreateString(tag_buf));
                                }
                            }
                        }

                        cJSON_AddItemToArray(events, event_obj);
                    }
                }
            }

            cJSON_AddItemToArray(animations, anim);
        }
    }

    char* json_text = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_text)
    {
        SDL_Log("Failed to serialize spritesheet JSON");
        return false;
    }

    bool result = mel_vfs_write_text(vfs, path, str8_from_cstr(json_text));
    free(json_text);

    if (result)
    {
        SDL_Log("Saved spritesheet: %.*s", (int)path.len, path.data);
    }

    return result;
}

void mel_spritesheet_free(Mel_Spritesheet* sheet)
{
    assert(sheet != nullptr);

    const Mel_Alloc* alloc = sheet->alloc;

    if (!str8_is_empty(sheet->name))
        mel_dealloc(alloc, sheet->name.data);
    if (!str8_is_empty(sheet->texture_path))
        mel_dealloc(alloc, sheet->texture_path.data);

    if (sheet->frames)
        mel_dealloc(alloc, sheet->frames);

    if (sheet->animations)
    {
        for (u32 i = 0; i < sheet->animation_count; i++)
        {
            Mel_Animation* anim = &sheet->animations[i];

            if (!str8_is_empty(anim->name))
                mel_dealloc(alloc, anim->name.data);
            if (anim->frame_indices)
                mel_dealloc(alloc, anim->frame_indices);
            if (anim->frame_durations)
                mel_dealloc(alloc, anim->frame_durations);

            if (anim->frame_events)
            {
                for (u32 j = 0; j < anim->frame_count; j++)
                {
                    mel_frame_event_free(&anim->frame_events[j], alloc);
                }
                mel_dealloc(alloc, anim->frame_events);
            }
        }
        mel_dealloc(alloc, sheet->animations);
    }

    *sheet = (Mel_Spritesheet){0};
}

Mel_Animation* mel_spritesheet_find_animation(Mel_Spritesheet* sheet, str8 name)
{
    assert(sheet != nullptr);
    assert(!str8_is_empty(name));

    for (u32 i = 0; i < sheet->animation_count; i++)
    {
        if (str8_equals(sheet->animations[i].name, name))
        {
            return &sheet->animations[i];
        }
    }

    return nullptr;
}

Mel_SpriteFrame* mel_spritesheet_get_frame(Mel_Spritesheet* sheet, u32 index)
{
    assert(sheet != nullptr);
    assert(index < sheet->frame_count);

    return &sheet->frames[index];
}

void mel_spritesheet_get_frame_uv(Mel_Spritesheet* sheet, u32 index, f32* u0, f32* v0, f32* u1, f32* v1)
{
    assert(sheet != nullptr);
    assert(index < sheet->frame_count);

    Mel_SpriteFrame* f = &sheet->frames[index];

    f32 tex_w = (f32)sheet->texture_width;
    f32 tex_h = (f32)sheet->texture_height;

    *u0 = (f32)f->x / tex_w;
    *v0 = (f32)f->y / tex_h;
    *u1 = (f32)(f->x + f->width) / tex_w;
    *v1 = (f32)(f->y + f->height) / tex_h;
}

void mel_animation_player_init(Mel_AnimationPlayer* player, Mel_Spritesheet* sheet)
{
    assert(player != nullptr);
    assert(sheet != nullptr);

    *player = (Mel_AnimationPlayer){0};
    player->sheet = sheet;
}

void mel_animation_player_play(Mel_AnimationPlayer* player, str8 name)
{
    assert(player != nullptr);
    assert(!str8_is_empty(name));

    Mel_Animation* anim = mel_spritesheet_find_animation(player->sheet, name);
    if (!anim)
    {
        SDL_Log("Animation not found: %.*s", (int)name.len, name.data);
        return;
    }

    if (player->current == anim && player->playing)
    {
        return;
    }

    player->current = anim;
    player->current_frame = 0;
    player->timer = 0.0f;
    player->playing = true;
    player->finished = false;
}

f32 mel_animation_get_frame_duration(Mel_Animation* anim, u32 frame_idx)
{
    assert(anim != nullptr);
    assert(frame_idx < anim->frame_count);

    if (anim->frame_durations && anim->frame_durations[frame_idx] > 0.0f)
    {
        return anim->frame_durations[frame_idx];
    }
    return anim->default_duration;
}

void mel_animation_player_update(Mel_AnimationPlayer* player, f32 dt)
{
    assert(player != nullptr);

    if (!player->playing || !player->current || player->finished)
    {
        return;
    }

    player->timer += dt;

    f32 frame_duration = mel_animation_get_frame_duration(player->current, player->current_frame);

    while (player->timer >= frame_duration)
    {
        player->timer -= frame_duration;
        player->current_frame++;

        if (player->current_frame >= player->current->frame_count)
        {
            if (player->current->loop)
            {
                player->current_frame = 0;
            }
            else
            {
                player->current_frame = player->current->frame_count - 1;
                player->finished = true;
                player->playing = false;
                break;
            }
        }

        frame_duration = mel_animation_get_frame_duration(player->current, player->current_frame);
    }
}

Mel_SpriteFrame* mel_animation_player_current_frame(Mel_AnimationPlayer* player)
{
    assert(player != nullptr);

    if (!player->current || player->current->frame_count == 0)
    {
        return nullptr;
    }

    u32 frame_index = player->current->frame_indices[player->current_frame];
    return mel_spritesheet_get_frame(player->sheet, frame_index);
}

Mel_FrameEvent* mel_animation_player_current_event(Mel_AnimationPlayer* player)
{
    assert(player != nullptr);

    if (!player->current || player->current->frame_count == 0)
    {
        return nullptr;
    }

    if (!player->current->frame_events)
    {
        return nullptr;
    }

    return &player->current->frame_events[player->current_frame];
}

void mel_frame_event_free(Mel_FrameEvent* event, const Mel_Alloc* alloc)
{
    assert(event != nullptr);
    assert(alloc != nullptr);

    if (!str8_is_empty(event->sound_path))
    {
        mel_dealloc(alloc, event->sound_path.data);
    }

    if (event->tags)
    {
        for (u32 i = 0; i < event->tag_count; i++)
        {
            if (!str8_is_empty(event->tags[i]))
            {
                mel_dealloc(alloc, event->tags[i].data);
            }
        }
        mel_dealloc(alloc, event->tags);
    }

    *event = (Mel_FrameEvent){0};
}

void mel_frame_event_add_tag(Mel_FrameEvent* event, const Mel_Alloc* alloc, str8 tag)
{
    assert(event != nullptr);
    assert(alloc != nullptr);
    assert(!str8_is_empty(tag));

    u32 new_count = event->tag_count + 1;
    str8* new_tags = mel_realloc(alloc, event->tags, new_count * sizeof(str8));
    new_tags[event->tag_count] = str8_dup(tag, alloc);
    event->tags = new_tags;
    event->tag_count = new_count;
    event->flags |= MEL_FRAME_EVENT_TAG;
}

bool mel_frame_event_has_tag(Mel_FrameEvent* event, str8 tag)
{
    assert(event != nullptr);
    assert(!str8_is_empty(tag));

    for (u32 i = 0; i < event->tag_count; i++)
    {
        if (str8_equals(event->tags[i], tag))
        {
            return true;
        }
    }
    return false;
}

Mel_Animation* mel_spritesheet_add_animation(Mel_Spritesheet* sheet, str8 name)
{
    assert(sheet != nullptr);
    assert(!str8_is_empty(name));

    u32 new_count = sheet->animation_count + 1;
    Mel_Animation* new_anims = mel_realloc(sheet->alloc, sheet->animations, new_count * sizeof(Mel_Animation));

    Mel_Animation* anim = &new_anims[sheet->animation_count];
    *anim = (Mel_Animation){0};
    anim->name = str8_dup(name, sheet->alloc);
    anim->default_duration = 0.1f;
    anim->loop = true;

    sheet->animations = new_anims;
    sheet->animation_count = new_count;

    return anim;
}

void mel_animation_add_frame(Mel_Animation* anim, const Mel_Alloc* alloc, u32 frame_index, f32 duration)
{
    assert(anim != nullptr);
    assert(alloc != nullptr);

    u32 new_count = anim->frame_count + 1;

    anim->frame_indices = mel_realloc(alloc, anim->frame_indices, new_count * sizeof(u32));
    anim->frame_indices[anim->frame_count] = frame_index;

    if (duration > 0.0f)
    {
        if (!anim->frame_durations)
        {
            anim->frame_durations = mel_calloc(alloc, new_count * sizeof(f32));
        }
        else
        {
            anim->frame_durations = mel_realloc(alloc, anim->frame_durations, new_count * sizeof(f32));
        }
        anim->frame_durations[anim->frame_count] = duration;
    }
    else if (anim->frame_durations)
    {
        anim->frame_durations = mel_realloc(alloc, anim->frame_durations, new_count * sizeof(f32));
        anim->frame_durations[anim->frame_count] = 0.0f;
    }

    anim->frame_count = new_count;
}

void mel_animation_set_frame_event(Mel_Animation* anim, const Mel_Alloc* alloc, u32 anim_frame_idx, Mel_FrameEvent event)
{
    assert(anim != nullptr);
    assert(anim_frame_idx < anim->frame_count);

    if (!anim->frame_events)
    {
        anim->frame_events = mel_calloc(alloc, anim->frame_count * sizeof(Mel_FrameEvent));
    }

    anim->frame_events[anim_frame_idx] = event;
}
