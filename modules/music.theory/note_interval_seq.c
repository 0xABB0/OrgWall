#include "note_interval_seq.h"
#include <stdlib.h>

void mel_note_interval_seq_free(Mel_NoteIntervalSeq* nis)
{
  if (!nis) return;
  mel_interval_seq_free(&nis->interval_seq);
}

Mel_NoteIntervalSeq mel_note_interval_seq_make(const Mel_Notation* n, Mel_NoteInterval* intervals, int32_t count)
{
  Mel_NoteIntervalSeq nis;
  nis.notation = n;
  nis.interval_seq = mel_interval_seq_make(n->tuning);
  for (int32_t i = 0; i < count; i++)
    mel_interval_seq_add(&nis.interval_seq, intervals[i].pitch_interval);
  return nis;
}
