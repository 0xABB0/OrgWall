#include "editor.tiles.h"
#include "tile.set.h"
#include "tile.map.h"
#include "texture.pool.h"
#include "string.str8.h"

#include <cimgui/cimgui.h>
#include <string.h>
#include <stdlib.h>
#include <SDL3/SDL.h>

#ifndef IM_COL32
#define IM_COL32(R,G,B,A) (((ImU32)(A)<<24) | ((ImU32)(B)<<16) | ((ImU32)(G)<<8) | ((ImU32)(R)))
#endif

static void draw_tile_palette(Mel_EdTiles* ed);
static void draw_map_canvas(Mel_EdTiles* ed);
static void draw_layer_panel(Mel_EdTiles* ed);
static void draw_brush_panel(Mel_EdTiles* ed);
static void draw_toolbar(Mel_EdTiles* ed);
static void draw_tileset_panel(Mel_EdTiles* ed);
static void draw_tilemap_panel(Mel_EdTiles* ed);
static void draw_import_dialog(Mel_EdTiles* ed);

void mel_ed_tiles_init(Mel_EdTiles* ed, const Mel_Alloc* alloc)
{
    assert(ed != nullptr);

    *ed = (Mel_EdTiles){0};
    ed->alloc = alloc;
    ed->selected_layer = 0;
    ed->tool = MEL_ED_TILES_TOOL_BRUSH;
    ed->zoom = 1.0f;
    ed->show_grid = true;
    ed->last_paint_x = -1;
    ed->last_paint_y = -1;

    ed->new_tilemap_width = 20;
    ed->new_tilemap_height = 15;
    ed->new_tilemap_grid_w = 16;
    ed->new_tilemap_grid_h = 16;

    ed->import_tile_width = 16;
    ed->import_tile_height = 16;
    ed->import_padding = 0;
    ed->import_margin = 0;
    ed->import_preview_texture = MEL_TEXTURE_HANDLE_NULL;

    strcpy(ed->new_layer_name, "New Layer");
    ed->renaming_layer = -1;
}

void mel_ed_tiles_shutdown(Mel_EdTiles* ed)
{
    assert(ed != nullptr);

    if (ed->brush.current.tiles)
    {
        mel_dealloc(ed->alloc, ed->brush.current.tiles);
    }

    for (u32 i = 0; i < ed->brush.history_count; i++)
    {
        if (ed->brush.history[i].tiles)
        {
            mel_dealloc(ed->alloc, ed->brush.history[i].tiles);
        }
    }

    *ed = (Mel_EdTiles){0};
}

void mel_ed_tiles_set_pools(Mel_EdTiles* ed, Mel_Tileset_Pool* ts_pool, Mel_Tilemap_Pool* tm_pool, Mel_Texture_Pool* tex_pool)
{
    assert(ed != nullptr);
    ed->tileset_pool = ts_pool;
    ed->tilemap_pool = tm_pool;
    ed->texture_pool = tex_pool;
}

void mel_ed_tiles_set_tileset(Mel_EdTiles* ed, Mel_Tileset_Handle handle)
{
    assert(ed != nullptr);

    ed->tileset_handle = handle;
    ed->tileset = mel_tileset_pool_get(ed->tileset_pool, handle);
    ed->tileset_path[0] = '\0';
}

void mel_ed_tiles_set_tilemap(Mel_EdTiles* ed, Mel_Tilemap_Handle handle)
{
    assert(ed != nullptr);
    ed->tilemap_handle = handle;
    ed->tilemap = mel_tilemap_pool_get(ed->tilemap_pool, handle);
    ed->selected_layer = 0;
    ed->scroll_x = 0;
    ed->scroll_y = 0;
    ed->dirty = false;
    ed->tilemap_path[0] = '\0';

    if (ed->tilemap && ed->tilemap->tileset.value)
    {
        ed->tileset_handle = ed->tilemap->tileset;
        ed->tileset = mel_tileset_pool_get(ed->tileset_pool, ed->tileset_handle);
    }
}

void mel_ed_tiles_draw(Mel_EdTiles* ed)
{
    assert(ed != nullptr);

    if (ed->show_import_dialog)
    {
        draw_import_dialog(ed);
    }

    draw_toolbar(ed);

    igSeparator();

    igColumns(3, "TileEditorColumns", true);

    igSetColumnWidth(0, 220);
    igSetColumnWidth(2, 200);

    draw_tileset_panel(ed);
    draw_tile_palette(ed);
    draw_brush_panel(ed);

    igNextColumn();

    draw_tilemap_panel(ed);
    draw_map_canvas(ed);

    igNextColumn();

    draw_layer_panel(ed);

    igColumns(1, nullptr, false);

    igSeparator();

    if (ed->dirty)
    {
        igTextColored((ImVec4){1.0f, 1.0f, 0.0f, 1.0f}, "* Unsaved changes");
        igSameLine(0, 10);
    }

    if (igButton("Save All", (ImVec2){100, 0}))
    {
        if (ed->tilemap && ed->tilemap_pool && strlen(ed->tilemap_path) > 0)
        {
            if (mel_tilemap_pool_save(ed->tilemap_pool, ed->tilemap_handle, str8_from_cstr(ed->tilemap_path)))
            {
                ed->dirty = false;
                SDL_Log("Saved tilemap: %s", ed->tilemap_path);
            }
        }
        if (ed->tileset && ed->tileset_pool && strlen(ed->tileset_path) > 0)
        {
            mel_tileset_pool_save(ed->tileset_pool, ed->tileset_handle, str8_from_cstr(ed->tileset_path));
            SDL_Log("Saved tileset: %s", ed->tileset_path);
        }
    }
}

static void draw_toolbar(Mel_EdTiles* ed)
{
    const char* tool_labels[] = { "B:Brush", "Q:Select", "E:Erase", "G:Fill" };
    const char* tool_tips[] = { "Paint tiles", "Rectangle select", "Erase tiles", "Flood fill" };

    for (i32 i = 0; i < 4; i++)
    {
        if (i > 0) igSameLine(0, 5);

        bool selected = (ed->tool == i);
        if (selected)
        {
            igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.4f, 0.6f, 0.8f, 1.0f});
        }

        if (igButton(tool_labels[i], (ImVec2){70, 0}))
        {
            ed->tool = i;
        }

        if (selected)
        {
            igPopStyleColor(1);
        }

        if (igIsItemHovered(0))
        {
            igSetTooltip("%s", tool_tips[i]);
        }
    }

    igSameLine(0, 20);

    if (igButton("F:FlipH", (ImVec2){60, 0}))
    {
        mel_ed_tiles_brush_flip_h(ed);
    }
    igSameLine(0, 5);
    if (igButton("V:FlipV", (ImVec2){60, 0}))
    {
        mel_ed_tiles_brush_flip_v(ed);
    }
    igSameLine(0, 5);
    if (igButton("R:Rot", (ImVec2){50, 0}))
    {
        mel_ed_tiles_brush_rotate_cw(ed);
    }

    ImGuiIO* io = igGetIO_Nil();
    if (!io->WantTextInput)
    {
        if (igIsKeyPressed_Bool(ImGuiKey_B, false)) ed->tool = MEL_ED_TILES_TOOL_BRUSH;
        if (igIsKeyPressed_Bool(ImGuiKey_Q, false)) ed->tool = MEL_ED_TILES_TOOL_SELECT;
        if (igIsKeyPressed_Bool(ImGuiKey_E, false)) ed->tool = MEL_ED_TILES_TOOL_ERASE;
        if (igIsKeyPressed_Bool(ImGuiKey_G, false)) ed->tool = MEL_ED_TILES_TOOL_FILL;
        if (igIsKeyPressed_Bool(ImGuiKey_F, false)) mel_ed_tiles_brush_flip_h(ed);
        if (igIsKeyPressed_Bool(ImGuiKey_V, false)) mel_ed_tiles_brush_flip_v(ed);
        if (igIsKeyPressed_Bool(ImGuiKey_R, false)) mel_ed_tiles_brush_rotate_cw(ed);
    }
}

