#include "position.h"

#include <cctype>
#include <cstring>
#include <sstream>

#include "movegen.h"

namespace endgame {

namespace zobrist {

Key psq[PIECE_NB][SQUARE_NB];
Key castling[16];
Key en_passant[8];
Key side;

void init() {
    // SplitMix64 with a fixed seed: keys are identical across runs and builds.
    std::uint64_t s = 0x656E6467616D6521ULL;  // "endgame!"
    auto next = [&s]() {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    };
    for (auto& piece : psq)
        for (Key& k : piece) k = next();
    // Castling keys are XOR-combinations of the four single rights so any
    // subset is consistent with toggling rights one at a time.
    Key single[4];
    for (Key& k : single) k = next();
    for (int cr = 0; cr < 16; ++cr) {
        castling[cr] = 0;
        for (int i = 0; i < 4; ++i)
            if (cr & (1 << i)) castling[cr] ^= single[i];
    }
    for (Key& k : en_passant) k = next();
    side = next();
}

}  // namespace zobrist

namespace {

constexpr const char* PIECE_CHARS = "PNBRQKpnbrqk";

Piece char_to_piece(char c) {
    const char* p = std::strchr(PIECE_CHARS, c);
    return p && c ? Piece(p - PIECE_CHARS) : NO_PIECE;
}

}  // namespace

// ---------------------------------------------------------------------------
// Low-level board edits (no hashing; callers update keys).
// ---------------------------------------------------------------------------
inline void Position::put_piece(Piece p, Square s) {
    board_[s] = p;
    piece_bb_[p] |= square_bb(s);
    color_bb_[color_of(p)] |= square_bb(s);
}

inline void Position::remove_piece(Square s) {
    Piece p = board_[s];
    piece_bb_[p] ^= square_bb(s);
    color_bb_[color_of(p)] ^= square_bb(s);
    board_[s] = NO_PIECE;
}

inline void Position::move_piece(Square from, Square to) {
    Piece p = board_[from];
    Bitboard ft = square_bb(from) | square_bb(to);
    piece_bb_[p] ^= ft;
    color_bb_[color_of(p)] ^= ft;
    board_[from] = NO_PIECE;
    board_[to] = p;
}

void Position::clear() {
    std::memset(piece_bb_, 0, sizeof(piece_bb_));
    std::memset(color_bb_, 0, sizeof(color_bb_));
    for (Piece& p : board_) p = NO_PIECE;
    for (int& m : castling_mask_) m = ALL_CASTLING;
    for (Square& s : castling_rook_) s = SQ_NONE;
    std::memset(castling_path_, 0, sizeof(castling_path_));
    std::memset(castling_king_path_, 0, sizeof(castling_king_path_));
    stm_ = WHITE;
    game_ply_ = 0;
    chess960_ = false;
    states_.clear();
    states_.reserve(1024);
    states_.emplace_back();
    StateInfo& s = states_.back();
    std::memset(&s, 0, sizeof(s));
    s.ep_square = SQ_NONE;
    s.captured = NO_PIECE;
}

void Position::add_castling_right(Color c, Square rook_sq) {
    Square ksq = king_square(c);
    bool kingside = rook_sq > ksq;
    CastlingRight cr = CastlingRight((c == WHITE ? WHITE_OO : BLACK_OO) << (kingside ? 0 : 1));
    int idx = castling_index(cr);
    st().castling |= cr;
    castling_mask_[ksq] &= ~(c == WHITE ? (WHITE_OO | WHITE_OOO) : (BLACK_OO | BLACK_OOO));
    castling_mask_[rook_sq] &= ~cr;
    castling_rook_[idx] = rook_sq;

    Square kto = relative_square(c, kingside ? SQ_G1 : SQ_C1);
    Square rto = relative_square(c, kingside ? SQ_F1 : SQ_D1);
    castling_path_[idx] = (between_bb(rook_sq, rto) | between_bb(ksq, kto) | square_bb(rto) |
                           square_bb(kto)) &
                          ~(square_bb(ksq) | square_bb(rook_sq));
    castling_king_path_[idx] = between_bb(ksq, kto) | square_bb(kto);
}

bool Position::set(const std::string& fen, bool chess960) {
    Position p{NoInit{}};
    if (!p.parse(fen, chess960)) return false;
    *this = std::move(p);
    return true;
}

bool Position::parse(const std::string& fen, bool chess960) {
    auto fail = []() { return false; };

    clear();
    chess960_ = chess960;
    std::istringstream is(fen);
    std::string board, side, castle, ep;
    int rule50 = 0, fullmove = 1;
    if (!(is >> board >> side)) return fail();
    if (!(is >> castle)) castle = "-";
    if (!(is >> ep)) ep = "-";
    if (!(is >> rule50)) rule50 = 0;
    if (!(is >> fullmove)) fullmove = 1;

    int rank = 7, file = 0;
    for (char c : board) {
        if (c == '/') {
            if (file != 8) return fail();
            --rank;
            file = 0;
            if (rank < 0) return fail();
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) return fail();
        } else {
            Piece p = char_to_piece(c);
            if (p == NO_PIECE || file > 7) return fail();
            put_piece(p, make_square(file, rank));
            ++file;
        }
    }
    if (rank != 0 || file != 8) return fail();
    if (popcount(pieces(WHITE, KING)) != 1 || popcount(pieces(BLACK, KING)) != 1) return fail();
    if (pieces(PAWN) & (RANK_1_BB | RANK_8_BB)) return fail();

