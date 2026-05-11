#include "nat_acc.h"
#include "pitch.h"
#include <stdlib.h>
#include <string.h>

void mel_nat_acc_notation_free(Mel_NatAccNotation* nn)
{
  if (!nn) return;
  for (int32_t i = 0; i < nn->naturals.count; i++)
    free(nn->naturals.symbols[i]);
  free(nn->naturals.symbols);
  free(nn->naturals.pitch_indices);
  mel_symbol_code_free(&nn->acc_code);
  for (int32_t i = 0; i < nn->interval_codes.count; i++)
    mel_symbol_code_free(&nn->interval_codes.codes[i]);
  free(nn->interval_codes.codes);
}

Mel_NatAccNotation mel_nat_acc_notation_make(const Mel_Tuning* tuning)
{
  Mel_NatAccNotation nn;
  nn.base = mel_notation_make(tuning);
  nn.naturals.symbols = NULL;
  nn.naturals.pitch_indices = NULL;
  nn.naturals.count = 0;
  nn.acc_code = mel_symbol_code_make();
  nn.interval_codes.codes = NULL;
  nn.interval_codes.count = 0;
  return nn;
}

void mel_nat_acc_add_natural(Mel_NatAccNotation* nn, const char* symbol, int32_t pitch_index)
{
  int32_t idx = nn->naturals.count;
  nn->naturals.count++;
  nn->naturals.symbols = realloc(nn->naturals.symbols, nn->naturals.count * sizeof(char*));
  nn->naturals.pitch_indices = realloc(nn->naturals.pitch_indices, nn->naturals.count * sizeof(int32_t));
  nn->naturals.symbols[idx] = strdup(symbol);
  nn->naturals.pitch_indices[idx] = pitch_index;
}

void mel_nat_acc_add_accidental(Mel_NatAccNotation* nn, const char* symbol, int32_t value)
{
  mel_symbol_code_add(&nn->acc_code, symbol, value);
}

void mel_nat_acc_add_accidental_at(Mel_NatAccNotation* nn, const char* symbol, int32_t value, int32_t position)
{
  mel_symbol_code_add_at(&nn->acc_code, symbol, value, position);
}

void mel_nat_acc_set_interval_code(Mel_NatAccNotation* nn, int32_t nat_diff_class, Mel_SymbolCode code)
{
  if (nat_diff_class >= nn->interval_codes.count)
  {
    int32_t old_count = nn->interval_codes.count;
    nn->interval_codes.count = nat_diff_class + 1;
    nn->interval_codes.codes = realloc(nn->interval_codes.codes, nn->interval_codes.count * sizeof(Mel_SymbolCode));
    for (int32_t i = old_count; i < nn->interval_codes.count; i++)
      nn->interval_codes.codes[i] = mel_symbol_code_make();
  }
  mel_symbol_code_free(&nn->interval_codes.codes[nat_diff_class]);
  nn->interval_codes.codes[nat_diff_class] = code;
}

static int32_t mel_nat_acc_parse_symbol(Mel_NatAccNotation* nn, const char* symbol, int32_t* out_nat_index, int32_t* out_acc_value)
{
  int32_t best_nat_idx = -1;
  int32_t best_nat_len = 0;

  for (int32_t i = 0; i < nn->naturals.count; i++)
  {
    int32_t len = (int32_t)strlen(nn->naturals.symbols[i]);
    if (strncmp(symbol, nn->naturals.symbols[i], len) == 0 && len > best_nat_len)
    {
      best_nat_idx = i;
      best_nat_len = len;
    }
  }

  if (best_nat_idx < 0) return -1;

  *out_nat_index = best_nat_idx;

  const char* acc_tail = symbol + best_nat_len;
  *out_acc_value = mel_symbol_code_parse(&nn->acc_code, acc_tail);

  return 0;
}

