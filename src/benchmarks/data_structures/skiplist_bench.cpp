/*
 * Skiplist Performance Benchmarks (Tier 1: Isolated Cold-Cache)
 *
 * Tests skiplist operations under cold-cache conditions:
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
#include "skiplist_bench_decls.h"

#include <algorithm>
#include <numeric>
#include <random>
#include <vector>

/* ============ Test Data Generation ============ */

static std::vector<sds> createElements(size_t count, size_t item_size, bool shuffle) {
    std::vector<size_t> indices(count);
    std::iota(indices.begin(), indices.end(), 0);
    if (shuffle) {
        std::mt19937 rng(42);
        std::shuffle(indices.begin(), indices.end(), rng);
    }

    size_t ele_size = item_size - sizeof(double);
    std::vector<sds> elems(count);
    std::vector<char> buf(ele_size + 1);
    for (size_t i = 0; i < count; i++) {
        snprintf(buf.data(), buf.size(), "%0*zu", (int)ele_size, indices[i]);
        elems[i] = sdsnewlen(buf.data(), ele_size);
    }
    return elems;
}

static void freeElements(std::vector<sds> &elems) {
    for (auto e : elems) sdsfree(e);
    elems.clear();
}

/* ============ Parameterized Fixture ============ */

/* Cache built skiplists to avoid rebuilding on every SetUp call */
struct SkiplistCache {
    zskiplist *zsl = nullptr;
    std::vector<sds> elems;
    std::vector<zskiplistNode *> nodes;
    size_t item_count = 0;
    size_t item_size = 0;
    bool random_build = false;

    SkiplistCache() = default;
    SkiplistCache(const SkiplistCache &) = delete;
    SkiplistCache &operator=(const SkiplistCache &) = delete;
    SkiplistCache(SkiplistCache &&o) noexcept
        : zsl(o.zsl), elems(std::move(o.elems)), nodes(std::move(o.nodes)),
          item_count(o.item_count), item_size(o.item_size), random_build(o.random_build) {
        o.zsl = nullptr;
    }
    SkiplistCache &operator=(SkiplistCache &&o) noexcept {
        if (this != &o) {
            if (zsl) zslFree(zsl);
            for (auto &elem : elems) sdsfree(elem);
            zsl = o.zsl;
            o.zsl = nullptr;
            elems = std::move(o.elems);
            nodes = std::move(o.nodes);
            item_count = o.item_count;
            item_size = o.item_size;
            random_build = o.random_build;
        }
        return *this;
    }
    ~SkiplistCache() {
        if (zsl) zslFree(zsl);
        for (auto &elem : elems) sdsfree(elem);
    }
};
static std::vector<SkiplistCache> g_skiplist_cache;

/* Clean up cache at program exit */
static struct SkiplistCacheCleanup {
    ~SkiplistCacheCleanup() {
        g_skiplist_cache.clear();
    }
} g_skiplist_cache_cleanup;

template <bool RandomBuild>
class Skiplist_Fixture : public benchmark::Fixture {
  protected:
    zskiplist *zsl = nullptr;
    std::vector<sds> *elems = nullptr;
    std::vector<zskiplistNode *> *nodes = nullptr;
    size_t item_count = 0;
    size_t item_size = 0;

    SkiplistCache *findOrCreateCache(size_t count, size_t size) {
        for (auto &c : g_skiplist_cache) {
            if (c.item_count == count && c.item_size == size && c.random_build == RandomBuild)
                return &c;
        }
        /* Build new cache entry */
        g_skiplist_cache.emplace_back();
        auto &c = g_skiplist_cache.back();
        c.item_count = count;
        c.item_size = size;
        c.random_build = RandomBuild;
        c.elems = createElements(count, size, false);
        c.zsl = zslCreate();
        c.nodes.resize(count);

        if constexpr (RandomBuild) {
            std::vector<size_t> order(count);
            std::iota(order.begin(), order.end(), 0);
            std::mt19937 rng(42);
            std::shuffle(order.begin(), order.end(), rng);
            for (size_t i = 0; i < count; i++)
                c.nodes[order[i]] = zslInsert(c.zsl, (double)order[i], c.elems[order[i]]);
        } else {
            for (size_t i = 0; i < count; i++)
                c.nodes[i] = zslInsert(c.zsl, (double)i, c.elems[i]);
        }
        return &c;
    }

