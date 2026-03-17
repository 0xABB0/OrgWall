#include "street_carlos_stage_select_stage.h"

#include <stdlib.h>
#include <string.h>

#include "street_carlos_char_select_stage.h"
#include "street_carlos_flow.h"
#include "core.engine.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "game_draw.h"
#include "gpu.pipeline.h"
#include "math.scalar.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "render.list.h"
#include "sprite.pass.h"
#include "string.path.h"
#include "string.str8.h"
#include "vfs.h"
#include "mugen.stage.h"

static void draw_centered_text(Street_Carlos_Ctx* ctx, Mel_Render_List* list, Mel_Font_Atlas_Handle font, str8 text, f32 y, Mel_Vec4 color)
{
    Mel_Vec2 size = mel_font_atlas_measure_text(font, text);
    f32 x = (f32)GAME_W * 0.5f - size.x * 0.5f;
    mel_font_atlas_draw_text(font, list, text, x, y, color);
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

static str8 preview_strip_comment(str8 line)
{
    for (size i = 0; i < line.len; i++)
    {
        if (line.data[i] == ';' || line.data[i] == '#')
            return str8_from_parts(line.data, i);
    }
    return line;
}

static str8 preview_before_eq(str8 line)
{
    for (size i = 0; i < line.len; i++)
    {
        if (line.data[i] == '=')
            return str8_trim(str8_from_parts(line.data, i));
    }
    return str8_trim(line);
}

static str8 preview_after_eq(str8 line)
{
    for (size i = 0; i < line.len; i++)
    {
        if (line.data[i] == '=')
            return str8_trim(str8_from_parts(line.data + i + 1, line.len - i - 1));
    }
    return STR8_EMPTY;
}

static f32 preview_parse_float(str8 value, f32 fallback)
{
    char buf[64];
    size copied = str8_to_buf(str8_trim(value), buf, sizeof(buf));
    if (copied == 0)
        return fallback;

    char* end = nullptr;
    f32 parsed = strtof(buf, &end);
    if (end == buf)
        return fallback;
    return parsed;
}

static bool preview_parse_vec2(str8 value, Mel_Vec2* out)
{
    size comma = str8_find(value, S8(","));
    if (comma == value.len)
        return false;

    str8 x_part = str8_trim(str8_prefix(value, comma));
    str8 y_part = str8_trim(str8_slice(value, comma + 1, value.len - comma - 1));
    out->x = preview_parse_float(x_part, out->x);
    out->y = preview_parse_float(y_part, out->y);
    return true;
}

static str8 stage_preview_path(str8 stage_path, u8* buf, size buf_cap)
{
    size dot = stage_path.len;
    for (size i = stage_path.len; i > 0; i--)
    {
        u8 c = stage_path.data[i - 1];
        if (c == '.')
        {
            dot = i - 1;
            break;
        }
        if (c == '/')
            break;
    }

    const str8 ext = S8(".preview");
    if (dot + ext.len > buf_cap)
        return STR8_EMPTY;

    memcpy(buf, stage_path.data, dot);
    memcpy(buf + dot, ext.data, ext.len);
    return str8_from_parts(buf, dot + ext.len);
}

static void load_stage_preview_meta(Street_Carlos_Ctx* ctx, Street_Carlos_Stage_Preview* preview, str8 stage_path)
{
    MEL_UNUSED(ctx);
    preview->preview_focus = mel_vec2(0.5f, 0.5f);
    preview->preview_zoom = 1.0f;

    i64 fsize = 0;
    u8* data = mel_vfs_read_file(stage_path, &fsize, mel_alloc_heap());
    if (data)
    {
        Mugen_Stage stage_def = {0};
        if (mugen_stage_load(&stage_def, str8_from_parts(data, (size)fsize), mel_alloc_heap()))
        {
            if (stage_def.camera_startx != 0 || stage_def.camera_starty != 0)
            {
                preview->preview_focus = mel_vec2(
                    stage_def.camera_startx / 640.0f,
                    stage_def.camera_starty / 480.0f);
            }
            mugen_stage_shutdown(&stage_def, mel_alloc_heap());
        }
        mel_dealloc(mel_alloc_heap(), data);
    }
}

static void load_stage_preview(Street_Carlos_Ctx* ctx, Street_Carlos_Stage_Preview* preview, str8 stage_path)
{
    load_stage_preview_meta(ctx, preview, stage_path);
}

static void street_carlos_stage_select_stage_start(Mel_Stage* base, void* user)
{
    MEL_UNUSED(base);
    Street_Carlos_Stage_Select_Stage* stage = user;
    stage->cursor = 0;
}

void street_carlos_stage_select_stage_init(Street_Carlos_Stage_Select_Stage* stage, Street_Carlos_Ctx* ctx)
{
    stage->preview_count = ctx->stage_choice_count;
    if (stage->preview_count > 0)
    {
        stage->previews = mel_alloc(mel_alloc_heap(), sizeof(*stage->previews) * stage->preview_count);
        memset(stage->previews, 0, sizeof(*stage->previews) * stage->preview_count);

        for (u32 i = 0; i < stage->preview_count; i++)
            load_stage_preview(ctx, &stage->previews[i], ctx->stage_choices[i].path);
    }

    mel_stage_init(&stage->stage,
        .on_start = street_carlos_stage_select_stage_start,
        .user = stage,
        .start_enabled = false);
}

void street_carlos_stage_select_stage_shutdown(Street_Carlos_Stage_Select_Stage* stage)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    for (u32 i = 0; i < stage->preview_count; i++)
    {
        if (mel_slotmap_handle_valid(stage->previews[i].handle.handle))
            mel_gpu_texture_shutdown(&stage->previews[i].texture, dev);
        if (stage->previews[i].sff.atlas_pixels)
            mugen_sff_shutdown(&stage->previews[i].sff, mel_alloc_heap());
        mugen_stage_shutdown(&stage->previews[i].stage, mel_alloc_heap());
    }
    if (stage->previews)
        mel_dealloc(mel_alloc_heap(), stage->previews);
    mel_stage_shutdown(&stage->stage);
}

