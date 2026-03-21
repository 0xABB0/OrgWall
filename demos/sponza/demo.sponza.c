#include <SDL3/SDL.h>
#include <cjson/cJSON.h>
#include <math.h>
#include <string.h>

#include "core.app.h"
#include "core.engine.h"
#include "window.h"
#include "swapchain.h"
#include "gpu.swapchain.h"
#include "gpu.device.h"
#include "gpu.geometry_pool.h"
#include "render.viewport.h"
#include "render.target.h"
#include "render.scene.h"
#include "render.source.manual.h"
#include "render.pipeline.scene_forward.h"
#include "render.material_base.h"
#include "render.types.3d.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "log.h"
#include "math.mat4.h"
#include "math.vec2.h"
#include "math.vec3.h"
#include "math.vec4.h"
#include "sim.ctx.h"
#include "string.str8.h"
#include "texture.pool.h"
#include "vfs.h"
#include "vfs.backend.os.h"

#define WIN_W 1600
#define WIN_H 900

typedef struct {
    f32 px, py, pz, _pad0;
    f32 nx, ny, nz, _pad1;
    f32 r, g, b, a;
    f32 u, v, _pad2, _pad3;
} Sponza_Vertex;

typedef struct {
    Mel_Vec4 base_color;
    u32 base_color_texture_idx;
    u32 flags;
    f32 alpha_cutoff;
    f32 _pad;
} Unlit_Params;

#define SPONZA_MATERIAL_USE_BASE_COLOR_TEXTURE (1u << 0)
#define SPONZA_MATERIAL_ALPHA_MASK             (1u << 1)

typedef struct {
    Mel_Render_Mesh_Part* parts;
    u32 part_count;
    Mel_Render_Material_Binding* bindings;
    u32 binding_count;
    Mel_Render_Bounds bounds;
    Mel_Mat4 model;
} Sponza_Load_Result;

static Mel_Window_Handle s_window;
static Mel_Swapchain_Handle s_swapchain;
static Mel_Render_Target_Handle s_target;
static Mel_Render_Scene* s_scene;
static Mel_Render_Source* s_source;
static Mel_Render_View_Handle s_view;
static Mel_Geometry_Pool s_geo_pool;
static Mel_Render_Handle s_sponza_handle;
static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];
static Mel_Vec3 s_camera_target;
static Mel_Vec3 s_camera_extents;
static Mel_Vec3 s_camera_position;
static f32 s_camera_yaw;
static f32 s_camera_pitch;
static bool s_mouse_captured;
static bool s_sim_initialized;
static bool s_sim_registered;

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

