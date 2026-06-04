#include <log/log.h>

#include <core/platform.h>

#if !MEL_LOG_DISABLED && MEL_PLATFORM_ANDROID

#include <log.sink/sink.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>

#include <android/log.h>

#include <stdio.h>
#include <string.h>

static android_LogPriority mel__log_android_priority(u32 level)
{
    if (level <= MEL_LOG_FATAL)
        return ANDROID_LOG_FATAL;
    if (level <= MEL_LOG_ERROR)
        return ANDROID_LOG_ERROR;
    if (level <= MEL_LOG_WARN)
        return ANDROID_LOG_WARN;
    if (level <= MEL_LOG_INFO)
        return ANDROID_LOG_INFO;
    if (level <= MEL_LOG_DEBUG)
        return ANDROID_LOG_DEBUG;
    return ANDROID_LOG_VERBOSE;
}

static void mel__log_sink_android_write(Mel_Log_Sink* self, const Mel_Log_Entry* entry)
{
    (void)self;

    char tag[64];
    int  tn = (int)entry->domain.len;
    if (tn > (int)sizeof(tag) - 1)
        tn = (int)sizeof(tag) - 1;
    if (tn > 0)
        memcpy(tag, entry->domain.data, (size_t)tn);
    tag[tn] = 0;

    char msg[MEL_LOG_MAX_MESSAGE_SIZE + 256];
    int  written;
    if (entry->context.len > 0)
        written = snprintf(msg, sizeof(msg), "(%.*s) %.*s    (%.*s:%u)", (int)entry->context.len, entry->context.data, (int)entry->message.len, entry->message.data, (int)entry->file.len, entry->file.data, entry->line);
    else
        written = snprintf(msg, sizeof(msg), "%.*s    (%.*s:%u)", (int)entry->message.len, entry->message.data, (int)entry->file.len, entry->file.data, entry->line);
    (void)written;

    __android_log_write(mel__log_android_priority(entry->level), tag[0] ? tag : "melody", msg);
}

static void mel__log_sink_android_flush(Mel_Log_Sink* self) { (void)self; }

static void mel__log_sink_android_destroy(Mel_Log_Sink* self) { mel_dealloc(mel_alloc_heap(), self); }

Mel_Log_Sink* mel__log_sink_android_create(void)
{
    Mel_Log_Sink* sink = mel_alloc_type(mel_alloc_heap(), Mel_Log_Sink);
    *sink = (Mel_Log_Sink){
        .write = mel__log_sink_android_write,
        .flush = mel__log_sink_android_flush,
        .destroy = mel__log_sink_android_destroy,
        .level_threshold = MEL_LOG_TRACE,
    };
    return sink;
}

#endif
