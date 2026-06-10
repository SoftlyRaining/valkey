/*
 * Copyright (c) Valkey Contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* OrderedIndex implementation — delegates to the active backend. */

#include "ordered_index.h"
#include "fbtree_ordered_index.h"

/* Lifecycle */

OrderedIndex *orderedIndexCreate(void) {
    return fbtreeOICreate();
}

void orderedIndexFree(OrderedIndex *oi) {
    fbtreeOIFree(oi);
}

/* Modification */

OrderedIndexItem *orderedIndexInsert(OrderedIndex *oi, double score, const char *ele, size_t len) {
    return fbtreeOIInsert(oi, score, ele, len);
}

void orderedIndexDelete(OrderedIndex *oi, OrderedIndexItem *item) {
    fbtreeOIDelete(oi, item);
}

OrderedIndexItem *orderedIndexUpdateScore(OrderedIndex *oi, OrderedIndexItem *item, double newscore) {
    return fbtreeOIUpdateScore(oi, item, newscore);
}

OrderedIndexItem *orderedIndexPopFirst(OrderedIndex *oi) {
    return fbtreeOIPopFirst(oi);
}

OrderedIndexItem *orderedIndexPopLast(OrderedIndex *oi) {
    return fbtreeOIPopLast(oi);
}

void orderedIndexFreeItem(OrderedIndexItem *item) {
    fbtreeOIFreeItem(item);
}

OrderedIndexItem *orderedIndexCreateDetached(double score, const char *ele, size_t len) {
    return fbtreeOICreateDetached(score, ele, len);
}

void orderedIndexDetachedSetScore(OrderedIndexItem *item, double score) {
    fbtreeOIDetachedSetScore(item, score);
}

OrderedIndexItem *orderedIndexInsertDetached(OrderedIndex *oi, OrderedIndexItem *item) {
    return fbtreeOIInsertDetached(oi, item);
}

unsigned long orderedIndexDeleteRangeByScore(OrderedIndex *oi, double min, double max, int min_ex, int max_ex, OrderedIndexOnDelete on_delete, void *ctx) {
    return fbtreeOIDeleteRangeByScore(oi, min, max, min_ex, max_ex, on_delete, ctx);
}

unsigned long orderedIndexDeleteRangeByIndex(OrderedIndex *oi, unsigned long start, unsigned long end, OrderedIndexOnDelete on_delete, void *ctx) {
    return fbtreeOIDeleteRangeByIndex(oi, start, end, on_delete, ctx);
}

unsigned long orderedIndexDeleteRangeByLex(OrderedIndex *oi, const_sds min, const_sds max, int min_ex, int max_ex, OrderedIndexOnDelete on_delete, void *ctx) {
    return fbtreeOIDeleteRangeByLex(oi, min, max, min_ex, max_ex, on_delete, ctx);
}

/* Query */

unsigned long orderedIndexLength(OrderedIndex *oi) {
    return fbtreeOILength(oi);
}

OrderedIndexItem *orderedIndexGetByIndex(OrderedIndex *oi, unsigned long index) {
    return fbtreeOIGetByIndex(oi, index);
}

OrderedIndexItem *orderedIndexGetFirst(OrderedIndex *oi) {
    return skiplistGetFirst(oi);
}

OrderedIndexItem *orderedIndexGetLast(OrderedIndex *oi) {
    return skiplistGetLast(oi);
}

unsigned long orderedIndexGetIndex(OrderedIndex *oi, const OrderedIndexItem *item) {
    return fbtreeOIGetIndex(oi, item);
}

void orderedIndexGetElementRaw(const OrderedIndexItem *item, const char **ptr, size_t *len) {
    fbtreeOIGetElementRaw(item, ptr, len);
}

double orderedIndexGetScore(const OrderedIndexItem *item) {
    return fbtreeOIGetScore(item);
}

unsigned long orderedIndexCountScoreRange(OrderedIndex *oi, double min, double max, int min_ex, int max_ex) {
    return fbtreeOICountScoreRange(oi, min, max, min_ex, max_ex);
}

unsigned long orderedIndexCountLexRange(OrderedIndex *oi, const_sds min, const_sds max, int min_ex, int max_ex) {
    return fbtreeOICountLexRange(oi, min, max, min_ex, max_ex);
}

/* Iterator */

void orderedIndexInitIterator(OrderedIndexIterator *iter, OrderedIndex *oi) {
    fbtreeOIInitIterator(iter, oi);
}

void orderedIndexResetIterator(OrderedIndexIterator *iter) {
    fbtreeOIResetIterator(iter);
}

OrderedIndexItem *orderedIndexNext(OrderedIndexIterator *iter) {
    return fbtreeOINext(iter);
}

OrderedIndexItem *orderedIndexPrev(OrderedIndexIterator *iter) {
    return fbtreeOIPrev(iter);
}

void orderedIndexSeekToIndex(OrderedIndexIterator *iter, unsigned long index) {
    fbtreeOISeekToIndex(iter, index);
}

void orderedIndexSeekToScoreRange(OrderedIndexIterator *iter, double min, double max, int min_ex, int max_ex, long offset) {
    fbtreeOISeekToScoreRange(iter, min, max, min_ex, max_ex, offset);
}

void orderedIndexSeekToLexRange(OrderedIndexIterator *iter, const_sds min, const_sds max, int min_ex, int max_ex, long offset) {
    fbtreeOISeekToLexRange(iter, min, max, min_ex, max_ex, offset);
}

/* Memory */

void orderedIndexDismissMemory(OrderedIndex *oi) {
    fbtreeOIDismissMemory(oi);
}

size_t orderedIndexEstimateMemory(OrderedIndex *oi, size_t sample_size) {
    return fbtreeOIEstimateMemory(oi, sample_size);
}

OrderedIndex *orderedIndexDefragInternals(OrderedIndex *oi, void *(*defragfn)(void *)) {
    return fbtreeOIDefragInternals(oi, defragfn);
}

unsigned long orderedIndexScanDefrag(OrderedIndex *oi, unsigned long cursor, OrderedIndexDefragCallback callback, void *ctx, void *(*defragfn)(void *)) {
    return fbtreeOIScanDefrag(oi, cursor, callback, ctx, defragfn);
}

/* Not declared in ordered_index.h — debug-only introspection. */
int orderedIndexGetDepth(OrderedIndex *oi) {
    (void)oi;
    return 0; /* TODO: expose fbtree depth */
}
