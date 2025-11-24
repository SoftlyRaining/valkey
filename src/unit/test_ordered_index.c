#include <stdio.h>
#include <string.h>
#include <math.h>
#include "test_help.h"
#include "../server.h"
#include "../ordered_index.h"

/* Generic tests that work with any OrderedIndex implementation */

static int test_create_free_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    TEST_ASSERT(idx != NULL);
    TEST_ASSERT(orderedIndexLength(ops, idx) == 0);
    TEST_ASSERT(orderedIndexFirst(ops, idx) == NULL);
    TEST_ASSERT(orderedIndexLast(ops, idx) == NULL);
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_insert_single_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    sds ele = sdsnew("test");
    OrderedIndexPosition *node = orderedIndexInsert(ops, idx, 1.0, ele);
    
    TEST_ASSERT(node != NULL);
    TEST_ASSERT(orderedIndexLength(ops, idx) == 1);
    TEST_ASSERT(orderedIndexFirst(ops, idx) == node);
    TEST_ASSERT(orderedIndexLast(ops, idx) == node);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == 1.0);
    
    const char *ptr;
    size_t len;
    orderedIndexGetElementRaw(ops, node, &ptr, &len);
    TEST_ASSERT(len == 4 && memcmp(ptr, "test", 4) == 0);
    
    sdsfree(ele);
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_insert_multiple_ordered_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    
    for (int i = 0; i < 10; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key%d", i);
        sds ele = sdsnew(buf);
        orderedIndexInsert(ops, idx, (double)i, ele);
        sdsfree(ele);
    }
    
    TEST_ASSERT(orderedIndexLength(ops, idx) == 10);
    
    /* Verify forward traversal */
    OrderedIndexPosition *node = orderedIndexFirst(ops, idx);
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT(node != NULL);
        TEST_ASSERT(orderedIndexGetScore(ops, node) == (double)i);
        node = orderedIndexNext(ops, node);
    }
    TEST_ASSERT(node == NULL);
    
    /* Verify backward traversal */
    node = orderedIndexLast(ops, idx);
    for (int i = 9; i >= 0; i--) {
        TEST_ASSERT(node != NULL);
        TEST_ASSERT(orderedIndexGetScore(ops, node) == (double)i);
        node = orderedIndexPrev(ops, node);
    }
    TEST_ASSERT(node == NULL);
    
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_duplicate_scores_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    
    /* Insert elements with same score but different keys */
    for (int i = 0; i < 5; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key%d", i);
        sds ele = sdsnew(buf);
        orderedIndexInsert(ops, idx, 1.0, ele);
        sdsfree(ele);
    }
    
    TEST_ASSERT(orderedIndexLength(ops, idx) == 5);
    
    /* Verify lexicographic ordering for same scores */
    OrderedIndexPosition *node = orderedIndexFirst(ops, idx);
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT(node != NULL);
        TEST_ASSERT(orderedIndexGetScore(ops, node) == 1.0);
        const char *ptr;
        size_t len;
        orderedIndexGetElementRaw(ops, node, &ptr, &len);
        char expected[32];
        snprintf(expected, sizeof(expected), "key%d", i);
        TEST_ASSERT(len == strlen(expected) && memcmp(ptr, expected, len) == 0);
        node = orderedIndexNext(ops, node);
    }
    
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_rank_operations_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    OrderedIndexPosition *nodes[10];
    
    /* Insert 10 elements */
    for (int i = 0; i < 10; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key%d", i);
        sds ele = sdsnew(buf);
        nodes[i] = orderedIndexInsert(ops, idx, (double)i, ele);
        sdsfree(ele);
    }
    
    /* Test get_rank */
    for (int i = 0; i < 10; i++) {
        unsigned long rank = orderedIndexGetRank(ops, idx, nodes[i]);
        TEST_ASSERT(rank == (unsigned long)(i + 1)); /* 1-based */
    }
    
    /* Test get_by_rank */
    for (int i = 0; i < 10; i++) {
        OrderedIndexPosition *node = orderedIndexGetByRank(ops, idx, i + 1);
        TEST_ASSERT(node == nodes[i]);
    }
    
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_delete_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    OrderedIndexPosition *nodes[5];
    
    /* Insert 5 elements */
    for (int i = 0; i < 5; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key%d", i);
        sds ele = sdsnew(buf);
        nodes[i] = orderedIndexInsert(ops, idx, (double)i, ele);
        sdsfree(ele);
    }
    
    TEST_ASSERT(orderedIndexLength(ops, idx) == 5);
    
    /* Delete middle element */
    orderedIndexDelete(ops, idx, nodes[2]);
    TEST_ASSERT(orderedIndexLength(ops, idx) == 4);
    
    /* Verify remaining elements */
    OrderedIndexPosition *node = orderedIndexFirst(ops, idx);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == 0.0);
    node = orderedIndexNext(ops, node);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == 1.0);
    node = orderedIndexNext(ops, node);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == 3.0); /* Skipped 2.0 */
    node = orderedIndexNext(ops, node);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == 4.0);
    
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_update_score_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    
    /* Insert elements */
    sds ele1 = sdsnew("key1");
    sds ele2 = sdsnew("key2");
    sds ele3 = sdsnew("key3");
    OrderedIndexPosition *node1 = orderedIndexInsert(ops, idx, 1.0, ele1);
    OrderedIndexPosition *node2 = orderedIndexInsert(ops, idx, 2.0, ele2);
    orderedIndexInsert(ops, idx, 3.0, ele3);
    sdsfree(ele1); sdsfree(ele2); sdsfree(ele3);
    
    /* Update middle element to move it to end */
    OrderedIndexPosition *updated = orderedIndexUpdateScore(ops, idx, node2, 4.0);
    TEST_ASSERT(updated != NULL);
    TEST_ASSERT(orderedIndexGetScore(ops, updated) == 4.0);
    const char *ptr;
    size_t len;
    orderedIndexGetElementRaw(ops, updated, &ptr, &len);
    TEST_ASSERT(len == 4 && memcmp(ptr, "key2", 4) == 0);
    
    /* Verify order: key1(1.0), key3(3.0), key2(4.0) */
    OrderedIndexPosition *node = orderedIndexFirst(ops, idx);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == 1.0);
    node = orderedIndexNext(ops, node);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == 3.0);
    node = orderedIndexNext(ops, node);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == 4.0);
    orderedIndexGetElementRaw(ops, node, &ptr, &len);
    TEST_ASSERT(len == 4 && memcmp(ptr, "key2", 4) == 0);
    
    /* Update to same score (no-op) */
    updated = orderedIndexUpdateScore(ops, idx, node1, 1.0);
    TEST_ASSERT(orderedIndexGetScore(ops, updated) == 1.0);
    
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_delete_range_by_score_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    
    /* Insert 10 elements with scores 0-9 */
    for (int i = 0; i < 10; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key%d", i);
        sds ele = sdsnew(buf);
        orderedIndexInsert(ops, idx, (double)i, ele);
        sdsfree(ele);
    }
    
    /* Delete range [3, 6] inclusive */
    unsigned long deleted = orderedIndexDeleteRangeByScore(ops, idx, 3.0, 6.0, 0, 0);
    TEST_ASSERT(deleted == 4); /* 3, 4, 5, 6 */
    TEST_ASSERT(orderedIndexLength(ops, idx) == 6);
    
    /* Verify remaining: 0,1,2,7,8,9 */
    OrderedIndexPosition *node = orderedIndexFirst(ops, idx);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT(orderedIndexGetScore(ops, node) == (double)i);
        node = orderedIndexNext(ops, node);
    }
    for (int i = 7; i < 10; i++) {
        TEST_ASSERT(orderedIndexGetScore(ops, node) == (double)i);
        node = orderedIndexNext(ops, node);
    }
    
    /* Delete with exclusive bounds (2, 8) - should delete 7 */
    deleted = orderedIndexDeleteRangeByScore(ops, idx, 2.0, 8.0, 1, 1);
    TEST_ASSERT(deleted == 1);
    TEST_ASSERT(orderedIndexLength(ops, idx) == 5);
    
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_delete_range_by_rank_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    
    /* Insert 10 elements */
    for (int i = 0; i < 10; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key%d", i);
        sds ele = sdsnew(buf);
        orderedIndexInsert(ops, idx, (double)i, ele);
        sdsfree(ele);
    }
    
    /* Delete ranks 3-5 (1-based, so elements at scores 2,3,4) */
    unsigned long deleted = orderedIndexDeleteRangeByRank(ops, idx, 3, 5);
    TEST_ASSERT(deleted == 3);
    TEST_ASSERT(orderedIndexLength(ops, idx) == 7);
    
    /* Verify first element is still score 0 */
    OrderedIndexPosition *node = orderedIndexFirst(ops, idx);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == 0.0);
    
    /* Verify rank 3 is now score 5 (was rank 6) */
    node = orderedIndexGetByRank(ops, idx, 3);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == 5.0);
    
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_mixed_operations_rank_integrity_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    OrderedIndexPosition *nodes[100];
    
    /* Insert 100 elements */
    for (int i = 0; i < 100; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key%d", i);
        sds ele = sdsnew(buf);
        nodes[i] = orderedIndexInsert(ops, idx, (double)i, ele);
        sdsfree(ele);
    }
    
    /* Delete every 3rd element */
    for (int i = 2; i < 100; i += 3) {
        orderedIndexDelete(ops, idx, nodes[i]);
        nodes[i] = NULL;
    }
    
    /* Update scores of some remaining elements */
    if (nodes[10]) nodes[10] = orderedIndexUpdateScore(ops, idx, nodes[10], 150.0);
    if (nodes[20]) nodes[20] = orderedIndexUpdateScore(ops, idx, nodes[20], 160.0);
    
    /* Verify all ranks are correct by forward traversal */
    OrderedIndexPosition *node = orderedIndexFirst(ops, idx);
    unsigned long expected_rank = 1;
    while (node != NULL) {
        unsigned long actual_rank = orderedIndexGetRank(ops, idx, node);
        TEST_ASSERT(actual_rank == expected_rank);
        expected_rank++;
        node = orderedIndexNext(ops, node);
    }
    
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_backward_traversal_after_deletions_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    OrderedIndexPosition *nodes[20];
    
    /* Insert 20 elements */
    for (int i = 0; i < 20; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key%d", i);
        sds ele = sdsnew(buf);
        nodes[i] = orderedIndexInsert(ops, idx, (double)i, ele);
        sdsfree(ele);
    }
    
    /* Delete elements at positions 5, 10, 15 */
    orderedIndexDelete(ops, idx, nodes[5]);
    orderedIndexDelete(ops, idx, nodes[10]);
    orderedIndexDelete(ops, idx, nodes[15]);
    
    /* Traverse backward and verify prev() pointers work correctly */
    OrderedIndexPosition *node = orderedIndexLast(ops, idx);
    int expected_scores[] = {19, 18, 17, 16, 14, 13, 12, 11, 9, 8, 7, 6, 4, 3, 2, 1, 0};
    int idx_score = 0;
    
    while (node != NULL) {
        TEST_ASSERT(orderedIndexGetScore(ops, node) == (double)expected_scores[idx_score]);
        idx_score++;
        node = orderedIndexPrev(ops, node);
    }
    TEST_ASSERT(idx_score == 17); /* Should have traversed all 17 remaining elements */
    
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_lexicographic_edge_cases_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    
    /* Test empty string */
    sds empty = sdsnew("");
    sds a = sdsnew("a");
    sds z = sdsnew("z");
    
    orderedIndexInsert(ops, idx, 1.0, z);
    orderedIndexInsert(ops, idx, 1.0, empty);
    orderedIndexInsert(ops, idx, 1.0, a);
    
    /* Verify lexicographic order: "", "a", "z" */
    OrderedIndexPosition *node = orderedIndexFirst(ops, idx);
    const char *ptr;
    size_t len;
    orderedIndexGetElementRaw(ops, node, &ptr, &len);
    TEST_ASSERT(len == 0);
    node = orderedIndexNext(ops, node);
    orderedIndexGetElementRaw(ops, node, &ptr, &len);
    TEST_ASSERT(len == 1 && memcmp(ptr, "a", 1) == 0);
    node = orderedIndexNext(ops, node);
    orderedIndexGetElementRaw(ops, node, &ptr, &len);
    TEST_ASSERT(len == 1 && memcmp(ptr, "z", 1) == 0);
    
    sdsfree(empty); sdsfree(a); sdsfree(z);
    orderedIndexFree(ops, idx);
    
    /* Test very long string (1KB) */
    idx = orderedIndexCreate(ops);
    char long_buf[1024];
    memset(long_buf, 'x', 1023);
    long_buf[1023] = '\0';
    sds long_str = sdsnew(long_buf);
    sds short_str = sdsnew("short");
    
    orderedIndexInsert(ops, idx, 1.0, long_str);
    orderedIndexInsert(ops, idx, 1.0, short_str);
    
    /* Verify ordering */
    node = orderedIndexFirst(ops, idx);
    orderedIndexGetElementRaw(ops, node, &ptr, &len);
    TEST_ASSERT(len == 5 && memcmp(ptr, "short", 5) == 0);
    node = orderedIndexNext(ops, node);
    orderedIndexGetElementRaw(ops, node, &ptr, &len);
    TEST_ASSERT(len == 1023 && memcmp(ptr, long_buf, 1023) == 0);
    
    sdsfree(long_str); sdsfree(short_str);
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_range_boundary_precision_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    
    /* Insert elements with very close scores */
    double base = 1.0;
    double epsilon = 1e-10;
    
    sds ele1 = sdsnew("at_base");
    sds ele2 = sdsnew("at_base_plus_epsilon");
    sds ele3 = sdsnew("at_base_plus_2epsilon");
    
    orderedIndexInsert(ops, idx, base, ele1);
    orderedIndexInsert(ops, idx, base + epsilon, ele2);
    orderedIndexInsert(ops, idx, base + 2*epsilon, ele3);
    
    /* Query with exclusive bounds (base, base+2*epsilon) - should only get middle element */
    unsigned long deleted = orderedIndexDeleteRangeByScore(ops, idx, base, base + 2*epsilon, 1, 1);
    TEST_ASSERT(deleted == 1);
    TEST_ASSERT(orderedIndexLength(ops, idx) == 2);
    
    /* Verify remaining elements */
    OrderedIndexPosition *node = orderedIndexFirst(ops, idx);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == base);
    node = orderedIndexNext(ops, node);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == base + 2*epsilon);
    
    sdsfree(ele1); sdsfree(ele2); sdsfree(ele3);
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_special_double_values_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    const char *ptr;
    size_t len;
    
    /* Test ±infinity */
    sds neg_inf = sdsnew("neg_inf");
    sds pos_inf = sdsnew("pos_inf");
    sds zero = sdsnew("zero");
    sds one = sdsnew("one");
    
    orderedIndexInsert(ops, idx, -INFINITY, neg_inf);
    orderedIndexInsert(ops, idx, INFINITY, pos_inf);
    orderedIndexInsert(ops, idx, 0.0, zero);
    orderedIndexInsert(ops, idx, 1.0, one);
    
    /* Verify ordering: -inf, 0, 1, +inf */
    OrderedIndexPosition *node = orderedIndexFirst(ops, idx);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == -INFINITY);
    node = orderedIndexNext(ops, node);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == 0.0);
    node = orderedIndexNext(ops, node);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == 1.0);
    node = orderedIndexNext(ops, node);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == INFINITY);
    
    sdsfree(neg_inf); sdsfree(pos_inf); sdsfree(zero); sdsfree(one);
    orderedIndexFree(ops, idx);
    
    /* Test +0.0 vs -0.0 (should be treated as equal) */
    idx = orderedIndexCreate(ops);
    sds pos_zero = sdsnew("pos_zero");
    sds neg_zero = sdsnew("neg_zero");
    
    orderedIndexInsert(ops, idx, 0.0, pos_zero);
    orderedIndexInsert(ops, idx, -0.0, neg_zero);
    
    /* Both should be in the list, ordered lexicographically since scores are equal */
    TEST_ASSERT(orderedIndexLength(ops, idx) == 2);
    node = orderedIndexFirst(ops, idx);
    orderedIndexGetElementRaw(ops, node, &ptr, &len);
    TEST_ASSERT(len == 8 && memcmp(ptr, "neg_zero", 8) == 0);
    node = orderedIndexNext(ops, node);
    orderedIndexGetElementRaw(ops, node, &ptr, &len);
    TEST_ASSERT(len == 8 && memcmp(ptr, "pos_zero", 8) == 0);
    
    sdsfree(pos_zero); sdsfree(neg_zero);
    orderedIndexFree(ops, idx);
    
    /* Test denormalized double (very small number near zero) */
    idx = orderedIndexCreate(ops);
    double denorm = 1e-320; /* Denormalized double */
    sds denorm_ele = sdsnew("denorm");
    sds normal_ele = sdsnew("normal");
    
    orderedIndexInsert(ops, idx, denorm, denorm_ele);
    orderedIndexInsert(ops, idx, 1.0, normal_ele);
    
    /* Verify denormalized value is handled correctly */
    TEST_ASSERT(orderedIndexLength(ops, idx) == 2);
    node = orderedIndexFirst(ops, idx);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == denorm);
    TEST_ASSERT(orderedIndexGetScore(ops, node) < 1.0);
    
    sdsfree(denorm_ele); sdsfree(normal_ele);
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_find_nth_in_range_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    
    /* Insert elements with scores 0,2,4,6,8 */
    for (int i = 0; i < 5; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key%d", i);
        sds ele = sdsnew(buf);
        orderedIndexInsert(ops, idx, (double)(i * 2), ele);
        sdsfree(ele);
    }
    
    /* Find 1st element in range [2, 6] - should be score 2 */
    OrderedIndexPosition *node = orderedIndexFindNthInRange(ops, idx, 2.0, 6.0, 0, 0, 0);
    TEST_ASSERT(node != NULL);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == 2.0);
    
    /* Find 2nd element in range [2, 6] - should be score 4 */
    node = orderedIndexFindNthInRange(ops, idx, 2.0, 6.0, 0, 0, 1);
    TEST_ASSERT(node != NULL);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == 4.0);
    
    /* Find last element (negative index) */
    node = orderedIndexFindNthInRange(ops, idx, 2.0, 6.0, 0, 0, -1);
    TEST_ASSERT(node != NULL);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == 6.0);
    
    /* Out of range */
    node = orderedIndexFindNthInRange(ops, idx, 2.0, 6.0, 0, 0, 10);
    TEST_ASSERT(node == NULL);
    
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_edge_cases_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    
    /* Empty index operations */
    TEST_ASSERT(orderedIndexLength(ops, idx) == 0);
    TEST_ASSERT(orderedIndexFirst(ops, idx) == NULL);
    TEST_ASSERT(orderedIndexLast(ops, idx) == NULL);
    TEST_ASSERT(orderedIndexGetByRank(ops, idx, 1) == NULL);
    
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_delete_edge_cases_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    
    /* Delete only element */
    sds ele = sdsnew("only");
    OrderedIndexPosition *node = orderedIndexInsert(ops, idx, 1.0, ele);
    orderedIndexDelete(ops, idx, node);
    TEST_ASSERT(orderedIndexLength(ops, idx) == 0);
    TEST_ASSERT(orderedIndexFirst(ops, idx) == NULL);
    TEST_ASSERT(orderedIndexLast(ops, idx) == NULL);
    sdsfree(ele);
    
    /* Delete first element */
    for (int i = 0; i < 3; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key%d", i);
        sds e = sdsnew(buf);
        orderedIndexInsert(ops, idx, (double)i, e);
        sdsfree(e);
    }
    OrderedIndexPosition *first = orderedIndexFirst(ops, idx);
    orderedIndexDelete(ops, idx, first);
    TEST_ASSERT(orderedIndexLength(ops, idx) == 2);
    TEST_ASSERT(orderedIndexGetScore(ops, orderedIndexFirst(ops, idx)) == 1.0);
    
    /* Delete last element */
    OrderedIndexPosition *last = orderedIndexLast(ops, idx);
    orderedIndexDelete(ops, idx, last);
    TEST_ASSERT(orderedIndexLength(ops, idx) == 1);
    TEST_ASSERT(orderedIndexGetScore(ops, orderedIndexLast(ops, idx)) == 1.0);
    
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_rank_edge_cases_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    
    /* Insert 5 elements */
    for (int i = 0; i < 5; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key%d", i);
        sds ele = sdsnew(buf);
        orderedIndexInsert(ops, idx, (double)i, ele);
        sdsfree(ele);
    }
    
    /* Rank 0 returns header node (not NULL, not first element - implementation quirk)
     * We don't test this as it's an implementation detail that shouldn't be relied upon */
    
    /* Rank beyond length returns NULL */
    TEST_ASSERT(orderedIndexGetByRank(ops, idx, 6) == NULL);
    TEST_ASSERT(orderedIndexGetByRank(ops, idx, 100) == NULL);
    
    /* Valid boundary ranks */
    TEST_ASSERT(orderedIndexGetByRank(ops, idx, 1) != NULL);
    TEST_ASSERT(orderedIndexGetByRank(ops, idx, 5) != NULL);
    
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_duplicate_insert_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    
    /* Insert same score+element twice */
    sds ele1 = sdsnew("duplicate");
    sds ele2 = sdsnew("duplicate");
    OrderedIndexPosition *node1 = orderedIndexInsert(ops, idx, 1.0, ele1);
    OrderedIndexPosition *node2 = orderedIndexInsert(ops, idx, 1.0, ele2);
    
    /* Should have 2 nodes (duplicates allowed) */
    TEST_ASSERT(orderedIndexLength(ops, idx) == 2);
    TEST_ASSERT(node1 != node2);
    
    sdsfree(ele1); sdsfree(ele2);
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_update_score_edge_cases_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    
    /* Insert elements */
    for (int i = 0; i < 5; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key%d", i);
        sds ele = sdsnew(buf);
        orderedIndexInsert(ops, idx, (double)i, ele);
        sdsfree(ele);
    }
    
    /* Update first element to move backward (should stay first) */
    OrderedIndexPosition *first = orderedIndexFirst(ops, idx);
    OrderedIndexPosition *updated = orderedIndexUpdateScore(ops, idx, first, -1.0);
    TEST_ASSERT(orderedIndexGetScore(ops, updated) == -1.0);
    TEST_ASSERT(orderedIndexFirst(ops, idx) == updated);
    
    /* Update last element to move forward */
    OrderedIndexPosition *last = orderedIndexLast(ops, idx);
    updated = orderedIndexUpdateScore(ops, idx, last, 10.0);
    TEST_ASSERT(orderedIndexGetScore(ops, updated) == 10.0);
    TEST_ASSERT(orderedIndexLast(ops, idx) == updated);
    
    /* Update middle element to move backward */
    OrderedIndexPosition *middle = orderedIndexGetByRank(ops, idx, 3);
    double old_score = orderedIndexGetScore(ops, middle);
    updated = orderedIndexUpdateScore(ops, idx, middle, 0.5);
    TEST_ASSERT(orderedIndexGetScore(ops, updated) == 0.5);
    TEST_ASSERT(orderedIndexGetScore(ops, updated) < old_score);
    
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_range_delete_edge_cases_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    
    /* Insert 10 elements */
    for (int i = 0; i < 10; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key%d", i);
        sds ele = sdsnew(buf);
        orderedIndexInsert(ops, idx, (double)i, ele);
        sdsfree(ele);
    }
    
    /* Delete empty range (min > max) */
    unsigned long deleted = orderedIndexDeleteRangeByScore(ops, idx, 5.0, 4.0, 0, 0);
    TEST_ASSERT(deleted == 0);
    TEST_ASSERT(orderedIndexLength(ops, idx) == 10);
    
    /* Delete range with no matches */
    deleted = orderedIndexDeleteRangeByScore(ops, idx, 10.5, 11.5, 0, 0);
    TEST_ASSERT(deleted == 0);
    TEST_ASSERT(orderedIndexLength(ops, idx) == 10);
    
    /* Delete first elements by rank */
    deleted = orderedIndexDeleteRangeByRank(ops, idx, 1, 2);
    TEST_ASSERT(deleted == 2);
    TEST_ASSERT(orderedIndexGetScore(ops, orderedIndexFirst(ops, idx)) == 2.0);
    
    /* Delete last elements by rank */
    unsigned long len = orderedIndexLength(ops, idx);
    deleted = orderedIndexDeleteRangeByRank(ops, idx, len - 1, len);
    TEST_ASSERT(deleted == 2);
    TEST_ASSERT(orderedIndexGetScore(ops, orderedIndexLast(ops, idx)) == 7.0);
    
    /* Delete entire remaining index by score */
    deleted = orderedIndexDeleteRangeByScore(ops, idx, -100.0, 100.0, 0, 0);
    TEST_ASSERT(deleted == 6);
    TEST_ASSERT(orderedIndexLength(ops, idx) == 0);
    
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_find_nth_edge_cases_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    
    /* Insert elements */
    for (int i = 0; i < 10; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key%d", i);
        sds ele = sdsnew(buf);
        orderedIndexInsert(ops, idx, (double)i, ele);
        sdsfree(ele);
    }
    
    /* Empty range (min > max) */
    OrderedIndexPosition *node = orderedIndexFindNthInRange(ops, idx, 5.0, 4.0, 0, 0, 0);
    TEST_ASSERT(node == NULL);
    
    /* Range with no matches */
    node = orderedIndexFindNthInRange(ops, idx, 10.5, 11.5, 0, 0, 0);
    TEST_ASSERT(node == NULL);
    
    /* Exclusive bounds */
    node = orderedIndexFindNthInRange(ops, idx, 2.0, 6.0, 1, 1, 0);
    TEST_ASSERT(node != NULL);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == 3.0);
    
    /* Negative index beyond range */
    node = orderedIndexFindNthInRange(ops, idx, 2.0, 6.0, 0, 0, -10);
    TEST_ASSERT(node == NULL);
    
    /* Second from last with negative index */
    node = orderedIndexFindNthInRange(ops, idx, 2.0, 6.0, 0, 0, -2);
    TEST_ASSERT(node != NULL);
    TEST_ASSERT(orderedIndexGetScore(ops, node) == 5.0);
    
    orderedIndexFree(ops, idx);
    return 0;
}

