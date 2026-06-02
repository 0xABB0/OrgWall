#version 460
#extension GL_EXT_nonuniform_qualifier : require

struct Particle
{
    vec4 pos_life;
    vec4 vel;
};

layout(set = 0, binding = 2, std430) readonly buffer Particles { Particle p[]; } buf[];

layout(push_constant, std430) uniform Root
{
    uint  particles;
    float aspect;
    float pad0;
    float pad1;
} root;

layout(location = 0) out vec4 v_color;

void main()
{
    const vec2 corners[6] = vec2[6](
        vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
        vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));

    Particle pt = buf[nonuniformEXT(root.particles)].p[gl_InstanceIndex];
    float life = clamp(pt.pos_life.z, 0.0, 1.0);
    float size = 0.004 + 0.012 * life;

    vec2 corner = corners[gl_VertexIndex];
    vec2 offset = corner * size;
    if (root.aspect >= 1.0)
        offset.x /= root.aspect;
    else
        offset.y *= root.aspect;
    vec2 ndc = pt.pos_life.xy + offset;

    float speed = length(pt.vel.xy);
    vec3  cool = vec3(0.15, 0.45, 0.95);
    vec3  hot = vec3(1.0, 0.65, 0.15);
    vec3  col = mix(cool, hot, clamp(speed * 3.0, 0.0, 1.0));
    v_color = vec4(col * (0.35 + 0.65 * life), life);

    gl_Position = vec4(ndc, 0.0, 1.0);
}
