#pragma once

#include "core.types.h"
#include "math.ivec2.h"
#include "math.geo.rect.h"

typedef struct {
    i32 x, y, w, h;
} Mel_IRect;

[[nodiscard]] static inline Mel_IRect mel_irect(i32 x, i32 y, i32 w, i32 h);
[[nodiscard]] static inline Mel_IRect mel_irect_from_minmax(i32 xmin, i32 ymin, i32 xmax, i32 ymax);
[[nodiscard]] static inline Mel_IRect mel_irect_from_pos_size(Mel_IVec2 pos, Mel_IVec2 size);
[[nodiscard]] static inline Mel_IVec2 mel_irect_pos(Mel_IRect r);
[[nodiscard]] static inline Mel_IVec2 mel_irect_size(Mel_IRect r);
[[nodiscard]] static inline i32 mel_irect_width(Mel_IRect r);
[[nodiscard]] static inline i32 mel_irect_height(Mel_IRect r);
[[nodiscard]] static inline bool mel_irect_contains_point(Mel_IRect r, Mel_IVec2 p);
[[nodiscard]] static inline bool mel_irect_overlaps(Mel_IRect a, Mel_IRect b);
[[nodiscard]] static inline Mel_IRect mel_irect_expand(Mel_IRect r, Mel_IVec2 amount);
[[nodiscard]] static inline Mel_IRect mel_irect_translate(Mel_IRect r, Mel_IVec2 offset);
[[nodiscard]] static inline Mel_IVec2 mel_irect_corner(Mel_IRect r, i32 index);
static inline void mel_irect_corners(Mel_IRect r, Mel_IVec2 out[4]);
static inline void mel_irect_add_point(Mel_IRect* r, Mel_IVec2 p);
[[nodiscard]] static inline Mel_Rect mel_irect_to_rect(Mel_IRect r);
[[nodiscard]] static inline Mel_IRect mel_rect_to_irect(Mel_Rect r);

#include "math.geo.irect.inl"
