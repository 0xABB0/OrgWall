#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef u32 Mel_Platform_Status;

#define MEL_PLATFORM_SEVERITY_MASK 0x3u
#define MEL_PLATFORM_OK            0u
#define MEL_PLATFORM_WARNED        1u
#define MEL_PLATFORM_ERROR         2u

#define MEL_PLATFORM_UNSUPPORTED   (1u << 2)
#define MEL_PLATFORM_UNAVAILABLE   (1u << 3)
#define MEL_PLATFORM_DENIED        (1u << 4)
#define MEL_PLATFORM_ALREADY       (1u << 5)
#define MEL_PLATFORM_INVALID       (1u << 6)

static inline bool mel_platform_status_failed(Mel_Platform_Status s) { return (s & MEL_PLATFORM_SEVERITY_MASK) == MEL_PLATFORM_ERROR; }
static inline bool mel_platform_status_warned(Mel_Platform_Status s) { return (s & MEL_PLATFORM_SEVERITY_MASK) == MEL_PLATFORM_WARNED; }
static inline bool mel_platform_status_ok(Mel_Platform_Status s) { return (s & MEL_PLATFORM_SEVERITY_MASK) == MEL_PLATFORM_OK; }
static inline bool mel_platform_status_unsupported(Mel_Platform_Status s) { return (s & MEL_PLATFORM_UNSUPPORTED) != 0u; }
static inline bool mel_platform_status_denied(Mel_Platform_Status s) { return (s & MEL_PLATFORM_DENIED) != 0u; }

void mel_platform_init(const Mel_Alloc* alloc);
void mel_platform_shutdown(void);

const char* mel_platform_name(void);

enum
{
    MEL_PLATFORM_SANDBOX_NONE = 0u,
    MEL_PLATFORM_SANDBOX_FLATPAK = 1u << 0,
    MEL_PLATFORM_SANDBOX_SNAP = 1u << 1,
    MEL_PLATFORM_SANDBOX_APPLE = 1u << 2,
};

typedef struct
{
    u32         flags;
    const char* app_id;
} Mel_Platform_Sandbox;

Mel_Platform_Sandbox mel_platform_sandbox(void);
static inline bool   mel_platform_sandboxed(void) { return mel_platform_sandbox().flags != MEL_PLATFORM_SANDBOX_NONE; }

enum
{
    MEL_PLATFORM_DEVICE_UNKNOWN = 0u,
    MEL_PLATFORM_DEVICE_PHONE = 1u << 0,
    MEL_PLATFORM_DEVICE_TABLET = 1u << 1,
    MEL_PLATFORM_DEVICE_TV = 1u << 2,
    MEL_PLATFORM_DEVICE_DESKTOP = 1u << 3,
};

u32                mel_platform_device_class(void);
static inline bool mel_platform_is_tablet(void) { return (mel_platform_device_class() & MEL_PLATFORM_DEVICE_TABLET) != 0u; }
static inline bool mel_platform_is_tv(void) { return (mel_platform_device_class() & MEL_PLATFORM_DEVICE_TV) != 0u; }

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Platform_Inhibitor;

#define MEL_PLATFORM_INHIBITOR_NULL ((Mel_Platform_Inhibitor){ 0, 0 })

static inline bool mel_platform_inhibitor_valid(Mel_Platform_Inhibitor h) { return h.index != 0 || h.generation != 0; }

typedef struct
{
    const char* reason;
} Mel_Platform_Inhibit_Opt;

typedef struct
{
    Mel_Platform_Inhibitor value;
    Mel_Platform_Status    status;
} Mel_Platform_Inhibit_Result;

Mel_Platform_Inhibit_Result mel_platform_screensaver_inhibit_opt(Mel_Platform_Inhibit_Opt opt);
#define mel_platform_screensaver_inhibit(...) mel_platform_screensaver_inhibit_opt((Mel_Platform_Inhibit_Opt){ __VA_ARGS__ })

Mel_Platform_Status mel_platform_screensaver_uninhibit(Mel_Platform_Inhibitor h);
bool                mel_platform_screensaver_inhibited(void);

#ifdef __cplusplus
}
#endif
