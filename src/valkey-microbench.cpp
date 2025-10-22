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

// Use exact dictType from unit tests
static uint64_t hashCallback(const void *key) {
    return dictGenHashFunction((unsigned char *)key, strlen((char *)key));
}

static int compareCallback(const void *key1, const void *key2) {
    int l1, l2;
    l1 = strlen((char *)key1);
    l2 = strlen((char *)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

static void freeCallback(void *val) {
    // Don't free values since we're using nullptr
    (void)val;
}

static dictType BenchmarkDictType = {hashCallback, nullptr, compareCallback, freeCallback, nullptr, nullptr};

// Helper function like unit tests
static char *stringFromInt(int value) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "key%d", value);
    char *s = static_cast<char *>(malloc(len + 1));
    if (!s) return nullptr;
    memcpy(s, buf, len);
    s[len] = '\0';
    return s;
}

static void BM_HashtableFind_0Miss(benchmark::State &state) {
    // Prepare a hashtable and insert some keys
    hashtableType type = {
        .instant_rehashing = 1};
    hashtable *ht = hashtableCreate(&type);

    const int num_keys = 16384;
    std::vector<char *> keys;
    for (int i = 0; i < num_keys; ++i) {
        char *key = stringFromInt(i);
        keys.push_back(key);
        hashtableAdd(ht, key);
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

static void BM_HashtableFind_50Miss(benchmark::State &state) {
    // Prepare a hashtable and insert some keys
    hashtableType type = {
        .instant_rehashing = 1};
    hashtable *ht = hashtableCreate(&type);

    int num_to_remove = 16384;
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

    const int num_keys = 16384;
    // Insert keys 0-16383
    std::vector<char *> inserted_keys;
    for (int i = 0; i < num_keys; ++i) {
        char *key = stringFromInt(i);
        inserted_keys.push_back(key);
        hashtableAdd(ht, key);
    }

    // Create different keys for lookup that don't exist (100000+)
    std::vector<char *> lookup_keys;
    for (int i = 0; i < num_keys; ++i) {
        char *key = stringFromInt(i + 100000);
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

    const int num_keys = 16384;
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

    int num_to_remove = 16384;
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

    const int num_keys = 16384;
    // Insert keys 0-16383
    std::vector<char *> inserted_keys;
    for (int i = 0; i < num_keys; ++i) {
        char *key = stringFromInt(i);
        inserted_keys.push_back(key);
        dictAdd(d, key, nullptr);
    }

    // Create different keys for lookup that don't exist (100000+)
    std::vector<char *> lookup_keys;
    for (int i = 0; i < num_keys; ++i) {
        char *key = stringFromInt(i + 100000);
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
