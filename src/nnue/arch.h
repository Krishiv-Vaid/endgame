#pragma once

#include <cstdint>

#include "../types.h"

namespace endgame::nnue {

// Architecture 1: (768 -> HIDDEN) x 2 perspectives -> 1, SCReLU activation.
// Inputs are (relative colour, piece type, square) from each side's view;
// black's view is mirrored vertically so both perspectives share weights.
constexpr std::uint32_t ARCH_ID = 1;
constexpr int INPUTS = 768;
constexpr int HIDDEN = 256;

// Quantisation: feature transformer x QA, output layer x QB, eval x SCALE.
constexpr int QA = 255;
constexpr int QB = 64;
constexpr int SCALE = 400;
// Output weights must satisfy |w| <= MAX_OUT_WEIGHT so that clamp(a)*w fits
// in int16 inside the SIMD kernels (255 * 128 = 32640).
constexpr int MAX_OUT_WEIGHT = 128;

inline int feature_index(Color perspective, Piece p, Square s) {
    const int relative = color_of(p) != perspective;
    const int sq = perspective == WHITE ? int(s) : int(s) ^ 56;
    return (relative * 6 + type_of(p)) * 64 + sq;
}

}  // namespace endgame::nnue
