#pragma once

#include <tts/tts.h>
#include <allocator/allocator.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    u64                stable_id;
    str8               name;
    str8               language;
    str8               viseme_set;
    Mel_Tts_Voice_Caps caps;
} Mel_Tts_Voice_Raw;

typedef struct
{
    str8               text;
    bool               ssml;
    f32                rate;
    f32                pitch;
    f32                volume;
    bool               want_ranges;
    bool               want_visemes;
    Mel_Tts_Voice_Caps caps;
} Mel_Tts_Lowered;

typedef struct
{
    void (*on_range)(void* token, Mel_Tts_Range range);
    void (*on_viseme)(void* token, Mel_Tts_Viseme viseme);
    void (*on_done)(void* token, Mel_Tts_Status status);
    void (*on_render)(void* token, const Mel_Tts_Render* pcm, Mel_Tts_Status status);
    void* token;
} Mel_Tts_Sink;

typedef struct
{
    const char* name;
    void*       user;

    u32 (*enumerate_voices)(void* user, const Mel_Alloc* alloc, Mel_Tts_Voice_Raw* out, u32 cap);

    Mel_Tts_Status (*speak)(void* user, u64 stable_id, u64 token, const Mel_Tts_Lowered* lowered, Mel_Tts_Sink sink);
    void (*pause)(void* user, u64 stable_id, u64 token);
    void (*resume)(void* user, u64 stable_id, u64 token);
    void (*abort)(void* user, u64 stable_id, u64 token);

    Mel_Tts_Status (*render)(void* user, u64 stable_id, u64 token, const Mel_Tts_Lowered* lowered, Mel_Tts_Sink sink);

    void* (*voice_native)(void* user, u64 stable_id);
    void (*shutdown)(void* user, const Mel_Alloc* alloc);
} Mel_Tts_Provider_Desc;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Tts_Provider;

Mel_Tts_Provider mel_tts_provider_register(const Mel_Tts_Provider_Desc* desc);
void             mel_tts_provider_unregister(Mel_Tts_Provider p);

void mel_tts__register_host_providers(void);

#ifdef __cplusplus
}
#endif
