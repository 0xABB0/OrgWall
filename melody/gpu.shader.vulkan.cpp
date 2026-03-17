#include "gpu.shader.h"
#include "gpu.device.vulkan.h"
extern "C" {
#include "event.channel.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "vfs.h"
#include "async.job.h"
}
#include "string.str8.h"
#include <slang.h>
#include <slang-deprecated.h>
#include <SDL3/SDL_log.h>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <cstdio>

extern "C" {

Mel_Event_Channel mel_slang_ready;

__attribute__((constructor))
static void mel__slang_register(void)
{
    mel_event_channel_init(&mel_slang_ready, mel_alloc_heap());
}

__attribute__((destructor))
static void mel__slang_unregister(void)
{
    mel_event_channel_destroy(&mel_slang_ready);
}

}

static SlangSession* g_slang_session = nullptr;

extern "C" bool mel_slang_init(void)
{
    if (g_slang_session) return true;

    g_slang_session = spCreateSession(nullptr);
    assert(g_slang_session != nullptr);

    SlangResult result = spSessionCheckCompileTargetSupport(g_slang_session, SLANG_SPIRV);
    assert(SLANG_SUCCEEDED(result) && "Slang does not support SPIRV target");

    SDL_Log("Slang session initialized");
    return true;
}

extern "C" void mel_slang_shutdown(void)
{
    if (g_slang_session)
    {
        spDestroySession(g_slang_session);
        g_slang_session = nullptr;
        SDL_Log("Slang session shut down");
    }
}

static VkShaderModule create_shader_module(Mel_Gpu_Device* dev, const void* code, size_t size)
{
    VkShaderModuleCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = size;
    create_info.pCode = (const uint32_t*)code;

    VkShaderModule module;
    VkResult result = vkCreateShaderModule(mel__gpu_device_vk(dev)->device, &create_info, nullptr, &module);
    assert(result == VK_SUCCESS);

    return module;
}

static bool compile_entry_point(const char* source, const char* entry_name, SlangStage stage,
                                void** out_code, size_t* out_size)
{
    assert(g_slang_session != nullptr);

    SlangCompileRequest* request = spCreateCompileRequest(g_slang_session);
    assert(request != nullptr);

    spSetCodeGenTarget(request, SLANG_SPIRV);
    spSetTargetFlags(request, 0, SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY);
    spSetMatrixLayoutMode(request, SLANG_MATRIX_LAYOUT_ROW_MAJOR);

    int tu = spAddTranslationUnit(request, SLANG_SOURCE_LANGUAGE_SLANG, nullptr);
    spAddTranslationUnitSourceString(request, tu, "shader.slang", source);
    spAddEntryPoint(request, tu, entry_name, stage);

    SlangResult result = spCompile(request);
    if (SLANG_FAILED(result))
    {
        const char* diagnostics = spGetDiagnosticOutput(request);
        if (diagnostics && diagnostics[0])
            SDL_Log("Slang compilation failed:\n%s", diagnostics);
        spDestroyCompileRequest(request);
        return false;
    }

    const char* diagnostics = spGetDiagnosticOutput(request);
    if (diagnostics && diagnostics[0])
        SDL_Log("Slang warnings:\n%s", diagnostics);

    size_t code_size = 0;
    const void* code = spGetEntryPointCode(request, 0, &code_size);
    assert(code != nullptr && code_size > 0);

    void* code_copy = malloc(code_size);
    memcpy(code_copy, code, code_size);
    *out_code = code_copy;
    *out_size = code_size;

    spDestroyCompileRequest(request);
    return true;
}

static void copy_entry_name(str8 entry, char* buf, size_t buf_size, const char* fallback)
{
    if (!str8_is_empty(entry))
        str8_to_buf(entry, buf, buf_size);
    else
        strncpy(buf, fallback, buf_size);
}

