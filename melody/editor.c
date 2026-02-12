#include "editor.h"
#include "editor.registry.h"
#include "string.str8.h"

#include <cimgui/cimgui.h>
#include <SDL3/SDL.h>
#include <string.h>

#include "texture.pool.h"
#include "tile.set.h"
#include "tile.map.h"
#include "collection.slotmap.h"

static void file_dialog_callback(void* userdata, const char* const* filelist, int filter)
{
    MEL_UNUSED(filter);
    Mel_GameEditor* ge = (Mel_GameEditor*)userdata;

    if (filelist && filelist[0])
    {
        strncpy(ge->pending_file_path, filelist[0], sizeof(ge->pending_file_path) - 1);
        ge->file_dialog_pending = true;
    }
}

static void draw_main_menu_bar(Mel_GameEditor* ge);
static void draw_asset_browser_window(Mel_GameEditor* ge);
static void draw_texture_picker_popup(Mel_GameEditor* ge);

void mel_game_editor_init(Mel_GameEditor* ge, Mel_EdRegistry* registry, Mel_Texture_Pool* tex_pool, Mel_Tileset_Pool* ts_pool, Mel_Tilemap_Pool* tm_pool, SDL_Window* window)
{
    assert(ge != nullptr);

    *ge = (Mel_GameEditor){0};
    ge->registry = registry;
    ge->texture_pool = tex_pool;
    ge->tileset_pool = ts_pool;
    ge->tilemap_pool = tm_pool;
    ge->window = window;
    ge->visible = false;
    ge->show_asset_browser = false;
}

void mel_game_editor_shutdown(Mel_GameEditor* ge)
{
    assert(ge != nullptr);
    MEL_UNUSED(ge);
}

void mel_game_editor_draw(Mel_GameEditor* ge, f32 dt)
{
    assert(ge != nullptr);

    if (!ge->visible) return;

    draw_main_menu_bar(ge);

    if (ge->show_asset_browser)
    {
        draw_asset_browser_window(ge);
    }

    if (ge->show_texture_picker)
    {
        draw_texture_picker_popup(ge);
    }

    mel_ed_registry_draw(ge->registry, dt);
}

void mel_game_editor_toggle(Mel_GameEditor* ge)
{
    assert(ge != nullptr);

    ge->visible = !ge->visible;
    SDL_Log("Editor %s", ge->visible ? "shown" : "hidden");
}

void mel_game_editor_show_texture_picker(Mel_GameEditor* ge, Mel_TexturePickerCallback cb, void* userdata)
{
    assert(ge != nullptr);

    ge->show_texture_picker = true;
    ge->texture_picker_callback = cb;
    ge->texture_picker_userdata = userdata;
}

static void draw_main_menu_bar(Mel_GameEditor* ge)
{
    if (igBeginMainMenuBar())
    {
        if (igBeginMenu("File", true))
        {
            if (igMenuItem_Bool("Import Texture...", nullptr, false, ge->texture_pool != nullptr))
            {
                mel_game_editor_show_texture_picker(ge, nullptr, nullptr);
            }
            igEndMenu();
        }

        if (igBeginMenu("Windows", true))
        {
            igMenuItem_BoolPtr("Asset Browser", nullptr, &ge->show_asset_browser, true);
            igSeparator();

            for (usize i = 0; i < mel_ed_registry_count(ge->registry); i++)
            {
                Mel_EdEntry* entry = mel_ed_registry_at(ge->registry, i);
                igMenuItem_BoolPtr(entry->window_title, nullptr, &entry->open, true);
            }

            igEndMenu();
        }

        igEndMainMenuBar();
    }
}