static bool sponza_load(Sponza_Load_Result* out, Mel_Geometry_Pool* pool,
                        Mel_Material_Base_Id unlit_id, const Mel_Alloc* alloc)
{
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
    str8 resolved_gltf_path = {0};
    u8* gltf_json = nullptr;
    if (!read_file_candidates4(alloc,
            gltf_path, gltf_path_alt,
            gltf_path_base, gltf_path_base_alt,
            &gltf_json, &gltf_size, &resolved_gltf_path))
    {
        mel_log_error("demo.sponza", "failed to read glTF at %.*s or %.*s",
            (int)gltf_path.len, gltf_path.data, (int)gltf_path_alt.len, gltf_path_alt.data);
        if (gltf_path_base.data) mel_dealloc(alloc, gltf_path_base.data);
        if (gltf_path_base_alt.data) mel_dealloc(alloc, gltf_path_base_alt.data);
        return false;
    }
    if (gltf_path_base.data) mel_dealloc(alloc, gltf_path_base.data);
    if (gltf_path_base_alt.data) mel_dealloc(alloc, gltf_path_base_alt.data);

    str8 gltf_dir = resolved_gltf_path;
    while (gltf_dir.len > 0 && gltf_dir.data[gltf_dir.len - 1] != '/')
        gltf_dir.len--;
    if (gltf_dir.len > 0)
        gltf_dir.len--;

    cJSON* root = cJSON_Parse((const char*)gltf_json);
    mel_dealloc(alloc, gltf_json);
    if (root == nullptr)
    {
        mel_log_error("demo.sponza", "failed to parse glTF JSON");
        mel_dealloc(alloc, resolved_gltf_path.data);
        return false;
    }

    cJSON* buffers = json_obj(root, "buffers");
    cJSON* buffer0 = cJSON_GetArrayItem(buffers, 0);
    cJSON* buffer_uri = buffer0 ? json_obj(buffer0, "uri") : nullptr;
    if (!buffer_uri || !cJSON_IsString(buffer_uri))
    {
        mel_log_error("demo.sponza", "missing buffer URI");
        mel_dealloc(alloc, resolved_gltf_path.data);
        cJSON_Delete(root);
        return false;
    }

    str8 bin_path = path_join2(gltf_dir, str8_from_cstr(buffer_uri->valuestring), alloc);
    i64 bin_size = 0;
    u8* bin = mel_vfs_read_file(bin_path, &bin_size, alloc);
    mel_dealloc(alloc, bin_path.data);
    if (bin == nullptr)
    {
        mel_log_error("demo.sponza", "failed to read binary buffer");
        mel_dealloc(alloc, resolved_gltf_path.data);
        cJSON_Delete(root);
        return false;
    }

    cJSON* materials = json_obj(root, "materials");
    cJSON* textures = json_obj(root, "textures");
    cJSON* images = json_obj(root, "images");
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
        mel_dealloc(alloc, resolved_gltf_path.data);
        mel_dealloc(alloc, bin);
        cJSON_Delete(root);
        return false;
    }

    u32 material_count = (u32)cJSON_GetArraySize(materials);
    u32 primitive_count = (u32)cJSON_GetArraySize(primitives);
    Mel_Render_Material_Binding* bindings = mel_alloc(alloc, (usize)material_count * sizeof(Mel_Render_Material_Binding));
    Mel_Render_Mesh_Part* parts = mel_alloc(alloc, (usize)primitive_count * sizeof(Mel_Render_Mesh_Part));
    Mel_Texture_Pool* texture_pool = mel_texture_pool();
    u32 white_texture_idx = texture_pool ? texture_pool->white_table_idx : 0;

    for (u32 i = 0; i < material_count; i++)
    {
        cJSON* material = cJSON_GetArrayItem(materials, (int)i);
        cJSON* pbr = material ? json_obj(material, "pbrMetallicRoughness") : nullptr;
        Mel_Vec4 base_color = mel_vec4(1, 1, 1, 1);
        u32 base_color_texture_idx = white_texture_idx;
        u32 flags = 0;
        f32 alpha_cutoff = 0.5f;
        u32 cull_mode = MEL_GPU_CULL_BACK;
        json_vec4_or_default(pbr ? json_obj(pbr, "baseColorFactor") : nullptr, &base_color, base_color);

        cJSON* base_color_tex = pbr ? json_obj(pbr, "baseColorTexture") : nullptr;
        if (base_color_tex && textures && images)
        {
            cJSON* texture_entry = cJSON_GetArrayItem(textures, json_int(base_color_tex, "index", -1));
            cJSON* image_entry = texture_entry ? cJSON_GetArrayItem(images, json_int(texture_entry, "source", -1)) : nullptr;
            cJSON* image_uri = image_entry ? json_obj(image_entry, "uri") : nullptr;
            if (image_uri && cJSON_IsString(image_uri) && texture_pool != nullptr)
            {
                str8 image_path = path_join2(gltf_dir, str8_from_cstr(image_uri->valuestring), alloc);
                Mel_Texture_Handle tex_handle = mel_texture_pool_load(texture_pool, image_path);
                Mel_Gpu_Texture* gpu_tex = mel_texture_pool_get(texture_pool, tex_handle);
                base_color_texture_idx = mel_texture_pool_add_to_table(gpu_tex);
                flags |= SPONZA_MATERIAL_USE_BASE_COLOR_TEXTURE;
                mel_dealloc(alloc, image_path.data);
            }
        }

        cJSON* alpha_mode = material ? json_obj(material, "alphaMode") : nullptr;
        if (alpha_mode && cJSON_IsString(alpha_mode) && strcmp(alpha_mode->valuestring, "MASK") == 0)
        {
            flags |= SPONZA_MATERIAL_ALPHA_MASK;
            alpha_cutoff = (f32)json_num(material, "alphaCutoff", 0.5);
        }

        cJSON* double_sided = material ? json_obj(material, "doubleSided") : nullptr;
        if (double_sided && cJSON_IsBool(double_sided) && cJSON_IsTrue(double_sided))
            cull_mode = MEL_GPU_CULL_NONE;

        Unlit_Params params = {
            .base_color = base_color,
            .base_color_texture_idx = base_color_texture_idx,
            .flags = flags,
            .alpha_cutoff = alpha_cutoff,
        };
        Mel_Material_Instance_Id mat_inst = mel_material_base_alloc_instance(unlit_id, &params);
        mel_material_base_set_cull_mode(unlit_id, mat_inst, cull_mode);
        bindings[i] = (Mel_Render_Material_Binding){
            .slot = i,
            .material_base_id = unlit_id,
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
        cJSON* uv_index = attrs ? json_obj(attrs, "TEXCOORD_0") : nullptr;
        cJSON* index_index = primitive ? json_obj(primitive, "indices") : nullptr;
        if (!position_index || !normal_index || !index_index)
            continue;

        cJSON* pos_accessor = cJSON_GetArrayItem(accessors, position_index->valueint);
        cJSON* nrm_accessor = cJSON_GetArrayItem(accessors, normal_index->valueint);
        cJSON* uv_accessor = uv_index ? cJSON_GetArrayItem(accessors, uv_index->valueint) : nullptr;
        cJSON* idx_accessor = cJSON_GetArrayItem(accessors, index_index->valueint);
        u32 vertex_count = (u32)json_int(pos_accessor, "count", 0);
        u32 index_count = (u32)json_int(idx_accessor, "count", 0);
        Sponza_Vertex* vertices = mel_alloc(alloc, (usize)vertex_count * sizeof(Sponza_Vertex));
        Mel_Vec3* positions = mel_alloc(alloc, (usize)vertex_count * sizeof(Mel_Vec3));
        Mel_Vec3* normals = mel_alloc(alloc, (usize)vertex_count * sizeof(Mel_Vec3));
        Mel_Vec2* uvs = uv_accessor ? mel_alloc(alloc, (usize)vertex_count * sizeof(Mel_Vec2)) : nullptr;
        u32* indices = mel_alloc(alloc, (usize)index_count * sizeof(u32));

        if (!gltf_read_accessor_vec3(accessors, buffer_views, pos_accessor, bin, positions, vertex_count) ||
            !gltf_read_accessor_vec3(accessors, buffer_views, nrm_accessor, bin, normals, vertex_count) ||
            (uv_accessor && !gltf_read_accessor_vec2(accessors, buffer_views, uv_accessor, bin, uvs, vertex_count)) ||
            !gltf_read_accessor_indices(buffer_views, idx_accessor, bin, indices, index_count))
        {
            mel_log_error("demo.sponza", "failed to decode primitive %u", i);
            mel_dealloc(alloc, resolved_gltf_path.data);
            mel_dealloc(alloc, indices);
            if (uvs) mel_dealloc(alloc, uvs);
            mel_dealloc(alloc, normals);
            mel_dealloc(alloc, positions);
            mel_dealloc(alloc, vertices);
            mel_dealloc(alloc, parts);
            mel_dealloc(alloc, bindings);
            mel_dealloc(alloc, bin);
            cJSON_Delete(root);
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
        if (uvs) mel_dealloc(alloc, uvs);
        mel_dealloc(alloc, normals);
        mel_dealloc(alloc, positions);
        mel_dealloc(alloc, vertices);
    }

    f32 scale = 1.0f;
    cJSON* node_scale = json_obj(node0, "scale");
    if (node_scale && cJSON_IsArray(node_scale) && cJSON_GetArraySize(node_scale) >= 1)
        scale = (f32)cJSON_GetArrayItem(node_scale, 0)->valuedouble;

    out->parts = parts;
    out->part_count = primitive_count;
    out->bindings = bindings;
    out->binding_count = material_count;
    out->bounds = (Mel_Render_Bounds){
        .center = mel_vec3((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f, (bmin.z + bmax.z) * 0.5f),
        .extents = mel_vec3((bmax.x - bmin.x) * 0.5f, (bmax.y - bmin.y) * 0.5f, (bmax.z - bmin.z) * 0.5f),
    };
    out->model = mel_mat4_scalef(scale);

    mel_dealloc(alloc, resolved_gltf_path.data);
    mel_dealloc(alloc, bin);
    cJSON_Delete(root);
    return true;
}

static Mel_Vec3 sponza_camera_forward(void)
{
    f32 cp = cosf(s_camera_pitch);
    return mel_vec3_normalize(mel_vec3(
        cosf(s_camera_yaw) * cp,
        sinf(s_camera_pitch),
        sinf(s_camera_yaw) * cp));
}

static void sponza_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);

    Mel_Swapchain_Entry* sc_entry = mel_swapchain_registry_get(s_swapchain);
    f32 aspect = (f32)sc_entry->swapchain.extent_width / (f32)sc_entry->swapchain.extent_height;
    const bool* keys = SDL_GetKeyboardState(nullptr);

    Mel_Vec3 up = mel_vec3(0, 1, 0);
    Mel_Vec3 forward = sponza_camera_forward();
    Mel_Vec3 flat_forward = mel_vec3(forward.x, 0.0f, forward.z);
    if (mel_vec3_len_sq(flat_forward) > 0.000001f)
        flat_forward = mel_vec3_normalize(flat_forward);
    else
        flat_forward = mel_vec3(0, 0, -1);

    Mel_Vec3 right = mel_vec3_cross(flat_forward, up);
    if (mel_vec3_len_sq(right) > 0.000001f)
        right = mel_vec3_normalize(right);
    else
        right = mel_vec3(1, 0, 0);

    Mel_Vec3 move = mel_vec3(0, 0, 0);
    if (keys[SDL_SCANCODE_W]) move = mel_vec3_add(move, flat_forward);
    if (keys[SDL_SCANCODE_S]) move = mel_vec3_sub(move, flat_forward);
    if (keys[SDL_SCANCODE_D]) move = mel_vec3_add(move, right);
    if (keys[SDL_SCANCODE_A]) move = mel_vec3_sub(move, right);
    if (keys[SDL_SCANCODE_E] || keys[SDL_SCANCODE_SPACE]) move = mel_vec3_add(move, up);
    if (keys[SDL_SCANCODE_Q] || keys[SDL_SCANCODE_C] || keys[SDL_SCANCODE_LCTRL]) move = mel_vec3_sub(move, up);

    if (mel_vec3_len_sq(move) > 0.000001f)
    {
        move = mel_vec3_normalize(move);

        f32 speed = 3.5f;
        f32 span_xz = s_camera_extents.x > s_camera_extents.z ? s_camera_extents.x : s_camera_extents.z;
        if (span_xz > 1.0f)
            speed = span_xz * 0.18f;
        if (speed < 3.5f)
            speed = 3.5f;
        if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT])
            speed *= 3.0f;

        s_camera_position = mel_vec3_add(s_camera_position, mel_vec3_scale(move, speed * dt));
    }

    Mel_Vec3 target = mel_vec3_add(s_camera_position, forward);

    mel_render_view_set_camera(s_view, (Mel_Render_Camera){
        .view = mel_mat4_look_at(s_camera_position, target, up),
        .projection = mel_mat4_perspective(60.0f * (3.14159265f / 180.0f), aspect, 0.1f, 100.0f),
        .viewport = {{ 0, 0, 1, 1 }},
        .visibility_mask = 0xFFFFFFFF,
        .lod_bias = 1.0f,
        .view_count = 1,
    });
}

