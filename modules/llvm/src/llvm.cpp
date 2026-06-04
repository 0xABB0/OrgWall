#include <llvm/llvm.h>

#include <allocator/allocator.h>
#include <collection.array/array.h>
#include <log/log.h>

#include <string.h>

#include <memory>

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/Mangling.h>
#include <llvm/ExecutionEngine/Orc/ExecutorProcessControl.h>
#include <llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/JITSymbol.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>

struct Mel_Jit_Module
{
    const Mel_Alloc*            alloc;
    llvm::orc::ThreadSafeModule tsm;
    bool                        consumed;
};

namespace
{

char* mel_llvm_dup_cstr(const Mel_Alloc* a, const char* msg)
{
    usize n = strlen(msg) + 1;
    char* p = (char*)mel_alloc(a, n);
    if (p)
        memcpy(p, msg, n);
    return p;
}

char* mel_llvm_dup_string(const Mel_Alloc* a, const std::string& s)
{
    usize n = s.size() + 1;
    char* p = (char*)mel_alloc(a, n);
    if (p)
        memcpy(p, s.c_str(), n);
    return p;
}

void mel_llvm_set_error(const Mel_Alloc* a, Mel_Jit_Result* out, const std::string& diag)
{
    if (!out)
        return;
    out->ok = false;
    out->diagnostics = mel_llvm_dup_string(a, diag);
    out->alloc = a;
}

std::string mel_llvm_error_to_string(llvm::Error err)
{
    std::string              msg;
    llvm::raw_string_ostream os(msg);
    os << err;
    os.flush();
    llvm::consumeError(std::move(err));
    return msg;
}

void mel_llvm_ensure_native_target()
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

llvm::OptimizationLevel mel_llvm_opt_level(u32 level)
{
    switch (level)
    {
    case 0:
        return llvm::OptimizationLevel::O0;
    case 1:
        return llvm::OptimizationLevel::O1;
    case 2:
        return llvm::OptimizationLevel::O2;
    default:
        return llvm::OptimizationLevel::O3;
    }
}

void mel_llvm_run_passes(llvm::Module& m, u32 level)
{
    if (level == 0)
        return;

    llvm::PassBuilder             pb;
    llvm::LoopAnalysisManager     lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager    cgam;
    llvm::ModuleAnalysisManager   mam;

    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);

    llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(mel_llvm_opt_level(level));
    mpm.run(m, mam);
}

struct Mel_Orc_Unit
{
    llvm::orc::ResourceTrackerSP* tracker;
    u32                           generation;
    bool                          live;
};

struct Mel_Orc_Backend
{
    const Mel_Alloc*                              alloc;
    std::unique_ptr<llvm::orc::LLJIT>             jit;
    std::unique_ptr<llvm::orc::MangleAndInterner> mangle;
    llvm::orc::JITDylib*                          dylib;
    u32                                           opt_level;
    Mel_Array(Mel_Orc_Unit) units;
    Mel_Array(u32) free_slots;
};

llvm::orc::ResourceTrackerSP* mel_orc_tracker_alloc(const Mel_Alloc* a, llvm::orc::ResourceTrackerSP rt)
{
    llvm::orc::ResourceTrackerSP* p = (llvm::orc::ResourceTrackerSP*)mel_alloc(a, sizeof(*p));
    if (p)
        new (p) llvm::orc::ResourceTrackerSP(std::move(rt));
    return p;
}

void mel_orc_tracker_free(const Mel_Alloc* a, llvm::orc::ResourceTrackerSP* p)
{
    if (!p)
        return;
    using RTSP = llvm::orc::ResourceTrackerSP;
    p->~RTSP();
    mel_dealloc(a, p);
}

void mel_orc_units_push(const Mel_Alloc* a, Mel_Orc_Backend* b, Mel_Orc_Unit unit)
{
    if (b->units.count >= b->units.capacity)
    {
        usize         new_cap = b->units.capacity == 0 ? MEL_DA_INIT_CAP : b->units.capacity * 2;
        usize         new_size = sizeof(Mel_Orc_Unit) * new_cap;
        Mel_Orc_Unit* grown = b->units.items == nullptr ? (Mel_Orc_Unit*)mel_alloc(a, new_size) : (Mel_Orc_Unit*)mel_realloc(a, b->units.items, new_size);
        b->units.items = grown;
        b->units.capacity = new_cap;
    }
    b->units.items[b->units.count++] = unit;
}

