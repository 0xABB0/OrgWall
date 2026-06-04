#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>

#include <dylib/status.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Dylib Mel_Dylib;

#define MEL_DYLIB_BIND_NOW  (1u << 0)
#define MEL_DYLIB_BIND_LAZY (1u << 1)
#define MEL_DYLIB_GLOBAL    (1u << 2)
#define MEL_DYLIB_LOCAL     (1u << 3)
#define MEL_DYLIB_NOLOAD    (1u << 4)
#define MEL_DYLIB_NODELETE  (1u << 5)
#define MEL_DYLIB_DEEPBIND  (1u << 6)

typedef struct
{
    const char*      path;
    const char*      name;
    u32              flags;
    const Mel_Alloc* alloc;
} Mel_Dylib_Open_Opt;

typedef struct
{
    Mel_Dylib*       value;
    Mel_Dylib_Status status;
} Mel_Dylib_Open_Result;

typedef struct
{
    void*            addr;
    Mel_Dylib_Status status;
} Mel_Dylib_Symbol;

Mel_Dylib_Open_Result mel_dylib_open_opt(Mel_Dylib_Open_Opt opt);
#define mel_dylib_open(...) mel_dylib_open_opt((Mel_Dylib_Open_Opt){ __VA_ARGS__ })

Mel_Dylib_Symbol mel_dylib_symbol(Mel_Dylib* lib, const char* symbol);

void mel_dylib_close(Mel_Dylib* lib);

bool        mel_dylib_available(void);
const char* mel_dylib_path(const Mel_Dylib* lib);
void*       mel_dylib_native(const Mel_Dylib* lib);

#ifdef __cplusplus
}
#endif
