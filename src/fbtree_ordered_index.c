/*
 * Copyright (c) Valkey Contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* fbtree adapter for OrderedIndex interface.
 * Maps OrderedIndex operations to fbtree calls, handling score normalization
 * and [score][element] packing/unpacking.
 *
 * The fbtree stores packed keys: [8-byte normalized score][element bytes].
 * Each packed sds is marked via aux bit 0 so the hashtable can identify it
 * and hash/compare only the element portion (skipping the score prefix). */

#include "server.h"
#include "ordered_index.h"
#include "fbtree.h"
#include "endianconv.h"

static_assert(sizeof(OrderedIndexIterator) >= sizeof(fbtreeIterator),
              "OrderedIndexIterator must be large enough to hold fbtreeIterator");

#define SCORE_SIZE 8 /* Normalized score prefix size */

/* Mark/check packed sds as fbtree items using aux bit 0.
 * The hashtable uses this to hash/compare only the element portion. */
static inline void sdsSetFbtreeItem(sds s) {
    unsigned char flags = s[-1];
    s[-1] = (char)(flags | (1 << SDS_TYPE_BITS));
}

/* ========== Score Normalization ==========
 * Converts IEEE 754 double to a sortable 8-byte big-endian representation.
 * Lexicographic byte comparison matches numeric order after transformation. */

static inline uint64_t scoreToSortable(double score) {
    uint64_t bits;
    memcpy(&bits, &score, sizeof(bits));
    if (bits & (1ULL << 63)) {
        bits = ~bits;
    } else {
        bits ^= (1ULL << 63);
    }
    return htonu64(bits);
}

static inline double sortableToScore(uint64_t be) {
    uint64_t bits = ntohu64(be);
    if (bits & (1ULL << 63)) {
        bits ^= (1ULL << 63);
    } else {
        bits = ~bits;
    }
    double score;
    memcpy(&score, &bits, sizeof(score));
    return score;
}

/* Pack score and element into sds: [8-byte sortable score][element]
 * The result is marked as an fbtree item via aux bit 0.
 * Since total length is always >= 8 (score prefix), and sdshdr5 max is 31,
 * most items will be sdshdr8+. For very short elements (total < 32), sdsnewlen
 * may pick sdshdr5. We handle this by using sdsMakeRoomForNonGreedy after
 * creating an empty sds (which is always sdshdr8). */
static sds packScoreElement(double score, const char *ele, size_t ele_len) {
    uint64_t sortable = scoreToSortable(score);
    size_t total = SCORE_SIZE + ele_len;
    /* Create empty sds (guaranteed sdshdr8), then ensure capacity */
    sds packed = sdsempty();
    packed = sdsMakeRoomFor(packed, total);
    memcpy(packed, &sortable, SCORE_SIZE);
    memcpy(packed + SCORE_SIZE, ele, ele_len);
    sdsIncrLen(packed, total);
    packed[total] = '\0';
    sdsSetFbtreeItem(packed);
    return packed;
}

static inline const char *unpackElement(const_sds packed, size_t *len) {
    *len = sdslen(packed) - SCORE_SIZE;
    return packed + SCORE_SIZE;
}

static inline double unpackScore(const_sds packed) {
    uint64_t sortable;
    memcpy(&sortable, packed, SCORE_SIZE);
    return sortableToScore(sortable);
}

/* ========== Lifecycle ========== */

OrderedIndex *fbtreeOICreate(void) {
    return (OrderedIndex *)fbtreeCreate();
}

void fbtreeOIFree(OrderedIndex *oi) {
    fbtreeFree((fbtreeIndex *)oi);
}

/* ========== Modification ========== */

OrderedIndexItem *fbtreeOIInsert(OrderedIndex *oi, double score, const char *ele, size_t len) {
    sds packed = packScoreElement(score, ele, len);
    return (OrderedIndexItem *)fbtreeInsert((fbtreeIndex *)oi, packed);
}

void fbtreeOIDelete(OrderedIndex *oi, OrderedIndexItem *item) {
    fbtreeDelete((fbtreeIndex *)oi, (const_sds)item);
}

