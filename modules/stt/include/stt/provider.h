#pragma once

#include <stt/stt.h>
#include <allocator/allocator.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    u64                     stable_id;
    str8                    language;
    Mel_Stt_Recognizer_Caps caps;
} Mel_Stt_Recognizer_Raw;

typedef struct
{
    bool                    partials;
    str8                    device_stable_id;
    bool                    feed;
    u32                     feed_sample_rate;
    bool                    require_on_device;
    const str8*             vocabulary;
    u32                     vocabulary_count;
    bool                    punctuation;
    bool                    profanity_filter;
    Mel_Stt_Recognizer_Caps caps;
} Mel_Stt_Listen_Lowered;

typedef struct
{
    void (*on_result)(void* token, const Mel_Stt_Result* result);
    void (*on_done)(void* token, Mel_Stt_Status status);
    void (*on_auth)(void* token, const mel_stt_auth* auth);
    void* token;
} Mel_Stt_Sink;

typedef struct
{
    const char* name;
    void*       user;

    u32 (*enumerate_recognizers)(void* user, const Mel_Alloc* alloc, Mel_Stt_Recognizer_Raw* out, u32 cap);

    const mel_stt_auth* (*authorization)(void* user);
    void (*authorize)(void* user, Mel_Stt_Sink sink);

    Mel_Stt_Status (*listen)(void* user, u64 stable_id, u64 token, const Mel_Stt_Listen_Lowered* lowered, Mel_Stt_Sink sink);
    void (*stop)(void* user, u64 stable_id, u64 token);
    void (*abort)(void* user, u64 stable_id, u64 token);
    Mel_Stt_Status (*feed)(void* user, u64 stable_id, u64 token, const f32* frames, u32 frame_count);

    void* (*recognizer_native)(void* user, u64 stable_id);
    void (*shutdown)(void* user, const Mel_Alloc* alloc);
} Mel_Stt_Provider_Desc;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Stt_Provider;

Mel_Stt_Provider mel_stt_provider_register(const Mel_Stt_Provider_Desc* desc);
void             mel_stt_provider_unregister(Mel_Stt_Provider p);

void mel_stt__register_host_providers(void);

#ifdef __cplusplus
}
#endif
