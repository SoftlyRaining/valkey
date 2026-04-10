/*
 * Copyright (c) Valkey Contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "generated_wrappers.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <vector>

extern "C" {
#include "fbtree_ordered_index.h"
#include "sds.h"
#include "zmalloc.h"
}

/* ========== Constants matching fbtree_ordered_index.c internals ========== */

/* Node capacity - must match NODE_SIZE in fbtree_ordered_index.c */
#define TEST_NODE_CAPACITY 61
#define TEST_TWO_LEVEL_ITEMS (TEST_NODE_CAPACITY * TEST_NODE_CAPACITY)
#define TEST_THREE_LEVEL_ITEMS (TEST_TWO_LEVEL_ITEMS + 200)

/* Size limit of embedded prefix - must match EMBED_PREFIX_LEN in fbtree_ordered_index.c */
#define TEST_EMBED_PREFIX_LEN 254

/* ========== Test Helpers ========== */

/* Create a null-terminated sds from a C string. */
static sds createString(const char *str) {
    size_t len = strlen(str) + 1;
    return sdsnewlen(str, len);
}

/* Create a string like "prefix" + base-26 encoded value + "suffix".
 * E.g., value=0 -> "AAA", value=1 -> "AAB", value=26 -> "ABA". */
static sds createBase26TestString(const char *prefix, const char *suffix, size_t value, size_t value_width) {
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);
    size_t len = prefix_len + suffix_len + value_width + 1;
    sds s = sdsnewlen(NULL, len);
    memcpy(s, prefix, prefix_len);

    for (size_t i = 0; i < value_width; i++) {
        char c = (char)('A' + (value % 26));
        s[prefix_len + value_width - 1 - i] = c;
        value /= 26;
    }

    memcpy(s + prefix_len + value_width, suffix, suffix_len);
    s[len - 1] = '\0';
    return s;
}

static sds createPrefixString(const char *prefix_char, size_t prefix_len, const char *suffix) {
    size_t suffix_len = strlen(suffix) + 1; /* include null terminator */
    sds s = sdsnewlen(NULL, prefix_len + suffix_len);
    memset(s, prefix_char[0], prefix_len);
    memcpy(s + prefix_len, suffix, suffix_len);
    return s;
}

/* ========== RAII Fixture ========== */

/* Base fixture that handles tree lifecycle and memory-leak detection. */
class FbtreeTest : public ::testing::Test {
  protected:
    fbtreeIndex *fbt = nullptr;
    size_t mem_before = 0;

    void SetUp() override {
        mem_before = zmalloc_used_memory();
        fbt = fbtreeCreate();
    }

    void TearDown() override {
        if (fbt) fbtreeFree(fbt);
        EXPECT_EQ(zmalloc_used_memory(), mem_before) << "Memory leak detected";
    }

    /* Validate tree invariants. */
    void expectValid() {
        ASSERT_TRUE(fbtreeDebugValidate(fbt, false)) << "Tree invariant violation";
    }

    /* Insert a null-terminated C string, return the stored pointer. */
    sds insert(const char *str) {
        return fbtreeInsert(fbt, createString(str));
    }

    /* Collect all elements via forward iteration into a vector. */
    std::vector<std::string> collectForward() {
        std::vector<std::string> result;
        fbtreeIterator it;
        fbtreeInitIterator(&it, fbt);
        const_sds pos;
        while (fbtreeNext(&it, &pos)) {
            result.emplace_back(pos, sdslen(pos));
        }
        return result;
    }

    /* Collect all elements via backward iteration into a vector. */
    std::vector<std::string> collectBackward() {
        std::vector<std::string> result;
        fbtreeIterator it;
        fbtreeInitIterator(&it, fbt);
        const_sds pos;
        while (fbtreePrev(&it, &pos)) {
            result.emplace_back(pos, sdslen(pos));
        }
        return result;
    }
};

/* ========== Basic Lifecycle Tests ========== */

TEST_F(FbtreeTest, CreateAndFree) {
    ASSERT_NE(fbt, nullptr);
    expectValid();
    /* TearDown verifies no leak */
}

/* Verify node sizes fit expected jemalloc size classes.
 * innerNode should fit in 2048-byte class, leafNode in 512-byte class.
 * This catches accidental struct bloat that wastes memory. */
TEST_F(FbtreeTest, NodeAllocationSizes) {
    void *inner_test = zmalloc(2048); /* sizeof(innerNode) rounds up to 2048 */
    void *leaf_test = zmalloc(512);   /* sizeof(leafNode) */

    EXPECT_EQ(zmalloc_usable_size(inner_test), 2048u);
    EXPECT_EQ(zmalloc_usable_size(leaf_test), 512u);

    zfree(inner_test);
    zfree(leaf_test);
}

/* ========== Insert & Lookup Tests ========== */

TEST_F(FbtreeTest, InsertAndLookup) {
    sds inserted = insert("hello");
    expectValid();

    EXPECT_GE(fbtreeGetRankOfItem(fbt, inserted), 0);

    sds missing = createString("world");
    EXPECT_LT(fbtreeGetRankOfItem(fbt, missing), 0);
    sdsfree(missing);
}

TEST_F(FbtreeTest, InsertMultiple) {
    const char *strings[] = {"apple", "banana", "cherry", "date", "elderberry", "elder"};
    sds inserted[6];
    for (int i = 0; i < 6; i++) {
        inserted[i] = insert(strings[i]);
    }
    expectValid();

    for (int i = 0; i < 6; i++) {
        EXPECT_GE(fbtreeGetRankOfItem(fbt, inserted[i]), 0);
    }

    sds missing = createString("fig");
    EXPECT_LT(fbtreeGetRankOfItem(fbt, missing), 0);
    sdsfree(missing);
}

TEST_F(FbtreeTest, LookupEmptyTree) {
    expectValid();
    sds s = createString("anything");
    EXPECT_LT(fbtreeGetRankOfItem(fbt, s), 0);
    sdsfree(s);
}

TEST_F(FbtreeTest, LengthIncrements) {
    EXPECT_EQ(fbtreeLength(fbt), 0u);
    insert("first");
    EXPECT_EQ(fbtreeLength(fbt), 1u);
    insert("second");
    EXPECT_EQ(fbtreeLength(fbt), 2u);
    insert("third");
    EXPECT_EQ(fbtreeLength(fbt), 3u);
    expectValid();
}

TEST_F(FbtreeTest, DuplicateInsert) {
    sds ins1 = insert("key");
    EXPECT_EQ(fbtreeLength(fbt), 1u);

    sds ins2 = insert("key");
    EXPECT_EQ(fbtreeLength(fbt), 2u);
    expectValid();

    EXPECT_GE(fbtreeGetRankOfItem(fbt, ins1), 0);
    EXPECT_GE(fbtreeGetRankOfItem(fbt, ins2), 0);
}

TEST_F(FbtreeTest, EmptyString) {
    sds inserted = insert("");
    expectValid();
    EXPECT_GE(fbtreeGetRankOfItem(fbt, inserted), 0);
}

TEST_F(FbtreeTest, LongStrings) {
    char long_str[256];
    memset(long_str, 'a', 255);
    long_str[255] = '\0';

    sds inserted = insert(long_str);
    expectValid();
    EXPECT_GE(fbtreeGetRankOfItem(fbt, inserted), 0);
}

/* Binary data with embedded null bytes - sds handles this, tree should too. */
TEST_F(FbtreeTest, BinaryDataWithNullBytes) {
    /* Create sds with embedded nulls: "ab\0cd\0ef" */
    char data1[] = {'a', 'b', '\0', 'c', 'd', '\0', 'e', 'f'};
    char data2[] = {'a', 'b', '\0', 'c', 'e', '\0', 'e', 'f'}; /* differs at byte 4 */
    char data3[] = {'a', 'b', '\0', 'c', 'd', '\0', 'e', 'g'}; /* differs at byte 7 */

    sds s1 = fbtreeInsert(fbt, sdsnewlen(data1, sizeof(data1)));
    sds s2 = fbtreeInsert(fbt, sdsnewlen(data2, sizeof(data2)));
    sds s3 = fbtreeInsert(fbt, sdsnewlen(data3, sizeof(data3)));
    expectValid();

    EXPECT_EQ(fbtreeLength(fbt), 3u);
    EXPECT_GE(fbtreeGetRankOfItem(fbt, s1), 0);
    EXPECT_GE(fbtreeGetRankOfItem(fbt, s2), 0);
    EXPECT_GE(fbtreeGetRankOfItem(fbt, s3), 0);

    /* Verify sorted order via iteration */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    const_sds prev = nullptr;
    int count = 0;
    while (fbtreeNext(&it, &pos)) {
        if (prev) {
            EXPECT_LT(sdscmp(prev, pos), 0);
        }
        prev = pos;
        count++;
    }
    EXPECT_EQ(count, 3);
}

/* ========== Forward Iterator Tests ========== */

TEST_F(FbtreeTest, IteratorSmall) {
    const char *strings[] = {"dog", "cat", "ant", "bat"};
    for (auto s : strings) insert(s);
    expectValid();

    auto items = collectForward();
    ASSERT_EQ(items.size(), 4u);
    EXPECT_EQ(items[0], std::string("ant\0", 4));
    EXPECT_EQ(items[1], std::string("bat\0", 4));
    EXPECT_EQ(items[2], std::string("cat\0", 4));
    EXPECT_EQ(items[3], std::string("dog\0", 4));
}

TEST_F(FbtreeTest, IteratorFullLeaf) {
    const int count = TEST_NODE_CAPACITY + 1;
    char buf[8];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        insert(buf);
    }
    expectValid();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        EXPECT_EQ(sdslen(pos), 4u);
        EXPECT_EQ(memcmp(pos, buf, 4), 0);
    }
    EXPECT_FALSE(fbtreeNext(&it, &pos));
}

TEST_F(FbtreeTest, IteratorReverseInsert) {
    const int count = TEST_NODE_CAPACITY + 1;
    char buf[8];
    for (int i = count - 1; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        insert(buf);
    }
    expectValid();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        EXPECT_EQ(memcmp(pos, buf, 4), 0);
    }
    EXPECT_FALSE(fbtreeNext(&it, &pos));
}

TEST_F(FbtreeTest, IteratorEmpty) {
    expectValid();
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    EXPECT_FALSE(fbtreeNext(&it, &pos));
}

TEST_F(FbtreeTest, IteratorResetInvalidates) {
    for (auto s : {"a", "b", "c"}) insert(s);
    expectValid();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    ASSERT_TRUE(fbtreeNext(&it, &pos));

    fbtreeResetIterator(&it);
    EXPECT_FALSE(fbtreeNext(&it, &pos));
}

/* ========== Ordering Tests ========== */

TEST_F(FbtreeTest, MultilevelReverseInsert) {
    char buf[8];
    for (int i = 95; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        insert(buf);
    }
    expectValid();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i <= 95; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        EXPECT_EQ(memcmp(pos, buf, 5), 0);
    }
    EXPECT_FALSE(fbtreeNext(&it, &pos));

    fbtreeInitIterator(&it, fbt);
    for (int i = 95; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        ASSERT_TRUE(fbtreePrev(&it, &pos));
        EXPECT_EQ(memcmp(pos, buf, 5), 0);
    }
    EXPECT_FALSE(fbtreePrev(&it, &pos));
}

TEST_F(FbtreeTest, PrefixOrdering) {
    const char *strings[] = {"elderberry", "elder", "e", "elderly"};
    for (auto s : strings) insert(s);
    expectValid();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "e", 2), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "elder", 6), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "elderberry", 11), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "elderly", 8), 0);
    EXPECT_FALSE(fbtreeNext(&it, &pos));
}

TEST_F(FbtreeTest, SameLengthOrdering) {
    const char *strings[] = {"zoo", "abc", "xyz", "def"};
    for (auto s : strings) insert(s);
    expectValid();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "abc", 4), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "def", 4), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "xyz", 4), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "zoo", 4), 0);
    EXPECT_FALSE(fbtreeNext(&it, &pos));
}

TEST_F(FbtreeTest, CommonPrefixOrdering) {
    const char *keys[] = {"prefix_aaa", "prefix_aab", "prefix_aac", "prefix_aad",
                          "prefix_baa", "prefix_bab", "prefix_bac", "prefix_bad",
                          "prefix_caa", "prefix_cab", "prefix_cac", "prefix_cad"};
    sds inserted[12];
    for (int i = 0; i < 12; i++) {
        inserted[i] = insert(keys[i]);
    }
    expectValid();

    for (int i = 0; i < 12; i++) {
        EXPECT_GE(fbtreeGetRankOfItem(fbt, inserted[i]), 0);
    }

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < 12; i++) {
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        size_t len = strlen(keys[i]) + 1;
        EXPECT_EQ(memcmp(pos, keys[i], len), 0);
    }
    EXPECT_FALSE(fbtreeNext(&it, &pos));
}

TEST_F(FbtreeTest, InsertBatchesSorted) {
    for (auto s : {"dog", "cat", "ant"}) insert(s);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "ant", 4), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "cat", 4), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "dog", 4), 0);
    EXPECT_FALSE(fbtreeNext(&it, &pos));

    for (auto s : {"bat", "elk"}) insert(s);
    expectValid();

    fbtreeInitIterator(&it, fbt);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "ant", 4), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "bat", 4), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "cat", 4), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "dog", 4), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "elk", 4), 0);
    EXPECT_FALSE(fbtreeNext(&it, &pos));
}

TEST_F(FbtreeTest, InsertAtBoundaries) {
    insert("m");
    for (auto s : {"a", "z", "n"}) insert(s);
    expectValid();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "a", 2), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "m", 2), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "n", 2), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "z", 2), 0);
    EXPECT_FALSE(fbtreeNext(&it, &pos));
}

/* ========== Backward Iterator (Prev) Tests ========== */

TEST_F(FbtreeTest, PrevSmall) {
    const char *strings[] = {"dog", "cat", "ant", "bat"};
    for (auto s : strings) insert(s);
    expectValid();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    EXPECT_EQ(memcmp(pos, "dog", 4), 0);
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    EXPECT_EQ(memcmp(pos, "cat", 4), 0);
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    EXPECT_EQ(memcmp(pos, "bat", 4), 0);
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    EXPECT_EQ(memcmp(pos, "ant", 4), 0);
    EXPECT_FALSE(fbtreePrev(&it, &pos));
}

TEST_F(FbtreeTest, PrevFullLeaf) {
    const int count = TEST_NODE_CAPACITY + 1;
    char buf[8];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        insert(buf);
    }
    expectValid();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = count - 1; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        ASSERT_TRUE(fbtreePrev(&it, &pos));
        EXPECT_EQ(memcmp(pos, buf, 4), 0);
    }
    EXPECT_FALSE(fbtreePrev(&it, &pos));
}

TEST_F(FbtreeTest, PrevEmpty) {
    expectValid();
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    EXPECT_FALSE(fbtreePrev(&it, &pos));
}

TEST_F(FbtreeTest, PrevNextMixed) {
    for (auto s : {"a", "b", "c", "d", "e"}) insert(s);
    expectValid();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;

    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "a", 2), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "b", 2), 0);
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    EXPECT_EQ(memcmp(pos, "b", 2), 0);
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    EXPECT_EQ(memcmp(pos, "a", 2), 0);

    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "a", 2), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "b", 2), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "c", 2), 0);
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    EXPECT_EQ(memcmp(pos, "c", 2), 0);
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    EXPECT_EQ(memcmp(pos, "b", 2), 0);
}

TEST_F(FbtreeTest, PrevSingle) {
    insert("only");
    expectValid();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    EXPECT_EQ(memcmp(pos, "only", 5), 0);
    EXPECT_FALSE(fbtreePrev(&it, &pos));
}

TEST_F(FbtreeTest, IteratorExhaustedStaysInvalid) {
    insert("x");
    expectValid();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;

    /* Exhaust forward - repeated calls stay false */
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_FALSE(fbtreeNext(&it, &pos));
    EXPECT_FALSE(fbtreeNext(&it, &pos));
    /* Can't reverse from exhausted state */
    EXPECT_FALSE(fbtreePrev(&it, &pos));

    /* Exhaust backward - same behavior */
    fbtreeInitIterator(&it, fbt);
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    EXPECT_FALSE(fbtreePrev(&it, &pos));
    EXPECT_FALSE(fbtreePrev(&it, &pos));
    EXPECT_FALSE(fbtreeNext(&it, &pos));

    /* Can reverse from last item, but not after exhausting */
    fbtreeInitIterator(&it, fbt);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_FALSE(fbtreeNext(&it, &pos));
    EXPECT_FALSE(fbtreePrev(&it, &pos));

    /* Same for first item */
    fbtreeInitIterator(&it, fbt);
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    EXPECT_FALSE(fbtreePrev(&it, &pos));
    EXPECT_FALSE(fbtreeNext(&it, &pos));
}

/* ========== Multi-Level Tree Tests ========== */

TEST_F(FbtreeTest, MultilevelLookup) {
    const int count = TEST_NODE_CAPACITY + 1;
    char buf[8];
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        inserted[i] = insert(buf);
    }
    expectValid();

    for (int i = 0; i < count; i++) {
        EXPECT_GE(fbtreeGetRankOfItem(fbt, inserted[i]), 0);
    }
    zfree(inserted);

    sds s1 = createString("k99");
    EXPECT_LT(fbtreeGetRankOfItem(fbt, s1), 0);
    sdsfree(s1);
    sds s2 = createString("x00");
    EXPECT_LT(fbtreeGetRankOfItem(fbt, s2), 0);
    sdsfree(s2);
}

TEST_F(FbtreeTest, MultilevelForwardIteration) {
    const int count = TEST_NODE_CAPACITY * 7 / 2;
    char buf[8];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        insert(buf);
    }
    expectValid();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        EXPECT_EQ(memcmp(pos, buf, 5), 0);
    }
    EXPECT_FALSE(fbtreeNext(&it, &pos));
}

TEST_F(FbtreeTest, MultilevelBackwardIteration) {
    const int count = TEST_NODE_CAPACITY * 7 / 2;
    char buf[8];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        insert(buf);
    }
    expectValid();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = count - 1; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        ASSERT_TRUE(fbtreePrev(&it, &pos));
        EXPECT_EQ(memcmp(pos, buf, 5), 0);
    }
    EXPECT_FALSE(fbtreePrev(&it, &pos));
}

TEST_F(FbtreeTest, MultilevelMixedIteration) {
    const int total = TEST_NODE_CAPACITY * 7 / 2;
    char buf[8];
    for (int i = 0; i < total; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        insert(buf);
    }
    expectValid();
    EXPECT_EQ(fbtreeLength(fbt), (size_t)total);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;

    for (int i = 0; i < 10; i++) {
        ASSERT_TRUE(fbtreeNext(&it, &pos));
    }
    EXPECT_EQ(memcmp(pos, "k009", 5), 0);

    for (int i = 0; i < 5; i++) {
        ASSERT_TRUE(fbtreePrev(&it, &pos));
    }
    EXPECT_EQ(memcmp(pos, "k005", 5), 0);

    int count = 1;
    while (fbtreeNext(&it, &pos)) count++;
    EXPECT_EQ(count, total - 5 + 1);
    snprintf(buf, sizeof(buf), "k%03d", total - 1);
    EXPECT_EQ(memcmp(pos, buf, 5), 0);

    fbtreeResetIterator(&it);
}

TEST_F(FbtreeTest, MultilevelCrossLeafIteration) {
    char buf[8];
    for (int i = 0; i < 200; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        insert(buf);
    }
    expectValid();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;

    const int boundary = TEST_NODE_CAPACITY;
    for (int i = 0; i < boundary - 5; i++) {
        ASSERT_TRUE(fbtreeNext(&it, &pos));
    }

    for (int i = boundary - 5; i < boundary + 5; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        EXPECT_EQ(memcmp(pos, buf, 5), 0);
    }
}

/* ========== Inner Node Split Tests (3+ Level Trees) ========== */

TEST_F(FbtreeTest, InnerSplitSequential) {
    const int count = TEST_THREE_LEVEL_ITEMS;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        sds str = createBase26TestString("key_", "", i, 3);
        inserted[i] = fbtreeInsert(fbt, str);
    }
    expectValid();
    EXPECT_EQ(fbtreeLength(fbt), (size_t)count);

    for (int i = 0; i < count; i++) {
        EXPECT_GE(fbtreeGetRankOfItem(fbt, inserted[i]), 0);
    }
    zfree(inserted);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < count; i++) {
        sds expected = createBase26TestString("key_", "", i, 3);
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        EXPECT_EQ(sdscmp(pos, expected), 0);
        sdsfree(expected);
    }
    EXPECT_FALSE(fbtreeNext(&it, &pos));
}

TEST_F(FbtreeTest, InnerSplitReverse) {
    const int count = TEST_THREE_LEVEL_ITEMS;
    char buf[16];
    for (int i = count - 1; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "key_%05d", i);
        insert(buf);
    }
    expectValid();
    EXPECT_EQ(fbtreeLength(fbt), (size_t)count);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%05d", i);
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0);
    }
    EXPECT_FALSE(fbtreeNext(&it, &pos));

    fbtreeInitIterator(&it, fbt);
    for (int i = count - 1; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "key_%05d", i);
        ASSERT_TRUE(fbtreePrev(&it, &pos));
        EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0);
    }
    EXPECT_FALSE(fbtreePrev(&it, &pos));
}

TEST_F(FbtreeTest, InnerSplitShuffled) {
    int *indices = (int *)zmalloc(5000 * sizeof(int));
    for (int i = 0; i < 5000; i++) indices[i] = i;
    for (int i = 4999; i > 0; i--) {
        int j = i % 1000;
        int temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
    }

    char buf[16];
    for (int i = 0; i < 5000; i++) {
        snprintf(buf, sizeof(buf), "rnd_%05d", indices[i]);
        insert(buf);
    }
    expectValid();
    EXPECT_EQ(fbtreeLength(fbt), 5000u);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < 5000; i++) {
        snprintf(buf, sizeof(buf), "rnd_%05d", i);
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0);
    }
    EXPECT_FALSE(fbtreeNext(&it, &pos));

    zfree(indices);
}