  public:
    void SetUp(benchmark::State &state) override {
        item_count = state.range(0);
        item_size = state.range(1);

        auto *cache = findOrCreateCache(item_count, item_size);
        zsl = cache->zsl;
        elems = &cache->elems;
        nodes = &cache->nodes;
    }

    void TearDown(benchmark::State &) override {
        /* Don't free - cache owns the data */
    }
};

using Skiplist_SeqBuild = Skiplist_Fixture<false>;
using Skiplist_RandBuild = Skiplist_Fixture<true>;

/* ============ Insert Benchmarks ============
 *
 * Pre-build to 90% of target size, then time only the last 10% of inserts.
 * This measures insert performance at the target size, not averaged from 0.
 */

static void BM_Skiplist_Insert_Append(benchmark::State &state) {
    size_t count = state.range(0);
    size_t size = state.range(1);
    size_t warmup = count * 9 / 10;
    size_t insert_count = count - warmup;
    auto elems = createElements(count, size, false);

    /* Build 90% once */
    zskiplist *zsl = zslCreate();
    for (size_t i = 0; i < warmup; i++)
        zslInsert(zsl, (double)i, elems[i]);

    double score_offset = 0;
    for (auto _ : state) {
        /* Insert last 10% at tail */
        for (size_t i = 0; i < insert_count; i++)
            zslInsert(zsl, (double)(warmup + i) + score_offset, elems[warmup + i]);
        benchmark::DoNotOptimize(zsl);

        state.PauseTiming();
        /* Pop 10% from head to restore size */
        for (size_t i = 0; i < insert_count; i++) {
            zskiplistNode *head = zslGetFirst(zsl);
            zslDelete(zsl, head);
        }
        score_offset += count;
        /* Guard against precision loss at extreme iteration counts */
        if (score_offset > 1e15) {
            zslFree(zsl);
            zsl = zslCreate();
            for (size_t i = 0; i < warmup; i++)
                zslInsert(zsl, (double)i, elems[i]);
            score_offset = 0;
        }
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * insert_count);
    zslFree(zsl);
    freeElements(elems);
}

static void BM_Skiplist_Insert_Prepend(benchmark::State &state) {
    size_t count = state.range(0);
    size_t size = state.range(1);
    size_t warmup = count * 9 / 10;
    size_t insert_count = count - warmup;
    auto elems = createElements(count, size, false);

    /* Build 90% once */
    zskiplist *zsl = zslCreate();
    for (size_t i = 0; i < warmup; i++)
        zslInsert(zsl, (double)i, elems[i]);

    double score_offset = 0;
    for (auto _ : state) {
        /* Insert 10% at head (negative scores) */
        for (size_t i = 0; i < insert_count; i++)
            zslInsert(zsl, score_offset - (double)(i + 1), elems[i]);
        benchmark::DoNotOptimize(zsl);

        state.PauseTiming();
        /* Pop 10% from tail to restore size */
        for (size_t i = 0; i < insert_count; i++) {
            zskiplistNode *tail = zslGetTail(zsl);
            zslDelete(zsl, tail);
        }
        score_offset -= count;
        /* Guard against precision loss at extreme iteration counts */
        if (score_offset < -1e15) {
            zslFree(zsl);
            zsl = zslCreate();
            for (size_t i = 0; i < warmup; i++)
                zslInsert(zsl, (double)i, elems[i]);
            score_offset = 0;
        }
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * insert_count);
    zslFree(zsl);
    freeElements(elems);
}

