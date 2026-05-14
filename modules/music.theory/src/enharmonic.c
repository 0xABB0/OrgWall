#include <music.theory/enharmonic.h>
#include <music.theory/notation.h>
#include <music.theory/note.h>
#include <music.theory/tuning.h>
#include <music.theory/pitch.h>
#include <stdlib.h>

typedef struct
{
  Mel_Note* notes;
  int32_t count;
} PcBlueprintCtx;

static Mel_Note pc_blueprint_guess_note(void* ctx, Mel_Notation* n, Mel_Pitch pitch)
{
  PcBlueprintCtx* c = (PcBlueprintCtx*)ctx;
  (void)n;

  int64_t pc = mel_pitch_pc_index(pitch);
  int64_t bi = mel_pitch_bi_index(pitch);

  if (pc < 0 || pc >= c->count) return c->notes[0];

  Mel_Note blueprint_note = c->notes[pc];
  return mel_note_transpose_bi(blueprint_note, bi - mel_note_bi_index(blueprint_note));
}

static Mel_Note pc_blueprint_note_transpose(void* ctx, Mel_Note note, int64_t pitch_diff)
{
  PcBlueprintCtx* c = (PcBlueprintCtx*)ctx;

  Mel_Pitch pitch = mel_note_pitch(note);
  Mel_Pitch transposed = mel_pitch_transpose(pitch, pitch_diff);

  int64_t pc = mel_pitch_pc_index(transposed);
  int64_t bi = mel_pitch_bi_index(transposed);

  Mel_Note result;
  if (pc >= 0 && pc < c->count)
    result = mel_note_transpose_bi(c->notes[pc], bi - mel_note_bi_index(c->notes[pc]));
  else
    result = note;

  return result;
}

Mel_EnharmonicStrategy mel_enharmonic_pc_blueprint(Mel_Note* blueprint, int32_t count)
{
  PcBlueprintCtx* ctx = malloc(sizeof(PcBlueprintCtx));
  ctx->notes = malloc(count * sizeof(Mel_Note));
  ctx->count = count;
  for (int32_t i = 0; i < count; i++)
    ctx->notes[i] = mel_note_copy(blueprint[i]);

  Mel_EnharmonicStrategy s;
  s.ctx = ctx;
  s.guess_note = pc_blueprint_guess_note;
  s.note_transpose = pc_blueprint_note_transpose;
  s.note_scale_transpose = NULL;
  s.note_scale_complement = NULL;
  return s;
}
