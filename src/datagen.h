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
    std::string output = "data/selfplay";  // writes <output>_t<i>_<run>.bin (or _t<i>.txt)
    bool binary = true;                    // PackedPosition shards; false = Texel text
};

// Self-play data generation: the engine plays itself at fixed nodes and
// records every quiet position (not in check, best move not a capture or
// promotion) with its white-view search score and the game result.
// Binary mode writes one PackedPosition shard per thread per run (a new run
// never overwrites old shards, so an interrupted run resumes by running
// again); text mode appends "<fen> | <score> | <result>" lines for Texel.
void run_datagen(const DatagenOptions& opts);

}  // namespace endgame
