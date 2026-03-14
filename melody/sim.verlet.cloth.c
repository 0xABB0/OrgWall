#include "sim.verlet.h"
#include "math.vec4.h"
#include <assert.h>
#include <math.h>

static inline u32 mel__cloth_idx(u32 row, u32 col, u32 width)
{
    return row * width + col;
}

void mel_verlet_cloth_opt(Mel_Verlet_System* sys, Mel_Verlet_Cloth_Opt opt)
{
    assert(sys);
    assert(opt.width >= 2 && opt.height >= 2);

    f32 spacing = opt.spacing > 0 ? opt.spacing : 0.2f;
    u32 w = opt.width;
    u32 h = opt.height;

    for (u32 r = 0; r < h; r++) {
        for (u32 c = 0; c < w; c++) {
            Mel_Vec3 pos = mel_vec3(
                opt.origin.x + (f32)c * spacing,
                opt.origin.y,
                opt.origin.z + (f32)r * spacing);

            f32 u = (f32)c / (f32)(w - 1);
            f32 v = (f32)r / (f32)(h - 1);

            mel_verlet_add_particle(sys, .pos = pos, .uv = mel_vec2(u, v));
        }
    }

    if (opt.pin_top_row) {
        for (u32 c = 0; c < w; c++)
            mel_verlet_pin(sys, mel__cloth_idx(0, c, w));
    } else {
        if (opt.pin_top_left)
            mel_verlet_pin(sys, mel__cloth_idx(0, 0, w));
        if (opt.pin_top_right)
            mel_verlet_pin(sys, mel__cloth_idx(0, w - 1, w));
    }

    for (u32 r = 0; r < h; r++) {
        for (u32 c = 0; c < w - 1; c++) {
            mel_verlet_add_distance(sys,
                mel__cloth_idx(r, c, w),
                mel__cloth_idx(r, c + 1, w), 0);
        }
    }

    for (u32 r = 0; r < h - 1; r++) {
        for (u32 c = 0; c < w; c++) {
            mel_verlet_add_distance(sys,
                mel__cloth_idx(r, c, w),
                mel__cloth_idx(r + 1, c, w), 0);
        }
    }

    if (opt.add_shear) {
        for (u32 r = 0; r < h - 1; r++) {
            for (u32 c = 0; c < w - 1; c++) {
                mel_verlet_add_distance(sys,
                    mel__cloth_idx(r, c, w),
                    mel__cloth_idx(r + 1, c + 1, w), 0);
                mel_verlet_add_distance(sys,
                    mel__cloth_idx(r, c + 1, w),
                    mel__cloth_idx(r + 1, c, w), 0);
            }
        }
    }

    if (opt.add_bend) {
        for (u32 r = 0; r < h; r++) {
            for (u32 c = 0; c < w - 2; c++) {
                mel_verlet_add_distance(sys,
                    mel__cloth_idx(r, c, w),
                    mel__cloth_idx(r, c + 2, w), 0);
            }
        }

        for (u32 r = 0; r < h - 2; r++) {
            for (u32 c = 0; c < w; c++) {
                mel_verlet_add_distance(sys,
                    mel__cloth_idx(r, c, w),
                    mel__cloth_idx(r + 2, c, w), 0);
            }
        }
    }
}

void mel_verlet_compute_normals(Mel_Verlet_System* sys, u32 width, u32 height)
{
    assert(sys);
    assert(width * height <= sys->count);

    for (u32 r = 0; r < height; r++) {
        for (u32 c = 0; c < width; c++) {
            u32 idx = mel__cloth_idx(r, c, width);
            Mel_Vec3 curr = sys->particles[idx].pos;
            Mel_Vec3 avg = MEL_VEC3_ZERO;
            u32 n = 0;

            if (c > 0 && r > 0) {
                Mel_Vec3 left = sys->particles[mel__cloth_idx(r, c - 1, width)].pos;
                Mel_Vec3 top  = sys->particles[mel__cloth_idx(r - 1, c, width)].pos;
                avg = mel_vec3_add(avg, mel_vec3_cross(
                    mel_vec3_sub(top, curr), mel_vec3_sub(left, curr)));
                n++;
            }

            if (c + 1 < width && r > 0) {
                Mel_Vec3 right = sys->particles[mel__cloth_idx(r, c + 1, width)].pos;
                Mel_Vec3 top   = sys->particles[mel__cloth_idx(r - 1, c, width)].pos;
                avg = mel_vec3_add(avg, mel_vec3_cross(
                    mel_vec3_sub(right, curr), mel_vec3_sub(top, curr)));
                n++;
            }

            if (c > 0 && r + 1 < height) {
                Mel_Vec3 left   = sys->particles[mel__cloth_idx(r, c - 1, width)].pos;
                Mel_Vec3 bottom = sys->particles[mel__cloth_idx(r + 1, c, width)].pos;
                avg = mel_vec3_add(avg, mel_vec3_cross(
                    mel_vec3_sub(left, curr), mel_vec3_sub(bottom, curr)));
                n++;
            }

            if (c + 1 < width && r + 1 < height) {
                Mel_Vec3 right  = sys->particles[mel__cloth_idx(r, c + 1, width)].pos;
                Mel_Vec3 bottom = sys->particles[mel__cloth_idx(r + 1, c, width)].pos;
                avg = mel_vec3_add(avg, mel_vec3_cross(
                    mel_vec3_sub(bottom, curr), mel_vec3_sub(right, curr)));
                n++;
            }

            if (n > 0) {
                avg = mel_vec3_scale(avg, 1.0f / (f32)n);
                f32 len = mel_vec3_len(avg);
                if (len > 1e-7f)
                    avg = mel_vec3_scale(avg, 1.0f / len);
                else
                    avg = MEL_VEC3_UP;
            } else {
                avg = MEL_VEC3_UP;
            }

            sys->particles[idx].normal = avg;
        }
    }
}

