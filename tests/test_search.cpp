#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

#include "doctest.h"
#include "eval_hce.h"
#include "search.h"

using namespace endgame;

namespace {

// Colour-flipped FEN: mirror ranks, swap piece case, side, castling and ep.
std::string flip_fen(const std::string& fen) {
    std::istringstream is(fen);
    std::string board, side, castle, ep, r50 = "0", full = "1";
    is >> board >> side >> castle >> ep >> r50 >> full;
    std::string ranks[8];
    int i = 0;
    std::istringstream bs(board);
    while (std::getline(bs, ranks[i], '/')) ++i;
    std::string out;
    for (int r = 7; r >= 0; --r) {
        for (char c : ranks[r]) out += std::isalpha((unsigned char)c) ? char(c ^ 32) : c;
        if (r) out += '/';
    }
    std::string cr;
    for (char c : castle) cr += c == '-' ? c : char(c ^ 32);
    if (ep != "-") ep[1] = ep[1] == '3' ? '6' : '3';
    return out + (side == "w" ? " b " : " w ") + cr + " " + ep + " " + r50 + " " + full;
}

}  // namespace

TEST_CASE("HCE is colour symmetric") {
    std::ifstream in(std::string(ENDGAME_TEST_DATA_DIR) + "/perft/standard.epd");
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::string fen = line.substr(0, line.find(';'));
        Position a(fen), b(flip_fen(fen));
        INFO(fen);
        CHECK(evaluate(a) == evaluate(b));
        ++n;
    }
    CHECK(n > 100);
}

TEST_CASE("HCE trace reproduces the evaluation") {
    for (const char* fen : {Position::START_FEN,
                            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                            "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"}) {
        Position pos(fen);
        hce::Trace tr;
        int white_view = evaluate_trace(pos, tr);
        long mg = 0, eg = 0;
        for (int i = 0; i < hce::PARAM_COUNT; ++i) {
            mg += long(tr.coef[i]) * hce::params[i].mg;
            eg += long(tr.coef[i]) * hce::params[i].eg;
        }
        int rebuilt = int((mg * tr.phase + eg * (PHASE_MAX - tr.phase)) / PHASE_MAX);
        CHECK(rebuilt == white_view);
        CHECK(white_view == (pos.side_to_move() == WHITE ? evaluate(pos) : -evaluate(pos)));
    }
}

TEST_CASE("search finds short mates and avoids stalemate") {
    Engine engine;
    SearchLimits limits;
    limits.depth = 6;

    // Mate in 2 (rook ladder): 1. Rg7 Kb8 2. Rh8#.
    Position m2("k7/8/8/8/8/8/8/K5RR w - - 0 1");
    SearchResult r = engine.search(m2, limits, false);
    CHECK(r.score == mate_in(3));

    Position m1("7k/8/6K1/8/8/8/8/R7 w - - 0 1");
    r = engine.search(m1, limits, false);
    CHECK(m1.move_to_uci(r.best) == "a1a8");
    CHECK(r.score == mate_in(1));

    // Black to move is checkmated.
    Position mated("R6k/8/6K1/8/8/8/8/8 b - - 0 1");
    r = engine.search(mated, limits, false);
    CHECK_FALSE(r.best);
    CHECK(r.score == mated_in(0));

    // Stalemate is a draw.
    Position stale("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    r = engine.search(stale, limits, false);
    CHECK(r.score == 0);
}

TEST_CASE("search wins hanging material and respects node limits") {
    Engine engine;
    SearchLimits limits;
    limits.depth = 4;
    Position pos("4k3/8/8/3q4/8/8/3R4/3K4 w - - 0 1");
    SearchResult r = engine.search(pos, limits, false);
    CHECK(pos.move_to_uci(r.best) == "d2d5");

    SearchLimits nodes;
    nodes.nodes = 20000;
    r = engine.search(Position(), nodes, false);
    CHECK(r.best);
    CHECK(r.nodes < 30000);
}

TEST_CASE("deep iterative deepening on a search thread does not overflow the stack") {
    // Regression: in this mate-in-1 position iterative deepening reaches
    // depth ~228 within milliseconds; with the default 512 KB macOS thread
    // stack the engine crashed (SIGBUS) during the sanity match.
    Engine engine;
    Position pos("5Q2/1Q6/8/1k1N4/8/7P/7K/1q1R4 b - - 1 65");
    SearchLimits limits;
    limits.depth = MAX_PLY - 1;
    engine.go(pos, limits);
    engine.wait();
    CHECK_FALSE(engine.searching());
}