OrderedIndexItem *fbtreeOIUpdateScore(OrderedIndex *oi, OrderedIndexItem *item, double newscore) {
    const_sds packed = (const_sds)item;
    size_t ele_len;
    const char *ele = unpackElement(packed, &ele_len);
    sds new_packed = packScoreElement(newscore, ele, ele_len);
    fbtreeDelete((fbtreeIndex *)oi, packed);
    return (OrderedIndexItem *)fbtreeInsert((fbtreeIndex *)oi, new_packed);
}

OrderedIndexItem *fbtreeOIPopFirst(OrderedIndex *oi) {
    return (OrderedIndexItem *)fbtreePopMin((fbtreeIndex *)oi);
}

OrderedIndexItem *fbtreeOIPopLast(OrderedIndex *oi) {
    return (OrderedIndexItem *)fbtreePopMax((fbtreeIndex *)oi);
}

void fbtreeOIFreeItem(OrderedIndexItem *item) {
    sdsfree((sds)item);
}

OrderedIndexItem *fbtreeOICreateDetached(double score, const char *ele, size_t len) {
    return (OrderedIndexItem *)packScoreElement(score, ele, len);
}

void fbtreeOIDetachedSetScore(OrderedIndexItem *item, double score) {
    uint64_t sortable = scoreToSortable(score);
    memcpy((char *)item, &sortable, SCORE_SIZE);
}

OrderedIndexItem *fbtreeOIInsertDetached(OrderedIndex *oi, OrderedIndexItem *item) {
    return (OrderedIndexItem *)fbtreeInsert((fbtreeIndex *)oi, (sds)item);
}

/* Helper: range delete with on_delete callback.
 * The fbtree itself frees the sds after this callback returns,
 * so we must NOT free here -- only notify the caller. */
typedef struct {
    OrderedIndexOnDelete on_delete;
    void *user_ctx;
} rangeDeleteArgs;

static void rangeDeleteCallback(sds item, void *ctx) {
    rangeDeleteArgs *args = (rangeDeleteArgs *)ctx;
    if (args->on_delete) {
        args->on_delete((OrderedIndexItem *)item, args->user_ctx);
    }
}

unsigned long fbtreeOIDeleteRangeByScore(OrderedIndex *oi, double min, double max, int min_ex, int max_ex, OrderedIndexOnDelete on_delete, void *ctx) {
    uint64_t min_sortable = scoreToSortable(min);
    uint64_t max_sortable = scoreToSortable(max);
    rangeDeleteArgs args = {on_delete, ctx};
    return fbtreeDeleteRangeByScore((fbtreeIndex *)oi, (const char *)&min_sortable, (const char *)&max_sortable, min_ex, max_ex, rangeDeleteCallback, &args);
}

unsigned long fbtreeOIDeleteRangeByIndex(OrderedIndex *oi, unsigned long start, unsigned long end, OrderedIndexOnDelete on_delete, void *ctx) {
    rangeDeleteArgs args = {on_delete, ctx};
    return fbtreeDeleteRangeByRank((fbtreeIndex *)oi, start, end, rangeDeleteCallback, &args);
}

unsigned long fbtreeOIDeleteRangeByLex(OrderedIndex *oi, const_sds min, const_sds max, int min_ex, int max_ex, OrderedIndexOnDelete on_delete, void *ctx) {
    /* Lex range: all items share the same score (zset lex semantics).
     * Get the score from the first element to build proper packed bounds. */
    fbtreeIndex *fbt = (fbtreeIndex *)oi;
    if (fbtreeLength(fbt) == 0) return 0;

    const_sds first = fbtreeGetAtRank(fbt, 0);
    if (!first) return 0;
    /* Extract the score prefix from any existing element */
    uint64_t score_prefix;
    memcpy(&score_prefix, first, SCORE_SIZE);

    sds min_packed = sdsempty();
    min_packed = sdsMakeRoomFor(min_packed, SCORE_SIZE + sdslen(min));
    memcpy(min_packed, &score_prefix, SCORE_SIZE);
    memcpy(min_packed + SCORE_SIZE, min, sdslen(min));
    sdsIncrLen(min_packed, SCORE_SIZE + sdslen(min));

    sds max_packed = sdsempty();
    max_packed = sdsMakeRoomFor(max_packed, SCORE_SIZE + sdslen(max));
    memcpy(max_packed, &score_prefix, SCORE_SIZE);
    memcpy(max_packed + SCORE_SIZE, max, sdslen(max));
    sdsIncrLen(max_packed, SCORE_SIZE + sdslen(max));

    rangeDeleteArgs args = {on_delete, ctx};
    unsigned long deleted = fbtreeDeleteRangeByValue(fbt, min_packed, max_packed, min_ex, max_ex, rangeDeleteCallback, &args);
    sdsfree(min_packed);
    sdsfree(max_packed);
    return deleted;
}

