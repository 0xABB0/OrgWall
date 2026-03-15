#pragma once

#include "core.types.h"
#include "async.job.fwd.h"
#include "async.job.cfg.h"
#include "async.signal.fwd.h"

typedef struct { u8 worker; } Mel_Job_Run_Opt;

void mel_job_run_opt(void* data, Mel_Job_Fn fn, Mel_Counter* on_finish, Mel_Job_Run_Opt opt);
#define mel_job_run(data, fn, counter, ...) \
    mel_job_run_opt((data), (fn), (counter), (Mel_Job_Run_Opt){.worker = 0xFF, __VA_ARGS__})

void mel_job_run_n(void* data, Mel_Job_Fn fn, Mel_Counter* on_finish, u32 n);
void mel_job_move_to_worker(u8 worker_index);
void mel_job_yield(void);
u8   mel_job_worker_count(void);
u8   mel_job_current_worker(void);
bool mel_job_is_worker_fiber(void);

void mel_job_init(void);
void mel_job_shutdown(void);
