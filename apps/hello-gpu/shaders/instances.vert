#version 460
#extension GL_EXT_nonuniform_qualifier : require

// gl_InstanceIndex instancing with a bindless per-instance store: one draw of
// six vertices times N instances. Each instance pulls its transform and colour
// from a heap-resident storage buffer addressed by the root record's buffer
// slot (gpu-rhi.md §6.7). No per-instance vertex buffer, no index buffer — the
// quad corners are generated from gl_VertexIndex.
struct Instance
{
    vec4 pos_scale; // xy = centre (NDC), z = half-size, w = unused
    vec4 color;     // rgb + alpha
};

layout(set = 0, binding = 2, std430) readonly buffer Instances
{
    Instance items[];
} u_buffers[];

layout(push_constant, std430) uniform Root
{
    uint instance_buf;
    float aspect;
} root;

layout(location = 0) out vec4 v_color;

void main()
{
    // Two triangles (0..5) forming a unit quad in [-1,1].
    const vec2 corners[6] = vec2[6](
        vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
        vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));

    Instance inst = u_buffers[nonuniformEXT(root.instance_buf)].items[gl_InstanceIndex];
    vec2 local = corners[gl_VertexIndex] * inst.pos_scale.z;
    local.x /= root.aspect;
    vec2 ndc = inst.pos_scale.xy + local;

    v_color = inst.color;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
