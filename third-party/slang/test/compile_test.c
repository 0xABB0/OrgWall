#include <test/test.h>

#include <slang/compile.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char* TRI =
    "struct VOut { float4 pos : SV_Position; float3 col : COLOR; };\n"
    "[shader(\"vertex\")] VOut vsMain(uint vid : SV_VertexID) {\n"
    "  float2 p[3] = { float2(0,-0.6), float2(0.6,0.6), float2(-0.6,0.6) };\n"
    "  float3 c[3] = { float3(1,0,0), float3(0,1,0), float3(0,0,1) };\n"
    "  VOut o; o.pos = float4(p[vid],0,1); o.col = c[vid]; return o; }\n"
    "[shader(\"fragment\")] float4 fsMain(VOut i) : SV_Target { return float4(i.col,1); }\n";

MEL_TEST(slang_compile, vertex_to_msl)
{
    Mel_Slang_Blob b = mel_slang_compile(TRI, "vsMain", MEL_SLANG_STAGE_VERTEX, MEL_SLANG_TARGET_MSL);
    if (!b.data)
        printf("slang diag: %s\n", b.diagnostics ? b.diagnostics : "(none)");
    MEL_REQUIRE(b.data != NULL);
    MEL_EXPECT(b.size > 0);
    MEL_EXPECT(strstr((const char*)b.data, "metal_stdlib") != NULL);
    mel_slang_blob_free(&b);
}

MEL_TEST(slang_compile, fragment_to_spirv)
{
    Mel_Slang_Blob b = mel_slang_compile(TRI, "fsMain", MEL_SLANG_STAGE_FRAGMENT, MEL_SLANG_TARGET_SPIRV);
    if (!b.data)
        printf("slang diag: %s\n", b.diagnostics ? b.diagnostics : "(none)");
    MEL_REQUIRE(b.data != NULL);
    MEL_EXPECT(b.size >= 20);
    MEL_EXPECT(((const uint32_t*)b.data)[0] == 0x07230203u); // SPIR-V magic
    mel_slang_blob_free(&b);
}

MEL_TEST(slang_compile, bad_source_reports_diagnostics)
{
    Mel_Slang_Blob b = mel_slang_compile("this is not valid slang", "nope", MEL_SLANG_STAGE_VERTEX, MEL_SLANG_TARGET_MSL);
    MEL_EXPECT(b.data == NULL);
    MEL_EXPECT(b.diagnostics != NULL);
    mel_slang_blob_free(&b);
}
