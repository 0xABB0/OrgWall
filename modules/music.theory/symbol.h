#pragma once

#include <core/compiler.h>

#include <stdint.h>

typedef struct Mel_SymbolCode Mel_SymbolCode;
typedef struct Mel_SymbolCodeEntry Mel_SymbolCodeEntry;

struct Mel_SymbolCodeEntry
{
  const char* symbol;
  int32_t value;
  int32_t position;
};

struct Mel_SymbolCode
{
  Mel_SymbolCodeEntry* entries;
  int32_t count;
  int32_t capacity;
};

void mel_symbol_code_free(Mel_SymbolCode* sc);
static inline void mel_symbol_code_cleanup(Mel_SymbolCode* sc) { mel_symbol_code_free(sc); }
#define Mel_SymbolCode_AUTO MEL_CLEANUP(mel_symbol_code_cleanup) Mel_SymbolCode

Mel_SymbolCode mel_symbol_code_make(void);

void mel_symbol_code_add(Mel_SymbolCode* sc, const char* symbol, int32_t value);

void mel_symbol_code_add_at(Mel_SymbolCode* sc, const char* symbol, int32_t value, int32_t position);

int32_t mel_symbol_code_parse(const Mel_SymbolCode* sc, const char* str);

char* mel_symbol_code_generate(const Mel_SymbolCode* sc, int32_t value);
