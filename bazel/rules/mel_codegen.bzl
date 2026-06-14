"""mel_codegen: run a host codegen tool (exec config) to emit a C source.

Mirrors nob's mel_codegen, including the $out/$dir/$cflags/$hostclang token
expansion done by emit.c: $cflags becomes the dependency include-closure (from
CcInfo) plus the target's own include dirs and defines; $hostclang becomes the
macOS SDK isysroot so libclang-based generators can parse system headers.

The action runs `local`: no sandbox (libclang reads the host macOS SDK and its
own resource dir, neither declarable as inputs) and no remote cache (the output
is host-SDK-sensitive, so a shared cache must not be poisoned). Local action
caching still applies, so the declared header closure drives incrementality. The
.d depfile nob emitted is not consumed — and could not be, since Starlark rules
cannot feed depfiles to Bazel's input discovery; declared inputs cover us.
"""

load("@llvm//:sdk.bzl", "LLVM_RESOURCE_INCLUDE", "MACOS_SDK")

def _impl(ctx):
    out = ctx.actions.declare_file(ctx.attr.out)

    ccs = [d[CcInfo].compilation_context for d in ctx.attr.deps if CcInfo in d]
    inc = depset(transitive = [c.includes for c in ccs]).to_list()
    quote = depset(transitive = [c.quote_includes for c in ccs]).to_list()
    syst = depset(transitive = [c.system_includes for c in ccs]).to_list()
    defs = depset(transitive = [c.defines for c in ccs]).to_list()
    hdrs = depset(transitive = [c.headers for c in ccs])

    cflags = ["-std=c23"]  # every Melody TU is C23 (headers use nullptr, etc.)
    for i in inc + quote + syst + ctx.attr.local_includes:
        cflags += ["-I" + i]
    for d in defs:
        cflags += ["-D" + d]
    hostclang = ["-isysroot", MACOS_SDK]
    if LLVM_RESOURCE_INCLUDE:
        hostclang += ["-isystem", LLVM_RESOURCE_INCLUDE]

    args = ctx.actions.args()
    for a in ctx.attr.args:
        # $cflags/$hostclang expand to multiple args; $dir/$out are substring tokens
        # (mirrors emit.c, where e.g. "$dir/test/x.coro.h" is one path arg).
        if a == "$cflags":
            args.add_all(cflags)
        elif a == "$hostclang":
            args.add_all(hostclang)
        else:
            args.add(a.replace("$dir", ctx.label.package).replace("$out", out.path))

    ctx.actions.run(
        executable = ctx.executable.tool,
        arguments = [args],
        inputs = depset(ctx.files.srcs, transitive = [hdrs]),
        outputs = [out],
        mnemonic = "MelCodegen",
        progress_message = "Generating %s" % out.short_path,
        execution_requirements = {"local": "1"},
    )
    return [DefaultInfo(files = depset([out]))]

mel_codegen = rule(
    implementation = _impl,
    attrs = {
        "out": attr.string(mandatory = True),
        "tool": attr.label(executable = True, cfg = "exec", mandatory = True),
        "args": attr.string_list(doc = "Raw arg template with $out/$dir/$cflags/$hostclang tokens."),
        "srcs": attr.label_list(allow_files = True, doc = "Declared input header(s)."),
        "deps": attr.label_list(providers = [CcInfo], doc = "Include-closure for libclang parsing."),
        "local_includes": attr.string_list(doc = "Target's own include dirs (execroot-relative)."),
    },
)