/* ========== Deep Tree Tests (4+ Levels) ========== */

TEST_F(FbtreeTest, DeepTree4Levels) {
    const int count = TEST_NODE_CAPACITY * TEST_NODE_CAPACITY * TEST_NODE_CAPACITY + 10000;
    char buf[16];
    sds first_item = NULL, middle_item = NULL, last_item = NULL;
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "deep_%06d", i);
        sds inserted = insert(buf);
        if (i == 0)
            first_item = inserted;
        else if (i == count / 2)
            middle_item = inserted;
        else if (i == count - 1)
            last_item = inserted;
    }
    expectValid();
    EXPECT_EQ(fbtreeLength(fbt), (size_t)count);

    EXPECT_GE(fbtreeGetRankOfItem(fbt, first_item), 0);
    EXPECT_GE(fbtreeGetRankOfItem(fbt, middle_item), 0);
    EXPECT_GE(fbtreeGetRankOfItem(fbt, last_item), 0);

    sds search_str = createString("deep_300000");
    EXPECT_LT(fbtreeGetRankOfItem(fbt, search_str), 0);
    sdsfree(search_str);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < count / 2; i++) {
        ASSERT_TRUE(fbtreeNext(&it, &pos));
    }
    for (int i = count / 2; i < count / 2 + 100; i++) {
        snprintf(buf, sizeof(buf), "deep_%06d", i);
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0);
    }
}

TEST_F(FbtreeTest, DeepTreeMixedInsertPatterns) {
    char buf[16];

    for (int i = 0; i < 10000; i++) {
        snprintf(buf, sizeof(buf), "mix_%06d", i * 3);
        insert(buf);
    }
    for (int i = 9999; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "mix_%06d", i * 3 + 1);
        insert(buf);
    }

    int *indices = (int *)zmalloc(10000 * sizeof(int));
    for (int i = 0; i < 10000; i++) indices[i] = i * 3 + 2;
    for (int i = 9999; i > 0; i--) {
        int j = (i * 17) % (i + 1);
        int temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
    }
    for (int i = 0; i < 10000; i++) {
        snprintf(buf, sizeof(buf), "mix_%06d", indices[i]);
        insert(buf);
    }
    zfree(indices);
    expectValid();
    EXPECT_EQ(fbtreeLength(fbt), 30000u);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < 30000; i++) {
        snprintf(buf, sizeof(buf), "mix_%06d", i);
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0);
    }
    EXPECT_FALSE(fbtreeNext(&it, &pos));

    fbtreeInitIterator(&it, fbt);
    for (int i = 0; i < 1000; i++) {
        ASSERT_TRUE(fbtreeNext(&it, &pos));
    }
    for (int i = 0; i < 500; i++) {
        ASSERT_TRUE(fbtreePrev(&it, &pos));
    }
    snprintf(buf, sizeof(buf), "mix_%06d", 500);
    EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0);
}

/* ========== String Pattern Tests ========== */

TEST_F(FbtreeTest, VariedStringPatterns) {
    char buf[32];
    sds *inserted = (sds *)zmalloc(4000 * sizeof(sds));
    int idx = 0;

    for (int i = 0; i < 1000; i++) {
        snprintf(buf, sizeof(buf), "common_prefix_%04d_suffix", i);
        inserted[idx++] = insert(buf);
    }
    for (int i = 0; i < 1000; i++) {
        snprintf(buf, sizeof(buf), "prefix_%04d_common_suffix", i);
        inserted[idx++] = insert(buf);
    }
    for (int i = 0; i < 1000; i++) {
        snprintf(buf, sizeof(buf), "pal_%04d_%04d_lap", i, i);
        inserted[idx++] = insert(buf);
    }
    for (int i = 0; i < 1000; i++) {
        char c = 'a' + (i % 26);
        snprintf(buf, sizeof(buf), "repeat_%c%c%c%c_%04d", c, c, c, c, i);
        inserted[idx++] = insert(buf);
    }
    expectValid();
    EXPECT_EQ(fbtreeLength(fbt), 4000u);

    for (int i = 0; i < 4000; i++) {
        EXPECT_GE(fbtreeGetRankOfItem(fbt, inserted[i]), 0);
    }
    zfree(inserted);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    const_sds prev_pos = NULL;
    int count = 0;
    while (fbtreeNext(&it, &pos)) {
        if (prev_pos) {
            EXPECT_LE(sdscmp(prev_pos, pos), 0);
        }
        prev_pos = pos;
        count++;
    }
    EXPECT_EQ(count, 4000);
}

/* ========== Split Boundary Tests ========== */

TEST_F(FbtreeTest, SplitAtExactBoundary) {
    char buf[16];
    const int count = TEST_TWO_LEVEL_ITEMS;

    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "bound_%05d", i);
        insert(buf);
    }

    snprintf(buf, sizeof(buf), "bound_%05d", count);
    sds boundary_item = insert(buf);
    expectValid();
    EXPECT_EQ(fbtreeLength(fbt), (size_t)(count + 1));
    EXPECT_GE(fbtreeGetRankOfItem(fbt, boundary_item), 0);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < count - 6; i++) {
        ASSERT_TRUE(fbtreeNext(&it, &pos));
    }
    for (int i = count - 6; i <= count; i++) {
        snprintf(buf, sizeof(buf), "bound_%05d", i);
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0);
    }
    EXPECT_FALSE(fbtreeNext(&it, &pos));
}

TEST_F(FbtreeTest, AlternatingMinMaxInsert) {
    const int count = TEST_TWO_LEVEL_ITEMS;
    char buf[16];
    int min_val = 0, max_val = count - 1;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));

    while (min_val <= max_val) {
        snprintf(buf, sizeof(buf), "mid_%06d", min_val);
        inserted[min_val] = insert(buf);
        min_val++;
        if (min_val > max_val) break;
        snprintf(buf, sizeof(buf), "mid_%06d", max_val);
        inserted[max_val] = insert(buf);
        max_val--;
    }
    expectValid();
    EXPECT_EQ(fbtreeLength(fbt), (size_t)count);

    for (int i = 0; i < count; i++) {
        EXPECT_GE(fbtreeGetRankOfItem(fbt, inserted[i]), 0);
    }
    zfree(inserted);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < count; i++) {
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        snprintf(buf, sizeof(buf), "mid_%06d", i);
        EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0);
    }
    EXPECT_FALSE(fbtreeNext(&it, &pos));

    fbtreeInitIterator(&it, fbt);
    for (int i = count - 1; i >= 0; i--) {
        ASSERT_TRUE(fbtreePrev(&it, &pos));
        snprintf(buf, sizeof(buf), "mid_%06d", i);
        EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0);
    }
    EXPECT_FALSE(fbtreePrev(&it, &pos));
}

TEST_F(FbtreeTest, SequentialMiddleInsert) {
    char buf[16];

    for (int i = 0; i < 1000; i++) {
        snprintf(buf, sizeof(buf), "seq_%06d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    for (int i = 3000; i < 4000; i++) {
        snprintf(buf, sizeof(buf), "seq_%06d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    expectValid();
    EXPECT_EQ(fbtreeLength(fbt), 2000u);

    for (int i = 1000; i < 2000; i++) {
        snprintf(buf, sizeof(buf), "seq_%06d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    expectValid();

    for (int i = 2999; i >= 2000; i--) {
        snprintf(buf, sizeof(buf), "seq_%06d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    expectValid();
    EXPECT_EQ(fbtreeLength(fbt), 4000u);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < 4000; i++) {
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        snprintf(buf, sizeof(buf), "seq_%06d", i);
        EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0);
    }
    EXPECT_FALSE(fbtreeNext(&it, &pos));
}

/* ========== Delete Tests ========== */

TEST_F(FbtreeTest, DeleteSingleItem) {
    sds inserted = insert("test");
    EXPECT_EQ(fbtreeLength(fbt), 1u);

    EXPECT_TRUE(fbtreeDelete(fbt, inserted));
    EXPECT_EQ(fbtreeLength(fbt), 0u);
    expectValid();
}

TEST_F(FbtreeTest, DeleteNonexistent) {
    sds inserted = insert("exists");

    sds other = createString("missing");
    EXPECT_FALSE(fbtreeDelete(fbt, other));
    sdsfree(other);

    EXPECT_GE(fbtreeGetRankOfItem(fbt, inserted), 0);
}

TEST_F(FbtreeTest, DeleteFromEmpty) {
    sds dummy = createString("anything");
    EXPECT_FALSE(fbtreeDelete(fbt, dummy));
    sdsfree(dummy);
}

TEST_F(FbtreeTest, DeleteAllItems) {
    char buf[8];
    sds inserted[10];
    for (int i = 0; i < 10; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        inserted[i] = insert(buf);
    }
    EXPECT_EQ(fbtreeLength(fbt), 10u);

    for (int i = 0; i < 10; i++) {
        EXPECT_TRUE(fbtreeDelete(fbt, inserted[i]));
        EXPECT_EQ(fbtreeLength(fbt), 10u - i - 1u);
    }
    EXPECT_EQ(fbtreeLength(fbt), 0u);
    expectValid();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    EXPECT_FALSE(fbtreeNext(&it, &pos));
    fbtreeInitIterator(&it, fbt);
    EXPECT_FALSE(fbtreePrev(&it, &pos));
}

TEST_F(FbtreeTest, DeleteMiddleItem) {
    char buf[8];
    sds inserted[50];
    for (int i = 0; i < 50; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        inserted[i] = insert(buf);
    }

    EXPECT_TRUE(fbtreeDelete(fbt, inserted[25]));
    EXPECT_EQ(fbtreeLength(fbt), 49u);
    EXPECT_GE(fbtreeGetRankOfItem(fbt, inserted[24]), 0);
    EXPECT_GE(fbtreeGetRankOfItem(fbt, inserted[26]), 0);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    int count = 0;
    while (fbtreeNext(&it, &pos)) count++;
    EXPECT_EQ(count, 49);
    expectValid();
}

TEST_F(FbtreeTest, DeleteMaxUpdatesAnchor) {
    char buf[16];
    sds inserted[200];
    for (int i = 0; i < 200; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        inserted[i] = insert(buf);
    }
    expectValid();

    EXPECT_TRUE(fbtreeDelete(fbt, inserted[199]));
    EXPECT_EQ(fbtreeLength(fbt), 199u);
    expectValid();
    EXPECT_GE(fbtreeGetRankOfItem(fbt, inserted[198]), 0);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    int count = 0;
    while (fbtreePrev(&it, &pos)) count++;
    EXPECT_EQ(count, 199);
}

TEST_F(FbtreeTest, DeleteAllMultilevel) {
    char buf[16];
    const int count = TEST_NODE_CAPACITY * 3;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        inserted[i] = fbtreeInsert(fbt, createString(buf));
    }
    expectValid();

    for (int i = 0; i < count; i++) {
        EXPECT_TRUE(fbtreeDelete(fbt, inserted[i]));
        expectValid();
    }
    EXPECT_EQ(fbtreeLength(fbt), 0u);

    zfree(inserted);
}

TEST_F(FbtreeTest, DeleteRootCollapse) {
    char buf[16];
    const int overflow = 10;
    const int count = TEST_NODE_CAPACITY + overflow;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        inserted[i] = fbtreeInsert(fbt, createString(buf));
    }
    expectValid();

    for (int i = 0; i < TEST_NODE_CAPACITY; i++) {
        EXPECT_TRUE(fbtreeDelete(fbt, inserted[i]));
    }
    expectValid();
    EXPECT_EQ(fbtreeLength(fbt), (size_t)overflow);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    int remaining = 0;
    while (fbtreeNext(&it, &pos)) remaining++;
    EXPECT_EQ(remaining, overflow);

    zfree(inserted);
}

TEST_F(FbtreeTest, DeleteLeftmostLeafUpdatesCache) {
    char buf[16];
    const int count = TEST_NODE_CAPACITY * 3;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        inserted[i] = fbtreeInsert(fbt, createString(buf));
    }

    for (int i = 0; i < TEST_NODE_CAPACITY; i++) {
        EXPECT_TRUE(fbtreeDelete(fbt, inserted[i]));
    }
    expectValid();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    snprintf(buf, sizeof(buf), "key_%03d", TEST_NODE_CAPACITY);
    EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0);

    zfree(inserted);
}

TEST_F(FbtreeTest, DeleteRightmostLeafUpdatesCache) {
    char buf[16];
    const int count = TEST_NODE_CAPACITY * 3;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        inserted[i] = fbtreeInsert(fbt, createString(buf));
    }

    for (int i = count - 1; i >= count - TEST_NODE_CAPACITY; i--) {
        EXPECT_TRUE(fbtreeDelete(fbt, inserted[i]));
    }
    expectValid();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    snprintf(buf, sizeof(buf), "key_%03d", count - TEST_NODE_CAPACITY - 1);
    EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0);

    zfree(inserted);
}

/* Delete in reverse order - stresses different anchor-update paths than forward delete. */
TEST_F(FbtreeTest, DeleteReverseOrder) {
    char buf[16];
    const int count = TEST_NODE_CAPACITY * 3;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        inserted[i] = fbtreeInsert(fbt, createString(buf));
    }
    expectValid();

    /* Delete from highest to lowest */
    for (int i = count - 1; i >= 0; i--) {
        EXPECT_TRUE(fbtreeDelete(fbt, inserted[i]));
        expectValid();
    }
    EXPECT_EQ(fbtreeLength(fbt), 0u);

    zfree(inserted);
}

/* Interleaved insert and delete - exercises tree after structural changes from deletion. */
TEST_F(FbtreeTest, InterleavedInsertDelete) {
    char buf[16];
    const int batch = 100;
    sds *inserted = (sds *)zmalloc(batch * sizeof(sds));

    /* Insert first batch */
    for (int i = 0; i < batch; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        inserted[i] = fbtreeInsert(fbt, createString(buf));
    }
    expectValid();

    /* Delete even-indexed items */
    for (int i = 0; i < batch; i += 2) {
        EXPECT_TRUE(fbtreeDelete(fbt, inserted[i]));
    }
    EXPECT_EQ(fbtreeLength(fbt), 50u);
    expectValid();

    /* Insert new items into the gaps */
    sds *new_inserted = (sds *)zmalloc(batch * sizeof(sds));
    for (int i = 0; i < batch; i++) {
        snprintf(buf, sizeof(buf), "new_%03d", i);
        new_inserted[i] = fbtreeInsert(fbt, createString(buf));
    }
    EXPECT_EQ(fbtreeLength(fbt), 150u);
    expectValid();

    /* Verify all remaining items are findable */
    for (int i = 1; i < batch; i += 2) {
        EXPECT_GE(fbtreeGetRankOfItem(fbt, inserted[i]), 0);
    }
    for (int i = 0; i < batch; i++) {
        EXPECT_GE(fbtreeGetRankOfItem(fbt, new_inserted[i]), 0);
    }

    /* Verify sorted iteration */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    const_sds prev_pos = nullptr;
    int count = 0;
    while (fbtreeNext(&it, &pos)) {
        if (prev_pos) {
            EXPECT_LT(sdscmp(prev_pos, pos), 0);
        }
        prev_pos = pos;
        count++;
    }
    EXPECT_EQ(count, 150);

    zfree(inserted);
    zfree(new_inserted);
}

/* ========== Rank Tests ========== */

TEST_F(FbtreeTest, RankSingleLeaf) {
    const char *items[] = {"cherry", "apple", "banana", "date"};
    for (auto s : items) insert(s);

    /* Sorted order: apple(0), banana(1), cherry(2), date(3) */
    const_sds result = fbtreeGetAtRank(fbt, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(memcmp(result, "apple", 6), 0);

    result = fbtreeGetAtRank(fbt, 1);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(memcmp(result, "banana", 7), 0);

    result = fbtreeGetAtRank(fbt, 2);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(memcmp(result, "cherry", 7), 0);

    result = fbtreeGetAtRank(fbt, 3);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(memcmp(result, "date", 5), 0);

    EXPECT_EQ(fbtreeGetAtRank(fbt, 4), nullptr);
}

TEST_F(FbtreeTest, RankMultilevel) {
    char buf[16];
    for (int i = 0; i < 200; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        insert(buf);
    }
    expectValid();

    EXPECT_EQ(memcmp(fbtreeGetAtRank(fbt, 0), "key_000", 8), 0);
    EXPECT_EQ(memcmp(fbtreeGetAtRank(fbt, 50), "key_050", 8), 0);
    EXPECT_EQ(memcmp(fbtreeGetAtRank(fbt, 100), "key_100", 8), 0);
    EXPECT_EQ(memcmp(fbtreeGetAtRank(fbt, 199), "key_199", 8), 0);
    EXPECT_EQ(fbtreeGetAtRank(fbt, 200), nullptr);
}

TEST_F(FbtreeTest, RankAfterDelete) {
    char buf[16];
    sds inserted[100];
    for (int i = 0; i < 100; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        inserted[i] = insert(buf);
    }

    EXPECT_TRUE(fbtreeDelete(fbt, inserted[50]));

    const_sds result = fbtreeGetAtRank(fbt, 50);
    EXPECT_EQ(memcmp(result, "key_051", 8), 0);
    result = fbtreeGetAtRank(fbt, 49);
    EXPECT_EQ(memcmp(result, "key_049", 8), 0);
}

TEST_F(FbtreeTest, GetRankOfItem) {
    char buf[16];
    sds inserted[100];
    for (int i = 0; i < 100; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        inserted[i] = insert(buf);
    }

    EXPECT_EQ(fbtreeGetRankOfItem(fbt, inserted[0]), 0);
    EXPECT_EQ(fbtreeGetRankOfItem(fbt, inserted[50]), 50);
    EXPECT_EQ(fbtreeGetRankOfItem(fbt, inserted[99]), 99);

    sds search = createString("key_100");
    EXPECT_EQ(fbtreeGetRankOfItem(fbt, search), -1);
    sdsfree(search);
}

TEST_F(FbtreeTest, GetAtRankEmptyTree) {
    EXPECT_EQ(fbtreeGetAtRank(fbt, 0), nullptr);
    EXPECT_EQ(fbtreeGetAtRank(fbt, 1), nullptr);
}

TEST_F(FbtreeTest, SeekToRank) {
    char buf[16];
    for (int i = 0; i < 100; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        insert(buf);
    }

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;

    fbtreeSeekToRank(&it, 50);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "key_050", 8), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "key_051", 8), 0);

    fbtreeSeekToRank(&it, 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "key_000", 8), 0);

    fbtreeSeekToRank(&it, 99);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "key_099", 8), 0);
    EXPECT_FALSE(fbtreeNext(&it, &pos));
}

/* SeekToRank then iterate backward. */
TEST_F(FbtreeTest, SeekToRankThenPrev) {
    char buf[16];
    for (int i = 0; i < 200; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        insert(buf);
    }

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;

    /* Seek to rank 100, then iterate backward */
    fbtreeSeekToRank(&it, 100);
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    EXPECT_EQ(memcmp(pos, "key_099", 8), 0);
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    EXPECT_EQ(memcmp(pos, "key_098", 8), 0);

    /* Seek to rank 0, prev should fail (nothing before first element) */
    fbtreeSeekToRank(&it, 0);
    /* Next returns rank 0 */
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "key_000", 8), 0);
}

/* SeekToRank out of bounds. */
TEST_F(FbtreeTest, SeekToRankOutOfBounds) {
    char buf[16];
    for (int i = 0; i < 10; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        insert(buf);
    }

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;

    /* Seek past end */
    fbtreeSeekToRank(&it, 100);
    EXPECT_FALSE(fbtreeNext(&it, &pos));
}

TEST_F(FbtreeTest, RankDeepTree) {
    const int count = TEST_THREE_LEVEL_ITEMS;
    char buf[16];
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%05d", i);
        inserted[i] = insert(buf);
    }
    expectValid();

    EXPECT_EQ(memcmp(fbtreeGetAtRank(fbt, 0), "key_00000", 10), 0);

    snprintf(buf, sizeof(buf), "key_%05d", count - 1);
    EXPECT_EQ(memcmp(fbtreeGetAtRank(fbt, count - 1), buf, 10), 0);

    for (int i = 0; i < count; i += count / 10) {
        EXPECT_EQ(fbtreeGetAtRank(fbt, i), inserted[i]);
    }
    for (int i = 0; i < count; i += count / 10) {
        EXPECT_EQ(fbtreeGetRankOfItem(fbt, inserted[i]), i);
    }
    zfree(inserted);
}

/* ========== fbtreeSeekToScore Tests ========== */

TEST_F(FbtreeTest, SeekToScoreExact) {
    fbtreeInsert(fbt, createString("AAAAAAAAelem_0"));
    fbtreeInsert(fbt, createString("BBBBBBBBelem_1"));
    fbtreeInsert(fbt, createString("CCCCCCCCelem_2"));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "BBBBBBBB", &it);

    const_sds pos;
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "BBBBBBBB", 8), 0);
}

TEST_F(FbtreeTest, SeekToScoreBetween) {
    fbtreeInsert(fbt, createString("AAAAAAAAelem_0"));
    fbtreeInsert(fbt, createString("CCCCCCCCelem_1"));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "BBBBBBBB", &it);

    const_sds pos;
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "CCCCCCCC", 8), 0);
}

TEST_F(FbtreeTest, SeekToScorePastEnd) {
    fbtreeInsert(fbt, createString("AAAAAAAAelem"));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "ZZZZZZZZ", &it);

    const_sds pos;
    EXPECT_FALSE(fbtreeNext(&it, &pos));
}

TEST_F(FbtreeTest, SeekToScorePastEndThenPrev) {
    fbtreeInsert(fbt, createString("AAAAAAAAelem_0"));
    fbtreeInsert(fbt, createString("BBBBBBBBelem_1"));
    fbtreeInsert(fbt, createString("CCCCCCCCelem_2"));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "ZZZZZZZZ", &it);

    const_sds pos;
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    EXPECT_EQ(memcmp(pos, "CCCCCCCC", 8), 0);
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    EXPECT_EQ(memcmp(pos, "BBBBBBBB", 8), 0);
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    EXPECT_EQ(memcmp(pos, "AAAAAAAA", 8), 0);
    EXPECT_FALSE(fbtreePrev(&it, &pos));

    /* Forward from past-end invalidates */
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "ZZZZZZZZ", &it);
    EXPECT_FALSE(fbtreeNext(&it, &pos));
    EXPECT_FALSE(fbtreePrev(&it, &pos));
}

