#include <benchmark/benchmark.h>

#include "../common/config.h"
#include "../common/util.h"

extern "C" {
#include "hashtable.h"
}

static hashtableType BenchmarkHashtableType = {.instant_rehashing = 1};

class HashtableFindFixture : public benchmark::Fixture {
protected:
    hashtable *ht = nullptr;
    std::unique_ptr<BenchmarkDataset> data;

public:
    void SetUp(benchmark::State &state) override {
        int hit_percent = 100 - state.range(0);
        data = std::make_unique<BenchmarkDataset>(hit_percent, bench::item_count);
        ht = hashtableCreate(&BenchmarkHashtableType);
        for (char *key : data->insert_ptrs) {
            hashtableAdd(ht, key);
        }
    }

    void TearDown(benchmark::State &) override {
        hashtableRelease(ht);
    }
};

BENCHMARK_DEFINE_F(HashtableFindFixture, Find)(benchmark::State &state) {
    size_t idx = 0;
    size_t num_keys = data->lookup_ptrs.size();
    for (auto _ : state) {
        bool found = hashtableFind(ht, data->lookup_ptrs[idx], nullptr);
        benchmark::DoNotOptimize(found);
        idx = (idx + 1) % num_keys;
        benchmark::ClobberMemory();
    }
}

BENCHMARK_REGISTER_F(HashtableFindFixture, Find)
    ->Arg(0)->Arg(50)->Arg(100)
    ->Repetitions(5)
    ->MinTime(5.0);
