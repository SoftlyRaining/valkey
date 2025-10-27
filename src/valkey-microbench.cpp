#include <benchmark/benchmark.h>

#include <cstring>
#include <string>
#include <random>
#include <vector>
#include <cstdlib>

extern "C" {
#include "hashtable.h"
#include "dict.h"
}

constexpr size_t megabyte = 1024 * 1024;
constexpr size_t dataset_size = 450 * megabyte; // 10x L3 cache size on my hardware
constexpr size_t key_string_size = 128;
constexpr size_t item_count = dataset_size / key_string_size;

// Use exact dictType from unit tests
static uint64_t hashCallback(const void *key) {
    return dictGenHashFunction((unsigned char *)key, strlen((char *)key));
}

static int dictCompareCallback(const void *key1, const void *key2) {
    char *keystr1 = (char *)key1;
    char *keystr2 = (char *)key2;
    return strcmp(keystr1, keystr2) == 0;
}

static dictType BenchmarkDictType = {hashCallback, nullptr, dictCompareCallback, nullptr, nullptr, nullptr};

static char *stringFromInt(int value) {
    constexpr size_t placeholder_size = 17; // key:123456789012 and null terminator
    char *s = static_cast<char *>(malloc(key_string_size));
    if (!s) return nullptr;
    std::fill_n(s, key_string_size, 'X');
    // Make string unique at end so whole string must be loaded for string comparison
    snprintf(s + key_string_size - placeholder_size, placeholder_size, "key:%012d", value);
    return s;
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

BENCHMARK_MAIN();
