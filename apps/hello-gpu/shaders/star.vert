#version 460

// A coloured 2D vertex stream (the spiky star the MSAA screen rasterizes) rotated
// by the push-constant angle and squared by the target aspect. The star's many
// thin diagonal spokes are deliberately edge-heavy so the multisample-resolved
// half reads visibly smoother than the single-sample half beside it.
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;

layout(push_constant, std430) uniform Root
{
    float angle;
    float aspect; // width / height of the render target
} root;

layout(location = 0) out vec4 v_color;

void main()
{
    float c = cos(root.angle), s = sin(root.angle);
    vec2  r = vec2(a_pos.x * c - a_pos.y * s, a_pos.x * s + a_pos.y * c);
    if (root.aspect >= 1.0)
        r.x /= root.aspect;
    else
        r.y *= root.aspect;
    v_color = a_color;
    gl_Position = vec4(r, 0.0, 1.0);
}
