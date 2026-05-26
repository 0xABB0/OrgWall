#include <thread/futex.h>

#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <math.h> // INFINITY, for the emscripten futex "wait forever" timeout

#if defined(__linux__) || defined(__ANDROID__)
    #include <linux/futex.h>
    #include <sys/syscall.h>
    #include <unistd.h>

    static int mel__futex(_Atomic(u32)* addr, int op, u32 val, const struct timespec* ts)
    {
        return (int)syscall(SYS_futex, (uint32_t*)addr, op | FUTEX_PRIVATE_FLAG, val, ts, NULL, 0);
    }

    void mel_futex_wait(_Atomic(u32)* addr, u32 expected)
    {
        mel__futex(addr, FUTEX_WAIT, expected, NULL);
    }

    bool mel_futex_wait_for(_Atomic(u32)* addr, u32 expected, i64 timeout_ns)
    {
        struct timespec ts;
        ts.tv_sec  = (time_t)(timeout_ns / 1000000000);
        ts.tv_nsec = (long)(timeout_ns % 1000000000);
        int rc = mel__futex(addr, FUTEX_WAIT, expected, &ts);
        return rc == 0 || errno == EAGAIN;
    }

    void mel_futex_wake_one(_Atomic(u32)* addr)
    {
        mel__futex(addr, FUTEX_WAKE, 1, NULL);
    }

    void mel_futex_wake_all(_Atomic(u32)* addr)
    {
        mel__futex(addr, FUTEX_WAKE, INT32_MAX, NULL);
    }

#elif defined(__EMSCRIPTEN__)
    #include <emscripten/threading.h>

    void mel_futex_wait(_Atomic(u32)* addr, u32 expected)
    {
        emscripten_futex_wait((void*)addr, expected, INFINITY);
    }

    bool mel_futex_wait_for(_Atomic(u32)* addr, u32 expected, i64 timeout_ns)
    {
        double ms = (double)timeout_ns / 1000000.0;
        int rc = emscripten_futex_wait((void*)addr, expected, ms);
        return rc == 0 || rc == -EWOULDBLOCK;
    }

    void mel_futex_wake_one(_Atomic(u32)* addr)
    {
        emscripten_futex_wake((void*)addr, 1);
    }

    void mel_futex_wake_all(_Atomic(u32)* addr)
    {
        emscripten_futex_wake((void*)addr, INT32_MAX);
    }

#elif defined(__wasi__)
    // wasm32-wasip1 is single-threaded: there is no sibling thread to change the
    // value or to wake, so wait is a no-op return and wake does nothing.
    void mel_futex_wait(_Atomic(u32)* addr, u32 expected) { (void)addr; (void)expected; }
    bool mel_futex_wait_for(_Atomic(u32)* addr, u32 expected, i64 timeout_ns) {
        (void)addr; (void)expected; (void)timeout_ns; return false;
    }
    void mel_futex_wake_one(_Atomic(u32)* addr) { (void)addr; }
    void mel_futex_wake_all(_Atomic(u32)* addr) { (void)addr; }

#else
    #error "no futex backend for this posix platform"
#endif
