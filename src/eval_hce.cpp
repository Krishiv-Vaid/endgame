#include "eval_hce.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace endgame {

namespace hce {

std::array<S, PARAM_COUNT> params;

namespace {

int center_distance(Square s) {
    // Chebyshev distance to the four centre squares: 0 (d4..e5) .. 3 (rim).
    int f = file_of(s), r = rank_of(s);
    int df = f < 4 ? 3 - f : f - 4;
    int dr = r < 4 ? 3 - r : r - 4;
    return std::max(df, dr);
}

}  // namespace

// Hand-written starting values. These are only the starting point for the
// Texel tuner; they come from simple shape formulas, not from any table.
void init_default_params() {
    params.fill(S{});
    const S material[5] = {{100, 120}, {320, 300}, {330, 320}, {470, 530}, {950, 980}};
    for (int pt = 0; pt < 5; ++pt) params[P_MATERIAL + pt] = material[pt];

    for (Square s = SQ_A1; s < SQUARE_NB; ++s) {
        const int f = file_of(s), r = rank_of(s), d = center_distance(s);
        const bool central_file = f == 3 || f == 4;
        S pawn{};
        if (r >= 1 && r <= 6) pawn = S{(r - 1) * 3 + (central_file && (r == 3 || r == 4) ? 12 : 0), (r - 1) * 6};
        params[P_PST + PAWN * 64 + s] = pawn;
        params[P_PST + KNIGHT * 64 + s] = S{15 - 10 * d, 10 - 8 * d};
        params[P_PST + BISHOP * 64 + s] = S{8 - 5 * d, 5 - 4 * d};
        params[P_PST + ROOK * 64 + s] = S{(r == 6 ? 12 : 0) + (central_file ? 4 : 0), 0};
        params[P_PST + QUEEN * 64 + s] = S{-3 * d, 10 - 5 * d};
        int king_mg = (r == 0 ? 10 : -15 * r) + ((f <= 2 || f >= 6) ? 10 : -10);
        params[P_PST + KING * 64 + s] = S{king_mg, 20 - 12 * d};
    }

    for (int i = 0; i < MOB_N_COUNT; ++i) params[P_MOB_N + i] = S{(i - 4) * 4, (i - 4) * 4};
    for (int i = 0; i < MOB_B_COUNT; ++i) params[P_MOB_B + i] = S{(i - 6) * 4, (i - 6) * 5};
    for (int i = 0; i < MOB_R_COUNT; ++i) params[P_MOB_R + i] = S{(i - 7) * 2, (i - 7) * 4};
    for (int i = 0; i < MOB_Q_COUNT; ++i) params[P_MOB_Q + i] = S{(i - 13), (i - 13) * 2};

    const S passed[8] = {{0, 0}, {5, 10}, {10, 20}, {15, 35}, {30, 60}, {50, 100}, {80, 150}, {0, 0}};
    for (int r = 0; r < 8; ++r) params[P_PASSED + r] = passed[r];
    params[P_ISOLATED] = S{-10, -15};
    params[P_DOUBLED] = S{-10, -20};
    params[P_BISHOP_PAIR] = S{30, 50};
    params[P_ROOK_OPEN] = S{20, 10};
    params[P_ROOK_SEMI_OPEN] = S{10, 5};
    params[P_SHIELD + 0] = S{10, 0};
    params[P_SHIELD + 1] = S{5, 0};
    for (int u = 0; u < KING_DANGER_COUNT; ++u) params[P_KING_DANGER + u] = S{-(u * u) / 3, -u};
    params[P_TEMPO] = S{15, 5};
}

const char* param_name(int i, char* buf, int n) {
    auto in = [i](int lo, int count) { return i >= lo && i < lo + count; };
    static const char* PT = "PNBRQK";
    if (in(P_MATERIAL, 5)) std::snprintf(buf, n, "material[%c]", PT[i - P_MATERIAL]);
    else if (in(P_PST, 384)) std::snprintf(buf, n, "pst[%c][%s]", PT[(i - P_PST) / 64], square_to_string(Square((i - P_PST) % 64)).c_str());
    else if (in(P_MOB_N, MOB_N_COUNT)) std::snprintf(buf, n, "mob_n[%d]", i - P_MOB_N);
    else if (in(P_MOB_B, MOB_B_COUNT)) std::snprintf(buf, n, "mob_b[%d]", i - P_MOB_B);
    else if (in(P_MOB_R, MOB_R_COUNT)) std::snprintf(buf, n, "mob_r[%d]", i - P_MOB_R);
    else if (in(P_MOB_Q, MOB_Q_COUNT)) std::snprintf(buf, n, "mob_q[%d]", i - P_MOB_Q);
    else if (in(P_PASSED, 8)) std::snprintf(buf, n, "passed[%d]", i - P_PASSED);
    else if (i == P_ISOLATED) std::snprintf(buf, n, "isolated");
    else if (i == P_DOUBLED) std::snprintf(buf, n, "doubled");
    else if (i == P_BISHOP_PAIR) std::snprintf(buf, n, "bishop_pair");
    else if (i == P_ROOK_OPEN) std::snprintf(buf, n, "rook_open");
    else if (i == P_ROOK_SEMI_OPEN) std::snprintf(buf, n, "rook_semi_open");
    else if (in(P_SHIELD, 2)) std::snprintf(buf, n, "shield[%d]", i - P_SHIELD);
    else if (in(P_KING_DANGER, KING_DANGER_COUNT)) std::snprintf(buf, n, "king_danger[%d]", i - P_KING_DANGER);
    else if (i == P_TEMPO) std::snprintf(buf, n, "tempo");
    else std::snprintf(buf, n, "?");
    return buf;
}

}  // namespace hce

