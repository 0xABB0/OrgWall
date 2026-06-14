"""mel_wasm_app: package a GUI wasm app as an emscripten .html, and serve it.

Mirrors nob's web emit/serve (emit.c web_gui branch, driver.c wasm serve):
the GUI wasm link emits `.html` from an emcc `--shell-file`, and the dev
server sets the cross-origin-isolation headers SharedArrayBuffer/threads
demand (`Cross-Origin-Opener-Policy: same-origin`,
`Cross-Origin-Embedder-Policy: require-corp`) plus `Cache-Control: no-store`.

emcc decides output format from `--oformat`; with `--oformat=html` plus a
`--shell-file` linkopt the emscripten toolchain tars an `.html` alongside the
`.js`/`.wasm`, which wasm_cc_binary then extracts.
"""

load("@emsdk//emscripten_toolchain:wasm_cc_binary.bzl", "wasm_cc_binary")

def _serve_impl(ctx):
    serve = ctx.file._serve
    site = ctx.files.site
    launcher = ctx.actions.declare_file(ctx.label.name + ".sh")
    ctx.actions.write(
        output = launcher,
        is_executable = True,
        content = "\n".join([
            "#!/bin/sh",
            "set -e",
            "dir=$(dirname \"{html}\")".format(html = ctx.file.html.short_path),
            "exec python3 \"{serve}\" \"$dir\" \"{port}\" 1".format(
                serve = serve.short_path,
                port = ctx.attr.port,
            ),
        ]),
    )
    runfiles = ctx.runfiles(files = site + [serve])
    return [DefaultInfo(executable = launcher, runfiles = runfiles)]

_mel_wasm_serve = rule(
    implementation = _serve_impl,
    executable = True,
    attrs = {
        "html": attr.label(allow_single_file = [".html"], mandatory = True),
        "site": attr.label_list(allow_files = True, mandatory = True),
        "port": attr.int(default = 8000),
        "_serve": attr.label(
            allow_single_file = True,
            default = Label("//bazel/rules/web:serve.py"),
        ),
    },
)

def mel_wasm_app(name, deps, shell = "//bazel/rules/web:shell.html", port = 8000, **kwargs):
    binary = name + "_wasm"
    native.cc_binary(
        name = binary,
        deps = deps,
        additional_linker_inputs = [shell],
        linkopts = [
            "--oformat=html",
            "--shell-file",
            "$(location %s)" % shell,
        ],
        target_compatible_with = ["@platforms//cpu:wasm32"],
        **kwargs
    )

    html = name + ".html"
    js = name + ".js"
    wasm = name + ".wasm"
    wasm_cc_binary(
        name = name + "_html",
        cc_target = ":" + binary,
        threads = "emscripten",
        outputs = [html, js, wasm],
        target_compatible_with = ["@platforms//cpu:wasm32"],
    )

    _mel_wasm_serve(
        name = name + "_serve",
        html = ":" + html,
        site = [":" + html, ":" + js, ":" + wasm],
        port = port,
        target_compatible_with = ["@platforms//cpu:wasm32"],
    )
