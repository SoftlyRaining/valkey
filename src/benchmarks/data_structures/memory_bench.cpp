/*
 * Memory Overhead Benchmarks
 *
 * Naming: BM_{DataStructure}_Mem_{Scenario}_{Variation}
 *
 * Allows filtering by:
 * - Data structure: Fbtree, Skiplist, Dict, Hashtable
 * - Benchmark type: Mem (memory), Perf (performance)
 * - Scenario: BySize, ByCount
 * - Variation: Append, Prepend, Random
 */

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstring>
#include <numeric>
#include <random>
#include <vector>

extern "C" {
#include "dict.h"
#include "hashtable.h"
#include "zmalloc.h"
#include "fbtree_ordered_index.h"

/* Forward declare sds functions (types come from fbtree_ordered_index.h) */
sds sdsnewlen(const void *init, size_t initlen);
void sdsfree(sds s);

typedef struct zskiplistNode zskiplistNode;
typedef struct zskiplist {
    struct zskiplistNode *header, *tail;
    unsigned long length;
    int level;
} zskiplist;

zskiplist *zslCreate(void);
void zslFree(zskiplist *zsl);
zskiplistNode *zslInsert(zskiplist *zsl, double score, const_sds ele);

unsigned char *lpNew(size_t capacity);
void lpFree(unsigned char *lp);
unsigned char *lpAppend(unsigned char *lp, unsigned char *s, uint32_t slen);
unsigned char *lpAppendInteger(unsigned char *lp, long long lval);
size_t lpBytes(unsigned char *lp);
}

/* ============ Constants & Helpers ============ */

static constexpr size_t kItemCount = 10'000;
static constexpr size_t kFixedItemSize = 32;

static uint64_t hashCallback(const void *key) {
    return dictGenHashFunction((unsigned char *)key, strlen((char *)key));
}

static int dictCompareCallback(const void *key1, const void *key2) {
    return strcmp((char *)key1, (char *)key2) == 0;
}

static dictType BenchmarkDictType = {
    .hashFunction = hashCallback,
    .keyCompare = dictCompareCallback,
};

static hashtableType BenchmarkHashtableType = {.instant_rehashing = 1};

enum class InsertOrder { Append,
                         Prepend,
                         Random };

static std::vector<std::vector<char>> generateItems(size_t item_size, size_t count, InsertOrder order = InsertOrder::Append) {
    std::vector<std::vector<char>> items(count);
    std::vector<size_t> indices(count);
    std::iota(indices.begin(), indices.end(), 0);

    if (order == InsertOrder::Prepend) {
        std::reverse(indices.begin(), indices.end());
    } else if (order == InsertOrder::Random) {
        std::mt19937 rng(42);
        std::shuffle(indices.begin(), indices.end(), rng);
    }

    for (size_t i = 0; i < count; i++) {
        items[i].resize(item_size);
        int prefix_len = snprintf(items[i].data(), item_size, "%09zu_", indices[i]);
        memset(items[i].data() + prefix_len, 'x', item_size - prefix_len - 1);
        items[i][item_size - 1] = '\0';
    }
    return items;
}

/* ============ Dict/Hashtable Benchmarks ============ */

static void BM_Dict_Mem_BySize(benchmark::State &state) {
    size_t item_size = state.range(0);
    auto items = generateItems(item_size, kItemCount);

    for (auto _ : state) {
        dict *d = dictCreate(&BenchmarkDictType);
        for (auto &item : items) dictAdd(d, item.data(), nullptr);
        state.counters["TotalBytes"] = dictMemUsage(d);
        state.counters["BytesPerItem"] = dictMemUsage(d) / static_cast<double>(kItemCount);
        dictRelease(d);
    }
}

static void BM_Hashtable_Mem_BySize(benchmark::State &state) {
    size_t item_size = state.range(0);
    auto items = generateItems(item_size, kItemCount);

    for (auto _ : state) {
        hashtable *ht = hashtableCreate(&BenchmarkHashtableType);
        for (auto &item : items) hashtableAdd(ht, item.data());
        state.counters["TotalBytes"] = hashtableMemUsage(ht);
        state.counters["BytesPerItem"] = hashtableMemUsage(ht) / static_cast<double>(kItemCount);
        hashtableRelease(ht);
    }
}

/* ============ Skiplist Benchmarks ============ */