Mel_Note mel_nat_acc_note(Mel_NatAccNotation* nn, const char* symbol, int32_t nat_bi_index)
{
  Mel_Note note;
  note.notation = &nn->base;
  note.symbol = strdup(symbol);
  note.nat_bi_index = nat_bi_index;
  note.acc_count = 0;
  note.acc_vector = NULL;

  int32_t nat_index, acc_value;
  if (mel_nat_acc_parse_symbol(nn, symbol, &nat_index, &acc_value) < 0)
  {
    note.pitch = mel_pitch_make(nn->base.tuning, 0);
    return note;
  }

  int32_t period = (int32_t)mel_tuning_period_length(nn->base.tuning);
  if (period == 0) period = 1;

  int32_t nat_pitch = nn->naturals.pitch_indices[nat_index];
  int32_t pitch_index = nat_pitch + nat_bi_index * period + acc_value;

  note.pitch = mel_pitch_make(nn->base.tuning, pitch_index);
  return note;
}

Mel_Note mel_nat_acc_note_by_index(Mel_NatAccNotation* nn, int32_t nat_index, int32_t* acc_vector, int32_t acc_count)
{
  Mel_Note note;
  note.notation = &nn->base;
  note.symbol = NULL;
  note.nat_bi_index = nat_index / nn->naturals.count;
  note.acc_count = acc_count;
  note.acc_vector = NULL;

  if (acc_count > 0)
  {
    note.acc_vector = malloc(acc_count * sizeof(int32_t));
    memcpy(note.acc_vector, acc_vector, acc_count * sizeof(int32_t));
  }

  int32_t nat_class = nat_index % nn->naturals.count;
  int32_t period = (int32_t)mel_tuning_period_length(nn->base.tuning);
  if (period == 0) period = 1;

  int32_t nat_pitch = nn->naturals.pitch_indices[nat_class];
  int32_t acc_sum = 0;
  for (int32_t i = 0; i < acc_count; i++) acc_sum += acc_vector[i];

  int32_t pitch_index = nat_pitch + note.nat_bi_index * period + acc_sum;
  note.pitch = mel_pitch_make(nn->base.tuning, pitch_index);

  return note;
}

Mel_NoteInterval mel_nat_acc_interval(Mel_NatAccNotation* nn, Mel_Note source, Mel_Note target)
{
  Mel_NoteInterval ni;
  ni.notation = &nn->base;
  ni.pitch_interval = mel_interval_from_pitches(source.pitch, target.pitch);
  ni.nat_diff = 0;
  ni.acc_count = 0;
  ni.acc_vector = NULL;
  ni.symbol = NULL;
  ni.number = 0;
  return ni;
}

char* mel_nat_acc_note_symbol(Mel_NatAccNotation* nn, Mel_Note note)
{
  int64_t pc = mel_pitch_pc_index(note.pitch);
  int64_t period = mel_tuning_period_length(nn->base.tuning);

  int32_t best_nat = 0;
  int32_t best_diff = 999999;

  for (int32_t i = 0; i < nn->naturals.count; i++)
  {
    int32_t nat_pc = nn->naturals.pitch_indices[i] % (int32_t)period;
    int32_t diff = (int32_t)pc - nat_pc;
    int32_t abs_diff = diff >= 0 ? diff : -diff;

    if (abs_diff < best_diff)
    {
      best_diff = abs_diff;
      best_nat = i;
    }
  }

  int32_t nat_pc = nn->naturals.pitch_indices[best_nat] % (int32_t)period;
  int32_t acc_value = (int32_t)pc - nat_pc;

  char* acc_str = mel_symbol_code_generate(&nn->acc_code, acc_value);
  const char* nat_str = nn->naturals.symbols[best_nat];

  size_t len = strlen(nat_str) + strlen(acc_str) + 1;
  char* symbol = malloc(len);
  strcpy(symbol, nat_str);
  strcat(symbol, acc_str);
  free(acc_str);

  return symbol;
}
