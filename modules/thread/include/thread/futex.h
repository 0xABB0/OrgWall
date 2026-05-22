#pragma once

#include <core/types.h>

#include <stdatomic.h>

void mel_futex_wait    (_Atomic(u32)* addr, u32 expected);
bool mel_futex_wait_for(_Atomic(u32)* addr, u32 expected, i64 timeout_ns);
void mel_futex_wake_one(_Atomic(u32)* addr);
void mel_futex_wake_all(_Atomic(u32)* addr);
