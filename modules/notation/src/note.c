#include <notation/note.h>

Mel_Note mel_note_transpose_bi(Mel_Note n, i32 bi_diff)
{
    Mel_Note r = n;
    r.nat_bi_index += bi_diff;
    r.pitch = mel_pitch_transpose_bi(n.pitch, bi_diff);
    return r;
}

u8 mel_note_eq(Mel_Note a, Mel_Note b) { return a.nat_class == b.nat_class && a.nat_bi_index == b.nat_bi_index && a.acc_value == b.acc_value && mel_pitch_eq(a.pitch, b.pitch) ? 1 : 0; }