    if (side == "w") stm_ = WHITE;
    else if (side == "b") stm_ = BLACK;
    else return fail();

    if (castle != "-") {
        for (char c : castle) {
            Color col = std::isupper(static_cast<unsigned char>(c)) ? WHITE : BLACK;
            char lc = char(std::tolower(static_cast<unsigned char>(c)));
            Piece rook = make_piece(col, ROOK);
            Square ksq = king_square(col);
            if (relative_rank(col, ksq) != 0) return fail();
            Square rsq = SQ_NONE;
            if (lc == 'k') {
                for (Square s = relative_square(col, SQ_H1); s > ksq; s = s - 1)
                    if (board_[s] == rook) { rsq = s; break; }
            } else if (lc == 'q') {
                for (Square s = relative_square(col, SQ_A1); s < ksq; s = s + 1)
                    if (board_[s] == rook) { rsq = s; break; }
            } else if (lc >= 'a' && lc <= 'h') {
                rsq = make_square(lc - 'a', col == WHITE ? 0 : 7);
                chess960_ = true;
                if (board_[rsq] != rook) return fail();
            } else {
                return fail();
            }
            if (rsq == SQ_NONE) return fail();
            add_castling_right(col, rsq);
        }
    }
    // A non-standard king/rook placement implies Chess960.
    for (int i = 0; i < 4; ++i) {
        if (castling_rook_[i] == SQ_NONE) continue;
        Color col = i < 2 ? WHITE : BLACK;
        if (king_square(col) != relative_square(col, SQ_E1) ||
            (file_of(castling_rook_[i]) != 0 && file_of(castling_rook_[i]) != 7))
            chess960_ = true;
    }

    StateInfo& s = st();
    s.ep_square = SQ_NONE;
    if (ep != "-") {
        if (ep.size() != 2 || ep[0] < 'a' || ep[0] > 'h' || (ep[1] != '3' && ep[1] != '6'))
            return fail();
        Square eps = make_square(ep[0] - 'a', ep[1] - '1');
        // Keep the square only if an en-passant capture is actually possible
        // pawn-wise, so equal positions hash equally.
        if (relative_rank(stm_, eps) == 5 &&
            (pawn_attacks(~stm_, eps) & pieces(stm_, PAWN)) &&
            (pieces(~stm_, PAWN) & square_bb(eps - pawn_push(stm_))))
            s.ep_square = eps;
    }
    s.rule50 = std::max(0, rule50);
    s.plies_from_null = s.rule50;
    game_ply_ = std::max(0, 2 * (fullmove - 1)) + (stm_ == BLACK);

    s.key = compute_key();
    s.pawn_key = compute_pawn_key();
    s.non_pawn_key[WHITE] = compute_non_pawn_key(WHITE);
    s.non_pawn_key[BLACK] = compute_non_pawn_key(BLACK);
    update_check_info();

    // The side that just moved must not be in check.
    if (attackers_to(king_square(~stm_)) & pieces(stm_)) return fail();
    return true;
}

