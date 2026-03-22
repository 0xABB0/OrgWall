#include "sponza.loader.h"

#include <SDL3/SDL.h>
#include <cjson/cJSON.h>
#include <math.h>
#include <string.h>

#include "allocator.h"
#include "log.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "string.str8.h"
#include "texture.pool.h"
#include "vfs.h"

#define SPONZA_MATERIAL_USE_BASE_COLOR_TEXTURE  (1u << 0)
#define SPONZA_MATERIAL_ALPHA_MASK              (1u << 1)
#define SPONZA_MATERIAL_USE_NORMAL_TEXTURE      (1u << 2)
#define SPONZA_MATERIAL_USE_METAL_ROUGH_TEXTURE (1u << 3)

typedef struct {
    f32 px, py, pz, _pad0;
    f32 nx, ny, nz, _pad1;
    f32 r, g, b, a;
    f32 u, v, _pad2, _pad3;
    f32 tx, ty, tz, tw;
} Sponza_Vertex;

typedef struct {
    Mel_Vec4 base_color;
    u32 base_color_texture_idx;
    u32 normal_texture_idx;
    u32 metallic_roughness_texture_idx;
    u32 flags;
    f32 alpha_cutoff;
    f32 normal_scale;
    f32 metallic_factor;
    f32 roughness_factor;
} Forward_Lit_Params;

typedef struct {
    cJSON* root;
    str8 resolved_gltf_path;
    str8 gltf_dir;
} Sponza_Gltf;

static str8 path_join2(str8 a, str8 b, const Mel_Alloc* alloc)
{
    str8 out = {0};
    out.len = a.len + 1 + b.len;
    out.data = mel_alloc(alloc, (usize)out.len + 1);
    memcpy(out.data, a.data, (usize)a.len);
    out.data[a.len] = '/';
    memcpy(out.data + a.len + 1, b.data, (usize)b.len);
    out.data[out.len] = 0;
    return out;
}

static bool read_file_candidates(const Mel_Alloc* alloc,
                                 str8 primary,
                                 str8 secondary,
                                 u8** out_data,
                                 i64* out_size,
                                 str8* out_path)
{
    *out_data = mel_vfs_read_file(primary, out_size, alloc);
    if (*out_data != nullptr)
    {
        if (out_path) *out_path = str8_dup(primary, alloc);
        return true;
    }

    *out_data = mel_vfs_read_file(secondary, out_size, alloc);
    if (*out_data != nullptr)
    {
        if (out_path) *out_path = str8_dup(secondary, alloc);
        return true;
    }

    if (out_path) *out_path = (str8){0};
    return false;
}

static bool read_file_candidates4(const Mel_Alloc* alloc,
                                  str8 p0, str8 p1, str8 p2, str8 p3,
                                  u8** out_data,
                                  i64* out_size,
                                  str8* out_path)
{
    if (read_file_candidates(alloc, p0, p1, out_data, out_size, out_path))
        return true;
    return read_file_candidates(alloc, p2, p3, out_data, out_size, out_path);
}

static cJSON* json_obj(cJSON* obj, const char* key)
{
    return cJSON_GetObjectItem(obj, key);
}

static int json_int(cJSON* obj, const char* key, int fallback)
{
    cJSON* item = json_obj(obj, key);
    return item ? item->valueint : fallback;
}

static double json_num(cJSON* obj, const char* key, double fallback)
{
    cJSON* item = json_obj(obj, key);
    return item ? item->valuedouble : fallback;
}

static bool sponza_gltf_material_uses_textures(cJSON* material)
{
    if (material == nullptr)
        return false;

    cJSON* pbr = json_obj(material, "pbrMetallicRoughness");
    return (pbr && json_obj(pbr, "baseColorTexture")) ||
           (pbr && json_obj(pbr, "metallicRoughnessTexture")) ||
           json_obj(material, "normalTexture") != nullptr;
}

