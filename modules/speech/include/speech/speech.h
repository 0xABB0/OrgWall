#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <string/str8.h>
#include <allocator/allocator.fwd.h>
#include <collection/slotmap.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Future Mel_Future;

typedef u32 Mel_Speech_Status;

#define MEL_SPEECH_SEVERITY_MASK         0x3u
#define MEL_SPEECH_OK                    0u
#define MEL_SPEECH_WARNED                1u
#define MEL_SPEECH_ERROR                 2u

#define MEL_SPEECH_WARN_RATE_CLAMPED     (1u << 2)
#define MEL_SPEECH_WARN_PITCH_DROPPED    (1u << 3)
#define MEL_SPEECH_WARN_VOLUME_DROPPED   (1u << 4)
#define MEL_SPEECH_WARN_RANGES_DROPPED   (1u << 5)
#define MEL_SPEECH_WARN_PARTIALS_DROPPED (1u << 6)
#define MEL_SPEECH_WARN_STOP_SYNTHESIZED (1u << 7)

#define MEL_SPEECH_RESULT_DENIED         (1u << 8)
#define MEL_SPEECH_RESULT_NO_DEVICE      (1u << 9)
#define MEL_SPEECH_RESULT_BUSY           (1u << 10)
#define MEL_SPEECH_RESULT_UNSUPPORTED    (1u << 11)
#define MEL_SPEECH_RESULT_LOST           (1u << 12)
#define MEL_SPEECH_RESULT_CANCELLED      (1u << 13)
#define MEL_SPEECH_RESULT_ABORTED        (1u << 14)
#define MEL_SPEECH_RESULT_AUDIO          (1u << 15)
#define MEL_SPEECH_RESULT_NETWORK        (1u << 16)

static inline bool mel_speech_failed(Mel_Speech_Status s) { return (s & MEL_SPEECH_SEVERITY_MASK) == MEL_SPEECH_ERROR; }
static inline bool mel_speech_warned(Mel_Speech_Status s) { return (s & MEL_SPEECH_SEVERITY_MASK) == MEL_SPEECH_WARNED; }

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Speech_Voice;

#define MEL_SPEECH_VOICE_NULL ((Mel_Speech_Voice){ 0 })

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Speech_Recognizer;

#define MEL_SPEECH_RECOGNIZER_NULL ((Mel_Speech_Recognizer){ 0 })

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Speech_Utterance;

#define MEL_SPEECH_UTTERANCE_NULL ((Mel_Speech_Utterance){ 0 })

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Speech_Session;

#define MEL_SPEECH_SESSION_NULL ((Mel_Speech_Session){ 0 })

typedef struct mel_speech_auth mel_speech_auth;

extern const mel_speech_auth mel_speech_auth_granted;
extern const mel_speech_auth mel_speech_auth_denied;
extern const mel_speech_auth mel_speech_auth_not_determined;
extern const mel_speech_auth mel_speech_auth_restricted;

const char* mel_speech_auth_name(const mel_speech_auth* a);
bool        mel_speech_auth_is_granted(const mel_speech_auth* a);

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
    usize offset;
    usize length;
} Mel_Speech_Range;

typedef struct
{
    str8 text;
    bool final;
    f32  confidence;
} Mel_Speech_Result;

typedef void (*Mel_Speech_On_Speak_Complete)(Mel_Speech_Utterance u, Mel_Speech_Status status, void* user);
typedef void (*Mel_Speech_On_Range)(Mel_Speech_Utterance u, Mel_Speech_Range range, void* user);
typedef void (*Mel_Speech_On_Result)(Mel_Speech_Session s, const Mel_Speech_Result* result, void* user);
typedef void (*Mel_Speech_On_Listen_Complete)(Mel_Speech_Session s, Mel_Speech_Status status, void* user);

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

void mel_speech_init(const Mel_Alloc* alloc);
void mel_speech_shutdown(void);

u32 mel_speech_refresh(void);

u32                              mel_speech_voice_count(void);
u32                              mel_speech_voice_list(Mel_Speech_Voice* out, u32 cap);
Mel_Speech_Voice_Describe_Result mel_speech_voice_describe(Mel_Speech_Voice v);
bool                             mel_speech_voice_alive(Mel_Speech_Voice v);
bool                             mel_speech_voice_equal(Mel_Speech_Voice a, Mel_Speech_Voice b);

u32                                   mel_speech_recognizer_count(void);
u32                                   mel_speech_recognizer_list(Mel_Speech_Recognizer* out, u32 cap);
Mel_Speech_Recognizer_Describe_Result mel_speech_recognizer_describe(Mel_Speech_Recognizer r);
bool                                  mel_speech_recognizer_alive(Mel_Speech_Recognizer r);
bool                                  mel_speech_recognizer_equal(Mel_Speech_Recognizer a, Mel_Speech_Recognizer b);

const mel_speech_auth* mel_speech_authorization(void);
Mel_Future*            mel_speech_authorize(const Mel_Alloc* a);
const mel_speech_auth* mel_speech_future_auth(const Mel_Future* f);
void                   mel_speech_future_free(Mel_Future* f);

Mel_Speech_Speak_Result mel_speech_speak_opt(Mel_Speech_Voice v, str8 text, Mel_Speech_Speak_Opt opt);
#define mel_speech_speak(v, text, ...) mel_speech_speak_opt((v), (text), (Mel_Speech_Speak_Opt){ __VA_ARGS__ })

Mel_Speech_Status mel_speech_speak_pause(Mel_Speech_Utterance u);
Mel_Speech_Status mel_speech_speak_resume(Mel_Speech_Utterance u);
void              mel_speech_speak_abort(Mel_Speech_Utterance u);
void              mel_speech_speak_abort_all(Mel_Speech_Voice v);

bool mel_speech_speaking(Mel_Speech_Utterance u);
bool mel_speech_speak_paused(Mel_Speech_Utterance u);

Mel_Speech_Listen_Result mel_speech_listen_opt(Mel_Speech_Recognizer r, Mel_Speech_Listen_Opt opt);
#define mel_speech_listen(r, ...) mel_speech_listen_opt((r), (Mel_Speech_Listen_Opt){ __VA_ARGS__ })

Mel_Speech_Status mel_speech_listen_stop(Mel_Speech_Session s);
void              mel_speech_listen_abort(Mel_Speech_Session s);

bool mel_speech_listening(Mel_Speech_Session s);

void* mel_speech_voice_native(Mel_Speech_Voice v);
void* mel_speech_recognizer_native(Mel_Speech_Recognizer r);

#ifdef __cplusplus
}
#endif