std::string Position::fen() const {
    std::string out;
    for (int r = 7; r >= 0; --r) {
        int empty_run = 0;
        for (int f = 0; f < 8; ++f) {
            Piece p = board_[make_square(f, r)];
            if (p == NO_PIECE) {
                ++empty_run;
                continue;
            }
            if (empty_run) out += char('0' + empty_run), empty_run = 0;
            out += PIECE_CHARS[p];
        }
        if (empty_run) out += char('0' + empty_run);
        if (r) out += '/';
    }
    out += stm_ == WHITE ? " w " : " b ";

    std::string cr;
    static constexpr CastlingRight order[4] = {WHITE_OO, WHITE_OOO, BLACK_OO, BLACK_OOO};
    for (CastlingRight right : order) {
        if (!can_castle(right)) continue;
        Square rsq = castling_rook(right);
        char c;
        if (chess960_) c = char('a' + file_of(rsq));
        else c = (right == WHITE_OO || right == BLACK_OO) ? 'k' : 'q';
        if (right == WHITE_OO || right == WHITE_OOO) c = char(std::toupper(c));
        cr += c;
    }
    out += cr.empty() ? "-" : cr;
    out += ' ';
    out += square_to_string(st().ep_square);
    out += ' ' + std::to_string(st().rule50) + ' ' + std::to_string(fullmove_number());
    return out;
}

std::string Position::pretty() const {
    std::ostringstream os;
    os << "\n +---+---+---+---+---+---+---+---+\n";
    for (int r = 7; r >= 0; --r) {
        for (int f = 0; f < 8; ++f) {
            Piece p = board_[make_square(f, r)];
            os << " | " << (p == NO_PIECE ? ' ' : PIECE_CHARS[p]);
        }
        os << " | " << (r + 1) << "\n +---+---+---+---+---+---+---+---+\n";
    }
    os << "   a   b   c   d   e   f   g   h\n\n";
    os << "Fen: " << fen() << "\nKey: " << std::hex << std::uppercase << key() << std::dec
       << "\nCheckers:";
    for (Bitboard b = checkers(); b;) os << ' ' << square_to_string(pop_lsb(b));
    os << '\n';
    return os.str();
}

// ---------------------------------------------------------------------------
// Attacks, pins, checks
// ---------------------------------------------------------------------------
Bitboard Position::attackers_to(Square s, Bitboard occ) const {
    return (pawn_attacks(BLACK, s) & pieces(WHITE, PAWN)) |
           (pawn_attacks(WHITE, s) & pieces(BLACK, PAWN)) |
           (knight_attacks(s) & pieces(KNIGHT)) | (king_attacks(s) & pieces(KING)) |
           (bishop_attacks(s, occ) & pieces(BISHOP, QUEEN)) |
           (rook_attacks(s, occ) & pieces(ROOK, QUEEN));
}

bool Position::attacked_by(Color c, Square s, Bitboard occ) const {
    return (pawn_attacks(~c, s) & pieces(c, PAWN)) || (knight_attacks(s) & pieces(c, KNIGHT)) ||
           (king_attacks(s) & pieces(c, KING)) ||
           (bishop_attacks(s, occ) & (pieces(c, BISHOP) | pieces(c, QUEEN))) ||
           (rook_attacks(s, occ) & (pieces(c, ROOK) | pieces(c, QUEEN)));
}

Bitboard Position::slider_blockers(Bitboard sliders, Square s) const {
    Bitboard result = 0;
    Bitboard snipers = ((rook_attacks(s, 0) & pieces(ROOK, QUEEN)) |
                        (bishop_attacks(s, 0) & pieces(BISHOP, QUEEN))) &
                       sliders;
    Bitboard occ = pieces() ^ snipers;
    while (snipers) {
        Square sn = pop_lsb(snipers);
        Bitboard b = between_bb(s, sn) & occ;
        if (b && !more_than_one(b)) result |= b;
    }
    return result;
}

void Position::update_check_info() {
    StateInfo& s = st();
    s.checkers = attackers_to(king_square(stm_)) & pieces(~stm_);
    s.blockers[WHITE] = slider_blockers(pieces(BLACK), king_square(WHITE));
    s.blockers[BLACK] = slider_blockers(pieces(WHITE), king_square(BLACK));
}

