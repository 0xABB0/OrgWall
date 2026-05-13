#include "note_scale.h"
#include <stdlib.h>

void mel_note_scale_free(Mel_NoteScale* ns)
{
  if (!ns) return;
  mel_scale_free(&ns->pitch_scale);
}

Mel_NoteScale mel_note_scale_make(const Mel_Notation* n, Mel_Note* notes, int32_t count)
{
  Mel_NoteScale ns;
  ns.notation = n;
  ns.pitch_scale = mel_scale_make(n->tuning);
  for (int32_t i = 0; i < count; i++)
    mel_scale_add_pitch(&ns.pitch_scale, notes[i].pitch);
  return ns;
}

int32_t mel_note_scale_count(const Mel_NoteScale* ns)
{
  return ns->pitch_scale.count;
}

Mel_Note mel_note_scale_get(const Mel_NoteScale* ns, int32_t idx)
{
  Mel_Note note;
  note.notation = ns->notation;
  note.pitch = mel_scale_get(&ns->pitch_scale, idx);
  note.symbol = NULL;
  note.nat_bi_index = 0;
  note.acc_vector = NULL;
  note.acc_count = 0;

  if (ns->notation->enharmonic.guess_note)
  {
    Mel_Note guessed = ns->notation->enharmonic.guess_note(
      ns->notation->enharmonic.ctx,
      (Mel_Notation*)ns->notation,
      note.pitch
    );
    return guessed;
  }

  return note;
}

Mel_NoteScale mel_note_scale_copy(const Mel_NoteScale* ns)
{
  Mel_NoteScale c;
  c.notation = ns->notation;
  c.pitch_scale = mel_scale_copy(&ns->pitch_scale);
  return c;
}

Mel_NoteScale mel_note_scale_transpose(const Mel_NoteScale* ns, int64_t diff)
{
  Mel_NoteScale r;
  r.notation = ns->notation;
  r.pitch_scale = mel_scale_transpose(&ns->pitch_scale, diff);
  return r;
}
