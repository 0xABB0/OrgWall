"""clang-cl + lld-link cc_toolchain_config targeting x86_64-pc-windows-msvc, using
an xwin sysroot. Runs on a unix exec host (mac/linux) -> identical MSVC-ABI
output. (The xwin splat shells out to `mv` and the dead tool_paths point at
/usr/bin/false, so a Windows exec host is not supported.)

The MSVC/SDK version dirs are resolved by scanning the staged sysroot files (no
version hardcoding, no load() of the lazily-fetched xwin repo). Headers/libs are
passed explicitly via /imsvc and /LIBPATH — clang-cl's /winsysroot auto-detection
misbehaves when cross-compiling from a non-Windows host. Tools live in
@llvm_toolchain_llvm, so they are bound via action_config + tool(tool=<File>).

SELF-CONTAINED (no_legacy_features): Bazel derives its implicit legacy feature set
from the EXEC host, so on a unix host it would emit gcc-style link/archive flags
(-o, -Wl,-S, `ar rcsD`) that lld-link/llvm-lib reject. We therefore disable the
legacy set and define every needed feature here in MSVC spelling — compile
(/I,/D,/Fo,/c,user flags), link (/OUT:, libraries_to_link, /machine, param file)
and archive (/OUT: via llvm-lib). DLL .def exports and linkstamps are not wired up.

CASE-SENSITIVITY REQUIREMENT: build win32 from a case-sensitive filesystem (Linux,
CI, or a case-sensitive macOS volume). The Windows SDK ships CamelCase headers
(Windows.h) that code includes lowercase (<windows.h>); xwin disambiguates with
symlinks on case-sensitive hosts, but on case-insensitive APFS those symlinks
can't exist, so Bazel's case-sensitive /showIncludes validation rejects the
headers. The compile itself is correct on every host — only the validation differs."""

load("@bazel_tools//tools/build_defs/cc:action_names.bzl", "ACTION_NAMES")
load(
    "@bazel_tools//tools/cpp:cc_toolchain_config_lib.bzl",
    "action_config",
    "feature",
    "flag_group",
    "flag_set",
    "tool",
    "tool_path",
    "variable_with_value",
)

_COMPILE = [
    ACTION_NAMES.c_compile,
    ACTION_NAMES.cpp_compile,
    ACTION_NAMES.preprocess_assemble,
    ACTION_NAMES.assemble,
]
_LINK = [
    ACTION_NAMES.cpp_link_executable,
    ACTION_NAMES.cpp_link_dynamic_library,
    ACTION_NAMES.cpp_link_nodeps_dynamic_library,
]

def _versioned_base(files, anchor):
    for f in files:
        p = f.path
        i = p.find(anchor)
        if i >= 0:
            seg = p[i + len(anchor):].split("/")[0]
            if seg:
                return p[:i] + anchor + seg
    fail("could not locate '%s<version>' in the xwin sysroot" % anchor)

def _resource_include(files):
    for f in files:
        p = f.path
        i = p.find("/lib/clang/")
        if i >= 0:
            j = p.find("/include/", i)
            if j >= 0:
                return p[:j + len("/include")]
    fail("could not locate clang's resource-dir include in the llvm distribution")

