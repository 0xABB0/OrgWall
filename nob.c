#define NOB_IMPLEMENTATION
#include "nob.h"

#define BUILD_DIR "build"
#define SUCK_DIR "/Users/gabbo/repo/suck"

#define BUILD_MODE_DEBUG    0
#define BUILD_MODE_RELEASE  1
#define BUILD_MODE_SANITIZE 2

static int g_build_mode = BUILD_MODE_DEBUG;
static bool g_verbose = false;
static bool g_timings = false;
static const char* g_backend = "vulkan";

#define TIMING_LEVEL (g_verbose ? NOB_INFO : NOB_WARNING)

static const char* INCLUDE_PATHS[] = {
    "-I/opt/homebrew/include",
    "-Ithird-party",
    "-I" SUCK_DIR "/third-party",
    "-I" SUCK_DIR "/third-party/slang/include",
    "-I" SUCK_DIR "/third-party/simdutf/singleheader",
    "-I" SUCK_DIR "/third-party/cimgui",
    "-I" SUCK_DIR "/third-party/cimgui/imgui",
    "-I" SUCK_DIR "/third-party/cimgui/imgui/backends",
    "-I" SUCK_DIR "/third-party/tracy/public",
    "-I" SUCK_DIR "/flecs/distr",
    "-I" SUCK_DIR "/tomlc17",
    "-Imelody",
    "-Ilib/mugen",
};

static const char* LIB_PATHS[] = {
    "-L/opt/homebrew/lib",
    "-L" SUCK_DIR "/third-party/slang/lib",
};

static const char* LIBS[] = {
    "-lSDL3",
    "-lslang",
    "-lcjson",
    "-lsqlite3",
};

static const char* FRAMEWORKS[] = {
    "-framework", "Metal",
    "-framework", "QuartzCore",
    "-framework", "Cocoa",
    "-framework", "IOKit",
};

static const char* CFLAGS_BASE[] = {
    "-std=c23",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wno-unused-parameter",
    "-Wno-missing-field-initializers",
    "-Wno-unused-private-field",
    "-Wno-unused-function",
    "-DFLECS_NO_CPP",
    "-DCIMGUI_DEFINE_ENUMS_AND_STRUCTS",
};

static const char* CXXFLAGS_BASE[] = {
    "-std=c++17",
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-Wno-missing-field-initializers",
    "-Wno-unused-variable",
    "-Wno-unused-private-field",
    "-Wno-nullability-completeness",
    "-Wno-unused-function",
    "-DFLECS_NO_CPP",
};

static void cmd_append_cflags(Nob_Cmd* cmd)
{
    for (size_t i = 0; i < NOB_ARRAY_LEN(CFLAGS_BASE); i++)
        nob_cmd_append(cmd, CFLAGS_BASE[i]);

    if (g_build_mode == BUILD_MODE_RELEASE)
    {
        nob_cmd_append(cmd, "-O2", "-DNDEBUG", "-flto");
    }
    else if (g_build_mode == BUILD_MODE_SANITIZE)
    {
        nob_cmd_append(cmd, "-g", "-O0");
        nob_cmd_append(cmd, "-DTRACY_ENABLE", "-DMEL_CONFIG_DEBUG_ALLOCATOR");
        nob_cmd_append(cmd, "-fsanitize=address,undefined", "-fno-omit-frame-pointer");
    }
    else
    {
        nob_cmd_append(cmd, "-g", "-O0");
        nob_cmd_append(cmd, "-DTRACY_ENABLE", "-DMEL_CONFIG_DEBUG_ALLOCATOR");
    }
}

static void cmd_append_cxxflags(Nob_Cmd* cmd)
{
    for (size_t i = 0; i < NOB_ARRAY_LEN(CXXFLAGS_BASE); i++)
        nob_cmd_append(cmd, CXXFLAGS_BASE[i]);

    if (g_build_mode == BUILD_MODE_RELEASE)
    {
        nob_cmd_append(cmd, "-O2", "-DNDEBUG", "-flto");
    }
    else if (g_build_mode == BUILD_MODE_SANITIZE)
    {
        nob_cmd_append(cmd, "-g", "-O0");
        nob_cmd_append(cmd, "-DMEL_CONFIG_DEBUG_ALLOCATOR");
        nob_cmd_append(cmd, "-fsanitize=address,undefined", "-fno-omit-frame-pointer");
    }
    else
    {
        nob_cmd_append(cmd, "-g", "-O0");
        nob_cmd_append(cmd, "-DMEL_CONFIG_DEBUG_ALLOCATOR");
    }
}

static void cmd_append_link_flags(Nob_Cmd* cmd)
{
    if (g_build_mode == BUILD_MODE_RELEASE)
        nob_cmd_append(cmd, "-flto");
    else if (g_build_mode == BUILD_MODE_SANITIZE)
        nob_cmd_append(cmd, "-fsanitize=address,undefined");
}

typedef struct {
    const char* suffix;
    bool active;
} Platform_Suffix;

static const Platform_Suffix PLATFORM_SUFFIXES[] = {
#if defined(__APPLE__)
    { ".osx.",     true  },
    { ".posix.",   true  },
    { ".win.",     false },
    { ".linux.",   false },
    { ".ios.",     false },
    { ".android.", false },
#elif defined(_WIN32)
    { ".win.",     true  },
    { ".osx.",     false },
    { ".posix.",   false },
    { ".linux.",   false },
    { ".ios.",     false },
    { ".android.", false },
#elif defined(__linux__) && defined(__ANDROID__)
    { ".android.", true  },
    { ".posix.",   true  },
    { ".osx.",     false },
    { ".win.",     false },
    { ".linux.",   false },
    { ".ios.",     false },
#elif defined(__linux__)
    { ".linux.",   true  },
    { ".posix.",   true  },
    { ".osx.",     false },
    { ".win.",     false },
    { ".ios.",     false },
    { ".android.", false },
#endif
};

typedef struct {
    const char* name;
    const char* suffix;
} Backend_Entry;

static const Backend_Entry BACKENDS[] = {
    { "vulkan", ".vulkan." },
    { "metal",  ".metal."  },
    { "dx12",   ".dx12."   },
    { "opengl", ".opengl." },
    { "webgpu", ".webgpu." },
};

static bool should_skip_platform_file(const char* name)
{
    for (size_t i = 0; i < NOB_ARRAY_LEN(PLATFORM_SUFFIXES); i++)
    {
        if (strstr(name, PLATFORM_SUFFIXES[i].suffix) && !PLATFORM_SUFFIXES[i].active)
            return true;
    }

    for (size_t i = 0; i < NOB_ARRAY_LEN(BACKENDS); i++)
    {
        if (strstr(name, BACKENDS[i].suffix) && strcmp(BACKENDS[i].name, g_backend) != 0)
            return true;
    }

    return false;
}

