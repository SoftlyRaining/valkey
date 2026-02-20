/* Unit tests for zset_fbtree_adapter.c - score normalization and packing */

#include <string.h>
#include <math.h>
#include <float.h>
#include "../fbtree_ordered_index.h"
#include "../sds.h"
#include "../zmalloc.h"
#include "test_help.h"

/* Test wrappers from zset_fbtree_adapter.c */
uint64_t scoreToSortable_test(double score);
double sortableToScore_test(uint64_t be);
sds packScoreElement_test(double score, const_sds ele);
const char *unpackElement_test(const_sds packed, size_t *len);
double unpackScore_test(const_sds packed);

/* Helper: compare two sortable values lexicographically (as big-endian bytes) */
static int sortableCmp(uint64_t a, uint64_t b) {
    /* Values are already big-endian, so memcmp gives correct lexicographic order */
    return memcmp(&a, &b, sizeof(a));
}

/* ========== Score Normalization Tests ========== */

int test_score_roundtrip_positive(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    double scores[] = {0.0, 1.0, 1.5, 100.0, 1e10, DBL_MAX};
    for (size_t i = 0; i < sizeof(scores)/sizeof(scores[0]); i++) {
        uint64_t sortable = scoreToSortable_test(scores[i]);
        double back = sortableToScore_test(sortable);
        TEST_ASSERT(back == scores[i]);
    }
    return 0;
}

int test_score_roundtrip_negative(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    double scores[] = {-0.0, -1.0, -1.5, -100.0, -1e10, -DBL_MAX};
    for (size_t i = 0; i < sizeof(scores)/sizeof(scores[0]); i++) {
        uint64_t sortable = scoreToSortable_test(scores[i]);
        double back = sortableToScore_test(sortable);
        /* -0.0 == 0.0 in C, but bit pattern differs - check value equality */
        TEST_ASSERT(back == scores[i]);
    }
    return 0;
}

int test_score_ordering_special(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    /* Special values: -inf, negative, zero, positive, +inf */
    double scores[] = {-INFINITY, -1.0, 0.0, 1.0, INFINITY};
    size_t n = sizeof(scores)/sizeof(scores[0]);
    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < n; j++) {
            uint64_t a = scoreToSortable_test(scores[i]);
            uint64_t b = scoreToSortable_test(scores[j]);
            int scmp = sortableCmp(a, b);
            /* Sortable comparison should match double < and > */
            TEST_ASSERT((scmp < 0) == (scores[i] < scores[j]));
            TEST_ASSERT((scmp > 0) == (scores[i] > scores[j]));
        }
    }
    return 0;
}

/* -0.0 and +0.0 have different bit patterns but compare equal with < and >.
 * Our encoding sorts them adjacently between negatives and positives,
 * so any range including 0 will include both. This is compatible with zset. */
int test_score_ordering_signed_zero(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    double neg_small = -1e-300, neg_zero = -0.0, pos_zero = 0.0, pos_small = 1e-300;
    uint64_t sn = scoreToSortable_test(neg_small);
    uint64_t snz = scoreToSortable_test(neg_zero);
    uint64_t spz = scoreToSortable_test(pos_zero);
    uint64_t sp = scoreToSortable_test(pos_small);
    /* Both zeros are adjacent and between negatives and positives */
    TEST_ASSERT(sortableCmp(sn, snz) < 0);
    TEST_ASSERT(sortableCmp(snz, spz) < 0);
    TEST_ASSERT(sortableCmp(spz, sp) < 0);
    return 0;
}

int test_score_ordering_positive(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    double scores[] = {0.0, 0.1, 1.0, 10.0, 100.0, DBL_MAX};
    for (size_t i = 0; i < sizeof(scores)/sizeof(scores[0]) - 1; i++) {
        uint64_t a = scoreToSortable_test(scores[i]);
        uint64_t b = scoreToSortable_test(scores[i+1]);
        /* Sortable order must match double order */
        TEST_ASSERT((sortableCmp(a, b) < 0) == (scores[i] < scores[i+1]));
    }
    return 0;
}

int test_score_ordering_negative(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    double scores[] = {-DBL_MAX, -100.0, -10.0, -1.0, -0.1};
    for (size_t i = 0; i < sizeof(scores)/sizeof(scores[0]) - 1; i++) {
        uint64_t a = scoreToSortable_test(scores[i]);
        uint64_t b = scoreToSortable_test(scores[i+1]);
        TEST_ASSERT((sortableCmp(a, b) < 0) == (scores[i] < scores[i+1]));
    }
    return 0;
}

int test_score_ordering_mixed(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    double scores[] = {-INFINITY, -DBL_MAX, -1.0, -0.1, 0.0, 0.1, 1.0, DBL_MAX, INFINITY};
    for (size_t i = 0; i < sizeof(scores)/sizeof(scores[0]) - 1; i++) {
        uint64_t a = scoreToSortable_test(scores[i]);
        uint64_t b = scoreToSortable_test(scores[i+1]);
        TEST_ASSERT((sortableCmp(a, b) < 0) == (scores[i] < scores[i+1]));
    }
    return 0;
}

int test_score_ordering_subnormal(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    double scores[] = {-DBL_MIN, 0.0, DBL_MIN, DBL_MIN * 2};
    for (size_t i = 0; i < sizeof(scores)/sizeof(scores[0]) - 1; i++) {
        uint64_t a = scoreToSortable_test(scores[i]);
        uint64_t b = scoreToSortable_test(scores[i+1]);
        TEST_ASSERT((sortableCmp(a, b) < 0) == (scores[i] < scores[i+1]));
    }
    return 0;
}

