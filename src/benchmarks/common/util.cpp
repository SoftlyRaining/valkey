
#include <cstdlib>
#include <string>

#include "util.h"

char *stringFromInt(int value) {
    constexpr size_t placeholder_size = 17; // key:123456789012 and null terminator
    char *s = static_cast<char *>(malloc(key_string_size));
    if (!s) return nullptr;
    std::fill_n(s, key_string_size, 'X');
    // Make string unique at end so whole string must be loaded for string comparison
    snprintf(s + key_string_size - placeholder_size, placeholder_size, "key:%012d", value);
    return s;
}
 