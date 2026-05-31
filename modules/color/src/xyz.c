#include <color/xyz.h>

#include "color_math.h"

mel_xyz mel_xyz_make(float x, float y, float z) {
    return (mel_xyz){x, y, z};
}

mel_xyz mel_color_to_xyz(mel_color c) {
    Mel_Mat3 m = mel__mat3_rows(0.4124564f, 0.3575761f, 0.1804375f, 0.2126729f, 0.7151522f,
                                0.0721750f, 0.0193339f, 0.1191920f, 0.9503041f);
    Mel_Vec3 v = mel_mat3_mul_vec3(m, mel__vec3(c.r, c.g, c.b));
    return (mel_xyz){v.x, v.y, v.z};
}

mel_color mel_color_from_xyz(mel_xyz c, float a) {
    Mel_Mat3 m = mel__mat3_rows(3.2404542f, -1.5371385f, -0.4985314f, -0.9692660f, 1.8760108f,
                                0.0415560f, 0.0556434f, -0.2040259f, 1.0572252f);
    Mel_Vec3 v = mel_mat3_mul_vec3(m, mel__vec3(c.x, c.y, c.z));
    return (mel_color){v.x, v.y, v.z, a};
}

mel_xyy mel_xyz_to_xyy(mel_xyz c) {
    float sum = c.x + c.y + c.z;
    if (sum <= 0.0f)
        return (mel_xyy){0.0f, 0.0f, c.y};
    return (mel_xyy){c.x / sum, c.y / sum, c.y};
}

mel_xyz mel_xyy_to_xyz(mel_xyy c) {
    if (c.y <= 0.0f)
        return (mel_xyz){0.0f, 0.0f, 0.0f};
    float x = c.x * c.Y / c.y;
    float z = (1.0f - c.x - c.y) * c.Y / c.y;
    return (mel_xyz){x, c.Y, z};
}
