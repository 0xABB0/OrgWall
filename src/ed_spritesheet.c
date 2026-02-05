#include "ed_spritesheet.h"

#include <cimgui/cimgui.h>
#include <string.h>
#include <stdlib.h>

#ifndef IM_COL32
#define IM_COL32(R,G,B,A) (((ImU32)(A)<<24) | ((ImU32)(B)<<16) | ((ImU32)(G)<<8) | ((ImU32)(R)))
#endif

static void draw_frame_list(Mel_EdSpritesheet* ed);
static void draw_frame_properties(Mel_EdSpritesheet* ed);
static void draw_animation_list(Mel_EdSpritesheet* ed);
static void draw_animation_properties(Mel_EdSpritesheet* ed);
static void draw_animation_timeline(Mel_EdSpritesheet* ed);
static void draw_frame_event_editor(Mel_EdSpritesheet* ed);
static void draw_texture_canvas(Mel_EdSpritesheet* ed);
static void draw_animation_preview(Mel_EdSpritesheet* ed, f32 dt);

void mel_ed_spritesheet_init(Mel_EdSpritesheet* ed, const Mel_Alloc* alloc)
{
    assert(ed != nullptr);

    *ed = (Mel_EdSpritesheet){0};
    ed->alloc = alloc;
    ed->selected_frame = -1;
    ed->selected_animation = -1;
    ed->selected_anim_frame = -1;
    ed->zoom = 1.0f;
    ed->show_grid = true;
    ed->preview_playing = false;
}

void mel_ed_spritesheet_shutdown(Mel_EdSpritesheet* ed)
{
    assert(ed != nullptr);
    ed->spritesheet = nullptr;
}

void mel_ed_spritesheet_set(Mel_EdSpritesheet* ed, Mel_Spritesheet* sheet)
{
    assert(ed != nullptr);

    ed->spritesheet = sheet;
    ed->selected_frame = -1;
    ed->selected_animation = -1;
    ed->selected_anim_frame = -1;
    ed->preview_time = 0;
    ed->preview_frame_idx = 0;
    ed->preview_playing = false;
    ed->dirty = false;
    ed->dragging_frame = false;

    if (sheet)
    {
        if (sheet->name)
        {
            strncpy(ed->name_buffer, sheet->name, sizeof(ed->name_buffer) - 1);
        }
        if (sheet->texture_path)
        {
            strncpy(ed->texture_path_buffer, sheet->texture_path, sizeof(ed->texture_path_buffer) - 1);
        }
    }
}

void mel_ed_spritesheet_draw(Mel_EdSpritesheet* ed, f32 dt)
{
    assert(ed != nullptr);

    igText("Spritesheet Editor");
    igSeparator();

    if (igInputText("Name##spritesheet", ed->name_buffer, sizeof(ed->name_buffer), 0, nullptr, nullptr))
    {
        ed->dirty = true;
    }

    igSliderFloat("Zoom", &ed->zoom, 0.25f, 4.0f, "%.2f", 0);
    igCheckbox("Show Grid", &ed->show_grid);

    igSeparator();

    if (!ed->spritesheet)
    {
        igTextColored((ImVec4){1.0f, 0.5f, 0.5f, 1.0f}, "No spritesheet loaded");
        return;
    }

    igText("Spritesheet: %s", ed->spritesheet->name ? ed->spritesheet->name : "(unnamed)");
    igText("Texture: %u x %u", ed->spritesheet->texture_width, ed->spritesheet->texture_height);
    igText("Frames: %u | Animations: %u", ed->spritesheet->frame_count, ed->spritesheet->animation_count);

    igSeparator();

    if (igBeginTabBar("SpritesheetTabs", 0))
    {
        if (igBeginTabItem("Frames", nullptr, 0))
        {
            igColumns(2, "FrameColumns", true);
            igSetColumnWidth(0, 300);

            draw_frame_list(ed);
            draw_frame_properties(ed);

            igNextColumn();

            draw_texture_canvas(ed);

            igColumns(1, nullptr, false);
            igEndTabItem();
        }

        if (igBeginTabItem("Animations", nullptr, 0))
        {
            igColumns(2, "AnimColumns", true);
            igSetColumnWidth(0, 350);

            draw_animation_list(ed);
            draw_animation_properties(ed);
            draw_animation_timeline(ed);

            igNextColumn();

            draw_animation_preview(ed, dt);

            igColumns(1, nullptr, false);
            igEndTabItem();
        }

        if (igBeginTabItem("Frame Events", nullptr, 0))
        {
            draw_frame_event_editor(ed);
            igEndTabItem();
        }

        igEndTabBar();
    }

    igSeparator();

    if (ed->dirty)
    {
        igTextColored((ImVec4){1.0f, 1.0f, 0.0f, 1.0f}, "* Unsaved changes");
    }
}