using namespace hce;

namespace {

struct Masks {
    Bitboard passed[COLOR_NB][SQUARE_NB];    // enemy pawns here stop a passer
    Bitboard forward[COLOR_NB][SQUARE_NB];   // same file, strictly ahead
    Bitboard adjacent_files[8];
    Masks() {
        for (int f = 0; f < 8; ++f)
            adjacent_files[f] = (f > 0 ? file_bb(f - 1) : 0) | (f < 7 ? file_bb(f + 1) : 0);
        for (Square s = SQ_A1; s < SQUARE_NB; ++s) {
            Bitboard ahead_w = 0, ahead_b = 0;
            for (int r = rank_of(s) + 1; r < 8; ++r) ahead_w |= rank_bb(r);
            for (int r = rank_of(s) - 1; r >= 0; --r) ahead_b |= rank_bb(r);
            Bitboard files = file_bb(file_of(s)) | adjacent_files[file_of(s)];
            forward[WHITE][s] = ahead_w & file_bb(file_of(s));
            forward[BLACK][s] = ahead_b & file_bb(file_of(s));
            passed[WHITE][s] = ahead_w & files;
            passed[BLACK][s] = ahead_b & files;
        }
    }
};
const Masks masks;

constexpr int KING_ATTACK_WEIGHT[6] = {0, 2, 2, 3, 5, 0};

template <bool TRACE>
class Evaluator {
public:
    Evaluator(const Position& p, Trace* t) : pos_(p), trace_(t) {}

    int run() {
        pawn_attacks_[WHITE] = pawn_attacks_bb<WHITE>(pos_.pieces(WHITE, PAWN));
        pawn_attacks_[BLACK] = pawn_attacks_bb<BLACK>(pos_.pieces(BLACK, PAWN));

        S score = side<WHITE>() - side<BLACK>();
        if (pos_.side_to_move() == WHITE) score += term(WHITE, P_TEMPO);
        else score -= term(BLACK, P_TEMPO);

        int phase = game_phase(pos_);
        if constexpr (TRACE) trace_->phase = phase;
        return (score.mg * phase + score.eg * (PHASE_MAX - phase)) / PHASE_MAX;
    }

private:
    // Value of feature `idx` counted n times for colour c; records the count
    // (with white-positive sign) when tracing.
    S term(Color c, int idx, int n = 1) {
        if constexpr (TRACE) trace_->coef[idx] += std::int16_t(c == WHITE ? n : -n);
        return params[idx] * n;
    }

