#include "editor.h"

#define CIMGUI_USE_SDL3
#define CIMGUI_USE_VULKAN

#include <volk.h>
#include <cimgui/cimgui.h>
#include <cimgui/cimgui_impl.h>
#include <tracy/TracyC.h>

#include <SDL3/SDL.h>
#include <string.h>

static void file_dialog_callback(void* userdata, const char* const* filelist, int filter)
{
    MEL_UNUSED(filter);
    Mel_Editor* editor = (Mel_Editor*)userdata;

    if (filelist && filelist[0])
    {
        strncpy(editor->pending_file_path, filelist[0], sizeof(editor->pending_file_path) - 1);
        editor->file_dialog_pending = true;
    }
}

static void draw_main_menu_bar(Mel_Editor* editor);
static void draw_tile_editor_window(Mel_Editor* editor);
static void draw_spritesheet_editor_window(Mel_Editor* editor, f32 dt);
static void draw_entity_editor_window(Mel_Editor* editor, f32 dt);
static void draw_asset_browser_window(Mel_Editor* editor);
static void draw_texture_picker_popup(Mel_Editor* editor);

bool mel_editor_init_opt(Mel_Editor* editor, Mel_Editor_Opt opt)
{
    assert(editor != nullptr);
    assert(opt.window != nullptr);
    assert(opt.vk != nullptr);
    assert(opt.swapchain != nullptr);

    *editor = (Mel_Editor){0};

    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 },
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 100,
        .poolSizeCount = 1,
        .pPoolSizes = pool_sizes,
    };

    if (vkCreateDescriptorPool(opt.vk->device, &pool_info, nullptr, &editor->descriptor_pool) != VK_SUCCESS)
    {
        SDL_Log("Failed to create ImGui descriptor pool");
        return false;
    }

    igCreateContext(nullptr);

    ImGuiIO* io = igGetIO_Nil();
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    igStyleColorsDark(nullptr);

    ImGuiStyle* style = igGetStyle();
    if (io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style->WindowRounding = 0.0f;
        style->Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplSDL3_InitForVulkan(opt.window);

    VkPipelineRenderingCreateInfoKHR rendering_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &opt.swapchain->format,
    };

    ImGui_ImplVulkan_InitInfo init_info = {
        .ApiVersion = VK_API_VERSION_1_3,
        .Instance = opt.vk->instance,
        .PhysicalDevice = opt.vk->physical_device,
        .Device = opt.vk->device,
        .QueueFamily = opt.vk->graphics_family,
        .Queue = opt.vk->graphics_queue,
        .DescriptorPool = editor->descriptor_pool,
        .MinImageCount = 2,
        .ImageCount = opt.swapchain->image_count,
        .UseDynamicRendering = true,
        .PipelineInfoMain = {
            .PipelineRenderingCreateInfo = rendering_info,
        },
    };

    ImGui_ImplVulkan_Init(&init_info);

    mel_ed_tiles_init(&editor->ed_tiles, opt.alloc);
    mel_ed_spritesheet_init(&editor->ed_spritesheet, opt.alloc);
    mel_ed_entities_init(&editor->ed_entities);

    editor->window = opt.window;
    editor->show_main_menu = true;
    editor->show_entity_editor = true;

    editor->initialized = true;
    editor->visible = false;

    SDL_Log("Editor initialized (ImGui)");
    return true;
}

void mel_editor_shutdown(Mel_Editor* editor, Mel_VkContext* vk)
{
    assert(editor != nullptr);

    if (!editor->initialized) return;

    mel_ed_tiles_shutdown(&editor->ed_tiles);
    mel_ed_spritesheet_shutdown(&editor->ed_spritesheet);
    mel_ed_entities_shutdown(&editor->ed_entities);

    vkDeviceWaitIdle(vk->device);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    igDestroyContext(nullptr);

    if (editor->descriptor_pool)
    {
        vkDestroyDescriptorPool(vk->device, editor->descriptor_pool, nullptr);
    }

    editor->initialized = false;
    SDL_Log("Editor shutdown");
}