static int test_traversal_edge_cases_generic(const OrderedIndexOps *ops) {
    OrderedIndex *idx = orderedIndexCreate(ops);
    
    /* Insert single element */
    sds ele = sdsnew("single");
    OrderedIndexPosition *node = orderedIndexInsert(ops, idx, 1.0, ele);
    
    /* next() on last element should return NULL */
    TEST_ASSERT(orderedIndexNext(ops, node) == NULL);
    
    /* prev() on first element should return NULL */
    TEST_ASSERT(orderedIndexPrev(ops, node) == NULL);
    
    sdsfree(ele);
    orderedIndexFree(ops, idx);
    return 0;
}

/* Test wrappers for skiplist implementation */

int test_ordered_index_skiplist_create_free(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_create_free_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_insert_single(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_insert_single_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_insert_multiple(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_insert_multiple_ordered_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_duplicate_scores(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_duplicate_scores_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_rank_operations(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_rank_operations_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_delete(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_delete_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_update_score(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_update_score_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_delete_range_by_score(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_delete_range_by_score_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_delete_range_by_rank(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_delete_range_by_rank_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_find_nth_in_range(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_find_nth_in_range_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_edge_cases(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_edge_cases_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_delete_edge_cases(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_delete_edge_cases_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_rank_edge_cases(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_rank_edge_cases_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_duplicate_insert(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_duplicate_insert_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_update_score_edge_cases(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_update_score_edge_cases_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_range_delete_edge_cases(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_range_delete_edge_cases_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_find_nth_edge_cases(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_find_nth_edge_cases_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_traversal_edge_cases(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_traversal_edge_cases_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_mixed_operations_rank_integrity(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_mixed_operations_rank_integrity_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_backward_traversal_after_deletions(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_backward_traversal_after_deletions_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_lexicographic_edge_cases(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_lexicographic_edge_cases_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_range_boundary_precision(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_range_boundary_precision_generic(&skiplistOrderedIndexOps);

}

int test_ordered_index_skiplist_special_double_values(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    return test_special_double_values_generic(&skiplistOrderedIndexOps);

}
