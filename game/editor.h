#ifndef MEL_GAME_EDITOR_H
#define MEL_GAME_EDITOR_H

#include "types.h"
#include "editor.registry.h"
#include "asset_registry.h"

#include <SDL3/SDL.h>

typedef void (*Mel_TexturePickerCallback)(const char* path, void* userdata);

typedef struct
{
    Mel_EdRegistry*    registry;
    Mel_AssetRegistry* asset_registry;
    SDL_Window*        window;

    char pending_file_path[512];
    char texture_picker_filter[128];

    bool visible;
    bool show_asset_browser;
    bool show_texture_picker;
    bool file_dialog_pending;

    Mel_TexturePickerCallback texture_picker_callback;
    void* texture_picker_userdata;
} Mel_GameEditor;

void mel_game_editor_init(Mel_GameEditor* ge, Mel_EdRegistry* registry, Mel_AssetRegistry* assets, SDL_Window* window);
void mel_game_editor_shutdown(Mel_GameEditor* ge);
void mel_game_editor_draw(Mel_GameEditor* ge, f32 dt);
void mel_game_editor_toggle(Mel_GameEditor* ge);

void mel_game_editor_show_texture_picker(Mel_GameEditor* ge, Mel_TexturePickerCallback cb, void* userdata);

#endif
