#include "seam.hh"

#include <utility>

#include <stdexec/execution.hpp>

extern "C" int mel_async_probe(int seed) {
    auto work = stdexec::just(seed) | stdexec::then([](int x) { return x + 2; });
    auto [result] = stdexec::sync_wait(std::move(work)).value();
    return result;
}
