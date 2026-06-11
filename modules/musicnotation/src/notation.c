#include <musicnotation/notation.h>

#include <assert.h>

void mel_notation_free(Mel_Notation* n)
{
    if (!n)
        return;
    if (n->enharmonic.ctx && n->enharmonic.destroy)
        n->enharmonic.destroy(n->enharmonic.ctx, n->enharmonic.alloc);
    n->enharmonic = (Mel_EnharmonicStrategy){ 0 };
}

Mel_Notation mel_notation_make(const Mel_Tuning* tuning)
{
    assert(tuning);
    Mel_Notation n;
    n.tuning = tuning;
    n.enharmonic = (Mel_EnharmonicStrategy){ 0 };
    return n;
}

void mel_notation_set_enharmonic(Mel_Notation* n, Mel_EnharmonicStrategy strategy)
{
    if (n->enharmonic.ctx && n->enharmonic.destroy)
        n->enharmonic.destroy(n->enharmonic.ctx, n->enharmonic.alloc);
    n->enharmonic = strategy;
}

Mel_Note mel_notation_guess_note(const Mel_Notation* n, Mel_Pitch pitch)
{
    assert(n->enharmonic.guess_note);
    return n->enharmonic.guess_note(n->enharmonic.ctx, n, pitch);
}

Mel_Note mel_notation_note_transpose(const Mel_Notation* n, Mel_Note note, i64 pitch_diff)
{
    assert(n->enharmonic.note_transpose);
    return n->enharmonic.note_transpose(n->enharmonic.ctx, note, pitch_diff);
}
