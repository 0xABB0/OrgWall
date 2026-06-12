#pragma once

#include <core/types.h>
#include <allocator/allocator.h>
#include <collection/array.h>
#include <collection/slotmap.fwd.h>
#include <thread/spinlock.h>
#include <thread/thread.h>
#include <thread/sem.h>

#include <audiomixer/engine.h>
#include <audiomixer/source.h>
#include <audiomixer/voice.h>
#include <audiomixer/event.h>
#include <audiomixer/tap.h>

#include <audioout/audioout.h>
#include <audioout/events.h>
#include <audioplayback/audioplayback.h>
#include <audiopolicy/events.h>
#include <pcm/ring.h>

#include <future/future.h>
#include <event/event.h>

#include <stdatomic.h>

typedef struct Mel_Mixer_Ring Mel_Mixer_Ring;

#define MEL_MIXER__FADER_NONE    0u
#define MEL_MIXER__FADER_LINEAR  1u
#define MEL_MIXER__FADER_OSC     2u

#define MEL_MIXER__VOICE_ENDED   (1u << 16)

#define MEL_MIXER__SLOT_SENTINEL UINT32_MAX

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
} Mel_Mixer__Scalar_Fade;

typedef struct
{
    Mel_SlotMap_Handle     self;
    Mel_Mixer_Source*      source;
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
    Mel_Mixer__Scalar_Fade fade_volume;
    Mel_Mixer__Scalar_Fade fade_pan;
    Mel_Mixer__Scalar_Fade fade_speed;
} Mel_Mixer__Voice;

typedef enum
{
    MEL_MIXER__SLOT_FREE = 0,
    MEL_MIXER__SLOT_RESERVED = 1,
    MEL_MIXER__SLOT_LIVE = 2,
} Mel_Mixer__Slot_State;

typedef struct
{
    u32 generation;
    u32 packed_idx;
    u32 state;
    u32 next_free;
} Mel_Mixer__Slot;

typedef struct
{
    Mel_Array(Mel_Mixer__Voice) packed;
    Mel_Array(Mel_Mixer__Slot) slots;
    u32              free_head;
    u32              occupancy;
    Mel_Spinlock     lock;
    const Mel_Alloc* alloc;
} Mel_Mixer__Voice_Table;

typedef struct Mel_Mixer__Command Mel_Mixer__Command;

typedef void (*Mel_Mixer__Command_Apply)(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);

struct Mel_Mixer__Command
{
    Mel_Mixer__Command_Apply apply;
    Mel_SlotMap_Handle       handle;
    Mel_Mixer_Source*        source;
    void*                    instance;
    f32*                     tail;
    Mel_Future*              fut;
    f32                      f0;
    f32                      f1;
    f64                      d0;
    f64                      d1;
    u32                      u0;
};

typedef Mel_Array(Mel_Mixer__Command) Mel_Mixer__Command_Queue;

typedef struct
{
    Mel_SlotMap_Handle handle;
    Mel_Future*        fut;
    u32                resolved;
} Mel_Mixer__End_Future;

typedef Mel_Array(Mel_Mixer__End_Future) Mel_Mixer__End_Future_Reg;

struct Mel_Mixer_Tap
{
    Mel_Mixer*         eng;
    const Mel_Alloc*   alloc;
    Mel_Pcm_Ring*      ring;
    Mel_SlotMap_Handle voice;
    u32                is_voice;
    _Atomic(u64)       dropped;
};

typedef Mel_Array(Mel_Mixer_Tap*) Mel_Mixer__Tap_List;

struct Mel_Mixer
{
    const Mel_Alloc* alloc;
    Mel_Executor*    exec;

    Mel_Mixer_Caps      caps;
    Mel_Mixer_Resampler resampler;
    u32                 online;
    _Atomic(u32)        destroying;
    _Atomic(u32)        api_inflight;

    f32                    master_volume;
    Mel_Mixer__Scalar_Fade master_fade;

    f64 stream_clock;

    Mel_Mixer__Voice_Table voices;

    Mel_Mixer__Command_Queue commands;
    Mel_Mixer__Command_Queue commands_back;
    Mel_Spinlock             command_lock;

    Mel_Mixer__End_Future_Reg end_futures;
    Mel_Event*                device_events;

    f32* scratch_planar;
    f32* scratch_voice;
    f32* scratch_resampled;
    u32  scratch_frames;
    u32  scratch_fetch_frames;
    u32  scratch_channels;
    u32  worst_channels;
    f64  worst_ratio;

    Mel_Mixer_Ring* ring;
    Mel_Thread      mix_thread;
    _Atomic(u32)    mix_stop;
    Mel_Sem         mix_wake;

    Mel_AudioPlayback* playback;
    Mel_AudioOut       bound;
    u32                follow;
    u32                interrupted;
    u32                native_rate;
    u32                native_channels;
    _Atomic(u32)       device_bits;
    _Atomic(u64)       device_underruns;

    Mel_AudioOut_Hotplug_Sub out_sub;
    Mel_AudioPolicy_Sub      policy_sub;
    u32                      policy_bound;

    Mel_Mixer__Tap_List taps;
    f32*                scratch_tap_planar;
    f32*                scratch_tap_inter;
    u32                 scratch_tap_frames;
    u32                 scratch_tap_channels;
};

static inline bool mel_mixer__api_enter(Mel_Mixer* eng)
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

static inline void mel_mixer__api_leave(Mel_Mixer* eng) { atomic_fetch_sub_explicit(&eng->api_inflight, 1u, memory_order_seq_cst); }

