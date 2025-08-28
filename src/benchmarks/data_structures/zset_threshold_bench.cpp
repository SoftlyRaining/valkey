/*
 * ZSET Threshold Performance Benchmarks (Cold Memory)
 *
 * Compares listpack vs skiplist vs fbtree performance at various item counts
 * to help determine optimal listpack->tree conversion threshold.
 *
 * Uses multiple collections to ensure cold memory access patterns.
 * Collections are populated in interleaved random order to simulate
 * realistic memory layout (not sequential RDB load).
 */

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstring>
#include <numeric>
#include <random>
#include <vector>

extern "C" {
#include "zmalloc.h"
#include "fbtree_ordered_index.h"

/* Forward declare sds functions (types come from fbtree_ordered_index.h) */
sds sdsnewlen(const void *init, size_t initlen);
void sdsfree(sds s);

/* Use opaque types and accessor functions - struct layout has changed */
typedef struct zskiplistNode zskiplistNode;
typedef struct zskiplist zskiplist;
typedef struct {
    double min, max;
    int minex, maxex;
} zrangespec;

zskiplist *zslCreate(void);
void zslFree(zskiplist *zsl);
zskiplistNode *zslInsert(zskiplist *zsl, double score, const_sds ele);
zskiplistNode *zslGetElementByRank(zskiplist *zsl, unsigned long rank);
void zslDelete(zskiplist *zsl, zskiplistNode *node);
zskiplistNode *zslNthInRange(zskiplist *zsl, zrangespec *range, long n, long *rank);

/* Accessor functions */
zskiplistNode *zslGetHeader(zskiplist *zsl);
zskiplistNode *zslGetTail(const zskiplist *zsl);
unsigned long zslGetLength(const zskiplist *zsl);
sds zslGetNodeElement(const zskiplistNode *x);

/* Iterator API */
typedef uint64_t zskiplistIterator[2]; /* Opaque iterator type */
void zslInitIterator(zskiplistIterator *iterator, zskiplist *zsl);
bool zslNext(zskiplistIterator *iterator, zskiplistNode **nodeptr);

unsigned char *lpNew(size_t capacity);
void lpFree(unsigned char *lp);
unsigned char *lpAppend(unsigned char *lp, unsigned char *s, uint32_t slen);
unsigned char *lpAppendInteger(unsigned char *lp, long long lval);
unsigned char *lpFirst(unsigned char *lp);
unsigned char *lpNext(unsigned char *lp, unsigned char *p);
unsigned char *lpSeek(unsigned char *lp, long index);
unsigned char *lpGet(unsigned char *p, int64_t *count, unsigned char *intbuf);
}

/* ============ Cold Memory Configuration ============ */
static constexpr size_t kEleSize = 24;                   // Enough for "%09zu" format + padding
static constexpr size_t kL3CacheSize = 48 * 1024 * 1024; // Adjust for your CPU
static constexpr size_t kColdMemoryMultiplier = 10;
static constexpr size_t kTargetColdMemory = kL3CacheSize * kColdMemoryMultiplier;
static constexpr size_t kMaxCollections = 10000000;
static constexpr size_t kMinCollections = 100;

// Bytes per item estimates from memory benchmarks
static constexpr size_t kListpackBytesPerItem = 28;
static constexpr size_t kSkiplistBytesPerItem = 120;
static constexpr size_t kFbtreeBytesPerItem = 70;

static size_t calcNumCollections(size_t item_count, size_t bytes_per_item) {
    size_t collection_size = item_count * bytes_per_item;
    size_t num = kTargetColdMemory / collection_size;
    return std::clamp(num, kMinCollections, kMaxCollections);
}

// Generate interleaved insertion order: (collection_idx, item_idx) pairs shuffled
static std::vector<std::pair<size_t, size_t>> generateInterleavedOrder(size_t num_collections, size_t items_per_collection) {
    std::vector<std::pair<size_t, size_t>> order;
    order.reserve(num_collections * items_per_collection);
    for (size_t c = 0; c < num_collections; c++)
        for (size_t i = 0; i < items_per_collection; i++)
            order.emplace_back(c, i);
    std::mt19937 rng(42);
    std::shuffle(order.begin(), order.end(), rng);
    return order;
}

