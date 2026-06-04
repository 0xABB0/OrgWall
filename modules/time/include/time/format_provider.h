#pragma once

#include <time/format_prefs.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    const char* name;
    void*       user;

    bool (*query)(void* user, Mel_Time_Format_Prefs* out);
} Mel_Time_Format_Provider_Desc;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Time_Format_Provider;

Mel_Time_Format_Provider mel_time_format_provider_register(const Mel_Time_Format_Provider_Desc* desc);
void                     mel_time_format_provider_unregister(Mel_Time_Format_Provider p);

void mel_time_format__register_host_providers(void);

#ifdef __cplusplus
}
#endif
