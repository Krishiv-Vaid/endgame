#pragma once

namespace endgame {

// Builds attack tables and Zobrist keys. Idempotent; call before creating a Position.
void init_all();

}  // namespace endgame
