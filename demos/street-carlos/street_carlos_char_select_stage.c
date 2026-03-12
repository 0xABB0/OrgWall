#include "street_carlos_char_select_stage.h"

#include <assert.h>
#include <string.h>

#include "street_carlos_flow.h"
#include "core.engine.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "game_draw.h"
#include "gpu.pipeline.h"
#include "math.scalar.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "mugen.air.h"
#include "render.list.h"
#include "sprite.pass.h"

static void draw_centered_text(Street_Carlos_Ctx* ctx, Mel_Render_List* list, Mel_Font_Handle font, str8 text, f32 y, Mel_Vec4 color)
{
    Mel_Vec2 size = mel_font_atlas_measure_text(&ctx->font_pool, font, text);
    f32 x = (f32)GAME_W * 0.5f - size.x * 0.5f;
    mel_font_atlas_draw_text(&ctx->font_pool, font, list, text, x, y, color);
}

static void draw_box_outline(Mel_Render_List* list, f32 x, f32 y, f32 w, f32 h, Mel_Vec4 color)
{
    mel_draw_sprite(list, .pos = mel_vec2(x, y), .size = mel_vec2(w, 2.0f), .color = color, .layer = 3);
    mel_draw_sprite(list, .pos = mel_vec2(x, y + h - 2.0f), .size = mel_vec2(w, 2.0f), .color = color, .layer = 3);
    mel_draw_sprite(list, .pos = mel_vec2(x, y), .size = mel_vec2(2.0f, h), .color = color, .layer = 3);
    mel_draw_sprite(list, .pos = mel_vec2(x + w - 2.0f, y), .size = mel_vec2(2.0f, h), .color = color, .layer = 3);
}

static u32 move_grid_cursor(u32 index, u32 count, u32 cols, i32 dx, i32 dy)
{
    if (count == 0)
        return 0;

    u32 rows = (count + cols - 1) / cols;
    u32 col = index % cols;
    u32 row = index / cols;

    col = (u32)((i32)col + dx + (i32)cols) % cols;
    row = (u32)((i32)row + dy + (i32)rows) % rows;

    u32 next = row * cols + col;
    while (next >= count)
    {
        if (col == 0)
            break;
        col--;
        next = row * cols + col;
    }
    return next;
}

static void upload_char_preview(Mugen_Char* ch, Street_Carlos_Char_Preview* preview)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    mel_gpu_texture_init(&preview->texture, dev,
        .pixels = ch->sff.atlas_pixels,
        .width = ch->sff.atlas_width,
        .height = ch->sff.atlas_height,
        .nearest_filter = true);
    preview->texture.descriptor = mel_gpu_pipeline_alloc_descriptor(&mel_sprite_pass()->pipeline, dev);
    mel_gpu_pipeline_write_texture(&mel_sprite_pass()->pipeline, dev,
        preview->texture.descriptor, preview->texture.image.view, preview->texture.sampler);
    preview->handle = mel_texture_pool_register(mel_texture_pool(), &preview->texture);
}

static void street_carlos_char_select_stage_start(Mel_Stage* base, void* user)
{
    MEL_UNUSED(base);
    Street_Carlos_Char_Select_Stage* stage = user;
    stage->p1_cursor = 0;
    stage->p2_cursor = stage->preview_count > 1 ? 1 : 0;
    stage->p1_locked = false;
    stage->p2_locked = false;
}

void street_carlos_char_select_stage_init(Street_Carlos_Char_Select_Stage* stage, Street_Carlos_Ctx* ctx)
{
    stage->preview_count = mugen_roster_count(&ctx->roster);
    if (stage->preview_count > 0)
    {
        stage->previews = mel_alloc(mel_alloc_heap(), sizeof(*stage->previews) * stage->preview_count);
        memset(stage->previews, 0, sizeof(*stage->previews) * stage->preview_count);

        for (u32 i = 0; i < stage->preview_count; i++)
        {
            Mugen_Char* ch = mugen_roster_at(&ctx->roster, i);
            if (ch)
                upload_char_preview(ch, &stage->previews[i]);
        }
    }

    mel_stage_init(&stage->stage,
        .on_start = street_carlos_char_select_stage_start,
        .user = stage,
        .start_enabled = false);
}

