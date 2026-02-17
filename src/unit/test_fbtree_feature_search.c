/* Unit tests for featureSearchSIMD - tests actual implementations */

#include "../config.h"
#include "../zmalloc.h"
#include "test_help.h"
#include <stdio.h>
#include <string.h>

/* Constants from fbtree_ordered_index.c */
#define FEATURE_SIZE 4
#define FEATURE_ROW_SIZE 64
#define FEATURE_BIAS 0x80

#define BIASED(x) ((char)((unsigned char)(x) ^ FEATURE_BIAS))

#define TEST_SETUP()                               \
    UNUSED(argc);                                  \
    UNUSED(argv);                                  \
    UNUSED(flags);                                 \
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE]; \
    initFeatures(features)

static void initFeatures(char features[FEATURE_SIZE][FEATURE_ROW_SIZE]) {
    for (int j = 0; j < FEATURE_SIZE; j++)
        for (int i = 0; i < FEATURE_ROW_SIZE; i++)
            features[j][i] = BIASED(0);
}

/* Declare test wrappers - these call actual code in fbtree_ordered_index.c */
extern void featureSearchSIMD_test_wrapper(char features[FEATURE_SIZE][FEATURE_ROW_SIZE],
                                           int num_keys,
                                           const unsigned char target[FEATURE_SIZE],
                                           int *out_left,
                                           int *out_right);

#if HAVE_X86_SIMD
extern void featureSearchSIMD_avx2_test_wrapper(char features[FEATURE_SIZE][FEATURE_ROW_SIZE],
                                                int num_keys,
                                                const unsigned char target[FEATURE_SIZE],
                                                int *out_left,
                                                int *out_right);

extern void featureSearchSIMD_sse2_test_wrapper(char features[FEATURE_SIZE][FEATURE_ROW_SIZE],
                                                int num_keys,
                                                const unsigned char target[FEATURE_SIZE],
                                                int *out_left,
                                                int *out_right);

#endif

extern void featureSearchSIMD_scalar_test_wrapper(char features[FEATURE_SIZE][FEATURE_ROW_SIZE],
                                                  int num_keys,
                                                  const unsigned char target[FEATURE_SIZE],
                                                  int *out_left,
                                                  int *out_right);

typedef void (*FeatureSearchFn)(char features[FEATURE_SIZE][FEATURE_ROW_SIZE],
                                int num_keys,
                                const unsigned char target[FEATURE_SIZE],
                                int *out_left,
                                int *out_right);

/* Test all available implementations produce identical results against scalar (truth) */
static int testAllImpls(char features[FEATURE_SIZE][FEATURE_ROW_SIZE],
                        int num_keys,
                        const unsigned char target[FEATURE_SIZE]) {
    int scalar_left, scalar_right;
    featureSearchSIMD_scalar_test_wrapper(features, num_keys, target, &scalar_left, &scalar_right);

    int def_left, def_right;
    featureSearchSIMD_test_wrapper(features, num_keys, target, &def_left, &def_right);
    TEST_ASSERT_MESSAGE("default mismatch", def_left == scalar_left && def_right == scalar_right);

#if HAVE_X86_SIMD
    int sse_left, sse_right;
    featureSearchSIMD_sse2_test_wrapper(features, num_keys, target, &sse_left, &sse_right);
    TEST_ASSERT_MESSAGE("sse2 mismatch", sse_left == scalar_left && sse_right == scalar_right);

    if (__builtin_cpu_supports("avx2")) {
        int avx_left, avx_right;
        featureSearchSIMD_avx2_test_wrapper(features, num_keys, target, &avx_left, &avx_right);
        TEST_ASSERT_MESSAGE("avx2 mismatch", avx_left == scalar_left && avx_right == scalar_right);
    }
#endif
    return 0;
}

/* ==========================================================================
 * Expected values test - demonstrates the semantics of featureSearchSIMD.
 * Range [left, right) = candidate children that might contain target.
 * ========================================================================== */

