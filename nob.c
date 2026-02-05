#define NOB_IMPLEMENTATION
#include "nob.h"

#define BUILD_DIR "build"
#define SUCK_DIR "/Users/gabbo/repo/suck"

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
    "-Isrc",
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

static const char* CFLAGS[] = {
    "-std=c23",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wno-unused-parameter",
    "-Wno-missing-field-initializers",
    "-Wno-unused-private-field",
    "-Wno-unused-function",
    "-DTRACY_ENABLE",
    "-DFLECS_NO_CPP",
    "-DCIMGUI_DEFINE_ENUMS_AND_STRUCTS",

    "-DMEL_CONFIG_DEBUG_ALLOCATOR",
    "-g", 
    "-O0",
};

static const char* CXXFLAGS[] = {
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
    "-DCIMGUI_DEFINE_ENUMS_AND_STRUCTS",

    "-DMEL_CONFIG_DEBUG_ALLOCATOR",
    "-g",
    "-O0",
};

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

bool compile_cpp_to_obj(const char* src, const char* obj, Cpp_Mode mode)
{
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang++");

    for (size_t i = 0; i < NOB_ARRAY_LEN(CXXFLAGS); i++)
        nob_cmd_append(&cmd, CXXFLAGS[i]);

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

    nob_cmd_append(&cmd, "-c", src, "-o", obj);

    return nob_cmd_run_sync(cmd);
}

bool copy_assets(void)
{
    const char* dest = BUILD_DIR "/assets";
    if (!nob_mkdir_if_not_exists(dest)) return false;

    Nob_File_Paths entries = {0};
    if (!nob_read_entire_dir("assets", &entries)) return true;

    for (size_t i = 0; i < entries.count; i++)
    {
        const char* name = entries.items[i];
        if (name[0] == '.') continue;

        const char* src_path = nob_temp_sprintf("assets/%s", name);
        const char* dst_path = nob_temp_sprintf("%s/%s", dest, name);

        if (!nob_copy_file(src_path, dst_path))
        {
            nob_log(NOB_WARNING, "Failed to copy asset: %s", name);
        }
    }

    return true;
}

bool compile_c_to_obj(const char* src, const char* obj)
{
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang");

    for (size_t i = 0; i < NOB_ARRAY_LEN(CFLAGS); i++)
        nob_cmd_append(&cmd, CFLAGS[i]);

    for (size_t i = 0; i < NOB_ARRAY_LEN(INCLUDE_PATHS); i++)
        nob_cmd_append(&cmd, INCLUDE_PATHS[i]);

    nob_cmd_append(&cmd, "-c", src, "-o", obj);

    return nob_cmd_run_sync(cmd);
}

