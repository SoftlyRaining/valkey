/* Static string with variable-sized headers optimized for constant-size strings.
 * Similar to SDS but without separate len/alloc tracking since they're always equal.
 *
 * Copyright (c) Valkey Contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef STATIC_STRING_H
#define STATIC_STRING_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

/* Pointer to the string buffer, similar to sds */
typedef char *static_string;
typedef const char *const_static_string;

/* Header types - use smallest type that fits the length */
#define SS_TYPE_8 0
#define SS_TYPE_16 1
#define SS_TYPE_32 2
#define SS_TYPE_64 3
#define SS_TYPE_MASK 3
#define SS_TYPE_BITS 2

/* Flags are stored in the 2 MSBs of len. On little-endian, MSB is at s[-1].
 * TODO: Handle big endian (flags would be in the first byte instead). */
struct __attribute__((__packed__)) sshdr8 {
    uint8_t len; /* (type << 6) | actual_len */
    char buf[];
};

struct __attribute__((__packed__)) sshdr16 {
    uint16_t len;
    char buf[];
};

struct __attribute__((__packed__)) sshdr32 {
    uint32_t len;
    char buf[];
};

struct __attribute__((__packed__)) sshdr64 {
    uint64_t len;
    char buf[];
};

#define SS_HDR(T, s) ((struct sshdr##T *)((s) - sizeof(struct sshdr##T)))

static inline unsigned char ssType(const_static_string s) {
    /* Type is in MSBs of len. On little-endian, MSB is at s[-1]. */
    return ((unsigned char *)s)[-1] >> (8 - SS_TYPE_BITS);
}

#define SS_LEN_MASK(bits) ((1ULL << ((bits) - SS_TYPE_BITS)) - 1)

static inline size_t sslen(const_static_string s) {
    switch (ssType(s)) {
    case SS_TYPE_8: return SS_HDR(8, s)->len & SS_LEN_MASK(8);
    case SS_TYPE_16: return SS_HDR(16, s)->len & SS_LEN_MASK(16);
    case SS_TYPE_32: return SS_HDR(32, s)->len & SS_LEN_MASK(32);
    case SS_TYPE_64: return SS_HDR(64, s)->len & SS_LEN_MASK(64);
    }
    return 0;
}

static inline int ssHdrSize(unsigned char type) {
    switch (type) {
    case SS_TYPE_8: return sizeof(struct sshdr8);
    case SS_TYPE_16: return sizeof(struct sshdr16);
    case SS_TYPE_32: return sizeof(struct sshdr32);
    case SS_TYPE_64: return sizeof(struct sshdr64);
    }
    return 0;
}

static inline unsigned char ssReqType(size_t len) {
    if (len < (1 << (8 - SS_TYPE_BITS))) return SS_TYPE_8;
    if (len < (1 << (16 - SS_TYPE_BITS))) return SS_TYPE_16;
#if (LONG_MAX == LLONG_MAX)
    if (len < (1ll << (32 - SS_TYPE_BITS))) return SS_TYPE_32;
#endif
    return SS_TYPE_64;
}

static inline bool sseq(const_static_string s1, const_static_string s2) {
    size_t l1 = sslen(s1);
    return l1 == sslen(s2) && memcmp(s1, s2, l1) == 0;
}

/* Returns total allocation size for a static_string */
static inline size_t ssAllocSize(const_static_string s) {
    return ssHdrSize(ssType(s)) + sslen(s);
}

/* Create a new static_string from data */
static_string ssnewlen(const void *init, size_t len);

/* Free a static_string */
void ssfree(static_string s);

/* Get pointer to the allocation (header start) */
void *ssAllocPtr(const_static_string s);

/* Duplicate a static_string */
static_string ssdup(const_static_string s);

/* Compare two static_strings */
int sscmp(const_static_string s1, const_static_string s2);

#endif /* STATIC_STRING_H */
