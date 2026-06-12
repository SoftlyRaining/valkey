/*
 * Copyright (c) Valkey Contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* OrderedIndex implementation — delegates to the active backend.
 * Default: fbtree. Build with ORDERED_INDEX_SKIPLIST=yes to use skiplist. */

// clang-format off
#include "ordered_index.h"

#ifdef ORDERED_INDEX_SKIPLIST
#include "skiplist_ordered_index.h"
#define BACKEND(fn) skiplist##fn
#else
#include "fbtree_ordered_index.h"
#define BACKEND(fn) fbtreeOI##fn
#endif

/* Lifecycle */

OrderedIndex *orderedIndexCreate(void) {
    return BACKEND(Create)();
}

void orderedIndexFree(OrderedIndex *oi) {
    BACKEND(Free)(oi);
}

/* Modification */

OrderedIndexItem *orderedIndexInsert(OrderedIndex *oi, double score, const char *ele, size_t len) {
    return BACKEND(Insert)(oi, score, ele, len);
}

void orderedIndexDelete(OrderedIndex *oi, OrderedIndexItem *item) {
    BACKEND(Delete)(oi, item);
}

OrderedIndexItem *orderedIndexUpdateScore(OrderedIndex *oi, OrderedIndexItem *item, double newscore) {
    return BACKEND(UpdateScore)(oi, item, newscore);
}

OrderedIndexItem *orderedIndexPopFirst(OrderedIndex *oi) {
    return BACKEND(PopFirst)(oi);
}

OrderedIndexItem *orderedIndexPopLast(OrderedIndex *oi) {
    return BACKEND(PopLast)(oi);
}

void orderedIndexFreeItem(OrderedIndexItem *item) {
    BACKEND(FreeItem)(item);
}

OrderedIndexItem *orderedIndexCreateDetached(double score, const char *ele, size_t len) {
    return BACKEND(CreateDetached)(score, ele, len);
}

void orderedIndexDetachedSetScore(OrderedIndexItem *item, double score) {
    BACKEND(DetachedSetScore)(item, score);
}

OrderedIndexItem *orderedIndexInsertDetached(OrderedIndex *oi, OrderedIndexItem *item) {
    return BACKEND(InsertDetached)(oi, item);
}

unsigned long orderedIndexDeleteRangeByScore(OrderedIndex *oi, double min, double max, int min_ex, int max_ex, OrderedIndexOnDelete on_delete, void *ctx) {
    return BACKEND(DeleteRangeByScore)(oi, min, max, min_ex, max_ex, on_delete, ctx);
}

unsigned long orderedIndexDeleteRangeByIndex(OrderedIndex *oi, unsigned long start, unsigned long end, OrderedIndexOnDelete on_delete, void *ctx) {
    return BACKEND(DeleteRangeByIndex)(oi, start, end, on_delete, ctx);
}

unsigned long orderedIndexDeleteRangeByLex(OrderedIndex *oi, const_sds min, const_sds max, int min_ex, int max_ex, OrderedIndexOnDelete on_delete, void *ctx) {
    return BACKEND(DeleteRangeByLex)(oi, min, max, min_ex, max_ex, on_delete, ctx);
}

/* Query */

unsigned long orderedIndexLength(OrderedIndex *oi) {
    return BACKEND(Length)(oi);
}

OrderedIndexItem *orderedIndexGetByIndex(OrderedIndex *oi, unsigned long index) {
    return BACKEND(GetByIndex)(oi, index);
}

OrderedIndexItem *orderedIndexGetFirst(OrderedIndex *oi) {
    return BACKEND(GetFirst)(oi);
}

OrderedIndexItem *orderedIndexGetLast(OrderedIndex *oi) {
    return BACKEND(GetLast)(oi);
}

unsigned long orderedIndexGetIndex(OrderedIndex *oi, const OrderedIndexItem *item) {
    return BACKEND(GetIndex)(oi, item);
}

void orderedIndexGetElementRaw(const OrderedIndexItem *item, const char **ptr, size_t *len) {
    BACKEND(GetElementRaw)(item, ptr, len);
}

double orderedIndexGetScore(const OrderedIndexItem *item) {
    return BACKEND(GetScore)(item);
}

unsigned long orderedIndexCountScoreRange(OrderedIndex *oi, double min, double max, int min_ex, int max_ex) {
    return BACKEND(CountScoreRange)(oi, min, max, min_ex, max_ex);
}

unsigned long orderedIndexCountLexRange(OrderedIndex *oi, const_sds min, const_sds max, int min_ex, int max_ex) {
    return BACKEND(CountLexRange)(oi, min, max, min_ex, max_ex);
}

/* Iterator */

void orderedIndexInitIterator(OrderedIndexIterator *iter, OrderedIndex *oi) {
    BACKEND(InitIterator)(iter, oi);
}

void orderedIndexResetIterator(OrderedIndexIterator *iter) {
    BACKEND(ResetIterator)(iter);
}

OrderedIndexItem *orderedIndexNext(OrderedIndexIterator *iter) {
    return BACKEND(Next)(iter);
}

OrderedIndexItem *orderedIndexPrev(OrderedIndexIterator *iter) {
    return BACKEND(Prev)(iter);
}

void orderedIndexSeekToIndex(OrderedIndexIterator *iter, unsigned long index) {
    BACKEND(SeekToIndex)(iter, index);
}

void orderedIndexSeekToScoreRange(OrderedIndexIterator *iter, double min, double max, int min_ex, int max_ex, long offset) {
    BACKEND(SeekToScoreRange)(iter, min, max, min_ex, max_ex, offset);
}

void orderedIndexSeekToLexRange(OrderedIndexIterator *iter, const_sds min, const_sds max, int min_ex, int max_ex, long offset) {
    BACKEND(SeekToLexRange)(iter, min, max, min_ex, max_ex, offset);
}

/* Memory */

void orderedIndexDismissMemory(OrderedIndex *oi) {
    BACKEND(DismissMemory)(oi);
}

size_t orderedIndexEstimateMemory(OrderedIndex *oi, size_t sample_size) {
    return BACKEND(EstimateMemory)(oi, sample_size);
}

OrderedIndex *orderedIndexDefragInternals(OrderedIndex *oi, void *(*defragfn)(void *)) {
    return BACKEND(DefragInternals)(oi, defragfn);
}

unsigned long orderedIndexScanDefrag(OrderedIndex *oi, unsigned long cursor, OrderedIndexDefragCallback callback, void *ctx, void *(*defragfn)(void *)) {
    return BACKEND(ScanDefrag)(oi, cursor, callback, ctx, defragfn);
}

/* Not declared in ordered_index.h — debug-only introspection. */
int orderedIndexGetDepth(OrderedIndex *oi) {
    (void)oi;
    return 0; /* TODO: expose fbtree depth */
}
