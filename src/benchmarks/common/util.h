#pragma once

#include <cstdlib>
#include <memory>
#include <vector>

struct FreeDeleter { void operator()(char *p) const { free(p); } };

char *stringFromInt(int value);

struct BenchmarkDataset {
    std::vector<std::unique_ptr<char, FreeDeleter>> insert_keys;
    std::vector<std::unique_ptr<char, FreeDeleter>> miss_keys;
    std::vector<char *> insert_ptrs;
    std::vector<char *> lookup_ptrs;

    BenchmarkDataset(int hit_percent, size_t count);
};
