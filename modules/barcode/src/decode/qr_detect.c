#include <barcode/qr_detect.h>

#include <allocator/allocator.h>

#include <math.mat/mat3.h>
#include <math.vector/vec2.h>

#include "../qr_internal.h"

#include <string.h>

typedef struct
{
    f32 x, y;
    f32 module;
} mel__qr_finder;

typedef struct
{
    f32 x, y;
    f32 module;
    i32 count;
} mel__qr_cluster;

typedef struct
{
    mel__qr_finder*  items;
    usize            count;
    usize            cap;
    const Mel_Alloc* a;
} mel__qr_finders;

static bool mel__qr_finders_push(mel__qr_finders* f, f32 x, f32 y, f32 module)
{
    if (f->count == f->cap)
    {
        usize           ncap = f->cap == 0 ? 64 : f->cap * 2;
        mel__qr_finder* grown = f->items == NULL ? mel_alloc(f->a, ncap * sizeof(mel__qr_finder)) : mel_realloc(f->a, f->items, ncap * sizeof(mel__qr_finder));
        if (grown == NULL)
        {
            return false;
        }
        f->items = grown;
        f->cap = ncap;
    }
    f->items[f->count].x = x;
    f->items[f->count].y = y;
    f->items[f->count].module = module;
    f->count += 1;
    return true;
}

static i32 mel__qr_otsu(const mel_image_gray* g)
{
    i32 hist[256];
    memset(hist, 0, sizeof hist);
    for (i32 y = 0; y < g->h; ++y)
    {
        const u8* row = g->pixels + (usize)y * (usize)g->stride;
        for (i32 x = 0; x < g->w; ++x)
        {
            hist[row[x]] += 1;
        }
    }

    i64 total = (i64)g->w * (i64)g->h;
    i64 sum_all = 0;
    for (i32 i = 0; i < 256; ++i)
    {
        sum_all += (i64)i * hist[i];
    }

    i64 sum_bg = 0;
    i64 weight_bg = 0;
    i64 best_var = -1;
    i32 best_t = 127;
    for (i32 t = 0; t < 256; ++t)
    {
        weight_bg += hist[t];
        if (weight_bg == 0)
        {
            continue;
        }
        i64 weight_fg = total - weight_bg;
        if (weight_fg == 0)
        {
            break;
        }
        sum_bg += (i64)t * hist[t];
        i64 mean_bg_num = sum_bg;
        i64 mean_fg_num = sum_all - sum_bg;
        f64 mean_bg = (f64)mean_bg_num / (f64)weight_bg;
        f64 mean_fg = (f64)mean_fg_num / (f64)weight_fg;
        f64 diff = mean_bg - mean_fg;
        f64 var = (f64)weight_bg * (f64)weight_fg * diff * diff;
        if ((i64)var > best_var)
        {
            best_var = (i64)var;
            best_t = t;
        }
    }
    return best_t;
}

static bool mel__qr_ratio_ok(const i32 run[5])
{
    i32 total = run[0] + run[1] + run[2] + run[3] + run[4];
    if (total < 7)
    {
        return false;
    }
    i32 side_lo = 4 * total;
    i32 side_hi = 16 * total;
    i32 mid_lo = 24 * total;
    i32 mid_hi = 36 * total;
    i32 s0 = 70 * run[0];
    i32 s1 = 70 * run[1];
    i32 s2 = 70 * run[2];
    i32 s3 = 70 * run[3];
    i32 s4 = 70 * run[4];
    if (s0 < side_lo || s0 > side_hi)
    {
        return false;
    }
    if (s1 < side_lo || s1 > side_hi)
    {
        return false;
    }
    if (s2 < mid_lo || s2 > mid_hi)
    {
        return false;
    }
    if (s3 < side_lo || s3 > side_hi)
    {
        return false;
    }
    if (s4 < side_lo || s4 > side_hi)
    {
        return false;
    }
    return true;
}

