#include <benchmark/benchmark.h>

#include <cstring>
#include <vector>

extern "C" {
#include "dict.h"
#include "hashtable.h"
}

static constexpr size_t kItemCount = 3'000'000;

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

// Pre-allocate items outside measured structure
static std::vector<std::vector<char>> generateItems(size_t item_size, size_t count) {
    std::vector<std::vector<char>> items(count);
    for (size_t i = 0; i < count; i++) {
        items[i].resize(item_size);
        snprintf(items[i].data(), item_size, "key_%zu", i);
    }
    return items;
}

static void BM_DictMemory(benchmark::State &state) {
    size_t item_size = state.range(0);
    auto items = generateItems(item_size, kItemCount);

    for (auto _ : state) {
        dict *d = dictCreate(&BenchmarkDictType);
        for (auto &item : items) {
            dictAdd(d, item.data(), nullptr);
        }

        state.counters["TotalBytes"] = dictMemUsage(d);
        state.counters["BytesPerItem"] = dictMemUsage(d) / static_cast<double>(kItemCount);

        dictRelease(d);
    }
}

static void BM_HashtableMemory(benchmark::State &state) {
    size_t item_size = state.range(0);
    auto items = generateItems(item_size, kItemCount);

    for (auto _ : state) {
        hashtable *ht = hashtableCreate(&BenchmarkHashtableType);
        for (auto &item : items) {
            hashtableAdd(ht, item.data());
        }

        state.counters["TotalBytes"] = hashtableMemUsage(ht);
        state.counters["BytesPerItem"] = hashtableMemUsage(ht) / static_cast<double>(kItemCount);

        hashtableRelease(ht);
    }
}

BENCHMARK(BM_DictMemory)->Args({16})->Args({64})->Args({512})->Iterations(1);
BENCHMARK(BM_HashtableMemory)->Args({16})->Args({64})->Args({512})->Iterations(1);
