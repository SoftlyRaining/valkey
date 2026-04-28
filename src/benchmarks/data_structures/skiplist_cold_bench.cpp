/*
 * Skiplist Cold-Cache Benchmarks (Tier 2)
 *
 * Simulates realistic Valkey workload: many small sets where each
 * operation hits cold memory. Rotates through N structures so that
 * by the time we revisit a structure, it's been evicted from cache.
 *
 * Parameters: (item_count, item_size)
 *   item_count: 128 to 64K (small sets)
 *   item_size:  24B (typical)
 */

#include "bench_common.h"
#include "skiplist_bench_decls.h"

#include <algorithm>
#include <deque>
#include <numeric>
#include <random>
#include <vector>

/* ============ Cold-Cache Data Structure ============ */

struct SkiplistColdCache {
    struct ListData {
        zskiplist *zsl;
        std::vector<sds> elems;
        std::vector<zskiplistNode *> nodes;
        std::vector<double> scores; /* Store scores for refill */
    };
    std::vector<ListData> lists;
    std::vector<size_t> access_order;
    size_t item_count = 0;
    size_t item_size = 0;

    ~SkiplistColdCache() {
        for (auto &list : lists) {
            zslFree(list.zsl);
            for (auto &elem : list.elems) sdsfree(elem);
        }
    }
};

/* Separate caches for read-only vs mutating benchmarks to avoid interference */
static std::deque<SkiplistColdCache> g_skiplist_readonly_cache;
static std::deque<SkiplistColdCache> g_skiplist_delete_cache;
static std::deque<SkiplistColdCache> g_skiplist_insert_cache;
static std::deque<SkiplistColdCache> g_skiplist_pophead_cache;
static std::deque<SkiplistColdCache> g_skiplist_poptail_cache;
static std::deque<SkiplistColdCache> g_skiplist_rangedelrank_cache;
static std::deque<SkiplistColdCache> g_skiplist_rangedelscore_cache;
static std::deque<SkiplistColdCache> g_skiplist_mixed_cache;
static std::deque<SkiplistColdCache> g_skiplist_scoreupdate_cache;

/* Clean up all caches at program exit */
static struct SkiplistColdCacheCleanup {
    ~SkiplistColdCacheCleanup() {
        g_skiplist_readonly_cache.clear();
        g_skiplist_delete_cache.clear();
        g_skiplist_insert_cache.clear();
        g_skiplist_pophead_cache.clear();
        g_skiplist_poptail_cache.clear();
        g_skiplist_rangedelrank_cache.clear();
        g_skiplist_rangedelscore_cache.clear();
        g_skiplist_mixed_cache.clear();
        g_skiplist_scoreupdate_cache.clear();
    }
} g_skiplist_cold_cache_cleanup;

/* ============ Helper Functions ============ */

static std::vector<sds> createElements(size_t count, size_t item_size) {
    size_t ele_size = item_size - sizeof(double);
    std::vector<sds> elems(count);
    std::vector<char> buf(ele_size + 1);
    for (size_t i = 0; i < count; i++) {
        snprintf(buf.data(), buf.size(), "%0*zu", (int)ele_size, i);
        elems[i] = sdsnewlen(buf.data(), ele_size);
    }
    return elems;
}

static SkiplistColdCache *findOrCreateCache(std::deque<SkiplistColdCache> &cache_store,
                                            size_t count,
                                            size_t size) {
    for (auto &c : cache_store)
        if (c.item_count == count && c.item_size == size) return &c;

    cache_store.emplace_back();
    auto &c = cache_store.back();
    c.item_count = count;
    c.item_size = size;

    size_t struct_size = count * size;
    size_t n = calcColdCacheStructureCount(struct_size);
    c.lists.resize(n);
    std::mt19937 rng(42);

    for (size_t t = 0; t < n; t++) {
        c.lists[t].elems = createElements(count, size);
        c.lists[t].zsl = zslCreate();
        c.lists[t].nodes.resize(count);
        c.lists[t].scores.resize(count);

        std::vector<size_t> order(count);
        std::iota(order.begin(), order.end(), 0);
        std::shuffle(order.begin(), order.end(), rng);

        for (size_t i = 0; i < count; i++) {
            double score = (double)order[i];
            c.lists[t].scores[order[i]] = score;
            c.lists[t].nodes[order[i]] = zslInsert(
                c.lists[t].zsl, score, c.lists[t].elems[order[i]]);
        }
    }

    c.access_order.resize(n);
    std::iota(c.access_order.begin(), c.access_order.end(), 0);
    std::shuffle(c.access_order.begin(), c.access_order.end(), rng);

    fprintf(stderr, "Cold setup: %zu lists x %zu items = %zuMB\n",
            n, count, (n * struct_size) / (1024 * 1024));
    return &c;
}

