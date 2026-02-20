#include "../fbtree_ordered_index.h"
#include "../zmalloc.h"
#include "../sds.h"
#include "test_help.h"

#include <stdio.h>
#include <string.h>

/* ========== Test Helpers ========== */

/* Node capacity - must match NODE_SIZE in fbtree_ordered_index.c */
#define TEST_NODE_CAPACITY 61
#define TEST_TWO_LEVEL_ITEMS (TEST_NODE_CAPACITY * TEST_NODE_CAPACITY)
#define TEST_THREE_LEVEL_ITEMS (TEST_TWO_LEVEL_ITEMS + 200)

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

int test_fbtree_create_and_free(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();

    /* Create empty tree */
    fbtreeIndex *fbt = fbtreeCreate();
    TEST_ASSERT(fbt != NULL);
    TEST_ASSERT(is_tree_valid(fbt));

    /* Free and verify no memory leak */
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_insert_and_lookup(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert single item */
    sds str = createString("hello");
    sds inserted = fbtreeInsert(fbt, str);
    TEST_ASSERT(is_tree_valid(fbt));

    /* Lookup existing item via returned pointer */
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted) >= 0);

    /* Lookup non-existent item returns -1 */
    sds search_string2 = createString("world");
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, search_string2) < 0);
    sdsfree(search_string2);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_insert_multiple(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);

    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert multiple strings and store pointers */
    const char *strings[] = {"apple", "banana", "cherry", "date", "elderberry", "elder"};
    int count = sizeof(strings) / sizeof(strings[0]);
    sds inserted[6];

    for (int i = 0; i < count; i++) {
        sds str = createString(strings[i]);
        inserted[i] = fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Verify all can be found using stored pointers */
    for (int i = 0; i < count; i++) {
        TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) >= 0);
    }

    /* Verify non-existent string not found */
    sds search_str = createString("fig");
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, search_str) < 0);
    sdsfree(search_str);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_lookup_empty_tree(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    TEST_ASSERT(is_tree_valid(fbt));

    /* Lookup in empty tree returns -1 */
    sds search_str = createString("anything");
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, search_str) < 0);
    sdsfree(search_str);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_length_increments(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Empty tree */
    TEST_ASSERT(fbtreeLength(fbt) == 0);

    /* Length increments with each insert */
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
    return 0;
}

/* ========== Forward Iterator Tests ========== */

int test_fbtree_iterator_small(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert out of order */
    const char *strings[] = {"dog", "cat", "ant", "bat"};
    for (int i = 0; i < 4; i++) {
        sds str = createString(strings[i]);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Iterate forward - should be sorted */
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
    return 0;
}

int test_fbtree_iterator_full_leaf(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
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
    return 0;
}

int test_fbtree_iterator_reverse_insert(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
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
    return 0;
}

int test_fbtree_iterator_empty(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
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
    return 0;
}

int test_fbtree_iterator_reset_invalidates(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    const char *strings[] = {"a", "b", "c"};
    for (int i = 0; i < 3; i++) {
        sds str = createString(strings[i]);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Advance iterator partway */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(fbtreeNext(&it, &pos));

    /* Reset invalidates iterator - next call returns false */
    fbtreeResetIterator(&it);
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* ========== Duplicate and Edge Case Tests ========== */

int test_fbtree_duplicate_insert(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert same value twice */
    sds str1 = createString("key");
    sds inserted1 = fbtreeInsert(fbt, str1);
    TEST_ASSERT(fbtreeLength(fbt) == 1);

    /* Duplicates are allowed - both stored separately */
    sds str2 = createString("key");
    sds inserted2 = fbtreeInsert(fbt, str2);
    TEST_ASSERT(fbtreeLength(fbt) == 2);
    TEST_ASSERT(is_tree_valid(fbt));

    /* Both can be found via their pointers */
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted1) >= 0);
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted2) >= 0);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_empty_string(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Empty string is valid */
    sds str = createString("");
    sds inserted = fbtreeInsert(fbt, str);
    TEST_ASSERT(is_tree_valid(fbt));

    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted) >= 0);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* ========== Ordering Tests ========== */

