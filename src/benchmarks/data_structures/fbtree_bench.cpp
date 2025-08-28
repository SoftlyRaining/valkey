/*
 * Fbtree Performance Benchmarks (Tier 1: Isolated Cold-Cache)
 *
 * Tests fbtree operations under cold-cache conditions:
 * - Small sets (< LLC): explicit cache flush + batched operations
 * - Large sets (>= LLC): random access naturally keeps data cold
 *
 * Parameters via Args(item_count, item_size):
 *   item_count: 128 to 5M (log scale)
 *   item_size:  16B to 64B (score + element)
 *
 * Build orders:
 *   SeqBuild:  Sequential insertion (RDB load, time series)
 *   RandBuild: Random insertion (typical runtime behavior)
 */

#include "bench_common.h"
#include "fbtree_bench_decls.h"

#include <algorithm>
#include <numeric>
#include <random>
#include <vector>

/* ============ Test Data Generation ============ */

static std::vector<sds> createStrings(size_t count, size_t item_size, bool shuffle) {
    std::vector<size_t> indices(count);
    std::iota(indices.begin(), indices.end(), 0);
    if (shuffle) {
        std::mt19937 rng(42);
        std::shuffle(indices.begin(), indices.end(), rng);
    }

    std::vector<sds> strs(count);
    std::vector<char> buf(item_size);
    for (size_t i = 0; i < count; i++) {
        encodeScoreToBytes((double)indices[i], (unsigned char *)buf.data());
        memset(buf.data() + 8, 'x', item_size - 8 - 1);
        buf[item_size - 1] = '\0';
        strs[i] = sdsnewlen(buf.data(), item_size);
    }
    return strs;
}

static void freeStrings(std::vector<sds> &strs) {
    for (auto s : strs) sdsfree(s);
    strs.clear();
}

/* ============ Parameterized Fixture ============ */

/* Cache built trees to avoid rebuilding on every SetUp call */
struct FbtreeCache {
    fbtreeIndex *fbt = nullptr;
    std::vector<sds> strs;
    std::vector<sds> items;
    size_t item_count = 0;
    size_t item_size = 0;
    bool random_build = false;

    FbtreeCache() = default;
    FbtreeCache(const FbtreeCache &) = delete;
    FbtreeCache &operator=(const FbtreeCache &) = delete;
    FbtreeCache(FbtreeCache &&o) noexcept
        : fbt(o.fbt), strs(std::move(o.strs)), items(std::move(o.items)),
          item_count(o.item_count), item_size(o.item_size), random_build(o.random_build) {
        o.fbt = nullptr;
    }
    FbtreeCache &operator=(FbtreeCache &&o) noexcept {
        if (this != &o) {
            if (fbt) fbtreeFree(fbt);
            for (auto &str : strs) sdsfree(str);
            fbt = o.fbt;
            o.fbt = nullptr;
            strs = std::move(o.strs);
            items = std::move(o.items);
            item_count = o.item_count;
            item_size = o.item_size;
            random_build = o.random_build;
        }
        return *this;
    }
    ~FbtreeCache() {
        if (fbt) fbtreeFree(fbt);
        for (auto &str : strs) sdsfree(str);
    }
};
static std::vector<FbtreeCache> g_fbtree_cache;

/* Clean up cache at program exit */
static struct FbtreeCacheCleanup {
    ~FbtreeCacheCleanup() {
        g_fbtree_cache.clear();
    }
} g_fbtree_cache_cleanup;

template <bool RandomBuild>
class Fbtree_Fixture : public benchmark::Fixture {
  protected:
    fbtreeIndex *fbt = nullptr;
    std::vector<sds> *strs = nullptr;
    std::vector<sds> *items = nullptr;
    size_t item_count = 0;
    size_t item_size = 0;

    FbtreeCache *findOrCreateCache(size_t count, size_t size) {
        for (auto &c : g_fbtree_cache) {
            if (c.item_count == count && c.item_size == size && c.random_build == RandomBuild)
                return &c;
        }
        /* Build new cache entry */
        g_fbtree_cache.emplace_back();
        auto &c = g_fbtree_cache.back();
        c.item_count = count;
        c.item_size = size;
        c.random_build = RandomBuild;
        c.strs = createStrings(count, size, false);
        c.fbt = fbtreeCreate();
        c.items.resize(count);

        if constexpr (RandomBuild) {
            std::vector<size_t> order(count);
            std::iota(order.begin(), order.end(), 0);
            std::mt19937 rng(42);
            std::shuffle(order.begin(), order.end(), rng);
            for (size_t i = 0; i < count; i++)
                c.items[order[i]] = fbtreeInsert(c.fbt, sdsdup(c.strs[order[i]]));
        } else {
            for (size_t i = 0; i < count; i++)
                c.items[i] = fbtreeInsert(c.fbt, sdsdup(c.strs[i]));
        }
        return &c;
    }