static bool sponza_gltf_validate_supported_usage(const Sponza_Gltf* gltf)
{
    assert(gltf != nullptr);
    cJSON* root = gltf->root;
    assert(root != nullptr);

    cJSON* extensions_used = json_obj(root, "extensionsUsed");
    cJSON* extensions_required = json_obj(root, "extensionsRequired");
    cJSON* scenes = json_obj(root, "scenes");
    cJSON* meshes = json_obj(root, "meshes");
    cJSON* nodes = json_obj(root, "nodes");
    cJSON* materials = json_obj(root, "materials");

    if ((extensions_used && cJSON_GetArraySize(extensions_used) > 0) ||
        (extensions_required && cJSON_GetArraySize(extensions_required) > 0))
    {
        mel_log_error("demo.sponza", "unsupported glTF extensions in Sponza asset");
        return false;
    }

    if (!scenes || cJSON_GetArraySize(scenes) != 1)
    {
        mel_log_error("demo.sponza", "Sponza loader expects exactly one glTF scene");
        return false;
    }
    if (!meshes || cJSON_GetArraySize(meshes) != 1)
    {
        mel_log_error("demo.sponza", "Sponza loader expects exactly one glTF mesh");
        return false;
    }
    if (!nodes || cJSON_GetArraySize(nodes) < 1)
    {
        mel_log_error("demo.sponza", "Sponza loader expects at least one glTF node");
        return false;
    }

    cJSON* scene0 = cJSON_GetArrayItem(scenes, 0);
    cJSON* scene_nodes = scene0 ? json_obj(scene0, "nodes") : nullptr;
    if (!scene_nodes || !cJSON_IsArray(scene_nodes) || cJSON_GetArraySize(scene_nodes) != 1)
    {
        mel_log_error("demo.sponza", "Sponza loader expects exactly one root node");
        return false;
    }
    cJSON* root_node_index = cJSON_GetArrayItem(scene_nodes, 0);
    if (!root_node_index || root_node_index->valueint != 0)
    {
        mel_log_error("demo.sponza", "Sponza loader expects root node 0");
        return false;
    }

    cJSON* node0 = cJSON_GetArrayItem(nodes, 0);
    if (json_obj(node0, "children") != nullptr)
    {
        mel_log_error("demo.sponza", "Sponza loader does not support nested node hierarchies yet");
        return false;
    }
    if (json_int(node0, "mesh", -1) != 0)
    {
        mel_log_error("demo.sponza", "Sponza loader expects node 0 to reference mesh 0");
        return false;
    }

    if (materials == nullptr)
    {
        mel_log_error("demo.sponza", "Sponza loader requires a material array");
        return false;
    }

    for (u32 i = 0; i < (u32)cJSON_GetArraySize(materials); i++)
    {
        cJSON* material = cJSON_GetArrayItem(materials, (int)i);
        cJSON* pbr = material ? json_obj(material, "pbrMetallicRoughness") : nullptr;
        cJSON* alpha_mode = material ? json_obj(material, "alphaMode") : nullptr;
        cJSON* emissive_texture = material ? json_obj(material, "emissiveTexture") : nullptr;
        cJSON* occlusion_texture = material ? json_obj(material, "occlusionTexture") : nullptr;
        cJSON* emissive_factor = material ? json_obj(material, "emissiveFactor") : nullptr;

        if (occlusion_texture != nullptr)
        {
            mel_log_error("demo.sponza", "material %u uses occlusionTexture, which this Sponza loader does not support", i);
            return false;
        }
        if (emissive_texture != nullptr)
        {
            mel_log_error("demo.sponza", "material %u uses emissiveTexture, which this Sponza loader does not support", i);
            return false;
        }
        if (emissive_factor && cJSON_IsArray(emissive_factor) && cJSON_GetArraySize(emissive_factor) >= 3)
        {
            f32 ex = (f32)cJSON_GetArrayItem(emissive_factor, 0)->valuedouble;
            f32 ey = (f32)cJSON_GetArrayItem(emissive_factor, 1)->valuedouble;
            f32 ez = (f32)cJSON_GetArrayItem(emissive_factor, 2)->valuedouble;
            if (fabsf(ex) > 0.0001f || fabsf(ey) > 0.0001f || fabsf(ez) > 0.0001f)
            {
                mel_log_error("demo.sponza", "material %u uses emissiveFactor, which this Sponza loader does not support", i);
                return false;
            }
        }
        if (alpha_mode && cJSON_IsString(alpha_mode))
        {
            if (strcmp(alpha_mode->valuestring, "OPAQUE") != 0 &&
                strcmp(alpha_mode->valuestring, "MASK") != 0)
            {
                mel_log_error("demo.sponza", "material %u uses unsupported alphaMode %s", i, alpha_mode->valuestring);
                return false;
            }
        }

        if (pbr == nullptr)
        {
            mel_log_error("demo.sponza", "material %u is missing pbrMetallicRoughness", i);
            return false;
        }
    }

    cJSON* mesh0 = cJSON_GetArrayItem(meshes, 0);
    cJSON* primitives = mesh0 ? json_obj(mesh0, "primitives") : nullptr;
    if (!primitives || !cJSON_IsArray(primitives))
    {
        mel_log_error("demo.sponza", "Sponza loader requires mesh 0 primitives");
        return false;
    }

    for (u32 i = 0; i < (u32)cJSON_GetArraySize(primitives); i++)
    {
        cJSON* primitive = cJSON_GetArrayItem(primitives, (int)i);
        cJSON* attrs = primitive ? json_obj(primitive, "attributes") : nullptr;
        if (attrs == nullptr)
        {
            mel_log_error("demo.sponza", "primitive %u is missing attributes", i);
            return false;
        }
        if (json_obj(attrs, "POSITION") == nullptr || json_obj(attrs, "NORMAL") == nullptr)
        {
            mel_log_error("demo.sponza", "primitive %u is missing POSITION or NORMAL", i);
            return false;
        }
        if (json_obj(primitive, "indices") == nullptr)
        {
            mel_log_error("demo.sponza", "primitive %u is missing indices", i);
            return false;
        }

        for (cJSON* child = attrs->child; child != nullptr; child = child->next)
        {
            const char* name = child->string;
            if (name == nullptr)
                continue;
            if (strcmp(name, "POSITION") == 0) continue;
            if (strcmp(name, "NORMAL") == 0) continue;
            if (strcmp(name, "TANGENT") == 0) continue;
            if (strcmp(name, "TEXCOORD_0") == 0) continue;

            mel_log_error("demo.sponza", "primitive %u uses unsupported attribute %s", i, name);
            return false;
        }

        int material_index = json_int(primitive, "material", -1);
        if (material_index < 0 || material_index >= cJSON_GetArraySize(materials))
        {
            mel_log_error("demo.sponza", "primitive %u references invalid material %d", i, material_index);
            return false;
        }

        cJSON* material = cJSON_GetArrayItem(materials, material_index);
        if (sponza_gltf_material_uses_textures(material) && json_obj(attrs, "TEXCOORD_0") == nullptr)
        {
            mel_log_error("demo.sponza", "primitive %u uses textured material %d without TEXCOORD_0", i, material_index);
            return false;
        }
        if (json_obj(material, "normalTexture") != nullptr && json_obj(attrs, "TANGENT") == nullptr)
        {
            mel_log_error("demo.sponza", "primitive %u uses normal-mapped material %d without TANGENT", i, material_index);
            return false;
        }
    }

    return true;
}

static void json_vec4_or_default(cJSON* arr, Mel_Vec4* out, Mel_Vec4 fallback)
{
    *out = fallback;
    if (!arr || !cJSON_IsArray(arr) || cJSON_GetArraySize(arr) < 4)
        return;

    out->x = (f32)cJSON_GetArrayItem(arr, 0)->valuedouble;
    out->y = (f32)cJSON_GetArrayItem(arr, 1)->valuedouble;
    out->z = (f32)cJSON_GetArrayItem(arr, 2)->valuedouble;
    out->w = (f32)cJSON_GetArrayItem(arr, 3)->valuedouble;
}

