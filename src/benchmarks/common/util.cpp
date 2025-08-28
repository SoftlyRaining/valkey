#include <algorithm>
#include <cstdlib>
#include <random>
#include <string>

#include "config.h"
#include "util.h"

char *stringFromInt(int value) {
    constexpr size_t placeholder_size = 17;
    char *s = static_cast<char *>(malloc(bench::key_string_size));
    if (!s) exit(1);
    std::fill_n(s, bench::key_string_size, 'X');
    snprintf(s + bench::key_string_size - placeholder_size, placeholder_size, "key:%012d", value);
    return s;
}

BenchmarkDataset::BenchmarkDataset(int hit_percent, size_t count) {
    size_t num_hits = count * hit_percent / 100;
    size_t num_misses = count - num_hits;
    std::mt19937 rng{std::random_device{}()};

    for (size_t i = 0; i < count; ++i) {
        insert_keys.emplace_back(stringFromInt(i));
        insert_ptrs.push_back(insert_keys.back().get());
    }
    std::shuffle(insert_ptrs.begin(), insert_ptrs.end(), rng);

    std::vector<char *> shuffled = insert_ptrs;
    std::shuffle(shuffled.begin(), shuffled.end(), rng);
    for (size_t i = 0; i < num_hits; ++i) {
        lookup_ptrs.push_back(shuffled[i]);
    }
    for (size_t i = 0; i < num_misses; ++i) {
        miss_keys.emplace_back(stringFromInt(count + i));
        lookup_ptrs.push_back(miss_keys.back().get());
    }
    std::shuffle(lookup_ptrs.begin(), lookup_ptrs.end(), rng);
}
