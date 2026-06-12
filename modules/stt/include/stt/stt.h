#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <string/str8.h>
#include <allocator/allocator.fwd.h>
#include <collection/slotmap.fwd.h>
#include <audioin/audioin.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Future Mel_Future;

typedef u32 Mel_Stt_Status;

#define MEL_STT_SEVERITY_MASK 0x3u
#define MEL_STT_OK            0u
#define MEL_STT_WARNED        1u
#define MEL_STT_ERROR         2u

#define MEL_STT_WARN_PARTIALS_DROPPED    (1u << 2)
#define MEL_STT_WARN_STOP_SYNTHESIZED    (1u << 3)
#define MEL_STT_WARN_VOCABULARY_DROPPED  (1u << 4)
#define MEL_STT_WARN_PUNCTUATION_DROPPED (1u << 5)
#define MEL_STT_WARN_PROFANITY_DROPPED   (1u << 6)

#define MEL_STT_RESULT_DENIED      (1u << 7)
#define MEL_STT_RESULT_NO_DEVICE   (1u << 8)
#define MEL_STT_RESULT_BUSY        (1u << 9)
#define MEL_STT_RESULT_UNSUPPORTED (1u << 10)
#define MEL_STT_RESULT_LOST        (1u << 11)
#define MEL_STT_RESULT_CANCELLED   (1u << 12)
#define MEL_STT_RESULT_ABORTED     (1u << 13)
#define MEL_STT_RESULT_AUDIO       (1u << 14)
#define MEL_STT_RESULT_NETWORK     (1u << 15)

static inline bool mel_stt_failed(Mel_Stt_Status s) { return (s & MEL_STT_SEVERITY_MASK) == MEL_STT_ERROR; }
static inline bool mel_stt_warned(Mel_Stt_Status s) { return (s & MEL_STT_SEVERITY_MASK) == MEL_STT_WARNED; }

typedef struct mel_stt_auth mel_stt_auth;

extern const mel_stt_auth mel_stt_auth_granted;
extern const mel_stt_auth mel_stt_auth_denied;
extern const mel_stt_auth mel_stt_auth_not_determined;
extern const mel_stt_auth mel_stt_auth_restricted;

const char* mel_stt_auth_name(const mel_stt_auth* a);
bool        mel_stt_auth_is_granted(const mel_stt_auth* a);

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Stt_Recognizer;

#define MEL_STT_RECOGNIZER_NULL ((Mel_Stt_Recognizer){ 0 })

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Stt_Session;

#define MEL_STT_SESSION_NULL ((Mel_Stt_Session){ 0 })

typedef struct
{
    bool on_device;
    bool require_on_device;
    bool partials;
    bool can_stop;
    bool feed;
    bool device_select;
    bool vocabulary;
    bool punctuation;
    bool profanity_filter;
} Mel_Stt_Recognizer_Caps;

typedef struct
{
    str8                    language;
    Mel_Stt_Recognizer_Caps caps;
} Mel_Stt_Recognizer_Descriptor;

typedef struct
{
    Mel_Stt_Recognizer_Descriptor value;
    Mel_Stt_Status                status;
} Mel_Stt_Recognizer_Describe_Result;

typedef struct
{
    str8 text;
    bool final;
    f32  confidence;
} Mel_Stt_Result;

typedef void (*Mel_Stt_On_Result)(Mel_Stt_Session s, const Mel_Stt_Result* result, void* user);
typedef void (*Mel_Stt_On_Complete)(Mel_Stt_Session s, Mel_Stt_Status status, void* user);

typedef struct
{
    bool                partials;
    Mel_AudioIn         device;
    bool                feed;
    u32                 feed_sample_rate;
    bool                require_on_device;
    const str8*         vocabulary;
    u32                 vocabulary_count;
    bool                punctuation;
    bool                profanity_filter;
    Mel_Stt_On_Result   on_result;
    Mel_Stt_On_Complete on_complete;
    void*               user;
} Mel_Stt_Listen_Opt;

typedef struct
{
    Mel_Stt_Session value;
    Mel_Stt_Status  status;
} Mel_Stt_Listen_Result;

void mel_stt_init(const Mel_Alloc* alloc);
void mel_stt_shutdown(void);

u32 mel_stt_refresh(void);

const mel_stt_auth* mel_stt_authorization(void);
Mel_Future*         mel_stt_authorize(const Mel_Alloc* a);
const mel_stt_auth* mel_stt_future_auth(const Mel_Future* f);
void                mel_stt_future_free(Mel_Future* f);

u32                                mel_stt_recognizer_count(void);
u32                                mel_stt_recognizer_list(Mel_Stt_Recognizer* out, u32 cap);
Mel_Stt_Recognizer_Describe_Result mel_stt_recognizer_describe(Mel_Stt_Recognizer r);
bool                               mel_stt_recognizer_alive(Mel_Stt_Recognizer r);
bool                               mel_stt_recognizer_equal(Mel_Stt_Recognizer a, Mel_Stt_Recognizer b);

Mel_Stt_Listen_Result mel_stt_listen_opt(Mel_Stt_Recognizer r, Mel_Stt_Listen_Opt opt);
#define mel_stt_listen(r, ...) mel_stt_listen_opt((r), (Mel_Stt_Listen_Opt){ __VA_ARGS__ })

Mel_Stt_Status mel_stt_feed(Mel_Stt_Session s, const f32* frames, u32 frame_count);

Mel_Stt_Status mel_stt_stop(Mel_Stt_Session s);
void           mel_stt_abort(Mel_Stt_Session s);

bool mel_stt_listening(Mel_Stt_Session s);

void* mel_stt_recognizer_native(Mel_Stt_Recognizer r);

#ifdef __cplusplus
}
#endif