static void BM_Skiplist_Insert_Random(benchmark::State &state) {
    size_t count = state.range(0);
    size_t size = state.range(1);
    size_t warmup = count * 9 / 10;
    auto elems = createElements(count, size, true);

    std::vector<double> scores(count);
    std::iota(scores.begin(), scores.end(), 0);
    std::mt19937 rng(42);
    std::shuffle(scores.begin(), scores.end(), rng);

    for (auto _ : state) {
        state.PauseTiming();
        zskiplist *zsl = zslCreate();
        for (size_t i = 0; i < warmup; i++)
            zslInsert(zsl, scores[i], elems[i]);
        state.ResumeTiming();

        for (size_t i = warmup; i < count; i++)
            zslInsert(zsl, scores[i], elems[i]);
        benchmark::DoNotOptimize(zsl);
        zslFree(zsl);
    }
    state.SetItemsProcessed(state.iterations() * (count - warmup));
    freeElements(elems);
}

/* ============ Delete Benchmarks ============
 *
 * Pre-build to 100% of target size, then time individual deletes.
 * Re-insert after each delete to maintain constant size throughout.
 */

static void BM_Skiplist_Delete_Random(benchmark::State &state) {
    size_t count = state.range(0);
    size_t size = state.range(1);
    auto elems = createElements(count, size, false);

    std::vector<size_t> delete_order(count);
    std::iota(delete_order.begin(), delete_order.end(), 0);
    std::mt19937 rng(42);
    std::shuffle(delete_order.begin(), delete_order.end(), rng);

    std::vector<double> scores(count);
    for (size_t i = 0; i < count; i++)
        scores[i] = (double)i;

    zskiplist *zsl = zslCreate();
    std::vector<zskiplistNode *> nodes(count);
    for (size_t i = 0; i < count; i++)
        nodes[i] = zslInsert(zsl, scores[i], elems[i]);

    size_t pos = 0;
    for (auto _ : state) {
        size_t idx = delete_order[pos++ % count];
        zslDelete(zsl, nodes[idx]);
        state.PauseTiming();
        nodes[idx] = zslInsert(zsl, scores[idx], elems[idx]);
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations());
    zslFree(zsl);
    freeElements(elems);
}

static void BM_Skiplist_Delete_PopHead(benchmark::State &state) {
    size_t count = state.range(0);
    size_t size = state.range(1);
    auto elems = createElements(count, size, false);

    zskiplist *zsl = zslCreate();
    for (size_t i = 0; i < count; i++)
        zslInsert(zsl, (double)i, elems[i]);
    double max_score = (double)count;

    for (auto _ : state) {
        zskiplistNode *head = zslGetFirst(zsl);
        sds ele = sdsdup(zslGetNodeElement(head));
        zslDelete(zsl, head);
        state.PauseTiming();
        zslInsert(zsl, max_score++, ele);
        sdsfree(ele); /* zslInsert copies the element, so free our copy */
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations());
    zslFree(zsl);
    freeElements(elems);
}

static void BM_Skiplist_Delete_PopTail(benchmark::State &state) {
    size_t count = state.range(0);
    size_t size = state.range(1);
    auto elems = createElements(count, size, false);

    zskiplist *zsl = zslCreate();
    for (size_t i = 0; i < count; i++)
        zslInsert(zsl, (double)i, elems[i]);
    double min_score = -1.0;

    for (auto _ : state) {
        zskiplistNode *tail = zslGetTail(zsl);
        sds ele = sdsdup(zslGetNodeElement(tail));
        zslDelete(zsl, tail);
        state.PauseTiming();
        zslInsert(zsl, min_score--, ele);
        sdsfree(ele); /* zslInsert copies the element, so free our copy */
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations());
    zslFree(zsl);
    freeElements(elems);
}

/* ============ Lookup Benchmark Macros ============ */

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

