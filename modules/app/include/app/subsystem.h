#pragma once

#include <app/lifecycle.h>
#include <allocator/allocator.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    const Mel_Alloc* alloc;
    Mel_Reactor*     reactor;
} Mel_App_Init_Opt;

u32 mel_app_init_opt(Mel_App_Init_Opt opt);
#define mel_app_init(...) mel_app_init_opt((Mel_App_Init_Opt){ __VA_ARGS__ })

u32 mel_app_quit(void);

u32  mel_app_refcount(void);
bool mel_app_initialized(void);

#ifdef __cplusplus
}
#endif
