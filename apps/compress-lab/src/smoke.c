#include "job.h"
#include "lab.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <log/log.h>

#include <stdio.h>
#include <string.h>

#define SMOKE_LEN ((usize)256 * 1024)

static str8 smoke_input(const Mel_Alloc* alloc)
{
    u8* data = mel_alloc(alloc, SMOKE_LEN);
    if (!data)
        return STR8_EMPTY;
    usize pos = 0;
    u32   line = 0;
    while (pos < SMOKE_LEN)
    {
        char  buf[96];
        int   n = snprintf(buf, sizeof buf, "%06u melody smoke payload, repetitive enough to squeeze\n", line++);
        usize take = (usize)n < SMOKE_LEN - pos ? (usize)n : SMOKE_LEN - pos;
        memcpy(data + pos, buf, take);
        pos += take;
    }
    return str8_from_parts(data, (size)SMOKE_LEN);
}

static bool smoke_roundtrip(const Mel_Alloc* alloc, str8 input)
{
    bool ok = true;
    for (usize i = 0; i < mel_compress_count() && ok; i++)
    {
        const Mel_Compress_Codec* codec = mel_compress_at(i);

        Lab_Job packed = { .codec = codec, .alloc = alloc, .level = codec->level_default, .in = input };
        if (!lab_job_open(&packed))
            return false;
        Lab_Pump_Co* pump = lab_pump_begin(&packed);
        u32          resumes = 0;
        while (lab_pump_step(pump))
            resumes++;
        lab_pump_end(pump);
        lab_job_close(&packed);
        if (packed.failed)
        {
            mel_log_error("compress-lab", "smoke: %.*s compress failed 0x%x", (int)codec->id.len, codec->id.data, packed.status);
            return false;
        }

        Lab_Job plain = { .codec = codec, .alloc = alloc, .decompress = true, .in = str8_from_parts(packed.out, (size)packed.out_len) };
        if (!lab_job_open(&plain))
            return false;
        Lab_Pump_Co* dpump = lab_pump_begin(&plain);
        while (lab_pump_step(dpump))
            resumes++;
        lab_pump_end(dpump);
        lab_job_close(&plain);

        ok = !plain.failed && plain.out_len == (usize)input.len && memcmp(plain.out, input.data, plain.out_len) == 0;
        mel_log_info("compress-lab", "smoke: %.*s %s — %u -> %u bytes, %u resumes", (int)codec->id.len, codec->id.data, ok ? "roundtrip ok" : "ROUNDTRIP MISMATCH", (u32)input.len, (u32)packed.out_len, resumes);

        if (plain.out)
            mel_dealloc(alloc, plain.out);
        if (packed.out)
            mel_dealloc(alloc, packed.out);
    }
    return ok;
}

static bool smoke_race(const Mel_Alloc* alloc, str8 input)
{
    Lab_Race race = {
        .alloc = alloc,
        .input = input,
        .results = mel_alloc_array(alloc, Lab_Race_Result, mel_compress_count()),
        .count = mel_compress_count(),
    };
    if (!race.results)
        return false;

    Lab_Race_Co* run = lab_race_begin(&race);
    while (lab_race_step(run))
        ;
    lab_race_end(run);

    bool ok = true;
    for (usize i = 0; i < race.count; i++)
    {
        Lab_Race_Result* r = &race.results[i];
        if (!r->done || r->failed)
            ok = false;
        mel_log_info("compress-lab", "smoke race: %-8.*s %u -> %u bytes in %.2f ms%s", (int)r->id.len, r->id.data, (u32)r->in_len, (u32)r->out_len, (double)r->ns / 1e6, r->failed ? " FAILED" : "");
    }
    mel_dealloc(alloc, race.results);
    return ok;
}

bool lab_smoke_run(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    str8             input = smoke_input(alloc);
    if (str8_is_empty(input))
        return false;
    bool ok = smoke_roundtrip(alloc, input) && smoke_race(alloc, input);
    mel_dealloc(alloc, input.data);
    mel_log_info("compress-lab", "smoke: %s", ok ? "ALL OK" : "FAILED");
    fprintf(stderr, "compress-lab smoke verdict: %s\n", ok ? "ALL OK" : "FAILED");
    fflush(stderr);
    return ok;
}
