#include <notation/enharmonic.h>
#include <notation/notation.h>

#include <assert.h>

typedef struct
{
    i32      count;
    Mel_Note notes[];
} Mel_PcBlueprint_Ctx;

static void mel_pc_blueprint__destroy(void* ctx, const Mel_Alloc* alloc) { mel_dealloc(alloc, ctx); }

static Mel_Note mel_pc_blueprint__guess(void* ctx, const Mel_Notation* n, Mel_Pitch pitch)
{
    Mel_PcBlueprint_Ctx* c = ctx;
    MEL_UNUSED(n);

    i64 pc = mel_pitch_pc_index(pitch);
    i64 bi = mel_pitch_bi_index(pitch);
    assert(pc >= 0 && pc < (i64)c->count);

    Mel_Note blueprint = c->notes[pc];
    return mel_note_transpose_bi(blueprint, (i32)(bi - mel_pitch_bi_index(blueprint.pitch)));
}

static Mel_Note mel_pc_blueprint__transpose(void* ctx, Mel_Note note, i64 pitch_diff) { return mel_pc_blueprint__guess(ctx, NULL, mel_pitch_transpose(note.pitch, pitch_diff)); }

Mel_EnharmonicStrategy mel_enharmonic_pc_blueprint(const Mel_Alloc* alloc, const Mel_Note* blueprint, i32 count)
{
    assert(alloc && blueprint && count > 0);

    Mel_PcBlueprint_Ctx* ctx = mel_alloc(alloc, sizeof(Mel_PcBlueprint_Ctx) + (usize)count * sizeof(Mel_Note));
    ctx->count = count;
    for (i32 i = 0; i < count; i++)
        ctx->notes[i] = blueprint[i];

    Mel_EnharmonicStrategy s;
    s.ctx = ctx;
    s.alloc = alloc;
    s.guess_note = mel_pc_blueprint__guess;
    s.note_transpose = mel_pc_blueprint__transpose;
    s.destroy = mel_pc_blueprint__destroy;
    return s;
}