// ---------------------------------------------------------------------------
// Make / unmake
// ---------------------------------------------------------------------------
void Position::make_move(Move m) {
    StateInfo prev = st();
    states_.push_back(prev);
    StateInfo& s = st();

    const Color us = stm_, them = ~us;
    const Square from = m.from(), to = m.to();
    const Piece pc = board_[from];
    Key k = prev.key ^ zobrist::side;

    s.move = m;
    s.captured = NO_PIECE;
    s.rule50 = prev.rule50 + 1;
    s.plies_from_null = prev.plies_from_null + 1;
    if (prev.ep_square != SQ_NONE) {
        k ^= zobrist::en_passant[file_of(prev.ep_square)];
        s.ep_square = SQ_NONE;
    }

    if (m.is_castle()) {
        bool kingside = to > from;
        Square kto = relative_square(us, kingside ? SQ_G1 : SQ_C1);
        Square rto = relative_square(us, kingside ? SQ_F1 : SQ_D1);
        Piece king = make_piece(us, KING), rook = make_piece(us, ROOK);
        remove_piece(from);
        remove_piece(to);
        put_piece(king, kto);
        put_piece(rook, rto);
        Key delta = zobrist::psq[king][from] ^ zobrist::psq[king][kto] ^ zobrist::psq[rook][to] ^
                    zobrist::psq[rook][rto];
        k ^= delta;
        s.non_pawn_key[us] ^= delta;
    } else {
        if (m.is_capture()) {
            Square capsq = m.is_en_passant() ? to - pawn_push(us) : to;
            Piece cap = board_[capsq];
            s.captured = cap;
            remove_piece(capsq);
            k ^= zobrist::psq[cap][capsq];
            if (type_of(cap) == PAWN) s.pawn_key ^= zobrist::psq[cap][capsq];
            else s.non_pawn_key[them] ^= zobrist::psq[cap][capsq];
            s.rule50 = 0;
        }
        move_piece(from, to);
        Key delta = zobrist::psq[pc][from] ^ zobrist::psq[pc][to];
        k ^= delta;
        if (type_of(pc) == PAWN) {
            s.rule50 = 0;
            s.pawn_key ^= delta;
            if (m.is_double_push()) {
                Square eps = from + pawn_push(us);
                if (pawn_attacks(us, eps) & pieces(them, PAWN)) {
                    s.ep_square = eps;
                    k ^= zobrist::en_passant[file_of(eps)];
                }
            } else if (m.is_promotion()) {
                Piece promo = make_piece(us, m.promotion_type());
                remove_piece(to);
                put_piece(promo, to);
                k ^= zobrist::psq[pc][to] ^ zobrist::psq[promo][to];
                s.pawn_key ^= zobrist::psq[pc][to];
                s.non_pawn_key[us] ^= zobrist::psq[promo][to];
            }
        } else {
            s.non_pawn_key[us] ^= delta;
        }
    }

    int keep = castling_mask_[from] & castling_mask_[to];
    if (s.castling && keep != ALL_CASTLING) {
        k ^= zobrist::castling[s.castling];
        s.castling &= keep;
        k ^= zobrist::castling[s.castling];
    }

    s.key = k;
    stm_ = them;
    ++game_ply_;
    update_check_info();
}

void Position::unmake_move() {
    const StateInfo& s = st();
    const Move m = s.move;
    const Color us = ~stm_;
    const Square from = m.from(), to = m.to();

    if (m.is_castle()) {
        bool kingside = to > from;
        Square kto = relative_square(us, kingside ? SQ_G1 : SQ_C1);
        Square rto = relative_square(us, kingside ? SQ_F1 : SQ_D1);
        remove_piece(kto);
        remove_piece(rto);
        put_piece(make_piece(us, KING), from);
        put_piece(make_piece(us, ROOK), to);
    } else {
        if (m.is_promotion()) {
            remove_piece(to);
            put_piece(make_piece(us, PAWN), to);
        }
        move_piece(to, from);
        if (s.captured != NO_PIECE) {
            Square capsq = m.is_en_passant() ? to - pawn_push(us) : to;
            put_piece(s.captured, capsq);
        }
    }

    stm_ = us;
    --game_ply_;
    states_.pop_back();
}

