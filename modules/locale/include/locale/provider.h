#pragma once

#include <locale/locale.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    str8 tag;
} Mel_Locale_Raw;

typedef void (*Mel_Locale_Change_Notify)(void* core);

typedef struct
{
    const char* name;
    void*       user;

    u32  (*enumerate)(void* user, const Mel_Alloc* alloc, Mel_Locale_Raw* out, u32 cap);
    void (*watch)(void* user, Mel_Locale_Change_Notify notify, void* core);
    void (*unwatch)(void* user);
} Mel_Locale_Provider_Desc;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Locale_Provider;

Mel_Locale_Provider mel_locale_provider_register(const Mel_Locale_Provider_Desc* desc);
void                mel_locale_provider_unregister(Mel_Locale_Provider p);

void mel_locale__register_host_providers(void);
void mel_locale__on_change(void* core);

#ifdef __cplusplus
}
#endif
