#pragma once

#include <core/types.h>
#include <allocator/allocator.h>
#include <collection.array/array.h>
#include <collection.slotmap/slotmap.fwd.h>
#include <thread/spinlock.h>
#include <thread/thread.h>
#include <thread/sem.h>

#include <audio/engine.h>
#include <audio/source.h>
#include <audio/voice.h>
#include <audio/backend.h>

#include <future/future.h>
#include <event/event.h>

#include <stdatomic.h>

#define MEL_AUDIO__FADER_NONE   0u
#define MEL_AUDIO__FADER_LINEAR 1u
#define MEL_AUDIO__FADER_OSC    2u

#define MEL_AUDIO__VOICE_ENDED  (1u << 16)

#define MEL_AUDIO__SLOT_SENTINEL UINT32_MAX

typedef struct
{
    u32 active;
    u32 kind;
    f64 from;
    f64 to;
    f64 start_clock;
    f64 duration_frames;
    f64 period_frames;
    u32 on_complete_pause;
    u32 on_complete_stop;
} Mel_Audio__Scalar_Fade;

typedef struct
{
    Mel_SlotMap_Handle     self;
    Mel_Audio_Source*      source;
    void*                  instance;
    f64                    cursor;
    f64                    play_speed;
    f32                    volume;
    f32                    pan;
    f32                    gain_l;
    f32                    gain_r;
    u32                    flags;
    f32*                   tail;
    u32                    has_tail;
    Mel_Audio__Scalar_Fade fade_volume;
    Mel_Audio__Scalar_Fade fade_pan;
    Mel_Audio__Scalar_Fade fade_speed;
} Mel_Audio__Voice;

typedef enum
{
    MEL_AUDIO__SLOT_FREE = 0,
    MEL_AUDIO__SLOT_RESERVED = 1,
    MEL_AUDIO__SLOT_LIVE = 2,
} Mel_Audio__Slot_State;

typedef struct
{
    u32 generation;
    u32 packed_idx;
    u32 state;
    u32 next_free;
} Mel_Audio__Slot;

typedef struct
{
    Mel_Array(Mel_Audio__Voice) packed;
    Mel_Array(Mel_Audio__Slot) slots;
    u32              free_head;
    u32              occupancy;
    Mel_Spinlock     lock;
    const Mel_Alloc* alloc;
} Mel_Audio__Voice_Table;

typedef struct Mel_Audio__Command Mel_Audio__Command;

typedef void (*Mel_Audio__Command_Apply)(Mel_Audio* eng, const Mel_Audio__Command* cmd);

struct Mel_Audio__Command
{
    Mel_Audio__Command_Apply apply;
    Mel_SlotMap_Handle       handle;
    Mel_Audio_Source*        source;
    void*                    instance;
    f32*                     tail;
    Mel_Future*              fut;
    f32                      f0;
    f32                      f1;
    f64                      d0;
    f64                      d1;
    u32                      u0;
};

typedef Mel_Array(Mel_Audio__Command) Mel_Audio__Command_Queue;

typedef struct
{
    Mel_SlotMap_Handle handle;
    Mel_Future*        fut;
    u32                resolved;
} Mel_Audio__End_Future;

typedef Mel_Array(Mel_Audio__End_Future) Mel_Audio__End_Future_Reg;

struct Mel_Audio
{
    const Mel_Alloc* alloc;
    Mel_Reactor*     reactor;
    Mel_Executor*    exec;

    Mel_Audio_Caps      caps;
    Mel_Audio_Resampler resampler;
    u32                 online;
    _Atomic(u32)        destroying;
    _Atomic(u32)        api_inflight;

    f32                    master_volume;
    Mel_Audio__Scalar_Fade master_fade;

    f64 stream_clock;

    Mel_Audio__Voice_Table voices;

    Mel_Audio__Command_Queue commands;
    Mel_Audio__Command_Queue commands_back;
    Mel_Spinlock             command_lock;

    Mel_Audio__End_Future_Reg end_futures;
    Mel_Event*                device_events;

    f32* scratch_planar;
    f32* scratch_voice;
    f32* scratch_resampled;
    u32  scratch_frames;
    u32  scratch_fetch_frames;
    u32  scratch_channels;
    u32  worst_channels;
    f64  worst_ratio;

    Mel_Audio_Ring* ring;
    Mel_Thread      mix_thread;
    _Atomic(u32)    mix_stop;
    Mel_Sem         mix_wake;
};

static inline bool mel_audio__api_enter(Mel_Audio* eng)
{
    if (atomic_load_explicit(&eng->destroying, memory_order_acquire) != 0u)
        return false;
    atomic_fetch_add_explicit(&eng->api_inflight, 1u, memory_order_seq_cst);
    if (atomic_load_explicit(&eng->destroying, memory_order_seq_cst) != 0u)
    {
        atomic_fetch_sub_explicit(&eng->api_inflight, 1u, memory_order_seq_cst);
        return false;
    }
    return true;
}

static inline void mel_audio__api_leave(Mel_Audio* eng)
{
    atomic_fetch_sub_explicit(&eng->api_inflight, 1u, memory_order_seq_cst);
}

u32 mel_audio_resample_linear(const f32* src, u32 src_frames, f32* dst, u32 dst_frames, f64 ratio, f64* cursor);

