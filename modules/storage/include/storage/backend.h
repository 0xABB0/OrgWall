#pragma once

#include <core/types.h>
#include <string/str8.h>
#include <future/future.h>

#include <storage/storage.h>
#include <storage/status.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Storage_Job Mel_Storage_Job;

typedef u32 Mel_Storage_Job_Kind;

#define MEL_STORAGE_JOB_READ      0u
#define MEL_STORAGE_JOB_WRITE     1u
#define MEL_STORAGE_JOB_SIZE      2u
#define MEL_STORAGE_JOB_META      3u
#define MEL_STORAGE_JOB_ENUMERATE 4u
#define MEL_STORAGE_JOB_GLOB      5u
#define MEL_STORAGE_JOB_MKDIR     6u
#define MEL_STORAGE_JOB_REMOVE    7u
#define MEL_STORAGE_JOB_RENAME    8u
#define MEL_STORAGE_JOB_COPY      9u
#define MEL_STORAGE_JOB_SPACE     10u

Mel_Storage_Job_Kind mel_storage_job_kind(const Mel_Storage_Job* job);
str8                 mel_storage_job_path(const Mel_Storage_Job* job);
str8                 mel_storage_job_path_b(const Mel_Storage_Job* job);
str8                 mel_storage_job_pattern(const Mel_Storage_Job* job);
const u8*            mel_storage_job_write_data(const Mel_Storage_Job* job);
usize                mel_storage_job_write_len(const Mel_Storage_Job* job);
usize                mel_storage_job_read_expect(const Mel_Storage_Job* job);
const Mel_Alloc*     mel_storage_job_alloc(const Mel_Storage_Job* job);
Mel_Reactor*         mel_storage_job_reactor(const Mel_Storage_Job* job);
Mel_Executor*        mel_storage_job_deliver(const Mel_Storage_Job* job);
bool                 mel_storage_job_create_parents(const Mel_Storage_Job* job);
bool                 mel_storage_job_atomic(const Mel_Storage_Job* job);
bool                 mel_storage_job_parents(const Mel_Storage_Job* job);
bool                 mel_storage_job_recursive(const Mel_Storage_Job* job);
bool                 mel_storage_job_overwrite(const Mel_Storage_Job* job);
bool                 mel_storage_job_case_insensitive(const Mel_Storage_Job* job);
bool                 mel_storage_job_stat_entries(const Mel_Storage_Job* job);
u32                  mel_storage_job_batch(const Mel_Storage_Job* job);
Mel_Storage_Enum_Cb  mel_storage_job_on_batch(const Mel_Storage_Job* job);
void*                mel_storage_job_stream_user(const Mel_Storage_Job* job);

void mel_storage_job_settle_bytes(Mel_Storage_Job* job, u8* data, usize len, Mel_Storage_Status status);
void mel_storage_job_settle_size(Mel_Storage_Job* job, u64 value, Mel_Storage_Status status);
void mel_storage_job_settle_void(Mel_Storage_Job* job, Mel_Storage_Status status);
void mel_storage_job_settle_meta(Mel_Storage_Job* job, Mel_Storage_Meta value, Mel_Storage_Status status);
void mel_storage_job_settle_space(Mel_Storage_Job* job, Mel_Storage_Space value, Mel_Storage_Status status);
void mel_storage_job_settle_list(Mel_Storage_Job* job, Mel_Storage_Entry* entries, u32 count, Mel_Storage_Status status);

typedef struct Mel_Storage_Interface
{
    const char* name;

    bool (*ready)(Mel_Storage* st, void* user);

    void (*submit)(Mel_Storage* st, void* user, Mel_Storage_Job* job);
    void (*cancel)(Mel_Storage* st, void* user, Mel_Storage_Job* job);

    void (*destroy)(Mel_Storage* st, void* user);
} Mel_Storage_Interface;

const Mel_Storage_Interface* mel_storage_fs_interface(void);

const Mel_Storage_Interface* mel_storage_title_interface(void);

bool mel_storage__title_native_available(void);

#ifdef __cplusplus
}
#endif