static Mel_Vec3 transform_point3(Mel_Mat4 m, Mel_Vec3 p)
{
    return mel_vec3(
        m.m[0][0] * p.x + m.m[0][1] * p.y + m.m[0][2] * p.z + m.m[0][3],
        m.m[1][0] * p.x + m.m[1][1] * p.y + m.m[1][2] * p.z + m.m[1][3],
        m.m[2][0] * p.x + m.m[2][1] * p.y + m.m[2][2] * p.z + m.m[2][3]);
}

static Mel_Vec3 transform_extents3(Mel_Mat4 m, Mel_Vec3 e)
{
    return mel_vec3(
        fabsf(m.m[0][0]) * e.x + fabsf(m.m[0][1]) * e.y + fabsf(m.m[0][2]) * e.z,
        fabsf(m.m[1][0]) * e.x + fabsf(m.m[1][1]) * e.y + fabsf(m.m[1][2]) * e.z,
        fabsf(m.m[2][0]) * e.x + fabsf(m.m[2][1]) * e.y + fabsf(m.m[2][2]) * e.z);
}

static u32 gltf_wrap_to_mel(int wrap)
{
    switch (wrap)
    {
        case 10497: return MEL_GPU_SAMPLER_ADDRESS_REPEAT;
        case 33648: return MEL_GPU_SAMPLER_ADDRESS_MIRRORED_REPEAT;
        case 33071:
        default:    return MEL_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE;
    }
}

static bool gltf_filter_is_nearest(int filter)
{
    switch (filter)
    {
        case 9728:
        case 9984:
        case 9985: return true;
        default:   return false;
    }
}

static bool gltf_filter_uses_mips(int filter)
{
    switch (filter)
    {
        case 9984:
        case 9985:
        case 9986:
        case 9987: return true;
        default:   return false;
    }
}

static usize gltf_component_size(int component_type)
{
    switch (component_type)
    {
        case 5123: return 2;
        case 5125: return 4;
        case 5126: return 4;
        default: return 0;
    }
}

static usize gltf_type_components(const char* type)
{
    if (strcmp(type, "SCALAR") == 0) return 1;
    if (strcmp(type, "VEC2") == 0) return 2;
    if (strcmp(type, "VEC3") == 0) return 3;
    if (strcmp(type, "VEC4") == 0) return 4;
    return 0;
}

static bool gltf_read_accessor_vec3(cJSON* accessors,
                                    cJSON* buffer_views,
                                    cJSON* accessor,
                                    const u8* bin,
                                    Mel_Vec3* out,
                                    u32 expected_count)
{
    int count = json_int(accessor, "count", 0);
    int component_type = json_int(accessor, "componentType", 0);
    cJSON* type = json_obj(accessor, "type");
    cJSON* view = cJSON_GetArrayItem(buffer_views, json_int(accessor, "bufferView", -1));
    if (count != (int)expected_count || component_type != 5126 || !type || strcmp(type->valuestring, "VEC3") != 0 || !view)
        return false;

    usize elem_size = gltf_component_size(component_type) * gltf_type_components(type->valuestring);
    usize stride = (usize)json_int(view, "byteStride", (int)elem_size);
    usize base = (usize)json_int(view, "byteOffset", 0) + (usize)json_int(accessor, "byteOffset", 0);

    for (u32 i = 0; i < expected_count; i++)
    {
        f32 v[3];
        memcpy(v, bin + base + (usize)i * stride, sizeof(v));
        out[i] = mel_vec3(v[0], v[1], v[2]);
    }

    return true;
}

static bool gltf_read_accessor_vec2(cJSON* accessors,
                                    cJSON* buffer_views,
                                    cJSON* accessor,
                                    const u8* bin,
                                    Mel_Vec2* out,
                                    u32 expected_count)
{
    int count = json_int(accessor, "count", 0);
    int component_type = json_int(accessor, "componentType", 0);
    cJSON* type = json_obj(accessor, "type");
    cJSON* view = cJSON_GetArrayItem(buffer_views, json_int(accessor, "bufferView", -1));
    if (count != (int)expected_count || component_type != 5126 || !type || strcmp(type->valuestring, "VEC2") != 0 || !view)
        return false;

    usize elem_size = gltf_component_size(component_type) * gltf_type_components(type->valuestring);
    usize stride = (usize)json_int(view, "byteStride", (int)elem_size);
    usize base = (usize)json_int(view, "byteOffset", 0) + (usize)json_int(accessor, "byteOffset", 0);

    for (u32 i = 0; i < expected_count; i++)
    {
        f32 v[2];
        memcpy(v, bin + base + (usize)i * stride, sizeof(v));
        out[i] = mel_vec2(v[0], v[1]);
    }

    return true;
}

static bool gltf_read_accessor_vec4(cJSON* accessors,
                                    cJSON* buffer_views,
                                    cJSON* accessor,
                                    const u8* bin,
                                    Mel_Vec4* out,
                                    u32 expected_count)
{
    int count = json_int(accessor, "count", 0);
    int component_type = json_int(accessor, "componentType", 0);
    cJSON* type = json_obj(accessor, "type");
    cJSON* view = cJSON_GetArrayItem(buffer_views, json_int(accessor, "bufferView", -1));
    if (count != (int)expected_count || component_type != 5126 || !type || strcmp(type->valuestring, "VEC4") != 0 || !view)
        return false;

    usize elem_size = gltf_component_size(component_type) * gltf_type_components(type->valuestring);
    usize stride = (usize)json_int(view, "byteStride", (int)elem_size);
    usize base = (usize)json_int(view, "byteOffset", 0) + (usize)json_int(accessor, "byteOffset", 0);

    for (u32 i = 0; i < expected_count; i++)
    {
        f32 v[4];
        memcpy(v, bin + base + (usize)i * stride, sizeof(v));
        out[i] = mel_vec4(v[0], v[1], v[2], v[3]);
    }

    return true;
}