void street_carlos_char_select_stage_shutdown(Street_Carlos_Char_Select_Stage* stage, Street_Carlos_Ctx* ctx)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    for (u32 i = 0; i < stage->preview_count; i++)
    {
        if (mel_slotmap_handle_valid(stage->previews[i].handle.handle))
            mel_gpu_texture_shutdown(&stage->previews[i].texture, dev);
    }
    if (stage->previews)
        mel_dealloc(mel_alloc_heap(), stage->previews);
    MEL_UNUSED(ctx);
    mel_stage_shutdown(&stage->stage);
}

bool street_carlos_char_select_stage_handle_event(Street_Carlos_Char_Select_Stage* stage, Street_Carlos_Ctx* ctx, SDL_Event* event)
{
    if (!mel_stage_is_enabled(&stage->stage) || event->type != SDL_EVENT_KEY_DOWN || event->key.repeat)
        return false;

    u32 count = mugen_roster_count(&ctx->roster);
    if (count == 0)
        return false;

    if (event->key.scancode == SDL_SCANCODE_BACKSPACE)
    {
        street_carlos_show_main_menu(ctx);
        return true;
    }

    const u32 cols = count < 4 ? count : 4;

    if (!stage->p1_locked)
    {
        if (event->key.scancode == SDL_SCANCODE_A) { stage->p1_cursor = move_grid_cursor(stage->p1_cursor, count, cols, -1, 0); return true; }
        if (event->key.scancode == SDL_SCANCODE_D) { stage->p1_cursor = move_grid_cursor(stage->p1_cursor, count, cols, 1, 0); return true; }
        if (event->key.scancode == SDL_SCANCODE_W) { stage->p1_cursor = move_grid_cursor(stage->p1_cursor, count, cols, 0, -1); return true; }
        if (event->key.scancode == SDL_SCANCODE_S) { stage->p1_cursor = move_grid_cursor(stage->p1_cursor, count, cols, 0, 1); return true; }
    }

    if (!stage->p2_locked)
    {
        if (event->key.scancode == SDL_SCANCODE_LEFT) { stage->p2_cursor = move_grid_cursor(stage->p2_cursor, count, cols, -1, 0); return true; }
        if (event->key.scancode == SDL_SCANCODE_RIGHT) { stage->p2_cursor = move_grid_cursor(stage->p2_cursor, count, cols, 1, 0); return true; }
        if (event->key.scancode == SDL_SCANCODE_UP) { stage->p2_cursor = move_grid_cursor(stage->p2_cursor, count, cols, 0, -1); return true; }
        if (event->key.scancode == SDL_SCANCODE_DOWN) { stage->p2_cursor = move_grid_cursor(stage->p2_cursor, count, cols, 0, 1); return true; }
    }

    if (event->key.scancode == SDL_SCANCODE_J)
    {
        stage->p1_locked = !stage->p1_locked;
    }
    else if (event->key.scancode == SDL_SCANCODE_KP_4 || event->key.scancode == SDL_SCANCODE_RETURN)
    {
        stage->p2_locked = !stage->p2_locked;
    }
    else
    {
        return false;
    }

    if (stage->p1_locked && stage->p2_locked)
        street_carlos_show_stage_select(ctx);
    return true;
}