void app_init(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Gpu_Device* dev = mel_gpu_dev();

    mel_vfs_mount(S8("/"), mel_vfs_backend_os(), .root = S8("/"));

    s_window = mel_window_create(S8("Melody Sponza"), .width = WIN_W, .height = WIN_H);
    s_swapchain = mel_gpu_swapchain_create_for_window(dev, s_window);
    s_target = mel_render_target_from_swapchain(s_swapchain);

    mel_geometry_pool_init(&s_geo_pool,
        .dev = dev, .alloc = alloc,
        .vertex_stride = sizeof(Sponza_Vertex),
        .vertex_capacity = 64ull * 1024ull * 1024ull,
        .index_capacity = 16ull * 1024ull * 1024ull);
    mel_pipeline_scene_forward_set_geometry_pool(&s_geo_pool);

    Mel_Material_Base_Id unlit_id = mel_material_base_find(S8("unlit"));
    if (unlit_id == MEL_MATERIAL_BASE_ID_INVALID)
    {
        unlit_id = mel_material_base_register(&(Mel_Material_Base_Desc){
            .name = S8("unlit"),
            .param_size = sizeof(Unlit_Params),
            .compat = MEL_COMPAT_FORWARD,
        });
    }

    Sponza_Load_Result loaded = {0};
    if (!sponza_load(&loaded, &s_geo_pool, unlit_id, alloc))
    {
        mel_log_error("demo.sponza", "failed to load Sponza");
        mel_quit();
        return;
    }

    s_source = mel_source_manual_create(alloc);
    s_scene = mel_render_scene_create(.dev = dev, .alloc = alloc);
    mel_render_scene_attach_source(s_scene, s_source);

    Mel_Render_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
        .viewport = {{ 0, 0, 1, 1 }},
        .visibility_mask = 0xFFFFFFFF,
        .lod_bias = 1.0f,
        .view_count = 1,
    };

    s_view = mel_render_view_create(
        .scene = s_scene,
        .camera = camera,
        .target = s_target,
        .pipeline = S8("scene_forward"),
        .dev = dev,
        .alloc = alloc);

    s_sponza_handle = mel_source_manual_add(s_source,
        loaded.model,
        loaded.bounds,
        (Mel_Render_Info){
            .material_base_id = loaded.binding_count > 0 ? loaded.bindings[0].material_base_id : unlit_id,
            .material_idx = loaded.binding_count > 0 ? loaded.bindings[0].material_idx : 0,
            .mesh = loaded.part_count > 0 ? loaded.parts[0].mesh : (Mel_Geometry_Handle){0},
            .layer_mask = 0xFFFFFFFFu,
        });
    mel_source_manual_set_material_bindings(s_source, s_sponza_handle, loaded.bindings, loaded.binding_count);
    mel_source_manual_set_mesh_parts(s_source, s_sponza_handle, loaded.parts, loaded.part_count);

    s_camera_target = mel_vec3(
        loaded.bounds.center.x * loaded.model.m[0][0],
        loaded.bounds.center.y * loaded.model.m[1][1],
        loaded.bounds.center.z * loaded.model.m[2][2]);
    s_camera_extents = mel_vec3(
        loaded.bounds.extents.x * fabsf(loaded.model.m[0][0]),
        loaded.bounds.extents.y * fabsf(loaded.model.m[1][1]),
        loaded.bounds.extents.z * fabsf(loaded.model.m[2][2]));
    s_camera_position = mel_vec3(
        s_camera_target.x,
        s_camera_target.y + s_camera_extents.y * 0.12f,
        s_camera_target.z + s_camera_extents.z * 0.55f);
    Mel_Vec3 initial_forward = mel_vec3_normalize(mel_vec3_sub(s_camera_target, s_camera_position));
    s_camera_yaw = atan2f(initial_forward.z, initial_forward.x);
    s_camera_pitch = asinf(initial_forward.y);

    mel_dealloc(alloc, loaded.parts);
    mel_dealloc(alloc, loaded.bindings);

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));
    s_sim_initialized = true;
    mel_sim_add_variable(&s_sim, sponza_update);
    mel_register_sim(&s_sim);
    s_sim_registered = true;

    SDL_SetWindowRelativeMouseMode(mel__window_sdl(s_window), true);
    s_mouse_captured = true;
    mel_log_info("demo.sponza", "controls: mouse look, WASD move, QE/space/ctrl vertical, shift sprint, esc release/quit");
}