static std::vector<std::vector<char>> generateElements(size_t count) {
    std::vector<std::vector<char>> elems(count);
    for (size_t i = 0; i < count; i++) {
        elems[i].resize(kEleSize);
        snprintf(elems[i].data(), kEleSize, "%09zu", i);
    }
    return elems;
}

/* ============ Listpack Cold Benchmarks ============ */

static void BM_Zset_Listpack_Cold_Insert(benchmark::State &state) {
    size_t count = state.range(0);
    size_t num_collections = calcNumCollections(count, kListpackBytesPerItem);
    auto elems = generateElements(count);

    std::vector<size_t> item_order(count);
    std::iota(item_order.begin(), item_order.end(), 0);
    std::mt19937 rng(42);
    std::shuffle(item_order.begin(), item_order.end(), rng);

    for (auto _ : state) {
        std::vector<unsigned char *> collections(num_collections);
        for (size_t c = 0; c < num_collections; c++)
            collections[c] = lpNew(256);

        auto order = generateInterleavedOrder(num_collections, count);
        for (auto [c, i] : order) {
            unsigned char *lp = collections[c];
            // Simplified append (not sorted insert - measuring raw insert cost)
            lp = lpAppend(lp, (unsigned char *)elems[item_order[i]].data(), kEleSize - 1);
            lp = lpAppendInteger(lp, (long long)item_order[i]);
            collections[c] = lp;
        }

        for (auto lp : collections) {
            benchmark::DoNotOptimize(lp);
            lpFree(lp);
        }
    }
    state.counters["Collections"] = num_collections;
    state.SetItemsProcessed(state.iterations() * num_collections * count);
}

static void BM_Zset_Listpack_Cold_LookupByScore(benchmark::State &state) {
    size_t count = state.range(0);
    size_t num_collections = calcNumCollections(count, kListpackBytesPerItem);
    auto elems = generateElements(count);

    std::vector<unsigned char *> collections(num_collections);
    auto order = generateInterleavedOrder(num_collections, count);
    for (size_t c = 0; c < num_collections; c++)
        collections[c] = lpNew(256);
    for (auto [c, i] : order) {
        collections[c] = lpAppend(collections[c], (unsigned char *)elems[i].data(), kEleSize - 1);
        collections[c] = lpAppendInteger(collections[c], (long long)i);
    }

    size_t idx = 0;
    for (auto _ : state) {
        unsigned char *lp = collections[idx % num_collections];
        long long target = idx % count;
        unsigned char *p = lpFirst(lp);
        while (p) {
            unsigned char *score_p = lpNext(lp, p);
            if (score_p) {
                int64_t score;
                lpGet(score_p, &score, nullptr);
                if (score == target) {
                    benchmark::DoNotOptimize(p);
                    break;
                }
                p = lpNext(lp, score_p);
            } else
                break;
        }
        idx++;
    }
    state.counters["Collections"] = num_collections;
    for (auto lp : collections) lpFree(lp);
}

static void BM_Zset_Listpack_Cold_RankLookup(benchmark::State &state) {
    size_t count = state.range(0);
    size_t num_collections = calcNumCollections(count, kListpackBytesPerItem);
    auto elems = generateElements(count);

    std::vector<unsigned char *> collections(num_collections);
    auto order = generateInterleavedOrder(num_collections, count);
    for (size_t c = 0; c < num_collections; c++)
        collections[c] = lpNew(256);
    for (auto [c, i] : order) {
        collections[c] = lpAppend(collections[c], (unsigned char *)elems[i].data(), kEleSize - 1);
        collections[c] = lpAppendInteger(collections[c], (long long)i);
    }

    size_t idx = 0;
    for (auto _ : state) {
        unsigned char *lp = collections[idx % num_collections];
        unsigned char *p = lpSeek(lp, (idx % count) * 2);
        benchmark::DoNotOptimize(p);
        idx++;
    }
    state.counters["Collections"] = num_collections;
    for (auto lp : collections) lpFree(lp);
}

