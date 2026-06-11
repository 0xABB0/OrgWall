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

typedef u32 Mel_Tts_Status;

#define MEL_TTS_SEVERITY_MASK 0x3u
#define MEL_TTS_OK            0u
#define MEL_TTS_WARNED        1u
#define MEL_TTS_ERROR         2u

#define MEL_TTS_WARN_RATE_CLAMPED   (1u << 2)
#define MEL_TTS_WARN_PITCH_DROPPED  (1u << 3)
#define MEL_TTS_WARN_VOLUME_DROPPED (1u << 4)
#define MEL_TTS_WARN_RANGES_DROPPED (1u << 5)
#define MEL_TTS_WARN_VISEMES_DROPPED (1u << 6)

#define MEL_TTS_RESULT_BUSY        (1u << 7)
#define MEL_TTS_RESULT_UNSUPPORTED (1u << 8)
#define MEL_TTS_RESULT_LOST        (1u << 9)
#define MEL_TTS_RESULT_CANCELLED   (1u << 10)
#define MEL_TTS_RESULT_ABORTED     (1u << 11)
#define MEL_TTS_RESULT_AUDIO       (1u << 12)
#define MEL_TTS_RESULT_NETWORK     (1u << 13)

static inline bool mel_tts_failed(Mel_Tts_Status s) { return (s & MEL_TTS_SEVERITY_MASK) == MEL_TTS_ERROR; }
static inline bool mel_tts_warned(Mel_Tts_Status s) { return (s & MEL_TTS_SEVERITY_MASK) == MEL_TTS_WARNED; }

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Tts_Voice;

#define MEL_TTS_VOICE_NULL ((Mel_Tts_Voice){ 0 })

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Tts_Utterance;

#define MEL_TTS_UTTERANCE_NULL ((Mel_Tts_Utterance){ 0 })

typedef struct
{
    bool rate;
    f32  rate_min, rate_max;
    bool pitch;
    bool volume;
    bool ranges;
    bool can_pause;
    bool render;
    bool ssml;
    bool visemes;
} Mel_Tts_Voice_Caps;

typedef struct
{
    str8               name;
    str8               language;
    str8               viseme_set;
    Mel_Tts_Voice_Caps caps;
} Mel_Tts_Voice_Descriptor;

typedef struct
{
    Mel_Tts_Voice_Descriptor value;
    Mel_Tts_Status           status;
} Mel_Tts_Voice_Describe_Result;

typedef struct
{
    usize offset;
    usize length;
} Mel_Tts_Range;

typedef struct
{
    u32           viseme;
    Mel_Tts_Range range;
} Mel_Tts_Viseme;

typedef struct
{
    const f32* frames;
    u32        frame_count;
    u32        sample_rate;
    u32        channels;
} Mel_Tts_Render;

typedef void (*Mel_Tts_On_Complete)(Mel_Tts_Utterance u, Mel_Tts_Status status, void* user);
typedef void (*Mel_Tts_On_Range)(Mel_Tts_Utterance u, Mel_Tts_Range range, void* user);
typedef void (*Mel_Tts_On_Viseme)(Mel_Tts_Utterance u, Mel_Tts_Viseme viseme, void* user);
typedef void (*Mel_Tts_On_Render)(Mel_Tts_Utterance u, const Mel_Tts_Render* pcm, Mel_Tts_Status status, void* user);

typedef struct
{
    f32                 rate;
    f32                 pitch;
    f32                 volume;
    bool                ssml;
    Mel_Tts_On_Complete on_complete;
    Mel_Tts_On_Range    on_range;
    Mel_Tts_On_Viseme   on_viseme;
    void*               user;
} Mel_Tts_Speak_Opt;

typedef struct
{
    Mel_Tts_Utterance value;
    Mel_Tts_Status    status;
} Mel_Tts_Speak_Result;

typedef struct
{
    f32               rate;
    f32               pitch;
    f32               volume;
    bool              ssml;
    Mel_Tts_On_Render on_render;
    void*             user;
} Mel_Tts_Render_Opt;

void mel_tts_init(const Mel_Alloc* alloc);
void mel_tts_shutdown(void);

u32 mel_tts_refresh(void);

u32                           mel_tts_voice_count(void);
u32                           mel_tts_voice_list(Mel_Tts_Voice* out, u32 cap);
Mel_Tts_Voice_Describe_Result mel_tts_voice_describe(Mel_Tts_Voice v);
bool                          mel_tts_voice_alive(Mel_Tts_Voice v);
bool                          mel_tts_voice_equal(Mel_Tts_Voice a, Mel_Tts_Voice b);

Mel_Tts_Speak_Result mel_tts_speak_opt(Mel_Tts_Voice v, str8 text, Mel_Tts_Speak_Opt opt);
#define mel_tts_speak(v, text, ...) mel_tts_speak_opt((v), (text), (Mel_Tts_Speak_Opt){ __VA_ARGS__ })

Mel_Tts_Speak_Result mel_tts_render_opt(Mel_Tts_Voice v, str8 text, Mel_Tts_Render_Opt opt);
#define mel_tts_render(v, text, ...) mel_tts_render_opt((v), (text), (Mel_Tts_Render_Opt){ __VA_ARGS__ })

Mel_Tts_Status mel_tts_pause(Mel_Tts_Utterance u);
Mel_Tts_Status mel_tts_resume(Mel_Tts_Utterance u);
void           mel_tts_abort(Mel_Tts_Utterance u);
void           mel_tts_abort_all(Mel_Tts_Voice v);

bool mel_tts_speaking(Mel_Tts_Utterance u);
bool mel_tts_paused(Mel_Tts_Utterance u);

void* mel_tts_voice_native(Mel_Tts_Voice v);

#ifdef __cplusplus
}
#endif
