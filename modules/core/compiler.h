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

#if MEL_COMPILER_CLANG || MEL_COMPILER_GCC
    #define MEL_CLEANUP(fn)         __attribute__((cleanup(fn)))
    #define MEL_CONSTRUCTOR         __attribute__((constructor))
    #define MEL_CONSTRUCTOR_PRIO(p) __attribute__((constructor(p)))
    #define MEL_DESTRUCTOR          __attribute__((destructor))
    #define MEL_DESTRUCTOR_PRIO(p)  __attribute__((destructor(p)))
    #define MEL_WEAK                __attribute__((weak))
    #define MEL_SECTION(name)       __attribute__((section(name)))
    #define MEL_MODE(m)             __attribute__((mode(m)))
    #define MEL_INTERRUPT           __attribute__((interrupt()))
#else
    #define MEL_CLEANUP(fn)
    #define MEL_CONSTRUCTOR
    #define MEL_CONSTRUCTOR_PRIO(p)
    #define MEL_DESTRUCTOR
    #define MEL_DESTRUCTOR_PRIO(p)
    #define MEL_WEAK
    #define MEL_SECTION(name)
    #define MEL_MODE(m)
    #define MEL_INTERRUPT
#endif

#if defined(__cplusplus) && __cplusplus >= MEL_LANG_CPP17
    #define MEL_MAYBE_UNUSED [[maybe_unused]]
#elif MEL_COMPILER_CLANG || MEL_COMPILER_GCC
    #define MEL_MAYBE_UNUSED __attribute__((unused))
#else
    #define MEL_MAYBE_UNUSED
#endif

#if MEL_COMPILER_CLANG
    #define MEL_VECTOR_TYPE(n) __attribute__((ext_vector_type(n)))
#else
    #define MEL_VECTOR_TYPE(n)
#endif

#if __has_attribute(nonnull)
    #define MEL_NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))
    #define MEL_NONNULL_ALL  __attribute__((nonnull))
#else
    #define MEL_NONNULL(...)
    #define MEL_NONNULL_ALL
#endif

#if __has_attribute(returns_nonnull)
    #define MEL_RETURNS_NONNULL __attribute__((returns_nonnull))
#else
    #define MEL_RETURNS_NONNULL
#endif

#if __has_attribute(null_terminated_string_arg)
    #define MEL_NULL_TERMINATED_STRING_ARG(n) __attribute__((null_terminated_string_arg(n)))
#else
    #define MEL_NULL_TERMINATED_STRING_ARG(n)
#endif

#if __has_attribute(sentinel)
    #define MEL_SENTINEL       __attribute__((sentinel))
    #define MEL_SENTINEL_AT(n) __attribute__((sentinel(n)))
#else
    #define MEL_SENTINEL
    #define MEL_SENTINEL_AT(n)
#endif

#if __has_attribute(access)
    #define MEL_ACCESS_RO(...)  __attribute__((access(read_only,  __VA_ARGS__)))
    #define MEL_ACCESS_WO(...)  __attribute__((access(write_only, __VA_ARGS__)))
    #define MEL_ACCESS_RW(...)  __attribute__((access(read_write, __VA_ARGS__)))
    #define MEL_ACCESS_NONE(n)  __attribute__((access(none, n)))
#else
    #define MEL_ACCESS_RO(...)
    #define MEL_ACCESS_WO(...)
    #define MEL_ACCESS_RW(...)
    #define MEL_ACCESS_NONE(n)
#endif

#if __has_attribute(malloc)
    #define MEL_MALLOC                   __attribute__((malloc))
    #define MEL_MALLOC_DEALLOC(fn)       __attribute__((malloc(fn)))
    #define MEL_MALLOC_DEALLOC_AT(fn, n) __attribute__((malloc(fn, n)))
#else
    #define MEL_MALLOC
    #define MEL_MALLOC_DEALLOC(fn)
    #define MEL_MALLOC_DEALLOC_AT(fn, n)
#endif

#if __has_attribute(alloc_size)
    #define MEL_ALLOC_SIZE(n)      __attribute__((alloc_size(n)))
    #define MEL_ALLOC_SIZE_2(n, m) __attribute__((alloc_size(n, m)))
#else
    #define MEL_ALLOC_SIZE(n)
    #define MEL_ALLOC_SIZE_2(n, m)
#endif

#if __has_attribute(alloc_align)
    #define MEL_ALLOC_ALIGN(n) __attribute__((alloc_align(n)))
#else
    #define MEL_ALLOC_ALIGN(n)
#endif

#if __has_attribute(assume_aligned)
    #define MEL_ASSUME_ALIGNED(n) __attribute__((assume_aligned(n)))
#else
    #define MEL_ASSUME_ALIGNED(n)
#endif

#if __has_attribute(format)
    #define MEL_SCANF_FORMAT(fmt, args)   __attribute__((format(scanf,   fmt, args)))
    #define MEL_STRFTIME_FORMAT(fmt)      __attribute__((format(strftime, fmt, 0)))
    #define MEL_STRFMON_FORMAT(fmt, args) __attribute__((format(strfmon, fmt, args)))
#else
    #define MEL_SCANF_FORMAT(fmt, args)
    #define MEL_STRFTIME_FORMAT(fmt)
    #define MEL_STRFMON_FORMAT(fmt, args)
#endif

#if __has_attribute(format_arg)
    #define MEL_FORMAT_ARG(n) __attribute__((format_arg(n)))
#else
    #define MEL_FORMAT_ARG(n)
#endif

#if __has_attribute(hot)
    #define MEL_HOT __attribute__((hot))
#else
    #define MEL_HOT
#endif

#if __has_attribute(cold)
    #define MEL_COLD __attribute__((cold))
#else
    #define MEL_COLD
#endif

#if __has_attribute(flatten)
    #define MEL_FLATTEN __attribute__((flatten))
#else
    #define MEL_FLATTEN
#endif

#if __has_attribute(optimize)
    #define MEL_OPTIMIZE(s) __attribute__((optimize(s)))
#else
    #define MEL_OPTIMIZE(s)
#endif

#if __has_attribute(target)
    #define MEL_TARGET(s) __attribute__((target(s)))
#else
    #define MEL_TARGET(s)
#endif

#if __has_attribute(target_clones)
    #define MEL_TARGET_CLONES(s) __attribute__((target_clones(s)))
#else
    #define MEL_TARGET_CLONES(s)
#endif

#if __has_attribute(visibility)
    #define MEL_VISIBILITY(v) __attribute__((visibility(v)))
    #define MEL_EXPORT        __attribute__((visibility("default")))
    #define MEL_HIDDEN        __attribute__((visibility("hidden")))
#elif MEL_COMPILER_MSVC
    #define MEL_VISIBILITY(v)
    #define MEL_EXPORT        __declspec(dllexport)
    #define MEL_HIDDEN
#else
    #define MEL_VISIBILITY(v)
    #define MEL_EXPORT
    #define MEL_HIDDEN
#endif

#if __has_attribute(used)
    #define MEL_USED __attribute__((used))
#else
    #define MEL_USED
#endif

#if __has_attribute(error)
    #define MEL_DIAG_ERROR(msg) __attribute__((error(msg)))
#else
    #define MEL_DIAG_ERROR(msg)
#endif

#if __has_attribute(warning)
    #define MEL_DIAG_WARNING(msg) __attribute__((warning(msg)))
#else
    #define MEL_DIAG_WARNING(msg)
#endif
