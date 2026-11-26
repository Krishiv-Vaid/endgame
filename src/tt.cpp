#include "tt.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>

namespace endgame {

TranspositionTable::~TranspositionTable() { std::free(clusters_); }

void TranspositionTable::resize(std::size_t mb) {
    std::free(clusters_);
    cluster_count_ = std::max<std::size_t>(1, mb * 1024 * 1024 / sizeof(TTCluster));
    clusters_ = static_cast<TTCluster*>(std::malloc(cluster_count_ * sizeof(TTCluster)));
    if (!clusters_) throw std::bad_alloc();
    clear();
}

void TranspositionTable::clear() {
    std::memset(static_cast<void*>(clusters_), 0, cluster_count_ * sizeof(TTCluster));
    generation_ = 0;
}

TTProbe TranspositionTable::probe(Key key, int ply) const {
    const TTCluster& c = clusters_[index(key)];
    const std::uint16_t k16 = std::uint16_t(key);
    for (const TTEntry& e : c.entries) {
        if (e.key16 == k16 && !e.empty()) {
            return {true, score_from_tt(e.score, ply), e.eval, e.get_move(), e.depth(), e.bound(), e.is_pv()};
        }
    }
    return {false, VALUE_NONE, VALUE_NONE, Move::none(), 0, BOUND_NONE, false};
}

void TranspositionTable::store(Key key, int ply, int score, int eval, Move move, int depth,
                               Bound bound, bool pv) {
    TTCluster& c = clusters_[index(key)];
    const std::uint16_t k16 = std::uint16_t(key);

    // Prefer the slot holding this key; otherwise replace the entry that is
    // shallowest after penalising old generations.
    TTEntry* replace = &c.entries[0];
    int worst = 1 << 30;
    for (TTEntry& e : c.entries) {
        if (e.key16 == k16 || e.empty()) {
            replace = &e;
            break;
        }
        int age = ((generation_ - (e.gen_bound & 0xF8)) & 0xF8) >> 3;
        int value = e.depth() - 4 * age;
        if (value < worst) {
            worst = value;
            replace = &e;
        }
    }
    TTEntry& e = *replace;

    // Keep the old move if we have none for the same position.
    if (move || e.key16 != k16) e.move = move.raw();

    // Don't overwrite a deeper entry for the same key unless this one is exact
    // or the old one is from a previous search.
    if (bound != BOUND_EXACT && e.key16 == k16 && !e.empty() &&
        depth + 3 < e.depth() && (e.gen_bound & 0xF8) == generation_)
        return;

    e.key16 = k16;
    e.score = std::int16_t(score_to_tt(score, ply));
    e.eval = std::int16_t(eval);
    e.depth8 = std::uint8_t(depth - TTEntry::DEPTH_OFFSET);
    e.gen_bound = std::uint8_t(generation_ | (pv ? 4 : 0) | bound);
}

int TranspositionTable::hashfull() const {
    int used = 0;
    const std::size_t n = std::min<std::size_t>(1000, cluster_count_);
    for (std::size_t i = 0; i < n; ++i)
        for (const TTEntry& e : clusters_[i].entries)
            used += !e.empty() && (e.gen_bound & 0xF8) == generation_;
    return int(used * 1000 / (n * 3));
}

}  // namespace endgame
