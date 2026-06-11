#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <string/str8.h>
#include <allocator/allocator.fwd.h>
#include <collection/array.h>
#include <collection/slotmap.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Executor Mel_Executor;

typedef u32 Mel_AudioOut_Status;

#define MEL_AUDIOOUT_SEVERITY_MASK 0x3u
#define MEL_AUDIOOUT_OK            0u
#define MEL_AUDIOOUT_WARNED        1u
#define MEL_AUDIOOUT_ERROR         2u

#define MEL_AUDIOOUT_RESULT_NO_DEVICE   (1u << 2)
#define MEL_AUDIOOUT_RESULT_LOST        (1u << 3)
#define MEL_AUDIOOUT_RESULT_UNSUPPORTED (1u << 4)

#define MEL_AUDIOOUT_WARN_LOCAL_ONLY (1u << 5)

static inline bool mel_audioout_status_failed(Mel_AudioOut_Status s) { return (s & MEL_AUDIOOUT_SEVERITY_MASK) == MEL_AUDIOOUT_ERROR; }
static inline bool mel_audioout_status_warned(Mel_AudioOut_Status s) { return (s & MEL_AUDIOOUT_SEVERITY_MASK) == MEL_AUDIOOUT_WARNED; }

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_AudioOut;

#define MEL_AUDIOOUT_NULL ((Mel_AudioOut){ 0 })

typedef struct mel_audioout_kind mel_audioout_kind;

extern const mel_audioout_kind mel_audioout_builtin;
extern const mel_audioout_kind mel_audioout_hdmi;
extern const mel_audioout_kind mel_audioout_usb;
extern const mel_audioout_kind mel_audioout_bluetooth;
extern const mel_audioout_kind mel_audioout_virtual;
extern const mel_audioout_kind mel_audioout_unknown;

const char* mel_audioout_kind_name(const mel_audioout_kind* k);

typedef struct
{
    bool volume;
} Mel_AudioOut_Caps;

typedef Mel_Array(u32) Mel_AudioOut_Rates;

typedef struct
{
    str8                     name;
    str8                     stable_id;
    const mel_audioout_kind* kind;
    u32                      channels;
    u32                      samplerate;
    Mel_AudioOut_Rates       samplerates;
    Mel_AudioOut_Caps        caps;
    const Mel_Alloc*         alloc;
} Mel_AudioOut_Descriptor;

typedef struct
{
    Mel_AudioOut_Descriptor value;
    Mel_AudioOut_Status     status;
} Mel_AudioOut_Describe_Result;

typedef struct
{
    Mel_AudioOut             device;
    const mel_audioout_kind* kind;
    bool                     added;
    bool                     removed;
    bool                     changed;
    bool                     default_changed;
} Mel_AudioOut_Event;

typedef struct
{
    Mel_SlotMap_Handle handle;
} Mel_AudioOut_Hotplug_Sub;

#define MEL_AUDIOOUT_HOTPLUG_SUB_NULL ((Mel_AudioOut_Hotplug_Sub){ MEL_SLOTMAP_HANDLE_NULL })

typedef void (*Mel_AudioOut_Event_Callback)(const Mel_AudioOut_Event* ev, void* user);

void mel_audioout_init(const Mel_Alloc* alloc, Mel_Executor* deliver);
void mel_audioout_shutdown(void);

u32          mel_audioout_refresh(void);
u32          mel_audioout_count(void);
u32          mel_audioout_list(Mel_AudioOut* out, u32 cap);
Mel_AudioOut mel_audioout_default(void);
Mel_AudioOut mel_audioout_find(str8 stable_id);

Mel_AudioOut_Describe_Result mel_audioout_describe(Mel_AudioOut d, const Mel_Alloc* a);
void                         mel_audioout_describe_free(Mel_AudioOut_Describe_Result* r);
bool                         mel_audioout_alive(Mel_AudioOut d);
bool                         mel_audioout_equal(Mel_AudioOut a, Mel_AudioOut b);

f32                 mel_audioout_volume(Mel_AudioOut d);
Mel_AudioOut_Status mel_audioout_set_volume(Mel_AudioOut d, f32 volume);
bool                mel_audioout_muted(Mel_AudioOut d);
Mel_AudioOut_Status mel_audioout_set_muted(Mel_AudioOut d, bool muted);

Mel_AudioOut_Hotplug_Sub mel_audioout_subscribe(Mel_Executor* exec, Mel_AudioOut_Event_Callback cb, void* user);
void                     mel_audioout_unsubscribe(Mel_AudioOut_Hotplug_Sub sub);

typedef struct
{
    Mel_SlotMap_Handle handle;
} Mel_AudioOut_Published;

#define MEL_AUDIOOUT_PUBLISHED_NULL ((Mel_AudioOut_Published){ MEL_SLOTMAP_HANDLE_NULL })

typedef struct
{
    str8 name;
    u32  channels;
    u32  samplerate;
    u32  ring_capacity_frames;
} Mel_AudioOut_Publish_Opt;

typedef struct
{
    Mel_AudioOut_Published published;
    Mel_AudioOut           device;
    Mel_AudioOut_Status    status;
} Mel_AudioOut_Publish_Result;

MEL_NODISCARD Mel_AudioOut_Publish_Result mel_audioout_publish(const Mel_Alloc* a, Mel_AudioOut_Publish_Opt opt);

u32  mel_audioout_publish_read(Mel_AudioOut_Published p, f32* interleaved_dst, u32 max_frames);
bool mel_audioout_publish_os_visible(Mel_AudioOut_Published p);
void mel_audioout_unpublish(Mel_AudioOut_Published p);

void* mel_audioout_native(Mel_AudioOut d);

#ifdef __cplusplus
}
#endif
