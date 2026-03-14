#pragma once

#include "core.types.h"
#include "math.vec3.h"
#include "math.vec4.h"
#include "mesh.pass.h"

#define MEL_GRID_LINE_COUNT 21
#define MEL_GRID_VERTS_PER_LINE 4
#define MEL_GRID_INDICES_PER_LINE 6
#define MEL_GRID_TOTAL_LINES (MEL_GRID_LINE_COUNT * 2)
#define MEL_GRID_VERTS (MEL_GRID_TOTAL_LINES * MEL_GRID_VERTS_PER_LINE)
#define MEL_GRID_INDICES (MEL_GRID_TOTAL_LINES * MEL_GRID_INDICES_PER_LINE)

typedef struct {
    Mel_Vec3 positions[MEL_GRID_VERTS];
    Mel_Vec4 colors[MEL_GRID_VERTS];
    u32 indices[MEL_GRID_INDICES];
    Mel_Mesh mesh;
} Mel_Grid;

typedef struct {
    f32 size;
    f32 spacing;
    f32 y;
    f32 line_width;
    Mel_Vec4 color;
    Mel_Vec4 axis_color_x;
    Mel_Vec4 axis_color_z;
} Mel_Grid_Init_Opt;

void mel_grid_init_opt(Mel_Grid* grid, Mel_Grid_Init_Opt opt);
#define mel_grid_init(grid, ...) mel_grid_init_opt((grid), (Mel_Grid_Init_Opt){ __VA_ARGS__ })
