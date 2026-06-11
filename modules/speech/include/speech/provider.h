#pragma once

#include <speech/tts.h>
#include <speech/stt.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    u64                   stable_id;
    str8                  name;
    str8                  language;
    Mel_Speech_Voice_Caps caps;
} Mel_Speech_Voice_Raw;

typedef struct
{
    u64                        stable_id;
    str8                       language;
    Mel_Speech_Recognizer_Caps caps;
} Mel_Speech_Recognizer_Raw;

typedef struct
{
    str8                  text;
    f32                   rate;
    f32                   pitch;
    f32                   volume;
    bool                  want_ranges;
    Mel_Speech_Voice_Caps caps;
} Mel_Speech_Speak_Lowered;

typedef struct
{
    bool                       partials;
    Mel_Speech_Recognizer_Caps caps;
} Mel_Speech_Listen_Lowered;

typedef struct
{
    void (*on_range)(void* token, Mel_Speech_Range range);
    void (*on_speak_done)(void* token, Mel_Speech_Status status);
    void (*on_result)(void* token, const Mel_Speech_Result* result);
    void (*on_listen_done)(void* token, Mel_Speech_Status status);
    void (*on_auth)(void* token, const mel_speech_auth* auth);
    void* token;
} Mel_Speech_Sink;

typedef struct
{
    const char* name;
    void*       user;

    u32 (*enumerate_voices)(void* user, const Mel_Alloc* alloc, Mel_Speech_Voice_Raw* out, u32 cap);
    u32 (*enumerate_recognizers)(void* user, const Mel_Alloc* alloc, Mel_Speech_Recognizer_Raw* out, u32 cap);

    Mel_Speech_Status (*speak)(void* user, u64 stable_id, u64 token, const Mel_Speech_Speak_Lowered* lowered, Mel_Speech_Sink sink);
    void (*speak_pause)(void* user, u64 stable_id, u64 token);
    void (*speak_resume)(void* user, u64 stable_id, u64 token);
    void (*speak_abort)(void* user, u64 stable_id, u64 token);

    const mel_speech_auth* (*authorization)(void* user);
    void (*authorize)(void* user, Mel_Speech_Sink sink);

    Mel_Speech_Status (*listen)(void* user, u64 stable_id, u64 token, const Mel_Speech_Listen_Lowered* lowered, Mel_Speech_Sink sink);
    void (*listen_stop)(void* user, u64 stable_id, u64 token);
    void (*listen_abort)(void* user, u64 stable_id, u64 token);

    void* (*voice_native)(void* user, u64 stable_id);
    void* (*recognizer_native)(void* user, u64 stable_id);

    void (*shutdown)(void* user, const Mel_Alloc* alloc);
} Mel_Speech_Provider_Desc;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Speech_Provider;

Mel_Speech_Provider mel_speech_provider_register(const Mel_Speech_Provider_Desc* desc);
void                mel_speech_provider_unregister(Mel_Speech_Provider p);

void mel_speech__register_host_providers(void);

#ifdef __cplusplus
}
#endif
