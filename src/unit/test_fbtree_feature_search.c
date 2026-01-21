/* Unit tests for featureSearchSIMD function */

#include "../zmalloc.h"
#include "test_help.h"
#include <stdio.h>
#include <string.h>

/* Constants from fbtree_ordered_index.c */
#define FEATURE_SIZE 4
#define FEATURE_ROW_SIZE 64

/* Declare the test wrapper - implemented in fbtree_ordered_index.c */
extern void featureSearchSIMD_test_wrapper(char features[FEATURE_SIZE][FEATURE_ROW_SIZE],
                                           int num_keys, const unsigned char target[FEATURE_SIZE],
                                           int *out_left, int *out_right);

/* Reference scalar implementation for comparison */
static void featureSearchScalar(char features[FEATURE_SIZE][FEATURE_ROW_SIZE],
                                int num_keys, const unsigned char target[FEATURE_SIZE],
                                int *out_left, int *out_right) {
    int left = 0, right = num_keys;
    
    /* Find leftmost position where target <= feature (lower bound) */
    for (int i = 0; i < num_keys; i++) {
        int cmp = 0;
        for (int j = 0; j < FEATURE_SIZE && cmp == 0; j++)
            cmp = (int)target[j] - (unsigned char)features[j][i];
        if (cmp <= 0) {
            left = i;
            break;
        }
        left = i + 1;
    }
    
    /* Find rightmost position where target >= feature (upper bound) */
    right = left;
    for (int i = left; i < num_keys; i++) {
        int cmp = 0;
        for (int j = 0; j < FEATURE_SIZE && cmp == 0; j++)
            cmp = (int)target[j] - (unsigned char)features[j][i];
        if (cmp < 0) break;
        right = i + 1;
    }
    
    *out_left = left;
    *out_right = right;
}

int test_feature_search_empty(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE] = {0};
    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};
    int left, right;
    
    featureSearchSIMD_test_wrapper(features, 0, target, &left, &right);
    TEST_ASSERT(left == 0);
    TEST_ASSERT(right == 0);
    
    return 0;
}

int test_feature_search_single_match(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE] = {0};
    features[0][0] = 0x50;
    
    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};
    int left, right, exp_left, exp_right;
    
    featureSearchSIMD_test_wrapper(features, 1, target, &left, &right);
    featureSearchScalar(features, 1, target, &exp_left, &exp_right);
    
    TEST_ASSERT(left == exp_left);
    TEST_ASSERT(right == exp_right);
    TEST_ASSERT(left == 0);
    TEST_ASSERT(right == 1);
    
    return 0;
}

int test_feature_search_single_less(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE] = {0};
    features[0][0] = 0x50;
    
    unsigned char target[FEATURE_SIZE] = {0x40, 0, 0, 0};  /* target < feature */
    int left, right, exp_left, exp_right;
    
    featureSearchSIMD_test_wrapper(features, 1, target, &left, &right);
    featureSearchScalar(features, 1, target, &exp_left, &exp_right);
    
    TEST_ASSERT(left == exp_left);
    TEST_ASSERT(right == exp_right);
    
    return 0;
}

int test_feature_search_single_greater(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE] = {0};
    features[0][0] = 0x50;
    
    unsigned char target[FEATURE_SIZE] = {0x60, 0, 0, 0};  /* target > feature */
    int left, right, exp_left, exp_right;
    
    featureSearchSIMD_test_wrapper(features, 1, target, &left, &right);
    featureSearchScalar(features, 1, target, &exp_left, &exp_right);
    
    TEST_ASSERT(left == exp_left);
    TEST_ASSERT(right == exp_right);
    
    return 0;
}

