#pragma once

#include <gui/layout.h>
#include <layout/linear.h>

typedef Mel_Linear_Layout_Opt Mel_Column_Layout_Opt;

Mel_Layout* mel_column_layout_opt(Mel_Column_Layout_Opt opt);
#define mel_column_layout(...) mel_column_layout_opt((Mel_Column_Layout_Opt){ __VA_ARGS__ })
