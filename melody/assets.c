#include "assets.h"
#include <physfs.h>
#include <SDL3/SDL.h>
#include <string.h>
#include <stdlib.h>

bool mel_assets_init_opt(Mel_Assets* assets, Mel_Assets_Opt opt)
{
    assert(assets != nullptr);

    *assets = (Mel_Assets){0};

    if (!PHYSFS_init(nullptr))
    {
        SDL_Log("Failed to init PhysFS: %s", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return false;
    }

    const char* base = PHYSFS_getBaseDir();
    char assets_path[1024] = {0};

    if (!str8_is_empty(opt.org_name) && !str8_is_empty(opt.app_name))
    {
        char org_buf[256];
        char app_buf[256];
        str8_to_buf(opt.org_name, org_buf, sizeof(org_buf));
        str8_to_buf(opt.app_name, app_buf, sizeof(app_buf));

        const char* pref_dir = PHYSFS_getPrefDir(org_buf, app_buf);
        if (pref_dir)
        {
            PHYSFS_setWriteDir(pref_dir);
            PHYSFS_mount(pref_dir, nullptr, 0);
        }
    }
    else if (base)
    {
        snprintf(assets_path, sizeof(assets_path), "%sassets", base);
        PHYSFS_setWriteDir(assets_path);
    }

    if (!str8_is_empty(opt.base_path))
    {
        char base_path_buf[1024];
        str8_to_buf(opt.base_path, base_path_buf, sizeof(base_path_buf));

        if (!PHYSFS_mount(base_path_buf, nullptr, 1))
        {
            SDL_Log("Failed to mount base path '%.*s': %s", (int)opt.base_path.len, opt.base_path.data, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        }
    }
    else
    {
        if (base)
        {
            if (assets_path[0] == '\0')
            {
                snprintf(assets_path, sizeof(assets_path), "%sassets", base);
            }
            PHYSFS_mount(assets_path, nullptr, 1);
            PHYSFS_mount(base, nullptr, 1);
        }
    }

    assets->initialized = true;
    SDL_Log("PhysFS initialized");

    return true;
}

void mel_assets_shutdown(Mel_Assets* assets)
{
    assert(assets != nullptr);

    if (assets->initialized)
    {
        PHYSFS_deinit();
        assets->initialized = false;
        SDL_Log("PhysFS shutdown");
    }
}

bool mel_assets_mount(str8 path, str8 mount_point)
{
    assert(!str8_is_empty(path));

    char path_buf[1024];
    str8_to_buf(path, path_buf, sizeof(path_buf));

    char mp_buf[1024];
    const char* mp = nullptr;
    if (!str8_is_empty(mount_point))
    {
        str8_to_buf(mount_point, mp_buf, sizeof(mp_buf));
        mp = mp_buf;
    }

    if (!PHYSFS_mount(path_buf, mp, 1))
    {
        SDL_Log("Failed to mount '%.*s': %s", (int)path.len, path.data, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return false;
    }

    SDL_Log("Mounted: %.*s -> %s", (int)path.len, path.data, mp ? mp : "/");
    return true;
}

bool mel_assets_mount_write(str8 path)
{
    assert(!str8_is_empty(path));

    char path_buf[1024];
    str8_to_buf(path, path_buf, sizeof(path_buf));

    if (!PHYSFS_setWriteDir(path_buf))
    {
        SDL_Log("Failed to set write dir '%.*s': %s", (int)path.len, path.data, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return false;
    }

    return true;
}

bool mel_assets_exists(str8 path)
{
    assert(!str8_is_empty(path));

    char path_buf[1024];
    str8_to_buf(path, path_buf, sizeof(path_buf));
    return PHYSFS_exists(path_buf) != 0;
}

bool mel_assets_is_directory(str8 path)
{
    assert(!str8_is_empty(path));

    char path_buf[1024];
    str8_to_buf(path, path_buf, sizeof(path_buf));

    PHYSFS_Stat stat;
    if (!PHYSFS_stat(path_buf, &stat)) return false;

    return stat.filetype == PHYSFS_FILETYPE_DIRECTORY;
}

u8* mel_assets_read(str8 path, u32* out_size)
{
    assert(!str8_is_empty(path));

    char path_buf[1024];
    str8_to_buf(path, path_buf, sizeof(path_buf));

    PHYSFS_File* file = PHYSFS_openRead(path_buf);
    if (!file)
    {
        SDL_Log("Failed to open '%.*s': %s", (int)path.len, path.data, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return nullptr;
    }

    PHYSFS_sint64 size = PHYSFS_fileLength(file);
    if (size < 0)
    {
        PHYSFS_close(file);
        return nullptr;
    }

    u8* data = (u8*)malloc((size_t)size);
    if (!data)
    {
        PHYSFS_close(file);
        return nullptr;
    }

    PHYSFS_sint64 read = PHYSFS_readBytes(file, data, (PHYSFS_uint64)size);
    PHYSFS_close(file);

    if (read != size)
    {
        free(data);
        return nullptr;
    }

    if (out_size) *out_size = (u32)size;

    return data;
}

char* mel_assets_read_text(str8 path)
{
    u32 size;
    u8* data = mel_assets_read(path, &size);
    if (!data) return nullptr;

    char* text = (char*)realloc(data, size + 1);
    if (!text)
    {
        free(data);
        return nullptr;
    }

    text[size] = '\0';
    return text;
}

void mel_assets_free(void* data)
{
    free(data);
}

bool mel_assets_write(str8 path, const void* data, u32 size)
{
    assert(!str8_is_empty(path));
    assert(data != nullptr || size == 0);

    char path_buf[1024];
    str8_to_buf(path, path_buf, sizeof(path_buf));

    PHYSFS_File* file = PHYSFS_openWrite(path_buf);
    if (!file)
    {
        SDL_Log("Failed to write '%.*s': %s", (int)path.len, path.data, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return false;
    }

    if (size > 0)
    {
        PHYSFS_sint64 written = PHYSFS_writeBytes(file, data, size);
        if (written != size)
        {
            PHYSFS_close(file);
            return false;
        }
    }

    PHYSFS_close(file);
    return true;
}

bool mel_assets_write_text(str8 path, str8 text)
{
    assert(!str8_is_empty(text));
    return mel_assets_write(path, text.data, (u32)text.len);
}

char** mel_assets_list(str8 path, u32* out_count)
{
    assert(!str8_is_empty(path));

    char path_buf[1024];
    str8_to_buf(path, path_buf, sizeof(path_buf));

    char** files = PHYSFS_enumerateFiles(path_buf);
    if (!files)
    {
        if (out_count) *out_count = 0;
        return nullptr;
    }

    u32 count = 0;
    for (char** i = files; *i != nullptr; i++)
    {
        count++;
    }

    if (out_count) *out_count = count;

    return files;
}

void mel_assets_list_free(char** list)
{
    if (list) PHYSFS_freeList(list);
}

bool mel_assets_import_file(str8 filesystem_path, str8 dest_name)
{
    assert(!str8_is_empty(filesystem_path));
    assert(!str8_is_empty(dest_name));

    char fs_path_buf[1024];
    str8_to_buf(filesystem_path, fs_path_buf, sizeof(fs_path_buf));

    size_t size;
    void* data = SDL_LoadFile(fs_path_buf, &size);
    if (!data)
    {
        SDL_Log("Failed to read file '%.*s': %s", (int)filesystem_path.len, filesystem_path.data, SDL_GetError());
        return false;
    }

    bool result = mel_assets_write(dest_name, data, (u32)size);
    SDL_free(data);

    if (result)
    {
        SDL_Log("Imported '%.*s' as '%.*s'", (int)filesystem_path.len, filesystem_path.data, (int)dest_name.len, dest_name.data);
    }

    return result;
}
