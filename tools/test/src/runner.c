#include <test/test.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>

#ifndef _WIN32
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#endif

// Child exit codes (POSIX) / in-process return codes (Windows).
enum { TEST_PASS = 0, TEST_FAIL = 1, TEST_SKIP = 2, TEST_TIMEOUT = 3 };

#define TEST_TIMEOUT_SECS 30

// ---------------------------------------------------------------------------
// Registry. Constructors append at static-init, before main, in link order.
// ---------------------------------------------------------------------------

static Mel_Test* g_head;
static Mel_Test* g_tail;

void mel_test_register(Mel_Test* node) {
    node->next = NULL;
    if (g_tail) g_tail->next = node;
    else        g_head = node;
    g_tail = node;
}

// ---------------------------------------------------------------------------
// Per-test execution state. With process isolation each test runs in its own
// child, so these globals are private to one test even though they are static.
// ---------------------------------------------------------------------------

static int     g_failed;
static int     g_skipped;
static jmp_buf g_abort_to;
static int     g_abort_armed;

void mel_test_fail(const char* file, int line, const char* fmt, ...) {
    g_failed = 1;
    fprintf(stderr, "      %s:%d: ", file, line);
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void mel_test_skip(const char* reason) {
    g_skipped = 1;
    fprintf(stderr, "      skip: %s\n", reason ? reason : "");
}

void mel_test_abort(void) {
    if (g_abort_armed) longjmp(g_abort_to, 1);
    _exit(g_failed ? TEST_FAIL : g_skipped ? TEST_SKIP : TEST_PASS);
}

static int run_body(Mel_Test* t) {
    g_failed = 0;
    g_skipped = 0;
    g_abort_armed = 1;
    if (setjmp(g_abort_to) == 0) t->fn();
    g_abort_armed = 0;
    return g_failed ? TEST_FAIL : g_skipped ? TEST_SKIP : TEST_PASS;
}

// ---------------------------------------------------------------------------
// Isolation. fork() per test on POSIX: a crash or hang in one test is reaped
// by the parent and reported as a failure without taking the suite down.
// Windows runs in-process (no isolation) until a spawn path is added.
// ---------------------------------------------------------------------------

typedef enum { R_PASS, R_FAIL, R_SKIP, R_CRASH, R_TIMEOUT } Result;

#ifndef _WIN32
static void on_alarm(int sig) { (void)sig; _exit(TEST_TIMEOUT); }

static Result run_isolated(Mel_Test* t) {
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return R_CRASH; }
    if (pid == 0) {
        signal(SIGALRM, on_alarm);
        alarm(TEST_TIMEOUT_SECS);
        int code = run_body(t);
        fflush(NULL);
        _exit(code);
    }
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    if (WIFSIGNALED(st)) return R_CRASH;
    if (!WIFEXITED(st)) return R_CRASH;
    switch (WEXITSTATUS(st)) {
        case TEST_PASS:    return R_PASS;
        case TEST_SKIP:    return R_SKIP;
        case TEST_TIMEOUT: return R_TIMEOUT;
        default:           return R_FAIL;
    }
}
#else
static Result run_isolated(Mel_Test* t) {
    switch (run_body(t)) {
        case TEST_PASS: return R_PASS;
        case TEST_SKIP: return R_SKIP;
        default:        return R_FAIL;
    }
}
#endif

// ---------------------------------------------------------------------------
// Driver.
// ---------------------------------------------------------------------------

static bool selected(const Mel_Test* t, const char* filter) {
    if (!filter) return true;
    char id[512];
    snprintf(id, sizeof id, "%s.%s", t->suite, t->name);
    return strstr(id, filter) != NULL;
}

int main(int argc, char** argv) {
    const char* filter = NULL;
    bool list = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list") == 0)        list = true;
        else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) filter = argv[++i];
        else                                        filter = argv[i];
    }

    if (list) {
        for (Mel_Test* t = g_head; t; t = t->next)
            printf("%s.%s\n", t->suite, t->name);
        return 0;
    }

    int passed = 0, failed = 0, skipped = 0, total = 0;
    for (Mel_Test* t = g_head; t; t = t->next) {
        if (!selected(t, filter)) continue;
        total++;
        printf("RUN   %s.%s\n", t->suite, t->name);
        fflush(stdout);
        switch (run_isolated(t)) {
            case R_PASS:    passed++;  printf("ok    %s.%s\n", t->suite, t->name); break;
            case R_SKIP:    skipped++; printf("skip  %s.%s\n", t->suite, t->name); break;
            case R_FAIL:    failed++;  printf("FAIL  %s.%s\n", t->suite, t->name); break;
            case R_TIMEOUT: failed++;  printf("FAIL  %s.%s (timeout after %ds)\n",
                                              t->suite, t->name, TEST_TIMEOUT_SECS); break;
            case R_CRASH:   failed++;  printf("CRASH %s.%s\n", t->suite, t->name); break;
        }
        fflush(stdout);
    }

    printf("\n%d passed, %d failed, %d skipped, of %d\n", passed, failed, skipped, total);
    return failed ? 1 : 0;
}