/* ========== Packing/Unpacking Tests ========== */

int test_pack_unpack_basic(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t mem_before = zmalloc_used_memory();
    
    sds ele = sdsnew("hello");
    sds packed = packScoreElement_test(42.5, ele);
    
    /* Unpack and verify */
    size_t len;
    const char *unpacked_ele = unpackElement_test(packed, &len);
    double unpacked_score = unpackScore_test(packed);
    
    TEST_ASSERT(unpacked_score == 42.5);
    TEST_ASSERT(len == 5);
    TEST_ASSERT(memcmp(unpacked_ele, "hello", 5) == 0);
    
    sdsfree(ele);
    sdsfree(packed);
    TEST_ASSERT(zmalloc_used_memory() == mem_before);
    return 0;
}

int test_pack_unpack_empty_element(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t mem_before = zmalloc_used_memory();
    
    sds ele = sdsempty();
    sds packed = packScoreElement_test(0.0, ele);
    
    size_t len;
    const char *unpacked_ele = unpackElement_test(packed, &len);
    double unpacked_score = unpackScore_test(packed);
    
    TEST_ASSERT(unpacked_score == 0.0);
    TEST_ASSERT(len == 0);
    UNUSED(unpacked_ele);
    
    sdsfree(ele);
    sdsfree(packed);
    TEST_ASSERT(zmalloc_used_memory() == mem_before);
    return 0;
}

int test_pack_unpack_binary_element(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t mem_before = zmalloc_used_memory();
    
    /* Element with null bytes and binary data */
    unsigned char binary[] = {0x00, 0x01, 0x02, 0xff, 0x00, 0xfe};
    sds ele = sdsnewlen(binary, sizeof(binary));
    sds packed = packScoreElement_test(-999.0, ele);
    
    size_t len;
    const char *unpacked_ele = unpackElement_test(packed, &len);
    double unpacked_score = unpackScore_test(packed);
    
    TEST_ASSERT(unpacked_score == -999.0);
    TEST_ASSERT(len == sizeof(binary));
    TEST_ASSERT(memcmp(unpacked_ele, binary, sizeof(binary)) == 0);
    
    sdsfree(ele);
    sdsfree(packed);
    TEST_ASSERT(zmalloc_used_memory() == mem_before);
    return 0;
}

int test_pack_unpack_negative_score(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t mem_before = zmalloc_used_memory();
    
    sds ele = sdsnew("neg");
    sds packed = packScoreElement_test(-123.456, ele);
    
    double unpacked_score = unpackScore_test(packed);
    TEST_ASSERT(unpacked_score == -123.456);
    
    sdsfree(ele);
    sdsfree(packed);
    TEST_ASSERT(zmalloc_used_memory() == mem_before);
    return 0;
}

int test_pack_unpack_infinity(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t mem_before = zmalloc_used_memory();
    
    sds ele = sdsnew("inf");
    
    sds packed_pos = packScoreElement_test(INFINITY, ele);
    TEST_ASSERT(unpackScore_test(packed_pos) == INFINITY);
    
    sds packed_neg = packScoreElement_test(-INFINITY, ele);
    TEST_ASSERT(unpackScore_test(packed_neg) == -INFINITY);
    
    sdsfree(ele);
    sdsfree(packed_pos);
    sdsfree(packed_neg);
    TEST_ASSERT(zmalloc_used_memory() == mem_before);
    return 0;
}

/* ========== Lexicographic Ordering Tests ========== */

int test_packed_lexicographic_order(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t mem_before = zmalloc_used_memory();
    
    /* Same element, different scores - should sort by score */
    sds ele = sdsnew("x");
    sds p1 = packScoreElement_test(-10.0, ele);
    sds p2 = packScoreElement_test(0.0, ele);
    sds p3 = packScoreElement_test(10.0, ele);
    
    /* memcmp should give correct ordering */
    TEST_ASSERT(memcmp(p1, p2, 8) < 0);
    TEST_ASSERT(memcmp(p2, p3, 8) < 0);
    TEST_ASSERT(memcmp(p1, p3, 8) < 0);
    
    sdsfree(ele);
    sdsfree(p1);
    sdsfree(p2);
    sdsfree(p3);
    TEST_ASSERT(zmalloc_used_memory() == mem_before);
    return 0;
}

int test_packed_same_score_element_order(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    size_t mem_before = zmalloc_used_memory();
    
    /* Same score, different elements - should sort by element */
    sds e1 = sdsnew("aaa");
    sds e2 = sdsnew("bbb");
    sds e3 = sdsnew("ccc");
    
    sds p1 = packScoreElement_test(5.0, e1);
    sds p2 = packScoreElement_test(5.0, e2);
    sds p3 = packScoreElement_test(5.0, e3);
    
    /* Full string comparison should sort by element after score prefix */
    TEST_ASSERT(sdscmp(p1, p2) < 0);
    TEST_ASSERT(sdscmp(p2, p3) < 0);
    
    sdsfree(e1); sdsfree(e2); sdsfree(e3);
    sdsfree(p1); sdsfree(p2); sdsfree(p3);
    TEST_ASSERT(zmalloc_used_memory() == mem_before);
    return 0;
}


