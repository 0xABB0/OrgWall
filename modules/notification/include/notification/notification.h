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

typedef struct Mel_Executor Mel_Executor;
typedef struct Mel_Future   Mel_Future;

typedef u32 Mel_Notif_Status;

#define MEL_NOTIF_SEVERITY_MASK          0x3u
#define MEL_NOTIF_OK                     0u
#define MEL_NOTIF_WARNED                 1u
#define MEL_NOTIF_ERROR                  2u

#define MEL_NOTIF_WARN_ACTIONS_DROPPED   (1u << 2)
#define MEL_NOTIF_WARN_REPLY_DROPPED     (1u << 3)
#define MEL_NOTIF_WARN_IMAGE_DROPPED     (1u << 4)
#define MEL_NOTIF_WARN_PROGRESS_DROPPED  (1u << 5)
#define MEL_NOTIF_WARN_BADGE_DROPPED     (1u << 6)
#define MEL_NOTIF_WARN_SOUND_DROPPED     (1u << 7)
#define MEL_NOTIF_WARN_SCHEDULE_VOLATILE (1u << 8)
#define MEL_NOTIF_WARN_REPEAT_CLAMPED    (1u << 9)
#define MEL_NOTIF_WARN_DEFAULT_CHANNEL   (1u << 10)
#define MEL_NOTIF_WARN_UPDATE_REPOSTED   (1u << 11)

#define MEL_NOTIF_ERR_NO_PROVIDER        (1u << 16)
#define MEL_NOTIF_ERR_INVALID_ARG        (1u << 17)
#define MEL_NOTIF_ERR_DEAD_HANDLE        (1u << 18)
#define MEL_NOTIF_ERR_BACKEND_FAIL       (1u << 19)
#define MEL_NOTIF_ERR_NOT_AUTHORIZED     (1u << 20)
#define MEL_NOTIF_ERR_UNSUPPORTED        (1u << 21)

static inline bool mel_notif_failed(Mel_Notif_Status s) { return (s & MEL_NOTIF_SEVERITY_MASK) == MEL_NOTIF_ERROR; }
static inline bool mel_notif_warned(Mel_Notif_Status s) { return (s & MEL_NOTIF_SEVERITY_MASK) == MEL_NOTIF_WARNED; }

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Notif;

#define MEL_NOTIF_NULL ((Mel_Notif){ 0 })

enum
{
    MEL_NOTIF_CAP_ACTIONS = 1u << 0,
    MEL_NOTIF_CAP_REPLY = 1u << 1,
    MEL_NOTIF_CAP_ICON = 1u << 2,
    MEL_NOTIF_CAP_ATTACHMENT = 1u << 3,
    MEL_NOTIF_CAP_PROGRESS = 1u << 4,
    MEL_NOTIF_CAP_BADGE = 1u << 5,
    MEL_NOTIF_CAP_SOUND = 1u << 6,
    MEL_NOTIF_CAP_SCHEDULE = 1u << 7,
    MEL_NOTIF_CAP_SCHEDULE_PERSISTS = 1u << 8,
    MEL_NOTIF_CAP_REPEAT = 1u << 9,
    MEL_NOTIF_CAP_UPDATE = 1u << 10,
    MEL_NOTIF_CAP_CHANNELS = 1u << 11,
    MEL_NOTIF_CAP_PUSH = 1u << 12,
    MEL_NOTIF_CAP_AUTH = 1u << 13,
};

typedef u32 Mel_Notif_Caps;

enum
{
    MEL_NOTIF_ACTION_FOREGROUND = 1u << 0,
    MEL_NOTIF_ACTION_DESTRUCTIVE = 1u << 1,
    MEL_NOTIF_ACTION_TEXT_INPUT = 1u << 2,
};

typedef u32 Mel_Notif_Action_Flags;

typedef struct
{
    str8                   id;
    str8                   label;
    Mel_Notif_Action_Flags flags;
    str8                   input_placeholder;
} Mel_Notif_Action;

typedef struct
{
    const u8* rgba;
    u32       width;
    u32       height;
    str8      path;
} Mel_Notif_Image;

typedef struct
{
    bool present;
    bool indeterminate;
    f32  value;
} Mel_Notif_Progress;

typedef struct
{
    str8                    title;
    str8                    subtitle;
    str8                    body;
    str8                    channel;
    str8                    group;
    Mel_Notif_Image         icon;
    Mel_Notif_Image         attachment;
    const Mel_Notif_Action* actions;
    u32                     action_count;
    Mel_Notif_Progress      progress;
    str8                    sound_path;
    bool                    silent;
    bool                    has_badge;
    i32                     badge;
    str8                    payload;
} Mel_Notif_Content;

typedef struct
{
    u64 at_unix_ms;
    u64 interval_ms;
} Mel_Notif_Trigger;

typedef struct
{
    str8 id;
    str8 label;
    str8 description;
    bool high;
    bool silent;
} Mel_Notif_Channel_Opt;

typedef struct
{
    Mel_Notif        value;
    Mel_Notif_Status status;
} Mel_Notif_Result;

typedef struct mel_notif_auth mel_notif_auth;

extern const mel_notif_auth mel_notif_auth_granted;
extern const mel_notif_auth mel_notif_auth_provisional;
extern const mel_notif_auth mel_notif_auth_denied;
extern const mel_notif_auth mel_notif_auth_not_determined;

const char* mel_notif_auth_name(const mel_notif_auth* a);
bool        mel_notif_auth_is_granted(const mel_notif_auth* a);

void mel_notif_init(const Mel_Alloc* alloc, Mel_Executor* exec);
void mel_notif_shutdown(void);

bool           mel_notif_supported(void);
Mel_Notif_Caps mel_notif_caps(void);

const mel_notif_auth* mel_notif_authorization(void);
Mel_Future*           mel_notif_authorize(const Mel_Alloc* a);
const mel_notif_auth* mel_notif_future_auth(const Mel_Future* f);

Mel_Notif_Status mel_notif_channel_register(Mel_Notif_Channel_Opt opt);

Mel_Notif_Result mel_notif_post(const Mel_Notif_Content* content);
Mel_Notif_Result mel_notif_schedule(const Mel_Notif_Content* content, Mel_Notif_Trigger trigger);
Mel_Notif_Status mel_notif_update(Mel_Notif n, const Mel_Notif_Content* content);
Mel_Notif_Status mel_notif_cancel(Mel_Notif n);
void             mel_notif_cancel_all(void);

bool mel_notif_alive(Mel_Notif n);
bool mel_notif_equal(Mel_Notif a, Mel_Notif b);

#ifdef __cplusplus
}
#endif
