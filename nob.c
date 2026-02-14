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

#define TIMING_LEVEL (g_verbose ? NOB_INFO : NOB_WARNING)

static const char* INCLUDE_PATHS[] = {
    "-I/opt/homebrew/include",
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
    "-Igame",
};

static const char* LIB_PATHS[] = {
    "-L/opt/homebrew/lib",
    "-L" SUCK_DIR "/third-party/slang/lib",
};

static const char* LIBS[] = {
    "-lSDL3",
    "-lphysfs",
    "-lslang",
    "-lcjson",
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

bool compile_c_to_obj(const char* src, const char* obj)
{
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang");

    cmd_append_cflags(&cmd);

    for (size_t i = 0; i < NOB_ARRAY_LEN(INCLUDE_PATHS); i++)
        nob_cmd_append(&cmd, INCLUDE_PATHS[i]);

    nob_cmd_append(&cmd, "-MD", "-MF", obj_to_depfile(obj));
    nob_cmd_append(&cmd, "-c", src, "-o", obj);

    return nob_cmd_run_sync(cmd);
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

bool build_main(void)
{
    if (!build_melody()) return false;
    if (!copy_assets()) return false;

    uint64_t t_total_start = nob_nanos_since_unspecified_epoch();
    bool any_recompiled = false;

    Nob_File_Paths c_files = {0};
    Nob_File_Paths cpp_files = {0};
    Nob_File_Paths m_files = {0};
    Nob_File_Paths obj_files = {0};

    if (!collect_c_files("game", &c_files, &cpp_files, &m_files)) return false;

    uint64_t t0 = nob_nanos_since_unspecified_epoch();

    for (size_t i = 0; i < c_files.count; i++)
    {
        const char* src = c_files.items[i];
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

    uint64_t t1 = nob_nanos_since_unspecified_epoch();

    const char* lib_dep = BUILD_DIR "/libmelody.a";
    bool melody_changed = nob_needs_rebuild(BUILD_DIR "/melody", &lib_dep, 1) != 0;

    if (any_recompiled || melody_changed || nob_file_exists(BUILD_DIR "/melody") != 1)
    {
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "clang", "-g");

        for (size_t i = 0; i < obj_files.count; i++)
            nob_cmd_append(&cmd, obj_files.items[i]);

        nob_cmd_append(&cmd, "-o", BUILD_DIR "/melody");
        cmd_append_melody_link_deps(&cmd);

        if (!nob_cmd_run_sync(cmd)) return false;
    }
    else
    {
        nob_log(NOB_INFO, "Everything up to date, nothing to do");
    }

    uint64_t t2 = nob_nanos_since_unspecified_epoch();

    double game_c_ms = (t1 - t0) / 1e6;
    double link_ms   = (t2 - t1) / 1e6;
    double total_ms  = (t2 - t_total_start) / 1e6;

    if (g_timings)
    {
        nob_log(TIMING_LEVEL, "──── Game Timings ─────");
        nob_log(TIMING_LEVEL, "  game C          : %8.1f ms", game_c_ms);
        nob_log(TIMING_LEVEL, "  link            : %8.1f ms", link_ms);
        nob_log(TIMING_LEVEL, "  ─────────────────────────");
        nob_log(TIMING_LEVEL, "  TOTAL           : %8.1f ms", total_ms);
    }

    return true;
}

static const char* ALLOCATOR_SOURCES[] = {
    "melody/allocator.c",
    "melody/allocator.heap.c",
    "melody/allocator.leak.c",
    "melody/allocator.tracking.c",
    "melody/allocator.arena.c",
    "melody/allocator.pool.c",
    "melody/allocator.stack.c",
    "melody/allocator.block.c",
    "melody/allocator.ring.c",
    "melody/allocator.slab.c",
    "melody/allocator.buddy.c",
};

static const char* NCTRL_SOURCES[] = {
    "melody/ui.native.ctrl.c",
    "melody/ui.layout.c",
};

static const char* FIBER_SOURCES[] = {
    "melody/allocator.vmem.c",
    "melody/async.fiber.c",
};

static const char* CORO_SOURCES[] = {
    "melody/allocator.vmem.c",
    "melody/async.fiber.c",
    "melody/async.coro.c",
    "melody/allocator.c",
    "melody/allocator.heap.c",
};

static const char* WIDGET_SOURCES[] = {
    "melody/ui.widget.c",
    "melody/ui.layout.c",
    "melody/ui.layout.box.c",
};

static const char* SKIPLIST_SOURCES[] = {
    "melody/collection.skiplist.c",
    "melody/allocator.c",
    "melody/allocator.heap.c",
};

static const char* TRIE_SOURCES[] = {
    "melody/collection.trie.c",
    "melody/allocator.c",
    "melody/allocator.heap.c",
};

static const char* BITSET_SOURCES[] = {
    "melody/collection.bitset.c",
    "melody/allocator.c",
    "melody/allocator.heap.c",
};

static const char* BTREE_SOURCES[] = {
    "melody/collection.btree.c",
    "melody/allocator.c",
    "melody/allocator.heap.c",
};

static const char* HASHMAP_SOURCES[] = {
    "melody/collection.hashmap.c",
    "melody/hash.xxh.c",
    "melody/allocator.c",
    "melody/allocator.heap.c",
};

static const char* SET_SOURCES[] = {
    "melody/collection.set.c",
    "melody/collection.hashmap.c",
    "melody/hash.xxh.c",
    "melody/allocator.c",
    "melody/allocator.heap.c",
};

static const char* RBTREE_SOURCES[] = {
    "melody/collection.rbtree.c",
    "melody/allocator.c",
    "melody/allocator.heap.c",
};

static const char* XXH_SOURCES[] = {
    "melody/hash.xxh.c",
};

static const char* GPU_FORMAT_SOURCES[] = {
    "melody/gpu.format.c",
};

static const char* RENDER_GRAPH_SOURCES[] = {
    "melody/render.graph.c",
};

static const char* RENDER_BLACKBOARD_SOURCES[] = {
    "melody/render.blackboard.c",
    "melody/string.str8.c",
    "melody/hash.xxh.c",
    "melody/allocator.c",
    "melody/allocator.heap.c",
};

static const char* SLOTMAP_SOURCES[] = {
    "melody/collection.slotmap.c",
    "melody/allocator.c",
    "melody/allocator.heap.c",
};

static const char* ANIM_TIMELINE_SOURCES[] = {
    "melody/anim.timeline.c",
    "melody/allocator.c",
    "melody/allocator.heap.c",
};

static const char* STR8_SOURCES[] = {
    "melody/string.str8.c",
    "melody/hash.xxh.c",
    "melody/allocator.c",
    "melody/allocator.heap.c",
};

static const char* EVENT_CHANNEL_SOURCES[] = {
    "melody/event.channel.c",
    "melody/allocator.c",
    "melody/allocator.heap.c",
};

typedef struct {
    const char** sources;
    size_t count;
    bool needs_asm;
} Test_Source_Set;

static Test_Source_Set test_source_set_for(const char* test_name)
{
    if (strcmp(test_name, "nctrl") == 0)
        return (Test_Source_Set){ NCTRL_SOURCES, NOB_ARRAY_LEN(NCTRL_SOURCES), false };
    if (strcmp(test_name, "fiber") == 0)
        return (Test_Source_Set){ FIBER_SOURCES, NOB_ARRAY_LEN(FIBER_SOURCES), true };
    if (strcmp(test_name, "coro") == 0)
        return (Test_Source_Set){ CORO_SOURCES, NOB_ARRAY_LEN(CORO_SOURCES), true };
    if (strcmp(test_name, "widget") == 0)
        return (Test_Source_Set){ WIDGET_SOURCES, NOB_ARRAY_LEN(WIDGET_SOURCES), false };
    if (strcmp(test_name, "collection_list") == 0)
        return (Test_Source_Set){ NULL, 0, false };
    if (strcmp(test_name, "collection_skiplist") == 0)
        return (Test_Source_Set){ SKIPLIST_SOURCES, NOB_ARRAY_LEN(SKIPLIST_SOURCES), false };
    if (strcmp(test_name, "collection_trie") == 0)
        return (Test_Source_Set){ TRIE_SOURCES, NOB_ARRAY_LEN(TRIE_SOURCES), false };
    if (strcmp(test_name, "collection_sort") == 0)
        return (Test_Source_Set){ NULL, 0, false };
    if (strcmp(test_name, "collection_btree") == 0)
        return (Test_Source_Set){ BTREE_SOURCES, NOB_ARRAY_LEN(BTREE_SOURCES), false };
    if (strcmp(test_name, "collection_bitset") == 0)
        return (Test_Source_Set){ BITSET_SOURCES, NOB_ARRAY_LEN(BITSET_SOURCES), false };
    if (strcmp(test_name, "collection_hashmap") == 0)
        return (Test_Source_Set){ HASHMAP_SOURCES, NOB_ARRAY_LEN(HASHMAP_SOURCES), false };
    if (strcmp(test_name, "collection_set") == 0)
        return (Test_Source_Set){ SET_SOURCES, NOB_ARRAY_LEN(SET_SOURCES), false };
    if (strcmp(test_name, "collection_rbtree") == 0)
        return (Test_Source_Set){ RBTREE_SOURCES, NOB_ARRAY_LEN(RBTREE_SOURCES), false };
    if (strcmp(test_name, "xxh") == 0)
        return (Test_Source_Set){ XXH_SOURCES, NOB_ARRAY_LEN(XXH_SOURCES), false };
    if (strcmp(test_name, "gpu_format") == 0)
        return (Test_Source_Set){ GPU_FORMAT_SOURCES, NOB_ARRAY_LEN(GPU_FORMAT_SOURCES), false };
    if (strcmp(test_name, "render_graph") == 0)
        return (Test_Source_Set){ RENDER_GRAPH_SOURCES, NOB_ARRAY_LEN(RENDER_GRAPH_SOURCES), false };
    if (strcmp(test_name, "render_blackboard") == 0)
        return (Test_Source_Set){ RENDER_BLACKBOARD_SOURCES, NOB_ARRAY_LEN(RENDER_BLACKBOARD_SOURCES), false };
    if (strcmp(test_name, "collection_slotmap") == 0)
        return (Test_Source_Set){ SLOTMAP_SOURCES, NOB_ARRAY_LEN(SLOTMAP_SOURCES), false };
    if (strcmp(test_name, "anim_timeline") == 0)
        return (Test_Source_Set){ ANIM_TIMELINE_SOURCES, NOB_ARRAY_LEN(ANIM_TIMELINE_SOURCES), false };
    if (strcmp(test_name, "string_str8") == 0)
        return (Test_Source_Set){ STR8_SOURCES, NOB_ARRAY_LEN(STR8_SOURCES), false };
    if (strcmp(test_name, "event_channel") == 0)
        return (Test_Source_Set){ EVENT_CHANNEL_SOURCES, NOB_ARRAY_LEN(EVENT_CHANNEL_SOURCES), false };
    return (Test_Source_Set){ ALLOCATOR_SOURCES, NOB_ARRAY_LEN(ALLOCATOR_SOURCES), false };
}

bool build_test(const char* test_name)
{
    const char* test_src = nob_temp_sprintf("tests/test_%s.c", test_name);
    const char* test_out = nob_temp_sprintf(BUILD_DIR "/test_%s", test_name);

    Test_Source_Set ss = test_source_set_for(test_name);

    Nob_File_Paths asm_objs = {0};
    if (ss.needs_asm)
    {
        for (size_t i = 0; i < NOB_ARRAY_LEN(ASM_FILES); i++)
        {
            const char* src = ASM_FILES[i];
            const char* base = strrchr(src, '/');
            base = base ? base + 1 : src;

            char obj_name[256];
            snprintf(obj_name, sizeof(obj_name), "%s", base);
            size_t len = strlen(obj_name);
            obj_name[len - 1] = 'o';
            obj_name[len - 0] = '\0';

            const char* obj = nob_temp_sprintf(BUILD_DIR "/%s", obj_name);
            nob_da_append(&asm_objs, nob_temp_strdup(obj));

            if (!compile_asm_to_obj(src, obj)) return false;
        }
    }

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang");

    cmd_append_cflags(&cmd);
    nob_cmd_append(&cmd, "-UTRACY_ENABLE");

    for (size_t i = 0; i < NOB_ARRAY_LEN(INCLUDE_PATHS); i++)
        nob_cmd_append(&cmd, INCLUDE_PATHS[i]);

    nob_cmd_append(&cmd, test_src);

    for (size_t i = 0; i < ss.count; i++)
        nob_cmd_append(&cmd, ss.sources[i]);

    for (size_t i = 0; i < asm_objs.count; i++)
        nob_cmd_append(&cmd, asm_objs.items[i]);

    nob_cmd_append(&cmd, "-o", test_out);
    nob_cmd_append(&cmd, "-lm");

    if (g_build_mode == BUILD_MODE_SANITIZE)
        nob_cmd_append(&cmd, "-fsanitize=address,undefined");

    return nob_cmd_run_sync(cmd);
}

bool build_widget_demo(const char* demo_name)
{
    const char* demo_src = nob_temp_sprintf("demos/demo.%s.c", demo_name);
    const char* demo_out = nob_temp_sprintf(BUILD_DIR "/demo.%s", demo_name);

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

    const char* demo_obj = nob_temp_sprintf(BUILD_DIR "/demo.%s.o", demo_name);
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

bool build_demo(const char* demo_name)
{
    if (!build_melody()) return false;

    const char* demo_src = nob_temp_sprintf("demos/demo.%s.c", demo_name);
    const char* demo_out = nob_temp_sprintf(BUILD_DIR "/demo.%s", demo_name);
    const char* demo_obj = nob_temp_sprintf(BUILD_DIR "/demo.%s.o", demo_name);

    bool any_recompiled = false;

    if (needs_compile(demo_src, demo_obj))
    {
        if (!compile_c_to_obj(demo_src, demo_obj)) return false;
        any_recompiled = true;
    }

    const char* lib_dep = BUILD_DIR "/libmelody.a";
    bool melody_changed = nob_needs_rebuild(demo_out, &lib_dep, 1) != 0;

    if (any_recompiled || melody_changed || nob_file_exists(demo_out) != 1)
    {
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "clang", "-g");
        nob_cmd_append(&cmd, demo_obj);
        nob_cmd_append(&cmd, "-o", demo_out);
        cmd_append_melody_link_deps(&cmd);

        if (!nob_cmd_run_sync(cmd)) return false;
    }

    return true;
}

bool run_test(const char* test_name)
{
    const char* test_bin = nob_temp_sprintf(BUILD_DIR "/test_%s", test_name);
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, test_bin);
    return nob_cmd_run_sync(cmd);
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
        if (!build_main()) return 1;
    }
    else if (strcmp(subcmd, "test") == 0)
    {
        const char* tests[] = { "math", "memory", "heap", "leak", "tracking", "arena", "pool", "stack", "block", "ring", "buddy", "slab", "nctrl", "fiber", "coro", "widget", "xxh", "gpu_format", "render_graph", "render_blackboard", "collection_array", "collection_queue", "collection_deque", "collection_ring", "collection_llist", "collection_heap", "collection_list", "collection_rbtree", "collection_skiplist", "collection_trie", "collection_sort", "collection_btree", "collection_bitset", "collection_hashmap", "collection_set", "collection_slotmap", "anim_timeline", "string_str8", "event_channel" };
        bool all_passed = true;

        for (size_t i = 0; i < NOB_ARRAY_LEN(tests); i++)
        {
            nob_log(NOB_INFO, "Building test: %s", tests[i]);
            if (!build_test(tests[i]))
            {
                all_passed = false;
                continue;
            }

            nob_log(NOB_INFO, "Running test: %s", tests[i]);
            if (!run_test(tests[i]))
            {
                all_passed = false;
            }
        }

        if (!all_passed)
        {
            nob_log(NOB_ERROR, "Some tests failed!");
            return 1;
        }
        nob_log(NOB_INFO, "All tests passed!");
    }
    else if (strcmp(subcmd, "run") == 0)
    {
        if (!build_main()) return 1;
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
    else if (strcmp(subcmd, "demo") == 0)
    {
        const char* demo = (arg_idx + 1) < argc ? argv[arg_idx + 1] : "nctrl";
        nob_log(NOB_INFO, "Building demo: %s", demo);

        bool ok;
        if (strcmp(demo, "widget") == 0)
            ok = build_widget_demo(demo);
        else
            ok = build_demo(demo);
        if (!ok) return 1;

        nob_log(NOB_INFO, "Running demo: %s", demo);
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, nob_temp_sprintf(BUILD_DIR "/demo.%s", demo));
        return nob_cmd_run_sync(cmd) ? 0 : 1;
    }
    else if (strcmp(subcmd, "clean") == 0)
    {
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "rm", "-rf", BUILD_DIR);
        return nob_cmd_run_sync(cmd) ? 0 : 1;
    }
    else if (strcmp(subcmd, "debug") == 0)
    {
        if (!build_main()) return 1;
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
        nob_log(NOB_ERROR, "Usage: ./nob [--verbose] [--timings] [release|sanitize] [libs|melody|build|test|demo|run|run-only|debug|clean]");
        return 1;
    }

    nob_minimal_log_level = NOB_INFO;
    nob_log(NOB_INFO, "OK!");

    return 0;
}
