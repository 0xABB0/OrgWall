#include "notation.h"
#include "note.h"
#include "note_scale.h"
#include "note_interval_seq.h"
#include <stdlib.h>

void mel_notation_free(Mel_Notation* n)
{
  if (!n) return;
  if (n->enharmonic.ctx) free(n->enharmonic.ctx);
}

Mel_Notation mel_notation_make(const Mel_Tuning* tuning)
{
  Mel_Notation n;
  n.tuning = tuning;
  n.enharmonic.ctx = NULL;
  n.enharmonic.guess_note = NULL;
  n.enharmonic.note_transpose = NULL;
  n.enharmonic.note_scale_transpose = NULL;
  n.enharmonic.note_scale_complement = NULL;
  return n;
}

void mel_notation_set_enharmonic(Mel_Notation* n, Mel_EnharmonicStrategy strategy)
{
  n->enharmonic = strategy;
}

Mel_Note mel_notation_guess_note(const Mel_Notation* n, Mel_Pitch pitch)
{
  if (n->enharmonic.guess_note)
    return n->enharmonic.guess_note(n->enharmonic.ctx, (Mel_Notation*)n, pitch);

  Mel_Note note;
  note.notation = n;
  note.pitch = pitch;
  note.symbol = NULL;
  note.nat_bi_index = 0;
  note.acc_vector = NULL;
  note.acc_count = 0;
  return note;
}

Mel_NoteInterval mel_notation_interval(const Mel_Notation* n, Mel_Note source, Mel_Note target)
{
  Mel_NoteInterval ni;
  ni.notation = n;
  ni.pitch_interval = mel_interval_from_pitches(source.pitch, target.pitch);
  ni.nat_diff = 0;
  ni.acc_vector = NULL;
  ni.acc_count = 0;
  ni.symbol = NULL;
  ni.number = 0;
  return ni;
}

Mel_NoteScale mel_notation_scale(const Mel_Notation* n, Mel_Note* notes, int32_t count)
{
  return mel_note_scale_make(n, notes, count);
}

Mel_NoteIntervalSeq mel_notation_interval_seq(const Mel_Notation* n, Mel_NoteInterval* intervals, int32_t count)
{
  return mel_note_interval_seq_make(n, intervals, count);
}