def _impl(ctx):
    files = ctx.files.sysroot
    vc = _versioned_base(files, "/VC/Tools/MSVC/")
    sdk_inc = _versioned_base(files, "/WindowsKits/10/Include/")
    sdk_lib = sdk_inc.replace("/Include/", "/Lib/")

    inc = [
        vc + "/include",
        sdk_inc + "/ucrt",
        sdk_inc + "/shared",
        sdk_inc + "/um",
        sdk_inc + "/winrt",
    ]
    lib = [
        vc + "/lib/x64",
        sdk_lib + "/ucrt/x64",
        sdk_lib + "/um/x64",
    ]

    resource_inc = _resource_include(ctx.files.clang_files)
    sr = sdk_inc[:sdk_inc.find("/WindowsKits")]
    builtin_dirs = inc + [resource_inc, sr + "/VC", sr + "/WindowsKits"]

    imsvc = ["/imsvc" + d for d in inc]
    libpaths = ["/LIBPATH:" + d for d in lib]

    clang_cl = ctx.file.clang_cl
    lld_link = ctx.file.lld_link
    llvm_lib = ctx.file.llvm_lib

    action_configs = (
        [action_config(action_name = a, enabled = True, tools = [tool(tool = clang_cl)]) for a in _COMPILE] +
        [action_config(action_name = a, enabled = True, tools = [tool(tool = lld_link)]) for a in _LINK] +
        [action_config(action_name = ACTION_NAMES.cpp_link_static_library, enabled = True, tools = [tool(tool = llvm_lib)])]
    )

    # /I and /D so Bazel emits MSVC-style, not the legacy -iquote/-isystem/-D that
    # clang-cl ignores.
    include_paths = feature(
        name = "include_paths",
        enabled = True,
        flag_sets = [flag_set(actions = _COMPILE, flag_groups = [
            flag_group(flags = ["/I%{quote_include_paths}"], iterate_over = "quote_include_paths"),
            flag_group(flags = ["/I%{include_paths}"], iterate_over = "include_paths"),
            flag_group(flags = ["/I%{system_include_paths}"], iterate_over = "system_include_paths"),
        ])],
    )
    defines = feature(
        name = "preprocessor_defines",
        enabled = True,
        flag_sets = [flag_set(actions = _COMPILE, flag_groups = [
            flag_group(flags = ["/D%{preprocessor_defines}"], iterate_over = "preprocessor_defines"),
        ])],
    )
    compiler_output = feature(
        name = "compiler_output_flags",
        enabled = True,
        flag_sets = [flag_set(actions = _COMPILE, flag_groups = [
            flag_group(flags = ["/Fo%{output_file}"], expand_if_available = "output_file"),
        ])],
    )
    compiler_input = feature(
        name = "compiler_input_flags",
        enabled = True,
        flag_sets = [flag_set(actions = _COMPILE, flag_groups = [
            flag_group(flags = ["/c", "%{source_file}"], expand_if_available = "source_file"),
        ])],
    )
    sysroot_compile = feature(
        name = "msvc_clang_cl_compile",
        enabled = True,
        flag_sets = [flag_set(actions = _COMPILE, flag_groups = [flag_group(flags = [
            "--target=x86_64-pc-windows-msvc",
            "-no-canonical-prefixes",
            "-fms-compatibility",
            # Static release CRT, explicit (clang-cl would otherwise pick it by
            # silent default). xwin splats only the redistributable release CRT;
            # the debug CRT (libcmtd) is not redistributable, so /MTd is
            # unavailable — debug builds link this same CRT.
            "/MT",
            "/D_CRT_SECURE_NO_WARNINGS",
            "/D_CRT_NONSTDC_NO_WARNINGS",
            "/D_WINSOCK_DEPRECATED_NO_WARNINGS",
            "-Wno-unknown-argument",
        ] + imsvc)])],
    )

    # The language standard is project policy, set per-platform in .bazelrc — win32
    # gets the MSVC /std: spelling clang-cl needs (it silently drops GNU -std=). The
    # toolchain owns only the C++ exception model.
    exceptions = feature(
        name = "msvc_clang_cl_exceptions",
        enabled = True,
        flag_sets = [
            flag_set(actions = [ACTION_NAMES.cpp_compile], flag_groups = [flag_group(flags = ["/EHsc"])]),
        ],
    )
    showincludes = feature(
        name = "parse_showincludes",
        enabled = True,
        flag_sets = [flag_set(actions = [ACTION_NAMES.c_compile, ACTION_NAMES.cpp_compile], flag_groups = [flag_group(flags = [
            "/showIncludes",
        ])])],
    )
    link_feature = feature(
        name = "msvc_clang_cl_link",
        enabled = True,
        flag_sets = [flag_set(actions = _LINK, flag_groups = [flag_group(flags = [
            "/machine:x64",
        ] + libpaths)])],
    )

    # Bazel auto-enables the feature whose name matches -c <mode>. opt optimizes and
    # strips asserts (NDEBUG); dbg adds debug info (below); fastbuild (the bazel
    # default) falls through to clang-cl's own defaults — no optimization, no NDEBUG
    # (asserts stay live per MEL-ENGINE-VIII), no debug info.
    opt = feature(
        name = "opt",
        flag_sets = [flag_set(actions = _COMPILE, flag_groups = [flag_group(flags = [
            "/O2",
            "/DNDEBUG",
        ])])],
    )

    # Empty marker: cc_binary/cc_shared_library declare a tracked <name>.pdb sibling
    # output when this is enabled (rule-side cc_common.is_enabled check). dbg implies
    # it; lld-link names the PDB by stripping the binary's extension, which is exactly
    # the path the rule declares, so no /PDB: flag is needed.
    generate_pdb_file = feature(name = "generate_pdb_file")

    # -c dbg: CodeView embedded in the objects (/Z7 — sandbox-safe, no shared
    # compiler-side PDB), merged by lld-link into <name>.pdb under /DEBUG. /DEBUG, not
    # :FASTLINK, because lld-link does not support FASTLINK.
    dbg = feature(
        name = "dbg",
        flag_sets = [
            flag_set(
                actions = [ACTION_NAMES.c_compile, ACTION_NAMES.cpp_compile],
                flag_groups = [flag_group(flags = ["/Od", "/Z7"])],
            ),
            flag_set(
                actions = _LINK,
                flag_groups = [flag_group(flags = ["/DEBUG", "/INCREMENTAL:NO"])],
            ),
        ],
        implies = ["generate_pdb_file"],
    )

    # ---- self-contained MSVC features (legacy injection is disabled below) ----
    # --copt/--conlyopt/--cxxopt and target linkopts. Bazel routes the right subset
    # of user flags into each per-action variable, so one flag_group per axis suffices.
    user_compile_flags = feature(
        name = "user_compile_flags",
        enabled = True,
        flag_sets = [flag_set(actions = _COMPILE, flag_groups = [
            flag_group(flags = ["%{user_compile_flags}"], iterate_over = "user_compile_flags", expand_if_available = "user_compile_flags"),
        ])],
    )
    user_link_flags = feature(
        name = "user_link_flags",
        enabled = True,
        flag_sets = [flag_set(actions = _LINK, flag_groups = [
            flag_group(flags = ["%{user_link_flags}"], iterate_over = "user_link_flags", expand_if_available = "user_link_flags"),
        ])],
    )

    # /OUT: for both the linker and the archiver (llvm-lib speaks lib.exe flags).
    output_execpath = feature(
        name = "output_execpath_flags",
        enabled = True,
        flag_sets = [flag_set(actions = _LINK, flag_groups = [
            flag_group(flags = ["/OUT:%{output_execpath}"], expand_if_available = "output_execpath"),
        ])],
    )
    archiver_flags = feature(
        name = "archiver_flags",
        enabled = True,
        flag_sets = [flag_set(actions = [ACTION_NAMES.cpp_link_static_library], flag_groups = [
            flag_group(flags = ["/OUT:%{output_execpath}"], expand_if_available = "output_execpath"),
            flag_group(flags = ["%{user_archiver_flags}"], iterate_over = "user_archiver_flags", expand_if_available = "user_archiver_flags"),
        ])],
    )

    # The object/library list, per library type. /IMPLIB for dynamic libs; whole-archive
    # via /WHOLEARCHIVE:. Static archiving reuses the libraries_to_link expansion.
    input_param_flags = feature(
        name = "input_param_flags",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = [ACTION_NAMES.cpp_link_dynamic_library, ACTION_NAMES.cpp_link_nodeps_dynamic_library],
                flag_groups = [flag_group(
                    flags = ["/IMPLIB:%{interface_library_output_path}"],
                    expand_if_available = "interface_library_output_path",
                )],
            ),
            flag_set(
                actions = _LINK + [ACTION_NAMES.cpp_link_static_library],
                flag_groups = [flag_group(
                    iterate_over = "libraries_to_link",
                    flag_groups = [
                        flag_group(
                            iterate_over = "libraries_to_link.object_files",
                            flag_groups = [flag_group(flags = ["%{libraries_to_link.object_files}"])],
                            expand_if_equal = variable_with_value(name = "libraries_to_link.type", value = "object_file_group"),
                        ),
                        flag_group(
                            flag_groups = [flag_group(flags = ["%{libraries_to_link.name}"])],
                            expand_if_equal = variable_with_value(name = "libraries_to_link.type", value = "object_file"),
                        ),
                        flag_group(
                            flag_groups = [flag_group(flags = ["%{libraries_to_link.name}"])],
                            expand_if_equal = variable_with_value(name = "libraries_to_link.type", value = "interface_library"),
                        ),
                        flag_group(
                            flag_groups = [
                                flag_group(flags = ["%{libraries_to_link.name}"], expand_if_false = "libraries_to_link.is_whole_archive"),
                                flag_group(flags = ["/WHOLEARCHIVE:%{libraries_to_link.name}"], expand_if_true = "libraries_to_link.is_whole_archive"),
                            ],
                            expand_if_equal = variable_with_value(name = "libraries_to_link.type", value = "static_library"),
                        ),
                    ],
                    expand_if_available = "libraries_to_link",
                )],
            ),
        ],
    )

    # Response file for the link/archive command (kept short on a unix host anyway).
    linker_param_file = feature(
        name = "linker_param_file",
        enabled = True,
        flag_sets = [flag_set(
            actions = _LINK + [ACTION_NAMES.cpp_link_static_library],
            flag_groups = [flag_group(flags = ["@%{linker_param_file}"], expand_if_available = "linker_param_file")],
        )],
    )
    nologo = feature(
        name = "nologo",
        enabled = True,
        flag_sets = [flag_set(
            actions = _COMPILE + _LINK + [ACTION_NAMES.cpp_link_static_library],
            flag_groups = [flag_group(flags = ["/nologo"])],
        )],
    )

    # Presence disables Bazel's exec-host-derived legacy features (which would emit
    # gcc-style -o/-Wl/ar flags to the MSVC tools). Everything above is the replacement.
    no_legacy_features = feature(name = "no_legacy_features")

    # Required once legacy is off. no_stripping: cc_binary always declares a
    # <name>.stripped output and demands a strip action_config unless told not to
    # strip — MSVC has no strip; debug info lives in the PDB. has_configured_linker_path:
    # we invoke lld-link directly via action_config, so the rules must not prepend a
    # compiler-driver linker path.
    no_stripping = feature(name = "no_stripping", enabled = True)
    has_configured_linker_path = feature(name = "has_configured_linker_path", enabled = True)

    param_file = feature(name = "compiler_param_file", enabled = True)
    archive_param_file = feature(name = "archive_param_file", enabled = True)
    supports_pic = feature(name = "supports_pic", enabled = False)

    return cc_common.create_cc_toolchain_config_info(
        ctx = ctx,
        toolchain_identifier = "clang-cl-x64-windows",
        host_system_name = "local",
        target_system_name = "x86_64-pc-windows-msvc",
        target_cpu = "x64_windows",
        target_libc = "msvcrt",
        compiler = "clang-cl",
        abi_version = "local",
        abi_libc_version = "local",
        action_configs = action_configs,
        # The xwin sysroot root: headers under it are builtin/system (covers VC +
        # WindowsKits without per-dir declaration). resource_inc lives in the llvm
        # repo (outside the sysroot), so it stays in the explicit list.
        builtin_sysroot = sr,
        cxx_builtin_include_directories = builtin_dirs,
        tool_paths = [
            tool_path(name = "gcc", path = "/usr/bin/false"),
            tool_path(name = "cpp", path = "/usr/bin/false"),
            tool_path(name = "ld", path = "/usr/bin/false"),
            tool_path(name = "ar", path = "/usr/bin/false"),
            tool_path(name = "nm", path = "/usr/bin/false"),
            tool_path(name = "objdump", path = "/usr/bin/false"),
            tool_path(name = "strip", path = "/usr/bin/false"),
            tool_path(name = "gcov", path = "/usr/bin/false"),
        ],
        features = [
            no_legacy_features,
            no_stripping,
            has_configured_linker_path,
            nologo,
            param_file,
            archive_param_file,
            supports_pic,
            include_paths,
            defines,
            compiler_output,
            compiler_input,
            sysroot_compile,
            exceptions,
            user_compile_flags,
            showincludes,
            opt,
            dbg,
            generate_pdb_file,
            output_execpath,
            input_param_flags,
            link_feature,
            user_link_flags,
            archiver_flags,
            linker_param_file,
        ],
    )

cc_toolchain_config = rule(
    implementation = _impl,
    attrs = {
        "sysroot": attr.label(mandatory = True, allow_files = True),
        "clang_files": attr.label(mandatory = True, allow_files = True),
        "clang_cl": attr.label(mandatory = True, allow_single_file = True, cfg = "exec"),
        "lld_link": attr.label(mandatory = True, allow_single_file = True, cfg = "exec"),
        "llvm_lib": attr.label(mandatory = True, allow_single_file = True, cfg = "exec"),
    },
    provides = [CcToolchainConfigInfo],
)
