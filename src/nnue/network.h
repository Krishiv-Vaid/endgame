#pragma once

#include <cstdint>
#include <string>

#include "../position.h"
#include "arch.h"

namespace endgame::nnue {

// Quantised weights. File format (little endian):
//   magic "EGNNUE01" | arch_id u32 | hidden u32 | hash u64 (FNV-1a 64 of payload)
//   payload: ft_weights int16[INPUTS][HIDDEN] | ft_bias int16[HIDDEN]
//            out_weights int16[2][HIDDEN] (side to move first) | out_bias int32
struct Network {
    alignas(64) std::int16_t ft_weights[INPUTS * HIDDEN];
    alignas(64) std::int16_t ft_bias[HIDDEN];
    alignas(64) std::int16_t out_weights[2 * HIDDEN];
    std::int32_t out_bias;
};

struct Accumulator {
    alignas(64) std::int16_t v[COLOR_NB][HIDDEN];
};

// The loaded network, or nullptr when evaluation should use the HCE.
const Network* active();
std::uint64_t active_hash();

// Load from file / memory. On failure the previous network stays active and
// `error` says why.
bool load_file(const std::string& path, std::string* error = nullptr);
bool load_memory(const void* data, std::size_t size, std::string* error = nullptr);
void unload();

// Serialise a network (used by tests and the trainer's C++ checks).
std::string serialize(const Network& net);

// Full refresh of one or both perspectives from the board.
void refresh(const Network& net, const Position& pos, Accumulator& acc);

// Apply one move's piece changes (from StateInfo::dirty) to a copy of the
// parent accumulator.
void update(const Network& net, const Accumulator& parent, const StateInfo& st, Accumulator& out);

// Final layer: centipawns from the side to move's point of view.
int output(const Network& net, const Accumulator& acc, Color stm);

}  // namespace endgame::nnue
