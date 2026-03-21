#pragma once

#include "core.types.h"
#include "async.job.fwd.h"
#include "async.job.cfg.h"
#include "async.signal.fwd.h"

typedef struct { u8 worker; } Mel_Job_Run_Opt;

typedef struct {
    bool on_worker;
    bool on_fiber;
    bool has_current_job;
    u8 worker_index;
    u16 fiber_index;
    const char* fiber_state;
    void* task;
    const char* debug_name;
    const char* debug_file;
    u32 debug_line;
    u16 resume_fiber_index;
    const char* resume_reason;
} Mel_Job_Debug_Info;

void mel_job_run_opt_debug(void* data, Mel_Job_Fn fn,
                           const char* debug_name,
                           const char* debug_file,
                           u32 debug_line,
                           Mel_Counter* on_finish,
                           Mel_Job_Run_Opt opt);
void mel_job_run_opt(void* data, Mel_Job_Fn fn, Mel_Counter* on_finish, Mel_Job_Run_Opt opt);
#define mel_job_run(data, fn, counter, ...) \
    mel_job_run_opt_debug((data), (fn), #fn, __FILE__, __LINE__, (counter), (Mel_Job_Run_Opt){.worker = 0xFF, __VA_ARGS__})

void mel_job_run_n_debug(void* data, Mel_Job_Fn fn,
                         const char* debug_name,
                         const char* debug_file,
                         u32 debug_line,
                         Mel_Counter* on_finish,
                         u32 n);
void mel_job_run_n_impl(void* data, Mel_Job_Fn fn, Mel_Counter* on_finish, u32 n);
#define mel_job_run_n(data, fn, counter, n) \
    mel_job_run_n_debug((data), (fn), #fn, __FILE__, __LINE__, (counter), (n))
void mel_job_move_to_worker(u8 worker_index);
void mel_job_yield(void);
u8   mel_job_worker_count(void);
u8   mel_job_current_worker(void);
bool mel_job_is_worker_fiber(void);
bool mel_job_debug_current(Mel_Job_Debug_Info* out);

void mel_job_init(void);
void mel_job_shutdown(void);
