/*
 * Common utilities for data structure benchmarks.
 *
 * Provides cache flushing, score encoding, and standard parameter sets
 * to ensure consistent, comparable benchmarks across implementations.
 */

#ifndef BENCH_COMMON_H
#define BENCH_COMMON_H

#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

/* ============ Parameter Sets ============
 *
 * Args format: (item_count, item_size)
 *
 * Item counts: logarithmic scale from listpack threshold to large sets
 *   128    - listpack->skiplist/fbtree transition point
 *   1K     - small set
 *   8K     - medium set
 *   64K    - large set
 *   512K   - very large set
 *   5M     - stress test
 *
 * Item sizes: 8B score + element
 *   16B - minimal (8B score + 8B id)
 *   24B - typical (8B score + 16B id)
 *   64B - large (8B score + composite key/metadata)
 */

/* Standard parameter sweep for lookup/query benchmarks */
#define BENCH_ARGS_STANDARD() \
    Args({1024, 24})          \
        ->Args({8192, 24})    \
        ->Args({65536, 24})   \
        ->Args({524288, 24})  \
        ->Args({4194304, 24})

/* Extended sweep including item size variations */
#define BENCH_ARGS_EXTENDED() \
    Args({1024, 24})          \
        ->Args({8192, 24})    \
        ->Args({65536, 24})   \
        ->Args({524288, 24})  \
        ->Args({4194304, 24})

/* ============ Cold-Cache Constants ============ */

/* Multiplier for total dataset size relative to LLC.
 * 2x guarantees eviction; increase if seeing unexpected cache hits. */
static constexpr size_t kColdCacheLLCMultiplier = 2;

/* ============ Cache Info ============ */

static size_t g_llc_size = 0;

inline size_t getLLCSize() {
    if (g_llc_size == 0) {
        int llc_level = 0;
        for (const auto &cache : benchmark::CPUInfo::Get().caches) {
            if (cache.level > llc_level) {
                llc_level = cache.level;
                g_llc_size = cache.size;
            }
        }
        if (g_llc_size == 0) g_llc_size = 32 * 1024 * 1024;
        fprintf(stderr, "LLC: L%d=%zuMB\n", llc_level, g_llc_size / (1024 * 1024));
    }
    return g_llc_size;
}

/* Calculate number of structures needed for cold-cache benchmarks */
inline size_t calcColdCacheStructureCount(size_t structure_size_bytes) {
    size_t target = getLLCSize() * kColdCacheLLCMultiplier;
    size_t count = (target + structure_size_bytes - 1) / structure_size_bytes;
    return count < 2 ? 2 : count;
}

/* ============ Score Encoding ============
 *
 * Encode double as 8 big-endian bytes that sort correctly via memcmp.
 * Used by fbtree which stores score+element as a single comparable string.
 */

inline void encodeScoreToBytes(double score, unsigned char out[8]) {
    uint64_t bits;
    memcpy(&bits, &score, sizeof(bits));
    if (bits & (1ULL << 63))
        bits = ~bits;
    else
        bits ^= (1ULL << 63);
    for (int i = 0; i < 8; i++)
        out[7 - i] = (bits >> (i * 8)) & 0xFF;
}

#endif /* BENCH_COMMON_H */