static void draw_frame_list(Mel_EdSpritesheet* ed)
{
    igText("Frames");

    if (igBeginChild_Str("FrameList", (ImVec2){0, 200}, ImGuiChildFlags_Borders, 0))
    {
        for (u32 i = 0; i < ed->spritesheet->frame_count; i++)
        {
            Mel_SpriteFrame* frame = &ed->spritesheet->frames[i];

            char label[64];
            snprintf(label, sizeof(label), "Frame %u (%u,%u %ux%u)",
                i, frame->x, frame->y, frame->width, frame->height);

            if (igSelectable_Bool(label, ed->selected_frame == (i32)i, 0, (ImVec2){0, 0}))
            {
                ed->selected_frame = (i32)i;
            }
        }
    }
    igEndChild();

    if (igButton("Add Frame", (ImVec2){100, 0}))
    {
        u32 new_count = ed->spritesheet->frame_count + 1;
        ed->spritesheet->frames = mel_realloc(ed->spritesheet->alloc, ed->spritesheet->frames, new_count * sizeof(Mel_SpriteFrame));

        Mel_SpriteFrame* frame = &ed->spritesheet->frames[ed->spritesheet->frame_count];
        *frame = (Mel_SpriteFrame){
            .x = 0,
            .y = 0,
            .width = 32,
            .height = 32
        };

        ed->spritesheet->frame_count = new_count;
        ed->selected_frame = (i32)(new_count - 1);
        ed->dirty = true;
    }

    igSameLine(0, 10);

    bool can_delete = ed->selected_frame >= 0 && (u32)ed->selected_frame < ed->spritesheet->frame_count;
    if (!can_delete) igBeginDisabled(true);
    if (igButton("Delete Frame", (ImVec2){100, 0}))
    {
        if (can_delete)
        {
            u32 idx = (u32)ed->selected_frame;
            if (idx < ed->spritesheet->frame_count - 1)
            {
                memmove(&ed->spritesheet->frames[idx],
                    &ed->spritesheet->frames[idx + 1],
                    (ed->spritesheet->frame_count - idx - 1) * sizeof(Mel_SpriteFrame));
            }
            ed->spritesheet->frame_count--;
            if (ed->selected_frame >= (i32)ed->spritesheet->frame_count)
            {
                ed->selected_frame = (i32)ed->spritesheet->frame_count - 1;
            }
            ed->dirty = true;
        }
    }
    if (!can_delete) igEndDisabled();
}

static void draw_frame_properties(Mel_EdSpritesheet* ed)
{
    igSeparator();

    if (ed->selected_frame >= 0 && (u32)ed->selected_frame < ed->spritesheet->frame_count)
    {
        igText("Frame %d Properties", ed->selected_frame);

        Mel_SpriteFrame* frame = &ed->spritesheet->frames[ed->selected_frame];

        i32 x = (i32)frame->x;
        i32 y = (i32)frame->y;
        i32 w = (i32)frame->width;
        i32 h = (i32)frame->height;
        i32 ox = frame->offset_x;
        i32 oy = frame->offset_y;

        if (igInputInt("X##frame", &x, 1, 10, 0))
        {
            frame->x = (u32)(x >= 0 ? x : 0);
            ed->dirty = true;
        }
        if (igInputInt("Y##frame", &y, 1, 10, 0))
        {
            frame->y = (u32)(y >= 0 ? y : 0);
            ed->dirty = true;
        }
        if (igInputInt("Width##frame", &w, 1, 10, 0))
        {
            frame->width = (u32)(w >= 1 ? w : 1);
            ed->dirty = true;
        }
        if (igInputInt("Height##frame", &h, 1, 10, 0))
        {
            frame->height = (u32)(h >= 1 ? h : 1);
            ed->dirty = true;
        }
        if (igInputInt("Offset X##frame", &ox, 1, 10, 0))
        {
            frame->offset_x = ox;
            ed->dirty = true;
        }
        if (igInputInt("Offset Y##frame", &oy, 1, 10, 0))
        {
            frame->offset_y = oy;
            ed->dirty = true;
        }
    }
    else
    {
        igTextDisabled("Select a frame to edit");
    }
}