static i32 mel__qr_px(const u8* line, i32 stride, i32 i, i32 t) { return line[(usize)i * (usize)stride] <= t ? 1 : 0; }

static i32 mel__qr_count_match(const u8* line, i32 stride, i32 n, i32 center, i32 t, f32 module)
{
    if (!mel__qr_px(line, stride, center, t))
    {
        return -1;
    }

    i32 run[5] = { 0, 0, 0, 0, 0 };

    i32 i = center;
    while (i >= 0 && mel__qr_px(line, stride, i, t))
    {
        run[2] += 1;
        i -= 1;
    }
    while (i >= 0 && !mel__qr_px(line, stride, i, t))
    {
        run[1] += 1;
        i -= 1;
    }
    while (i >= 0 && mel__qr_px(line, stride, i, t))
    {
        run[0] += 1;
        i -= 1;
    }
    i32 start = i + 1;

    i = center + 1;
    while (i < n && mel__qr_px(line, stride, i, t))
    {
        run[2] += 1;
        i += 1;
    }
    while (i < n && !mel__qr_px(line, stride, i, t))
    {
        run[3] += 1;
        i += 1;
    }
    while (i < n && mel__qr_px(line, stride, i, t))
    {
        run[4] += 1;
        i += 1;
    }

    if (run[0] == 0 || run[1] == 0 || run[2] == 0 || run[3] == 0 || run[4] == 0)
    {
        return -1;
    }
    if (!mel__qr_ratio_ok(run))
    {
        return -1;
    }
    i32 total = run[0] + run[1] + run[2] + run[3] + run[4];
    f32 unit = (f32)total / 7.0f;
    if (module > 0.0f && (unit < module * 0.5f || unit > module * 2.0f))
    {
        return -1;
    }
    return start + run[0] + run[1] + run[2] / 2;
}

static bool mel__qr_verify_center(const mel_image_gray* g, i32 t, i32 cx, i32 cy, f32 module, f32* out_cy)
{
    const u8* col = g->pixels + (usize)cx;
    i32       refined = mel__qr_count_match(col, g->stride, g->h, cy, t, module);
    if (refined < 0)
    {
        return false;
    }
    *out_cy = (f32)refined;
    return true;
}

static bool mel__qr_collect_finders(const mel_image_gray* g, i32 t, mel__qr_finders* out)
{
    for (i32 y = 0; y < g->h; ++y)
    {
        const u8* row = g->pixels + (usize)y * (usize)g->stride;

        i32  run[5] = { 0, 0, 0, 0, 0 };
        i32  filled = 0;
        bool cur = (row[0] <= t);
        i32  run_end = 0;
        for (i32 x = 0; x <= g->w; ++x)
        {
            bool d = (x < g->w) ? (row[x] <= t) : !cur;
            if (d == cur && x < g->w)
            {
                continue;
            }

            i32 len = x - run_end;
            run_end = x;

            if (filled < 5)
            {
                run[filled] = len;
                filled += 1;
            }
            else
            {
                run[0] = run[1];
                run[1] = run[2];
                run[2] = run[3];
                run[3] = run[4];
                run[4] = len;
            }

            if (filled >= 5 && cur && mel__qr_ratio_ok(run))
            {
                i32 total = run[0] + run[1] + run[2] + run[3] + run[4];
                f32 module = (f32)total / 7.0f;
                i32 center_x = x - run[4] - run[3] - run[2] / 2;
                f32 cyf = 0;
                if (mel__qr_verify_center(g, t, center_x, y, module, &cyf))
                {
                    if (!mel__qr_finders_push(out, (f32)center_x, cyf, module))
                    {
                        return false;
                    }
                }
            }
            cur = d;
        }
    }
    return true;
}

