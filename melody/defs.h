#pragma once

#define unused(x) ((void)(x))

#define countof(a)    (size)(sizeof(a) / sizeof(*(a)))
#define lengthof(s)   (countof(s) - 1)
#define new(a, t, n)  (t *)alloc(a, sizeof(t), _Alignof(t), n)