template <InsertOrder Order>
static void BM_Skiplist_Mem_ByCount(benchmark::State &state) {
    size_t count = state.range(0);
    constexpr size_t item_size = 24;
    size_t ele_size = item_size - sizeof(double);
    auto items = generateItems(ele_size, count, Order);

    for (auto _ : state) {
        size_t mem_before = zmalloc_used_memory();
        zskiplist *zsl = zslCreate();
        for (size_t i = 0; i < count; i++) {
            sds ele = sdsnewlen(items[i].data(), ele_size - 1);
            zslInsert(zsl, (double)i, ele);
            sdsfree(ele);
        }
        size_t total_bytes = zmalloc_used_memory() - mem_before;
        size_t overhead = total_bytes - item_size * count;
        state.counters["TotalBytes"] = overhead;
        state.counters["BytesPerItem"] = count ? overhead / static_cast<double>(count) : 0;
        zslFree(zsl);
    }
}

/* ============ Fbtree Benchmarks ============ */

template <InsertOrder Order>
static void BM_Fbtree_Mem_ByCount(benchmark::State &state) {
    size_t count = state.range(0);
    constexpr size_t item_size = 24;
    auto items = generateItems(item_size, count, Order);

    for (auto _ : state) {
        size_t mem_before = zmalloc_used_memory();
        fbtreeIndex *fbt = fbtreeCreate();
        for (size_t i = 0; i < count; i++) {
            sds ss = sdsnewlen(items[i].data(), item_size);
            fbtreeInsert(fbt, ss);
        }
        size_t total_bytes = zmalloc_used_memory() - mem_before;
        size_t overhead = total_bytes - item_size * count;
        state.counters["TotalBytes"] = overhead;
        state.counters["BytesPerItem"] = count ? overhead / static_cast<double>(count) : 0;
        fbtreeFree(fbt);
    }
}

/* ============ Full ZSET Memory Benchmarks ============ */

static void BM_Zset_Listpack_Mem_ByCount(benchmark::State &state) {
    size_t count = state.range(0);
    size_t ele_size = kFixedItemSize - sizeof(double);
    auto items = generateItems(ele_size, count, InsertOrder::Append);

    for (auto _ : state) {
        unsigned char *lp = lpNew(256);
        for (size_t i = 0; i < count; i++) {
            lp = lpAppend(lp, (unsigned char *)items[i].data(), ele_size - 1);
            lp = lpAppendInteger(lp, (long long)i); // score as integer
        }
        size_t total = lpBytes(lp);
        state.counters["TotalBytes"] = total;
        state.counters["BytesPerItem"] = count ? total / static_cast<double>(count) : 0;
        lpFree(lp);
    }
}

static void BM_Zset_Skiplist_Mem_ByCount(benchmark::State &state) {
    size_t count = state.range(0);
    size_t ele_size = kFixedItemSize - sizeof(double);
    auto items = generateItems(ele_size, count, InsertOrder::Random);

    for (auto _ : state) {
        size_t mem_before = zmalloc_used_memory();
        zskiplist *zsl = zslCreate();
        hashtable *ht = hashtableCreate(&BenchmarkHashtableType);
        for (size_t i = 0; i < count; i++) {
            sds ele = sdsnewlen(items[i].data(), ele_size - 1);
            zslInsert(zsl, (double)i, ele);
            hashtableAdd(ht, ele);
        }
        size_t total = zmalloc_used_memory() - mem_before;
        state.counters["TotalBytes"] = total;
        state.counters["BytesPerItem"] = count ? total / static_cast<double>(count) : 0;
        hashtableRelease(ht);
        zslFree(zsl);
    }
}

static void BM_Zset_Fbtree_Mem_ByCount(benchmark::State &state) {
    size_t count = state.range(0);
    auto items = generateItems(kFixedItemSize, count, InsertOrder::Random);

    for (auto _ : state) {
        size_t mem_before = zmalloc_used_memory();
        fbtreeIndex *fbt = fbtreeCreate();
        hashtable *ht = hashtableCreate(&BenchmarkHashtableType);
        for (size_t i = 0; i < count; i++) {
            sds ss = sdsnewlen(items[i].data(), kFixedItemSize);
            fbtreeInsert(fbt, ss);
            hashtableAdd(ht, (void *)ss);
        }
        size_t total = zmalloc_used_memory() - mem_before;
        state.counters["TotalBytes"] = total;
        state.counters["BytesPerItem"] = count ? total / static_cast<double>(count) : 0;
        hashtableRelease(ht);
        fbtreeFree(fbt);
    }
}

/* ============ Fbtree Mixed Workload (Steady-State) Benchmark ============ */

