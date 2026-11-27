#include "network.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

#include "../packed.h"
#include "simd.h"

namespace endgame::nnue {

namespace {

constexpr char MAGIC[8] = {'E', 'G', 'N', 'N', 'U', 'E', '0', '1'};
constexpr std::size_t HEADER_SIZE = 8 + 4 + 4 + 8;
constexpr std::size_t PAYLOAD_SIZE =
    sizeof(std::int16_t) * (INPUTS * HIDDEN + HIDDEN + 2 * HIDDEN) + sizeof(std::int32_t);

std::unique_ptr<Network> g_net;
std::uint64_t g_hash = 0;

}  // namespace

const char* simd::name() {
#if defined(ENDGAME_SIMD_AVX512)
    return "avx512";
#elif defined(ENDGAME_SIMD_AVX2)
    return "avx2";
#elif defined(ENDGAME_SIMD_NEON)
    return "neon";
#else
    return "scalar";
#endif
}

const Network* active() { return g_net.get(); }
std::uint64_t active_hash() { return g_hash; }
void unload() {
    g_net.reset();
    g_hash = 0;
}

std::string serialize(const Network& net) {
    std::string payload;
    payload.append(reinterpret_cast<const char*>(net.ft_weights), sizeof(net.ft_weights));
    payload.append(reinterpret_cast<const char*>(net.ft_bias), sizeof(net.ft_bias));
    payload.append(reinterpret_cast<const char*>(net.out_weights), sizeof(net.out_weights));
    payload.append(reinterpret_cast<const char*>(&net.out_bias), sizeof(net.out_bias));
    std::string out(MAGIC, 8);
    std::uint32_t arch = ARCH_ID, hidden = HIDDEN;
    std::uint64_t hash = fnv1a64(payload.data(), payload.size());
    out.append(reinterpret_cast<const char*>(&arch), 4);
    out.append(reinterpret_cast<const char*>(&hidden), 4);
    out.append(reinterpret_cast<const char*>(&hash), 8);
    return out + payload;
}

bool load_memory(const void* data, std::size_t size, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    const auto* p = static_cast<const char*>(data);
    if (size != HEADER_SIZE + PAYLOAD_SIZE) return fail("wrong file size " + std::to_string(size));
    if (std::memcmp(p, MAGIC, 8) != 0) return fail("bad magic");
    std::uint32_t arch, hidden;
    std::uint64_t hash;
    std::memcpy(&arch, p + 8, 4);
    std::memcpy(&hidden, p + 12, 4);
    std::memcpy(&hash, p + 16, 8);
    if (arch != ARCH_ID || hidden != std::uint32_t(HIDDEN))
        return fail("architecture " + std::to_string(arch) + "/" + std::to_string(hidden) +
                    " does not match this build");
    const char* payload = p + HEADER_SIZE;
    if (fnv1a64(payload, PAYLOAD_SIZE) != hash) return fail("hash mismatch (corrupt file)");

    auto net = std::make_unique<Network>();
    std::size_t off = 0;
    auto take = [&](void* dst, std::size_t n) {
        std::memcpy(dst, payload + off, n);
        off += n;
    };
    take(net->ft_weights, sizeof(net->ft_weights));
    take(net->ft_bias, sizeof(net->ft_bias));
    take(net->out_weights, sizeof(net->out_weights));
    take(&net->out_bias, sizeof(net->out_bias));
    for (std::int16_t w : net->out_weights)
        if (w > MAX_OUT_WEIGHT || w < -MAX_OUT_WEIGHT) return fail("output weight out of range");
    g_net = std::move(net);
    g_hash = hash;
    return true;
}

bool load_file(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::vector<char> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return load_memory(buf.data(), buf.size(), error);
}

void refresh(const Network& net, const Position& pos, Accumulator& acc) {
    for (Color c : {WHITE, BLACK}) {
        std::memcpy(acc.v[c], net.ft_bias, sizeof(net.ft_bias));
        for (Bitboard b = pos.pieces(); b;) {
            Square s = pop_lsb(b);
            simd::add(acc.v[c], net.ft_weights + feature_index(c, pos.piece_on(s), s) * HIDDEN);
        }
    }
}

void update(const Network& net, const Accumulator& parent, const StateInfo& st, Accumulator& out) {
    for (Color c : {WHITE, BLACK}) {
        std::int16_t* v = out.v[c];
        std::memcpy(v, parent.v[c], sizeof(parent.v[c]));
        for (int i = 0; i < st.dirty_count; ++i) {
            const DirtyPiece& d = st.dirty[i];
            if (d.from != SQ_NONE) simd::sub(v, net.ft_weights + feature_index(c, d.piece, d.from) * HIDDEN);
            if (d.to != SQ_NONE) simd::add(v, net.ft_weights + feature_index(c, d.piece, d.to) * HIDDEN);
        }
    }
}

int output(const Network& net, const Accumulator& acc, Color stm) {
    std::int64_t sum = simd::screlu_dot(acc.v[stm], net.out_weights) +
                       simd::screlu_dot(acc.v[~stm], net.out_weights + HIDDEN);
    // sum is in QA*QA*QB units: one QA comes off first, then the bias (QA*QB).
    std::int64_t v = sum / QA + net.out_bias;
    return int(v * SCALE / (std::int64_t(QA) * QB));
}

}  // namespace endgame::nnue
