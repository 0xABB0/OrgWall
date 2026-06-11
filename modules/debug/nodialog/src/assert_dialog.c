#include "../../src/assert_backend.h"

#include <core/platform.h>

#include <stdatomic.h>
#include <stdio.h>

#if MEL_PLATFORM_IOS
#define MEL__NODIALOG_PLATFORM "iOS"
#elif MEL_PLATFORM_ANDROID
#define MEL__NODIALOG_PLATFORM "Android"
#elif MEL_PLATFORM_LINUX
#define MEL__NODIALOG_PLATFORM "Linux"
#elif MEL_PLATFORM_WEB
#define MEL__NODIALOG_PLATFORM "wasm"
#else
#error "nodialog assert backend selected on a platform that may have a native modal; wire its own assert_dialog backend instead of defaulting silently"
#endif

bool mel__assert_dialog_available(void) { return false; }

Mel_Assert_Response mel__assert_dialog(const Mel_Assert_Report* report)
{
    static atomic_flag announced = ATOMIC_FLAG_INIT;
    if (!atomic_flag_test_and_set_explicit(&announced, memory_order_relaxed))
    {
        fprintf(stderr, "debug: no native blocking assert modal on " MEL__NODIALOG_PLATFORM "; degrading to stderr report.\n");
        fflush(stderr);
    }
    return mel_assert_default_handler(report, NULL);
}
