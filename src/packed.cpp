#include "packed.h"

#include <algorithm>
#include <cstring>

namespace endgame {

std::uint64_t fnv1a64(const void* data, std::size_t len, std::uint64_t h) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

PackedPosition PackedPosition::pack(const Position& pos, int white_score, int white_result) {
    PackedPosition p{};
    p.occ = pos.pieces();
    int i = 0;
    for (Bitboard b = p.occ; b; ++i) {
        Square s = pop_lsb(b);
        std::uint8_t code = std::uint8_t(pos.piece_on(s));
        p.pieces[i / 2] |= std::uint8_t(i % 2 ? code << 4 : code);
    }
    p.score = std::int16_t(std::clamp(white_score, -32000, 32000));
    p.result = std::uint8_t(std::clamp(white_result, 0, 2));
    p.stm = std::uint8_t(pos.side_to_move());
    p.ep = std::uint8_t(pos.ep_square());
    p.castling = std::uint8_t(pos.castling_rights());
    p.rule50 = std::uint8_t(std::min(pos.rule50(), 255));
    return p;
}

std::string PackedPosition::to_fen() const {
    static constexpr const char* CHARS = "PNBRQKpnbrqk";
    char board[64];
    std::memset(board, 0, sizeof(board));
    int i = 0;
    for (Bitboard b = occ; b; ++i) {
        Square s = pop_lsb(b);
        int code = (pieces[i / 2] >> (i % 2 ? 4 : 0)) & 15;
        board[s] = code < 12 ? CHARS[code] : '?';
    }
    std::string fen;
    for (int r = 7; r >= 0; --r) {
        int run = 0;
        for (int f = 0; f < 8; ++f) {
            char c = board[r * 8 + f];
            if (!c) { ++run; continue; }
            if (run) fen += char('0' + run), run = 0;
            fen += c;
        }
        if (run) fen += char('0' + run);
        if (r) fen += '/';
    }
    fen += stm ? " b " : " w ";
    std::string cr;
    if (castling & WHITE_OO) cr += 'K';
    if (castling & WHITE_OOO) cr += 'Q';
    if (castling & BLACK_OO) cr += 'k';
    if (castling & BLACK_OOO) cr += 'q';
    fen += cr.empty() ? "-" : cr;
    fen += ' ' + square_to_string(ep < 64 ? Square(ep) : SQ_NONE);
    fen += ' ' + std::to_string(rule50) + " 1";
    return fen;
}

ShardWriter::ShardWriter(const std::string& path) {
    file_ = std::fopen(path.c_str(), "wb");
    if (!file_) return;
    ShardHeader h{};
    std::memcpy(h.magic, SHARD_MAGIC, 8);
    std::fwrite(&h, sizeof(h), 1, file_);
}

ShardWriter::~ShardWriter() {
    if (file_) std::fclose(file_);
}

void ShardWriter::write(const PackedPosition* records, std::size_t n) {
    if (!file_ || !n) return;
    std::fseek(file_, 0, SEEK_END);
    std::fwrite(records, sizeof(PackedPosition), n, file_);
    count_ += n;
    checksum_ = fnv1a64(records, n * sizeof(PackedPosition), checksum_);
    ShardHeader h{};
    std::memcpy(h.magic, SHARD_MAGIC, 8);
    h.count = count_;
    h.checksum = checksum_;
    std::fflush(file_);  // records first, then the header that covers them
    std::fseek(file_, 0, SEEK_SET);
    std::fwrite(&h, sizeof(h), 1, file_);
    std::fflush(file_);
}

bool read_shard(const std::string& path, std::vector<PackedPosition>& out, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return fail("cannot open");
    ShardHeader h{};
    if (std::fread(&h, sizeof(h), 1, f) != 1 || std::memcmp(h.magic, SHARD_MAGIC, 8) != 0) {
        std::fclose(f);
        return fail("bad magic");
    }
    std::size_t start = out.size();
    out.resize(start + h.count);
    std::size_t got = std::fread(out.data() + start, sizeof(PackedPosition), h.count, f);
    std::fclose(f);
    if (got != h.count) {
        out.resize(start);
        return fail("short file");
    }
    if (fnv1a64(out.data() + start, h.count * sizeof(PackedPosition)) != h.checksum) {
        out.resize(start);
        return fail("checksum mismatch");
    }
    return true;
}

}  // namespace endgame