TEST_F(FbtreeTest, SeekToScoreBeforeStartThenNext) {
    fbtreeInsert(fbt, createString("MMMMMMMMelem_0"));
    fbtreeInsert(fbt, createString("NNNNNNNNelem_1"));
    fbtreeInsert(fbt, createString("OOOOOOOOelem_2"));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "AAAAAAAA", &it);

    const_sds pos;
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "MMMMMMMM", 8), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "NNNNNNNN", 8), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "OOOOOOOO", 8), 0);
    EXPECT_FALSE(fbtreeNext(&it, &pos));

    /* Backward from before-start invalidates */
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "AAAAAAAA", &it);
    EXPECT_FALSE(fbtreePrev(&it, &pos));
    EXPECT_FALSE(fbtreeNext(&it, &pos));
}

TEST_F(FbtreeTest, SeekToScoreBeforeStart) {
    fbtreeInsert(fbt, createString("MMMMMMMMelem"));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "AAAAAAAA", &it);

    const_sds pos;
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "MMMMMMMM", 8), 0);
}

TEST_F(FbtreeTest, SeekToScoreEmpty) {
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "AAAAAAAA", &it);

    const_sds pos;
    EXPECT_FALSE(fbtreeNext(&it, &pos));
}

/* Seek to exact score match then prev - verifies prev returns element before the match. */
TEST_F(FbtreeTest, SeekToScoreExactThenPrev) {
    fbtreeInsert(fbt, createString("AAAAAAAAelem_0"));
    fbtreeInsert(fbt, createString("BBBBBBBBelem_1"));
    fbtreeInsert(fbt, createString("CCCCCCCCelem_2"));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "BBBBBBBB", &it);

    const_sds pos;
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    EXPECT_EQ(memcmp(pos, "AAAAAAAA", 8), 0);
}

/* Seek to score on single-element tree. */
TEST_F(FbtreeTest, SeekToScoreSingleElement) {
    fbtreeInsert(fbt, createString("MMMMMMMMonly"));

    fbtreeIterator it;
    const_sds pos;

    /* Exact match */
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "MMMMMMMM", &it);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "MMMMMMMM", 8), 0);

    /* Before */
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "AAAAAAAA", &it);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "MMMMMMMM", 8), 0);

    /* After */
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "ZZZZZZZZ", &it);
    EXPECT_FALSE(fbtreeNext(&it, &pos));
}

TEST_F(FbtreeTest, SeekToScoreDeepTree) {
    const int count = TEST_THREE_LEVEL_ITEMS;
    char buf[24];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "%08d_elem_%05d", i, i);
        fbtreeInsert(fbt, createString(buf));
    }
    expectValid();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;

    snprintf(buf, sizeof(buf), "%08d", count / 2);
    fbtreeSeekToScore(fbt, buf, &it);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, buf, 8), 0);

    snprintf(buf, sizeof(buf), "%08d", count - 10);
    fbtreeSeekToScore(fbt, buf, &it);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, buf, 8), 0);
}

TEST_F(FbtreeTest, SeekToScoreIterate) {
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
    EXPECT_EQ(count, 5);
}

/* Seek to score with duplicate scores - verify boundary positioning. */
TEST_F(FbtreeTest, SeekToScoreDuplicateScores) {
    /* Insert multiple elements with same 8-byte score prefix */
    fbtreeInsert(fbt, createString("AAAAAAAAfirst"));
    fbtreeInsert(fbt, createString("BBBBBBBBa_elem"));
    fbtreeInsert(fbt, createString("BBBBBBBBb_elem"));
    fbtreeInsert(fbt, createString("BBBBBBBBc_elem"));
    fbtreeInsert(fbt, createString("CCCCCCCClast"));

    fbtreeIterator it;
    const_sds pos;

    /* Seek to "BBBBBBBB" - should land at first B element */
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "BBBBBBBB", &it);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "BBBBBBBBa_elem", 15), 0);

    /* Prev from that position should return the A element */
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "BBBBBBBB", &it);
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    EXPECT_EQ(memcmp(pos, "AAAAAAAA", 8), 0);
}

/* ========== Inner Node Binary Search Tests ========== */

TEST_F(FbtreeTest, InnerBsearchIdenticalFeatureBytes) {
    sds outlier = fbtreeInsert(fbt, createString("A_outlier"));

    const int count = TEST_NODE_CAPACITY + 10;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    char buf[16];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "XXXX_%05d", i);
        inserted[i] = fbtreeInsert(fbt, createString(buf));
    }
    expectValid();
    EXPECT_EQ(fbtreeLength(fbt), (size_t)(count + 1));

    for (int i = 0; i < count; i++) {
        EXPECT_EQ(fbtreeGetRankOfItem(fbt, inserted[i]), i + 1);
    }
    EXPECT_EQ(fbtreeGetRankOfItem(fbt, outlier), 0);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "A_outlier", 10), 0);
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "XXXX_%05d", i);
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0);
    }
    EXPECT_FALSE(fbtreeNext(&it, &pos));

    zfree(inserted);
}

/* ========== Long Prefix Tests ========== */

TEST_F(FbtreeTest, LongPrefixBasic) {
    const size_t prefix_len = TEST_EMBED_PREFIX_LEN + 6;
    const int count = TEST_TWO_LEVEL_ITEMS;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    char suffix[8];

    for (int i = 0; i < count; i++) {
        snprintf(suffix, sizeof(suffix), "s%05d", i);
        inserted[i] = fbtreeInsert(fbt, createPrefixString("X", prefix_len, suffix));
    }
    EXPECT_EQ(fbtreeLength(fbt), (size_t)count);
    expectValid();

    for (int i = 0; i < count; i++) {
        EXPECT_EQ(fbtreeGetRankOfItem(fbt, inserted[i]), i);
    }

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < count; i++) {
        snprintf(suffix, sizeof(suffix), "s%05d", i);
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        EXPECT_EQ(memcmp(pos + prefix_len, suffix, 6), 0);
    }
    EXPECT_FALSE(fbtreeNext(&it, &pos));

    zfree(inserted);
}

TEST_F(FbtreeTest, VeryLongPrefix) {
    const size_t prefix_len = TEST_EMBED_PREFIX_LEN * 10;
    const int count = TEST_NODE_CAPACITY + 10;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    char suffix[8];

    for (int i = 0; i < count; i++) {
        snprintf(suffix, sizeof(suffix), "s%03d", i);
        inserted[i] = fbtreeInsert(fbt, createPrefixString("P", prefix_len, suffix));
    }
    EXPECT_EQ(fbtreeLength(fbt), (size_t)count);
    expectValid();

    for (int i = 0; i < count; i++) {
        EXPECT_EQ(fbtreeGetRankOfItem(fbt, inserted[i]), i);
    }

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i < count; i++) {
        snprintf(suffix, sizeof(suffix), "s%03d", i);
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        EXPECT_EQ(memcmp(pos + prefix_len, suffix, 5), 0);
    }
    EXPECT_FALSE(fbtreeNext(&it, &pos));

    zfree(inserted);
}

TEST_F(FbtreeTest, LongPrefixMultilevel) {
    const size_t prefix_len = TEST_EMBED_PREFIX_LEN + 14;
    const int count = TEST_THREE_LEVEL_ITEMS;
    char suffix[16];

    for (int i = 0; i < count; i++) {
        snprintf(suffix, sizeof(suffix), "item_%05d", i);
        fbtreeInsert(fbt, createPrefixString("L", prefix_len, suffix));
    }
    EXPECT_EQ(fbtreeLength(fbt), (size_t)count);
    expectValid();

    const_sds first = fbtreeGetAtRank(fbt, 0);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(memcmp(first + prefix_len, "item_00000", 11), 0);

    const_sds last = fbtreeGetAtRank(fbt, count - 1);
    snprintf(suffix, sizeof(suffix), "item_%05d", count - 1);
    ASSERT_NE(last, nullptr);
    EXPECT_EQ(memcmp(last + prefix_len, suffix, 11), 0);
}

TEST_F(FbtreeTest, LongPrefixDelete) {
    const size_t prefix_len = TEST_EMBED_PREFIX_LEN + 4;
    const int count = TEST_NODE_CAPACITY * 2;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    char suffix[8];

    for (int i = 0; i < count; i++) {
        snprintf(suffix, sizeof(suffix), "d%03d", i);
        inserted[i] = fbtreeInsert(fbt, createPrefixString("D", prefix_len, suffix));
    }
    expectValid();

    for (int i = 0; i < count; i += 2) {
        EXPECT_TRUE(fbtreeDelete(fbt, inserted[i]));
    }
    EXPECT_EQ(fbtreeLength(fbt), (size_t)(count / 2));
    expectValid();

    for (int i = 1; i < count; i += 2) {
        EXPECT_GE(fbtreeGetRankOfItem(fbt, inserted[i]), 0);
    }

    zfree(inserted);
}

TEST_F(FbtreeTest, LongPrefixBoundary) {
    const int count = TEST_NODE_CAPACITY + 10;
    char suffix[8];

    /* Case 1: prefix exactly at EMBED_PREFIX_LEN (embedded storage) */
    {
        /* Use a separate tree for this sub-case */
        fbtreeIndex *fbt1 = fbtreeCreate();
        sds *inserted = (sds *)zmalloc(count * sizeof(sds));
        for (int i = 0; i < count; i++) {
            snprintf(suffix, sizeof(suffix), "%c%04d", 'a' + (i % 26), i);
            inserted[i] = fbtreeInsert(fbt1, createPrefixString("X", TEST_EMBED_PREFIX_LEN, suffix));
        }
        EXPECT_TRUE(fbtreeDebugValidate(fbt1, false));
        for (int i = 0; i < count; i++)
            EXPECT_GE(fbtreeGetRankOfItem(fbt1, inserted[i]), 0);
        zfree(inserted);
        fbtreeFree(fbt1);
    }

    /* Case 2: prefix at EMBED_PREFIX_LEN+1 (long prefix storage) */
    {
        fbtreeIndex *fbt2 = fbtreeCreate();
        sds *inserted = (sds *)zmalloc(count * sizeof(sds));
        for (int i = 0; i < count; i++) {
            snprintf(suffix, sizeof(suffix), "%c%04d", 'a' + (i % 26), i);
            inserted[i] = fbtreeInsert(fbt2, createPrefixString("X", TEST_EMBED_PREFIX_LEN + 1, suffix));
        }
        EXPECT_TRUE(fbtreeDebugValidate(fbt2, false));
        for (int i = 0; i < count; i++)
            EXPECT_GE(fbtreeGetRankOfItem(fbt2, inserted[i]), 0);
        zfree(inserted);
        fbtreeFree(fbt2);
    }
}

TEST_F(FbtreeTest, LongPrefixShrinkToShort) {
    const size_t long_prefix = TEST_EMBED_PREFIX_LEN + 14;
    const int count = TEST_NODE_CAPACITY + 10;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    char suffix[8];

    for (int i = 0; i < count; i++) {
        snprintf(suffix, sizeof(suffix), "s%03d", i);
        inserted[i] = fbtreeInsert(fbt, createPrefixString("A", long_prefix, suffix));
    }
    expectValid();

    sds outlier = fbtreeInsert(fbt, createPrefixString("B", long_prefix, "zzz"));
    expectValid();

    for (int i = 0; i < count; i++) {
        EXPECT_GE(fbtreeGetRankOfItem(fbt, inserted[i]), 0);
    }
    EXPECT_GE(fbtreeGetRankOfItem(fbt, outlier), 0);

    EXPECT_TRUE(fbtreeDelete(fbt, outlier));
    expectValid();

    for (int i = 0; i < count; i++) {
        EXPECT_GE(fbtreeGetRankOfItem(fbt, inserted[i]), 0);
    }

    zfree(inserted);
}

TEST_F(FbtreeTest, LongPrefixRealloc) {
    const size_t long_prefix = TEST_EMBED_PREFIX_LEN * 2;
    const int count = TEST_NODE_CAPACITY + 10;
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    char suffix[8];

    for (int i = 0; i < count; i++) {
        snprintf(suffix, sizeof(suffix), "s%03d", i);
        inserted[i] = fbtreeInsert(fbt, createPrefixString("X", long_prefix, suffix));
    }
    expectValid();

    const size_t mid_prefix = TEST_EMBED_PREFIX_LEN + 4;
    sds s_mid = sdsnewlen(NULL, mid_prefix + 5);
    memset(s_mid, 'X', mid_prefix);
    memcpy(s_mid + mid_prefix, "Ymid", 5);
    sds item_mid = fbtreeInsert(fbt, s_mid);
    expectValid();

    const size_t longer_prefix = TEST_EMBED_PREFIX_LEN + 14;
    sds s_longer = sdsnewlen(NULL, longer_prefix + 5);
    memset(s_longer, 'X', longer_prefix);
    memcpy(s_longer + longer_prefix, "Zlng", 5);
    sds item_longer = fbtreeInsert(fbt, s_longer);
    expectValid();

    for (int i = 0; i < count; i++) {
        EXPECT_GE(fbtreeGetRankOfItem(fbt, inserted[i]), 0);
    }
    EXPECT_GE(fbtreeGetRankOfItem(fbt, item_mid), 0);
    EXPECT_GE(fbtreeGetRankOfItem(fbt, item_longer), 0);

    EXPECT_TRUE(fbtreeDelete(fbt, item_mid));
    EXPECT_TRUE(fbtreeDelete(fbt, item_longer));
    expectValid();

    for (int i = 0; i < count; i++) {
        EXPECT_GE(fbtreeGetRankOfItem(fbt, inserted[i]), 0);
    }

    zfree(inserted);
}

/* ========== Pop Min/Max Tests ========== */

TEST_F(FbtreeTest, PopMinSingle) {
    insert("only");
    EXPECT_EQ(fbtreeLength(fbt), 1u);

    sds popped = fbtreePopMin(fbt);
    ASSERT_NE(popped, nullptr);
    EXPECT_EQ(memcmp(popped, "only", 5), 0);
    EXPECT_EQ(fbtreeLength(fbt), 0u);
    expectValid();
    sdsfree(popped);

    EXPECT_EQ(fbtreePopMin(fbt), nullptr);
}

TEST_F(FbtreeTest, PopMaxSingle) {
    insert("only");
    EXPECT_EQ(fbtreeLength(fbt), 1u);

    sds popped = fbtreePopMax(fbt);
    ASSERT_NE(popped, nullptr);
    EXPECT_EQ(memcmp(popped, "only", 5), 0);
    EXPECT_EQ(fbtreeLength(fbt), 0u);
    expectValid();
    sdsfree(popped);

    EXPECT_EQ(fbtreePopMax(fbt), nullptr);
}

TEST_F(FbtreeTest, PopMinMultiple) {
    const char *items[] = {"cherry", "apple", "banana", "date"};
    for (auto s : items) fbtreeInsert(fbt, createString(s));

    const char *expected[] = {"apple", "banana", "cherry", "date"};
    size_t expected_lens[] = {6, 7, 7, 5};
    for (int i = 0; i < 4; i++) {
        sds popped = fbtreePopMin(fbt);
        ASSERT_NE(popped, nullptr);
        EXPECT_EQ(memcmp(popped, expected[i], expected_lens[i]), 0);
        sdsfree(popped);
        expectValid();
    }
    EXPECT_EQ(fbtreeLength(fbt), 0u);
}

TEST_F(FbtreeTest, PopMaxMultiple) {
    const char *items[] = {"cherry", "apple", "banana", "date"};
    for (auto s : items) fbtreeInsert(fbt, createString(s));

    const char *expected[] = {"date", "cherry", "banana", "apple"};
    size_t expected_lens[] = {5, 7, 7, 6};
    for (int i = 0; i < 4; i++) {
        sds popped = fbtreePopMax(fbt);
        ASSERT_NE(popped, nullptr);
        EXPECT_EQ(memcmp(popped, expected[i], expected_lens[i]), 0);
        sdsfree(popped);
        expectValid();
    }
    EXPECT_EQ(fbtreeLength(fbt), 0u);
}

TEST_F(FbtreeTest, PopMinMultilevel) {
    char buf[16];
    const int count = TEST_NODE_CAPACITY * 3;
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    expectValid();

    for (int i = 0; i < count; i++) {
        sds popped = fbtreePopMin(fbt);
        ASSERT_NE(popped, nullptr);
        snprintf(buf, sizeof(buf), "key_%03d", i);
        EXPECT_EQ(memcmp(popped, buf, strlen(buf) + 1), 0);
        sdsfree(popped);
        expectValid();
    }
    EXPECT_EQ(fbtreeLength(fbt), 0u);
}

TEST_F(FbtreeTest, PopMaxMultilevel) {
    char buf[16];
    const int count = TEST_NODE_CAPACITY * 3;
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    expectValid();

    for (int i = count - 1; i >= 0; i--) {
        sds popped = fbtreePopMax(fbt);
        ASSERT_NE(popped, nullptr);
        snprintf(buf, sizeof(buf), "key_%03d", i);
        EXPECT_EQ(memcmp(popped, buf, strlen(buf) + 1), 0);
        sdsfree(popped);
        expectValid();
    }
    EXPECT_EQ(fbtreeLength(fbt), 0u);
}

TEST_F(FbtreeTest, PopAlternating) {
    char buf[16];
    const int count = 100;
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        fbtreeInsert(fbt, createString(buf));
    }

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
        ASSERT_NE(popped, nullptr);
        EXPECT_EQ(memcmp(popped, buf, strlen(buf) + 1), 0);
        sdsfree(popped);
        expectValid();
    }
    EXPECT_EQ(fbtreeLength(fbt), 0u);
}

TEST_F(FbtreeTest, PopEmpty) {
    EXPECT_EQ(fbtreePopMin(fbt), nullptr);
    EXPECT_EQ(fbtreePopMax(fbt), nullptr);
}

/* Pop from deep (3+ level) tree - exercises pop through multiple inner node levels. */
TEST_F(FbtreeTest, PopMinDeepTree) {
    const int count = TEST_THREE_LEVEL_ITEMS;
    char buf[16];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%05d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    expectValid();

    /* Pop first 100 items from deep tree */
    for (int i = 0; i < 100; i++) {
        sds popped = fbtreePopMin(fbt);
        ASSERT_NE(popped, nullptr);
        snprintf(buf, sizeof(buf), "key_%05d", i);
        EXPECT_EQ(memcmp(popped, buf, strlen(buf) + 1), 0);
        sdsfree(popped);
    }
    EXPECT_EQ(fbtreeLength(fbt), (size_t)(count - 100));
    expectValid();
}

TEST_F(FbtreeTest, PopMaxDeepTree) {
    const int count = TEST_THREE_LEVEL_ITEMS;
    char buf[16];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%05d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    expectValid();

    /* Pop last 100 items from deep tree */
    for (int i = count - 1; i >= count - 100; i--) {
        sds popped = fbtreePopMax(fbt);
        ASSERT_NE(popped, nullptr);
        snprintf(buf, sizeof(buf), "key_%05d", i);
        EXPECT_EQ(memcmp(popped, buf, strlen(buf) + 1), 0);
        sdsfree(popped);
    }
    EXPECT_EQ(fbtreeLength(fbt), (size_t)(count - 100));
    expectValid();
}

TEST_F(FbtreeTest, PopWithIterationAndInsert) {
    char buf[16];
    sds popped;
    const_sds pos;

    /* Insert ascending: k100-k199 */
    for (int i = 100; i < 200; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    EXPECT_EQ(fbtreeLength(fbt), 100u);

    /* Pop min a few times */
    for (int i = 100; i < 110; i++) {
        popped = fbtreePopMin(fbt);
        snprintf(buf, sizeof(buf), "k%03d", i);
        EXPECT_EQ(memcmp(popped, buf, strlen(buf) + 1), 0);
        sdsfree(popped);
        expectValid();
    }
    EXPECT_EQ(fbtreeLength(fbt), 90u);

    /* Insert descending: k099 down to k050 */
    for (int i = 99; i >= 50; i--) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    EXPECT_EQ(fbtreeLength(fbt), 140u);

    /* Pop max a few times */
    for (int i = 199; i >= 190; i--) {
        popped = fbtreePopMax(fbt);
        snprintf(buf, sizeof(buf), "k%03d", i);
        EXPECT_EQ(memcmp(popped, buf, strlen(buf) + 1), 0);
        sdsfree(popped);
        expectValid();
    }
    EXPECT_EQ(fbtreeLength(fbt), 130u);

    /* Iterate forward: k050-k189 (excluding popped k100-k109) */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    for (int i = 50; i < 190; i++) {
        if (i >= 100 && i < 110) continue;
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        snprintf(buf, sizeof(buf), "k%03d", i);
        EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0);
    }
    EXPECT_FALSE(fbtreeNext(&it, &pos));

    /* Iterate backward */
    fbtreeInitIterator(&it, fbt);
    for (int i = 189; i >= 50; i--) {
        if (i >= 100 && i < 110) continue;
        ASSERT_TRUE(fbtreePrev(&it, &pos));
        snprintf(buf, sizeof(buf), "k%03d", i);
        EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0);
    }
    EXPECT_FALSE(fbtreePrev(&it, &pos));
}

/* ========== fbtreeSeekToValue Tests ========== */

TEST_F(FbtreeTest, SeekToValueExact) {
    insert("apple");
    insert("banana");
    insert("cherry");

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    sds seek_val = createString("banana");
    fbtreeSeekToValue(fbt, seek_val, &it);

    const_sds pos;
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "banana", 7), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "cherry", 7), 0);
    EXPECT_FALSE(fbtreeNext(&it, &pos));

    sdsfree(seek_val);
}

