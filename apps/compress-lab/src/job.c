#include "lab.h"

#include <allocator/allocator.h>
#include <log/log.h>

#define LAB_CHUNK    ((usize)256 * 1024)
#define LAB_MIN_ROOM (2 * LAB_CHUNK)

bool lab_job_open(Lab_Job* job)
{
    Mel_Compress_Status st = MEL_COMPRESS_OK;
    job->in_consumed = 0;
    job->out_len = 0;
    job->failed = false;
    job->finished = false;
    job->status = MEL_COMPRESS_OK;
    job->t_begin = mel_nanos_since_unspecified_epoch();
    job->t_end = job->t_begin;

    job->stream = job->codec->begin((Mel_Compress_Begin){ .decompress = job->decompress, .level = job->level, .alloc = job->alloc }, &st);
    if (!job->stream)
    {
        job->failed = true;
        job->status = st;
        return false;
    }

    usize bound = job->decompress ? (usize)job->in.len * 3 + 256 : job->codec->bound((usize)job->in.len, job->level);
    job->out_cap = bound < LAB_MIN_ROOM ? bound + 256 : LAB_MIN_ROOM;
    job->out = mel_alloc(job->alloc, job->out_cap);
    if (!job->out)
    {
        lab_job_close(job);
        job->failed = true;
        job->status = MEL_COMPRESS_ERROR | MEL_COMPRESS_NO_MEMORY;
        return false;
    }
    return true;
}

void lab_job_chunk(Lab_Job* job)
{
    if (job->failed || job->finished || !job->stream)
        return;

    if (job->out_cap - job->out_len < LAB_MIN_ROOM)
    {
        usize new_cap = job->out_cap * 2;
        if (new_cap < job->out_len + LAB_MIN_ROOM)
            new_cap = job->out_len + LAB_MIN_ROOM;
        u8* grown = mel_realloc(job->alloc, job->out, new_cap);
        if (!grown)
        {
            job->failed = true;
            job->status = MEL_COMPRESS_ERROR | MEL_COMPRESS_NO_MEMORY;
            job->t_end = mel_nanos_since_unspecified_epoch();
            return;
        }
        job->out = grown;
        job->out_cap = new_cap;
    }

    usize remaining = (usize)job->in.len - job->in_consumed;
    usize n = remaining < LAB_CHUNK ? remaining : LAB_CHUNK;
    str8  in = str8_from_parts(job->in.data + job->in_consumed, (size)n);
    bool  in_last = job->in_consumed + n == (usize)job->in.len;

    Mel_Compress_Step step = job->codec->step(job->stream, in, in_last, job->out + job->out_len, job->out_cap - job->out_len);
    job->in_consumed += step.in_consumed;
    job->out_len += step.out_produced;

    if (mel_compress_status_failed(step.status))
    {
        job->failed = true;
        job->status = step.status;
        job->t_end = mel_nanos_since_unspecified_epoch();
        mel_log_error("compress-lab", "%.*s %s failed, status 0x%x", (int)job->codec->id.len, job->codec->id.data, job->decompress ? "decompress" : "compress", job->status);
        return;
    }
    if (step.finished)
    {
        job->finished = true;
        job->t_end = mel_nanos_since_unspecified_epoch();
    }
}

void lab_job_close(Lab_Job* job)
{
    if (job->stream)
    {
        job->codec->end(job->stream);
        job->stream = NULL;
    }
}

void lab_race_arm(Lab_Race* race, i32 index)
{
    race->current = index;
    race->job = (Lab_Job){
        .codec = mel_compress_at((usize)index),
        .alloc = race->alloc,
        .decompress = false,
        .level = mel_compress_at((usize)index)->level_default,
        .in = race->input,
    };
    lab_job_open(&race->job);
}

void lab_race_settle(Lab_Race* race, i32 index)
{
    Lab_Job*         job = &race->job;
    Lab_Race_Result* r = &race->results[index];
    lab_job_close(job);
    r->id = job->codec->id;
    r->in_len = (usize)race->input.len;
    r->out_len = job->out_len;
    r->ns = job->t_end - job->t_begin;
    r->failed = job->failed;
    r->done = true;
    if (job->out)
    {
        mel_dealloc(race->alloc, job->out);
        job->out = NULL;
    }
}
