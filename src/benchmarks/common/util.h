#pragma once

#include <cstdlib>

constexpr size_t megabyte = 1024 * 1024;
constexpr size_t dataset_size = 450 * megabyte; // 10x L3 cache size on my hardware
constexpr size_t key_string_size = 128;
constexpr size_t item_count = dataset_size / key_string_size;

char *stringFromInt(int value);
