#pragma once

#include <continuation/abi.h>

#if defined(MEL_CONT_CODEGEN)

int __mel_cont_yield(int, ...);
int __mel_cont_await(int, ...);
int __mel_cont_return(int, ...);

#define mel_cont(name, params, ret, ...) __attribute__((annotate("mel:continuation"))) ret name params

#define mel_cont_yield(...)  __mel_cont_yield(0 __VA_OPT__(, ) __VA_ARGS__)
#define mel_cont_await(...)  __mel_cont_await(0 __VA_OPT__(, ) __VA_ARGS__)
#define mel_cont_return(...) __mel_cont_return(0 __VA_OPT__(, ) __VA_ARGS__)

#else

#define mel_cont(name, params, ret, ...) ret name params

#define mel_cont_yield(...)  ((void)(0 __VA_OPT__(, (__VA_ARGS__))))
#define mel_cont_await(...)  ((void)(0 __VA_OPT__(, (__VA_ARGS__))))
#define mel_cont_return(...) return __VA_OPT__ ((__VA_ARGS__))

#endif
