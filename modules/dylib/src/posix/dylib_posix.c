#include <dylib/backend.h>

#include <core/platform.h>
#include <log/log.h>

#include <dlfcn.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

bool mel_dylib__plat_available(void) { return true; }

static int posix_flags(u32 flags)
{
    int out = 0;
    if (flags & MEL_DYLIB_BIND_LAZY)
        out |= RTLD_LAZY;
    else
        out |= RTLD_NOW;
    if (flags & MEL_DYLIB_GLOBAL)
        out |= RTLD_GLOBAL;
    else
        out |= RTLD_LOCAL;
#ifdef RTLD_NOLOAD
    if (flags & MEL_DYLIB_NOLOAD)
        out |= RTLD_NOLOAD;
#endif
#ifdef RTLD_NODELETE
    if (flags & MEL_DYLIB_NODELETE)
        out |= RTLD_NODELETE;
#endif
#ifdef RTLD_DEEPBIND
    if (flags & MEL_DYLIB_DEEPBIND)
        out |= RTLD_DEEPBIND;
#endif
    return out;
}

static Mel_Dylib_Status classify(const char* path, const char* err)
{
    bool is_explicit_path = path && strchr(path, '/') != NULL;
    if (is_explicit_path && access(path, F_OK) != 0)
        return MEL_DYLIB_ERROR | (errno == EACCES ? MEL_DYLIB_PERMISSION : MEL_DYLIB_NOT_FOUND);
    if (err)
    {
        if (strcasestr(err, "no such file") || strcasestr(err, "not found") || strcasestr(err, "cannot open"))
            return MEL_DYLIB_ERROR | MEL_DYLIB_NOT_FOUND;
        if (strcasestr(err, "permission denied"))
            return MEL_DYLIB_ERROR | MEL_DYLIB_PERMISSION;
        if (strcasestr(err, "symbol not found") || strcasestr(err, "undefined symbol"))
            return MEL_DYLIB_ERROR | MEL_DYLIB_INIT_FAILED;
    }
    return MEL_DYLIB_ERROR | MEL_DYLIB_BAD_IMAGE;
}

void* mel_dylib__plat_open(const char* path, u32 flags, Mel_Dylib_Status* status, i64* os_error)
{
    *os_error = 0;
    dlerror();
    void* h = dlopen(path, posix_flags(flags));
    if (!h)
    {
        const char* err = dlerror();
        *status = classify(path, err);
        mel_log_warn("dylib", "dlopen('%s'): %s", path, err ? err : "(no error string)");
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
