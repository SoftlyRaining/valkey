#include "../fbtree_ordered_index.h"
#include "../zmalloc.h"
#include "../static_string.h"
#include "test_help.h"

#include <stdio.h>
#include <string.h>

/* Helper to create a static_string */
static static_string createString(const char *str) {
    size_t len = strlen(str) + 1;
    return ssnewlen(str, len);
}

static static_string createBase26TestString(const char *prefix, const char *suffix, size_t value, size_t value_width) {
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);
    size_t len = prefix_len + suffix_len + value_width + 1;
    static_string s = ssnewlen(NULL, len);
    memcpy(s, prefix, prefix_len);

    /* Create a base-26 number using letters */
    for (size_t i = 0; i < value_width; i++) {
        char c = (char)('A' + (value % 26));
        s[prefix_len + value_width - 1 - i] = c;
        value /= 26;
    }

    memcpy(s + prefix_len + value_width, suffix, suffix_len);
    s[len - 1] = '\0';
    return s;
}

bool is_tree_valid(fbtreeIndex *fbt) {
    return fbtreeDebugValidate(fbt, false); // TODO temp change - it's impossibly verbose when all tests fail.
    // if (!fbtreeDebugValidate(fbt, false))
    //     return fbtreeDebugValidate(fbt, true);
    // return true;
}

