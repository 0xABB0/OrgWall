#pragma once

#include "log.cfg.h"
#include "log.fwd.h"
#include "string.str8.fwd.h"

#ifdef __cplusplus
extern "C" {
#endif

#if MEL_LOG_DISABLED

#define MEL_LOG(level, domain, fmt, ...)
#define mel_log_fatal(domain, fmt, ...)
#define mel_log_error(domain, fmt, ...)
#define mel_log_warn(domain, fmt, ...)
#define mel_log_info(domain, fmt, ...)
#define mel_log_debug(domain, fmt, ...)
#define mel_log_trace(domain, fmt, ...)
#define MEL_LOG_SCOPE(tag)
#define mel_log_context_push(tag)
#define mel_log_context_pop()
#define mel_log_set_global_frame(f)
#define mel_log_set_sim_frame(f)
#define mel_log_set_fixed_tick(t)
#define mel_log_signal(level, msg)

#else

#define MEL_LOG_FATAL   100
#define MEL_LOG_ERROR   200
#define MEL_LOG_WARN    300
#define MEL_LOG_INFO    400
#define MEL_LOG_DEBUG   500
#define MEL_LOG_TRACE   600

struct Mel_Log_Entry {
    u64     timestamp_ns;
    u32     level;
    str8    domain;
    str8    message;
    str8    file;
    u32     line;
    u32     thread_id;
    u64     global_frame;
    u64     sim_frame;
    u32     fixed_tick;
    str8    context;
};

void mel__log(u32 level, str8 domain, const char* file, u32 line, const char* fmt, ...) __attribute__((format(printf, 5, 6)));

#define MEL_LOG(level, domain, fmt, ...) mel__log((level), (str8){(u8*)(domain), sizeof(domain) - 1}, __FILE__, __LINE__, (fmt), ##__VA_ARGS__)

#define mel_log_fatal(domain, fmt, ...)  MEL_LOG(MEL_LOG_FATAL, domain, fmt, ##__VA_ARGS__)
#define mel_log_error(domain, fmt, ...)  MEL_LOG(MEL_LOG_ERROR, domain, fmt, ##__VA_ARGS__)
#define mel_log_warn(domain, fmt, ...)   MEL_LOG(MEL_LOG_WARN, domain, fmt, ##__VA_ARGS__)
#define mel_log_info(domain, fmt, ...)   MEL_LOG(MEL_LOG_INFO, domain, fmt, ##__VA_ARGS__)
#define mel_log_debug(domain, fmt, ...)  MEL_LOG(MEL_LOG_DEBUG, domain, fmt, ##__VA_ARGS__)
#define mel_log_trace(domain, fmt, ...)  MEL_LOG(MEL_LOG_TRACE, domain, fmt, ##__VA_ARGS__)

void mel_log_context_push(str8 tag);
void mel_log_context_pop(void);
void mel__log_context_cleanup(str8* tag);

#define MEL_LOG_SCOPE(tag) \
    __attribute__((cleanup(mel__log_context_cleanup))) \
    str8 mel__log_scope_##__LINE__ = (mel_log_context_push(S8(tag)), S8(tag))

void mel_log_set_global_frame(u64 frame);
void mel_log_set_sim_frame(u64 frame);
void mel_log_set_fixed_tick(u32 tick);

typedef struct {
    str8 stack[MEL_LOG_MAX_CONTEXT_DEPTH];
    u32  depth;
    u64  global_frame;
    u64  sim_frame;
    u32  fixed_tick;
} Mel_Log_Thread_State;

Mel_Log_Thread_State mel_log_thread_state_save(void);
void                 mel_log_thread_state_restore(Mel_Log_Thread_State state);

Mel_Log_Sink_Handle mel_log_sink_add(Mel_Log_Sink* sink);
void                mel_log_sink_remove(Mel_Log_Sink_Handle handle);
void                mel_log_sink_flush_all(void);

void mel_log_level_register(u32 value, str8 name);
str8 mel_log_level_name(u32 value);

void mel__log_signal(u32 level, const char* static_message);
#define mel_log_signal(level, msg) mel__log_signal((level), (msg))

#endif

#ifdef __cplusplus
}
#endif
