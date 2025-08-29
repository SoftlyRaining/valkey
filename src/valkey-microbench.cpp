#include <benchmark/benchmark.h>

#include <cstring>
#include <string>
#include <random>
#include <vector>

extern "C" {
#include "hashtable.h"
}

static void BM_HashtableFind_0Miss(benchmark::State &state) {
    // Prepare a hashtable and insert some keys
    hashtableType type = {
        .instant_rehashing = 1};
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

static void BM_HashtableFind_50Miss(benchmark::State &state) {
    // Prepare a hashtable and insert some keys
    hashtableType type = {
        .instant_rehashing = 1};
    hashtable *ht = hashtableCreate(&type);

    int num_to_remove = 16384;
    const int num_keys = num_to_remove * 2;

    std::vector<std::string> keys;
    for (int i = 0; i < num_keys; ++i) {
        keys.push_back("key" + std::to_string(i));
        hashtableAdd(ht, (void *)keys.back().c_str());
    }

    for (int i = 0; i < num_keys; ++i) {
        int chances_remaining = num_keys - i;
        if (rand() % chances_remaining < num_to_remove) { // Probability of num_to_remove / chances_remaining
            hashtableDelete(ht, (void *)keys.back().c_str());
            num_to_remove--;
        }
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

static void BM_HashtableFind_100Miss(benchmark::State &state) {
    // Prepare a hashtable and insert some keys
    hashtableType type = {
        .instant_rehashing = 1};
    hashtable *ht = hashtableCreate(&type);

    const int num_keys = 16384;
    std::vector<std::string> keys;
    for (int i = 0; i < num_keys; ++i) {
        keys.push_back("key" + std::to_string(i));
        hashtableAdd(ht, (void *)keys.back().c_str());
    }

    for (int i = 0; i < num_keys; ++i) {
        hashtableDelete(ht, (void *)keys.back().c_str());
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

BENCHMARK(BM_HashtableFind_0Miss)
    ->Repetitions(5)
    ->MinTime(5.0);

BENCHMARK(BM_HashtableFind_50Miss)
    ->Repetitions(5)
    ->MinTime(5.0);

BENCHMARK(BM_HashtableFind_100Miss)
    ->Repetitions(5)
    ->MinTime(5.0);

BENCHMARK_MAIN();