bool collect_c_files(const char* dir, Nob_File_Paths* c_files, Nob_File_Paths* cpp_files, Nob_File_Paths* m_files)
{
    Nob_File_Paths entries = {0};
    if (!nob_read_entire_dir(dir, &entries)) return false;

    for (size_t i = 0; i < entries.count; i++)
    {
        const char* name = entries.items[i];
        if (name[0] == '.') continue;

        const char* path = nob_temp_sprintf("%s/%s", dir, name);
        Nob_File_Type type = nob_get_file_type(path);

        if (type == NOB_FILE_DIRECTORY)
        {
            if (!collect_c_files(path, c_files, cpp_files, m_files)) return false;
        }
        else if (type == NOB_FILE_REGULAR)
        {
            if (should_skip_platform_file(name)) continue;

            size_t len = strlen(name);
            if (len > 2 && name[len-2] == '.' && name[len-1] == 'c')
            {
                nob_da_append(c_files, nob_temp_strdup(path));
            }
            else if (len > 4 && strcmp(name + len - 4, ".cpp") == 0)
            {
                nob_da_append(cpp_files, nob_temp_strdup(path));
            }
            else if (len > 2 && name[len-2] == '.' && name[len-1] == 'm')
            {
                nob_da_append(m_files, nob_temp_strdup(path));
            }
        }
    }
    return true;
}

typedef enum {
    CPP_MODE_DEFAULT,
    CPP_MODE_CIMGUI,
    CPP_MODE_TRACY,
} Cpp_Mode;

const char* obj_to_depfile(const char* obj)
{
    char* dep = nob_temp_strdup(obj);
    size_t len = strlen(dep);
    if (len > 0) dep[len - 1] = 'd';
    return dep;
}

bool parse_depfile(const char* depfile_path, Nob_File_Paths* deps)
{
    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(depfile_path, &sb)) return false;

    char* p = sb.items;
    char* end = sb.items + sb.count;

    while (p < end && *p != ':') p++;
    if (p < end) p++;

    while (p < end)
    {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            p++;

        if (p < end && *p == '\\' && p + 1 < end && p[1] == '\n')
        {
            p += 2;
            continue;
        }

        if (p >= end) break;

        Nob_String_Builder path = {0};
        while (p < end && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
        {
            if (*p == '\\' && p + 1 < end)
            {
                if (p[1] == '\n')
                {
                    p += 2;
                    break;
                }
                if (p[1] == ' ')
                {
                    nob_da_append(&path, ' ');
                    p += 2;
                    continue;
                }
            }
            nob_da_append(&path, *p);
            p++;
        }

        if (path.count > 0)
        {
            nob_da_append(&path, '\0');
            nob_da_append(deps, nob_temp_strdup(path.items));
        }
        free(path.items);
    }

    free(sb.items);
    return true;
}

int needs_compile(const char* src, const char* obj)
{
    (void)src;
    const char* depfile = obj_to_depfile(obj);

    if (nob_file_exists(depfile) != 1) return 1;

    Nob_File_Paths deps = {0};
    if (!parse_depfile(depfile, &deps)) return 1;

    int result = nob_needs_rebuild(obj, deps.items, deps.count);
    free(deps.items);

    if (result != 0) return 1;
    return 0;
}

bool compile_cpp_to_obj(const char* src, const char* obj, Cpp_Mode mode)
{
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang++");

    cmd_append_cxxflags(&cmd);

    for (size_t i = 0; i < NOB_ARRAY_LEN(INCLUDE_PATHS); i++)
        nob_cmd_append(&cmd, INCLUDE_PATHS[i]);

    if (mode == CPP_MODE_CIMGUI)
    {
        nob_cmd_append(&cmd, "-DIMGUI_IMPL_API=extern \"C\"");
        nob_cmd_append(&cmd, "-DCIMGUI_USE_SDL3");
        nob_cmd_append(&cmd, "-DCIMGUI_USE_VULKAN");
        nob_cmd_append(&cmd, "-DIMGUI_IMPL_VULKAN_USE_VOLK");
    }
    else if (mode == CPP_MODE_TRACY)
    {
        nob_cmd_append(&cmd, "-DTRACY_ENABLE");
        nob_cmd_append(&cmd, "-Wno-deprecated-declarations");
    }
    else if (g_build_mode != BUILD_MODE_RELEASE)
    {
        nob_cmd_append(&cmd, "-DTRACY_ENABLE");
    }

    nob_cmd_append(&cmd, "-MD", "-MF", obj_to_depfile(obj));
    nob_cmd_append(&cmd, "-c", src, "-o", obj);

    return nob_cmd_run_sync(cmd);
}

bool copy_dir_recursive(const char* src_dir, const char* dst_dir)
{
    if (!nob_mkdir_if_not_exists(dst_dir)) return false;

    Nob_File_Paths entries = {0};
    if (!nob_read_entire_dir(src_dir, &entries)) return true;

    for (size_t i = 0; i < entries.count; i++)
    {
        const char* name = entries.items[i];
        if (name[0] == '.') continue;

        const char* src_path = nob_temp_sprintf("%s/%s", src_dir, name);
        const char* dst_path = nob_temp_sprintf("%s/%s", dst_dir, name);

        Nob_File_Type type = nob_get_file_type(src_path);
        if (type == NOB_FILE_DIRECTORY)
        {
            if (!copy_dir_recursive(src_path, dst_path)) return false;
        }
        else if (type == NOB_FILE_REGULAR)
        {
            if (!nob_copy_file(src_path, dst_path))
            {
                nob_log(NOB_WARNING, "Failed to copy asset: %s", src_path);
            }
        }
    }

    return true;
}

bool copy_assets(void)
{
    return copy_dir_recursive("assets", BUILD_DIR "/assets");
}

bool compile_c_to_obj_ex(const char* src, const char* obj, const char** extra_includes, size_t extra_count)
{
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang");

    cmd_append_cflags(&cmd);

    for (size_t i = 0; i < NOB_ARRAY_LEN(INCLUDE_PATHS); i++)
        nob_cmd_append(&cmd, INCLUDE_PATHS[i]);

    for (size_t i = 0; i < extra_count; i++)
        nob_cmd_append(&cmd, extra_includes[i]);

    nob_cmd_append(&cmd, "-MD", "-MF", obj_to_depfile(obj));
    nob_cmd_append(&cmd, "-c", src, "-o", obj);

    return nob_cmd_run_sync(cmd);
}

bool compile_c_to_obj(const char* src, const char* obj)
{
    return compile_c_to_obj_ex(src, obj, NULL, 0);
}

bool compile_m_to_obj(const char* src, const char* obj)
{
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang");

    cmd_append_cflags(&cmd);

    nob_cmd_append(&cmd, "-fobjc-arc");

    for (size_t i = 0; i < NOB_ARRAY_LEN(INCLUDE_PATHS); i++)
        nob_cmd_append(&cmd, INCLUDE_PATHS[i]);

    nob_cmd_append(&cmd, "-MD", "-MF", obj_to_depfile(obj));
    nob_cmd_append(&cmd, "-c", src, "-o", obj);

    return nob_cmd_run_sync(cmd);
}

