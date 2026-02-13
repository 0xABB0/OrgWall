#pragma once

#define unused(x) ((void)(x))

#define countof(a)    (size)(sizeof(a) / sizeof(*(a)) \
    + 0 * sizeof(char[1 - 2 * __builtin_types_compatible_p(__typeof__(a), __typeof__(&(a)[0]))]))
#define lengthof(s)   (countof(s) - 1)
#define new(a, t, n)  (t *)alloc(a, sizeof(t), _Alignof(t), n)

