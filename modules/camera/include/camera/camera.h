#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <string/str8.h>
#include <allocator/allocator.fwd.h>
#include <collection.array/array.h>
#include <collection.slotmap/slotmap.fwd.h>

#include <image/image.h>
#include <image/geometry.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Reactor  Mel_Reactor;
typedef struct Mel_Executor Mel_Executor;
typedef struct Mel_Future   Mel_Future;
typedef struct Mel_Event    Mel_Event;

typedef u32 Mel_Camera_Status;

#define MEL_CAMERA_SEVERITY_MASK    0x3u
#define MEL_CAMERA_OK               0u
#define MEL_CAMERA_WARNED           1u
#define MEL_CAMERA_ERROR            2u

#define MEL_CAMERA_RESULT_DENIED    (1u << 2)
#define MEL_CAMERA_RESULT_NO_DEVICE (1u << 3)
#define MEL_CAMERA_RESULT_BUSY      (1u << 4)
#define MEL_CAMERA_RESULT_UNSUPPORTED (1u << 5)
#define MEL_CAMERA_RESULT_LOST      (1u << 6)
#define MEL_CAMERA_RESULT_CANCELLED (1u << 7)

static inline bool mel_camera_status_failed(Mel_Camera_Status s) { return (s & MEL_CAMERA_SEVERITY_MASK) == MEL_CAMERA_ERROR; }
static inline bool mel_camera_status_warned(Mel_Camera_Status s) { return (s & MEL_CAMERA_SEVERITY_MASK) == MEL_CAMERA_WARNED; }

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Camera;

#define MEL_CAMERA_NULL ((Mel_Camera){ 0 })

typedef struct mel_camera_facing mel_camera_facing;

extern const mel_camera_facing mel_camera_front;
extern const mel_camera_facing mel_camera_back;
extern const mel_camera_facing mel_camera_external;
extern const mel_camera_facing mel_camera_unknown;

const char* mel_camera_facing_name(const mel_camera_facing* f);

typedef struct mel_camera_auth mel_camera_auth;

extern const mel_camera_auth mel_camera_auth_granted;
extern const mel_camera_auth mel_camera_auth_denied;
extern const mel_camera_auth mel_camera_auth_not_determined;
extern const mel_camera_auth mel_camera_auth_restricted;

const char* mel_camera_auth_name(const mel_camera_auth* a);
bool        mel_camera_auth_is_granted(const mel_camera_auth* a);

typedef struct
{
    const mel_image_format* format;
    i32                     width, height;
    f32                     fps_min, fps_max;
} Mel_Camera_Mode;

typedef Mel_Array(Mel_Camera_Mode) Mel_Camera_Modes;

typedef struct
{
    str8                     name;
    const mel_camera_facing* facing;
    Mel_Camera_Modes         modes;
    const Mel_Alloc*         alloc;
} Mel_Camera_Descriptor;

typedef struct
{
    Mel_Camera_Descriptor value;
    Mel_Camera_Status     status;
} Mel_Camera_Describe_Result;

typedef struct
{
    const mel_image_format* format;
    i32                     width, height;
    f32                     fps;
} Mel_Camera_Config;

typedef struct
{
    Mel_Image        image;
    u64              timestamp_ns;
    u64              sequence;
    Mel_Image_Orient orient;
} Mel_Camera_Frame;

typedef struct
{
    Mel_Camera               camera;
    const mel_camera_facing* facing;
    bool                     added;
    bool                     removed;
    bool                     changed;
} Mel_Camera_Event;

typedef struct
{
    Mel_SlotMap_Handle handle;
} Mel_Camera_Hotplug_Sub;

typedef struct
{
    Mel_SlotMap_Handle handle;
} Mel_Camera_Frame_Sub;

#define MEL_CAMERA_HOTPLUG_SUB_NULL ((Mel_Camera_Hotplug_Sub){ MEL_SLOTMAP_HANDLE_NULL })
#define MEL_CAMERA_FRAME_SUB_NULL   ((Mel_Camera_Frame_Sub){ MEL_SLOTMAP_HANDLE_NULL })

typedef void (*Mel_Camera_Event_Callback)(const Mel_Camera_Event* ev, void* user);
typedef void (*Mel_Camera_Frame_Callback)(const Mel_Camera_Frame* frame, void* user);

void mel_camera_init(const Mel_Alloc* alloc, Mel_Reactor* reactor);
void mel_camera_shutdown(void);

u32 mel_camera_refresh(void);
u32 mel_camera_count(void);
u32 mel_camera_list(Mel_Camera* out, u32 cap);

Mel_Camera_Describe_Result mel_camera_describe(Mel_Camera c, const Mel_Alloc* a);
void                       mel_camera_describe_free(Mel_Camera_Describe_Result* r);
bool                       mel_camera_alive(Mel_Camera c);
bool                       mel_camera_equal(Mel_Camera a, Mel_Camera b);

Mel_Camera_Hotplug_Sub mel_camera_subscribe(Mel_Executor* exec, Mel_Camera_Event_Callback cb, void* user);
void                   mel_camera_unsubscribe(Mel_Camera_Hotplug_Sub sub);

const mel_camera_auth* mel_camera_authorization(void);
Mel_Future*            mel_camera_authorize(const Mel_Alloc* a);
const mel_camera_auth* mel_camera_future_auth(const Mel_Future* f);

Mel_Future* mel_camera_open(Mel_Camera c, Mel_Camera_Config cfg, const Mel_Alloc* a);
Mel_Future* mel_camera_start(Mel_Camera c, const Mel_Alloc* a);
Mel_Future* mel_camera_stop(Mel_Camera c, const Mel_Alloc* a);
void        mel_camera_close(Mel_Camera c);

Mel_Camera_Status mel_camera_future_status(const Mel_Future* f);
void              mel_camera_future_free(Mel_Future* f);

Mel_Event*           mel_camera_frames(Mel_Camera c);
Mel_Camera_Frame_Sub mel_camera_frame_subscribe(Mel_Camera c, Mel_Camera_Frame_Callback cb, void* user);
void                 mel_camera_frame_unsubscribe(Mel_Camera c, Mel_Camera_Frame_Sub sub);
bool                 mel_camera_frame_pull(Mel_Camera c, Mel_Camera_Frame* out);

void* mel_camera_native(Mel_Camera c);

#ifdef __cplusplus
}
#endif
