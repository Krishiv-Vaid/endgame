#pragma once

// Vector kernels for the NNUE. Every path must give bit-identical results to
// the scalar reference (checked by unit tests):
//   add/sub: int16 lane-wise with wrap-around
//   screlu_dot: sum over i of clamp(a[i], 0, QA)^2 * w[i], computed as
//     (clamp(a) * w) in int16 (exact: |w| <= 128), times clamp(a) into int32
//     lane sums (each lane sums <= 64 terms, so no int32 overflow), reduced in
//     int64.

#include <cstdint>

#include "arch.h"

#if defined(__AVX512BW__)
#define ENDGAME_SIMD_AVX512 1
#include <immintrin.h>
#elif defined(__AVX2__)
#define ENDGAME_SIMD_AVX2 1
#include <immintrin.h>
#elif defined(__ARM_NEON)
#define ENDGAME_SIMD_NEON 1
#include <arm_neon.h>
#endif

namespace endgame::nnue::simd {

const char* name();

namespace scalar {
inline void add(std::int16_t* acc, const std::int16_t* w) {
    for (int i = 0; i < HIDDEN; ++i) acc[i] = std::int16_t(acc[i] + w[i]);
}
inline void sub(std::int16_t* acc, const std::int16_t* w) {
    for (int i = 0; i < HIDDEN; ++i) acc[i] = std::int16_t(acc[i] - w[i]);
}
inline std::int64_t screlu_dot(const std::int16_t* a, const std::int16_t* w) {
    std::int64_t sum = 0;
    for (int i = 0; i < HIDDEN; ++i) {
        const int c = a[i] < 0 ? 0 : a[i] > QA ? QA : a[i];
        sum += std::int64_t(std::int16_t(c * w[i])) * c;
    }
    return sum;
}
}  // namespace scalar

inline void add(std::int16_t* acc, const std::int16_t* w) {
#if defined(ENDGAME_SIMD_AVX512)
    for (int i = 0; i < HIDDEN; i += 32) {
        __m512i a = _mm512_load_si512(acc + i), b = _mm512_loadu_si512(w + i);
        _mm512_store_si512(acc + i, _mm512_add_epi16(a, b));
    }
#elif defined(ENDGAME_SIMD_AVX2)
    for (int i = 0; i < HIDDEN; i += 16) {
        __m256i a = _mm256_load_si256(reinterpret_cast<const __m256i*>(acc + i));
        __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + i));
        _mm256_store_si256(reinterpret_cast<__m256i*>(acc + i), _mm256_add_epi16(a, b));
    }
#elif defined(ENDGAME_SIMD_NEON)
    for (int i = 0; i < HIDDEN; i += 8) vst1q_s16(acc + i, vaddq_s16(vld1q_s16(acc + i), vld1q_s16(w + i)));
#else
    scalar::add(acc, w);
#endif
}

inline void sub(std::int16_t* acc, const std::int16_t* w) {
#if defined(ENDGAME_SIMD_AVX512)
    for (int i = 0; i < HIDDEN; i += 32) {
        __m512i a = _mm512_load_si512(acc + i), b = _mm512_loadu_si512(w + i);
        _mm512_store_si512(acc + i, _mm512_sub_epi16(a, b));
    }
#elif defined(ENDGAME_SIMD_AVX2)
    for (int i = 0; i < HIDDEN; i += 16) {
        __m256i a = _mm256_load_si256(reinterpret_cast<const __m256i*>(acc + i));
        __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + i));
        _mm256_store_si256(reinterpret_cast<__m256i*>(acc + i), _mm256_sub_epi16(a, b));
    }
#elif defined(ENDGAME_SIMD_NEON)
    for (int i = 0; i < HIDDEN; i += 8) vst1q_s16(acc + i, vsubq_s16(vld1q_s16(acc + i), vld1q_s16(w + i)));
#else
    scalar::sub(acc, w);
#endif
}

inline std::int64_t screlu_dot(const std::int16_t* a, const std::int16_t* w) {
#if defined(ENDGAME_SIMD_AVX512)
    const __m512i zero = _mm512_setzero_si512(), qa = _mm512_set1_epi16(QA);
    __m512i sum = _mm512_setzero_si512();
    for (int i = 0; i < HIDDEN; i += 32) {
        __m512i c = _mm512_min_epi16(_mm512_max_epi16(_mm512_load_si512(a + i), zero), qa);
        __m512i cw = _mm512_mullo_epi16(c, _mm512_loadu_si512(w + i));
        sum = _mm512_add_epi32(sum, _mm512_madd_epi16(cw, c));
    }
    alignas(64) std::int32_t lanes[16];
    _mm512_store_si512(lanes, sum);
    std::int64_t total = 0;
    for (std::int32_t v : lanes) total += v;
    return total;
#elif defined(ENDGAME_SIMD_AVX2)
    const __m256i zero = _mm256_setzero_si256(), qa = _mm256_set1_epi16(QA);
    __m256i sum = _mm256_setzero_si256();
    for (int i = 0; i < HIDDEN; i += 16) {
        __m256i x = _mm256_load_si256(reinterpret_cast<const __m256i*>(a + i));
        __m256i c = _mm256_min_epi16(_mm256_max_epi16(x, zero), qa);
        __m256i cw = _mm256_mullo_epi16(c, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + i)));
        sum = _mm256_add_epi32(sum, _mm256_madd_epi16(cw, c));
    }
    alignas(32) std::int32_t lanes[8];
    _mm256_store_si256(reinterpret_cast<__m256i*>(lanes), sum);
    std::int64_t total = 0;
    for (std::int32_t v : lanes) total += v;
    return total;
#elif defined(ENDGAME_SIMD_NEON)
    const int16x8_t zero = vdupq_n_s16(0), qa = vdupq_n_s16(QA);
    int32x4_t s0 = vdupq_n_s32(0), s1 = vdupq_n_s32(0);
    for (int i = 0; i < HIDDEN; i += 8) {
        int16x8_t c = vminq_s16(vmaxq_s16(vld1q_s16(a + i), zero), qa);
        int16x8_t cw = vmulq_s16(c, vld1q_s16(w + i));
        s0 = vmlal_s16(s0, vget_low_s16(cw), vget_low_s16(c));
        s1 = vmlal_high_s16(s1, cw, c);
    }
    return std::int64_t(vaddlvq_s32(s0)) + std::int64_t(vaddlvq_s32(s1));
#else
    return scalar::screlu_dot(a, w);
#endif
}

}  // namespace endgame::nnue::simd