bool street_carlos_stage_select_stage_handle_event(Street_Carlos_Stage_Select_Stage* stage, Street_Carlos_Ctx* ctx, SDL_Event* event)
{
    if (!mel_stage_is_enabled(&stage->stage) || event->type != SDL_EVENT_KEY_DOWN || event->key.repeat)
        return false;

    if (ctx->stage_choice_count == 0)
        return false;

    if (event->key.scancode == SDL_SCANCODE_BACKSPACE)
    {
        street_carlos_show_char_select(ctx);
        return true;
    }

    const u32 cols = ctx->stage_choice_count < 3 ? ctx->stage_choice_count : 3;
    if (event->key.scancode == SDL_SCANCODE_LEFT) { stage->cursor = move_grid_cursor(stage->cursor, ctx->stage_choice_count, cols, -1, 0); return true; }
    if (event->key.scancode == SDL_SCANCODE_RIGHT) { stage->cursor = move_grid_cursor(stage->cursor, ctx->stage_choice_count, cols, 1, 0); return true; }
    if (event->key.scancode == SDL_SCANCODE_UP) { stage->cursor = move_grid_cursor(stage->cursor, ctx->stage_choice_count, cols, 0, -1); return true; }
    if (event->key.scancode == SDL_SCANCODE_DOWN) { stage->cursor = move_grid_cursor(stage->cursor, ctx->stage_choice_count, cols, 0, 1); return true; }
    if (event->key.scancode != SDL_SCANCODE_RETURN)
        return false;

    Mugen_Char* p1 = mugen_roster_at(&ctx->roster, ctx->char_select_stage->p1_cursor);
    Mugen_Char* p2 = mugen_roster_at(&ctx->roster, ctx->char_select_stage->p2_cursor);
    street_carlos_start_loading(ctx, p1, p2, ctx->stage_choices[stage->cursor].path);
    return true;
}

void street_carlos_stage_select_stage_draw_world(Street_Carlos_Stage_Select_Stage* stage, Street_Carlos_Ctx* ctx, Mel_Render_List* list)
{
    if (!mel_stage_is_enabled(&stage->stage))
        return;

    draw_centered_text(ctx, list, ctx->title_font, S8("SELECT STAGE"), 10.0f, mel_vec4(1.0f, 0.3f, 0.3f, 1.0f));

    if (ctx->stage_choice_count == 0)
    {
        draw_centered_text(ctx, list, ctx->ui_font, S8("No stages found in /stages"), 96.0f, mel_vec4(1, 1, 1, 1));
        return;
    }

    mel_draw_sprite(list, .pos = mel_vec2(12.0f, 30.0f), .size = mel_vec2(360.0f, 94.0f), .color = mel_vec4(0.08f, 0.08f, 0.12f, 1.0f));
    Street_Carlos_Stage_Preview* preview = &stage->previews[stage->cursor];
    if (preview->loaded)
        game_draw_stage_preview_framed(&preview->stage, &preview->sff, preview->handle,
            mel_rect(16.0f, 34.0f, 352.0f, 86.0f), preview->preview_focus, preview->preview_zoom, list);

    draw_centered_text(ctx, list, ctx->ui_font, ctx->stage_choices[stage->cursor].label, 124.0f, mel_vec4(1, 1, 1, 1));

    const u32 cols = ctx->stage_choice_count < 3 ? ctx->stage_choice_count : 3;
    const f32 cell_w = 108.0f;
    const f32 cell_h = 44.0f;
    const f32 start_x = 24.0f;
    const f32 start_y = 146.0f;
    for (u32 i = 0; i < ctx->stage_choice_count; i++)
    {
        u32 col = i % cols;
        u32 row = i / cols;
        f32 x = start_x + (f32)col * (cell_w + 8.0f);
        f32 y = start_y + (f32)row * (cell_h + 8.0f);

        mel_draw_sprite(list, .pos = mel_vec2(x, y), .size = mel_vec2(cell_w, cell_h), .color = mel_vec4(0.12f, 0.12f, 0.16f, 1.0f));
        Street_Carlos_Stage_Preview* tile_preview = &stage->previews[i];
        if (tile_preview->loaded)
            game_draw_stage_preview_framed(&tile_preview->stage, &tile_preview->sff, tile_preview->handle,
                mel_rect(x + 4.0f, y + 4.0f, cell_w - 8.0f, 24.0f), tile_preview->preview_focus, tile_preview->preview_zoom, list);
        if (i == stage->cursor)
            draw_box_outline(list, x, y, cell_w, cell_h, mel_vec4(1.0f, 1.0f, 0.4f, 1.0f));

        Mel_Vec2 size = mel_font_atlas_measure_text(ctx->ui_font, ctx->stage_choices[i].label);
        mel_font_atlas_draw_text(ctx->ui_font, list, ctx->stage_choices[i].label,
            x + (cell_w - size.x) * 0.5f, y + 28.0f, mel_vec4(1, 1, 1, 1));
    }

    draw_centered_text(ctx, list, ctx->ui_font, S8("ARROWS + ENTER"), 210.0f, mel_vec4(0.8f, 0.8f, 0.8f, 1.0f));
    draw_centered_text(ctx, list, ctx->ui_font, S8("BACKSPACE to return"), 226.0f, mel_vec4(0.8f, 0.8f, 0.8f, 1.0f));
}
