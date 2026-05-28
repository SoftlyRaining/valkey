/*
 * Copyright (c) Valkey Contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FBTREE_ORDERED_INDEX_H
#define FBTREE_ORDERED_INDEX_H

/* fbtree (flat B+ tree) backend for the OrderedIndex interface.
 *
 * This file declares the fbtree-specific implementations of all OrderedIndex
 * operations. These are called by ordered_index.c (the dispatch layer) when
 * ORDERED_INDEX_FBTREE is defined, and should not be called directly.
 *
 * The fbtree stores [8-byte normalized score][element] as its key, enabling
 * lexicographic byte comparison to match numeric score ordering. */

#include "ordered_index.h"

/* Lifecycle */
OrderedIndex *fbtreeOICreate(void);
void fbtreeOIFree(OrderedIndex *oi);

/* Modification */
OrderedIndexItem *fbtreeOIInsert(OrderedIndex *oi, double score, const char *ele, size_t len);
void fbtreeOIDelete(OrderedIndex *oi, OrderedIndexItem *item);
OrderedIndexItem *fbtreeOIUpdateScore(OrderedIndex *oi, OrderedIndexItem *item, double newscore);
OrderedIndexItem *fbtreeOIPopFirst(OrderedIndex *oi);
OrderedIndexItem *fbtreeOIPopLast(OrderedIndex *oi);
void fbtreeOIFreeItem(OrderedIndexItem *item);
OrderedIndexItem *fbtreeOICreateDetached(double score, const char *ele, size_t len);
void fbtreeOIDetachedSetScore(OrderedIndexItem *item, double score);
OrderedIndexItem *fbtreeOIInsertDetached(OrderedIndex *oi, OrderedIndexItem *item);
unsigned long fbtreeOIDeleteRangeByScore(OrderedIndex *oi, double min, double max, int min_ex, int max_ex, OrderedIndexOnDelete on_delete, void *ctx);
unsigned long fbtreeOIDeleteRangeByIndex(OrderedIndex *oi, unsigned long start, unsigned long end, OrderedIndexOnDelete on_delete, void *ctx);
unsigned long fbtreeOIDeleteRangeByLex(OrderedIndex *oi, const_sds min, const_sds max, int min_ex, int max_ex, OrderedIndexOnDelete on_delete, void *ctx);

/* Query */
unsigned long fbtreeOILength(OrderedIndex *oi);
OrderedIndexItem *fbtreeOIGetByIndex(OrderedIndex *oi, unsigned long index);
unsigned long fbtreeOIGetIndex(OrderedIndex *oi, const OrderedIndexItem *item);
void fbtreeOIGetElementRaw(const OrderedIndexItem *item, const char **ptr, size_t *len);
double fbtreeOIGetScore(const OrderedIndexItem *item);
unsigned long fbtreeOICountScoreRange(OrderedIndex *oi, double min, double max, int min_ex, int max_ex);
unsigned long fbtreeOICountLexRange(OrderedIndex *oi, const_sds min, const_sds max, int min_ex, int max_ex);

/* Iterator */
void fbtreeOIInitIterator(OrderedIndexIterator *iter, OrderedIndex *oi);
void fbtreeOIResetIterator(OrderedIndexIterator *iter);
OrderedIndexItem *fbtreeOINext(OrderedIndexIterator *iter);
OrderedIndexItem *fbtreeOIPrev(OrderedIndexIterator *iter);
void fbtreeOISeekToIndex(OrderedIndexIterator *iter, unsigned long index);
void fbtreeOISeekToScoreRange(OrderedIndexIterator *iter, double min, double max, int min_ex, int max_ex, long offset);
void fbtreeOISeekToLexRange(OrderedIndexIterator *iter, const_sds min, const_sds max, int min_ex, int max_ex, long offset);

/* Memory */
void fbtreeOIDismissMemory(OrderedIndex *oi);
size_t fbtreeOIEstimateMemory(OrderedIndex *oi, size_t sample_size);

/* Defrag */
OrderedIndex *fbtreeOIDefragInternals(OrderedIndex *oi, void *(*defragfn)(void *));
unsigned long fbtreeOIScanDefrag(OrderedIndex *oi, unsigned long cursor, OrderedIndexDefragCallback callback, void *ctx, void *(*defragfn)(void *));

/* Debug */
int fbtreeOIVerifyIntegrity(OrderedIndex *oi, char *errmsg, size_t errmsg_len);

#endif /* FBTREE_ORDERED_INDEX_H */