  public:
    void SetUp(benchmark::State &state) override {
        item_count = state.range(0);
        item_size = state.range(1);

        auto *cache = findOrCreateCache(item_count, item_size);
        fbt = cache->fbt;
        strs = &cache->strs;
        items = &cache->items;
    }

    void TearDown(benchmark::State &) override {
        /* Don't free - cache owns the data */
    }
};

using Fbtree_SeqBuild = Fbtree_Fixture<false>;
using Fbtree_RandBuild = Fbtree_Fixture<true>;

/* ============ Insert Benchmarks ============
 *
 * Pre-build to 90% of target size, then time only the last 10% of inserts.
 * This measures insert performance at the target size, not averaged from 0.
 */

static void BM_Fbtree_Insert_Append(benchmark::State &state) {
    size_t count = state.range(0);
    size_t size = state.range(1);
    size_t warmup = count * 9 / 10;
    size_t insert_count = count - warmup;
    auto strs = createStrings(count, size, false);
    std::vector<char> buf(size);

    /* Build 90% once */
    fbtreeIndex *fbt = fbtreeCreate();
    for (size_t i = 0; i < warmup; i++)
        fbtreeInsert(fbt, sdsdup(strs[i]));

    double score_offset = 0;
    for (auto _ : state) {
        /* Insert last 10% at tail */
        for (size_t i = 0; i < insert_count; i++) {
            encodeScoreToBytes((double)(warmup + i) + score_offset, (unsigned char *)buf.data());
            memset(buf.data() + 8, 'x', size - 8 - 1);
            buf[size - 1] = '\0';
            fbtreeInsert(fbt, sdsnewlen(buf.data(), size));
        }
        benchmark::DoNotOptimize(fbt);

        state.PauseTiming();
        /* Pop 10% from head to restore size */
        for (size_t i = 0; i < insert_count; i++) {
            sds item = fbtreePopMin(fbt);
            sdsfree(item);
        }
        score_offset += count;
        /* Guard against precision loss at extreme iteration counts */
        if (score_offset > 1e15) {
            fbtreeFree(fbt);
            fbt = fbtreeCreate();
            for (size_t i = 0; i < warmup; i++)
                fbtreeInsert(fbt, sdsdup(strs[i]));
            score_offset = 0;
        }
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * insert_count);
    fbtreeFree(fbt);
    freeStrings(strs);
}

static void BM_Fbtree_Insert_Prepend(benchmark::State &state) {
    size_t count = state.range(0);
    size_t size = state.range(1);
    size_t warmup = count * 9 / 10;
    size_t insert_count = count - warmup;
    auto strs = createStrings(count, size, false);
    std::vector<char> buf(size);

    /* Build 90% once */
    fbtreeIndex *fbt = fbtreeCreate();
    for (size_t i = 0; i < warmup; i++)
        fbtreeInsert(fbt, sdsdup(strs[i]));

    double score_offset = 0;
    for (auto _ : state) {
        /* Insert 10% at head (negative scores) */
        for (size_t i = 0; i < insert_count; i++) {
            encodeScoreToBytes(score_offset - (double)(i + 1), (unsigned char *)buf.data());
            memset(buf.data() + 8, 'x', size - 8 - 1);
            buf[size - 1] = '\0';
            fbtreeInsert(fbt, sdsnewlen(buf.data(), size));
        }
        benchmark::DoNotOptimize(fbt);

        state.PauseTiming();
        /* Pop 10% from tail to restore size */
        for (size_t i = 0; i < insert_count; i++) {
            sds item = fbtreePopMax(fbt);
            sdsfree(item);
        }
        score_offset -= count;
        /* Guard against precision loss at extreme iteration counts */
        if (score_offset < -1e15) {
            fbtreeFree(fbt);
            fbt = fbtreeCreate();
            for (size_t i = 0; i < warmup; i++)
                fbtreeInsert(fbt, sdsdup(strs[i]));
            score_offset = 0;
        }
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * insert_count);
    fbtreeFree(fbt);
    freeStrings(strs);
}

static void BM_Fbtree_Insert_Random(benchmark::State &state) {
    size_t count = state.range(0);
    size_t size = state.range(1);
    size_t warmup = count * 9 / 10;
    auto strs = createStrings(count, size, true);

    for (auto _ : state) {
        state.PauseTiming();
        fbtreeIndex *fbt = fbtreeCreate();
        for (size_t i = 0; i < warmup; i++)
            fbtreeInsert(fbt, sdsdup(strs[i]));
        state.ResumeTiming();

        for (size_t i = warmup; i < count; i++)
            fbtreeInsert(fbt, sdsdup(strs[i]));
        benchmark::DoNotOptimize(fbt);
        fbtreeFree(fbt);
    }
    state.SetItemsProcessed(state.iterations() * (count - warmup));
    freeStrings(strs);
}