static bool mel__qr_cluster_finders(const mel__qr_finders* in, mel__qr_cluster* out, i32* out_n, i32 max_out)
{
    i32 n = 0;
    for (usize i = 0; i < in->count; ++i)
    {
        f32  x = in->items[i].x;
        f32  y = in->items[i].y;
        f32  m = in->items[i].module;
        bool merged = false;
        for (i32 c = 0; c < n; ++c)
        {
            f32 dx = out[c].x / (f32)out[c].count - x;
            f32 dy = out[c].y / (f32)out[c].count - y;
            f32 tol = m * 2.0f + 4.0f;
            if (dx * dx + dy * dy <= tol * tol)
            {
                out[c].x += x;
                out[c].y += y;
                out[c].module += m;
                out[c].count += 1;
                merged = true;
                break;
            }
        }
        if (!merged)
        {
            if (n >= max_out)
            {
                continue;
            }
            out[n].x = x;
            out[n].y = y;
            out[n].module = m;
            out[n].count = 1;
            n += 1;
        }
    }
    for (i32 c = 0; c < n; ++c)
    {
        out[c].x /= (f32)out[c].count;
        out[c].y /= (f32)out[c].count;
        out[c].module /= (f32)out[c].count;
    }
    *out_n = n;
    return n >= 3;
}

static f32 mel__qr_dist2(mel__qr_cluster a, mel__qr_cluster b)
{
    f32 dx = a.x - b.x;
    f32 dy = a.y - b.y;
    return dx * dx + dy * dy;
}

static f32 mel__qr_triple_score(mel__qr_cluster a, mel__qr_cluster b, mel__qr_cluster c)
{
    f32 mmin = a.module < b.module ? a.module : b.module;
    mmin = c.module < mmin ? c.module : mmin;
    f32 mmax = a.module > b.module ? a.module : b.module;
    mmax = c.module > mmax ? c.module : mmax;
    if (mmin <= 0.0f)
    {
        return -1.0f;
    }
    f32 module_ratio = mmax / mmin;

    f32 dab = mel__qr_dist2(a, b);
    f32 dbc = mel__qr_dist2(b, c);
    f32 dac = mel__qr_dist2(a, c);

    f32 hyp = dab;
    f32 leg0 = dbc;
    f32 leg1 = dac;
    if (dbc >= dab && dbc >= dac)
    {
        hyp = dbc;
        leg0 = dab;
        leg1 = dac;
    }
    else if (dac >= dab && dac >= dbc)
    {
        hyp = dac;
        leg0 = dab;
        leg1 = dbc;
    }
    if (leg0 <= 0.0f || leg1 <= 0.0f)
    {
        return -1.0f;
    }

    f32 leg_lo = leg0 < leg1 ? leg0 : leg1;
    f32 leg_hi = leg0 > leg1 ? leg0 : leg1;
    f32 leg_ratio = leg_hi / leg_lo;

    f32 right_err = hyp / (leg0 + leg1);

    f32 right_dev = right_err > 1.0f ? right_err - 1.0f : 1.0f - right_err;

    return module_ratio + leg_ratio + right_dev * 2.0f;
}

static bool mel__qr_pick_triple(const mel__qr_cluster* c, i32 n, mel__qr_cluster out[3])
{
    if (n < 3)
    {
        return false;
    }
    f32 best = -1.0f;
    i32 bi = -1, bj = -1, bk = -1;
    for (i32 i = 0; i < n; ++i)
    {
        for (i32 j = i + 1; j < n; ++j)
        {
            for (i32 k = j + 1; k < n; ++k)
            {
                f32 s = mel__qr_triple_score(c[i], c[j], c[k]);
                if (s < 0.0f)
                {
                    continue;
                }
                if (bi < 0 || s < best)
                {
                    best = s;
                    bi = i;
                    bj = j;
                    bk = k;
                }
            }
        }
    }
    if (bi < 0)
    {
        return false;
    }
    out[0] = c[bi];
    out[1] = c[bj];
    out[2] = c[bk];
    return true;
}

