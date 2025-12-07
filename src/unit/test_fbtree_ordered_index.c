#include "../fbtree_ordered_index.h"
#include "../zmalloc.h"
#include "test_help.h"

#include <stdio.h>
#include <string.h>

/* Helper to create a static_string */
static static_string *createString(const char *str) {
    size_t len = strlen(str);
    static_string *s = zmalloc(sizeof(static_string) + len + 1);
    s->len = len + 1;
    memcpy((char *)s->buf, str, len + 1);
    return s;
}

int test_fbtree_create_and_free(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    TEST_ASSERT(fbt != NULL);
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
    static_string *str = createString("hello");
    fbtreeInsert(fbt, str);
    zfree(str);
    
    /* Lookup existing string */
    static_string *found = fbtreeLookup(fbt, "hello", 6);
    TEST_ASSERT(found != NULL);
    TEST_ASSERT(found->len == 6);
    TEST_ASSERT(memcmp(found->buf, "hello", 6) == 0);
    
    /* Lookup non-existent string */
    static_string *not_found = fbtreeLookup(fbt, "world", 6);
    TEST_ASSERT(not_found == NULL);
    
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
        static_string *str = createString(strings[i]);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    
    /* Verify all can be found */
    for (int i = 0; i < count; i++) {
        size_t len = strlen(strings[i]) + 1;
        static_string *found = fbtreeLookup(fbt, strings[i], len);
        TEST_ASSERT(found != NULL);
        TEST_ASSERT(found->len == len);
        TEST_ASSERT(memcmp(found->buf, strings[i], len) == 0);
    }
    
    /* Verify non-existent string not found */
    static_string *not_found = fbtreeLookup(fbt, "fig", 4);
    TEST_ASSERT(not_found == NULL);
    
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
    
    /* Lookup in empty tree */
    static_string *not_found = fbtreeLookup(fbt, "anything", 8);
    TEST_ASSERT(not_found == NULL);
    
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
    static_string *str1 = createString("first");
    fbtreeInsert(fbt, str1);
    zfree(str1);
    TEST_ASSERT(fbtreeLength(fbt) == 1);
    
    static_string *str2 = createString("second");
    fbtreeInsert(fbt, str2);
    zfree(str2);
    TEST_ASSERT(fbtreeLength(fbt) == 2);
    
    static_string *str3 = createString("third");
    fbtreeInsert(fbt, str3);
    zfree(str3);
    TEST_ASSERT(fbtreeLength(fbt) == 3);
    
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
        static_string *str = createString(strings[i]);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "ant", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "bat", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "cat", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "dog", 4) == 0);
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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;

    for (int i = 0; i < 65; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(pos->len == 4);
        TEST_ASSERT(memcmp(pos->buf, buf, 4) == 0);
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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;

    for (int i = 0; i < 65; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(pos->len == 4);
        TEST_ASSERT(memcmp(pos->buf, buf, 4) == 0);
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
    
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
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
        static_string *str = createString(strings[i]);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    
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
    
    static_string *str1 = createString("key");
    fbtreeInsert(fbt, str1);
    zfree(str1);
    TEST_ASSERT(fbtreeLength(fbt) == 1);
    
    static_string *str2 = createString("key");
    fbtreeInsert(fbt, str2);
    zfree(str2);
    TEST_ASSERT(fbtreeLength(fbt) == 2);
    
    static_string *found = fbtreeLookup(fbt, "key", 4);
    TEST_ASSERT(found != NULL);
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

int test_fbtree_empty_string(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    static_string *str = createString("");
    fbtreeInsert(fbt, str);
    zfree(str);
    
    static_string *found = fbtreeLookup(fbt, "", 1);
    TEST_ASSERT(found != NULL);
    TEST_ASSERT(found->len == 1);
    
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
        static_string *str = createString(strings[i]);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "e", 2) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "elder", 6) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "elderberry", 11) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "elderly", 8) == 0);
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
    
    static_string *str = createString(long_str);
    fbtreeInsert(fbt, str);
    zfree(str);
    
    static_string *found = fbtreeLookup(fbt, long_str, 256);
    TEST_ASSERT(found != NULL);
    TEST_ASSERT(found->len == 256);
    TEST_ASSERT(memcmp(found->buf, long_str, 256) == 0);
    
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
        static_string *str = createString(strings[i]);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "abc", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "def", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "xyz", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "zoo", 4) == 0);
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
        static_string *str = createString(batch1[i]);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    
    /* Trigger sort via iteration */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    fbtreeNext(&it, &pos);
    
    /* Insert more (now ordered) */
    const char *batch2[] = {"bat", "elk"};
    for (int i = 0; i < 2; i++) {
        static_string *str = createString(batch2[i]);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    
    /* Final iteration - verify all sorted */
    fbtreeInitIterator(&it, fbt);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "ant", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "bat", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "cat", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "dog", 4) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "elk", 4) == 0);
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
    static_string *str = createString("m");
    fbtreeInsert(fbt, str);
    zfree(str);
    
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    fbtreeNext(&it, &pos);
    
    /* Insert at beginning, middle, end */
    const char *inserts[] = {"a", "z", "n"};
    for (int i = 0; i < 3; i++) {
        str = createString(inserts[i]);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    
    fbtreeInitIterator(&it, fbt);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "a", 2) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "m", 2) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "n", 2) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "z", 2) == 0);
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
        static_string *str = createString(strings[i]);
        fbtreeInsert(fbt, str);
        zfree(str);
    }

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;

    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos->buf, "dog", 4) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos->buf, "cat", 4) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos->buf, "bat", 4) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos->buf, "ant", 4) == 0);
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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;

    for (int i = 64; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        TEST_ASSERT(fbtreePrev(&it, &pos));
        TEST_ASSERT(pos->len == 4);
        TEST_ASSERT(memcmp(pos->buf, buf, 4) == 0);
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

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
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
        static_string *str = createString(strings[i]);
        fbtreeInsert(fbt, str);
        zfree(str);
    }

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;

    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "a", 2) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "b", 2) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos->buf, "b", 2) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos->buf, "a", 2) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "a", 2) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "b", 2) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "c", 2) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos->buf, "c", 2) == 0);
    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos->buf, "b", 2) == 0);

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

    static_string *str = createString("only");
    fbtreeInsert(fbt, str);
    zfree(str);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;

    TEST_ASSERT(fbtreePrev(&it, &pos) && memcmp(pos->buf, "only", 5) == 0);
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

    static_string *str = createString("x");
    fbtreeInsert(fbt, str);
    zfree(str);

    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;

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
