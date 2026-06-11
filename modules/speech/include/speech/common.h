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

typedef struct mel_speech_auth mel_speech_auth;

extern const mel_speech_auth mel_speech_auth_granted;
extern const mel_speech_auth mel_speech_auth_denied;
extern const mel_speech_auth mel_speech_auth_not_determined;
extern const mel_speech_auth mel_speech_auth_restricted;

const char* mel_speech_auth_name(const mel_speech_auth* a);
bool        mel_speech_auth_is_granted(const mel_speech_auth* a);

void mel_speech_init(const Mel_Alloc* alloc);
void mel_speech_shutdown(void);

u32 mel_speech_refresh(void);

const mel_speech_auth* mel_speech_authorization(void);
Mel_Future*            mel_speech_authorize(const Mel_Alloc* a);
const mel_speech_auth* mel_speech_future_auth(const Mel_Future* f);
void                   mel_speech_future_free(Mel_Future* f);

#ifdef __cplusplus
}
#endif