static void mel__qr_order_finders(mel__qr_cluster* f, mel__qr_cluster* tl, mel__qr_cluster* tr, mel__qr_cluster* bl)
{
    f32 d01 = mel__qr_dist2(f[0], f[1]);
    f32 d12 = mel__qr_dist2(f[1], f[2]);
    f32 d02 = mel__qr_dist2(f[0], f[2]);

    i32 ci;
    if (d12 >= d01 && d12 >= d02)
    {
        ci = 0;
    }
    else if (d02 >= d01 && d02 >= d12)
    {
        ci = 1;
    }
    else
    {
        ci = 2;
    }

    i32 oa = (ci + 1) % 3;
    i32 ob = (ci + 2) % 3;
    *tl = f[ci];

    f32 cross = (f[oa].x - tl->x) * (f[ob].y - tl->y) - (f[oa].y - tl->y) * (f[ob].x - tl->x);
    if (cross > 0.0f)
    {
        *tr = f[oa];
        *bl = f[ob];
    }
    else
    {
        *tr = f[ob];
        *bl = f[oa];
    }
}

static Mel_Mat3 mel__qr_homography(const Mel_Vec2 src[4], const Mel_Vec2 dst[4])
{
    f32 a[8][8];
    f32 b[8];
    for (i32 i = 0; i < 4; ++i)
    {
        f32 sx = src[i].x, sy = src[i].y;
        f32 dx = dst[i].x, dy = dst[i].y;
        a[2 * i][0] = sx;
        a[2 * i][1] = sy;
        a[2 * i][2] = 1;
        a[2 * i][3] = 0;
        a[2 * i][4] = 0;
        a[2 * i][5] = 0;
        a[2 * i][6] = -sx * dx;
        a[2 * i][7] = -sy * dx;
        b[2 * i] = dx;

        a[2 * i + 1][0] = 0;
        a[2 * i + 1][1] = 0;
        a[2 * i + 1][2] = 0;
        a[2 * i + 1][3] = sx;
        a[2 * i + 1][4] = sy;
        a[2 * i + 1][5] = 1;
        a[2 * i + 1][6] = -sx * dy;
        a[2 * i + 1][7] = -sy * dy;
        b[2 * i + 1] = dy;
    }

    for (i32 col = 0; col < 8; ++col)
    {
        i32 piv = col;
        f32 best = a[col][col] < 0 ? -a[col][col] : a[col][col];
        for (i32 r = col + 1; r < 8; ++r)
        {
            f32 v = a[r][col] < 0 ? -a[r][col] : a[r][col];
            if (v > best)
            {
                best = v;
                piv = r;
            }
        }
        if (piv != col)
        {
            for (i32 k = 0; k < 8; ++k)
            {
                f32 tmp = a[col][k];
                a[col][k] = a[piv][k];
                a[piv][k] = tmp;
            }
            f32 tb = b[col];
            b[col] = b[piv];
            b[piv] = tb;
        }
        f32 diag = a[col][col];
        if (diag == 0.0f)
        {
            return MEL_MAT3_ZERO;
        }
        for (i32 r = 0; r < 8; ++r)
        {
            if (r == col)
            {
                continue;
            }
            f32 factor = a[r][col] / diag;
            if (factor == 0.0f)
            {
                continue;
            }
            for (i32 k = col; k < 8; ++k)
            {
                a[r][k] -= factor * a[col][k];
            }
            b[r] -= factor * b[col];
        }
    }

    Mel_Mat3 h;
    h.m[0][0] = b[0] / a[0][0];
    h.m[0][1] = b[1] / a[1][1];
    h.m[0][2] = b[2] / a[2][2];
    h.m[1][0] = b[3] / a[3][3];
    h.m[1][1] = b[4] / a[4][4];
    h.m[1][2] = b[5] / a[5][5];
    h.m[2][0] = b[6] / a[6][6];
    h.m[2][1] = b[7] / a[7][7];
    h.m[2][2] = 1.0f;
    return h;
}