void mel_orc_free_slots_push(const Mel_Alloc* a, Mel_Orc_Backend* b, u32 slot)
{
    if (b->free_slots.count >= b->free_slots.capacity)
    {
        usize new_cap = b->free_slots.capacity == 0 ? MEL_DA_INIT_CAP : b->free_slots.capacity * 2;
        usize new_size = sizeof(u32) * new_cap;
        u32*  grown = b->free_slots.items == nullptr ? (u32*)mel_alloc(a, new_size) : (u32*)mel_realloc(a, b->free_slots.items, new_size);
        b->free_slots.items = grown;
        b->free_slots.capacity = new_cap;
    }
    b->free_slots.items[b->free_slots.count++] = slot;
}

usize mel_orc_acquire_slot(Mel_Orc_Backend* b, llvm::orc::ResourceTrackerSP* tracker, u32* out_gen)
{
    if (b->free_slots.count > 0)
    {
        u32 slot = b->free_slots.items[b->free_slots.count - 1];
        b->free_slots.count -= 1;
        Mel_Orc_Unit* u = &b->units.items[slot];
        u->generation += 1;
        u->tracker = tracker;
        u->live = true;
        *out_gen = u->generation;
        return slot;
    }

    Mel_Orc_Unit unit;
    unit.tracker = tracker;
    unit.generation = 1;
    unit.live = true;
    mel_orc_units_push(b->alloc, b, unit);
    *out_gen = 1;
    return b->units.count - 1;
}

bool mel_orc_unit_valid(Mel_Orc_Backend* b, Mel_Jit_Unit u)
{
    if (u.index == 0)
        return false;
    usize slot = (usize)u.index - 1;
    if (slot >= b->units.count)
        return false;
    Mel_Orc_Unit* unit = &b->units.items[slot];
    return unit->live && unit->generation == u.generation;
}

llvm::Error mel_orc_add_to_tracker(Mel_Orc_Backend* b, llvm::orc::ResourceTrackerSP rt, Mel_Jit_Module* module)
{
    mel_llvm_run_passes(*module->tsm.getModuleUnlocked(), b->opt_level);
    llvm::Error err = b->jit->addIRModule(rt, std::move(module->tsm));
    module->consumed = true;
    return err;
}

