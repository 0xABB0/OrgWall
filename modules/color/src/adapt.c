#include <color/adapt.h>

#include "color_math.h"

static Mel_Mat3 mel__cat_bradford(void) { return mel__mat3_rows(0.8951f, 0.2664f, -0.1614f, -0.7502f, 1.7135f, 0.0367f, 0.0389f, -0.0685f, 1.0296f); }

static Mel_Mat3 mel__cat_cat02(void) { return mel__mat3_rows(0.7328f, 0.4296f, -0.1624f, -0.7036f, 1.6975f, 0.0061f, 0.0030f, 0.0136f, 0.9834f); }

static Mel_Mat3 mel__cat_von_kries(void) { return mel__mat3_rows(0.40024f, 0.70760f, -0.08081f, -0.22630f, 1.16532f, 0.04570f, 0.0f, 0.0f, 0.91822f); }

static mel_xyz mel__adapt(mel_xyz c, mel_white_point src, mel_white_point dst, Mel_Mat3 cone)
{
    Mel_Vec3 ws = mel__chromaticity_xyz(src.x, src.y);
    Mel_Vec3 wd = mel__chromaticity_xyz(dst.x, dst.y);
    Mel_Vec3 cs = mel_mat3_mul_vec3(cone, ws);
    Mel_Vec3 cd = mel_mat3_mul_vec3(cone, wd);
    Mel_Mat3 ratio = mel__diag3(cd.x / cs.x, cd.y / cs.y, cd.z / cs.z);
    Mel_Mat3 m = mel_mat3_mul(mel_mat3_inverse(cone), mel_mat3_mul(ratio, cone));
    Mel_Vec3 v = mel_mat3_mul_vec3(m, mel__vec3(c.x, c.y, c.z));
    return (mel_xyz){ v.x, v.y, v.z };
}

mel_xyz mel_xyz_adapt_bradford(mel_xyz c, mel_white_point src, mel_white_point dst) { return mel__adapt(c, src, dst, mel__cat_bradford()); }

mel_xyz mel_xyz_adapt_cat02(mel_xyz c, mel_white_point src, mel_white_point dst) { return mel__adapt(c, src, dst, mel__cat_cat02()); }

mel_xyz mel_xyz_adapt_von_kries(mel_xyz c, mel_white_point src, mel_white_point dst) { return mel__adapt(c, src, dst, mel__cat_von_kries()); }