DEFINE_LOOKUP_BENCHMARK(Skiplist_SeqBuild, RankLookup, auto *node = zslGetElementByRank(zsl, idx + 1); /* 1-indexed */
                        benchmark::DoNotOptimize(node))

DEFINE_LOOKUP_BENCHMARK(Skiplist_SeqBuild, SeekToScore, zrangespec range = {(double)idx, (double)idx, 0, 0};
                        auto *node = zslNthInRange(zsl, &range, 0, nullptr);
                        benchmark::DoNotOptimize(node))

DEFINE_LOOKUP_BENCHMARK(Skiplist_SeqBuild, GetRankOfItem, auto rank = zslGetRank(zsl, (*nodes)[idx]);
                        benchmark::DoNotOptimize(rank))

BENCHMARK_DEFINE_F(Skiplist_SeqBuild, IterateForward)
(benchmark::State &state) {
    for (auto _ : state) {
        zskiplistIterator iter;
        zslInitIterator(&iter, zsl);
        zskiplistNode *node;
        unsigned long count = 0;
        while (zslNext(&iter, &node)) {
            count++;
            benchmark::DoNotOptimize(node);
        }
        benchmark::DoNotOptimize(count);
    }
    state.SetItemsProcessed(state.iterations() * item_count);
}

BENCHMARK_DEFINE_F(Skiplist_SeqBuild, IterateBackward)
(benchmark::State &state) {
    for (auto _ : state) {
        zskiplistIterator iter;
        zslInitIterator(&iter, zsl);
        zslSeekToRank(&iter, item_count); /* Position at end */
        zskiplistNode *node;
        unsigned long count = 0;
        while (zslPrev(&iter, &node)) {
            count++;
            benchmark::DoNotOptimize(node);
        }
        benchmark::DoNotOptimize(count);
    }
    state.SetItemsProcessed(state.iterations() * item_count);
}

/* ============ RandBuild Lookup Benchmarks ============ */

DEFINE_LOOKUP_BENCHMARK(Skiplist_RandBuild, RankLookup, auto *node = zslGetElementByRank(zsl, idx + 1);
                        benchmark::DoNotOptimize(node))

DEFINE_LOOKUP_BENCHMARK(Skiplist_RandBuild, SeekToScore, zrangespec range = {(double)idx, (double)idx, 0, 0};
                        auto *node = zslNthInRange(zsl, &range, 0, nullptr);
                        benchmark::DoNotOptimize(node))

DEFINE_LOOKUP_BENCHMARK(Skiplist_RandBuild, GetRankOfItem, auto rank = zslGetRank(zsl, (*nodes)[idx]);
                        benchmark::DoNotOptimize(rank))

BENCHMARK_DEFINE_F(Skiplist_RandBuild, IterateForward)
(benchmark::State &state) {
    for (auto _ : state) {
        zskiplistIterator iter;
        zslInitIterator(&iter, zsl);
        zskiplistNode *node;
        unsigned long count = 0;
        while (zslNext(&iter, &node)) {
            count++;
            benchmark::DoNotOptimize(node);
        }
        benchmark::DoNotOptimize(count);
    }
    state.SetItemsProcessed(state.iterations() * item_count);
}

BENCHMARK_DEFINE_F(Skiplist_RandBuild, IterateBackward)
(benchmark::State &state) {
    for (auto _ : state) {
        zskiplistIterator iter;
        zslInitIterator(&iter, zsl);
        zslSeekToRank(&iter, item_count); /* Position at end */
        zskiplistNode *node;
        unsigned long count = 0;
        while (zslPrev(&iter, &node)) {
            count++;
            benchmark::DoNotOptimize(node);
        }
        benchmark::DoNotOptimize(count);
    }
    state.SetItemsProcessed(state.iterations() * item_count);
}

/* ============ Range Delete by Rank Benchmark ============ */

