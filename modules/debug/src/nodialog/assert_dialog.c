#include "../assert_backend.h"

bool mel__assert_dialog_available(void) { return false; }

Mel_Assert_Response mel__assert_dialog(const Mel_Assert_Report* report)
{
    return mel_assert_default_handler(report, NULL);
}