/* ============ Read-Only Fixture ============ */

class Skiplist_Cold : public benchmark::Fixture {
  protected:
    SkiplistColdCache *cache = nullptr;
    size_t item_count = 0;
    size_t num_lists = 0;

  public:
    void SetUp(benchmark::State &state) override {
        item_count = state.range(0);
        cache = findOrCreateCache(g_skiplist_readonly_cache, item_count, state.range(1));
        num_lists = cache->lists.size();
    }

    void TearDown(benchmark::State &) override {
    }
};

/* ============ Read-Only Benchmarks ============ */

BENCHMARK_DEFINE_F(Skiplist_Cold, RankLookup)
(benchmark::State &state) {
    std::mt19937 rng(123);
    size_t list_idx = 0;
    for (auto _ : state) {
        size_t t = cache->access_order[list_idx++ % num_lists];
        size_t idx = rng() % item_count;
        auto *node = zslGetElementByRank(cache->lists[t].zsl, idx + 1);
        benchmark::DoNotOptimize(node);
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_DEFINE_F(Skiplist_Cold, SeekToScore)
(benchmark::State &state) {
    std::mt19937 rng(123);
    size_t list_idx = 0;
    for (auto _ : state) {
        size_t t = cache->access_order[list_idx++ % num_lists];
        size_t idx = rng() % item_count;
        zrangespec range = {(double)idx, (double)idx, 0, 0};
        auto *node = zslNthInRange(cache->lists[t].zsl, &range, 0, nullptr);
        benchmark::DoNotOptimize(node);
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_DEFINE_F(Skiplist_Cold, GetRankOfItem)
(benchmark::State &state) {
    std::mt19937 rng(123);
    size_t list_idx = 0;
    for (auto _ : state) {
        size_t t = cache->access_order[list_idx++ % num_lists];
        size_t idx = rng() % item_count;
        auto rank = zslGetRank(cache->lists[t].zsl, cache->lists[t].nodes[idx]);
        benchmark::DoNotOptimize(rank);
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_DEFINE_F(Skiplist_Cold, IterateForward)
(benchmark::State &state) {
    size_t list_idx = 0;
    for (auto _ : state) {
        size_t t = cache->access_order[list_idx++ % num_lists];
        zskiplistIterator iter;
        zslInitIterator(&iter, cache->lists[t].zsl);
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

/* ============ Mutating Fixture Base ============ */

template <std::deque<SkiplistColdCache> &CacheStore>
class Skiplist_Cold_Mutating : public benchmark::Fixture {
  protected:
    SkiplistColdCache *cache = nullptr;
    size_t item_count = 0;
    size_t num_lists = 0;

  public:
    void SetUp(benchmark::State &state) override {
        item_count = state.range(0);
        cache = findOrCreateCache(CacheStore, item_count, state.range(1));
        num_lists = cache->lists.size();
    }

    void TearDown(benchmark::State &) override {
    }
};

/* ============ Insert Benchmarks ============ */

class Skiplist_Cold_Insert : public Skiplist_Cold_Mutating<g_skiplist_insert_cache> {};

BENCHMARK_DEFINE_F(Skiplist_Cold_Insert, Random)
(benchmark::State &state) {
    std::mt19937 rng(456);
    size_t list_idx = 0;
    size_t insert_idx = 0;
    size_t ele_size = cache->item_size - sizeof(double);
    for (auto _ : state) {
        size_t t = cache->access_order[list_idx++ % num_lists];
        double score = (double)(item_count + insert_idx);
        std::vector<char> buf(ele_size + 1);
        snprintf(buf.data(), buf.size(), "%0*zu", (int)ele_size, item_count + insert_idx);
        insert_idx++;
        sds elem = sdsnewlen(buf.data(), ele_size);
        auto *node = zslInsert(cache->lists[t].zsl, score, elem);
        sdsfree(elem);
        benchmark::DoNotOptimize(node);
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_DEFINE_F(Skiplist_Cold_Insert, Append)
(benchmark::State &state) {
    size_t list_idx = 0;
    size_t insert_idx = 0;
    size_t ele_size = cache->item_size - sizeof(double);
    for (auto _ : state) {
        size_t t = cache->access_order[list_idx++ % num_lists];
        double score = (double)(item_count * 2 + insert_idx);
        std::vector<char> buf(ele_size + 1);
        snprintf(buf.data(), buf.size(), "%0*zu", (int)ele_size, item_count * 2 + insert_idx);
        insert_idx++;
        sds elem = sdsnewlen(buf.data(), ele_size);
        auto *node = zslInsert(cache->lists[t].zsl, score, elem);
        sdsfree(elem);
        benchmark::DoNotOptimize(node);
    }
    state.SetItemsProcessed(state.iterations());
}

/* ============ Delete Benchmark ============ */

class Skiplist_Cold_Delete : public Skiplist_Cold_Mutating<g_skiplist_delete_cache> {};

BENCHMARK_DEFINE_F(Skiplist_Cold_Delete, Random)
(benchmark::State &state) {
    std::mt19937 rng(789);
    size_t list_idx = 0;
    for (auto _ : state) {
        size_t t = cache->access_order[list_idx++ % num_lists];
        auto &list = cache->lists[t];
        size_t idx = rng() % list.nodes.size();
        if (list.nodes[idx]) {
            zslDelete(list.zsl, list.nodes[idx]);
            state.PauseTiming();
            list.nodes[idx] = zslInsert(list.zsl, list.scores[idx], list.elems[idx]);
            state.ResumeTiming();
        }
    }
    state.SetItemsProcessed(state.iterations());
}

/* ============ PopHead Benchmark ============ */

class Skiplist_Cold_PopHead : public Skiplist_Cold_Mutating<g_skiplist_pophead_cache> {};

BENCHMARK_DEFINE_F(Skiplist_Cold_PopHead, Pop)
(benchmark::State &state) {
    size_t list_idx = 0;
    /* Track max score per list for refill at tail */
    std::vector<double> max_scores(num_lists, (double)item_count);
    for (auto _ : state) {
        size_t t = cache->access_order[list_idx++ % num_lists];
        auto *head = zslGetFirst(cache->lists[t].zsl);
        if (head) {
            zslDetachNode(cache->lists[t].zsl, head);
            state.PauseTiming();
            /* Re-insert at tail to keep head cold */
            max_scores[t] += 1.0;
            sds ele = sdsdup(zslGetNodeElement(head));
            zslInsert(cache->lists[t].zsl, max_scores[t], ele);
            sdsfree(ele); /* zslInsert copies the element, so free our copy */
            zslFreeNode(head);
            state.ResumeTiming();
        }
    }
    state.SetItemsProcessed(state.iterations());
}

/* ============ PopTail Benchmark ============ */

class Skiplist_Cold_PopTail : public Skiplist_Cold_Mutating<g_skiplist_poptail_cache> {};

BENCHMARK_DEFINE_F(Skiplist_Cold_PopTail, Pop)
(benchmark::State &state) {
    size_t list_idx = 0;
    /* Track min score per list for refill at head */
    std::vector<double> min_scores(num_lists, 0.0);
    for (auto _ : state) {
        size_t t = cache->access_order[list_idx++ % num_lists];
        auto *tail = zslGetTail(cache->lists[t].zsl);
        if (tail) {
            zslDetachNode(cache->lists[t].zsl, tail);
            state.PauseTiming();
            /* Re-insert at head to keep tail cold */
            min_scores[t] -= 1.0;
            sds ele = sdsdup(zslGetNodeElement(tail));
            zslInsert(cache->lists[t].zsl, min_scores[t], ele);
            sdsfree(ele); /* zslInsert copies the element, so free our copy */
            zslFreeNode(tail);
            state.ResumeTiming();
        }
    }
    state.SetItemsProcessed(state.iterations());
}

/* ============ Range Delete by Rank Benchmark ============ */

class Skiplist_Cold_RangeDeleteRank : public Skiplist_Cold_Mutating<g_skiplist_rangedelrank_cache> {};

BENCHMARK_DEFINE_F(Skiplist_Cold_RangeDeleteRank, Op)
(benchmark::State &state) {
    size_t list_idx = 0;
    size_t range_size = item_count / 10;
    unsigned long start_rank = (unsigned long)(item_count * 45 / 100);
    /* zslDeleteRangeByRank uses 1-based inclusive ranks */
    unsigned long zsl_start = start_rank + 1;
    unsigned long zsl_end = start_rank + range_size;
    for (auto _ : state) {
        size_t t = cache->access_order[list_idx++ % num_lists];
        auto &list = cache->lists[t];

        unsigned long deleted = zslDeleteRangeByRank(list.zsl, zsl_start, zsl_end, NULL);
        benchmark::DoNotOptimize(deleted);

        state.PauseTiming();
        /* Restore deleted elements */
        for (size_t i = 0; i < range_size; i++) {
            size_t idx = start_rank + i;
            list.nodes[idx] = zslInsert(list.zsl, list.scores[idx], list.elems[idx]);
        }
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * range_size);
}

/* ============ Range Delete by Score Benchmark ============ */

class Skiplist_Cold_RangeDeleteScore : public Skiplist_Cold_Mutating<g_skiplist_rangedelscore_cache> {};

BENCHMARK_DEFINE_F(Skiplist_Cold_RangeDeleteScore, Op)
(benchmark::State &state) {
    size_t list_idx = 0;
    size_t range_size = item_count / 10;
    size_t start_idx = item_count * 45 / 100;
    size_t end_idx = start_idx + range_size - 1;
    double min_score = (double)start_idx;
    double max_score = (double)end_idx;

    for (auto _ : state) {
        size_t t = cache->access_order[list_idx++ % num_lists];
        auto &list = cache->lists[t];

        zrangespec range = {min_score, max_score, 0, 0};
        unsigned long deleted = zslDeleteRangeByScore(list.zsl, &range, NULL);
        benchmark::DoNotOptimize(deleted);

        state.PauseTiming();
        /* Restore deleted elements */
        for (size_t i = 0; i < range_size; i++) {
            size_t idx = start_idx + i;
            list.nodes[idx] = zslInsert(list.zsl, list.scores[idx], list.elems[idx]);
        }
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * range_size);
}

/* ============ Partial Range Scan Benchmark ============ */

BENCHMARK_DEFINE_F(Skiplist_Cold, PartialScan)
(benchmark::State &state) {
    std::mt19937 rng(123);
    size_t list_idx = 0;
    for (auto _ : state) {
        size_t t = cache->access_order[list_idx++ % num_lists];
        size_t start = rng() % (item_count - 100);
        zrangespec range = {(double)start, (double)(item_count - 1), 0, 0};
        zskiplistNode *node = zslNthInRange(cache->lists[t].zsl, &range, 0, nullptr);
        benchmark::DoNotOptimize(node);

        zskiplistIterator iter;
        zslInitIterator(&iter, cache->lists[t].zsl);
        zslSeekToRank(&iter, zslGetRank(cache->lists[t].zsl, node));
        zskiplistNode *n;
        for (int i = 0; i < 99 && zslNext(&iter, &n); i++) {
            benchmark::DoNotOptimize(n);
        }
    }
    state.SetItemsProcessed(state.iterations() * 100);
}

/* ============ Mixed Workload Benchmark ============ */

class Skiplist_Cold_Mixed : public Skiplist_Cold_Mutating<g_skiplist_mixed_cache> {};

BENCHMARK_DEFINE_F(Skiplist_Cold_Mixed, Op)
(benchmark::State &state) {
    const int cycle_ops = 100;
    int num_reads = 50;
    int num_writes = cycle_ops - num_reads;
    int num_deletes = num_writes / 2;
    int num_inserts = num_writes - num_deletes;

    std::mt19937 rng(123);
    size_t list_idx = 0;
    size_t ele_size = cache->item_size - sizeof(double);
    std::vector<char> ele_buf(ele_size + 1);

    for (auto _ : state) {
        size_t t = cache->access_order[list_idx++ % num_lists];
        auto &list = cache->lists[t];

        /* Lookups */
        for (int i = 0; i < num_reads; i++) {
            unsigned long rank = (rng() % item_count) + 1; /* 1-indexed */
            auto *node = zslGetElementByRank(list.zsl, rank);
            benchmark::DoNotOptimize(node);
        }

        /* Deletes */
        std::vector<size_t> del_indices(num_deletes);
        for (int i = 0; i < num_deletes; i++) {
            size_t idx = rng() % item_count;
            del_indices[i] = idx;
            if (list.nodes[idx]) {
                zslDelete(list.zsl, list.nodes[idx]);
                list.nodes[idx] = nullptr;
            }
        }

        /* Inserts to balance deletes */
        double next_score = (double)(item_count * 2);
        for (int i = 0; i < num_inserts; i++) {
            snprintf(ele_buf.data(), ele_buf.size(), "%0*d", (int)ele_size, (int)(item_count * 2 + i));
            sds elem = sdsnewlen(ele_buf.data(), ele_size);
            zslInsert(list.zsl, next_score + i, elem);
            sdsfree(elem); /* zslInsert copies the element */
        }

        state.PauseTiming();
        /* Restore: rebuild the list from scratch to ensure clean state */
        zslFree(list.zsl);
        list.zsl = zslCreate();
        for (size_t i = 0; i < item_count; i++)
            list.nodes[i] = zslInsert(list.zsl, list.scores[i], list.elems[i]);
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * cycle_ops);
}

/* ============ Score Update Benchmark ============ */

class Skiplist_Cold_ScoreUpdate : public Skiplist_Cold_Mutating<g_skiplist_scoreupdate_cache> {};

BENCHMARK_DEFINE_F(Skiplist_Cold_ScoreUpdate, Op)
(benchmark::State &state) {
    std::mt19937 rng(42);
    size_t list_idx = 0;
    for (auto _ : state) {
        size_t t = cache->access_order[list_idx++ % num_lists];
        auto &list = cache->lists[t];
        size_t idx = rng() % item_count;

        /* Prepare new score during paused timing */
        state.PauseTiming();
        double new_score = (double)(rng() % item_count);
        state.ResumeTiming();

        zslDelete(list.zsl, list.nodes[idx]);
        list.nodes[idx] = zslInsert(list.zsl, new_score, list.elems[idx]);
        list.scores[idx] = new_score;
        benchmark::DoNotOptimize(list.nodes[idx]);
    }
    state.SetItemsProcessed(state.iterations());
}

/* ============ Register Benchmarks ============ */

#define COLD_BENCH_ARGS()  \
    Args({128, 24})        \
        ->Args({1024, 24}) \
        ->Args({8192, 24}) \
        ->Args({65536, 24})

/* Read-only benchmarks */
BENCHMARK_REGISTER_F(Skiplist_Cold, RankLookup)->COLD_BENCH_ARGS();
BENCHMARK_REGISTER_F(Skiplist_Cold, SeekToScore)->COLD_BENCH_ARGS();
BENCHMARK_REGISTER_F(Skiplist_Cold, GetRankOfItem)->COLD_BENCH_ARGS();
BENCHMARK_REGISTER_F(Skiplist_Cold, IterateForward)->COLD_BENCH_ARGS();
BENCHMARK_REGISTER_F(Skiplist_Cold, PartialScan)->COLD_BENCH_ARGS();

/* Mutating benchmarks - each uses its own cache */
BENCHMARK_REGISTER_F(Skiplist_Cold_Insert, Random)->COLD_BENCH_ARGS();
BENCHMARK_REGISTER_F(Skiplist_Cold_Insert, Append)->COLD_BENCH_ARGS();
BENCHMARK_REGISTER_F(Skiplist_Cold_Delete, Random)->COLD_BENCH_ARGS();
BENCHMARK_REGISTER_F(Skiplist_Cold_PopHead, Pop)->COLD_BENCH_ARGS();
BENCHMARK_REGISTER_F(Skiplist_Cold_PopTail, Pop)->COLD_BENCH_ARGS();
BENCHMARK_REGISTER_F(Skiplist_Cold_RangeDeleteRank, Op)->COLD_BENCH_ARGS();
BENCHMARK_REGISTER_F(Skiplist_Cold_RangeDeleteScore, Op)->COLD_BENCH_ARGS();
BENCHMARK_REGISTER_F(Skiplist_Cold_Mixed, Op)->COLD_BENCH_ARGS();
BENCHMARK_REGISTER_F(Skiplist_Cold_ScoreUpdate, Op)->COLD_BENCH_ARGS();
