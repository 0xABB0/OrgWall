#include "debug.backtrace.h"
#include "async.job.h"
#include "core.platform.h"

#if MEL_PLATFORM_POSIX
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <execinfo.h>
#include <string.h>

static void sig_write(const char* s)
{
    write(STDERR_FILENO, s, strlen(s));
}

static void sig_write_hex(uintptr_t val)
{
    char buf[20];
    buf[19] = '\0';
    i32 pos = 18;

    if (val == 0)
    {
        buf[pos--] = '0';
    }
    else
    {
        while (val > 0 && pos >= 0)
        {
            i32 digit = val & 0xF;
            buf[pos--] = digit < 10 ? '0' + digit : 'a' + digit - 10;
            val >>= 4;
        }
    }

    buf[pos--] = 'x';
    buf[pos] = '0';

    write(STDERR_FILENO, &buf[pos], 19 - pos);
}

static void sig_write_u32(u32 val)
{
    char buf[16];
    i32 pos = 15;
    buf[pos] = '\0';

    if (val == 0)
    {
        buf[--pos] = '0';
    }
    else
    {
        while (val > 0 && pos > 0)
        {
            buf[--pos] = (char)('0' + (val % 10));
            val /= 10;
        }
    }

    write(STDERR_FILENO, &buf[pos], 15 - pos);
}

static void sig_write_u16(u16 val)
{
    sig_write_u32((u32)val);
}

static void signal_handler(int sig)
{
    sig_write("\n=== CRASH: ");

    switch (sig)
    {
        case SIGSEGV: sig_write("SIGSEGV (Segmentation fault)"); break;
        case SIGABRT: sig_write("SIGABRT (Abort)"); break;
        case SIGFPE:  sig_write("SIGFPE (Floating point exception)"); break;
        case SIGILL:  sig_write("SIGILL (Illegal instruction)"); break;
        case SIGBUS:  sig_write("SIGBUS (Bus error)"); break;
        default:      sig_write("UNKNOWN"); break;
    }

    sig_write(" ===\n");

    Mel_Job_Debug_Info job_info;
    if (mel_job_debug_current(&job_info) && job_info.on_worker)
    {
        sig_write("Worker context:\n");
        sig_write("  worker_index: ");
        sig_write_u32(job_info.worker_index);
        sig_write("\n");
        sig_write("  on_fiber: ");
        sig_write(job_info.on_fiber ? "yes\n" : "no\n");
        if (job_info.on_fiber)
        {
            sig_write("  fiber_index: ");
            sig_write_u16(job_info.fiber_index);
            sig_write("\n");
            if (job_info.fiber_state)
            {
                sig_write("  fiber_state: ");
                sig_write(job_info.fiber_state);
                sig_write("\n");
            }
        }
        if (job_info.resume_reason)
        {
            sig_write("  resume_reason: ");
            sig_write(job_info.resume_reason);
            sig_write("\n");
            sig_write("  resume_fiber_index: ");
            sig_write_u16(job_info.resume_fiber_index);
            sig_write("\n");
        }

        if (job_info.has_current_job)
        {
            sig_write("  current_job.task: ");
            sig_write_hex((uintptr_t)job_info.task);
            sig_write("\n");

            if (job_info.debug_name)
            {
                sig_write("  current_job.name: ");
                sig_write(job_info.debug_name);
                sig_write("\n");
            }

            if (job_info.debug_file)
            {
                sig_write("  current_job.submitted_at: ");
                sig_write(job_info.debug_file);
                sig_write(":");
                sig_write_u32(job_info.debug_line);
                sig_write("\n");
            }
        }
        else
        {
            sig_write("  current_job: <none>\n");
            if (job_info.task)
            {
                sig_write("  last_job.task: ");
                sig_write_hex((uintptr_t)job_info.task);
                sig_write("\n");
            }
            if (job_info.debug_name)
            {
                sig_write("  last_job.name: ");
                sig_write(job_info.debug_name);
                sig_write("\n");
            }
            if (job_info.debug_file)
            {
                sig_write("  last_job.submitted_at: ");
                sig_write(job_info.debug_file);
                sig_write(":");
                sig_write_u32(job_info.debug_line);
                sig_write("\n");
            }
        }
    }

    void* frames[MEL_BACKTRACE_MAX_FRAMES];
    int frame_count = backtrace(frames, MEL_BACKTRACE_MAX_FRAMES);

    if (frame_count > 0)
        backtrace_symbols_fd(frames, frame_count, STDERR_FILENO);
    else
        sig_write("  <no backtrace available>\n");

    sig_write("==============================\n");

    _exit(128 + sig);
}

void mel_backtrace_init(void)
{
    struct sigaction sa = {0};
    sa.sa_handler = signal_handler;
    sa.sa_flags = SA_RESETHAND;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
}

__attribute__((constructor))
static void mel__backtrace_register(void)
{
    mel_backtrace_init();
}

void mel_backtrace_capture(Mel_Backtrace* bt, i32 skip)
{
    assert(bt != nullptr);

    void* raw_frames[MEL_BACKTRACE_MAX_FRAMES + 16];
    int total = backtrace(raw_frames, MEL_BACKTRACE_MAX_FRAMES + skip + 1);

    i32 start = skip + 1;
    if (start > total) start = total;

    bt->frame_count = total - start;
    if (bt->frame_count > MEL_BACKTRACE_MAX_FRAMES)
        bt->frame_count = MEL_BACKTRACE_MAX_FRAMES;

    for (i32 i = 0; i < bt->frame_count; i++)
        bt->frames[i] = raw_frames[start + i];
}

void mel_backtrace_print(Mel_Backtrace* bt)
{
    assert(bt != nullptr);

    if (bt->frame_count == 0)
    {
        fprintf(stderr, "  <no backtrace available>\n");
        return;
    }

    char** symbols = backtrace_symbols(bt->frames, bt->frame_count);
    if (!symbols)
    {
        for (i32 i = 0; i < bt->frame_count; i++)
            fprintf(stderr, "  #%d 0x%lx\n", i, (unsigned long)(uintptr_t)bt->frames[i]);
        return;
    }

    for (i32 i = 0; i < bt->frame_count; i++)
        fprintf(stderr, "  #%d %s\n", i, symbols[i]);

    free(symbols);
}

void mel_backtrace_print_current(void)
{
    Mel_Backtrace bt;
    mel_backtrace_capture(&bt, 1);
    mel_backtrace_print(&bt);
}

#else

void mel_backtrace_init(void) {}
void mel_backtrace_capture(Mel_Backtrace* bt, i32 skip) { (void)skip; bt->frame_count = 0; }
void mel_backtrace_print(Mel_Backtrace* bt) { (void)bt; }
void mel_backtrace_print_current(void) {}

#endif
