#include <time/sleep.h>

#include "sleep_backend.h"

static inline void mel__spin_relax(void)
{
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("yield");
#else
    __asm__ __volatile__("" ::: "memory");
#endif
}

Mel_Duration mel_sleep(Mel_Duration d)
{
    mel_nanosec start = mel_nanos_since_unspecified_epoch();
    if (d <= 0)
        return 0;
    mel__sleep_block((u64)d);
    return (Mel_Duration)(mel_nanos_since_unspecified_epoch() - start);
}

Mel_Duration mel_sleep_until(mel_nanosec deadline)
{
    mel_nanosec now = mel_nanos_since_unspecified_epoch();
    if (deadline <= now)
        return 0;
    mel__sleep_block(deadline - now);
    return (Mel_Duration)(mel_nanos_since_unspecified_epoch() - now);
}

Mel_Duration mel_busy_wait_until(mel_nanosec deadline)
{
    mel_nanosec start = mel_nanos_since_unspecified_epoch();
    mel_nanosec now = start;
    while (now < deadline)
    {
        mel__spin_relax();
        now = mel_nanos_since_unspecified_epoch();
    }
    return (Mel_Duration)(now - start);
}

Mel_Duration mel_busy_wait(Mel_Duration d)
{
    mel_nanosec start = mel_nanos_since_unspecified_epoch();
    if (d <= 0)
        return 0;
    return mel_busy_wait_until(start + (mel_nanosec)d);
}