void mel_editor_process_event(Mel_Editor* editor, SDL_Event* event)
{
    if (!editor->initialized) return;

    ImGui_ImplSDL3_ProcessEvent(event);
}

void mel_editor_begin_frame(Mel_Editor* editor, f32 dt)
{
    TracyCZoneN(ctx, "editor_begin_frame", true);
    if (!editor->initialized || !editor->visible)
    {
        TracyCZoneEnd(ctx);
        return;
    }

    TracyCZoneN(ctx_newframe, "imgui_new_frame", true);
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    igNewFrame();
    TracyCZoneEnd(ctx_newframe);

    draw_main_menu_bar(editor);

    if (editor->show_tile_editor)
    {
        draw_tile_editor_window(editor);
    }

    if (editor->show_spritesheet_editor)
    {
        draw_spritesheet_editor_window(editor, dt);
    }

    if (editor->show_entity_editor)
    {
        draw_entity_editor_window(editor, dt);
    }

    if (editor->show_asset_browser)
    {
        draw_asset_browser_window(editor);
    }

    if (editor->show_texture_picker)
    {
        draw_texture_picker_popup(editor);
    }

    TracyCZoneEnd(ctx);
}

void mel_editor_end_frame(Mel_Editor* editor, VkCommandBuffer cmd)
{
    TracyCZoneN(ctx, "editor_end_frame", true);
    if (!editor->initialized || !editor->visible)
    {
        TracyCZoneEnd(ctx);
        return;
    }

    TracyCZoneN(ctx_render, "imgui_render", true);
    igRender();
    TracyCZoneEnd(ctx_render);

    TracyCZoneN(ctx_vulkan, "imgui_vulkan_render", true);
    ImGui_ImplVulkan_RenderDrawData(igGetDrawData(), cmd, VK_NULL_HANDLE);
    TracyCZoneEnd(ctx_vulkan);

    ImGuiIO* io = igGetIO_Nil();
    if (io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        TracyCZoneN(ctx_viewports, "imgui_viewports", true);
        igUpdatePlatformWindows();
        igRenderPlatformWindowsDefault(nullptr, nullptr);
        TracyCZoneEnd(ctx_viewports);
    }
    TracyCZoneEnd(ctx);
}

void mel_editor_toggle(Mel_Editor* editor)
{
    assert(editor != nullptr);

    if (!editor->initialized) return;

    editor->visible = !editor->visible;
    SDL_Log("Editor %s", editor->visible ? "shown" : "hidden");
}

void mel_editor_set_ecs_world(Mel_Editor* editor, ecs_world_t* world)
{
    assert(editor != nullptr);

    mel_ed_entities_set_world(&editor->ed_entities, world);
}

void mel_editor_set_asset_registry(Mel_Editor* editor, Mel_AssetRegistry* registry)
{
    assert(editor != nullptr);

    editor->registry = registry;
    mel_ed_tiles_set_registry(&editor->ed_tiles, registry);
}

static void draw_main_menu_bar(Mel_Editor* editor)
{
    if (igBeginMainMenuBar())
    {
        if (igBeginMenu("File", true))
        {
            if (igMenuItem_Bool("New Tile Visual...", nullptr, false, editor->registry != nullptr))
            {
                editor->show_tile_editor = true;
            }
            if (igMenuItem_Bool("New Tilemap...", nullptr, false, editor->registry != nullptr))
            {
                editor->show_tile_editor = true;
            }
            if (igMenuItem_Bool("New Spritesheet...", nullptr, false, editor->registry != nullptr))
            {
                editor->show_spritesheet_editor = true;
                mel_ed_spritesheet_set(&editor->ed_spritesheet, nullptr);
            }
            igSeparator();
            if (igMenuItem_Bool("Import Texture...", nullptr, false, editor->registry != nullptr))
            {
                editor->show_texture_picker = true;
            }
            igEndMenu();
        }

        if (igBeginMenu("Windows", true))
        {
            igMenuItem_BoolPtr("Asset Browser", nullptr, &editor->show_asset_browser, true);
            igMenuItem_BoolPtr("Tile Editor", nullptr, &editor->show_tile_editor, true);
            igMenuItem_BoolPtr("Spritesheet Editor", nullptr, &editor->show_spritesheet_editor, true);
            igMenuItem_BoolPtr("Entity Editor", nullptr, &editor->show_entity_editor, true);
            igEndMenu();
        }

        igEndMainMenuBar();
    }
}