void Position::make_null_move() {
    assert(!in_check());
    StateInfo prev = st();
    states_.push_back(prev);
    StateInfo& s = st();
    s.key ^= zobrist::side;
    if (s.ep_square != SQ_NONE) {
        s.key ^= zobrist::en_passant[file_of(s.ep_square)];
        s.ep_square = SQ_NONE;
    }
    s.move = Move::null();
    s.captured = NO_PIECE;
    s.rule50 = prev.rule50 + 1;
    s.plies_from_null = 0;
    s.checkers = 0;
    stm_ = ~stm_;
    ++game_ply_;
}

void Position::unmake_null_move() {
    stm_ = ~stm_;
    --game_ply_;
    states_.pop_back();
}

Key Position::key_after(Move m) const {
    Square from = m.from(), to = m.to();
    Piece pc = board_[from];
    Key k = st().key ^ zobrist::side ^ zobrist::psq[pc][from] ^ zobrist::psq[pc][to];
    if (board_[to] != NO_PIECE && !m.is_castle()) k ^= zobrist::psq[board_[to]][to];
    return k;
}

bool Position::gives_check(Move m) const {
    const Color us = stm_, them = ~us;
    const Square from = m.from(), to = m.to();
    const Square ksq = king_square(them);
    const PieceType pt = type_of(board_[from]);

    if (!m.is_castle() && !m.is_promotion() && !m.is_en_passant()) {
        // Direct check.
        if (pt == PAWN) {
            if (pawn_attacks(us, to) & square_bb(ksq)) return true;
        } else if (pt != KING &&
                   (attacks_bb(pt, to, pieces() ^ square_bb(from)) & square_bb(ksq))) {
            return true;
        }
        // Discovered check.
        return (blockers(them) & square_bb(from)) && !aligned(from, to, ksq);
    }

    // Rare special moves: rebuild the occupancy and test every slider.
    Bitboard occ = pieces() ^ square_bb(from);
    Bitboard rq = pieces(us, ROOK) | pieces(us, QUEEN);
    Bitboard bq = pieces(us, BISHOP) | pieces(us, QUEEN);
    if (m.is_castle()) {
        bool kingside = to > from;
        Square kto = relative_square(us, kingside ? SQ_G1 : SQ_C1);
        Square rto = relative_square(us, kingside ? SQ_F1 : SQ_D1);
        occ = (occ ^ square_bb(to)) | square_bb(kto) | square_bb(rto);
        rq = (rq ^ square_bb(to)) | square_bb(rto);
        return rook_attacks(ksq, occ) & rq;
    }
    occ |= square_bb(to);
    if (m.is_en_passant()) {
        occ ^= square_bb(to - pawn_push(us));
        if (pawn_attacks(us, to) & square_bb(ksq)) return true;
    } else {  // promotion (possibly with capture)
        PieceType promo = m.promotion_type();
        if (promo == KNIGHT) {
            if (knight_attacks(to) & square_bb(ksq)) return true;
        } else {
            if (promo == ROOK || promo == QUEEN) rq |= square_bb(to);
            if (promo == BISHOP || promo == QUEEN) bq |= square_bb(to);
        }
    }
    return (rook_attacks(ksq, occ) & rq) || (bishop_attacks(ksq, occ) & bq);
}

bool Position::is_pseudo_legal(Move m) const {
    if (!m || !m.is_ok()) return false;
    MoveList list;
    generate<GEN_ALL>(*this, list);
    return list.contains(m);
}

// ---------------------------------------------------------------------------
// Draw detection
// ---------------------------------------------------------------------------
bool Position::is_repetition(int ply) const {
    const int n = int(states_.size()) - 1;
    const int limit = std::min(st().rule50, st().plies_from_null);
    if (limit < 4) return false;
    int count = 0;
    for (int i = 4; i <= limit && n - i >= 0; i += 2) {
        if (states_[n - i].key == st().key) {
            // Repetition inside the search tree: a draw already.
            if (i < ply) return true;
            if (++count == 2) return true;
        }
    }
    return false;
}

bool Position::has_insufficient_material() const {
    if (pieces(PAWN) | pieces(ROOK) | pieces(QUEEN)) return false;
    Bitboard minors = pieces(KNIGHT) | pieces(BISHOP);
    if (!more_than_one(minors)) return true;
    // Only bishops, all on one square colour.
    constexpr Bitboard DARK = 0xAA55AA55AA55AA55ULL;
    if (!pieces(KNIGHT)) {
        Bitboard b = pieces(BISHOP);
        return !(b & DARK) || !(b & ~DARK);
    }
    return false;
}

