#pragma once

#include "types.h"

// s for string and 8 for utf8
#define s8(s) (s8){(u8 *)s, lengthof(s)}
typedef struct {
    u8*  data;
    size len;
} s8;

typedef s8 str8;

s8   s8span(u8 *, u8 *);
bool  s8equals(s8, s8);
size s8compare(s8, s8);
u64  s8hash(s8);
s8   s8trim(s8);
/* s8    s8clone(s8, arena*); */
