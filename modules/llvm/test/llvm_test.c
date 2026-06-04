#include <test/test.h>

#include <jit/jit.h>
#include <llvm/llvm.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <string/str8.h>

static Mel_Jit* mel_test_jit(const Mel_Alloc* a, bool expose_process)
{
    Mel_Llvm_Orc_Config cfg = { .opt_level = 0, .expose_process_symbols = expose_process };
    return mel_jit_create(a, mel_llvm_orc_backend(a, &cfg));
}

MEL_TEST(llvm, parse_ir_bad_reports_diagnostics)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Jit_Result   r = { 0 };
    Mel_Jit_Module*  m = mel_llvm_parse_ir(a, S8("bad"), S8("this is not llvm ir"), &r);
    MEL_EXPECT_NULL(m);
    MEL_EXPECT(!r.ok);
    MEL_EXPECT_NOT_NULL(r.diagnostics);
    MEL_EXPECT_EQ(r.alloc, a);
    mel_jit_result_free(&r);
    MEL_EXPECT_NULL(r.diagnostics);
}

MEL_TEST(llvm, executes_ir)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Jit*         jit = mel_test_jit(a, false);
    MEL_REQUIRE_NOT_NULL(jit);

    Mel_Jit_Result  r = { 0 };
    Mel_Jit_Module* m = mel_llvm_parse_ir(a, S8("answer"), S8("define i32 @answer() { ret i32 42 }"), &r);
    MEL_REQUIRE_NOT_NULL(m);
    Mel_Jit_Unit u = mel_jit_add(jit, m, &r);
    MEL_EXPECT_NEQ(u.index, 0u);

    int (*answer)(void) = (int (*)(void))mel_jit_lookup(jit, "answer");
    MEL_REQUIRE_NOT_NULL(answer);
    MEL_EXPECT_EQ(answer(), 42);

    mel_jit_destroy(jit);
}

MEL_TEST(llvm, process_symbols_isolated_when_disabled)
{
    const Mel_Alloc* a = mel_alloc_heap();
    const char*      ir = "declare i32 @getpid()\n"
                          "define i32 @uses_pid() {\n"
                          "  %p = call i32 @getpid()\n"
                          "  ret i32 %p\n"
                          "}\n";

    Mel_Jit* off = mel_test_jit(a, false);
    MEL_REQUIRE_NOT_NULL(off);
    Mel_Jit_Result  r = { 0 };
    Mel_Jit_Module* m = mel_llvm_parse_ir(a, S8("pid"), str8_from_cstr(ir), &r);
    MEL_REQUIRE_NOT_NULL(m);
    mel_jit_add(off, m, &r);
    MEL_EXPECT_NULL(mel_jit_lookup(off, "uses_pid"));
    mel_jit_destroy(off);

    Mel_Jit* on = mel_test_jit(a, true);
    MEL_REQUIRE_NOT_NULL(on);
    Mel_Jit_Module* m2 = mel_llvm_parse_ir(a, S8("pid2"), str8_from_cstr(ir), &r);
    MEL_REQUIRE_NOT_NULL(m2);
    mel_jit_add(on, m2, &r);
    int (*uses_pid)(void) = (int (*)(void))mel_jit_lookup(on, "uses_pid");
    MEL_REQUIRE_NOT_NULL(uses_pid);
    MEL_EXPECT_GT(uses_pid(), 0);
    mel_jit_destroy(on);
}

MEL_TEST(llvm, replace_keeps_handle_and_reresolves)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Jit*         jit = mel_test_jit(a, false);
    MEL_REQUIRE_NOT_NULL(jit);

    Mel_Jit_Result  r = { 0 };
    Mel_Jit_Module* v1 = mel_llvm_parse_ir(a, S8("v1"), S8("define i32 @v() { ret i32 1 }"), &r);
    Mel_Jit_Unit    u = mel_jit_add(jit, v1, &r);
    MEL_REQUIRE(u.index != 0u);

    int (*v)(void) = (int (*)(void))mel_jit_lookup(jit, "v");
    MEL_REQUIRE_NOT_NULL(v);
    MEL_EXPECT_EQ(v(), 1);

    Mel_Jit_Module* v2 = mel_llvm_parse_ir(a, S8("v2"), S8("define i32 @v() { ret i32 2 }"), &r);
    Mel_Jit_Result  rep = mel_jit_replace(jit, u, v2);
    MEL_EXPECT(rep.ok);
    mel_jit_result_free(&rep);

    v = (int (*)(void))mel_jit_lookup(jit, "v");
    MEL_REQUIRE_NOT_NULL(v);
    MEL_EXPECT_EQ(v(), 2);

    mel_jit_destroy(jit);
}

MEL_TEST(llvm, remove_then_lookup_null)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Jit*         jit = mel_test_jit(a, false);
    MEL_REQUIRE_NOT_NULL(jit);

    Mel_Jit_Result  r = { 0 };
    Mel_Jit_Module* m = mel_llvm_parse_ir(a, S8("g"), S8("define i32 @g() { ret i32 7 }"), &r);
    Mel_Jit_Unit    u = mel_jit_add(jit, m, &r);
    MEL_REQUIRE_NOT_NULL(mel_jit_lookup(jit, "g"));

    mel_jit_remove(jit, u);
    MEL_EXPECT_NULL(mel_jit_lookup(jit, "g"));

    mel_jit_destroy(jit);
}

static int g_host_calls = 0;
static int mel_test_host_add7(int x)
{
    g_host_calls++;
    return x + 7;
}

MEL_TEST(llvm, define_symbol_callable_from_jit)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Jit*         jit = mel_test_jit(a, false);
    MEL_REQUIRE_NOT_NULL(jit);

    mel_jit_define_symbol(jit, "mel_test_host_add7", (void*)mel_test_host_add7);

    const char*     ir = "declare i32 @mel_test_host_add7(i32)\n"
                         "define i32 @use() {\n"
                         "  %r = call i32 @mel_test_host_add7(i32 35)\n"
                         "  ret i32 %r\n"
                         "}\n";
    Mel_Jit_Result  r = { 0 };
    Mel_Jit_Module* m = mel_llvm_parse_ir(a, S8("use"), str8_from_cstr(ir), &r);
    MEL_REQUIRE_NOT_NULL(m);
    mel_jit_add(jit, m, &r);

    int (*use)(void) = (int (*)(void))mel_jit_lookup(jit, "use");
    MEL_REQUIRE_NOT_NULL(use);
    g_host_calls = 0;
    MEL_EXPECT_EQ(use(), 42);
    MEL_EXPECT_EQ(g_host_calls, 1);

    mel_jit_destroy(jit);
}