bool Position::is_draw(int ply) const {
    if (st().rule50 >= 100) {
        if (!in_check()) return true;
        MoveList list;
        generate<GEN_ALL>(*this, list);
        return list.size() > 0;  // checkmate takes precedence
    }
    return is_repetition(ply) || has_insufficient_material();
}

// ---------------------------------------------------------------------------
// UCI move text
// ---------------------------------------------------------------------------
std::string Position::move_to_uci(Move m) const {
    if (!m) return "0000";
    if (m == Move::null()) return "0000";
    Square from = m.from(), to = m.to();
    if (m.is_castle() && !chess960_) {
        Color c = color_of(board_[from]);
        to = relative_square(c, to > from ? SQ_G1 : SQ_C1);
    }
    std::string s = square_to_string(from) + square_to_string(to);
    if (m.is_promotion()) s += "nbrq"[m.promotion_type() - KNIGHT];
    return s;
}

Move Position::uci_to_move(const std::string& str) const {
    std::string s = str;
    if (s.size() == 5) s[4] = char(std::tolower(static_cast<unsigned char>(s[4])));
    MoveList list;
    generate<GEN_ALL>(*this, list);
    for (Move m : list) {
        if (move_to_uci(m) == s) return m;
        // Accept king-takes-rook castling notation in standard chess too.
        if (m.is_castle() && square_to_string(m.from()) + square_to_string(m.to()) == s) return m;
    }
    return Move::none();
}

// ---------------------------------------------------------------------------
// Consistency checking
// ---------------------------------------------------------------------------
Key Position::compute_key() const {
    Key k = 0;
    for (Square s = SQ_A1; s < SQUARE_NB; ++s)
        if (board_[s] != NO_PIECE) k ^= zobrist::psq[board_[s]][s];
    k ^= zobrist::castling[st().castling];
    if (st().ep_square != SQ_NONE) k ^= zobrist::en_passant[file_of(st().ep_square)];
    if (stm_ == BLACK) k ^= zobrist::side;
    return k;
}

Key Position::compute_pawn_key() const {
    Key k = 0;
    for (Bitboard b = pieces(PAWN); b;) {
        Square s = pop_lsb(b);
        k ^= zobrist::psq[board_[s]][s];
    }
    return k;
}

Key Position::compute_non_pawn_key(Color c) const {
    Key k = 0;
    for (Bitboard b = pieces(c) & ~pieces(PAWN); b;) {
        Square s = pop_lsb(b);
        k ^= zobrist::psq[board_[s]][s];
    }
    return k;
}

bool Position::is_consistent(std::string* why) const {
    auto bad = [&](const char* msg) {
        if (why) *why = msg;
        return false;
    };
    Bitboard all = 0;
    for (int p = 0; p < PIECE_NB; ++p) {
        if (all & piece_bb_[p]) return bad("overlapping piece bitboards");
        all |= piece_bb_[p];
    }
    if ((color_bb_[WHITE] | color_bb_[BLACK]) != all) return bad("colour occupancy mismatch");
    if (color_bb_[WHITE] & color_bb_[BLACK]) return bad("colour bitboards overlap");
    for (Square s = SQ_A1; s < SQUARE_NB; ++s) {
        Piece p = board_[s];
        if (p == NO_PIECE) {
            if (all & square_bb(s)) return bad("mailbox empty but bitboard set");
        } else if (!(piece_bb_[p] & square_bb(s))) {
            return bad("mailbox/bitboard mismatch");
        }
    }
    if (popcount(pieces(WHITE, KING)) != 1 || popcount(pieces(BLACK, KING)) != 1)
        return bad("king count");
    if (compute_key() != st().key) return bad("zobrist key");
    if (compute_pawn_key() != st().pawn_key) return bad("pawn key");
    if (compute_non_pawn_key(WHITE) != st().non_pawn_key[WHITE] ||
        compute_non_pawn_key(BLACK) != st().non_pawn_key[BLACK])
        return bad("non-pawn key");
    if ((attackers_to(king_square(stm_)) & pieces(~stm_)) != st().checkers)
        return bad("checkers");
    if (attackers_to(king_square(~stm_)) & pieces(stm_)) return bad("side not to move in check");
    return true;
}

}  // namespace endgame