/* ============ Delete Benchmarks ============
 *
 * Pre-build to 100% of target size, then time individual deletes.
 * Re-insert after each delete to maintain constant size throughout.
 */

static void BM_Fbtree_Delete_Random(benchmark::State &state) {
    size_t count = state.range(0);
    size_t size = state.range(1);
    auto strs = createStrings(count, size, false);

    std::vector<size_t> delete_order(count);
    std::iota(delete_order.begin(), delete_order.end(), 0);
    std::mt19937 rng(42);
    std::shuffle(delete_order.begin(), delete_order.end(), rng);

    fbtreeIndex *fbt = fbtreeCreate();
    std::vector<sds> items(count);
    for (size_t i = 0; i < count; i++)
        items[i] = fbtreeInsert(fbt, sdsdup(strs[i]));

    size_t pos = 0;
    for (auto _ : state) {
        size_t idx = delete_order[pos++ % count];
        fbtreeDelete(fbt, items[idx]);
        state.PauseTiming();
        items[idx] = fbtreeInsert(fbt, sdsdup(strs[idx]));
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations());
    fbtreeFree(fbt);
    freeStrings(strs);
}

static void BM_Fbtree_Delete_PopHead(benchmark::State &state) {
    size_t count = state.range(0);
    size_t size = state.range(1);
    auto strs = createStrings(count, size, false);
    double max_score = (double)count;
    std::vector<char> buf(size);

    fbtreeIndex *fbt = fbtreeCreate();
    for (size_t i = 0; i < count; i++)
        fbtreeInsert(fbt, sdsdup(strs[i]));

    for (auto _ : state) {
        sds item = fbtreePopMin(fbt);
        state.PauseTiming();
        sdsfree(item);
        encodeScoreToBytes(max_score++, (unsigned char *)buf.data());
        memset(buf.data() + 8, 'x', size - 8 - 1);
        buf[size - 1] = '\0';
        fbtreeInsert(fbt, sdsnewlen(buf.data(), size));
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations());
    fbtreeFree(fbt);
    freeStrings(strs);
}

static void BM_Fbtree_Delete_PopTail(benchmark::State &state) {
    size_t count = state.range(0);
    size_t size = state.range(1);
    auto strs = createStrings(count, size, false);
    double min_score = -1.0;
    std::vector<char> buf(size);

    fbtreeIndex *fbt = fbtreeCreate();
    for (size_t i = 0; i < count; i++)
        fbtreeInsert(fbt, sdsdup(strs[i]));

    for (auto _ : state) {
        sds item = fbtreePopMax(fbt);
        state.PauseTiming();
        sdsfree(item);
        encodeScoreToBytes(min_score--, (unsigned char *)buf.data());
        memset(buf.data() + 8, 'x', size - 8 - 1);
        buf[size - 1] = '\0';
        fbtreeInsert(fbt, sdsnewlen(buf.data(), size));
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations());
    fbtreeFree(fbt);
    freeStrings(strs);
}

/* ============ Lookup Benchmark Macros ============
 *
 * Random access on datasets > LLC gives natural cold-cache behavior.
 */

#define DEFINE_LOOKUP_BENCHMARK(FixtureClass, Name, ...) \
    BENCHMARK_DEFINE_F(FixtureClass, Name)               \
    (benchmark::State & state) {                         \
        std::mt19937 rng(123);                           \
        for (auto _ : state) {                           \
            size_t idx = rng() % item_count;             \
            __VA_ARGS__;                                 \
        }                                                \
        state.SetItemsProcessed(state.iterations());     \
    }

/* ============ SeqBuild Lookup Benchmarks ============ */

DEFINE_LOOKUP_BENCHMARK(Fbtree_SeqBuild, RankLookup, auto s = fbtreeGetAtRank(fbt, idx);
                        benchmark::DoNotOptimize(s))

DEFINE_LOOKUP_BENCHMARK(Fbtree_SeqBuild, SeekToScore, fbtreeIterator iter;
                        fbtreeSeekToScore(fbt, (*strs)[idx], &iter);
                        benchmark::DoNotOptimize(iter))

DEFINE_LOOKUP_BENCHMARK(Fbtree_SeqBuild, GetRankOfItem, auto rank = fbtreeGetRankOfItem(fbt, (*items)[idx]);
                        benchmark::DoNotOptimize(rank))

