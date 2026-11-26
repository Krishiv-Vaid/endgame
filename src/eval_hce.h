#pragma once

#include <array>
#include <cstdint>

#include "position.h"

namespace endgame {

// Middlegame / endgame score pair, blended by game phase.
struct S {
    int mg = 0;
    int eg = 0;
    constexpr S() = default;
    constexpr S(int m, int e) : mg(m), eg(e) {}
    constexpr S operator+(S o) const { return {mg + o.mg, eg + o.eg}; }
    constexpr S operator-(S o) const { return {mg - o.mg, eg - o.eg}; }
    constexpr S operator*(int k) const { return {mg * k, eg * k}; }
    constexpr S& operator+=(S o) { mg += o.mg, eg += o.eg; return *this; }
    constexpr S& operator-=(S o) { mg -= o.mg, eg -= o.eg; return *this; }
};

// The HCE is linear in every parameter below, so a Texel tuner can fit all of
// them from a trace of feature counts (see tools/texel).
namespace hce {

constexpr int MOB_N_COUNT = 9, MOB_B_COUNT = 14, MOB_R_COUNT = 15, MOB_Q_COUNT = 28;
constexpr int KING_DANGER_COUNT = 40;

enum ParamIndex : int {
    P_MATERIAL = 0,                                   // [PAWN..QUEEN]
    P_PST = P_MATERIAL + 5,                           // [pt][sq], white's view
    P_MOB_N = P_PST + 6 * 64,
    P_MOB_B = P_MOB_N + MOB_N_COUNT,
    P_MOB_R = P_MOB_B + MOB_B_COUNT,
    P_MOB_Q = P_MOB_R + MOB_R_COUNT,
    P_PASSED = P_MOB_Q + MOB_Q_COUNT,                 // [relative rank]
    P_ISOLATED = P_PASSED + 8,
    P_DOUBLED = P_ISOLATED + 1,
    P_BISHOP_PAIR = P_DOUBLED + 1,
    P_ROOK_OPEN = P_BISHOP_PAIR + 1,
    P_ROOK_SEMI_OPEN = P_ROOK_OPEN + 1,
    P_SHIELD = P_ROOK_SEMI_OPEN + 1,                  // [one / two ranks ahead]
    P_KING_DANGER = P_SHIELD + 2,                     // [attack units]
    P_TEMPO = P_KING_DANGER + KING_DANGER_COUNT,
    PARAM_COUNT
};

extern std::array<S, PARAM_COUNT> params;

// Feature counts (white minus black) for one position: eval_white =
// sum(coef[i] * params[i]) tapered by phase. Used by the Texel tuner.
struct Trace {
    std::array<std::int16_t, PARAM_COUNT> coef{};
    int phase = 0;  // 0 (bare kings) .. 24 (all pieces)
};

void init_default_params();
const char* param_name(int index, char* buf, int buflen);

}  // namespace hce

// Static evaluation in centipawns from the side to move's point of view.
int evaluate(const Position& pos);

// White's-view evaluation that also fills a feature trace.
int evaluate_trace(const Position& pos, hce::Trace& trace);

constexpr int PHASE_MAX = 24;
int game_phase(const Position& pos);

}  // namespace endgame