int test_feature_search_expected_values(int argc, char **argv, int flags) {
    TEST_SETUP();
    features[0][0] = BIASED(0x20);
    features[0][1] = BIASED(0x40);
    features[0][2] = BIASED(0x40);
    features[0][3] = BIASED(0x40);
    features[0][4] = BIASED(0x60);
    features[0][5] = BIASED(0x80);
    int left, right;

    /* Case 1: Target before all - empty range (no candidates) */
    unsigned char t1[FEATURE_SIZE] = {0x10, 0, 0, 0};
    featureSearchSIMD_scalar_test_wrapper(features, 6, t1, &left, &right);
    TEST_ASSERT_MESSAGE("before all: left", left == 0);
    TEST_ASSERT_MESSAGE("before all: right", right == 0);
    if (testAllImpls(features, 6, t1)) return 1;

    /* Case 2: Target after all - empty range (no candidates) */
    unsigned char t2[FEATURE_SIZE] = {0x90, 0, 0, 0};
    featureSearchSIMD_scalar_test_wrapper(features, 6, t2, &left, &right);
    TEST_ASSERT_MESSAGE("after all: left", left == 6);
    TEST_ASSERT_MESSAGE("after all: right", right == 6);
    if (testAllImpls(features, 6, t2)) return 1;

    /* Case 3: Exact match on unique value - 1 candidate */
    unsigned char t3[FEATURE_SIZE] = {0x20, 0, 0, 0};
    featureSearchSIMD_scalar_test_wrapper(features, 6, t3, &left, &right);
    TEST_ASSERT_MESSAGE("unique match: left", left == 0);
    TEST_ASSERT_MESSAGE("unique match: right", right == 1);
    if (testAllImpls(features, 6, t3)) return 1;

    /* Case 4: Match on duplicates - few candidates (3 matches) */
    unsigned char t4[FEATURE_SIZE] = {0x40, 0, 0, 0};
    featureSearchSIMD_scalar_test_wrapper(features, 6, t4, &left, &right);
    TEST_ASSERT_MESSAGE("duplicates: left", left == 1);
    TEST_ASSERT_MESSAGE("duplicates: right", right == 4);
    if (testAllImpls(features, 6, t4)) return 1;

    /* Case 5: All same features - all candidates (nothing narrowed) */
    for (int i = 0; i < 5; i++) features[0][i] = BIASED(0x50);
    unsigned char t5[FEATURE_SIZE] = {0x50, 0, 0, 0};
    featureSearchSIMD_scalar_test_wrapper(features, 5, t5, &left, &right);
    TEST_ASSERT_MESSAGE("all same: left", left == 0);
    TEST_ASSERT_MESSAGE("all same: right", right == 5);
    if (testAllImpls(features, 5, t5)) return 1;

    /* Case 6: Between values - empty range (target 0x50 between 0x40 and 0x60) */
    features[0][0] = BIASED(0x20);
    features[0][1] = BIASED(0x40);
    features[0][2] = BIASED(0x60);
    features[0][3] = BIASED(0x80);
    unsigned char t6[FEATURE_SIZE] = {0x50, 0, 0, 0};
    featureSearchSIMD_scalar_test_wrapper(features, 4, t6, &left, &right);
    TEST_ASSERT_MESSAGE("between: left", left == 2);
    TEST_ASSERT_MESSAGE("between: right", right == 2);
    if (testAllImpls(features, 4, t6)) return 1;

    return 0;
}

/* ==========================================================================
 * Edge cases - empty, single element, two elements
 * ========================================================================== */

int test_feature_search_empty(int argc, char **argv, int flags) {
    TEST_SETUP();
    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};
    return testAllImpls(features, 0, target);
}

int test_feature_search_single_match(int argc, char **argv, int flags) {
    TEST_SETUP();
    features[0][0] = BIASED(0x50);
    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};
    return testAllImpls(features, 1, target);
}

int test_feature_search_single_less(int argc, char **argv, int flags) {
    TEST_SETUP();
    features[0][0] = BIASED(0x50);
    unsigned char target[FEATURE_SIZE] = {0x40, 0, 0, 0};
    return testAllImpls(features, 1, target);
}