static void BM_Skiplist_RangeDeleteByRank(benchmark::State &state) {
    size_t count = state.range(0);
    size_t size = state.range(1);
    auto elems = createElements(count, size, false);

    zskiplist *zsl = zslCreate();
    for (size_t i = 0; i < count; i++)
        zslInsert(zsl, (double)i, elems[i]);

    size_t range_size = count / 10;
    unsigned long start_rank = (unsigned long)(count * 45 / 100);

    /* Pre-save scores and elements for restore */
    std::vector<double> saved_scores(range_size);
    std::vector<sds> saved_elems(range_size);
    for (size_t i = 0; i < range_size; i++) {
        saved_scores[i] = (double)(start_rank + i);
        saved_elems[i] = elems[start_rank + i];
    }

    /* zslDeleteRangeByRank uses 1-based inclusive ranks */
    unsigned long zsl_start = start_rank + 1;
    unsigned long zsl_end = start_rank + range_size;

    for (auto _ : state) {
        unsigned long deleted = zslDeleteRangeByRank(zsl, zsl_start, zsl_end, NULL);
        benchmark::DoNotOptimize(deleted);

        state.PauseTiming();
        /* Restore deleted elements */
        for (size_t i = 0; i < range_size; i++)
            zslInsert(zsl, saved_scores[i], saved_elems[i]);
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * range_size);
    zslFree(zsl);
    freeElements(elems);
}

/* ============ Range Delete by Score Benchmark ============ */

static void BM_Skiplist_RangeDeleteByScore(benchmark::State &state) {
    size_t count = state.range(0);
    size_t size = state.range(1);
    auto elems = createElements(count, size, false);

    zskiplist *zsl = zslCreate();
    for (size_t i = 0; i < count; i++)
        zslInsert(zsl, (double)i, elems[i]);

    size_t range_size = count / 10;
    size_t start_idx = count * 45 / 100;
    size_t end_idx = start_idx + range_size - 1;
    double min_score = (double)start_idx;
    double max_score = (double)end_idx;

    /* Pre-save scores and elements for restore */
    std::vector<double> saved_scores(range_size);
    std::vector<sds> saved_elems(range_size);
    for (size_t i = 0; i < range_size; i++) {
        saved_scores[i] = (double)(start_idx + i);
        saved_elems[i] = elems[start_idx + i];
    }

    for (auto _ : state) {
        zrangespec range = {min_score, max_score, 0, 0};
        unsigned long deleted = zslDeleteRangeByScore(zsl, &range, NULL);
        benchmark::DoNotOptimize(deleted);

        state.PauseTiming();
        /* Restore deleted elements */
        for (size_t i = 0; i < range_size; i++)
            zslInsert(zsl, saved_scores[i], saved_elems[i]);
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * range_size);
    zslFree(zsl);
    freeElements(elems);
}

/* ============ Partial Range Scan Benchmark ============ */

static void BM_Skiplist_PartialRangeScan(benchmark::State &state) {
    size_t count = state.range(0);
    size_t size = state.range(1);
    auto elems = createElements(count, size, false);

    zskiplist *zsl = zslCreate();
    for (size_t i = 0; i < count; i++)
        zslInsert(zsl, (double)i, elems[i]);

    std::mt19937 rng(123);
    for (auto _ : state) {
        size_t start = rng() % (count - 100);
        zrangespec range = {(double)start, (double)(count - 1), 0, 0};
        zskiplistNode *node = zslNthInRange(zsl, &range, 0, nullptr);
        benchmark::DoNotOptimize(node);

        zskiplistIterator iter;
        zslInitIterator(&iter, zsl);
        zslSeekToRank(&iter, zslGetRank(zsl, node));
        zskiplistNode *n;
        for (int i = 0; i < 99 && zslNext(&iter, &n); i++) {
            benchmark::DoNotOptimize(n);
        }
    }
    state.SetItemsProcessed(state.iterations() * 100);
    zslFree(zsl);
    freeElements(elems);
}

