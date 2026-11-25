#include "movegen.h"

namespace endgame {

namespace {

template <GenType T>
inline void add_promotions(MoveList& list, Square from, Square to, bool capture) {
    const int base = capture ? MF_PROMO_CAPTURE : MF_PROMO;
    // Queen promotions are noisy; under-promotions are noisy only as captures.
    if constexpr (T != GEN_QUIET) list.push(Move(from, to, base + (QUEEN - KNIGHT)));
    if (T == GEN_ALL || (T == GEN_NOISY && capture) || (T == GEN_QUIET && !capture)) {
        list.push(Move(from, to, base + (KNIGHT - KNIGHT)));
        list.push(Move(from, to, base + (BISHOP - KNIGHT)));
        list.push(Move(from, to, base + (ROOK - KNIGHT)));
    }
}

template <Color Us, GenType T>
void generate_pawn_moves(const Position& pos, MoveList& list, Bitboard check_mask,
                         Bitboard pinned, Square ksq) {
    constexpr Color Them = ~Us;
    constexpr Direction Up = Us == WHITE ? NORTH : SOUTH;
    constexpr Direction UpWest = Us == WHITE ? NORTH_WEST : SOUTH_WEST;
    constexpr Direction UpEast = Us == WHITE ? NORTH_EAST : SOUTH_EAST;
    constexpr Bitboard Rank7 = relative_rank_bb(Us, 6);
    constexpr Bitboard Rank3 = relative_rank_bb(Us, 2);

    const Bitboard pawns = pos.pieces(Us, PAWN);
    const Bitboard enemies = pos.pieces(Them);
    const Bitboard empty = ~pos.pieces();
    const Bitboard promoters = pawns & Rank7;
    const Bitboard others = pawns & ~Rank7;

    // A pinned pawn may only move along the line through its king.
    auto legal = [&](Square from, Square to) {
        return !(pinned & square_bb(from)) || (line_bb(ksq, from) & square_bb(to));
    };

    if constexpr (T != GEN_NOISY) {
        Bitboard single = shift<Up>(others) & empty;
        Bitboard dbl = shift<Up>(single & Rank3) & empty & check_mask;
        single &= check_mask;
        while (single) {
            Square to = pop_lsb(single);
            Square from = to - Up;
            if (legal(from, to)) list.push(Move(from, to, MF_QUIET));
        }
        while (dbl) {
            Square to = pop_lsb(dbl);
            Square from = to - 2 * Up;
            if (legal(from, to)) list.push(Move(from, to, MF_DOUBLE_PUSH));
        }
    }

    if (promoters) {
        Bitboard push = shift<Up>(promoters) & empty & check_mask;
        Bitboard west = shift<UpWest>(promoters) & enemies & check_mask;
        Bitboard east = shift<UpEast>(promoters) & enemies & check_mask;
        while (push) {
            Square to = pop_lsb(push);
            if (legal(to - Up, to)) add_promotions<T>(list, to - Up, to, false);
        }
        while (west) {
            Square to = pop_lsb(west);
            if (legal(to - UpWest, to)) add_promotions<T>(list, to - UpWest, to, true);
        }
        while (east) {
            Square to = pop_lsb(east);
            if (legal(to - UpEast, to)) add_promotions<T>(list, to - UpEast, to, true);
        }
    }

    if constexpr (T != GEN_QUIET) {
        Bitboard west = shift<UpWest>(others) & enemies & check_mask;
        Bitboard east = shift<UpEast>(others) & enemies & check_mask;
        while (west) {
            Square to = pop_lsb(west);
            if (legal(to - UpWest, to)) list.push(Move(to - UpWest, to, MF_CAPTURE));
        }
        while (east) {
            Square to = pop_lsb(east);
            if (legal(to - UpEast, to)) list.push(Move(to - UpEast, to, MF_CAPTURE));
        }

        const Square ep = pos.ep_square();
        if (ep != SQ_NONE) {
            const Square capsq = ep - Up;
            // In check, en passant must capture the checker or block the check.
            if (!pos.checkers() || (check_mask & (square_bb(ep) | square_bb(capsq)))) {
                const Bitboard rq = pos.pieces(Them, ROOK) | pos.pieces(Them, QUEEN);
                const Bitboard bq = pos.pieces(Them, BISHOP) | pos.pieces(Them, QUEEN);
                for (Bitboard b = pawn_attacks(Them, ep) & others; b;) {
                    Square from = pop_lsb(b);
                    // Two pieces leave and one arrives, so verify sliders directly
                    // (catches the horizontal pin through both pawns).
                    Bitboard occ = (pos.pieces() ^ square_bb(from) ^ square_bb(capsq)) | square_bb(ep);
                    if (!(rook_attacks(ksq, occ) & rq) && !(bishop_attacks(ksq, occ) & bq))
                        list.push(Move(from, ep, MF_EN_PASSANT));
                }
            }
        }
    }
}

template <Color Us, GenType T>
void generate_all(const Position& pos, MoveList& list) {
    constexpr Color Them = ~Us;
    const Square ksq = pos.king_square(Us);
    const Bitboard ours = pos.pieces(Us);
    const Bitboard theirs = pos.pieces(Them);
    const Bitboard occ = ours | theirs;
    const Bitboard checkers = pos.checkers();

    Bitboard targets = T == GEN_NOISY ? theirs : T == GEN_QUIET ? ~occ : ~ours;

    // King moves: test each destination with the king removed from the board
    // so it cannot hide behind itself from a slider.
    {
        const Bitboard occ_no_king = occ ^ square_bb(ksq);
        for (Bitboard b = king_attacks(ksq) & targets; b;) {
            Square to = pop_lsb(b);
            if (!pos.attacked_by(Them, to, occ_no_king))
                list.push(Move(ksq, to, (theirs & square_bb(to)) ? MF_CAPTURE : MF_QUIET));
        }
    }
    if (more_than_one(checkers)) return;  // double check: king moves only

    const Bitboard check_mask = checkers ? between_bb(ksq, lsb(checkers)) | checkers : ~Bitboard(0);
    const Bitboard pinned = pos.blockers(Us) & ours;
    targets &= check_mask;

    generate_pawn_moves<Us, T>(pos, list, check_mask, pinned, ksq);

    auto emit = [&](Square from, Bitboard to_set) {
        while (to_set) {
            Square to = pop_lsb(to_set);
            list.push(Move(from, to, (theirs & square_bb(to)) ? MF_CAPTURE : MF_QUIET));
        }
    };

    // Pinned knights can never move.
    for (Bitboard b = pos.pieces(Us, KNIGHT) & ~pinned; b;) {
        Square from = pop_lsb(b);
        emit(from, knight_attacks(from) & targets);
    }
    for (Bitboard b = pos.pieces(Us, BISHOP) | pos.pieces(Us, QUEEN); b;) {
        Square from = pop_lsb(b);
        Bitboard att = bishop_attacks(from, occ) & targets;
        if (pinned & square_bb(from)) att &= line_bb(ksq, from);
        emit(from, att);
    }
    for (Bitboard b = pos.pieces(Us, ROOK) | pos.pieces(Us, QUEEN); b;) {
        Square from = pop_lsb(b);
        Bitboard att = rook_attacks(from, occ) & targets;
        if (pinned & square_bb(from)) att &= line_bb(ksq, from);
        emit(from, att);
    }

    if constexpr (T != GEN_NOISY) {
        if (!checkers) {
            constexpr CastlingRight rights[2] = {Us == WHITE ? WHITE_OO : BLACK_OO,
                                                 Us == WHITE ? WHITE_OOO : BLACK_OOO};
            for (CastlingRight cr : rights) {
                if (!pos.can_castle(cr) || (pos.castling_path(cr) & occ)) continue;
                const Square rsq = pos.castling_rook(cr);
                // Check the king's path with king and castling rook lifted off:
                // any attack this uncovers along the rank also hits the king's
                // destination, so it is exactly the legal condition.
                const Bitboard occ2 = occ ^ square_bb(ksq) ^ square_bb(rsq);
                bool safe = true;
                for (Bitboard path = pos.castling_king_path(cr); path && safe;)
                    safe = !pos.attacked_by(Them, pop_lsb(path), occ2);
                if (safe) list.push(Move(ksq, rsq, MF_CASTLE));
            }
        }
    }
}

}  // namespace

template <GenType T>
void generate(const Position& pos, MoveList& list) {
    if (pos.side_to_move() == WHITE) generate_all<WHITE, T>(pos, list);
    else generate_all<BLACK, T>(pos, list);
}

template void generate<GEN_NOISY>(const Position&, MoveList&);
template void generate<GEN_QUIET>(const Position&, MoveList&);
template void generate<GEN_ALL>(const Position&, MoveList&);

}  // namespace endgame