int test_fbtree_multilevel_reverse_insert(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert 96 items in reverse order */
    char buf[8];
    for (int i = 95; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        sds str = createString(buf);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Forward iteration is sorted ascending */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    for (int i = 0; i <= 95; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, 5) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    /* Backward iteration is sorted descending */
    fbtreeInitIterator(&it, fbt);
    for (int i = 95; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        TEST_ASSERT(fbtreePrev(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, 5) == 0);
    }
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_prefix_ordering(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Strings where one is a prefix of another */
    const char *strings[] = {"elderberry", "elder", "e", "elderly"};
    for (int i = 0; i < 4; i++) {
        sds str = createString(strings[i]);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Shorter prefixes sort before longer strings */
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
    return 0;
}

int test_fbtree_long_strings(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* 255-byte string */
    char long_str[256];
    memset(long_str, 'a', 255);
    long_str[255] = '\0';

    sds str = createString(long_str);
    sds inserted = fbtreeInsert(fbt, str);
    TEST_ASSERT(is_tree_valid(fbt));

    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted) >= 0);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_same_length_ordering(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Same-length strings sort lexicographically */
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
    return 0;
}

int test_fbtree_common_prefix_ordering(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Keys with identical prefix, differ only in suffix */
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

    /* All keys findable */
    for (int i = 0; i < 12; i++) {
        TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) >= 0);
    }

    /* Iteration is sorted */
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
    return 0;
}

int test_fbtree_insert_batches_sorted(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert first batch */
    const char *batch1[] = {"dog", "cat", "ant"};
    for (int i = 0; i < 3; i++) {
        sds str = createString(batch1[i]);
        fbtreeInsert(fbt, str);
    }

    /* Verify sorted after first batch */
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

    /* Verify all items merged correctly */
    fbtreeInitIterator(&it, fbt);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "ant", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "bat", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "cat", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "dog", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "elk", 4) == 0);
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_insert_at_boundaries(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert middle item */
    sds str = createString("m");
    fbtreeInsert(fbt, str);

    /* Insert at beginning, end, and adjacent to existing */
    const char *inserts[] = {"a", "z", "n"};
    for (int i = 0; i < 3; i++) {
        str = createString(inserts[i]);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* All positions handled correctly */
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
    return 0;
}

/* ========== Backward Iterator (Prev) Tests ========== */

int test_fbtree_prev_small(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
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
    return 0;
}

int test_fbtree_prev_full_leaf(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* NODE_CAPACITY+1 items exceeds single leaf */
    const int count = TEST_NODE_CAPACITY + 1;
    char buf[8];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        sds str = createString(buf);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Backward iteration in descending order */
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
    return 0;
}

int test_fbtree_prev_empty(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    TEST_ASSERT(is_tree_valid(fbt));

    /* Prev on empty tree returns false */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_prev_next_mixed(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
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
    return 0;
}

int test_fbtree_prev_single(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    sds str = createString("only");
    fbtreeInsert(fbt, str);
    TEST_ASSERT(is_tree_valid(fbt));

    /* Single item tree */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos, "only", 5) == 0);
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_iterator_exhausted_stays_invalid(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
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
    return 0;
}

/* ========== Multi-Level Tree Tests ========== */

int test_fbtree_multilevel_lookup(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* NODE_CAPACITY+1 items forces 2-level tree */
    const int count = TEST_NODE_CAPACITY + 1;
    char buf[8];
    sds *inserted = zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        sds str = createString(buf);
        inserted[i] = fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* All items findable via stored pointers */
    for (int i = 0; i < count; i++) {
        TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) >= 0);
    }
    zfree(inserted);

    /* Non-existent keys return -1 */
    sds search_str1 = createString("k99");
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, search_str1) < 0);
    sdsfree(search_str1);
    sds search_str2 = createString("x00");
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, search_str2) < 0);
    sdsfree(search_str2);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_multilevel_forward_iteration(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
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
    return 0;
}

int test_fbtree_multilevel_backward_iteration(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
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
    return 0;
}

int test_fbtree_multilevel_mixed_iteration(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
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
    return 0;
}

int test_fbtree_multilevel_cross_leaf_iteration(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
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
    return 0;
}

/* ========== Inner Node Split Tests (3+ Level Trees) ========== */

int test_fbtree_inner_split_sequential(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert enough items to force inner node splits and create 3+ level tree */
    /* Sequential pattern stresses append pattern insertion shortcut and asymetric node splits */
    const int count = TEST_THREE_LEVEL_ITEMS;
    sds *inserted = zmalloc(count * sizeof(sds));
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
    return 0;
}

int test_fbtree_inner_split_reverse(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
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
    return 0;
}

int test_fbtree_inner_split_shuffled(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Shuffle indices for random insertion */
    /* Random insertion stresses normal mid-collection insertion path and symetric 50/50 node splits */
    int *indices = zmalloc(5000 * sizeof(int));
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
    return 0;
}

/* ========== Deep Tree Tests (4+ Levels) ========== */

int test_fbtree_deep_tree_4_levels(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
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
    return 0;
}
int test_fbtree_deep_tree_mixed_insert_patterns(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
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
    int *indices = zmalloc(10000 * sizeof(int));
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
    return 0;
}