void app_shutdown(void)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    mel_gpu_device_wait_idle(dev);

    if (s_sim_registered)
    {
        mel_unregister_sim(&s_sim);
        s_sim_registered = false;
    }
    if (s_sim_initialized)
    {
        mel_sim_shutdown(&s_sim);
        s_sim_initialized = false;
    }
    if (s_mouse_captured)
    {
        SDL_SetWindowRelativeMouseMode(mel__window_sdl(s_window), false);
        s_mouse_captured = false;
    }
    if (mel_render_view_handle_valid(s_view))
        mel_render_view_destroy(s_view);
    if (s_source != nullptr)
        mel_render_source_destroy(s_source);
    if (s_scene != nullptr)
        mel_render_scene_destroy(s_scene);
    if (mel_render_target_handle_valid(s_target))
        mel_render_target_destroy(s_target);
    if (s_geo_pool.dev != nullptr)
        mel_geometry_pool_shutdown(&s_geo_pool);
}

void app_event(SDL_Event* event)
{
    if (event->type == SDL_EVENT_QUIT)
        mel_quit();
    if (event->type == SDL_EVENT_MOUSE_MOTION && s_mouse_captured)
    {
        const f32 sensitivity = 0.0025f;
        s_camera_yaw += event->motion.xrel * sensitivity;
        s_camera_pitch -= event->motion.yrel * sensitivity;

        const f32 pitch_limit = 1.55334306f;
        if (s_camera_pitch > pitch_limit) s_camera_pitch = pitch_limit;
        if (s_camera_pitch < -pitch_limit) s_camera_pitch = -pitch_limit;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT && !s_mouse_captured)
    {
        SDL_SetWindowRelativeMouseMode(mel__window_sdl(s_window), true);
        s_mouse_captured = true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE && !event->key.repeat)
    {
        if (s_mouse_captured)
        {
            SDL_SetWindowRelativeMouseMode(mel__window_sdl(s_window), false);
            s_mouse_captured = false;
        }
        else
        {
            mel_quit();
        }
    }
}