static void draw_asset_browser_window(Mel_GameEditor* ge)
{
    igSetNextWindowSize((ImVec2){400, 500}, ImGuiCond_FirstUseEver);

    if (igBegin("Asset Browser", &ge->show_asset_browser, 0))
    {
        if (!ge->texture_pool)
        {
            igTextColored((ImVec4){1.0f, 0.5f, 0.5f, 1.0f}, "No pools connected");
        }
        else
        {
            igSeparator();

            if (igCollapsingHeader_TreeNodeFlags("Textures", ImGuiTreeNodeFlags_DefaultOpen))
            {
                igIndent(0);
                u32 tex_count = mel_texture_pool_count(ge->texture_pool);
                igText("%u textures loaded", tex_count);
                if (tex_count == 0)
                {
                    igTextDisabled("No textures loaded");
                }
                igUnindent(0);
            }

            if (igCollapsingHeader_TreeNodeFlags("Tilesets", ImGuiTreeNodeFlags_DefaultOpen))
            {
                igIndent(0);
                u32 ts_count = ge->tileset_pool ? mel_slotmap_count(&ge->tileset_pool->slotmap) : 0;
                igText("%u tilesets loaded", ts_count);
                if (ts_count == 0)
                {
                    igTextDisabled("No tilesets loaded");
                }
                igUnindent(0);
            }

            if (igCollapsingHeader_TreeNodeFlags("Tilemaps", ImGuiTreeNodeFlags_DefaultOpen))
            {
                igIndent(0);
                u32 tm_count = ge->tilemap_pool ? mel_slotmap_count(&ge->tilemap_pool->slotmap) : 0;
                igText("%u tilemaps loaded", tm_count);
                if (tm_count == 0)
                {
                    igTextDisabled("No tilemaps loaded");
                }
                igUnindent(0);
            }
        }
    }
    igEnd();
}

static void draw_texture_picker_popup(Mel_GameEditor* ge)
{
    if (ge->file_dialog_pending)
    {
        ge->file_dialog_pending = false;

        if (strlen(ge->pending_file_path) > 0 && ge->texture_pool)
        {
            Mel_Texture_Handle h = mel_texture_pool_load(ge->texture_pool, str8_from_cstr(ge->pending_file_path));
            if (h.value != 0)
            {
                str8 full_path = str8_from_cstr(ge->pending_file_path);
                const char* slash = strrchr(ge->pending_file_path, '/');
                str8 filename = slash ? str8_from_cstr(slash + 1) : full_path;

                if (ge->texture_picker_callback)
                {
                    ge->texture_picker_callback(filename, ge->texture_picker_userdata);
                }

                SDL_Log("Imported texture: %s", ge->pending_file_path);
                ge->show_texture_picker = false;
            }
            else
            {
                SDL_Log("Failed to import texture: %s", ge->pending_file_path);
            }
        }
        ge->pending_file_path[0] = '\0';
    }

    igSetNextWindowSize((ImVec2){450, 350}, ImGuiCond_FirstUseEver);

    if (igBegin("Select Texture", &ge->show_texture_picker, 0))
    {
        if (igButton("Browse Files...", (ImVec2){-1, 30}))
        {
            SDL_DialogFileFilter filters[] = {
                { "Image Files", "png;jpg;jpeg;bmp;tga" },
                { "All Files", "*" },
            };

            SDL_ShowOpenFileDialog(file_dialog_callback, ge, ge->window, filters, 2, nullptr, false);
        }

        igSeparator();

        igInputText("Filter##texturepicker", ge->texture_picker_filter,
            sizeof(ge->texture_picker_filter), 0, nullptr, nullptr);

        igText("Loaded Textures:");

        if (!ge->texture_pool)
        {
            igTextColored((ImVec4){1.0f, 0.5f, 0.5f, 1.0f}, "No texture pool");
        }
        else
        {
            if (igBeginChild_Str("TextureList", (ImVec2){0, -60}, ImGuiChildFlags_Borders, 0))
            {
                u32 tex_count = mel_texture_pool_count(ge->texture_pool);
                if (tex_count == 0)
                {
                    igTextDisabled("No textures loaded yet");
                }
                else
                {
                    igText("%u textures available", tex_count);
                }
            }
            igEndChild();

            igSeparator();

            igText("Or enter path manually:");
            static char manual_path[256] = "";
            igInputText("##manualpath", manual_path, sizeof(manual_path), 0, nullptr, nullptr);
            igSameLine(0, 10);
            if (igButton("Load", (ImVec2){60, 0}))
            {
                if (strlen(manual_path) > 0)
                {
                    Mel_Texture_Handle h = mel_texture_pool_load(ge->texture_pool, str8_from_cstr(manual_path));
                    if (h.value != 0)
                    {
                        if (ge->texture_picker_callback)
                        {
                            ge->texture_picker_callback(str8_from_cstr(manual_path), ge->texture_picker_userdata);
                        }
                        ge->show_texture_picker = false;
                        SDL_Log("Loaded texture: %s", manual_path);
                    }
                }
            }
        }
    }
    igEnd();
}
