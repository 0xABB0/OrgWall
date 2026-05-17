#include <string/levenshtein.h>
#include <allocator/allocator.h>
#include <allocator.arena/arena.h>

size str8_levenshtein_arena(str8 a, str8 b, Mel_Arena* arena)
{
    if (a.len == 0) return b.len;
    if (b.len == 0) return a.len;

    if (a.len < b.len) { str8 tmp = a; a = b; b = tmp; }

    Mel_Arena_Scratch scratch = mel_arena_scratch_begin(arena);

    size cols = b.len + 1;
    size* prev = mel_arena_push_array(arena, size, (usize)cols);
    size* curr = mel_arena_push_array(arena, size, (usize)cols);

    for (size j = 0; j < cols; j++) prev[j] = j;

    for (size i = 1; i <= a.len; i++)
    {
        curr[0] = i;
        for (size j = 1; j <= b.len; j++)
        {
            size cost = (a.data[i - 1] == b.data[j - 1]) ? 0 : 1;
            size del = prev[j] + 1;
            size ins = curr[j - 1] + 1;
            size sub = prev[j - 1] + cost;

            size min = del;
            if (ins < min) min = ins;
            if (sub < min) min = sub;
            curr[j] = min;
        }
        size* tmp = prev;
        prev = curr;
        curr = tmp;
    }

    size result = prev[b.len];
    mel_arena_scratch_discard(scratch);
    return result;
}

size str8_levenshtein_alloc(str8 a, str8 b, const Mel_Alloc* alloc)
{
    if (a.len == 0) return b.len;
    if (b.len == 0) return a.len;

    if (a.len < b.len) { str8 tmp = a; a = b; b = tmp; }

    size cols = b.len + 1;
    size* prev = (size*)mel_alloc(alloc, (usize)(cols * (size)sizeof(size)));
    size* curr = (size*)mel_alloc(alloc, (usize)(cols * (size)sizeof(size)));

    for (size j = 0; j < cols; j++) prev[j] = j;

    for (size i = 1; i <= a.len; i++)
    {
        curr[0] = i;
        for (size j = 1; j <= b.len; j++)
        {
            size cost = (a.data[i - 1] == b.data[j - 1]) ? 0 : 1;
            size del = prev[j] + 1;
            size ins = curr[j - 1] + 1;
            size sub = prev[j - 1] + cost;

            size min = del;
            if (ins < min) min = ins;
            if (sub < min) min = sub;
            curr[j] = min;
        }
        size* tmp = prev;
        prev = curr;
        curr = tmp;
    }

    size result = prev[b.len];
    mel_dealloc(alloc, prev);
    mel_dealloc(alloc, curr);
    return result;
}
