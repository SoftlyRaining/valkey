#include <benchmark/benchmark.h>

#include "../common/util.h"

extern "C" {
#include "hashtable.h"
}

static void BM_HashtableFind_0Miss(benchmark::State &state) {
    // Prepare a hashtable and insert some keys
    hashtableType type = {
        .instant_rehashing = 1};
    hashtable *ht = hashtableCreate(&type);

    std::vector<char *> keys;
    for (int i = 0; static_cast<size_t>(i) < item_count; ++i) {
        char *key = stringFromInt(i);
        keys.push_back(key);
        hashtableAdd(ht, key);
    }

    size_t idx = 0;
    for (auto _ : state) {
        bool found = hashtableFind(ht, keys[idx], nullptr);
        benchmark::DoNotOptimize(found);
        idx = (idx + 1) % item_count;
        benchmark::ClobberMemory();
    }

    hashtableRelease(ht);

    // Free allocated keys
    for (char *key : keys) {
        free(key);
    }
}

static void BM_HashtableFind_50Miss(benchmark::State &state) {
    // Prepare a hashtable and insert some keys
    hashtableType type = {
        .instant_rehashing = 1};
    hashtable *ht = hashtableCreate(&type);

    int num_to_remove = item_count;
    const int num_keys = num_to_remove * 2;

    std::vector<char *> keys;
    for (int i = 0; i < num_keys; ++i) {
        char *key = stringFromInt(i);
        keys.push_back(key);
        hashtableAdd(ht, key);
    }

    for (int i = 0; i < num_keys; ++i) {
        int chances_remaining = num_keys - i;
        if (rand() % chances_remaining < num_to_remove) { // Probability of num_to_remove / chances_remaining
            hashtableDelete(ht, keys[i]);
            num_to_remove--;
        }
    }

    size_t idx = 0;
    for (auto _ : state) {
        bool found = hashtableFind(ht, keys[idx], nullptr);
        benchmark::DoNotOptimize(found);
        idx = (idx + 1) % num_keys;
        benchmark::ClobberMemory();
    }

    hashtableRelease(ht);

    // Free allocated keys
    for (char *key : keys) {
        free(key);
    }
}

static void BM_HashtableFind_100Miss(benchmark::State &state) {
    // Prepare a hashtable and insert some keys
    hashtableType type = {
        .instant_rehashing = 1};
    hashtable *ht = hashtableCreate(&type);

    const int num_keys = item_count;
    std::vector<char *> inserted_keys;
    for (int i = 0; i < num_keys; ++i) {
        char *key = stringFromInt(i);
        inserted_keys.push_back(key);
        hashtableAdd(ht, key);
    }

    // Create different keys for lookup that don't exist
    std::vector<char *> lookup_keys;
    for (int i = 0; i < num_keys; ++i) {
        char *key = stringFromInt(i + item_count);
        lookup_keys.push_back(key);
    }

    size_t idx = 0;
    for (auto _ : state) {
        bool found = hashtableFind(ht, lookup_keys[idx], nullptr);
        benchmark::DoNotOptimize(found);
        idx = (idx + 1) % num_keys;
        benchmark::ClobberMemory();
    }

    hashtableRelease(ht);

    // Free allocated keys
    for (char *key : inserted_keys) {
        free(key);
    }
    for (char *key : lookup_keys) {
        free(key);
    }
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
