#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "movegen.h"
#include "position.h"
#include "threads.h"
#include "timeman.h"
#include "tt.h"

namespace endgame {

struct SearchResult {
    Move best = Move::none();
    Move ponder = Move::none();
    int score = 0;
    int depth = 0;
    std::uint64_t nodes = 0;
};

struct StackEntry {
    int static_eval = VALUE_NONE;
    Move move = Move::none();
    Move killers[2] = {Move::none(), Move::none()};
};

class Engine;

class Worker {
public:
    explicit Worker(Engine& engine) : engine_(engine) {}
    SearchResult run(const Position& root, const SearchLimits& limits, bool print);
    std::uint64_t nodes() const { return nodes_; }

private:
    enum NodeType { NON_PV, PV };

    template <NodeType NT>
    int search(int alpha, int beta, int depth, int ply);
    template <NodeType NT>
    int qsearch(int alpha, int beta, int ply);

    void check_time();
    void update_pv(int ply, Move m);
    void print_info(int depth, int score, TimePoint elapsed) const;
    bool should_stop() const { return stop_flag_; }

    Engine& engine_;
    Position pos_;
    SearchLimits limits_;
    TimeManager tm_;
    std::uint64_t nodes_ = 0;
    int seldepth_ = 0;
    bool stop_flag_ = false;
    int root_depth_ = 0;

    StackEntry stack_[MAX_PLY + 8];
    Move pv_[MAX_PLY + 2][MAX_PLY + 2];
    int pv_len_[MAX_PLY + 2] = {};
};

// Owns the TT, options and the search thread; the UCI layer talks to this.
class Engine {
public:
    Engine();
    ~Engine();

    void set_hash(std::size_t mb);
    void new_game();
    int move_overhead = 10;

    // Asynchronous search for UCI: prints info lines and "bestmove".
    void go(const Position& pos, const SearchLimits& limits);
    void stop();       // request stop and wait for bestmove
    void ponderhit();  // switch from pondering to normal timing
    void wait();       // block until the current search finished
    bool searching() const { return searching_; }

    // Synchronous search (bench, datagen). `print` controls info output.
    SearchResult search(const Position& pos, const SearchLimits& limits, bool print);

    TranspositionTable& tt() { return tt_; }
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> pondering{false};

private:
    TranspositionTable tt_;
    NativeThread thread_;
    std::atomic<bool> searching_{false};
    std::mutex mutex_;
};

}  // namespace endgame
