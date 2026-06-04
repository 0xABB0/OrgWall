#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <collection/slotmap.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Reactor  Mel_Reactor;
typedef struct Mel_Executor Mel_Executor;

typedef u32 Mel_App_Status;

#define MEL_APP_SEVERITY_MASK 0x3u
#define MEL_APP_OK            0u
#define MEL_APP_WARNED        1u
#define MEL_APP_ERROR         2u

#define MEL_APP_UNAVAILABLE   (1u << 2)
#define MEL_APP_NO_BACKEND    (1u << 3)
#define MEL_APP_WRONG_THREAD  (1u << 4)

static inline bool mel_app_status_ok(Mel_App_Status s) { return (s & MEL_APP_SEVERITY_MASK) == MEL_APP_OK; }
static inline bool mel_app_status_warned(Mel_App_Status s) { return (s & MEL_APP_SEVERITY_MASK) == MEL_APP_WARNED; }
static inline bool mel_app_status_failed(Mel_App_Status s) { return (s & MEL_APP_SEVERITY_MASK) == MEL_APP_ERROR; }

enum
{
    MEL_APP_PHASE_WILL_TERMINATE = 1u << 0,
    MEL_APP_PHASE_LOW_MEMORY = 1u << 1,
    MEL_APP_PHASE_WILL_RESIGN_ACTIVE = 1u << 2,
    MEL_APP_PHASE_DID_BECOME_ACTIVE = 1u << 3,
    MEL_APP_PHASE_DID_ENTER_BACKGROUND = 1u << 4,
    MEL_APP_PHASE_WILL_ENTER_FOREGROUND = 1u << 5,
};

typedef struct
{
    u64 monotonic_ns;
    u32 phase;
} Mel_App_Lifecycle_Event;

typedef struct
{
    Mel_SlotMap_Handle handle;
} Mel_App_Lifecycle_Subscription;

#define MEL_APP_LIFECYCLE_SUBSCRIPTION_NULL ((Mel_App_Lifecycle_Subscription){ MEL_SLOTMAP_HANDLE_NULL })

typedef void (*Mel_App_Lifecycle_Callback)(const Mel_App_Lifecycle_Event* ev, void* user);

u32 mel_app_lifecycle_poll(Mel_App_Lifecycle_Event* out, u32 cap);

Mel_App_Lifecycle_Subscription mel_app_lifecycle_subscribe(Mel_Executor* deliver, Mel_App_Lifecycle_Callback cb, void* user);
void                           mel_app_lifecycle_unsubscribe(Mel_App_Lifecycle_Subscription sub);

bool mel_app_active(void);
bool mel_app_foreground(void);

#ifdef __cplusplus
}
#endif
