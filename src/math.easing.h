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

#include "math.easing.inl"
