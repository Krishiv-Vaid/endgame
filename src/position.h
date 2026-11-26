#pragma once

#include <string>
#include <vector>

#include "attacks.h"
#include "types.h"

namespace endgame {

namespace zobrist {
extern Key psq[PIECE_NB][SQUARE_NB];
extern Key castling[16];
extern Key en_passant[8];
extern Key side;
void init();
}  // namespace zobrist

// Everything that make_move() changes and unmake_move() must restore, plus
// derived data (checkers, pins) computed once per position.
struct StateInfo {
    Key key;
    Key pawn_key;
    Key non_pawn_key[COLOR_NB];
    int castling;
    Square ep_square;
    int rule50;
    int plies_from_null;
    Piece captured;
    Move move;  // move that led to this state (none at the root)

    Bitboard checkers;
    // blockers[c]: pieces of either colour that shield c's king from an enemy
    // slider. Our own blockers are pinned.
    Bitboard blockers[COLOR_NB];
};

class Position {
public:
    static constexpr const char* START_FEN =
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    Position() { set(START_FEN); }
    explicit Position(const std::string& fen, bool chess960 = false) { set(fen, chess960); }

    // Returns false (and leaves the position unchanged) on malformed FEN.
    bool set(const std::string& fen, bool chess960 = false);
    std::string fen() const;
    std::string pretty() const;

    // --- board queries ---------------------------------------------------
    Piece piece_on(Square s) const { return board_[s]; }
    bool empty(Square s) const { return board_[s] == NO_PIECE; }
    Bitboard pieces(Piece p) const { return piece_bb_[p]; }
    Bitboard pieces(Color c, PieceType pt) const { return piece_bb_[make_piece(c, pt)]; }
    Bitboard pieces(PieceType pt) const {
        return piece_bb_[make_piece(WHITE, pt)] | piece_bb_[make_piece(BLACK, pt)];
    }
    Bitboard pieces(PieceType a, PieceType b) const { return pieces(a) | pieces(b); }
    Bitboard pieces(Color c) const { return color_bb_[c]; }
    Bitboard pieces() const { return color_bb_[WHITE] | color_bb_[BLACK]; }
    Square king_square(Color c) const { return lsb(pieces(c, KING)); }

    Color side_to_move() const { return stm_; }
    int castling_rights() const { return st().castling; }
    bool can_castle(CastlingRight cr) const { return st().castling & cr; }
    Square castling_rook(CastlingRight cr) const { return castling_rook_[castling_index(cr)]; }
    Square ep_square() const { return st().ep_square; }
    int rule50() const { return st().rule50; }
    int game_ply() const { return game_ply_; }
    int fullmove_number() const { return 1 + game_ply_ / 2; }
    bool chess960() const { return chess960_; }

    Key key() const { return st().key; }
    Key pawn_key() const { return st().pawn_key; }
    Key non_pawn_key(Color c) const { return st().non_pawn_key[c]; }

    Bitboard checkers() const { return st().checkers; }
    bool in_check() const { return st().checkers; }
    Bitboard blockers(Color c) const { return st().blockers[c]; }
    Bitboard pinned() const { return st().blockers[stm_] & pieces(stm_); }
    Piece captured_piece() const { return st().captured; }
    Move last_move() const { return st().move; }

    Bitboard attackers_to(Square s, Bitboard occ) const;
    Bitboard attackers_to(Square s) const { return attackers_to(s, pieces()); }
    bool attacked_by(Color c, Square s, Bitboard occ) const;

    // Castling path data, valid for rights present in the starting FEN.
    Bitboard castling_path(CastlingRight cr) const { return castling_path_[castling_index(cr)]; }
    Bitboard castling_king_path(CastlingRight cr) const {
        return castling_king_path_[castling_index(cr)];
    }

    // --- moves ------------------------------------------------------------
    void make_move(Move m);
    void unmake_move();
    void make_null_move();
    void unmake_null_move();

    // Key after m, cheap approximation used only for TT prefetching.
    Key key_after(Move m) const;

    bool is_pseudo_legal(Move m) const;  // full check against the legal move list
    bool gives_check(Move m) const;
    // Static exchange evaluation: does m win at least `threshold` centipawns
    // once all captures on its target square are played out?
    bool see_ge(Move m, int threshold) const;

    // Draw by 50-move rule or repetition (any earlier repetition since the
    // last irreversible move counts once `ply` > 0 into the search; at the
    // root a threefold is required).
    bool is_repetition(int ply) const;
    bool is_draw(int ply) const;
    bool has_insufficient_material() const;

    // UCI move text <-> Move. Castling prints as king-to-g/c unless chess960.
    std::string move_to_uci(Move m) const;
    Move uci_to_move(const std::string& s) const;  // Move::none() if illegal

    // Self-consistency check of every incremental field; used by tests.
    bool is_consistent(std::string* why = nullptr) const;
    Key compute_key() const;
    Key compute_pawn_key() const;
    Key compute_non_pawn_key(Color c) const;

private:
    struct NoInit {};
    explicit Position(NoInit) { clear(); }
    bool parse(const std::string& fen, bool chess960);
    void clear();
    void put_piece(Piece p, Square s);
    void remove_piece(Square s);
    void move_piece(Square from, Square to);
    void add_castling_right(Color c, Square rook_sq);
    void update_check_info();
    Bitboard slider_blockers(Bitboard sliders, Square s) const;

    StateInfo& st() { return states_.back(); }
    const StateInfo& st() const { return states_.back(); }

    Bitboard piece_bb_[PIECE_NB];
    Bitboard color_bb_[COLOR_NB];
    Piece board_[SQUARE_NB];
    Color stm_;
    int game_ply_;
    bool chess960_;

    int castling_mask_[SQUARE_NB];  // rights kept when a piece leaves/enters s
    Square castling_rook_[4];
    Bitboard castling_path_[4];       // must be empty (excluding king and rook)
    Bitboard castling_king_path_[4];  // must not be attacked

    std::vector<StateInfo> states_;
};

}  // namespace endgame
