/*
 * Fbtree Cold-Cache Benchmarks (Tier 2)
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
#include "fbtree_bench_decls.h"

#include <algorithm>
#include <deque>
#include <numeric>
#include <random>
#include <vector>

/* ============ Cold-Cache Data Structure ============ */

struct FbtreeColdCache {
    struct TreeData {
        fbtreeIndex *fbt;
        std::vector<sds> strs;
        std::vector<sds> items;
    };
    std::vector<TreeData> trees;
    std::vector<size_t> access_order;
    size_t item_count = 0;
    size_t item_size = 0;

    ~FbtreeColdCache() {
        for (auto &tree : trees) {
            fbtreeFree(tree.fbt);
            for (auto &str : tree.strs) sdsfree(str);
        }
    }
};

/* Separate caches for read-only vs mutating benchmarks to avoid interference */
static std::deque<FbtreeColdCache> g_fbtree_readonly_cache;
static std::deque<FbtreeColdCache> g_fbtree_delete_cache;
static std::deque<FbtreeColdCache> g_fbtree_insert_cache;
static std::deque<FbtreeColdCache> g_fbtree_pophead_cache;
static std::deque<FbtreeColdCache> g_fbtree_poptail_cache;

/* Clean up all caches at program exit */
static struct FbtreeColdCacheCleanup {
    ~FbtreeColdCacheCleanup() {
        g_fbtree_readonly_cache.clear();
        g_fbtree_delete_cache.clear();
        g_fbtree_insert_cache.clear();
        g_fbtree_pophead_cache.clear();
        g_fbtree_poptail_cache.clear();
    }
} g_fbtree_cold_cache_cleanup;

/* ============ Helper Functions ============ */

static std::vector<sds> createStrings(size_t count, size_t size) {
    std::vector<sds> strs(count);
    std::vector<char> buf(size);
    for (size_t i = 0; i < count; i++) {
        encodeScoreToBytes((double)i, (unsigned char *)buf.data());
        memset(buf.data() + 8, 'x', size - 8 - 1);
        buf[size - 1] = '\0';
        strs[i] = sdsnewlen(buf.data(), size);
    }
    return strs;
}

static FbtreeColdCache *findOrCreateCache(std::deque<FbtreeColdCache> &cache_store,
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
    c.trees.resize(n);
    std::mt19937 rng(42);

    for (size_t t = 0; t < n; t++) {
        c.trees[t].strs = createStrings(count, size);
        c.trees[t].fbt = fbtreeCreate();
        c.trees[t].items.resize(count);

        std::vector<size_t> order(count);
        std::iota(order.begin(), order.end(), 0);
        std::shuffle(order.begin(), order.end(), rng);

        for (size_t i = 0; i < count; i++)
            c.trees[t].items[order[i]] = fbtreeInsert(
                c.trees[t].fbt, sdsdup(c.trees[t].strs[order[i]]));
    }

    c.access_order.resize(n);
    std::iota(c.access_order.begin(), c.access_order.end(), 0);
    std::shuffle(c.access_order.begin(), c.access_order.end(), rng);

    fprintf(stderr, "Cold setup: %zu trees x %zu items = %zuMB\n",
            n, count, (n * struct_size) / (1024 * 1024));
    return &c;
}

/* ============ Read-Only Fixture ============ */

class Fbtree_Cold : public benchmark::Fixture {
  protected:
    FbtreeColdCache *cache = nullptr;
    size_t item_count = 0;
    size_t num_trees = 0;

  public:
    void SetUp(benchmark::State &state) override {
        item_count = state.range(0);
        cache = findOrCreateCache(g_fbtree_readonly_cache, item_count, state.range(1));
        num_trees = cache->trees.size();
    }

    void TearDown(benchmark::State &) override {
    }
};

/* ============ Read-Only Benchmarks ============ */

