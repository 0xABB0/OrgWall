#pragma once

#include "platform.h"

#define MEL_COMPILER_CLANG  0
#define MEL_COMPILER_GCC    0
#define MEL_COMPILER_MSVC   0

#define MEL_CRT_BIONIC      0
#define MEL_CRT_GLIBC       0
#define MEL_CRT_LIBCXX      0
#define MEL_CRT_MINGW       0
#define MEL_CRT_MSVC        0
#define MEL_CRT_NEWLIB      0

#define MEL_LANG_C99        199901L
#define MEL_LANG_C11        201112L
#define MEL_LANG_C17        201710L
#define MEL_LANG_C23        202311L
#define MEL_LANG_CPP98      199711L
#define MEL_LANG_CPP11      201103L
#define MEL_LANG_CPP14      201402L
#define MEL_LANG_CPP17      201703L
#define MEL_LANG_CPP20      202002L
#define MEL_LANG_CPP23      202302L

#if defined(__clang__)
    #undef  MEL_COMPILER_CLANG
    #define MEL_COMPILER_CLANG (__clang_major__ * 10000 + __clang_minor__ * 100 + __clang_patchlevel__)
#elif defined(_MSC_VER)
    #undef  MEL_COMPILER_MSVC
    #define MEL_COMPILER_MSVC _MSC_VER
#elif defined(__GNUC__)
    #undef  MEL_COMPILER_GCC
    #define MEL_COMPILER_GCC (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#else
    #error "MEL_COMPILER_* is not defined!"
#endif

#if defined(__BIONIC__)
    #undef  MEL_CRT_BIONIC
    #define MEL_CRT_BIONIC 1
#elif defined(__GLIBC__)
    #undef  MEL_CRT_GLIBC
    #define MEL_CRT_GLIBC (__GLIBC__ * 10000 + __GLIBC_MINOR__ * 100)
#elif defined(__MINGW32__) || defined(__MINGW64__)
    #undef  MEL_CRT_MINGW
    #define MEL_CRT_MINGW 1
#elif defined(_MSC_VER)
    #undef  MEL_CRT_MSVC
    #define MEL_CRT_MSVC 1
#elif defined(__NEWLIB__)
    #undef  MEL_CRT_NEWLIB
    #define MEL_CRT_NEWLIB 1
#elif defined(__apple_build_version__) || defined(__llvm__)
    #undef  MEL_CRT_LIBCXX
    #define MEL_CRT_LIBCXX 1
#endif

#if MEL_COMPILER_MSVC && defined(__cplusplus) && defined(_MSVC_LANG) && _MSVC_LANG != __cplusplus
    #error "When using MSVC you must set /Zc:__cplusplus compiler option."
#endif

#if MEL_COMPILER_MSVC && (!defined(_MSVC_TRADITIONAL) || _MSVC_TRADITIONAL)
    #error "When using MSVC you must set /Zc:preprocessor compiler option."
#endif

#ifndef __has_attribute
    #define __has_attribute(x) 0
#endif
#ifndef __has_builtin
    #define __has_builtin(x) 0
#endif
#ifndef __has_cpp_attribute
    #define __has_cpp_attribute(x) 0
#endif

#if MEL_COMPILER_MSVC
    #define MEL_FORCE_INLINE __forceinline
    #define MEL_NO_INLINE    __declspec(noinline)
#else
    #define MEL_FORCE_INLINE inline __attribute__((always_inline))
    #define MEL_NO_INLINE    __attribute__((noinline))
#endif

#if defined(__cplusplus) && __cplusplus >= MEL_LANG_CPP11
    #define MEL_NO_RETURN [[noreturn]]
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= MEL_LANG_C11
    #define MEL_NO_RETURN _Noreturn
#elif MEL_COMPILER_MSVC
    #define MEL_NO_RETURN __declspec(noreturn)
#else
    #define MEL_NO_RETURN __attribute__((noreturn))
#endif

#if defined(__cplusplus) && __cplusplus >= MEL_LANG_CPP11
    #define MEL_THREAD_LOCAL thread_local
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= MEL_LANG_C11 && !defined(__STDC_NO_THREADS__)
    #define MEL_THREAD_LOCAL _Thread_local
