#include <core/types.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>
#include <string/str8.h>

#include <clang/clang.h>
#include <repl/repl.h>

#include <stdio.h>

typedef struct
{
    FILE*         in;
    Mel_Array(u8) line;
} Stdin_Source;

static bool stdin_read(void* self, str8* out)
{
    Stdin_Source* s = (Stdin_Source*)self;
    mel_array_clear(&s->line);

    int c = fgetc(s->in);
    if (c == EOF)
        return false;
    while (c != EOF && c != '\n')
    {
        mel_array_push(&s->line, (u8)c);
        c = fgetc(s->in);
    }
    *out = str8_from_parts(s->line.items, (size)s->line.count);
    return true;
}

static void stdout_write(void* self, str8 bytes)
{
    (void)self;
    fwrite(bytes.data, 1, (size_t)bytes.len, stdout);
    fflush(stdout);
}

int main(void)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Repl_Lang lang = mel_clang_repl_lang(a);
    if (!lang.eval)
    {
        fprintf(stderr, "repl: failed to create the C interpreter\n");
        return 1;
    }

    Mel_Repl* repl = mel_repl_create(a, lang);
    if (!repl)
    {
        fprintf(stderr, "repl: failed to create the REPL\n");
        if (lang.destroy)
            lang.destroy(lang.self);
        return 1;
    }

    Stdin_Source src = { 0 };
    src.in = stdin;
    mel_array_init(&src.line, a);

    Mel_Repl_Source  source = { &src, stdin_read, NULL };
    Mel_Repl_Sink    sink = { NULL, stdout_write, NULL };
    Mel_Repl_Prompts prompts = { S8("» "), S8("… ") };

    fputs("melody C repl — LLVM ORC JIT. Enter C expressions or declarations; Ctrl-D to exit.\n", stdout);
    fflush(stdout);

    mel_repl_run(repl, source, sink, prompts);

    mel_array_free(&src.line);
    mel_repl_destroy(repl);
    return 0;
}
