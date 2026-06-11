#include <clang/clang.h>
#include <llvm/llvm.h>

#include <allocator/allocator.h>
#include <collection/array.h>
#include <log/log.h>

#include <string.h>
#include <mach-o/dyld.h>

#include <string>
#include <vector>
#include <memory>

#include <clang/AST/ASTConsumer.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticIDs.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/CodeGen/ModuleBuilder.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Lex/PreprocessorOptions.h>

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/VirtualFileSystem.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>

#include <clang/Interpreter/Interpreter.h>
#include <clang/Interpreter/Value.h>

namespace
{

char* clang_dup_cstr(const Mel_Alloc* a, const char* msg)
{
    usize n = strlen(msg) + 1;
    char* p = (char*)mel_alloc(a, n);
    if (p)
        memcpy(p, msg, n);
    return p;
}

char* clang_dup_string(const Mel_Alloc* a, const std::string& s)
{
    usize n = s.size() + 1;
    char* p = (char*)mel_alloc(a, n);
    if (p)
        memcpy(p, s.c_str(), n);
    return p;
}

const std::string& clang_resource_dir()
{
    static const std::string cached = []() -> std::string
    {
        uint32_t count = _dyld_image_count();
        for (uint32_t i = 0; i < count; i++)
        {
            const char* path = _dyld_get_image_name(i);
            if (!path)
                continue;
            std::string image = path;
            size_t      slash = image.find_last_of('/');
            std::string base = slash == std::string::npos ? image : image.substr(slash + 1);
            if (base.rfind("libclang-cpp", 0) == 0)
            {
                std::string libdir = slash == std::string::npos ? std::string() : image.substr(0, slash);
                return libdir + "/clang/" + std::to_string(LLVM_VERSION_MAJOR);
            }
        }
        return std::string();
    }();
    return cached;
}

const std::string& clang_process_triple()
{
    static const std::string cached = llvm::sys::getProcessTriple();
    return cached;
}

class Clang_IR_Action: public clang::ASTFrontendAction
{
public:
    Clang_IR_Action(llvm::LLVMContext& ctx): ctx_(ctx) {}

    std::unique_ptr<llvm::Module> take_module() { return std::move(module_); }

protected:
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& ci, llvm::StringRef in_file) override
    {
        (void)in_file;
        std::unique_ptr<clang::CodeGenerator> gen(clang::CreateLLVMCodeGen(ci.getDiagnostics(), "mel_repl_module", &ci.getVirtualFileSystem(), ci.getHeaderSearchOpts(), ci.getPreprocessorOpts(), ci.getCodeGenOpts(), ctx_));
        gen_ = gen.get();
        return gen;
    }

    void EndSourceFileAction() override
    {
        if (gen_)
            module_ = std::unique_ptr<llvm::Module>(gen_->ReleaseModule());
    }

private:
    llvm::LLVMContext&            ctx_;
    clang::CodeGenerator*         gen_ = nullptr;
    std::unique_ptr<llvm::Module> module_;
};

bool clang_emit_ir(str8 name, str8 source, u32 opt_level, std::string& ir_out, std::string& diag_out)
{
    std::string name_str((const char*)name.data, (size_t)name.len);
    std::string src_str((const char*)source.data, (size_t)source.len);

    llvm::raw_string_ostream diag_os(diag_out);

    clang::DiagnosticOptions                           diag_opts;
    clang::TextDiagnosticPrinter                       diag_printer(diag_os, diag_opts);
    auto                                               diag_ids = llvm::makeIntrusiveRefCnt<clang::DiagnosticIDs>();
    llvm::IntrusiveRefCntPtr<clang::DiagnosticsEngine> diags = llvm::makeIntrusiveRefCnt<clang::DiagnosticsEngine>(diag_ids, diag_opts, &diag_printer, false);

    const std::string& resource_dir = clang_resource_dir();
    std::string        opt_flag = "-O" + std::to_string(opt_level > 3 ? 3u : opt_level);

    std::vector<const char*> args;
    args.push_back("-triple");
    args.push_back(clang_process_triple().c_str());
    args.push_back("-x");
    args.push_back("c");
    args.push_back("-std=c23");
    args.push_back("-emit-llvm-only");
    args.push_back(opt_flag.c_str());
    if (!resource_dir.empty())
    {
        args.push_back("-resource-dir");
        args.push_back(resource_dir.c_str());
    }
    args.push_back(name_str.c_str());

    auto invocation = std::make_shared<clang::CompilerInvocation>();
    if (!clang::CompilerInvocation::CreateFromArgs(*invocation, args, *diags))
    {
        diag_os.flush();
        if (diag_out.empty())
            diag_out = "clang: failed to build compiler invocation";
        return false;
    }

    clang::CompilerInstance ci(invocation);
    ci.setDiagnostics(diags.get());

    std::unique_ptr<llvm::MemoryBuffer> buf = llvm::MemoryBuffer::getMemBufferCopy(src_str, name_str);
    ci.getPreprocessorOpts().addRemappedFile(name_str, buf.release());

    auto            ctx = std::make_unique<llvm::LLVMContext>();
    Clang_IR_Action action(*ctx);

    bool ok = ci.ExecuteAction(action);
    diag_os.flush();

    std::unique_ptr<llvm::Module> mod = action.take_module();
    if (!ok || diags->hasErrorOccurred() || !mod)
    {
        if (diag_out.empty())
            diag_out = "clang: compilation failed";
        return false;
    }

    llvm::raw_string_ostream ir_os(ir_out);
    mod->print(ir_os, nullptr);
    ir_os.flush();
    return true;
}

} // namespace

