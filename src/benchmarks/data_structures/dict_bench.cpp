#include <benchmark/benchmark.h>

#include <cstring>

#include "../common/util.h"

extern "C" {
#include "dict.h"
}

// Use exact dictType from unit tests
static uint64_t hashCallback(const void *key) {
    return dictGenHashFunction((unsigned char *)key, strlen((char *)key));
}

static int dictCompareCallback(const void *key1, const void *key2) {
    char *keystr1 = (char *)key1;
    char *keystr2 = (char *)key2;
    return strcmp(keystr1, keystr2) == 0;
}

static dictType BenchmarkDictType = {
    .hashFunction = hashCallback,
    .keyCompare = dictCompareCallback};

static void BM_DictFind_0Miss(benchmark::State &state) {
    dict *d = dictCreate(&BenchmarkDictType);

    const int num_keys = item_count;
    std::vector<char *> keys;
    for (int i = 0; i < num_keys; ++i) {
        char *key = stringFromInt(i);
        keys.push_back(key);
        dictAdd(d, key, nullptr);
    }

    size_t idx = 0;
    for (auto _ : state) {
        dictEntry *entry = dictFind(d, keys[idx]);
        benchmark::DoNotOptimize(entry);
        idx = (idx + 1) % num_keys;
        benchmark::ClobberMemory();
    }

    dictRelease(d);

    // Free allocated keys
    for (char *key : keys) {
        free(key);
    }
}

static void BM_DictFind_50Miss(benchmark::State &state) {
    dict *d = dictCreate(&BenchmarkDictType);

    int num_to_remove = item_count;
    const int num_keys = num_to_remove * 2;

    std::vector<char *> keys;
    for (int i = 0; i < num_keys; ++i) {
        char *key = stringFromInt(i);
        keys.push_back(key);
        dictAdd(d, key, nullptr);
    }

    for (int i = 0; i < num_keys; ++i) {
        int chances_remaining = num_keys - i;
        if (rand() % chances_remaining < num_to_remove) {
            dictDelete(d, keys[i]);
            num_to_remove--;
        }
    }

    size_t idx = 0;
    for (auto _ : state) {
        dictEntry *entry = dictFind(d, keys[idx]);
        benchmark::DoNotOptimize(entry);
        idx = (idx + 1) % num_keys;
        benchmark::ClobberMemory();
    }

    dictRelease(d);

    // Free allocated keys
    for (char *key : keys) {
        free(key);
    }
}

static void BM_DictFind_100Miss(benchmark::State &state) {
    dict *d = dictCreate(&BenchmarkDictType);

    const int num_keys = item_count;
    std::vector<char *> inserted_keys;
    for (int i = 0; i < num_keys; ++i) {
        char *key = stringFromInt(i);
        inserted_keys.push_back(key);
        dictAdd(d, key, nullptr);
    }

    // Create different keys for lookup that don't exist
    std::vector<char *> lookup_keys;
    for (int i = 0; i < num_keys; ++i) {
        char *key = stringFromInt(i + item_count);
        lookup_keys.push_back(key);
    }

    size_t idx = 0;
    for (auto _ : state) {
        dictEntry *entry = dictFind(d, lookup_keys[idx]);
        benchmark::DoNotOptimize(entry);
        idx = (idx + 1) % num_keys;
        benchmark::ClobberMemory();
    }

    dictRelease(d);

    // Free allocated keys
    for (char *key : inserted_keys) {
        free(key);
    }
    for (char *key : lookup_keys) {
        free(key);
    }
}

BENCHMARK(BM_DictFind_0Miss)
    ->Repetitions(5)
    ->MinTime(5.0);

BENCHMARK(BM_DictFind_50Miss)
    ->Repetitions(5)
    ->MinTime(5.0);

BENCHMARK(BM_DictFind_100Miss)
    ->Repetitions(5)
    ->MinTime(5.0);
