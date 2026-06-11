#include <musicnotation/symbol.h>

#include <assert.h>
#include <stdlib.h>

typedef Mel_Array(char) Mel_Symbol_Buf;

void mel_symbol_code_free(Mel_SymbolCode* sc)
{
    if (!sc)
        return;
    for (usize i = 0; i < sc->entries.count; i++)
        mel_dealloc(sc->entries.allocator, sc->entries.items[i].symbol.data);
    mel_array_free(&sc->entries);
}

Mel_SymbolCode mel_symbol_code_make(const Mel_Alloc* alloc)
{
    assert(alloc);
    Mel_SymbolCode sc;
    mel_array_init(&sc.entries, alloc);
    return sc;
}

void mel_symbol_code_add(Mel_SymbolCode* sc, str8 symbol, i32 value) { mel_symbol_code_add_at(sc, symbol, value, (i32)sc->entries.count); }

void mel_symbol_code_add_at(Mel_SymbolCode* sc, str8 symbol, i32 value, i32 position)
{
    assert(symbol.len > 0);
    Mel_SymbolCodeEntry e;
    e.symbol = str8_dup_alloc(symbol, sc->entries.allocator);
    e.value = value;
    e.position = position;
    mel_array_push(&sc->entries, e);
}

bool mel_symbol_code_parse(const Mel_SymbolCode* sc, str8 text, i32* out_value)
{
    i32 total = 0;
    while (text.len > 0)
    {
        size best_len = 0;
        i32  best_value = 0;
        for (usize i = 0; i < sc->entries.count; i++)
        {
            str8 sym = sc->entries.items[i].symbol;
            if (sym.len > best_len && str8_starts_with(text, sym))
            {
                best_len = sym.len;
                best_value = sc->entries.items[i].value;
            }
        }
        if (best_len == 0)
            return false;
        total += best_value;
        text = str8_suffix(text, text.len - best_len);
    }
    *out_value = total;
    return true;
}

static int mel_symbol_code__entry_cmp(const void* a, const void* b)
{
    const Mel_SymbolCodeEntry* ea = a;
    const Mel_SymbolCodeEntry* eb = b;
    if (ea->position != eb->position)
        return ea->position - eb->position;
    i32 abs_a = ea->value >= 0 ? ea->value : -ea->value;
    i32 abs_b = eb->value >= 0 ? eb->value : -eb->value;
    return abs_b - abs_a;
}

bool mel_symbol_code_generate(const Mel_SymbolCode* sc, i32 value, const Mel_Alloc* alloc, str8* out)
{
    if (value == 0)
    {
        const Mel_SymbolCodeEntry* zero = NULL;
        for (usize i = 0; i < sc->entries.count; i++)
            if (sc->entries.items[i].value == 0 && (!zero || sc->entries.items[i].position < zero->position))
                zero = &sc->entries.items[i];
        *out = zero ? str8_dup_alloc(zero->symbol, alloc) : STR8_EMPTY;
        return true;
    }
    if (sc->entries.count == 0)
        return false;

    usize                sorted_size = sc->entries.count * sizeof(Mel_SymbolCodeEntry);
    Mel_SymbolCodeEntry* sorted = mel_alloc(alloc, sorted_size);
    for (usize i = 0; i < sc->entries.count; i++)
        sorted[i] = sc->entries.items[i];
    qsort(sorted, sc->entries.count, sizeof(Mel_SymbolCodeEntry), mel_symbol_code__entry_cmp);

    Mel_Symbol_Buf buf;
    mel_array_init(&buf, alloc);

    i32 remaining = value;
    for (usize i = 0; i < sc->entries.count && remaining != 0; i++)
    {
        i32 v = sorted[i].value;
        while ((remaining > 0 && v > 0 && remaining >= v) || (remaining < 0 && v < 0 && remaining <= v))
        {
            for (size k = 0; k < sorted[i].symbol.len; k++)
                mel_array_push(&buf, (char)sorted[i].symbol.data[k]);
            remaining -= v;
        }
    }

    mel_dealloc(alloc, sorted);

    if (remaining != 0)
    {
        mel_array_free(&buf);
        return false;
    }

    *out = str8_from_parts((u8*)buf.items, (size)buf.count);
    return true;
}
