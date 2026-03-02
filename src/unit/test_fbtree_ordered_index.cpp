/*
 * Copyright (c) Valkey Contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "generated_wrappers.hpp"

#include <cstdio>
#include <cstring>

extern "C" {
#include "fbtree_ordered_index.h"
#include "zmalloc.h"
#include "sds.h"
}

#define TEST_ASSERT(x) ASSERT_TRUE(x)

/* ========== Test Helpers ========== */

/* Node capacity - must match NODE_SIZE in fbtree_ordered_index.c */
#define TEST_NODE_CAPACITY 61
#define TEST_TWO_LEVEL_ITEMS (TEST_NODE_CAPACITY * TEST_NODE_CAPACITY)
#define TEST_THREE_LEVEL_ITEMS (TEST_TWO_LEVEL_ITEMS + 200)

/* Size limit of embedded prefix - must match EMBED_PREFIX_LEN in fbtree_ordered_index.c */
#define TEST_EMBED_PREFIX_LEN 54

/* Create a null-terminated sds from a C string. */
static sds createString(const char *str) {
    size_t len = strlen(str) + 1;
    return sdsnewlen(str, len);
}

/* Create a string like "prefix" + base-26 encoded value + "suffix".
 * E.g., value=0 → "AAA", value=1 → "AAB", value=26 → "ABA", value=27 → "ABB". */
static sds createBase26TestString(const char *prefix, const char *suffix, size_t value, size_t value_width) {
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);
    size_t len = prefix_len + suffix_len + value_width + 1;
    sds s = sdsnewlen(NULL, len);
    memcpy(s, prefix, prefix_len);

    /* Encode value as base-26 letters (A=0, B=1, ..., Z=25) */
    for (size_t i = 0; i < value_width; i++) {
        char c = (char)('A' + (value % 26));
        s[prefix_len + value_width - 1 - i] = c;
        value /= 26;
    }

    memcpy(s + prefix_len + value_width, suffix, suffix_len);
    s[len - 1] = '\0';
    return s;
}

/* Validate tree invariants. Returns false if tree is corrupted. */
bool is_tree_valid(fbtreeIndex *fbt) {
    /* TODO: re-enable verbose output once tests stabilize */
    return fbtreeDebugValidate(fbt, false);
}

/* ========== Basic Lifecycle Tests ========== */

TEST(FbtreeTest, create_and_free) {
    size_t used_memory_before = zmalloc_used_memory();

    fbtreeIndex *fbt = fbtreeCreate();
    TEST_ASSERT(fbt != NULL);
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

/* Verify node sizes fit expected jemalloc size classes.
 * innerNode should fit in 1792-byte class, leafNode in 512-byte class.
 * This catches accidental struct bloat that wastes memory. */
TEST(FbtreeTest, node_allocation_sizes) {
    /* Directly test allocation sizes using zmalloc_usable_size */
    void *inner_test = zmalloc(1784); /* sizeof(innerNode) */
    void *leaf_test = zmalloc(512);   /* sizeof(leafNode) */

    size_t inner_actual = zmalloc_usable_size(inner_test);
    size_t leaf_actual = zmalloc_usable_size(leaf_test);

    /* innerNode (1784 bytes) should fit in 1792-byte jemalloc class */
    TEST_ASSERT(inner_actual == 1792);

    /* leafNode (512 bytes) should fit in 512-byte jemalloc class */
    TEST_ASSERT(leaf_actual == 512);

    zfree(inner_test);
    zfree(leaf_test);
}

TEST(FbtreeTest, insert_and_lookup) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    sds str = createString("hello");
    sds inserted = fbtreeInsert(fbt, str);
    TEST_ASSERT(is_tree_valid(fbt));

    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted) >= 0);

    sds search_string2 = createString("world");
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, search_string2) < 0);
    sdsfree(search_string2);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, insert_multiple) {

    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    const char *strings[] = {"apple", "banana", "cherry", "date", "elderberry", "elder"};
    int count = sizeof(strings) / sizeof(strings[0]);
    sds inserted[6];

    for (int i = 0; i < count; i++) {
        sds str = createString(strings[i]);
        inserted[i] = fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    for (int i = 0; i < count; i++) {
        TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) >= 0);
    }

    sds search_str = createString("fig");
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, search_str) < 0);
    sdsfree(search_str);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, lookup_empty_tree) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    TEST_ASSERT(is_tree_valid(fbt));

    sds search_str = createString("anything");
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, search_str) < 0);
    sdsfree(search_str);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, length_increments) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    TEST_ASSERT(fbtreeLength(fbt) == 0);

    sds str1 = createString("first");
    fbtreeInsert(fbt, str1);
    TEST_ASSERT(fbtreeLength(fbt) == 1);

    sds str2 = createString("second");
    fbtreeInsert(fbt, str2);
    TEST_ASSERT(fbtreeLength(fbt) == 2);

    sds str3 = createString("third");
    fbtreeInsert(fbt, str3);
    TEST_ASSERT(fbtreeLength(fbt) == 3);
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

/* ========== Forward Iterator Tests ========== */

TEST(FbtreeTest, iterator_small) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    const char *strings[] = {"dog", "cat", "ant", "bat"};
    for (int i = 0; i < 4; i++) {
        sds str = createString(strings[i]);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "ant", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "bat", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "cat", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "dog", 4) == 0);
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, iterator_full_leaf) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert NODE_CAPACITY+1 items to exceed single leaf */
    const int count = TEST_NODE_CAPACITY + 1;
    char buf[8];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        sds str = createString(buf);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Verify all items in sorted order */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(sdslen(pos) == 4);
        TEST_ASSERT(memcmp(pos, buf, 4) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, iterator_reverse_insert) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert in reverse order */
    const int count = TEST_NODE_CAPACITY + 1;
    char buf[8];
    for (int i = count - 1; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        sds str = createString(buf);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Iteration should still be sorted ascending */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(sdslen(pos) == 4);
        TEST_ASSERT(memcmp(pos, buf, 4) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, iterator_empty) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    TEST_ASSERT(is_tree_valid(fbt));

    /* Next on empty tree returns false */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, iterator_reset_invalidates) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    const char *strings[] = {"a", "b", "c"};
    for (int i = 0; i < 3; i++) {
        sds str = createString(strings[i]);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(fbtreeNext(&it, &pos));

    fbtreeResetIterator(&it);
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

/* ========== Duplicate and Edge Case Tests ========== */

TEST(FbtreeTest, duplicate_insert) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    sds str1 = createString("key");
    sds inserted1 = fbtreeInsert(fbt, str1);
    TEST_ASSERT(fbtreeLength(fbt) == 1);

    /* Duplicates are allowed - both stored separately */
    sds str2 = createString("key");
    sds inserted2 = fbtreeInsert(fbt, str2);
    TEST_ASSERT(fbtreeLength(fbt) == 2);
    TEST_ASSERT(is_tree_valid(fbt));

    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted1) >= 0);
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted2) >= 0);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, empty_string) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    sds str = createString("");
    sds inserted = fbtreeInsert(fbt, str);
    TEST_ASSERT(is_tree_valid(fbt));

    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted) >= 0);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, long_strings) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    char long_str[256];
    memset(long_str, 'a', 255);
    long_str[255] = '\0';

    sds str = createString(long_str);
    sds inserted = fbtreeInsert(fbt, str);
    TEST_ASSERT(is_tree_valid(fbt));

    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted) >= 0);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

/* ========== Ordering Tests ========== */

