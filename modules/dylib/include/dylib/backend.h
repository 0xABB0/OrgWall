#pragma once

#include <dylib/dylib.h>

#ifdef __cplusplus
extern "C"
{
#endif

bool mel_dylib__plat_available(void);

void* mel_dylib__plat_open(const char* path, u32 flags, Mel_Dylib_Status* status, i64* os_error);

void mel_dylib__plat_close(void* handle);

void* mel_dylib__plat_symbol(void* handle, const char* symbol, bool* found, i64* os_error);

#ifdef __cplusplus
}
#endif
