#pragma once

#include "assert.cfg.h"

#include <core/compiler.h>
#include <core/types.h>
#include <string/str8.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef u32 Mel_Assert_Response;

#define MEL_ASSERT_RESPONSE_RETRY          (1u << 0)
#define MEL_ASSERT_RESPONSE_IGNORE_ONCE    (1u << 1)
#define MEL_ASSERT_RESPONSE_IGNORE_FOREVER (1u << 2)
#define MEL_ASSERT_RESPONSE_ABORT          (1u << 3)
#define MEL_ASSERT_RESPONSE_BREAK          (1u << 4)

static inline bool mel_assert_response_retry(Mel_Assert_Response r) { return (r & MEL_ASSERT_RESPONSE_RETRY) != 0u; }
static inline bool mel_assert_response_break(Mel_Assert_Response r) { return (r & MEL_ASSERT_RESPONSE_BREAK) != 0u; }
static inline bool mel_assert_response_abort(Mel_Assert_Response r) { return (r & MEL_ASSERT_RESPONSE_ABORT) != 0u; }
static inline bool mel_assert_response_ignore_forever(Mel_Assert_Response r) { return (r & MEL_ASSERT_RESPONSE_IGNORE_FOREVER) != 0u; }
static inline bool mel_assert_response_ignored(Mel_Assert_Response r) { return (r & (MEL_ASSERT_RESPONSE_IGNORE_ONCE | MEL_ASSERT_RESPONSE_IGNORE_FOREVER)) != 0u; }

typedef struct Mel_Stacktrace Mel_Stacktrace;

typedef struct
{
    str8            condition;
    str8            location;
    str8            message;
    u32             level;
    Mel_Stacktrace* stack;
} Mel_Assert_Report;

typedef Mel_Assert_Response (*Mel_Assert_Handler)(const Mel_Assert_Report* report, void* user);

typedef struct
{
    Mel_Assert_Handler handler;
    void*              user;
} Mel_Assert_Handler_Slot;

void                    mel_assert_install_handler(Mel_Assert_Handler handler, void* user);
Mel_Assert_Handler_Slot mel_assert_handler(void);
Mel_Assert_Response     mel_assert_default_handler(const Mel_Assert_Report* report, void* user);
Mel_Assert_Response     mel_assert_interactive_handler(const Mel_Assert_Report* report, void* user);
bool                    mel_assert_interactive_available(void);
void                    mel_abort(void);

#define MEL__ASSERT_STR2(x) #x
#define MEL__ASSERT_STR(x)  MEL__ASSERT_STR2(x)

#if MEL_ASSERT_LEVEL >= MEL_ASSERT_LEVEL_RELEASE

void                mel_assert_fail(str8 condition, str8 location);
Mel_Assert_Response mel__assert_report(u32 level, str8 condition, str8 location, str8 message);

#define MEL__ASSERT_AT(level_, msg_, ...)                                                                                                        \
    do                                                                                                                                           \
    {                                                                                                                                            \
        if ((level_) <= MEL_ASSERT_LEVEL)                                                                                                        \
        {                                                                                                                                        \
            static bool mel__assert_silenced = false;                                                                                            \
            while (!mel__assert_silenced && !(__VA_ARGS__))                                                                                      \
            {                                                                                                                                    \
                Mel_Assert_Response mel__r = mel__assert_report((level_), S8(#__VA_ARGS__), S8(__FILE__ ":" MEL__ASSERT_STR(__LINE__)), (msg_)); \
                if (mel_assert_response_ignore_forever(mel__r))                                                                                  \
                    mel__assert_silenced = true;                                                                                                 \
                if (mel_assert_response_break(mel__r))                                                                                           \
                    MEL_BREAKPOINT();                                                                                                            \
                if (mel_assert_response_abort(mel__r))                                                                                           \
                    mel_abort();                                                                                                                 \
                if (!mel_assert_response_retry(mel__r))                                                                                          \
                    break;                                                                                                                       \
            }                                                                                                                                    \
        }                                                                                                                                        \
    } while (0)

#define mel_assert_release(...)        MEL__ASSERT_AT(MEL_ASSERT_LEVEL_RELEASE, STR8_EMPTY, __VA_ARGS__)
#define mel_assert_release_msg(m, ...) MEL__ASSERT_AT(MEL_ASSERT_LEVEL_RELEASE, S8(m), __VA_ARGS__)

#else

#define mel_assert_release(...)        ((void)sizeof(__VA_ARGS__))
#define mel_assert_release_msg(m, ...) ((void)sizeof(__VA_ARGS__))

#endif

#if MEL_ASSERT_LEVEL >= MEL_ASSERT_LEVEL_DEBUG

#define mel_assert(...)        MEL__ASSERT_AT(MEL_ASSERT_LEVEL_DEBUG, STR8_EMPTY, __VA_ARGS__)
#define mel_assert_msg(m, ...) MEL__ASSERT_AT(MEL_ASSERT_LEVEL_DEBUG, S8(m), __VA_ARGS__)

#else

#define mel_assert(...)        ((void)sizeof(__VA_ARGS__))
#define mel_assert_msg(m, ...) ((void)sizeof(__VA_ARGS__))

#endif

#if MEL_ASSERT_LEVEL >= MEL_ASSERT_LEVEL_PARANOID

#define mel_assert_paranoid(...)        MEL__ASSERT_AT(MEL_ASSERT_LEVEL_PARANOID, STR8_EMPTY, __VA_ARGS__)
#define mel_assert_paranoid_msg(m, ...) MEL__ASSERT_AT(MEL_ASSERT_LEVEL_PARANOID, S8(m), __VA_ARGS__)

#else

#define mel_assert_paranoid(...)        ((void)sizeof(__VA_ARGS__))
#define mel_assert_paranoid_msg(m, ...) ((void)sizeof(__VA_ARGS__))

#endif

#ifdef __cplusplus
}
#endif
