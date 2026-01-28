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

static void initFeatures(char features[FEATURE_SIZE][FEATURE_ROW_SIZE]) {
    for (int j = 0; j < FEATURE_SIZE; j++)
        for (int i = 0; i < FEATURE_ROW_SIZE; i++)
            features[j][i] = BIASED(0);
}

/* Declare test wrappers - these call actual code in fbtree_ordered_index.c */
extern void featureSearchSIMD_test_wrapper(char features[FEATURE_SIZE][FEATURE_ROW_SIZE],
                                           int num_keys, const unsigned char target[FEATURE_SIZE],
                                           int *out_left, int *out_right);

#if HAVE_X86_SIMD
extern void featureSearchSIMD_avx2_test_wrapper(char features[FEATURE_SIZE][FEATURE_ROW_SIZE],
                                                int num_keys, const unsigned char target[FEATURE_SIZE],
                                                int *out_left, int *out_right);

extern void featureSearchSIMD_sse2_test_wrapper(char features[FEATURE_SIZE][FEATURE_ROW_SIZE],
                                                int num_keys, const unsigned char target[FEATURE_SIZE],
                                                int *out_left, int *out_right);
#endif

typedef void (*FeatureSearchFn)(char features[FEATURE_SIZE][FEATURE_ROW_SIZE],
                                int num_keys, const unsigned char target[FEATURE_SIZE],
                                int *out_left, int *out_right);

/* Test all available implementations produce identical results */
static int testAllImpls(char features[FEATURE_SIZE][FEATURE_ROW_SIZE],
                        int num_keys, const unsigned char target[FEATURE_SIZE]) {
    int def_left, def_right;
    featureSearchSIMD_test_wrapper(features, num_keys, target, &def_left, &def_right);
    
#if HAVE_X86_SIMD
    int sse_left, sse_right;
    featureSearchSIMD_sse2_test_wrapper(features, num_keys, target, &sse_left, &sse_right);
    if (sse_left != def_left || sse_right != def_right) {
        printf("FAIL sse2: got [%d,%d) expected [%d,%d)\n", sse_left, sse_right, def_left, def_right);
        return 1;
    }
    
    if (__builtin_cpu_supports("avx2")) {
        int avx_left, avx_right;
        featureSearchSIMD_avx2_test_wrapper(features, num_keys, target, &avx_left, &avx_right);
        if (avx_left != def_left || avx_right != def_right) {
            printf("FAIL avx2: got [%d,%d) expected [%d,%d)\n", avx_left, avx_right, def_left, def_right);
            return 1;
        }
    }
#endif
    return 0;
}

int test_feature_search_empty(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE];
    initFeatures(features);
    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};
    TEST_ASSERT(testAllImpls(features, 0, target) == 0);
    return 0;
}

int test_feature_search_single_match(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE];
    initFeatures(features);
    features[0][0] = BIASED(0x50);
    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};
    TEST_ASSERT(testAllImpls(features, 1, target) == 0);
    return 0;
}

int test_feature_search_single_less(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE];
    initFeatures(features);
    features[0][0] = BIASED(0x50);
    unsigned char target[FEATURE_SIZE] = {0x40, 0, 0, 0};
    TEST_ASSERT(testAllImpls(features, 1, target) == 0);
    return 0;
}

int test_feature_search_single_greater(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE];
    initFeatures(features);
    features[0][0] = BIASED(0x50);
    unsigned char target[FEATURE_SIZE] = {0x60, 0, 0, 0};
    TEST_ASSERT(testAllImpls(features, 1, target) == 0);
    return 0;
}

int test_feature_search_three_elements(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE];
    initFeatures(features);
    features[0][0] = BIASED(0x30);
    features[0][1] = BIASED(0x50);
    features[0][2] = BIASED(0x70);
    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};
    TEST_ASSERT(testAllImpls(features, 3, target) == 0);
    return 0;
}

int test_feature_search_before_all(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE];
    initFeatures(features);
    features[0][0] = BIASED(0x30);
    features[0][1] = BIASED(0x50);
    features[0][2] = BIASED(0x70);
    unsigned char target[FEATURE_SIZE] = {0x20, 0, 0, 0};
    TEST_ASSERT(testAllImpls(features, 3, target) == 0);
    return 0;
}

int test_feature_search_after_all(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE];
    initFeatures(features);
    features[0][0] = BIASED(0x30);
    features[0][1] = BIASED(0x50);
    features[0][2] = BIASED(0x70);
    unsigned char target[FEATURE_SIZE] = {0x80, 0, 0, 0};
    TEST_ASSERT(testAllImpls(features, 3, target) == 0);
    return 0;
}

