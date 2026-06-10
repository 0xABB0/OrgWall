#pragma once

#include <gui/layout.h>
#include <layout/stack.h>

Mel_Layout* mel_stack_layout_opt(Mel_Stack_Layout_Opt opt);
#define mel_stack_layout(...) mel_stack_layout_opt((Mel_Stack_Layout_Opt){ __VA_ARGS__ })
