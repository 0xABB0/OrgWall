#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <allocator/allocator.fwd.h>
#include <collection/slotmap.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Executor Mel_Executor;

typedef u32 Mel_AudioPolicy_Status;

#define MEL_AUDIOPOLICY_SEVERITY_MASK 0x3u
#define MEL_AUDIOPOLICY_OK            0u
#define MEL_AUDIOPOLICY_WARNED        1u
#define MEL_AUDIOPOLICY_ERROR         2u

#define MEL_AUDIOPOLICY_WARN_CATEGORY_LOWERED  (1u << 2)
#define MEL_AUDIOPOLICY_WARN_MODE_IGNORED      (1u << 3)
#define MEL_AUDIOPOLICY_WARN_MIX_IGNORED       (1u << 4)
#define MEL_AUDIOPOLICY_WARN_DUCK_IGNORED      (1u << 5)
#define MEL_AUDIOPOLICY_WARN_BLUETOOTH_IGNORED (1u << 6)
#define MEL_AUDIOPOLICY_WARN_OVERRIDE_IGNORED  (1u << 7)

#define MEL_AUDIOPOLICY_RESULT_UNSUPPORTED (1u << 8)
#define MEL_AUDIOPOLICY_RESULT_BUSY        (1u << 9)

static inline bool mel_audiopolicy_status_failed(Mel_AudioPolicy_Status s) { return (s & MEL_AUDIOPOLICY_SEVERITY_MASK) == MEL_AUDIOPOLICY_ERROR; }
static inline bool mel_audiopolicy_status_warned(Mel_AudioPolicy_Status s) { return (s & MEL_AUDIOPOLICY_SEVERITY_MASK) == MEL_AUDIOPOLICY_WARNED; }

typedef struct mel_audiopolicy_category mel_audiopolicy_category;

extern const mel_audiopolicy_category mel_audiopolicy_playback;
extern const mel_audiopolicy_category mel_audiopolicy_record;
extern const mel_audiopolicy_category mel_audiopolicy_duplex;
extern const mel_audiopolicy_category mel_audiopolicy_ambient;

const char* mel_audiopolicy_category_name(const mel_audiopolicy_category* c);

typedef struct mel_audiopolicy_mode mel_audiopolicy_mode;

extern const mel_audiopolicy_mode mel_audiopolicy_mode_default;
extern const mel_audiopolicy_mode mel_audiopolicy_mode_voice_chat;
extern const mel_audiopolicy_mode mel_audiopolicy_mode_video_chat;
extern const mel_audiopolicy_mode mel_audiopolicy_mode_measurement;
extern const mel_audiopolicy_mode mel_audiopolicy_mode_media;

const char* mel_audiopolicy_mode_name(const mel_audiopolicy_mode* m);

typedef struct mel_audiopolicy_output mel_audiopolicy_output;

extern const mel_audiopolicy_output mel_audiopolicy_output_default;
extern const mel_audiopolicy_output mel_audiopolicy_output_speaker;

typedef struct mel_audiopolicy_route_reason mel_audiopolicy_route_reason;

extern const mel_audiopolicy_route_reason mel_audiopolicy_route_device_added;
extern const mel_audiopolicy_route_reason mel_audiopolicy_route_device_removed;
extern const mel_audiopolicy_route_reason mel_audiopolicy_route_category_changed;
extern const mel_audiopolicy_route_reason mel_audiopolicy_route_override;
extern const mel_audiopolicy_route_reason mel_audiopolicy_route_unknown;

const char* mel_audiopolicy_route_reason_name(const mel_audiopolicy_route_reason* r);

typedef struct
{
    const mel_audiopolicy_category* category;
    const mel_audiopolicy_mode*     mode;
    bool                            mix_with_others;
    bool                            duck_others;
    bool                            default_to_speaker;
    bool                            allow_bluetooth;
    bool                            allow_bluetooth_a2dp;
} Mel_AudioPolicy;

typedef struct
{
    bool may_duck_me;
} Mel_AudioPolicy_Focus_Opt;

typedef struct
{
    bool                                interruption_began;
    bool                                interruption_ended;
    bool                                should_resume;
    bool                                should_duck;
    bool                                focus_lost;
    bool                                focus_gained;
    bool                                route_changed;
    const mel_audiopolicy_route_reason* reason;
} Mel_AudioPolicy_Event;

typedef struct
{
    Mel_SlotMap_Handle handle;
} Mel_AudioPolicy_Sub;

#define MEL_AUDIOPOLICY_SUB_NULL ((Mel_AudioPolicy_Sub){ MEL_SLOTMAP_HANDLE_NULL })

typedef void (*Mel_AudioPolicy_Event_Callback)(const Mel_AudioPolicy_Event* ev, void* user);

void mel_audiopolicy_init(const Mel_Alloc* alloc, Mel_Executor* deliver);
void mel_audiopolicy_shutdown(void);

Mel_AudioPolicy_Status mel_audiopolicy_apply(Mel_AudioPolicy policy);
Mel_AudioPolicy        mel_audiopolicy_current(void);

Mel_AudioPolicy_Status mel_audiopolicy_override_output(const mel_audiopolicy_output* port);

Mel_AudioPolicy_Status mel_audiopolicy_focus_request(Mel_AudioPolicy_Focus_Opt opt);
void                   mel_audiopolicy_focus_abandon(void);

Mel_AudioPolicy_Sub mel_audiopolicy_subscribe(Mel_Executor* exec, Mel_AudioPolicy_Event_Callback cb, void* user);
void                mel_audiopolicy_unsubscribe(Mel_AudioPolicy_Sub sub);

#ifdef __cplusplus
}
#endif