static void draw_tile_editor_window(Mel_Editor* editor)
{
    igSetNextWindowSize((ImVec2){1000, 700}, ImGuiCond_FirstUseEver);

    if (igBegin("Tile Editor", &editor->show_tile_editor, 0))
    {
        mel_ed_tiles_draw(&editor->ed_tiles);
    }
    igEnd();
}

static void draw_spritesheet_editor_window(Mel_Editor* editor, f32 dt)
{
    igSetNextWindowSize((ImVec2){700, 600}, ImGuiCond_FirstUseEver);

    if (igBegin("Spritesheet Editor", &editor->show_spritesheet_editor, 0))
    {
        if (!editor->ed_spritesheet.spritesheet)
        {
            igText("Create a new spritesheet:");
            igSeparator();

            igInputText("Name##newspritesheet", editor->ed_spritesheet.name_buffer,
                sizeof(editor->ed_spritesheet.name_buffer), 0, nullptr, nullptr);

            igText("Texture:");
            const char* tex_label = strlen(editor->ed_spritesheet.texture_path_buffer) > 0 ?
                editor->ed_spritesheet.texture_path_buffer : "(select texture)";
            if (igBeginCombo("##spritesheettexture", tex_label, 0))
            {
                if (editor->registry)
                {
                    u32 tex_count = mel_asset_registry_texture_count(editor->registry);
                    for (u32 i = 0; i < tex_count; i++)
                    {
                        const char* path = mel_asset_registry_texture_path(editor->registry, i);
                        bool selected = strcmp(path, editor->ed_spritesheet.texture_path_buffer) == 0;
                        if (igSelectable_Bool(path, selected, 0, (ImVec2){0, 0}))
                        {
                            strncpy(editor->ed_spritesheet.texture_path_buffer, path,
                                sizeof(editor->ed_spritesheet.texture_path_buffer) - 1);
                        }
                    }
                    if (tex_count == 0)
                    {
                        igTextDisabled("No textures loaded");
                    }
                }
                igEndCombo();
            }
            igSameLine(0, 10);
            if (igButton("Browse##spritesheetbrowse", (ImVec2){60, 0}))
            {
                editor->show_texture_picker = true;
            }

            igSeparator();

            bool can_create = editor->registry != nullptr &&
                              strlen(editor->ed_spritesheet.name_buffer) > 0 &&
                              strlen(editor->ed_spritesheet.texture_path_buffer) > 0;

            if (!can_create) igBeginDisabled(true);
            if (igButton("Create Spritesheet", (ImVec2){150, 30}))
            {
                Mel_Spritesheet* sheet = mel_asset_registry_create_spritesheet(
                    editor->registry,
                    editor->ed_spritesheet.name_buffer,
                    editor->ed_spritesheet.texture_path_buffer);

                if (sheet)
                {
                    mel_ed_spritesheet_set(&editor->ed_spritesheet, sheet);
                    SDL_Log("Created spritesheet: %s", sheet->name);
                }
                else
                {
                    SDL_Log("Failed to create spritesheet");
                }
            }
            if (!can_create) igEndDisabled();
        }
        else
        {
            mel_ed_spritesheet_draw(&editor->ed_spritesheet, dt);

            igSeparator();

            igInputText("Save Path##spritesheet", editor->save_path_buffer,
                sizeof(editor->save_path_buffer), 0, nullptr, nullptr);
            igSameLine(0, 10);

            if (igButton("Save Spritesheet", (ImVec2){120, 0}))
            {
                if (strlen(editor->save_path_buffer) > 0 && editor->registry)
                {
                    if (mel_asset_registry_save_spritesheet(editor->registry,
                        editor->ed_spritesheet.spritesheet, editor->save_path_buffer))
                    {
                        editor->ed_spritesheet.dirty = false;
                        SDL_Log("Saved spritesheet to: %s", editor->save_path_buffer);
                    }
                }
            }
        }
    }
    igEnd();
}