int test_feature_search_three_elements(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    
    /* Sorted features: 0x30, 0x50, 0x70 */
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE] = {0};
    features[0][0] = 0x30;
    features[0][1] = 0x50;
    features[0][2] = 0x70;
    
    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};
    int left, right, exp_left, exp_right;
    
    featureSearchSIMD_test_wrapper(features, 3, target, &left, &right);
    featureSearchScalar(features, 3, target, &exp_left, &exp_right);
    
    TEST_ASSERT(left == exp_left);
    TEST_ASSERT(right == exp_right);
    
    return 0;
}

int test_feature_search_before_all(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE] = {0};
    features[0][0] = 0x30;
    features[0][1] = 0x50;
    features[0][2] = 0x70;
    
    unsigned char target[FEATURE_SIZE] = {0x20, 0, 0, 0};  /* before all */
    int left, right, exp_left, exp_right;
    
    featureSearchSIMD_test_wrapper(features, 3, target, &left, &right);
    featureSearchScalar(features, 3, target, &exp_left, &exp_right);
    
    TEST_ASSERT(left == exp_left);
    TEST_ASSERT(right == exp_right);
    TEST_ASSERT(left == 0);
    TEST_ASSERT(right == 0);
    
    return 0;
}

int test_feature_search_after_all(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE] = {0};
    features[0][0] = 0x30;
    features[0][1] = 0x50;
    features[0][2] = 0x70;
    
    unsigned char target[FEATURE_SIZE] = {0x80, 0, 0, 0};  /* after all */
    int left, right, exp_left, exp_right;
    
    featureSearchSIMD_test_wrapper(features, 3, target, &left, &right);
    featureSearchScalar(features, 3, target, &exp_left, &exp_right);
    
    TEST_ASSERT(left == exp_left);
    TEST_ASSERT(right == exp_right);
    TEST_ASSERT(left == 3);
    TEST_ASSERT(right == 3);
    
    return 0;
}

int test_feature_search_multibyte(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    
    /* Features differ in second byte */
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE] = {0};
    features[0][0] = 0x50; features[1][0] = 0x10;
    features[0][1] = 0x50; features[1][1] = 0x30;
    features[0][2] = 0x50; features[1][2] = 0x50;
    
    unsigned char target[FEATURE_SIZE] = {0x50, 0x30, 0, 0};
    int left, right, exp_left, exp_right;
    
    featureSearchSIMD_test_wrapper(features, 3, target, &left, &right);
    featureSearchScalar(features, 3, target, &exp_left, &exp_right);
    
    TEST_ASSERT(left == exp_left);
    TEST_ASSERT(right == exp_right);
    
    return 0;
}

int test_feature_search_duplicates(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    
    /* Multiple identical features */
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE] = {0};
    features[0][0] = 0x30;
    features[0][1] = 0x50;
    features[0][2] = 0x50;
    features[0][3] = 0x50;
    features[0][4] = 0x70;
    
    unsigned char target[FEATURE_SIZE] = {0x50, 0, 0, 0};
    int left, right, exp_left, exp_right;
    
    featureSearchSIMD_test_wrapper(features, 5, target, &left, &right);
    featureSearchScalar(features, 5, target, &exp_left, &exp_right);
    
    TEST_ASSERT(left == exp_left);
    TEST_ASSERT(right == exp_right);
    /* Should return range [1, 4) covering all 0x50 entries */
    TEST_ASSERT(left == 1);
    TEST_ASSERT(right == 4);
    
    return 0;
}

int test_feature_search_high_values(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    
    /* Test with high byte values (unsigned comparison matters) */
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE] = {0};
    features[0][0] = (char)0x7F;  /* 127 signed, 127 unsigned */
    features[0][1] = (char)0x80;  /* -128 signed, 128 unsigned */
    features[0][2] = (char)0xFF;  /* -1 signed, 255 unsigned */
    
    unsigned char target[FEATURE_SIZE] = {0x80, 0, 0, 0};
    int left, right, exp_left, exp_right;
    
    featureSearchSIMD_test_wrapper(features, 3, target, &left, &right);
    featureSearchScalar(features, 3, target, &exp_left, &exp_right);
    
    TEST_ASSERT(left == exp_left);
    TEST_ASSERT(right == exp_right);
    
    return 0;
}

