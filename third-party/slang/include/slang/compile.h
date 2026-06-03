#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Shader code-gen target and stage are a fixed graphics-API protocol set (MEL-CODE-001 sanctioned).
typedef enum
{
    MEL_SLANG_TARGET_SPIRV = 0,
    MEL_SLANG_TARGET_MSL = 1,
} Mel_Slang_Target;

typedef enum
{
    MEL_SLANG_STAGE_VERTEX = 0,
    MEL_SLANG_STAGE_FRAGMENT = 1,
    MEL_SLANG_STAGE_COMPUTE = 2,
} Mel_Slang_Stage;

typedef struct
{
    void*  data;        // MSL is NUL-terminated text; SPIR-V is raw words. NULL on failure.
    size_t size;        // byte count (excludes the MSL NUL terminator)
    char*  diagnostics; // NUL-terminated Slang diagnostics, or NULL. Free via mel_slang_blob_free.
} Mel_Slang_Blob;

// Compile one [shader("...")] entry point of Slang source to the target's native form.
// The stage hint is advisory; the entry point's stage comes from its [shader] attribute.
Mel_Slang_Blob mel_slang_compile(const char* source, const char* entry, Mel_Slang_Stage stage, Mel_Slang_Target target);
void           mel_slang_blob_free(Mel_Slang_Blob* blob);

#ifdef __cplusplus
}
#endif