/* ========== Query ========== */

unsigned long fbtreeOILength(OrderedIndex *oi) {
    return fbtreeLength((fbtreeIndex *)oi);
}

OrderedIndexItem *fbtreeOIGetByIndex(OrderedIndex *oi, unsigned long index) {
    return (OrderedIndexItem *)fbtreeGetAtRank((fbtreeIndex *)oi, index);
}

unsigned long fbtreeOIGetIndex(OrderedIndex *oi, const OrderedIndexItem *item) {
    long rank = fbtreeGetRankOfItem((fbtreeIndex *)oi, (const_sds)item);
    return (unsigned long)rank;
}

void fbtreeOIGetElementRaw(const OrderedIndexItem *item, const char **ptr, size_t *len) {
    const_sds packed = (const_sds)item;
    *len = sdslen(packed) - SCORE_SIZE;
    *ptr = packed + SCORE_SIZE;
}

double fbtreeOIGetScore(const OrderedIndexItem *item) {
    return unpackScore((const_sds)item);
}

unsigned long fbtreeOICountScoreRange(OrderedIndex *oi, double min, double max, int min_ex, int max_ex) {
    /* Use iterator to count — seek to start, iterate until past end */
    fbtreeIterator iter;
    fbtreeInitIterator(&iter, (fbtreeIndex *)oi);
    uint64_t min_sortable = scoreToSortable(min);
    if (min_ex) {
        uint64_t native = ntohu64(min_sortable);
        native++;
        min_sortable = htonu64(native);
    }
    fbtreeSeekToScore((const char *)&min_sortable, &iter);

    unsigned long count = 0;
    const_sds pos;
    while (fbtreeNext(&iter, &pos)) {
        double score = unpackScore(pos);
        if (max_ex ? score >= max : score > max) break;
        count++;
    }
    return count;
}

unsigned long fbtreeOICountLexRange(OrderedIndex *oi, const_sds min, const_sds max, int min_ex, int max_ex) {
    /* Lex range: all items have same score, sorted by element. */
    fbtreeIndex *fbt = (fbtreeIndex *)oi;
    if (fbtreeLength(fbt) == 0) return 0;

    const_sds first = fbtreeGetAtRank(fbt, 0);
    if (!first) return 0;
    uint64_t score_prefix;
    memcpy(&score_prefix, first, SCORE_SIZE);

    fbtreeIterator iter;
    fbtreeInitIterator(&iter, fbt);

    sds min_packed = sdsempty();
    min_packed = sdsMakeRoomFor(min_packed, SCORE_SIZE + sdslen(min));
    memcpy(min_packed, &score_prefix, SCORE_SIZE);
    memcpy(min_packed + SCORE_SIZE, min, sdslen(min));
    sdsIncrLen(min_packed, SCORE_SIZE + sdslen(min));
    fbtreeSeekToValue(min_packed, &iter);
    sdsfree(min_packed);

    unsigned long count = 0;
    const_sds pos;
    while (fbtreeNext(&iter, &pos)) {
        const char *ele = pos + SCORE_SIZE;
        size_t ele_len = sdslen(pos) - SCORE_SIZE;
        int cmp = memcmp(ele, max, ele_len < sdslen(max) ? ele_len : sdslen(max));
        if (cmp == 0) cmp = (int)ele_len - (int)sdslen(max);
        if (max_ex ? cmp >= 0 : cmp > 0) break;
        if (min_ex && count == 0) {
            int cmp_min = memcmp(ele, min, ele_len < sdslen(min) ? ele_len : sdslen(min));
            if (cmp_min == 0) cmp_min = (int)ele_len - (int)sdslen(min);
            if (cmp_min == 0) continue;
        }
        count++;
    }
    return count;
}

