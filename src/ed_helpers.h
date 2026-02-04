#ifndef MEL_ED_HELPERS_H
#define MEL_ED_HELPERS_H

#include "types.h"
#include "vec2.h"
#include "vec4.h"

void mel_ed_draw_vec2(const char* label, Mel_Vec2* v, bool* changed);
void mel_ed_draw_vec4(const char* label, Mel_Vec4* v, bool* changed);

#endif
