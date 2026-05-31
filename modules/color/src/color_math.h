#pragma once

#include <math.mat/mat3.h>
#include <math.vector/vec3.h>

static inline Mel_Vec3 mel__vec3(float x, float y, float z) { return mel_vec3(x, y, z); }

static inline Mel_Vec3 mel__vec3_scale(Mel_Vec3 v, float s) { return mel_vec3_scale(v, s); }

static inline Mel_Mat3 mel__mat3_rows(float a, float b, float c, float d, float e, float f, float g, float h, float i) { return (Mel_Mat3){ .e = { a, b, c, d, e, f, g, h, i } }; }

static inline Mel_Mat3 mel__mat3_cols(Mel_Vec3 c0, Mel_Vec3 c1, Mel_Vec3 c2) { return (Mel_Mat3){ .e = { c0.x, c1.x, c2.x, c0.y, c1.y, c2.y, c0.z, c1.z, c2.z } }; }

static inline Mel_Mat3 mel__diag3(float x, float y, float z) { return (Mel_Mat3){ .e = { x, 0.0f, 0.0f, 0.0f, y, 0.0f, 0.0f, 0.0f, z } }; }

static inline Mel_Vec3 mel__chromaticity_xyz(float x, float y) { return mel_vec3(x / y, 1.0f, (1.0f - x - y) / y); }
