#define VK_NO_PROTOTYPES
#include "vk_shader.h"
#include <slang.h>
#include <slang-deprecated.h>
#include <SDL3/SDL_log.h>
#include <cstring>
#include <cstdlib>
#include <cassert>

static SlangSession* g_slang_session = nullptr;

extern "C" bool mel_slang_init(void)
{
    if (g_slang_session)
    {
        return true;
    }

    g_slang_session = spCreateSession(nullptr);
    if (!g_slang_session)
    {
        SDL_Log("Failed to create Slang session");
        return false;
    }

    SlangResult result = spSessionCheckCompileTargetSupport(g_slang_session, SLANG_SPIRV);
    if (SLANG_FAILED(result))
    {
        SDL_Log("Slang does not support SPIRV target");
        spDestroySession(g_slang_session);
        g_slang_session = nullptr;
        return false;
    }

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

static VkShaderModule create_shader_module(Mel_VkContext* ctx, const void* code, size_t size)
{
    VkShaderModuleCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = size;
    create_info.pCode = (const u32*)code;

    VkShaderModule module;
    VkResult result = vkCreateShaderModule(ctx->device, &create_info, nullptr, &module);
    if (result != VK_SUCCESS)
    {
        SDL_Log("Failed to create shader module: %d", result);
        return VK_NULL_HANDLE;
    }

    return module;
}

static bool compile_entry_point(const char* source, const char* entry_name, SlangStage stage,
                                void** out_code, size_t* out_size)
{
    assert(g_slang_session != nullptr);
    assert(source != nullptr);
    assert(entry_name != nullptr);
    assert(out_code != nullptr);
    assert(out_size != nullptr);

    SlangCompileRequest* request = spCreateCompileRequest(g_slang_session);
    if (!request)
    {
        SDL_Log("Failed to create Slang compile request");
        return false;
    }

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
        {
            SDL_Log("Slang compilation failed:\n%s", diagnostics);
        }
        else
        {
            SDL_Log("Slang compilation failed with code: %d", result);
        }
        spDestroyCompileRequest(request);
        return false;
    }

    const char* diagnostics = spGetDiagnosticOutput(request);
    if (diagnostics && diagnostics[0])
    {
        SDL_Log("Slang warnings:\n%s", diagnostics);
    }

    size_t code_size = 0;
    const void* code = spGetEntryPointCode(request, 0, &code_size);
    if (!code || code_size == 0)
    {
        SDL_Log("Failed to get compiled SPIRV code");
        spDestroyCompileRequest(request);
        return false;
    }

    void* code_copy = malloc(code_size);
    if (!code_copy)
    {
        SDL_Log("Failed to allocate memory for shader code");
        spDestroyCompileRequest(request);
        return false;
    }

    memcpy(code_copy, code, code_size);
    *out_code = code_copy;
    *out_size = code_size;

    spDestroyCompileRequest(request);
    return true;
}

extern "C" bool mel_vk_shader_init_opt(Mel_VkShader* shader, Mel_VkContext* ctx, Mel_VkShader_Opt opt)
{
    assert(shader != nullptr);
    assert(ctx != nullptr);
    assert(opt.source != nullptr);

    *shader = {};

    if (!g_slang_session)
    {
        SDL_Log("Slang not initialized! Call mel_slang_init() first");
        return false;
    }

    const char* vert_entry = opt.vertex_entry ? opt.vertex_entry : "vertexMain";
    const char* frag_entry = opt.fragment_entry ? opt.fragment_entry : "fragmentMain";

    void* vert_code = nullptr;
    size_t vert_size = 0;
    void* frag_code = nullptr;
    size_t frag_size = 0;

    if (!compile_entry_point(opt.source, vert_entry, SLANG_STAGE_VERTEX, &vert_code, &vert_size))
    {
        SDL_Log("Failed to compile vertex shader");
        return false;
    }

    if (!compile_entry_point(opt.source, frag_entry, SLANG_STAGE_FRAGMENT, &frag_code, &frag_size))
    {
        SDL_Log("Failed to compile fragment shader");
        free(vert_code);
        return false;
    }

    shader->vertex = create_shader_module(ctx, vert_code, vert_size);
    shader->fragment = create_shader_module(ctx, frag_code, frag_size);

    free(vert_code);
    free(frag_code);

    if (shader->vertex == VK_NULL_HANDLE || shader->fragment == VK_NULL_HANDLE)
    {
        mel_vk_shader_shutdown(shader, ctx);
        return false;
    }

    SDL_Log("Shader compiled successfully (vertex + fragment)");
    return true;
}

extern "C" void mel_vk_shader_shutdown(Mel_VkShader* shader, Mel_VkContext* ctx)
{
    assert(shader != nullptr);
    assert(ctx != nullptr);

    if (shader->vertex)
    {
        vkDestroyShaderModule(ctx->device, shader->vertex, nullptr);
        shader->vertex = VK_NULL_HANDLE;
    }

    if (shader->fragment)
    {
        vkDestroyShaderModule(ctx->device, shader->fragment, nullptr);
        shader->fragment = VK_NULL_HANDLE;
    }
}
