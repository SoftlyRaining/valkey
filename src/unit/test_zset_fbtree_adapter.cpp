/*
 * Copyright (c) Valkey Contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Unit tests for zset_fbtree_adapter.c - score normalization and packing
 */

#include "generated_wrappers.hpp"

#include <cstring>
#include <cmath>
#include <cfloat>

extern "C" {
#include "fbtree_ordered_index.h"
#include "sds.h"
#include "zmalloc.h"

/* Test wrappers from zset_fbtree_adapter.c */
uint64_t scoreToSortable_test(double score);
double sortableToScore_test(uint64_t be);
sds packScoreElement_test(double score, const_sds ele);
const char *unpackElement_test(const_sds packed, size_t *len);
double unpackScore_test(const_sds packed);
}

/* Helper: compare two sortable values lexicographically (as big-endian bytes) */
static int sortableCmp(uint64_t a, uint64_t b) {
    return memcmp(&a, &b, sizeof(a));
}

/* ========== Score Normalization Tests ========== */

TEST(ZsetFbtreeAdapterTest, score_roundtrip_positive) {
    double scores[] = {0.0, 1.0, 1.5, 100.0, 1e10, DBL_MAX};
    for (size_t i = 0; i < sizeof(scores)/sizeof(scores[0]); i++) {
        uint64_t sortable = scoreToSortable_test(scores[i]);
        double back = sortableToScore_test(sortable);
        ASSERT_EQ(back, scores[i]);
    }
}

TEST(ZsetFbtreeAdapterTest, score_roundtrip_negative) {
    double scores[] = {-0.0, -1.0, -1.5, -100.0, -1e10, -DBL_MAX};
    for (size_t i = 0; i < sizeof(scores)/sizeof(scores[0]); i++) {
        uint64_t sortable = scoreToSortable_test(scores[i]);
        double back = sortableToScore_test(sortable);
        ASSERT_EQ(back, scores[i]);
    }
}

TEST(ZsetFbtreeAdapterTest, score_ordering_special) {
    double scores[] = {-INFINITY, -1.0, 0.0, 1.0, INFINITY};
    size_t n = sizeof(scores)/sizeof(scores[0]);
    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < n; j++) {
            uint64_t a = scoreToSortable_test(scores[i]);
            uint64_t b = scoreToSortable_test(scores[j]);
            int scmp = sortableCmp(a, b);
            ASSERT_EQ((scmp < 0), (scores[i] < scores[j]));
            ASSERT_EQ((scmp > 0), (scores[i] > scores[j]));
        }
    }
}

TEST(ZsetFbtreeAdapterTest, score_ordering_signed_zero) {
    double neg_small = -1e-300, neg_zero = -0.0, pos_zero = 0.0, pos_small = 1e-300;
    uint64_t sn = scoreToSortable_test(neg_small);
    uint64_t snz = scoreToSortable_test(neg_zero);
    uint64_t spz = scoreToSortable_test(pos_zero);
    uint64_t sp = scoreToSortable_test(pos_small);
    ASSERT_LT(sortableCmp(sn, snz), 0);
    ASSERT_LT(sortableCmp(snz, spz), 0);
    ASSERT_LT(sortableCmp(spz, sp), 0);
}

TEST(ZsetFbtreeAdapterTest, score_ordering_positive) {
    double scores[] = {0.0, 0.1, 1.0, 10.0, 100.0, DBL_MAX};
    for (size_t i = 0; i < sizeof(scores)/sizeof(scores[0]) - 1; i++) {
        uint64_t a = scoreToSortable_test(scores[i]);
        uint64_t b = scoreToSortable_test(scores[i+1]);
        ASSERT_EQ((sortableCmp(a, b) < 0), (scores[i] < scores[i+1]));
    }
}

TEST(ZsetFbtreeAdapterTest, score_ordering_negative) {
    double scores[] = {-DBL_MAX, -100.0, -10.0, -1.0, -0.1};
    for (size_t i = 0; i < sizeof(scores)/sizeof(scores[0]) - 1; i++) {
        uint64_t a = scoreToSortable_test(scores[i]);
        uint64_t b = scoreToSortable_test(scores[i+1]);
        ASSERT_EQ((sortableCmp(a, b) < 0), (scores[i] < scores[i+1]));
    }
}

TEST(ZsetFbtreeAdapterTest, score_ordering_mixed) {
    double scores[] = {-INFINITY, -DBL_MAX, -1.0, -0.1, 0.0, 0.1, 1.0, DBL_MAX, INFINITY};
    for (size_t i = 0; i < sizeof(scores)/sizeof(scores[0]) - 1; i++) {
        uint64_t a = scoreToSortable_test(scores[i]);
        uint64_t b = scoreToSortable_test(scores[i+1]);
        ASSERT_EQ((sortableCmp(a, b) < 0), (scores[i] < scores[i+1]));
    }
}

TEST(ZsetFbtreeAdapterTest, score_ordering_subnormal) {
    double scores[] = {-DBL_MIN, 0.0, DBL_MIN, DBL_MIN * 2};
    for (size_t i = 0; i < sizeof(scores)/sizeof(scores[0]) - 1; i++) {
        uint64_t a = scoreToSortable_test(scores[i]);
        uint64_t b = scoreToSortable_test(scores[i+1]);
        ASSERT_EQ((sortableCmp(a, b) < 0), (scores[i] < scores[i+1]));
    }
}

