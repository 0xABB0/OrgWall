#include <test/test.h>

#include <slang/compile.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* TRI =
    "struct Root { float4x4 mvp; float4 tint; };\n"
    "[[vk::push_constant]] Root root;\n"
    "struct VIn { float3 pos : POSITION; float3 col : COLOR; float2 uv : TEXCOORD; };\n"
    "struct VOut { float4 pos : SV_Position; float3 col : COLOR; };\n"
    "[[vk::binding(0,0)]] Texture2D u_tex;\n"
    "[[vk::binding(1,0)]] SamplerState u_samp;\n"
    "[shader(\"vertex\")] VOut vsMain(VIn i) {\n"
    "  VOut o; o.pos = mul(root.mvp, float4(i.pos,1)); o.col = i.col * root.tint.rgb; return o; }\n"
    "[shader(\"fragment\")] float4 fsMain(VOut i) : SV_Target {\n"
    "  return float4(i.col,1) * u_tex.Sample(u_samp, i.col.xy); }\n";

static const char* COMP =
    "[[vk::binding(0,0)]] RWStructuredBuffer<float> u_out;\n"
    "[numthreads(8,4,2)]\n"
    "[shader(\"compute\")] void csMain(uint3 tid : SV_DispatchThreadID) {\n"
    "  u_out[tid.x] = (float)tid.y; }\n";

static int has_spirv_magic(const Mel_Slang_Blob* b)
{
    return b->data && b->size >= 4 && ((const uint32_t*)b->data)[0] == 0x07230203u;
}

static int spirv_word_count_ok(const Mel_Slang_Blob* b)
{
    return b->size % 4 == 0;
}

static int write_tmp(const char* path, const void* data, size_t n)
{
    FILE* f = fopen(path, "wb");
    if (!f)
        return 0;
    size_t w = fwrite(data, 1, n, f);
    fclose(f);
    return w == n;
}

MEL_TEST(slang_compile, vertex_to_spirv)
{
    Mel_Slang_Blob b = mel_slang_compile(TRI, "vsMain", MEL_SLANG_STAGE_VERTEX, MEL_SLANG_TARGET_SPIRV);
    if (!b.data)
        printf("slang diag: %s\n", b.diagnostics ? b.diagnostics : "(none)");
    MEL_REQUIRE(b.data != NULL);
    MEL_EXPECT(b.size >= 20);
    MEL_EXPECT(has_spirv_magic(&b));
    MEL_EXPECT(spirv_word_count_ok(&b));
    mel_slang_blob_free(&b);
}

MEL_TEST(slang_compile, vertex_to_msl)
{
    Mel_Slang_Blob b = mel_slang_compile(TRI, "vsMain", MEL_SLANG_STAGE_VERTEX, MEL_SLANG_TARGET_MSL);
    if (!b.data)
        printf("slang diag: %s\n", b.diagnostics ? b.diagnostics : "(none)");
    MEL_REQUIRE(b.data != NULL);
    MEL_EXPECT(b.size > 0);
    MEL_EXPECT(strstr((const char*)b.data, "metal_stdlib") != NULL);

    if (write_tmp("/tmp/mel_slang_test_vs.metal", b.data, b.size))
    {
        int rc = system("xcrun -sdk macosx metal -c /tmp/mel_slang_test_vs.metal -o /tmp/mel_slang_test_vs.air 2>/tmp/mel_slang_test_vs.err");
        if (rc == 0)
            printf("MSL verified: xcrun metal -c compiled the emitted source\n");
        else
            printf("MSL UNVERIFIED: xcrun metal -c unavailable or failed (rc=%d); structural check only\n", rc);
    }
    mel_slang_blob_free(&b);
}

MEL_TEST(slang_compile, fragment_to_spirv)
{
    Mel_Slang_Blob b = mel_slang_compile(TRI, "fsMain", MEL_SLANG_STAGE_FRAGMENT, MEL_SLANG_TARGET_SPIRV);
    if (!b.data)
        printf("slang diag: %s\n", b.diagnostics ? b.diagnostics : "(none)");
    MEL_REQUIRE(b.data != NULL);
    MEL_EXPECT(b.size >= 20);
    MEL_EXPECT(has_spirv_magic(&b));
    mel_slang_blob_free(&b);
}

MEL_TEST(slang_compile, vertex_to_wgsl)
{
    Mel_Slang_Blob b = mel_slang_compile(TRI, "vsMain", MEL_SLANG_STAGE_VERTEX, MEL_SLANG_TARGET_WGSL);
    if (!b.data)
        printf("slang diag: %s\n", b.diagnostics ? b.diagnostics : "(none)");
    MEL_REQUIRE(b.data != NULL);
    MEL_EXPECT(b.size > 0);
    MEL_EXPECT(((const char*)b.data)[b.size] == 0);
    MEL_EXPECT(strstr((const char*)b.data, "fn ") != NULL);
    MEL_EXPECT(strstr((const char*)b.data, "@vertex") != NULL || strstr((const char*)b.data, "vsMain") != NULL);
    printf("WGSL UNVERIFIED by tint/naga (not in toolchain); structural well-formedness only\n");
    mel_slang_blob_free(&b);
}

MEL_TEST(slang_compile, compute_to_wgsl)
{
    Mel_Slang_Blob b = mel_slang_compile(COMP, "csMain", MEL_SLANG_STAGE_COMPUTE, MEL_SLANG_TARGET_WGSL);
    if (!b.data)
        printf("slang diag: %s\n", b.diagnostics ? b.diagnostics : "(none)");
    MEL_REQUIRE(b.data != NULL);
    MEL_EXPECT(b.size > 0);
    MEL_EXPECT(((const char*)b.data)[b.size] == 0);
    mel_slang_blob_free(&b);
}

