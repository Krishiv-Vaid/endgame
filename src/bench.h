#pragma once

#include <cstdint>

namespace endgame {

class Engine;

constexpr int BENCH_DEPTH = 6;

// Searches a fixed set of positions to a fixed depth (single thread, 16 MB
// hash cleared per position) and prints "<nodes> nodes <nps> nps".
std::uint64_t run_bench(Engine& engine, int depth = 0);

}  // namespace endgame
