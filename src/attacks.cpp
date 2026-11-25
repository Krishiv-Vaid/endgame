#include "attacks.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace endgame {

std::string square_to_string(Square s) {
    if (s == SQ_NONE) return "-";
    return std::string{char('a' + file_of(s)), char('1' + rank_of(s))};
}

std::string bitboard_to_string(Bitboard b) {
    std::string out;
    for (int r = 7; r >= 0; --r) {
        for (int f = 0; f < 8; ++f) {
            out += (b & square_bb(make_square(f, r))) ? 'X' : '.';
            out += f == 7 ? '\n' : ' ';
        }
    }
    return out;
}

namespace attacks {

Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
Bitboard KnightAttacks[SQUARE_NB];
Bitboard KingAttacks[SQUARE_NB];
Magic BishopMagics[SQUARE_NB];
Magic RookMagics[SQUARE_NB];
Bitboard Between[SQUARE_NB][SQUARE_NB];
Bitboard Line[SQUARE_NB][SQUARE_NB];

namespace {

// 0x1480 entries for bishops and 0x19000 for rooks with fancy magics.
Bitboard BishopTable[0x1480];
Bitboard RookTable[0x19000];

// Adds square (file + df, rank + dr) to b if it is on the board.
Bitboard step(Square s, int df, int dr) {
    int f = file_of(s) + df, r = rank_of(s) + dr;
    if (f < 0 || f > 7 || r < 0 || r > 7) return 0;
    return square_bb(make_square(f, r));
}

// xorshift64* with a fixed seed: magics are identical on every run.
struct Prng {
    std::uint64_t s;
    std::uint64_t next() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 2685821657736338717ULL;
    }
    std::uint64_t sparse() { return next() & next() & next(); }
};

void init_magics(PieceType pt, Magic* magics, Bitboard* table) {
    std::vector<Bitboard> occupancy(4096), reference(4096);
    std::vector<int> epoch(4096, 0);
    int attempt = 0;
    Bitboard* next_slot = table;

    for (Square s = SQ_A1; s < SQUARE_NB; ++s) {
        Bitboard edges = ((RANK_1_BB | RANK_8_BB) & ~rank_bb(rank_of(s))) |
                         ((FILE_A_BB | FILE_H_BB) & ~file_bb(file_of(s)));
        Magic& m = magics[s];
        m.mask = sliding_attacks_slow(pt, s, 0) & ~edges;
        int bits = popcount(m.mask);
        m.shift = unsigned(64 - bits);
        m.attacks = next_slot;
        int size = 0;

        // Carry-Rippler enumeration of every subset of the mask.
        Bitboard b = 0;
        do {
            occupancy[size] = b;
            reference[size] = sliding_attacks_slow(pt, s, b);
            ++size;
            b = (b - m.mask) & m.mask;
        } while (b);
        next_slot += size;

#if defined(USE_PEXT)
        m.magic = 0;
        for (int i = 0; i < size; ++i) m.attacks[m.index(occupancy[i])] = reference[i];
#else
        Prng rng{0x9E3779B97F4A7C15ULL ^ (std::uint64_t(s) * 0x2545F4914F6CDD1DULL) ^ pt};
        for (bool found = false; !found;) {
            m.magic = rng.sparse();
            if (popcount((m.mask * m.magic) >> 56) < 6) continue;
            ++attempt;
            found = true;
            for (int i = 0; i < size; ++i) {
                unsigned idx = m.index(occupancy[i]);
                if (epoch[idx] < attempt) {
                    epoch[idx] = attempt;
                    m.attacks[idx] = reference[i];
                } else if (m.attacks[idx] != reference[i]) {
                    found = false;
                    break;
                }
            }
        }
#endif
    }
}

}  // namespace

Bitboard sliding_attacks_slow(PieceType pt, Square s, Bitboard occ) {
    static constexpr int rook_dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    static constexpr int bishop_dirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    const auto& dirs = pt == ROOK ? rook_dirs : bishop_dirs;
    Bitboard result = 0;
    for (const auto& d : dirs) {
        Square cur = s;
        while (Bitboard to = step(cur, d[0], d[1])) {
            result |= to;
            cur = lsb(to);
            if (occ & to) break;
        }
    }
    return result;
}

void init() {
    static bool done = false;
    if (done) return;
    done = true;

    for (Square s = SQ_A1; s < SQUARE_NB; ++s) {
        PawnAttacks[WHITE][s] = step(s, -1, 1) | step(s, 1, 1);
        PawnAttacks[BLACK][s] = step(s, -1, -1) | step(s, 1, -1);

        KnightAttacks[s] = 0;
        static constexpr int knight[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2},
                                             {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
        for (const auto& d : knight) KnightAttacks[s] |= step(s, d[0], d[1]);

        KingAttacks[s] = 0;
        for (int df = -1; df <= 1; ++df)
            for (int dr = -1; dr <= 1; ++dr)
                if (df || dr) KingAttacks[s] |= step(s, df, dr);
    }

    init_magics(BISHOP, BishopMagics, BishopTable);
    init_magics(ROOK, RookMagics, RookTable);

    for (Square a = SQ_A1; a < SQUARE_NB; ++a) {
        for (Square b = SQ_A1; b < SQUARE_NB; ++b) {
            Between[a][b] = Line[a][b] = 0;
            if (a == b) continue;
            for (PieceType pt : {BISHOP, ROOK}) {
                if (sliding_attacks_slow(pt, a, 0) & square_bb(b)) {
                    Line[a][b] = (sliding_attacks_slow(pt, a, 0) & sliding_attacks_slow(pt, b, 0)) |
                                 square_bb(a) | square_bb(b);
                    Between[a][b] = sliding_attacks_slow(pt, a, square_bb(b)) &
                                    sliding_attacks_slow(pt, b, square_bb(a));
                }
            }
        }
    }
}

}  // namespace attacks
}  // namespace endgame
