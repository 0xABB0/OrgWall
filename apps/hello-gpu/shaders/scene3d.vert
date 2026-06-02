#version 460

// Scene geometry vertex stage. The app feeds clip-space-ready positions
// (x,y already perspective-divided, z carrying the [0,1] depth) plus a shaded
// colour, matching Pt_Vertex {pos[3]; color[4]}. The depth attachment does the
// occlusion the CPU back-face sort used to fake (gpu-rhi.md §6.5).
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_color;

layout(location = 0) out vec4 v_color;

void main()
{
    v_color = a_color;
    gl_Position = vec4(a_pos, 1.0);
}