BENCHMARK_DEFINE_F(Fbtree_SeqBuild, IterateForward)
(benchmark::State &state) {
    for (auto _ : state) {
        fbtreeIterator iter;
        fbtreeInitIterator(&iter, fbt);
        const_sds pos;
        unsigned long count = 0;
        while (fbtreeNext(&iter, &pos)) count++;
        benchmark::DoNotOptimize(count);
    }
    state.SetItemsProcessed(state.iterations() * item_count);
}

BENCHMARK_DEFINE_F(Fbtree_SeqBuild, IterateBackward)
(benchmark::State &state) {
    for (auto _ : state) {
        fbtreeIterator iter;
        fbtreeInitIterator(&iter, fbt);
        const_sds pos;
        unsigned long count = 0;
        while (fbtreePrev(&iter, &pos)) count++;
        benchmark::DoNotOptimize(count);
    }
    state.SetItemsProcessed(state.iterations() * item_count);
}

/* ============ RandBuild Lookup Benchmarks ============ */

DEFINE_LOOKUP_BENCHMARK(Fbtree_RandBuild, RankLookup, auto s = fbtreeGetAtRank(fbt, idx);
                        benchmark::DoNotOptimize(s))

DEFINE_LOOKUP_BENCHMARK(Fbtree_RandBuild, SeekToScore, fbtreeIterator iter;
                        fbtreeSeekToScore(fbt, (*strs)[idx], &iter);
                        benchmark::DoNotOptimize(iter))

DEFINE_LOOKUP_BENCHMARK(Fbtree_RandBuild, GetRankOfItem, auto rank = fbtreeGetRankOfItem(fbt, (*items)[idx]);
                        benchmark::DoNotOptimize(rank))

BENCHMARK_DEFINE_F(Fbtree_RandBuild, IterateForward)
(benchmark::State &state) {
    for (auto _ : state) {
        fbtreeIterator iter;
        fbtreeInitIterator(&iter, fbt);
        const_sds pos;
        unsigned long count = 0;
        while (fbtreeNext(&iter, &pos)) count++;
        benchmark::DoNotOptimize(count);
    }
    state.SetItemsProcessed(state.iterations() * item_count);
}

BENCHMARK_DEFINE_F(Fbtree_RandBuild, IterateBackward)
(benchmark::State &state) {
    for (auto _ : state) {
        fbtreeIterator iter;
        fbtreeInitIterator(&iter, fbt);
        const_sds pos;
        unsigned long count = 0;
        while (fbtreePrev(&iter, &pos)) count++;
        benchmark::DoNotOptimize(count);
    }
    state.SetItemsProcessed(state.iterations() * item_count);
}

/* ============ Register Benchmarks ============ */

/* Insert benchmarks - test all size combinations */
BENCHMARK(BM_Fbtree_Insert_Append)->BENCH_ARGS_EXTENDED();
BENCHMARK(BM_Fbtree_Insert_Prepend)->BENCH_ARGS_EXTENDED();
BENCHMARK(BM_Fbtree_Insert_Random)->BENCH_ARGS_EXTENDED()->Iterations(1);

/* Delete benchmarks */
BENCHMARK(BM_Fbtree_Delete_Random)->BENCH_ARGS_STANDARD();
BENCHMARK(BM_Fbtree_Delete_PopHead)->BENCH_ARGS_STANDARD();
BENCHMARK(BM_Fbtree_Delete_PopTail)->BENCH_ARGS_STANDARD();

/* SeqBuild lookup benchmarks */
BENCHMARK_REGISTER_F(Fbtree_SeqBuild, RankLookup)->BENCH_ARGS_STANDARD();
BENCHMARK_REGISTER_F(Fbtree_SeqBuild, SeekToScore)->BENCH_ARGS_STANDARD();
BENCHMARK_REGISTER_F(Fbtree_SeqBuild, GetRankOfItem)->BENCH_ARGS_STANDARD();
BENCHMARK_REGISTER_F(Fbtree_SeqBuild, IterateForward)->BENCH_ARGS_STANDARD()->Iterations(1);
BENCHMARK_REGISTER_F(Fbtree_SeqBuild, IterateBackward)->BENCH_ARGS_STANDARD()->Iterations(1);

/* RandBuild lookup benchmarks */
BENCHMARK_REGISTER_F(Fbtree_RandBuild, RankLookup)->BENCH_ARGS_STANDARD();
BENCHMARK_REGISTER_F(Fbtree_RandBuild, SeekToScore)->BENCH_ARGS_STANDARD();
BENCHMARK_REGISTER_F(Fbtree_RandBuild, GetRankOfItem)->BENCH_ARGS_STANDARD();
BENCHMARK_REGISTER_F(Fbtree_RandBuild, IterateForward)->BENCH_ARGS_STANDARD()->Iterations(1);
BENCHMARK_REGISTER_F(Fbtree_RandBuild, IterateBackward)->BENCH_ARGS_STANDARD()->Iterations(1);
