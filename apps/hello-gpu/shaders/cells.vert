#version 460
#extension GL_EXT_nonuniform_qualifier : require

// Reads the compute-produced plasma colours out of a heap-resident storage
// buffer, one colour per instance, and lays out a grid of quad cells. The cell
// index is gl_InstanceIndex; the quad corners come from gl_VertexIndex.
layout(set = 0, binding = 2, std430) readonly buffer Cells
{
    vec4 color[];
} u_buffers[];

layout(push_constant, std430) uniform Root
{
    uint cell_buf;
    uint grid_w;
    uint grid_h;
} root;

layout(location = 0) out vec4 v_color;

void main()
{
    const vec2 corners[6] = vec2[6](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

    uint cx = uint(gl_InstanceIndex) % root.grid_w;
    uint cy = uint(gl_InstanceIndex) / root.grid_w;

    vec2 cell = vec2(1.0 / float(root.grid_w), 1.0 / float(root.grid_h));
    vec2 uv = (vec2(cx, cy) + corners[gl_VertexIndex] * 0.96) * cell;
    vec2 ndc = uv * 2.0 - 1.0;

    v_color = u_buffers[nonuniformEXT(root.cell_buf)].color[gl_InstanceIndex];
    gl_Position = vec4(ndc, 0.0, 1.0);
}
