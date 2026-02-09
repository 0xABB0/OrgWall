#ifndef MEL_ASSETS_H
#define MEL_ASSETS_H

#include "types.h"
#include "string.str8.h"

typedef struct
{
    bool initialized;
} Mel_Assets;

typedef struct
{
    str8 org_name;
    str8 app_name;
    str8 base_path;
} Mel_Assets_Opt;

bool mel_assets_init_opt(Mel_Assets* assets, Mel_Assets_Opt opt);
#define mel_assets_init(assets, ...) mel_assets_init_opt((assets), (Mel_Assets_Opt){__VA_ARGS__})

void mel_assets_shutdown(Mel_Assets* assets);

bool mel_assets_mount(str8 path, str8 mount_point);
bool mel_assets_mount_write(str8 path);

bool mel_assets_exists(str8 path);
bool mel_assets_is_directory(str8 path);

u8* mel_assets_read(str8 path, u32* out_size);
char* mel_assets_read_text(str8 path);
void mel_assets_free(void* data);

bool mel_assets_write(str8 path, const void* data, u32 size);
bool mel_assets_write_text(str8 path, str8 text);

char** mel_assets_list(str8 path, u32* out_count);
void mel_assets_list_free(char** list);

bool mel_assets_import_file(str8 filesystem_path, str8 dest_name);

#endif
