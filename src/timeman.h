#pragma once

#include <chrono>
#include <cstdint>

#include "types.h"

namespace endgame {

using TimePoint = std::int64_t;  // milliseconds

inline TimePoint now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

struct SearchLimits {
    TimePoint time[COLOR_NB] = {0, 0};
    TimePoint inc[COLOR_NB] = {0, 0};
    int movestogo = 0;
    int depth = 0;
    TimePoint movetime = 0;
    std::uint64_t nodes = 0;       // hard node limit
    std::uint64_t soft_nodes = 0;  // no new iteration after this many nodes
    bool infinite = false;
    bool ponder = false;

    bool use_time() const { return time[WHITE] || time[BLACK] || movetime; }
};

// Soft limit: don't start another iteration after it. Hard limit: abort the
// search mid-iteration.
class TimeManager {
public:
    void init(const SearchLimits& limits, Color us, TimePoint start, int move_overhead);
    TimePoint elapsed() const { return now_ms() - start_; }
    TimePoint soft() const { return soft_; }
    TimePoint hard() const { return hard_; }
    bool enabled() const { return enabled_; }

private:
    TimePoint start_ = 0;
    TimePoint soft_ = 0;
    TimePoint hard_ = 0;
    bool enabled_ = false;
};

}  // namespace endgame