// Multiplier for warmup ops relative to item count (e.g., 10 = 10x item count)
static constexpr size_t kMixedWorkloadChurnFactor = 10;

static void BM_Fbtree_Mem_MixedWorkload(benchmark::State &state) {
    size_t count = state.range(0);
    constexpr size_t item_size = 24;
    size_t warmup_ops = count * kMixedWorkloadChurnFactor;

    // Generate enough items for initial fill + warmup inserts (50% of warmup are inserts)
    size_t total_items = count + warmup_ops / 2 + 1000;
    auto items = generateItems(item_size, total_items, InsertOrder::Random);
    std::mt19937 rng(42);

    for (auto _ : state) {
        // Build initial tree
        size_t mem_before = zmalloc_used_memory();
        fbtreeIndex *fbt = fbtreeCreate();
        std::vector<sds> live_items; // stores pointers returned by fbtreeInsert
        live_items.reserve(total_items);

        for (size_t i = 0; i < count; i++) {
            sds ss = sdsnewlen(items[i].data(), item_size);
            sds stored = fbtreeInsert(fbt, ss); // fbtree takes ownership, returns stored ptr
            live_items.push_back(stored);
        }

        size_t next_insert = count; // next item index to insert

        // Run mixed workload: 50% insert, 50% delete
        for (size_t op = 0; op < warmup_ops; op++) {
            if (rng() % 2 == 0 && next_insert < items.size()) {
                // Insert
                sds ss = sdsnewlen(items[next_insert].data(), item_size);
                sds stored = fbtreeInsert(fbt, ss);
                live_items.push_back(stored);
                next_insert++;
            } else if (!live_items.empty()) {
                // Delete random item
                size_t idx = rng() % live_items.size();
                fbtreeDelete(fbt, live_items[idx]); // fbtree frees the sds
                live_items[idx] = live_items.back();
                live_items.pop_back();
            }
        }

        // Measure memory at steady state
        size_t live_count = live_items.size();
        size_t total_bytes = zmalloc_used_memory() - mem_before;
        size_t data_bytes = item_size * live_count;
        size_t overhead = total_bytes > data_bytes ? total_bytes - data_bytes : 0;

        state.counters["TotalBytes"] = overhead;
        state.counters["BytesPerItem"] = live_count ? overhead / static_cast<double>(live_count) : 0;
        state.counters["LiveItems"] = live_count;

        fbtreeFree(fbt); // frees all remaining items
    }
}

/* ============ Register Benchmarks ============ */

static void ZsetCountArgs(::benchmark::Benchmark *b) {
    for (int64_t n = 8; n <= 512; n *= 2) b->Args({n});
}

static void StandardCountArgs(::benchmark::Benchmark *b) {
    for (int64_t n = 8; n <= 262'144; n *= 2) b->Args({n});
}

BENCHMARK(BM_Dict_Mem_BySize)->Args({16})->Args({64})->Args({512})->Iterations(1);
BENCHMARK(BM_Hashtable_Mem_BySize)->Args({16})->Args({64})->Args({512})->Iterations(1);

BENCHMARK(BM_Skiplist_Mem_ByCount<InsertOrder::Append>)->Name("BM_Skiplist_Mem_ByCount_Append")->Apply(StandardCountArgs)->Iterations(1);
BENCHMARK(BM_Skiplist_Mem_ByCount<InsertOrder::Random>)->Name("BM_Skiplist_Mem_ByCount_Random")->Apply(StandardCountArgs)->Iterations(1);

BENCHMARK(BM_Fbtree_Mem_ByCount<InsertOrder::Append>)->Name("BM_Fbtree_Mem_ByCount_Append")->Apply(StandardCountArgs)->Iterations(1);
BENCHMARK(BM_Fbtree_Mem_ByCount<InsertOrder::Random>)->Name("BM_Fbtree_Mem_ByCount_Random")->Apply(StandardCountArgs)->Iterations(1);
BENCHMARK(BM_Fbtree_Mem_MixedWorkload)->Apply(StandardCountArgs)->Iterations(1);

BENCHMARK(BM_Zset_Listpack_Mem_ByCount)->Apply(ZsetCountArgs)->Iterations(1);
BENCHMARK(BM_Zset_Skiplist_Mem_ByCount)->Apply(ZsetCountArgs)->Iterations(1);
BENCHMARK(BM_Zset_Fbtree_Mem_ByCount)->Apply(ZsetCountArgs)->Iterations(1);
