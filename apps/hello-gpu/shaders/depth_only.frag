#version 460

// Depth-prepass fragment stage: writes no colour at all, so the prepass (which has
// no colour attachment) raises no "unused fragment output" diagnostic. Depth is
// written by fixed-function from gl_Position; the shader body is intentionally empty.
layout(location = 0) in vec4 v_color;

void main()
{
}
