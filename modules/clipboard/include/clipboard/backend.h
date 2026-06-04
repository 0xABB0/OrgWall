#pragma once

#include <clipboard/clipboard.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Clip_Job Mel_Clip_Job;
typedef struct Mel_Reactor  Mel_Reactor;

Mel_Reactor*     mel_clip__reactor(void);
const Mel_Alloc* mel_clip__alloc(void);

// The platform layer: one translation unit implements these per platform, build-time selected.
// The core links and calls them directly — there is exactly one clipboard per platform, so no
// runtime indirection. A platform that completes asynchronously resolves the job later.
bool  mel_clip__plat_available(void);
bool  mel_clip__plat_channel_supported(Mel_Clip_Channel ch);
void  mel_clip__plat_read(Mel_Clip_Job* job);
void  mel_clip__plat_write(Mel_Clip_Job* job);
void  mel_clip__plat_clear(Mel_Clip_Job* job);
void  mel_clip__plat_query(Mel_Clip_Job* job);
void  mel_clip__plat_has(Mel_Clip_Job* job);
u64   mel_clip__plat_sequence(Mel_Clip_Channel ch);
void* mel_clip__plat_native(void);

// The accessors the platform layer uses to read a job's inputs and build its result.
const Mel_Alloc* mel_clip_job_alloc(const Mel_Clip_Job* j);
u64              mel_clip_job_token(const Mel_Clip_Job* j);
Mel_Clip_Channel mel_clip_job_channel(const Mel_Clip_Job* j);
Mel_Clip_Job*    mel_clip__job_from_token(u64 token);

u32             mel_clip_job_request_count(const Mel_Clip_Job* j);
Mel_Clip_Format mel_clip_job_request(const Mel_Clip_Job* j, u32 i);
bool            mel_clip_job_wants(const Mel_Clip_Job* j, Mel_Clip_Format f);

u32          mel_clip_job_item_count(const Mel_Clip_Job* j);
u32          mel_clip_job_rep_count(const Mel_Clip_Job* j, u32 item);
Mel_Clip_Rep mel_clip_job_rep(const Mel_Clip_Job* j, u32 item, u32 rep);

void mel_clip_job_emit(Mel_Clip_Job* j, Mel_Clip_Format f, const void* bytes, usize len);
void mel_clip_job_emit_format(Mel_Clip_Job* j, Mel_Clip_Format f);
void mel_clip_job_set_present(Mel_Clip_Job* j, bool present);
void mel_clip_job_add_warning(Mel_Clip_Job* j, Mel_Clip_Status warn_bits);
void mel_clip_job_resolve(Mel_Clip_Job* j, Mel_Clip_Status s);

#ifdef __cplusplus
}
#endif
