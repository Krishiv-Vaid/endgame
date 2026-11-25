#pragma once

#include "position.h"

namespace endgame {

// GEN_NOISY: captures (including capture-promotions to any piece) and quiet
//            queen promotions.
// GEN_QUIET: everything else (quiet moves, castling, quiet under-promotions).
// GEN_ALL:   both. All generators produce strictly legal moves.
enum GenType { GEN_NOISY, GEN_QUIET, GEN_ALL };

struct MoveList {
    Move moves[MAX_MOVES];
    int count = 0;

    void push(Move m) { moves[count++] = m; }
    int size() const { return count; }
    Move* begin() { return moves; }
    Move* end() { return moves + count; }
    const Move* begin() const { return moves; }
    const Move* end() const { return moves + count; }
    Move operator[](int i) const { return moves[i]; }
    bool contains(Move m) const {
        for (int i = 0; i < count; ++i)
            if (moves[i] == m) return true;
        return false;
    }
};

template <GenType T>
void generate(const Position& pos, MoveList& list);

}  // namespace endgame