static Mel_Vec2 mel__qr_project(Mel_Mat3 h, f32 x, f32 y)
{
    f32 wx = h.m[0][0] * x + h.m[0][1] * y + h.m[0][2];
    f32 wy = h.m[1][0] * x + h.m[1][1] * y + h.m[1][2];
    f32 w = h.m[2][0] * x + h.m[2][1] * y + h.m[2][2];
    if (w == 0.0f)
    {
        return mel_vec2(0, 0);
    }
    return mel_vec2(wx / w, wy / w);
}

static i32 mel__qr_sample(const mel_image_gray* g, i32 t, f32 fx, f32 fy)
{
    i32 dark = 0;
    i32 seen = 0;
    for (i32 dy = -1; dy <= 1; ++dy)
    {
        for (i32 dx = -1; dx <= 1; ++dx)
        {
            i32 x = (i32)(fx + 0.5f) + dx;
            i32 y = (i32)(fy + 0.5f) + dy;
            if (x < 0 || x >= g->w || y < 0 || y >= g->h)
            {
                continue;
            }
            seen += 1;
            if (g->pixels[(usize)y * (usize)g->stride + (usize)x] <= t)
            {
                dark += 1;
            }
        }
    }
    if (seen == 0)
    {
        return -1;
    }
    return dark * 2 > seen ? 1 : 0;
}

static f32 mel__qr_align_ring(const mel_image_gray* g, i32 t, i32 x, i32 y, f32 dist_px, bool want_dark, i32* out_total)
{
    static const f32 ox[8] = { 1, 0.7071f, 0, -0.7071f, -1, -0.7071f, 0, 0.7071f };
    static const f32 oy[8] = { 0, 0.7071f, 1, 0.7071f, 0, -0.7071f, -1, -0.7071f };
    i32              match = 0;
    i32              total = 0;
    for (i32 a = 0; a < 8; ++a)
    {
        i32 rx = x + (i32)(ox[a] * dist_px + (ox[a] < 0 ? -0.5f : 0.5f));
        i32 ry = y + (i32)(oy[a] * dist_px + (oy[a] < 0 ? -0.5f : 0.5f));
        if (rx < 0 || rx >= g->w || ry < 0 || ry >= g->h)
        {
            continue;
        }
        total += 1;
        bool dark = g->pixels[(usize)ry * (usize)g->stride + (usize)rx] <= t;
        if (dark == want_dark)
        {
            match += 1;
        }
    }
    *out_total = total;
    return total == 0 ? 0.0f : (f32)match / (f32)total;
}

static bool mel__qr_refine_alignment(const mel_image_gray* g, i32 t, Mel_Mat3 h, i32 grid_x, i32 grid_y, f32 module_px, Mel_Vec2* out)
{
    Mel_Vec2 guess = mel__qr_project(h, (f32)grid_x + 0.5f, (f32)grid_y + 0.5f);
    i32      gx = (i32)(guess.x + 0.5f);
    i32      gy = (i32)(guess.y + 0.5f);
    i32      radius = (i32)(module_px * 2.5f) + 2;

    f32 best_score = -1.0f;
    f64 acc_x = 0.0;
    f64 acc_y = 0.0;
    i32 acc_n = 0;
    for (i32 y = gy - radius; y <= gy + radius; ++y)
    {
        if (y < 0 || y >= g->h)
        {
            continue;
        }
        for (i32 x = gx - radius; x <= gx + radius; ++x)
        {
            if (x < 0 || x >= g->w)
            {
                continue;
            }
            if (g->pixels[(usize)y * (usize)g->stride + (usize)x] > t)
            {
                continue;
            }
            i32 inner_total = 0;
            i32 outer_total = 0;
            f32 inner = mel__qr_align_ring(g, t, x, y, module_px, false, &inner_total);
            f32 outer = mel__qr_align_ring(g, t, x, y, module_px * 2.0f, true, &outer_total);
            if (inner_total < 6 || outer_total < 6)
            {
                continue;
            }
            f32 score = inner + outer;
            if (score > best_score + 1e-4f)
            {
                best_score = score;
                acc_x = (f64)x;
                acc_y = (f64)y;
                acc_n = 1;
            }
            else if (score > best_score - 1e-4f)
            {
                acc_x += (f64)x;
                acc_y += (f64)y;
                acc_n += 1;
            }
        }
    }
    if (best_score < 1.5f || acc_n == 0)
    {
        return false;
    }
    *out = mel_vec2((f32)(acc_x / (f64)acc_n), (f32)(acc_y / (f64)acc_n));
    return true;
}

