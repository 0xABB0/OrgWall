#include <notation/nat_acc.h>

#include <assert.h>

static i64 mel_nat_acc__floor_div(i64 a, i64 b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

static i64 mel_nat_acc__floor_mod(i64 a, i64 b) { return a - mel_nat_acc__floor_div(a, b) * b; }

void mel_nat_acc_notation_free(Mel_NatAccNotation* nn)
{
    if (!nn)
        return;
    mel_notation_free(&nn->base);
    for (usize i = 0; i < nn->naturals.count; i++)
        mel_dealloc(nn->alloc, nn->naturals.items[i].symbol.data);
    mel_array_free(&nn->naturals);
    mel_symbol_code_free(&nn->acc_code);
    for (usize i = 0; i < nn->interval_codes.count; i++)
        mel_symbol_code_free(&nn->interval_codes.items[i].code);
    mel_array_free(&nn->interval_codes);
}

Mel_NatAccNotation mel_nat_acc_notation_make(const Mel_Alloc* alloc, const Mel_Tuning* tuning)
{
    assert(alloc);
    assert(mel_tuning_is_periodic(tuning));

    Mel_NatAccNotation nn;
    nn.base = mel_notation_make(tuning);
    nn.alloc = alloc;
    mel_array_init(&nn.naturals, alloc);
    nn.acc_code = mel_symbol_code_make(alloc);
    mel_array_init(&nn.interval_codes, alloc);
    return nn;
}

void mel_nat_acc_add_natural(Mel_NatAccNotation* nn, str8 symbol, i32 pitch_index)
{
    assert(symbol.len > 0);
    Mel_NatEntry e;
    e.symbol = str8_dup_alloc(symbol, nn->alloc);
    e.pitch_index = pitch_index;
    mel_array_push(&nn->naturals, e);
}

void mel_nat_acc_add_accidental(Mel_NatAccNotation* nn, str8 symbol, i32 value) { mel_symbol_code_add(&nn->acc_code, symbol, value); }

void mel_nat_acc_add_accidental_at(Mel_NatAccNotation* nn, str8 symbol, i32 value, i32 position) { mel_symbol_code_add_at(&nn->acc_code, symbol, value, position); }

void mel_nat_acc_set_interval_code(Mel_NatAccNotation* nn, i32 nat_diff_class, Mel_SymbolCode code, i32 reference_steps)
{
    assert(nat_diff_class >= 0);
    while ((usize)nat_diff_class >= nn->interval_codes.count)
        mel_array_push(&nn->interval_codes, ((Mel_IntervalCode){ .code = mel_symbol_code_make(nn->alloc), .reference_steps = 0 }));
    mel_symbol_code_free(&nn->interval_codes.items[nat_diff_class].code);
    nn->interval_codes.items[nat_diff_class] = (Mel_IntervalCode){ .code = code, .reference_steps = reference_steps };
}

static i64 mel_nat_acc__nat_pitch(const Mel_NatAccNotation* nn, i64 nat_flat)
{
    i64 natc = (i64)nn->naturals.count;
    i64 cls = mel_nat_acc__floor_mod(nat_flat, natc);
    i64 bi = mel_nat_acc__floor_div(nat_flat, natc);
    return nn->naturals.items[cls].pitch_index + bi * (i64)nn->base.tuning->period;
}

bool mel_nat_acc_note(Mel_Note* out, const Mel_NatAccNotation* nn, str8 symbol, i32 nat_bi_index)
{
    size best_len = 0;
    i32  best_class = -1;
    for (usize i = 0; i < nn->naturals.count; i++)
    {
        str8 nat = nn->naturals.items[i].symbol;
        if (nat.len > best_len && str8_starts_with(symbol, nat))
        {
            best_len = nat.len;
            best_class = (i32)i;
        }
    }
    if (best_class < 0)
        return false;

    i32 acc_value = 0;
    if (!mel_symbol_code_parse(&nn->acc_code, str8_suffix(symbol, symbol.len - best_len), &acc_value))
        return false;

    *out = mel_nat_acc_note_by_class(nn, best_class, nat_bi_index, acc_value);
    return true;
}

Mel_Note mel_nat_acc_note_by_class(const Mel_NatAccNotation* nn, i32 nat_class, i32 nat_bi_index, i32 acc_value)
{
    assert(nat_class >= 0 && (usize)nat_class < nn->naturals.count);

    i64 period = (i64)nn->base.tuning->period;
    i64 pitch_index = nn->naturals.items[nat_class].pitch_index + (i64)nat_bi_index * period + acc_value;

    Mel_Note note;
    note.pitch = mel_pitch_make(nn->base.tuning, pitch_index);
    note.nat_class = nat_class;
    note.nat_bi_index = nat_bi_index;
    note.acc_value = acc_value;
    return note;
}

Mel_NoteInterval mel_nat_acc_interval(const Mel_NatAccNotation* nn, Mel_Note source, Mel_Note target)
{
    i64 natc = (i64)nn->naturals.count;
    i64 period = (i64)nn->base.tuning->period;
    i64 src_flat = source.nat_class + (i64)source.nat_bi_index * natc;
    i64 tgt_flat = target.nat_class + (i64)target.nat_bi_index * natc;
    i64 nat_diff = tgt_flat - src_flat;

    i64 pitch_diff = target.pitch.index - source.pitch.index;
    i64 abs_nat = nat_diff >= 0 ? nat_diff : -nat_diff;
    i64 abs_pitch = nat_diff >= 0 ? pitch_diff : -pitch_diff;

    i64 cls = abs_nat % natc;
    i64 reference = period * (abs_nat / natc);
    if ((usize)cls < nn->interval_codes.count)
        reference += nn->interval_codes.items[cls].reference_steps;

    Mel_NoteInterval ni;
    ni.pitch_interval = mel_interval_from_pitches(source.pitch, target.pitch);
    ni.nat_diff = (i32)nat_diff;
    ni.number = (i32)(nat_diff >= 0 ? nat_diff + 1 : nat_diff - 1);
    ni.quality_value = (i32)(abs_pitch - reference);
    return ni;
}

str8 mel_nat_acc_note_symbol(const Mel_NatAccNotation* nn, Mel_Note note, const Mel_Alloc* alloc)
{
    assert(note.nat_class >= 0 && (usize)note.nat_class < nn->naturals.count);

    str8 acc;
    if (!mel_symbol_code_generate(&nn->acc_code, note.acc_value, alloc, &acc))
        return STR8_EMPTY;

    str8 nat = nn->naturals.items[note.nat_class].symbol;
    str8 symbol = str8_fmt_alloc(alloc, "%.*s%.*s", (int)nat.len, (const char*)nat.data, (int)acc.len, (const char*)acc.data);
    if (acc.data)
        mel_dealloc(alloc, acc.data);
    return symbol;
}

str8 mel_nat_acc_interval_symbol(const Mel_NatAccNotation* nn, Mel_NoteInterval ni, const Mel_Alloc* alloc)
{
    i64 natc = (i64)nn->naturals.count;
    i64 cls = mel_nat_acc__floor_mod(ni.nat_diff >= 0 ? ni.nat_diff : -ni.nat_diff, natc);
    if ((usize)cls >= nn->interval_codes.count)
        return STR8_EMPTY;

    str8 quality_str;
    if (!mel_symbol_code_generate(&nn->interval_codes.items[cls].code, ni.quality_value, alloc, &quality_str))
        return STR8_EMPTY;

    i32  number = ni.number >= 0 ? ni.number : -ni.number;
    str8 symbol = str8_fmt_alloc(alloc, "%s%.*s%d", ni.nat_diff < 0 ? "-" : "", (int)quality_str.len, (const char*)quality_str.data, number);
    if (quality_str.data)
        mel_dealloc(alloc, quality_str.data);
    return symbol;
}
