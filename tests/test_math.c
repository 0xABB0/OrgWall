#include "../melody/test.harness.h"
#include "../melody/math.scalar.h"
#include "../melody/math.easing.h"
#include "../melody/math.vec2.h"
#include "../melody/math.vec3.h"
#include "../melody/math.vec4.h"
#include "../melody/math.mat3.h"
#include "../melody/math.mat4.h"
#include "../melody/math.quat.h"
#include "../melody/math.ivec2.h"
#include "../melody/math.ivec3.h"
#include "../melody/math.ivec4.h"
#include "../melody/math.dvec2.h"
#include "../melody/math.dvec3.h"
#include "../melody/math.dvec4.h"
#include "../melody/math.geo.rect.h"
#include "../melody/math.geo.irect.h"
#include "../melody/math.geo.plane.h"
#include "../melody/math.geo.point2.h"
#include "../melody/math.geo.point3.h"

#define EPS 0.0001f
#define DEPS 0.00000001

MEL_TEST(vec2_create, .tags = "math")
{
    Mel_Vec2 v = mel_vec2(3.0f, 4.0f);
    MEL_ASSERT_FLOAT_EQ(v.x, 3.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(v.y, 4.0f, EPS);
}

MEL_TEST(vec2_add, .tags = "math")
{
    Mel_Vec2 a = mel_vec2(1.0f, 2.0f);
    Mel_Vec2 b = mel_vec2(3.0f, 4.0f);
    Mel_Vec2 c = mel_vec2_add(a, b);
    MEL_ASSERT_FLOAT_EQ(c.x, 4.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(c.y, 6.0f, EPS);
}

MEL_TEST(vec2_sub, .tags = "math")
{
    Mel_Vec2 a = mel_vec2(5.0f, 7.0f);
    Mel_Vec2 b = mel_vec2(2.0f, 3.0f);
    Mel_Vec2 c = mel_vec2_sub(a, b);
    MEL_ASSERT_FLOAT_EQ(c.x, 3.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(c.y, 4.0f, EPS);
}

MEL_TEST(vec2_scale, .tags = "math")
{
    Mel_Vec2 v = mel_vec2(2.0f, 3.0f);
    Mel_Vec2 s = mel_vec2_scale(v, 2.0f);
    MEL_ASSERT_FLOAT_EQ(s.x, 4.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(s.y, 6.0f, EPS);
}

MEL_TEST(vec2_dot, .tags = "math")
{
    Mel_Vec2 a = mel_vec2(1.0f, 2.0f);
    Mel_Vec2 b = mel_vec2(3.0f, 4.0f);
    f32 d = mel_vec2_dot(a, b);
    MEL_ASSERT_FLOAT_EQ(d, 11.0f, EPS);
}

MEL_TEST(vec2_len, .tags = "math")
{
    Mel_Vec2 v = mel_vec2(3.0f, 4.0f);
    f32 len = mel_vec2_len(v);
    MEL_ASSERT_FLOAT_EQ(len, 5.0f, EPS);
}

MEL_TEST(vec2_normalize, .tags = "math")
{
    Mel_Vec2 v = mel_vec2(3.0f, 4.0f);
    Mel_Vec2 n = mel_vec2_normalize(v);
    MEL_ASSERT_FLOAT_EQ(n.x, 0.6f, EPS);
    MEL_ASSERT_FLOAT_EQ(n.y, 0.8f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_vec2_len(n), 1.0f, EPS);
}

MEL_TEST(vec2_lerp, .tags = "math")
{
    Mel_Vec2 a = mel_vec2(0.0f, 0.0f);
    Mel_Vec2 b = mel_vec2(10.0f, 20.0f);
    Mel_Vec2 mid = mel_vec2_lerp(a, b, 0.5f);
    MEL_ASSERT_FLOAT_EQ(mid.x, 5.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mid.y, 10.0f, EPS);
}

MEL_TEST(vec3_cross, .tags = "math")
{
    Mel_Vec3 x = mel_vec3(1.0f, 0.0f, 0.0f);
    Mel_Vec3 y = mel_vec3(0.0f, 1.0f, 0.0f);
    Mel_Vec3 z = mel_vec3_cross(x, y);
    MEL_ASSERT_FLOAT_EQ(z.x, 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(z.y, 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(z.z, 1.0f, EPS);
}

MEL_TEST(vec3_len, .tags = "math")
{
    Mel_Vec3 v = mel_vec3(2.0f, 3.0f, 6.0f);
    f32 len = mel_vec3_len(v);
    MEL_ASSERT_FLOAT_EQ(len, 7.0f, EPS);
}

MEL_TEST(vec4_dot, .tags = "math")
{
    Mel_Vec4 a = mel_vec4(1.0f, 2.0f, 3.0f, 4.0f);
    Mel_Vec4 b = mel_vec4(2.0f, 3.0f, 4.0f, 5.0f);
    f32 d = mel_vec4_dot(a, b);
    MEL_ASSERT_FLOAT_EQ(d, 40.0f, EPS);
}

MEL_TEST(mat4_identity, .tags = "math")
{
    Mel_Mat4 m = MEL_MAT4_IDENTITY;
    MEL_ASSERT_FLOAT_EQ(m.m[0][0], 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(m.m[1][1], 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(m.m[2][2], 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(m.m[3][3], 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(m.m[0][1], 0.0f, EPS);
}

MEL_TEST(mat4_mul_identity, .tags = "math")
{
    Mel_Mat4 a = MEL_MAT4_IDENTITY;
    Mel_Mat4 b = mel_mat4_translate(mel_vec3(1.0f, 2.0f, 3.0f));
    Mel_Mat4 c = mel_mat4_mul(a, b);
    MEL_ASSERT_FLOAT_EQ(c.m[0][3], 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(c.m[1][3], 2.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(c.m[2][3], 3.0f, EPS);
}

MEL_TEST(mat4_mul_vec4, .tags = "math")
{
    Mel_Mat4 m = mel_mat4_translate(mel_vec3(10.0f, 20.0f, 30.0f));
    Mel_Vec4 v = mel_vec4(0.0f, 0.0f, 0.0f, 1.0f);
    Mel_Vec4 r = mel_mat4_mul_vec4(m, v);
    MEL_ASSERT_FLOAT_EQ(r.x, 10.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(r.y, 20.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(r.z, 30.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(r.w, 1.0f, EPS);
}

MEL_TEST(mat4_scale, .tags = "math")
{
    Mel_Mat4 m = mel_mat4_scale(mel_vec3(2.0f, 3.0f, 4.0f));
    Mel_Vec4 v = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Mel_Vec4 r = mel_mat4_mul_vec4(m, v);
    MEL_ASSERT_FLOAT_EQ(r.x, 2.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(r.y, 3.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(r.z, 4.0f, EPS);
}

MEL_TEST(quat_identity, .tags = "math")
{
    Mel_Quat q = MEL_QUAT_IDENTITY;
    MEL_ASSERT_FLOAT_EQ(q.x, 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(q.y, 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(q.z, 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(q.w, 1.0f, EPS);
}

MEL_TEST(quat_rotate_vec3, .tags = "math")
{
    Mel_Quat q = mel_quat_from_axis_angle(mel_vec3(0.0f, 0.0f, 1.0f), MEL_PI * 0.5f);
    Mel_Vec3 v = mel_vec3(1.0f, 0.0f, 0.0f);
    Mel_Vec3 r = mel_quat_rotate_vec3(q, v);
    MEL_ASSERT_FLOAT_EQ(r.x, 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(r.y, 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(r.z, 0.0f, EPS);
}

MEL_TEST(rect_contains, .tags = "math")
{
    Mel_Rect r = mel_rect(10.0f, 10.0f, 100.0f, 100.0f);
    MEL_ASSERT(mel_rect_contains_point(r, mel_vec2(50.0f, 50.0f)));
    MEL_ASSERT(!mel_rect_contains_point(r, mel_vec2(5.0f, 50.0f)));
    MEL_ASSERT(!mel_rect_contains_point(r, mel_vec2(150.0f, 50.0f)));
}

MEL_TEST(rect_overlaps, .tags = "math")
{
    Mel_Rect a = mel_rect(0.0f, 0.0f, 100.0f, 100.0f);
    Mel_Rect b = mel_rect(50.0f, 50.0f, 100.0f, 100.0f);
    Mel_Rect c = mel_rect(200.0f, 200.0f, 50.0f, 50.0f);
    MEL_ASSERT(mel_rect_overlaps(a, b));
    MEL_ASSERT(!mel_rect_overlaps(a, c));
}

MEL_TEST(scalar_clamp, .tags = "math")
{
    MEL_ASSERT_FLOAT_EQ(mel_clampf(5.0f, 0.0f, 10.0f), 5.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_clampf(-5.0f, 0.0f, 10.0f), 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_clampf(15.0f, 0.0f, 10.0f), 10.0f, EPS);
}

MEL_TEST(scalar_lerp, .tags = "math")
{
    MEL_ASSERT_FLOAT_EQ(mel_lerpf(0.0f, 100.0f, 0.5f), 50.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_lerpf(0.0f, 100.0f, 0.0f), 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_lerpf(0.0f, 100.0f, 1.0f), 100.0f, EPS);
}

MEL_TEST(scalar_constants, .tags = "math")
{
    MEL_ASSERT_FLOAT_EQ(MEL_PI, 3.14159265f, EPS);
    MEL_ASSERT_FLOAT_EQ(MEL_TAU, MEL_PI * 2.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(MEL_HALF_PI, MEL_PI * 0.5f, EPS);
    MEL_ASSERT_FLOAT_EQ(MEL_DEG2RAD * 180.0f, MEL_PI, EPS);
    MEL_ASSERT_FLOAT_EQ(MEL_RAD2DEG * MEL_PI, 180.0f, EPS);
}

MEL_TEST(scalar_minmax, .tags = "math")
{
    MEL_ASSERT_FLOAT_EQ(mel_minf(3.0f, 7.0f), 3.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_maxf(3.0f, 7.0f), 7.0f, EPS);
    MEL_ASSERT_EQ(mel_mini(3, 7), 3);
    MEL_ASSERT_EQ(mel_maxi(3, 7), 7);
}

MEL_TEST(scalar_trig, .tags = "math")
{
    MEL_ASSERT_FLOAT_EQ(mel_sinf(0.0f), 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_cosf(0.0f), 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_sinf(MEL_HALF_PI), 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_sqrtf(4.0f), 2.0f, EPS);
}

MEL_TEST(scalar_smoothstep, .tags = "math")
{
    MEL_ASSERT_FLOAT_EQ(mel_smoothstepf(0.0f, 1.0f, 0.0f), 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_smoothstepf(0.0f, 1.0f, 1.0f), 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_smoothstepf(0.0f, 1.0f, 0.5f), 0.5f, EPS);
}

MEL_TEST(scalar_saturate, .tags = "math")
{
    MEL_ASSERT_FLOAT_EQ(mel_saturatef(0.5f), 0.5f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_saturatef(-1.0f), 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_saturatef(2.0f), 1.0f, EPS);
}

MEL_TEST(scalar_deg_rad, .tags = "math")
{
    MEL_ASSERT_FLOAT_EQ(mel_toradf(180.0f), MEL_PI, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_todegf(MEL_PI), 180.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_toradf(90.0f), MEL_HALF_PI, EPS);
}

MEL_TEST(scalar_inverselerp, .tags = "math")
{
    MEL_ASSERT_FLOAT_EQ(mel_inverselerp(0.0f, 100.0f, 50.0f), 0.5f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_inverselerp(0.0f, 100.0f, 0.0f), 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_inverselerp(0.0f, 100.0f, 100.0f), 1.0f, EPS);
}

MEL_TEST(scalar_pow2, .tags = "math")
{
    MEL_ASSERT(mel_is_power_of_two(1));
    MEL_ASSERT(mel_is_power_of_two(16));
    MEL_ASSERT(mel_is_power_of_two(1024));
    MEL_ASSERT(!mel_is_power_of_two(0));
    MEL_ASSERT(!mel_is_power_of_two(15));
    MEL_ASSERT_EQ(mel_next_power_of_two(5), (u32)8);
    MEL_ASSERT_EQ(mel_next_power_of_two(16), (u32)16);
    MEL_ASSERT_EQ(mel_next_power_of_two(1), (u32)1);
}

MEL_TEST(scalar_sign_abs, .tags = "math")
{
    MEL_ASSERT_FLOAT_EQ(mel_signf(5.0f), 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_signf(-5.0f), -1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_signf(0.0f), 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_absf(-3.0f), 3.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_absf(3.0f), 3.0f, EPS);
}

MEL_TEST(scalar_wrapf, .tags = "math")
{
    MEL_ASSERT_FLOAT_EQ(mel_wrapf(5.0f, 3.0f), 2.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_wrapf(3.0f, 3.0f), 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_wrapf(-1.0f, 3.0f), 2.0f, EPS);
}

MEL_TEST(scalar_float_bits, .tags = "math")
{
    f32 v = 1.0f;
    u32 bits = mel_ftob(v);
    MEL_ASSERT_FLOAT_EQ(mel_btof(bits), 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_btof(mel_ftob(-42.0f)), -42.0f, EPS);
}

MEL_TEST(easing_linear, .tags = "math")
{
    MEL_ASSERT_FLOAT_EQ(mel_ease_linear(0.0f), 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_ease_linear(0.5f), 0.5f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_ease_linear(1.0f), 1.0f, EPS);
}

MEL_TEST(easing_quad, .tags = "math")
{
    MEL_ASSERT_FLOAT_EQ(mel_ease_in_quad(0.0f), 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_ease_in_quad(1.0f), 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_ease_out_quad(0.0f), 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_ease_out_quad(1.0f), 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_ease_in_out_quad(0.0f), 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_ease_in_out_quad(1.0f), 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_ease_in_quad(0.5f), 0.25f, EPS);
}

MEL_TEST(easing_bounce, .tags = "math")
{
    MEL_ASSERT_FLOAT_EQ(mel_ease_out_bounce(0.0f), 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_ease_out_bounce(1.0f), 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_ease_in_bounce(0.0f), 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_ease_in_bounce(1.0f), 1.0f, EPS);
}

MEL_TEST(easing_endpoints, .tags = "math")
{
    MEL_ASSERT_FLOAT_EQ(mel_ease_in_cubic(0.0f), 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_ease_in_cubic(1.0f), 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_ease_in_sine(0.0f), 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_ease_in_sine(1.0f), 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_ease_in_expo(0.0f), 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_ease_in_expo(1.0f), 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_ease_in_circ(0.0f), 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_ease_in_circ(1.0f), 1.0f, EPS);
}

MEL_TEST(mat3_identity, .tags = "math")
{
    Mel_Mat3 m = MEL_MAT3_IDENTITY;
    MEL_ASSERT_FLOAT_EQ(m.m[0][0], 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(m.m[1][1], 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(m.m[2][2], 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(m.m[0][1], 0.0f, EPS);
}

MEL_TEST(mat3_mul_identity, .tags = "math")
{
    Mel_Mat3 a = MEL_MAT3_IDENTITY;
    Mel_Mat3 b = mel_mat3_translate_2d(5.0f, 10.0f);
    Mel_Mat3 c = mel_mat3_mul(a, b);
    MEL_ASSERT_FLOAT_EQ(c.m[0][2], 5.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(c.m[1][2], 10.0f, EPS);
}

MEL_TEST(mat3_determinant, .tags = "math")
{
    Mel_Mat3 m = MEL_MAT3_IDENTITY;
    MEL_ASSERT_FLOAT_EQ(mel_mat3_determinant(m), 1.0f, EPS);

    Mel_Mat3 s = mel_mat3_scale_2d(2.0f, 3.0f);
    MEL_ASSERT_FLOAT_EQ(mel_mat3_determinant(s), 6.0f, EPS);
}

MEL_TEST(mat3_inverse, .tags = "math")
{
    Mel_Mat3 t = mel_mat3_translate_2d(5.0f, 10.0f);
    Mel_Mat3 inv = mel_mat3_inverse(t);
    Mel_Mat3 result = mel_mat3_mul(t, inv);
    MEL_ASSERT_FLOAT_EQ(result.m[0][0], 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(result.m[1][1], 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(result.m[2][2], 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(result.m[0][2], 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(result.m[1][2], 0.0f, EPS);
}

MEL_TEST(mat3_transpose, .tags = "math")
{
    Mel_Mat3 m = mel_mat3_translate_2d(3.0f, 7.0f);
    Mel_Mat3 t = mel_mat3_transpose(m);
    MEL_ASSERT_FLOAT_EQ(t.m[0][0], m.m[0][0], EPS);
    MEL_ASSERT_FLOAT_EQ(t.m[0][1], m.m[1][0], EPS);
    MEL_ASSERT_FLOAT_EQ(t.m[1][0], m.m[0][1], EPS);
    MEL_ASSERT_FLOAT_EQ(t.m[2][0], m.m[0][2], EPS);
}

MEL_TEST(mat3_from_mat4, .tags = "math")
{
    Mel_Mat4 m4 = mel_mat4_scale(mel_vec3(2.0f, 3.0f, 4.0f));
    Mel_Mat3 m3 = mel_mat3_from_mat4(m4);
    MEL_ASSERT_FLOAT_EQ(m3.m[0][0], 2.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(m3.m[1][1], 3.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(m3.m[2][2], 4.0f, EPS);
}

MEL_TEST(ivec2_basic, .tags = "math")
{
    Mel_IVec2 a = mel_ivec2(3, 4);
    Mel_IVec2 b = mel_ivec2(1, 2);
    Mel_IVec2 sum = mel_ivec2_add(a, b);
    MEL_ASSERT_EQ(sum.x, 4);
    MEL_ASSERT_EQ(sum.y, 6);

    Mel_IVec2 diff = mel_ivec2_sub(a, b);
    MEL_ASSERT_EQ(diff.x, 2);
    MEL_ASSERT_EQ(diff.y, 2);

    MEL_ASSERT_EQ(mel_ivec2_dot(a, b), 11);
}

MEL_TEST(ivec2_eq, .tags = "math")
{
    Mel_IVec2 a = mel_ivec2(5, 10);
    Mel_IVec2 b = mel_ivec2(5, 10);
    Mel_IVec2 c = mel_ivec2(5, 11);
    MEL_ASSERT(mel_ivec2_eq(a, b));
    MEL_ASSERT(!mel_ivec2_eq(a, c));
}

MEL_TEST(ivec2_convert, .tags = "math")
{
    Mel_IVec2 iv = mel_ivec2(3, 7);
    Mel_Vec2 fv = mel_ivec2_to_vec2(iv);
    MEL_ASSERT_FLOAT_EQ(fv.x, 3.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(fv.y, 7.0f, EPS);

    Mel_IVec2 back = mel_vec2_to_ivec2(mel_vec2(3.9f, 7.1f));
    MEL_ASSERT_EQ(back.x, 3);
    MEL_ASSERT_EQ(back.y, 7);
}

MEL_TEST(ivec3_basic, .tags = "math")
{
    Mel_IVec3 a = mel_ivec3(1, 2, 3);
    Mel_IVec3 b = mel_ivec3(4, 5, 6);
    Mel_IVec3 sum = mel_ivec3_add(a, b);
    MEL_ASSERT_EQ(sum.x, 5);
    MEL_ASSERT_EQ(sum.y, 7);
    MEL_ASSERT_EQ(sum.z, 9);
    MEL_ASSERT_EQ(mel_ivec3_dot(a, b), 32);
}

MEL_TEST(ivec4_basic, .tags = "math")
{
    Mel_IVec4 a = mel_ivec4(1, 2, 3, 4);
    Mel_IVec4 b = mel_ivec4(5, 6, 7, 8);
    Mel_IVec4 sum = mel_ivec4_add(a, b);
    MEL_ASSERT_EQ(sum.x, 6);
    MEL_ASSERT_EQ(sum.y, 8);
    MEL_ASSERT_EQ(sum.z, 10);
    MEL_ASSERT_EQ(sum.w, 12);
    MEL_ASSERT_EQ(mel_ivec4_dot(a, b), 70);
}

MEL_TEST(dvec2_basic, .tags = "math")
{
    Mel_DVec2 a = mel_dvec2(1.0, 2.0);
    Mel_DVec2 b = mel_dvec2(3.0, 4.0);
    Mel_DVec2 sum = mel_dvec2_add(a, b);
    MEL_ASSERT(fabs(sum.x - 4.0) < DEPS);
    MEL_ASSERT(fabs(sum.y - 6.0) < DEPS);

    f64 len = mel_dvec2_len(mel_dvec2(3.0, 4.0));
    MEL_ASSERT(fabs(len - 5.0) < DEPS);
}

MEL_TEST(dvec2_convert, .tags = "math")
{
    Mel_DVec2 d = mel_dvec2(1.5, 2.5);
    Mel_Vec2 f = mel_dvec2_to_vec2(d);
    MEL_ASSERT_FLOAT_EQ(f.x, 1.5f, EPS);
    MEL_ASSERT_FLOAT_EQ(f.y, 2.5f, EPS);

    Mel_DVec2 back = mel_vec2_to_dvec2(f);
    MEL_ASSERT(fabs(back.x - 1.5) < DEPS);
    MEL_ASSERT(fabs(back.y - 2.5) < DEPS);
}

MEL_TEST(dvec3_basic, .tags = "math")
{
    Mel_DVec3 a = mel_dvec3(1.0, 2.0, 3.0);
    Mel_DVec3 b = mel_dvec3(4.0, 5.0, 6.0);
    f64 d = mel_dvec3_dot(a, b);
    MEL_ASSERT(fabs(d - 32.0) < DEPS);
}

MEL_TEST(dvec4_basic, .tags = "math")
{
    Mel_DVec4 a = mel_dvec4(1.0, 2.0, 3.0, 4.0);
    Mel_DVec4 b = mel_dvec4(5.0, 6.0, 7.0, 8.0);
    f64 d = mel_dvec4_dot(a, b);
    MEL_ASSERT(fabs(d - 70.0) < DEPS);
}

MEL_TEST(irect_basic, .tags = "math")
{
    Mel_IRect r = mel_irect(10, 20, 100, 50);
    MEL_ASSERT_EQ(mel_irect_width(r), 100);
    MEL_ASSERT_EQ(mel_irect_height(r), 50);
    MEL_ASSERT(mel_irect_contains_point(r, mel_ivec2(50, 40)));
    MEL_ASSERT(!mel_irect_contains_point(r, mel_ivec2(5, 40)));
}

MEL_TEST(irect_overlaps, .tags = "math")
{
    Mel_IRect a = mel_irect(0, 0, 100, 100);
    Mel_IRect b = mel_irect(50, 50, 100, 100);
    Mel_IRect c = mel_irect(200, 200, 50, 50);
    MEL_ASSERT(mel_irect_overlaps(a, b));
    MEL_ASSERT(!mel_irect_overlaps(a, c));
}

MEL_TEST(irect_convert, .tags = "math")
{
    Mel_IRect ir = mel_irect(10, 20, 30, 40);
    Mel_Rect fr = mel_irect_to_rect(ir);
    MEL_ASSERT_FLOAT_EQ(fr.x, 10.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(fr.y, 20.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(fr.w, 30.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(fr.h, 40.0f, EPS);
}

MEL_TEST(plane_basic, .tags = "math")
{
    Mel_Vec3 normal = mel_vec3(0.0f, 1.0f, 0.0f);
    Mel_Vec3 point = mel_vec3(0.0f, 5.0f, 0.0f);
    Mel_Plane p = mel_plane_from_normal_point(normal, point);

    Mel_Vec3 n = mel_plane_normal(p);
    MEL_ASSERT_FLOAT_EQ(n.x, 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(n.y, 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(n.z, 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_plane_distance(p), -5.0f, EPS);
}

MEL_TEST(plane_dist_to_point, .tags = "math")
{
    Mel_Plane p = mel_plane_from_normal_point(mel_vec3(0.0f, 1.0f, 0.0f), mel_vec3(0.0f, 0.0f, 0.0f));
    MEL_ASSERT_FLOAT_EQ(mel_plane_dist_to_point(p, mel_vec3(0.0f, 10.0f, 0.0f)), 10.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_plane_dist_to_point(p, mel_vec3(0.0f, -5.0f, 0.0f)), -5.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_plane_dist_to_point(p, mel_vec3(100.0f, 0.0f, -50.0f)), 0.0f, EPS);
}

MEL_TEST(plane_project, .tags = "math")
{
    Mel_Plane p = mel_plane_from_normal_point(mel_vec3(0.0f, 1.0f, 0.0f), mel_vec3(0.0f, 0.0f, 0.0f));
    Mel_Vec3 projected = mel_plane_project_point(p, mel_vec3(5.0f, 10.0f, 3.0f));
    MEL_ASSERT_FLOAT_EQ(projected.x, 5.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(projected.y, 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(projected.z, 3.0f, EPS);
}

MEL_TEST(point2_alias, .tags = "math")
{
    Mel_Point2 p = mel_point2(3.0f, 4.0f);
    MEL_ASSERT_FLOAT_EQ(p.x, 3.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(p.y, 4.0f, EPS);

    Mel_Vec2 v = p;
    MEL_ASSERT_FLOAT_EQ(mel_vec2_len(v), 5.0f, EPS);
}

MEL_TEST(point3_alias, .tags = "math")
{
    Mel_Point3 p = mel_point3(1.0f, 2.0f, 3.0f);
    MEL_ASSERT_FLOAT_EQ(p.x, 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(p.y, 2.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(p.z, 3.0f, EPS);

    Mel_Vec3 v = p;
    MEL_ASSERT_FLOAT_EQ(mel_vec3_dot(v, v), 14.0f, EPS);
}

MEL_TEST(quat_slerp, .tags = "math")
{
    Mel_Quat a = MEL_QUAT_IDENTITY;
    Mel_Quat b = mel_quat_from_axis_angle(mel_vec3(0.0f, 0.0f, 1.0f), MEL_PI);

    Mel_Quat mid = mel_quat_slerp(a, b, 0.0f);
    MEL_ASSERT_FLOAT_EQ(mid.w, a.w, EPS);
    MEL_ASSERT_FLOAT_EQ(mid.z, a.z, EPS);

    mid = mel_quat_slerp(a, b, 1.0f);
    MEL_ASSERT_FLOAT_EQ(mel_quat_len(mid), 1.0f, EPS);
}

MEL_TEST(quat_to_mat4, .tags = "math")
{
    Mel_Quat q = MEL_QUAT_IDENTITY;
    Mel_Mat4 m = mel_quat_to_mat4(q);
    MEL_ASSERT_FLOAT_EQ(m.m[0][0], 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(m.m[1][1], 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(m.m[2][2], 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(m.m[3][3], 1.0f, EPS);
}

MEL_TEST(rect_width_height, .tags = "math")
{
    Mel_Rect r = mel_rect(10.0f, 20.0f, 300.0f, 400.0f);
    MEL_ASSERT_FLOAT_EQ(mel_rect_width(r), 300.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_rect_height(r), 400.0f, EPS);

    Mel_Vec2 ext = mel_rect_extents(r);
    MEL_ASSERT_FLOAT_EQ(ext.x, 150.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(ext.y, 200.0f, EPS);
}

MEL_TEST(vec3_rcp, .tags = "math")
{
    Mel_Vec3 v = mel_vec3(2.0f, 4.0f, 5.0f);
    Mel_Vec3 r = mel_vec3_rcp(v);
    MEL_ASSERT_FLOAT_EQ(r.x, 0.5f, EPS);
    MEL_ASSERT_FLOAT_EQ(r.y, 0.25f, EPS);
    MEL_ASSERT_FLOAT_EQ(r.z, 0.2f, EPS);
}

MEL_TEST(mat4_SRT, .tags = "math")
{
    Mel_Mat4 m = mel_mat4_SRT(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 5.0f, 10.0f, 15.0f);
    Mel_Vec3 p = mel_mat4_mul_point(m, mel_vec3(0.0f, 0.0f, 0.0f));
    MEL_ASSERT_FLOAT_EQ(p.x, 5.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(p.y, 10.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(p.z, 15.0f, EPS);
}