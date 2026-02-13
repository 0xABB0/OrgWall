#include "debug.backtrace.h"
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
