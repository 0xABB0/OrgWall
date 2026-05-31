#include <color/lms.h>

#include "color_math.h"

static Mel_Mat3 mel__hpe(void) {
    return mel__mat3_rows(0.4002f, 0.7076f, -0.0808f, -0.2263f, 1.1653f, 0.0457f, 0.0f, 0.0f,
                          0.9182f);
}

mel_lms mel_xyz_to_lms(mel_xyz c) {
    Mel_Vec3 v = mel_mat3_mul_vec3(mel__hpe(), mel__vec3(c.x, c.y, c.z));
    return (mel_lms){v.x, v.y, v.z};
}

mel_xyz mel_lms_to_xyz(mel_lms c) {
    Mel_Vec3 v = mel_mat3_mul_vec3(mel_mat3_inverse(mel__hpe()), mel__vec3(c.l, c.m, c.s));
    return (mel_xyz){v.x, v.y, v.z};
}