static void draw_tileset_panel(Mel_EdTiles* ed)
{
    igText("Tileset");

    if (ed->tileset && !str8_is_empty(ed->tileset->name))
        igText("Current: %.*s", (int)ed->tileset->name.len, ed->tileset->name.data);
    else
        igText("Current: (none)");

    if (ed->tileset)
    {
        igText("%u sources, %u tiles", ed->tileset->source_count, ed->tileset->tile_count);
    }

    if (igButton("New##tileset", (ImVec2){60, 0}))
    {
        igOpenPopup_Str("NewTileset", 0);
    }
    igSameLine(0, 5);
    if (igButton("Import##tileset", (ImVec2){60, 0}))
    {
        ed->show_import_dialog = true;
    }

    if (igBeginPopup("NewTileset", 0))
    {
        igText("Create New Tileset");
        igInputText("Name##newtileset", ed->new_tile_visual_name, sizeof(ed->new_tile_visual_name), 0, nullptr, nullptr);

        if (igButton("Create##newtileset", (ImVec2){100, 0}))
        {
            if (ed->tileset_pool && strlen(ed->new_tile_visual_name) > 0)
            {
                Mel_Tileset_Handle handle = mel_tileset_pool_create(ed->tileset_pool, str8_from_cstr(ed->new_tile_visual_name));
                if (handle.value)
                {
                    char path[256];
                    snprintf(path, sizeof(path), "%s.json", ed->new_tile_visual_name);
                    mel_ed_tiles_set_tileset(ed, handle);
                    strncpy(ed->tileset_path, path, sizeof(ed->tileset_path) - 1);
                    ed->new_tile_visual_name[0] = '\0';
                }
            }
            igCloseCurrentPopup();
        }
        igEndPopup();
    }

    igSeparator();
}

