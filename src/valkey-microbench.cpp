#include <benchmark/benchmark.h>

#include <cstring>
#include <string>
#include <random>
#include <vector>

extern "C" {
    #include "hashtable.h"
}

static void BM_HashtableFind(benchmark::State& state) {
    // Prepare a hashtable and insert some keys
    hashtableType type = {
        .instant_rehashing = 1
    };
    hashtable *ht = hashtableCreate(&type);

    const int num_keys = 16384;
    std::vector<std::string> keys;
    for (int i = 0; i < num_keys; ++i) {
        keys.push_back("key" + std::to_string(i));
        hashtableAdd(ht, (void *)keys.back().c_str());
    }

    // Random generator for key selection
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, num_keys - 1);

    for (auto _ : state) {
        int idx = dist(rng);
        bool found = hashtableFind(ht, keys[idx].c_str(), nullptr);
        benchmark::DoNotOptimize(found);
    }

    hashtableRelease(ht);
}
BENCHMARK(BM_HashtableFind)
    ->MinTime(5.0);

BENCHMARK_MAIN();