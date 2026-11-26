#include "search.h"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <sstream>

#include "eval_hce.h"

namespace endgame {

namespace {

constexpr int MVV_LVA_VALUE[7] = {1, 3, 3, 5, 9, 0, 0};
constexpr int DELTA_VALUE[7] = {100, 320, 330, 500, 950, 0, 0};

// Baseline ordering: captures and promotions by MVV-LVA, then quiets.
struct ScoredMoves {
    Move moves[MAX_MOVES];
    int scores[MAX_MOVES];
    int count = 0;
    int next = 0;

    ScoredMoves(const Position& pos, const MoveList& list, Move tt_move = Move::none()) {
        for (Move m : list) {
            int s = 0;
            if (m.is_capture()) {
                PieceType victim = m.is_en_passant() ? PAWN : type_of(pos.piece_on(m.to()));
                PieceType attacker = type_of(pos.piece_on(m.from()));
                s = MVV_LVA_VALUE[victim] * 16 - MVV_LVA_VALUE[attacker];
                // Losing captures (SEE < 0) go after all quiet moves.
                s += pos.see_ge(m, 0) ? 1000 : -1000;
            }
            if (m.is_promotion()) s += m.promotion_type() == QUEEN ? 1000 + 9 * 16 : -2000;
            if (m == tt_move) s = 1'000'000;  // TT move first
            moves[count] = m;
            scores[count++] = s;
        }
    }

