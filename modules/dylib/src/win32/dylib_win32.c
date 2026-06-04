#include <dylib/backend.h>

#include <log/log.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

bool mel_dylib__plat_available(void) { return true; }

static Mel_Dylib_Status classify(DWORD e)
{
    switch (e)
    {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_MOD_NOT_FOUND:
    case ERROR_DLL_NOT_FOUND:
        return MEL_DYLIB_ERROR | MEL_DYLIB_NOT_FOUND;
    case ERROR_ACCESS_DENIED:
        return MEL_DYLIB_ERROR | MEL_DYLIB_PERMISSION;
    case ERROR_BAD_EXE_FORMAT:
    case ERROR_INVALID_MODULETYPE:
        return MEL_DYLIB_ERROR | MEL_DYLIB_BAD_IMAGE;
    case ERROR_DLL_INIT_FAILED:
        return MEL_DYLIB_ERROR | MEL_DYLIB_INIT_FAILED;
    case ERROR_PROC_NOT_FOUND:
        return MEL_DYLIB_ERROR | MEL_DYLIB_NO_SYMBOL;
    default:
        return MEL_DYLIB_ERROR | MEL_DYLIB_BAD_IMAGE;
    }
}

void* mel_dylib__plat_open(const char* path, u32 flags, Mel_Dylib_Status* status, i64* os_error)
{
    *os_error = 0;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (wlen <= 0)
    {
        *status = MEL_DYLIB_ERROR | MEL_DYLIB_NOT_FOUND;
        mel_log_warn("dylib", "LoadLibraryW: cannot widen path '%s'", path);
        return NULL;
    }
    WCHAR  stackbuf[512];
    WCHAR* wpath = stackbuf;
    HANDLE heap = GetProcessHeap();
    bool   heaped = false;
    if (wlen > (int)(sizeof(stackbuf) / sizeof(stackbuf[0])))
    {
        wpath = (WCHAR*)HeapAlloc(heap, 0, (SIZE_T)wlen * sizeof(WCHAR));
        if (!wpath)
        {
            *status = MEL_DYLIB_ERROR | MEL_DYLIB_OUT_OF_MEMORY;
            mel_log_error("dylib", "LoadLibraryW: out of memory widening '%s'", path);
            return NULL;
        }
        heaped = true;
    }
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);

    DWORD load_flags = 0;
    if (flags & MEL_DYLIB_NOLOAD)
    {
        HMODULE existing = NULL;
        if (!GetModuleHandleExW(0, wpath, &existing) || !existing)
        {
            DWORD e = GetLastError();
            *os_error = (i64)e;
            *status = MEL_DYLIB_ERROR | MEL_DYLIB_NOT_FOUND;
            if (heaped)
                HeapFree(heap, 0, wpath);
            mel_log_warn("dylib", "GetModuleHandleExW('%s'): not already loaded", path);
            return NULL;
        }
        if (heaped)
            HeapFree(heap, 0, wpath);
        *status = MEL_DYLIB_OK;
        return (void*)existing;
    }

    HMODULE h = LoadLibraryExW(wpath, NULL, load_flags);
    DWORD   e = h ? 0 : GetLastError();
    if (heaped)
        HeapFree(heap, 0, wpath);
    if (!h)
    {
        *os_error = (i64)e;
        *status = classify(e);
        mel_log_warn("dylib", "LoadLibraryExW('%s'): GetLastError=%lu", path, (unsigned long)e);
        return NULL;
    }
    *status = MEL_DYLIB_OK;
    return (void*)h;
}

void mel_dylib__plat_close(void* handle)
{
    if (handle)
        FreeLibrary((HMODULE)handle);
}

void* mel_dylib__plat_symbol(void* handle, const char* symbol, bool* found, i64* os_error)
{
    *os_error = 0;
    FARPROC p = GetProcAddress((HMODULE)handle, symbol);
    if (!p)
    {
        DWORD e = GetLastError();
        *os_error = (i64)e;
        *found = false;
        return NULL;
    }
    *found = true;
    return (void*)(uintptr_t)p;
}