static void draw_animation_list(Mel_EdSpritesheet* ed)
{
    igText("Animations");

    if (igBeginChild_Str("AnimList", (ImVec2){0, 150}, ImGuiChildFlags_Borders, 0))
    {
        for (u32 i = 0; i < ed->spritesheet->animation_count; i++)
        {
            Mel_Animation* anim = &ed->spritesheet->animations[i];

            char label[64];
            snprintf(label, sizeof(label), "%s (%u frames)",
                anim->name ? anim->name : "(unnamed)", anim->frame_count);

            if (igSelectable_Bool(label, ed->selected_animation == (i32)i, 0, (ImVec2){0, 0}))
            {
                ed->selected_animation = (i32)i;
                ed->selected_anim_frame = -1;
                ed->preview_time = 0;
                ed->preview_frame_idx = 0;
            }
        }
    }
    igEndChild();

    if (igButton("Add Animation", (ImVec2){120, 0}))
    {
        igOpenPopup_Str("New Animation##popup", 0);
    }

    if (igBeginPopupModal("New Animation##popup", nullptr, 0))
    {
        igInputText("Name##newanimation", ed->anim_name_buffer, sizeof(ed->anim_name_buffer), 0, nullptr, nullptr);

        if (igButton("Create##newanim", (ImVec2){100, 0}))
        {
            if (strlen(ed->anim_name_buffer) > 0)
            {
                mel_spritesheet_add_animation(ed->spritesheet, ed->anim_name_buffer);
                ed->selected_animation = (i32)(ed->spritesheet->animation_count - 1);
                ed->anim_name_buffer[0] = '\0';
                ed->dirty = true;
            }
            igCloseCurrentPopup();
        }
        igSameLine(0, 10);
        if (igButton("Cancel##newanim", (ImVec2){100, 0}))
        {
            igCloseCurrentPopup();
        }
        igEndPopup();
    }
}

