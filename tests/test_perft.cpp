#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "perft.h"
#include "position.h"

using namespace endgame;

namespace {

struct PerftCase {
    std::string fen;
    std::vector<std::pair<int, std::uint64_t>> counts;  // (depth, nodes)
};

std::vector<PerftCase> load_epd(const std::string& name) {
    std::ifstream in(std::string(ENDGAME_TEST_DATA_DIR) + "/perft/" + name);
    REQUIRE(in.good());
    std::vector<PerftCase> cases;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream is(line);
        PerftCase c;
        std::string field;
        std::getline(is, c.fen, ';');
        while (std::getline(is, field, ';')) {
            std::istringstream fs(field);
            std::string d;
            std::uint64_t n;
            if (fs >> d >> n && d.size() > 1 && d[0] == 'D') c.counts.push_back({std::stoi(d.substr(1)), n});
        }
        cases.push_back(c);
    }
    return cases;
}

// Skip depths above this many leaf nodes unless ENDGAME_PERFT_MAX_NODES says
// otherwise (0 = run everything).
std::uint64_t node_cap() {
    if (const char* env = std::getenv("ENDGAME_PERFT_MAX_NODES")) return std::strtoull(env, nullptr, 10);
    return 20'000'000;
}

void run_suite(const std::string& name, bool chess960) {
    const std::uint64_t cap = node_cap();
    int checked = 0;
    for (const PerftCase& c : load_epd(name)) {
        Position pos;
        REQUIRE_MESSAGE(pos.set(c.fen, chess960), c.fen);
        for (auto [depth, nodes] : c.counts) {
            if (cap && nodes > cap) break;
            INFO(c.fen << " depth " << depth);
            CHECK(perft(pos, depth) == nodes);
            ++checked;
        }
        CHECK(pos.fen() == Position(c.fen, chess960).fen());
    }
    MESSAGE(name << ": " << checked << " (position, depth) pairs checked");
}

}  // namespace

TEST_CASE("perft: reference positions from the build brief") {
    struct { const char* fen; int depth; std::uint64_t nodes; } cases[] = {
        {Position::START_FEN, 5, 4'865'609},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4'085'603},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 6, 11'030'083},
        {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 5, 15'833'292},
    };
    for (auto& c : cases) {
        Position pos(c.fen);
        INFO(c.fen);
        CHECK(perft(pos, c.depth) == c.nodes);
    }
}

TEST_CASE("perft: standard suite") { run_suite("standard.epd", false); }

TEST_CASE("perft: chess960 suite") { run_suite("chess960.epd", true); }