static u32 mel__qr_version_bch(u32 version)
{
    u32 rem = version;
    for (i32 i = 0; i < 12; ++i)
    {
        rem = (rem << 1) ^ (((rem >> 11) & 1u) * 0x1F25u);
    }
    return ((u32)version << 12) | rem;
}

static i32 mel__qr_popcount18(u32 v)
{
    i32 n = 0;
    for (i32 i = 0; i < 18; ++i)
    {
        n += (i32)((v >> i) & 1u);
    }
    return n;
}

static i32 mel__qr_version_correct(u32 raw)
{
    i32 best_v = -1;
    i32 best_dist = 19;
    for (i32 v = 7; v <= MEL__QR_MAXV; ++v)
    {
        i32 dist = mel__qr_popcount18(raw ^ mel__qr_version_bch((u32)v));
        if (dist < best_dist)
        {
            best_dist = dist;
            best_v = v;
        }
    }
    if (best_v < 0 || best_dist > 3)
    {
        return -1;
    }
    return best_v;
}

static i32 mel__qr_read_version_grid(const u8* g, i32 size, bool transpose)
{
    u32 bits = 0;
    for (i32 i = 0; i < 18; ++i)
    {
        i32 col = size - 11 + i % 3;
        i32 row = i / 3;
        i32 idx = transpose ? (col * size + row) : (row * size + col);
        bits |= (u32)(g[idx] & 1u) << i;
    }
    return mel__qr_version_correct(bits);
}