static bool gltf_read_accessor_indices(cJSON* buffer_views,
                                       cJSON* accessor,
                                       const u8* bin,
                                       u32* out,
                                       u32 expected_count)
{
    int count = json_int(accessor, "count", 0);
    int component_type = json_int(accessor, "componentType", 0);
    cJSON* type = json_obj(accessor, "type");
    cJSON* view = cJSON_GetArrayItem(buffer_views, json_int(accessor, "bufferView", -1));
    if (count != (int)expected_count || !type || strcmp(type->valuestring, "SCALAR") != 0 || !view)
        return false;

    usize elem_size = gltf_component_size(component_type);
    usize stride = (usize)json_int(view, "byteStride", (int)elem_size);
    usize base = (usize)json_int(view, "byteOffset", 0) + (usize)json_int(accessor, "byteOffset", 0);

    if (component_type == 5123)
    {
        for (u32 i = 0; i < expected_count; i++)
        {
            u16 idx = 0;
            memcpy(&idx, bin + base + (usize)i * stride, sizeof(idx));
            out[i] = idx;
        }
        return true;
    }

    if (component_type == 5125)
    {
        for (u32 i = 0; i < expected_count; i++)
            memcpy(&out[i], bin + base + (usize)i * stride, sizeof(u32));
        return true;
    }

    return false;
}

static u32 sponza_load_texture_idx(cJSON* textures,
                                   cJSON* images,
                                   cJSON* samplers,
                                   cJSON* texture_ref,
                                   str8 gltf_dir,
                                   const Mel_Alloc* alloc,
                                   const char* label,
                                   u32 fallback_idx,
                                   u32 format)
{
    Mel_Texture_Pool* texture_pool = mel_texture_pool();
    if (texture_ref == nullptr)
        return fallback_idx;
    if (texture_pool == nullptr)
    {
        mel_log_warn("demo.sponza", "texture pool unavailable for %s, using fallback", label);
        return fallback_idx;
    }

    cJSON* texture_entry = cJSON_GetArrayItem(textures, json_int(texture_ref, "index", -1));
    if (texture_entry == nullptr)
    {
        mel_log_warn("demo.sponza", "missing texture entry for %s, using fallback", label);
        return fallback_idx;
    }

    cJSON* image_entry = cJSON_GetArrayItem(images, json_int(texture_entry, "source", -1));
    if (image_entry == nullptr)
    {
        mel_log_warn("demo.sponza", "missing image entry for %s, using fallback", label);
        return fallback_idx;
    }

    cJSON* image_uri = json_obj(image_entry, "uri");
    if (image_uri == nullptr || !cJSON_IsString(image_uri))
    {
        mel_log_warn("demo.sponza", "missing image uri for %s, using fallback", label);
        return fallback_idx;
    }

    u32 address_mode_u = MEL_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE;
    u32 address_mode_v = MEL_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE;
    u32 address_mode_w = MEL_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE;
    bool nearest_filter = false;
    bool generate_mips = false;
    cJSON* sampler_entry = cJSON_GetArrayItem(samplers, json_int(texture_entry, "sampler", -1));
    if (sampler_entry != nullptr)
    {
        address_mode_u = gltf_wrap_to_mel(json_int(sampler_entry, "wrapS", 10497));
        address_mode_v = gltf_wrap_to_mel(json_int(sampler_entry, "wrapT", 10497));
        address_mode_w = address_mode_v;
        nearest_filter = gltf_filter_is_nearest(json_int(sampler_entry, "magFilter", 9729)) ||
                         gltf_filter_is_nearest(json_int(sampler_entry, "minFilter", 9987));
        generate_mips = gltf_filter_uses_mips(json_int(sampler_entry, "minFilter", 9987));
    }

    str8 image_path = path_join2(gltf_dir, str8_from_cstr(image_uri->valuestring), alloc);
    Mel_Texture_Handle tex_handle = mel_texture_pool_load(texture_pool, image_path,
        .format = format,
        .nearest_filter = nearest_filter,
        .generate_mips = generate_mips,
        .address_mode_u = address_mode_u,
        .address_mode_v = address_mode_v,
        .address_mode_w = address_mode_w);
    Mel_Gpu_Texture* gpu_tex = mel_texture_pool_get(texture_pool, tex_handle);
    if (gpu_tex == nullptr)
    {
        mel_log_warn("demo.sponza", "failed to load texture %.*s for %s, using fallback",
            (int)image_path.len, image_path.data, label);
        mel_dealloc(alloc, image_path.data);
        return fallback_idx;
    }

    u32 table_idx = mel_texture_pool_add_to_table(gpu_tex);
    mel_dealloc(alloc, image_path.data);
    return table_idx;
}

