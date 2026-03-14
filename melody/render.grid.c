#include "render.grid.h"
#include "mesh.pass.h"
#include "math.scalar.h"

static void mel__grid_push_line(Mel_Grid* grid, u32* vi, u32* ii,
    Mel_Vec3 a, Mel_Vec3 b, Mel_Vec3 offset, Mel_Vec4 color)
{
    u32 base = *vi;
    grid->positions[(*vi)++] = mel_vec3(a.x - offset.x, a.y, a.z - offset.z);
    grid->positions[(*vi)++] = mel_vec3(a.x + offset.x, a.y, a.z + offset.z);
    grid->positions[(*vi)++] = mel_vec3(b.x + offset.x, b.y, b.z + offset.z);
    grid->positions[(*vi)++] = mel_vec3(b.x - offset.x, b.y, b.z - offset.z);
    grid->colors[base] = color;
    grid->colors[base + 1] = color;
    grid->colors[base + 2] = color;
    grid->colors[base + 3] = color;
    grid->indices[(*ii)++] = base;
    grid->indices[(*ii)++] = base + 1;
    grid->indices[(*ii)++] = base + 2;
    grid->indices[(*ii)++] = base;
    grid->indices[(*ii)++] = base + 2;
    grid->indices[(*ii)++] = base + 3;
}

void mel_grid_init_opt(Mel_Grid* grid, Mel_Grid_Init_Opt opt)
{
    f32 spacing = opt.spacing > 0 ? opt.spacing : 1.0f;
    f32 y = opt.y;
    f32 w = opt.line_width > 0 ? opt.line_width : 0.005f;
    Mel_Vec4 color = opt.color.w > 0 ? opt.color : (Mel_Vec4){{0.25f, 0.25f, 0.25f, 1.0f}};
    Mel_Vec4 axis_x = opt.axis_color_x.w > 0 ? opt.axis_color_x : (Mel_Vec4){{0.5f, 0.15f, 0.15f, 1.0f}};
    Mel_Vec4 axis_z = opt.axis_color_z.w > 0 ? opt.axis_color_z : (Mel_Vec4){{0.15f, 0.15f, 0.5f, 1.0f}};

    f32 half = (f32)(MEL_GRID_LINE_COUNT / 2) * spacing;
    u32 vi = 0;
    u32 ii = 0;

    for (u32 i = 0; i < MEL_GRID_LINE_COUNT; i++) {
        f32 t = -half + (f32)i * spacing;
        bool is_center = mel_absf(t) < spacing * 0.01f;
        f32 lw = is_center ? w * 2.0f : w;

        mel__grid_push_line(grid, &vi, &ii,
            mel_vec3(t, y, -half), mel_vec3(t, y, half),
            mel_vec3(-lw, 0, 0),
            is_center ? axis_z : color);

        mel__grid_push_line(grid, &vi, &ii,
            mel_vec3(-half, y, t), mel_vec3(half, y, t),
            mel_vec3(0, 0, lw),
            is_center ? axis_x : color);
    }

    grid->mesh = (Mel_Mesh){
        .positions = grid->positions,
        .colors = grid->colors,
        .vertex_count = vi,
        .indices = grid->indices,
        .index_count = ii,
    };
}