static void BM_Zset_Listpack_Cold_Iterate(benchmark::State &state) {
    size_t count = state.range(0);
    size_t num_collections = calcNumCollections(count, kListpackBytesPerItem);
    auto elems = generateElements(count);

    std::vector<unsigned char *> collections(num_collections);
    auto order = generateInterleavedOrder(num_collections, count);
    for (size_t c = 0; c < num_collections; c++)
        collections[c] = lpNew(256);
    for (auto [c, i] : order) {
        collections[c] = lpAppend(collections[c], (unsigned char *)elems[i].data(), kEleSize - 1);
        collections[c] = lpAppendInteger(collections[c], (long long)i);
    }

    size_t idx = 0;
    for (auto _ : state) {
        unsigned char *lp = collections[idx % num_collections];
        unsigned char *p = lpFirst(lp);
        size_t n = 0;
        while (p) {
            benchmark::DoNotOptimize(p);
            p = lpNext(lp, p);
            if (p) p = lpNext(lp, p);
            n++;
        }
        benchmark::DoNotOptimize(n);
        idx++;
    }
    state.counters["Collections"] = num_collections;
    state.SetItemsProcessed(state.iterations() * count);
    for (auto lp : collections) lpFree(lp);
}

/* ============ Skiplist Cold Benchmarks ============ */

static void BM_Zset_Skiplist_Cold_Insert(benchmark::State &state) {
    size_t count = state.range(0);
    size_t num_collections = calcNumCollections(count, kSkiplistBytesPerItem);
    auto elems = generateElements(count);

    std::vector<double> scores(count);
    std::iota(scores.begin(), scores.end(), 0);
    std::mt19937 rng(42);
    std::shuffle(scores.begin(), scores.end(), rng);

    for (auto _ : state) {
        std::vector<zskiplist *> collections(num_collections);
        for (size_t c = 0; c < num_collections; c++)
            collections[c] = zslCreate();

        auto order = generateInterleavedOrder(num_collections, count);
        for (auto [c, i] : order) {
            sds ele = sdsnewlen(elems[i].data(), kEleSize - 1);
            zslInsert(collections[c], scores[i], ele);
            sdsfree(ele);
        }

        for (auto zsl : collections) {
            benchmark::DoNotOptimize(zsl);
            zslFree(zsl);
        }
    }
    state.counters["Collections"] = num_collections;
    state.SetItemsProcessed(state.iterations() * num_collections * count);
}

static void BM_Zset_Skiplist_Cold_LookupByScore(benchmark::State &state) {
    size_t count = state.range(0);
    size_t num_collections = calcNumCollections(count, kSkiplistBytesPerItem);
    auto elems = generateElements(count);

    std::vector<zskiplist *> collections(num_collections);
    for (size_t c = 0; c < num_collections; c++)
        collections[c] = zslCreate();
    auto order = generateInterleavedOrder(num_collections, count);
    for (auto [c, i] : order) {
        sds ele = sdsnewlen(elems[i].data(), kEleSize - 1);
        zslInsert(collections[c], (double)i, ele);
        sdsfree(ele);
    }

    size_t idx = 0;
    for (auto _ : state) {
        zskiplist *zsl = collections[idx % num_collections];
        double score = (double)(idx % count);
        zrangespec range = {score, score, 0, 0};
        auto *node = zslNthInRange(zsl, &range, 0, nullptr);
        benchmark::DoNotOptimize(node);
        idx++;
    }
    state.counters["Collections"] = num_collections;
    for (auto zsl : collections) zslFree(zsl);
}

static void BM_Zset_Skiplist_Cold_RankLookup(benchmark::State &state) {
    size_t count = state.range(0);
    size_t num_collections = calcNumCollections(count, kSkiplistBytesPerItem);
    auto elems = generateElements(count);

    std::vector<zskiplist *> collections(num_collections);
    for (size_t c = 0; c < num_collections; c++)
        collections[c] = zslCreate();
    auto order = generateInterleavedOrder(num_collections, count);
    for (auto [c, i] : order) {
        sds ele = sdsnewlen(elems[i].data(), kEleSize - 1);
        zslInsert(collections[c], (double)i, ele);
        sdsfree(ele);
    }

    size_t idx = 0;
    for (auto _ : state) {
        zskiplist *zsl = collections[idx % num_collections];
        auto *node = zslGetElementByRank(zsl, (idx % count) + 1);
        benchmark::DoNotOptimize(node);
        idx++;
    }
    state.counters["Collections"] = num_collections;
    for (auto zsl : collections) zslFree(zsl);
}