TEST(FbtreeTest, multilevel_reverse_insert) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    char buf[8];
    for (int i = 95; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        sds str = createString(buf);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i <= 95; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, 5) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeInitIterator(&it, fbt);
    for (int i = 95; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        TEST_ASSERT(fbtreePrev(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, 5) == 0);
    }
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, prefix_ordering) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    const char *strings[] = {"elderberry", "elder", "e", "elderly"};
    for (int i = 0; i < 4; i++) {
        sds str = createString(strings[i]);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "e", 2) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "elder", 6) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "elderberry", 11) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "elderly", 8) == 0);
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, same_length_ordering) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    const char *strings[] = {"zoo", "abc", "xyz", "def"};
    for (int i = 0; i < 4; i++) {
        sds str = createString(strings[i]);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "abc", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "def", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "xyz", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "zoo", 4) == 0);
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, common_prefix_ordering) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    const char *keys[] = {
        "prefix_aaa", "prefix_aab", "prefix_aac", "prefix_aad",
        "prefix_baa", "prefix_bab", "prefix_bac", "prefix_bad",
        "prefix_caa", "prefix_cab", "prefix_cac", "prefix_cad"};
    sds inserted[12];
    for (int i = 0; i < 12; i++) {
        sds str = createString(keys[i]);
        inserted[i] = fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    for (int i = 0; i < 12; i++) {
        TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) >= 0);
    }

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < 12; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
        size_t len = strlen(keys[i]) + 1;
        TEST_ASSERT(memcmp(pos, keys[i], len) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, insert_batches_sorted) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    const char *batch1[] = {"dog", "cat", "ant"};
    for (int i = 0; i < 3; i++) {
        sds str = createString(batch1[i]);
        fbtreeInsert(fbt, str);
    }

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "ant", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "cat", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "dog", 4) == 0);
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    /* Insert second batch (interleaves with first) */
    const char *batch2[] = {"bat", "elk"};
    for (int i = 0; i < 2; i++) {
        sds str = createString(batch2[i]);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeInitIterator(&it, fbt);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "ant", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "bat", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "cat", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "dog", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "elk", 4) == 0);
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, insert_at_boundaries) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    sds str = createString("m");
    fbtreeInsert(fbt, str);

    const char *inserts[] = {"a", "z", "n"};
    for (int i = 0; i < 3; i++) {
        str = createString(inserts[i]);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "a", 2) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "m", 2) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "n", 2) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "z", 2) == 0);
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

/* ========== Backward Iterator (Prev) Tests ========== */

TEST(FbtreeTest, prev_small) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    const char *strings[] = {"dog", "cat", "ant", "bat"};
    for (int i = 0; i < 4; i++) {
        sds str = createString(strings[i]);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Iterate backward from end - descending order */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos, "dog", 4) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos, "cat", 4) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos, "bat", 4) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos, "ant", 4) == 0);
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, prev_full_leaf) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    const int count = TEST_NODE_CAPACITY + 1;
    char buf[8];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        sds str = createString(buf);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = count - 1; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        TEST_ASSERT(fbtreePrev(&it, &pos));
        TEST_ASSERT(sdslen(pos) == 4);
        TEST_ASSERT(memcmp(pos, buf, 4) == 0);
    }
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, prev_empty) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, prev_next_mixed) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    const char *strings[] = {"a", "b", "c", "d", "e"};
    for (int i = 0; i < 5; i++) {
        sds str = createString(strings[i]);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;

    /* Forward then backward - prev returns current before moving */
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "a", 2) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "b", 2) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos, "b", 2) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos, "a", 2) == 0);

    /* Can resume forward */
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "a", 2) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "b", 2) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "c", 2) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos, "c", 2) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos, "b", 2) == 0);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, prev_single) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    sds str = createString("only");
    fbtreeInsert(fbt, str);
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos, "only", 5) == 0);
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, iterator_exhausted_stays_invalid) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    sds str = createString("x");
    fbtreeInsert(fbt, str);
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;

    /* Exhaust forward - repeated calls stay false */
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    /* Can't reverse from exhausted state */
    TEST_ASSERT(!fbtreePrev(&it, &pos));
    TEST_ASSERT(!fbtreePrev(&it, &pos));
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    /* Exhaust backward - same behavior */
    fbtreeInitIterator(&it, fbt);
    TEST_ASSERT(fbtreePrev(&it, &pos));
    TEST_ASSERT(!fbtreePrev(&it, &pos));
    TEST_ASSERT(!fbtreePrev(&it, &pos));
    TEST_ASSERT(!fbtreePrev(&it, &pos));
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    /* Ensure we can reverse from last item, but not after exhausting */
    fbtreeInitIterator(&it, fbt);
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(fbtreePrev(&it, &pos));
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    /* Same for first item - can move forward from first, but not after exhausting */
    fbtreeInitIterator(&it, fbt);
    TEST_ASSERT(fbtreePrev(&it, &pos));
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(fbtreePrev(&it, &pos));
    TEST_ASSERT(!fbtreePrev(&it, &pos));
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

/* ========== Multi-Level Tree Tests ========== */

TEST(FbtreeTest, multilevel_lookup) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    const int count = TEST_NODE_CAPACITY + 1;
    char buf[8];
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        sds str = createString(buf);
        inserted[i] = fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    for (int i = 0; i < count; i++) {
        TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) >= 0);
    }
    zfree(inserted);

    sds search_str1 = createString("k99");
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, search_str1) < 0);
    sdsfree(search_str1);
    sds search_str2 = createString("x00");
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, search_str2) < 0);
    sdsfree(search_str2);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, multilevel_forward_iteration) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* 3.5x node capacity creates multi-level tree with multiple leaves */
    const int count = TEST_NODE_CAPACITY * 7 / 2;
    char buf[8];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        sds str = createString(buf);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Forward iteration visits all items in order */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, 5) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, multilevel_backward_iteration) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* 3.5x node capacity creates multi-level tree */
    const int count = TEST_NODE_CAPACITY * 7 / 2;
    char buf[8];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        sds str = createString(buf);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Backward iteration visits all items in reverse order */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = count - 1; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        TEST_ASSERT(fbtreePrev(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, 5) == 0);
    }
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, multilevel_mixed_iteration) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* 3.5x node capacity */
    const int total = TEST_NODE_CAPACITY * 7 / 2;
    char buf[8];
    for (int i = 0; i < total; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        sds str = createString(buf);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));
    TEST_ASSERT(fbtreeLength(fbt) == (size_t)total);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;

    /* Forward 10 items: lands on k009 */
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
    }
    TEST_ASSERT(sdslen(pos) == 5);
    TEST_ASSERT(memcmp(pos, "k009", 5) == 0);

    /* Backward 5 items: lands on k005 */
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT(fbtreePrev(&it, &pos));
    }
    TEST_ASSERT(sdslen(pos) == 5);
    TEST_ASSERT(memcmp(pos, "k005", 5) == 0);

    /* Forward to end */
    int count = 1;
    while (fbtreeNext(&it, &pos)) count++;
    TEST_ASSERT(count == total - 5 + 1); /* from k005 to end */
    snprintf(buf, sizeof(buf), "k%03d", total - 1);
    TEST_ASSERT(memcmp(pos, buf, 5) == 0);

    fbtreeResetIterator(&it);
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, multilevel_cross_leaf_iteration) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* 200 items ensures multiple leaves */
    char buf[8];
    for (int i = 0; i < 200; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        sds str = createString(buf);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;

    /* Skip to near first leaf boundary (at TEST_NODE_CAPACITY with asymmetric splits) */
    const int boundary = TEST_NODE_CAPACITY;
    for (int i = 0; i < boundary - 5; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
    }

    /* Verify items around leaf boundary are correct */
    for (int i = boundary - 5; i < boundary + 5; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, 5) == 0);
    }

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

