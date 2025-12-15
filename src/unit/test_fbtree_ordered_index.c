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

static static_string *createBase26TestString(const char *prefix, const char *suffix, size_t value, size_t value_width) {
    size_t len = strlen(prefix) + strlen(suffix) + value_width + 1;
    static_string *s = zmalloc(sizeof(static_string) + len);
    *(size_t*)&s->len = len;
    char *buf = (char *)s->buf;
    memcpy(buf, prefix, strlen(prefix));

    /* Create a base-26 number using letters */
    for (size_t i = 0; i < value_width; i++) {
        char c = (char)('A' + (value % 26));
        buf[strlen(prefix) + value_width - 1 - i] = c;
        value /= 26;
    }

    memcpy(buf + strlen(prefix) + value_width, suffix, strlen(suffix));
    buf[len - 1] = '\0';

    return s;
}

int test_fbtree_create_and_free(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);
    
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    TEST_ASSERT(fbt != NULL);
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
    
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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
    
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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
    
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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
    
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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
    
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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
    
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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
    
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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
    
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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
    
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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
    
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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
    
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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
    
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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
    
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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
    
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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

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
    TEST_ASSERT(count == 96); /* 5 to 99 inclusive is 96 items */
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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

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
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

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
        static_string *str = createBase26TestString("key_", "", i, 3);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

    TEST_ASSERT(fbtreeLength(fbt) == 4200);

    /* Verify all items can be found */
    for (int i = 0; i < 4200; i++) {
        static_string *search_str = createBase26TestString("key_", "", i, 3);
        TEST_ASSERT(fbtreeLookup(fbt, search_str));
        zfree(search_str);
    }

    /* Verify forward iteration maintains order */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    for (int i = 0; i < 4200; i++) {
        static_string *expected = createBase26TestString("key_", "", i, 3);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(pos->len == expected->len);
        TEST_ASSERT(memcmp(pos->buf, expected->buf, expected->len) == 0);
        zfree(expected);
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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

    TEST_ASSERT(fbtreeLength(fbt) == 4200);

    /* Verify forward iteration is still sorted */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    for (int i = 0; i < 4200; i++) {
        snprintf(buf, sizeof(buf), "key_%05d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos->buf, buf, strlen(buf) + 1) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    /* Verify backward iteration */
    fbtreeInitIterator(&it, fbt);
    for (int i = 4199; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "key_%05d", i);
        TEST_ASSERT(fbtreePrev(&it, &pos));
        TEST_ASSERT(memcmp(pos->buf, buf, strlen(buf) + 1) == 0);
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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

    TEST_ASSERT(fbtreeLength(fbt) == 5000);

    /* Verify all items exist and iteration is sorted */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    for (int i = 0; i < 5000; i++) {
        snprintf(buf, sizeof(buf), "rnd_%05d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos->buf, buf, strlen(buf) + 1) == 0);
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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

    TEST_ASSERT(fbtreeLength(fbt) == 300000);

    /* Test lookups at various points */
    static_string *search_str;
    
    /* First item */
    search_str = createString("deep_000000");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    zfree(search_str);
    
    /* Middle item */
    search_str = createString("deep_150000");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    zfree(search_str);
    
    /* Last item */
    search_str = createString("deep_299999");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    zfree(search_str);
    
    /* Non-existent items */
    search_str = createString("deep_300000");
    TEST_ASSERT(fbtreeLookup(fbt, search_str) == false);
    zfree(search_str);

    /* Test iteration across deep tree boundaries */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    
    /* Skip to middle and verify ordering around deep boundaries */
    for (int i = 0; i < 150000; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
    }
    
    /* Verify next 100 items are in order */
    for (int i = 150000; i < 150100; i++) {
        snprintf(buf, sizeof(buf), "deep_%06d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos->buf, buf, strlen(buf) + 1) == 0);
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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    
    /* Insert second batch in reverse */
    for (int i = 9999; i >= 0; i--) {
        snprintf(buf, sizeof(buf), "mix_%06d", i * 3 + 1);
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    zfree(indices);
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

    TEST_ASSERT(fbtreeLength(fbt) == 30000);

    /* Verify complete sorted iteration */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    for (int i = 0; i < 30000; i++) {
        snprintf(buf, sizeof(buf), "mix_%06d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos->buf, buf, strlen(buf) + 1) == 0);
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
    TEST_ASSERT(memcmp(pos->buf, buf, strlen(buf) + 1) == 0);

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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    
    /* Pattern 2: Varying prefixes with common suffixes */
    for (int i = 0; i < 1000; i++) {
        snprintf(buf, sizeof(buf), "prefix_%04d_common_suffix", i);
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    
    /* Pattern 3: Palindromic patterns */
    for (int i = 0; i < 1000; i++) {
        snprintf(buf, sizeof(buf), "pal_%04d_%04d_lap", i, i);
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    
    /* Pattern 4: Repeated character patterns */
    for (int i = 0; i < 1000; i++) {
        char c = 'a' + (i % 26);
        snprintf(buf, sizeof(buf), "repeat_%c%c%c%c_%04d", c, c, c, c, i);
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

    TEST_ASSERT(fbtreeLength(fbt) == 4000);

    /* Verify all patterns can be found */
    for (int i = 0; i < 1000; i++) {
        static_string *search_str;
        
        snprintf(buf, sizeof(buf), "common_prefix_%04d_suffix", i);
        search_str = createString(buf);
        TEST_ASSERT(fbtreeLookup(fbt, search_str));
        zfree(search_str);
        
        snprintf(buf, sizeof(buf), "prefix_%04d_common_suffix", i);
        search_str = createString(buf);
        TEST_ASSERT(fbtreeLookup(fbt, search_str));
        zfree(search_str);
        
        snprintf(buf, sizeof(buf), "pal_%04d_%04d_lap", i, i);
        search_str = createString(buf);
        TEST_ASSERT(fbtreeLookup(fbt, search_str));
        zfree(search_str);
        
        char c = 'a' + (i % 26);
        snprintf(buf, sizeof(buf), "repeat_%c%c%c%c_%04d", c, c, c, c, i);
        search_str = createString(buf);
        TEST_ASSERT(fbtreeLookup(fbt, search_str));
        zfree(search_str);
    }

    /* Verify sorted iteration works correctly */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    static_string *prev_pos = NULL;
    
    for (int i = 0; i < 4000; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
        if (prev_pos) {
            /* Verify ordering */
            TEST_ASSERT(memcmp(prev_pos->buf, pos->buf, 
                              (prev_pos->len < pos->len ? prev_pos->len : pos->len)) <= 0);
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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    
    /* Add one more to force inner node split */
    static_string *str = createString("bound_04096");
    fbtreeInsert(fbt, str);
    zfree(str);
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

    TEST_ASSERT(fbtreeLength(fbt) == 4097);

    /* Verify the boundary item that caused the split */
    static_string *search_str = createString("bound_04096");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    zfree(search_str);

    /* Test iteration across the split boundary */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    
    /* Skip to near the boundary */
    for (int i = 0; i < 4090; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
    }
    
    /* Verify items around the split boundary */
    for (int i = 4090; i < 4097; i++) {
        snprintf(buf, sizeof(buf), "bound_%05d", i);
        TEST_ASSERT(fbtreeNext(&it, &pos));
        TEST_ASSERT(memcmp(pos->buf, buf, strlen(buf) + 1) == 0);
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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

    TEST_ASSERT(fbtreeLength(fbt) == num_items);

    /* Random lookup test */
    for (int i = 0; i < 1000; i++) {
        int idx = (i * 47) % num_items; /* Pseudo-random access pattern */
        snprintf(buf, sizeof(buf), "stress_%06d", idx);
        static_string *search_str = createString(buf);
        TEST_ASSERT(fbtreeLookup(fbt, search_str));
        zfree(search_str);
    }

    /* Test iteration performance across many splits */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    
    int index = 0;
    while (fbtreeNext(&it, &pos)) {
        /* Verify ordering every 1000 items */
        if (index % 1000 == 10) {
            static_string *prev_pos = pos;
            TEST_ASSERT(fbtreeNext(&it, &pos));
            index++;
            TEST_ASSERT(prev_pos->len == 14);
            TEST_ASSERT(pos->len == 14);
            TEST_ASSERT(memcmp(prev_pos->buf, pos->buf, 14) <= 0);
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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
        min_val++;

        if (min_val > max_val) break;
        snprintf(buf, sizeof(buf), "mid_%06d", max_val);
        str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
        max_val--;
    }
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));

    TEST_ASSERT(fbtreeLength(fbt) == 4096);

    /* Lookup each inserted item */
    for (int i = 0; i < 4096; i++) {
        snprintf(buf, sizeof(buf), "mid_%06d", i);
        static_string *search_str = createString(buf);
        TEST_ASSERT(fbtreeLookup(fbt, search_str));
        zfree(search_str);
    }

    /* Forward iteration - verify exact values 0-4095 */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    
    for (int i = 0; i < 4096; i++) {
        TEST_ASSERT(fbtreeNext(&it, &pos));
        snprintf(buf, sizeof(buf), "mid_%06d", i);
        TEST_ASSERT(memcmp(pos->buf, buf, strlen(buf) + 1) == 0);
    }
    TEST_ASSERT(!fbtreeNext(&it, &pos));

    /* Reverse iteration - verify exact values 4095-0 */
    fbtreeInitIterator(&it, fbt);
    for (int i = 4095; i >= 0; i--) {
        TEST_ASSERT(fbtreePrev(&it, &pos));
        snprintf(buf, sizeof(buf), "mid_%06d", i);
        TEST_ASSERT(memcmp(pos->buf, buf, strlen(buf) + 1) == 0);
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
    
    static_string *str = createString("test");
    fbtreeInsert(fbt, str);
    zfree(str);
    TEST_ASSERT(fbtreeLength(fbt) == 1);
    
    static_string *del_str = createString("test");
    TEST_ASSERT(fbtreeDelete(fbt, del_str));
    zfree(del_str);
    TEST_ASSERT(fbtreeLength(fbt) == 0);
    
    static_string *search_str = createString("test");
    TEST_ASSERT(!fbtreeLookup(fbt, search_str));
    zfree(search_str);
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}



/* Test fbtreeDelete on single leaf node - delete non-existent item */
int test_fbtree_delete_nonexistent(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    static_string *str = createString("exists");
    fbtreeInsert(fbt, str);
    zfree(str);
    
    static_string *del_str = createString("missing");
    TEST_ASSERT(!fbtreeDelete(fbt, del_str));
    zfree(del_str);
    
    static_string *search_str = createString("exists");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    zfree(search_str);
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}

/* Test fbtreeDelete on single leaf node - delete from empty tree */
int test_fbtree_delete_empty(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t used_memory_before = zmalloc_used_memory();
    fbtreeIndex *fbt = fbtreeCreate();
    
    static_string *del_str = createString("anything");
    TEST_ASSERT(!fbtreeDelete(fbt, del_str));
    zfree(del_str);
    
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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    TEST_ASSERT(fbtreeLength(fbt) == 10);
    
    for (int i = 0; i < 10; i++) {
        snprintf(buf, sizeof(buf), "k%02d", i);
        static_string *del_str = createString(buf);
        TEST_ASSERT(fbtreeDelete(fbt, del_str));
        zfree(del_str);
        TEST_ASSERT(fbtreeLength(fbt) == 10UL - i - 1UL);
        
        static_string *search_str = createString(buf);
        TEST_ASSERT(!fbtreeLookup(fbt, search_str));
        zfree(search_str);
    }
    TEST_ASSERT(fbtreeLength(fbt) == 0);
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
    
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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    TEST_ASSERT(fbtreeLength(fbt) == 50);
    
    static_string *del_str = createString("k25");
    TEST_ASSERT(fbtreeDelete(fbt, del_str));
    zfree(del_str);
    TEST_ASSERT(fbtreeLength(fbt) == 49);
    
    static_string *search_str = createString("k25");
    TEST_ASSERT(!fbtreeLookup(fbt, search_str));
    zfree(search_str);
    
    search_str = createString("k24");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    zfree(search_str);
    
    search_str = createString("k26");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    zfree(search_str);
    
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    int count = 0;
    while (fbtreeNext(&it, &pos)) count++;
    TEST_ASSERT(count == 49);
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
    
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
        static_string *str = createString(items[i]);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    TEST_ASSERT(fbtreeLength(fbt) == 3);
    
    /* Delete without iterating (node remains unordered) */
    static_string *del_str = createString("banana");
    TEST_ASSERT(fbtreeDelete(fbt, del_str));
    zfree(del_str);
    TEST_ASSERT(fbtreeLength(fbt) == 2);
    
    static_string *search_str = createString("banana");
    TEST_ASSERT(!fbtreeLookup(fbt, search_str));
    zfree(search_str);
    
    search_str = createString("zebra");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    zfree(search_str);
    
    search_str = createString("apple");
    TEST_ASSERT(fbtreeLookup(fbt, search_str));
    zfree(search_str);
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
    
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
        static_string *str = createString(items[i]);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    TEST_ASSERT(fbtreeLength(fbt) == 3);
    
    /* Trigger ordering by iterating */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    TEST_ASSERT(fbtreeNext(&it, &pos));
    
    /* Delete from now-ordered node */
    static_string *del_str = createString("banana");
    TEST_ASSERT(fbtreeDelete(fbt, del_str));
    zfree(del_str);
    TEST_ASSERT(fbtreeLength(fbt) == 2);
    
    /* Verify sorted order maintained */
    fbtreeInitIterator(&it, fbt);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "apple", 6) == 0);
    TEST_ASSERT(fbtreeNext(&it, &pos) && memcmp(pos->buf, "zebra", 6) == 0);
    TEST_ASSERT(!fbtreeNext(&it, &pos));
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
    
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
        static_string *str = createString(buf);
        fbtreeInsert(fbt, str);
        zfree(str);
    }
    TEST_ASSERT(fbtreeLength(fbt) == 20);
    
    /* Delete from unordered node */
    static_string *del_str = createString("k10");
    TEST_ASSERT(fbtreeDelete(fbt, del_str));
    zfree(del_str);
    TEST_ASSERT(fbtreeLength(fbt) == 19);
    
    /* Trigger ordering */
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    static_string *pos;
    TEST_ASSERT(fbtreeNext(&it, &pos));
    
    /* Delete from ordered node */
    del_str = createString("k05");
    TEST_ASSERT(fbtreeDelete(fbt, del_str));
    zfree(del_str);
    TEST_ASSERT(fbtreeLength(fbt) == 18);
    
    /* Verify both deletions worked and order maintained */
    static_string *search_str = createString("k10");
    TEST_ASSERT(!fbtreeLookup(fbt, search_str));
    zfree(search_str);
    
    search_str = createString("k05");
    TEST_ASSERT(!fbtreeLookup(fbt, search_str));
    zfree(search_str);
    
    fbtreeInitIterator(&it, fbt);
    int count = 0;
    while (fbtreeNext(&it, &pos)) count++;
    TEST_ASSERT(count == 18);
    TEST_ASSERT(fbtreeDebugValidate(fbt, 0));
    
    fbtreeFree(fbt);
    TEST_ASSERT(zmalloc_used_memory() == used_memory_before);
    return 0;
}