u32 mel_mixer_resample_linear(const f32* src, u32 src_frames, f32* dst, u32 dst_frames, f64 ratio, f64* cursor);

void mel_mixer__pan_gains(f32 pan, f32* out_l, f32* out_r);
void mel_mixer__pan_accumulate(const f32* voice_planar, u32 src_channels, u32 frames, f32 gain_l, f32 gain_r, f32* planar_out, u32 out_channels);

void mel_mixer__command_push(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);
void mel_mixer__commands_drain(Mel_Mixer* eng);

void mel_mixer__cmd_create(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);
void mel_mixer__cmd_set_volume(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);
void mel_mixer__cmd_set_pan(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);
void mel_mixer__cmd_set_speed(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);
void mel_mixer__cmd_set_paused(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);
void mel_mixer__cmd_set_loop(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);
void mel_mixer__cmd_seek(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);
void mel_mixer__cmd_stop(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);
void mel_mixer__cmd_stop_all(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);
void mel_mixer__cmd_fade_volume(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);
void mel_mixer__cmd_fade_pan(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);
void mel_mixer__cmd_fade_speed(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);
void mel_mixer__cmd_oscillate_volume(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);
void mel_mixer__cmd_schedule_pause(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);
void mel_mixer__cmd_schedule_stop(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);
void mel_mixer__cmd_fade_master(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);
void mel_mixer__cmd_attach_end_future(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);
void mel_mixer__cmd_release_end_future(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);
void mel_mixer__cmd_tap_attach(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);
void mel_mixer__cmd_tap_detach(Mel_Mixer* eng, const Mel_Mixer__Command* cmd);

bool mel_mixer__device_open(Mel_Mixer* eng, Mel_AudioOut target, Mel_Mixer_Status* status);
void mel_mixer__device_close(Mel_Mixer* eng);
void mel_mixer__device_subscribe(Mel_Mixer* eng);
void mel_mixer__device_unsubscribe(Mel_Mixer* eng);
void mel_mixer__device_event_fire(Mel_Mixer* eng, Mel_Mixer_Device_Event ev);

void mel_mixer__taps_master_write(Mel_Mixer* eng, const f32* planar_out, u32 frames);
void mel_mixer__taps_voice_write(Mel_Mixer* eng, Mel_SlotMap_Handle voice, u32 src_channels, u32 frames, f32 gain_l, f32 gain_r);
bool mel_mixer__tap_scratch_ensure(Mel_Mixer* eng, u32 frames);
void mel_mixer__taps_free_all(Mel_Mixer* eng);

void mel_mixer__end_future_register(Mel_Mixer* eng, Mel_SlotMap_Handle handle, Mel_Future* fut);
void mel_mixer__end_future_resolve(Mel_Mixer* eng, Mel_SlotMap_Handle handle, Mel_Future_Status status);
void mel_mixer__end_future_release(Mel_Mixer* eng, Mel_Future* fut);
void mel_mixer__end_futures_free(Mel_Mixer* eng);

f32 mel_mixer__fade_eval(const Mel_Mixer__Scalar_Fade* f, f64 clock, u32* done);

u32 mel_mixer__mix_block(Mel_Mixer* eng, f32* planar_out, u32 frames);

void               mel_mixer__voices_init(Mel_Mixer__Voice_Table* t, const Mel_Alloc* a, u32 initial_capacity);
void               mel_mixer__voices_free(Mel_Mixer__Voice_Table* t);
Mel_SlotMap_Handle mel_mixer__voice_reserve(Mel_Mixer* eng);
Mel_Mixer__Voice*  mel_mixer__voice_get(Mel_Mixer__Voice_Table* t, Mel_SlotMap_Handle handle);
bool               mel_mixer__voice_alive(const Mel_Mixer__Voice_Table* t, Mel_SlotMap_Handle handle);
u32                mel_mixer__voice_count(const Mel_Mixer__Voice_Table* t);
bool               mel_mixer__voice_activate(Mel_Mixer* eng, Mel_SlotMap_Handle handle, const Mel_Mixer__Voice* payload);
void               mel_mixer__voice_remove(Mel_Mixer* eng, Mel_SlotMap_Handle handle);
void               mel_mixer__voice_remove_reserved(Mel_Mixer* eng, Mel_SlotMap_Handle handle, Mel_Mixer_Source* source, void* instance, f32* tail);

void mel_mixer__scratch_size_worst(Mel_Mixer* eng, u32 frames);
void mel_mixer__scratch_ensure_offline(Mel_Mixer* eng, u32 frames);
u32  mel_mixer__fetch_frames_for(u32 frames, f64 ratio, f64 cursor_frac);

Mel_Mixer_Ring* mel_mixer_ring_create(const Mel_Alloc* a, u32 capacity_samples);
void            mel_mixer_ring_destroy(Mel_Mixer_Ring* r);
void            mel_mixer_ring_set_wake(Mel_Mixer_Ring* r, Mel_Sem* wake);
u32             mel_mixer_ring_capacity(const Mel_Mixer_Ring* r);
u32             mel_mixer_ring_write_available(const Mel_Mixer_Ring* r);
u32             mel_mixer_ring_read_available(const Mel_Mixer_Ring* r);
u32             mel_mixer_ring_write(Mel_Mixer_Ring* r, const f32* src, u32 count);
u32             mel_mixer_ring_read(Mel_Mixer_Ring* r, f32* dst, u32 count);