static void draw_tile_palette(Mel_EdTiles* ed)
{
    igText("Tile Palette");

    if (!ed->tileset || ed->tileset->tile_count == 0)
    {
        igTextDisabled("No tiles available");
        return;
    }

    if (igBeginChild_Str("TilePalette", (ImVec2){0, 200}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
    {
        ImVec2_c cursor_pos = igGetCursorScreenPos();
        ImDrawList* draw_list = igGetWindowDrawList();

        f32 tile_display_size = 32.0f;
        u32 cols = 6;

        for (u32 i = 0; i < ed->tileset->tile_count; i++)
        {
            u32 tx = i % cols;
            u32 ty = i / cols;

            ImVec2 p0 = {cursor_pos.x + tx * tile_display_size, cursor_pos.y + ty * tile_display_size};
            ImVec2 p1 = {p0.x + tile_display_size, p0.y + tile_display_size};

            Mel_Tile_Def* entry = &ed->tileset->tiles[i];
            bool selected = (ed->brush.current.tiles &&
                             ed->brush.current.width == 1 &&
                             ed->brush.current.height == 1 &&
                             ed->brush.current.tiles[0] == (i32)entry->id);

            if (entry->source_idx < ed->tileset->source_count)
            {
                Mel_Tile_Source* source = &ed->tileset->sources[entry->source_idx];
                Mel_Gpu_Texture* tex = mel_texture_pool_get(ed->texture_pool, source->texture);
                if (tex && tex->descriptor)
                {
                    f32 tex_w = (f32)tex->image.width;
                    f32 tex_h = (f32)tex->image.height;

                    f32 u0 = (f32)entry->source_x / tex_w;
                    f32 v0 = (f32)entry->source_y / tex_h;
                    f32 u1 = (f32)(entry->source_x + entry->width) / tex_w;
                    f32 v1 = (f32)(entry->source_y + entry->height) / tex_h;

                    ImTextureRef_c tex_ref = {._TexData = nullptr, ._TexID = (ImTextureID)tex->descriptor};
                    ImDrawList_AddImage(draw_list, tex_ref,
                        p0, p1, (ImVec2){u0, v0}, (ImVec2){u1, v1}, IM_COL32(255, 255, 255, 255));
                }
                else
                {
                    ImDrawList_AddRectFilled(draw_list, p0, p1, IM_COL32(60, 60, 80, 255), 0, 0);
                }
            }

            if (selected)
            {
                ImDrawList_AddRect(draw_list, p0, p1, IM_COL32(255, 200, 50, 255), 0, 0, 2.0f);
            }
            else
            {
                ImDrawList_AddRect(draw_list, p0, p1, IM_COL32(100, 100, 100, 255), 0, 0, 1.0f);
            }
        }

        ImGuiIO* io = igGetIO_Nil();
        if (igIsWindowHovered(0) && io->MouseClicked[0])
        {
            ImVec2_c mouse_pos = igGetMousePos();
            f32 rel_x = mouse_pos.x - cursor_pos.x;
            f32 rel_y = mouse_pos.y - cursor_pos.y;

            if (rel_x >= 0 && rel_y >= 0)
            {
                i32 tx = (i32)(rel_x / tile_display_size);
                i32 ty = (i32)(rel_y / tile_display_size);
                u32 idx = ty * cols + tx;

                if (idx < ed->tileset->tile_count)
                {
                    mel_ed_tiles_brush_set_single(ed, (i32)ed->tileset->tiles[idx].id);
                }
            }
        }

        u32 rows = (ed->tileset->tile_count + cols - 1) / cols;
        igDummy((ImVec2){(f32)cols * tile_display_size, (f32)rows * tile_display_size});
    }
    igEndChild();
}

static void draw_brush_preview(Mel_EdTiles* ed, Mel_TileBrush* brush, f32 max_size)
{
    if (!brush || !brush->tiles || brush->width == 0 || brush->height == 0 || !ed->tileset)
    {
        igTextDisabled("Empty");
        return;
    }

    f32 tile_size = max_size / (brush->width > brush->height ? brush->width : brush->height);
    if (tile_size > 24.0f) tile_size = 24.0f;

    ImVec2_c cursor_pos = igGetCursorScreenPos();
    ImDrawList* draw_list = igGetWindowDrawList();

    f32 preview_w = brush->width * tile_size;
    f32 preview_h = brush->height * tile_size;

    ImDrawList_AddRectFilled(draw_list,
        (ImVec2){cursor_pos.x, cursor_pos.y},
        (ImVec2){cursor_pos.x + preview_w, cursor_pos.y + preview_h},
        IM_COL32(40, 40, 50, 255), 0, 0);

    for (u32 by = 0; by < brush->height; by++)
    {
        for (u32 bx = 0; bx < brush->width; bx++)
        {
            i32 tile_id = brush->tiles[by * brush->width + bx];
            if (tile_id < 0) continue;

            ImVec2 p0 = {cursor_pos.x + bx * tile_size, cursor_pos.y + by * tile_size};
            ImVec2 p1 = {p0.x + tile_size, p0.y + tile_size};

            Mel_Tile_Def* entry = mel_tileset_entry_get_tile(ed->tileset, (u32)tile_id);
            if (entry && entry->source_idx < ed->tileset->source_count)
            {
                Mel_Tile_Source* source = &ed->tileset->sources[entry->source_idx];
                Mel_Gpu_Texture* tex = mel_texture_pool_get(ed->texture_pool, source->texture);
                if (tex && tex->descriptor)
                {
                    f32 tex_w = (f32)tex->image.width;
                    f32 tex_h = (f32)tex->image.height;
                    f32 u0 = (f32)entry->source_x / tex_w;
                    f32 v0 = (f32)entry->source_y / tex_h;
                    f32 u1 = (f32)(entry->source_x + entry->width) / tex_w;
                    f32 v1 = (f32)(entry->source_y + entry->height) / tex_h;

                    ImTextureRef_c tex_ref = {._TexData = nullptr, ._TexID = (ImTextureID)tex->descriptor};
                    ImDrawList_AddImage(draw_list, tex_ref, p0, p1, (ImVec2){u0, v0}, (ImVec2){u1, v1}, IM_COL32(255, 255, 255, 255));
                }
            }
        }
    }

    igDummy((ImVec2){preview_w, preview_h});
}

static void draw_brush_panel(Mel_EdTiles* ed)
{
    igSeparator();
    igText("Brush");

    if (ed->brush.current.tiles && ed->brush.current.width > 0 && ed->brush.current.height > 0)
    {
        igText("Size: %ux%u", ed->brush.current.width, ed->brush.current.height);
        draw_brush_preview(ed, &ed->brush.current, 80.0f);

        if (ed->brush.flip_h || ed->brush.flip_v || ed->brush.rotation > 0)
        {
            igText("%s%s%s",
                ed->brush.flip_h ? "[H] " : "",
                ed->brush.flip_v ? "[V] " : "",
                ed->brush.rotation > 0 ? "[R]" : "");
        }
    }
    else
    {
        igTextDisabled("No brush selected");
    }

    if (ed->brush.history_count > 0)
    {
        igSeparator();
        igText("History");

        f32 history_tile_size = 48.0f;

        for (u32 i = 0; i < ed->brush.history_count && i < 6; i++)
        {
            if (i > 0 && i % 3 != 0) igSameLine(0, 5);

            igPushID_Int((int)i);

            ImVec2_c btn_pos = igGetCursorScreenPos();
            if (igInvisibleButton("##histbtn", (ImVec2){history_tile_size, history_tile_size}, 0))
            {
                if (ed->brush.current.tiles)
                {
                    mel_dealloc(ed->alloc, ed->brush.current.tiles);
                }

                u32 count = ed->brush.history[i].width * ed->brush.history[i].height;
                ed->brush.current.tiles = mel_alloc(ed->alloc, count * sizeof(i32));
                memcpy(ed->brush.current.tiles, ed->brush.history[i].tiles, count * sizeof(i32));
                ed->brush.current.width = ed->brush.history[i].width;
                ed->brush.current.height = ed->brush.history[i].height;
                ed->brush.flip_h = false;
                ed->brush.flip_v = false;
                ed->brush.rotation = 0;
            }
            if (igIsItemHovered(0))
            {
                igSetTooltip("%ux%u", ed->brush.history[i].width, ed->brush.history[i].height);
            }

            Mel_TileBrush* hist = &ed->brush.history[i];
            if (hist->tiles && hist->width > 0 && hist->height > 0 && ed->tileset)
            {
                f32 tile_size = history_tile_size / (hist->width > hist->height ? hist->width : hist->height);
                if (tile_size > 24.0f) tile_size = 24.0f;

                ImDrawList* draw_list = igGetWindowDrawList();
                ImDrawList_AddRectFilled(draw_list,
                    (ImVec2){btn_pos.x, btn_pos.y},
                    (ImVec2){btn_pos.x + hist->width * tile_size, btn_pos.y + hist->height * tile_size},
                    IM_COL32(40, 40, 50, 255), 0, 0);

                for (u32 by = 0; by < hist->height; by++)
                {
                    for (u32 bx = 0; bx < hist->width; bx++)
                    {
                        i32 tile_id = hist->tiles[by * hist->width + bx];
                        if (tile_id < 0) continue;

                        ImVec2 p0 = {btn_pos.x + bx * tile_size, btn_pos.y + by * tile_size};
                        ImVec2 p1 = {p0.x + tile_size, p0.y + tile_size};

                        Mel_Tile_Def* entry = mel_tileset_entry_get_tile(ed->tileset, (u32)tile_id);
                        if (entry && entry->source_idx < ed->tileset->source_count)
                        {
                            Mel_Tile_Source* source = &ed->tileset->sources[entry->source_idx];
                            Mel_Gpu_Texture* tex = mel_texture_pool_get(ed->texture_pool, source->texture);
                            if (tex && tex->descriptor)
                            {
                                f32 tex_w = (f32)tex->image.width;
                                f32 tex_h = (f32)tex->image.height;
                                f32 u0 = (f32)entry->source_x / tex_w;
                                f32 v0 = (f32)entry->source_y / tex_h;
                                f32 u1 = (f32)(entry->source_x + entry->width) / tex_w;
                                f32 v1 = (f32)(entry->source_y + entry->height) / tex_h;

                                ImTextureRef_c tex_ref = {._TexData = nullptr, ._TexID = (ImTextureID)tex->descriptor};
                                ImDrawList_AddImage(draw_list, tex_ref, p0, p1, (ImVec2){u0, v0}, (ImVec2){u1, v1}, IM_COL32(255, 255, 255, 255));
                            }
                        }
                    }
                }
            }

            igPopID();
        }
    }
}

static void draw_tilemap_panel(Mel_EdTiles* ed)
{
    igText("Tilemap");

    if (ed->tilemap && !str8_is_empty(ed->tilemap->name))
        igText("Current: %.*s", (int)ed->tilemap->name.len, ed->tilemap->name.data);
    else
        igText("Current: (none)");

    if (ed->tilemap)
    {
        igText("%ux%u tiles, Grid: %ux%u", ed->tilemap->width, ed->tilemap->height,
            ed->tilemap->grid_width, ed->tilemap->grid_height);
    }

    if (igButton("New##tilemap", (ImVec2){60, 0}))
    {
        igOpenPopup_Str("NewTilemap", 0);
    }
    igSameLine(0, 5);
    igSliderFloat("Zoom", &ed->zoom, 0.25f, 4.0f, "%.2f", 0);

    igCheckbox("Grid", &ed->show_grid);

    if (igBeginPopup("NewTilemap", 0))
    {
        igText("Create New Tilemap");
        igInputText("Name##newtilemap", ed->new_tilemap_name, sizeof(ed->new_tilemap_name), 0, nullptr, nullptr);
        igInputInt("Width##newtilemap", &ed->new_tilemap_width, 1, 10, 0);
        igInputInt("Height##newtilemap", &ed->new_tilemap_height, 1, 10, 0);
        igInputInt("Grid W##newtilemap", &ed->new_tilemap_grid_w, 1, 8, 0);
        igInputInt("Grid H##newtilemap", &ed->new_tilemap_grid_h, 1, 8, 0);

        if (ed->new_tilemap_width < 1) ed->new_tilemap_width = 1;
        if (ed->new_tilemap_height < 1) ed->new_tilemap_height = 1;
        if (ed->new_tilemap_grid_w < 1) ed->new_tilemap_grid_w = 1;
        if (ed->new_tilemap_grid_h < 1) ed->new_tilemap_grid_h = 1;

        if (igButton("Create##newtilemap", (ImVec2){100, 0}))
        {
            if (ed->tilemap_pool && strlen(ed->new_tilemap_name) > 0)
            {
                Mel_Tilemap_Handle handle = mel_tilemap_pool_create(ed->tilemap_pool,
                    str8_from_cstr(ed->new_tilemap_name),
                    (u32)ed->new_tilemap_width, (u32)ed->new_tilemap_height,
                    (u32)ed->new_tilemap_grid_w, (u32)ed->new_tilemap_grid_h);

                if (handle.value)
                {
                    Mel_Tilemap_Entry* tilemap = mel_tilemap_pool_get(ed->tilemap_pool, handle);
                    if (tilemap && ed->tileset)
                    {
                        tilemap->tileset = ed->tileset_handle;
                    }

                    mel_ed_tiles_set_tilemap(ed, handle);
                    ed->new_tilemap_name[0] = '\0';
                    ed->dirty = true;
                }
            }
            igCloseCurrentPopup();
        }
        igEndPopup();
    }

    igSeparator();
}

static void draw_map_canvas(Mel_EdTiles* ed)
{
    if (!ed->tilemap)
    {
        igTextDisabled("No tilemap loaded");
        return;
    }

    if (igBeginChild_Str("MapCanvas", (ImVec2){0, 0}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
    {
        ImVec2_c cursor_pos = igGetCursorScreenPos();
        ImDrawList* draw_list = igGetWindowDrawList();

        f32 tile_size = (f32)ed->tilemap->grid_width * ed->zoom;
        u32 map_w = ed->tilemap->width;
        u32 map_h = ed->tilemap->height;

        ImDrawList_AddRectFilled(draw_list,
            (ImVec2){cursor_pos.x - ed->scroll_x, cursor_pos.y - ed->scroll_y},
            (ImVec2){cursor_pos.x - ed->scroll_x + map_w * tile_size, cursor_pos.y - ed->scroll_y + map_h * tile_size},
            IM_COL32(30, 30, 40, 255), 0, 0);

        for (u32 l = 0; l < ed->tilemap->layer_count; l++)
        {
            Mel_Tilemap_Layer* layer = &ed->tilemap->layers[l];
            if (!layer->visible) continue;

            for (u32 y = 0; y < map_h; y++)
            {
                for (u32 x = 0; x < map_w; x++)
                {
                    i32 tile_id = layer->data[y * map_w + x];
                    if (tile_id < 0) continue;

                    ImVec2 p0 = {
                        cursor_pos.x + (f32)x * tile_size - ed->scroll_x + layer->offset_x * ed->zoom,
                        cursor_pos.y + (f32)y * tile_size - ed->scroll_y + layer->offset_y * ed->zoom
                    };
                    ImVec2 p1 = {p0.x + tile_size, p0.y + tile_size};

                    if (ed->tileset)
                    {
                        Mel_Tile_Def* entry = mel_tileset_entry_get_tile(ed->tileset, (u32)tile_id);
                        if (entry && entry->source_idx < ed->tileset->source_count)
                        {
                            Mel_Tile_Source* source = &ed->tileset->sources[entry->source_idx];
                            Mel_Gpu_Texture* tex = mel_texture_pool_get(ed->texture_pool, source->texture);
                            if (tex && tex->descriptor)
                            {
                                f32 tex_w = (f32)tex->image.width;
                                f32 tex_h = (f32)tex->image.height;

                                f32 u0 = (f32)entry->source_x / tex_w;
                                f32 v0 = (f32)entry->source_y / tex_h;
                                f32 u1 = (f32)(entry->source_x + entry->width) / tex_w;
                                f32 v1 = (f32)(entry->source_y + entry->height) / tex_h;

                                ImTextureRef_c tex_ref = {._TexData = nullptr, ._TexID = (ImTextureID)tex->descriptor};
                                ImDrawList_AddImage(draw_list, tex_ref,
                                    p0, p1, (ImVec2){u0, v0}, (ImVec2){u1, v1}, IM_COL32(255, 255, 255, 255));
                            }
                        }
                    }
                    else
                    {
                        ImDrawList_AddRectFilled(draw_list, p0, p1, IM_COL32(80, 80, 100, 255), 0, 0);
                    }
                }
            }
        }

        if (ed->show_grid)
        {
            for (u32 y = 0; y <= map_h; y++)
            {
                ImVec2 start = {cursor_pos.x - ed->scroll_x, cursor_pos.y + (f32)y * tile_size - ed->scroll_y};
                ImVec2 end = {cursor_pos.x - ed->scroll_x + map_w * tile_size, start.y};
                ImDrawList_AddLine(draw_list, start, end, IM_COL32(80, 80, 80, 128), 1.0f);
            }
            for (u32 x = 0; x <= map_w; x++)
            {
                ImVec2 start = {cursor_pos.x + (f32)x * tile_size - ed->scroll_x, cursor_pos.y - ed->scroll_y};
                ImVec2 end = {start.x, cursor_pos.y - ed->scroll_y + map_h * tile_size};
                ImDrawList_AddLine(draw_list, start, end, IM_COL32(80, 80, 80, 128), 1.0f);
            }
        }

        if (ed->selection.active)
        {
            i32 sx = ed->selection.start_x < ed->selection.end_x ? ed->selection.start_x : ed->selection.end_x;
            i32 sy = ed->selection.start_y < ed->selection.end_y ? ed->selection.start_y : ed->selection.end_y;
            i32 ex = ed->selection.start_x > ed->selection.end_x ? ed->selection.start_x : ed->selection.end_x;
            i32 ey = ed->selection.start_y > ed->selection.end_y ? ed->selection.start_y : ed->selection.end_y;

            ImVec2 sel_p0 = {cursor_pos.x + sx * tile_size - ed->scroll_x, cursor_pos.y + sy * tile_size - ed->scroll_y};
            ImVec2 sel_p1 = {cursor_pos.x + (ex + 1) * tile_size - ed->scroll_x, cursor_pos.y + (ey + 1) * tile_size - ed->scroll_y};

            ImDrawList_AddRectFilled(draw_list, sel_p0, sel_p1, IM_COL32(100, 150, 255, 50), 0, 0);
            ImDrawList_AddRect(draw_list, sel_p0, sel_p1, IM_COL32(100, 150, 255, 255), 0, 0, 2.0f);
        }

        ImGuiIO* io = igGetIO_Nil();
        if (igIsWindowHovered(0))
        {
            ImVec2_c mouse_pos = igGetMousePos();
            f32 rel_x = mouse_pos.x - cursor_pos.x + ed->scroll_x;
            f32 rel_y = mouse_pos.y - cursor_pos.y + ed->scroll_y;

            i32 tx = (i32)(rel_x / tile_size);
            i32 ty = (i32)(rel_y / tile_size);

            if (tx >= 0 && ty >= 0 && (u32)tx < map_w && (u32)ty < map_h)
            {
                ImVec2 hover_p0 = {cursor_pos.x + tx * tile_size - ed->scroll_x, cursor_pos.y + ty * tile_size - ed->scroll_y};
                ImVec2 hover_p1 = {hover_p0.x + tile_size, hover_p0.y + tile_size};
                ImDrawList_AddRect(draw_list, hover_p0, hover_p1, IM_COL32(255, 255, 100, 200), 0, 0, 2.0f);

                if (ed->tool == MEL_ED_TILES_TOOL_BRUSH)
                {
                    if (io->MouseClicked[0] || (io->MouseDown[0] && (tx != ed->last_paint_x || ty != ed->last_paint_y)))
                    {
                        mel_ed_tiles_paint(ed, tx, ty);
                        ed->last_paint_x = tx;
                        ed->last_paint_y = ty;
                        ed->painting = true;
                    }
                }
                else if (ed->tool == MEL_ED_TILES_TOOL_ERASE)
                {
                    if (io->MouseClicked[0] || (io->MouseDown[0] && (tx != ed->last_paint_x || ty != ed->last_paint_y)))
                    {
                        mel_ed_tiles_erase(ed, tx, ty);
                        ed->last_paint_x = tx;
                        ed->last_paint_y = ty;
                    }
                }
                else if (ed->tool == MEL_ED_TILES_TOOL_SELECT)
                {
                    if (io->MouseClicked[0])
                    {
                        ed->selection.active = true;
                        ed->selection.start_x = tx;
                        ed->selection.start_y = ty;
                        ed->selection.end_x = tx;
                        ed->selection.end_y = ty;
                    }
                    else if (io->MouseDown[0] && ed->selection.active)
                    {
                        ed->selection.end_x = tx;
                        ed->selection.end_y = ty;
                    }
                }
                else if (ed->tool == MEL_ED_TILES_TOOL_FILL)
                {
                    if (io->MouseClicked[0])
                    {
                        mel_ed_tiles_fill(ed, tx, ty);
                    }
                }
            }

            if (!io->MouseDown[0])
            {
                ed->painting = false;
                ed->last_paint_x = -1;
                ed->last_paint_y = -1;
            }

            if (io->MouseDown[2])
            {
                ed->scroll_x -= io->MouseDelta.x;
                ed->scroll_y -= io->MouseDelta.y;
            }

            ed->zoom += io->MouseWheel * 0.1f;
            if (ed->zoom < 0.25f) ed->zoom = 0.25f;
            if (ed->zoom > 4.0f) ed->zoom = 4.0f;

            if (ed->selection.active && !io->MouseDown[0])
            {
                if (io->KeyCtrl && igIsKeyPressed_Bool(ImGuiKey_C, false))
                {
                    mel_ed_tiles_brush_from_selection(ed);
                }
                if (io->KeyCtrl && igIsKeyPressed_Bool(ImGuiKey_X, false))
                {
                    i32 sx = ed->selection.start_x < ed->selection.end_x ? ed->selection.start_x : ed->selection.end_x;
                    i32 sy = ed->selection.start_y < ed->selection.end_y ? ed->selection.start_y : ed->selection.end_y;
                    i32 ex = ed->selection.start_x > ed->selection.end_x ? ed->selection.start_x : ed->selection.end_x;
                    i32 ey = ed->selection.start_y > ed->selection.end_y ? ed->selection.start_y : ed->selection.end_y;

                    mel_ed_tiles_brush_from_selection(ed);

                    if (ed->selected_layer >= 0 && (u32)ed->selected_layer < ed->tilemap->layer_count)
                    {
                        Mel_Tilemap_Layer* layer = &ed->tilemap->layers[ed->selected_layer];
                        for (i32 cy = sy; cy <= ey; cy++)
                        {
                            for (i32 cx = sx; cx <= ex; cx++)
                            {
                                if (cx >= 0 && cy >= 0 && (u32)cx < ed->tilemap->width && (u32)cy < ed->tilemap->height)
                                {
                                    layer->data[cy * ed->tilemap->width + cx] = -1;
                                }
                            }
                        }
                        ed->dirty = true;
                    }
                }
            }
        }

        igDummy((ImVec2){map_w * tile_size, map_h * tile_size});
    }
    igEndChild();
}

static void draw_layer_panel(Mel_EdTiles* ed)
{
    igText("Layers");

    if (!ed->tilemap)
    {
        igTextDisabled("No tilemap loaded");
        return;
    }

    if (igButton("+##addlayer", (ImVec2){25, 0}))
    {
        igOpenPopup_Str("AddLayer", 0);
    }
    igSameLine(0, 5);
    if (igButton("-##dellayer", (ImVec2){25, 0}))
    {
        if (ed->selected_layer >= 0 && (u32)ed->selected_layer < ed->tilemap->layer_count)
        {
            mel_tilemap_entry_remove_layer(ed->tilemap, (u32)ed->selected_layer);
            if (ed->selected_layer >= (i32)ed->tilemap->layer_count)
            {
                ed->selected_layer = (i32)ed->tilemap->layer_count - 1;
            }
            ed->dirty = true;
        }
    }
    igSameLine(0, 5);
    if (igButton("^##layerup", (ImVec2){25, 0}))
    {
        if (ed->selected_layer > 0)
        {
            mel_tilemap_entry_move_layer(ed->tilemap, (u32)ed->selected_layer, (u32)(ed->selected_layer - 1));
            ed->selected_layer--;
            ed->dirty = true;
        }
    }
    igSameLine(0, 5);
    if (igButton("v##layerdown", (ImVec2){25, 0}))
    {
        if (ed->selected_layer >= 0 && (u32)ed->selected_layer < ed->tilemap->layer_count - 1)
        {
            mel_tilemap_entry_move_layer(ed->tilemap, (u32)ed->selected_layer, (u32)(ed->selected_layer + 1));
            ed->selected_layer++;
            ed->dirty = true;
        }
    }

    if (igBeginPopup("AddLayer", 0))
    {
        igInputText("Name##newlayer", ed->new_layer_name, sizeof(ed->new_layer_name), 0, nullptr, nullptr);
        if (igButton("Add##newlayer", (ImVec2){80, 0}))
        {
            mel_tilemap_entry_add_layer(ed->tilemap, str8_from_cstr(ed->new_layer_name));
            ed->dirty = true;
            igCloseCurrentPopup();
        }
        igEndPopup();
    }

    if (igBeginChild_Str("LayerList", (ImVec2){0, 200}, ImGuiChildFlags_Borders, 0))
    {
        for (u32 i = 0; i < ed->tilemap->layer_count; i++)
        {
            Mel_Tilemap_Layer* layer = &ed->tilemap->layers[i];

            igPushID_Int((int)i);

            if (igCheckbox("##vis", &layer->visible))
            {
                ed->dirty = true;
            }
            igSameLine(0, 5);

            if (ed->renaming_layer == (i32)i)
            {
                igSetKeyboardFocusHere(0);
                if (igInputText("##rename", ed->rename_layer_buffer, sizeof(ed->rename_layer_buffer),
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll, nullptr, nullptr))
                {
                    if (strlen(ed->rename_layer_buffer) > 0)
                    {
                        if (layer->name.data) mel_dealloc(ed->tilemap->alloc, layer->name.data);
                        layer->name = str8_dup(str8_from_cstr(ed->rename_layer_buffer), ed->tilemap->alloc);
                        ed->dirty = true;
                    }
                    ed->renaming_layer = -1;
                }
                if (!igIsItemActive() && !igIsItemFocused())
                {
                    ed->renaming_layer = -1;
                }
            }
            else
            {
                char label[128];
                char name_buf[64];
                if (!str8_is_empty(layer->name))
                    str8_to_buf(layer->name, name_buf, sizeof(name_buf));
                else
                    strncpy(name_buf, "(unnamed)", sizeof(name_buf));
                snprintf(label, sizeof(label), "%s###layer", name_buf);

                if (igSelectable_Bool(label, ed->selected_layer == (i32)i, ImGuiSelectableFlags_AllowDoubleClick, (ImVec2){0, 0}))
                {
                    ed->selected_layer = (i32)i;

                    if (igIsMouseDoubleClicked_Nil(0))
                    {
                        ed->renaming_layer = (i32)i;
                        if (!str8_is_empty(layer->name))
                            str8_to_buf(layer->name, ed->rename_layer_buffer, sizeof(ed->rename_layer_buffer));
                        else
                            ed->rename_layer_buffer[0] = '\0';
                    }
                }
            }

            igPopID();
        }
    }
    igEndChild();

    if (ed->selected_layer >= 0 && (u32)ed->selected_layer < ed->tilemap->layer_count)
    {
        Mel_Tilemap_Layer* layer = &ed->tilemap->layers[ed->selected_layer];

        igSeparator();
        igText("Layer Properties");

        igInputFloat("Parallax X", &layer->parallax_x, 0.1f, 0.5f, "%.2f", 0);
        if (igIsItemDeactivatedAfterEdit()) ed->dirty = true;
        igInputFloat("Parallax Y", &layer->parallax_y, 0.1f, 0.5f, "%.2f", 0);
        if (igIsItemDeactivatedAfterEdit()) ed->dirty = true;
        igInputInt("Offset X", &layer->offset_x, 1, 10, 0);
        if (igIsItemDeactivatedAfterEdit()) ed->dirty = true;
        igInputInt("Offset Y", &layer->offset_y, 1, 10, 0);
        if (igIsItemDeactivatedAfterEdit()) ed->dirty = true;
    }
}

static void draw_import_dialog(Mel_EdTiles* ed)
{
    igSetNextWindowSize((ImVec2){600, 500}, ImGuiCond_FirstUseEver);

    if (igBegin("Import Tile Sheet", &ed->show_import_dialog, 0))
    {
        igColumns(2, "ImportColumns", true);
        igSetColumnWidth(0, 250);

        igText("Source Texture:");
        igInputText("##importtexpath", ed->import_texture_path, sizeof(ed->import_texture_path), 0, nullptr, nullptr);

        if (igButton("Load Preview", (ImVec2){100, 0}))
        {
            if (ed->texture_pool && strlen(ed->import_texture_path) > 0)
            {
                ed->import_preview_texture = mel_texture_pool_load(ed->texture_pool, str8_from_cstr(ed->import_texture_path));
            }
        }

        igSeparator();
        igText("Grid Settings:");

        igInputInt("Tile Width", &ed->import_tile_width, 1, 8, 0);
        igInputInt("Tile Height", &ed->import_tile_height, 1, 8, 0);
        igInputInt("Columns (0=auto)", &ed->import_columns, 1, 8, 0);
        igInputInt("Rows (0=auto)", &ed->import_rows, 1, 8, 0);
        igInputInt("Padding", &ed->import_padding, 1, 4, 0);
        igInputInt("Margin", &ed->import_margin, 1, 4, 0);

        if (ed->import_tile_width < 1) ed->import_tile_width = 1;
        if (ed->import_tile_height < 1) ed->import_tile_height = 1;
        if (ed->import_columns < 0) ed->import_columns = 0;
        if (ed->import_rows < 0) ed->import_rows = 0;
        if (ed->import_padding < 0) ed->import_padding = 0;
        if (ed->import_margin < 0) ed->import_margin = 0;

        igSeparator();

        if (igButton("Import", (ImVec2){100, 30}))
        {
            if (ed->texture_pool && strlen(ed->import_texture_path) > 0)
            {
                if (!ed->tileset)
                {
                    Mel_Tileset_Handle handle = mel_tileset_pool_create(ed->tileset_pool, S8("imported"));
                    mel_ed_tiles_set_tileset(ed, handle);
                    strcpy(ed->tileset_path, "imported.json");
                }

                Mel_Tile_Source* source = mel_tileset_entry_add_source(ed->tileset, str8_from_cstr(ed->import_texture_path), true);
                if (source)
                {
                    source->tile_width = (u32)ed->import_tile_width;
                    source->tile_height = (u32)ed->import_tile_height;
                    source->padding = (u32)ed->import_padding;
                    source->margin = (u32)ed->import_margin;

                    source->texture = ed->import_preview_texture.value
                        ? ed->import_preview_texture
                        : mel_texture_pool_load(ed->texture_pool, str8_from_cstr(ed->import_texture_path));

                    Mel_Gpu_Texture* tex = mel_texture_pool_get(ed->texture_pool, source->texture);
                    if (tex)
                    {
                        if (ed->import_columns > 0)
                        {
                            source->columns = (u32)ed->import_columns;
                        }
                        else
                        {
                            source->columns = tex->image.width / source->tile_width;
                        }

                        if (ed->import_rows > 0)
                        {
                            source->rows = (u32)ed->import_rows;
                        }
                        else
                        {
                            source->rows = tex->image.height / source->tile_height;
                        }
                    }

                    mel_tileset_entry_regenerate_tiles(ed->tileset, ed->texture_pool);
                    ed->dirty = true;
                }

                ed->import_texture_path[0] = '\0';
                ed->import_preview_texture = MEL_TEXTURE_HANDLE_NULL;
                ed->show_import_dialog = false;
            }
        }
        igSameLine(0, 10);
        if (igButton("Cancel", (ImVec2){100, 30}))
        {
            ed->import_preview_texture = MEL_TEXTURE_HANDLE_NULL;
            ed->show_import_dialog = false;
        }

        igNextColumn();

        igText("Preview");
        Mel_Gpu_Texture* preview = mel_texture_pool_get(ed->texture_pool, ed->import_preview_texture);
        if (preview && preview->descriptor)
        {
            f32 tex_w = (f32)preview->image.width;
            f32 tex_h = (f32)preview->image.height;

            f32 preview_max = 300.0f;
            f32 scale = 1.0f;
            if (tex_w > preview_max || tex_h > preview_max)
            {
                scale = preview_max / (tex_w > tex_h ? tex_w : tex_h);
            }
            f32 display_w = tex_w * scale;
            f32 display_h = tex_h * scale;

            ImVec2_c cursor_pos = igGetCursorScreenPos();
            ImDrawList* draw_list = igGetWindowDrawList();

            ImTextureRef_c tex_ref = {._TexData = nullptr, ._TexID = (ImTextureID)preview->descriptor};
            ImVec2 p0 = {cursor_pos.x, cursor_pos.y};
            ImVec2 p1 = {cursor_pos.x + display_w, cursor_pos.y + display_h};
            ImDrawList_AddImage(draw_list, tex_ref, p0, p1, (ImVec2){0, 0}, (ImVec2){1, 1}, IM_COL32(255, 255, 255, 255));

            i32 tile_w = ed->import_tile_width;
            i32 tile_h = ed->import_tile_height;
            i32 padding = ed->import_padding;
            i32 margin = ed->import_margin;

            i32 cols = ed->import_columns > 0 ? ed->import_columns : (i32)((tex_w - 2 * margin + padding) / (tile_w + padding));
            i32 rows = ed->import_rows > 0 ? ed->import_rows : (i32)((tex_h - 2 * margin + padding) / (tile_h + padding));

            for (i32 row = 0; row < rows; row++)
            {
                for (i32 col = 0; col < cols; col++)
                {
                    f32 gx = (f32)(margin + col * (tile_w + padding)) * scale;
                    f32 gy = (f32)(margin + row * (tile_h + padding)) * scale;
                    f32 gw = (f32)tile_w * scale;
                    f32 gh = (f32)tile_h * scale;

                    ImVec2 gp0 = {cursor_pos.x + gx, cursor_pos.y + gy};
                    ImVec2 gp1 = {gp0.x + gw, gp0.y + gh};
                    ImDrawList_AddRect(draw_list, gp0, gp1, IM_COL32(255, 200, 50, 200), 0, 0, 1.0f);
                }
            }

            igDummy((ImVec2){display_w, display_h});
            igText("Detected: %dx%d tiles", cols, rows);
        }
        else
        {
            igTextDisabled("Enter path and click Load Preview");
        }

        igColumns(1, nullptr, false);
    }
    igEnd();
}

void mel_ed_tiles_brush_set_single(Mel_EdTiles* ed, i32 tile_id)
{
    assert(ed != nullptr);

    if (ed->brush.current.tiles)
    {
        mel_ed_tiles_brush_push_history(ed);
        mel_dealloc(ed->alloc, ed->brush.current.tiles);
    }

    ed->brush.current.tiles = mel_alloc(ed->alloc, sizeof(i32));
    ed->brush.current.tiles[0] = tile_id;
    ed->brush.current.width = 1;
    ed->brush.current.height = 1;
    ed->brush.flip_h = false;
    ed->brush.flip_v = false;
    ed->brush.rotation = 0;
}

void mel_ed_tiles_brush_from_selection(Mel_EdTiles* ed)
{
    assert(ed != nullptr);

    if (!ed->selection.active || !ed->tilemap) return;
    if (ed->selected_layer < 0 || (u32)ed->selected_layer >= ed->tilemap->layer_count) return;

    i32 sx = ed->selection.start_x < ed->selection.end_x ? ed->selection.start_x : ed->selection.end_x;
    i32 sy = ed->selection.start_y < ed->selection.end_y ? ed->selection.start_y : ed->selection.end_y;
    i32 ex = ed->selection.start_x > ed->selection.end_x ? ed->selection.start_x : ed->selection.end_x;
    i32 ey = ed->selection.start_y > ed->selection.end_y ? ed->selection.start_y : ed->selection.end_y;

    u32 width = (u32)(ex - sx + 1);
    u32 height = (u32)(ey - sy + 1);

    if (ed->brush.current.tiles)
    {
        mel_ed_tiles_brush_push_history(ed);
        mel_dealloc(ed->alloc, ed->brush.current.tiles);
    }

    ed->brush.current.tiles = mel_alloc(ed->alloc, width * height * sizeof(i32));
    ed->brush.current.width = width;
    ed->brush.current.height = height;
    ed->brush.flip_h = false;
    ed->brush.flip_v = false;
    ed->brush.rotation = 0;

    Mel_Tilemap_Layer* layer = &ed->tilemap->layers[ed->selected_layer];
    for (u32 y = 0; y < height; y++)
    {
        for (u32 x = 0; x < width; x++)
        {
            u32 map_x = (u32)sx + x;
            u32 map_y = (u32)sy + y;
            ed->brush.current.tiles[y * width + x] = layer->data[map_y * ed->tilemap->width + map_x];
        }
    }

    ed->selection.active = false;
}

void mel_ed_tiles_brush_push_history(Mel_EdTiles* ed)
{
    assert(ed != nullptr);

    if (!ed->brush.current.tiles || ed->brush.current.width == 0 || ed->brush.current.height == 0)
        return;

    if (ed->brush.history_count >= MEL_ED_TILES_BRUSH_HISTORY_SIZE)
    {
        if (ed->brush.history[MEL_ED_TILES_BRUSH_HISTORY_SIZE - 1].tiles)
        {
            mel_dealloc(ed->alloc, ed->brush.history[MEL_ED_TILES_BRUSH_HISTORY_SIZE - 1].tiles);
        }
        for (u32 i = MEL_ED_TILES_BRUSH_HISTORY_SIZE - 1; i > 0; i--)
        {
            ed->brush.history[i] = ed->brush.history[i - 1];
        }
    }
    else
    {
        for (u32 i = ed->brush.history_count; i > 0; i--)
        {
            ed->brush.history[i] = ed->brush.history[i - 1];
        }
        ed->brush.history_count++;
    }

    u32 count = ed->brush.current.width * ed->brush.current.height;
    ed->brush.history[0].tiles = mel_alloc(ed->alloc, count * sizeof(i32));
    memcpy(ed->brush.history[0].tiles, ed->brush.current.tiles, count * sizeof(i32));
    ed->brush.history[0].width = ed->brush.current.width;
    ed->brush.history[0].height = ed->brush.current.height;
}

void mel_ed_tiles_brush_flip_h(Mel_EdTiles* ed)
{
    assert(ed != nullptr);

    if (!ed->brush.current.tiles) return;

    ed->brush.flip_h = !ed->brush.flip_h;

    u32 w = ed->brush.current.width;
    u32 h = ed->brush.current.height;

    for (u32 y = 0; y < h; y++)
    {
        for (u32 x = 0; x < w / 2; x++)
        {
            i32 temp = ed->brush.current.tiles[y * w + x];
            ed->brush.current.tiles[y * w + x] = ed->brush.current.tiles[y * w + (w - 1 - x)];
            ed->brush.current.tiles[y * w + (w - 1 - x)] = temp;
        }
    }
}

void mel_ed_tiles_brush_flip_v(Mel_EdTiles* ed)
{
    assert(ed != nullptr);

    if (!ed->brush.current.tiles) return;

    ed->brush.flip_v = !ed->brush.flip_v;

    u32 w = ed->brush.current.width;
    u32 h = ed->brush.current.height;

    for (u32 y = 0; y < h / 2; y++)
    {
        for (u32 x = 0; x < w; x++)
        {
            i32 temp = ed->brush.current.tiles[y * w + x];
            ed->brush.current.tiles[y * w + x] = ed->brush.current.tiles[(h - 1 - y) * w + x];
            ed->brush.current.tiles[(h - 1 - y) * w + x] = temp;
        }
    }
}

void mel_ed_tiles_brush_rotate_cw(Mel_EdTiles* ed)
{
    assert(ed != nullptr);

    if (!ed->brush.current.tiles) return;

    ed->brush.rotation = (ed->brush.rotation + 1) % 4;

    u32 old_w = ed->brush.current.width;
    u32 old_h = ed->brush.current.height;
    u32 new_w = old_h;
    u32 new_h = old_w;

    i32* new_tiles = mel_alloc(ed->alloc, new_w * new_h * sizeof(i32));

    for (u32 y = 0; y < old_h; y++)
    {
        for (u32 x = 0; x < old_w; x++)
        {
            u32 new_x = old_h - 1 - y;
            u32 new_y = x;
            new_tiles[new_y * new_w + new_x] = ed->brush.current.tiles[y * old_w + x];
        }
    }

    mel_dealloc(ed->alloc, ed->brush.current.tiles);
    ed->brush.current.tiles = new_tiles;
    ed->brush.current.width = new_w;
    ed->brush.current.height = new_h;
}

void mel_ed_tiles_paint(Mel_EdTiles* ed, i32 x, i32 y)
{
    assert(ed != nullptr);

    if (!ed->tilemap || !ed->brush.current.tiles) return;
    if (ed->selected_layer < 0 || (u32)ed->selected_layer >= ed->tilemap->layer_count) return;

    u32 bw = ed->brush.current.width;
    u32 bh = ed->brush.current.height;

    for (u32 by = 0; by < bh; by++)
    {
        for (u32 bx = 0; bx < bw; bx++)
        {
            i32 map_x = x + (i32)bx;
            i32 map_y = y + (i32)by;

            if (map_x >= 0 && map_y >= 0 && (u32)map_x < ed->tilemap->width && (u32)map_y < ed->tilemap->height)
            {
                i32 tile = ed->brush.current.tiles[by * bw + bx];
                if (tile >= 0)
                {
                    mel_tilemap_entry_set_tile(ed->tilemap, (u32)ed->selected_layer, (u32)map_x, (u32)map_y, tile);
                    ed->dirty = true;
                }
            }
        }
    }
}

void mel_ed_tiles_erase(Mel_EdTiles* ed, i32 x, i32 y)
{
    assert(ed != nullptr);

    if (!ed->tilemap) return;
    if (ed->selected_layer < 0 || (u32)ed->selected_layer >= ed->tilemap->layer_count) return;

    if (x >= 0 && y >= 0 && (u32)x < ed->tilemap->width && (u32)y < ed->tilemap->height)
    {
        mel_tilemap_entry_set_tile(ed->tilemap, (u32)ed->selected_layer, (u32)x, (u32)y, -1);
        ed->dirty = true;
    }
}

void mel_ed_tiles_fill(Mel_EdTiles* ed, i32 x, i32 y)
{
    assert(ed != nullptr);

    if (!ed->tilemap || !ed->brush.current.tiles) return;
    if (ed->selected_layer < 0 || (u32)ed->selected_layer >= ed->tilemap->layer_count) return;
    if (x < 0 || y < 0 || (u32)x >= ed->tilemap->width || (u32)y >= ed->tilemap->height) return;

    i32 target_tile = mel_tilemap_entry_get_tile(ed->tilemap, (u32)ed->selected_layer, (u32)x, (u32)y);

    u32 bw = ed->brush.current.width;
    u32 bh = ed->brush.current.height;

    u32 map_size = ed->tilemap->width * ed->tilemap->height;
    bool* visited = mel_calloc(ed->alloc, map_size * sizeof(bool));
    i32* stack = mel_alloc(ed->alloc, map_size * 2 * sizeof(i32));
    u32 stack_size = 0;

    stack[stack_size++] = x;
    stack[stack_size++] = y;

    while (stack_size > 0)
    {
        i32 cy = stack[--stack_size];
        i32 cx = stack[--stack_size];

        if (cx < 0 || cy < 0 || (u32)cx >= ed->tilemap->width || (u32)cy >= ed->tilemap->height)
            continue;

        u32 idx = (u32)cy * ed->tilemap->width + (u32)cx;
        if (visited[idx])
            continue;

        if (mel_tilemap_entry_get_tile(ed->tilemap, (u32)ed->selected_layer, (u32)cx, (u32)cy) != target_tile)
            continue;

        visited[idx] = true;

        u32 bx = ((u32)cx % bw);
        u32 by = ((u32)cy % bh);
        i32 fill_tile = ed->brush.current.tiles[by * bw + bx];

        if (fill_tile >= 0)
        {
            mel_tilemap_entry_set_tile(ed->tilemap, (u32)ed->selected_layer, (u32)cx, (u32)cy, fill_tile);
            ed->dirty = true;
        }

        stack[stack_size++] = cx + 1;
        stack[stack_size++] = cy;
        stack[stack_size++] = cx - 1;
        stack[stack_size++] = cy;
        stack[stack_size++] = cx;
        stack[stack_size++] = cy + 1;
        stack[stack_size++] = cx;
        stack[stack_size++] = cy - 1;
    }

    mel_dealloc(ed->alloc, stack);
    mel_dealloc(ed->alloc, visited);
}