extern "C"
{
Mel_Jit_Unit mel_orc_add(void* self, Mel_Jit_Module* module, Mel_Jit_Result* out)
{
    Mel_Orc_Backend* b = (Mel_Orc_Backend*)self;
    if (!module || module->consumed)
    {
        const char* msg = "llvm orc add: null or already-consumed module";
        mel_log_error("llvm", "%s", msg);
        mel_llvm_set_error(b->alloc, out, msg);
        return Mel_Jit_Unit{};
    }

    const Mel_Alloc*             ma = module->alloc;
    llvm::orc::JITDylib&         jd = *b->dylib;
    llvm::orc::ResourceTrackerSP rt = jd.createResourceTracker();

    if (llvm::Error err = mel_orc_add_to_tracker(b, rt, module))
    {
        std::string diag = "llvm orc add: " + mel_llvm_error_to_string(std::move(err));
        mel_log_error("llvm", "%s", diag.c_str());
        mel_llvm_set_error(b->alloc, out, diag);
        llvm::consumeError(rt->remove());
        mel_llvm_module_free(ma, module);
        return Mel_Jit_Unit{};
    }
    mel_llvm_module_free(ma, module);

    llvm::orc::ResourceTrackerSP* tracker = mel_orc_tracker_alloc(b->alloc, rt);
    if (!tracker)
    {
        const char* msg = "llvm orc add: out of memory tracking the unit";
        mel_log_error("llvm", "%s", msg);
        mel_llvm_set_error(b->alloc, out, msg);
        llvm::consumeError(rt->remove());
        return Mel_Jit_Unit{};
    }

    u32   gen = 0;
    usize slot = mel_orc_acquire_slot(b, tracker, &gen);

    if (out)
    {
        out->ok = true;
        out->diagnostics = nullptr;
        out->alloc = nullptr;
    }
    return Mel_Jit_Unit{ (u32)slot + 1, gen };
}

Mel_Jit_Result mel_orc_replace(void* self, Mel_Jit_Unit unit, Mel_Jit_Module* module)
{
    Mel_Orc_Backend* b = (Mel_Orc_Backend*)self;
    Mel_Jit_Result   r = {};
    r.alloc = b->alloc;

    if (!module || module->consumed)
    {
        const char* msg = "llvm orc replace: null or already-consumed module";
        mel_log_error("llvm", "%s", msg);
        r.ok = false;
        r.diagnostics = mel_llvm_dup_cstr(b->alloc, msg);
        return r;
    }

    const Mel_Alloc* ma = module->alloc;

    if (!mel_orc_unit_valid(b, unit))
    {
        const char* msg = "llvm orc replace: stale or unknown unit";
        mel_log_error("llvm", "%s", msg);
        mel_llvm_module_free(ma, module);
        r.ok = false;
        r.diagnostics = mel_llvm_dup_cstr(b->alloc, msg);
        return r;
    }

    usize         slot = (usize)unit.index - 1;
    Mel_Orc_Unit* slot_unit = &b->units.items[slot];

    if (llvm::Error err2 = (*slot_unit->tracker)->remove())
    {
        std::string diag = "llvm orc replace (remove old): " + mel_llvm_error_to_string(std::move(err2));
        mel_log_error("llvm", "%s", diag.c_str());
        mel_llvm_module_free(ma, module);
        r.ok = false;
        r.diagnostics = mel_llvm_dup_string(b->alloc, diag);
        return r;
    }

    llvm::orc::JITDylib&         jd = *b->dylib;
    llvm::orc::ResourceTrackerSP rt = jd.createResourceTracker();

    if (llvm::Error err = mel_orc_add_to_tracker(b, rt, module))
    {
        std::string diag = "llvm orc replace (re-add): " + mel_llvm_error_to_string(std::move(err));
        mel_log_error("llvm", "%s", diag.c_str());
        llvm::consumeError(rt->remove());
        mel_llvm_module_free(ma, module);
        mel_orc_tracker_free(b->alloc, slot_unit->tracker);
        slot_unit->tracker = nullptr;
        slot_unit->live = false;
        mel_orc_free_slots_push(b->alloc, b, (u32)slot);
        r.ok = false;
        r.diagnostics = mel_llvm_dup_string(b->alloc, diag);
        return r;
    }
    mel_llvm_module_free(ma, module);

    llvm::orc::ResourceTrackerSP* tracker = mel_orc_tracker_alloc(b->alloc, rt);
    if (!tracker)
    {
        const char* msg = "llvm orc replace: out of memory tracking the unit";
        mel_log_error("llvm", "%s", msg);
        llvm::consumeError(rt->remove());
        mel_orc_tracker_free(b->alloc, slot_unit->tracker);
        slot_unit->tracker = nullptr;
        slot_unit->live = false;
        mel_orc_free_slots_push(b->alloc, b, (u32)slot);
        r.ok = false;
        r.diagnostics = mel_llvm_dup_cstr(b->alloc, msg);
        return r;
    }

    mel_orc_tracker_free(b->alloc, slot_unit->tracker);
    slot_unit->tracker = tracker;
    slot_unit->live = true;

    r.ok = true;
    r.diagnostics = nullptr;
    return r;
}

void mel_orc_remove(void* self, Mel_Jit_Unit unit)
{
    Mel_Orc_Backend* b = (Mel_Orc_Backend*)self;
    if (!mel_orc_unit_valid(b, unit))
    {
        mel_log_warn("llvm", "llvm orc remove: stale or unknown unit");
        return;
    }
    usize         slot = (usize)unit.index - 1;
    Mel_Orc_Unit* slot_unit = &b->units.items[slot];
    if (llvm::Error err = (*slot_unit->tracker)->remove())
    {
        std::string diag = "llvm orc remove: " + mel_llvm_error_to_string(std::move(err));
        mel_log_error("llvm", "%s", diag.c_str());
    }
    mel_orc_tracker_free(b->alloc, slot_unit->tracker);
    slot_unit->tracker = nullptr;
    slot_unit->live = false;
    mel_orc_free_slots_push(b->alloc, b, (u32)slot);
}

void* mel_orc_lookup(void* self, const char* symbol)
{
    Mel_Orc_Backend*                        b = (Mel_Orc_Backend*)self;
    llvm::Expected<llvm::orc::ExecutorAddr> addr = b->jit->lookup(*b->dylib, symbol);
    if (!addr)
    {
        std::string diag = "llvm orc lookup '" + std::string(symbol) + "': " + mel_llvm_error_to_string(addr.takeError());
        mel_log_error("llvm", "%s", diag.c_str());
        return nullptr;
    }
    return addr->toPtr<void*>();
}

void mel_orc_define(void* self, const char* symbol, void* address)
{
    Mel_Orc_Backend*     b = (Mel_Orc_Backend*)self;
    llvm::orc::JITDylib& jd = *b->dylib;

    llvm::orc::ExecutorSymbolDef def(llvm::orc::ExecutorAddr::fromPtr(address), llvm::JITSymbolFlags::Exported);
    llvm::orc::SymbolMap         symbols;
    symbols[(*b->mangle)(symbol)] = def;

    if (llvm::Error err = jd.define(llvm::orc::absoluteSymbols(std::move(symbols))))
    {
        std::string diag = "llvm orc define_symbol '" + std::string(symbol) + "': " + mel_llvm_error_to_string(std::move(err));
        mel_log_error("llvm", "%s", diag.c_str());
    }
}

void mel_orc_destroy(void* self)
{
    Mel_Orc_Backend* b = (Mel_Orc_Backend*)self;
    if (!b)
        return;
    const Mel_Alloc* a = b->alloc;
    for (usize i = 0; i < b->units.count; i++)
    {
        Mel_Orc_Unit* unit = &b->units.items[i];
        if (unit->tracker)
        {
            mel_orc_tracker_free(a, unit->tracker);
            unit->tracker = nullptr;
        }
    }
    mel_array_free(&b->units);
    mel_array_free(&b->free_slots);
    b->mangle.reset();
    b->jit.reset();
    b->~Mel_Orc_Backend();
    mel_dealloc(a, b);
}
}

}

