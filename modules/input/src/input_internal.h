#pragma once

#include <input/input.h>
#include <input/provider.h>

bool mel_input__stable_id(Mel_Input_Device d, u64* out_id);

typedef struct Mel_Input_Sink Mel_Input_Sink;
Mel_Input_Sink*               mel_input__sink(void);

typedef u32 (*Mel_Input_Enumerate_Fn)(const Mel_Alloc* alloc, Mel_Input_Raw* out, u32 cap);

void mel_input__set_test_provider(const Mel_Input_Provider_Desc* desc);

u32 mel_input_events__changed_fields(const Mel_Input_Device_Descriptor* a, const Mel_Input_Device_Descriptor* b);