void mel_verlet_compute_tangents(Mel_Verlet_System* sys, u32 width, u32 height,
    Mel_Vec4* out_tangents)
{
    assert(sys);
    assert(width * height <= sys->count);
    assert(out_tangents);

    for (u32 r = 0; r < height; r++) {
        for (u32 c = 0; c < width; c++) {
            u32 idx = mel__cloth_idx(r, c, width);
            Mel_Vec3 tangent = MEL_VEC3_ZERO;
            u32 n = 0;

            if (c > 0) {
                Mel_Vec3 left = sys->particles[mel__cloth_idx(r, c - 1, width)].pos;
                tangent = mel_vec3_add(tangent,
                    mel_vec3_sub(sys->particles[idx].pos, left));
                n++;
            }
            if (c + 1 < width) {
                Mel_Vec3 right = sys->particles[mel__cloth_idx(r, c + 1, width)].pos;
                tangent = mel_vec3_add(tangent,
                    mel_vec3_sub(right, sys->particles[idx].pos));
                n++;
            }

            if (n > 0) {
                tangent = mel_vec3_scale(tangent, 1.0f / (f32)n);
                f32 len = mel_vec3_len(tangent);
                if (len > 1e-7f)
                    tangent = mel_vec3_scale(tangent, 1.0f / len);
                else
                    tangent = (Mel_Vec3){{1, 0, 0}};
            } else {
                tangent = (Mel_Vec3){{1, 0, 0}};
            }

            Mel_Vec3 normal = sys->particles[idx].normal;
            Mel_Vec3 bitangent = mel_vec3_cross(normal, tangent);
            f32 handedness = mel_vec3_dot(bitangent,
                mel_vec3_cross(normal, (Mel_Vec3){{1, 0, 0}})) < 0 ? -1.0f : 1.0f;

            out_tangents[idx] = (Mel_Vec4){{tangent.x, tangent.y, tangent.z, handedness}};
        }
    }
}

Mel_Verlet_Mesh_Counts mel_verlet_cloth_mesh_counts(u32 width, u32 height)
{
    return (Mel_Verlet_Mesh_Counts){
        .vertex_count = width * height,
        .index_count = (width - 1) * (height - 1) * 6,
    };
}

void mel_verlet_cloth_mesh(
    Mel_Verlet_System* sys,
    u32 width, u32 height,
    Mel_Vec3* out_positions,
    Mel_Vec3* out_normals,
    Mel_Vec2* out_uvs,
    Mel_Vec4* out_colors,
    u32* out_indices,
    Mel_Vec4 color)
{
    assert(sys);
    assert(width * height <= sys->count);

    for (u32 i = 0; i < width * height; i++) {
        out_positions[i] = sys->particles[i].pos;
        out_normals[i] = sys->particles[i].normal;
        if (out_uvs) out_uvs[i] = sys->particles[i].uv;
        out_colors[i] = color;
    }

    u32 idx = 0;
    for (u32 r = 0; r < height - 1; r++) {
        for (u32 c = 0; c < width - 1; c++) {
            u32 tl = mel__cloth_idx(r, c, width);
            u32 tr = mel__cloth_idx(r, c + 1, width);
            u32 bl = mel__cloth_idx(r + 1, c, width);
            u32 br = mel__cloth_idx(r + 1, c + 1, width);

            out_indices[idx++] = tl;
            out_indices[idx++] = bl;
            out_indices[idx++] = tr;

            out_indices[idx++] = tr;
            out_indices[idx++] = bl;
            out_indices[idx++] = br;
        }
    }
}
