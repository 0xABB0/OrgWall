#include "editor.h"

#include <cimgui/cimgui.h>
#include <SDL3/SDL.h>
#include <string.h>

#include "ed_tiles.h"
#include "ed_spritesheet.h"

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

void mel_game_editor_init(Mel_GameEditor* ge, Mel_EdRegistry* registry, Mel_AssetRegistry* assets, SDL_Window* window)
{
    assert(ge != nullptr);

    *ge = (Mel_GameEditor){0};
    ge->registry = registry;
    ge->asset_registry = assets;
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
            if (igMenuItem_Bool("Import Texture...", nullptr, false, ge->asset_registry != nullptr))
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
                igMenuItem_BoolPtr(entry->name, nullptr, &entry->open, true);
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
        if (!ge->asset_registry)
        {
            igTextColored((ImVec4){1.0f, 0.5f, 0.5f, 1.0f}, "No asset registry connected");
        }
        else
        {
            if (igButton("Rescan Assets", (ImVec2){-1, 30}))
            {
                mel_asset_registry_scan_directory(ge->asset_registry, nullptr);
            }
            igSeparator();

            if (igCollapsingHeader_TreeNodeFlags("Textures", ImGuiTreeNodeFlags_DefaultOpen))
            {
                igIndent(0);
                u32 tex_count = mel_asset_registry_texture_count(ge->asset_registry);
                for (u32 i = 0; i < tex_count; i++)
                {
                    const char* path = mel_asset_registry_texture_path(ge->asset_registry, i);
                    if (igSelectable_Bool(path, false, 0, (ImVec2){0, 0}))
                    {
                        SDL_Log("Selected texture: %s", path);
                    }
                }
                if (tex_count == 0)
                {
                    igTextDisabled("No textures loaded");
                }
                igUnindent(0);
            }

            if (igCollapsingHeader_TreeNodeFlags("Tile Visuals", ImGuiTreeNodeFlags_DefaultOpen))
            {
                igIndent(0);
                u32 tv_count = mel_asset_registry_tile_visual_count(ge->asset_registry);
                for (u32 i = 0; i < tv_count; i++)
                {
                    Mel_TileVisual* tv = mel_asset_registry_tile_visual_at(ge->asset_registry, i);
                    const char* path = mel_asset_registry_tile_visual_path(ge->asset_registry, i);
                    char label[128];
                    snprintf(label, sizeof(label), "%s (%u tiles)",
                        tv->name ? tv->name : "(unnamed)", tv->tile_count);

                    if (igSelectable_Bool(label, false, 0, (ImVec2){0, 0}))
                    {
                        SDL_Log("Selected tile visual: %s", path);
                    }
                }
                if (tv_count == 0)
                {
                    igTextDisabled("No tile visuals loaded");
                }
                igUnindent(0);
            }

            if (igCollapsingHeader_TreeNodeFlags("Tilemaps", ImGuiTreeNodeFlags_DefaultOpen))
            {
                igIndent(0);
                u32 tm_count = mel_asset_registry_tilemap_count(ge->asset_registry);
                for (u32 i = 0; i < tm_count; i++)
                {
                    Mel_Tilemap* tm = mel_asset_registry_tilemap_at(ge->asset_registry, i);
                    const char* path = mel_asset_registry_tilemap_path(ge->asset_registry, i);
                    char label[128];
                    snprintf(label, sizeof(label), "%s (%ux%u, %u layers)",
                        tm->name ? tm->name : "(unnamed)", tm->width, tm->height, tm->layer_count);

                    if (igSelectable_Bool(label, false, 0, (ImVec2){0, 0}))
                    {
                        SDL_Log("Selected tilemap: %s", path);
                    }
                }
                if (tm_count == 0)
                {
                    igTextDisabled("No tilemaps loaded");
                }
                igUnindent(0);
            }

            if (igCollapsingHeader_TreeNodeFlags("Spritesheets", ImGuiTreeNodeFlags_DefaultOpen))
            {
                igIndent(0);
                u32 ss_count = mel_asset_registry_spritesheet_count(ge->asset_registry);
                for (u32 i = 0; i < ss_count; i++)
                {
                    Mel_Spritesheet* ss = mel_asset_registry_spritesheet_at(ge->asset_registry, i);
                    char label[128];
                    snprintf(label, sizeof(label), "%s (%u frames, %u anims)",
                        ss->name ? ss->name : "(unnamed)", ss->frame_count, ss->animation_count);

                    if (igSelectable_Bool(label, false, 0, (ImVec2){0, 0}))
                    {
                        SDL_Log("Selected spritesheet: %s", ss->name);
                    }
                }
                if (ss_count == 0)
                {
                    igTextDisabled("No spritesheets loaded");
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

        if (strlen(ge->pending_file_path) > 0 && ge->asset_registry)
        {
            Mel_VkTexture* tex = mel_asset_registry_import_texture(ge->asset_registry, ge->pending_file_path);
            if (tex)
            {
                const char* filename = strrchr(ge->pending_file_path, '/');
                filename = filename ? filename + 1 : ge->pending_file_path;

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

        if (!ge->asset_registry)
        {
            igTextColored((ImVec4){1.0f, 0.5f, 0.5f, 1.0f}, "No asset registry");
        }
        else
        {
            if (igBeginChild_Str("TextureList", (ImVec2){0, -60}, ImGuiChildFlags_Borders, 0))
            {
                u32 tex_count = mel_asset_registry_texture_count(ge->asset_registry);
                for (u32 i = 0; i < tex_count; i++)
                {
                    const char* path = mel_asset_registry_texture_path(ge->asset_registry, i);

                    if (strlen(ge->texture_picker_filter) > 0)
                    {
                        if (strstr(path, ge->texture_picker_filter) == nullptr)
                        {
                            continue;
                        }
                    }

                    if (igSelectable_Bool(path, false, ImGuiSelectableFlags_NoAutoClosePopups, (ImVec2){0, 0}))
                    {
                        if (ge->texture_picker_callback)
                        {
                            ge->texture_picker_callback(path, ge->texture_picker_userdata);
                        }
                        ge->show_texture_picker = false;
                    }
                }

                if (tex_count == 0)
                {
                    igTextDisabled("No textures loaded yet");
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
                    Mel_VkTexture* tex = mel_asset_registry_load_texture(ge->asset_registry, manual_path);
                    if (tex)
                    {
                        if (ge->texture_picker_callback)
                        {
                            ge->texture_picker_callback(manual_path, ge->texture_picker_userdata);
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
