#version 460
#extension GL_EXT_nonuniform_qualifier : require

struct Boid
{
    vec4 pos_vel;
};

layout(set = 2, binding = 0, std430) readonly buffer Boids { Boid b[]; } buf[];

layout(push_constant, std430) uniform Root
{
    uint  boids;
    float aspect;
    float time;
    float pad;
} root;

layout(location = 0) out vec4 v_color;

void main()
{
    const vec2 shape[3] = vec2[3](
        vec2( 0.018, 0.0),
        vec2(-0.012, 0.008),
        vec2(-0.012, -0.008));

    Boid me = buf[nonuniformEXT(root.boids)].b[gl_InstanceIndex];
    vec2 pos = me.pos_vel.xy;
    vec2 vel = me.pos_vel.zw;

    float sp = length(vel);
    vec2  dir = sp > 1e-4 ? vel / sp : vec2(1.0, 0.0);
    vec2  perp = vec2(-dir.y, dir.x);

    vec2 local = shape[gl_VertexIndex];
    vec2 world = pos + dir * local.x + perp * local.y;

    if (root.aspect >= 1.0)
        world.x /= root.aspect;
    else
        world.y *= root.aspect;

    float hue = atan(dir.y, dir.x);
    vec3  col = 0.55 + 0.45 * cos(vec3(0.0, 2.094, 4.188) + hue + root.time * 0.1);
    col = mix(vec3(0.4, 0.5, 0.7), col, clamp(sp * 2.0, 0.3, 1.0));
    v_color = vec4(col, 1.0);

    gl_Position = vec4(world, 0.0, 1.0);
}
