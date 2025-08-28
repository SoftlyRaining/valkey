#pragma once

#include <cstddef>

namespace bench {
constexpr size_t megabyte = 1024 * 1024;
constexpr size_t dataset_size = 450 * megabyte;
constexpr size_t key_string_size = 128;
constexpr size_t item_count = dataset_size / key_string_size;
} // namespace bench