static void BM_Zset_Skiplist_Cold_Iterate(benchmark::State &state) {
    size_t count = state.range(0);
    size_t num_collections = calcNumCollections(count, kSkiplistBytesPerItem);
    auto elems = generateElements(count);

    std::vector<zskiplist *> collections(num_collections);
    for (size_t c = 0; c < num_collections; c++)
        collections[c] = zslCreate();
    auto order = generateInterleavedOrder(num_collections, count);
    for (auto [c, i] : order) {
        sds ele = sdsnewlen(elems[i].data(), kEleSize - 1);
        zslInsert(collections[c], (double)i, ele);
        sdsfree(ele);
    }

    size_t idx = 0;
    for (auto _ : state) {
        zskiplist *zsl = collections[idx % num_collections];
        zskiplistIterator iter;
        zslInitIterator(&iter, zsl);
        zskiplistNode *node;
        size_t n = 0;
        while (zslNext(&iter, &node)) {
            benchmark::DoNotOptimize(node);
            n++;
        }
        benchmark::DoNotOptimize(n);
        idx++;
    }
    state.counters["Collections"] = num_collections;
    state.SetItemsProcessed(state.iterations() * count);
    for (auto zsl : collections) zslFree(zsl);
}

/* ============ Fbtree Cold Benchmarks ============ */

static constexpr size_t kItemSize = sizeof(double) + kEleSize;

static void BM_Zset_Fbtree_Cold_Insert(benchmark::State &state) {
    size_t count = state.range(0);
    size_t num_collections = calcNumCollections(count, kFbtreeBytesPerItem);
    auto elems = generateElements(count);

    std::vector<double> scores(count);
    std::iota(scores.begin(), scores.end(), 0);
    std::mt19937 rng(42);
    std::shuffle(scores.begin(), scores.end(), rng);

    for (auto _ : state) {
        std::vector<fbtreeIndex *> collections(num_collections);
        for (size_t c = 0; c < num_collections; c++)
            collections[c] = fbtreeCreate();

        auto order = generateInterleavedOrder(num_collections, count);
        for (auto [c, i] : order) {
            char buf[kItemSize];
            memcpy(buf, &scores[i], sizeof(double));
            memcpy(buf + sizeof(double), elems[i].data(), kEleSize);
            sds ss = sdsnewlen(buf, kItemSize);
            fbtreeInsert(collections[c], ss);
        }

        for (auto fbt : collections) {
            benchmark::DoNotOptimize(fbt);
            fbtreeFree(fbt);
        }
    }
    state.counters["Collections"] = num_collections;
    state.SetItemsProcessed(state.iterations() * num_collections * count);
}

static void BM_Zset_Fbtree_Cold_SeekToScore(benchmark::State &state) {
    size_t count = state.range(0);
    size_t num_collections = calcNumCollections(count, kFbtreeBytesPerItem);
    auto elems = generateElements(count);

    std::vector<fbtreeIndex *> collections(num_collections);
    for (size_t c = 0; c < num_collections; c++)
        collections[c] = fbtreeCreate();
    auto order = generateInterleavedOrder(num_collections, count);
    for (auto [c, i] : order) {
        char buf[kItemSize];
        double score = (double)i;
        memcpy(buf, &score, sizeof(double));
        memcpy(buf + sizeof(double), elems[i].data(), kEleSize);
        sds ss = sdsnewlen(buf, kItemSize);
        fbtreeInsert(collections[c], ss);
    }

    size_t idx = 0;
    for (auto _ : state) {
        fbtreeIndex *fbt = collections[idx % num_collections];
        double score = (double)(idx % count);
        fbtreeIterator iter;
        fbtreeSeekToScore(fbt, (const char *)&score, &iter);
        benchmark::DoNotOptimize(iter);
        idx++;
    }
    state.counters["Collections"] = num_collections;
    for (auto fbt : collections) fbtreeFree(fbt);
}