void street_carlos_char_select_stage_draw_world(Street_Carlos_Char_Select_Stage* stage, Street_Carlos_Ctx* ctx, Mel_Render_List* list)
{
    if (!mel_stage_is_enabled(&stage->stage))
        return;

    u32 count = mugen_roster_count(&ctx->roster);
    draw_centered_text(ctx, list, ctx->title_font, S8("SELECT FIGHTER"), 10.0f, mel_vec4(1.0f, 0.3f, 0.3f, 1.0f));

    if (count == 0)
    {
        draw_centered_text(ctx, list, ctx->ui_font, S8("No fighters found in /chars"), 96.0f, mel_vec4(1, 1, 1, 1));
        return;
    }

    mel_draw_sprite(list, .pos = mel_vec2(12.0f, 38.0f), .size = mel_vec2(168.0f, 88.0f), .color = mel_vec4(0.08f, 0.08f, 0.12f, 1.0f));
    mel_draw_sprite(list, .pos = mel_vec2(204.0f, 38.0f), .size = mel_vec2(168.0f, 88.0f), .color = mel_vec4(0.08f, 0.08f, 0.12f, 1.0f));

    Mugen_Char* p1 = mugen_roster_at(&ctx->roster, stage->p1_cursor);
    Mugen_Char* p2 = mugen_roster_at(&ctx->roster, stage->p2_cursor);
    str8 p1_name = mugen_roster_name_at(&ctx->roster, stage->p1_cursor);
    str8 p2_name = mugen_roster_name_at(&ctx->roster, stage->p2_cursor);

    if (!game_draw_char_portrait_preview(p1, stage->previews[stage->p1_cursor].handle, 9000, 0, true,
            mel_rect(20.0f, 48.0f, 152.0f, 68.0f), list))
        game_draw_char_action_preview(p1, stage->previews[stage->p1_cursor].handle, 0, true, mel_vec2(96.0f, 120.0f), 0.75f, list);

    if (!game_draw_char_portrait_preview(p2, stage->previews[stage->p2_cursor].handle, 9000, 0, false,
            mel_rect(212.0f, 48.0f, 152.0f, 68.0f), list))
        game_draw_char_action_preview(p2, stage->previews[stage->p2_cursor].handle, 0, false, mel_vec2(288.0f, 120.0f), 0.75f, list);

    draw_centered_text(ctx, list, ctx->ui_font, p1_name, 28.0f, mel_vec4(1.0f, 0.6f, 0.6f, 1.0f));
    draw_centered_text(ctx, list, ctx->ui_font, p2_name, 28.0f, mel_vec4(0.6f, 0.8f, 1.0f, 1.0f));
    draw_centered_text(ctx, list, ctx->ui_font, stage->p1_locked ? S8("P1 READY") : S8("P1: WASD + J"), 124.0f, mel_vec4(1.0f, 0.6f, 0.6f, 1.0f));
    draw_centered_text(ctx, list, ctx->ui_font, stage->p2_locked ? S8("P2 READY") : S8("P2: ARROWS + ENTER"), 140.0f, mel_vec4(0.6f, 0.8f, 1.0f, 1.0f));

    const u32 cols = count < 4 ? count : 4;
    const f32 cell_w = 80.0f;
    const f32 cell_h = 30.0f;
    const f32 start_x = 24.0f;
    const f32 start_y = 164.0f;

    for (u32 i = 0; i < count; i++)
    {
        u32 col = i % cols;
        u32 row = i / cols;
        f32 x = start_x + (f32)col * (cell_w + 8.0f);
        f32 y = start_y + (f32)row * (cell_h + 8.0f);

        mel_draw_sprite(list, .pos = mel_vec2(x, y), .size = mel_vec2(cell_w, cell_h), .color = mel_vec4(0.12f, 0.12f, 0.16f, 1.0f));
        if (i == stage->p1_cursor)
            draw_box_outline(list, x, y, cell_w, cell_h, mel_vec4(1.0f, 0.4f, 0.4f, 1.0f));
        if (i == stage->p2_cursor)
            draw_box_outline(list, x + 4.0f, y + 4.0f, cell_w - 8.0f, cell_h - 8.0f, mel_vec4(0.4f, 0.7f, 1.0f, 1.0f));

        str8 name = mugen_roster_name_at(&ctx->roster, i);
        Mugen_Char* ch = mugen_roster_at(&ctx->roster, i);
        game_draw_char_portrait_preview(ch, stage->previews[i].handle, 9000, 0, true,
            mel_rect(x + 4.0f, y + 3.0f, 18.0f, cell_h - 6.0f), list);

        Mel_Vec2 size = mel_font_atlas_measure_text(&ctx->font_pool, ctx->ui_font, name);
        mel_font_atlas_draw_text(&ctx->font_pool, ctx->ui_font, list, name,
            x + 26.0f + mel_maxf((cell_w - 30.0f - size.x) * 0.5f, 0.0f), y + 7.0f, mel_vec4(1, 1, 1, 1));
    }

    draw_centered_text(ctx, list, ctx->ui_font, S8("BACKSPACE to return"), 222.0f, mel_vec4(0.8f, 0.8f, 0.8f, 1.0f));
}
