# cppcoro

C++20 coroutine primitives — `generator<T>` and `recursive_generator<T>` — used by modules that
moved off the deprecated codegen-based `coro` module to native C++ coroutines.

The standard `<generator>` (C++23) is unavailable on the toolchains in use (Apple clang 17 ships no
`<generator>`), so the header-only `<coroutine>`-based closure is vendored instead.

Vendored, header-only. Only the `generator`/`recursive_generator` closure is kept:
`config.hpp`, `coroutine.hpp`, `generator.hpp`, `recursive_generator.hpp`.

- Upstream: https://github.com/andreasbuhr/cppcoro (MIT, see `LICENSE.txt`)
- Commit: 8642e98596a92be30a2b061d3ed306d959d3214e

Bazel-only: consumed as `//third-party/cppcoro`. C++ in this repo is built only under Bazel.