static void BM_Zset_Fbtree_Cold_RankLookup(benchmark::State &state) {
    size_t count = state.range(0);
    size_t num_collections = calcNumCollections(count, kFbtreeBytesPerItem);
    auto elems = generateElements(count);

    std::vector<fbtreeIndex *> collections(num_collections);
    struct FbtreeIter {
        fbtreeIterator it;
    };
    std::vector<FbtreeIter> iterators(num_collections);
    for (size_t c = 0; c < num_collections; c++)
        collections[c] = fbtreeCreate();
    auto order = generateInterleavedOrder(num_collections, count);
    for (auto [c, i] : order) {
        char buf[kItemSize];
        double score = (double)i;
        memcpy(buf, &score, sizeof(double));
        memcpy(buf + sizeof(double), elems[i].data(), kEleSize);
        sds ss = sdsnewlen(buf, kItemSize);
        fbtreeInsert(collections[c], ss);
    }
    for (size_t c = 0; c < num_collections; c++)
        fbtreeInitIterator(&iterators[c].it, collections[c]);

    size_t idx = 0;
    for (auto _ : state) {
        size_t c = idx % num_collections;
        fbtreeSeekToRank(&iterators[c].it, idx % count);
        const_sds pos;
        fbtreeNext(&iterators[c].it, &pos);
        benchmark::DoNotOptimize(pos);
        idx++;
    }
    state.counters["Collections"] = num_collections;
    for (auto fbt : collections) fbtreeFree(fbt);
}

static void BM_Zset_Fbtree_Cold_Iterate(benchmark::State &state) {
    size_t count = state.range(0);
    size_t num_collections = calcNumCollections(count, kFbtreeBytesPerItem);
    auto elems = generateElements(count);

    std::vector<fbtreeIndex *> collections(num_collections);
    for (size_t c = 0; c < num_collections; c++)
        collections[c] = fbtreeCreate();
    auto order = generateInterleavedOrder(num_collections, count);
    for (auto [c, i] : order) {
        char buf[kItemSize];
        double score = (double)i;
        memcpy(buf, &score, sizeof(double));
        memcpy(buf + sizeof(double), elems[i].data(), kEleSize);
        sds ss = sdsnewlen(buf, kItemSize);
        fbtreeInsert(collections[c], ss);
    }

    size_t idx = 0;
    for (auto _ : state) {
        fbtreeIndex *fbt = collections[idx % num_collections];
        fbtreeIterator iter;
        fbtreeInitIterator(&iter, fbt);
        const_sds pos;
        size_t n = 0;
        while (fbtreeNext(&iter, &pos)) {
            benchmark::DoNotOptimize(pos);
            n++;
        }
        benchmark::DoNotOptimize(n);
        idx++;
    }
    state.counters["Collections"] = num_collections;
    state.SetItemsProcessed(state.iterations() * count);
    for (auto fbt : collections) fbtreeFree(fbt);
}

// TODO: Add BM_Zset_Fbtree_Cold_Delete once fbtree delete/merge/shrink is implemented

/* ============ Register Benchmarks ============ */

static void ThresholdCountArgs(::benchmark::Benchmark *b) {
    for (int64_t n = 2 << 4; n <= 2 << 20; n <<= 1) b->Args({n});
}

BENCHMARK(BM_Zset_Listpack_Cold_Insert)->Apply(ThresholdCountArgs)->Iterations(10);
BENCHMARK(BM_Zset_Listpack_Cold_LookupByScore)->Apply(ThresholdCountArgs);
BENCHMARK(BM_Zset_Listpack_Cold_RankLookup)->Apply(ThresholdCountArgs);
BENCHMARK(BM_Zset_Listpack_Cold_Iterate)->Apply(ThresholdCountArgs);

BENCHMARK(BM_Zset_Skiplist_Cold_Insert)->Apply(ThresholdCountArgs)->Iterations(10);
BENCHMARK(BM_Zset_Skiplist_Cold_LookupByScore)->Apply(ThresholdCountArgs);
BENCHMARK(BM_Zset_Skiplist_Cold_RankLookup)->Apply(ThresholdCountArgs);
BENCHMARK(BM_Zset_Skiplist_Cold_Iterate)->Apply(ThresholdCountArgs);

BENCHMARK(BM_Zset_Fbtree_Cold_Insert)->Apply(ThresholdCountArgs)->Iterations(10);
BENCHMARK(BM_Zset_Fbtree_Cold_SeekToScore)->Apply(ThresholdCountArgs);
BENCHMARK(BM_Zset_Fbtree_Cold_RankLookup)->Apply(ThresholdCountArgs);
BENCHMARK(BM_Zset_Fbtree_Cold_Iterate)->Apply(ThresholdCountArgs);