static bool sponza_gltf_open(Sponza_Gltf* out, const Mel_Alloc* alloc)
{
    assert(out != nullptr);
    assert(alloc != nullptr);

    *out = (Sponza_Gltf){0};

    str8 gltf_path = S8("assets/sponza/khronos/glTF/Sponza.gltf");
    str8 gltf_path_alt = S8("../assets/sponza/khronos/glTF/Sponza.gltf");
    str8 gltf_path_base = {0};
    str8 gltf_path_base_alt = {0};
    const char* base_path_c = SDL_GetBasePath();
    if (base_path_c != nullptr)
    {
        str8 base_path = str8_from_cstr(base_path_c);
        gltf_path_base = path_join2(base_path, S8("../assets/sponza/khronos/glTF/Sponza.gltf"), alloc);
        gltf_path_base_alt = path_join2(base_path, S8("assets/sponza/khronos/glTF/Sponza.gltf"), alloc);
    }

    i64 gltf_size = 0;
    u8* gltf_json = nullptr;
    if (!read_file_candidates4(alloc,
            gltf_path, gltf_path_alt,
            gltf_path_base, gltf_path_base_alt,
            &gltf_json, &gltf_size, &out->resolved_gltf_path))
    {
        mel_log_error("demo.sponza", "failed to read glTF at %.*s or %.*s",
            (int)gltf_path.len, gltf_path.data, (int)gltf_path_alt.len, gltf_path_alt.data);
        if (gltf_path_base.data) mel_dealloc(alloc, gltf_path_base.data);
        if (gltf_path_base_alt.data) mel_dealloc(alloc, gltf_path_base_alt.data);
        return false;
    }
    if (gltf_path_base.data) mel_dealloc(alloc, gltf_path_base.data);
    if (gltf_path_base_alt.data) mel_dealloc(alloc, gltf_path_base_alt.data);

    out->gltf_dir = out->resolved_gltf_path;
    while (out->gltf_dir.len > 0 && out->gltf_dir.data[out->gltf_dir.len - 1] != '/')
        out->gltf_dir.len--;
    if (out->gltf_dir.len > 0)
        out->gltf_dir.len--;

    out->root = cJSON_Parse((const char*)gltf_json);
    mel_dealloc(alloc, gltf_json);
    if (out->root == nullptr)
    {
        mel_log_error("demo.sponza", "failed to parse glTF JSON");
        mel_dealloc(alloc, out->resolved_gltf_path.data);
        *out = (Sponza_Gltf){0};
        return false;
    }

    return true;
}

static void sponza_gltf_close(Sponza_Gltf* gltf, const Mel_Alloc* alloc)
{
    assert(gltf != nullptr);
    assert(alloc != nullptr);

    if (gltf->resolved_gltf_path.data != nullptr)
        mel_dealloc(alloc, gltf->resolved_gltf_path.data);
    if (gltf->root != nullptr)
        cJSON_Delete(gltf->root);
    *gltf = (Sponza_Gltf){0};
}

usize sponza_vertex_stride(void)
{
    return sizeof(Sponza_Vertex);
}

Mel_Material_Base_Id sponza_ensure_forward_lit_material_base(void)
{
    Mel_Material_Base_Id forward_lit_id = mel_material_base_find(S8("forward_lit"));
    if (forward_lit_id == MEL_MATERIAL_BASE_ID_INVALID)
    {
        forward_lit_id = mel_material_base_register(&(Mel_Material_Base_Desc){
            .name = S8("forward_lit"),
            .param_size = sizeof(Forward_Lit_Params),
            .compat = MEL_COMPAT_FORWARD,
        });
    }
    return forward_lit_id;
}

bool sponza_scan(Sponza_Scan_Result* out, const Mel_Alloc* alloc)
{
    assert(out != nullptr);
    assert(alloc != nullptr);

    *out = (Sponza_Scan_Result){0};

    Sponza_Gltf gltf = {0};
    if (!sponza_gltf_open(&gltf, alloc))
        return false;
    if (!sponza_gltf_validate_supported_usage(&gltf))
    {
        sponza_gltf_close(&gltf, alloc);
        return false;
    }

    cJSON* root = gltf.root;
    cJSON* materials = json_obj(root, "materials");
    cJSON* meshes = json_obj(root, "meshes");
    cJSON* accessors = json_obj(root, "accessors");
    cJSON* mesh = cJSON_GetArrayItem(meshes, 0);
    cJSON* primitives = mesh ? json_obj(mesh, "primitives") : nullptr;
    if (!materials || !primitives || !accessors)
    {
        mel_log_error("demo.sponza", "missing required glTF arrays during scan");
        sponza_gltf_close(&gltf, alloc);
        return false;
    }

    out->material_count = (u32)cJSON_GetArraySize(materials);
    out->primitive_count = (u32)cJSON_GetArraySize(primitives);

    for (u32 i = 0; i < out->primitive_count; i++)
    {
        cJSON* primitive = cJSON_GetArrayItem(primitives, (int)i);
        cJSON* attrs = primitive ? json_obj(primitive, "attributes") : nullptr;
        cJSON* position_index = attrs ? json_obj(attrs, "POSITION") : nullptr;
        cJSON* index_index = primitive ? json_obj(primitive, "indices") : nullptr;
        cJSON* pos_accessor = position_index ? cJSON_GetArrayItem(accessors, position_index->valueint) : nullptr;
        cJSON* idx_accessor = index_index ? cJSON_GetArrayItem(accessors, index_index->valueint) : nullptr;
        if (!pos_accessor || !idx_accessor)
        {
            mel_log_error("demo.sponza", "primitive %u missing POSITION or indices during scan", i);
            sponza_gltf_close(&gltf, alloc);
            return false;
        }

        out->vertex_count += (u32)json_int(pos_accessor, "count", 0);
        out->index_count += (u32)json_int(idx_accessor, "count", 0);
    }

    sponza_gltf_close(&gltf, alloc);
    return true;
}

