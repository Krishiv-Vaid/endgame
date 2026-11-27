#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "movegen.h"
#include "nnue/network.h"
#include "nnue/simd.h"
#include "search.h"

using namespace endgame;
using namespace endgame::nnue;

namespace {

struct Rng {
    std::uint64_t s;
    std::uint64_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    int range(int lo, int hi) { return lo + int(next() % std::uint64_t(hi - lo + 1)); }
};

// A random network with weights in realistic quantised ranges.
std::unique_ptr<Network> random_net(std::uint64_t seed) {
    auto net = std::make_unique<Network>();
    Rng r{seed};
    for (auto& w : net->ft_weights) w = std::int16_t(r.range(-180, 180));
    for (auto& b : net->ft_bias) b = std::int16_t(r.range(-60, 200));
    for (auto& w : net->out_weights) w = std::int16_t(r.range(-MAX_OUT_WEIGHT, MAX_OUT_WEIGHT));
    net->out_bias = r.range(-5000, 5000);
    return net;
}

bool load_random(std::uint64_t seed) {
    auto net = random_net(seed);
    std::string bytes = serialize(*net);
    std::string err;
    return load_memory(bytes.data(), bytes.size(), &err);
}

}  // namespace

TEST_CASE("NNUE SIMD kernels match the scalar reference") {
    Rng r{42};
    alignas(64) std::int16_t a[HIDDEN], b[HIDDEN], w[HIDDEN];
    for (int trial = 0; trial < 200; ++trial) {
        for (int i = 0; i < HIDDEN; ++i) {
            a[i] = std::int16_t(r.range(-600, 600));
            w[i] = std::int16_t(r.range(-MAX_OUT_WEIGHT, MAX_OUT_WEIGHT));
        }
        // Saturated extremes in some trials.
        if (trial % 10 == 0)
            for (int i = 0; i < HIDDEN; ++i) a[i] = QA, w[i] = std::int16_t(i % 2 ? MAX_OUT_WEIGHT : -MAX_OUT_WEIGHT);
        CHECK(simd::screlu_dot(a, w) == simd::scalar::screlu_dot(a, w));

        std::memcpy(b, a, sizeof(a));
        simd::add(a, w);
        simd::scalar::add(b, w);
        CHECK(std::memcmp(a, b, sizeof(a)) == 0);
        simd::sub(a, w);
        simd::scalar::sub(b, w);
        CHECK(std::memcmp(a, b, sizeof(a)) == 0);
    }
    MESSAGE("SIMD path: " << std::string(simd::name()));
}

TEST_CASE("NNUE file round-trip, hash and validation") {
    auto net = random_net(7);
    std::string bytes = serialize(*net);
    std::string err;
    REQUIRE(load_memory(bytes.data(), bytes.size(), &err));
    CHECK(std::memcmp(active()->ft_weights, net->ft_weights, sizeof(net->ft_weights)) == 0);
    CHECK(active()->out_bias == net->out_bias);

    std::string corrupt = bytes;
    corrupt[1000] ^= 1;
    CHECK_FALSE(load_memory(corrupt.data(), corrupt.size(), &err));
    CHECK(err.find("hash") != std::string::npos);
    CHECK_FALSE(load_memory(bytes.data(), bytes.size() - 1, &err));

    auto bad = random_net(8);
    bad->out_weights[3] = MAX_OUT_WEIGHT + 1;
    std::string b2 = serialize(*bad);
    CHECK_FALSE(load_memory(b2.data(), b2.size(), &err));
    unload();
}

TEST_CASE("NNUE incremental updates equal a full refresh over random games") {
    REQUIRE(load_random(99));
    const Network& net = *active();
    std::ifstream in(std::string(ENDGAME_TEST_DATA_DIR) + "/perft/standard.epd");
    std::vector<std::string> fens;
    std::string line;
    while (std::getline(in, line))
        if (!line.empty()) fens.push_back(line.substr(0, line.find(';')));
    std::ifstream in960(std::string(ENDGAME_TEST_DATA_DIR) + "/perft/chess960.epd");
    int n960 = 0;
    while (std::getline(in960, line) && n960++ < 100) fens.push_back(line.substr(0, line.find(';')));

    Rng r{1234};
    int checked = 0;
    for (std::size_t g = 0; g < fens.size(); ++g) {
        Position pos(fens[g], g >= fens.size() - 100);
        Accumulator acc, fresh;
        refresh(net, pos, acc);
        for (int ply = 0; ply < 60; ++ply) {
            MoveList list;
            generate<GEN_ALL>(pos, list);
            if (!list.size()) break;
            Move m = list[int(r.next() % std::uint64_t(list.size()))];
            pos.make_move(m);
            Accumulator next;
            update(net, acc, pos.state_back(0), next);
            acc = next;
            refresh(net, pos, fresh);
            REQUIRE(std::memcmp(acc.v, fresh.v, sizeof(acc.v)) == 0);
            CHECK(output(net, acc, pos.side_to_move()) == output(net, fresh, pos.side_to_move()));
            ++checked;
        }
    }
    MESSAGE(checked << " incremental updates checked");
    unload();
}

TEST_CASE("search runs with a loaded network and is deterministic") {
    REQUIRE(load_random(5));
    Engine engine;
    SearchLimits limits;
    limits.depth = 5;
    Position pos("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    SearchResult a = engine.search(pos, limits, false);
    engine.new_game();
    SearchResult b = engine.search(pos, limits, false);
    CHECK(a.best);
    CHECK(a.nodes == b.nodes);
    CHECK(a.best == b.best);
    unload();
}
