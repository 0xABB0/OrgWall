#pragma once

#include <coro/abi.h>

#if defined(MEL_CORO_CODEGEN)

int __mel_coro_yield(int, ...);
int __mel_coro_await(int, ...);
int __mel_coro_return(int, ...);

#define mel_coro(name, params, ret, ...) __attribute__((annotate("mel:coro"))) ret name params

#define mel_coro_yield(...)              __mel_coro_yield(0 __VA_OPT__(, ) __VA_ARGS__)
#define mel_coro_await(...)              __mel_coro_await(0 __VA_OPT__(, ) __VA_ARGS__)
#define mel_coro_return(...)             __mel_coro_return(0 __VA_OPT__(, ) __VA_ARGS__)

#else

#if defined(__GNUC__) || defined(__clang__)
#define MEL_CORO__UNUSED __attribute__((unused))
#else
#define MEL_CORO__UNUSED
#endif

#define mel_coro(name, params, ret, ...) MEL_CORO__UNUSED static inline ret name##__mel_src params

#define mel_coro_yield(...)              ((void)(0 __VA_OPT__(, (__VA_ARGS__))))
#define mel_coro_await(...)              ((void)(0 __VA_OPT__(, (__VA_ARGS__))))
#define mel_coro_return(...)             return __VA_OPT__((__VA_ARGS__))

#endif
