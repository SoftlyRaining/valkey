/* Static string with variable-sized headers optimized for constant-size strings.
 *
 * Copyright (c) Valkey Contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "static_string.h"
#include "zmalloc.h"
#include <string.h>
#include <limits.h>

static_string ssnewlen(const void *init, size_t len) {
    unsigned char type = ssReqType(len);
    int hdrlen = ssHdrSize(type);
    unsigned char *buf = zmalloc(hdrlen + len);
    static_string s = (char *)buf + hdrlen;

    switch (type) {
    case SS_TYPE_8: {
        struct sshdr8 *hdr = (void *)buf;
        hdr->len = ((uint8_t)type << 6) | len;
        break;
    }
    case SS_TYPE_16: {
        struct sshdr16 *hdr = (void *)buf;
        hdr->len = ((uint16_t)type << 14) | len;
        break;
    }
    case SS_TYPE_32: {
        struct sshdr32 *hdr = (void *)buf;
        hdr->len = ((uint32_t)type << 30) | len;
        break;
    }
    case SS_TYPE_64: {
        struct sshdr64 *hdr = (void *)buf;
        hdr->len = ((uint64_t)type << 62) | len;
        break;
    }
    }

    if (init && len) memcpy(s, init, len);
    return s;
}

void ssfree(static_string s) {
    if (s) zfree(ssAllocPtr(s));
}

void *ssAllocPtr(const_static_string s) {
    return (void *)(s - ssHdrSize(ssType(s)));
}

static_string ssdup(const_static_string s) {
    return ssnewlen(s, sslen(s));
}

int sscmp(const_static_string s1, const_static_string s2) {
    size_t l1 = sslen(s1);
    size_t l2 = sslen(s2);
    size_t minlen = (l1 < l2) ? l1 : l2;
    int cmp = memcmp(s1, s2, minlen);
    if (cmp == 0) return (l1 > l2) - (l1 < l2);
    return cmp;
}