int test_feature_search_multibyte(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE];
    initFeatures(features);
    features[0][0] = BIASED(0x50); features[1][0] = BIASED(0x10);
    features[0][1] = BIASED(0x50); features[1][1] = BIASED(0x30);
    features[0][2] = BIASED(0x50); features[1][2] = BIASED(0x50);
    unsigned char target[FEATURE_SIZE] = {0x50, 0x30, 0, 0};
    TEST_ASSERT(testAllImpls(features, 3, target) == 0);
    return 0;
}

int test_feature_search_duplicates(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE];
    initFeatures(features);
    features[0][0] = BIASED(0x30);
    features[0][1] = BIASED(0x50);
    features[0][2] = BIASED(0x50);
    features[0][3] = BIASED(0x50);
    features[0][4] = BIASED(0x70);
    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};
    TEST_ASSERT(testAllImpls(features, 5, target) == 0);
    return 0;
}

int test_feature_search_high_values(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE];
    initFeatures(features);
    features[0][0] = BIASED(0x7F);
    features[0][1] = BIASED(0x80);
    features[0][2] = BIASED(0xFF);
    unsigned char target[FEATURE_SIZE] = {0x80, 0, 0, 0};
    TEST_ASSERT(testAllImpls(features, 3, target) == 0);
    return 0;
}

int test_feature_search_boundary_values(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE];
    initFeatures(features);
    features[0][0] = BIASED(0x00);
    features[0][1] = BIASED(0x7F);
    features[0][2] = BIASED(0x80);
    features[0][3] = BIASED(0xFF);
    unsigned char targets[][FEATURE_SIZE] = {
        {0x00, 0, 0, 0}, {0x7F, 0, 0, 0}, {0x80, 0, 0, 0}, {0xFF, 0, 0, 0}
    };
    for (int i = 0; i < 4; i++)
        TEST_ASSERT(testAllImpls(features, 4, targets[i]) == 0);
    return 0;
}

int test_feature_search_full_row(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE];
    initFeatures(features);
    for (int i = 0; i < 60; i++)
        features[0][i] = BIASED(i * 4);
    unsigned char target[FEATURE_SIZE] = {120, 0, 0, 0};
    TEST_ASSERT(testAllImpls(features, 60, target) == 0);
    return 0;
}

int test_feature_search_all_same(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE];
    initFeatures(features);
    for (int i = 0; i < 10; i++)
        features[0][i] = BIASED(0x50);
    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};
    TEST_ASSERT(testAllImpls(features, 10, target) == 0);
    return 0;
}

int test_feature_search_multibyte_tiebreak(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE];
    initFeatures(features);
    for (int i = 0; i < 5; i++) {
        features[0][i] = BIASED(0x50);
        features[1][i] = BIASED(i * 0x20);
    }
    unsigned char target[FEATURE_SIZE] = {0x50, 0x40, 0, 0};
    TEST_ASSERT(testAllImpls(features, 5, target) == 0);
    return 0;
}

int test_feature_search_all_bytes_matter(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE];
    initFeatures(features);
    for (int i = 0; i < 4; i++) {
        features[0][i] = BIASED(0x50);
        features[1][i] = BIASED(0x50);
        features[2][i] = BIASED(0x50);
        features[3][i] = BIASED(i * 0x30);
    }
    unsigned char target[FEATURE_SIZE] = {0x50, 0x50, 0x50, 0x60};
    TEST_ASSERT(testAllImpls(features, 4, target) == 0);
    return 0;
}

int test_feature_search_64_elements(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE];
    initFeatures(features);
    for (int i = 0; i < 64; i++)
        features[0][i] = BIASED(i * 3);
    unsigned char target[FEATURE_SIZE] = {96, 0, 0, 0};
    TEST_ASSERT(testAllImpls(features, 64, target) == 0);
    return 0;
}

int test_feature_search_cross_avx_boundary(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE];
    initFeatures(features);
    for (int i = 0; i < 40; i++)
        features[0][i] = BIASED(i < 32 ? 0x30 : 0x70);
    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};
    TEST_ASSERT(testAllImpls(features, 40, target) == 0);
    return 0;
}

int test_feature_search_cross_sse_boundary(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE];
    initFeatures(features);
    for (int i = 0; i < 20; i++)
        features[0][i] = BIASED(i < 16 ? 0x30 : 0x70);
    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};
    TEST_ASSERT(testAllImpls(features, 20, target) == 0);
    return 0;
}