#if defined(__aarch64__) && defined(__APPLE__)
static const char* ASM_FILES[] = { "asm/jump_arm64_aapcs_macho_gas.S", "asm/make_arm64_aapcs_macho_gas.S", "asm/ontop_arm64_aapcs_macho_gas.S" };
#elif defined(__x86_64__) && defined(__APPLE__)
static const char* ASM_FILES[] = { "asm/jump_x86_64_sysv_macho_gas.S", "asm/make_x86_64_sysv_macho_gas.S", "asm/ontop_x86_64_sysv_macho_gas.S" };
#elif defined(__aarch64__) && defined(__linux__)
static const char* ASM_FILES[] = { "asm/jump_arm64_aapcs_elf_gas.S", "asm/make_arm64_aapcs_elf_gas.S", "asm/ontop_arm64_aapcs_elf_gas.S" };
#elif defined(__x86_64__) && defined(__linux__)
static const char* ASM_FILES[] = { "asm/jump_x86_64_sysv_elf_gas.S", "asm/make_x86_64_sysv_elf_gas.S", "asm/ontop_x86_64_sysv_elf_gas.S" };
#elif defined(__x86_64__) && defined(_WIN32)
static const char* ASM_FILES[] = { "asm/jump_x86_64_ms_pe_clang_gas.S", "asm/make_x86_64_ms_pe_clang_gas.S", "asm/ontop_x86_64_ms_pe_clang_gas.S" };
#endif

bool compile_asm_to_obj(const char* src, const char* obj)
{
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang", "-MD", "-MF", obj_to_depfile(obj), "-c", src, "-o", obj);
    return nob_cmd_run_sync(cmd);
}

static const char* CIMGUI_SOURCES[] = {
    SUCK_DIR "/third-party/cimgui/cimgui.cpp",
    SUCK_DIR "/third-party/cimgui/imgui/imgui.cpp",
    SUCK_DIR "/third-party/cimgui/imgui/imgui_demo.cpp",
    SUCK_DIR "/third-party/cimgui/imgui/imgui_draw.cpp",
    SUCK_DIR "/third-party/cimgui/imgui/imgui_tables.cpp",
    SUCK_DIR "/third-party/cimgui/imgui/imgui_widgets.cpp",
    SUCK_DIR "/third-party/cimgui/imgui/backends/imgui_impl_sdl3.cpp",
    SUCK_DIR "/third-party/cimgui/imgui/backends/imgui_impl_vulkan.cpp",
};


bool build_libs(void)
{
    uint64_t t_total_start = nob_nanos_since_unspecified_epoch();

    uint64_t t0 = nob_nanos_since_unspecified_epoch();

    for (size_t i = 0; i < NOB_ARRAY_LEN(CIMGUI_SOURCES); i++)
    {
        const char* src = CIMGUI_SOURCES[i];
        const char* base = strrchr(src, '/');
        base = base ? base + 1 : src;

        char obj_name[256];
        snprintf(obj_name, sizeof(obj_name), "%s", base);
        size_t len = strlen(obj_name);
        obj_name[len - 3] = 'o';
        obj_name[len - 2] = '\0';

        const char* obj = nob_temp_sprintf(BUILD_DIR "/%s", obj_name);

        if (needs_compile(src, obj))
        {
            nob_log(NOB_WARNING, "Compiling cimgui: %s", base);
            if (!compile_cpp_to_obj(src, obj, CPP_MODE_CIMGUI)) return false;
        }
    }

    uint64_t t1 = nob_nanos_since_unspecified_epoch();

    {
        const char* tracy_src = SUCK_DIR "/third-party/tracy/public/TracyClient.cpp";
        const char* tracy_obj = BUILD_DIR "/TracyClient.o";
        if (needs_compile(tracy_src, tracy_obj))
        {
            nob_log(NOB_WARNING, "Compiling Tracy client");
            if (!compile_cpp_to_obj(tracy_src, tracy_obj, CPP_MODE_TRACY)) return false;
        }
    }

    uint64_t t2 = nob_nanos_since_unspecified_epoch();

    {
        const char* flecs_src = SUCK_DIR "/flecs/distr/flecs.c";
        const char* flecs_obj = BUILD_DIR "/flecs.o";
        if (needs_compile(flecs_src, flecs_obj))
        {
            nob_log(NOB_WARNING, "Compiling flecs");
            if (!compile_c_to_obj(flecs_src, flecs_obj)) return false;
        }
    }

    uint64_t t3 = nob_nanos_since_unspecified_epoch();

    double cimgui_ms = (t1 - t0) / 1e6;
    double tracy_ms  = (t2 - t1) / 1e6;
    double flecs_ms  = (t3 - t2) / 1e6;
    double total_ms  = (t3 - t_total_start) / 1e6;

    if (g_timings)
    {
        nob_log(TIMING_LEVEL, "──── Libs Timings ─────");
        nob_log(TIMING_LEVEL, "  cimgui (C++)    : %8.1f ms", cimgui_ms);
        nob_log(TIMING_LEVEL, "  tracy  (C++)    : %8.1f ms", tracy_ms);
        nob_log(TIMING_LEVEL, "  flecs  (C)      : %8.1f ms", flecs_ms);
        nob_log(TIMING_LEVEL, "  ─────────────────────────");
        nob_log(TIMING_LEVEL, "  TOTAL           : %8.1f ms", total_ms);
    }

    return true;
}