static void draw_entity_editor_window(Mel_Editor* editor, f32 dt)
{
    igSetNextWindowSize((ImVec2){600, 700}, ImGuiCond_FirstUseEver);

    if (igBegin("Entity Editor", &editor->show_entity_editor, 0))
    {
        mel_ed_entities_draw(&editor->ed_entities, dt);
    }
    igEnd();
}

static void draw_asset_browser_window(Mel_Editor* editor)
{
    igSetNextWindowSize((ImVec2){400, 500}, ImGuiCond_FirstUseEver);

    if (igBegin("Asset Browser", &editor->show_asset_browser, 0))
    {
        if (!editor->registry)
        {
            igTextColored((ImVec4){1.0f, 0.5f, 0.5f, 1.0f}, "No asset registry connected");
        }
        else
        {
            if (igButton("Rescan Assets", (ImVec2){-1, 30}))
            {
                mel_asset_registry_scan_directory(editor->registry, nullptr);
            }
            igSeparator();

            if (igCollapsingHeader_TreeNodeFlags("Textures", ImGuiTreeNodeFlags_DefaultOpen))
            {
                igIndent(0);
                u32 tex_count = mel_asset_registry_texture_count(editor->registry);
                for (u32 i = 0; i < tex_count; i++)
                {
                    const char* path = mel_asset_registry_texture_path(editor->registry, i);
                    if (igSelectable_Bool(path, false, 0, (ImVec2){0, 0}))
                    {
                        strncpy(editor->ed_spritesheet.texture_path_buffer, path,
                            sizeof(editor->ed_spritesheet.texture_path_buffer) - 1);
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
                u32 tv_count = mel_asset_registry_tile_visual_count(editor->registry);
                for (u32 i = 0; i < tv_count; i++)
                {
                    Mel_TileVisual* tv = mel_asset_registry_tile_visual_at(editor->registry, i);
                    const char* path = mel_asset_registry_tile_visual_path(editor->registry, i);
                    char label[128];
                    snprintf(label, sizeof(label), "%s (%u tiles)",
                        tv->name ? tv->name : "(unnamed)", tv->tile_count);

                    if (igSelectable_Bool(label, editor->ed_tiles.tile_visual == tv, 0, (ImVec2){0, 0}))
                    {
                        mel_ed_tiles_set_tile_visual(&editor->ed_tiles, tv, path);
                        editor->show_tile_editor = true;
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
                u32 tm_count = mel_asset_registry_tilemap_count(editor->registry);
                for (u32 i = 0; i < tm_count; i++)
                {
                    Mel_Tilemap* tm = mel_asset_registry_tilemap_at(editor->registry, i);
                    const char* path = mel_asset_registry_tilemap_path(editor->registry, i);
                    char label[128];
                    snprintf(label, sizeof(label), "%s (%ux%u, %u layers)",
                        tm->name ? tm->name : "(unnamed)", tm->width, tm->height, tm->layer_count);

                    if (igSelectable_Bool(label, editor->ed_tiles.tilemap == tm, 0, (ImVec2){0, 0}))
                    {
                        mel_ed_tiles_set_tilemap(&editor->ed_tiles, tm, path);
                        editor->show_tile_editor = true;
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
                u32 ss_count = mel_asset_registry_spritesheet_count(editor->registry);
                for (u32 i = 0; i < ss_count; i++)
                {
                    Mel_Spritesheet* ss = mel_asset_registry_spritesheet_at(editor->registry, i);
                    char label[128];
                    snprintf(label, sizeof(label), "%s (%u frames, %u anims)",
                        ss->name ? ss->name : "(unnamed)", ss->frame_count, ss->animation_count);

                    if (igSelectable_Bool(label, editor->ed_spritesheet.spritesheet == ss, 0, (ImVec2){0, 0}))
                    {
                        mel_ed_spritesheet_set(&editor->ed_spritesheet, ss);
                        editor->show_spritesheet_editor = true;
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

static void draw_texture_picker_popup(Mel_Editor* editor)
{
    if (editor->file_dialog_pending)
    {
        editor->file_dialog_pending = false;

        if (strlen(editor->pending_file_path) > 0 && editor->registry)
        {
            Mel_VkTexture* tex = mel_asset_registry_import_texture(editor->registry, editor->pending_file_path);
            if (tex)
            {
                const char* filename = strrchr(editor->pending_file_path, '/');
                filename = filename ? filename + 1 : editor->pending_file_path;

                if (editor->file_dialog_for_tiles)
                {
                    strncpy(editor->ed_tiles.import_texture_path, filename,
                        sizeof(editor->ed_tiles.import_texture_path) - 1);
                }
                if (editor->file_dialog_for_spritesheet)
                {
                    strncpy(editor->ed_spritesheet.texture_path_buffer, filename,
                        sizeof(editor->ed_spritesheet.texture_path_buffer) - 1);
                }
                SDL_Log("Imported texture: %s", editor->pending_file_path);
                editor->show_texture_picker = false;
            }
            else
            {
                SDL_Log("Failed to import texture: %s", editor->pending_file_path);
            }
        }
        editor->pending_file_path[0] = '\0';
        editor->file_dialog_for_tiles = false;
        editor->file_dialog_for_spritesheet = false;
    }

    igSetNextWindowSize((ImVec2){450, 350}, ImGuiCond_FirstUseEver);

    if (igBegin("Select Texture", &editor->show_texture_picker, 0))
    {
        if (igButton("Browse Files...", (ImVec2){-1, 30}))
        {
            SDL_DialogFileFilter filters[] = {
                { "Image Files", "png;jpg;jpeg;bmp;tga" },
                { "All Files", "*" },
            };

            editor->file_dialog_for_tiles = true;
            editor->file_dialog_for_spritesheet = true;

            SDL_ShowOpenFileDialog(file_dialog_callback, editor, editor->window, filters, 2, nullptr, false);
        }

        igSeparator();

        igInputText("Filter##texturepicker", editor->texture_picker_filter,
            sizeof(editor->texture_picker_filter), 0, nullptr, nullptr);

        igText("Loaded Textures:");

        if (!editor->registry)
        {
            igTextColored((ImVec4){1.0f, 0.5f, 0.5f, 1.0f}, "No asset registry");
        }
        else
        {
            if (igBeginChild_Str("TextureList", (ImVec2){0, -60}, ImGuiChildFlags_Borders, 0))
            {
                u32 tex_count = mel_asset_registry_texture_count(editor->registry);
                for (u32 i = 0; i < tex_count; i++)
                {
                    const char* path = mel_asset_registry_texture_path(editor->registry, i);

                    if (strlen(editor->texture_picker_filter) > 0)
                    {
                        if (strstr(path, editor->texture_picker_filter) == nullptr)
                        {
                            continue;
                        }
                    }

                    if (igSelectable_Bool(path, false, ImGuiSelectableFlags_NoAutoClosePopups, (ImVec2){0, 0}))
                    {
                        strncpy(editor->ed_tiles.import_texture_path, path,
                            sizeof(editor->ed_tiles.import_texture_path) - 1);
                        strncpy(editor->ed_spritesheet.texture_path_buffer, path,
                            sizeof(editor->ed_spritesheet.texture_path_buffer) - 1);
                        editor->show_texture_picker = false;
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
                    Mel_VkTexture* tex = mel_asset_registry_load_texture(editor->registry, manual_path);
                    if (tex)
                    {
                        strncpy(editor->ed_tiles.import_texture_path, manual_path,
                            sizeof(editor->ed_tiles.import_texture_path) - 1);
                        strncpy(editor->ed_spritesheet.texture_path_buffer, manual_path,
                            sizeof(editor->ed_spritesheet.texture_path_buffer) - 1);
                        editor->show_texture_picker = false;
                        SDL_Log("Loaded texture: %s", manual_path);
                    }
                }
            }
        }
    }
    igEnd();
}