extern "C" void mel_gpu_shader_init_opt(Mel_Gpu_Shader* shader, Mel_Gpu_Device* dev, Mel_Gpu_Shader_Opt opt)
{
    assert(shader != nullptr);
    assert(dev != nullptr);
    assert(!str8_is_empty(opt.source));
    assert(g_slang_session != nullptr);

    *shader = {};

    char source_buf[32768];
    str8_to_buf(opt.source, source_buf, sizeof(source_buf));

    char vert_entry_buf[256] = {0};
    char frag_entry_buf[256] = {0};
    char compute_entry_buf[256] = {0};
    char task_entry_buf[256] = {0};
    char mesh_entry_buf[256] = {0};

    copy_entry_name(opt.vertex_entry, vert_entry_buf, sizeof(vert_entry_buf), "vertexMain");
    copy_entry_name(opt.fragment_entry, frag_entry_buf, sizeof(frag_entry_buf), "fragmentMain");
    copy_entry_name(opt.compute_entry, compute_entry_buf, sizeof(compute_entry_buf), "computeMain");
    copy_entry_name(opt.task_entry, task_entry_buf, sizeof(task_entry_buf), "taskMain");
    copy_entry_name(opt.mesh_entry, mesh_entry_buf, sizeof(mesh_entry_buf), "meshMain");

    bool wants_compute = !str8_is_empty(opt.compute_entry);
    bool wants_mesh = !str8_is_empty(opt.mesh_entry);
    bool wants_graphics = !wants_compute && !wants_mesh;

    if (wants_graphics)
    {
        void* vert_code = nullptr;
        size_t vert_size = 0;
        void* frag_code = nullptr;
        size_t frag_size = 0;

        bool vert_ok = compile_entry_point(source_buf, vert_entry_buf, SLANG_STAGE_VERTEX, &vert_code, &vert_size);
        assert(vert_ok);

        bool frag_ok = compile_entry_point(source_buf, frag_entry_buf, SLANG_STAGE_FRAGMENT, &frag_code, &frag_size);
        if (!frag_ok) { free(vert_code); }
        assert(frag_ok);

        shader->_vertex = create_shader_module(dev, vert_code, vert_size);
        shader->_fragment = create_shader_module(dev, frag_code, frag_size);

        free(vert_code);
        free(frag_code);

        SDL_Log("Shader compiled successfully (vertex + fragment)");
        return;
    }

    if (wants_compute)
    {
        void* compute_code = nullptr;
        size_t compute_size = 0;
        bool ok = compile_entry_point(source_buf, compute_entry_buf, SLANG_STAGE_COMPUTE, &compute_code, &compute_size);
        assert(ok);
        shader->_compute = create_shader_module(dev, compute_code, compute_size);
        free(compute_code);
        SDL_Log("Shader compiled successfully (compute)");
        return;
    }

    void* mesh_code = nullptr;
    size_t mesh_size = 0;
    void* frag_code = nullptr;
    size_t frag_size = 0;
    bool mesh_ok = compile_entry_point(source_buf, mesh_entry_buf, SLANG_STAGE_MESH, &mesh_code, &mesh_size);
    assert(mesh_ok);
    bool frag_ok = compile_entry_point(source_buf, frag_entry_buf, SLANG_STAGE_FRAGMENT, &frag_code, &frag_size);
    if (!frag_ok)
        free(mesh_code);
    assert(frag_ok);

    shader->_mesh = create_shader_module(dev, mesh_code, mesh_size);
    shader->_fragment = create_shader_module(dev, frag_code, frag_size);
    free(mesh_code);
    free(frag_code);
    SDL_Log("Shader compiled successfully (mesh + fragment)");
}

typedef struct {
    Mel_Gpu_Shader* shader;
    str8 path;
    Mel_Gpu_Device* dev;
    Mel_Gpu_Shader_Opt shader_opt;
    const Mel_Alloc* alloc;
} Mel__Shader_Load_Job_Ctx;

