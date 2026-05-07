#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct Mel_Encoder Mel_Encoder;

typedef void* (*Mel_Encoder_Encode)(Mel_Encoder* encoder, void* data, size_t data_size);
typedef void* (*Mel_Encoder_Decode)(Mel_Encoder* encoder, void* data);

struct Mel_Encoder
{
  void* user;
  Mel_Encoder_Decode decode;
  Mel_Encoder_Encode encode;
};

void* encode(Mel_Encoder* encoder, void* data, size_t data_size);
void* decode(Mel_Encoder* encoder, void* data, size_t data_size);


