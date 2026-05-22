#pragma once

#include <core/types.h>
#include <thread/storage.h>

#include <stdalign.h>
#include <stdatomic.h>

typedef u64 Mel_Thread_Id;

typedef int (*Mel_Thread_Fn)(void* user);

typedef struct Mel_Thread_Spawn_Opt {
    const char* name;
    usize       stack_size;
} Mel_Thread_Spawn_Opt;

typedef struct Mel_Thread {
    alignas(MEL_THREAD_HANDLE_ALIGN) byte _handle[MEL_THREAD_HANDLE_SIZE];
    Mel_Thread_Id  id;
    Mel_Thread_Fn  fn;
    void*          user;
    _Atomic(u32)   finished;
    char           name[16];
} Mel_Thread;

bool          mel_thread_spawn_opt   (Mel_Thread* t, Mel_Thread_Fn fn, void* user, Mel_Thread_Spawn_Opt opt);
#define       mel_thread_spawn(t, fn, user, ...) mel_thread_spawn_opt((t), (fn), (user), (Mel_Thread_Spawn_Opt){__VA_ARGS__})

bool          mel_thread_join        (Mel_Thread* t, int* exit_code);
bool          mel_thread_detach      (Mel_Thread* t);
bool          mel_thread_is_finished (const Mel_Thread* t);

Mel_Thread_Id mel_thread_current_id  (void);
bool          mel_thread_id_equal    (Mel_Thread_Id a, Mel_Thread_Id b);

void          mel_thread_yield       (void);
void          mel_thread_sleep       (i64 ns);
void          mel_thread_set_name    (const char* name);
u32           mel_thread_hardware_concurrency(void);