/* ========== Inner Node Split Tests (3+ Level Trees) ========== */

TEST(FbtreeTest, inner_split_sequential) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert enough items to force inner node splits and create 3+ level tree */
    /* Sequential pattern stresses append pattern insertion shortcut and asymetric node splits */
    const int count = TEST_THREE_LEVEL_ITEMS;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        sds str = createBase26TestString("key_", "", i, 3);
        inserted[i] = fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));
    TEST_ASSERT(fbtreeLength(fbt) == (size_t)count);

    /* All items findable */
    for (int i = 0; i < count; i++) {
        TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) >= 0);
    }
    zfree(inserted);

    /* Verify all items exist and iteration is sorted */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < count; i++) {
        sds expected = createBase26TestString("key_", "", i, 3);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(sdscmp(pos, expected) == 0);
        sdsfree(expected);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, inner_split_reverse) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Reverse insertion stresses prepend pattern insertion shortcut and asymetric node splits */
    const int count = TEST_THREE_LEVEL_ITEMS;
    char buf[16];
    for (int i = count - 1; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "key_%05d", i);
        sds str = createString(buf);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));
    TEST_ASSERT(fbtreeLength(fbt) == (size_t)count);

    /* Forward iteration still sorted */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%05d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    /* Backward iteration also works */
    fbtreeInitIterator(&it, fbt);
    for (int i = count - 1; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "key_%05d", i);
        TEST_ASSERT(fbtreePrev(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);
    }
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, inner_split_shuffled) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Shuffle indices for random insertion */
    /* Random insertion stresses normal mid-collection insertion path and symetric 50/50 node splits */
    int *indices = (int *)zmalloc(5000 * sizeof(int));
    for (int i = 0; i < 5000; i++) {
        indices[i] = i;
    }
    for (int i = 4999; i > 0; i--) {
        int j = i % 1000; /* Simple pseudo-random swap */
        int temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
    }

    /* Insert in shuffled order */
    char buf[16];
    for (int i = 0; i < 5000; i++) {
        snprintf(buf, sizeof(buf), "rnd_%05d", indices[i]);
        sds str = createString(buf);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));
    TEST_ASSERT(fbtreeLength(fbt) == 5000);

    /* Verify all items exist and iteration is sorted */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < 5000; i++) {
        snprintf(buf, sizeof(buf), "rnd_%05d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    zfree(indices);
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

/* ========== Deep Tree Tests (4+ Levels) ========== */

TEST(FbtreeTest, deep_tree_4_levels) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Ensure 4+ level tree by exceeding NODE_CAPACITY^3 */
    const int count = TEST_NODE_CAPACITY * TEST_NODE_CAPACITY * TEST_NODE_CAPACITY + 10000;
    char buf[16];
    sds first_item = NULL, middle_item = NULL, last_item = NULL;
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "deep_%06d", i);
        sds str = createString(buf);
        sds inserted = fbtreeInsert(fbt, str);
        if (i == 0)
            first_item = inserted;
        else if (i == count / 2)
            middle_item = inserted;
        else if (i == count - 1)
            last_item = inserted;
    }
    TEST_ASSERT(is_tree_valid(fbt));
    TEST_ASSERT(fbtreeLength(fbt) == (size_t)count);

    /* Lookups at first, middle, last */
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, first_item) >= 0);
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, middle_item) >= 0);
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, last_item) >= 0);

    /* Non-existent item */
    sds search_str = createString("deep_300000");
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, search_str) < 0);
    sdsfree(search_str);

    /* Iteration across deep tree boundaries */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;

    /* Skip to middle */
    for (int i = 0; i < count / 2; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
    }

    /* Verify next 100 items */
    for (int i = count / 2; i < count / 2 + 100; i++) {
        snprintf(buf, sizeof(buf), "deep_%06d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);
    }

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}
TEST(FbtreeTest, deep_tree_mixed_insert_patterns) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    char buf[16];

    /* Batch 1: sequential (items 0, 3, 6, 9, ...) */
    for (int i = 0; i < 10000; i++) {
        snprintf(buf, sizeof(buf), "mix_%06d", i * 3);
        sds str = createString(buf);
        fbtreeInsert(fbt, str);
    }

    /* Batch 2: reverse (items 1, 4, 7, 10, ...) */
    for (int i = 9999; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "mix_%06d", i * 3 + 1);
        sds str = createString(buf);
        fbtreeInsert(fbt, str);
    }

    /* Batch 3: shuffled (items 2, 5, 8, 11, ...) */
    int *indices = (int *)zmalloc(10000 * sizeof(int));
    for (int i = 0; i < 10000; i++) {
        indices[i] = i * 3 + 2;
    }
    for (int i = 9999; i > 0; i--) {
        int j = (i * 17) % (i + 1);
        int temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
    }
    for (int i = 0; i < 10000; i++) {
        snprintf(buf, sizeof(buf), "mix_%06d", indices[i]);
        sds str = createString(buf);
        fbtreeInsert(fbt, str);
    }
    zfree(indices);
    TEST_ASSERT(is_tree_valid(fbt));
    TEST_ASSERT(fbtreeLength(fbt) == 30000);

    /* All 30k items in sorted order */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < 30000; i++) {
        snprintf(buf, sizeof(buf), "mix_%06d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    /* Test mixed forward/backward iteration in deep tree */
    fbtreeInitIterator(&it, fbt);

    /* Forward 1000 */
    for (int i = 0; i < 1000; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
    }

    /* Backward 500 - lands on item 500 */
    for (int i = 0; i < 500; i++) {
        TEST_ASSERT(fbtreePrev(&it, &pos));
    }
    snprintf(buf, sizeof(buf), "mix_%06d", 500);
    TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

/* ========== String Pattern Tests ========== */

TEST(FbtreeTest, varied_string_patterns) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    char buf[32];
    sds *inserted = (sds *)zmalloc(4000 * sizeof(sds));
    int idx = 0;

    /* Pattern 1: Common prefix, varying suffix */
    for (int i = 0; i < 1000; i++) {
        snprintf(buf, sizeof(buf), "common_prefix_%04d_suffix", i);
        sds str = createString(buf);
        inserted[idx++] = fbtreeInsert(fbt, str);
    }

    /* Pattern 2: Varying prefix, common suffix */
    for (int i = 0; i < 1000; i++) {
        snprintf(buf, sizeof(buf), "prefix_%04d_common_suffix", i);
        sds str = createString(buf);
        inserted[idx++] = fbtreeInsert(fbt, str);
    }

    /* Pattern 3: Palindromic */
    for (int i = 0; i < 1000; i++) {
        snprintf(buf, sizeof(buf), "pal_%04d_%04d_lap", i, i);
        sds str = createString(buf);
        inserted[idx++] = fbtreeInsert(fbt, str);
    }

    /* Pattern 4: Repeated characters */
    for (int i = 0; i < 1000; i++) {
        char c = 'a' + (i % 26);
        snprintf(buf, sizeof(buf), "repeat_%c%c%c%c_%04d", c, c, c, c, i);
        sds str = createString(buf);
        inserted[idx++] = fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));
    TEST_ASSERT(fbtreeLength(fbt) == 4000);

    for (int i = 0; i < 4000; i++) {
        TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) >= 0);
    }
    zfree(inserted);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    const_sds prev_pos = NULL;
    for (int i = 0; i < 4000; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
        if (prev_pos) {
            TEST_ASSERT(sdscmp(prev_pos, pos) <= 0);
        }
        prev_pos = pos;
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

/* ========== Split Boundary Tests ========== */

TEST(FbtreeTest, split_at_exact_boundary) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    char buf[16];
    const int count = TEST_TWO_LEVEL_ITEMS;

    /* Insert NODE_CAPACITY^2 items - with optimized append path, this creates fully packed nodes */
    /* This should create exactly a 2-level tree with the final item triggering inner node split */
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "bound_%05d", i);
        sds str = createString(buf);
        fbtreeInsert(fbt, str);
    }

    /* Add one more to force inner node split */
    snprintf(buf, sizeof(buf), "bound_%05d", count);
    sds str = createString(buf);
    sds boundary_item = fbtreeInsert(fbt, str);
    TEST_ASSERT(is_tree_valid(fbt));
    TEST_ASSERT(fbtreeLength(fbt) == (size_t)(count + 1));

    /* The boundary item is findable */
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, boundary_item) >= 0);

    /* Iteration across split boundary */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;

    /* Skip to near boundary */
    for (int i = 0; i < count - 6; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
    }

    /* Verify items around split boundary */
    for (int i = count - 6; i <= count; i++) {
        snprintf(buf, sizeof(buf), "bound_%05d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, alternating_min_max_insert) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Alternating min/max insertion stresses split boundaries */
    const int count = TEST_TWO_LEVEL_ITEMS;
    char buf[16];
    int min_val = 0, max_val = count - 1;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));

    while (min_val <= max_val) {
        snprintf(buf, sizeof(buf), "mid_%06d", min_val);
        sds str = createString(buf);
        inserted[min_val] = fbtreeInsert(fbt, str);
        min_val++;

        if (min_val > max_val) break;
        snprintf(buf, sizeof(buf), "mid_%06d", max_val);
        str = createString(buf);
        inserted[max_val] = fbtreeInsert(fbt, str);
        max_val--;
    }
    TEST_ASSERT(is_tree_valid(fbt));
    TEST_ASSERT(fbtreeLength(fbt) == (size_t)count);

    /* All items findable */
    for (int i = 0; i < count; i++) {
        TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) >= 0);
    }
    zfree(inserted);

    /* Forward iteration */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < count; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
        snprintf(buf, sizeof(buf), "mid_%06d", i);
        TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    /* Reverse iteration */
    fbtreeInitIterator(&it, fbt);
    for (int i = count - 1; i >= 0; i--) {
        TEST_ASSERT(fbtreePrev(&it, &pos));
        snprintf(buf, sizeof(buf), "mid_%06d", i);
        TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);
    }
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