bool mel_qr_detect_image(const mel_image_gray* gray, mel_barcode_matrix* out_grid, const Mel_Alloc* a)
{
    if (gray == NULL || out_grid == NULL || a == NULL || gray->pixels == NULL)
    {
        return false;
    }
    if (gray->w < 21 || gray->h < 21)
    {
        return false;
    }

    i32 t = mel__qr_otsu(gray);

    mel__qr_finders finders = { NULL, 0, 0, a };
    if (!mel__qr_collect_finders(gray, t, &finders))
    {
        mel_dealloc(a, finders.items);
        return false;
    }
    if (finders.count < 3)
    {
        mel_dealloc(a, finders.items);
        return false;
    }

    i32              max_clusters = (i32)finders.count;
    mel__qr_cluster* clusters = mel_alloc(a, (usize)max_clusters * sizeof(mel__qr_cluster));
    if (clusters == NULL)
    {
        mel_dealloc(a, finders.items);
        return false;
    }
    i32  ncluster = 0;
    bool clustered = mel__qr_cluster_finders(&finders, clusters, &ncluster, max_clusters);
    mel_dealloc(a, finders.items);
    if (!clustered)
    {
        mel_dealloc(a, clusters);
        return false;
    }

    mel__qr_cluster top3[3];
    bool            picked = mel__qr_pick_triple(clusters, ncluster, top3);
    mel_dealloc(a, clusters);
    if (!picked)
    {
        return false;
    }

    mel__qr_cluster tl, tr, bl;
    mel__qr_order_finders(top3, &tl, &tr, &bl);

    f32 module_px = (tl.module + tr.module + bl.module) / 3.0f;
    if (module_px < 1.0f)
    {
        return false;
    }

    f32 span_tr = mel_vec2_dist(mel_vec2(tl.x, tl.y), mel_vec2(tr.x, tr.y));
    f32 span_bl = mel_vec2_dist(mel_vec2(tl.x, tl.y), mel_vec2(bl.x, bl.y));
    f32 span = (span_tr + span_bl) / 2.0f;

    f32 dim_est = span / module_px + 7.0f;
    i32 dim = (i32)(dim_est + 0.5f);
    dim = ((dim - 17 + 2) / 4) * 4 + 17;
    if (dim < 21)
    {
        return false;
    }
    i32 version = (dim - 17) / 4;
    if (version < 1 || version > MEL__QR_MAXV)
    {
        return false;
    }
    i32 size = dim;

    f32      c = 3.5f;
    Mel_Vec2 src[4] = {
        mel_vec2(c, c),
        mel_vec2((f32)size - c, c),
        mel_vec2(c, (f32)size - c),
        mel_vec2((f32)size - c, (f32)size - c),
    };
    Mel_Vec2 dst[4] = {
        mel_vec2(tl.x, tl.y),
        mel_vec2(tr.x, tr.y),
        mel_vec2(bl.x, bl.y),
        mel_vec2(tr.x + bl.x - tl.x, tr.y + bl.y - tl.y),
    };

    Mel_Mat3 h = mel__qr_homography(src, dst);
    if (h.m[2][2] == 0.0f && h.m[0][0] == 0.0f)
    {
        return false;
    }

    const i32* al = MEL__QR_ALIGN[version - 1];
    if (al[0] >= 2)
    {
        i32      apos = al[al[0]];
        Mel_Vec2 found;
        if (mel__qr_refine_alignment(gray, t, h, apos, apos, module_px, &found))
        {
            src[3] = mel_vec2((f32)apos + 0.5f, (f32)apos + 0.5f);
            dst[3] = found;
            Mel_Mat3 refined = mel__qr_homography(src, dst);
            if (!(refined.m[2][2] == 0.0f && refined.m[0][0] == 0.0f))
            {
                h = refined;
            }
        }
    }

    if (!mel_barcode_matrix_init(out_grid, size, size, a))
    {
        return false;
    }

    for (i32 row = 0; row < size; ++row)
    {
        for (i32 col = 0; col < size; ++col)
        {
            Mel_Vec2 p = mel__qr_project(h, (f32)col + 0.5f, (f32)row + 0.5f);
            i32      v = mel__qr_sample(gray, t, p.x, p.y);
            if (v < 0)
            {
                mel_barcode_matrix_free(out_grid);
                return false;
            }
            out_grid->modules[(usize)row * (usize)size + (usize)col] = (u8)v;
        }
    }

    if (version >= 7)
    {
        i32 va = mel__qr_read_version_grid(out_grid->modules, size, false);
        i32 vb = mel__qr_read_version_grid(out_grid->modules, size, true);
        i32 decoded = va >= 0 ? va : vb;
        if (decoded >= 0 && decoded != version)
        {
            mel_barcode_matrix_free(out_grid);
            return false;
        }
    }

    out_grid->quiet_zone = 0;
    return true;
}

bool mel_qr_decode_image_gf(const mel_image_gray* gray, mel_qr_decoded* out, mel_gf* gf, const Mel_Alloc* a)
{
    if (gray == NULL || out == NULL || a == NULL)
    {
        return false;
    }
    mel_barcode_matrix grid;
    if (!mel_qr_detect_image(gray, &grid, a))
    {
        return false;
    }
    bool ok = mel_qr_decode_gf(&grid, out, gf, a);
    mel_barcode_matrix_free(&grid);
    return ok;
}

bool mel_qr_decode_image(const mel_image_gray* gray, mel_qr_decoded* out, const Mel_Alloc* a) { return mel_qr_decode_image_gf(gray, out, NULL, a); }