    // Selection sort, one step at a time: most nodes cut off early.
    Move pick() {
        if (next >= count) return Move::none();
        int best = next;
        for (int i = next + 1; i < count; ++i)
            if (scores[i] > scores[best]) best = i;
        std::swap(moves[best], moves[next]);
        std::swap(scores[best], scores[next]);
        return moves[next++];
    }
};

std::string score_to_uci(int v) {
    if (v >= VALUE_MATE_IN_MAX_PLY) return "mate " + std::to_string((VALUE_MATE - v + 1) / 2);
    if (v <= VALUE_MATED_IN_MAX_PLY) return "mate " + std::to_string(-(VALUE_MATE + v) / 2);
    return "cp " + std::to_string(v);
}

}  // namespace

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------
void Worker::check_time() {
    if (engine_.stop_requested.load(std::memory_order_relaxed)) {
        stop_flag_ = true;
        return;
    }
    if (root_depth_ <= 1) return;  // always finish depth 1 so we have a move
    if (limits_.nodes && nodes_ >= limits_.nodes) stop_flag_ = true;
    if (tm_.enabled() && !engine_.pondering.load(std::memory_order_relaxed) &&
        tm_.elapsed() >= tm_.hard())
        stop_flag_ = true;
}

void Worker::update_pv(int ply, Move m) {
    pv_[ply][0] = m;
    const int child = pv_len_[ply + 1];
    for (int i = 0; i < child; ++i) pv_[ply][i + 1] = pv_[ply + 1][i];
    pv_len_[ply] = child + 1;
}

void Worker::print_info(int depth, int score, TimePoint elapsed) const {
    std::ostringstream os;
    const std::uint64_t nps = elapsed > 0 ? nodes_ * 1000 / std::uint64_t(elapsed) : nodes_ * 1000;
    os << "info depth " << depth << " seldepth " << seldepth_ << " multipv 1 score "
       << score_to_uci(score) << " nodes " << nodes_ << " nps " << nps << " hashfull "
       << engine_.tt().hashfull() << " time " << elapsed << " pv";
    // Walk the PV on a copy so moves print in the right notation.
    Position p = pos_;
    for (int i = 0; i < pv_len_[0]; ++i) {
        os << ' ' << p.move_to_uci(pv_[0][i]);
        p.make_move(pv_[0][i]);
    }
    std::cout << os.str() << std::endl;
}

SearchResult Worker::run(const Position& root, const SearchLimits& limits, bool print) {
    const TimePoint start = limits.start_time ? limits.start_time : now_ms();
    pos_ = root;
    limits_ = limits;
    tm_.init(limits, root.side_to_move(), start, engine_.move_overhead);
    nodes_ = 0;
    stop_flag_ = false;
    engine_.tt().new_search();

    SearchResult result;
    MoveList root_moves;
    generate<GEN_ALL>(pos_, root_moves);
    if (!root_moves.size()) {
        result.score = pos_.in_check() ? mated_in(0) : 0;
        return result;
    }
    result.best = root_moves[0];

    const int max_depth = limits.depth ? std::min(limits.depth, MAX_PLY - 1) : MAX_PLY - 1;
    for (int depth = 1; depth <= max_depth; ++depth) {
        root_depth_ = depth;
        seldepth_ = 0;
        const int score = search<PV>(-VALUE_INF, VALUE_INF, depth, 0);
        if (stop_flag_ && depth > 1) break;  // incomplete iteration: discard

        if (pv_len_[0] > 0) {
            result.best = pv_[0][0];
            result.ponder = pv_len_[0] > 1 ? pv_[0][1] : Move::none();
        }
        result.score = score;
        result.depth = depth;
        if (print) print_info(depth, score, now_ms() - start);

        if (stop_flag_) break;
        if (limits.soft_nodes && nodes_ >= limits.soft_nodes) break;
        if (tm_.enabled() && !engine_.pondering.load(std::memory_order_relaxed) &&
            tm_.elapsed() >= tm_.soft())
            break;
    }
    result.nodes = nodes_;
    return result;
}

template <Worker::NodeType NT>
int Worker::search(int alpha, int beta, int depth, int ply) {
    constexpr bool pv_node = NT == PV;
    const bool root = ply == 0;

    if (depth <= 0) return qsearch<NT>(alpha, beta, ply);

    pv_len_[ply] = 0;
    if ((++nodes_ & 1023) == 0) check_time();
    if (stop_flag_) return 0;
    seldepth_ = std::max(seldepth_, ply + 1);

    const bool in_check = pos_.in_check();
    if (!root) {
        if (pos_.is_draw(ply)) return 0;
        if (ply >= MAX_PLY - 1) return in_check ? 0 : evaluate(pos_);
        // Mate distance pruning.
        alpha = std::max(alpha, mated_in(ply));
        beta = std::min(beta, mate_in(ply + 1));
        if (alpha >= beta) return alpha;
    }

    const Key key = pos_.key();
    const TTProbe tte = engine_.tt().probe(key, ply);
    if (!pv_node && tte.hit && tte.depth >= depth && tte.score != VALUE_NONE &&
        (tte.bound == BOUND_EXACT || (tte.bound == BOUND_LOWER && tte.score >= beta) ||
         (tte.bound == BOUND_UPPER && tte.score <= alpha)))
        return tte.score;

    int static_eval = VALUE_NONE;
    if (!in_check) static_eval = tte.hit && tte.eval != VALUE_NONE ? tte.eval : evaluate(pos_);
    stack_[ply].static_eval = static_eval;

    MoveList list;
    generate<GEN_ALL>(pos_, list);
    if (!list.size()) return in_check ? mated_in(ply) : 0;
    ScoredMoves picker(pos_, list, tte.move);

    int best_score = -VALUE_INF;
    Move best_move = Move::none();
    int move_count = 0;

    while (Move m = picker.pick()) {
        // SEE pruning at shallow depth: skip moves that lose too much material
        // once we already have a non-losing line.
        if (!root && !in_check && best_score > VALUE_MATED_IN_MAX_PLY && depth <= 6 &&
            !pos_.see_ge(m, m.is_capture() ? -100 * depth : -50 * depth))
            continue;

        ++move_count;
        stack_[ply].move = m;
        engine_.tt().prefetch(pos_.key_after(m));
        pos_.make_move(m);

        int score;
        if (move_count == 1) {
            score = -search<NT>(-beta, -alpha, depth - 1, ply + 1);
        } else {
            score = -search<NON_PV>(-alpha - 1, -alpha, depth - 1, ply + 1);
            if (pv_node && score > alpha && score < beta)
                score = -search<PV>(-beta, -alpha, depth - 1, ply + 1);
        }
        pos_.unmake_move();
        if (stop_flag_) return 0;

        if (score > best_score) {
            best_score = score;
            if (score > alpha) {
                best_move = m;
                if (pv_node) update_pv(ply, m);
                if (score >= beta) break;
                alpha = score;
            }
        }
    }

    const Bound bound = best_score >= beta ? BOUND_LOWER
                        : (pv_node && best_move) ? BOUND_EXACT
                                                 : BOUND_UPPER;
    engine_.tt().store(key, ply, best_score, static_eval, best_move, depth, bound, pv_node);
    return best_score;
}

template <Worker::NodeType NT>
int Worker::qsearch(int alpha, int beta, int ply) {
    constexpr bool pv_node = NT == PV;

    pv_len_[ply] = 0;
    if ((++nodes_ & 1023) == 0) check_time();
    if (stop_flag_) return 0;
    seldepth_ = std::max(seldepth_, ply + 1);

    const bool in_check = pos_.in_check();
    if (pos_.is_draw(ply)) return 0;
    if (ply >= MAX_PLY - 1) return in_check ? 0 : evaluate(pos_);

    const Key key = pos_.key();
    const TTProbe tte = engine_.tt().probe(key, ply);
    if (!pv_node && tte.hit && tte.score != VALUE_NONE &&
        (tte.bound == BOUND_EXACT || (tte.bound == BOUND_LOWER && tte.score >= beta) ||
         (tte.bound == BOUND_UPPER && tte.score <= alpha)))
        return tte.score;

    int best_score, static_eval = VALUE_NONE;
    MoveList list;
    if (in_check) {
        best_score = -VALUE_INF;
        generate<GEN_ALL>(pos_, list);
        if (!list.size()) return mated_in(ply);
    } else {
        static_eval = tte.hit && tte.eval != VALUE_NONE ? tte.eval : evaluate(pos_);
        best_score = static_eval;
        if (best_score >= beta) return best_score;
        alpha = std::max(alpha, best_score);
        generate<GEN_NOISY>(pos_, list);
    }

    ScoredMoves picker(pos_, list, tte.move);
    Move best_move = Move::none();
    while (Move m = picker.pick()) {
        // Delta pruning: even winning the captured piece can't reach alpha.
        if (!in_check && !m.is_promotion()) {
            PieceType victim = m.is_en_passant() ? PAWN : type_of(pos_.piece_on(m.to()));
            if (static_eval + DELTA_VALUE[victim] + 200 <= alpha) continue;
        }
        // SEE pruning: captures that lose material can't help a quiet position.
        if (!in_check && !pos_.see_ge(m, 0)) continue;
        pos_.make_move(m);
        const int score = -qsearch<NT>(-beta, -alpha, ply + 1);
        pos_.unmake_move();
        if (stop_flag_) return 0;

        if (score > best_score) {
            best_score = score;
            if (score > alpha) {
                best_move = m;
                if (pv_node) update_pv(ply, m);
                if (score >= beta) break;
                alpha = score;
            }
        }
    }

    const Bound bound = best_score >= beta ? BOUND_LOWER : BOUND_UPPER;
    engine_.tt().store(key, ply, best_score, static_eval, best_move, 0, bound, pv_node);
    return best_score;
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------
Engine::Engine() { tt_.resize(16); }

Engine::~Engine() { stop(); }

void Engine::set_hash(std::size_t mb) {
    wait();
    tt_.resize(mb);
}

void Engine::new_game() {
    wait();
    tt_.clear();
}

void Engine::go(const Position& pos, const SearchLimits& limits) {
    wait();
    stop_requested = false;
    pondering = limits.ponder;
    searching_ = true;
    thread_ = NativeThread([this, pos, limits]() {
        auto worker = std::make_unique<Worker>(*this);
        SearchResult r = worker->run(pos, limits, true);
        // UCI: never send bestmove while pondering or in infinite mode until
        // the GUI says stop / ponderhit.
        while ((pondering || limits.infinite) && !stop_requested)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::string out = "bestmove " + pos.move_to_uci(r.best);
        if (r.best && r.ponder) {
            Position after = pos;
            after.make_move(r.best);
            out += " ponder " + after.move_to_uci(r.ponder);
        }
        std::cout << out << std::endl;
        searching_ = false;
    });
}

void Engine::stop() {
    stop_requested = true;
    pondering = false;
    wait();
}

void Engine::ponderhit() { pondering = false; }

void Engine::wait() {
    if (thread_.joinable()) thread_.join();
}

SearchResult Engine::search(const Position& pos, const SearchLimits& limits, bool print) {
    wait();
    stop_requested = false;
    pondering = false;
    auto worker = std::make_unique<Worker>(*this);
    return worker->run(pos, limits, print);
}

}  // namespace endgame