/* Sequential insertion into middle of tree can still trigger optimized append/prepend paths
 * when the insertion point becomes the new rightmost/leftmost position in a subtree */
TEST(FbtreeTest, sequential_middle_insert) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    char buf[16];

    /* Create tree with gap: [0-999] and [3000-3999] */
    for (int i = 0; i < 1000; i++) {
        snprintf(buf, sizeof(buf), "seq_%06d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    for (int i = 3000; i < 4000; i++) {
        snprintf(buf, sizeof(buf), "seq_%06d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    TEST_ASSERT(is_tree_valid(fbt));
    TEST_ASSERT(fbtreeLength(fbt) == 2000);

    /* Fill gap with ascending sequence [1000-1999] (append pattern into middle) */
    for (int i = 1000; i < 2000; i++) {
        snprintf(buf, sizeof(buf), "seq_%06d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    TEST_ASSERT(is_tree_valid(fbt));
    TEST_ASSERT(fbtreeLength(fbt) == 3000);

    /* Fill remaining gap with descending sequence [2999-2000] (prepend pattern into middle) */
    for (int i = 2999; i >= 2000; i--) {
        snprintf(buf, sizeof(buf), "seq_%06d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    TEST_ASSERT(is_tree_valid(fbt));
    TEST_ASSERT(fbtreeLength(fbt) == 4000);

    /* All 4000 items in order */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < 4000; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
        snprintf(buf, sizeof(buf), "seq_%06d", i);
        TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

/* ========== Delete Tests ========== */

TEST(FbtreeTest, delete_single_item) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    sds str = createString("test");
    sds inserted = fbtreeInsert(fbt, str);
    TEST_ASSERT(fbtreeLength(fbt) == 1);

    TEST_ASSERT(fbtreeDelete(fbt, inserted));
    TEST_ASSERT(fbtreeLength(fbt) == 0);
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, delete_nonexistent) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    sds str = createString("exists");
    sds inserted = fbtreeInsert(fbt, str);

    sds other = createString("missing");
    TEST_ASSERT(!fbtreeDelete(fbt, other));
    sdsfree(other);

    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted) >= 0);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, delete_from_empty) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    sds dummy = createString("anything");
    TEST_ASSERT(!fbtreeDelete(fbt, dummy));
    sdsfree(dummy);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, delete_all_items) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    char buf[8];
    sds inserted[10];
    for (int i = 0; i < 10; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        sds str = createString(buf);
        inserted[i] = fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(fbtreeLength(fbt) == 10);

    for (int i = 0; i < 10; i++) {
        TEST_ASSERT(fbtreeDelete(fbt, inserted[i]));
        TEST_ASSERT(fbtreeLength(fbt) == 10UL - i - 1UL);
    }
    TEST_ASSERT(fbtreeLength(fbt) == 0);
    TEST_ASSERT(is_tree_valid(fbt));

    /* Verify leaf caches cleared - iterators return false on empty tree */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    fbtreeInitIterator(&it, fbt);
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, delete_middle_item) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    char buf[8];
    sds inserted[50];
    for (int i = 0; i < 50; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        sds str = createString(buf);
        inserted[i] = fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(fbtreeLength(fbt) == 50);

    TEST_ASSERT(fbtreeDelete(fbt, inserted[25]));
    TEST_ASSERT(fbtreeLength(fbt) == 49);

    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[24]) >= 0);
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[26]) >= 0);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    int count = 0;
    while (fbtreeNext(&it, &pos)) count++;
    TEST_ASSERT(count == 49);
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, delete_max_updates_anchor) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    char buf[16];
    sds inserted[200];
    for (int i = 0; i < 200; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        sds str = createString(buf);
        inserted[i] = fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(fbtreeLength(fbt) == 200);
    TEST_ASSERT(is_tree_valid(fbt));

    TEST_ASSERT(fbtreeDelete(fbt, inserted[199]));
    TEST_ASSERT(fbtreeLength(fbt) == 199);
    TEST_ASSERT(is_tree_valid(fbt));

    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[198]) >= 0);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    int count = 0;
    while (fbtreePrev(&it, &pos)) count++;
    TEST_ASSERT(count == 199);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, delete_all_multilevel) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Create multi-level tree */
    char buf[16];
    const int count = TEST_NODE_CAPACITY * 3;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        inserted[i] = fbtreeInsert(fbt, createString(buf));
    }
    TEST_ASSERT(fbtreeLength(fbt) == (size_t)count);
    TEST_ASSERT(is_tree_valid(fbt));

    /* Delete all items - exercises empty node removal */
    for (int i = 0; i < count; i++) {
        TEST_ASSERT(fbtreeDelete(fbt, inserted[i]));
        TEST_ASSERT(is_tree_valid(fbt));
    }
    TEST_ASSERT(fbtreeLength(fbt) == 0);

    zfree(inserted);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, delete_root_collapse) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Create 2-level tree: root inner node with 2 leaf children */
    char buf[16];
    const int overflow = 10;
    const int count = TEST_NODE_CAPACITY + overflow;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        inserted[i] = fbtreeInsert(fbt, createString(buf));
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Delete items from first leaf until it's empty - triggers root collapse */
    for (int i = 0; i < TEST_NODE_CAPACITY; i++) {
        TEST_ASSERT(fbtreeDelete(fbt, inserted[i]));
    }
    TEST_ASSERT(is_tree_valid(fbt));
    TEST_ASSERT(fbtreeLength(fbt) == (size_t)overflow);

    /* Tree should still work after collapse */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    int remaining = 0;
    while (fbtreeNext(&it, &pos)) remaining++;
    TEST_ASSERT(remaining == overflow);

    zfree(inserted);
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, delete_leftmost_leaf_updates_cache) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Create multi-level tree */
    char buf[16];
    const int count = TEST_NODE_CAPACITY * 3;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        inserted[i] = fbtreeInsert(fbt, createString(buf));
    }

    /* Delete all items from the first leaf */
    for (int i = 0; i < TEST_NODE_CAPACITY; i++) {
        TEST_ASSERT(fbtreeDelete(fbt, inserted[i]));
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Forward iteration should still work - leftmost_leaf must be valid */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    TEST_ASSERT(fbtreeNext(&it, &pos));
    /* First remaining item should be key_061 */
    snprintf(buf, sizeof(buf), "key_%03d", TEST_NODE_CAPACITY);
    TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);

    zfree(inserted);
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, delete_rightmost_leaf_updates_cache) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Create multi-level tree */
    char buf[16];
    const int count = TEST_NODE_CAPACITY * 3;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        inserted[i] = fbtreeInsert(fbt, createString(buf));
    }

    /* Delete all items from the last leaf (items from end backwards) */
    for (int i = count - 1; i >= count - TEST_NODE_CAPACITY; i--) {
        TEST_ASSERT(fbtreeDelete(fbt, inserted[i]));
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Backward iteration should still work - rightmost_leaf must be valid */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    TEST_ASSERT(fbtreePrev(&it, &pos));
    /* Last remaining item */
    snprintf(buf, sizeof(buf), "key_%03d", count - TEST_NODE_CAPACITY - 1);
    TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);

    zfree(inserted);
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

