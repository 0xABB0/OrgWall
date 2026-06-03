#version 460

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec3 a_color;
layout(location = 3) in vec3 a_shadow_uvz;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_color;
layout(location = 2) out vec3 v_shadow_uvz;

void main()
{
    v_normal     = a_normal;
    v_color      = a_color;
    v_shadow_uvz = a_shadow_uvz;
    gl_Position  = vec4(a_pos, 1.0);
}