TEST_F(FbtreeTest, SeekToValueBetween) {
    insert("apple");
    insert("cherry");
    insert("elderberry");

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    sds seek_val = createString("banana");
    fbtreeSeekToValue(fbt, seek_val, &it);

    const_sds pos;
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "cherry", 7), 0);

    sdsfree(seek_val);
}

TEST_F(FbtreeTest, SeekToValuePastEnd) {
    insert("apple");
    insert("banana");
    insert("cherry");

    fbtreeIterator it;
    sds seek_val = createString("zzz");
    const_sds pos;

    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToValue(fbt, seek_val, &it);
    EXPECT_FALSE(fbtreeNext(&it, &pos));

    /* Prev from past-end returns last element */
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToValue(fbt, seek_val, &it);
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    EXPECT_EQ(memcmp(pos, "cherry", 7), 0);

    /* Forward from past-end invalidates */
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToValue(fbt, seek_val, &it);
    EXPECT_FALSE(fbtreeNext(&it, &pos));
    EXPECT_FALSE(fbtreePrev(&it, &pos));

    sdsfree(seek_val);
}

TEST_F(FbtreeTest, SeekToValueBeforeStart) {
    insert("banana");
    insert("cherry");
    insert("date");

    fbtreeIterator it;
    sds seek_val = createString("apple");
    const_sds pos;

    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToValue(fbt, seek_val, &it);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "banana", 7), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "cherry", 7), 0);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "date", 5), 0);
    EXPECT_FALSE(fbtreeNext(&it, &pos));

    /* Backward from before-start invalidates */
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToValue(fbt, seek_val, &it);
    EXPECT_FALSE(fbtreePrev(&it, &pos));
    EXPECT_FALSE(fbtreeNext(&it, &pos));

    sdsfree(seek_val);
}

TEST_F(FbtreeTest, SeekToValueEmpty) {
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    sds seek_val = createString("anything");
    fbtreeSeekToValue(fbt, seek_val, &it);

    const_sds pos;
    EXPECT_FALSE(fbtreeNext(&it, &pos));
    EXPECT_FALSE(fbtreePrev(&it, &pos));

    sdsfree(seek_val);
}

/* Seek to value on single-element tree. */
TEST_F(FbtreeTest, SeekToValueSingleElement) {
    insert("middle");

    fbtreeIterator it;
    const_sds pos;

    /* Exact match */
    fbtreeInitIterator(&it, fbt);
    sds exact = createString("middle");
    fbtreeSeekToValue(fbt, exact, &it);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "middle", 7), 0);
    sdsfree(exact);

    /* Before */
    fbtreeInitIterator(&it, fbt);
    sds before = createString("aaa");
    fbtreeSeekToValue(fbt, before, &it);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "middle", 7), 0);
    sdsfree(before);

    /* After */
    fbtreeInitIterator(&it, fbt);
    sds after = createString("zzz");
    fbtreeSeekToValue(fbt, after, &it);
    EXPECT_FALSE(fbtreeNext(&it, &pos));
    sdsfree(after);
}

TEST_F(FbtreeTest, SeekToValueSharedPrefix) {
    fbtreeInsert(fbt, createString("XXXXXXXXalpha"));
    fbtreeInsert(fbt, createString("XXXXXXXXbravo"));
    fbtreeInsert(fbt, createString("XXXXXXXXcharlie"));
    fbtreeInsert(fbt, createString("XXXXXXXXdelta"));
    fbtreeInsert(fbt, createString("XXXXXXXXecho"));

    fbtreeIterator it;
    const_sds pos;

    /* Exact match within shared-prefix group */
    fbtreeInitIterator(&it, fbt);
    sds seek_val = createString("XXXXXXXXcharlie");
    fbtreeSeekToValue(fbt, seek_val, &it);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "XXXXXXXXcharlie", 16), 0);
    sdsfree(seek_val);

    /* Between two shared-prefix elements */
    fbtreeInitIterator(&it, fbt);
    seek_val = createString("XXXXXXXXcat");
    fbtreeSeekToValue(fbt, seek_val, &it);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "XXXXXXXXcharlie", 16), 0);
    sdsfree(seek_val);

    /* Prev from that position should return bravo */
    fbtreeInitIterator(&it, fbt);
    seek_val = createString("XXXXXXXXcat");
    fbtreeSeekToValue(fbt, seek_val, &it);
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    EXPECT_EQ(memcmp(pos, "XXXXXXXXbravo", 14), 0);
    sdsfree(seek_val);
}

TEST_F(FbtreeTest, SeekToValueDeepTree) {
    const int count = TEST_THREE_LEVEL_ITEMS;
    for (int i = 0; i < count; i++) {
        sds str = createBase26TestString("val_", "", i, 4);
        fbtreeInsert(fbt, str);
    }
    expectValid();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;

    sds seek_mid = createBase26TestString("val_", "", count / 2, 4);
    fbtreeSeekToValue(fbt, seek_mid, &it);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(sdscmp(pos, seek_mid), 0);
    sdsfree(seek_mid);

    sds seek_near_end = createBase26TestString("val_", "", count - 5, 4);
    fbtreeSeekToValue(fbt, seek_near_end, &it);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(sdscmp(pos, seek_near_end), 0);
    sdsfree(seek_near_end);

    sds seek_first = createBase26TestString("val_", "", 0, 4);
    fbtreeSeekToValue(fbt, seek_first, &it);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(sdscmp(pos, seek_first), 0);
    sdsfree(seek_first);

    sds seek_past = createString("zzz_past_end");
    fbtreeSeekToValue(fbt, seek_past, &it);
    EXPECT_FALSE(fbtreeNext(&it, &pos));
    sdsfree(seek_past);
}

TEST_F(FbtreeTest, SeekToValueThenIterate) {
    char buf[16];
    const int count = 200;
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "item_%03d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    expectValid();

    fbtreeIterator it;
    const_sds pos;
    sds seek_val = createString("item_100");

    /* Forward from seek position */
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToValue(fbt, seek_val, &it);
    for (int i = 100; i < count; i++) {
        snprintf(buf, sizeof(buf), "item_%03d", i);
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0);
    }
    EXPECT_FALSE(fbtreeNext(&it, &pos));

    /* Backward from seek position */
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToValue(fbt, seek_val, &it);
    for (int i = 99; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "item_%03d", i);
        ASSERT_TRUE(fbtreePrev(&it, &pos));
        EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0);
    }
    EXPECT_FALSE(fbtreePrev(&it, &pos));

    sdsfree(seek_val);
}

/* ========== Property-Based SeekToValue Tests ========== */

/* Seek positions iterator at first element >= value (forward) */
TEST_F(FbtreeTest, SeekToValuePropertyForwardPositioning) {
    /* Use a separate tree per iteration - free the fixture tree first */
    fbtreeFree(fbt);
    fbt = nullptr;

    const int NUM_ITERATIONS = 150;
    unsigned int seed = 42;

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        fbtreeIndex *tree = fbtreeCreate();

        int num_elements;
        if (iter < 50) {
            num_elements = 1 + (rand_r(&seed) % 60);
        } else if (iter < 100) {
            num_elements = TEST_NODE_CAPACITY + 1 + (rand_r(&seed) % 140);
        } else {
            num_elements = 200 + (rand_r(&seed) % 1300);
        }

        std::vector<sds> sorted_values;
        sorted_values.reserve(num_elements);

        for (int i = 0; i < num_elements; i++) {
            size_t val = rand_r(&seed) % 50000;
            const char *prefixes[] = {"aa_", "bb_", "cc_", "dd_", "ee_"};
            const char *prefix = prefixes[rand_r(&seed) % 5];
            sds s = createBase26TestString(prefix, "", val, 4);
            fbtreeInsert(tree, s);
            sorted_values.push_back(sdsnewlen(s, sdslen(s)));
        }

        std::sort(sorted_values.begin(), sorted_values.end(), [](const sds a, const sds b) {
            return sdscmp(a, b) < 0;
        });

        sds seek_val;
        int seek_type = rand_r(&seed) % 5;
        if (seek_type == 0 && num_elements > 0) {
            int idx = rand_r(&seed) % num_elements;
            seek_val = sdsnewlen(sorted_values[idx], sdslen(sorted_values[idx]));
        } else if (seek_type == 1) {
            seek_val = createString("AAAA");
        } else if (seek_type == 2) {
            seek_val = createString("zzzzzzzz");
        } else {
            size_t val = rand_r(&seed) % 50000;
            const char *prefixes[] = {"aa_", "bb_", "cc_", "dd_", "ee_"};
            const char *prefix = prefixes[rand_r(&seed) % 5];
            seek_val = createBase26TestString(prefix, "", val, 4);
        }

        sds expected = NULL;
        for (size_t i = 0; i < sorted_values.size(); i++) {
            if (sdscmp(sorted_values[i], seek_val) >= 0) {
                expected = sorted_values[i];
                break;
            }
        }

        fbtreeIterator it;
        fbtreeInitIterator(&it, tree);
        fbtreeSeekToValue(tree, seek_val, &it);

        const_sds pos;
        bool got_next = fbtreeNext(&it, &pos);

        if (expected == NULL) {
            ASSERT_FALSE(got_next) << "Iteration " << iter << ": expected no element >= seek value, but got one"
                                   << " (seek_val=" << seek_val << ", num_elements=" << num_elements << ")";
        } else {
            ASSERT_TRUE(got_next) << "Iteration " << iter << ": expected element >= seek value, but got none"
                                  << " (seek_val=" << seek_val << ", num_elements=" << num_elements << ")";
            ASSERT_EQ(sdscmp(pos, expected), 0)
                << "Iteration " << iter << ": wrong element returned"
                << " (seek_val=" << seek_val << ", got=" << pos << ", expected=" << expected
                << ", num_elements=" << num_elements << ")";
        }

        sdsfree(seek_val);
        for (auto &v : sorted_values) sdsfree(v);
        fbtreeFree(tree);
    }
}

/* Seek positions iterator correctly for reverse iteration */
TEST_F(FbtreeTest, SeekToValuePropertyReversePositioning) {
    fbtreeFree(fbt);
    fbt = nullptr;

    const int NUM_ITERATIONS = 150;
    unsigned int seed = 123;

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        fbtreeIndex *tree = fbtreeCreate();

        int num_elements;
        if (iter < 50) {
            num_elements = 1 + (rand_r(&seed) % 60);
        } else if (iter < 100) {
            num_elements = TEST_NODE_CAPACITY + 1 + (rand_r(&seed) % 140);
        } else {
            num_elements = 200 + (rand_r(&seed) % 1300);
        }

        std::vector<sds> sorted_values;
        sorted_values.reserve(num_elements);

        for (int i = 0; i < num_elements; i++) {
            size_t val = rand_r(&seed) % 50000;
            const char *prefixes[] = {"aa_", "bb_", "cc_", "dd_", "ee_"};
            const char *prefix = prefixes[rand_r(&seed) % 5];
            sds s = createBase26TestString(prefix, "", val, 4);
            fbtreeInsert(tree, s);
            sorted_values.push_back(sdsnewlen(s, sdslen(s)));
        }

        std::sort(sorted_values.begin(), sorted_values.end(), [](const sds a, const sds b) {
            return sdscmp(a, b) < 0;
        });

        sds seek_val;
        int seek_type = rand_r(&seed) % 5;
        if (seek_type == 0 && num_elements > 0) {
            int idx = rand_r(&seed) % num_elements;
            seek_val = sdsnewlen(sorted_values[idx], sdslen(sorted_values[idx]));
        } else if (seek_type == 1) {
            seek_val = createString("AAAA");
        } else if (seek_type == 2) {
            seek_val = createString("zzzzzzzz");
        } else {
            size_t val = rand_r(&seed) % 50000;
            const char *prefixes[] = {"aa_", "bb_", "cc_", "dd_", "ee_"};
            const char *prefix = prefixes[rand_r(&seed) % 5];
            seek_val = createBase26TestString(prefix, "", val, 4);
        }

        sds expected_prev = NULL;
        for (int i = (int)sorted_values.size() - 1; i >= 0; i--) {
            if (sdscmp(sorted_values[i], seek_val) < 0) {
                expected_prev = sorted_values[i];
                break;
            }
        }

        fbtreeIterator it;
        fbtreeInitIterator(&it, tree);
        fbtreeSeekToValue(tree, seek_val, &it);

        const_sds pos;
        bool got_prev = fbtreePrev(&it, &pos);

        if (expected_prev == NULL) {
            ASSERT_FALSE(got_prev) << "Iteration " << iter << ": expected no element < seek value, but got one"
                                   << " (seek_val=" << seek_val << ", num_elements=" << num_elements << ")";
        } else {
            ASSERT_TRUE(got_prev) << "Iteration " << iter << ": expected element < seek value, but got none"
                                  << " (seek_val=" << seek_val << ", num_elements=" << num_elements << ")";
            ASSERT_EQ(sdscmp(pos, expected_prev), 0)
                << "Iteration " << iter << ": wrong element returned by fbtreePrev"
                << " (seek_val=" << seek_val << ", got=" << pos << ", expected=" << expected_prev
                << ", num_elements=" << num_elements << ")";
        }

        sdsfree(seek_val);
        for (auto &v : sorted_values) sdsfree(v);
        fbtreeFree(tree);
    }
}

/* Shared-prefix discrimination */
TEST_F(FbtreeTest, SeekToValuePropertySharedPrefixDiscrimination) {
    fbtreeFree(fbt);
    fbt = nullptr;

    const int NUM_ITERATIONS = 150;
    unsigned int seed = 777;

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        fbtreeIndex *tree = fbtreeCreate();

        char prefix[9];
        for (int j = 0; j < 8; j++) {
            prefix[j] = 'A' + (char)(rand_r(&seed) % 26);
        }
        prefix[8] = '\0';

        int num_elements = 10 + (int)(rand_r(&seed) % 491);

        std::vector<sds> sorted_values;
        sorted_values.reserve(num_elements);

        for (int i = 0; i < num_elements; i++) {
            int suffix_len = 1 + (int)(rand_r(&seed) % 12);
            sds s = sdsnewlen(NULL, 8 + suffix_len + 1);
            memcpy(s, prefix, 8);
            for (int k = 0; k < suffix_len; k++) {
                s[8 + k] = 'a' + (char)(rand_r(&seed) % 26);
            }
            s[8 + suffix_len] = '\0';

            fbtreeInsert(tree, s);
            sorted_values.push_back(sdsnewlen(s, sdslen(s)));
        }

        std::sort(sorted_values.begin(), sorted_values.end(), [](const sds a, const sds b) {
            return sdscmp(a, b) < 0;
        });

        int seek_suffix_len = 1 + (int)(rand_r(&seed) % 12);
        sds seek_val = sdsnewlen(NULL, 8 + seek_suffix_len + 1);
        memcpy(seek_val, prefix, 8);
        for (int k = 0; k < seek_suffix_len; k++) {
            seek_val[8 + k] = 'a' + (char)(rand_r(&seed) % 26);
        }
        seek_val[8 + seek_suffix_len] = '\0';

        sds expected = NULL;
        for (size_t i = 0; i < sorted_values.size(); i++) {
            if (sdscmp(sorted_values[i], seek_val) >= 0) {
                expected = sorted_values[i];
                break;
            }
        }

        fbtreeIterator it;
        fbtreeInitIterator(&it, tree);
        fbtreeSeekToValue(tree, seek_val, &it);

        const_sds pos;
        bool got_next = fbtreeNext(&it, &pos);

        if (expected == NULL) {
            ASSERT_FALSE(got_next) << "Iteration " << iter << ": expected no element >= seek value, but got one"
                                   << " (prefix=" << prefix << ", num_elements=" << num_elements << ")";
        } else {
            ASSERT_TRUE(got_next) << "Iteration " << iter << ": expected element >= seek value, but got none"
                                  << " (prefix=" << prefix << ", seek_val=" << seek_val
                                  << ", num_elements=" << num_elements << ")";
            ASSERT_EQ(sdscmp(pos, expected), 0)
                << "Iteration " << iter << ": wrong element returned - full value comparison not used"
                << " (prefix=" << prefix << ", seek_val=" << seek_val << ", got=" << pos
                << ", expected=" << expected << ", num_elements=" << num_elements << ")";

            fbtreeInitIterator(&it, tree);
            fbtreeSeekToValue(tree, seek_val, &it);
            const_sds prev_pos;
            bool got_prev = fbtreePrev(&it, &prev_pos);
            if (got_prev) {
                ASSERT_LT(sdscmp(prev_pos, seek_val), 0)
                    << "Iteration " << iter << ": element before seek position is not < seek value"
                    << " (prefix=" << prefix << ", prev=" << prev_pos << ", seek_val=" << seek_val << ")";
            }
        }

        sdsfree(seek_val);
        for (auto &v : sorted_values) sdsfree(v);
        fbtreeFree(tree);
    }
}

/* ========== fbtreeDeleteRangeByRank Tests ========== */

TEST_F(FbtreeTest, DeleteRangeByRankEmpty) {
    EXPECT_EQ(fbtreeDeleteRangeByRank(fbt, 0, 0, NULL, NULL), 0u);
    expectValid();
}

TEST_F(FbtreeTest, DeleteRangeByRankSingle) {
    insert("a");
    EXPECT_EQ(fbtreeDeleteRangeByRank(fbt, 0, 0, NULL, NULL), 1u);
    EXPECT_EQ(fbtreeLength(fbt), 0u);
    expectValid();
}

TEST_F(FbtreeTest, DeleteRangeByRankAll) {
    for (int i = 0; i < 10; i++) {
        fbtreeInsert(fbt, createBase26TestString("", "", i, 3));
    }
    EXPECT_EQ(fbtreeDeleteRangeByRank(fbt, 0, 9, NULL, NULL), 10u);
    EXPECT_EQ(fbtreeLength(fbt), 0u);
    expectValid();
}

TEST_F(FbtreeTest, DeleteRangeByRankMiddle) {
    for (int i = 0; i < 10; i++) {
        fbtreeInsert(fbt, createBase26TestString("", "", i, 3));
    }
    /* Delete ranks 3..6 (4 elements: AAD, AAE, AAF, AAG) */
    EXPECT_EQ(fbtreeDeleteRangeByRank(fbt, 3, 6, NULL, NULL), 4u);
    EXPECT_EQ(fbtreeLength(fbt), 6u);
    expectValid();

    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), 6u);
    /* Element just before deleted range (was rank 2) should survive */
    EXPECT_EQ(remaining[2], std::string("AAC\0", 4));
    /* Element just after deleted range (was rank 7) should now be at rank 3 */
    EXPECT_EQ(remaining[3], std::string("AAH\0", 4));
}

TEST_F(FbtreeTest, DeleteRangeByRankFirst) {
    for (int i = 0; i < 10; i++) {
        fbtreeInsert(fbt, createBase26TestString("", "", i, 3));
    }
    EXPECT_EQ(fbtreeDeleteRangeByRank(fbt, 0, 2, NULL, NULL), 3u);
    EXPECT_EQ(fbtreeLength(fbt), 7u);
    expectValid();

    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), 7u);
    /* First surviving element should be what was rank 3 (AAD) */
    EXPECT_EQ(remaining[0], std::string("AAD\0", 4));
}

TEST_F(FbtreeTest, DeleteRangeByRankLast) {
    for (int i = 0; i < 10; i++) {
        fbtreeInsert(fbt, createBase26TestString("", "", i, 3));
    }
    EXPECT_EQ(fbtreeDeleteRangeByRank(fbt, 7, 9, NULL, NULL), 3u);
    EXPECT_EQ(fbtreeLength(fbt), 7u);
    expectValid();

    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), 7u);
    /* Last surviving element should be what was rank 6 (AAG) */
    EXPECT_EQ(remaining[6], std::string("AAG\0", 4));
}

TEST_F(FbtreeTest, DeleteRangeByRankOutOfBounds) {
    for (int i = 0; i < 5; i++) {
        fbtreeInsert(fbt, createBase26TestString("", "", i, 3));
    }
    /* end_rank beyond length - should clamp */
    EXPECT_EQ(fbtreeDeleteRangeByRank(fbt, 3, 100, NULL, NULL), 2u);
    EXPECT_EQ(fbtreeLength(fbt), 3u);
    expectValid();

    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), 3u);
    /* Last surviving element should be what was rank 2 (AAC) */
    EXPECT_EQ(remaining[2], std::string("AAC\0", 4));
}

TEST_F(FbtreeTest, DeleteRangeByRankStartBeyondLength) {
    for (int i = 0; i < 5; i++) {
        fbtreeInsert(fbt, createBase26TestString("", "", i, 3));
    }
    EXPECT_EQ(fbtreeDeleteRangeByRank(fbt, 10, 20, NULL, NULL), 0u);
    EXPECT_EQ(fbtreeLength(fbt), 5u);
    expectValid();
}

TEST_F(FbtreeTest, DeleteRangeByRankInvertedRange) {
    for (int i = 0; i < 5; i++) {
        fbtreeInsert(fbt, createBase26TestString("", "", i, 3));
    }
    /* start > end should delete nothing */
    EXPECT_EQ(fbtreeDeleteRangeByRank(fbt, 3, 1, NULL, NULL), 0u);
    EXPECT_EQ(fbtreeLength(fbt), 5u);
    expectValid();
}

TEST_F(FbtreeTest, DeleteRangeByRankMultilevel) {
    /* Build a tree with enough elements to have multiple inner node levels */
    const int N = TEST_NODE_CAPACITY * 4;
    for (int i = 0; i < N; i++) {
        fbtreeInsert(fbt, createBase26TestString("key_", "", i, 5));
    }
    expectValid();

    /* Delete a range spanning multiple leaves */
    unsigned long start = TEST_NODE_CAPACITY / 2;
    unsigned long end = TEST_NODE_CAPACITY * 2 + TEST_NODE_CAPACITY / 2;
    unsigned long expected = end - start + 1;
    EXPECT_EQ(fbtreeDeleteRangeByRank(fbt, start, end, NULL, NULL), expected);
    EXPECT_EQ(fbtreeLength(fbt), (unsigned long)(N - expected));
    expectValid();

    /* Verify iteration still works */
    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), (size_t)(N - expected));
    auto backward = collectBackward();
    EXPECT_EQ(backward.size(), remaining.size());

    /* Verify boundary elements survived */
    sds expected_before = createBase26TestString("key_", "", start - 1, 5);
    sds expected_after = createBase26TestString("key_", "", end + 1, 5);
    EXPECT_EQ(remaining[start - 1], std::string(expected_before, sdslen(expected_before)));
    EXPECT_EQ(remaining[start], std::string(expected_after, sdslen(expected_after)));
    sdsfree(expected_before);
    sdsfree(expected_after);
}