MEL_TEST(slang_compile, dxil_loud_fails_off_win32)
{
    Mel_Slang_Blob b = mel_slang_compile(TRI, "vsMain", MEL_SLANG_STAGE_VERTEX, MEL_SLANG_TARGET_DXIL);
#if defined(MEL_SLANG_EMIT_DXIL)
    MEL_EXPECT(b.data != NULL);
#else
    MEL_EXPECT(b.data == NULL);
    MEL_REQUIRE(b.diagnostics != NULL);
    MEL_EXPECT(strstr(b.diagnostics, "DXIL unavailable off-win32") != NULL);
#endif
    mel_slang_blob_free(&b);
}

MEL_TEST(slang_reflect, vertex_inputs_and_push_constant)
{
    Mel_Slang_Reflection r;
    Mel_Slang_Blob       b = mel_slang_compile_reflect(TRI, "vsMain", MEL_SLANG_STAGE_VERTEX, MEL_SLANG_TARGET_SPIRV, &r);
    if (!b.data)
        printf("slang diag: %s\n", b.diagnostics ? b.diagnostics : "(none)");
    MEL_REQUIRE(b.data != NULL);

    MEL_REQUIRE(r.entry != NULL);
    MEL_EXPECT_STR_EQ(r.entry, "vsMain");
    MEL_EXPECT_EQ((int)r.stage, MEL_SLANG_STAGE_VERTEX);
    MEL_EXPECT(r.is_compute == 0);

    MEL_REQUIRE_EQ(r.vertex_attr_count, 3u);

    Mel_Slang_Vertex_Attr* pos = &r.vertex_attrs[0];
    Mel_Slang_Vertex_Attr* col = &r.vertex_attrs[1];
    Mel_Slang_Vertex_Attr* uv  = &r.vertex_attrs[2];
    MEL_EXPECT_EQ((int)pos->format, MEL_SLANG_FORMAT_F32X3);
    MEL_EXPECT_EQ((int)col->format, MEL_SLANG_FORMAT_F32X3);
    MEL_EXPECT_EQ((int)uv->format, MEL_SLANG_FORMAT_F32X2);
    MEL_EXPECT_EQ(pos->offset, 0u);
    MEL_EXPECT_EQ(col->offset, 12u);
    MEL_EXPECT_EQ(uv->offset, 24u);
    MEL_EXPECT_EQ(r.vertex_stride, 32u);
    if (pos->semantic)
        MEL_EXPECT_STR_EQ(pos->semantic, "POSITION");

    MEL_EXPECT_EQ(r.push_constant_size, 80u);

    mel_slang_blob_free(&b);
    mel_slang_reflection_free(&r);
}

MEL_TEST(slang_reflect, fragment_resource_bindings)
{
    Mel_Slang_Reflection r;
    Mel_Slang_Blob       b = mel_slang_compile_reflect(TRI, "fsMain", MEL_SLANG_STAGE_FRAGMENT, MEL_SLANG_TARGET_SPIRV, &r);
    if (!b.data)
        printf("slang diag: %s\n", b.diagnostics ? b.diagnostics : "(none)");
    MEL_REQUIRE(b.data != NULL);

    int tex = 0, samp = 0;
    for (uint32_t i = 0; i < r.binding_count; ++i)
    {
        if (r.bindings[i].kind == MEL_SLANG_RESOURCE_SAMPLED_TEXTURE)
            tex++;
        if (r.bindings[i].kind == MEL_SLANG_RESOURCE_SAMPLER)
            samp++;
    }
    MEL_EXPECT_EQ(tex, 1);
    MEL_EXPECT_EQ(samp, 1);

    mel_slang_blob_free(&b);
    mel_slang_reflection_free(&r);
}

MEL_TEST(slang_reflect, compute_numthreads)
{
    Mel_Slang_Reflection r;
    Mel_Slang_Blob       b = mel_slang_compile_reflect(COMP, "csMain", MEL_SLANG_STAGE_COMPUTE, MEL_SLANG_TARGET_SPIRV, &r);
    if (!b.data)
        printf("slang diag: %s\n", b.diagnostics ? b.diagnostics : "(none)");
    MEL_REQUIRE(b.data != NULL);

    MEL_EXPECT(r.is_compute != 0);
    MEL_EXPECT_EQ((int)r.stage, MEL_SLANG_STAGE_COMPUTE);
    MEL_EXPECT_EQ(r.workgroup[0], 8u);
    MEL_EXPECT_EQ(r.workgroup[1], 4u);
    MEL_EXPECT_EQ(r.workgroup[2], 2u);

    int storage = 0;
    for (uint32_t i = 0; i < r.binding_count; ++i)
        if (r.bindings[i].kind == MEL_SLANG_RESOURCE_STORAGE_BUFFER)
            storage++;
    MEL_EXPECT_EQ(storage, 1);

    mel_slang_blob_free(&b);
    mel_slang_reflection_free(&r);
}

MEL_TEST(slang_compile, bad_source_reports_diagnostics)
{
    Mel_Slang_Blob b = mel_slang_compile("this is not valid slang", "nope", MEL_SLANG_STAGE_VERTEX, MEL_SLANG_TARGET_MSL);
    MEL_EXPECT(b.data == NULL);
    MEL_EXPECT(b.diagnostics != NULL);
    mel_slang_blob_free(&b);
}