bool build_melody(void)
{
    if (!build_libs()) return false;

    uint64_t t_total_start = nob_nanos_since_unspecified_epoch();
    bool any_recompiled = false;

    Nob_File_Paths c_files = {0};
    Nob_File_Paths cpp_files = {0};
    Nob_File_Paths m_files = {0};
    Nob_File_Paths obj_files = {0};

    if (!collect_c_files("melody", &c_files, &cpp_files, &m_files)) return false;

    uint64_t t0 = nob_nanos_since_unspecified_epoch();

    for (size_t i = 0; i < cpp_files.count; i++)
    {
        const char* src = cpp_files.items[i];
        const char* base = strrchr(src, '/');
        base = base ? base + 1 : src;

        char obj_name[256];
        snprintf(obj_name, sizeof(obj_name), "%s", base);
        size_t len = strlen(obj_name);
        obj_name[len - 3] = 'o';
        obj_name[len - 2] = '\0';

        const char* obj = nob_temp_sprintf(BUILD_DIR "/%s", obj_name);
        nob_da_append(&obj_files, nob_temp_strdup(obj));

        if (needs_compile(src, obj))
        {
            nob_log(NOB_INFO, "Compiling C++: %s", src);
            if (!compile_cpp_to_obj(src, obj, CPP_MODE_DEFAULT)) return false;
            any_recompiled = true;
        }
    }

    uint64_t t1 = nob_nanos_since_unspecified_epoch();

    for (size_t i = 0; i < c_files.count; i++)
    {
        const char* src = c_files.items[i];
        if (strcmp(src, "melody/core.app.entry.c") == 0
         || strcmp(src, "melody/core.app.callbacks.c") == 0)
            continue;
        const char* base = strrchr(src, '/');
        base = base ? base + 1 : src;

        char obj_name[256];
        snprintf(obj_name, sizeof(obj_name), "%s", base);
        size_t len = strlen(obj_name);
        if (len > 2 && obj_name[len-2] == '.' && obj_name[len-1] == 'c')
            obj_name[len - 1] = 'o';

        const char* obj = nob_temp_sprintf(BUILD_DIR "/%s", obj_name);
        nob_da_append(&obj_files, nob_temp_strdup(obj));

        if (needs_compile(src, obj))
        {
            nob_log(NOB_INFO, "Compiling C: %s", src);
            if (!compile_c_to_obj(src, obj)) return false;
            any_recompiled = true;
        }
    }

    uint64_t t2 = nob_nanos_since_unspecified_epoch();

    for (size_t i = 0; i < m_files.count; i++)
    {
        const char* src = m_files.items[i];
        const char* base = strrchr(src, '/');
        base = base ? base + 1 : src;

        char obj_name[256];
        snprintf(obj_name, sizeof(obj_name), "%s", base);
        size_t len = strlen(obj_name);
        if (len > 2 && obj_name[len-2] == '.' && obj_name[len-1] == 'm')
            obj_name[len - 1] = 'o';

        const char* obj = nob_temp_sprintf(BUILD_DIR "/%s", obj_name);
        nob_da_append(&obj_files, nob_temp_strdup(obj));

        if (needs_compile(src, obj))
        {
            nob_log(NOB_INFO, "Compiling ObjC: %s", src);
            if (!compile_m_to_obj(src, obj)) return false;
            any_recompiled = true;
        }
    }

    uint64_t t2b = nob_nanos_since_unspecified_epoch();

    for (size_t i = 0; i < NOB_ARRAY_LEN(ASM_FILES); i++)
    {
        const char* src = ASM_FILES[i];
        const char* base = strrchr(src, '/');
        base = base ? base + 1 : src;

        char obj_name[256];
        snprintf(obj_name, sizeof(obj_name), "%s", base);
        size_t len = strlen(obj_name);
        obj_name[len - 1] = 'o';

        const char* obj = nob_temp_sprintf(BUILD_DIR "/%s", obj_name);
        nob_da_append(&obj_files, nob_temp_strdup(obj));

        if (needs_compile(src, obj))
        {
            if (!compile_asm_to_obj(src, obj)) return false;
            any_recompiled = true;
        }
    }

    uint64_t t3 = nob_nanos_since_unspecified_epoch();

    for (size_t i = 0; i < NOB_ARRAY_LEN(CIMGUI_SOURCES); i++)
    {
        const char* base = strrchr(CIMGUI_SOURCES[i], '/');
        base = base ? base + 1 : CIMGUI_SOURCES[i];

        char obj_name[256];
        snprintf(obj_name, sizeof(obj_name), "%s", base);
        size_t len = strlen(obj_name);
        obj_name[len - 3] = 'o';
        obj_name[len - 2] = '\0';

        nob_da_append(&obj_files, nob_temp_strdup(nob_temp_sprintf(BUILD_DIR "/%s", obj_name)));
    }
    nob_da_append(&obj_files, nob_temp_strdup(BUILD_DIR "/TracyClient.o"));
    nob_da_append(&obj_files, nob_temp_strdup(BUILD_DIR "/flecs.o"));

    if (any_recompiled || nob_file_exists(BUILD_DIR "/libmelody.a") != 1)
    {
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "ar", "rcs", BUILD_DIR "/libmelody.a");

        for (size_t i = 0; i < obj_files.count; i++)
            nob_cmd_append(&cmd, obj_files.items[i]);

        if (!nob_cmd_run_sync(cmd)) return false;
    }

    uint64_t t4 = nob_nanos_since_unspecified_epoch();

    double cpp_ms   = (t1 - t0) / 1e6;
    double c_ms     = (t2 - t1) / 1e6;
    double objc_ms  = (t2b - t2) / 1e6;
    double asm_ms   = (t3 - t2b) / 1e6;
    double ar_ms    = (t4 - t3) / 1e6;
    double total_ms = (t4 - t_total_start) / 1e6;

    if (g_timings)
    {
        nob_log(TIMING_LEVEL, "──── Melody Timings ────");
        nob_log(TIMING_LEVEL, "  melody C++      : %8.1f ms", cpp_ms);
        nob_log(TIMING_LEVEL, "  melody C        : %8.1f ms", c_ms);
        nob_log(TIMING_LEVEL, "  melody ObjC     : %8.1f ms", objc_ms);
        nob_log(TIMING_LEVEL, "  melody ASM      : %8.1f ms", asm_ms);
        nob_log(TIMING_LEVEL, "  archive         : %8.1f ms", ar_ms);
        nob_log(TIMING_LEVEL, "  ─────────────────────────");
        nob_log(TIMING_LEVEL, "  TOTAL           : %8.1f ms", total_ms);
    }

    return true;
}

static void cmd_append_melody_link_deps(Nob_Cmd* cmd)
{
    nob_cmd_append(cmd, BUILD_DIR "/libmelody.a");

    for (size_t i = 0; i < NOB_ARRAY_LEN(LIB_PATHS); i++)
        nob_cmd_append(cmd, LIB_PATHS[i]);

    for (size_t i = 0; i < NOB_ARRAY_LEN(LIBS); i++)
        nob_cmd_append(cmd, LIBS[i]);

    for (size_t i = 0; i < NOB_ARRAY_LEN(FRAMEWORKS); i++)
        nob_cmd_append(cmd, FRAMEWORKS[i]);

    nob_cmd_append(cmd, "-lc++");
    nob_cmd_append(cmd, "-Wl,-rpath,/opt/homebrew/lib");
    nob_cmd_append(cmd, "-Wl,-rpath," SUCK_DIR "/third-party/slang/lib");
    cmd_append_link_flags(cmd);
}


