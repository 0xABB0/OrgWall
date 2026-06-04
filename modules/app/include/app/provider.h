#pragma once

#include <app/lifecycle.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    const char* name;
    void*       user;

    void (*start)(void* user);
    void (*stop)(void* user);
} Mel_App_Provider_Desc;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_App_Provider;

Mel_App_Provider mel_app_provider_register(const Mel_App_Provider_Desc* desc);
void             mel_app_provider_unregister(Mel_App_Provider p);

void mel_app__emit(u32 phase);

void mel_app__register_platform_provider(void);

Mel_Reactor* mel_app__reactor(void);

#ifdef __cplusplus
}
#endif