TEST_F(FbtreeTest, DeleteRangeByRankThenInsert) {
    for (int i = 0; i < 20; i++) {
        fbtreeInsert(fbt, createBase26TestString("", "", i, 3));
    }
    EXPECT_EQ(fbtreeDeleteRangeByRank(fbt, 5, 14, NULL, NULL), 10u);
    EXPECT_EQ(fbtreeLength(fbt), 10u);
    expectValid();

    /* Insert new elements after range delete */
    for (int i = 100; i < 110; i++) {
        fbtreeInsert(fbt, createBase26TestString("", "", i, 3));
    }
    EXPECT_EQ(fbtreeLength(fbt), 20u);
    expectValid();

    auto all = collectForward();
    EXPECT_EQ(all.size(), 20u);
}

TEST_F(FbtreeTest, DeleteRangeByRankDeepTree) {
    /* Build a 3+ level tree */
    const int N = TEST_THREE_LEVEL_ITEMS;
    for (int i = 0; i < N; i++) {
        fbtreeInsert(fbt, createBase26TestString("deep_", "", i, 6));
    }
    expectValid();

    /* Delete a large chunk from the middle */
    unsigned long mid = N / 3;
    unsigned long end = 2 * N / 3;
    unsigned long expected = end - mid + 1;
    EXPECT_EQ(fbtreeDeleteRangeByRank(fbt, mid, end, NULL, NULL), expected);
    EXPECT_EQ(fbtreeLength(fbt), (unsigned long)(N - expected));
    expectValid();
}

/* ========== fbtreeDeleteRangeByScore Tests ========== */

TEST_F(FbtreeTest, DeleteRangeByScoreEmpty) {
    EXPECT_EQ(fbtreeDeleteRangeByScore(fbt, "AAAAAAAA", "ZZZZZZZZ", 0, 0, NULL, NULL), 0u);
    expectValid();
}

TEST_F(FbtreeTest, DeleteRangeByScoreAll) {
    fbtreeInsert(fbt, createString("AAAAAAAAelem_0"));
    fbtreeInsert(fbt, createString("BBBBBBBBelem_1"));
    fbtreeInsert(fbt, createString("CCCCCCCCelem_2"));

    /* Range covers all scores */
    EXPECT_EQ(fbtreeDeleteRangeByScore(fbt, "AAAAAAAA", "CCCCCCCC", 0, 0, NULL, NULL), 3u);
    EXPECT_EQ(fbtreeLength(fbt), 0u);
    expectValid();
}

TEST_F(FbtreeTest, DeleteRangeByScoreMiddle) {
    fbtreeInsert(fbt, createString("AAAAAAAAelem_0"));
    fbtreeInsert(fbt, createString("BBBBBBBBelem_1"));
    fbtreeInsert(fbt, createString("CCCCCCCCelem_2"));
    fbtreeInsert(fbt, createString("DDDDDDDDelem_3"));
    fbtreeInsert(fbt, createString("EEEEEEEEelem_4"));

    /* Delete B and C scores */
    EXPECT_EQ(fbtreeDeleteRangeByScore(fbt, "BBBBBBBB", "CCCCCCCC", 0, 0, NULL, NULL), 2u);
    EXPECT_EQ(fbtreeLength(fbt), 3u);
    expectValid();

    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), 3u);
    EXPECT_EQ(memcmp(remaining[0].data(), "AAAAAAAA", 8), 0);
    EXPECT_EQ(memcmp(remaining[1].data(), "DDDDDDDD", 8), 0);
    EXPECT_EQ(memcmp(remaining[2].data(), "EEEEEEEE", 8), 0);
}

TEST_F(FbtreeTest, DeleteRangeByScoreExclusiveMin) {
    fbtreeInsert(fbt, createString("AAAAAAAAelem_0"));
    fbtreeInsert(fbt, createString("BBBBBBBBelem_1"));
    fbtreeInsert(fbt, createString("CCCCCCCCelem_2"));

    /* Exclusive min: should skip B, only delete C */
    EXPECT_EQ(fbtreeDeleteRangeByScore(fbt, "BBBBBBBB", "CCCCCCCC", 1, 0, NULL, NULL), 1u);
    EXPECT_EQ(fbtreeLength(fbt), 2u);
    expectValid();

    auto remaining = collectForward();
    EXPECT_EQ(memcmp(remaining[0].data(), "AAAAAAAA", 8), 0);
    EXPECT_EQ(memcmp(remaining[1].data(), "BBBBBBBB", 8), 0);
}

TEST_F(FbtreeTest, DeleteRangeByScoreExclusiveMax) {
    fbtreeInsert(fbt, createString("AAAAAAAAelem_0"));
    fbtreeInsert(fbt, createString("BBBBBBBBelem_1"));
    fbtreeInsert(fbt, createString("CCCCCCCCelem_2"));

    /* Exclusive max: should skip C, only delete B */
    EXPECT_EQ(fbtreeDeleteRangeByScore(fbt, "BBBBBBBB", "CCCCCCCC", 0, 1, NULL, NULL), 1u);
    EXPECT_EQ(fbtreeLength(fbt), 2u);
    expectValid();

    auto remaining = collectForward();
    EXPECT_EQ(memcmp(remaining[0].data(), "AAAAAAAA", 8), 0);
    EXPECT_EQ(memcmp(remaining[1].data(), "CCCCCCCC", 8), 0);
}

TEST_F(FbtreeTest, DeleteRangeByScoreBothExclusive) {
    fbtreeInsert(fbt, createString("AAAAAAAAelem_0"));
    fbtreeInsert(fbt, createString("BBBBBBBBelem_1"));
    fbtreeInsert(fbt, createString("CCCCCCCCelem_2"));
    fbtreeInsert(fbt, createString("DDDDDDDDelem_3"));

    /* Both exclusive on B..D: should only delete C */
    EXPECT_EQ(fbtreeDeleteRangeByScore(fbt, "BBBBBBBB", "DDDDDDDD", 1, 1, NULL, NULL), 1u);
    EXPECT_EQ(fbtreeLength(fbt), 3u);
    expectValid();

    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), 3u);
    EXPECT_EQ(memcmp(remaining[0].data(), "AAAAAAAA", 8), 0);
    EXPECT_EQ(memcmp(remaining[1].data(), "BBBBBBBB", 8), 0);
    EXPECT_EQ(memcmp(remaining[2].data(), "DDDDDDDD", 8), 0);
}

TEST_F(FbtreeTest, DeleteRangeByScoreNoMatch) {
    fbtreeInsert(fbt, createString("AAAAAAAAelem_0"));
    fbtreeInsert(fbt, createString("DDDDDDDDelem_1"));

    /* Range between existing scores - nothing to delete */
    EXPECT_EQ(fbtreeDeleteRangeByScore(fbt, "BBBBBBBB", "CCCCCCCC", 0, 0, NULL, NULL), 0u);
    EXPECT_EQ(fbtreeLength(fbt), 2u);
    expectValid();
}

TEST_F(FbtreeTest, DeleteRangeByScorePastEnd) {
    fbtreeInsert(fbt, createString("AAAAAAAAelem_0"));
    fbtreeInsert(fbt, createString("BBBBBBBBelem_1"));

    /* Range entirely beyond all elements */
    EXPECT_EQ(fbtreeDeleteRangeByScore(fbt, "YYYYYYYY", "ZZZZZZZZ", 0, 0, NULL, NULL), 0u);
    EXPECT_EQ(fbtreeLength(fbt), 2u);
    expectValid();
}

TEST_F(FbtreeTest, DeleteRangeByScoreBeforeStart) {
    fbtreeInsert(fbt, createString("MMMMMMMMelem_0"));
    fbtreeInsert(fbt, createString("NNNNNNNNelem_1"));

    /* Range entirely before all elements */
    EXPECT_EQ(fbtreeDeleteRangeByScore(fbt, "AAAAAAAA", "BBBBBBBB", 0, 0, NULL, NULL), 0u);
    EXPECT_EQ(fbtreeLength(fbt), 2u);
    expectValid();
}

TEST_F(FbtreeTest, DeleteRangeByScoreDuplicateScores) {
    /* Multiple elements with same score prefix but different suffixes */
    fbtreeInsert(fbt, createString("BBBBBBBBaaa"));
    fbtreeInsert(fbt, createString("BBBBBBBBbbb"));
    fbtreeInsert(fbt, createString("BBBBBBBBccc"));
    fbtreeInsert(fbt, createString("CCCCCCCCddd"));

    EXPECT_EQ(fbtreeDeleteRangeByScore(fbt, "BBBBBBBB", "BBBBBBBB", 0, 0, NULL, NULL), 3u);
    EXPECT_EQ(fbtreeLength(fbt), 1u);
    expectValid();

    auto remaining = collectForward();
    EXPECT_EQ(memcmp(remaining[0].data(), "CCCCCCCC", 8), 0);
}

TEST_F(FbtreeTest, DeleteRangeByScoreMultilevel) {
    /* Build a multilevel tree with score-prefixed elements */
    const int N = TEST_NODE_CAPACITY * 4;
    for (int i = 0; i < N; i++) {
        /* Create 8-byte score prefix from index, then element suffix */
        char score[9];
        snprintf(score, sizeof(score), "%08d", i);
        sds s = sdsnewlen(NULL, 8 + 6 + 1);
        memcpy(s, score, 8);
        snprintf(s + 8, 7, "elem%c", '\0');
        s[8 + 6] = '\0';
        fbtreeInsert(fbt, s);
    }
    expectValid();

    /* Delete a range in the middle */
    char min_score[9], max_score[9];
    snprintf(min_score, sizeof(min_score), "%08d", N / 4);
    snprintf(max_score, sizeof(max_score), "%08d", 3 * N / 4);

    unsigned long before = fbtreeLength(fbt);
    unsigned long deleted = fbtreeDeleteRangeByScore(fbt, min_score, max_score, 0, 0, NULL, NULL);
    EXPECT_GT(deleted, 0u);
    EXPECT_EQ(fbtreeLength(fbt), before - deleted);
    expectValid();

    /* Verify iteration still works correctly */
    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), (size_t)(before - deleted));
}

TEST_F(FbtreeTest, DeleteRangeByScoreThenInsert) {
    fbtreeInsert(fbt, createString("AAAAAAAAelem_0"));
    fbtreeInsert(fbt, createString("BBBBBBBBelem_1"));
    fbtreeInsert(fbt, createString("CCCCCCCCelem_2"));
    fbtreeInsert(fbt, createString("DDDDDDDDelem_3"));

    EXPECT_EQ(fbtreeDeleteRangeByScore(fbt, "BBBBBBBB", "CCCCCCCC", 0, 0, NULL, NULL), 2u);
    expectValid();

    /* Insert into the gap */
    fbtreeInsert(fbt, createString("BBBBBBBBnew_elem"));
    EXPECT_EQ(fbtreeLength(fbt), 3u);
    expectValid();

    auto all = collectForward();
    EXPECT_EQ(all.size(), 3u);
    EXPECT_EQ(memcmp(all[0].data(), "AAAAAAAA", 8), 0);
    EXPECT_EQ(memcmp(all[1].data(), "BBBBBBBB", 8), 0);
    EXPECT_EQ(memcmp(all[2].data(), "DDDDDDDD", 8), 0);
}

/* ========== fbtreeDeleteRangeByValue Tests ========== */

TEST_F(FbtreeTest, DeleteRangeByValueEmpty) {
    sds min_val = createString("aaa");
    sds max_val = createString("zzz");
    EXPECT_EQ(fbtreeDeleteRangeByValue(fbt, min_val, max_val, 0, 0, NULL, NULL), 0u);
    sdsfree(min_val);
    sdsfree(max_val);
    expectValid();
}

TEST_F(FbtreeTest, DeleteRangeByValueAll) {
    insert("bbb");
    insert("ccc");
    insert("ddd");

    sds min_val = createString("aaa");
    sds max_val = createString("eee");
    EXPECT_EQ(fbtreeDeleteRangeByValue(fbt, min_val, max_val, 0, 0, NULL, NULL), 3u);
    EXPECT_EQ(fbtreeLength(fbt), 0u);
    sdsfree(min_val);
    sdsfree(max_val);
    expectValid();
}

TEST_F(FbtreeTest, DeleteRangeByValueMiddle) {
    insert("aaa");
    insert("bbb");
    insert("ccc");
    insert("ddd");
    insert("eee");

    sds min_val = createString("bbb");
    sds max_val = createString("ddd");
    EXPECT_EQ(fbtreeDeleteRangeByValue(fbt, min_val, max_val, 0, 0, NULL, NULL), 3u);
    EXPECT_EQ(fbtreeLength(fbt), 2u);
    sdsfree(min_val);
    sdsfree(max_val);
    expectValid();

    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), 2u);
    EXPECT_EQ(remaining[0], std::string("aaa\0", 4));
    EXPECT_EQ(remaining[1], std::string("eee\0", 4));
}

TEST_F(FbtreeTest, DeleteRangeByValueExclusiveMin) {
    insert("aaa");
    insert("bbb");
    insert("ccc");

    sds min_val = createString("aaa");
    sds max_val = createString("ccc");
    EXPECT_EQ(fbtreeDeleteRangeByValue(fbt, min_val, max_val, 1, 0, NULL, NULL), 2u);
    EXPECT_EQ(fbtreeLength(fbt), 1u);
    sdsfree(min_val);
    sdsfree(max_val);
    expectValid();

    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), 1u);
    EXPECT_EQ(remaining[0], std::string("aaa\0", 4));
}

TEST_F(FbtreeTest, DeleteRangeByValueExclusiveMax) {
    insert("aaa");
    insert("bbb");
    insert("ccc");

    sds min_val = createString("aaa");
    sds max_val = createString("ccc");
    EXPECT_EQ(fbtreeDeleteRangeByValue(fbt, min_val, max_val, 0, 1, NULL, NULL), 2u);
    EXPECT_EQ(fbtreeLength(fbt), 1u);
    sdsfree(min_val);
    sdsfree(max_val);
    expectValid();

    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), 1u);
    EXPECT_EQ(remaining[0], std::string("ccc\0", 4));
}

TEST_F(FbtreeTest, DeleteRangeByValueBothExclusive) {
    insert("aaa");
    insert("bbb");
    insert("ccc");
    insert("ddd");

    sds min_val = createString("aaa");
    sds max_val = createString("ddd");
    EXPECT_EQ(fbtreeDeleteRangeByValue(fbt, min_val, max_val, 1, 1, NULL, NULL), 2u);
    EXPECT_EQ(fbtreeLength(fbt), 2u);
    sdsfree(min_val);
    sdsfree(max_val);
    expectValid();

    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), 2u);
    EXPECT_EQ(remaining[0], std::string("aaa\0", 4));
    EXPECT_EQ(remaining[1], std::string("ddd\0", 4));
}

TEST_F(FbtreeTest, DeleteRangeByValueNoMatch) {
    insert("aaa");
    insert("ddd");

    sds min_val = createString("bbb");
    sds max_val = createString("ccc");
    EXPECT_EQ(fbtreeDeleteRangeByValue(fbt, min_val, max_val, 0, 0, NULL, NULL), 0u);
    EXPECT_EQ(fbtreeLength(fbt), 2u);
    sdsfree(min_val);
    sdsfree(max_val);
    expectValid();
}

TEST_F(FbtreeTest, DeleteRangeByValueExactMatch) {
    insert("aaa");
    insert("bbb");
    insert("ccc");

    /* min == max, inclusive: delete exactly one element */
    sds val = createString("bbb");
    sds val2 = createString("bbb");
    EXPECT_EQ(fbtreeDeleteRangeByValue(fbt, val, val2, 0, 0, NULL, NULL), 1u);
    EXPECT_EQ(fbtreeLength(fbt), 2u);
    sdsfree(val);
    sdsfree(val2);
    expectValid();

    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), 2u);
    EXPECT_EQ(remaining[0], std::string("aaa\0", 4));
    EXPECT_EQ(remaining[1], std::string("ccc\0", 4));
}

TEST_F(FbtreeTest, DeleteRangeByValueExactMatchExclusive) {
    insert("aaa");
    insert("bbb");
    insert("ccc");

    /* min == max, both exclusive: delete nothing */
    sds val = createString("bbb");
    sds val2 = createString("bbb");
    EXPECT_EQ(fbtreeDeleteRangeByValue(fbt, val, val2, 1, 1, NULL, NULL), 0u);
    EXPECT_EQ(fbtreeLength(fbt), 3u);
    sdsfree(val);
    sdsfree(val2);
    expectValid();

    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), 3u);
    EXPECT_EQ(remaining[0], std::string("aaa\0", 4));
    EXPECT_EQ(remaining[1], std::string("bbb\0", 4));
    EXPECT_EQ(remaining[2], std::string("ccc\0", 4));
}

TEST_F(FbtreeTest, DeleteRangeByValueMultilevel) {
    const int N = TEST_NODE_CAPACITY * 4;
    for (int i = 0; i < N; i++) {
        fbtreeInsert(fbt, createBase26TestString("val_", "", i, 5));
    }
    expectValid();

    /* Delete a range in the middle using value comparison */
    sds min_val = createBase26TestString("val_", "", N / 4, 5);
    sds max_val = createBase26TestString("val_", "", 3 * N / 4, 5);

    unsigned long before = fbtreeLength(fbt);
    unsigned long deleted = fbtreeDeleteRangeByValue(fbt, min_val, max_val, 0, 0, NULL, NULL);
    EXPECT_GT(deleted, 0u);
    EXPECT_EQ(fbtreeLength(fbt), before - deleted);
    sdsfree(min_val);
    sdsfree(max_val);
    expectValid();

    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), (size_t)(before - deleted));
}

TEST_F(FbtreeTest, DeleteRangeByValueWithScorePrefix) {
    /* Simulate the adapter pattern: [8-byte score][element] */
    fbtreeInsert(fbt, createString("SCOREAAAbbb"));
    fbtreeInsert(fbt, createString("SCOREAAAccc"));
    fbtreeInsert(fbt, createString("SCOREAAAddd"));
    fbtreeInsert(fbt, createString("SCOREAAAeee"));
    fbtreeInsert(fbt, createString("SCOREAAAfff"));

    /* Delete lex range [ccc, eee] within the same score prefix */
    sds min_val = createString("SCOREAAAccc");
    sds max_val = createString("SCOREAAAeee");
    EXPECT_EQ(fbtreeDeleteRangeByValue(fbt, min_val, max_val, 0, 0, NULL, NULL), 3u);
    EXPECT_EQ(fbtreeLength(fbt), 2u);
    sdsfree(min_val);
    sdsfree(max_val);
    expectValid();

    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), 2u);
    EXPECT_EQ(remaining[0], std::string("SCOREAAAbbb\0", 12));
    EXPECT_EQ(remaining[1], std::string("SCOREAAAfff\0", 12));
}

TEST_F(FbtreeTest, DeleteRangeByValueThenInsert) {
    insert("aaa");
    insert("bbb");
    insert("ccc");
    insert("ddd");

    sds min_val = createString("bbb");
    sds max_val = createString("ccc");
    EXPECT_EQ(fbtreeDeleteRangeByValue(fbt, min_val, max_val, 0, 0, NULL, NULL), 2u);
    sdsfree(min_val);
    sdsfree(max_val);
    expectValid();

    insert("bbb");
    EXPECT_EQ(fbtreeLength(fbt), 3u);
    expectValid();

    auto all = collectForward();
    EXPECT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0], std::string("aaa\0", 4));
    EXPECT_EQ(all[1], std::string("bbb\0", 4));
    EXPECT_EQ(all[2], std::string("ddd\0", 4));
}


TEST_F(FbtreeTest, DeleteRangeByRankSingleMiddle) {
    for (int i = 0; i < 5; i++) {
        fbtreeInsert(fbt, createBase26TestString("", "", i, 3));
    }
    /* Delete exactly one element at rank 2 */
    EXPECT_EQ(fbtreeDeleteRangeByRank(fbt, 2, 2, NULL, NULL), 1u);
    EXPECT_EQ(fbtreeLength(fbt), 4u);
    expectValid();

    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), 4u);
    EXPECT_EQ(remaining[0], std::string("AAA\0", 4));
    EXPECT_EQ(remaining[1], std::string("AAB\0", 4));
    EXPECT_EQ(remaining[2], std::string("AAD\0", 4));
    EXPECT_EQ(remaining[3], std::string("AAE\0", 4));
}

TEST_F(FbtreeTest, DeleteRangeByScoreAdjacentExclusive) {
    fbtreeInsert(fbt, createString("AAAAAAAAelem_0"));
    fbtreeInsert(fbt, createString("BBBBBBBBelem_1"));
    fbtreeInsert(fbt, createString("CCCCCCCCelem_2"));

    /* Both exclusive on adjacent scores B..C: nothing between them */
    EXPECT_EQ(fbtreeDeleteRangeByScore(fbt, "BBBBBBBB", "CCCCCCCC", 1, 1, NULL, NULL), 0u);
    EXPECT_EQ(fbtreeLength(fbt), 3u);
    expectValid();
}

TEST_F(FbtreeTest, DeleteRangeByValueAdjacentExclusive) {
    insert("aaa");
    insert("bbb");
    insert("ccc");

    /* Both exclusive on adjacent values: nothing between bbb and ccc */
    sds min_val = createString("bbb");
    sds max_val = createString("ccc");
    EXPECT_EQ(fbtreeDeleteRangeByValue(fbt, min_val, max_val, 1, 1, NULL, NULL), 0u);
    EXPECT_EQ(fbtreeLength(fbt), 3u);
    sdsfree(min_val);
    sdsfree(max_val);
    expectValid();

    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), 3u);
    EXPECT_EQ(remaining[0], std::string("aaa\0", 4));
    EXPECT_EQ(remaining[1], std::string("bbb\0", 4));
    EXPECT_EQ(remaining[2], std::string("ccc\0", 4));
}


