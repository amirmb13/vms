// =============================================================================
// Layer 3 (CPU-only) — SIMD 3-Frame Difference Engine.
//
// Pipeline (Spatio-Temporal Downsampling):
//   * Temporal: MotionEngine feeds only 3–5 fps sampled from the sub-stream.
//   * Spatial:  frames arrive already scaled to 320x240 GRAY8 via libswscale
//               (SWS_FAST_BILINEAR, AVX2-accelerated internally).
//   * Diff:     AVX2/AVX-512 bitwise math over 3 consecutive frames:
//                 motion = (|f2-f1| AND |f1-f0|) > threshold
//   * ROI:      UI-defined bitmask grid ANDed in BEFORE threshold counting so
//               ignored regions (trees, highways) cost zero evaluation.
// =============================================================================
#include "motion/simd_frame_diff.h"

#include <immintrin.h>
#include <cstring>

namespace vms {

namespace {

// Scalar tail / fallback used for non-multiple-of-32 remainders.
inline uint32_t diff3_scalar(const uint8_t* f0, const uint8_t* f1,
                             const uint8_t* f2, const uint8_t* roi,
                             size_t n, uint8_t threshold) {
    uint32_t changed = 0;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t d10 = static_cast<uint8_t>(f1[i] > f0[i] ? f1[i] - f0[i] : f0[i] - f1[i]);
        const uint8_t d21 = static_cast<uint8_t>(f2[i] > f1[i] ? f2[i] - f1[i] : f1[i] - f2[i]);
        const uint8_t both = static_cast<uint8_t>(d10 < d21 ? d10 : d21);  // AND-like min
        changed += (roi[i] != 0) && (both > threshold);
    }
    return changed;
}

}  // namespace

uint32_t simd_three_frame_diff(const uint8_t* f0, const uint8_t* f1,
                               const uint8_t* f2, const uint8_t* roi_mask,
                               size_t pixel_count, uint8_t threshold) {
    uint32_t changed = 0;
    size_t i = 0;

#if defined(VMS_HAVE_AVX512) && defined(__AVX512BW__)
    // AVX-512BW path: 64 pixels per iteration, mask registers give us the
    // (both > threshold) AND (roi != 0) predicate for free via __mmask64.
    const __m512i vthresh512 = _mm512_set1_epi8(static_cast<char>(threshold));
    const __m512i zero512 = _mm512_setzero_si512();

    for (; i + 64 <= pixel_count; i += 64) {
        const __m512i a = _mm512_loadu_si512(f0 + i);
        const __m512i b = _mm512_loadu_si512(f1 + i);
        const __m512i c = _mm512_loadu_si512(f2 + i);
        const __m512i m = _mm512_loadu_si512(roi_mask + i);

        // |b-a| and |c-b| via unsigned saturating subtraction.
        const __m512i d10 =
            _mm512_or_si512(_mm512_subs_epu8(b, a), _mm512_subs_epu8(a, b));
        const __m512i d21 =
            _mm512_or_si512(_mm512_subs_epu8(c, b), _mm512_subs_epu8(b, c));

        // 3-frame confirmation: min == bitwise-AND-like conjunction.
        const __m512i both = _mm512_min_epu8(d10, d21);

        // Native unsigned compares straight into mask registers.
        const __mmask64 gt =
            _mm512_cmpgt_epu8_mask(both, vthresh512);       // both > threshold
        const __mmask64 roi_ok =
            _mm512_cmpneq_epu8_mask(m, zero512);            // ROI enabled

        changed += static_cast<uint32_t>(
            __builtin_popcountll(static_cast<unsigned long long>(gt & roi_ok)));
    }
#endif  // VMS_HAVE_AVX512 && __AVX512BW__

#if defined(__AVX2__)
    const __m256i vthresh = _mm256_set1_epi8(static_cast<char>(threshold));
    const __m256i zero = _mm256_setzero_si256();

    for (; i + 32 <= pixel_count; i += 32) {
        const __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(f0 + i));
        const __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(f1 + i));
        const __m256i c = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(f2 + i));
        const __m256i m = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(roi_mask + i));

        // |b-a| and |c-b| via unsigned saturating subtraction (no overflow).
        const __m256i d10 = _mm256_or_si256(_mm256_subs_epu8(b, a), _mm256_subs_epu8(a, b));
        const __m256i d21 = _mm256_or_si256(_mm256_subs_epu8(c, b), _mm256_subs_epu8(b, c));

        // 3-frame confirmation: pixel counts only if BOTH diffs exceed threshold.
        const __m256i both = _mm256_min_epu8(d10, d21);

        // (both > threshold) as 0xFF mask — unsigned compare via max trick.
        const __m256i gt = _mm256_andnot_si256(
            _mm256_cmpeq_epi8(_mm256_max_epu8(both, vthresh), vthresh),
            _mm256_cmpeq_epi8(zero, zero));

        // Apply ROI bitmask grid BEFORE counting (ignore masked-out regions).
        const __m256i roi_ok = _mm256_andnot_si256(_mm256_cmpeq_epi8(m, zero),
                                                   _mm256_cmpeq_epi8(zero, zero));
        const __m256i hit = _mm256_and_si256(gt, roi_ok);

        changed += static_cast<uint32_t>(
            __builtin_popcount(static_cast<unsigned>(_mm256_movemask_epi8(hit))));
    }
#endif  // __AVX2__

    changed += diff3_scalar(f0 + i, f1 + i, f2 + i, roi_mask + i,
                            pixel_count - i, threshold);
    return changed;
}

bool evaluate_motion(uint32_t changed_pixels, size_t pixel_count,
                     float trigger_ratio) {
    return static_cast<float>(changed_pixels) >=
           trigger_ratio * static_cast<float>(pixel_count);
}

}  // namespace vms