static void mel__shader_load_execute(Mel__Shader_Load_Job_Ctx* ctx)
{
    const Mel_Alloc* alloc = ctx->alloc ? ctx->alloc : mel_alloc_heap();

    i64 file_size = 0;
    u8* file_data = mel_vfs_read_file(ctx->path, &file_size, alloc);

    if (!file_data)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_GPU, "Shader '%.*s' not found in VFS, falling back to filesystem",
                    (int)ctx->path.len, (const char*)ctx->path.data);

        char path_buf[1024];
        str8_to_buf(ctx->path, path_buf, sizeof(path_buf));

        FILE* f = fopen(path_buf, "rb");
        assert(f != nullptr);
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        file_data = (u8*)mel_alloc(alloc, (usize)len);
        size_t n_read = fread(file_data, 1, (usize)len, f);
        assert((long)n_read == len);
        fclose(f);
        file_size = len;
    }

    ctx->shader_opt.source = (str8){ .data = file_data, .len = (size)file_size };
    mel_gpu_shader_init_opt(ctx->shader, ctx->dev, ctx->shader_opt);

    mel_dealloc(alloc, file_data);
}

static void mel__shader_load_job(void* data)
{
    Mel__Shader_Load_Job_Ctx* ctx = (Mel__Shader_Load_Job_Ctx*)data;
    mel__shader_load_execute(ctx);
    const Mel_Alloc* alloc = ctx->alloc ? ctx->alloc : mel_alloc_heap();
    mel_dealloc(alloc, ctx);
}

extern "C" void mel_gpu_shader_load_opt(Mel_Gpu_Shader* shader, Mel_Gpu_Shader_Load_Opt opt)
{
    assert(shader != nullptr);
    assert(opt.dev != nullptr);
    assert(!str8_is_empty(opt.path));

    Mel_Gpu_Shader_Opt shader_opt = {};
    shader_opt.vertex_entry   = opt.vertex_entry;
    shader_opt.fragment_entry = opt.fragment_entry;
    shader_opt.compute_entry  = opt.compute_entry;
    shader_opt.task_entry     = opt.task_entry;
    shader_opt.mesh_entry     = opt.mesh_entry;

    if (!opt.on_finish)
    {
        Mel__Shader_Load_Job_Ctx ctx = {};
        ctx.shader     = shader;
        ctx.path       = opt.path;
        ctx.dev        = opt.dev;
        ctx.shader_opt = shader_opt;
        ctx.alloc      = opt.alloc;
        mel__shader_load_execute(&ctx);
        return;
    }

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    Mel__Shader_Load_Job_Ctx* ctx = (Mel__Shader_Load_Job_Ctx*)mel_alloc(alloc, sizeof(Mel__Shader_Load_Job_Ctx));
    ctx->shader     = shader;
    ctx->path       = opt.path;
    ctx->dev        = opt.dev;
    ctx->shader_opt = shader_opt;
    ctx->alloc      = alloc;

    mel_job_run(ctx, mel__shader_load_job, opt.on_finish);
}

extern "C" void mel_gpu_shader_shutdown(Mel_Gpu_Shader* shader, Mel_Gpu_Device* dev)
{
    assert(shader != nullptr);
    assert(dev != nullptr);

    if (shader->_vertex)
    {
        vkDestroyShaderModule(mel__gpu_device_vk(dev)->device, (VkShaderModule)shader->_vertex, nullptr);
        shader->_vertex = nullptr;
    }

    if (shader->_fragment)
    {
        vkDestroyShaderModule(mel__gpu_device_vk(dev)->device, (VkShaderModule)shader->_fragment, nullptr);
        shader->_fragment = nullptr;
    }
    if (shader->_compute)
    {
        vkDestroyShaderModule(mel__gpu_device_vk(dev)->device, (VkShaderModule)shader->_compute, nullptr);
        shader->_compute = nullptr;
    }
    if (shader->_task)
    {
        vkDestroyShaderModule(mel__gpu_device_vk(dev)->device, (VkShaderModule)shader->_task, nullptr);
        shader->_task = nullptr;
    }
    if (shader->_mesh)
    {
        vkDestroyShaderModule(mel__gpu_device_vk(dev)->device, (VkShaderModule)shader->_mesh, nullptr);
        shader->_mesh = nullptr;
    }
}
