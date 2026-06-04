#include <test/test.h>

#include <clang/clang.h>
#include <jit/jit.h>
#include <llvm/llvm.h>
#include <repl/repl.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <string/str8.h>

#include <string.h>

MEL_TEST(clang, compile_add_lookup_call)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Clang_Config cfg = { .opt_level = 0 };
    Mel_Jit_Result   cr = { 0 };
    Mel_Jit_Module*  m = mel_clang_compile(a, S8("answer_mod"), S8("int answer(void){return 42;}"), &cfg, &cr);
    MEL_REQUIRE_NOT_NULL(m);
    MEL_EXPECT(cr.ok);
    mel_jit_result_free(&cr);

    Mel_Llvm_Orc_Config bc = { .opt_level = 0, .expose_process_symbols = false };
    Mel_Jit*            jit = mel_jit_create(a, mel_llvm_orc_backend(a, &bc));
    MEL_REQUIRE_NOT_NULL(jit);

    Mel_Jit_Result ar = { 0 };
    Mel_Jit_Unit   unit = mel_jit_add(jit, m, &ar);
    MEL_EXPECT(ar.ok);
    MEL_EXPECT_NEQ(unit.index, 0u);
    mel_jit_result_free(&ar);

    void* sym = mel_jit_lookup(jit, "answer");
    MEL_REQUIRE_NOT_NULL(sym);
    int (*answer)(void) = (int (*)(void))sym;
    MEL_EXPECT_EQ(answer(), 42);

    mel_jit_destroy(jit);
}

MEL_TEST(clang, compile_reports_error)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Clang_Config cfg = { .opt_level = 0 };
    Mel_Jit_Result   cr = { 0 };
    Mel_Jit_Module*  m = mel_clang_compile(a, S8("bad"), S8("int answer(void){return nonsense;}"), &cfg, &cr);
    MEL_EXPECT_NULL(m);
    MEL_EXPECT(!cr.ok);
    MEL_EXPECT_NOT_NULL(cr.diagnostics);
    mel_jit_result_free(&cr);
}

MEL_TEST(clang, evaluates_c)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Repl_Lang lang = mel_clang_repl_lang(a);
    MEL_REQUIRE_NOT_NULL(lang.eval);

    Mel_Repl* repl = mel_repl_create(a, lang);
    MEL_REQUIRE_NOT_NULL(repl);

    Mel_Repl_Result r1 = mel_repl_eval(repl, S8("1+2*3"));
    MEL_EXPECT(r1.ok);
    MEL_REQUIRE_NOT_NULL(r1.text);
    MEL_EXPECT_NOT_NULL(strstr(r1.text, "7"));
    mel_repl_result_free(&r1);

    Mel_Repl_Result r2 = mel_repl_eval(repl, S8("int sq(int x){return x*x;}"));
    MEL_EXPECT(r2.ok);
    mel_repl_result_free(&r2);

    Mel_Repl_Result r3 = mel_repl_eval(repl, S8("sq(7)"));
    MEL_EXPECT(r3.ok);
    MEL_REQUIRE_NOT_NULL(r3.text);
    MEL_EXPECT_NOT_NULL(strstr(r3.text, "49"));
    mel_repl_result_free(&r3);

    mel_repl_destroy(repl);
}

MEL_TEST(clang, persists_globals_and_typed_values)
{
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_Repl_Lang lang = mel_clang_repl_lang(a);
    MEL_REQUIRE_NOT_NULL(lang.eval);
    Mel_Repl* repl = mel_repl_create(a, lang);
    MEL_REQUIRE_NOT_NULL(repl);

    Mel_Repl_Result d = mel_repl_eval(repl, S8("int counter = 10;"));
    MEL_EXPECT(d.ok);
    mel_repl_result_free(&d);

    Mel_Repl_Result m = mel_repl_eval(repl, S8("counter += 5;"));
    MEL_EXPECT(m.ok);
    mel_repl_result_free(&m);

    Mel_Repl_Result g = mel_repl_eval(repl, S8("counter"));
    MEL_EXPECT(g.ok);
    MEL_REQUIRE_NOT_NULL(g.text);
    MEL_EXPECT_NOT_NULL(strstr(g.text, "15"));
    mel_repl_result_free(&g);

    Mel_Repl_Result f = mel_repl_eval(repl, S8("3.0/2.0"));
    MEL_EXPECT(f.ok);
    MEL_REQUIRE_NOT_NULL(f.text);
    MEL_EXPECT_NOT_NULL(strstr(f.text, "1.5"));
    mel_repl_result_free(&f);

    mel_repl_destroy(repl);
}
