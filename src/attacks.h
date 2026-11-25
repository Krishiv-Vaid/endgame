#pragma once

#include "bitboard.h"

#if defined(USE_PEXT)
#include <immintrin.h>
#endif

namespace endgame {

// Slider attacks via "fancy" magic bitboards: one shared table, a magic
// multiplier per square found at startup with a fixed-seed PRNG (so start-up
// is deterministic). With USE_PEXT the index is _pext_u64 instead.
struct Magic {
    Bitboard mask;
    Bitboard magic;
    Bitboard* attacks;
    unsigned shift;

    unsigned index(Bitboard occ) const {
#if defined(USE_PEXT)
        return unsigned(_pext_u64(occ, mask));
#else
        return unsigned(((occ & mask) * magic) >> shift);
#endif
    }
};

namespace attacks {

extern Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
extern Bitboard KnightAttacks[SQUARE_NB];
extern Bitboard KingAttacks[SQUARE_NB];
extern Magic BishopMagics[SQUARE_NB];
extern Magic RookMagics[SQUARE_NB];
// Squares strictly between two aligned squares (empty if not aligned).
extern Bitboard Between[SQUARE_NB][SQUARE_NB];
// Full line through two aligned squares, edge to edge (empty if not aligned).
extern Bitboard Line[SQUARE_NB][SQUARE_NB];

// Must be called once before anything else uses attack tables.
void init();

// Slow ray-walking reference used to build the magic tables and in tests.
Bitboard sliding_attacks_slow(PieceType pt, Square s, Bitboard occ);

}  // namespace attacks

inline Bitboard pawn_attacks(Color c, Square s) { return attacks::PawnAttacks[c][s]; }
inline Bitboard knight_attacks(Square s) { return attacks::KnightAttacks[s]; }
inline Bitboard king_attacks(Square s) { return attacks::KingAttacks[s]; }

inline Bitboard bishop_attacks(Square s, Bitboard occ) {
    const Magic& m = attacks::BishopMagics[s];
    return m.attacks[m.index(occ)];
}
inline Bitboard rook_attacks(Square s, Bitboard occ) {
    const Magic& m = attacks::RookMagics[s];
    return m.attacks[m.index(occ)];
}
inline Bitboard queen_attacks(Square s, Bitboard occ) {
    return bishop_attacks(s, occ) | rook_attacks(s, occ);
}

inline Bitboard attacks_bb(PieceType pt, Square s, Bitboard occ) {
    switch (pt) {
        case KNIGHT: return knight_attacks(s);
        case BISHOP: return bishop_attacks(s, occ);
        case ROOK: return rook_attacks(s, occ);
        case QUEEN: return queen_attacks(s, occ);
        case KING: return king_attacks(s);
        default: return 0;
    }
}

inline Bitboard between_bb(Square a, Square b) { return attacks::Between[a][b]; }
inline Bitboard line_bb(Square a, Square b) { return attacks::Line[a][b]; }
inline bool aligned(Square a, Square b, Square c) { return line_bb(a, b) & square_bb(c); }

}  // namespace endgame
