#include <dylib/backend.h>

#include <log/log.h>

#if defined(MEL_DYLIB_WASM_DYNAMIC)

#include <dlfcn.h>
#include <string.h>

bool mel_dylib__plat_available(void) { return true; }

static int wasm_flags(u32 flags)
{
    int out = (flags & MEL_DYLIB_BIND_LAZY) ? RTLD_LAZY : RTLD_NOW;
    out |= (flags & MEL_DYLIB_GLOBAL) ? RTLD_GLOBAL : RTLD_LOCAL;
    if (flags & MEL_DYLIB_NOLOAD)
        out |= RTLD_NOLOAD;
    if (flags & MEL_DYLIB_NODELETE)
        out |= RTLD_NODELETE;
    return out;
}

static Mel_Dylib_Status classify(const char* err)
{
    if (err && (strstr(err, "no such file") || strstr(err, "not found") || strstr(err, "Could not load")))
        return MEL_DYLIB_ERROR | MEL_DYLIB_NOT_FOUND;
    return MEL_DYLIB_ERROR | MEL_DYLIB_BAD_IMAGE;
}

void* mel_dylib__plat_open(const char* path, u32 flags, Mel_Dylib_Status* status, i64* os_error)
{
    *os_error = 0;
    dlerror();
    void* h = dlopen(path, wasm_flags(flags));
    if (!h)
    {
        const char* err = dlerror();
        *status = classify(err);
        mel_log_warn("dylib", "emscripten dlopen('%s'): %s", path, err ? err : "(no error string)");
        return NULL;
    }
    *status = MEL_DYLIB_OK;
    return h;
}

void mel_dylib__plat_close(void* handle)
{
    if (handle)
        dlclose(handle);
}

void* mel_dylib__plat_symbol(void* handle, const char* symbol, bool* found, i64* os_error)
{
    *os_error = 0;
    dlerror();
    void*       addr = dlsym(handle, symbol);
    const char* err = dlerror();
    *found = err == NULL;
    return addr;
}

#else

bool mel_dylib__plat_available(void) { return false; }

void* mel_dylib__plat_open(const char* path, u32 flags, Mel_Dylib_Status* status, i64* os_error)
{
    (void)path;
    (void)flags;
    *os_error = 0;
    *status = MEL_DYLIB_ERROR | MEL_DYLIB_UNAVAILABLE;
    mel_log_warn("dylib", "open: dynamic loading needs an emscripten MAIN_MODULE build (define MEL_DYLIB_WASM_DYNAMIC)");
    return NULL;
}

void mel_dylib__plat_close(void* handle) { (void)handle; }

void* mel_dylib__plat_symbol(void* handle, const char* symbol, bool* found, i64* os_error)
{
    (void)handle;
    (void)symbol;
    *os_error = 0;
    *found = false;
    return NULL;
}

#endif
