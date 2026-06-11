#pragma once

#include <speech/common.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Speech_Recognizer;

#define MEL_SPEECH_RECOGNIZER_NULL ((Mel_Speech_Recognizer){ 0 })

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Speech_Session;

#define MEL_SPEECH_SESSION_NULL ((Mel_Speech_Session){ 0 })

typedef struct
{
    bool on_device;
    bool partials;
    bool can_stop;
} Mel_Speech_Recognizer_Caps;

typedef struct
{
    str8                       language;
    Mel_Speech_Recognizer_Caps caps;
} Mel_Speech_Recognizer_Descriptor;

typedef struct
{
    Mel_Speech_Recognizer_Descriptor value;
    Mel_Speech_Status                status;
} Mel_Speech_Recognizer_Describe_Result;

typedef struct
{
    str8 text;
    bool final;
    f32  confidence;
} Mel_Speech_Result;

typedef void (*Mel_Speech_On_Result)(Mel_Speech_Session s, const Mel_Speech_Result* result, void* user);
typedef void (*Mel_Speech_On_Listen_Complete)(Mel_Speech_Session s, Mel_Speech_Status status, void* user);

typedef struct
{
    bool                          partials;
    Mel_Speech_On_Result          on_result;
    Mel_Speech_On_Listen_Complete on_complete;
    void*                         user;
} Mel_Speech_Listen_Opt;

typedef struct
{
    Mel_Speech_Session value;
    Mel_Speech_Status  status;
} Mel_Speech_Listen_Result;

u32                                   mel_speech_recognizer_count(void);
u32                                   mel_speech_recognizer_list(Mel_Speech_Recognizer* out, u32 cap);
Mel_Speech_Recognizer_Describe_Result mel_speech_recognizer_describe(Mel_Speech_Recognizer r);
bool                                  mel_speech_recognizer_alive(Mel_Speech_Recognizer r);
bool                                  mel_speech_recognizer_equal(Mel_Speech_Recognizer a, Mel_Speech_Recognizer b);

Mel_Speech_Listen_Result mel_speech_listen_opt(Mel_Speech_Recognizer r, Mel_Speech_Listen_Opt opt);
#define mel_speech_listen(r, ...) mel_speech_listen_opt((r), (Mel_Speech_Listen_Opt){ __VA_ARGS__ })

Mel_Speech_Status mel_speech_listen_stop(Mel_Speech_Session s);
void              mel_speech_listen_abort(Mel_Speech_Session s);

bool mel_speech_listening(Mel_Speech_Session s);

void* mel_speech_recognizer_native(Mel_Speech_Recognizer r);

#ifdef __cplusplus
}
#endif