TEST_F(FbtreeTest, DeleteRangeByRankSweep) {
    /* Sweep many start/end combinations on a multilevel tree to exercise
     * every possible split point in the optimized range deletion. */
    const int N = TEST_NODE_CAPACITY * 4;
    unsigned long step = TEST_NODE_CAPACITY / 2;

    /* Build reference set once */
    std::vector<std::string> all_elements;
    for (int i = 0; i < N; i++) {
        sds s = createBase26TestString("k_", "", i, 5);
        all_elements.emplace_back(s, sdslen(s));
        sdsfree(s);
    }

    for (unsigned long start = 0; start < (unsigned long)N; start += step) {
        for (unsigned long end = start; end < (unsigned long)N; end += step) {
            fbtreeIndex *tree = fbtreeCreate();
            for (int i = 0; i < N; i++) {
                fbtreeInsert(tree, createBase26TestString("k_", "", i, 5));
            }

            unsigned long clamped_end = end >= (unsigned long)N ? (unsigned long)(N - 1) : end;
            unsigned long expected = clamped_end - start + 1;

            unsigned long deleted = fbtreeDeleteRangeByRank(tree, start, end, NULL, NULL);
            unsigned long remaining = fbtreeLength(tree);

            EXPECT_EQ(deleted, expected)
                << "start=" << start << " end=" << end;
            EXPECT_EQ(remaining, (unsigned long)N - deleted)
                << "start=" << start << " end=" << end;
            EXPECT_TRUE(fbtreeDebugValidate(tree, false))
                << "Validation failed: start=" << start << " end=" << end;

            /* Verify surviving elements match expected */
            fbtreeIterator it;
            fbtreeInitIterator(&it, tree);
            const_sds pos;
            size_t idx = 0;
            while (fbtreeNext(&it, &pos)) {
                /* Find the next expected surviving element */
                while (idx >= start && idx <= clamped_end) idx++;
                ASSERT_LT(idx, (size_t)N)
                    << "Too many elements after delete: start=" << start << " end=" << end;
                EXPECT_EQ(std::string(pos, sdslen(pos)), all_elements[idx])
                    << "Wrong element at position: start=" << start << " end=" << end << " idx=" << idx;
                idx++;
            }

            fbtreeFree(tree);
        }
    }
}

/* ========== Node Merge Unit Tests ========== */

/* MIN_FILL = NODE_SIZE / 4 = 15 */
#define TEST_MIN_FILL (TEST_NODE_CAPACITY / 4)

/* Insert enough items to create 2 leaves, then delete from one leaf until
 * underflow triggers a merge back to a single leaf. */
TEST_F(FbtreeTest, NodeMergeLeafBasic) {
    /* Insert NODE_SIZE+1 items sequentially. With append pattern, the split
     * creates a full left leaf (NODE_SIZE items) and a right leaf with 1 item. */
    const int count = TEST_NODE_CAPACITY + 1;
    char buf[16];
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "item_%03d", i);
        inserted[i] = fbtreeInsert(fbt, createString(buf));
    }
    expectValid();
    EXPECT_EQ(fbtreeLength(fbt), (size_t)count);

    /* Delete items from the beginning of the tree. This removes items from the
     * left (larger) leaf. After enough deletes, the left leaf drops below
     * MIN_FILL and should merge with the right leaf (which has few items). */
    int to_delete = count - TEST_MIN_FILL + 1; /* leave fewer than MIN_FILL in left leaf */
    for (int i = 0; i < to_delete; i++) {
        EXPECT_TRUE(fbtreeDelete(fbt, inserted[i]));
    }
    int remaining = count - to_delete;
    EXPECT_EQ(fbtreeLength(fbt), (size_t)remaining);
    expectValid();

    /* Verify all remaining items are accessible via iteration */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    int iter_count = 0;
    while (fbtreeNext(&it, &pos)) iter_count++;
    EXPECT_EQ(iter_count, remaining);

    zfree(inserted);
}

/* Build a 3-level tree, delete enough items to cause inner node underflow
 * and merge. */
TEST_F(FbtreeTest, NodeMergeInner) {
    /* Build a 3-level tree */
    const int count = TEST_THREE_LEVEL_ITEMS;
    char buf[16];
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%05d", i);
        inserted[i] = fbtreeInsert(fbt, createString(buf));
    }
    expectValid();
    EXPECT_EQ(fbtreeLength(fbt), (size_t)count);

    /* Delete a large portion of items from the beginning. This will cause
     * multiple leaf merges, which in turn remove children from inner nodes,
     * eventually causing inner node underflow and merge. */
    int to_delete = count * 3 / 4;
    for (int i = 0; i < to_delete; i++) {
        EXPECT_TRUE(fbtreeDelete(fbt, inserted[i]));
    }
    int remaining = count - to_delete;
    EXPECT_EQ(fbtreeLength(fbt), (size_t)remaining);
    expectValid();

    /* Verify iteration produces correct sorted order */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = to_delete; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%05d", i);
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0);
    }
    EXPECT_FALSE(fbtreeNext(&it, &pos));

    zfree(inserted);
}

/* Build a tree where deletes trigger merges at multiple levels as the
 * recursion unwinds. */
TEST_F(FbtreeTest, NodeMergeCascading) {
    /* Build a 3-level tree */
    const int count = TEST_THREE_LEVEL_ITEMS;
    char buf[16];
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "cas_%05d", i);
        inserted[i] = fbtreeInsert(fbt, createString(buf));
    }
    expectValid();

    /* Delete most items, leaving very few. This forces cascading merges:
     * leaf merges → inner node child removal → inner node underflow → inner merge.
     * Delete all but ~20 items spread across the range. */
    for (int i = 0; i < count; i++) {
        /* Keep every (count/20)th item */
        if (i % (count / 20) == 0) continue;
        EXPECT_TRUE(fbtreeDelete(fbt, inserted[i]));
    }
    expectValid();

    /* Verify remaining items are correct */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    const_sds prev_pos = nullptr;
    int iter_count = 0;
    while (fbtreeNext(&it, &pos)) {
        if (prev_pos) {
            EXPECT_LT(sdscmp(prev_pos, pos), 0) << "Sorted order violated after cascading merge";
        }
        prev_pos = pos;
        iter_count++;
    }
    EXPECT_EQ((size_t)iter_count, fbtreeLength(fbt));

    zfree(inserted);
}

/* Build a tree where the underflowed node's sibling has NODE_SIZE items,
 * so merge is impossible. */
TEST_F(FbtreeTest, NodeMergeNoMergeWhenSiblingFull) {
    /* Strategy: insert items in a pattern that creates a full leaf (NODE_SIZE items)
     * next to a leaf that will underflow. With sequential insert and middle-split,
     * we get two leaves of ~NODE_SIZE/2 each. Instead, we'll build a larger tree
     * and selectively delete to create the scenario.
     *
     * Build a tree with 3*NODE_SIZE items (3+ leaves). Delete from the middle leaf
     * until it underflows. If both neighbors are full (NODE_SIZE), no merge occurs. */
    const int count = TEST_NODE_CAPACITY * 3;
    char buf[16];
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "nfm_%04d", i);
        inserted[i] = fbtreeInsert(fbt, createString(buf));
    }
    expectValid();

    /* Delete items from the middle region to cause underflow in one leaf.
     * The tree should still validate - if merge can't happen (sibling full),
     * the underflowed node persists. */
    int mid_start = TEST_NODE_CAPACITY;
    int mid_end = mid_start + TEST_NODE_CAPACITY - TEST_MIN_FILL;
    for (int i = mid_start; i < mid_end; i++) {
        EXPECT_TRUE(fbtreeDelete(fbt, inserted[i]));
    }
    int remaining = count - (mid_end - mid_start);
    EXPECT_EQ(fbtreeLength(fbt), (size_t)remaining);
    expectValid();

    /* Verify all remaining items are accessible */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    int iter_count = 0;
    while (fbtreeNext(&it, &pos)) iter_count++;
    EXPECT_EQ(iter_count, remaining);

    zfree(inserted);
}

/* Verify that after merging the leftmost or rightmost leaf, the cache
 * pointers are correct. */
TEST_F(FbtreeTest, NodeMergeLeafCacheUpdate) {
    const int count = TEST_NODE_CAPACITY * 3;
    char buf[16];
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "cache_%04d", i);
        inserted[i] = fbtreeInsert(fbt, createString(buf));
    }
    expectValid();

    /* Part 1: Delete from leftmost leaf until merge, verify leftmost_leaf cache
     * is correct via forward iteration. */
    for (int i = 0; i < TEST_NODE_CAPACITY; i++) {
        EXPECT_TRUE(fbtreeDelete(fbt, inserted[i]));
    }
    expectValid();

    /* Forward iteration should start from the correct leftmost leaf */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    snprintf(buf, sizeof(buf), "cache_%04d", TEST_NODE_CAPACITY);
    EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0)
        << "leftmost_leaf cache incorrect after merge";

    /* Part 2: Delete from rightmost leaf until merge, verify rightmost_leaf cache
     * is correct via backward iteration. */
    for (int i = count - 1; i >= count - TEST_NODE_CAPACITY; i--) {
        EXPECT_TRUE(fbtreeDelete(fbt, inserted[i]));
    }
    expectValid();

    /* Backward iteration should start from the correct rightmost leaf */
    fbtreeInitIterator(&it, fbt);
    ASSERT_TRUE(fbtreePrev(&it, &pos));
    snprintf(buf, sizeof(buf), "cache_%04d", count - TEST_NODE_CAPACITY - 1);
    EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0)
        << "rightmost_leaf cache incorrect after merge";

    zfree(inserted);
}

/* Range delete that leaves boundary nodes underflowed, verify merges
 * happen via fbtreeDebugValidate. */
TEST_F(FbtreeTest, NodeMergeRangeDelete) {
    const int count = TEST_NODE_CAPACITY * 4;
    for (int i = 0; i < count; i++) {
        fbtreeInsert(fbt, createBase26TestString("rng_", "", i, 5));
    }
    expectValid();

    /* Delete a range from the middle that spans multiple leaves.
     * The boundary leaves (partially deleted) may underflow and trigger merges. */
    unsigned long start = TEST_NODE_CAPACITY + 5;
    unsigned long end = TEST_NODE_CAPACITY * 3 - 5;
    unsigned long expected_deleted = end - start + 1;
    EXPECT_EQ(fbtreeDeleteRangeByRank(fbt, start, end, NULL, NULL), expected_deleted);
    EXPECT_EQ(fbtreeLength(fbt), (unsigned long)(count - expected_deleted));
    expectValid();

    /* Verify sorted iteration still works */
    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), (size_t)(count - expected_deleted));

    /* Verify backward iteration matches */
    auto backward = collectBackward();
    EXPECT_EQ(backward.size(), remaining.size());
}

/* Merge reduces root to single child, verify root collapses correctly. */
TEST_F(FbtreeTest, NodeMergeRootCollapse) {
    /* Insert just enough to create a 2-level tree (root inner + 2 leaves).
     * Then delete until one leaf is empty/merged, leaving root with 1 child.
     * Root should collapse to that single child (a leaf). */
    const int count = TEST_NODE_CAPACITY + 1;
    char buf[16];
    sds *inserted = (sds *)zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "root_%03d", i);
        inserted[i] = fbtreeInsert(fbt, createString(buf));
    }
    expectValid();

    /* Delete all but a handful of items. After merges, the root should collapse
     * from an inner node to a leaf node. */
    int keep = 5;
    for (int i = 0; i < count - keep; i++) {
        EXPECT_TRUE(fbtreeDelete(fbt, inserted[i]));
    }
    EXPECT_EQ(fbtreeLength(fbt), (size_t)keep);
    expectValid();

    /* Verify the remaining items are correct */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = count - keep; i < count; i++) {
        snprintf(buf, sizeof(buf), "root_%03d", i);
        ASSERT_TRUE(fbtreeNext(&it, &pos));
        EXPECT_EQ(memcmp(pos, buf, strlen(buf) + 1), 0);
    }
    EXPECT_FALSE(fbtreeNext(&it, &pos));

    zfree(inserted);
}

/* Pop operations that trigger leaf merges, verify tree invariants. */
TEST_F(FbtreeTest, NodeMergePopMinMax) {
    const int count = TEST_NODE_CAPACITY * 3;
    char buf[16];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "pop_%04d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    expectValid();

    /* Pop min repeatedly - this removes from the leftmost leaf, eventually
     * causing underflow and merge. */
    for (int i = 0; i < TEST_NODE_CAPACITY + TEST_MIN_FILL; i++) {
        sds popped = fbtreePopMin(fbt);
        ASSERT_NE(popped, nullptr);
        snprintf(buf, sizeof(buf), "pop_%04d", i);
        EXPECT_EQ(memcmp(popped, buf, strlen(buf) + 1), 0);
        sdsfree(popped);
    }
    expectValid();

    /* Pop max repeatedly - this removes from the rightmost leaf, eventually
     * causing underflow and merge. */
    for (int i = count - 1; i >= count - TEST_NODE_CAPACITY - TEST_MIN_FILL; i--) {
        sds popped = fbtreePopMax(fbt);
        ASSERT_NE(popped, nullptr);
        snprintf(buf, sizeof(buf), "pop_%04d", i);
        EXPECT_EQ(memcmp(popped, buf, strlen(buf) + 1), 0);
        sdsfree(popped);
    }
    expectValid();

    /* Verify remaining items via iteration */
    int expected_remaining = count - 2 * (TEST_NODE_CAPACITY + TEST_MIN_FILL);
    EXPECT_EQ(fbtreeLength(fbt), (size_t)expected_remaining);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    int iter_count = 0;
    const_sds prev_pos = nullptr;
    while (fbtreeNext(&it, &pos)) {
        if (prev_pos) {
            EXPECT_LT(sdscmp(prev_pos, pos), 0) << "Sorted order violated after pop merges";
        }
        prev_pos = pos;
        iter_count++;
    }
    EXPECT_EQ(iter_count, expected_remaining);
}

/* ========== Property-Based Tests for Node Merging ========== */

/* Helper: generate a unique key using a monotonic counter for deterministic uniqueness. */
static sds generateUniqueKey(const char *prefix, int counter, unsigned int *seed) {
    char buf[32];
    (void)seed; /* seed available for future use */
    snprintf(buf, sizeof(buf), "%s%08d", prefix, counter);
    return createString(buf);
}

/* child_num_items consistency: for any FBTree built by any sequence of inserts
 * and deletes, and for any inner node in that tree,
 * child_num_items[i] == children[i]->num_items. */
TEST_F(FbtreeTest, PropertyChildNumItemsConsistency) {
    fbtreeFree(fbt);
    fbt = nullptr;

    const int NUM_ITERATIONS = 150;
    unsigned int seed = 100;
    int key_counter = 0;

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        fbtreeIndex *tree = fbtreeCreate();

        /* Vary tree size: single-leaf, 2-level, 3-level */
        int target_size;
        if (iter < 50) {
            target_size = 1 + (rand_r(&seed) % TEST_NODE_CAPACITY);
        } else if (iter < 100) {
            target_size = TEST_NODE_CAPACITY + 1 + (rand_r(&seed) % 300);
        } else {
            target_size = 400 + (rand_r(&seed) % 1600);
        }

        /* Insert phase */
        for (int i = 0; i < target_size; i++) {
            sds s = generateUniqueKey("c1_", key_counter++, &seed);
            fbtreeInsert(tree, s);
        }

        /* Delete phase: delete a random subset using rank-based lookup */
        int num_deletes = (int)(rand_r(&seed) % (target_size + 1));
        for (int d = 0; d < num_deletes && fbtreeLength(tree) > 0; d++) {
            unsigned long len = fbtreeLength(tree);
            unsigned long rank = rand_r(&seed) % len;
            const_sds item = fbtreeGetAtRank(tree, rank);
            ASSERT_NE(item, nullptr);
            fbtreeDelete(tree, item);
        }

        /* Validate child_num_items consistency after all operations */
        if (fbtreeLength(tree) > 0) {
            ASSERT_TRUE(fbtreeDebugValidate(tree, false))
                << "Validation failed after deletes, iter=" << iter;
        }

        fbtreeFree(tree);
    }
}

/* Sorted order preserved after merges: for any FBTree and any sequence of
 * insert and delete operations, iterating forward produces non-decreasing
 * order, backward produces non-increasing order. */
TEST_F(FbtreeTest, PropertySortedOrderAfterMerges) {
    fbtreeFree(fbt);
    fbt = nullptr;

    const int NUM_ITERATIONS = 150;
    unsigned int seed = 200;
    int key_counter = 0;

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        fbtreeIndex *tree = fbtreeCreate();

        /* Vary tree size to trigger merges */
        int target_size;
        if (iter < 50) {
            target_size = 10 + (rand_r(&seed) % 60);
        } else if (iter < 100) {
            target_size = TEST_NODE_CAPACITY + 1 + (rand_r(&seed) % 300);
        } else {
            target_size = 400 + (rand_r(&seed) % 4600);
        }

        /* Insert phase */
        for (int i = 0; i < target_size; i++) {
            sds s = generateUniqueKey("c2_", key_counter++, &seed);
            fbtreeInsert(tree, s);
        }

        /* Delete enough to trigger merges using rank-based lookup */
        int num_deletes = target_size / 2 + (rand_r(&seed) % (target_size / 2 + 1));
        for (int d = 0; d < num_deletes && fbtreeLength(tree) > 0; d++) {
            unsigned long len = fbtreeLength(tree);
            unsigned long rank = rand_r(&seed) % len;
            const_sds item = fbtreeGetAtRank(tree, rank);
            if (item) fbtreeDelete(tree, item);
        }

        /* Verify forward iteration is non-decreasing */
        {
            fbtreeIterator it;
            fbtreeInitIterator(&it, tree);
            const_sds pos;
            const_sds prev = nullptr;
            int count = 0;
            while (fbtreeNext(&it, &pos)) {
                if (prev) {
                    ASSERT_LE(sdscmp(prev, pos), 0)
                        << "Forward order violated at iter=" << iter << " count=" << count;
                }
                prev = pos;
                count++;
            }
            ASSERT_EQ((size_t)count, fbtreeLength(tree))
                << "Forward count mismatch at iter=" << iter;
        }

        /* Verify backward iteration is non-increasing */
        {
            fbtreeIterator it;
            fbtreeInitIterator(&it, tree);
            const_sds pos;
            const_sds prev = nullptr;
            int count = 0;
            while (fbtreePrev(&it, &pos)) {
                if (prev) {
                    ASSERT_GE(sdscmp(prev, pos), 0)
                        << "Backward order violated at iter=" << iter << " count=" << count;
                }
                prev = pos;
                count++;
            }
            ASSERT_EQ((size_t)count, fbtreeLength(tree))
                << "Backward count mismatch at iter=" << iter;
        }

        fbtreeFree(tree);
    }
}

/* Tree invariants hold after any operation: for any FBTree and any sequence
 * of operations (single delete, pop min/max, range delete),
 * fbtreeDebugValidate returns true. */
TEST_F(FbtreeTest, PropertyTreeInvariantsAfterAnyOperation) {
    fbtreeFree(fbt);
    fbt = nullptr;

    const int NUM_ITERATIONS = 150;
    unsigned int seed = 300;
    int key_counter = 0;

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        fbtreeIndex *tree = fbtreeCreate();

        /* Vary tree size */
        int target_size;
        if (iter < 50) {
            target_size = 5 + (rand_r(&seed) % 60);
        } else if (iter < 100) {
            target_size = TEST_NODE_CAPACITY + 1 + (rand_r(&seed) % 300);
        } else {
            target_size = 400 + (rand_r(&seed) % 1600);
        }

        /* Insert phase */
        for (int i = 0; i < target_size; i++) {
            sds s = generateUniqueKey("c3_", key_counter++, &seed);
            fbtreeInsert(tree, s);
        }
        ASSERT_TRUE(fbtreeDebugValidate(tree, false))
            << "Validation failed after inserts, iter=" << iter;

        /* Mixed operation phase: use rank-based operations to avoid pointer tracking */
        int num_ops = target_size / 2 + (rand_r(&seed) % (target_size / 2 + 1));
        for (int op = 0; op < num_ops && fbtreeLength(tree) > 0; op++) {
            int op_type = rand_r(&seed) % 4;
            unsigned long len = fbtreeLength(tree);

            if (op_type == 0 && len > 0) {
                /* Delete by rank: get item at random rank, then delete it */
                unsigned long rank = rand_r(&seed) % len;
                const_sds item = fbtreeGetAtRank(tree, rank);
                if (item) fbtreeDelete(tree, item);
            } else if (op_type == 1 && len > 0) {
                /* Pop min */
                sds popped = fbtreePopMin(tree);
                if (popped) sdsfree(popped);
            } else if (op_type == 2 && len > 0) {
                /* Pop max */
                sds popped = fbtreePopMax(tree);
                if (popped) sdsfree(popped);
            } else if (len >= 2) {
                /* Range delete by rank */
                unsigned long start = rand_r(&seed) % len;
                unsigned long end = start + (rand_r(&seed) % (len - start));
                if (end >= len) end = len - 1;
                fbtreeDeleteRangeByRank(tree, start, end, NULL, NULL);
            }

            ASSERT_TRUE(fbtreeDebugValidate(tree, false))
                << "Validation failed after op " << op << " type=" << op_type
                << " iter=" << iter;
        }

        fbtreeFree(tree);
    }
}

/* Insert-then-delete-all round trip: for any set of N randomly generated
 * strings, inserting all N then deleting all N results in an empty tree
 * with zero memory leaks. */
