#pragma once

#include "str8.h"
#include <allocator/allocator.fwd.h>
#include <allocator/arena.fwd.h>

size        str8_levenshtein_arena(str8 a, str8 b, Mel_Arena* arena);
size        str8_levenshtein_alloc(str8 a, str8 b, const Mel_Alloc* alloc);

#define str8_levenshtein(a, b, allocator) _Generic((allocator), \
    Mel_Arena*: str8_levenshtein_arena, \
    const Mel_Alloc*: str8_levenshtein_alloc \
)(a, b, allocator)
