#pragma once

#include <compress/compress.h>
#include <time/nano.h>

typedef struct
{
    const Mel_Compress_Codec* codec;
    const Mel_Alloc*          alloc;
    bool                      decompress;
    u32                       level;
    str8                      in;
    usize                     in_consumed;
    Mel_Compress_Stream*      stream;
    u8*                       out;
    usize                     out_len;
    usize                     out_cap;
    Mel_Compress_Status       status;
    bool                      failed;
    bool                      finished;
    mel_nanosec               t_begin;
    mel_nanosec               t_end;
} Lab_Job;

typedef struct
{
    str8        id;
    usize       in_len;
    usize       out_len;
    mel_nanosec ns;
    bool        failed;
    bool        done;
} Lab_Race_Result;

typedef struct
{
    const Mel_Alloc* alloc;
    str8             input;
    Lab_Job          job;
    Lab_Race_Result* results;
    usize            count;
    i32              current;
} Lab_Race;

bool lab_job_open(Lab_Job* job);
void lab_job_chunk(Lab_Job* job);
void lab_job_close(Lab_Job* job);

void lab_race_arm(Lab_Race* race, i32 index);
void lab_race_settle(Lab_Race* race, i32 index);
