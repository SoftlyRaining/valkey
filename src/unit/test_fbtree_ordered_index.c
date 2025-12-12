#include "../fbtree_ordered_index.h"
#include "../zmalloc.h"
#include "test_help.h"

#include <stdio.h>
#include <string.h>

/* Helper to create a static_string */
static static_string *createString(const char *str) {
    size_t len = strlen(str);
    static_string *s = zmalloc(sizeof(static_string) + len + 1);
    *(size_t*)&s->len = len + 1;
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
    static_string *search_string = createString("hello");
    TEST_ASSERT(fbtreeLookup(fbt, search_string));
    zfree(search_string);
    
    /* Lookup non-existent string */
    static_string *search_string2 = createString("world");
    TEST_ASSERT(fbtreeLookup(fbt, search_string2) == false);
    zfree(search_string2);
    
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
        static_string *search_str = createString(strings[i]);
        TEST_ASSERT(fbtreeLookup(fbt, search_str));
        zfree(search_str);
    }
    
    /* Verify non-existent string not found */
    static_string *search_str = createString("fig");
    TEST_ASSERT(fbtreeLookup(fbt, search_str) == false);
    zfree(search_str);
    
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
    static_string *search_str = createString("anything");
    TEST_ASSERT(fbtreeLookup(fbt, search_str) == false);
    zfree(search_str);
    
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
    
    static_string *search_str = createString("key");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    zfree(search_str);
    
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
    
    static_string *search_str = createString("");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    zfree(search_str);
    
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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }

    /* Verify forward iteration is sorted */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    for (int i = 0; i <= 95; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos->buf, buf, 5) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    /* Verify backward iteration */
    fbtreeInitIterator(&it, fbt);
    for (int i = 95; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        TEST_ASSERT(fbtreePrev(&it, &pos));
        TEST_ASSERT(memcmp(pos->buf, buf, 5) == 0);
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
    
    static_string *search_str = createString(long_str);
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    zfree(search_str);
    
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

int test_fbtree_multilevel_lookup(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();

    /* Insert 65 items to create 2-level tree */
    char buf[8];
    for (int i = 0; i < 65; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }

    /* Lookup all items in multi-level tree */
    for (int i = 0; i < 65; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        static_string *search_str = createString(buf);
        TEST_ASSERT(fbtreeLookup(fbt, search_str));
        zfree(search_str);
    }

    /* Lookup non-existent keys */
    static_string *search_str1 = createString("k99");
    TEST_ASSERT(fbtreeLookup(fbt, search_str1) == false);
    zfree(search_str1);
    static_string *search_str2 = createString("x00");
    TEST_ASSERT(fbtreeLookup(fbt, search_str2) == false);
    zfree(search_str2);

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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }

    /* Forward iteration through all items */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    for (int i = 0; i < 130; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos->buf, buf, 5) == 0);
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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }

    /* Backward iteration through all items */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    for (int i = 129; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        TEST_ASSERT(fbtreePrev(&it, &pos));
        TEST_ASSERT(memcmp(pos->buf, buf, 5) == 0);
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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }

    TEST_ASSERT(fbtreeLength(fbt) == 100);

    /* Mixed forward/backward iteration */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;

    /* Forward 10: 0,1,2,3,4,5,6,7,8,9 */
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
    }
    TEST_ASSERT(pos->len == 5);
    TEST_ASSERT(memcmp(pos->buf, "k009", 5) == 0);

    /* Backward 5: 9,8,7,6,5 */
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT(fbtreePrev(&it, &pos));
    }
    TEST_ASSERT(pos->len == 5);
    TEST_ASSERT(memcmp(pos->buf, "k005", 5) == 0);

    /* Forward to end: 5,6,7...98,99 */
    int count = 1;
    while (fbtreeNext(&it, &pos)) count++;
    TEST_ASSERT(count == 96); /* 5 to 95 inclusive is 96 items */
    TEST_ASSERT(pos->len == 5);
    TEST_ASSERT(memcmp(pos->buf, "k099", 5) == 0);

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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }

    /* Verify sorted iteration */
    int expected[] = {0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 66,
                      67, 68, 69, 70, 71, 72, 73, 75, 80, 85, 90, 95, 100, 110, 120};
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    for (int i = 0; i < 30; i++) {
        snprintf(buf, sizeof(buf), "k%03d", expected[i]);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos->buf, buf, 5) == 0);
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
        static_string *str = createString(keys[i]);
        fbtreeInsert(fbt, str);
        zfree(str);
    }

    /* Lookup all keys */
    for (int i = 0; i < 12; i++) {
        static_string *search_str = createString(keys[i]);
        TEST_ASSERT(fbtreeLookup(fbt, search_str));
        zfree(search_str);
    }

    /* Verify sorted iteration */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    for (int i = 0; i < 12; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
        size_t len = strlen(keys[i]) + 1;
        TEST_ASSERT(memcmp(pos->buf, keys[i], len) == 0);
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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }

    /* Test iteration crossing leaf boundaries */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;

    /* Skip to near first leaf boundary (around item 32) */
    for (int i = 0; i < 30; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
    }

    /* Verify items around boundary */
    for (int i = 30; i < 40; i++) {
        snprintf(buf, sizeof(buf), "k%03d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos->buf, buf, 5) == 0);
    }

    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}
