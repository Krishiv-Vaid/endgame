#include "timeman.h"

#include <algorithm>

namespace endgame {

void TimeManager::init(const SearchLimits& limits, Color us, TimePoint start, int move_overhead) {
    start_ = start;
    enabled_ = limits.use_time();
    if (!enabled_) return;

    if (limits.movetime) {
        soft_ = hard_ = std::max<TimePoint>(1, limits.movetime - move_overhead);
        return;
    }

    const TimePoint remaining = limits.time[us];
    const TimePoint inc = limits.inc[us];
    // Never plan to use time we can't afford once overhead is paid.
    const TimePoint usable = std::max<TimePoint>(1, remaining - move_overhead);
    const int mtg = limits.movestogo ? std::min(limits.movestogo, 40) : 20;

    // Spend 3/4 of the increment while the bank is healthy, less as the bank
    // drops below ~10 increments, so the clock settles at a safe reserve
    // instead of draining towards zero (two sanity-match time losses at
    // 8+0.08 came from living on ~100 ms with OS scheduling jitter).
    TimePoint inc_part = inc * 3 / 4;
    if (inc > 0 && remaining < 10 * inc) inc_part = inc_part * remaining / (10 * inc);

    soft_ = usable / mtg + inc_part;
    // Abort at 3x the target, but never past half of what is left (90% when
    // this is the last move before the time control).
    const TimePoint cap = limits.movestogo == 1 ? usable * 9 / 10 : usable / 2;
    hard_ = std::min(soft_ * 3, cap);
    soft_ = std::min(soft_, hard_);
    soft_ = std::max<TimePoint>(1, soft_);
    hard_ = std::max<TimePoint>(1, hard_);
}

}  // namespace endgame