#elif MEL_COMPILER_MSVC
    #define MEL_THREAD_LOCAL __declspec(thread)
#else
    #define MEL_THREAD_LOCAL __thread
#endif

#if defined(__cplusplus) && __cplusplus >= MEL_LANG_CPP11
    #define MEL_ALIGNAS(n) alignas(n)
    #define MEL_ALIGNOF(T) alignof(T)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= MEL_LANG_C11
    #define MEL_ALIGNAS(n) _Alignas(n)
    #define MEL_ALIGNOF(T) _Alignof(T)
#elif MEL_COMPILER_MSVC
    #define MEL_ALIGNAS(n) __declspec(align(n))
    #define MEL_ALIGNOF(T) __alignof(T)
#else
    #define MEL_ALIGNAS(n) __attribute__((aligned(n)))
    #define MEL_ALIGNOF(T) __alignof__(T)
#endif

#if defined(__cplusplus)
    #if MEL_COMPILER_MSVC
        #define MEL_RESTRICT __restrict
    #else
        #define MEL_RESTRICT __restrict__
    #endif
#else
    #define MEL_RESTRICT restrict
#endif

#if MEL_COMPILER_CLANG || MEL_COMPILER_GCC
    #define MEL_LIKELY(x)   __builtin_expect(!!(x), 1)
    #define MEL_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define MEL_LIKELY(x)   (x)
    #define MEL_UNLIKELY(x) (x)
#endif

#if defined(__cplusplus) && __cplusplus >= MEL_LANG_CPP17
    #define MEL_NODISCARD [[nodiscard]]
#elif MEL_COMPILER_CLANG || MEL_COMPILER_GCC
    #define MEL_NODISCARD __attribute__((warn_unused_result))
#else
    #define MEL_NODISCARD
#endif

#if defined(__cplusplus) && __cplusplus >= MEL_LANG_CPP14
    #define MEL_DEPRECATED [[deprecated]]
#elif MEL_COMPILER_MSVC
    #define MEL_DEPRECATED __declspec(deprecated)
#else
    #define MEL_DEPRECATED __attribute__((deprecated))
#endif

#if defined(__cplusplus) && __cplusplus >= MEL_LANG_CPP17
    #define MEL_FALLTHROUGH [[fallthrough]]
#elif __has_attribute(fallthrough)
    #define MEL_FALLTHROUGH __attribute__((fallthrough))
#else
    #define MEL_FALLTHROUGH ((void)0)
#endif

#if MEL_COMPILER_MSVC
    #define MEL_UNREACHABLE() __assume(0)
    #define MEL_ASSUME(x)     __assume(x)
#elif MEL_COMPILER_CLANG
    #define MEL_UNREACHABLE() __builtin_unreachable()
    #define MEL_ASSUME(x)     __builtin_assume(x)
#else
    #define MEL_UNREACHABLE() __builtin_unreachable()
    #define MEL_ASSUME(x)     do { if (!(x)) __builtin_unreachable(); } while (0)
#endif

#if MEL_COMPILER_MSVC
    #define MEL_BREAKPOINT() __debugbreak()
#elif __has_builtin(__builtin_debugtrap)
    #define MEL_BREAKPOINT() __builtin_debugtrap()
#else
    #define MEL_BREAKPOINT() __builtin_trap()
#endif

#if MEL_COMPILER_MSVC
    #define MEL_FUNCTION __FUNCSIG__
#else
    #define MEL_FUNCTION __PRETTY_FUNCTION__
#endif

#if MEL_COMPILER_CLANG || MEL_COMPILER_GCC
    #define MEL_PRINTF_FORMAT(fmt, args) __attribute__((format(__printf__, fmt, args)))
#else
    #define MEL_PRINTF_FORMAT(fmt, args)
#endif

#if MEL_COMPILER_CLANG || MEL_COMPILER_GCC
    #define MEL_CONST __attribute__((const))
    #define MEL_PURE  __attribute__((pure))
#else
    #define MEL_CONST
    #define MEL_PURE
#endif

#if MEL_COMPILER_CLANG
    #define MEL_OVERLOADABLE __attribute__((overloadable))
#else
    #define MEL_OVERLOADABLE
#endif
