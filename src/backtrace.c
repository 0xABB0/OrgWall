#include "backtrace.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <execinfo.h>

#define MEL_BACKTRACE_MAX_FRAMES 64

static void signal_handler(int sig)
{
    const char* sig_name = "UNKNOWN";
    switch (sig)
    {
        case SIGSEGV: sig_name = "SIGSEGV (Segmentation fault)"; break;
        case SIGABRT: sig_name = "SIGABRT (Abort)"; break;
        case SIGFPE:  sig_name = "SIGFPE (Floating point exception)"; break;
        case SIGILL:  sig_name = "SIGILL (Illegal instruction)"; break;
        case SIGBUS:  sig_name = "SIGBUS (Bus error)"; break;
    }

    fprintf(stderr, "\n=== CRASH: %s ===\n", sig_name);
    mel_backtrace_print();
    fprintf(stderr, "==============================\n");

    _exit(1);
}

void mel_backtrace_init(void)
{
    signal(SIGSEGV, signal_handler);
    signal(SIGABRT, signal_handler);
    signal(SIGFPE, signal_handler);
    signal(SIGILL, signal_handler);
    signal(SIGBUS, signal_handler);
}

void mel_backtrace_print(void)
{
    void* frames[MEL_BACKTRACE_MAX_FRAMES];
    int frame_count = backtrace(frames, MEL_BACKTRACE_MAX_FRAMES);

    if (frame_count == 0)
    {
        fprintf(stderr, "  <no backtrace available>\n");
        return;
    }

    char** symbols = backtrace_symbols(frames, frame_count);
    if (!symbols)
    {
        fprintf(stderr, "  <failed to get symbols>\n");
        return;
    }

    for (int i = 0; i < frame_count; i++)
    {
        fprintf(stderr, "  #%d %s\n", i, symbols[i]);
    }

    free(symbols);
}