/* ============ Mixed Workload Benchmark ============ */

static void BM_Skiplist_MixedWorkload(benchmark::State &state) {
    size_t count = state.range(0);
    size_t size = state.range(1);
    int read_pct = (int)state.range(2);
    auto elems = createElements(count, size, false);

    zskiplist *zsl = zslCreate();
    std::vector<zskiplistNode *> nodes(count);
    std::vector<double> scores(count);
    for (size_t i = 0; i < count; i++) {
        scores[i] = (double)i;
        nodes[i] = zslInsert(zsl, scores[i], elems[i]);
    }

    /* Per cycle: read_pct lookups, remaining split 50/50 insert/delete */
    const int cycle_ops = 100;
    int num_reads = read_pct;
    int num_writes = cycle_ops - num_reads;
    int num_deletes = num_writes / 2;
    int num_inserts = num_writes - num_deletes;

    std::mt19937 rng(123);
    double next_score = (double)count;

    /* Pre-generate a shuffled delete order */
    std::vector<size_t> delete_order(count);
    std::iota(delete_order.begin(), delete_order.end(), 0);
    std::shuffle(delete_order.begin(), delete_order.end(), rng);
    size_t del_pos = 0;

    size_t ele_size = size - sizeof(double);
    std::vector<char> ele_buf(ele_size + 1);

    for (auto _ : state) {
        /* Lookups */
        for (int i = 0; i < num_reads; i++) {
            unsigned long rank = (rng() % count) + 1; /* 1-indexed */
            auto *node = zslGetElementByRank(zsl, rank);
            benchmark::DoNotOptimize(node);
        }

        /* Deletes */
        for (int i = 0; i < num_deletes; i++) {
            size_t idx = delete_order[del_pos++ % count];
            zslDelete(zsl, nodes[idx]);
            nodes[idx] = nullptr;
        }

        /* Inserts (to balance deletes and maintain stable size) */
        del_pos -= num_deletes; /* rewind to re-use same slots */
        for (int i = 0; i < num_inserts; i++) {
            size_t idx = delete_order[(del_pos + i) % count];
            snprintf(ele_buf.data(), ele_buf.size(), "%0*zu", (int)ele_size, idx);
            sds new_ele = sdsnewlen(ele_buf.data(), ele_size);
            double new_score = next_score++;
            nodes[idx] = zslInsert(zsl, new_score, new_ele);
            sdsfree(new_ele); /* zslInsert copies the element */
            scores[idx] = new_score;
        }
        del_pos += num_inserts;
    }
    state.SetItemsProcessed(state.iterations() * cycle_ops);
    zslFree(zsl);
    freeElements(elems);
}

/* ============ Score Update Benchmark ============ */

static void BM_Skiplist_ScoreUpdate(benchmark::State &state) {
    size_t count = state.range(0);
    size_t size = state.range(1);
    auto elems = createElements(count, size, false);

    zskiplist *zsl = zslCreate();
    std::vector<zskiplistNode *> nodes(count);
    std::vector<double> scores(count);
    for (size_t i = 0; i < count; i++) {
        scores[i] = (double)i;
        nodes[i] = zslInsert(zsl, scores[i], elems[i]);
    }

    std::vector<size_t> update_order(count);
    std::iota(update_order.begin(), update_order.end(), 0);
    std::mt19937 rng(42);
    std::shuffle(update_order.begin(), update_order.end(), rng);

    size_t pos = 0;
    for (auto _ : state) {
        size_t idx = update_order[pos++ % count];

        /* Save element sds before delete (node is freed, element in elems[] stays valid) */
        state.PauseTiming();
        sds ele = elems[idx];
        double new_score = (double)(rng() % count);
        state.ResumeTiming();

        zslDelete(zsl, nodes[idx]);
        nodes[idx] = zslInsert(zsl, new_score, ele);
        scores[idx] = new_score;
        benchmark::DoNotOptimize(nodes[idx]);
    }
    state.SetItemsProcessed(state.iterations());
    zslFree(zsl);
    freeElements(elems);
}

