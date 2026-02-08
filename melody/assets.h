#ifndef MEL_ASSETS_H
#define MEL_ASSETS_H

#include "types.h"

typedef struct
{
    bool initialized;
} Mel_Assets;

typedef struct
{
    const char* org_name;
    const char* app_name;
    const char* base_path;
} Mel_Assets_Opt;

bool mel_assets_init_opt(Mel_Assets* assets, Mel_Assets_Opt opt);
#define mel_assets_init(assets, ...) mel_assets_init_opt((assets), (Mel_Assets_Opt){__VA_ARGS__})

void mel_assets_shutdown(Mel_Assets* assets);

bool mel_assets_mount(const char* path, const char* mount_point);
bool mel_assets_mount_write(const char* path);

bool mel_assets_exists(const char* path);
bool mel_assets_is_directory(const char* path);

u8* mel_assets_read(const char* path, u32* out_size);
char* mel_assets_read_text(const char* path);
void mel_assets_free(void* data);

bool mel_assets_write(const char* path, const void* data, u32 size);
bool mel_assets_write_text(const char* path, const char* text);

char** mel_assets_list(const char* path, u32* out_count);
void mel_assets_list_free(char** list);

bool mel_assets_import_file(const char* filesystem_path, const char* dest_name);

#endif