BENCHMARK_DEFINE_F(Fbtree_Cold, RankLookup)
(benchmark::State &state) {
    std::mt19937 rng(123);
    size_t tree_idx = 0;
    for (auto _ : state) {
        size_t t = cache->access_order[tree_idx++ % num_trees];
        size_t idx = rng() % item_count;
        auto s = fbtreeGetAtRank(cache->trees[t].fbt, idx);
        benchmark::DoNotOptimize(s);
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_DEFINE_F(Fbtree_Cold, SeekToScore)
(benchmark::State &state) {
    std::mt19937 rng(123);
    size_t tree_idx = 0;
    for (auto _ : state) {
        size_t t = cache->access_order[tree_idx++ % num_trees];
        size_t idx = rng() % item_count;
        fbtreeIterator iter;
        fbtreeSeekToScore(cache->trees[t].fbt, cache->trees[t].strs[idx], &iter);
        benchmark::DoNotOptimize(iter);
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_DEFINE_F(Fbtree_Cold, GetRankOfItem)
(benchmark::State &state) {
    std::mt19937 rng(123);
    size_t tree_idx = 0;
    for (auto _ : state) {
        size_t t = cache->access_order[tree_idx++ % num_trees];
        size_t idx = rng() % item_count;
        auto rank = fbtreeGetRankOfItem(cache->trees[t].fbt, cache->trees[t].items[idx]);
        benchmark::DoNotOptimize(rank);
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_DEFINE_F(Fbtree_Cold, IterateForward)
(benchmark::State &state) {
    size_t tree_idx = 0;
    for (auto _ : state) {
        size_t t = cache->access_order[tree_idx++ % num_trees];
        fbtreeIterator iter;
        fbtreeInitIterator(&iter, cache->trees[t].fbt);
        const_sds pos;
        unsigned long count = 0;
        while (fbtreeNext(&iter, &pos)) count++;
        benchmark::DoNotOptimize(count);
    }
    state.SetItemsProcessed(state.iterations() * item_count);
}

/* ============ Mutating Fixture Base ============ */

template <std::deque<FbtreeColdCache> &CacheStore>
class Fbtree_Cold_Mutating : public benchmark::Fixture {
  protected:
    FbtreeColdCache *cache = nullptr;
    size_t item_count = 0;
    size_t num_trees = 0;

  public:
    void SetUp(benchmark::State &state) override {
        item_count = state.range(0);
        cache = findOrCreateCache(CacheStore, item_count, state.range(1));
        num_trees = cache->trees.size();
    }

    void TearDown(benchmark::State &) override {
    }
};

/* ============ Insert Benchmarks ============ */

class Fbtree_Cold_Insert : public Fbtree_Cold_Mutating<g_fbtree_insert_cache> {};

BENCHMARK_DEFINE_F(Fbtree_Cold_Insert, Random)
(benchmark::State &state) {
    std::mt19937 rng(456);
    size_t tree_idx = 0;
    size_t insert_idx = 0;
    for (auto _ : state) {
        size_t t = cache->access_order[tree_idx++ % num_trees];
        /* Create unique item for this insert */
        double score = (double)(item_count + insert_idx++);
        std::vector<char> buf(cache->item_size);
        encodeScoreToBytes(score, (unsigned char *)buf.data());
        memset(buf.data() + 8, 'y', cache->item_size - 8 - 1);
        buf[cache->item_size - 1] = '\0';
        sds s = sdsnewlen(buf.data(), cache->item_size);
        auto item = fbtreeInsert(cache->trees[t].fbt, s);
        benchmark::DoNotOptimize(item);
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_DEFINE_F(Fbtree_Cold_Insert, Append)
(benchmark::State &state) {
    size_t tree_idx = 0;
    size_t insert_idx = 0;
    for (auto _ : state) {
        size_t t = cache->access_order[tree_idx++ % num_trees];
        /* Append: score higher than all existing */
        double score = (double)(item_count * 2 + insert_idx++);
        std::vector<char> buf(cache->item_size);
        encodeScoreToBytes(score, (unsigned char *)buf.data());
        memset(buf.data() + 8, 'y', cache->item_size - 8 - 1);
        buf[cache->item_size - 1] = '\0';
        sds s = sdsnewlen(buf.data(), cache->item_size);
        auto item = fbtreeInsert(cache->trees[t].fbt, s);
        benchmark::DoNotOptimize(item);
    }
    state.SetItemsProcessed(state.iterations());
}

/* ============ Delete Benchmark ============ */

class Fbtree_Cold_Delete : public Fbtree_Cold_Mutating<g_fbtree_delete_cache> {};

BENCHMARK_DEFINE_F(Fbtree_Cold_Delete, Random)
(benchmark::State &state) {
    std::mt19937 rng(789);
    size_t tree_idx = 0;
    for (auto _ : state) {
        size_t t = cache->access_order[tree_idx++ % num_trees];
        auto &tree = cache->trees[t];
        size_t idx = rng() % tree.items.size();
        if (tree.items[idx]) {
            fbtreeDelete(tree.fbt, tree.items[idx]);
            state.PauseTiming();
            tree.items[idx] = fbtreeInsert(tree.fbt, sdsdup(tree.strs[idx]));
            state.ResumeTiming();
        }
    }
    state.SetItemsProcessed(state.iterations());
}

/* ============ PopHead Benchmark ============ */

class Fbtree_Cold_PopHead : public Fbtree_Cold_Mutating<g_fbtree_pophead_cache> {};

BENCHMARK_DEFINE_F(Fbtree_Cold_PopHead, Pop)
(benchmark::State &state) {
    size_t tree_idx = 0;
    /* Track max score per tree for refill at tail */
    std::vector<double> max_scores(num_trees, (double)item_count);
    std::vector<char> buf(cache->item_size);
    for (auto _ : state) {
        size_t t = cache->access_order[tree_idx++ % num_trees];
        sds item = fbtreePopMin(cache->trees[t].fbt);
        if (item) {
            state.PauseTiming();
            /* Re-insert at tail to keep head cold */
            max_scores[t] += 1.0;
            encodeScoreToBytes(max_scores[t], (unsigned char *)buf.data());
            memset(buf.data() + 8, 'x', cache->item_size - 8 - 1);
            buf[cache->item_size - 1] = '\0';
            sdsfree(item);
            fbtreeInsert(cache->trees[t].fbt, sdsnewlen(buf.data(), cache->item_size));
            state.ResumeTiming();
        }
    }
    state.SetItemsProcessed(state.iterations());
}

/* ============ PopTail Benchmark ============ */

class Fbtree_Cold_PopTail : public Fbtree_Cold_Mutating<g_fbtree_poptail_cache> {};

BENCHMARK_DEFINE_F(Fbtree_Cold_PopTail, Pop)
(benchmark::State &state) {
    size_t tree_idx = 0;
    /* Track min score per tree for refill at head */
    std::vector<double> min_scores(num_trees, 0.0);
    std::vector<char> buf(cache->item_size);
    for (auto _ : state) {
        size_t t = cache->access_order[tree_idx++ % num_trees];
        sds item = fbtreePopMax(cache->trees[t].fbt);
        if (item) {
            state.PauseTiming();
            /* Re-insert at head to keep tail cold */
            min_scores[t] -= 1.0;
            encodeScoreToBytes(min_scores[t], (unsigned char *)buf.data());
            memset(buf.data() + 8, 'x', cache->item_size - 8 - 1);
            buf[cache->item_size - 1] = '\0';
            sdsfree(item);
            fbtreeInsert(cache->trees[t].fbt, sdsnewlen(buf.data(), cache->item_size));
            state.ResumeTiming();
        }
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
BENCHMARK_REGISTER_F(Fbtree_Cold, RankLookup)->COLD_BENCH_ARGS();
BENCHMARK_REGISTER_F(Fbtree_Cold, SeekToScore)->COLD_BENCH_ARGS();
BENCHMARK_REGISTER_F(Fbtree_Cold, GetRankOfItem)->COLD_BENCH_ARGS();
BENCHMARK_REGISTER_F(Fbtree_Cold, IterateForward)->COLD_BENCH_ARGS();

/* Mutating benchmarks - each uses its own cache */
BENCHMARK_REGISTER_F(Fbtree_Cold_Insert, Random)->COLD_BENCH_ARGS();
BENCHMARK_REGISTER_F(Fbtree_Cold_Insert, Append)->COLD_BENCH_ARGS();
BENCHMARK_REGISTER_F(Fbtree_Cold_Delete, Random)->COLD_BENCH_ARGS();
BENCHMARK_REGISTER_F(Fbtree_Cold_PopHead, Pop)->COLD_BENCH_ARGS();
BENCHMARK_REGISTER_F(Fbtree_Cold_PopTail, Pop)->COLD_BENCH_ARGS();
