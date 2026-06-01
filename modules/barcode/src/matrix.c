#include <barcode/matrix.h>

bool mel_barcode_matrix_init(mel_barcode_matrix* m, i32 width, i32 height, const Mel_Alloc* allocator)
{
    if (width <= 0 || height <= 0 || allocator == NULL)
    {
        return false;
    }
    usize count = (usize)width * (usize)height;
    u8*   modules = mel_calloc(allocator, count);
    if (modules == NULL)
    {
        return false;
    }
    m->modules = modules;
    m->width = width;
    m->height = height;
    m->quiet_zone = 0;
    m->allocator = allocator;
    return true;
}

void mel_barcode_matrix_free(mel_barcode_matrix* m)
{
    if (m->modules != NULL)
    {
        mel_dealloc(m->allocator, m->modules);
    }
    m->modules = NULL;
    m->width = 0;
    m->height = 0;
    m->quiet_zone = 0;
    m->allocator = NULL;
}

bool mel_barcode_matrix_get(const mel_barcode_matrix* m, i32 x, i32 y)
{
    assert(x >= 0 && x < m->width && y >= 0 && y < m->height);
    return m->modules[(usize)y * (usize)m->width + (usize)x] != 0;
}

void mel_barcode_matrix_set(mel_barcode_matrix* m, i32 x, i32 y, bool dark)
{
    assert(x >= 0 && x < m->width && y >= 0 && y < m->height);
    m->modules[(usize)y * (usize)m->width + (usize)x] = dark ? 1 : 0;
}

void mel_barcode_matrix_fill_column(mel_barcode_matrix* m, i32 x, bool dark)
{
    assert(x >= 0 && x < m->width);
    u8 v = dark ? 1 : 0;
    for (i32 y = 0; y < m->height; ++y)
    {
        m->modules[(usize)y * (usize)m->width + (usize)x] = v;
    }
}
