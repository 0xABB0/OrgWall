#define VK_NO_PROTOTYPES
#include "gpu.shader.h"
#include <slang.h>
#include <slang-deprecated.h>
#include <SDL3/SDL_log.h>
#include <cstring>
#include <cstdlib>
#include <cassert>

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
    VkResult result = vkCreateShaderModule(dev->device, &create_info, nullptr, &module);
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

extern "C" void mel_gpu_shader_init_opt(Mel_Gpu_Shader* shader, Mel_Gpu_Device* dev, Mel_Gpu_Shader_Opt opt)
{
    assert(shader != nullptr);
    assert(dev != nullptr);
    assert(opt.source != nullptr);
    assert(g_slang_session != nullptr);

    *shader = {};

    const char* vert_entry = opt.vertex_entry ? opt.vertex_entry : "vertexMain";
    const char* frag_entry = opt.fragment_entry ? opt.fragment_entry : "fragmentMain";

    void* vert_code = nullptr;
    size_t vert_size = 0;
    void* frag_code = nullptr;
    size_t frag_size = 0;

    bool vert_ok = compile_entry_point(opt.source, vert_entry, SLANG_STAGE_VERTEX, &vert_code, &vert_size);
    assert(vert_ok);

    bool frag_ok = compile_entry_point(opt.source, frag_entry, SLANG_STAGE_FRAGMENT, &frag_code, &frag_size);
    if (!frag_ok) { free(vert_code); }
    assert(frag_ok);

    shader->vertex = create_shader_module(dev, vert_code, vert_size);
    shader->fragment = create_shader_module(dev, frag_code, frag_size);

    free(vert_code);
    free(frag_code);

    SDL_Log("Shader compiled successfully (vertex + fragment)");
}

extern "C" void mel_gpu_shader_shutdown(Mel_Gpu_Shader* shader, Mel_Gpu_Device* dev)
{
    assert(shader != nullptr);
    assert(dev != nullptr);

    if (shader->vertex)
    {
        vkDestroyShaderModule(dev->device, shader->vertex, nullptr);
        shader->vertex = VK_NULL_HANDLE;
    }

    if (shader->fragment)
    {
        vkDestroyShaderModule(dev->device, shader->fragment, nullptr);
        shader->fragment = VK_NULL_HANDLE;
    }
}