bool compile_m_to_obj(const char* src, const char* obj)
{
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang");

    for (size_t i = 0; i < NOB_ARRAY_LEN(CFLAGS); i++)
        nob_cmd_append(&cmd, CFLAGS[i]);

    nob_cmd_append(&cmd, "-fobjc-arc");

    for (size_t i = 0; i < NOB_ARRAY_LEN(INCLUDE_PATHS); i++)
        nob_cmd_append(&cmd, INCLUDE_PATHS[i]);

    nob_cmd_append(&cmd, "-c", src, "-o", obj);

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

void collect_lib_obj_paths(Nob_File_Paths* obj_files)
{
    for (size_t i = 0; i < NOB_ARRAY_LEN(CIMGUI_SOURCES); i++)
    {
        const char* base = strrchr(CIMGUI_SOURCES[i], '/');
        base = base ? base + 1 : CIMGUI_SOURCES[i];

        char obj_name[256];
        snprintf(obj_name, sizeof(obj_name), "%s", base);
        size_t len = strlen(obj_name);
        obj_name[len - 3] = 'o';
        obj_name[len - 2] = '\0';

        nob_da_append(obj_files, nob_temp_strdup(nob_temp_sprintf(BUILD_DIR "/%s", obj_name)));
    }

    nob_da_append(obj_files, nob_temp_strdup(BUILD_DIR "/TracyClient.o"));
    nob_da_append(obj_files, nob_temp_strdup(BUILD_DIR "/flecs.o"));
}

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

        nob_log(NOB_WARNING, "Compiling cimgui: %s", base);
        if (!compile_cpp_to_obj(src, obj, CPP_MODE_CIMGUI)) return false;
    }

    uint64_t t1 = nob_nanos_since_unspecified_epoch();

    {
        const char* tracy_src = SUCK_DIR "/third-party/tracy/public/TracyClient.cpp";
        const char* tracy_obj = BUILD_DIR "/TracyClient.o";
        nob_log(NOB_WARNING, "Compiling Tracy client");
        if (!compile_cpp_to_obj(tracy_src, tracy_obj, CPP_MODE_TRACY)) return false;
    }

    uint64_t t2 = nob_nanos_since_unspecified_epoch();

    {
        const char* flecs_src = SUCK_DIR "/flecs/distr/flecs.c";
        const char* flecs_obj = BUILD_DIR "/flecs.o";
        nob_log(NOB_WARNING, "Compiling flecs");
        if (!compile_c_to_obj(flecs_src, flecs_obj)) return false;
    }

    uint64_t t3 = nob_nanos_since_unspecified_epoch();

    double cimgui_ms = (t1 - t0) / 1e6;
    double tracy_ms  = (t2 - t1) / 1e6;
    double flecs_ms  = (t3 - t2) / 1e6;
    double total_ms  = (t3 - t_total_start) / 1e6;

    nob_log(NOB_WARNING, "──── Libs Timings ─────");
    nob_log(NOB_WARNING, "  cimgui (C++)    : %8.1f ms", cimgui_ms);
    nob_log(NOB_WARNING, "  tracy  (C++)    : %8.1f ms", tracy_ms);
    nob_log(NOB_WARNING, "  flecs  (C)      : %8.1f ms", flecs_ms);
    nob_log(NOB_WARNING, "  ─────────────────────────");
    nob_log(NOB_WARNING, "  TOTAL           : %8.1f ms", total_ms);

    return true;
}

bool build_main(void)
{
    if (!copy_assets()) return false;

    uint64_t t_total_start = nob_nanos_since_unspecified_epoch();

    Nob_File_Paths c_files = {0};
    Nob_File_Paths cpp_files = {0};
    Nob_File_Paths m_files = {0};
    Nob_File_Paths obj_files = {0};

    if (!collect_c_files("src", &c_files, &cpp_files, &m_files)) return false;

    collect_lib_obj_paths(&obj_files);

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

        nob_log(NOB_INFO, "Compiling C++: %s", src);
        if (!compile_cpp_to_obj(src, obj, CPP_MODE_DEFAULT)) return false;
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
        {
            obj_name[len - 1] = 'o';
        }

        const char* obj = nob_temp_sprintf(BUILD_DIR "/%s", obj_name);
        nob_da_append(&obj_files, nob_temp_strdup(obj));

        nob_log(NOB_INFO, "Compiling C: %s", src);
        if (!compile_c_to_obj(src, obj)) return false;
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
        {
            obj_name[len - 1] = 'o';
        }

        const char* obj = nob_temp_sprintf(BUILD_DIR "/%s", obj_name);
        nob_da_append(&obj_files, nob_temp_strdup(obj));

        nob_log(NOB_INFO, "Compiling ObjC: %s", src);
        if (!compile_m_to_obj(src, obj)) return false;
    }

    uint64_t t2b = nob_nanos_since_unspecified_epoch();

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang");
    nob_cmd_append(&cmd, "-g");

    for (size_t i = 0; i < obj_files.count; i++)
        nob_cmd_append(&cmd, obj_files.items[i]);

    nob_cmd_append(&cmd, "-o", BUILD_DIR "/melody");

    for (size_t i = 0; i < NOB_ARRAY_LEN(LIB_PATHS); i++)
        nob_cmd_append(&cmd, LIB_PATHS[i]);

    for (size_t i = 0; i < NOB_ARRAY_LEN(LIBS); i++)
        nob_cmd_append(&cmd, LIBS[i]);

    for (size_t i = 0; i < NOB_ARRAY_LEN(FRAMEWORKS); i++)
        nob_cmd_append(&cmd, FRAMEWORKS[i]);

    nob_cmd_append(&cmd, "-lc++");

    nob_cmd_append(&cmd, "-Wl,-rpath,/opt/homebrew/lib");
    nob_cmd_append(&cmd, "-Wl,-rpath," SUCK_DIR "/third-party/slang/lib");

    if (!nob_cmd_run_sync(cmd)) return false;

    uint64_t t3 = nob_nanos_since_unspecified_epoch();

    double your_cpp_ms  = (t1 - t0) / 1e6;
    double your_c_ms    = (t2 - t1) / 1e6;
    double your_objc_ms = (t2b - t2) / 1e6;
    double link_ms      = (t3 - t2b) / 1e6;
    double total_ms     = (t3 - t_total_start) / 1e6;

    nob_log(NOB_WARNING, "──── Build Timings ────");
    nob_log(NOB_WARNING, "  project C++     : %8.1f ms", your_cpp_ms);
    nob_log(NOB_WARNING, "  project C       : %8.1f ms", your_c_ms);
    nob_log(NOB_WARNING, "  project ObjC    : %8.1f ms", your_objc_ms);
    nob_log(NOB_WARNING, "  link            : %8.1f ms", link_ms);
    nob_log(NOB_WARNING, "  ─────────────────────────");
    nob_log(NOB_WARNING, "  TOTAL           : %8.1f ms", total_ms);

    return true;
}