static void draw_animation_properties(Mel_EdSpritesheet* ed)
{
    igSeparator();

    if (ed->selected_animation >= 0 && (u32)ed->selected_animation < ed->spritesheet->animation_count)
    {
        Mel_Animation* anim = &ed->spritesheet->animations[ed->selected_animation];

        igText("Animation: %s", anim->name ? anim->name : "(unnamed)");

        f32 duration = anim->default_duration;
        if (igInputFloat("Default Duration", &duration, 0.01f, 0.1f, "%.3f", 0))
        {
            anim->default_duration = duration > 0.001f ? duration : 0.001f;
            ed->dirty = true;
        }

        if (igCheckbox("Loop##anim", &anim->loop))
        {
            ed->dirty = true;
        }

        igSeparator();

        igText("Animation Frames:");

        if (igBeginChild_Str("AnimFrameList", (ImVec2){0, 100}, ImGuiChildFlags_Borders, 0))
        {
            for (u32 i = 0; i < anim->frame_count; i++)
            {
                u32 frame_idx = anim->frame_indices[i];
                f32 frame_dur = mel_animation_get_frame_duration(anim, i);

                char label[64];
                snprintf(label, sizeof(label), "[%u] Frame %u (%.3fs)", i, frame_idx, frame_dur);

                if (igSelectable_Bool(label, ed->selected_anim_frame == (i32)i, 0, (ImVec2){0, 0}))
                {
                    ed->selected_anim_frame = (i32)i;
                }
            }
        }
        igEndChild();

        if (igButton("Add Frame to Anim", (ImVec2){140, 0}))
        {
            if (ed->selected_frame >= 0)
            {
                mel_animation_add_frame(anim, ed->spritesheet->alloc, (u32)ed->selected_frame, 0);
                ed->dirty = true;
            }
        }

        if (ed->selected_anim_frame >= 0 && (u32)ed->selected_anim_frame < anim->frame_count)
        {
            igSameLine(0, 10);
            if (igButton("Remove from Anim", (ImVec2){140, 0}))
            {
                u32 idx = (u32)ed->selected_anim_frame;
                if (idx < anim->frame_count - 1)
                {
                    memmove(&anim->frame_indices[idx],
                        &anim->frame_indices[idx + 1],
                        (anim->frame_count - idx - 1) * sizeof(u32));

                    if (anim->frame_durations)
                    {
                        memmove(&anim->frame_durations[idx],
                            &anim->frame_durations[idx + 1],
                            (anim->frame_count - idx - 1) * sizeof(f32));
                    }

                    if (anim->frame_events)
                    {
                        mel_frame_event_free(&anim->frame_events[idx], ed->spritesheet->alloc);
                        memmove(&anim->frame_events[idx],
                            &anim->frame_events[idx + 1],
                            (anim->frame_count - idx - 1) * sizeof(Mel_FrameEvent));
                    }
                }
                anim->frame_count--;
                if (ed->selected_anim_frame >= (i32)anim->frame_count)
                {
                    ed->selected_anim_frame = (i32)anim->frame_count - 1;
                }
                ed->dirty = true;
            }
        }
    }
    else
    {
        igTextDisabled("Select an animation to edit");
    }
}

static void draw_animation_timeline(Mel_EdSpritesheet* ed)
{
    if (ed->selected_animation < 0 || (u32)ed->selected_animation >= ed->spritesheet->animation_count)
    {
        return;
    }

    Mel_Animation* anim = &ed->spritesheet->animations[ed->selected_animation];

    igSeparator();
    igText("Selected Animation Frame:");

    if (ed->selected_anim_frame >= 0 && (u32)ed->selected_anim_frame < anim->frame_count)
    {
        u32 idx = (u32)ed->selected_anim_frame;

        i32 frame_idx = (i32)anim->frame_indices[idx];
        if (igInputInt("Sprite Frame##animframe", &frame_idx, 1, 10, 0))
        {
            if (frame_idx >= 0 && (u32)frame_idx < ed->spritesheet->frame_count)
            {
                anim->frame_indices[idx] = (u32)frame_idx;
                ed->dirty = true;
            }
        }

        f32 dur = anim->frame_durations ? anim->frame_durations[idx] : 0;
        if (igInputFloat("Duration Override##animframe", &dur, 0.01f, 0.1f, "%.3f", 0))
        {
            if (!anim->frame_durations)
            {
                anim->frame_durations = mel_calloc(ed->spritesheet->alloc, anim->frame_count * sizeof(f32));
            }
            anim->frame_durations[idx] = dur >= 0 ? dur : 0;
            ed->dirty = true;
        }
        igTextDisabled("(0 = use default duration)");
    }
    else
    {
        igTextDisabled("Select an animation frame");
    }
}

