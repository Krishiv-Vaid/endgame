#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "movegen.h"
#include "position.h"

using namespace endgame;

namespace {

struct Rng {
    std::uint64_t s;
    std::uint64_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
};

std::vector<std::string> suite_fens(const char* name) {
    std::ifstream in(std::string(ENDGAME_TEST_DATA_DIR) + "/perft/" + name);
    std::vector<std::string> fens;
    std::string line;
    while (std::getline(in, line))
        if (!line.empty()) fens.push_back(line.substr(0, line.find(';')));
    return fens;
}

std::uint64_t fuzz_moves() {
    if (const char* env = std::getenv("ENDGAME_FUZZ_MOVES")) return std::strtoull(env, nullptr, 10);
    return 300'000;
}

}  // namespace

TEST_CASE("FEN round-trips") {
    for (const auto& fen : suite_fens("standard.epd")) {
        Position pos(fen);
        Position again(pos.fen());
        CHECK(again.fen() == pos.fen());
        CHECK(again.key() == pos.key());
    }
}

TEST_CASE("malformed FEN is rejected and leaves the position unchanged") {
    Position pos;
    const std::string before = pos.fen();
    for (const char* bad : {"", "garbage", "8/8/8/8/8/8/8/8 w - - 0 1",
                            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1",
                            "rnbqkbnr/pppppppp/9/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                            "4k3/8/8/8/8/8/8/r3K3 b - - 0 1" /* side not to move in check */,
                            "P3k3/8/8/8/8/8/8/4K3 w - - 0 1"}) {
        CHECK_FALSE(pos.set(bad));
        CHECK(pos.fen() == before);
    }
}

