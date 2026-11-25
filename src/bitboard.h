#pragma once

#include <bit>
#include <string>

#include "types.h"

namespace endgame {

constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
constexpr Bitboard FILE_H_BB = FILE_A_BB << 7;
constexpr Bitboard RANK_1_BB = 0xFFULL;
constexpr Bitboard RANK_8_BB = RANK_1_BB << 56;

constexpr Bitboard file_bb(int f) { return FILE_A_BB << f; }
constexpr Bitboard rank_bb(int r) { return RANK_1_BB << (8 * r); }
constexpr Bitboard square_bb(Square s) { return Bitboard(1) << s; }

constexpr Bitboard relative_rank_bb(Color c, int r) { return rank_bb(c == WHITE ? r : 7 - r); }

inline int popcount(Bitboard b) { return std::popcount(b); }
inline Square lsb(Bitboard b) {
    assert(b);
    return Square(std::countr_zero(b));
}
inline Square msb(Bitboard b) {
    assert(b);
    return Square(63 - std::countl_zero(b));
}
inline Square pop_lsb(Bitboard& b) {
    Square s = lsb(b);
    b &= b - 1;
    return s;
}
constexpr bool more_than_one(Bitboard b) { return b & (b - 1); }

// Directional shifts that do not wrap around the board edge.
enum Direction : int {
    NORTH = 8, SOUTH = -8, EAST = 1, WEST = -1,
    NORTH_EAST = 9, NORTH_WEST = 7, SOUTH_EAST = -7, SOUTH_WEST = -9
};

template <Direction D>
constexpr Bitboard shift(Bitboard b) {
    if constexpr (D == NORTH) return b << 8;
    else if constexpr (D == SOUTH) return b >> 8;
    else if constexpr (D == EAST) return (b & ~FILE_H_BB) << 1;
    else if constexpr (D == WEST) return (b & ~FILE_A_BB) >> 1;
    else if constexpr (D == NORTH_EAST) return (b & ~FILE_H_BB) << 9;
    else if constexpr (D == NORTH_WEST) return (b & ~FILE_A_BB) << 7;
    else if constexpr (D == SOUTH_EAST) return (b & ~FILE_H_BB) >> 7;
    else return (b & ~FILE_A_BB) >> 9;
}

template <Color C>
constexpr Bitboard pawn_attacks_bb(Bitboard pawns) {
    if constexpr (C == WHITE) return shift<NORTH_WEST>(pawns) | shift<NORTH_EAST>(pawns);
    else return shift<SOUTH_WEST>(pawns) | shift<SOUTH_EAST>(pawns);
}

std::string bitboard_to_string(Bitboard b);

}  // namespace endgame
