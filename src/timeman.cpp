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

    soft_ = remaining / mtg + inc * 3 / 4;
    // Abort at 3x the target, but never past 70% of what is left (90% when
    // this is the last move before the time control).
    const TimePoint cap = limits.movestogo == 1 ? usable * 9 / 10 : usable * 7 / 10;
    hard_ = std::min(soft_ * 3, cap);
    soft_ = std::min(soft_, hard_);
    soft_ = std::max<TimePoint>(1, soft_);
    hard_ = std::max<TimePoint>(1, hard_);
}

}  // namespace endgame
