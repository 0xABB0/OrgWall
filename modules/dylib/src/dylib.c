#include <dylib/dylib.h>
#include <dylib/backend.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <core/platform.h>
#include <log/log.h>

#include <string.h>

struct Mel_Dylib
{
    void*            handle;
    char*            path;
    const Mel_Alloc* alloc;
};

static const char* decorate(const Mel_Alloc* a, const char* name, char** owned_out)
{
    *owned_out = NULL;
    const char* prefix = "lib";
#if MEL_PLATFORM_WINDOWS
    const char* suffix = ".dll";
    prefix = "";
#elif MEL_PLATFORM_APPLE
    const char* suffix = ".dylib";
#else
    const char* suffix = ".so";
#endif

    usize pn = strlen(prefix);
    usize nn = strlen(name);
    usize sn = strlen(suffix);
    char* buf = (char*)mel_alloc(a, pn + nn + sn + 1);
    if (!buf)
        return NULL;
    memcpy(buf, prefix, pn);
    memcpy(buf + pn, name, nn);
    memcpy(buf + pn + nn, suffix, sn);
    buf[pn + nn + sn] = '\0';
    *owned_out = buf;
    return buf;
}

Mel_Dylib_Open_Result mel_dylib_open_opt(Mel_Dylib_Open_Opt opt)
{
    Mel_Dylib_Open_Result r = { .value = NULL, .status = MEL_DYLIB_OK };
    const Mel_Alloc*      a = opt.alloc ? opt.alloc : mel_alloc_heap();

    if (!opt.path && !opt.name)
    {
        mel_log_error("dylib", "open: neither path nor name given");
        r.status = MEL_DYLIB_ERROR | MEL_DYLIB_NOT_FOUND;
        return r;
    }
    if (opt.path && opt.name)
    {
        mel_log_error("dylib", "open: both path and name given; pass exactly one");
        r.status = MEL_DYLIB_ERROR | MEL_DYLIB_NOT_FOUND;
        return r;
    }

    if (!mel_dylib__plat_available())
    {
        mel_log_warn("dylib", "open: no dynamic-loading backend on this platform");
        r.status = MEL_DYLIB_ERROR | MEL_DYLIB_UNAVAILABLE;
        return r;
    }

    char*       decorated = NULL;
    const char* target = opt.path;
    if (!target)
    {
        target = decorate(a, opt.name, &decorated);
        if (!target)
        {
            mel_log_error("dylib", "open: out of memory decorating '%s'", opt.name);
            r.status = MEL_DYLIB_ERROR | MEL_DYLIB_OUT_OF_MEMORY;
            return r;
        }
    }

    i64              os_error = 0;
    Mel_Dylib_Status status = MEL_DYLIB_OK;
    void*            handle = mel_dylib__plat_open(target, opt.flags, &status, &os_error);
    if (!handle)
    {
        mel_log_warn("dylib", "open('%s') failed (status=0x%x, os_error=%lld)", target, (unsigned)status, (long long)os_error);
        if (decorated)
            mel_dealloc(a, decorated);
        r.status = status;
        return r;
    }

    usize tlen = strlen(target);
    char* path_copy = (char*)mel_alloc(a, tlen + 1);
    if (!path_copy)
    {
        mel_dylib__plat_close(handle);
        if (decorated)
            mel_dealloc(a, decorated);
        mel_log_error("dylib", "open: out of memory recording path '%s'", target);
        r.status = MEL_DYLIB_ERROR | MEL_DYLIB_OUT_OF_MEMORY;
        return r;
    }
    memcpy(path_copy, target, tlen + 1);

    Mel_Dylib* lib = mel_alloc_type(a, Mel_Dylib);
    if (!lib)
    {
        mel_dylib__plat_close(handle);
        mel_dealloc(a, path_copy);
        if (decorated)
            mel_dealloc(a, decorated);
        mel_log_error("dylib", "open: out of memory allocating handle for '%s'", target);
        r.status = MEL_DYLIB_ERROR | MEL_DYLIB_OUT_OF_MEMORY;
        return r;
    }
    lib->handle = handle;
    lib->path = path_copy;
    lib->alloc = a;

    if (decorated)
        mel_dealloc(a, decorated);

    r.value = lib;
    r.status = status;
    return r;
}

Mel_Dylib_Symbol mel_dylib_symbol(Mel_Dylib* lib, const char* symbol)
{
    Mel_Dylib_Symbol s = { .addr = NULL, .status = MEL_DYLIB_OK };
    if (!lib || !lib->handle)
    {
        mel_log_error("dylib", "symbol: bad library handle");
        s.status = MEL_DYLIB_ERROR | MEL_DYLIB_BAD_HANDLE;
        return s;
    }
    if (!symbol || symbol[0] == '\0')
    {
        mel_log_error("dylib", "symbol: empty symbol name in '%s'", lib->path);
        s.status = MEL_DYLIB_ERROR | MEL_DYLIB_NO_SYMBOL;
        return s;
    }

    bool  found = false;
    i64   os_error = 0;
    void* addr = mel_dylib__plat_symbol(lib->handle, symbol, &found, &os_error);
    if (!found)
    {
        mel_log_warn("dylib", "symbol('%s') not found in '%s' (os_error=%lld)", symbol, lib->path, (long long)os_error);
        s.status = MEL_DYLIB_ERROR | MEL_DYLIB_NO_SYMBOL;
        return s;
    }
    s.addr = addr;
    s.status = MEL_DYLIB_OK;
    return s;
}

void mel_dylib_close(Mel_Dylib* lib)
{
    if (!lib)
        return;
    if (lib->handle)
        mel_dylib__plat_close(lib->handle);
    const Mel_Alloc* a = lib->alloc;
    if (lib->path)
        mel_dealloc(a, lib->path);
    mel_dealloc(a, lib);
}

bool mel_dylib_available(void) { return mel_dylib__plat_available(); }

const char* mel_dylib_path(const Mel_Dylib* lib) { return lib ? lib->path : NULL; }

void* mel_dylib_native(const Mel_Dylib* lib) { return lib ? lib->handle : NULL; }
