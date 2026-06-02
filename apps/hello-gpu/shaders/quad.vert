#version 460

// Parametric quad from gl_VertexIndex (six verts), placed and coloured entirely
// from the push-constant root record. Drives the alpha-layers and the
// fill/blend gallery: one draw per quad, no vertex buffer.
layout(push_constant, std430) uniform Root
{
    vec4 rect;  // xy = centre (NDC), zw = half-extent (NDC)
    vec4 color; // rgb + alpha
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
