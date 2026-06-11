#pragma once

#include <coro/coro.h>
#include <core/types.h>

#include "lab.h"

/* >>> mel_coro generated frames — managed region, do not edit >>> */
typedef struct Mel_Coro_Frame_lab_pump
{
    i32 state;
    Lab_Job * job;
    i64 __ret;
} Mel_Coro_Frame_lab_pump;

#define MEL_CORO_LAYOUT_HASH_lab_pump 0x961d38a188b592fdull

Mel_Coro_Suspended lab_pump__resume(Mel_Coro_Frame_lab_pump* __f, long long* __f_out);

typedef struct Mel_Coro_Frame_lab_race_run
{
    i32 state;
    Lab_Race * race;
    i32 i;
    Mel_Coro_Frame_lab_pump child;
    i64 __ret;
} Mel_Coro_Frame_lab_race_run;

#define MEL_CORO_LAYOUT_HASH_lab_race_run 0x06be54d6706bd43bull

Mel_Coro_Suspended lab_race_run__resume(Mel_Coro_Frame_lab_race_run* __f, long long* __f_out);

/* <<< mel_coro generated frames <<< */

mel_coro(lab_pump, (Lab_Job* job), i64)
{
    while (!job->finished && !job->failed)
    {
        lab_job_chunk(job);
        mel_coro_yield((i64)job->in_consumed);
    }
    mel_coro_return((i64)job->out_len);
}

mel_coro(lab_race_run, (Lab_Race* race), i64)
{
    for (i32 i = 0; i < (i32)race->count; i++)
    {
        lab_race_arm(race, i);
        Mel_Coro_Frame_lab_pump child = { 0 };
        child.job = &race->job;
        mel_coro_await(child);
        lab_race_settle(race, i);
    }
    mel_coro_return(0);
}