/* ========== Packing/Unpacking Tests ========== */

TEST(ZsetFbtreeAdapterTest, pack_unpack_basic) {
    size_t mem_before = zmalloc_used_memory();

    sds ele = sdsnew("hello");
    sds packed = packScoreElement_test(42.5, ele);

    size_t len;
    const char *unpacked_ele = unpackElement_test(packed, &len);
    double unpacked_score = unpackScore_test(packed);

    ASSERT_EQ(unpacked_score, 42.5);
    ASSERT_EQ(len, 5u);
    ASSERT_EQ(memcmp(unpacked_ele, "hello", 5), 0);

    sdsfree(ele);
    sdsfree(packed);
    ASSERT_EQ(zmalloc_used_memory(), mem_before);
}

TEST(ZsetFbtreeAdapterTest, pack_unpack_empty_element) {
    size_t mem_before = zmalloc_used_memory();

    sds ele = sdsempty();
    sds packed = packScoreElement_test(0.0, ele);

    size_t len;
    unpackElement_test(packed, &len);
    double unpacked_score = unpackScore_test(packed);

    ASSERT_EQ(unpacked_score, 0.0);
    ASSERT_EQ(len, 0u);

    sdsfree(ele);
    sdsfree(packed);
    ASSERT_EQ(zmalloc_used_memory(), mem_before);
}

TEST(ZsetFbtreeAdapterTest, pack_unpack_binary_element) {
    size_t mem_before = zmalloc_used_memory();

    unsigned char binary[] = {0x00, 0x01, 0x02, 0xff, 0x00, 0xfe};
    sds ele = sdsnewlen(binary, sizeof(binary));
    sds packed = packScoreElement_test(-999.0, ele);

    size_t len;
    const char *unpacked_ele = unpackElement_test(packed, &len);
    double unpacked_score = unpackScore_test(packed);

    ASSERT_EQ(unpacked_score, -999.0);
    ASSERT_EQ(len, sizeof(binary));
    ASSERT_EQ(memcmp(unpacked_ele, binary, sizeof(binary)), 0);

    sdsfree(ele);
    sdsfree(packed);
    ASSERT_EQ(zmalloc_used_memory(), mem_before);
}

TEST(ZsetFbtreeAdapterTest, pack_unpack_negative_score) {
    size_t mem_before = zmalloc_used_memory();

    sds ele = sdsnew("neg");
    sds packed = packScoreElement_test(-123.456, ele);

    double unpacked_score = unpackScore_test(packed);
    ASSERT_EQ(unpacked_score, -123.456);

    sdsfree(ele);
    sdsfree(packed);
    ASSERT_EQ(zmalloc_used_memory(), mem_before);
}

TEST(ZsetFbtreeAdapterTest, pack_unpack_infinity) {
    size_t mem_before = zmalloc_used_memory();

    sds ele = sdsnew("inf");

    sds packed_pos = packScoreElement_test(INFINITY, ele);
    ASSERT_EQ(unpackScore_test(packed_pos), INFINITY);

    sds packed_neg = packScoreElement_test(-INFINITY, ele);
    ASSERT_EQ(unpackScore_test(packed_neg), -INFINITY);

    sdsfree(ele);
    sdsfree(packed_pos);
    sdsfree(packed_neg);
    ASSERT_EQ(zmalloc_used_memory(), mem_before);
}

/* ========== Lexicographic Ordering Tests ========== */

TEST(ZsetFbtreeAdapterTest, packed_lexicographic_order) {
    size_t mem_before = zmalloc_used_memory();

    sds ele = sdsnew("x");
    sds p1 = packScoreElement_test(-10.0, ele);
    sds p2 = packScoreElement_test(0.0, ele);
    sds p3 = packScoreElement_test(10.0, ele);

    ASSERT_LT(memcmp(p1, p2, 8), 0);
    ASSERT_LT(memcmp(p2, p3, 8), 0);
    ASSERT_LT(memcmp(p1, p3, 8), 0);

    sdsfree(ele);
    sdsfree(p1);
    sdsfree(p2);
    sdsfree(p3);
    ASSERT_EQ(zmalloc_used_memory(), mem_before);
}

TEST(ZsetFbtreeAdapterTest, packed_same_score_element_order) {
    size_t mem_before = zmalloc_used_memory();

    sds e1 = sdsnew("aaa");
    sds e2 = sdsnew("bbb");
    sds e3 = sdsnew("ccc");

    sds p1 = packScoreElement_test(5.0, e1);
    sds p2 = packScoreElement_test(5.0, e2);
    sds p3 = packScoreElement_test(5.0, e3);

    ASSERT_LT(sdscmp(p1, p2), 0);
    ASSERT_LT(sdscmp(p2, p3), 0);

    sdsfree(e1); sdsfree(e2); sdsfree(e3);
    sdsfree(p1); sdsfree(p2); sdsfree(p3);
    ASSERT_EQ(zmalloc_used_memory(), mem_before);
}
