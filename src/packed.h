#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "position.h"

namespace endgame {

// One training position in exactly 32 bytes (endgame's own format).
//   occ       occupancy bitboard, bit 0 = a1
//   pieces    4-bit piece codes (Piece enum, 0..11) for each occupied square
//             in ascending square order; low nibble first
//   score     search score in centipawns from white's point of view
//   result    game result for white: 0 = loss, 1 = draw, 2 = win
//   stm       side to move (0 = white, 1 = black)
//   ep        en-passant square or 64
//   castling  CastlingRight bits
//   rule50    half-move clock (saturated at 255)
struct PackedPosition {
    std::uint64_t occ;
    std::uint8_t pieces[16];
    std::int16_t score;
    std::uint8_t result;
    std::uint8_t stm;
    std::uint8_t ep;
    std::uint8_t castling;
    std::uint8_t rule50;
    std::uint8_t reserved;

    static PackedPosition pack(const Position& pos, int white_score, int white_result);
    // Rebuilds piece placement, side to move, castling bits (standard rook
    // squares), ep square and rule50 as a FEN. Enough for training and tests.
    std::string to_fen() const;
};
static_assert(sizeof(PackedPosition) == 32, "PackedPosition must be 32 bytes");

// Shard file: 32-byte header followed by `count` PackedPositions.
//   magic "EGDATA01" | count u64 | checksum u64 (FNV-1a 64 over the records) | reserved u64
struct ShardHeader {
    char magic[8];
    std::uint64_t count;
    std::uint64_t checksum;
    std::uint64_t reserved;
};
static_assert(sizeof(ShardHeader) == 32, "ShardHeader must be 32 bytes");

constexpr char SHARD_MAGIC[8] = {'E', 'G', 'D', 'A', 'T', 'A', '0', '1'};

std::uint64_t fnv1a64(const void* data, std::size_t len, std::uint64_t h = 0xcbf29ce484222325ULL);

// Writes records and keeps the header's count and checksum current on every
// flush, so a killed process leaves a valid shard of everything flushed.
class ShardWriter {
public:
    explicit ShardWriter(const std::string& path);
    ~ShardWriter();
    ShardWriter(const ShardWriter&) = delete;
    ShardWriter& operator=(const ShardWriter&) = delete;
    bool ok() const { return file_ != nullptr; }
    void write(const PackedPosition* records, std::size_t n);
    std::uint64_t count() const { return count_; }

private:
    std::FILE* file_ = nullptr;
    std::uint64_t count_ = 0;
    std::uint64_t checksum_ = 0xcbf29ce484222325ULL;
};

// Reads and verifies a whole shard. Returns false on bad magic, short file or
// checksum mismatch.
bool read_shard(const std::string& path, std::vector<PackedPosition>& out, std::string* error = nullptr);

}  // namespace endgame
