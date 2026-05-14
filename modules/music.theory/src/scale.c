#include <music.theory/scale.h>
#include <music.theory/interval_seq.h>
#include <music.theory/pitch.h>
#include <stdlib.h>
#include <string.h>

#define MEL_SCALE_INITIAL_CAPACITY 16

void mel_scale_free(Mel_Scale* s)
{
  if (!s) return;
  free(s->pitches);
  s->pitches = NULL;
  s->count = 0;
  s->capacity = 0;
}

Mel_Scale mel_scale_make(const Mel_Tuning* tuning)
{
  Mel_Scale s;
  s.tuning = tuning;
  s.count = 0;
  s.capacity = MEL_SCALE_INITIAL_CAPACITY;
  s.pitches = malloc(s.capacity * sizeof(Mel_Pitch));
  return s;
}

Mel_Scale mel_scale_from_indices(const Mel_Tuning* tuning, const int64_t* indices, int32_t count)
{
  Mel_Scale s = mel_scale_make(tuning);
  for (int32_t i = 0; i < count; i++)
    mel_scale_add_index(&s, indices[i]);
  return s;
}

Mel_Scale mel_scale_from_pitches(const Mel_Tuning* tuning, const Mel_Pitch* pitches, int32_t count)
{
  Mel_Scale s = mel_scale_make(tuning);
  for (int32_t i = 0; i < count; i++)
    mel_scale_add_pitch(&s, pitches[i]);
  return s;
}

Mel_Scale mel_scale_copy(const Mel_Scale* s)
{
  Mel_Scale c;
  c.tuning = s->tuning;
  c.count = s->count;
  c.capacity = s->capacity;
  c.pitches = malloc(c.capacity * sizeof(Mel_Pitch));
  for (int32_t i = 0; i < s->count; i++)
    c.pitches[i] = mel_pitch_copy(s->pitches[i]);
  return c;
}