TEST_CASE("en-passant square is kept only when a capture is possible") {
    Position a("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    CHECK(a.ep_square() == SQ_NONE);
    Position b("rnbqkbnr/ppp1pppp/8/8/3pP3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 3");
    CHECK(b.ep_square() == SQ_E3);
}

TEST_CASE("noisy + quiet generation partitions all legal moves") {
    std::vector<std::string> fens = suite_fens("standard.epd");
    for (const auto& f : suite_fens("chess960.epd")) fens.push_back(f);
    int i = 0;
    for (const auto& fen : fens) {
        Position pos(fen, i++ >= 126);
        MoveList all, noisy, quiet;
        generate<GEN_ALL>(pos, all);
        generate<GEN_NOISY>(pos, noisy);
        generate<GEN_QUIET>(pos, quiet);
        CHECK(noisy.size() + quiet.size() == all.size());
        for (Move m : noisy) {
            CHECK(all.contains(m));
            CHECK_FALSE(quiet.contains(m));
            CHECK((m.is_capture() || m.promotion_type() == QUEEN));
        }
        for (Move m : quiet) CHECK(all.contains(m));
    }
}

TEST_CASE("fuzz: make/unmake round-trips, keys and gives_check") {
    std::vector<std::string> fens = suite_fens("standard.epd");
    const std::size_t n960 = fens.size();
    for (const auto& f : suite_fens("chess960.epd")) fens.push_back(f);

    Rng rng{0xC0FFEE1234567ULL};
    const std::uint64_t target = fuzz_moves();
    std::uint64_t made = 0;
    std::string why;
    std::size_t game = 0;
    while (made < target) {
        const std::string& start = fens[game++ % fens.size()];
        Position pos(start, (game - 1) % fens.size() >= n960);
        std::vector<std::string> fen_stack;
        for (int ply = 0; ply < 200 && made < target; ++ply) {
            MoveList list;
            generate<GEN_ALL>(pos, list);
            if (!list.size()) break;

            // Every legal move: gives_check agrees with the real result and
            // make/unmake restores the exact position.
            const std::string before = pos.fen();
            const Key key = pos.key();
            for (Move m : list) {
                bool predicted = pos.gives_check(m);
                pos.make_move(m);
                ++made;
                if (!pos.is_consistent(&why)) {
                    FAIL_CHECK(why << " after " << m.raw() << " from " << before);
                }
                CHECK_MESSAGE(predicted == pos.in_check(), before, " move ", pos.fen());
                pos.unmake_move();
                if (pos.key() != key || pos.fen() != before) {
                    FAIL_CHECK("unmake did not restore " << before);
                }
            }

            Move m = list[int(rng.next() % std::uint64_t(list.size()))];
            fen_stack.push_back(pos.fen());
            pos.make_move(m);
            if (pos.is_draw(0) && rng.next() % 4 == 0) break;
        }
        // Unwind the whole game and compare against the recorded history.
        while (!fen_stack.empty()) {
            pos.unmake_move();
            CHECK(pos.fen() == fen_stack.back());
            fen_stack.pop_back();
        }
        CHECK(pos.fen() == Position(start, pos.chess960()).fen());
    }
    MESSAGE("fuzz: " << made << " make/unmake pairs");
}

TEST_CASE("null move round-trips") {
    Position pos("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    const std::string fen = pos.fen();
    const Key key = pos.key();
    pos.make_null_move();
    CHECK(pos.side_to_move() == BLACK);
    CHECK(pos.is_consistent());
    pos.unmake_null_move();
    CHECK(pos.fen() == fen);
    CHECK(pos.key() == key);
}

TEST_CASE("repetition and draw rules") {
    Position pos;
    for (const char* mv : {"g1f3", "g8f6", "f3g1", "f6g8"}) pos.make_move(pos.uci_to_move(mv));
    CHECK(pos.is_repetition(10));   // twofold inside the search tree
    CHECK_FALSE(pos.is_repetition(0));  // at the root a threefold is needed
    for (const char* mv : {"g1f3", "g8f6", "f3g1", "f6g8"}) pos.make_move(pos.uci_to_move(mv));
    CHECK(pos.is_repetition(0));

    CHECK(Position("8/8/4k3/8/8/3K4/8/8 w - - 0 1").has_insufficient_material());
    CHECK(Position("8/8/4k3/8/8/3KN3/8/8 w - - 0 1").has_insufficient_material());
    CHECK(Position("8/8/4kb2/8/8/3KB3/8/8 w - - 0 1").has_insufficient_material());  // same colour
    CHECK_FALSE(Position("8/8/2b1k3/8/8/3KB3/8/8 w - - 0 1").has_insufficient_material());
    CHECK_FALSE(Position("8/8/4k3/8/8/3KNN2/8/8 w - - 0 1").has_insufficient_material());
    CHECK(Position("8/8/4k3/8/8/3K4/8/7R w - - 100 80").is_draw(1));
    // Checkmate on the 100th half-move is a loss, not a draw.
    CHECK_FALSE(Position("7k/7Q/6K1/8/8/8/8/8 b - - 100 80").is_draw(1));
}

TEST_CASE("UCI move notation") {
    Position pos("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    Move oo = pos.uci_to_move("e1g1");
    REQUIRE(oo);
    CHECK(oo.is_castle());
    CHECK(pos.move_to_uci(oo) == "e1g1");
    CHECK(pos.uci_to_move("e1h1") == oo);
    CHECK(Position("8/P6k/8/8/8/8/8/K7 w - - 0 1").uci_to_move("a7a8n").promotion_type() == KNIGHT);
    CHECK_FALSE(pos.uci_to_move("e1e3"));

    Position f("bqnb1rkr/pp3ppp/3ppn2/2p5/5P2/P2P4/NPP1P1PP/BQ1BNRKR w HFhf - 2 9", true);
    CHECK(f.chess960());
    CHECK(f.fen() == "bqnb1rkr/pp3ppp/3ppn2/2p5/5P2/P2P4/NPP1P1PP/BQ1BNRKR w HFhf - 2 9");
}
