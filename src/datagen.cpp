#include "datagen.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

#include "movegen.h"
#include "search.h"
#include "threads.h"

namespace endgame {

namespace {

struct Rng {
    std::uint64_t s;
    std::uint64_t next() {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
};

struct Record {
    std::string fen;
    int score;  // white's view
};

constexpr int WIN_ADJ_SCORE = 2500, WIN_ADJ_PLIES = 4;
constexpr int DRAW_ADJ_SCORE = 10, DRAW_ADJ_PLIES = 8, DRAW_ADJ_MIN_PLY = 80;
constexpr int OPENING_MAX_EVAL = 1000;

std::atomic<std::uint64_t> g_positions{0};
std::atomic<std::uint64_t> g_games{0};

// Plays a random opening; returns false if the game ended or the position is
// too unbalanced after a short search.
bool random_opening(Position& pos, Engine& engine, Rng& rng, int plies) {
    pos.set(Position::START_FEN);
    for (int i = 0; i < plies; ++i) {
        MoveList list;
        generate<GEN_ALL>(pos, list);
        if (!list.size()) return false;
        pos.make_move(list[int(rng.next() % std::uint64_t(list.size()))]);
    }
    MoveList list;
    generate<GEN_ALL>(pos, list);
    if (!list.size()) return false;
    SearchLimits l;
    l.depth = 8;
    l.nodes = 50000;
    SearchResult r = engine.search(pos, l, false);
    return std::abs(r.score) <= OPENING_MAX_EVAL;
}

void worker(const DatagenOptions& opts, int id) {
    Rng rng{opts.seed * 0x1000193ULL + std::uint64_t(id) * 0x9E3779B9ULL};
    auto engine = std::make_unique<Engine>();
    std::ofstream out(opts.output + "_t" + std::to_string(id) + ".txt", std::ios::app);
    const std::uint64_t per_thread_target = opts.positions;

    while (g_positions.load() < per_thread_target) {
        engine->new_game();
        Position pos;
        const int plies = opts.random_plies + int(rng.next() % 2);
        if (!random_opening(pos, *engine, rng, plies)) continue;

        std::vector<Record> records;
        double result = -1;  // white's result
        int win_streak = 0, draw_streak = 0, ply = 0;

        while (result < 0) {
            MoveList list;
            generate<GEN_ALL>(pos, list);
            if (!list.size()) {
                result = pos.in_check() ? (pos.side_to_move() == WHITE ? 0.0 : 1.0) : 0.5;
                break;
            }
            if (pos.is_draw(0)) {
                result = 0.5;
                break;
            }

            SearchLimits l;
            l.soft_nodes = opts.soft_nodes;
            l.nodes = opts.hard_nodes;
            SearchResult r = engine->search(pos, l, false);
            if (!r.best) break;
            const int white_score = pos.side_to_move() == WHITE ? r.score : -r.score;

            if (!pos.in_check() && !r.best.is_noisy() && !is_mate_score(r.score))
                records.push_back({pos.fen(), white_score});

            // Adjudication.
            win_streak = std::abs(white_score) > WIN_ADJ_SCORE ? win_streak + 1 : 0;
            if (win_streak >= WIN_ADJ_PLIES) {
                result = white_score > 0 ? 1.0 : 0.0;
                break;
            }
            draw_streak = (ply >= DRAW_ADJ_MIN_PLY && std::abs(white_score) < DRAW_ADJ_SCORE)
                              ? draw_streak + 1 : 0;
            if (draw_streak >= DRAW_ADJ_PLIES) {
                result = 0.5;
                break;
            }

            pos.make_move(r.best);
            ++ply;
        }
        if (result < 0) continue;

        std::ostringstream buf;
        for (const Record& rec : records)
            buf << rec.fen << " | " << rec.score << " | " << (result == 1.0 ? "1.0" : result == 0.0 ? "0.0" : "0.5") << '\n';
        out << buf.str();
        out.flush();
        g_positions += records.size();
        g_games += 1;
    }
}

}  // namespace

void run_datagen(const DatagenOptions& opts) {
    std::cout << "datagen: " << opts.threads << " threads, " << opts.positions << " positions, nodes "
              << opts.soft_nodes << "/" << opts.hard_nodes << ", output " << opts.output << "_t*.txt"
              << std::endl;
    g_positions = 0;
    g_games = 0;
    const auto start = std::chrono::steady_clock::now();
    std::vector<NativeThread> threads;
    for (int i = 0; i < opts.threads; ++i) threads.emplace_back([&opts, i]() { worker(opts, i); });

    std::atomic<bool> done{false};
    std::thread progress([&]() {
        while (!done) {
            for (int i = 0; i < 100 && !done; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::uint64_t n = g_positions.load();
            double rate = secs > 0 ? double(n) / secs : 0;
            double eta = rate > 0 && n < opts.positions ? double(opts.positions - n) / rate : 0;
            std::printf("datagen: %llu positions, %llu games, %.0f pos/s, ETA %.0f s\n",
                        (unsigned long long)n, (unsigned long long)g_games.load(), rate, eta);
            std::fflush(stdout);
        }
    });
    for (auto& t : threads) t.join();
    done = true;
    progress.join();
    std::cout << "datagen: done, " << g_positions.load() << " positions in " << g_games.load()
              << " games" << std::endl;
}

}  // namespace endgame