static int32_t mel_scale_find_insert_pos(const Mel_Scale* s, Mel_Pitch pitch)
{
  int32_t lo = 0, hi = s->count;
  while (lo < hi)
  {
    int32_t mid = lo + (hi - lo) / 2;
    uint8_t cmp = mel_pitch_cmp(s->pitches[mid], pitch);
    if (cmp == 1) return mid;
    if (cmp == 0) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

static void mel_scale_grow(Mel_Scale* s)
{
  s->capacity *= 2;
  s->pitches = realloc(s->pitches, s->capacity * sizeof(Mel_Pitch));
}

void mel_scale_add_pitch(Mel_Scale* s, Mel_Pitch pitch)
{
  if (pitch.tuning != s->tuning) return;

  int32_t pos = mel_scale_find_insert_pos(s, pitch);
  if (pos < s->count && mel_pitch_cmp(s->pitches[pos], pitch) == 1) return;

  if (s->count >= s->capacity) mel_scale_grow(s);

  for (int32_t i = s->count; i > pos; i--)
    s->pitches[i] = s->pitches[i - 1];

  s->pitches[pos] = mel_pitch_copy(pitch);
  s->count++;
}

void mel_scale_add_index(Mel_Scale* s, int64_t index)
{
  Mel_Pitch p = mel_pitch_make(s->tuning, index);
  mel_scale_add_pitch(s, p);
}

Mel_Pitch mel_scale_get(const Mel_Scale* s, int32_t idx)
{
  return mel_pitch_copy(s->pitches[idx]);
}

uint8_t mel_scale_contains_pitch(const Mel_Scale* s, Mel_Pitch pitch)
{
  if (pitch.tuning != s->tuning) return 0;
  int32_t lo = 0, hi = s->count;
  while (lo < hi)
  {
    int32_t mid = lo + (hi - lo) / 2;
    uint8_t cmp = mel_pitch_cmp(s->pitches[mid], pitch);
    if (cmp == 1) return 1;
    if (cmp == 0) lo = mid + 1;
    else hi = mid;
  }
  return 0;
}

uint8_t mel_scale_contains_index(const Mel_Scale* s, int64_t index)
{
  Mel_Pitch p = mel_pitch_make(s->tuning, index);
  return mel_scale_contains_pitch(s, p);
}

Mel_Interval mel_scale_spec_interval(const Mel_Scale* s, int32_t source_idx, int32_t target_idx)
{
  return mel_interval_from_pitches(s->pitches[source_idx], s->pitches[target_idx]);
}

Mel_Scale mel_scale_transpose(const Mel_Scale* s, int64_t diff)
{
  Mel_Scale r = mel_scale_make(s->tuning);
  for (int32_t i = 0; i < s->count; i++)
    mel_scale_add_index(&r, s->pitches[i].pitch_index + diff);
  return r;
}

Mel_Scale mel_scale_union(const Mel_Scale* a, const Mel_Scale* b)
{
  Mel_Scale r = mel_scale_make(a->tuning);
  int32_t ai = 0, bi = 0;
  while (ai < a->count && bi < b->count)
  {
    uint8_t cmp = mel_pitch_cmp(a->pitches[ai], b->pitches[bi]);
    if (cmp == 1)
    {
      mel_scale_add_pitch(&r, a->pitches[ai]);
      ai++;
      bi++;
    }
    else if (cmp == 0)
    {
      mel_scale_add_pitch(&r, a->pitches[ai]);
      ai++;
    }
    else
    {
      mel_scale_add_pitch(&r, b->pitches[bi]);
      bi++;
    }
  }
  for (; ai < a->count; ai++) mel_scale_add_pitch(&r, a->pitches[ai]);
  for (; bi < b->count; bi++) mel_scale_add_pitch(&r, b->pitches[bi]);
  return r;
}

Mel_Scale mel_scale_intersection(const Mel_Scale* a, const Mel_Scale* b)
{
  Mel_Scale r = mel_scale_make(a->tuning);
  int32_t ai = 0, bi = 0;
  while (ai < a->count && bi < b->count)
  {
    uint8_t cmp = mel_pitch_cmp(a->pitches[ai], b->pitches[bi]);
    if (cmp == 1)
    {
      mel_scale_add_pitch(&r, a->pitches[ai]);
      ai++;
      bi++;
    }
    else if (cmp == 0)
    {
      ai++;
    }
    else
    {
      bi++;
    }
  }
  return r;
}

Mel_Scale mel_scale_difference(const Mel_Scale* a, const Mel_Scale* b)
{
  Mel_Scale r = mel_scale_make(a->tuning);
  int32_t ai = 0, bi = 0;
  while (ai < a->count && bi < b->count)
  {
    uint8_t cmp = mel_pitch_cmp(a->pitches[ai], b->pitches[bi]);
    if (cmp == 1)
    {
      ai++;
      bi++;
    }
    else if (cmp == 0)
    {
      mel_scale_add_pitch(&r, a->pitches[ai]);
      ai++;
    }
    else
    {
      bi++;
    }
  }
  for (; ai < a->count; ai++) mel_scale_add_pitch(&r, a->pitches[ai]);
  return r;
}

uint8_t mel_scale_is_subset(const Mel_Scale* a, const Mel_Scale* b)
{
  if (a->count > b->count) return 0;
  int32_t bi = 0;
  for (int32_t ai = 0; ai < a->count; ai++)
  {
    while (bi < b->count && mel_pitch_cmp(a->pitches[ai], b->pitches[bi]) == 2) bi++;
    if (bi >= b->count || mel_pitch_cmp(a->pitches[ai], b->pitches[bi]) != 1) return 0;
    bi++;
  }
  return 1;
}

uint8_t mel_scale_eq(const Mel_Scale* a, const Mel_Scale* b)
{
  if (a->count != b->count) return 0;
  for (int32_t i = 0; i < a->count; i++)
    if (!mel_pitch_eq(a->pitches[i], b->pitches[i]))
      return 0;
  return 1;
}

Mel_Scale mel_scale_reflect(const Mel_Scale* s, Mel_Pitch axis)
{
  Mel_Scale r = mel_scale_make(s->tuning);
  for (int32_t i = 0; i < s->count; i++)
  {
    Mel_Interval iv = mel_interval_from_pitches(s->pitches[i], axis);
    int64_t reflected_idx = axis.pitch_index + iv.pitch_diff;
    mel_scale_add_index(&r, reflected_idx);
    mel_interval_free(&iv);
  }
  return r;
}

Mel_Scale mel_scale_zero_normalized(const Mel_Scale* s)
{
  if (s->count == 0) return mel_scale_make(s->tuning);

  Mel_Pitch root = s->pitches[0];
  if (root.pitch_index == 0) return mel_scale_copy(s);

  return mel_scale_transpose(s, -root.pitch_index);
}

void mel_scale_indices(const Mel_Scale* s, int64_t* out_indices)
{
  for (int32_t i = 0; i < s->count; i++)
    out_indices[i] = s->pitches[i].pitch_index;
}

Mel_Scale mel_scale_rotated_up(const Mel_Scale* s)
{
  if (s->count == 0) return mel_scale_make(s->tuning);

  Mel_Pitch first = s->pitches[0];
  Mel_Pitch last = s->pitches[s->count - 1];

  int64_t bi_diff = mel_pitch_bi_index(last) - mel_pitch_bi_index(first);
  Mel_Pitch new_first = mel_pitch_transpose_bi(first, bi_diff);
  if (mel_pitch_cmp(new_first, last) != 2)
    new_first = mel_pitch_transpose_bi(new_first, 1);

  Mel_Scale r = mel_scale_make(s->tuning);
  for (int32_t i = 1; i < s->count; i++)
    mel_scale_add_pitch(&r, s->pitches[i]);
  mel_scale_add_pitch(&r, new_first);

  return r;
}

Mel_Scale mel_scale_rotated_down(const Mel_Scale* s)
{
  if (s->count == 0) return mel_scale_make(s->tuning);

  Mel_Pitch first = s->pitches[0];
  Mel_Pitch last = s->pitches[s->count - 1];

  int64_t bi_diff = mel_pitch_bi_index(first) - mel_pitch_bi_index(last);
  Mel_Pitch new_last = mel_pitch_transpose_bi(last, bi_diff);
  if (mel_pitch_cmp(new_last, first) != 0)
    new_last = mel_pitch_transpose_bi(new_last, -1);

  Mel_Scale r = mel_scale_make(s->tuning);
  mel_scale_add_pitch(&r, new_last);
  for (int32_t i = 0; i < s->count - 1; i++)
    mel_scale_add_pitch(&r, s->pitches[i]);

  return r;
}

Mel_Scale mel_scale_rotation(const Mel_Scale* s, int32_t order)
{
  Mel_Scale r = mel_scale_copy(s);
  if (order > 0)
  {
    for (int32_t i = 0; i < order; i++)
    {
      Mel_Scale tmp = mel_scale_rotated_up(&r);
      mel_scale_free(&r);
      r = tmp;
    }
  }
  else if (order < 0)
  {
    for (int32_t i = 0; i < -order; i++)
    {
      Mel_Scale tmp = mel_scale_rotated_down(&r);
      mel_scale_free(&r);
      r = tmp;
    }
  }
  return r;
}

Mel_Scale mel_scale_pcs_normalized(const Mel_Scale* s)
{
  Mel_Scale r = mel_scale_make(s->tuning);
  for (int32_t i = 0; i < s->count; i++)
  {
    Mel_Pitch normalized = mel_pitch_pcs_normalized(s->pitches[i]);
    mel_scale_add_pitch(&r, normalized);
  }
  return r;
}

Mel_Scale mel_scale_period_normalized(const Mel_Scale* s)
{
  if (s->count == 0) return mel_scale_make(s->tuning);

  Mel_Pitch root = s->pitches[0];
  Mel_Scale r = mel_scale_make(s->tuning);
  mel_scale_add_pitch(&r, root);

  for (int32_t i = 1; i < s->count; i++)
  {
    Mel_Pitch elem = s->pitches[i];
    int64_t bi_diff = mel_pitch_bi_index(root) - mel_pitch_bi_index(elem);
    elem = mel_pitch_transpose_bi(elem, bi_diff);

    if (mel_pitch_cmp(elem, root) != 2)
    {
      continue;
    }
    if (mel_pitch_cmp(elem, root) == 0)
    {
      elem = mel_pitch_transpose_bi(elem, 1);
    }

    mel_scale_add_pitch(&r, elem);
  }

  return r;
}

Mel_Scale mel_scale_pcs_complement(const Mel_Scale* s)
{
  uint32_t period = mel_tuning_period_length(s->tuning);
  if (period == 0) return mel_scale_make(s->tuning);

  Mel_Scale norm = mel_scale_pcs_normalized(s);
  Mel_Scale complement = mel_scale_make(s->tuning);

  for (uint32_t i = 0; i < period; i++)
  {
    uint8_t found = 0;
    for (int32_t j = 0; j < norm.count; j++)
    {
      if (norm.pitches[j].pitch_index == (int64_t)i)
      {
        found = 1;
        break;
      }
    }
    if (!found)
      mel_scale_add_index(&complement, (int64_t)i);
  }

  mel_scale_free(&norm);
  return complement;
}

uint8_t mel_scale_is_set_equivalent(const Mel_Scale* a, const Mel_Scale* b)
{
  if (a->tuning == b->tuning)
  {
    if (a->count != b->count) return 0;
    for (int32_t i = 0; i < a->count; i++)
    {
      int64_t pc_a = mel_pitch_pc_index(a->pitches[i]);
      uint8_t found = 0;
      for (int32_t j = 0; j < b->count; j++)
      {
        if (mel_pitch_pc_index(b->pitches[j]) == pc_a)
        {
          found = 1;
          break;
        }
      }
      if (!found) return 0;
    }
    return 1;
  }

  Mel_Scale na = mel_scale_pcs_normalized(a);
  Mel_Scale nb = mel_scale_pcs_normalized(b);
  uint8_t result = mel_scale_eq(&na, &nb);
  mel_scale_free(&na);
  mel_scale_free(&nb);
  return result;
}
