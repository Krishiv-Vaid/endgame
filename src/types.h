#pragma once

#include <cassert>
#include <cstdint>
#include <string>

namespace endgame {

using Bitboard = std::uint64_t;
using Key = std::uint64_t;

enum Color : int { WHITE, BLACK, COLOR_NB = 2 };

constexpr Color operator~(Color c) { return Color(c ^ 1); }

enum PieceType : int {
    PAWN,
    KNIGHT,
    BISHOP,
    ROOK,
    QUEEN,
    KING,
    PIECE_TYPE_NB = 6,
    NO_PIECE_TYPE = 6
};

// Piece = color * 6 + type, so the 12 piece bitboards index directly.
enum Piece : int {
    W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
    NO_PIECE = 12,
    PIECE_NB = 12
};

constexpr Piece make_piece(Color c, PieceType pt) { return Piece(c * 6 + pt); }
constexpr PieceType type_of(Piece p) { return PieceType(p >= 6 ? p - 6 : p); }
constexpr Color color_of(Piece p) { return Color(p >= 6); }

enum Square : int {
    SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
    SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
    SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
    SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
    SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
    SQ_NONE = 64,
    SQUARE_NB = 64
};

constexpr Square operator+(Square s, int d) { return Square(int(s) + d); }
constexpr Square operator-(Square s, int d) { return Square(int(s) - d); }
constexpr Square& operator++(Square& s) { return s = Square(int(s) + 1); }

constexpr int file_of(Square s) { return s & 7; }
constexpr int rank_of(Square s) { return s >> 3; }
constexpr Square make_square(int file, int rank) { return Square(rank * 8 + file); }
// Mirror vertically for black so "relative" ranks/squares read from the mover's side.
constexpr Square relative_square(Color c, Square s) { return Square(s ^ (c * 56)); }
constexpr int relative_rank(Color c, Square s) { return rank_of(s) ^ (c * 7); }
constexpr int pawn_push(Color c) { return c == WHITE ? 8 : -8; }

enum CastlingRight : int {
    NO_CASTLING = 0,
    WHITE_OO = 1,
    WHITE_OOO = 2,
    BLACK_OO = 4,
    BLACK_OOO = 8,
    ALL_CASTLING = 15
};

// Index 0..3 matching the bit position of a single castling right.
constexpr int castling_index(CastlingRight cr) {
    return cr == WHITE_OO ? 0 : cr == WHITE_OOO ? 1 : cr == BLACK_OO ? 2 : 3;
}

// ---------------------------------------------------------------------------
// Move: 16 bits = from (6) | to (6) << 6 | flag (4) << 12.
// Castling is encoded king-takes-own-rook (from = king, to = rook) so the same
// code handles standard chess and Chess960.
// Flag bit 2 = capture, bit 3 = promotion; low two bits of a promotion give the
// piece (N, B, R, Q).
// ---------------------------------------------------------------------------
enum MoveFlag : int {
    MF_QUIET = 0,
    MF_DOUBLE_PUSH = 1,
    MF_CASTLE = 2,
    MF_CAPTURE = 4,
    MF_EN_PASSANT = 5,
    MF_PROMO = 8,           // + 0..3 for N, B, R, Q
    MF_PROMO_CAPTURE = 12,  // + 0..3 for N, B, R, Q
};

class Move {
public:
    constexpr Move() = default;
    constexpr explicit Move(std::uint16_t raw) : data_(raw) {}
    constexpr Move(Square from, Square to, int flag = MF_QUIET)
        : data_(std::uint16_t(from | (to << 6) | (flag << 12))) {}

    static constexpr Move none() { return Move(); }
    // from == to == b1: never a real move, distinct from none().
    static constexpr Move null() { return Move(std::uint16_t(65)); }

    constexpr Square from() const { return Square(data_ & 63); }
    constexpr Square to() const { return Square((data_ >> 6) & 63); }
    constexpr int flag() const { return data_ >> 12; }
    constexpr std::uint16_t raw() const { return data_; }

    constexpr bool is_capture() const { return flag() & 4; }
    constexpr bool is_promotion() const { return flag() & 8; }
    constexpr bool is_castle() const { return flag() == MF_CASTLE; }
    constexpr bool is_en_passant() const { return flag() == MF_EN_PASSANT; }
    constexpr bool is_double_push() const { return flag() == MF_DOUBLE_PUSH; }
    constexpr PieceType promotion_type() const { return PieceType(KNIGHT + (flag() & 3)); }
    // Captures and queen promotions: the moves quiescence search looks at.
    constexpr bool is_noisy() const {
        return is_capture() || (is_promotion() && promotion_type() == QUEEN);
    }

    constexpr bool is_ok() const { return from() != to(); }
    constexpr explicit operator bool() const { return data_ != 0; }
    constexpr bool operator==(const Move&) const = default;

private:
    std::uint16_t data_ = 0;
};

constexpr int MAX_MOVES = 256;
constexpr int MAX_PLY = 246;

std::string square_to_string(Square s);

}  // namespace endgame
