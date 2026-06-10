#pragma once

#include <gui/layout.h>
#include <layout/linear.h>

typedef Mel_Linear_Layout_Opt Mel_Row_Layout_Opt;

Mel_Layout* mel_row_layout_opt(Mel_Row_Layout_Opt opt);
#define mel_row_layout(...) mel_row_layout_opt((Mel_Row_Layout_Opt){ __VA_ARGS__ })