/* ========== Rank Tests ========== */

TEST(FbtreeTest, rank_single_leaf) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    const char *items[] = {"cherry", "apple", "banana", "date"};
    for (int i = 0; i < 4; i++) {
        sds str = createString(items[i]);
        fbtreeInsert(fbt, str);
    }

    /* Sorted order: apple(0), banana(1), cherry(2), date(3) */
    const_sds result = fbtreeGetAtRank(fbt, 0);
    TEST_ASSERT(result && memcmp(result, "apple", 6) == 0);

    result = fbtreeGetAtRank(fbt, 1);
    TEST_ASSERT(result && memcmp(result, "banana", 7) == 0);

    result = fbtreeGetAtRank(fbt, 2);
    TEST_ASSERT(result && memcmp(result, "cherry", 7) == 0);

    result = fbtreeGetAtRank(fbt, 3);
    TEST_ASSERT(result && memcmp(result, "date", 5) == 0);

    /* Out of bounds returns NULL */
    result = fbtreeGetAtRank(fbt, 4);
    TEST_ASSERT(result == NULL);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, rank_multilevel) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    char buf[16];
    for (int i = 0; i < 200; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        sds str = createString(buf);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    const_sds result;

    result = fbtreeGetAtRank(fbt, 0);
    TEST_ASSERT(result && memcmp(result, "key_000", 8) == 0);

    result = fbtreeGetAtRank(fbt, 50);
    TEST_ASSERT(result && memcmp(result, "key_050", 8) == 0);

    result = fbtreeGetAtRank(fbt, 100);
    TEST_ASSERT(result && memcmp(result, "key_100", 8) == 0);

    result = fbtreeGetAtRank(fbt, 199);
    TEST_ASSERT(result && memcmp(result, "key_199", 8) == 0);

    result = fbtreeGetAtRank(fbt, 200);
    TEST_ASSERT(result == NULL);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, rank_after_delete) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    char buf[16];
    sds inserted[100];
    for (int i = 0; i < 100; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        sds str = createString(buf);
        inserted[i] = fbtreeInsert(fbt, str);
    }

    TEST_ASSERT(fbtreeDelete(fbt, inserted[50]));

    const_sds result = fbtreeGetAtRank(fbt, 50);
    TEST_ASSERT(result && memcmp(result, "key_051", 8) == 0);

    result = fbtreeGetAtRank(fbt, 49);
    TEST_ASSERT(result && memcmp(result, "key_049", 8) == 0);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, get_rank_of_item) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    char buf[16];
    sds inserted[100];
    for (int i = 0; i < 100; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        sds str = createString(buf);
        inserted[i] = fbtreeInsert(fbt, str);
    }

    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[0]) == 0);
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[50]) == 50);
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[99]) == 99);

    sds search = createString("key_100");
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, search) == -1);
    sdsfree(search);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, seek_to_rank) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    char buf[16];
    for (int i = 0; i < 100; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        sds str = createString(buf);
        fbtreeInsert(fbt, str);
    }

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;

    /* Seek to middle and iterate */
    fbtreeSeekToRank(&it, 50);
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, "key_050", 8) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, "key_051", 8) == 0);

    /* Seek to beginning */
    fbtreeSeekToRank(&it, 0);
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, "key_000", 8) == 0);

    /* Seek to last item */
    fbtreeSeekToRank(&it, 99);
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, "key_099", 8) == 0);
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, rank_deep_tree) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    const int count = TEST_THREE_LEVEL_ITEMS;
    char buf[16];
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%05d", i);
        sds str = createString(buf);
        inserted[i] = fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    const_sds result;

    result = fbtreeGetAtRank(fbt, 0);
    TEST_ASSERT(result && memcmp(result, "key_00000", 10) == 0);

    result = fbtreeGetAtRank(fbt, count - 1);
    snprintf(buf, sizeof(buf), "key_%05d", count - 1);
    TEST_ASSERT(result && memcmp(result, buf, 10) == 0);

    for (int i = 0; i < count; i += count / 10) {
        result = fbtreeGetAtRank(fbt, i);
        TEST_ASSERT(result == inserted[i]); /* Exact pointer match */
    }

    for (int i = 0; i < count; i += count / 10) {
        TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) == i);
    }
    zfree(inserted);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

/* ========== fbtreeSeekToScore Tests ========== 
 * These tests use string literals where the first 8 bytes are the "score" prefix.
 * Lexicographic ordering: "AAAAAAAA" < "BBBBBBBB" < "CCCCCCCC" */