bool build_tests(void)
{
    if (!build_melody()) return false;

    Nob_File_Paths entries = {0};
    if (!nob_read_entire_dir("tests", &entries)) return false;

    Nob_File_Paths obj_files = {0};
    bool any_recompiled = false;

    for (size_t i = 0; i < entries.count; i++)
    {
        const char* name = entries.items[i];
        if (name[0] == '.') continue;

        size_t len = strlen(name);
        if (len < 3 || name[len-2] != '.' || name[len-1] != 'c') continue;

        const char* src = nob_temp_sprintf("tests/%s", name);

        char obj_name[256];
        snprintf(obj_name, sizeof(obj_name), "test_%s", name);
        size_t olen = strlen(obj_name);
        obj_name[olen - 1] = 'o';

        const char* obj = nob_temp_sprintf(BUILD_DIR "/%s", obj_name);
        nob_da_append(&obj_files, nob_temp_strdup(obj));

        if (needs_compile(src, obj))
        {
            nob_log(NOB_INFO, "Compiling test: %s", name);
            if (!compile_c_to_obj(src, obj)) return false;
            any_recompiled = true;
        }
    }

    Nob_File_Paths demo_dirs = {0};
    if (nob_read_entire_dir("demos", &demo_dirs))
    {
        for (size_t d = 0; d < demo_dirs.count; d++)
        {
            const char* dname = demo_dirs.items[d];
            if (dname[0] == '.') continue;

            const char* demo_dir = nob_temp_sprintf("demos/%s", dname);
            const char* extra_inc = nob_temp_sprintf("-I%s", demo_dir);

            bool has_tests = false;
            Nob_File_Paths demo_files = {0};
            if (!nob_read_entire_dir(demo_dir, &demo_files)) continue;

            for (size_t i = 0; i < demo_files.count; i++)
            {
                const char* file = demo_files.items[i];
                size_t flen = strlen(file);
                if (flen < 3 || strcmp(file + flen - 2, ".c") != 0) continue;
                if (strncmp(file, "test_", 5) == 0) { has_tests = true; break; }
            }

            if (!has_tests) continue;

            for (size_t i = 0; i < demo_files.count; i++)
            {
                const char* file = demo_files.items[i];
                size_t flen = strlen(file);
                if (flen < 3 || strcmp(file + flen - 2, ".c") != 0) continue;
                if (strcmp(file, "main.c") == 0) continue;

                const char* src = nob_temp_sprintf("%s/%s", demo_dir, file);
                const char* obj = nob_temp_sprintf(BUILD_DIR "/dtest.%s.%s.o", dname, file);

                nob_da_append(&obj_files, nob_temp_strdup(obj));

                if (needs_compile(src, obj))
                {
                    nob_log(NOB_INFO, "Compiling demo test: %s/%s", dname, file);
                    if (!compile_c_to_obj_ex(src, obj, (const char*[]){ extra_inc }, 1)) return false;
                    any_recompiled = true;
                }
            }

        }
    }

    Nob_File_Paths lib_dirs = {0};
    if (nob_read_entire_dir("lib", &lib_dirs))
    {
        for (size_t ld = 0; ld < lib_dirs.count; ld++)
        {
            const char* lname = lib_dirs.items[ld];
            if (lname[0] == '.') continue;
            const char* lib_dir = nob_temp_sprintf("lib/%s", lname);
            if (nob_get_file_type(lib_dir) != NOB_FILE_DIRECTORY) continue;

            Nob_File_Paths lib_files = {0};
            if (!nob_read_entire_dir(lib_dir, &lib_files)) continue;

            for (size_t li = 0; li < lib_files.count; li++)
            {
                const char* file = lib_files.items[li];
                size_t flen = strlen(file);
                if (flen < 3 || strcmp(file + flen - 2, ".c") != 0) continue;

                const char* src = nob_temp_sprintf("%s/%s", lib_dir, file);
                const char* obj = nob_temp_sprintf(BUILD_DIR "/test.lib.%s.%s.o", lname, file);

                nob_da_append(&obj_files, nob_temp_strdup(obj));

                if (needs_compile(src, obj))
                {
                    nob_log(NOB_INFO, "Compiling lib for test: %s/%s", lname, file);
                    if (!compile_c_to_obj(src, obj)) return false;
                    any_recompiled = true;
                }
            }
        }
    }

    const char* lib_dep = BUILD_DIR "/libmelody.a";
    bool melody_changed = nob_needs_rebuild(BUILD_DIR "/tests", &lib_dep, 1) != 0;

    if (any_recompiled || melody_changed || nob_file_exists(BUILD_DIR "/tests") != 1)
    {
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "clang", "-g");

        for (size_t i = 0; i < obj_files.count; i++)
            nob_cmd_append(&cmd, obj_files.items[i]);

        nob_cmd_append(&cmd, "-o", BUILD_DIR "/tests");
        cmd_append_melody_link_deps(&cmd);
        nob_cmd_append(&cmd, "-lm");

        if (!nob_cmd_run_sync(cmd)) return false;
    }

    return true;
}

void compdb_append_args(Nob_String_Builder* sb, const char** args, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        nob_sb_append_cstr(sb, " ");
        bool needs_quote = strchr(args[i], ' ') || strchr(args[i], '"');
        if (needs_quote) nob_sb_append_cstr(sb, "\"");
        nob_sb_append_cstr(sb, args[i]);
        if (needs_quote) nob_sb_append_cstr(sb, "\"");
    }
}

void compdb_write_json_string(FILE* f, const char* s, size_t len)
{
    fputc('"', f);
    for (size_t i = 0; i < len; i++)
    {
        char c = s[i];
        if (c == '"' || c == '\\') fputc('\\', f);
        fputc(c, f);
    }
    fputc('"', f);
}

void compdb_entry(FILE* f, const char* dir, const char* file, Nob_String_Builder* args_sb, bool* first)
{
    if (!*first) fprintf(f, ",\n");
    *first = false;

    fprintf(f, "  {\n");
    fprintf(f, "    \"directory\": \"%s\",\n", dir);
    fprintf(f, "    \"command\": ");
    compdb_write_json_string(f, args_sb->items, args_sb->count);
    fprintf(f, ",\n");
    fprintf(f, "    \"file\": \"%s\"\n", file);
    fprintf(f, "  }");
}