int test_feature_search_single_greater(int argc, char **argv, int flags) {
    TEST_SETUP();
    features[0][0] = BIASED(0x50);
    unsigned char target[FEATURE_SIZE] = {0x60, 0, 0, 0};
    return testAllImpls(features, 1, target);
}

int test_feature_search_two_elements(int argc, char **argv, int flags) {
    TEST_SETUP();
    features[0][0] = BIASED(0x30);
    features[0][1] = BIASED(0x70);
    unsigned char targets[][FEATURE_SIZE] = {
        {0x20, 0, 0, 0}, {0x30, 0, 0, 0}, {0x50, 0, 0, 0}, {0x70, 0, 0, 0}, {0x80, 0, 0, 0}};
    for (int i = 0; i < 5; i++) {
        int ret = testAllImpls(features, 2, targets[i]);
        if (ret) return ret;
    }
    return 0;
}

/* ==========================================================================
 * Basic scenarios - small arrays, before/after all, not found
 * ========================================================================== */

int test_feature_search_three_elements(int argc, char **argv, int flags) {
    TEST_SETUP();
    features[0][0] = BIASED(0x30);
    features[0][1] = BIASED(0x50);
    features[0][2] = BIASED(0x70);
    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};
    return testAllImpls(features, 3, target);
}

int test_feature_search_before_all(int argc, char **argv, int flags) {
    TEST_SETUP();
    features[0][0] = BIASED(0x30);
    features[0][1] = BIASED(0x50);
    features[0][2] = BIASED(0x70);
    unsigned char target[FEATURE_SIZE] = {0x20, 0, 0, 0};
    return testAllImpls(features, 3, target);
}

int test_feature_search_after_all(int argc, char **argv, int flags) {
    TEST_SETUP();
    features[0][0] = BIASED(0x30);
    features[0][1] = BIASED(0x50);
    features[0][2] = BIASED(0x70);
    unsigned char target[FEATURE_SIZE] = {0x80, 0, 0, 0};
    return testAllImpls(features, 3, target);
}

int test_feature_search_not_found(int argc, char **argv, int flags) {
    TEST_SETUP();
    features[0][0] = BIASED(0x30);
    features[0][1] = BIASED(0x50);
    features[0][2] = BIASED(0x70);
    unsigned char target[FEATURE_SIZE] = {0x45, 0, 0, 0}; /* Between 0x30 and 0x50 */
    return testAllImpls(features, 3, target);
}

/* ==========================================================================
 * Duplicates - multiple matching features
 * ========================================================================== */

int test_feature_search_duplicates(int argc, char **argv, int flags) {
    TEST_SETUP();
    features[0][0] = BIASED(0x30);
    features[0][1] = BIASED(0x50);
    features[0][2] = BIASED(0x50);
    features[0][3] = BIASED(0x50);
    features[0][4] = BIASED(0x70);
    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};
    return testAllImpls(features, 5, target);
}

int test_feature_search_all_same(int argc, char **argv, int flags) {
    TEST_SETUP();
    for (int i = 0; i < 10; i++)
        features[0][i] = BIASED(0x50);
    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};
    return testAllImpls(features, 10, target);
}

/* ==========================================================================
 * Multi-byte features - tests all 4 feature bytes
 * ========================================================================== */

int test_feature_search_multibyte(int argc, char **argv, int flags) {
    TEST_SETUP();
    features[0][0] = BIASED(0x50);
    features[1][0] = BIASED(0x10);
    features[0][1] = BIASED(0x50);
    features[1][1] = BIASED(0x30);
    features[0][2] = BIASED(0x50);
    features[1][2] = BIASED(0x50);
    unsigned char target[FEATURE_SIZE] = {0x50, 0x30, 0, 0};
    return testAllImpls(features, 3, target);
}