bool build_test(const char* test_name)
{
    const char* test_src = nob_temp_sprintf("tests/test_%s.c", test_name);
    const char* test_out = nob_temp_sprintf(BUILD_DIR "/test_%s", test_name);

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang");

    for (size_t i = 0; i < NOB_ARRAY_LEN(CFLAGS); i++)
        nob_cmd_append(&cmd, CFLAGS[i]);

    for (size_t i = 0; i < NOB_ARRAY_LEN(INCLUDE_PATHS); i++)
        nob_cmd_append(&cmd, INCLUDE_PATHS[i]);

    nob_cmd_append(&cmd, test_src);
    nob_cmd_append(&cmd, "src/allocator.c");
    nob_cmd_append(&cmd, "src/allocator.heap.c");
    nob_cmd_append(&cmd, "src/allocator.leak.c");
    nob_cmd_append(&cmd, "src/allocator.tracking.c");
    nob_cmd_append(&cmd, "src/allocator.arena.c");
    nob_cmd_append(&cmd, "src/allocator.pool.c");
    nob_cmd_append(&cmd, "src/allocator.stack.c");
    nob_cmd_append(&cmd, "src/allocator.block.c");
    nob_cmd_append(&cmd, "src/allocator.ring.c");
    nob_cmd_append(&cmd, "src/allocator.slab.c");
    nob_cmd_append(&cmd, "src/allocator.buddy.c");
    nob_cmd_append(&cmd, "-o", test_out);
    nob_cmd_append(&cmd, "-lm");

    return nob_cmd_run_sync(cmd);
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

    const char* subcmd = argc > 1 ? argv[1] : "build";

    if (strcmp(subcmd, "libs") == 0)
    {
        if (!build_libs()) return 1;
    }
    else if (strcmp(subcmd, "build") == 0)
    {
        if (!build_main()) return 1;
    }
    else if (strcmp(subcmd, "test") == 0)
    {
        const char* tests[] = { "math", "memory", "heap", "leak", "tracking", "arena", "pool", "stack", "block", "ring", "buddy", "slab" };
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
        nob_log(NOB_INFO, "Usage: ./nob [libs|build|test|run|run-only|debug|clean]");
        return 1;
    }

    return 0;
}