Mel_Jit_Module* mel_llvm_parse_ir(const Mel_Alloc* a, str8 name, str8 ir_text, Mel_Jit_Result* out)
{
    mel_llvm_ensure_native_target();

    std::string     name_str((const char*)name.data, (size_t)name.len);
    llvm::StringRef ir_ref((const char*)ir_text.data, (size_t)ir_text.len);

    auto ctx = std::make_unique<llvm::LLVMContext>();

    llvm::SMDiagnostic                  diag;
    std::unique_ptr<llvm::MemoryBuffer> buf = llvm::MemoryBuffer::getMemBuffer(ir_ref, name_str, false);
    std::unique_ptr<llvm::Module>       mod = llvm::parseIR(buf->getMemBufferRef(), diag, *ctx);

    if (!mod)
    {
        std::string              diag_str;
        llvm::raw_string_ostream os(diag_str);
        diag.print(name_str.c_str(), os);
        os.flush();
        mel_log_error("llvm", "llvm parse_ir: %s", diag_str.c_str());
        mel_llvm_set_error(a, out, diag_str);
        return nullptr;
    }

    Mel_Jit_Module* m = (Mel_Jit_Module*)mel_alloc(a, sizeof(Mel_Jit_Module));
    if (!m)
    {
        mel_llvm_set_error(a, out, "llvm parse_ir: out of memory");
        return nullptr;
    }
    new (m) Mel_Jit_Module();
    m->alloc = a;
    m->consumed = false;
    m->tsm = llvm::orc::ThreadSafeModule(std::move(mod), std::move(ctx));

    if (out)
    {
        out->ok = true;
        out->diagnostics = nullptr;
        out->alloc = nullptr;
    }
    return m;
}

void mel_llvm_module_free(const Mel_Alloc* a, Mel_Jit_Module* module)
{
    if (!module)
        return;
    module->~Mel_Jit_Module();
    mel_dealloc(a, module);
}

Mel_Jit_Backend mel_llvm_orc_backend(const Mel_Alloc* a, const Mel_Llvm_Orc_Config* cfg)
{
    Mel_Jit_Backend be = {};

    if (!a || !cfg)
    {
        mel_log_error("llvm", "mel_llvm_orc_backend requires allocator + config");
        return be;
    }

    mel_llvm_ensure_native_target();

    llvm::Expected<std::unique_ptr<llvm::orc::LLJIT>> jit_or_err = llvm::orc::LLJITBuilder().create();
    if (!jit_or_err)
    {
        std::string diag = "mel_llvm_orc_backend (LLJIT create): " + mel_llvm_error_to_string(jit_or_err.takeError());
        mel_log_error("llvm", "%s", diag.c_str());
        return be;
    }

    Mel_Orc_Backend* b = (Mel_Orc_Backend*)mel_alloc(a, sizeof(Mel_Orc_Backend));
    if (!b)
    {
        mel_log_error("llvm", "mel_llvm_orc_backend: out of memory");
        return be;
    }
    new (b) Mel_Orc_Backend();
    b->alloc = a;
    b->jit = std::move(*jit_or_err);
    b->mangle = std::make_unique<llvm::orc::MangleAndInterner>(b->jit->getExecutionSession(), b->jit->getDataLayout());
    b->opt_level = cfg->opt_level;
    mel_array_init(&b->units, a);
    mel_array_init(&b->free_slots, a);

    if (cfg->expose_process_symbols)
    {
        b->dylib = &b->jit->getMainJITDylib();
    }
    else
    {
        b->dylib = &b->jit->getExecutionSession().createBareJITDylib("mel_jit_user");
    }

    be.self = b;
    be.add = mel_orc_add;
    be.replace = mel_orc_replace;
    be.remove = mel_orc_remove;
    be.lookup = mel_orc_lookup;
    be.define_symbol = mel_orc_define;
    be.destroy = mel_orc_destroy;
    return be;
}