bool build_compdb(void)
{
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd)))
    {
        nob_log(NOB_ERROR, "Failed to get cwd");
        return false;
    }

    FILE* f = fopen("compile_commands.json", "w");
    if (!f)
    {
        nob_log(NOB_ERROR, "Failed to open compile_commands.json for writing");
        return false;
    }

    fprintf(f, "[\n");
    bool first = true;

    Nob_File_Paths c_files = {0};
    Nob_File_Paths cpp_files = {0};
    Nob_File_Paths m_files = {0};

    collect_c_files("melody", &c_files, &cpp_files, &m_files);

    {
        Nob_File_Paths entries = {0};
        if (nob_read_entire_dir("tests", &entries))
        {
            for (size_t i = 0; i < entries.count; i++)
            {
                const char* name = entries.items[i];
                if (name[0] == '.') continue;
                size_t len = strlen(name);
                if (len < 3 || name[len-2] != '.' || name[len-1] != 'c') continue;
                nob_da_append(&c_files, nob_temp_strdup(nob_temp_sprintf("tests/%s", name)));
            }
        }
    }

    {
        Nob_File_Paths entries = {0};
        if (nob_read_entire_dir("examples", &entries))
        {
            for (size_t i = 0; i < entries.count; i++)
            {
                const char* name = entries.items[i];
                if (name[0] == '.') continue;
                size_t len = strlen(name);
                if (len < 3 || name[len-2] != '.' || name[len-1] != 'c') continue;
                nob_da_append(&c_files, nob_temp_strdup(nob_temp_sprintf("examples/%s", name)));
            }
        }
    }

    {
        Nob_File_Paths entries = {0};
        if (nob_read_entire_dir("demos", &entries))
        {
            for (size_t i = 0; i < entries.count; i++)
            {
                const char* dname = entries.items[i];
                if (dname[0] == '.') continue;
                const char* demo_dir = nob_temp_sprintf("demos/%s", dname);
                if (nob_get_file_type(demo_dir) != NOB_FILE_DIRECTORY) continue;

                Nob_File_Paths demo_entries = {0};
                if (nob_read_entire_dir(demo_dir, &demo_entries))
                {
                    for (size_t j = 0; j < demo_entries.count; j++)
                    {
                        const char* fname = demo_entries.items[j];
                        size_t flen = strlen(fname);
                        if (flen < 3 || fname[flen-2] != '.' || fname[flen-1] != 'c') continue;
                        nob_da_append(&c_files, nob_temp_strdup(nob_temp_sprintf("%s/%s", demo_dir, fname)));
                    }
                }
            }
        }
    }

    {
        Nob_File_Paths lib_entries = {0};
        if (nob_read_entire_dir("lib", &lib_entries))
        {
            for (size_t i = 0; i < lib_entries.count; i++)
            {
                const char* lname = lib_entries.items[i];
                if (lname[0] == '.') continue;
                const char* lib_dir = nob_temp_sprintf("lib/%s", lname);
                if (nob_get_file_type(lib_dir) != NOB_FILE_DIRECTORY) continue;

                Nob_File_Paths lf = {0};
                if (nob_read_entire_dir(lib_dir, &lf))
                {
                    for (size_t j = 0; j < lf.count; j++)
                    {
                        const char* fname = lf.items[j];
                        size_t flen = strlen(fname);
                        if (flen < 3 || fname[flen-2] != '.' || fname[flen-1] != 'c') continue;
                        nob_da_append(&c_files, nob_temp_strdup(nob_temp_sprintf("%s/%s", lib_dir, fname)));
                    }
                }
            }
        }
    }

    for (size_t i = 0; i < c_files.count; i++)
    {
        Nob_String_Builder sb = {0};
        nob_sb_append_cstr(&sb, "clang");
        compdb_append_args(&sb, CFLAGS_BASE, NOB_ARRAY_LEN(CFLAGS_BASE));
        nob_sb_append_cstr(&sb, " -g -O0 -DTRACY_ENABLE -DMEL_CONFIG_DEBUG_ALLOCATOR");
        compdb_append_args(&sb, INCLUDE_PATHS, NOB_ARRAY_LEN(INCLUDE_PATHS));
        nob_sb_append_cstr(&sb, " -c ");
        nob_sb_append_cstr(&sb, c_files.items[i]);

        compdb_entry(f, cwd, c_files.items[i], &sb, &first);
        free(sb.items);
    }

    for (size_t i = 0; i < m_files.count; i++)
    {
        Nob_String_Builder sb = {0};
        nob_sb_append_cstr(&sb, "clang");
        compdb_append_args(&sb, CFLAGS_BASE, NOB_ARRAY_LEN(CFLAGS_BASE));
        nob_sb_append_cstr(&sb, " -g -O0 -DTRACY_ENABLE -DMEL_CONFIG_DEBUG_ALLOCATOR");
        nob_sb_append_cstr(&sb, " -fobjc-arc");
        compdb_append_args(&sb, INCLUDE_PATHS, NOB_ARRAY_LEN(INCLUDE_PATHS));
        nob_sb_append_cstr(&sb, " -c ");
        nob_sb_append_cstr(&sb, m_files.items[i]);

        compdb_entry(f, cwd, m_files.items[i], &sb, &first);
        free(sb.items);
    }

    for (size_t i = 0; i < cpp_files.count; i++)
    {
        Nob_String_Builder sb = {0};
        nob_sb_append_cstr(&sb, "clang++");
        compdb_append_args(&sb, CXXFLAGS_BASE, NOB_ARRAY_LEN(CXXFLAGS_BASE));
        nob_sb_append_cstr(&sb, " -g -O0 -DMEL_CONFIG_DEBUG_ALLOCATOR");
        compdb_append_args(&sb, INCLUDE_PATHS, NOB_ARRAY_LEN(INCLUDE_PATHS));
        nob_sb_append_cstr(&sb, " -c ");
        nob_sb_append_cstr(&sb, cpp_files.items[i]);

        compdb_entry(f, cwd, cpp_files.items[i], &sb, &first);
        free(sb.items);
    }

    for (size_t i = 0; i < NOB_ARRAY_LEN(CIMGUI_SOURCES); i++)
    {
        Nob_String_Builder sb = {0};
        nob_sb_append_cstr(&sb, "clang++");
        compdb_append_args(&sb, CXXFLAGS_BASE, NOB_ARRAY_LEN(CXXFLAGS_BASE));
        nob_sb_append_cstr(&sb, " -g -O0 -DMEL_CONFIG_DEBUG_ALLOCATOR");
        compdb_append_args(&sb, INCLUDE_PATHS, NOB_ARRAY_LEN(INCLUDE_PATHS));
        nob_sb_append_cstr(&sb, " \"-DIMGUI_IMPL_API=extern \\\"C\\\"\"");
        nob_sb_append_cstr(&sb, " -DCIMGUI_USE_SDL3 -DCIMGUI_USE_VULKAN -DIMGUI_IMPL_VULKAN_USE_VOLK");
        nob_sb_append_cstr(&sb, " -c ");
        nob_sb_append_cstr(&sb, CIMGUI_SOURCES[i]);

        compdb_entry(f, cwd, CIMGUI_SOURCES[i], &sb, &first);
        free(sb.items);
    }

    {
        const char* tracy_src = SUCK_DIR "/third-party/tracy/public/TracyClient.cpp";
        Nob_String_Builder sb = {0};
        nob_sb_append_cstr(&sb, "clang++");
        compdb_append_args(&sb, CXXFLAGS_BASE, NOB_ARRAY_LEN(CXXFLAGS_BASE));
        nob_sb_append_cstr(&sb, " -g -O0 -DTRACY_ENABLE -Wno-deprecated-declarations");
        compdb_append_args(&sb, INCLUDE_PATHS, NOB_ARRAY_LEN(INCLUDE_PATHS));
        nob_sb_append_cstr(&sb, " -c ");
        nob_sb_append_cstr(&sb, tracy_src);

        compdb_entry(f, cwd, tracy_src, &sb, &first);
        free(sb.items);
    }

    {
        const char* flecs_src = SUCK_DIR "/flecs/distr/flecs.c";
        Nob_String_Builder sb = {0};
        nob_sb_append_cstr(&sb, "clang");
        compdb_append_args(&sb, CFLAGS_BASE, NOB_ARRAY_LEN(CFLAGS_BASE));
        nob_sb_append_cstr(&sb, " -g -O0 -DTRACY_ENABLE -DMEL_CONFIG_DEBUG_ALLOCATOR");
        compdb_append_args(&sb, INCLUDE_PATHS, NOB_ARRAY_LEN(INCLUDE_PATHS));
        nob_sb_append_cstr(&sb, " -c ");
        nob_sb_append_cstr(&sb, flecs_src);

        compdb_entry(f, cwd, flecs_src, &sb, &first);
        free(sb.items);
    }

    fprintf(f, "\n]\n");
    fclose(f);

    free(c_files.items);
    free(cpp_files.items);
    free(m_files.items);

    nob_log(NOB_WARNING, "Generated compile_commands.json");
    return true;
}

