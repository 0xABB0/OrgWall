#version 460

// Attributeless fullscreen triangle. gl_VertexIndex in {0,1,2} expands to a
// triangle covering the viewport; v_uv runs [0,1] across the screen. Shared by
// every bindless blit / post-process screen so one vertex stage drives them all
// (MEL-ENGINE-IX: one rule, reused).
layout(location = 0) out vec2 v_uv;

void main()
{
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    v_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
