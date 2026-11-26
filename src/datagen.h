#pragma once

#include <cstdint>
#include <string>

namespace endgame {

struct DatagenOptions {
    int threads = 8;
    std::uint64_t positions = 1'000'000;  // stop after this many recorded positions (all threads)
    std::uint64_t soft_nodes = 5000;
    std::uint64_t hard_nodes = 20000;
    int random_plies = 8;                  // 8 or 9 (chosen per game)
    std::uint64_t seed = 1;
    std::string output = "data/selfplay";  // writes <output>_t<i>.txt
};

// Self-play data generation: the engine plays itself at fixed nodes and
// writes "<fen> | <white-view score> | <white result 1.0/0.5/0.0>" for every
// quiet position (not in check, best move not a capture or promotion).
// Appends, so an interrupted run resumes by simply running again.
void run_datagen(const DatagenOptions& opts);

}  // namespace endgame