/* ========== Iterator ========== */

void fbtreeOIInitIterator(OrderedIndexIterator *iter, OrderedIndex *oi) {
    fbtreeInitIterator((fbtreeIterator *)iter, (fbtreeIndex *)oi);
}

void fbtreeOIResetIterator(OrderedIndexIterator *iter) {
    fbtreeResetIterator((fbtreeIterator *)iter);
}

OrderedIndexItem *fbtreeOINext(OrderedIndexIterator *iter) {
    const_sds pos;
    if (fbtreeNext((fbtreeIterator *)iter, &pos)) {
        return (OrderedIndexItem *)pos;
    }
    return NULL;
}

OrderedIndexItem *fbtreeOIPrev(OrderedIndexIterator *iter) {
    const_sds pos;
    if (fbtreePrev((fbtreeIterator *)iter, &pos)) {
        return (OrderedIndexItem *)pos;
    }
    return NULL;
}

void fbtreeOISeekToIndex(OrderedIndexIterator *iter, unsigned long index) {
    fbtreeSeekToRank((fbtreeIterator *)iter, index + 1);
}

void fbtreeOISeekToScoreRange(OrderedIndexIterator *iter, double min, double max, int min_ex, int max_ex, long offset) {
    fbtreeIterator *fbt_iter = (fbtreeIterator *)iter;
    fbtreeIndex *fbt = fbtreeIteratorGetIndex(fbt_iter);
    if (!fbt) return;

    if (min > max || (min == max && (min_ex || max_ex))) {
        fbtreeResetIterator(fbt_iter);
        return;
    }

    uint64_t sortable;
    if (offset >= 0) {
        sortable = scoreToSortable(min);
        if (min_ex) {
            uint64_t native = ntohu64(sortable);
            native++;
            sortable = htonu64(native);
        }
    } else {
        sortable = scoreToSortable(max);
        if (!max_ex) {
            uint64_t native = ntohu64(sortable);
            native++;
            sortable = htonu64(native);
        }
    }
    unsigned long len = fbtreeLength(fbt);
    long base = fbtreeSeekToScore((const char *)&sortable, fbt_iter);
    long target = offset + base;

    if (target < 0 || (unsigned long)target >= len) {
        fbtreeResetIterator(fbt_iter);
        return;
    }

    /* Validate the element at target is within [min, max]. */
    const_sds item = fbtreeGetAtRank(fbt, (unsigned long)target);
    if (item) {
        double score = unpackScore(item);
        if (score > max || (max_ex && score == max) ||
            score < min || (min_ex && score == min)) {
            fbtreeResetIterator(fbt_iter);
            return;
        }
    }

    /* For reverse (offset<0), fbtreePrev decrements before returning,
     * so position one past the target for prev() to return it. */
    fbtreeSeekToRank(fbt_iter, (unsigned long)target + (offset < 0 ? 1 : 0));
}