/* ========== String Pattern Tests ========== */

int test_fbtree_varied_string_patterns(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    char buf[32];
    sds *inserted = zmalloc(4000 * sizeof(sds));
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

    /* All patterns findable */
    for (int i = 0; i < 4000; i++) {
        TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) >= 0);
    }
    zfree(inserted);

    /* Iteration maintains sort order */
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
    return 0;
}

/* ========== Split Boundary Tests ========== */

int test_fbtree_split_at_exact_boundary(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
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
    return 0;
}

int test_fbtree_alternating_min_max_insert(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Alternating min/max insertion stresses split boundaries */
    const int count = TEST_TWO_LEVEL_ITEMS;
    char buf[16];
    int min_val = 0, max_val = count - 1;
    sds *inserted = zmalloc(count * sizeof(sds));

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
    return 0;
}

/* Sequential insertion into middle of tree can still trigger optimized append/prepend paths
 * when the insertion point becomes the new rightmost/leftmost position in a subtree */
int test_fbtree_sequential_middle_insert(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
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
    return 0;
}

/* ========== Delete Tests ========== */

int test_fbtree_delete_single_item(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    sds str = createString("test");
    sds inserted = fbtreeInsert(fbt, str);
    TEST_ASSERT(fbtreeLength(fbt) == 1);

    /* Delete the only item */
    TEST_ASSERT(fbtreeDelete(fbt, inserted));
    TEST_ASSERT(fbtreeLength(fbt) == 0);
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_delete_nonexistent(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    sds str = createString("exists");
    sds inserted = fbtreeInsert(fbt, str);

    /* Different pointer - delete fails */
    sds other = createString("missing");
    TEST_ASSERT(!fbtreeDelete(fbt, other));
    sdsfree(other);

    /* Original still exists */
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted) >= 0);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_delete_from_empty(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Delete on empty tree fails */
    sds dummy = createString("anything");
    TEST_ASSERT(!fbtreeDelete(fbt, dummy));
    sdsfree(dummy);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_delete_all_items(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
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

    /* Delete all items one by one */
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
    return 0;
}

int test_fbtree_delete_middle_item(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
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

    /* Delete middle item */
    TEST_ASSERT(fbtreeDelete(fbt, inserted[25]));
    TEST_ASSERT(fbtreeLength(fbt) == 49);

    /* Neighbors still exist */
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[24]) >= 0);
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[26]) >= 0);

    /* Iteration count correct */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    int count = 0;
    while (fbtreeNext(&it, &pos)) count++;
    TEST_ASSERT(count == 49);
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_delete_max_updates_anchor(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Multi-level tree */
    char buf[16];
    sds inserted[200];
    for (int i = 0; i < 200; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        sds str = createString(buf);
        inserted[i] = fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(fbtreeLength(fbt) == 200);
    TEST_ASSERT(is_tree_valid(fbt));

    /* Delete max item - triggers anchor bubble-up */
    TEST_ASSERT(fbtreeDelete(fbt, inserted[199]));
    TEST_ASSERT(fbtreeLength(fbt) == 199);
    TEST_ASSERT(is_tree_valid(fbt));

    /* New max still exists */
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[198]) >= 0);

    /* Backward iteration works */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    int count = 0;
    while (fbtreePrev(&it, &pos)) count++;
    TEST_ASSERT(count == 199);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_delete_all_multilevel(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Create multi-level tree */
    char buf[16];
    const int count = TEST_NODE_CAPACITY * 3;
    sds *inserted = zmalloc(count * sizeof(sds));
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
    return 0;
}

int test_fbtree_delete_root_collapse(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Create 2-level tree: root inner node with 2 leaf children */
    char buf[16];
    const int overflow = 10;
    const int count = TEST_NODE_CAPACITY + overflow;
    sds *inserted = zmalloc(count * sizeof(sds));
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
    return 0;
}

int test_fbtree_delete_leftmost_leaf_updates_cache(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Create multi-level tree */
    char buf[16];
    const int count = TEST_NODE_CAPACITY * 3;
    sds *inserted = zmalloc(count * sizeof(sds));
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
    return 0;
}

int test_fbtree_delete_rightmost_leaf_updates_cache(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Create multi-level tree */
    char buf[16];
    const int count = TEST_NODE_CAPACITY * 3;
    sds *inserted = zmalloc(count * sizeof(sds));
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
    return 0;
}

/* ========== Rank Tests ========== */

int test_fbtree_rank_single_leaf(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert out of order */
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
    return 0;
}

