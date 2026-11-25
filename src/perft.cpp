#include "perft.h"

#include <chrono>
#include <ostream>

#include "movegen.h"

namespace endgame {

std::uint64_t perft(Position& pos, int depth) {
    MoveList list;
    generate<GEN_ALL>(pos, list);
    if (depth <= 1) return depth == 1 ? std::uint64_t(list.size()) : 1;
    std::uint64_t nodes = 0;
    for (Move m : list) {
        pos.make_move(m);
        nodes += perft(pos, depth - 1);
        pos.unmake_move();
    }
    return nodes;
}

std::uint64_t divide(Position& pos, int depth, std::ostream& out, bool per_move) {
    auto start = std::chrono::steady_clock::now();
    std::uint64_t total = 0;
    if (depth <= 0) {
        total = 1;
    } else {
        MoveList list;
        generate<GEN_ALL>(pos, list);
        for (Move m : list) {
            pos.make_move(m);
            std::uint64_t n = perft(pos, depth - 1);
            pos.unmake_move();
            total += n;
            if (per_move) out << pos.move_to_uci(m) << ": " << n << '\n';
        }
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    out << "\nNodes searched: " << total << "\nTime: " << int(secs * 1000) << " ms\nNPS: "
        << std::uint64_t(secs > 0 ? double(total) / secs : 0) << std::endl;
    return total;
}

}  // namespace endgame