bool build_widget_example(const char* name)
{
    const char* demo_src = nob_temp_sprintf("examples/example.%s.c", name);
    const char* demo_out = nob_temp_sprintf(BUILD_DIR "/example.%s", name);

    static const char* WIDGET_DEMO_SOURCES[] = {
        "melody/debug.backtrace.c",
        "melody/ui.widget.c",
        "melody/ui.layout.c",
        "melody/ui.layout.box.c",
    };

    bool any_recompiled = false;
    Nob_File_Paths obj_files = {0};

    for (size_t i = 0; i < NOB_ARRAY_LEN(WIDGET_DEMO_SOURCES); i++)
    {
        const char* src = WIDGET_DEMO_SOURCES[i];
        const char* base = strrchr(src, '/');
        base = base ? base + 1 : src;

        char obj_name[256];
        snprintf(obj_name, sizeof(obj_name), "%s", base);
        size_t len = strlen(obj_name);
        obj_name[len - 1] = 'o';

        const char* obj = nob_temp_sprintf(BUILD_DIR "/%s", obj_name);
        nob_da_append(&obj_files, nob_temp_strdup(obj));

        if (needs_compile(src, obj))
        {
            if (!compile_c_to_obj(src, obj)) return false;
            any_recompiled = true;
        }
    }

    const char* demo_obj = nob_temp_sprintf(BUILD_DIR "/example.%s.o", name);
    nob_da_append(&obj_files, nob_temp_strdup(demo_obj));

    if (needs_compile(demo_src, demo_obj))
    {
        if (!compile_c_to_obj(demo_src, demo_obj)) return false;
        any_recompiled = true;
    }

    if (any_recompiled || nob_file_exists(demo_out) != 1)
    {
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "clang", "-g");

        for (size_t i = 0; i < obj_files.count; i++)
            nob_cmd_append(&cmd, obj_files.items[i]);

        nob_cmd_append(&cmd, "-o", demo_out);
        nob_cmd_append(&cmd, "-lSDL3");
        nob_cmd_append(&cmd, "-L/opt/homebrew/lib");
        nob_cmd_append(&cmd, "-Wl,-rpath,/opt/homebrew/lib");
        nob_cmd_append(&cmd, "-lm");
        cmd_append_link_flags(&cmd);

        if (!nob_cmd_run_sync(cmd)) return false;
    }

    return true;
}

bool build_example(const char* name)
{
    if (!build_melody()) return false;

    const char* demo_src = nob_temp_sprintf("examples/example.%s.c", name);
    const char* demo_out = nob_temp_sprintf(BUILD_DIR "/example.%s", name);
    const char* demo_obj = nob_temp_sprintf(BUILD_DIR "/example.%s.o", name);
    const char* app_entry_obj = BUILD_DIR "/core.app.entry.o";
    const char* app_callbacks_obj = BUILD_DIR "/core.app.callbacks.o";
    bool needs_app_entry = false;
    bool needs_app_callbacks = strcmp(name, "swapchain.headless") != 0;
    if (strcmp(name, "swapchain.headless") != 0)
    {
        Nob_String_Builder sb = {0};
        if (nob_read_entire_file(demo_src, &sb))
            needs_app_entry = strstr(sb.items, "SDL_MAIN_USE_CALLBACKS") == NULL;
        free(sb.items);
    }

    bool any_recompiled = false;

    if (needs_compile(demo_src, demo_obj))
    {
        if (!compile_c_to_obj(demo_src, demo_obj)) return false;
        any_recompiled = true;
    }

    if (needs_app_callbacks && needs_compile("melody/core.app.callbacks.c", app_callbacks_obj))
    {
        if (!compile_c_to_obj("melody/core.app.callbacks.c", app_callbacks_obj)) return false;
        any_recompiled = true;
    }

    if (needs_app_entry && needs_compile("melody/core.app.entry.c", app_entry_obj))
    {
        if (!compile_c_to_obj("melody/core.app.entry.c", app_entry_obj)) return false;
        any_recompiled = true;
    }

    const char* lib_dep = BUILD_DIR "/libmelody.a";
    bool melody_changed = nob_needs_rebuild(demo_out, &lib_dep, 1) != 0;

    if (any_recompiled || melody_changed || nob_file_exists(demo_out) != 1)
    {
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "clang", "-g");
        nob_cmd_append(&cmd, demo_obj);
        if (needs_app_callbacks) nob_cmd_append(&cmd, app_callbacks_obj);
        if (needs_app_entry) nob_cmd_append(&cmd, app_entry_obj);
        nob_cmd_append(&cmd, "-o", demo_out);
        cmd_append_melody_link_deps(&cmd);

        if (!nob_cmd_run_sync(cmd)) return false;
    }

    return true;
}

bool build_demo(const char* name)
{
    if (!build_melody()) return false;

    const char* demo_dir = nob_temp_sprintf("demos/%s", name);
    const char* demo_out = nob_temp_sprintf(BUILD_DIR "/demo.%s", name);
    const char* app_entry_obj = BUILD_DIR "/core.app.entry.o";
    const char* app_callbacks_obj = BUILD_DIR "/core.app.callbacks.o";
    bool needs_app_entry = true;
    bool needs_app_callbacks = true;

    bool any_recompiled = false;
    Nob_File_Paths obj_files = {0};

    Nob_File_Paths src_files = {0};
    nob_read_entire_dir(demo_dir, &src_files);

    for (size_t i = 0; i < src_files.count; i++)
    {
        const char* file = src_files.items[i];
        size_t len = strlen(file);
        if (len < 3 || strcmp(file + len - 2, ".c") != 0) continue;

        const char* src = nob_temp_sprintf("%s/%s", demo_dir, file);
        const char* obj = nob_temp_sprintf(BUILD_DIR "/demo.%s.%s.o", name, file);

        Nob_String_Builder sb = {0};
        if (nob_read_entire_file(src, &sb))
            if (strstr(sb.items, "SDL_MAIN_USE_CALLBACKS") != NULL)
                needs_app_entry = false;
        free(sb.items);

        nob_da_append(&obj_files, nob_temp_strdup(obj));

        if (needs_compile(src, obj))
        {
            if (!compile_c_to_obj(src, obj)) return false;
            any_recompiled = true;
        }
    }

    if (needs_app_callbacks && needs_compile("melody/core.app.callbacks.c", app_callbacks_obj))
    {
        if (!compile_c_to_obj("melody/core.app.callbacks.c", app_callbacks_obj)) return false;
        any_recompiled = true;
    }

    if (needs_app_entry && needs_compile("melody/core.app.entry.c", app_entry_obj))
    {
        if (!compile_c_to_obj("melody/core.app.entry.c", app_entry_obj)) return false;
        any_recompiled = true;
    }

    Nob_File_Paths lib_dirs = {0};
    if (nob_read_entire_dir("lib", &lib_dirs))
    {
        for (size_t d = 0; d < lib_dirs.count; d++)
        {
            const char* lname = lib_dirs.items[d];
            if (lname[0] == '.') continue;
            const char* lib_dir = nob_temp_sprintf("lib/%s", lname);
            if (nob_get_file_type(lib_dir) != NOB_FILE_DIRECTORY) continue;

            Nob_File_Paths lib_files = {0};
            if (!nob_read_entire_dir(lib_dir, &lib_files)) continue;

            for (size_t i = 0; i < lib_files.count; i++)
            {
                const char* file = lib_files.items[i];
                size_t len = strlen(file);
                if (len < 3 || strcmp(file + len - 2, ".c") != 0) continue;

                const char* src = nob_temp_sprintf("%s/%s", lib_dir, file);
                const char* obj = nob_temp_sprintf(BUILD_DIR "/lib.%s.%s.o", lname, file);

                nob_da_append(&obj_files, nob_temp_strdup(obj));

                if (needs_compile(src, obj))
                {
                    nob_log(NOB_INFO, "Compiling lib: %s/%s", lname, file);
                    if (!compile_c_to_obj(src, obj)) return false;
                    any_recompiled = true;
                }
            }
        }
    }

    const char* lib_dep = BUILD_DIR "/libmelody.a";
    bool melody_changed = nob_needs_rebuild(demo_out, &lib_dep, 1) != 0;

    if (any_recompiled || melody_changed || nob_file_exists(demo_out) != 1)
    {
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "clang", "-g");
        if (needs_app_callbacks) nob_cmd_append(&cmd, app_callbacks_obj);
        if (needs_app_entry) nob_cmd_append(&cmd, app_entry_obj);

        for (size_t i = 0; i < obj_files.count; i++)
            nob_cmd_append(&cmd, obj_files.items[i]);

        nob_cmd_append(&cmd, "-o", demo_out);
        cmd_append_melody_link_deps(&cmd);

        if (!nob_cmd_run_sync(cmd)) return false;
    }

    return true;
}