/* ============ Register Benchmarks ============ */

/* Insert benchmarks */
BENCHMARK(BM_Skiplist_Insert_Append)->BENCH_ARGS_EXTENDED();
BENCHMARK(BM_Skiplist_Insert_Prepend)->BENCH_ARGS_EXTENDED();
BENCHMARK(BM_Skiplist_Insert_Random)->BENCH_ARGS_EXTENDED()->Iterations(1);

/* Delete benchmarks */
BENCHMARK(BM_Skiplist_Delete_Random)->BENCH_ARGS_STANDARD();
BENCHMARK(BM_Skiplist_Delete_PopHead)->BENCH_ARGS_STANDARD();
BENCHMARK(BM_Skiplist_Delete_PopTail)->BENCH_ARGS_STANDARD();

/* SeqBuild lookup benchmarks */
BENCHMARK_REGISTER_F(Skiplist_SeqBuild, RankLookup)->BENCH_ARGS_STANDARD();
BENCHMARK_REGISTER_F(Skiplist_SeqBuild, SeekToScore)->BENCH_ARGS_STANDARD();
BENCHMARK_REGISTER_F(Skiplist_SeqBuild, GetRankOfItem)->BENCH_ARGS_STANDARD();
BENCHMARK_REGISTER_F(Skiplist_SeqBuild, IterateForward)->BENCH_ARGS_STANDARD()->Iterations(1);
BENCHMARK_REGISTER_F(Skiplist_SeqBuild, IterateBackward)->BENCH_ARGS_STANDARD()->Iterations(1);

/* RandBuild lookup benchmarks */
BENCHMARK_REGISTER_F(Skiplist_RandBuild, RankLookup)->BENCH_ARGS_STANDARD();
BENCHMARK_REGISTER_F(Skiplist_RandBuild, SeekToScore)->BENCH_ARGS_STANDARD();
BENCHMARK_REGISTER_F(Skiplist_RandBuild, GetRankOfItem)->BENCH_ARGS_STANDARD();
BENCHMARK_REGISTER_F(Skiplist_RandBuild, IterateForward)->BENCH_ARGS_STANDARD()->Iterations(1);
BENCHMARK_REGISTER_F(Skiplist_RandBuild, IterateBackward)->BENCH_ARGS_STANDARD()->Iterations(1);

/* Range delete benchmarks */
BENCHMARK(BM_Skiplist_RangeDeleteByRank)->BENCH_ARGS_STANDARD();
BENCHMARK(BM_Skiplist_RangeDeleteByScore)->BENCH_ARGS_STANDARD();

/* Partial range scan benchmark */
BENCHMARK(BM_Skiplist_PartialRangeScan)->BENCH_ARGS_STANDARD();

/* Mixed workload benchmark — BENCH_ARGS_STANDARD × {20, 50, 80} read percentages */
BENCHMARK(BM_Skiplist_MixedWorkload)
    ->Args({1024, 24, 20})
    ->Args({1024, 24, 50})
    ->Args({1024, 24, 80})
    ->Args({8192, 24, 20})
    ->Args({8192, 24, 50})
    ->Args({8192, 24, 80})
    ->Args({65536, 24, 20})
    ->Args({65536, 24, 50})
    ->Args({65536, 24, 80})
    ->Args({524288, 24, 20})
    ->Args({524288, 24, 50})
    ->Args({524288, 24, 80})
    ->Args({4194304, 24, 20})
    ->Args({4194304, 24, 50})
    ->Args({4194304, 24, 80});

/* Score update benchmark */
BENCHMARK(BM_Skiplist_ScoreUpdate)->BENCH_ARGS_STANDARD();
