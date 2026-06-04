#include "../assert_backend.h"

bool mel__assert_prompt_available(void) { return false; }

Mel_Assert_Response mel__assert_prompt(const Mel_Assert_Report* report)
{
    (void)report;
    return MEL_ASSERT_RESPONSE_BREAK | MEL_ASSERT_RESPONSE_ABORT;
}
