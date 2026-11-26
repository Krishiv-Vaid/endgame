#pragma once

#include <cstddef>
#include <cstdint>

#include "types.h"

namespace endgame {

constexpr int VALUE_NONE = 32002;
constexpr int VALUE_INF = 32001;
constexpr int VALUE_MATE = 32000;
constexpr int VALUE_MATE_IN_MAX_PLY = VALUE_MATE - MAX_PLY;
constexpr int VALUE_MATED_IN_MAX_PLY = -VALUE_MATE_IN_MAX_PLY;

constexpr int mate_in(int ply) { return VALUE_MATE - ply; }
constexpr int mated_in(int ply) { return -VALUE_MATE + ply; }
constexpr bool is_mate_score(int v) { return v >= VALUE_MATE_IN_MAX_PLY || v <= VALUE_MATED_IN_MAX_PLY; }

enum Bound : std::uint8_t { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };

// Mate scores are stored relative to the node, not the root.
inline int score_to_tt(int v, int ply) {
    if (v == VALUE_NONE) return v;
    return v >= VALUE_MATE_IN_MAX_PLY ? v + ply : v <= VALUE_MATED_IN_MAX_PLY ? v - ply : v;
}
inline int score_from_tt(int v, int ply) {
    if (v == VALUE_NONE) return v;
    return v >= VALUE_MATE_IN_MAX_PLY ? v - ply : v <= VALUE_MATED_IN_MAX_PLY ? v + ply : v;
}

// 10-byte entry; three per 32-byte cluster.
struct TTEntry {
    std::uint16_t key16;
    std::int16_t score;
    std::int16_t eval;
    std::uint16_t move;
    std::uint8_t depth8;     // depth - DEPTH_OFFSET, 0 = empty
    std::uint8_t gen_bound;  // generation (5 bits) | pv (1 bit) | bound (2 bits)

    static constexpr int DEPTH_OFFSET = -2;

    int depth() const { return int(depth8) + DEPTH_OFFSET; }
    Bound bound() const { return Bound(gen_bound & 3); }
    bool is_pv() const { return gen_bound & 4; }
    Move get_move() const { return Move(move); }
    bool empty() const { return depth8 == 0; }
};

struct TTCluster {
    TTEntry entries[3];
    char padding[2];
};
static_assert(sizeof(TTCluster) == 32);

struct TTProbe {
    bool hit;
    int score;
    int eval;
    Move move;
    int depth;
    Bound bound;
    bool pv;
};

class TranspositionTable {
public:
    TranspositionTable() = default;
    ~TranspositionTable();
    TranspositionTable(const TranspositionTable&) = delete;
    TranspositionTable& operator=(const TranspositionTable&) = delete;

    void resize(std::size_t mb);
    void clear();
    void new_search() { generation_ = std::uint8_t((generation_ + 8) & 0xF8); }

    TTProbe probe(Key key, int ply) const;
    void store(Key key, int ply, int score, int eval, Move move, int depth, Bound bound, bool pv);

    void prefetch(Key key) const { __builtin_prefetch(&clusters_[index(key)]); }
    int hashfull() const;  // permille of entries from the current search

private:
    std::size_t index(Key key) const {
        return std::size_t((static_cast<unsigned __int128>(key) * cluster_count_) >> 64);
    }

    TTCluster* clusters_ = nullptr;
    std::size_t cluster_count_ = 0;
    std::uint8_t generation_ = 0;
};

}  // namespace endgame
