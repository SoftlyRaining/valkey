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

    size_t idx = 0;
    for (auto _ : state) {
        bool found = hashtableFind(ht, keys[idx].c_str(), nullptr);
        benchmark::DoNotOptimize(found);
        idx = (idx + 1) % num_keys;
    }

    hashtableRelease(ht);
}



static void BM_HashtableFind_MemClobber(benchmark::State& state) {
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

    size_t idx = 0;
    for (auto _ : state) {
        bool found = hashtableFind(ht, keys[idx].c_str(), nullptr);
        benchmark::DoNotOptimize(found);
        idx = (idx + 1) % num_keys;
        benchmark::ClobberMemory();
    }

    hashtableRelease(ht);
}

BENCHMARK(BM_HashtableFind)
    ->Repetitions(5)
    ->MinTime(5.0);

BENCHMARK(BM_HashtableFind_MemClobber)
    ->Repetitions(5)
    ->MinTime(5.0);

BENCHMARK_MAIN();