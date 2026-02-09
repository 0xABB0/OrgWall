#ifndef MEL_EDITOR_HELPERS_H
#define MEL_EDITOR_HELPERS_H

#include "types.h"
#include "math.vec2.h"
#include "math.vec4.h"

void mel_ed_draw_vec2(const char* label, Mel_Vec2* v, bool* changed);
void mel_ed_draw_vec4(const char* label, Mel_Vec4* v, bool* changed);

#endif