Mel_Jit_Module* mel_clang_compile(const Mel_Alloc* a, str8 name, str8 source, const Mel_Clang_Config* cfg, Mel_Jit_Result* out)
{
    if (!a || !cfg)
    {
        mel_log_error("clang", "mel_clang_compile requires allocator + config");
        if (out)
        {
            out->ok = false;
            out->diagnostics = a ? clang_dup_cstr(a, "clang: null allocator or config") : nullptr;
            out->alloc = a;
        }
        return nullptr;
    }

    std::string diag;
    std::string ir;
    if (!clang_emit_ir(name, source, cfg->opt_level, ir, diag))
    {
        mel_log_error("clang", "mel_clang_compile: %s", diag.c_str());
        if (out)
        {
            out->ok = false;
            out->diagnostics = clang_dup_string(a, diag);
            out->alloc = a;
        }
        return nullptr;
    }

    str8 name_view;
    name_view.data = name.data;
    name_view.len = name.len;
    str8 ir_view;
    ir_view.data = (u8*)ir.data();
    ir_view.len = ir.size();

    Mel_Jit_Module* m = mel_llvm_parse_ir(a, name_view, ir_view, out);
    return m;
}

struct Clang_Lang
{
    const Mel_Alloc*                    alloc;
    std::unique_ptr<clang::Interpreter> interp;
};

namespace
{

void clang_ensure_native_target()
{
    static const bool once = []
    {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
        return true;
    }();
    (void)once;
}

extern "C"
{
Mel_Repl_Result clang_lang_eval(void* self, str8 line, const Mel_Alloc* a)
{
    Clang_Lang* l = (Clang_Lang*)self;

    usize start = 0;
    while (start < line.len && (line.data[start] == ' ' || line.data[start] == '\t' || line.data[start] == '\n'))
        start++;
    if (start >= line.len)
    {
        Mel_Repl_Result r;
        r.ok = true;
        r.text = nullptr;
        r.diagnostics = nullptr;
        r.alloc = nullptr;
        return r;
    }

    std::string code((const char*)line.data, (size_t)line.len);

    clang::Value v;
    if (llvm::Error err = l->interp->ParseAndExecute(code, &v))
    {
        std::string     diag = llvm::toString(std::move(err));
        Mel_Repl_Result r;
        r.ok = false;
        r.text = nullptr;
        r.diagnostics = clang_dup_string(a, diag);
        r.alloc = a;
        return r;
    }

    Mel_Repl_Result r;
    r.ok = true;
    r.diagnostics = nullptr;
    if (v.isValid() && !v.isVoid())
    {
        std::string              s;
        llvm::raw_string_ostream os(s);
        v.print(os);
        os.flush();
        r.text = clang_dup_string(a, s);
        r.alloc = a;
    }
    else
    {
        r.text = nullptr;
        r.alloc = nullptr;
    }
    return r;
}

void clang_lang_destroy(void* self)
{
    Clang_Lang* l = (Clang_Lang*)self;
    if (!l)
        return;
    const Mel_Alloc* a = l->alloc;
    l->interp.reset();
    l->~Clang_Lang();
    mel_dealloc(a, l);
}
}

} // namespace

Mel_Repl_Lang mel_clang_repl_lang(const Mel_Alloc* a)
{
    Mel_Repl_Lang lang = {};

    clang_ensure_native_target();

    const std::string&       rd = clang_resource_dir();
    std::vector<const char*> cargs;
    if (!rd.empty())
    {
        cargs.push_back("-resource-dir");
        cargs.push_back(rd.c_str());
    }

    clang::IncrementalCompilerBuilder cb;
    cb.SetCompilerArgs(cargs);

    llvm::Expected<std::unique_ptr<clang::CompilerInstance>> ci = cb.CreateCpp();
    if (!ci)
    {
        std::string diag = llvm::toString(ci.takeError());
        mel_log_error("clang", "mel_clang_repl_lang (compiler): %s", diag.c_str());
        return lang;
    }

    llvm::Expected<std::unique_ptr<clang::Interpreter>> interp = clang::Interpreter::create(std::move(*ci));
    if (!interp)
    {
        llvm::logAllUnhandledErrors(interp.takeError(), llvm::errs(), "mel_clang_repl_lang interpreter: ");
        mel_log_error("clang", "mel_clang_repl_lang: clang::Interpreter::create failed");
        return lang;
    }

    Clang_Lang* l = (Clang_Lang*)mel_alloc(a, sizeof(Clang_Lang));
    if (!l)
    {
        mel_log_error("clang", "mel_clang_repl_lang: out of memory");
        return lang;
    }
    new (l) Clang_Lang();
    l->alloc = a;
    l->interp = std::move(*interp);

    lang.self = l;
    lang.eval = clang_lang_eval;
    lang.destroy = clang_lang_destroy;
    return lang;
}
