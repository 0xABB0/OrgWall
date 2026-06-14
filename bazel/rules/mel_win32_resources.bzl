"""mel_win32_resources: compile a win32 .rc to a linkable .res, and pick a subsystem.

Mirrors nob's mel_win32_resource (package.c) + the win32 link branch (emit.c):
the .rc carries the manifest reference, the optional icon, and VERSIONINFO; it
is templated then run through llvm-rc to a .res; lld-link consumes the .res as a
positional link input. A GUI app links `/subsystem:windows`, a console app
`/subsystem:console`.

The .res is surfaced as a CcInfo whose linker input names the .res path as a
user link flag (lld-link takes a .res positionally) and carries the .res as an
additional input, so a plain `deps = [...]` edge from the win32 cc_binary both
links the resource and tracks it as a dependency.
"""

load("@bazel_tools//tools/build_defs/cc:action_names.bzl", "CPP_LINK_EXECUTABLE_ACTION_NAME")

_RC_TEMPLATE = """{manifest}
{icon}
1 VERSIONINFO
FILEVERSION 1,0,0,0
PRODUCTVERSION 1,0,0,0
FILEOS 0x40004
FILETYPE 0x1
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "FileDescription", "{label}"
            VALUE "ProductName", "{label}"
            VALUE "FileVersion", "{version}"
            VALUE "ProductVersion", "{version}"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END
"""

def _impl(ctx):
    rc = ctx.actions.declare_file(ctx.label.name + ".rc")
    icon_line = ""
    extra_inputs = [ctx.file.manifest]
    if ctx.file.icon:
        icon_line = "100 ICON \"%s\"" % ctx.file.icon.basename
        extra_inputs.append(ctx.file.icon)
    ctx.actions.write(
        output = rc,
        content = _RC_TEMPLATE.format(
            manifest = "1 24 \"%s\"" % ctx.file.manifest.basename,
            icon = icon_line,
            label = ctx.attr.app_label or ctx.label.name,
            version = ctx.attr.version,
        ),
    )

    res = ctx.actions.declare_file(ctx.label.name + ".res")
    rc_dir = rc.dirname
    # The .rc names the manifest (and icon) by basename, so llvm-rc must be given each
    # referenced file's staged directory on its /I search path — the manifest lives in
    # its own package, not beside the generated .rc.
    inc_dirs = [rc_dir, ctx.file.manifest.dirname]
    if ctx.file.icon:
        inc_dirs.append(ctx.file.icon.dirname)
    args = ctx.actions.args()
    for d in inc_dirs:
        args.add("/I" + d)
    args.add("/fo", res)
    args.add(rc)

    ctx.actions.run(
        executable = ctx.file._llvm_rc,
        arguments = [args],
        inputs = depset([rc] + extra_inputs),
        tools = ctx.files._llvm_rc_files,
        outputs = [res],
        mnemonic = "MelWin32Rc",
        progress_message = "RC %s" % res.short_path,
    )

    cc_toolchain = ctx.attr._cc_toolchain[cc_common.CcToolchainInfo]
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features,
    )
    linker_input = cc_common.create_linker_input(
        owner = ctx.label,
        user_link_flags = [res.path],
        additional_inputs = depset([res]),
    )
    linking_context = cc_common.create_linking_context(
        linker_inputs = depset([linker_input]),
    )
    return [
        DefaultInfo(files = depset([res])),
        CcInfo(linking_context = linking_context),
    ]

mel_win32_resources = rule(
    implementation = _impl,
    fragments = ["cpp"],
    toolchains = ["@bazel_tools//tools/cpp:toolchain_type"],
    attrs = {
        "app_label": attr.string(),
        "version": attr.string(default = "1.0.0"),
        "icon": attr.label(allow_single_file = [".ico"]),
        "manifest": attr.label(
            allow_single_file = [".manifest"],
            default = Label("//bazel/rules/win32:app.manifest"),
        ),
        "_llvm_rc": attr.label(
            allow_single_file = True,
            cfg = "exec",
            default = Label("@llvm_toolchain_llvm//:bin/llvm-rc"),
        ),
        "_llvm_rc_files": attr.label(
            cfg = "exec",
            default = Label("@llvm_toolchain_llvm//:bin"),
        ),
        "_cc_toolchain": attr.label(default = Label("@bazel_tools//tools/cpp:current_cc_toolchain")),
    },
)