    template <Color Us>
    S side() {
        constexpr Color Them = ~Us;
        S s;
        const Bitboard occ = pos_.pieces();
        const Bitboard own = pos_.pieces(Us);
        const Bitboard own_pawns = pos_.pieces(Us, PAWN);
        const Bitboard their_pawns = pos_.pieces(Them, PAWN);
        const Bitboard mobility_area = ~own & ~pawn_attacks_[Them];

        // King zone of the *enemy* king, for our attack units.
        const Square their_king = pos_.king_square(Them);
        Bitboard zone = king_attacks(their_king) | square_bb(their_king);
        zone |= Them == WHITE ? shift<NORTH>(zone) : shift<SOUTH>(zone);
        int attack_units = 0;

        for (int pt = PAWN; pt <= KING; ++pt) {
            for (Bitboard b = pos_.pieces(Us, PieceType(pt)); b;) {
                Square sq = pop_lsb(b);
                Square rel = relative_square(Us, sq);
                if (pt != KING) s += term(Us, P_MATERIAL + pt);
                s += term(Us, P_PST + pt * 64 + rel);

                Bitboard att = 0;
                switch (pt) {
                    case KNIGHT:
                        att = knight_attacks(sq);
                        s += term(Us, P_MOB_N + popcount(att & mobility_area));
                        break;
                    case BISHOP:
                        att = bishop_attacks(sq, occ);
                        s += term(Us, P_MOB_B + popcount(att & mobility_area));
                        break;
                    case ROOK: {
                        att = rook_attacks(sq, occ);
                        s += term(Us, P_MOB_R + popcount(att & mobility_area));
                        Bitboard file = file_bb(file_of(sq));
                        if (!(file & own_pawns))
                            s += term(Us, (file & their_pawns) ? P_ROOK_SEMI_OPEN : P_ROOK_OPEN);
                        break;
                    }
                    case QUEEN:
                        att = queen_attacks(sq, occ);
                        s += term(Us, P_MOB_Q + popcount(att & mobility_area));
                        break;
                    case PAWN: {
                        if (!(masks.adjacent_files[file_of(sq)] & own_pawns)) s += term(Us, P_ISOLATED);
                        if (masks.forward[Us][sq] & own_pawns) s += term(Us, P_DOUBLED);
                        if (!(masks.passed[Us][sq] & their_pawns))
                            s += term(Us, P_PASSED + relative_rank(Us, sq));
                        break;
                    }
                    default: break;
                }
                if (att & zone)
                    attack_units += KING_ATTACK_WEIGHT[pt] + popcount(att & zone) - 1;
            }
        }

        if (more_than_one(pos_.pieces(Us, BISHOP))) s += term(Us, P_BISHOP_PAIR);

        // Pawn shield in front of our own king.
        const Square ksq = pos_.king_square(Us);
        Bitboard files = file_bb(file_of(ksq)) | masks.adjacent_files[file_of(ksq)];
        int kr = rank_of(ksq) + (Us == WHITE ? 1 : -1);
        if (kr >= 0 && kr <= 7) {
            int n1 = popcount(own_pawns & files & rank_bb(kr));
            if (n1) s += term(Us, P_SHIELD + 0, n1);
            int kr2 = kr + (Us == WHITE ? 1 : -1);
            if (kr2 >= 0 && kr2 <= 7) {
                int n2 = popcount(own_pawns & files & rank_bb(kr2));
                if (n2) s += term(Us, P_SHIELD + 1, n2);
            }
        }

        // King danger is scored against the defender, so it appears with the
        // defender's sign: we add it to *their* side by subtracting here.
        int u = std::min(attack_units, KING_DANGER_COUNT - 1);
        s -= term(Them, P_KING_DANGER + u);
        return s;
    }

    const Position& pos_;
    Trace* trace_;
    Bitboard pawn_attacks_[COLOR_NB];
};

}  // namespace

int game_phase(const Position& pos) {
    int phase = popcount(pos.pieces(KNIGHT)) + popcount(pos.pieces(BISHOP)) +
                2 * popcount(pos.pieces(ROOK)) + 4 * popcount(pos.pieces(QUEEN));
    return std::min(phase, PHASE_MAX);
}

int evaluate(const Position& pos) {
    int v = Evaluator<false>(pos, nullptr).run();
    return pos.side_to_move() == WHITE ? v : -v;
}

int evaluate_trace(const Position& pos, Trace& trace) {
    trace = Trace{};
    return Evaluator<true>(pos, &trace).run();
}

}  // namespace endgame