TEST_F(FbtreeTest, PropertyInsertDeleteAllRoundTrip) {
    fbtreeFree(fbt);
    fbt = nullptr;

    const int NUM_ITERATIONS = 150;
    unsigned int seed = 400;
    int key_counter = 0;

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        size_t mem_before_iter = zmalloc_used_memory();

        int n;
        if (iter < 50) {
            n = 1 + (rand_r(&seed) % 60);
        } else if (iter < 100) {
            n = TEST_NODE_CAPACITY + 1 + (rand_r(&seed) % 300);
        } else {
            n = 400 + (rand_r(&seed) % 4600);
        }

        /* Choose deletion strategy: 0=random delete, 1=pop-min, 2=pop-max */
        int strategy = iter % 3;

        fbtreeIndex *tree = fbtreeCreate();
        std::vector<sds> inserted_items;

        for (int i = 0; i < n; i++) {
            sds s = generateUniqueKey("c4_", key_counter++, &seed);
            sds ins = fbtreeInsert(tree, s);
            inserted_items.push_back(ins);
        }

        if (strategy == 0) {
            /* Delete all in random order by pointer */
            while (!inserted_items.empty()) {
                int idx = rand_r(&seed) % inserted_items.size();
                ASSERT_TRUE(fbtreeDelete(tree, inserted_items[idx]))
                    << "Delete failed at iter=" << iter;
                inserted_items.erase(inserted_items.begin() + idx);
            }
        } else if (strategy == 1) {
            /* Pop min all */
            inserted_items.clear();
            while (fbtreeLength(tree) > 0) {
                sds popped = fbtreePopMin(tree);
                ASSERT_NE(popped, nullptr) << "PopMin returned null, iter=" << iter;
                sdsfree(popped);
            }
        } else {
            /* Pop max all */
            inserted_items.clear();
            while (fbtreeLength(tree) > 0) {
                sds popped = fbtreePopMax(tree);
                ASSERT_NE(popped, nullptr) << "PopMax returned null, iter=" << iter;
                sdsfree(popped);
            }
        }

        ASSERT_EQ(fbtreeLength(tree), 0u) << "Tree not empty after delete-all, iter=" << iter;
        fbtreeFree(tree);
        ASSERT_EQ(zmalloc_used_memory(), mem_before_iter)
            << "Memory leak detected at iter=" << iter << " strategy=" << strategy;
    }
}

/* Merge enforcement — no unnecessarily sparse nodes: for any FBTree after any
 * sequence of inserts and deletes, no non-root node has num_items < MIN_FILL
 * unless all siblings have num_items + node.num_items > NODE_SIZE. */
TEST_F(FbtreeTest, PropertyMergeEnforcement) {
    fbtreeFree(fbt);
    fbt = nullptr;

    const int NUM_ITERATIONS = 150;
    unsigned int seed = 500;
    int key_counter = 0;

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        fbtreeIndex *tree = fbtreeCreate();

        /* Vary tree size */
        int target_size;
        if (iter < 50) {
            target_size = 10 + (rand_r(&seed) % 60);
        } else if (iter < 100) {
            target_size = TEST_NODE_CAPACITY + 1 + (rand_r(&seed) % 300);
        } else {
            target_size = 400 + (rand_r(&seed) % 4600);
        }

        /* Insert phase */
        for (int i = 0; i < target_size; i++) {
            sds s = generateUniqueKey("c5_", key_counter++, &seed);
            fbtreeInsert(tree, s);
        }

        /* Delete phase: delete a random subset to trigger merges using rank-based lookup */
        int num_deletes = target_size / 2 + (rand_r(&seed) % (target_size / 2 + 1));
        for (int d = 0; d < num_deletes && fbtreeLength(tree) > 0; d++) {
            unsigned long len = fbtreeLength(tree);
            unsigned long rank = rand_r(&seed) % len;
            const_sds item = fbtreeGetAtRank(tree, rank);
            if (item) fbtreeDelete(tree, item);
        }

        ASSERT_TRUE(fbtreeDebugValidateMergeEnforcement(tree))
            << "Merge enforcement violated at iter=" << iter
            << " (sparse node exists with a sibling that has room)";

        fbtreeFree(tree);
    }
}

/* ========== Naive Oracle Helpers ========== */

/* Naive delete-by-score: iterate, collect matching items, delete one-by-one.
 * This is the original O(K log N) approach preserved as a reference oracle. */
static unsigned long naiveDeleteRangeByScore(fbtreeIndex *fbt,
                                             const char *min_score,
                                             const char *max_score,
                                             int min_ex,
                                             int max_ex) {
    if (fbtreeLength(fbt) == 0) return 0;

    /* Collect items to delete */
    std::vector<const_sds> to_delete;
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, min_score, &it);

    const_sds pos;
    while (fbtreeNext(&it, &pos)) {
        int cmp_min = memcmp(pos, min_score, 8);
        int cmp_max = memcmp(pos, max_score, 8);

        if (cmp_min < 0) continue;
        if (min_ex && cmp_min == 0) continue;
        if (cmp_max > 0) break;
        if (max_ex && cmp_max == 0) break;

        to_delete.push_back(pos);
    }

    /* Delete one by one */
    for (auto item : to_delete) {
        fbtreeDelete(fbt, item);
    }
    return to_delete.size();
}

/* Naive delete-by-value: iterate, collect matching items, delete one-by-one.
 * This is the original O(K log N) approach preserved as a reference oracle. */
static unsigned long naiveDeleteRangeByValue(fbtreeIndex *fbt,
                                             const_sds min_val,
                                             const_sds max_val,
                                             int min_ex,
                                             int max_ex) {
    if (fbtreeLength(fbt) == 0) return 0;

    /* Collect items to delete */
    std::vector<const_sds> to_delete;
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToValue(fbt, min_val, &it);

    const_sds pos;
    while (fbtreeNext(&it, &pos)) {
        int cmp_min = sdscmp(pos, min_val);
        int cmp_max = sdscmp(pos, max_val);

        if (cmp_min < 0) continue;
        if (min_ex && cmp_min == 0) continue;
        if (cmp_max > 0) break;
        if (max_ex && cmp_max == 0) break;

        to_delete.push_back(pos);
    }

    /* Delete one by one */
    for (auto item : to_delete) {
        fbtreeDelete(fbt, item);
    }
    return to_delete.size();
}

/* Helper: collect all elements from a tree into a vector of strings */
static std::vector<std::string> collectAllElements(fbtreeIndex *fbt) {
    std::vector<std::string> result;
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    while (fbtreeNext(&it, &pos)) {
        result.emplace_back(pos, sdslen(pos));
    }
    return result;
}

/* Helper: build a tree from a vector of sds strings (makes copies) */
static fbtreeIndex *buildTreeFromElements(const std::vector<sds> &elements) {
    fbtreeIndex *tree = fbtreeCreate();
    for (auto &elem : elements) {
        fbtreeInsert(tree, sdsdup(elem));
    }
    return tree;
}

/* ========== Range Delete Equivalence Tests ========== */

/* For any FBTree with N elements and for any valid start_rank and end_rank,
 * fbtreeDeleteRangeByRank SHALL return end_rank - start_rank + 1, the tree length
 * SHALL decrease by that amount, and the remaining elements SHALL be exactly those
 * that were not at ranks in [start_rank, end_rank]. */
TEST_F(FbtreeTest, DeleteRangeByRankPropertyCorrectness) {
    fbtreeFree(fbt);
    fbt = nullptr;

    const int NUM_ITERATIONS = 150;
    unsigned int seed = 42;
    int key_counter = 0;

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        /* Generate random tree size: small, medium, large */
        int N;
        if (iter < 30) {
            N = 1 + (rand_r(&seed) % 10);
        } else if (iter < 80) {
            N = 10 + (rand_r(&seed) % 500);
        } else {
            N = 500 + (rand_r(&seed) % 4500);
        }

        fbtreeIndex *tree = fbtreeCreate();
        for (int i = 0; i < N; i++) {
            sds s = generateUniqueKey("rp_", key_counter++, &seed);
            fbtreeInsert(tree, s);
        }

        /* Record all elements before deletion */
        std::vector<std::string> before = collectAllElements(tree);
        ASSERT_EQ(before.size(), (size_t)N) << "iter=" << iter;

        /* Generate random rank range */
        unsigned long start_rank = rand_r(&seed) % N;
        unsigned long end_rank = start_rank + (rand_r(&seed) % (N - start_rank));
        unsigned long expected_deleted = end_rank - start_rank + 1;

        /* Execute range delete */
        unsigned long deleted = fbtreeDeleteRangeByRank(tree, start_rank, end_rank, NULL, NULL);

        /* Verify return count */
        EXPECT_EQ(deleted, expected_deleted)
            << "iter=" << iter << " start=" << start_rank << " end=" << end_rank;

        /* Verify tree length */
        EXPECT_EQ(fbtreeLength(tree), (unsigned long)(N - expected_deleted))
            << "iter=" << iter;

        /* Verify tree validation passes */
        EXPECT_TRUE(fbtreeDebugValidate(tree, false))
            << "Validation failed at iter=" << iter;

        /* Verify merge enforcement */
        EXPECT_TRUE(fbtreeDebugValidateMergeEnforcement(tree))
            << "Merge enforcement violated at iter=" << iter;

        /* Verify sorted order */
        std::vector<std::string> after = collectAllElements(tree);
        EXPECT_EQ(after.size(), (size_t)(N - expected_deleted)) << "iter=" << iter;
        for (size_t i = 1; i < after.size(); i++) {
            EXPECT_LT(after[i - 1], after[i])
                << "Sorted order violated at iter=" << iter << " pos=" << i;
        }

        /* Verify correct elements removed */
        std::vector<std::string> expected_remaining;
        for (size_t i = 0; i < before.size(); i++) {
            if (i < start_rank || i > end_rank) {
                expected_remaining.push_back(before[i]);
            }
        }
        EXPECT_EQ(after, expected_remaining)
            << "Wrong elements remaining at iter=" << iter;

        fbtreeFree(tree);
    }
}

/* For any FBTree with N score-prefixed elements and for any min_score, max_score,
 * min_ex, max_ex parameters, the optimized fbtreeDeleteRangeByScore SHALL delete
 * exactly the same set of elements and return the same count as the naive
 * iterate-and-delete implementation. */
TEST_F(FbtreeTest, DeleteRangeByScorePropertyEquivalence) {
    fbtreeFree(fbt);
    fbt = nullptr;

    const int NUM_ITERATIONS = 150;
    unsigned int seed = 123;

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        /* Generate random tree size */
        int N;
        if (iter < 30) {
            N = 1 + (rand_r(&seed) % 10);
        } else if (iter < 80) {
            N = 10 + (rand_r(&seed) % 500);
        } else {
            N = 500 + (rand_r(&seed) % 4500);
        }

        /* Generate random score-prefixed elements */
        std::vector<sds> elements;
        for (int i = 0; i < N; i++) {
            /* Generate random 8-byte score prefix */
            char score[8];
            for (int b = 0; b < 8; b++) {
                score[b] = (char)(rand_r(&seed) % 256);
            }
            /* Append a unique suffix */
            char suffix[16];
            snprintf(suffix, sizeof(suffix), "e%06d", i);
            size_t suffix_len = strlen(suffix) + 1;
            sds s = sdsnewlen(NULL, 8 + suffix_len);
            memcpy(s, score, 8);
            memcpy(s + 8, suffix, suffix_len);
            elements.push_back(s);
        }

        /* Build two identical trees */
        fbtreeIndex *tree_optimized = buildTreeFromElements(elements);
        fbtreeIndex *tree_naive = buildTreeFromElements(elements);

        /* Generate random score range */
        char min_score[8], max_score[8];
        for (int b = 0; b < 8; b++) {
            min_score[b] = (char)(rand_r(&seed) % 256);
            max_score[b] = (char)(rand_r(&seed) % 256);
        }
        /* Ensure min <= max */
        if (memcmp(min_score, max_score, 8) > 0) {
            char tmp[8];
            memcpy(tmp, min_score, 8);
            memcpy(min_score, max_score, 8);
            memcpy(max_score, tmp, 8);
        }

        int min_ex = rand_r(&seed) % 2;
        int max_ex = rand_r(&seed) % 2;

        /* Apply optimized range delete to one tree */
        unsigned long deleted_optimized = fbtreeDeleteRangeByScore(
            tree_optimized, min_score, max_score, min_ex, max_ex, NULL, NULL);

        /* Apply naive to other tree */
        unsigned long deleted_naive = naiveDeleteRangeByScore(
            tree_naive, min_score, max_score, min_ex, max_ex);

        /* Compare return counts */
        EXPECT_EQ(deleted_optimized, deleted_naive)
            << "Delete count mismatch at iter=" << iter
            << " min_ex=" << min_ex << " max_ex=" << max_ex;

        /* Compare remaining elements */
        std::vector<std::string> remaining_optimized = collectAllElements(tree_optimized);
        std::vector<std::string> remaining_naive = collectAllElements(tree_naive);
        EXPECT_EQ(remaining_optimized, remaining_naive)
            << "Remaining elements mismatch at iter=" << iter;

        /* Validate optimized tree */
        EXPECT_TRUE(fbtreeDebugValidate(tree_optimized, false))
            << "Validation failed at iter=" << iter;

        /* Verify merge enforcement */
        EXPECT_TRUE(fbtreeDebugValidateMergeEnforcement(tree_optimized))
            << "Merge enforcement violated at iter=" << iter;

        /* Cleanup */
        fbtreeFree(tree_optimized);
        fbtreeFree(tree_naive);
        for (auto &s : elements) sdsfree(s);
    }
}

/* For any FBTree with N elements and for any min_val, max_val, min_ex, max_ex
 * parameters, the optimized fbtreeDeleteRangeByValue SHALL delete exactly the same
 * set of elements and return the same count as the naive iterate-and-delete
 * implementation. */
TEST_F(FbtreeTest, DeleteRangeByValuePropertyEquivalence) {
    fbtreeFree(fbt);
    fbt = nullptr;

    const int NUM_ITERATIONS = 150;
    unsigned int seed = 456;
    int key_counter = 0;

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        /* Generate random tree size */
        int N;
        if (iter < 30) {
            N = 1 + (rand_r(&seed) % 10);
        } else if (iter < 80) {
            N = 10 + (rand_r(&seed) % 500);
        } else {
            N = 500 + (rand_r(&seed) % 4500);
        }

        /* Generate random elements */
        std::vector<sds> elements;
        for (int i = 0; i < N; i++) {
            char buf[24];
            snprintf(buf, sizeof(buf), "vp_%08d", key_counter++);
            elements.push_back(createString(buf));
        }

        /* Build two identical trees */
        fbtreeIndex *tree_optimized = buildTreeFromElements(elements);
        fbtreeIndex *tree_naive = buildTreeFromElements(elements);

        /* Generate random value range by picking two elements or nearby values */
        int idx_a = rand_r(&seed) % N;
        int idx_b = rand_r(&seed) % N;

        /* Sort the elements to pick range bounds from sorted order */
        std::vector<std::string> sorted_elems = collectAllElements(tree_optimized);

        size_t lo, hi;
        if ((size_t)idx_a < (size_t)idx_b) {
            lo = (size_t)idx_a % sorted_elems.size();
            hi = (size_t)idx_b % sorted_elems.size();
        } else {
            lo = (size_t)idx_b % sorted_elems.size();
            hi = (size_t)idx_a % sorted_elems.size();
        }

        sds min_val = sdsnewlen(sorted_elems[lo].data(), sorted_elems[lo].size());
        sds max_val = sdsnewlen(sorted_elems[hi].data(), sorted_elems[hi].size());

        int min_ex = rand_r(&seed) % 2;
        int max_ex = rand_r(&seed) % 2;

        /* Apply optimized range delete to one tree */
        unsigned long deleted_optimized = fbtreeDeleteRangeByValue(
            tree_optimized, min_val, max_val, min_ex, max_ex, NULL, NULL);

        /* Apply naive to other tree */
        unsigned long deleted_naive = naiveDeleteRangeByValue(
            tree_naive, min_val, max_val, min_ex, max_ex);

        /* Compare return counts */
        EXPECT_EQ(deleted_optimized, deleted_naive)
            << "Delete count mismatch at iter=" << iter
            << " min_ex=" << min_ex << " max_ex=" << max_ex;

        /* Compare remaining elements */
        std::vector<std::string> remaining_optimized = collectAllElements(tree_optimized);
        std::vector<std::string> remaining_naive = collectAllElements(tree_naive);
        EXPECT_EQ(remaining_optimized, remaining_naive)
            << "Remaining elements mismatch at iter=" << iter;

        /* Validate optimized tree */
        EXPECT_TRUE(fbtreeDebugValidate(tree_optimized, false))
            << "Validation failed at iter=" << iter;

        /* Verify merge enforcement */
        EXPECT_TRUE(fbtreeDebugValidateMergeEnforcement(tree_optimized))
            << "Merge enforcement violated at iter=" << iter;

        /* Cleanup */
        fbtreeFree(tree_optimized);
        fbtreeFree(tree_naive);
        sdsfree(min_val);
        sdsfree(max_val);
        for (auto &s : elements) sdsfree(s);
    }
}

/* ========== Callback Tests ========== */

/* Context for tracking deleted items in the callback test */
struct CallbackTracker {
    std::vector<std::string> deleted_items;
};

static void trackDeletedItem(sds item, void *ctx) {
    CallbackTracker *tracker = (CallbackTracker *)ctx;
    tracker->deleted_items.emplace_back(item, sdslen(item));
}

/* Verify that the item deletion callback is invoked for every deleted item
 * with the correct sds pointer. */
TEST_F(FbtreeTest, DeleteRangeCallbackInvocation) {
    const int N = 200;
    char buf[24];

    /* Insert N elements */
    for (int i = 0; i < N; i++) {
        snprintf(buf, sizeof(buf), "cb_%06d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    expectValid();

    /* Record elements that should be deleted (ranks 50..149) */
    std::vector<std::string> expected_deleted;
    for (int i = 50; i <= 149; i++) {
        const_sds at_rank = fbtreeGetAtRank(fbt, i);
        ASSERT_NE(at_rank, nullptr);
        expected_deleted.emplace_back(at_rank, sdslen(at_rank));
    }

    /* Delete with callback */
    CallbackTracker tracker;
    unsigned long deleted = fbtreeDeleteRangeByRank(
        fbt, 50, 149, trackDeletedItem, &tracker);

    EXPECT_EQ(deleted, 100u);
    EXPECT_EQ(fbtreeLength(fbt), 100u);
    expectValid();

    /* Verify callback was invoked for every deleted item */
    ASSERT_EQ(tracker.deleted_items.size(), expected_deleted.size());

    /* Sort both vectors since callback order may differ from rank order
     * (e.g., subtree freeing order) */
    std::vector<std::string> sorted_tracked = tracker.deleted_items;
    std::vector<std::string> sorted_expected = expected_deleted;
    std::sort(sorted_tracked.begin(), sorted_tracked.end());
    std::sort(sorted_expected.begin(), sorted_expected.end());
    EXPECT_EQ(sorted_tracked, sorted_expected);
}

/* Callback test with delete-all: verify callback fires for every element */
TEST_F(FbtreeTest, DeleteRangeCallbackDeleteAll) {
    const int N = 50;
    char buf[24];

    std::vector<std::string> all_elements;
    for (int i = 0; i < N; i++) {
        snprintf(buf, sizeof(buf), "cba_%04d", i);
        sds inserted = fbtreeInsert(fbt, createString(buf));
        all_elements.emplace_back(inserted, sdslen(inserted));
    }
    expectValid();

    CallbackTracker tracker;
    unsigned long deleted = fbtreeDeleteRangeByRank(
        fbt, 0, N - 1, trackDeletedItem, &tracker);

    EXPECT_EQ(deleted, (unsigned long)N);
    EXPECT_EQ(fbtreeLength(fbt), 0u);

    /* Verify all items were reported */
    ASSERT_EQ(tracker.deleted_items.size(), (size_t)N);
    std::vector<std::string> sorted_tracked = tracker.deleted_items;
    std::vector<std::string> sorted_expected = all_elements;
    std::sort(sorted_tracked.begin(), sorted_tracked.end());
    std::sort(sorted_expected.begin(), sorted_expected.end());
    EXPECT_EQ(sorted_tracked, sorted_expected);
}

/* Callback test with NULL callback: verify normal behavior (no crash) */
TEST_F(FbtreeTest, DeleteRangeCallbackNull) {
    for (int i = 0; i < 20; i++) {
        fbtreeInsert(fbt, createBase26TestString("", "", i, 3));
    }
    expectValid();

    unsigned long deleted = fbtreeDeleteRangeByRank(fbt, 5, 14, NULL, NULL);
    EXPECT_EQ(deleted, 10u);
    EXPECT_EQ(fbtreeLength(fbt), 10u);
    expectValid();
}

/* ========== Prefix Monotonicity / validated_len Tests ========== */

/* A parent's prefix can exceed a child's prefix even with pure inserts.
 * When a few early elements have a shorter common prefix with the rest of
 * the tree, the leftmost child spans a wider range than the parent's anchors
 * suggest. Example: insert one "59..." element then many "60..." elements.
 * The root's anchors are all "60..." (prefix="60"), but child[0] contains
 * the "59..." element (prefix=""). Lookups for "59..." must still work. */
TEST_F(FbtreeTest, LookupWithParentPrefixExceedingChildPrefix) {
    /* Insert a small number of "59" items, then fill the tree with "60" items.
     * This creates a root whose anchors all share "60" as a prefix, but the
     * leftmost child contains "59" items with a shorter common prefix. */
    char buf[24];

    /* Insert a few items with prefix "59" */
    for (int i = 0; i < 5; i++) {
        snprintf(buf, sizeof(buf), "59_%06d", i);
        fbtreeInsert(fbt, createString(buf));
    }

    /* Insert many items with prefix "60" to force splits and create a
     * multi-level tree where most anchors start with "60" */
    for (int i = 0; i < TEST_NODE_CAPACITY * 4; i++) {
        snprintf(buf, sizeof(buf), "60_%06d", i);
        fbtreeInsert(fbt, createString(buf));
    }
    expectValid();

    /* Verify all "59" items are findable via GetRankOfItem (uses validated_len) */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "59_", 3), 0);

    /* Seek to a "59" value — must route to the correct child despite
     * the parent's prefix being "60" (longer than child's prefix) */
    sds seek_val = createString("59_000000");
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToValue(fbt, seek_val, &it);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "59_000000", 10), 0);
    sdsfree(seek_val);

    /* Seek to something between "59" and "60" */
    seek_val = createString("59_zzzzzz");
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToValue(fbt, seek_val, &it);
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "60_", 3), 0);
    sdsfree(seek_val);

    /* Verify full iteration order */
    fbtreeInitIterator(&it, fbt);
    const_sds prev = nullptr;
    int count = 0;
    while (fbtreeNext(&it, &pos)) {
        if (prev) {
            EXPECT_LT(sdscmp(prev, pos), 0) << "Order violated at " << count;
        }
        prev = pos;
        count++;
    }
    EXPECT_EQ((size_t)count, fbtreeLength(fbt));
}

