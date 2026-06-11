#pragma once

#include <speech/common.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Speech_Voice;

#define MEL_SPEECH_VOICE_NULL ((Mel_Speech_Voice){ 0 })

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Speech_Utterance;

#define MEL_SPEECH_UTTERANCE_NULL ((Mel_Speech_Utterance){ 0 })

typedef struct
{
    bool rate;
    f32  rate_min, rate_max;
    bool pitch;
    bool volume;
    bool ranges;
    bool can_pause;
} Mel_Speech_Voice_Caps;

typedef struct
{
    str8                  name;
    str8                  language;
    Mel_Speech_Voice_Caps caps;
} Mel_Speech_Voice_Descriptor;

typedef struct
{
    Mel_Speech_Voice_Descriptor value;
    Mel_Speech_Status           status;
} Mel_Speech_Voice_Describe_Result;

typedef struct
{
    usize offset;
    usize length;
} Mel_Speech_Range;

typedef void (*Mel_Speech_On_Speak_Complete)(Mel_Speech_Utterance u, Mel_Speech_Status status, void* user);
typedef void (*Mel_Speech_On_Range)(Mel_Speech_Utterance u, Mel_Speech_Range range, void* user);

typedef struct
{
    f32                          rate;
    f32                          pitch;
    f32                          volume;
    Mel_Speech_On_Speak_Complete on_complete;
    Mel_Speech_On_Range          on_range;
    void*                        user;
} Mel_Speech_Speak_Opt;

typedef struct
{
    Mel_Speech_Utterance value;
    Mel_Speech_Status    status;
} Mel_Speech_Speak_Result;

u32                              mel_speech_voice_count(void);
u32                              mel_speech_voice_list(Mel_Speech_Voice* out, u32 cap);
Mel_Speech_Voice_Describe_Result mel_speech_voice_describe(Mel_Speech_Voice v);
bool                             mel_speech_voice_alive(Mel_Speech_Voice v);
bool                             mel_speech_voice_equal(Mel_Speech_Voice a, Mel_Speech_Voice b);

Mel_Speech_Speak_Result mel_speech_speak_opt(Mel_Speech_Voice v, str8 text, Mel_Speech_Speak_Opt opt);
#define mel_speech_speak(v, text, ...) mel_speech_speak_opt((v), (text), (Mel_Speech_Speak_Opt){ __VA_ARGS__ })

Mel_Speech_Status mel_speech_speak_pause(Mel_Speech_Utterance u);
Mel_Speech_Status mel_speech_speak_resume(Mel_Speech_Utterance u);
void              mel_speech_speak_abort(Mel_Speech_Utterance u);
void              mel_speech_speak_abort_all(Mel_Speech_Voice v);

bool mel_speech_speaking(Mel_Speech_Utterance u);
bool mel_speech_speak_paused(Mel_Speech_Utterance u);

void* mel_speech_voice_native(Mel_Speech_Voice v);

#ifdef __cplusplus
}
#endif