int test_fbtree_create_and_free(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    TEST_ASSERT(fbt != NULL);
    TEST_ASSERT(is_tree_valid(fbt));
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
    
    /* Insert a single string */
    static_string str = createString("hello");
    fbtreeInsert(fbt, str);
    ssfree(str);
    TEST_ASSERT(is_tree_valid(fbt));
    
    /* Lookup existing string */
    static_string search_string = createString("hello");
    TEST_ASSERT(fbtreeLookup(fbt, search_string));
    ssfree(search_string);
    
    /* Lookup non-existent string */
    static_string search_string2 = createString("world");
    TEST_ASSERT(fbtreeLookup(fbt, search_string2) == false);
    ssfree(search_string2);
    
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
    
    /* Insert multiple strings */
    const char *strings[] = {"apple", "banana", "cherry", "date", "elderberry", "elder"};
    int count = sizeof(strings) / sizeof(strings[0]);
    
    for (int i = 0; i < count; i++) {
        static_string str = createString(strings[i]);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));
    
    /* Verify all can be found */
    for (int i = 0; i < count; i++) {
        static_string search_str = createString(strings[i]);
        TEST_ASSERT(fbtreeLookup(fbt, search_str));
        ssfree(search_str);
    }
    
    /* Verify non-existent string not found */
    static_string search_str = createString("fig");
    TEST_ASSERT(fbtreeLookup(fbt, search_str) == false);
    ssfree(search_str);
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_empty_lookup(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    TEST_ASSERT(is_tree_valid(fbt));
    
    /* Lookup in empty tree */
    static_string search_str = createString("anything");
    TEST_ASSERT(fbtreeLookup(fbt, search_str) == false);
    ssfree(search_str);
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_length(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    /* Empty tree has length 0 */
    TEST_ASSERT(fbtreeLength(fbt) == 0);
    
    /* Insert and check length */
    static_string str1 = createString("first");
    fbtreeInsert(fbt, str1);
    ssfree(str1);
    TEST_ASSERT(fbtreeLength(fbt) == 1);
    
    static_string str2 = createString("second");
    fbtreeInsert(fbt, str2);
    ssfree(str2);
    TEST_ASSERT(fbtreeLength(fbt) == 2);
    
    static_string str3 = createString("third");
    fbtreeInsert(fbt, str3);
    ssfree(str3);
    TEST_ASSERT(fbtreeLength(fbt) == 3);
    TEST_ASSERT(is_tree_valid(fbt));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_iterator_small(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    const char *strings[] = {"dog", "cat", "ant", "bat"};
    for (int i = 0; i < 4; i++) {
        static_string str = createString(strings[i]);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));
    
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "ant", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "bat", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "cat", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "dog", 4) == 0);
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_iterator_max(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    char buf[8];
    for (int i = 0; i < 65; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;

    for (int i = 0; i < 65; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(sslen(pos) == 4);
        TEST_ASSERT(memcmp(pos, buf, 4) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_iterator_reverse_insert(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    char buf[8];
    for (int i = 64; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;

    for (int i = 0; i < 65; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(sslen(pos) == 4);
        TEST_ASSERT(memcmp(pos, buf, 4) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_iterator_empty(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    TEST_ASSERT(is_tree_valid(fbt));
    
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_iterator_reset(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    const char *strings[] = {"a", "b", "c"};
    for (int i = 0; i < 3; i++) {
        static_string str = createString(strings[i]);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));
    
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(fbtreeNext(&it, &pos));
    
    fbtreeResetIterator(&it);
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_duplicate_insert(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    static_string str1 = createString("key");
    fbtreeInsert(fbt, str1);
    ssfree(str1);
    TEST_ASSERT(fbtreeLength(fbt) == 1);
    
    static_string str2 = createString("key");
    fbtreeInsert(fbt, str2);
    ssfree(str2);
    TEST_ASSERT(fbtreeLength(fbt) == 2);
    TEST_ASSERT(is_tree_valid(fbt));
    
    static_string search_str = createString("key");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    ssfree(search_str);
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_empty_string(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    static_string str = createString("");
    fbtreeInsert(fbt, str);
    ssfree(str);
    TEST_ASSERT(is_tree_valid(fbt));
    
    static_string search_str = createString("");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    ssfree(search_str);
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_multilevel_reverse_insert(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert 100 items in reverse order */
    char buf[8];
    
    for (int i = 95; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Verify forward iteration is sorted */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    for (int i = 0; i <= 95; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, 5) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    /* Verify backward iteration */
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
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    const char *strings[] = {"elderberry", "elder", "e", "elderly"};
    for (int i = 0; i < 4; i++) {
        static_string str = createString(strings[i]);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));
    
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    
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
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    char long_str[256];
    memset(long_str, 'a', 255);
    long_str[255] = '\0';
    
    static_string str = createString(long_str);
    fbtreeInsert(fbt, str);
    ssfree(str);
    TEST_ASSERT(is_tree_valid(fbt));
    
    static_string search_str = createString(long_str);
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    ssfree(search_str);
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_same_length_ordering(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    const char *strings[] = {"zoo", "abc", "xyz", "def"};
    for (int i = 0; i < 4; i++) {
        static_string str = createString(strings[i]);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));
    
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "abc", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "def", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "xyz", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "zoo", 4) == 0);
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_ordered_insert_after_sort(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    /* Insert unordered */
    const char *batch1[] = {"dog", "cat", "ant"};
    for (int i = 0; i < 3; i++) {
        static_string str = createString(batch1[i]);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    
    /* Trigger sort via iteration */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    fbtreeNext(&it, &pos);
    
    /* Insert more (now ordered) */
    const char *batch2[] = {"bat", "elk"};
    for (int i = 0; i < 2; i++) {
        static_string str = createString(batch2[i]);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));
    
    /* Final iteration - verify all sorted */
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

int test_fbtree_ordered_insert_boundaries(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    /* Insert and sort */
    static_string str = createString("m");
    fbtreeInsert(fbt, str);
    ssfree(str);
    
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    fbtreeNext(&it, &pos);
    
    /* Insert at beginning, middle, end */
    const char *inserts[] = {"a", "z", "n"};
    for (int i = 0; i < 3; i++) {
        str = createString(inserts[i]);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));
    
    fbtreeInitIterator(&it, fbt);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "a", 2) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "m", 2) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "n", 2) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "z", 2) == 0);
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_prev_small(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    const char *strings[] = {"dog", "cat", "ant", "bat"};
    for (int i = 0; i < 4; i++) {
        static_string str = createString(strings[i]);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;

    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos, "dog", 4) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos, "cat", 4) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos, "bat", 4) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos, "ant", 4) == 0);
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_prev_max(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    char buf[8];
    for (int i = 0; i < 65; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;

    for (int i = 64; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        TEST_ASSERT(fbtreePrev(&it, &pos));
        TEST_ASSERT(sslen(pos) == 4);
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

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
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
        static_string str = createString(strings[i]);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;

    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "a", 2) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "b", 2) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos, "b", 2) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos, "a", 2) == 0);
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

    static_string str = createString("only");
    fbtreeInsert(fbt, str);
    ssfree(str);
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;

    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos, "only", 5) == 0);
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_iterator_stays_invalid(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    static_string str = createString("x");
    fbtreeInsert(fbt, str);
    ssfree(str);
    TEST_ASSERT(is_tree_valid(fbt));

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;

    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    TEST_ASSERT(!fbtreePrev(&it, &pos));
    TEST_ASSERT(!fbtreePrev(&it, &pos));
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    fbtreeInitIterator(&it, fbt);
    TEST_ASSERT(fbtreePrev(&it, &pos));
    TEST_ASSERT(!fbtreePrev(&it, &pos));
    TEST_ASSERT(!fbtreePrev(&it, &pos));
    TEST_ASSERT(!fbtreePrev(&it, &pos));
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeInitIterator(&it, fbt);
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(fbtreePrev(&it, &pos));
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    TEST_ASSERT(!fbtreePrev(&it, &pos));

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

int test_fbtree_multilevel_lookup(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert 65 items to create 2-level tree */
    char buf[8];
    for (int i = 0; i < 65; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Lookup all items in multi-level tree */
    for (int i = 0; i < 65; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        static_string search_str = createString(buf);
        TEST_ASSERT(fbtreeLookup(fbt, search_str));
        ssfree(search_str);
    }

    /* Lookup non-existent keys */
    static_string search_str1 = createString("k99");
    TEST_ASSERT(fbtreeLookup(fbt, search_str1) == false);
    ssfree(search_str1);
    static_string search_str2 = createString("x00");
    TEST_ASSERT(fbtreeLookup(fbt, search_str2) == false);
    ssfree(search_str2);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_multilevel_forward_iteration(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert 130 items to create multi-level tree with multiple leaves */
    char buf[8];
    for (int i = 0; i < 130; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Forward iteration through all items */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    for (int i = 0; i < 130; i++) {
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
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert 130 items to create multi-level tree */
    char buf[8];
    for (int i = 0; i < 130; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Backward iteration through all items */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    for (int i = 129; i >= 0; i--) {
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
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert 100 items */
    char buf[8];
    for (int i = 0; i < 100; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    TEST_ASSERT(fbtreeLength(fbt) == 100);

    /* Mixed forward/backward iteration */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;

    /* Forward 10: 0,1,2,3,4,5,6,7,8,9 */
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
    }
    TEST_ASSERT(sslen(pos) == 5);
    TEST_ASSERT(memcmp(pos, "k009", 5) == 0);

    /* Backward 5: 9,8,7,6,5 */
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT(fbtreePrev(&it, &pos));
    }
    TEST_ASSERT(sslen(pos) == 5);
    TEST_ASSERT(memcmp(pos, "k005", 5) == 0);

    /* Forward to end: 5,6,7...98,99 */
    int count = 1;
    while (fbtreeNext(&it, &pos)) count++;
    TEST_ASSERT(count == 96); /* 5 to 99 inclusive is 96 items */
    TEST_ASSERT(sslen(pos) == 5);
    TEST_ASSERT(memcmp(pos, "k099", 5) == 0);

    fbtreeResetIterator(&it);
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_multilevel_random_insert(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert in random order */
    int order[] = {50, 25, 75, 10, 40, 60, 90, 5, 15, 30, 45, 55, 65, 80, 95,
                   0, 20, 35, 70, 85, 100, 110, 120, 66, 67, 68, 69, 71, 72, 73};
    char buf[8];
    for (int i = 0; i < 30; i++) {
        snprintf(buf, sizeof(buf), "k%03d", order[i]);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Verify sorted iteration */
    int expected[] = {0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 66,
                      67, 68, 69, 70, 71, 72, 73, 75, 80, 85, 90, 95, 100, 110, 120};
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    for (int i = 0; i < 30; i++) {
        snprintf(buf, sizeof(buf), "k%03d", expected[i]);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, 5) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_multilevel_feature_collision(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert keys with common prefixes to test feature collision handling */
    const char *keys[] = {
        "prefix_aaa", "prefix_aab", "prefix_aac", "prefix_aad",
        "prefix_baa", "prefix_bab", "prefix_bac", "prefix_bad",
        "prefix_caa", "prefix_cab", "prefix_cac", "prefix_cad"
    };
    for (int i = 0; i < 12; i++) {
        static_string str = createString(keys[i]);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Lookup all keys */
    for (int i = 0; i < 12; i++) {
        static_string search_str = createString(keys[i]);
        TEST_ASSERT(fbtreeLookup(fbt, search_str));
        ssfree(search_str);
    }

    /* Verify sorted iteration */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
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

int test_fbtree_multilevel_across_leaves(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert 200 items to ensure multiple leaves */
    char buf[8];
    for (int i = 0; i < 200; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    /* Test iteration crossing leaf boundaries */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;

    /* Skip to near first leaf boundary (around item 32) */
    for (int i = 0; i < 30; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
    }

    /* Verify items around boundary */
    for (int i = 30; i < 40; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, 5) == 0);
    }

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* Tests for inner node splits and 3+ level B+ trees */

/* Test that forces inner node split by creating 3-level tree */
int test_fbtree_inner_node_split_sequential(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert 4200 items to force inner node splits and create 3+ level tree
     * With 64 items per node: 64*64 = 4096 items fills 2 levels
     * 4200 items will force creation of 3rd level and inner node splits */
    for (int i = 0; i < 4200; i++) {
        static_string str = createBase26TestString("key_", "", i, 3);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    TEST_ASSERT(fbtreeLength(fbt) == 4200);

    /* Verify all items can be found */
    for (int i = 0; i < 4200; i++) {
        static_string search_str = createBase26TestString("key_", "", i, 3);
        TEST_ASSERT(fbtreeLookup(fbt, search_str));
        ssfree(search_str);
    }

    /* Verify forward iteration maintains order */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    for (int i = 0; i < 4200; i++) {
        static_string expected = createBase26TestString("key_", "", i, 3);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(sslen(pos) == sslen(expected));
        TEST_ASSERT(memcmp(pos, expected, sslen(expected)) == 0);
        ssfree(expected);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* Test inner node split with reverse insertion order */
int test_fbtree_inner_node_split_reverse(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert 4200 items in reverse order to stress inner node splits */
    char buf[16];
    for (int i = 4199; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "key_%05d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    TEST_ASSERT(fbtreeLength(fbt) == 4200);

    /* Verify forward iteration is still sorted */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    for (int i = 0; i < 4200; i++) {
        snprintf(buf, sizeof(buf), "key_%05d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    /* Verify backward iteration */
    fbtreeInitIterator(&it, fbt);
    for (int i = 4199; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "key_%05d", i);
        TEST_ASSERT(fbtreePrev(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);
    }
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* Test inner node split with random insertion pattern */
int test_fbtree_inner_node_split_random(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Create array of indices for random insertion */
    int *indices = zmalloc(5000 * sizeof(int));
    for (int i = 0; i < 5000; i++) {
        indices[i] = i;
    }

    /* Simple shuffle algorithm */
    for (int i = 4999; i > 0; i--) {
        int j = i % 1000; /* Simple pseudo-random */
        int temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
    }

    /* Insert in shuffled order */
    char buf[16];
    for (int i = 0; i < 5000; i++) {
        snprintf(buf, sizeof(buf), "rnd_%05d", indices[i]);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    TEST_ASSERT(fbtreeLength(fbt) == 5000);

    /* Verify all items exist and iteration is sorted */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
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

/* Test deep tree with 4+ levels */
int test_fbtree_deep_tree_levels(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert enough items to create 4+ level tree
     * 64^3 = 262,144 items should create 4 levels */
    char buf[16];
    for (int i = 0; i < 300000; i++) {
        snprintf(buf, sizeof(buf), "deep_%06d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    TEST_ASSERT(fbtreeLength(fbt) == 300000);

    /* Test lookups at various points */
    static_string search_str;
    
    /* First item */
    search_str = createString("deep_000000");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    ssfree(search_str);
    
    /* Middle item */
    search_str = createString("deep_150000");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    ssfree(search_str);
    
    /* Last item */
    search_str = createString("deep_299999");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    ssfree(search_str);
    
    /* Non-existent items */
    search_str = createString("deep_300000");
    TEST_ASSERT(fbtreeLookup(fbt, search_str) == false);
    ssfree(search_str);

    /* Test iteration across deep tree boundaries */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    
    /* Skip to middle and verify ordering around deep boundaries */
    for (int i = 0; i < 150000; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
    }
    
    /* Verify next 100 items are in order */
    for (int i = 150000; i < 150100; i++) {
        snprintf(buf, sizeof(buf), "deep_%06d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);
    }

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}
/* Test mixed operations on deep tree */
int test_fbtree_deep_tree_mixed_operations(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Build deep tree with mixed insertion patterns */
    char buf[16];
    
    /* Insert first batch sequentially */
    for (int i = 0; i < 10000; i++) {
        snprintf(buf, sizeof(buf), "mix_%06d", i * 3);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    
    /* Insert second batch in reverse */
    for (int i = 9999; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "mix_%06d", i * 3 + 1);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    
    /* Insert third batch randomly */
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
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    zfree(indices);
    TEST_ASSERT(is_tree_valid(fbt));

    TEST_ASSERT(fbtreeLength(fbt) == 30000);

    /* Verify complete sorted iteration */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
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
    
    /* Backward 500 */
    for (int i = 0; i < 500; i++) {
        TEST_ASSERT(fbtreePrev(&it, &pos));
    }
    
    /* Should be at position 500 */
    snprintf(buf, sizeof(buf), "mix_%06d", 500);
    TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* Test inner node splits with string patterns that stress feature extraction */
int test_fbtree_inner_node_split_string_patterns(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert strings with patterns that may cause feature collisions */
    char buf[32];
    
    /* Pattern 1: Common prefixes with varying suffixes */
    for (int i = 0; i < 1000; i++) {
        snprintf(buf, sizeof(buf), "common_prefix_%04d_suffix", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    
    /* Pattern 2: Varying prefixes with common suffixes */
    for (int i = 0; i < 1000; i++) {
        snprintf(buf, sizeof(buf), "prefix_%04d_common_suffix", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    
    /* Pattern 3: Palindromic patterns */
    for (int i = 0; i < 1000; i++) {
        snprintf(buf, sizeof(buf), "pal_%04d_%04d_lap", i, i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    
    /* Pattern 4: Repeated character patterns */
    for (int i = 0; i < 1000; i++) {
        char c = 'a' + (i % 26);
        snprintf(buf, sizeof(buf), "repeat_%c%c%c%c_%04d", c, c, c, c, i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    TEST_ASSERT(fbtreeLength(fbt) == 4000);

    /* Verify all patterns can be found */
    for (int i = 0; i < 1000; i++) {
        static_string search_str;
        
        snprintf(buf, sizeof(buf), "common_prefix_%04d_suffix", i);
        search_str = createString(buf);
        TEST_ASSERT(fbtreeLookup(fbt, search_str));
        ssfree(search_str);
        
        snprintf(buf, sizeof(buf), "prefix_%04d_common_suffix", i);
        search_str = createString(buf);
        TEST_ASSERT(fbtreeLookup(fbt, search_str));
        ssfree(search_str);
        
        snprintf(buf, sizeof(buf), "pal_%04d_%04d_lap", i, i);
        search_str = createString(buf);
        TEST_ASSERT(fbtreeLookup(fbt, search_str));
        ssfree(search_str);
        
        char c = 'a' + (i % 26);
        snprintf(buf, sizeof(buf), "repeat_%c%c%c%c_%04d", c, c, c, c, i);
        search_str = createString(buf);
        TEST_ASSERT(fbtreeLookup(fbt, search_str));
        ssfree(search_str);
    }

    /* Verify sorted iteration works correctly */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    const_static_string prev_pos = NULL;
    
    for (int i = 0; i < 4000; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
        if (prev_pos) {
            /* Verify ordering */
            TEST_ASSERT(memcmp(prev_pos, pos, 
                              (sslen(prev_pos) < sslen(pos) ? sslen(prev_pos) : sslen(pos))) <= 0);
        }
        prev_pos = pos;
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* Test boundary conditions during inner node splits */
int test_fbtree_inner_node_split_boundaries(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert exactly enough items to trigger specific split scenarios */
    char buf[16];
    
    /* Insert items that will cause splits at exact node boundaries */
    // TODO: this number is wrong - sequential insert tends to leave behind half-empty nodes
    for (int i = 0; i < 4096; i++) { /* 64^2 = 4096 */
        snprintf(buf, sizeof(buf), "bound_%05d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    
    /* Add one more to force inner node split */
    static_string str = createString("bound_04096");
    fbtreeInsert(fbt, str);
    ssfree(str);
    TEST_ASSERT(is_tree_valid(fbt));

    TEST_ASSERT(fbtreeLength(fbt) == 4097);

    /* Verify the boundary item that caused the split */
    static_string search_str = createString("bound_04096");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    ssfree(search_str);

    /* Test iteration across the split boundary */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    
    /* Skip to near the boundary */
    for (int i = 0; i < 4090; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
    }
    
    /* Verify items around the split boundary */
    for (int i = 4090; i < 4097; i++) {
        snprintf(buf, sizeof(buf), "bound_%05d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* Test stress scenario with many inner node splits */
int test_fbtree_stress_inner_node_splits(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert a large number of items to stress test inner node splits */
    char buf[16];
    const int num_items = 50000;
    
    for (int i = 0; i < num_items; i++) {
        snprintf(buf, sizeof(buf), "stress_%06d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));

    TEST_ASSERT(fbtreeLength(fbt) == num_items);

    /* Random lookup test */
    for (int i = 0; i < 1000; i++) {
        int idx = (i * 47) % num_items; /* Pseudo-random access pattern */
        snprintf(buf, sizeof(buf), "stress_%06d", idx);
        static_string search_str = createString(buf);
        TEST_ASSERT(fbtreeLookup(fbt, search_str));
        ssfree(search_str);
    }

    /* Test iteration performance across many splits */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    
    int index = 0;
    while (fbtreeNext(&it, &pos)) {
        /* Verify ordering every 1000 items */
        if (index % 1000 == 10) {
            const_static_string prev_pos = pos;
            TEST_ASSERT(fbtreeNext(&it, &pos));
            index++;
            TEST_ASSERT(sslen(prev_pos) == 14);
            TEST_ASSERT(sslen(pos) == 14);
            TEST_ASSERT(memcmp(prev_pos, pos, 14) <= 0);
        }
        index++;
    }
    TEST_ASSERT(index == num_items);

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* Test middle insertion pattern to stress node split boundaries */
int test_fbtree_middle_insertion_splits(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Always insert in middle of current range to stress split boundaries */
    char buf[16];
    int min_val = 0, max_val = 4095;
    
    while (min_val <= max_val) {
        /* insert min val, then insert max val */
        snprintf(buf, sizeof(buf), "mid_%06d", min_val);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
        min_val++;

        if (min_val > max_val) break;
        snprintf(buf, sizeof(buf), "mid_%06d", max_val);
        str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
        max_val--;
    }
    TEST_ASSERT(is_tree_valid(fbt));

    TEST_ASSERT(fbtreeLength(fbt) == 4096);

    /* Lookup each inserted item */
    for (int i = 0; i < 4096; i++) {
        snprintf(buf, sizeof(buf), "mid_%06d", i);
        static_string search_str = createString(buf);
        TEST_ASSERT(fbtreeLookup(fbt, search_str));
        ssfree(search_str);
    }

    /* Forward iteration - verify exact values 0-4095 */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    
    for (int i = 0; i < 4096; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
        snprintf(buf, sizeof(buf), "mid_%06d", i);
        TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    /* Reverse iteration - verify exact values 4095-0 */
    fbtreeInitIterator(&it, fbt);
    for (int i = 4095; i >= 0; i--) {
        TEST_ASSERT(fbtreePrev(&it, &pos));
        snprintf(buf, sizeof(buf), "mid_%06d", i);
        TEST_ASSERT(memcmp(pos, buf, strlen(buf) + 1) == 0);
    }
    TEST_ASSERT(!fbtreePrev(&it, &pos));

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* Test fbtreeDelete on single leaf node - delete single item */
int test_fbtree_delete_single_item(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    static_string str = createString("test");
    fbtreeInsert(fbt, str);
    ssfree(str);
    TEST_ASSERT(fbtreeLength(fbt) == 1);
    
    static_string del_str = createString("test");
    TEST_ASSERT(fbtreeDelete(fbt, del_str));
    ssfree(del_str);
    TEST_ASSERT(fbtreeLength(fbt) == 0);
    
    static_string search_str = createString("test");
    TEST_ASSERT(!fbtreeLookup(fbt, search_str));
    ssfree(search_str);
    TEST_ASSERT(is_tree_valid(fbt));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* Test fbtreeDelete on single leaf node - delete non-existent item */
int test_fbtree_delete_nonexistent(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    static_string str = createString("exists");
    fbtreeInsert(fbt, str);
    ssfree(str);
    
    static_string del_str = createString("missing");
    TEST_ASSERT(!fbtreeDelete(fbt, del_str));
    ssfree(del_str);
    
    static_string search_str = createString("exists");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    ssfree(search_str);
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* Test fbtreeDelete on single leaf node - delete from empty tree */
int test_fbtree_delete_empty(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    static_string del_str = createString("anything");
    TEST_ASSERT(!fbtreeDelete(fbt, del_str));
    ssfree(del_str);
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* Test fbtreeDelete - delete all items maintains empty state */
int test_fbtree_delete_all_sequential(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    char buf[8];
    for (int i = 0; i < 10; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(fbtreeLength(fbt) == 10);
    
    for (int i = 0; i < 10; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        static_string del_str = createString(buf);
        TEST_ASSERT(fbtreeDelete(fbt, del_str));
        ssfree(del_str);
        TEST_ASSERT(fbtreeLength(fbt) == 10UL - i - 1UL);
        
        static_string search_str = createString(buf);
        TEST_ASSERT(!fbtreeLookup(fbt, search_str));
        ssfree(search_str);
    }
    TEST_ASSERT(fbtreeLength(fbt) == 0);
    TEST_ASSERT(is_tree_valid(fbt));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* Test fbtreeDelete - delete from larger set preserves structure */
int test_fbtree_delete_large_set(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    char buf[8];
    for (int i = 0; i < 50; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(fbtreeLength(fbt) == 50);
    
    static_string del_str = createString("k25");
    TEST_ASSERT(fbtreeDelete(fbt, del_str));
    ssfree(del_str);
    TEST_ASSERT(fbtreeLength(fbt) == 49);
    
    static_string search_str = createString("k25");
    TEST_ASSERT(!fbtreeLookup(fbt, search_str));
    ssfree(search_str);
    
    search_str = createString("k24");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    ssfree(search_str);
    
    search_str = createString("k26");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    ssfree(search_str);
    
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    int count = 0;
    while (fbtreeNext(&it, &pos)) count++;
    TEST_ASSERT(count == 49);
    TEST_ASSERT(is_tree_valid(fbt));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* Test fbtreeDelete - delete from unordered node */
int test_fbtree_delete_unordered_node(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    const char *items[] = {"zebra", "apple", "banana"};
    for (int i = 0; i < 3; i++) {
        static_string str = createString(items[i]);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(fbtreeLength(fbt) == 3);
    
    /* Delete without iterating (node remains unordered) */
    static_string del_str = createString("banana");
    TEST_ASSERT(fbtreeDelete(fbt, del_str));
    ssfree(del_str);
    TEST_ASSERT(fbtreeLength(fbt) == 2);
    
    static_string search_str = createString("banana");
    TEST_ASSERT(!fbtreeLookup(fbt, search_str));
    ssfree(search_str);
    
    search_str = createString("zebra");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    ssfree(search_str);
    
    search_str = createString("apple");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    ssfree(search_str);
    TEST_ASSERT(is_tree_valid(fbt));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* Test fbtreeDelete - delete from ordered node after iteration */
int test_fbtree_delete_ordered_node(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    const char *items[] = {"zebra", "apple", "banana"};
    for (int i = 0; i < 3; i++) {
        static_string str = createString(items[i]);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(fbtreeLength(fbt) == 3);
    
    /* Trigger ordering by iterating */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    TEST_ASSERT(fbtreeNext(&it, &pos));
    
    /* Delete from now-ordered node */
    static_string del_str = createString("banana");
    TEST_ASSERT(fbtreeDelete(fbt, del_str));
    ssfree(del_str);
    TEST_ASSERT(fbtreeLength(fbt) == 2);
    
    /* Verify sorted order maintained */
    fbtreeInitIterator(&it, fbt);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "apple", 6) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "zebra", 6) == 0);
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    TEST_ASSERT(is_tree_valid(fbt));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* Test fbtreeDelete - mixed ordered/unordered operations */
int test_fbtree_delete_mixed_order_states(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    char buf[8];
    for (int i = 0; i < 20; i++) {
        snprintf(buf, sizeof(buf), "k%02d", 19 - i); /* Insert in reverse */
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(fbtreeLength(fbt) == 20);
    
    /* Delete from unordered node */
    static_string del_str = createString("k10");
    TEST_ASSERT(fbtreeDelete(fbt, del_str));
    ssfree(del_str);
    TEST_ASSERT(fbtreeLength(fbt) == 19);
    
    /* Trigger ordering */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    TEST_ASSERT(fbtreeNext(&it, &pos));
    
    /* Delete from ordered node */
    del_str = createString("k05");
    TEST_ASSERT(fbtreeDelete(fbt, del_str));
    ssfree(del_str);
    TEST_ASSERT(fbtreeLength(fbt) == 18);
    
    /* Verify both deletions worked and order maintained */
    static_string search_str = createString("k10");
    TEST_ASSERT(!fbtreeLookup(fbt, search_str));
    ssfree(search_str);
    
    search_str = createString("k05");
    TEST_ASSERT(!fbtreeLookup(fbt, search_str));
    ssfree(search_str);
    
    fbtreeInitIterator(&it, fbt);
    int count = 0;
    while (fbtreeNext(&it, &pos)) count++;
    TEST_ASSERT(count == 18);
    TEST_ASSERT(is_tree_valid(fbt));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* Test anchor bubble-up when deleting from rightmost leaf */
int test_fbtree_delete_anchor_bubbleup(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    /* Insert enough items to force splits and create multi-level tree */
    char buf[16];
    for (int i = 0; i < 200; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(fbtreeLength(fbt) == 200);
    TEST_ASSERT(is_tree_valid(fbt));
    
    /* Delete the maximum key (should trigger anchor updates) */
    static_string del_str = createString("key_199");
    TEST_ASSERT(fbtreeDelete(fbt, del_str));
    ssfree(del_str);
    TEST_ASSERT(fbtreeLength(fbt) == 199);
    TEST_ASSERT(is_tree_valid(fbt));
    
    /* Verify tree is still valid and max key is now key_198 */
    TEST_ASSERT(is_tree_valid(fbt));
    static_string search_str = createString("key_199");
    TEST_ASSERT(!fbtreeLookup(fbt, search_str));
    ssfree(search_str);
    
    search_str = createString("key_198");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    ssfree(search_str);
    
    /* Verify iteration still works correctly */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    int count = 0;
    while (fbtreePrev(&it, &pos)) count++;
    TEST_ASSERT(count == 199);
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* ========== Rank Tests ========== */

int test_fbtree_rank_single_leaf(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    const char *items[] = {"cherry", "apple", "banana", "date"};
    for (int i = 0; i < 4; i++) {
        static_string str = createString(items[i]);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    
    /* After sorting: apple, banana, cherry, date */
    const_static_string result = fbtreeGetAtRank(fbt, 0);
    TEST_ASSERT(result && memcmp(result, "apple", 6) == 0);
    
    result = fbtreeGetAtRank(fbt, 1);
    TEST_ASSERT(result && memcmp(result, "banana", 7) == 0);
    
    result = fbtreeGetAtRank(fbt, 2);
    TEST_ASSERT(result && memcmp(result, "cherry", 7) == 0);
    
    result = fbtreeGetAtRank(fbt, 3);
    TEST_ASSERT(result && memcmp(result, "date", 5) == 0);
    
    result = fbtreeGetAtRank(fbt, 4);
    TEST_ASSERT(result == NULL);
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_rank_multilevel(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    char buf[16];
    for (int i = 0; i < 200; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));
    
    /* Test rank access at various positions */
    const_static_string result;
    
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
    return 0;
}

int test_fbtree_rank_after_delete(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    char buf[16];
    for (int i = 0; i < 100; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    
    /* Delete key_050 */
    static_string del_str = createString("key_050");
    TEST_ASSERT(fbtreeDelete(fbt, del_str));
    ssfree(del_str);
    
    /* Rank 50 should now be key_051 */
    const_static_string result = fbtreeGetAtRank(fbt, 50);
    TEST_ASSERT(result && memcmp(result, "key_051", 8) == 0);
    
    /* Rank 49 should still be key_049 */
    result = fbtreeGetAtRank(fbt, 49);
    TEST_ASSERT(result && memcmp(result, "key_049", 8) == 0);
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_get_rank_of_key(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    char buf[16];
    for (int i = 0; i < 100; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    
    static_string search;
    
    search = createString("key_000");
    TEST_ASSERT(fbtreeGetRankOfKey(fbt, search) == 0);
    ssfree(search);
    
    search = createString("key_050");
    TEST_ASSERT(fbtreeGetRankOfKey(fbt, search) == 50);
    ssfree(search);
    
    search = createString("key_099");
    TEST_ASSERT(fbtreeGetRankOfKey(fbt, search) == 99);
    ssfree(search);
    
    /* Non-existent key */
    search = createString("key_100");
    TEST_ASSERT(fbtreeGetRankOfKey(fbt, search) == fbtreeLength(fbt));
    ssfree(search);
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_seek_to_rank(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    char buf[16];
    for (int i = 0; i < 100; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_static_string pos;
    
    /* Seek to rank 50 and iterate forward */
    fbtreeSeekToRank(&it, 50);
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, "key_050", 8) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, "key_051", 8) == 0);
    
    /* Seek to rank 0 */
    fbtreeSeekToRank(&it, 0);
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, "key_000", 8) == 0);
    
    /* Seek to last rank */
    fbtreeSeekToRank(&it, 99);
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, "key_099", 8) == 0);
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_rank_deep_tree(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    /* Create deep tree with 5000 items */
    char buf[16];
    for (int i = 0; i < 5000; i++) {
        snprintf(buf, sizeof(buf), "key_%05d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));
    
    /* Test rank access at various positions */
    const_static_string result;
    
    result = fbtreeGetAtRank(fbt, 0);
    TEST_ASSERT(result && memcmp(result, "key_00000", 10) == 0);
    
    result = fbtreeGetAtRank(fbt, 2500);
    TEST_ASSERT(result && memcmp(result, "key_02500", 10) == 0);
    
    result = fbtreeGetAtRank(fbt, 4999);
    TEST_ASSERT(result && memcmp(result, "key_04999", 10) == 0);
    
    /* Test get rank of key */
    static_string search = createString("key_02500");
    TEST_ASSERT(fbtreeGetRankOfKey(fbt, search) == 2500);
    ssfree(search);
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* ========== Prefix Lookup Tests ========== */

int test_fbtree_lookup_by_prefix_basic(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    /* Insert items with common prefixes */
    const char *items[] = {"abc_001", "abc_002", "abc_003", "def_001", "def_002"};
    for (int i = 0; i < 5; i++) {
        static_string str = createString(items[i]);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    
    fbtreeIterator it;
    const_static_string pos;
    
    /* Find first item with prefix "abc" */
    TEST_ASSERT(fbtreeLookupByPrefix(fbt, "abc", 3, &it));
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, "abc_001", 8) == 0);
    
    /* Find first item with prefix "def" */
    TEST_ASSERT(fbtreeLookupByPrefix(fbt, "def", 3, &it));
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, "def_001", 8) == 0);
    
    /* Non-existent prefix */
    TEST_ASSERT(!fbtreeLookupByPrefix(fbt, "xyz", 3, &it));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_lookup_by_prefix_iterate(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    /* Insert items with common prefixes */
    const char *items[] = {"aaa", "aab", "aac", "baa", "bab", "bac"};
    for (int i = 0; i < 6; i++) {
        static_string str = createString(items[i]);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    
    fbtreeIterator it;
    const_static_string pos;
    
    /* Find first "aa" and iterate through all "aa" items */
    TEST_ASSERT(fbtreeLookupByPrefix(fbt, "aa", 2, &it));
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "aaa", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "aab", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "aac", 4) == 0);
    /* Next item is "baa" which doesn't match prefix */
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos, "baa", 4) == 0);
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_lookup_by_prefix_8byte(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    /* Simulate zset-like keys: 8-byte prefix (score) + element */
    char key[24];
    for (int i = 0; i < 100; i++) {
        /* Create key with 8-byte "score" prefix */
        uint64_t score = (uint64_t)i * 1000;
        memcpy(key, &score, 8);
        snprintf(key + 8, 16, "element_%03d", i);
        static_string str = ssnewlen(key, 8 + strlen(key + 8) + 1);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    
    fbtreeIterator it;
    const_static_string pos;
    
    /* Lookup by 8-byte score prefix */
    uint64_t search_score = 50 * 1000;
    TEST_ASSERT(fbtreeLookupByPrefix(fbt, (const char *)&search_score, 8, &it));
    TEST_ASSERT(fbtreeNext(&it, &pos));
    uint64_t found_score;
    memcpy(&found_score, pos, 8);
    TEST_ASSERT(found_score == search_score);
    
    /* Non-existent score */
    search_score = 999999;
    TEST_ASSERT(!fbtreeLookupByPrefix(fbt, (const char *)&search_score, 8, &it));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_lookup_by_prefix_multilevel(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    /* Insert enough items to create multi-level tree */
    char buf[16];
    for (int i = 0; i < 200; i++) {
        snprintf(buf, sizeof(buf), "key_%03d", i);
        static_string str = createString(buf);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    TEST_ASSERT(is_tree_valid(fbt));
    
    fbtreeIterator it;
    const_static_string pos;
    
    /* Find first item with prefix "key_05" */
    TEST_ASSERT(fbtreeLookupByPrefix(fbt, "key_05", 6, &it));
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, "key_050", 8) == 0);
    
    /* Find first item with prefix "key_1" */
    TEST_ASSERT(fbtreeLookupByPrefix(fbt, "key_1", 5, &it));
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, "key_100", 8) == 0);
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_lookup_by_prefix_empty(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    fbtreeIterator it;
    
    /* Lookup in empty tree */
    TEST_ASSERT(!fbtreeLookupByPrefix(fbt, "abc", 3, &it));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_lookup_by_prefix_single_char(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    const char *items[] = {"apple", "banana", "cherry"};
    for (int i = 0; i < 3; i++) {
        static_string str = createString(items[i]);
        fbtreeInsert(fbt, str);
        ssfree(str);
    }
    
    fbtreeIterator it;
    const_static_string pos;
    
    /* Single character prefix */
    TEST_ASSERT(fbtreeLookupByPrefix(fbt, "b", 1, &it));
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, "banana", 7) == 0);
    
    TEST_ASSERT(fbtreeLookupByPrefix(fbt, "c", 1, &it));
    TEST_ASSERT(fbtreeNext(&it, &pos));
    TEST_ASSERT(memcmp(pos, "cherry", 7) == 0);
    
    /* No items starting with 'd' */
    TEST_ASSERT(!fbtreeLookupByPrefix(fbt, "d", 1, &it));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}
