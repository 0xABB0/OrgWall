#include <music.theory/symbol.h>
#include <stdlib.h>
#include <string.h>

#define MEL_SYMBOL_CODE_INITIAL_CAPACITY 16

void mel_symbol_code_free(Mel_SymbolCode* sc)
{
  if (!sc) return;
  for (int32_t i = 0; i < sc->count; i++)
    free((void*)sc->entries[i].symbol);
  free(sc->entries);
  sc->entries = NULL;
  sc->count = 0;
  sc->capacity = 0;
}

Mel_SymbolCode mel_symbol_code_make(void)
{
  Mel_SymbolCode sc;
  sc.entries = malloc(MEL_SYMBOL_CODE_INITIAL_CAPACITY * sizeof(Mel_SymbolCodeEntry));
  sc.count = 0;
  sc.capacity = MEL_SYMBOL_CODE_INITIAL_CAPACITY;
  return sc;
}

static void mel_symbol_code_grow(Mel_SymbolCode* sc)
{
  sc->capacity *= 2;
  sc->entries = realloc(sc->entries, sc->capacity * sizeof(Mel_SymbolCodeEntry));
}

void mel_symbol_code_add(Mel_SymbolCode* sc, const char* symbol, int32_t value)
{
  mel_symbol_code_add_at(sc, symbol, value, sc->count);
}

void mel_symbol_code_add_at(Mel_SymbolCode* sc, const char* symbol, int32_t value, int32_t position)
{
  if (sc->count >= sc->capacity) mel_symbol_code_grow(sc);

#ifdef _WIN32
  sc->entries[sc->count].symbol = _strdup(symbol);
#else
  sc->entries[sc->count].symbol = strdup(symbol);
#endif
  sc->entries[sc->count].value = value;
  sc->entries[sc->count].position = position;
  sc->count++;
}

int32_t mel_symbol_code_parse(const Mel_SymbolCode* sc, const char* str)
{
  int32_t total = 0;
  const char* cursor = str;

  while (*cursor)
  {
    int32_t best_len = 0;
    int32_t best_value = 0;

    for (int32_t i = 0; i < sc->count; i++)
    {
      int32_t len = (int32_t)strlen(sc->entries[i].symbol);
      if (strncmp(cursor, sc->entries[i].symbol, len) == 0 && len > best_len)
      {
        best_len = len;
        best_value = sc->entries[i].value;
      }
    }

    if (best_len == 0) return total;

    total += best_value;
    cursor += best_len;
  }

  return total;
}

static int mel_symbol_code_entry_cmp(const void* a, const void* b)
{
  const Mel_SymbolCodeEntry* ea = (const Mel_SymbolCodeEntry*)a;
  const Mel_SymbolCodeEntry* eb = (const Mel_SymbolCodeEntry*)b;

  if (ea->position != eb->position)
    return ea->position - eb->position;

  int32_t abs_a = ea->value >= 0 ? ea->value : -ea->value;
  int32_t abs_b = eb->value >= 0 ? eb->value : -eb->value;
  return abs_b - abs_a;
}

char* mel_symbol_code_generate(const Mel_SymbolCode* sc, int32_t value)
{
  if (sc->count == 0)
  {
    char* s = malloc(1);
    s[0] = '\0';
    return s;
  }

  Mel_SymbolCodeEntry* sorted = malloc(sc->count * sizeof(Mel_SymbolCodeEntry));
  memcpy(sorted, sc->entries, sc->count * sizeof(Mel_SymbolCodeEntry));
  qsort(sorted, sc->count, sizeof(Mel_SymbolCodeEntry), mel_symbol_code_entry_cmp);

  char* buf = malloc(4096);
  buf[0] = '\0';
  int32_t remaining = value;

  while (remaining != 0)
  {
    int32_t best_idx = -1;

    for (int32_t i = 0; i < sc->count; i++)
    {
      int32_t v = sorted[i].value;
      if (remaining > 0 && v > 0) { best_idx = i; break; }
      if (remaining < 0 && v < 0) { best_idx = i; break; }
    }

    if (best_idx < 0) break;

    int32_t v = sorted[best_idx].value;
    while ((remaining > 0 && remaining >= v) || (remaining < 0 && remaining <= v))
    {
#ifdef _WIN32
      strcat_s(buf, 4096, sorted[best_idx].symbol);
#else
      strcat(buf, sorted[best_idx].symbol);
#endif
      remaining -= v;
    }
  }

  free(sorted);

  if (buf[0] == '\0')
  {
    free(buf);
    buf = malloc(1);
    buf[0] = '\0';
  }

  return buf;
}