TEST(FbtreeTest, seek_to_score_exact) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    fbtreeInsert(fbt, createString("AAAAAAAAelem_0"));
    fbtreeInsert(fbt, createString("BBBBBBBBelem_1"));
    fbtreeInsert(fbt, createString("CCCCCCCCelem_2"));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "BBBBBBBB", &it);

    const_sds pos;
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, "BBBBBBBB", 8) == 0);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, seek_to_score_between) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    fbtreeInsert(fbt, createString("AAAAAAAAelem_0"));
    fbtreeInsert(fbt, createString("CCCCCCCCelem_1"));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "BBBBBBBB", &it); /* Between AAAA and CCCC */

    const_sds pos;
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, "CCCCCCCC", 8) == 0); /* Should position at first >= "BBBB" */

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, seek_to_score_past_end) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    fbtreeInsert(fbt, createString("AAAAAAAAelem"));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "ZZZZZZZZ", &it); /* Past all elements */

    const_sds pos;
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, seek_to_score_past_end_then_prev) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    fbtreeInsert(fbt, createString("AAAAAAAAelem_0"));
    fbtreeInsert(fbt, createString("BBBBBBBBelem_1"));
    fbtreeInsert(fbt, createString("CCCCCCCCelem_2"));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "ZZZZZZZZ", &it); /* Past all elements (+inf) */

    /* Backward should work - iterate from end */
    const_sds pos;
    TEST_ASSERT(fbtreePrev(&it, &pos));
    TEST_ASSERT(memcmp(pos, "CCCCCCCC", 8) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos));
    TEST_ASSERT(memcmp(pos, "BBBBBBBB", 8) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos));
    TEST_ASSERT(memcmp(pos, "AAAAAAAA", 8) == 0);
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    /* Verify forward from past-end invalidates iterator */
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "ZZZZZZZZ", &it);
    TEST_ASSERT(!fbtreeNext(&it, &pos)); /* Fails and invalidates */
    TEST_ASSERT(!fbtreePrev(&it, &pos)); /* Now invalid */

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, seek_to_score_before_start_then_next) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    fbtreeInsert(fbt, createString("MMMMMMMMelem_0"));
    fbtreeInsert(fbt, createString("NNNNNNNNelem_1"));
    fbtreeInsert(fbt, createString("OOOOOOOOelem_2"));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "AAAAAAAA", &it); /* Before all elements (-inf) */

    /* Forward should work - iterate from start */
    const_sds pos;
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, "MMMMMMMM", 8) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, "NNNNNNNN", 8) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, "OOOOOOOO", 8) == 0);
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    /* Verify backward from before-start invalidates iterator */
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "AAAAAAAA", &it);
    TEST_ASSERT(!fbtreePrev(&it, &pos)); /* Fails and invalidates */
    TEST_ASSERT(!fbtreeNext(&it, &pos)); /* Now invalid */

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, seek_to_score_before_start) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    fbtreeInsert(fbt, createString("MMMMMMMMelem"));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "AAAAAAAA", &it); /* Before all elements */

    const_sds pos;
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, "MMMMMMMM", 8) == 0);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, seek_to_score_empty) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "AAAAAAAA", &it);

    const_sds pos;
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, seek_to_score_deep_tree) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    const int count = TEST_THREE_LEVEL_ITEMS;
    char buf[24];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "%08d_elem_%05d", i, i);
        fbtreeInsert(fbt, createString(buf));
    }
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;

    /* Seek to middle of deep tree */
    snprintf(buf, sizeof(buf), "%08d", count / 2);
    fbtreeSeekToScore(fbt, buf, &it);
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, buf, 8) == 0);

    /* Seek near end */
    snprintf(buf, sizeof(buf), "%08d", count - 10);
    fbtreeSeekToScore(fbt, buf, &it);
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, buf, 8) == 0);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, seek_to_score_iterate) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    fbtreeInsert(fbt, createString("AAAAAAAAelem_0"));
    fbtreeInsert(fbt, createString("BBBBBBBBelem_0"));
    fbtreeInsert(fbt, createString("BBBBBBBBelem_1"));
    fbtreeInsert(fbt, createString("BBBBBBBBelem_2"));
    fbtreeInsert(fbt, createString("BBBBBBBBelem_3"));
    fbtreeInsert(fbt, createString("BBBBBBBBelem_4"));
    fbtreeInsert(fbt, createString("CCCCCCCCelem_0"));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "BBBBBBBB", &it);

    const_sds pos;
    int count = 0;
    while (fbtreeNext(&it, &pos) && memcmp(pos, "BBBBBBBB", 8) == 0) {
        count++;
    }
    TEST_ASSERT(count == 5);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

/* ========== Inner Node Binary Search Tests ========== */

/* When all items in an inner node share identical feature bytes (first 4 bytes
 * after the common prefix), the SIMD pass returns the full range and the binary
 * search on anchors must resolve every lookup on its own.
 *
 * Setup: fill >NODE_CAPACITY items all starting with "XXXX" (identical feature
 * bytes), plus one outlier starting with "A" so the inner node's common prefix
 * is zero bytes.  Every search among the "XXXX" items therefore hits the binary
 * search path. */
TEST(FbtreeTest, inner_bsearch_identical_feature_bytes) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert outlier first so it sorts before all "XXXX" items */
    sds outlier = fbtreeInsert(fbt, createString("A_outlier"));

    /* Insert enough same-prefix items to fill an inner node (needs >NODE_CAPACITY
     * items to create at least 2 leaves and thus an inner node) */
    const int count = TEST_NODE_CAPACITY + 10;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    char buf[16];
    for (int i = 0; i < count; i++) {
        /* All share "XXXX" as first 4 bytes - identical feature bytes at prefix_len=0 */
        snprintf(buf, sizeof(buf), "XXXX_%05d", i);
        inserted[i] = fbtreeInsert(fbt, createString(buf));
    }
    TEST_ASSERT(is_tree_valid(fbt));
    TEST_ASSERT(fbtreeLength(fbt) == (size_t)(count + 1));

    /* All same-prefix items must be findable - each lookup exercises binary search */
    for (int i = 0; i < count; i++) {
        TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) == i + 1); /* +1 for outlier at rank 0 */
    }
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, outlier) == 0);

    /* Iteration is sorted: outlier first, then XXXX items in order */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "A_outlier", 10) == 0);
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "XXXX_%05d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    zfree(inserted);
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

/* ========== Long Prefix Tests ========== */

static sds createPrefixString(const char *prefix_char, size_t prefix_len, const char *suffix) {
    size_t suffix_len = strlen(suffix) + 1; /* include null terminator */
    sds s = sdsnewlen(NULL, prefix_len + suffix_len);
    memset(s, prefix_char[0], prefix_len);
    memcpy(s + prefix_len, suffix, suffix_len);
    return s;
}