static void draw_frame_event_editor(Mel_EdSpritesheet* ed)
{
    if (ed->selected_animation < 0 || (u32)ed->selected_animation >= ed->spritesheet->animation_count)
    {
        igTextDisabled("Select an animation first");
        return;
    }

    Mel_Animation* anim = &ed->spritesheet->animations[ed->selected_animation];

    if (ed->selected_anim_frame < 0 || (u32)ed->selected_anim_frame >= anim->frame_count)
    {
        igTextDisabled("Select an animation frame first");
        return;
    }

    u32 idx = (u32)ed->selected_anim_frame;

    igText("Frame Event Editor - Animation Frame %u", idx);
    igSeparator();

    Mel_FrameEvent* event = nullptr;
    if (anim->frame_events)
    {
        event = &anim->frame_events[idx];
    }

    if (igCollapsingHeader_TreeNodeFlags("Sound Event", ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool has_sound = event && (event->flags & MEL_FRAME_EVENT_SOUND);

        if (has_sound && event->sound_path)
        {
            strncpy(ed->event_sound_buffer, event->sound_path, sizeof(ed->event_sound_buffer) - 1);
        }

        if (igInputText("Sound Path##event", ed->event_sound_buffer, sizeof(ed->event_sound_buffer), 0, nullptr, nullptr))
        {
            ed->dirty = true;
        }

        f32 volume = event ? event->sound_volume : 1.0f;
        if (igSliderFloat("Volume##event", &volume, 0.0f, 1.0f, "%.2f", 0))
        {
            if (!anim->frame_events)
            {
                anim->frame_events = mel_calloc(ed->spritesheet->alloc, anim->frame_count * sizeof(Mel_FrameEvent));
                event = &anim->frame_events[idx];
            }
            event->sound_volume = volume;
            ed->dirty = true;
        }

        if (igButton("Set Sound##event", (ImVec2){100, 0}))
        {
            if (strlen(ed->event_sound_buffer) > 0)
            {
                if (!anim->frame_events)
                {
                    anim->frame_events = mel_calloc(ed->spritesheet->alloc, anim->frame_count * sizeof(Mel_FrameEvent));
                    event = &anim->frame_events[idx];
                }
                if (event->sound_path) mel_dealloc(ed->spritesheet->alloc, (void*)event->sound_path);
                usize len = strlen(ed->event_sound_buffer) + 1;
                char* dup = mel_alloc(ed->spritesheet->alloc, len);
                memcpy(dup, ed->event_sound_buffer, len);
                event->sound_path = dup;
                event->flags |= MEL_FRAME_EVENT_SOUND;
                ed->dirty = true;
            }
        }

        igSameLine(0, 10);
        if (igButton("Clear Sound##event", (ImVec2){100, 0}))
        {
            if (event)
            {
                if (event->sound_path) mel_dealloc(ed->spritesheet->alloc, (void*)event->sound_path);
                event->sound_path = nullptr;
                event->flags &= ~MEL_FRAME_EVENT_SOUND;
                ed->event_sound_buffer[0] = '\0';
                ed->dirty = true;
            }
        }
    }

    if (igCollapsingHeader_TreeNodeFlags("Hitbox Event", ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool has_hitbox = event && (event->flags & MEL_FRAME_EVENT_HITBOX);

        i32 hx = event ? event->hitbox_x : 0;
        i32 hy = event ? event->hitbox_y : 0;
        i32 hw = event ? (i32)event->hitbox_width : 0;
        i32 hh = event ? (i32)event->hitbox_height : 0;

        bool changed = false;
        if (igInputInt("Hitbox X##event", &hx, 1, 10, 0)) changed = true;
        if (igInputInt("Hitbox Y##event", &hy, 1, 10, 0)) changed = true;
        if (igInputInt("Hitbox Width##event", &hw, 1, 10, 0)) changed = true;
        if (igInputInt("Hitbox Height##event", &hh, 1, 10, 0)) changed = true;

        if (changed)
        {
            if (!anim->frame_events)
            {
                anim->frame_events = mel_calloc(ed->spritesheet->alloc, anim->frame_count * sizeof(Mel_FrameEvent));
                event = &anim->frame_events[idx];
            }
            event->hitbox_x = hx;
            event->hitbox_y = hy;
            event->hitbox_width = (u32)(hw >= 0 ? hw : 0);
            event->hitbox_height = (u32)(hh >= 0 ? hh : 0);
            if (hw > 0 && hh > 0)
            {
                event->flags |= MEL_FRAME_EVENT_HITBOX;
            }
            else
            {
                event->flags &= ~MEL_FRAME_EVENT_HITBOX;
            }
            ed->dirty = true;
        }

        if (has_hitbox)
        {
            igTextColored((ImVec4){0.5f, 1.0f, 0.5f, 1.0f}, "Hitbox: %d,%d %ux%u",
                event->hitbox_x, event->hitbox_y, event->hitbox_width, event->hitbox_height);
        }
        else
        {
            igTextDisabled("No hitbox set");
        }
    }

    if (igCollapsingHeader_TreeNodeFlags("Custom Tags", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (event && event->tag_count > 0)
        {
            for (u32 t = 0; t < event->tag_count; t++)
            {
                if (event->tags[t])
                {
                    igBulletText("%s", event->tags[t]);
                }
            }
        }
        else
        {
            igTextDisabled("No tags");
        }

        igInputText("New Tag##event", ed->event_tag_buffer, sizeof(ed->event_tag_buffer), 0, nullptr, nullptr);
        igSameLine(0, 10);

        if (igButton("Add Tag##event", (ImVec2){80, 0}))
        {
            if (strlen(ed->event_tag_buffer) > 0)
            {
                if (!anim->frame_events)
                {
                    anim->frame_events = mel_calloc(ed->spritesheet->alloc, anim->frame_count * sizeof(Mel_FrameEvent));
                    event = &anim->frame_events[idx];
                }
                mel_frame_event_add_tag(event, ed->spritesheet->alloc, ed->event_tag_buffer);
                ed->event_tag_buffer[0] = '\0';
                ed->dirty = true;
            }
        }
    }
}

static void draw_texture_canvas(Mel_EdSpritesheet* ed)
{
    igText("Texture Preview");
    igText("Click to select frame, drag to create new frame");

    if (igBeginChild_Str("TextureCanvas", (ImVec2){0, 0}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
    {
        ImVec2_c cursor_pos = igGetCursorScreenPos();
        ImDrawList* draw_list = igGetWindowDrawList();

        f32 tex_w = (f32)ed->spritesheet->texture_width * ed->zoom;
        f32 tex_h = (f32)ed->spritesheet->texture_height * ed->zoom;

        ImVec2 p0 = {cursor_pos.x, cursor_pos.y};
        ImVec2 p1 = {p0.x + tex_w, p0.y + tex_h};
        ImDrawList_AddRectFilled(draw_list, p0, p1, IM_COL32(30, 30, 40, 255), 0, 0);
        ImDrawList_AddRect(draw_list, p0, p1, IM_COL32(100, 100, 100, 255), 0, 0, 1.0f);

        for (u32 i = 0; i < ed->spritesheet->frame_count; i++)
        {
            Mel_SpriteFrame* frame = &ed->spritesheet->frames[i];

            ImVec2 fp0 = {
                cursor_pos.x + (f32)frame->x * ed->zoom,
                cursor_pos.y + (f32)frame->y * ed->zoom
            };
            ImVec2 fp1 = {
                fp0.x + (f32)frame->width * ed->zoom,
                fp0.y + (f32)frame->height * ed->zoom
            };

            ImU32 frame_color = (ed->selected_frame == (i32)i)
                ? IM_COL32(100, 200, 100, 200)
                : IM_COL32(100, 100, 200, 100);

            ImDrawList_AddRectFilled(draw_list, fp0, fp1, IM_COL32(50, 50, 80, 100), 0, 0);
            ImDrawList_AddRect(draw_list, fp0, fp1, frame_color, 0, 0, 2.0f);

            char label[16];
            snprintf(label, sizeof(label), "%u", i);
            ImDrawList_AddText_Vec2(draw_list, (ImVec2){fp0.x + 2, fp0.y + 2}, IM_COL32(255, 255, 255, 200), label, nullptr);
        }

        if (ed->dragging_frame)
        {
            i32 x0 = ed->drag_start_x < ed->drag_end_x ? ed->drag_start_x : ed->drag_end_x;
            i32 y0 = ed->drag_start_y < ed->drag_end_y ? ed->drag_start_y : ed->drag_end_y;
            i32 x1 = ed->drag_start_x > ed->drag_end_x ? ed->drag_start_x : ed->drag_end_x;
            i32 y1 = ed->drag_start_y > ed->drag_end_y ? ed->drag_start_y : ed->drag_end_y;

            ImVec2 dp0 = {cursor_pos.x + (f32)x0 * ed->zoom, cursor_pos.y + (f32)y0 * ed->zoom};
            ImVec2 dp1 = {cursor_pos.x + (f32)x1 * ed->zoom, cursor_pos.y + (f32)y1 * ed->zoom};
            ImDrawList_AddRect(draw_list, dp0, dp1, IM_COL32(255, 200, 100, 255), 0, 0, 2.0f);
        }

        ImGuiIO* io = igGetIO_Nil();
        if (igIsWindowHovered(0))
        {
            ImVec2_c mouse_pos = igGetMousePos();
            f32 rel_x = (mouse_pos.x - cursor_pos.x) / ed->zoom;
            f32 rel_y = (mouse_pos.y - cursor_pos.y) / ed->zoom;

            if (io->MouseClicked[0])
            {
                bool found = false;
                for (u32 i = 0; i < ed->spritesheet->frame_count; i++)
                {
                    Mel_SpriteFrame* frame = &ed->spritesheet->frames[i];

                    if (rel_x >= (f32)frame->x && rel_x < (f32)(frame->x + frame->width) &&
                        rel_y >= (f32)frame->y && rel_y < (f32)(frame->y + frame->height))
                    {
                        ed->selected_frame = (i32)i;
                        found = true;
                        break;
                    }
                }

                if (!found)
                {
                    ed->dragging_frame = true;
                    ed->drag_start_x = (i32)rel_x;
                    ed->drag_start_y = (i32)rel_y;
                    ed->drag_end_x = ed->drag_start_x;
                    ed->drag_end_y = ed->drag_start_y;
                }
            }

            if (ed->dragging_frame && io->MouseDown[0])
            {
                ed->drag_end_x = (i32)rel_x;
                ed->drag_end_y = (i32)rel_y;
            }

            if (ed->dragging_frame && !io->MouseDown[0])
            {
                ed->dragging_frame = false;

                i32 x0 = ed->drag_start_x < ed->drag_end_x ? ed->drag_start_x : ed->drag_end_x;
                i32 y0 = ed->drag_start_y < ed->drag_end_y ? ed->drag_start_y : ed->drag_end_y;
                i32 x1 = ed->drag_start_x > ed->drag_end_x ? ed->drag_start_x : ed->drag_end_x;
                i32 y1 = ed->drag_start_y > ed->drag_end_y ? ed->drag_start_y : ed->drag_end_y;

                i32 w = x1 - x0;
                i32 h = y1 - y0;

                if (w >= 4 && h >= 4)
                {
                    u32 new_count = ed->spritesheet->frame_count + 1;
                    ed->spritesheet->frames = mel_realloc(ed->spritesheet->alloc, ed->spritesheet->frames, new_count * sizeof(Mel_SpriteFrame));

                    Mel_SpriteFrame* frame = &ed->spritesheet->frames[ed->spritesheet->frame_count];
                    *frame = (Mel_SpriteFrame){
                        .x = (u32)x0,
                        .y = (u32)y0,
                        .width = (u32)w,
                        .height = (u32)h
                    };

                    ed->spritesheet->frame_count = new_count;
                    ed->selected_frame = (i32)(new_count - 1);
                    ed->dirty = true;
                }
            }
        }

        igSetCursorPosY(igGetCursorPosY() + tex_h);
    }
    igEndChild();
}

static void draw_animation_preview(Mel_EdSpritesheet* ed, f32 dt)
{
    igText("Animation Preview");

    if (ed->selected_animation >= 0 && (u32)ed->selected_animation < ed->spritesheet->animation_count)
    {
        Mel_Animation* anim = &ed->spritesheet->animations[ed->selected_animation];

        if (igButton(ed->preview_playing ? "Pause" : "Play", (ImVec2){60, 0}))
        {
            ed->preview_playing = !ed->preview_playing;
        }
        igSameLine(0, 10);
        if (igButton("Reset", (ImVec2){60, 0}))
        {
            ed->preview_time = 0;
            ed->preview_frame_idx = 0;
        }

        if (ed->preview_playing && anim->frame_count > 0)
        {
            ed->preview_time += dt;

            f32 frame_dur = mel_animation_get_frame_duration(anim, ed->preview_frame_idx);

            while (ed->preview_time >= frame_dur && frame_dur > 0)
            {
                ed->preview_time -= frame_dur;
                ed->preview_frame_idx++;

                if (ed->preview_frame_idx >= anim->frame_count)
                {
                    if (anim->loop)
                    {
                        ed->preview_frame_idx = 0;
                    }
                    else
                    {
                        ed->preview_frame_idx = anim->frame_count - 1;
                        ed->preview_playing = false;
                        break;
                    }
                }

                frame_dur = mel_animation_get_frame_duration(anim, ed->preview_frame_idx);
            }
        }

        igText("Frame: %u / %u", ed->preview_frame_idx + 1, anim->frame_count);
        igText("Time: %.2fs", ed->preview_time);

        if (anim->frame_count > 0 && ed->preview_frame_idx < anim->frame_count)
        {
            u32 sprite_frame_idx = anim->frame_indices[ed->preview_frame_idx];
            if (sprite_frame_idx < ed->spritesheet->frame_count)
            {
                Mel_SpriteFrame* frame = &ed->spritesheet->frames[sprite_frame_idx];
                igText("Sprite Frame: %u (%ux%u)", sprite_frame_idx, frame->width, frame->height);

                if (anim->frame_events)
                {
                    Mel_FrameEvent* ev = &anim->frame_events[ed->preview_frame_idx];
                    if (ev->flags & MEL_FRAME_EVENT_SOUND)
                    {
                        igTextColored((ImVec4){1.0f, 0.8f, 0.2f, 1.0f}, "Sound: %s", ev->sound_path);
                    }
                    if (ev->flags & MEL_FRAME_EVENT_HITBOX)
                    {
                        igTextColored((ImVec4){1.0f, 0.5f, 0.5f, 1.0f}, "Hitbox: %d,%d %ux%u",
                            ev->hitbox_x, ev->hitbox_y, ev->hitbox_width, ev->hitbox_height);
                    }
                    if (ev->flags & MEL_FRAME_EVENT_TAG)
                    {
                        for (u32 t = 0; t < ev->tag_count; t++)
                        {
                            igTextColored((ImVec4){0.5f, 1.0f, 0.8f, 1.0f}, "Tag: %s", ev->tags[t]);
                        }
                    }
                }
            }
        }
    }
    else
    {
        igTextDisabled("Select an animation to preview");
    }

    igSeparator();

    if (igBeginChild_Str("PreviewCanvas", (ImVec2){0, 0}, ImGuiChildFlags_Borders, 0))
    {
        if (ed->selected_animation >= 0 && (u32)ed->selected_animation < ed->spritesheet->animation_count)
        {
            Mel_Animation* anim = &ed->spritesheet->animations[ed->selected_animation];

            if (anim->frame_count > 0 && ed->preview_frame_idx < anim->frame_count)
            {
                u32 sprite_frame_idx = anim->frame_indices[ed->preview_frame_idx];
                if (sprite_frame_idx < ed->spritesheet->frame_count)
                {
                    Mel_SpriteFrame* frame = &ed->spritesheet->frames[sprite_frame_idx];

                    ImVec2_c cursor_pos = igGetCursorScreenPos();
                    ImDrawList* draw_list = igGetWindowDrawList();

                    f32 scale = 2.0f;
                    ImVec2 p0 = {cursor_pos.x + 20, cursor_pos.y + 20};
                    ImVec2 p1 = {p0.x + (f32)frame->width * scale, p0.y + (f32)frame->height * scale};

                    ImDrawList_AddRectFilled(draw_list, p0, p1, IM_COL32(100, 150, 200, 255), 0, 0);
                    ImDrawList_AddRect(draw_list, p0, p1, IM_COL32(255, 255, 255, 255), 0, 0, 2.0f);
                }
            }
        }
    }
    igEndChild();
}
