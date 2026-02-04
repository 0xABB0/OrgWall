#include "../src/core/test.h"
#include "../src/math/math.h"

#define EPS 0.0001f

MEL_TEST(vec2_create)
{
    Mel_Vec2 v = mel_vec2(3.0f, 4.0f);
    MEL_ASSERT_FLOAT_EQ(v.x, 3.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(v.y, 4.0f, EPS);
    MEL_PASS();
}

MEL_TEST(vec2_add)
{
    Mel_Vec2 a = mel_vec2(1.0f, 2.0f);
    Mel_Vec2 b = mel_vec2(3.0f, 4.0f);
    Mel_Vec2 c = mel_vec2_add(a, b);
    MEL_ASSERT_FLOAT_EQ(c.x, 4.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(c.y, 6.0f, EPS);
    MEL_PASS();
}

MEL_TEST(vec2_sub)
{
    Mel_Vec2 a = mel_vec2(5.0f, 7.0f);
    Mel_Vec2 b = mel_vec2(2.0f, 3.0f);
    Mel_Vec2 c = mel_vec2_sub(a, b);
    MEL_ASSERT_FLOAT_EQ(c.x, 3.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(c.y, 4.0f, EPS);
    MEL_PASS();
}

MEL_TEST(vec2_scale)
{
    Mel_Vec2 v = mel_vec2(2.0f, 3.0f);
    Mel_Vec2 s = mel_vec2_scale(v, 2.0f);
    MEL_ASSERT_FLOAT_EQ(s.x, 4.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(s.y, 6.0f, EPS);
    MEL_PASS();
}

MEL_TEST(vec2_dot)
{
    Mel_Vec2 a = mel_vec2(1.0f, 2.0f);
    Mel_Vec2 b = mel_vec2(3.0f, 4.0f);
    f32 d = mel_vec2_dot(a, b);
    MEL_ASSERT_FLOAT_EQ(d, 11.0f, EPS);
    MEL_PASS();
}

MEL_TEST(vec2_len)
{
    Mel_Vec2 v = mel_vec2(3.0f, 4.0f);
    f32 len = mel_vec2_len(v);
    MEL_ASSERT_FLOAT_EQ(len, 5.0f, EPS);
    MEL_PASS();
}

MEL_TEST(vec2_normalize)
{
    Mel_Vec2 v = mel_vec2(3.0f, 4.0f);
    Mel_Vec2 n = mel_vec2_normalize(v);
    MEL_ASSERT_FLOAT_EQ(n.x, 0.6f, EPS);
    MEL_ASSERT_FLOAT_EQ(n.y, 0.8f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_vec2_len(n), 1.0f, EPS);
    MEL_PASS();
}

MEL_TEST(vec2_lerp)
{
    Mel_Vec2 a = mel_vec2(0.0f, 0.0f);
    Mel_Vec2 b = mel_vec2(10.0f, 20.0f);
    Mel_Vec2 mid = mel_vec2_lerp(a, b, 0.5f);
    MEL_ASSERT_FLOAT_EQ(mid.x, 5.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mid.y, 10.0f, EPS);
    MEL_PASS();
}

MEL_TEST(vec3_cross)
{
    Mel_Vec3 x = mel_vec3(1.0f, 0.0f, 0.0f);
    Mel_Vec3 y = mel_vec3(0.0f, 1.0f, 0.0f);
    Mel_Vec3 z = mel_vec3_cross(x, y);
    MEL_ASSERT_FLOAT_EQ(z.x, 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(z.y, 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(z.z, 1.0f, EPS);
    MEL_PASS();
}

MEL_TEST(vec3_len)
{
    Mel_Vec3 v = mel_vec3(2.0f, 3.0f, 6.0f);
    f32 len = mel_vec3_len(v);
    MEL_ASSERT_FLOAT_EQ(len, 7.0f, EPS);
    MEL_PASS();
}

MEL_TEST(vec4_dot)
{
    Mel_Vec4 a = mel_vec4(1.0f, 2.0f, 3.0f, 4.0f);
    Mel_Vec4 b = mel_vec4(2.0f, 3.0f, 4.0f, 5.0f);
    f32 d = mel_vec4_dot(a, b);
    MEL_ASSERT_FLOAT_EQ(d, 40.0f, EPS);
    MEL_PASS();
}

MEL_TEST(mat4_identity)
{
    Mel_Mat4 m = MEL_MAT4_IDENTITY;
    MEL_ASSERT_FLOAT_EQ(m.m[0][0], 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(m.m[1][1], 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(m.m[2][2], 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(m.m[3][3], 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(m.m[0][1], 0.0f, EPS);
    MEL_PASS();
}

MEL_TEST(mat4_mul_identity)
{
    Mel_Mat4 a = MEL_MAT4_IDENTITY;
    Mel_Mat4 b = mel_mat4_translate(mel_vec3(1.0f, 2.0f, 3.0f));
    Mel_Mat4 c = mel_mat4_mul(a, b);
    MEL_ASSERT_FLOAT_EQ(c.m[0][3], 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(c.m[1][3], 2.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(c.m[2][3], 3.0f, EPS);
    MEL_PASS();
}

MEL_TEST(mat4_mul_vec4)
{
    Mel_Mat4 m = mel_mat4_translate(mel_vec3(10.0f, 20.0f, 30.0f));
    Mel_Vec4 v = mel_vec4(0.0f, 0.0f, 0.0f, 1.0f);
    Mel_Vec4 r = mel_mat4_mul_vec4(m, v);
    MEL_ASSERT_FLOAT_EQ(r.x, 10.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(r.y, 20.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(r.z, 30.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(r.w, 1.0f, EPS);
    MEL_PASS();
}

MEL_TEST(mat4_scale)
{
    Mel_Mat4 m = mel_mat4_scale(mel_vec3(2.0f, 3.0f, 4.0f));
    Mel_Vec4 v = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Mel_Vec4 r = mel_mat4_mul_vec4(m, v);
    MEL_ASSERT_FLOAT_EQ(r.x, 2.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(r.y, 3.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(r.z, 4.0f, EPS);
    MEL_PASS();
}

MEL_TEST(quat_identity)
{
    Mel_Quat q = MEL_QUAT_IDENTITY;
    MEL_ASSERT_FLOAT_EQ(q.x, 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(q.y, 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(q.z, 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(q.w, 1.0f, EPS);
    MEL_PASS();
}

MEL_TEST(quat_rotate_vec3)
{
    Mel_Quat q = mel_quat_from_axis_angle(mel_vec3(0.0f, 0.0f, 1.0f), MEL_PI * 0.5f);
    Mel_Vec3 v = mel_vec3(1.0f, 0.0f, 0.0f);
    Mel_Vec3 r = mel_quat_rotate_vec3(q, v);
    MEL_ASSERT_FLOAT_EQ(r.x, 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(r.y, 1.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(r.z, 0.0f, EPS);
    MEL_PASS();
}

MEL_TEST(rect_contains)
{
    Mel_Rect r = mel_rect(10.0f, 10.0f, 100.0f, 100.0f);
    MEL_ASSERT(mel_rect_contains_point(r, mel_vec2(50.0f, 50.0f)));
    MEL_ASSERT(!mel_rect_contains_point(r, mel_vec2(5.0f, 50.0f)));
    MEL_ASSERT(!mel_rect_contains_point(r, mel_vec2(150.0f, 50.0f)));
    MEL_PASS();
}

MEL_TEST(rect_overlaps)
{
    Mel_Rect a = mel_rect(0.0f, 0.0f, 100.0f, 100.0f);
    Mel_Rect b = mel_rect(50.0f, 50.0f, 100.0f, 100.0f);
    Mel_Rect c = mel_rect(200.0f, 200.0f, 50.0f, 50.0f);
    MEL_ASSERT(mel_rect_overlaps(a, b));
    MEL_ASSERT(!mel_rect_overlaps(a, c));
    MEL_PASS();
}

MEL_TEST(scalar_clamp)
{
    MEL_ASSERT_FLOAT_EQ(mel_clampf(5.0f, 0.0f, 10.0f), 5.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_clampf(-5.0f, 0.0f, 10.0f), 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_clampf(15.0f, 0.0f, 10.0f), 10.0f, EPS);
    MEL_PASS();
}

MEL_TEST(scalar_lerp)
{
    MEL_ASSERT_FLOAT_EQ(mel_lerpf(0.0f, 100.0f, 0.5f), 50.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_lerpf(0.0f, 100.0f, 0.0f), 0.0f, EPS);
    MEL_ASSERT_FLOAT_EQ(mel_lerpf(0.0f, 100.0f, 1.0f), 100.0f, EPS);
    MEL_PASS();
}

int main(void)
{
    MEL_TEST_BEGIN("Math Tests");

    MEL_RUN_TEST(vec2_create);
    MEL_RUN_TEST(vec2_add);
    MEL_RUN_TEST(vec2_sub);
    MEL_RUN_TEST(vec2_scale);
    MEL_RUN_TEST(vec2_dot);
    MEL_RUN_TEST(vec2_len);
    MEL_RUN_TEST(vec2_normalize);
    MEL_RUN_TEST(vec2_lerp);
    MEL_RUN_TEST(vec3_cross);
    MEL_RUN_TEST(vec3_len);
    MEL_RUN_TEST(vec4_dot);
    MEL_RUN_TEST(mat4_identity);
    MEL_RUN_TEST(mat4_mul_identity);
    MEL_RUN_TEST(mat4_mul_vec4);
    MEL_RUN_TEST(mat4_scale);
    MEL_RUN_TEST(quat_identity);
    MEL_RUN_TEST(quat_rotate_vec3);
    MEL_RUN_TEST(rect_contains);
    MEL_RUN_TEST(rect_overlaps);
    MEL_RUN_TEST(scalar_clamp);
    MEL_RUN_TEST(scalar_lerp);

    MEL_TEST_END();

    return MEL_TEST_EXIT_CODE();
}
