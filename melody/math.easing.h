#pragma once

#include "math.scalar.h"

[[nodiscard]] static inline f32 mel_ease_linear(f32 t);

[[nodiscard]] static inline f32 mel_ease_in_quad(f32 t);
[[nodiscard]] static inline f32 mel_ease_out_quad(f32 t);
[[nodiscard]] static inline f32 mel_ease_in_out_quad(f32 t);

[[nodiscard]] static inline f32 mel_ease_in_cubic(f32 t);
[[nodiscard]] static inline f32 mel_ease_out_cubic(f32 t);
[[nodiscard]] static inline f32 mel_ease_in_out_cubic(f32 t);

[[nodiscard]] static inline f32 mel_ease_in_quart(f32 t);
[[nodiscard]] static inline f32 mel_ease_out_quart(f32 t);
[[nodiscard]] static inline f32 mel_ease_in_out_quart(f32 t);

[[nodiscard]] static inline f32 mel_ease_in_quint(f32 t);
[[nodiscard]] static inline f32 mel_ease_out_quint(f32 t);
[[nodiscard]] static inline f32 mel_ease_in_out_quint(f32 t);

[[nodiscard]] static inline f32 mel_ease_in_sine(f32 t);
[[nodiscard]] static inline f32 mel_ease_out_sine(f32 t);
[[nodiscard]] static inline f32 mel_ease_in_out_sine(f32 t);

[[nodiscard]] static inline f32 mel_ease_in_circ(f32 t);
[[nodiscard]] static inline f32 mel_ease_out_circ(f32 t);
[[nodiscard]] static inline f32 mel_ease_in_out_circ(f32 t);

[[nodiscard]] static inline f32 mel_ease_in_expo(f32 t);
[[nodiscard]] static inline f32 mel_ease_out_expo(f32 t);
[[nodiscard]] static inline f32 mel_ease_in_out_expo(f32 t);

[[nodiscard]] static inline f32 mel_ease_in_elastic(f32 t);
[[nodiscard]] static inline f32 mel_ease_out_elastic(f32 t);
[[nodiscard]] static inline f32 mel_ease_in_out_elastic(f32 t);

[[nodiscard]] static inline f32 mel_ease_in_back(f32 t);
[[nodiscard]] static inline f32 mel_ease_out_back(f32 t);
[[nodiscard]] static inline f32 mel_ease_in_out_back(f32 t);

[[nodiscard]] static inline f32 mel_ease_in_bounce(f32 t);
[[nodiscard]] static inline f32 mel_ease_out_bounce(f32 t);
[[nodiscard]] static inline f32 mel_ease_in_out_bounce(f32 t);

[[nodiscard]] static inline f32 mel_ease_step(f32 t);

#include "math.easing.inl"

typedef f32 (*Mel_Easing_Func)(f32);

typedef struct
{
    const char* name;
    Mel_Easing_Func func;
} Mel_Easing_Entry;

#define MEL_EASING_LIST(X) \
    X("linear",         mel_ease_linear) \
    X("in_quad",        mel_ease_in_quad) \
    X("out_quad",       mel_ease_out_quad) \
    X("in_out_quad",    mel_ease_in_out_quad) \
    X("in_cubic",       mel_ease_in_cubic) \
    X("out_cubic",      mel_ease_out_cubic) \
    X("in_out_cubic",   mel_ease_in_out_cubic) \
    X("in_quart",       mel_ease_in_quart) \
    X("out_quart",      mel_ease_out_quart) \
    X("in_out_quart",   mel_ease_in_out_quart) \
    X("in_quint",       mel_ease_in_quint) \
    X("out_quint",      mel_ease_out_quint) \
    X("in_out_quint",   mel_ease_in_out_quint) \
    X("in_sine",        mel_ease_in_sine) \
    X("out_sine",       mel_ease_out_sine) \
    X("in_out_sine",    mel_ease_in_out_sine) \
    X("in_circ",        mel_ease_in_circ) \
    X("out_circ",       mel_ease_out_circ) \
    X("in_out_circ",    mel_ease_in_out_circ) \
    X("in_expo",        mel_ease_in_expo) \
    X("out_expo",       mel_ease_out_expo) \
    X("in_out_expo",    mel_ease_in_out_expo) \
    X("in_elastic",     mel_ease_in_elastic) \
    X("out_elastic",    mel_ease_out_elastic) \
    X("in_out_elastic", mel_ease_in_out_elastic) \
    X("in_back",        mel_ease_in_back) \
    X("out_back",       mel_ease_out_back) \
    X("in_out_back",    mel_ease_in_out_back) \
    X("in_bounce",      mel_ease_in_bounce) \
    X("out_bounce",     mel_ease_out_bounce) \
    X("in_out_bounce",  mel_ease_in_out_bounce) \
    X("step",           mel_ease_step)

#define MEL_EASING_COUNT 32
