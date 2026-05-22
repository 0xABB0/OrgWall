#include <music.theory/note.h>
#include <stdlib.h>
#include <string.h>

void mel_note_free(Mel_Note* n)
{
  if (!n) return;
  free(n->symbol);
  free(n->acc_vector);
}

void mel_note_interval_free(Mel_NoteInterval* ni)
{
  if (!ni) return;
  mpfr_clear(ni->pitch_interval.ratio);
  free(ni->acc_vector);
  free(ni->symbol);
}

Mel_Note mel_note_copy(Mel_Note n)
{
  Mel_Note c;
  c.notation = n.notation;
  c.pitch = mel_pitch_copy(n.pitch);
  c.symbol = n.symbol ? _strdup(n.symbol) : NULL;
  c.nat_bi_index = n.nat_bi_index;
  c.acc_count = n.acc_count;
  if (n.acc_vector && n.acc_count > 0)
  {
    c.acc_vector = malloc(n.acc_count * sizeof(int32_t));
    memcpy(c.acc_vector, n.acc_vector, n.acc_count * sizeof(int32_t));
  }
  else
  {
    c.acc_vector = NULL;
  }
  return c;
}

Mel_Pitch mel_note_pitch(Mel_Note n)
{
  return mel_pitch_copy(n.pitch);
}

int32_t mel_note_bi_index(Mel_Note n)
{
  return n.nat_bi_index;
}

int32_t mel_note_acc_value(Mel_Note n)
{
  int32_t sum = 0;
  for (int32_t i = 0; i < n.acc_count; i++)
    sum += n.acc_vector[i];
  return sum;
}

Mel_Note mel_note_transpose_bi(Mel_Note n, int32_t bi_diff)
{
  Mel_Note r = mel_note_copy(n);
  r.nat_bi_index += bi_diff;
  r.pitch = mel_pitch_transpose_bi(r.pitch, bi_diff);
  return r;
}

uint8_t mel_note_eq(Mel_Note a, Mel_Note b)
{
  if (a.notation != b.notation) return 0;
  if (!a.symbol || !b.symbol) return mel_pitch_eq(a.pitch, b.pitch);
  return strcmp(a.symbol, b.symbol) == 0 && a.nat_bi_index == b.nat_bi_index;
}

Mel_NoteInterval mel_note_interval_copy(Mel_NoteInterval ni)
{
  Mel_NoteInterval c;
  c.notation = ni.notation;
  c.pitch_interval = mel_interval_copy(ni.pitch_interval);
  c.nat_diff = ni.nat_diff;
  c.acc_count = ni.acc_count;
  if (ni.acc_vector && ni.acc_count > 0)
  {
    c.acc_vector = malloc(ni.acc_count * sizeof(int32_t));
    memcpy(c.acc_vector, ni.acc_vector, ni.acc_count * sizeof(int32_t));
  }
  else
  {
    c.acc_vector = NULL;
  }
  c.symbol = ni.symbol ? _strdup(ni.symbol) : NULL;
  c.number = ni.number;
  return c;
}

int32_t mel_note_interval_acc_value(Mel_NoteInterval ni)
{
  int32_t sum = 0;
  for (int32_t i = 0; i < ni.acc_count; i++)
    sum += ni.acc_vector[i];
  return sum;
}