int test_feature_search_multibyte_tiebreak(int argc, char **argv, int flags) {
    TEST_SETUP();
    for (int i = 0; i < 5; i++) {
        features[0][i] = BIASED(0x50);
        features[1][i] = BIASED(i * 0x20);
    }
    unsigned char target[FEATURE_SIZE] = {0x50, 0x40, 0, 0};
    return testAllImpls(features, 5, target);
}

int test_feature_search_all_bytes_matter(int argc, char **argv, int flags) {
    TEST_SETUP();
    for (int i = 0; i < 4; i++) {
        features[0][i] = BIASED(0x50);
        features[1][i] = BIASED(0x50);
        features[2][i] = BIASED(0x50);
        features[3][i] = BIASED(i * 0x30);
    }
    unsigned char target[FEATURE_SIZE] = {0x50, 0x50, 0x50, 0x60};
    return testAllImpls(features, 4, target);
}

/* Test that each byte position can be the deciding factor.
 * 4 children with features: all bytes equal except one differs.
 * Child 0: {0x50, 0x50, 0x50, 0x50} - all match
 * Child 1: differs only on byte being tested */
int test_feature_search_deciding_byte(int argc, char **argv, int flags) {
    TEST_SETUP();

    /* Test each byte position (1, 2, 3) as the deciding factor.
     * Byte 0 is already well-tested by single-byte tests. */
    for (int deciding_byte = 1; deciding_byte < FEATURE_SIZE; deciding_byte++) {
        initFeatures(features);

        /* Set up 4 children, all with 0x50 in all bytes */
        for (int child = 0; child < 4; child++)
            for (int byte = 0; byte < FEATURE_SIZE; byte++)
                features[byte][child] = BIASED(0x50);

        /* Make children differ only on the deciding byte */
        features[deciding_byte][0] = BIASED(0x20);
        features[deciding_byte][1] = BIASED(0x40);
        features[deciding_byte][2] = BIASED(0x60);
        features[deciding_byte][3] = BIASED(0x80);

        /* Target matches child 1 exactly */
        unsigned char target[FEATURE_SIZE] = {0x50, 0x50, 0x50, 0x50};
        target[deciding_byte] = 0x40;
        int ret = testAllImpls(features, 4, target);
        if (ret) return ret;

        /* Target between child 1 and 2 - should find empty range */
        target[deciding_byte] = 0x50;
        ret = testAllImpls(features, 4, target);
        if (ret) return ret;

        /* Target less than all on deciding byte */
        target[deciding_byte] = 0x10;
        ret = testAllImpls(features, 4, target);
        if (ret) return ret;

        /* Target greater than all on deciding byte */
        target[deciding_byte] = 0x90;
        ret = testAllImpls(features, 4, target);
        if (ret) return ret;
    }
    return 0;
}

/* ==========================================================================
 * Value boundaries - signed/unsigned edge cases
 * ========================================================================== */

int test_feature_search_high_values(int argc, char **argv, int flags) {
    TEST_SETUP();
    features[0][0] = BIASED(0x7F);
    features[0][1] = BIASED(0x80);
    features[0][2] = BIASED(0xFF);
    unsigned char target[FEATURE_SIZE] = {0x80, 0, 0, 0};
    return testAllImpls(features, 3, target);
}

int test_feature_search_boundary_values(int argc, char **argv, int flags) {
    TEST_SETUP();
    features[0][0] = BIASED(0x00);
    features[0][1] = BIASED(0x7F);
    features[0][2] = BIASED(0x80);
    features[0][3] = BIASED(0xFF);
    unsigned char targets[][FEATURE_SIZE] = {
        {0x00, 0, 0, 0}, {0x7F, 0, 0, 0}, {0x80, 0, 0, 0}, {0xFF, 0, 0, 0}};
    for (int i = 0; i < 4; i++) {
        int ret = testAllImpls(features, 4, targets[i]);
        if (ret) return ret;
    }
    return 0;
}

/* ==========================================================================
 * SIMD-specific - chunk boundaries, validity masks
 * ========================================================================== */

