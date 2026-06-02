#version 460

layout(push_constant, std430) uniform Root
{
    vec4 rect;
    vec4 color;
} root;

layout(location = 0) out vec4 v_color;

void main()
{
    const vec2 corners[6] = vec2[6](
        vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
        vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));

    vec2 ndc = root.rect.xy + corners[gl_VertexIndex] * root.rect.zw;
    v_color = root.color;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
