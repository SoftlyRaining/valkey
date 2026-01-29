/* Unit tests for static_string
 *
 * Copyright (c) Valkey Contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>
#include <limits.h>
#include "test_help.h"
#include "../static_string.h"
#include "../zmalloc.h"

int test_static_string_create_and_free(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);

    static_string s = ssnewlen("hello", 5);
    TEST_ASSERT(s != NULL);
    TEST_ASSERT(sslen(s) == 5);
    TEST_ASSERT(memcmp(s, "hello", 5) == 0);
    ssfree(s);
    return 0;
}

int test_static_string_empty(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);

    static_string s = ssnewlen("", 0);
    TEST_ASSERT(s != NULL);
    TEST_ASSERT(sslen(s) == 0);
    ssfree(s);
    return 0;
}

int test_static_string_type8(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);

    /* Type 8: 0-255 bytes */
    static_string s = ssnewlen("test", 4);
    TEST_ASSERT(ssType(s) == SS_TYPE_8);
    TEST_ASSERT(sslen(s) == 4);
    TEST_ASSERT(ssAllocSize(s) == 1 + 4);  /* 1 byte len (includes flags) */
    ssfree(s);

    /* Boundary: 63 bytes (max for 6-bit len) */
    char buf[63];
    memset(buf, 'x', 63);
    s = ssnewlen(buf, 63);
    TEST_ASSERT(ssType(s) == SS_TYPE_8);
    TEST_ASSERT(sslen(s) == 63);
    ssfree(s);

    return 0;
}

int test_static_string_type16(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);

    /* Type 16: 64-16383 bytes */
    char *buf = zmalloc(64);
    memset(buf, 'y', 64);
    static_string s = ssnewlen(buf, 64);
    TEST_ASSERT(ssType(s) == SS_TYPE_16);
    TEST_ASSERT(sslen(s) == 64);
    TEST_ASSERT(ssAllocSize(s) == 2 + 64);  /* 2 byte len (includes flags) */
    ssfree(s);
    zfree(buf);

    /* Boundary: 16383 bytes (max for 14-bit len) */
    buf = zmalloc(16383);
    memset(buf, 'z', 16383);
    s = ssnewlen(buf, 16383);
    TEST_ASSERT(ssType(s) == SS_TYPE_16);
    TEST_ASSERT(sslen(s) == 16383);
    ssfree(s);
    zfree(buf);

    return 0;
}

int test_static_string_type32(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);

    /* Type 32: 16384+ bytes */
    size_t len = 16384;
    char *buf = zmalloc(len);
    memset(buf, 'a', len);
    static_string s = ssnewlen(buf, len);
    TEST_ASSERT(ssType(s) == SS_TYPE_32);
    TEST_ASSERT(sslen(s) == len);
    TEST_ASSERT(ssAllocSize(s) == 4 + len);  /* 4 byte len (includes flags) */
    ssfree(s);
    zfree(buf);

    return 0;
}

int test_static_string_dup(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);

    static_string s1 = ssnewlen("duplicate me", 12);
    static_string s2 = ssdup(s1);

    TEST_ASSERT(s2 != NULL);
    TEST_ASSERT(s1 != s2);
    TEST_ASSERT(sslen(s1) == sslen(s2));
    TEST_ASSERT(memcmp(s1, s2, sslen(s1)) == 0);

    ssfree(s1);
    ssfree(s2);
    return 0;
}

int test_static_string_cmp(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);

    static_string s1 = ssnewlen("abc", 3);
    static_string s2 = ssnewlen("abc", 3);
    static_string s3 = ssnewlen("abd", 3);
    static_string s4 = ssnewlen("ab", 2);
    static_string s5 = ssnewlen("abcd", 4);

    TEST_ASSERT(sscmp(s1, s2) == 0);
    TEST_ASSERT(sscmp(s1, s3) < 0);
    TEST_ASSERT(sscmp(s3, s1) > 0);
    TEST_ASSERT(sscmp(s1, s4) > 0);
    TEST_ASSERT(sscmp(s4, s1) < 0);
    TEST_ASSERT(sscmp(s1, s5) < 0);

    ssfree(s1);
    ssfree(s2);
    ssfree(s3);
    ssfree(s4);
    ssfree(s5);
    return 0;
}

int test_static_string_binary_safe(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);

    const char data[] = "hello\0world";
    static_string s = ssnewlen(data, 11);

    TEST_ASSERT(sslen(s) == 11);
    TEST_ASSERT(memcmp(s, data, 11) == 0);
    TEST_ASSERT(s[5] == '\0');
    TEST_ASSERT(s[6] == 'w');

    ssfree(s);
    return 0;
}

int test_static_string_alloc_ptr(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);

    static_string s = ssnewlen("test", 4);
    void *ptr = ssAllocPtr(s);

    TEST_ASSERT(ptr < (void *)s);
    TEST_ASSERT((char *)ptr + ssHdrSize(ssType(s)) == s);

    ssfree(s);
    return 0;
}

int test_static_string_header_sizes(int argc, char **argv, int flags) {
    UNUSED(argc);
    UNUSED(argv);
    UNUSED(flags);

    TEST_ASSERT(sizeof(struct sshdr8) == 1);   /* 1 len (flags in LSBs) */
    TEST_ASSERT(sizeof(struct sshdr16) == 2);  /* 2 len (flags in LSBs) */
    TEST_ASSERT(sizeof(struct sshdr32) == 4);  /* 4 len (flags in LSBs) */
    TEST_ASSERT(sizeof(struct sshdr64) == 8);  /* 8 len (flags in LSBs) */

    return 0;
}