void mel_audio__pan_gains(f32 pan, f32* out_l, f32* out_r);
void mel_audio__pan_accumulate(const f32* voice_planar, u32 src_channels, u32 frames, f32 gain_l, f32 gain_r, f32* planar_out, u32 out_channels);

void mel_audio__command_push(Mel_Audio* eng, const Mel_Audio__Command* cmd);
void mel_audio__commands_drain(Mel_Audio* eng);

void mel_audio__cmd_create(Mel_Audio* eng, const Mel_Audio__Command* cmd);
void mel_audio__cmd_set_volume(Mel_Audio* eng, const Mel_Audio__Command* cmd);
void mel_audio__cmd_set_pan(Mel_Audio* eng, const Mel_Audio__Command* cmd);
void mel_audio__cmd_set_speed(Mel_Audio* eng, const Mel_Audio__Command* cmd);
void mel_audio__cmd_set_paused(Mel_Audio* eng, const Mel_Audio__Command* cmd);
void mel_audio__cmd_set_loop(Mel_Audio* eng, const Mel_Audio__Command* cmd);
void mel_audio__cmd_seek(Mel_Audio* eng, const Mel_Audio__Command* cmd);
void mel_audio__cmd_stop(Mel_Audio* eng, const Mel_Audio__Command* cmd);
void mel_audio__cmd_stop_all(Mel_Audio* eng, const Mel_Audio__Command* cmd);
void mel_audio__cmd_fade_volume(Mel_Audio* eng, const Mel_Audio__Command* cmd);
void mel_audio__cmd_fade_pan(Mel_Audio* eng, const Mel_Audio__Command* cmd);
void mel_audio__cmd_fade_speed(Mel_Audio* eng, const Mel_Audio__Command* cmd);
void mel_audio__cmd_oscillate_volume(Mel_Audio* eng, const Mel_Audio__Command* cmd);
void mel_audio__cmd_schedule_pause(Mel_Audio* eng, const Mel_Audio__Command* cmd);
void mel_audio__cmd_schedule_stop(Mel_Audio* eng, const Mel_Audio__Command* cmd);
void mel_audio__cmd_fade_master(Mel_Audio* eng, const Mel_Audio__Command* cmd);
void mel_audio__cmd_attach_end_future(Mel_Audio* eng, const Mel_Audio__Command* cmd);
void mel_audio__cmd_release_end_future(Mel_Audio* eng, const Mel_Audio__Command* cmd);

void mel_audio__end_future_register(Mel_Audio* eng, Mel_SlotMap_Handle handle, Mel_Future* fut);
void mel_audio__end_future_resolve(Mel_Audio* eng, Mel_SlotMap_Handle handle, Mel_Future_Status status);
void mel_audio__end_future_release(Mel_Audio* eng, Mel_Future* fut);
void mel_audio__end_futures_free(Mel_Audio* eng);

f32  mel_audio__fade_eval(const Mel_Audio__Scalar_Fade* f, f64 clock, u32* done);

u32  mel_audio__mix_block(Mel_Audio* eng, f32* planar_out, u32 frames);

void               mel_audio__voices_init(Mel_Audio__Voice_Table* t, const Mel_Alloc* a, u32 initial_capacity);
void               mel_audio__voices_free(Mel_Audio__Voice_Table* t);
Mel_SlotMap_Handle mel_audio__voice_reserve(Mel_Audio* eng);
Mel_Audio__Voice*  mel_audio__voice_get(Mel_Audio__Voice_Table* t, Mel_SlotMap_Handle handle);
bool               mel_audio__voice_alive(const Mel_Audio__Voice_Table* t, Mel_SlotMap_Handle handle);
u32                mel_audio__voice_count(const Mel_Audio__Voice_Table* t);
bool               mel_audio__voice_activate(Mel_Audio* eng, Mel_SlotMap_Handle handle, const Mel_Audio__Voice* payload);
void               mel_audio__voice_remove(Mel_Audio* eng, Mel_SlotMap_Handle handle);
void               mel_audio__voice_remove_reserved(Mel_Audio* eng, Mel_SlotMap_Handle handle, Mel_Audio_Source* source, void* instance, f32* tail);

void mel_audio__scratch_size_worst(Mel_Audio* eng, u32 frames);
void mel_audio__scratch_ensure_offline(Mel_Audio* eng, u32 frames);
u32  mel_audio__fetch_frames_for(u32 frames, f64 ratio, f64 cursor_frac);

Mel_Audio_Ring* mel_audio_ring_create(const Mel_Alloc* a, u32 capacity_samples);
void            mel_audio_ring_destroy(Mel_Audio_Ring* r);
void            mel_audio_ring_set_wake(Mel_Audio_Ring* r, Mel_Sem* wake);
u32             mel_audio_ring_capacity(const Mel_Audio_Ring* r);
u32             mel_audio_ring_write_available(const Mel_Audio_Ring* r);
u32             mel_audio_ring_read_available(const Mel_Audio_Ring* r);
u32             mel_audio_ring_write(Mel_Audio_Ring* r, const f32* src, u32 count);
u32             mel_audio_ring_read(Mel_Audio_Ring* r, f32* dst, u32 count);
