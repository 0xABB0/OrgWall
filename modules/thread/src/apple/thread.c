#include <thread/thread.h>

#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

static_assert(sizeof(pthread_t) <= MEL_THREAD_HANDLE_SIZE, "MEL_THREAD_HANDLE_SIZE too small");
static_assert(_Alignof(pthread_t) <= MEL_THREAD_HANDLE_ALIGN, "MEL_THREAD_HANDLE_ALIGN too small");

#define MEL__THREAD_HANDLE(t) (*(pthread_t*)(t)->_handle)

static void* mel__thread_trampoline(void* arg)
{
    Mel_Thread* t = (Mel_Thread*)arg;
    if (t->name[0]) mel_thread_set_name(t->name);
    int rc = t->fn(t->user);
    atomic_store_explicit(&t->finished, 1, memory_order_release);
    return (void*)(intptr_t)rc;
}

bool mel_thread_spawn_opt(Mel_Thread* t, Mel_Thread_Fn fn, void* user, Mel_Thread_Spawn_Opt opt)
{
    t->fn   = fn;
    t->user = user;
    t->id   = 0;
    atomic_store_explicit(&t->finished, 0, memory_order_relaxed);

    if (opt.name) {
        usize n = strlen(opt.name);
        if (n > sizeof(t->name) - 1) n = sizeof(t->name) - 1;
        memcpy(t->name, opt.name, n);
        t->name[n] = 0;
    } else {
        t->name[0] = 0;
    }

    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) return false;
    if (opt.stack_size) pthread_attr_setstacksize(&attr, opt.stack_size);

    int rc = pthread_create(&MEL__THREAD_HANDLE(t), &attr, mel__thread_trampoline, t);
    pthread_attr_destroy(&attr);
    if (rc != 0) return false;

    u64 tid = 0;
    pthread_threadid_np(MEL__THREAD_HANDLE(t), &tid);
    t->id = tid;
    return true;
}

bool mel_thread_join(Mel_Thread* t, int* exit_code)
{
    void* retval = NULL;
    if (pthread_join(MEL__THREAD_HANDLE(t), &retval) != 0) return false;
    if (exit_code) *exit_code = (int)(intptr_t)retval;
    return true;
}

bool mel_thread_detach(Mel_Thread* t)
{
    return pthread_detach(MEL__THREAD_HANDLE(t)) == 0;
}

bool mel_thread_is_finished(const Mel_Thread* t)
{
    return atomic_load_explicit(&t->finished, memory_order_acquire) != 0;
}

Mel_Thread_Id mel_thread_current_id(void)
{
    u64 tid = 0;
    pthread_threadid_np(NULL, &tid);
    return tid;
}

bool mel_thread_id_equal(Mel_Thread_Id a, Mel_Thread_Id b)
{
    return a == b;
}

void mel_thread_yield(void)
{
    sched_yield();
}

void mel_thread_sleep(i64 ns)
{
    if (ns <= 0) return;
    struct timespec ts;
    ts.tv_sec  = (time_t)(ns / 1000000000);
    ts.tv_nsec = (long)(ns % 1000000000);
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { }
}

void mel_thread_set_name(const char* name)
{
    pthread_setname_np(name);
}

u32 mel_thread_hardware_concurrency(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (u32)n : 1;
}