void fbtreeOISeekToLexRange(OrderedIndexIterator *iter, const_sds min, const_sds max, int min_ex, int max_ex, long offset) {
    fbtreeIterator *fbt_iter = (fbtreeIterator *)iter;
    fbtreeIndex *fbt = fbtreeIteratorGetIndex(fbt_iter);
    if (!fbt || fbtreeLength(fbt) == 0) return;

    /* Get score prefix from first element (all share same score in lex zsets) */
    const_sds first = fbtreeGetAtRank(fbt, 0);
    if (!first) return;
    uint64_t score_prefix;
    memcpy(&score_prefix, first, SCORE_SIZE);

    unsigned long len = fbtreeLength(fbt);

    if (offset >= 0) {
        /* Forward seek: position so that next() returns first element in range */
        if (min == shared.minstring) {
            fbtreeSeekToRank(fbt_iter, 0);
        } else {
            sds packed = sdsempty();
            packed = sdsMakeRoomFor(packed, SCORE_SIZE + sdslen(min));
            memcpy(packed, &score_prefix, SCORE_SIZE);
            memcpy(packed + SCORE_SIZE, min, sdslen(min));
            sdsIncrLen(packed, SCORE_SIZE + sdslen(min));

            fbtreeSeekToValue(packed, fbt_iter);
            sdsfree(packed);

            /* For exclusive min: if positioned at exact match, advance past it. */
            if (min_ex) {
                const_sds pos;
                if (fbtreeNext(fbt_iter, &pos)) {
                    const char *ele = pos + SCORE_SIZE;
                    size_t ele_len = sdslen(pos) - SCORE_SIZE;
                    if (ele_len != sdslen(min) || memcmp(ele, min, ele_len) != 0) {
                        /* Not an exact match — re-seek to include this element */
                        fbtreeSeekToValue(pos, fbt_iter);
                    }
                }
            }
        }
        /* Apply LIMIT offset: skip 'offset' elements */
        if (offset > 0) {
            const_sds pos;
            for (long i = 0; i < offset; i++) {
                if (!fbtreeNext(fbt_iter, &pos)) return;
            }
        }
    } else {
        /* Reverse seek: position so that prev() returns last element in range */
        if (max == shared.maxstring) {
            fbtreeSeekToRank(fbt_iter, len);
        } else {
            sds packed = sdsempty();
            packed = sdsMakeRoomFor(packed, SCORE_SIZE + sdslen(max) + 1);
            memcpy(packed, &score_prefix, SCORE_SIZE);
            memcpy(packed + SCORE_SIZE, max, sdslen(max));
            sdsIncrLen(packed, SCORE_SIZE + sdslen(max));
            if (!max_ex) {
                /* Inclusive max: append 0xFF so we seek past max,
                 * then prev() returns max itself. */
                packed = sdscatlen(packed, "\xff", 1);
            }
            /* Exclusive max: seek to exact value, prev() returns element before it. */

            fbtreeSeekToValue(packed, fbt_iter);
            sdsfree(packed);
        }
        /* Apply LIMIT offset for reverse: offset is -(skip+1),
         * so -1 = no skip, -2 = skip 1, etc. */
        long skip = -(offset + 1);
        if (skip > 0) {
            const_sds pos;
            for (long i = 0; i < skip; i++) {
                if (!fbtreePrev(fbt_iter, &pos)) return;
            }
        }
    }
}

/* ========== Memory ========== */

void fbtreeOIDismissMemory(OrderedIndex *oi) {
    /* fbtree nodes are allocated individually — would need to walk all nodes.
     * For now, no-op. Can be implemented when needed. */
    UNUSED(oi);
}

size_t fbtreeOIEstimateMemory(OrderedIndex *oi, size_t sample_size) {
    /* TODO: implement proper memory estimation by sampling nodes.
     * For now, approximate: each item is ~(SCORE_SIZE + avg_ele_len + sds_header + node_overhead). */
    UNUSED(sample_size);
    unsigned long len = fbtreeLength((fbtreeIndex *)oi);
    /* Rough estimate: 64 bytes per item (sds + node slot overhead) */
    return len * 64;
}

/* ========== Defrag ========== */

OrderedIndex *fbtreeOIDefragInternals(OrderedIndex *oi, void *(*defragfn)(void *)) {
    /* fbtree nodes are fixed-size allocations — defrag the index struct itself. */
    void *newptr = defragfn(oi);
    return newptr ? (OrderedIndex *)newptr : oi;
}

unsigned long fbtreeOIScanDefrag(OrderedIndex *oi, unsigned long cursor, OrderedIndexDefragCallback callback, void *ctx, void *(*defragfn)(void *)) {
    /* TODO: implement incremental defrag scan over fbtree leaf nodes.
     * For now, no-op — returns 0 (complete). */
    UNUSED(oi);
    UNUSED(cursor);
    UNUSED(callback);
    UNUSED(ctx);
    UNUSED(defragfn);
    return 0;
}

/* ========== Debug ========== */

int fbtreeOIVerifyIntegrity(OrderedIndex *oi, char *errmsg, size_t errmsg_len) {
    if (fbtreeDebugValidate((fbtreeIndex *)oi, false)) {
        errmsg[0] = '\0';
        return 1;
    }
    snprintf(errmsg, errmsg_len, "fbtree integrity check failed");
    return 0;
}
