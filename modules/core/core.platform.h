#pragma once

#define MEL_PLATFORM_WINDOWS  0
#define MEL_PLATFORM_OSX      0
#define MEL_PLATFORM_IOS      0
#define MEL_PLATFORM_LINUX    0
#define MEL_PLATFORM_ANDROID  0
#define MEL_PLATFORM_POSIX    0
#define MEL_PLATFORM_APPLE    0
#define MEL_CPU_ARM           0
#define MEL_CPU_X86           0
#define MEL_ARCH_32BIT        0
#define MEL_ARCH_64BIT        0

#if defined(_WIN32) || defined(_WIN64)
    #undef  MEL_PLATFORM_WINDOWS
    #define MEL_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #undef  MEL_PLATFORM_APPLE
    #define MEL_PLATFORM_APPLE 1
    #undef  MEL_PLATFORM_POSIX
    #define MEL_PLATFORM_POSIX 1
    #if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
        #undef  MEL_PLATFORM_IOS
        #define MEL_PLATFORM_IOS 1
    #else
        #undef  MEL_PLATFORM_OSX
        #define MEL_PLATFORM_OSX 1
    #endif
#elif defined(__ANDROID__)
    #undef  MEL_PLATFORM_ANDROID
    #define MEL_PLATFORM_ANDROID 1
    #undef  MEL_PLATFORM_POSIX
    #define MEL_PLATFORM_POSIX 1
#elif defined(__linux__)
    #undef  MEL_PLATFORM_LINUX
    #define MEL_PLATFORM_LINUX 1
    #undef  MEL_PLATFORM_POSIX
    #define MEL_PLATFORM_POSIX 1
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
    #undef  MEL_CPU_ARM
    #define MEL_CPU_ARM 1
    #undef  MEL_ARCH_64BIT
    #define MEL_ARCH_64BIT 1
#elif defined(__arm__) || defined(_M_ARM)
    #undef  MEL_CPU_ARM
    #define MEL_CPU_ARM 1
    #undef  MEL_ARCH_32BIT
    #define MEL_ARCH_32BIT 1
#elif defined(__x86_64__) || defined(_M_X64)
    #undef  MEL_CPU_X86
    #define MEL_CPU_X86 1
    #undef  MEL_ARCH_64BIT
    #define MEL_ARCH_64BIT 1
#elif defined(__i386__) || defined(_M_IX86)
    #undef  MEL_CPU_X86
    #define MEL_CPU_X86 1
    #undef  MEL_ARCH_32BIT
    #define MEL_ARCH_32BIT 1
#endif