bool sponza_load(Sponza_Load_Result* out,
                 Mel_Geometry_Pool* pool,
                 Mel_Material_Base_Id forward_lit_id,
                 const Mel_Alloc* alloc)
{
    *out = (Sponza_Load_Result){0};

    Sponza_Gltf gltf = {0};
    if (!sponza_gltf_open(&gltf, alloc))
        return false;
    if (!sponza_gltf_validate_supported_usage(&gltf))
    {
        sponza_gltf_close(&gltf, alloc);
        return false;
    }

    cJSON* root = gltf.root;
    str8 gltf_dir = gltf.gltf_dir;

    cJSON* buffers = json_obj(root, "buffers");
    cJSON* buffer0 = cJSON_GetArrayItem(buffers, 0);
    cJSON* buffer_uri = buffer0 ? json_obj(buffer0, "uri") : nullptr;
    if (!buffer_uri || !cJSON_IsString(buffer_uri))
    {
        mel_log_error("demo.sponza", "missing buffer URI");
        sponza_gltf_close(&gltf, alloc);
        return false;
    }

    str8 bin_path = path_join2(gltf_dir, str8_from_cstr(buffer_uri->valuestring), alloc);
    i64 bin_size = 0;
    u8* bin = mel_vfs_read_file(bin_path, &bin_size, alloc);
    mel_dealloc(alloc, bin_path.data);
    if (bin == nullptr)
    {
        mel_log_error("demo.sponza", "failed to read binary buffer");
        sponza_gltf_close(&gltf, alloc);
        return false;
    }

    cJSON* materials = json_obj(root, "materials");
    cJSON* textures = json_obj(root, "textures");
    cJSON* images = json_obj(root, "images");
    cJSON* samplers = json_obj(root, "samplers");
    cJSON* meshes = json_obj(root, "meshes");
    cJSON* nodes = json_obj(root, "nodes");
    cJSON* accessors = json_obj(root, "accessors");
    cJSON* buffer_views = json_obj(root, "bufferViews");
    cJSON* mesh = cJSON_GetArrayItem(meshes, 0);
    cJSON* primitives = mesh ? json_obj(mesh, "primitives") : nullptr;
    cJSON* node0 = cJSON_GetArrayItem(nodes, 0);
    if (!materials || !primitives || !accessors || !buffer_views || !node0)
    {
        mel_log_error("demo.sponza", "missing required glTF arrays");
        sponza_gltf_close(&gltf, alloc);
        mel_dealloc(alloc, bin);
        return false;
    }

    u32 material_count = (u32)cJSON_GetArraySize(materials);
    u32 primitive_count = (u32)cJSON_GetArraySize(primitives);
    Mel_Render_Material_Binding* bindings = mel_alloc(alloc, (usize)material_count * sizeof(Mel_Render_Material_Binding));
    Mel_Render_Mesh_Part* parts = mel_alloc(alloc, (usize)primitive_count * sizeof(Mel_Render_Mesh_Part));
    Mel_Texture_Pool* texture_pool = mel_texture_pool();
    u32 white_texture_idx = texture_pool ? texture_pool->white_table_idx : 0;
    u32 normal_texture_idx_default = white_texture_idx;
    u32 metal_rough_texture_idx_default = white_texture_idx;

    for (u32 i = 0; i < material_count; i++)
    {
        cJSON* material = cJSON_GetArrayItem(materials, (int)i);
        cJSON* pbr = material ? json_obj(material, "pbrMetallicRoughness") : nullptr;
        Mel_Vec4 base_color = mel_vec4(1, 1, 1, 1);
        u32 base_color_texture_idx = white_texture_idx;
        u32 normal_texture_idx = normal_texture_idx_default;
        u32 metallic_roughness_texture_idx = metal_rough_texture_idx_default;
        u32 flags = 0;
        f32 alpha_cutoff = 0.5f;
        f32 normal_scale = 1.0f;
        f32 metallic_factor = (f32)json_num(pbr, "metallicFactor", 1.0);
        f32 roughness_factor = (f32)json_num(pbr, "roughnessFactor", 1.0);
        u32 cull_mode = MEL_GPU_CULL_BACK;
        u32 blend_mode = MEL_GPU_BLEND_NONE;
        json_vec4_or_default(pbr ? json_obj(pbr, "baseColorFactor") : nullptr, &base_color, base_color);

        cJSON* base_color_tex = pbr ? json_obj(pbr, "baseColorTexture") : nullptr;
        if (base_color_tex && textures && images)
        {
            u32 loaded_idx = sponza_load_texture_idx(textures, images, samplers, base_color_tex, gltf_dir, alloc,
                "baseColorTexture", white_texture_idx, MEL_GPU_FORMAT_R8G8B8A8_SRGB);
            if (loaded_idx != white_texture_idx)
            {
                base_color_texture_idx = loaded_idx;
                flags |= SPONZA_MATERIAL_USE_BASE_COLOR_TEXTURE;
            }
        }

        cJSON* normal_tex = material ? json_obj(material, "normalTexture") : nullptr;
        if (normal_tex && textures && images)
        {
            u32 loaded_idx = sponza_load_texture_idx(textures, images, samplers, normal_tex, gltf_dir, alloc,
                "normalTexture", normal_texture_idx_default, MEL_GPU_FORMAT_R8G8B8A8_UNORM);
            if (loaded_idx != normal_texture_idx_default)
            {
                normal_texture_idx = loaded_idx;
                normal_scale = (f32)json_num(normal_tex, "scale", 1.0);
                flags |= SPONZA_MATERIAL_USE_NORMAL_TEXTURE;
            }
        }

        cJSON* metal_rough_tex = pbr ? json_obj(pbr, "metallicRoughnessTexture") : nullptr;
        if (metal_rough_tex && textures && images)
        {
            u32 loaded_idx = sponza_load_texture_idx(textures, images, samplers, metal_rough_tex, gltf_dir, alloc,
                "metallicRoughnessTexture", metal_rough_texture_idx_default, MEL_GPU_FORMAT_R8G8B8A8_UNORM);
            if (loaded_idx != metal_rough_texture_idx_default)
            {
                metallic_roughness_texture_idx = loaded_idx;
                flags |= SPONZA_MATERIAL_USE_METAL_ROUGH_TEXTURE;
            }
        }

        cJSON* alpha_mode = material ? json_obj(material, "alphaMode") : nullptr;
        if (alpha_mode && cJSON_IsString(alpha_mode) && strcmp(alpha_mode->valuestring, "MASK") == 0)
        {
            flags |= SPONZA_MATERIAL_ALPHA_MASK;
            alpha_cutoff = (f32)json_num(material, "alphaCutoff", 0.5);
        }
        else if (alpha_mode && cJSON_IsString(alpha_mode) && strcmp(alpha_mode->valuestring, "BLEND") == 0)
        {
            blend_mode = MEL_GPU_BLEND_ALPHA;
        }

        cJSON* double_sided = material ? json_obj(material, "doubleSided") : nullptr;
        if (double_sided && cJSON_IsBool(double_sided) && cJSON_IsTrue(double_sided))
            cull_mode = MEL_GPU_CULL_NONE;

        Forward_Lit_Params params = {
            .base_color = base_color,
            .base_color_texture_idx = base_color_texture_idx,
            .normal_texture_idx = normal_texture_idx,
            .metallic_roughness_texture_idx = metallic_roughness_texture_idx,
            .flags = flags,
            .alpha_cutoff = alpha_cutoff,
            .normal_scale = normal_scale,
            .metallic_factor = metallic_factor,
            .roughness_factor = roughness_factor,
        };
        Mel_Material_Instance_Id mat_inst = mel_material_base_alloc_instance(forward_lit_id, &params);
        mel_material_base_set_cull_mode(forward_lit_id, mat_inst, cull_mode);
        mel_material_base_set_blend_mode(forward_lit_id, mat_inst, blend_mode);
        bindings[i] = (Mel_Render_Material_Binding){
            .slot = i,
            .material_base_id = forward_lit_id,
            .material_idx = mat_inst,
            .flags = 0,
        };
    }

    Mel_Vec3 bmin = mel_vec3(1e30f, 1e30f, 1e30f);
    Mel_Vec3 bmax = mel_vec3(-1e30f, -1e30f, -1e30f);

    for (u32 i = 0; i < primitive_count; i++)
    {
        cJSON* primitive = cJSON_GetArrayItem(primitives, (int)i);
        cJSON* attrs = primitive ? json_obj(primitive, "attributes") : nullptr;
        cJSON* position_index = attrs ? json_obj(attrs, "POSITION") : nullptr;
        cJSON* normal_index = attrs ? json_obj(attrs, "NORMAL") : nullptr;
        cJSON* tangent_index = attrs ? json_obj(attrs, "TANGENT") : nullptr;
        cJSON* uv_index = attrs ? json_obj(attrs, "TEXCOORD_0") : nullptr;
        cJSON* index_index = primitive ? json_obj(primitive, "indices") : nullptr;
        if (!position_index || !normal_index || !index_index)
            continue;

        cJSON* pos_accessor = cJSON_GetArrayItem(accessors, position_index->valueint);
        cJSON* nrm_accessor = cJSON_GetArrayItem(accessors, normal_index->valueint);
        cJSON* tan_accessor = tangent_index ? cJSON_GetArrayItem(accessors, tangent_index->valueint) : nullptr;
        cJSON* uv_accessor = uv_index ? cJSON_GetArrayItem(accessors, uv_index->valueint) : nullptr;
        cJSON* idx_accessor = cJSON_GetArrayItem(accessors, index_index->valueint);
        u32 vertex_count = (u32)json_int(pos_accessor, "count", 0);
        u32 index_count = (u32)json_int(idx_accessor, "count", 0);
        Sponza_Vertex* vertices = mel_alloc(alloc, (usize)vertex_count * sizeof(Sponza_Vertex));
        Mel_Vec3* positions = mel_alloc(alloc, (usize)vertex_count * sizeof(Mel_Vec3));
        Mel_Vec3* normals = mel_alloc(alloc, (usize)vertex_count * sizeof(Mel_Vec3));
        Mel_Vec4* tangents = tan_accessor ? mel_alloc(alloc, (usize)vertex_count * sizeof(Mel_Vec4)) : nullptr;
        Mel_Vec2* uvs = uv_accessor ? mel_alloc(alloc, (usize)vertex_count * sizeof(Mel_Vec2)) : nullptr;
        u32* indices = mel_alloc(alloc, (usize)index_count * sizeof(u32));

        if (!gltf_read_accessor_vec3(accessors, buffer_views, pos_accessor, bin, positions, vertex_count) ||
            !gltf_read_accessor_vec3(accessors, buffer_views, nrm_accessor, bin, normals, vertex_count) ||
            (tan_accessor && !gltf_read_accessor_vec4(accessors, buffer_views, tan_accessor, bin, tangents, vertex_count)) ||
            (uv_accessor && !gltf_read_accessor_vec2(accessors, buffer_views, uv_accessor, bin, uvs, vertex_count)) ||
            !gltf_read_accessor_indices(buffer_views, idx_accessor, bin, indices, index_count))
        {
            mel_log_error("demo.sponza", "failed to decode primitive %u", i);
            mel_dealloc(alloc, indices);
            if (tangents) mel_dealloc(alloc, tangents);
            if (uvs) mel_dealloc(alloc, uvs);
            mel_dealloc(alloc, normals);
            mel_dealloc(alloc, positions);
            mel_dealloc(alloc, vertices);
            mel_dealloc(alloc, parts);
            mel_dealloc(alloc, bindings);
            mel_dealloc(alloc, bin);
            sponza_gltf_close(&gltf, alloc);
            return false;
        }

        for (u32 v = 0; v < vertex_count; v++)
        {
            vertices[v] = (Sponza_Vertex){
                .px = positions[v].x, .py = positions[v].y, .pz = positions[v].z,
                .nx = normals[v].x, .ny = normals[v].y, .nz = normals[v].z,
                .r = 1, .g = 1, .b = 1, .a = 1,
                .u = uvs ? uvs[v].x : 0.0f,
                .v = uvs ? uvs[v].y : 0.0f,
                .tx = tangents ? tangents[v].x : 1.0f,
                .ty = tangents ? tangents[v].y : 0.0f,
                .tz = tangents ? tangents[v].z : 0.0f,
                .tw = tangents ? tangents[v].w : 1.0f,
            };

            if (positions[v].x < bmin.x) bmin.x = positions[v].x;
            if (positions[v].y < bmin.y) bmin.y = positions[v].y;
            if (positions[v].z < bmin.z) bmin.z = positions[v].z;
            if (positions[v].x > bmax.x) bmax.x = positions[v].x;
            if (positions[v].y > bmax.y) bmax.y = positions[v].y;
            if (positions[v].z > bmax.z) bmax.z = positions[v].z;
        }

        Mel_Geometry_Upload upload = {
            .vertices = vertices,
            .vertex_count = vertex_count,
            .indices = indices,
            .index_count = index_count,
            .index_type = MEL_GPU_INDEX_TYPE_U32,
        };
        Mel_Geometry_Handle mesh_handle = mel_geometry_pool_upload(pool, &upload);
        parts[i] = (Mel_Render_Mesh_Part){
            .mesh = mesh_handle,
            .material_binding_index = (u32)json_int(primitive, "material", 0),
            .flags = 0,
        };

        mel_dealloc(alloc, indices);
        if (tangents) mel_dealloc(alloc, tangents);
        if (uvs) mel_dealloc(alloc, uvs);
        mel_dealloc(alloc, normals);
        mel_dealloc(alloc, positions);
        mel_dealloc(alloc, vertices);
    }

    Mel_Mat4 node_model = MEL_MAT4_IDENTITY;
    cJSON* node_matrix = json_obj(node0, "matrix");
    if (node_matrix && cJSON_IsArray(node_matrix) && cJSON_GetArraySize(node_matrix) >= 16)
    {
        for (i32 row = 0; row < 4; row++)
            for (i32 col = 0; col < 4; col++)
                node_model.m[row][col] = (f32)cJSON_GetArrayItem(node_matrix, row * 4 + col)->valuedouble;
    }
    else
    {
        Mel_Vec3 translation = mel_vec3(0, 0, 0);
        Mel_Vec4 rotation = mel_vec4(0, 0, 0, 1);
        Mel_Vec3 scale = mel_vec3(1, 1, 1);

        cJSON* node_translation = json_obj(node0, "translation");
        if (node_translation && cJSON_IsArray(node_translation) && cJSON_GetArraySize(node_translation) >= 3)
        {
            translation.x = (f32)cJSON_GetArrayItem(node_translation, 0)->valuedouble;
            translation.y = (f32)cJSON_GetArrayItem(node_translation, 1)->valuedouble;
            translation.z = (f32)cJSON_GetArrayItem(node_translation, 2)->valuedouble;
        }

        cJSON* node_rotation = json_obj(node0, "rotation");
        if (node_rotation && cJSON_IsArray(node_rotation) && cJSON_GetArraySize(node_rotation) >= 4)
        {
            rotation.x = (f32)cJSON_GetArrayItem(node_rotation, 0)->valuedouble;
            rotation.y = (f32)cJSON_GetArrayItem(node_rotation, 1)->valuedouble;
            rotation.z = (f32)cJSON_GetArrayItem(node_rotation, 2)->valuedouble;
            rotation.w = (f32)cJSON_GetArrayItem(node_rotation, 3)->valuedouble;
        }

        cJSON* node_scale = json_obj(node0, "scale");
        if (node_scale && cJSON_IsArray(node_scale) && cJSON_GetArraySize(node_scale) >= 3)
        {
            scale.x = (f32)cJSON_GetArrayItem(node_scale, 0)->valuedouble;
            scale.y = (f32)cJSON_GetArrayItem(node_scale, 1)->valuedouble;
            scale.z = (f32)cJSON_GetArrayItem(node_scale, 2)->valuedouble;
        }

        f32 x = rotation.x, y = rotation.y, z = rotation.z, w = rotation.w;
        Mel_Mat4 rot = MEL_MAT4_IDENTITY;
        rot.m[0][0] = 1.0f - 2.0f * (y * y + z * z);
        rot.m[0][1] = 2.0f * (x * y - z * w);
        rot.m[0][2] = 2.0f * (x * z + y * w);
        rot.m[1][0] = 2.0f * (x * y + z * w);
        rot.m[1][1] = 1.0f - 2.0f * (x * x + z * z);
        rot.m[1][2] = 2.0f * (y * z - x * w);
        rot.m[2][0] = 2.0f * (x * z - y * w);
        rot.m[2][1] = 2.0f * (y * z + x * w);
        rot.m[2][2] = 1.0f - 2.0f * (x * x + y * y);

        node_model = mel_mat4_mul(
            mel_mat4_translate(translation),
            mel_mat4_mul(rot, mel_mat4_scale(scale)));
    }

    out->parts = parts;
    out->part_count = primitive_count;
    out->bindings = bindings;
    out->binding_count = material_count;
    out->bounds = (Mel_Render_Bounds){
        .center = mel_vec3((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f, (bmin.z + bmax.z) * 0.5f),
        .extents = mel_vec3((bmax.x - bmin.x) * 0.5f, (bmax.y - bmin.y) * 0.5f, (bmax.z - bmin.z) * 0.5f),
    };
    out->model = node_model;
    out->world_center = transform_point3(node_model, out->bounds.center);
    out->world_extents = transform_extents3(node_model, out->bounds.extents);

    mel_dealloc(alloc, bin);
    sponza_gltf_close(&gltf, alloc);
    return true;
}

void sponza_load_result_free(Sponza_Load_Result* result, const Mel_Alloc* alloc)
{
    if (result->parts != nullptr)
        mel_dealloc(alloc, result->parts);
    if (result->bindings != nullptr)
        mel_dealloc(alloc, result->bindings);
    *result = (Sponza_Load_Result){0};
}