/* After bulk deletion, a parent's prefix can exceed a child's prefix.
 * This test constructs that scenario and verifies lookups still work:
 * - Build a tree where all anchors share a long prefix (e.g., "prefix_06...")
 * - But the leftmost child contains items with a shorter common prefix
 *   (e.g., "prefix_05..." through "prefix_06...")
 * - After deleting middle children, the parent's prefix grows
 * - Lookups for keys below the prefix (e.g., "prefix_05...") must still
 *   route to the correct leftmost child via the prefix < comparison */
TEST_F(FbtreeTest, LookupAfterPrefixGrowthFromBulkDelete) {
    /* Build a tree with items spanning prefix_050000 through prefix_069999.
     * The root's children will have anchors like prefix_05XXXX and prefix_06XXXX,
     * giving the root a short prefix ("prefix_0"). After deleting the middle
     * range, the remaining children may all start with "prefix_06", growing
     * the root's prefix to "prefix_06" while the leftmost child still has
     * items starting with "prefix_05". */
    const int N = TEST_NODE_CAPACITY * 6;
    char buf[24];
    sds *inserted = (sds *)zmalloc(N * sizeof(sds));

    for (int i = 0; i < N; i++) {
        snprintf(buf, sizeof(buf), "prefix_%06d", 50000 + i);
        inserted[i] = fbtreeInsert(fbt, createString(buf));
    }
    expectValid();

    /* Delete a range from the middle that removes all "prefix_05XXXX" items
     * except those in the leftmost child. This should cause the parent's
     * prefix to grow past the leftmost child's prefix. */
    unsigned long start = TEST_NODE_CAPACITY;
    unsigned long end = N / 2;
    fbtreeDeleteRangeByRank(fbt, start, end, NULL, NULL);
    expectValid();

    /* Now look up items that are still in the tree — especially the early
     * ones whose prefix diverges from the parent's grown prefix */
    for (unsigned long i = 0; i < start; i++) {
        long rank = fbtreeGetRankOfItem(fbt, inserted[i]);
        EXPECT_GE(rank, 0) << "Failed to find item at original index " << i;
    }

    /* Look up via SeekToValue for a key below the parent's prefix */
    sds seek_val = createString("prefix_050000");
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToValue(fbt, seek_val, &it);
    const_sds pos;
    ASSERT_TRUE(fbtreeNext(&it, &pos));
    EXPECT_EQ(memcmp(pos, "prefix_050000", 14), 0);
    sdsfree(seek_val);

    /* Verify forward iteration produces all remaining elements in order */
    fbtreeInitIterator(&it, fbt);
    const_sds prev = nullptr;
    int count = 0;
    while (fbtreeNext(&it, &pos)) {
        if (prev) {
            EXPECT_LT(sdscmp(prev, pos), 0) << "Sorted order violated at position " << count;
        }
        prev = pos;
        count++;
    }
    EXPECT_EQ((size_t)count, fbtreeLength(fbt));

    zfree(inserted);
}

/* After any range delete, all remaining items must be findable
 * via fbtreeGetRankOfItem. This catches lookup routing bugs. */
TEST_F(FbtreeTest, PropertyAllItemsFindableAfterRangeDelete) {
    fbtreeFree(fbt);
    fbt = nullptr;

    const int NUM_ITERATIONS = 100;
    unsigned int seed = 9999;
    int key_counter = 0;

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        int N;
        if (iter < 30) {
            N = 10 + (rand_r(&seed) % 50);
        } else if (iter < 70) {
            N = TEST_NODE_CAPACITY + 1 + (rand_r(&seed) % 300);
        } else {
            N = 500 + (rand_r(&seed) % 4500);
        }

        fbtreeIndex *tree = fbtreeCreate();
        std::vector<sds> items;
        items.reserve(N);

        for (int i = 0; i < N; i++) {
            char buf[24];
            snprintf(buf, sizeof(buf), "fi_%08d", key_counter++);
            sds ins = fbtreeInsert(tree, createString(buf));
            items.push_back(ins);
        }

        /* Delete a random range */
        unsigned long start_rank = rand_r(&seed) % N;
        unsigned long end_rank = start_rank + (rand_r(&seed) % (N - start_rank));
        fbtreeDeleteRangeByRank(tree, start_rank, end_rank, NULL, NULL);

        /* Every surviving item must be findable */
        unsigned long remaining = fbtreeLength(tree);
        for (unsigned long r = 0; r < remaining; r++) {
            const_sds at_rank = fbtreeGetAtRank(tree, r);
            ASSERT_NE(at_rank, nullptr) << "iter=" << iter << " rank=" << r;
            long found_rank = fbtreeGetRankOfItem(tree, at_rank);
            ASSERT_EQ(found_rank, (long)r)
                << "iter=" << iter << " rank=" << r
                << " item found at wrong rank " << found_rank;
        }

        fbtreeFree(tree);
    }
}

/* SeekToValue must find the correct element after range
 * deletion creates parent->prefix_len > child->prefix_len scenarios. */
TEST_F(FbtreeTest, PropertySeekCorrectAfterRangeDelete) {
    fbtreeFree(fbt);
    fbt = nullptr;

    const int NUM_ITERATIONS = 100;
    unsigned int seed = 7777;
    int key_counter = 0;

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        int N;
        if (iter < 30) {
            N = 10 + (rand_r(&seed) % 50);
        } else if (iter < 70) {
            N = TEST_NODE_CAPACITY + 1 + (rand_r(&seed) % 300);
        } else {
            N = 500 + (rand_r(&seed) % 4500);
        }

        fbtreeIndex *tree = fbtreeCreate();
        for (int i = 0; i < N; i++) {
            char buf[24];
            snprintf(buf, sizeof(buf), "sk_%08d", key_counter++);
            fbtreeInsert(tree, createString(buf));
        }

        /* Delete a random range */
        unsigned long start_rank = rand_r(&seed) % N;
        unsigned long end_rank = start_rank + (rand_r(&seed) % (N - start_rank));
        fbtreeDeleteRangeByRank(tree, start_rank, end_rank, NULL, NULL);

        /* Collect remaining elements */
        std::vector<std::string> remaining;
        {
            fbtreeIterator it;
            fbtreeInitIterator(&it, tree);
            const_sds pos;
            while (fbtreeNext(&it, &pos)) {
                remaining.emplace_back(pos, sdslen(pos));
            }
        }

        if (remaining.empty()) {
            fbtreeFree(tree);
            continue;
        }

        /* Seek to each remaining element and verify correct positioning */
        for (size_t i = 0; i < remaining.size(); i += (remaining.size() / 20 > 0 ? remaining.size() / 20 : 1)) {
            sds seek_val = sdsnewlen(remaining[i].data(), remaining[i].size());
            fbtreeIterator it;
            fbtreeInitIterator(&it, tree);
            fbtreeSeekToValue(tree, seek_val, &it);
            const_sds pos;
            ASSERT_TRUE(fbtreeNext(&it, &pos))
                << "iter=" << iter << " seek failed for element " << i;
            ASSERT_EQ(sdscmp(pos, seek_val), 0)
                << "iter=" << iter << " seek returned wrong element at " << i;
            sdsfree(seek_val);
        }

        /* Also seek for a value before the first element */
        sds before = createString("A_before_everything");
        {
            fbtreeIterator it;
            fbtreeInitIterator(&it, tree);
            fbtreeSeekToValue(tree, before, &it);
            const_sds pos;
            ASSERT_TRUE(fbtreeNext(&it, &pos))
                << "iter=" << iter << " seek before first element failed";
            /* Should return the first remaining element */
            ASSERT_EQ(std::string(pos, sdslen(pos)), remaining[0])
                << "iter=" << iter << " seek before first returned wrong element";
        }
        sdsfree(before);

        fbtreeFree(tree);
    }
}

/* ========== Edge Range Deletion Tests (start/end of tree, multilevel) ========== */

/* Delete a large range from the start of a multilevel tree. Ensures inner
 * nodes are removed, leftmost_leaf cache is updated, and merge enforcement
 * holds after deleting from the left edge. */
TEST_F(FbtreeTest, DeleteRangeByRankFromStartMultilevel) {
    const int N = TEST_NODE_CAPACITY * 4;
    for (int i = 0; i < N; i++) {
        fbtreeInsert(fbt, createBase26TestString("edge_", "", i, 5));
    }
    expectValid();

    /* Delete the first ~2 inner nodes worth of elements */
    unsigned long delete_end = TEST_NODE_CAPACITY * 2;
    unsigned long deleted = fbtreeDeleteRangeByRank(fbt, 0, delete_end - 1, NULL, NULL);
    EXPECT_EQ(deleted, delete_end);
    EXPECT_EQ(fbtreeLength(fbt), (unsigned long)(N - delete_end));
    expectValid();
    EXPECT_TRUE(fbtreeDebugValidateMergeEnforcement(fbt));

    /* Verify first remaining element */
    const_sds first = fbtreeGetAtRank(fbt, 0);
    ASSERT_NE(first, nullptr);
    sds expected_first = createBase26TestString("edge_", "", delete_end, 5);
    EXPECT_EQ(sdscmp(first, expected_first), 0);
    sdsfree(expected_first);

    /* Verify iteration */
    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), (size_t)(N - delete_end));
}

/* Delete a large range from the end of a multilevel tree. */
TEST_F(FbtreeTest, DeleteRangeByRankFromEndMultilevel) {
    const int N = TEST_NODE_CAPACITY * 4;
    for (int i = 0; i < N; i++) {
        fbtreeInsert(fbt, createBase26TestString("edge_", "", i, 5));
    }
    expectValid();

    unsigned long delete_start = N - TEST_NODE_CAPACITY * 2;
    unsigned long deleted = fbtreeDeleteRangeByRank(fbt, delete_start, N - 1, NULL, NULL);
    EXPECT_EQ(deleted, (unsigned long)(N - delete_start));
    EXPECT_EQ(fbtreeLength(fbt), delete_start);
    expectValid();
    EXPECT_TRUE(fbtreeDebugValidateMergeEnforcement(fbt));

    /* Verify last remaining element */
    const_sds last = fbtreeGetAtRank(fbt, delete_start - 1);
    ASSERT_NE(last, nullptr);
    sds expected_last = createBase26TestString("edge_", "", delete_start - 1, 5);
    EXPECT_EQ(sdscmp(last, expected_last), 0);
    sdsfree(expected_last);

    auto remaining = collectForward();
    EXPECT_EQ(remaining.size(), (size_t)delete_start);
}

/* Delete from start of a 3+ level tree. */
TEST_F(FbtreeTest, DeleteRangeByRankFromStartDeepTree) {
    const int N = TEST_THREE_LEVEL_ITEMS;
    for (int i = 0; i < N; i++) {
        fbtreeInsert(fbt, createBase26TestString("deep_", "", i, 6));
    }
    expectValid();

    unsigned long delete_end = N / 3;
    unsigned long deleted = fbtreeDeleteRangeByRank(fbt, 0, delete_end - 1, NULL, NULL);
    EXPECT_EQ(deleted, delete_end);
    EXPECT_EQ(fbtreeLength(fbt), (unsigned long)(N - delete_end));
    expectValid();
    EXPECT_TRUE(fbtreeDebugValidateMergeEnforcement(fbt));
}

/* Delete from end of a 3+ level tree. */
TEST_F(FbtreeTest, DeleteRangeByRankFromEndDeepTree) {
    const int N = TEST_THREE_LEVEL_ITEMS;
    for (int i = 0; i < N; i++) {
        fbtreeInsert(fbt, createBase26TestString("deep_", "", i, 6));
    }
    expectValid();

    unsigned long delete_start = N - N / 3;
    unsigned long deleted = fbtreeDeleteRangeByRank(fbt, delete_start, N - 1, NULL, NULL);
    EXPECT_EQ(deleted, (unsigned long)(N - delete_start));
    EXPECT_EQ(fbtreeLength(fbt), delete_start);
    expectValid();
    EXPECT_TRUE(fbtreeDebugValidateMergeEnforcement(fbt));
}

/* Delete from start by score on a multilevel tree. */
TEST_F(FbtreeTest, DeleteRangeByScoreFromStartMultilevel) {
    const int N = TEST_NODE_CAPACITY * 4;
    for (int i = 0; i < N; i++) {
        char score[9];
        snprintf(score, sizeof(score), "%08d", i);
        sds s = sdsnewlen(NULL, 8 + 7);
        memcpy(s, score, 8);
        memcpy(s + 8, "elem", 5);
        s[8 + 6] = '\0';
        fbtreeInsert(fbt, s);
    }
    expectValid();

    /* Delete elements with score < midpoint */
    int mid = N / 2;
    char min_score[9], max_score[9];
    snprintf(min_score, sizeof(min_score), "%08d", 0);
    snprintf(max_score, sizeof(max_score), "%08d", mid - 1);

    unsigned long deleted = fbtreeDeleteRangeByScore(fbt, min_score, max_score, 0, 0, NULL, NULL);
    EXPECT_EQ(deleted, (unsigned long)mid);
    EXPECT_EQ(fbtreeLength(fbt), (unsigned long)(N - mid));
    expectValid();
    EXPECT_TRUE(fbtreeDebugValidateMergeEnforcement(fbt));
}

/* Delete from end by score on a multilevel tree. */
TEST_F(FbtreeTest, DeleteRangeByScoreFromEndMultilevel) {
    const int N = TEST_NODE_CAPACITY * 4;
    for (int i = 0; i < N; i++) {
        char score[9];
        snprintf(score, sizeof(score), "%08d", i);
        sds s = sdsnewlen(NULL, 8 + 7);
        memcpy(s, score, 8);
        memcpy(s + 8, "elem", 5);
        s[8 + 6] = '\0';
        fbtreeInsert(fbt, s);
    }
    expectValid();

    int mid = N / 2;
    char min_score[9], max_score[9];
    snprintf(min_score, sizeof(min_score), "%08d", mid);
    snprintf(max_score, sizeof(max_score), "%08d", N - 1);

    unsigned long deleted = fbtreeDeleteRangeByScore(fbt, min_score, max_score, 0, 0, NULL, NULL);
    EXPECT_EQ(deleted, (unsigned long)(N - mid));
    EXPECT_EQ(fbtreeLength(fbt), (unsigned long)mid);
    expectValid();
    EXPECT_TRUE(fbtreeDebugValidateMergeEnforcement(fbt));
}

/* Delete from start by value on a multilevel tree. */
TEST_F(FbtreeTest, DeleteRangeByValueFromStartMultilevel) {
    const int N = TEST_NODE_CAPACITY * 4;
    for (int i = 0; i < N; i++) {
        fbtreeInsert(fbt, createBase26TestString("val_", "", i, 5));
    }
    expectValid();

    int mid = N / 2;
    sds min_val = createBase26TestString("val_", "", 0, 5);
    sds max_val = createBase26TestString("val_", "", mid - 1, 5);

    unsigned long deleted = fbtreeDeleteRangeByValue(fbt, min_val, max_val, 0, 0, NULL, NULL);
    EXPECT_EQ(deleted, (unsigned long)mid);
    EXPECT_EQ(fbtreeLength(fbt), (unsigned long)(N - mid));
    sdsfree(min_val);
    sdsfree(max_val);
    expectValid();
    EXPECT_TRUE(fbtreeDebugValidateMergeEnforcement(fbt));
}

/* Delete from end by value on a multilevel tree. */
TEST_F(FbtreeTest, DeleteRangeByValueFromEndMultilevel) {
    const int N = TEST_NODE_CAPACITY * 4;
    for (int i = 0; i < N; i++) {
        fbtreeInsert(fbt, createBase26TestString("val_", "", i, 5));
    }
    expectValid();

    int mid = N / 2;
    sds min_val = createBase26TestString("val_", "", mid, 5);
    sds max_val = createBase26TestString("val_", "", N - 1, 5);

    unsigned long deleted = fbtreeDeleteRangeByValue(fbt, min_val, max_val, 0, 0, NULL, NULL);
    EXPECT_EQ(deleted, (unsigned long)(N - mid));
    EXPECT_EQ(fbtreeLength(fbt), (unsigned long)mid);
    sdsfree(min_val);
    sdsfree(max_val);
    expectValid();
    EXPECT_TRUE(fbtreeDebugValidateMergeEnforcement(fbt));
}

/* ========== Massive Range Deletion (collapse to single leaf) ========== */

/* Delete all but the first few elements from a 3+ level tree.
 * The entire right side of the tree is freed and the root collapses
 * down to a single leaf. */
TEST_F(FbtreeTest, DeleteMostOfDeepTreeKeepFirst) {
    const int N = TEST_THREE_LEVEL_ITEMS;
    const int KEEP = 10;
    for (int i = 0; i < N; i++) {
        fbtreeInsert(fbt, createBase26TestString("big_", "", i, 6));
    }
    expectValid();

    unsigned long deleted = fbtreeDeleteRangeByRank(fbt, KEEP, N - 1, NULL, NULL);
    EXPECT_EQ(deleted, (unsigned long)(N - KEEP));
    EXPECT_EQ(fbtreeLength(fbt), (unsigned long)KEEP);
    expectValid();
    EXPECT_TRUE(fbtreeDebugValidateMergeEnforcement(fbt));

    /* Verify the surviving elements are the first KEEP */
    for (int i = 0; i < KEEP; i++) {
        sds expected = createBase26TestString("big_", "", i, 6);
        const_sds actual = fbtreeGetAtRank(fbt, i);
        ASSERT_NE(actual, nullptr);
        EXPECT_EQ(sdscmp(actual, expected), 0);
        sdsfree(expected);
    }

    /* Verify iteration works in both directions */
    auto fwd = collectForward();
    EXPECT_EQ(fwd.size(), (size_t)KEEP);
    auto bwd = collectBackward();
    EXPECT_EQ(bwd.size(), (size_t)KEEP);
}

/* Delete all but the last few elements from a 3+ level tree. */
TEST_F(FbtreeTest, DeleteMostOfDeepTreeKeepLast) {
    const int N = TEST_THREE_LEVEL_ITEMS;
    const int KEEP = 10;
    for (int i = 0; i < N; i++) {
        fbtreeInsert(fbt, createBase26TestString("big_", "", i, 6));
    }
    expectValid();

    unsigned long deleted = fbtreeDeleteRangeByRank(fbt, 0, N - KEEP - 1, NULL, NULL);
    EXPECT_EQ(deleted, (unsigned long)(N - KEEP));
    EXPECT_EQ(fbtreeLength(fbt), (unsigned long)KEEP);
    expectValid();
    EXPECT_TRUE(fbtreeDebugValidateMergeEnforcement(fbt));

    for (int i = 0; i < KEEP; i++) {
        sds expected = createBase26TestString("big_", "", N - KEEP + i, 6);
        const_sds actual = fbtreeGetAtRank(fbt, i);
        ASSERT_NE(actual, nullptr);
        EXPECT_EQ(sdscmp(actual, expected), 0);
        sdsfree(expected);
    }

    auto fwd = collectForward();
    EXPECT_EQ(fwd.size(), (size_t)KEEP);
    auto bwd = collectBackward();
    EXPECT_EQ(bwd.size(), (size_t)KEEP);
}

/* Delete the middle of a 3+ level tree, leaving a few elements on each
 * end. The surviving boundary leaves are underflowed and should merge,
 * collapsing the tree down to a single leaf via root collapse. */
TEST_F(FbtreeTest, DeleteMostOfDeepTreeKeepEnds) {
    const int N = TEST_THREE_LEVEL_ITEMS;
    const int KEEP_EACH_SIDE = 5; /* < MIN_FILL, forces merge */
    for (int i = 0; i < N; i++) {
        fbtreeInsert(fbt, createBase26TestString("big_", "", i, 6));
    }
    expectValid();

    unsigned long deleted = fbtreeDeleteRangeByRank(
        fbt, KEEP_EACH_SIDE, N - KEEP_EACH_SIDE - 1, NULL, NULL);
    unsigned long expected_remaining = KEEP_EACH_SIDE * 2;
    EXPECT_EQ(deleted, (unsigned long)(N - expected_remaining));
    EXPECT_EQ(fbtreeLength(fbt), expected_remaining);
    expectValid();
    EXPECT_TRUE(fbtreeDebugValidateMergeEnforcement(fbt));

    /* Verify first and last surviving elements */
    sds expected_first = createBase26TestString("big_", "", 0, 6);
    sds expected_last = createBase26TestString("big_", "", N - 1, 6);
    const_sds actual_first = fbtreeGetAtRank(fbt, 0);
    const_sds actual_last = fbtreeGetAtRank(fbt, expected_remaining - 1);
    ASSERT_NE(actual_first, nullptr);
    ASSERT_NE(actual_last, nullptr);
    EXPECT_EQ(sdscmp(actual_first, expected_first), 0);
    EXPECT_EQ(sdscmp(actual_last, expected_last), 0);
    sdsfree(expected_first);
    sdsfree(expected_last);

    auto fwd = collectForward();
    EXPECT_EQ(fwd.size(), (size_t)expected_remaining);
    auto bwd = collectBackward();
    EXPECT_EQ(bwd.size(), (size_t)expected_remaining);
}