int test_feature_search_full_node(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    
    /* Test with 60 elements (NODE_SIZE) */
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE] = {0};
    for (int i = 0; i < 60; i++) {
        features[0][i] = (char)(i * 4);  /* 0, 4, 8, ... 236 */
    }
    
    unsigned char target[FEATURE_SIZE] = {100, 0, 0, 0};
    int left, right, exp_left, exp_right;
    
    featureSearchSIMD_test_wrapper(features, 60, target, &left, &right);
    featureSearchScalar(features, 60, target, &exp_left, &exp_right);
    
    TEST_ASSERT(left == exp_left);
    TEST_ASSERT(right == exp_right);
    
    return 0;
}

int test_feature_search_64_elements(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    
    /* Test with exactly 64 elements (full mask) */
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE] = {0};
    for (int i = 0; i < 64; i++) {
        features[0][i] = (char)(i * 3);
    }
    
    unsigned char target[FEATURE_SIZE] = {96, 0, 0, 0};  /* 96 = 32*3, should match index 32 */
    int left, right, exp_left, exp_right;
    
    featureSearchSIMD_test_wrapper(features, 64, target, &left, &right);
    featureSearchScalar(features, 64, target, &exp_left, &exp_right);
    
    TEST_ASSERT(left == exp_left);
    TEST_ASSERT(right == exp_right);
    
    return 0;
}

int test_feature_search_boundary_17(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    
    /* Test boundary at chunk 1 (index 16-17) */
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE] = {0};
    for (int i = 0; i < 20; i++) {
        features[0][i] = (char)(i * 10);
    }
    
    unsigned char target[FEATURE_SIZE] = {170, 0, 0, 0};  /* index 17 */
    int left, right, exp_left, exp_right;
    
    featureSearchSIMD_test_wrapper(features, 20, target, &left, &right);
    featureSearchScalar(features, 20, target, &exp_left, &exp_right);
    
    TEST_ASSERT(left == exp_left);
    TEST_ASSERT(right == exp_right);
    
    return 0;
}


/* Debug test with many keys */
int test_feature_search_debug(int argc, char **argv, int flags) {
    UNUSED(argc); UNUSED(argv); UNUSED(flags);
    
    /* Test case: 60 sequential keys like k00, k01, ... k59 (NODE_SIZE) */
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE] = {0};
    
    /* Simulate features for "k00" through "k59" */
    for (int i = 0; i < 60; i++) {
        features[0][i] = 'k';
        features[1][i] = '0' + (i / 10);
        features[2][i] = '0' + (i % 10);
        features[3][i] = '\0';
    }
    
    /* Search for k32 */
    unsigned char target[FEATURE_SIZE] = {'k', '3', '2', '\0'};
    int left, right, exp_left, exp_right;
    
    featureSearchSIMD_test_wrapper(features, 60, target, &left, &right);
    featureSearchScalar(features, 60, target, &exp_left, &exp_right);
    
    TEST_ASSERT(left == exp_left);
    TEST_ASSERT(right == exp_right);
    
    /* Search for k00 (first) */
    unsigned char target2[FEATURE_SIZE] = {'k', '0', '0', '\0'};
    featureSearchSIMD_test_wrapper(features, 60, target2, &left, &right);
    featureSearchScalar(features, 60, target2, &exp_left, &exp_right);
    
    TEST_ASSERT(left == exp_left);
    TEST_ASSERT(right == exp_right);
    
    /* Search for k59 (last) */
    unsigned char target3[FEATURE_SIZE] = {'k', '5', '9', '\0'};
    featureSearchSIMD_test_wrapper(features, 60, target3, &left, &right);
    featureSearchScalar(features, 60, target3, &exp_left, &exp_right);
    
    TEST_ASSERT(left == exp_left);
    TEST_ASSERT(right == exp_right);
    
    return 0;
}
