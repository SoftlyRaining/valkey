#include <benchmark/benchmark.h>

#include <cstring>

#include "../common/config.h"
#include "../common/util.h"

extern "C" {
#include "dict.h"
}

static uint64_t hashCallback(const void *key) {
    return dictGenHashFunction((unsigned char *)key, strlen((char *)key));
}

static int dictCompareCallback(const void *key1, const void *key2) {
    return strcmp((char *)key1, (char *)key2) == 0;
}

static dictType BenchmarkDictType = {
    .hashFunction = hashCallback,
    .keyCompare = dictCompareCallback};

class DictFindFixture : public benchmark::Fixture {
  protected:
    dict *d = nullptr;
    std::unique_ptr<BenchmarkDataset> data;

  public:
    void SetUp(benchmark::State &state) override {
        int hit_percent = 100 - state.range(0);
        data = std::make_unique<BenchmarkDataset>(hit_percent, bench::item_count);
        d = dictCreate(&BenchmarkDictType);
        for (char *key : data->insert_ptrs) {
            dictAdd(d, key, nullptr);
        }
    }

    void TearDown(benchmark::State &) override {
        dictRelease(d);
    }
};

BENCHMARK_DEFINE_F(DictFindFixture, Find)
(benchmark::State &state) {
    size_t idx = 0;
    size_t num_keys = data->lookup_ptrs.size();
    for (auto _ : state) {
        dictEntry *entry = dictFind(d, data->lookup_ptrs[idx]);
        benchmark::DoNotOptimize(entry);
        idx = (idx + 1) % num_keys;
        benchmark::ClobberMemory();
    }
}

BENCHMARK_REGISTER_F(DictFindFixture, Find)
    ->Arg(0)
    ->Arg(50)
    ->Arg(100)
    ->Repetitions(5)
    ->MinTime(5.0);