int test_feature_search_cross_chunk_boundary(int argc, char **argv, int flags) {
    TEST_SETUP();
    int boundaries[] = {16, 32}; /* SSE and AVX boundaries */
    int sizes[] = {20, 40};
    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};

    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < sizes[b]; i++)
            features[0][i] = BIASED(i < boundaries[b] ? 0x30 : 0x70);
        int ret = testAllImpls(features, sizes[b], target);
        if (ret) return ret;
    }
    return 0;
}

int test_feature_search_chunk_boundaries(int argc, char **argv, int flags) {
    TEST_SETUP();
    int sizes[] = {16, 32, 48};
    for (int s = 0; s < 3; s++) {
        for (int i = 0; i < sizes[s]; i++)
            features[0][i] = BIASED(i * 4);
        unsigned char target[FEATURE_SIZE] = {(unsigned char)(sizes[s] / 2 * 4), 0, 0, 0};
        int ret = testAllImpls(features, sizes[s], target);
        if (ret) return ret;
    }
    return 0;
}

int test_feature_search_last_in_chunk(int argc, char **argv, int flags) {
    TEST_SETUP();
    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};
    int positions[] = {15, 31, 47, 63};
    int sizes[] = {20, 40, 55, 64};

    for (int t = 0; t < 4; t++) {
        for (int i = 0; i < sizes[t]; i++)
            features[0][i] = BIASED(i == positions[t] ? 0x50 : 0x30);
        int ret = testAllImpls(features, sizes[t], target);
        if (ret) return ret;
    }
    return 0;
}

int test_feature_search_first_in_chunk(int argc, char **argv, int flags) {
    TEST_SETUP();
    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};
    int positions[] = {16, 32, 48};
    int sizes[] = {20, 40, 55};

    for (int t = 0; t < 3; t++) {
        for (int i = 0; i < sizes[t]; i++)
            features[0][i] = BIASED(i == positions[t] ? 0x50 : 0x70);
        int ret = testAllImpls(features, sizes[t], target);
        if (ret) return ret;
    }
    return 0;
}

int test_feature_search_no_false_positives(int argc, char **argv, int flags) {
    TEST_SETUP();

    /* Set up: valid keys are all 0x30, but invalid positions have 0x50 */
    for (int i = 0; i < 64; i++)
        features[0][i] = BIASED(i < 20 ? 0x30 : 0x50);

    /* Search for 0x50 - should NOT find it since it's beyond num_keys=20 */
    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};
    return testAllImpls(features, 20, target);
}

int test_feature_search_duplicates_cross_chunk(int argc, char **argv, int flags) {
    TEST_SETUP();

    /* Duplicates from position 14 to 18 (crosses chunk 0/1 boundary) */
    for (int i = 0; i < 30; i++)
        features[0][i] = BIASED(i < 14 ? 0x30 : (i < 19 ? 0x50 : 0x70));

    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};
    return testAllImpls(features, 30, target);
}

/* ==========================================================================
 * Realistic sizes - full row, typical node size
 * ========================================================================== */

int test_feature_search_full_row(int argc, char **argv, int flags) {
    TEST_SETUP();
    for (int i = 0; i < 64; i++)
        features[0][i] = BIASED(i * 3);
    unsigned char target[FEATURE_SIZE] = {96, 0, 0, 0};
    return testAllImpls(features, 64, target);
}

int test_feature_search_typical_node_size(int argc, char **argv, int flags) {
    TEST_SETUP();
    for (int i = 0; i < 61; i++)
        features[0][i] = BIASED(i * 4);
    unsigned char targets[][FEATURE_SIZE] = {
        {0, 0, 0, 0},
        {60, 0, 0, 0},
        {64, 0, 0, 0},
        {124, 0, 0, 0},
        {128, 0, 0, 0},
        {188, 0, 0, 0},
        {192, 0, 0, 0},
        {240, 0, 0, 0},
    };
    for (int i = 0; i < 8; i++) {
        int ret = testAllImpls(features, 61, targets[i]);
        if (ret) return ret;
    }
    return 0;
}