int main(int argc, char** argv)
{
    nob_minimal_log_level = NOB_WARNING;
    NOB_GO_REBUILD_URSELF(argc, argv);

    if (!nob_mkdir_if_not_exists(BUILD_DIR)) return 1;

    int arg_idx = 1;

    while (arg_idx < argc && argv[arg_idx][0] == '-')
    {
        if (strcmp(argv[arg_idx], "--verbose") == 0)
        {
            g_verbose = true;
            nob_minimal_log_level = NOB_INFO;
        }
        else if (strcmp(argv[arg_idx], "--timings") == 0)
        {
            g_timings = true;
        }
        else if (strncmp(argv[arg_idx], "--backend=", 10) == 0)
        {
            const char* name = argv[arg_idx] + 10;
            bool found = false;
            for (size_t i = 0; i < NOB_ARRAY_LEN(BACKENDS); i++)
            {
                if (strcmp(name, BACKENDS[i].name) == 0)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                nob_log(NOB_ERROR, "Unknown backend: %s (available: vulkan, metal, dx12, opengl, webgpu)", name);
                return 1;
            }
            g_backend = name;
        }
        else
        {
            nob_log(NOB_ERROR, "Unknown flag: %s", argv[arg_idx]);
            return 1;
        }
        arg_idx++;
    }

    if (arg_idx < argc && strcmp(argv[arg_idx], "release") == 0)
    {
        g_build_mode = BUILD_MODE_RELEASE;
        nob_log(NOB_WARNING, "Build mode: RELEASE");
        arg_idx++;
    }
    else if (arg_idx < argc && strcmp(argv[arg_idx], "sanitize") == 0)
    {
        g_build_mode = BUILD_MODE_SANITIZE;
        nob_log(NOB_WARNING, "Build mode: SANITIZE (ASan + UBSan)");
        arg_idx++;
    }

    if (strcmp(g_backend, "vulkan") != 0)
        nob_log(NOB_WARNING, "GPU backend: %s", g_backend);

    const char* subcmd = arg_idx < argc ? argv[arg_idx] : "build";

    if (strcmp(subcmd, "libs") == 0)
    {
        if (!build_libs()) return 1;
    }
    else if (strcmp(subcmd, "melody") == 0)
    {
        if (!build_melody()) return 1;
    }
    else if (strcmp(subcmd, "build") == 0)
    {
        if (!build_melody()) return 1;
    }
    else if (strcmp(subcmd, "test") == 0)
    {
        if (!build_tests()) return 1;

        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, BUILD_DIR "/tests");
        for (int i = arg_idx + 1; i < argc; i++)
            nob_cmd_append(&cmd, argv[i]);

        return nob_cmd_run_sync(cmd) ? 0 : 1;
    }
    else if (strcmp(subcmd, "run") == 0)
    {
        nob_log(NOB_ERROR, "No game target. Use './nob example <name>' or './nob demo <name>'");
        return 1;
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd,
            /* "env", */
            /* "DYLD_LIBRARY_PATH=/opt/homebrew/lib:" SUCK_DIR "/third-party/slang/lib", */
            /* "VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json", */
            /* "VK_LAYER_PATH=/opt/homebrew/etc/vulkan/explicit_layer.d", */
            BUILD_DIR "/melody");
        return nob_cmd_run_sync(cmd) ? 0 : 1;
    }
    else if (strcmp(subcmd, "run-only") == 0)
    {
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, BUILD_DIR "/melody");
        return nob_cmd_run_sync(cmd) ? 0 : 1;
    }
    else if (strcmp(subcmd, "example") == 0)
    {
        const char* name = (arg_idx + 1) < argc ? argv[arg_idx + 1] : "nctrl";
        nob_log(NOB_INFO, "Building example: %s", name);

        bool ok;
        if (strcmp(name, "widget") == 0)
            ok = build_widget_example(name);
        else
            ok = build_example(name);
        if (!ok) return 1;

        nob_log(NOB_INFO, "Running example: %s", name);
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, nob_temp_sprintf(BUILD_DIR "/example.%s", name));
        return nob_cmd_run_sync(cmd) ? 0 : 1;
    }
    else if (strcmp(subcmd, "demo") == 0)
    {
        const char* name = (arg_idx + 1) < argc ? argv[arg_idx + 1] : NULL;
        if (!name)
        {
            nob_log(NOB_ERROR, "Usage: ./nob demo <name>");
            return 1;
        }
        nob_log(NOB_INFO, "Building demo: %s", name);

        if (!build_demo(name)) return 1;

        nob_log(NOB_INFO, "Running demo: %s", name);
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, nob_temp_sprintf(BUILD_DIR "/demo.%s", name));
        for (int i = arg_idx + 2; i < argc; i++)
            nob_cmd_append(&cmd, argv[i]);
        return nob_cmd_run_sync(cmd) ? 0 : 1;
    }
    else if (strcmp(subcmd, "compdb") == 0)
    {
        if (!build_compdb()) return 1;
    }
    else if (strcmp(subcmd, "clean") == 0)
    {
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "rm", "-rf", BUILD_DIR);
        return nob_cmd_run_sync(cmd) ? 0 : 1;
    }
    else if (strcmp(subcmd, "debug") == 0)
    {
        nob_log(NOB_ERROR, "No game target. Use './nob example <name>' or './nob demo <name>'");
        return 1;
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "lldb", BUILD_DIR "/melody");
        nob_cmd_append(&cmd,
            "-o", "env VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json",
            "-o", "env VK_LAYER_PATH=/opt/homebrew/etc/vulkan/explicit_layer.d",
            "-o", "run",
            "-o", "bt");
        for (int i = 2; i < argc; i++)
        {
            nob_cmd_append(&cmd, argv[i]);
        }
        return nob_cmd_run_sync(cmd) ? 0 : 1;
    }
    else
    {
        nob_log(NOB_ERROR, "Unknown command: %s", subcmd);
        nob_log(NOB_ERROR, "Usage: ./nob [--verbose] [--timings] [release|sanitize] [libs|melody|build|test|demo|run|run-only|debug|compdb|clean]");
        return 1;
    }

    nob_minimal_log_level = NOB_INFO;
    nob_log(NOB_INFO, "OK!");

    return 0;
}
