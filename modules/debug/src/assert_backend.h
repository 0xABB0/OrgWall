#pragma once

#include <debug/assert.h>

bool                mel__assert_dialog_available(void);
Mel_Assert_Response mel__assert_dialog(const Mel_Assert_Report* report);

bool                mel__assert_prompt_available(void);
Mel_Assert_Response mel__assert_prompt(const Mel_Assert_Report* report);
