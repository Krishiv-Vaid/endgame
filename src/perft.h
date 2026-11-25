#pragma once

#include <cstdint>
#include <iosfwd>

#include "position.h"

namespace endgame {

// Leaf nodes at `depth` plies, with bulk counting at depth 1.
std::uint64_t perft(Position& pos, int depth);

// Prints per-move counts, the total and the speed (`divide` / `perft` UCI commands).
std::uint64_t divide(Position& pos, int depth, std::ostream& out, bool per_move = true);

}  // namespace endgame