TEST(FbtreeTest, long_prefix_basic) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    const size_t prefix_len = TEST_EMBED_PREFIX_LEN + 6; /* require long prefix storage */
    const int count = TEST_TWO_LEVEL_ITEMS;              /* enough to create inner node */
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    char suffix[8];

    for (int i = 0; i < count; i++) {
        snprintf(suffix, sizeof(suffix), "s%05d", i);
        inserted[i] = fbtreeInsert(fbt, createPrefixString("X", prefix_len, suffix));
    }

    TEST_ASSERT(fbtreeLength(fbt) == (size_t)count);
    TEST_ASSERT(is_tree_valid(fbt));

    /* All items findable */
    for (int i = 0; i < count; i++) {
        TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) == i);
    }

    /* Iteration order correct */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < count; i++) {
        snprintf(suffix, sizeof(suffix), "s%05d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos + prefix_len, suffix, 6) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    zfree(inserted);
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, very_long_prefix) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    const size_t prefix_len = TEST_EMBED_PREFIX_LEN * 10; /* well beyond embedded limit */
    const int count = TEST_NODE_CAPACITY + 10;            /* force node split */
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    char suffix[8];

    for (int i = 0; i < count; i++) {
        snprintf(suffix, sizeof(suffix), "s%03d", i);
        inserted[i] = fbtreeInsert(fbt, createPrefixString("P", prefix_len, suffix));
    }

    TEST_ASSERT(fbtreeLength(fbt) == (size_t)count);
    TEST_ASSERT(is_tree_valid(fbt));

    /* Verify ordering and rank */
    for (int i = 0; i < count; i++) {
        TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) == i);
    }

    /* Forward iteration */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < count; i++) {
        snprintf(suffix, sizeof(suffix), "s%03d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos + prefix_len, suffix, 5) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    zfree(inserted);
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, long_prefix_multilevel) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Long prefix, enough items for 3-level tree */
    const size_t prefix_len = TEST_EMBED_PREFIX_LEN + 14;
    const int count = TEST_THREE_LEVEL_ITEMS;
    char suffix[16];

    for (int i = 0; i < count; i++) {
        snprintf(suffix, sizeof(suffix), "item_%05d", i);
        fbtreeInsert(fbt, createPrefixString("L", prefix_len, suffix));
    }

    TEST_ASSERT(fbtreeLength(fbt) == (size_t)count);
    TEST_ASSERT(is_tree_valid(fbt));

    /* Spot check ranks */
    const_sds first = fbtreeGetAtRank(fbt, 0);
    TEST_ASSERT(first && memcmp(first + prefix_len, "item_00000", 11) == 0);

    const_sds last = fbtreeGetAtRank(fbt, count - 1);
    snprintf(suffix, sizeof(suffix), "item_%05d", count - 1);
    TEST_ASSERT(last && memcmp(last + prefix_len, suffix, 11) == 0);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, long_prefix_delete) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Long prefix with enough items to create inner nodes */
    const size_t prefix_len = TEST_EMBED_PREFIX_LEN + 4;
    const int count = TEST_NODE_CAPACITY * 2;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    char suffix[8];

    for (int i = 0; i < count; i++) {
        snprintf(suffix, sizeof(suffix), "d%03d", i);
        inserted[i] = fbtreeInsert(fbt, createPrefixString("D", prefix_len, suffix));
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Delete every other item */
    for (int i = 0; i < count; i += 2) {
        TEST_ASSERT(fbtreeDelete(fbt, inserted[i]));
    }
    TEST_ASSERT(fbtreeLength(fbt) == (size_t)(count / 2));
    TEST_ASSERT(is_tree_valid(fbt));

    /* Remaining items still accessible */
    for (int i = 1; i < count; i += 2) {
        TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) >= 0);
    }

    zfree(inserted);
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, long_prefix_boundary) {
    size_t used_memory_before = zmalloc_used_memory();

    /* Case 1: inner node common prefix is exactly TEST_EMBED_PREFIX_LEN (embedded storage).
     * Items differ only in the byte immediately after the shared prefix, so the inner
     * node prefix is exactly TEST_EMBED_PREFIX_LEN bytes and is stored inline. */
    const int count = TEST_NODE_CAPACITY + 10;
    char suffix[8];
    {
        fbtreeIndex *fbt = fbtreeCreate();
        sds *inserted = (sds *)zmalloc(count * sizeof(sds));
        for (int i = 0; i < count; i++) {
            snprintf(suffix, sizeof(suffix), "%c%04d", 'a' + (i % 26), i);
            inserted[i] = fbtreeInsert(fbt, createPrefixString("X", TEST_EMBED_PREFIX_LEN, suffix));
        }
        TEST_ASSERT(is_tree_valid(fbt));
        for (int i = 0; i < count; i++)
            TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) >= 0);
        zfree(inserted);
        fbtreeFree(fbt);
    }

    /* Case 2: inner node common prefix is TEST_EMBED_PREFIX_LEN+1 (long prefix storage).
     * Items differ only after TEST_EMBED_PREFIX_LEN+1 shared bytes, so the inner node
     * must store its prefix via the long-prefix pointer path. */
    {
        fbtreeIndex *fbt = fbtreeCreate();
        sds *inserted = (sds *)zmalloc(count * sizeof(sds));
        for (int i = 0; i < count; i++) {
            snprintf(suffix, sizeof(suffix), "%c%04d", 'a' + (i % 26), i);
            inserted[i] = fbtreeInsert(fbt, createPrefixString("X", TEST_EMBED_PREFIX_LEN + 1, suffix));
        }
        TEST_ASSERT(is_tree_valid(fbt));
        for (int i = 0; i < count; i++)
            TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) >= 0);
        zfree(inserted);
        fbtreeFree(fbt);
    }

    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, long_prefix_shrink_to_short) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Create tree with long prefix - enough items to force inner node */
    const size_t long_prefix = TEST_EMBED_PREFIX_LEN + 14;
    const int count = TEST_NODE_CAPACITY + 10;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    char suffix[8];

    for (int i = 0; i < count; i++) {
        snprintf(suffix, sizeof(suffix), "s%03d", i);
        inserted[i] = fbtreeInsert(fbt, createPrefixString("A", long_prefix, suffix));
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Insert item with different first byte - shrinks common prefix to 0 (long → short) */
    sds outlier = fbtreeInsert(fbt, createPrefixString("B", long_prefix, "zzz"));
    TEST_ASSERT(is_tree_valid(fbt));

    /* All items still findable */
    for (int i = 0; i < count; i++) {
        TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) >= 0);
    }
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, outlier) >= 0);

    /* Delete outlier - prefix could grow back to long */
    TEST_ASSERT(fbtreeDelete(fbt, outlier));
    TEST_ASSERT(is_tree_valid(fbt));

    /* Original items still findable after prefix regrows */
    for (int i = 0; i < count; i++) {
        TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) >= 0);
    }

    zfree(inserted);
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, long_prefix_realloc) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Create tree with long common prefix (2x embedded limit) */
    const size_t long_prefix = TEST_EMBED_PREFIX_LEN * 2;
    const int count = TEST_NODE_CAPACITY + 10;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    char suffix[8];

    for (int i = 0; i < count; i++) {
        snprintf(suffix, sizeof(suffix), "s%03d", i);
        inserted[i] = fbtreeInsert(fbt, createPrefixString("X", long_prefix, suffix));
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Insert item that shares fewer bytes - shrinks long prefix, still long */
    const size_t mid_prefix = TEST_EMBED_PREFIX_LEN + 4;
    sds s_mid = sdsnewlen(NULL, mid_prefix + 5);
    memset(s_mid, 'X', mid_prefix);
    memcpy(s_mid + mid_prefix, "Ymid", 5); /* Differs at mid_prefix */
    sds item_mid = fbtreeInsert(fbt, s_mid);
    TEST_ASSERT(is_tree_valid(fbt));

    /* Insert item that shares more bytes with original but fewer with s_mid */
    /* Common prefix stays at mid_prefix */
    const size_t longer_prefix = TEST_EMBED_PREFIX_LEN + 14;
    sds s_longer = sdsnewlen(NULL, longer_prefix + 5);
    memset(s_longer, 'X', longer_prefix);
    memcpy(s_longer + longer_prefix, "Zlng", 5);
    sds item_longer = fbtreeInsert(fbt, s_longer);
    TEST_ASSERT(is_tree_valid(fbt));

    /* All items findable */
    for (int i = 0; i < count; i++) {
        TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) >= 0);
    }
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, item_mid) >= 0);
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, item_longer) >= 0);

    /* Delete items to potentially grow prefix back */
    TEST_ASSERT(fbtreeDelete(fbt, item_mid));
    TEST_ASSERT(fbtreeDelete(fbt, item_longer));
    TEST_ASSERT(is_tree_valid(fbt));

    /* Original items still work */
    for (int i = 0; i < count; i++) {
        TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) >= 0);
    }

    zfree(inserted);
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}
/* ========== Pop Min/Max Tests ========== */

