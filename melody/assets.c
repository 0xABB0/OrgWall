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

    if (opt.org_name && opt.app_name)
    {
        const char* pref_dir = PHYSFS_getPrefDir(opt.org_name, opt.app_name);
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

    if (opt.base_path)
    {
        if (!PHYSFS_mount(opt.base_path, nullptr, 1))
        {
            SDL_Log("Failed to mount base path '%s': %s", opt.base_path, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
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

bool mel_assets_mount(const char* path, const char* mount_point)
{
    assert(path != nullptr);

    if (!PHYSFS_mount(path, mount_point, 1))
    {
        SDL_Log("Failed to mount '%s': %s", path, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return false;
    }

    SDL_Log("Mounted: %s -> %s", path, mount_point ? mount_point : "/");
    return true;
}

bool mel_assets_mount_write(const char* path)
{
    assert(path != nullptr);

    if (!PHYSFS_setWriteDir(path))
    {
        SDL_Log("Failed to set write dir '%s': %s", path, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return false;
    }

    return true;
}

bool mel_assets_exists(const char* path)
{
    assert(path != nullptr);
    return PHYSFS_exists(path) != 0;
}

bool mel_assets_is_directory(const char* path)
{
    assert(path != nullptr);

    PHYSFS_Stat stat;
    if (!PHYSFS_stat(path, &stat)) return false;

    return stat.filetype == PHYSFS_FILETYPE_DIRECTORY;
}

u8* mel_assets_read(const char* path, u32* out_size)
{
    assert(path != nullptr);

    PHYSFS_File* file = PHYSFS_openRead(path);
    if (!file)
    {
        SDL_Log("Failed to open '%s': %s", path, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
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

char* mel_assets_read_text(const char* path)
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

bool mel_assets_write(const char* path, const void* data, u32 size)
{
    assert(path != nullptr);
    assert(data != nullptr || size == 0);

    PHYSFS_File* file = PHYSFS_openWrite(path);
    if (!file)
    {
        SDL_Log("Failed to write '%s': %s", path, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
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

bool mel_assets_write_text(const char* path, const char* text)
{
    assert(text != nullptr);
    return mel_assets_write(path, text, (u32)strlen(text));
}

char** mel_assets_list(const char* path, u32* out_count)
{
    assert(path != nullptr);

    char** files = PHYSFS_enumerateFiles(path);
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

bool mel_assets_import_file(const char* filesystem_path, const char* dest_name)
{
    assert(filesystem_path != nullptr);
    assert(dest_name != nullptr);

    size_t size;
    void* data = SDL_LoadFile(filesystem_path, &size);
    if (!data)
    {
        SDL_Log("Failed to read file '%s': %s", filesystem_path, SDL_GetError());
        return false;
    }

    bool result = mel_assets_write(dest_name, data, (u32)size);
    SDL_free(data);

    if (result)
    {
        SDL_Log("Imported '%s' as '%s'", filesystem_path, dest_name);
    }

    return result;
}
