#pragma once

static inline float mel__clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static inline float mel__sat(float x) {
    return mel__clampf(x, 0.0f, 1.0f);
}