int test_fbtree_rank_multilevel(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    char buf[16];
    for (int i = 0; i < 200; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        sds str = createString(buf);
        fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Rank access at various positions */
    const_sds result;

    result = fbtreeGetAtRank(fbt, 0);
    TEST_ASSERT(result && memcmp(result, "key_000", 8) == 0);

    result = fbtreeGetAtRank(fbt, 50);
    TEST_ASSERT(result && memcmp(result, "key_050", 8) == 0);

    result = fbtreeGetAtRank(fbt, 100);
    TEST_ASSERT(result && memcmp(result, "key_100", 8) == 0);

    result = fbtreeGetAtRank(fbt, 199);
    TEST_ASSERT(result && memcmp(result, "key_199", 8) == 0);

    /* Out of bounds */
    result = fbtreeGetAtRank(fbt, 200);
    TEST_ASSERT(result == NULL);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_rank_after_delete(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    char buf[16];
    sds inserted[100];
    for (int i = 0; i < 100; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        sds str = createString(buf);
        inserted[i] = fbtreeInsert(fbt, str);
    }

    /* Delete key_050 */
    TEST_ASSERT(fbtreeDelete(fbt, inserted[50]));

    /* Rank 50 now returns key_051 */
    const_sds result = fbtreeGetAtRank(fbt, 50);
    TEST_ASSERT(result && memcmp(result, "key_051", 8) == 0);

    /* Rank 49 still returns key_049 */
    result = fbtreeGetAtRank(fbt, 49);
    TEST_ASSERT(result && memcmp(result, "key_049", 8) == 0);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_get_rank_of_item(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    char buf[16];
    sds inserted[100];
    for (int i = 0; i < 100; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        sds str = createString(buf);
        inserted[i] = fbtreeInsert(fbt, str);
    }

    /* Rank lookup via stored pointers */
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[0]) == 0);
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[50]) == 50);
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[99]) == 99);

    /* Non-existent returns -1 */
    sds search = createString("key_100");
    TEST_ASSERT(fbtreeGetRankOfItem(fbt, search) == -1);
    sdsfree(search);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_seek_to_rank(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
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
    return 0;
}

int test_fbtree_rank_deep_tree(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Create deep tree with enough items for 3+ levels */
    const int count = TEST_THREE_LEVEL_ITEMS;
    char buf[16];
    sds *inserted = zmalloc(count * sizeof(sds));
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "key_%05d", i);
        sds str = createString(buf);
        inserted[i] = fbtreeInsert(fbt, str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Test boundary ranks */
    const_sds result;

    result = fbtreeGetAtRank(fbt, 0);
    TEST_ASSERT(result && memcmp(result, "key_00000", 10) == 0);

    result = fbtreeGetAtRank(fbt, count - 1);
    snprintf(buf, sizeof(buf), "key_%05d", count - 1);
    TEST_ASSERT(result && memcmp(result, buf, 10) == 0);

    /* Sample ranks across entire tree */
    for (int i = 0; i < count; i += count / 10) {
        result = fbtreeGetAtRank(fbt, i);
        TEST_ASSERT(result == inserted[i]); /* Exact pointer match */
    }

    /* Sample GetRankOfItem across tree depth */
    for (int i = 0; i < count; i += count / 10) {
        TEST_ASSERT(fbtreeGetRankOfItem(fbt, inserted[i]) == i);
    }
    zfree(inserted);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* ========== fbtreeSeekToScore Tests ========== 
 * These tests use string literals where the first 8 bytes are the "score" prefix.
 * Lexicographic ordering: "AAAAAAAA" < "BBBBBBBB" < "CCCCCCCC" */

int test_fbtree_seek_to_score_exact(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert 3 items with distinct 8-byte prefixes */
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
    return 0;
}

int test_fbtree_seek_to_score_between(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert items with prefixes "AAAA" and "CCCC" (no "BBBB") */
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
    return 0;
}

int test_fbtree_seek_to_score_past_end(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
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
    return 0;
}

int test_fbtree_seek_to_score_past_end_then_prev(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
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
    return 0;
}

int test_fbtree_seek_to_score_before_start_then_next(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
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
    return 0;
}

int test_fbtree_seek_to_score_before_start(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
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
    return 0;
}

int test_fbtree_seek_to_score_empty(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    fbtreeSeekToScore(fbt, "AAAAAAAA", &it);

    const_sds pos;
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_seek_to_score_deep_tree(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
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
    return 0;
}

int test_fbtree_seek_to_score_iterate(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert 5 items with same score prefix, different elements */
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

    /* Count items with matching score prefix */
    const_sds pos;
    int count = 0;
    while (fbtreeNext(&it, &pos) && memcmp(pos, "BBBBBBBB", 8) == 0) {
        count++;
    }
    TEST_ASSERT(count == 5);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}