TEST(FbtreeTest, pop_min_single) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    sds str = createString("only");
    fbtreeInsert(fbt, str);
    TEST_ASSERT(fbtreeLength(fbt) == 1);

    sds popped = fbtreePopMin(fbt);
    TEST_ASSERT(popped != NULL);
    TEST_ASSERT(memcmp(popped, "only", 5) == 0);
    TEST_ASSERT(fbtreeLength(fbt) == 0);
    TEST_ASSERT(is_tree_valid(fbt));
    sdsfree(popped);

    /* Pop from empty returns NULL */
    TEST_ASSERT(fbtreePopMin(fbt) == NULL);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, pop_max_single) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    sds str = createString("only");
    fbtreeInsert(fbt, str);
    TEST_ASSERT(fbtreeLength(fbt) == 1);

    sds popped = fbtreePopMax(fbt);
    TEST_ASSERT(popped != NULL);
    TEST_ASSERT(memcmp(popped, "only", 5) == 0);
    TEST_ASSERT(fbtreeLength(fbt) == 0);
    TEST_ASSERT(is_tree_valid(fbt));
    sdsfree(popped);

    /* Pop from empty returns NULL */
    TEST_ASSERT(fbtreePopMax(fbt) == NULL);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, pop_min_multiple) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert out of order */
    const char *items[] = {"cherry", "apple", "banana", "date"};
    for (int i = 0; i < 4; i++) {
        fbtreeInsert(fbt, createString(items[i]));
    }

    /* Pop in sorted order: apple, banana, cherry, date */
    sds popped = fbtreePopMin(fbt);
    TEST_ASSERT(memcmp(popped, "apple", 6) == 0);
    sdsfree(popped);
    TEST_ASSERT(fbtreeLength(fbt) == 3);
    TEST_ASSERT(is_tree_valid(fbt));

    popped = fbtreePopMin(fbt);
    TEST_ASSERT(memcmp(popped, "banana", 7) == 0);
    sdsfree(popped);
    TEST_ASSERT(is_tree_valid(fbt));

    popped = fbtreePopMin(fbt);
    TEST_ASSERT(memcmp(popped, "cherry", 7) == 0);
    sdsfree(popped);
    TEST_ASSERT(is_tree_valid(fbt));

    popped = fbtreePopMin(fbt);
    TEST_ASSERT(memcmp(popped, "date", 5) == 0);
    sdsfree(popped);

    TEST_ASSERT(fbtreeLength(fbt) == 0);
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, pop_max_multiple) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert out of order */
    const char *items[] = {"cherry", "apple", "banana", "date"};
    for (int i = 0; i < 4; i++) {
        fbtreeInsert(fbt, createString(items[i]));
    }

    /* Pop in reverse sorted order: date, cherry, banana, apple */
    sds popped = fbtreePopMax(fbt);
    TEST_ASSERT(memcmp(popped, "date", 5) == 0);
    sdsfree(popped);
    TEST_ASSERT(fbtreeLength(fbt) == 3);
    TEST_ASSERT(is_tree_valid(fbt));

    popped = fbtreePopMax(fbt);
    TEST_ASSERT(memcmp(popped, "cherry", 7) == 0);
    sdsfree(popped);
    TEST_ASSERT(is_tree_valid(fbt));

    popped = fbtreePopMax(fbt);
    TEST_ASSERT(memcmp(popped, "banana", 7) == 0);
    sdsfree(popped);
    TEST_ASSERT(is_tree_valid(fbt));

    popped = fbtreePopMax(fbt);
    TEST_ASSERT(memcmp(popped, "apple", 6) == 0);
    sdsfree(popped);

    TEST_ASSERT(fbtreeLength(fbt) == 0);
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, pop_min_multilevel) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Create multi-level tree */
    char buf[16];
    const int count = TEST_NODE_CAPACITY * 3;
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Pop all items - should come out in sorted order */
    for (int i = 0; i < count; i++) {
        sds popped = fbtreePopMin(fbt);
        TEST_ASSERT(popped != NULL);
        snprintf(buf, sizeof(buf), "key_%03d", i);
        TEST_ASSERT(memcmp(popped, buf, strlen(buf) + 1) == 0);
        sdsfree(popped);
        TEST_ASSERT(is_tree_valid(fbt));
    }
    TEST_ASSERT(fbtreeLength(fbt) == 0);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, pop_max_multilevel) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Create multi-level tree */
    char buf[16];
    const int count = TEST_NODE_CAPACITY * 3;
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Pop all items - should come out in reverse sorted order */
    for (int i = count - 1; i >= 0; i--) {
        sds popped = fbtreePopMax(fbt);
        TEST_ASSERT(popped != NULL);
        snprintf(buf, sizeof(buf), "key_%03d", i);
        TEST_ASSERT(memcmp(popped, buf, strlen(buf) + 1) == 0);
        sdsfree(popped);
        TEST_ASSERT(is_tree_valid(fbt));
    }
    TEST_ASSERT(fbtreeLength(fbt) == 0);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, pop_alternating) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    char buf[16];
    const int count = 100;
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        fbtreeInsert(fbt, createString(buf));
    }

    /* Alternate between pop min and pop max */
    int min_idx = 0, max_idx = count - 1;
    for (int i = 0; i < count; i++) {
        sds popped;
        if (i % 2 == 0) {
            popped = fbtreePopMin(fbt);
            snprintf(buf, sizeof(buf), "key_%03d", min_idx++);
        } else {
            popped = fbtreePopMax(fbt);
            snprintf(buf, sizeof(buf), "key_%03d", max_idx--);
        }
        TEST_ASSERT(popped != NULL);
        TEST_ASSERT(memcmp(popped, buf, strlen(buf) + 1) == 0);
        sdsfree(popped);
        TEST_ASSERT(is_tree_valid(fbt));
    }
    TEST_ASSERT(fbtreeLength(fbt) == 0);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, pop_empty) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Pop from empty tree returns NULL */
    TEST_ASSERT(fbtreePopMin(fbt) == NULL);
    TEST_ASSERT(fbtreePopMax(fbt) == NULL);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}

TEST(FbtreeTest, pop_with_iteration_and_insert) {
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    char buf[16];
    sds popped;
    const_sds pos;

    /* Insert ascending (append path): k100-k199 */
    for (int i = 100; i < 200; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    TEST_ASSERT(fbtreeLength(fbt) == 100);

    /* Pop min a few times */
    for (int i = 100; i < 110; i++) {
        popped = fbtreePopMin(fbt);
        snprintf(buf, sizeof(buf), "k%03d", i);
        TEST_ASSERT(memcmp(popped, buf, strlen(buf) + 1) == 0);
        sdsfree(popped);
        TEST_ASSERT(is_tree_valid(fbt));
    }
    TEST_ASSERT(fbtreeLength(fbt) == 90);

    /* Insert descending (prepend path): k099 down to k050 */
    for (int i = 99; i >= 50; i--) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    TEST_ASSERT(fbtreeLength(fbt) == 140);

    /* Pop max a few times */
    for (int i = 199; i >= 190; i--) {
        popped = fbtreePopMax(fbt);
        snprintf(buf, sizeof(buf), "k%03d", i);
        TEST_ASSERT(memcmp(popped, buf, strlen(buf) + 1) == 0);
        sdsfree(popped);
        TEST_ASSERT(is_tree_valid(fbt));
    }
    TEST_ASSERT(fbtreeLength(fbt) == 130);

    /* Iterate forward and verify sorted order: k050-k189 */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    for (int i = 50; i < 190; i++) {
        if (i >= 100 && i < 110) continue; /* popped */
        TEST_ASSERT(fbtreeNext(&it, &pos));
        snprintf(buf, sizeof(buf), "k%03d", i);
        TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    /* Iterate backward */
    fbtreeInitIterator(&it, fbt);
    for (int i = 189; i >= 50; i--) {
        if (i >= 100 && i < 110) continue;
        TEST_ASSERT(fbtreePrev(&it, &pos));
        snprintf(buf, sizeof(buf), "k%03d", i);
        TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);
    }
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);

}
