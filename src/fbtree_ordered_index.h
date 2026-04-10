#ifndef FBTREE_ORDERED_INDEX_H
#define FBTREE_ORDERED_INDEX_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
/* Forward declare sds types to avoid C++ issues with sds.h macros.
 * C code will have sds.h included elsewhere; C++ benchmarks get the typedef here. */
typedef char *sds;
typedef const char *const_sds;

typedef struct fbtreeIndex fbtreeIndex;

/* Opaque iterator type that can be stack allocated */
typedef uint64_t fbtreeIterator[3];

/* Internal API for testing */
fbtreeIndex *fbtreeCreate(void);
sds fbtreeInsert(fbtreeIndex *fbt, sds string);
bool fbtreeDelete(fbtreeIndex *fbt, const_sds key);
sds fbtreePopMin(fbtreeIndex *fbt);
sds fbtreePopMax(fbtreeIndex *fbt);
void fbtreeEmpty(fbtreeIndex *fbt);
void fbtreeFree(fbtreeIndex *fbt);
unsigned long fbtreeLength(fbtreeIndex *fbt);
void fbtreeInitIterator(fbtreeIterator *iterator, fbtreeIndex *fbt);
void fbtreeResetIterator(fbtreeIterator *iterator);
bool fbtreeNext(fbtreeIterator *iterator, const_sds *pos);
bool fbtreePrev(fbtreeIterator *iterator, const_sds *pos);

void fbtreeSeekToRank(fbtreeIterator *iterator, unsigned long rank);
const_sds fbtreeGetAtRank(fbtreeIndex *fbt, unsigned long rank);
long fbtreeGetRankOfItem(fbtreeIndex *fbt, const_sds item);

/* Score seek - positions iterator at first element with score >= given score.
 * Always positions the iterator (even if no exact match). Use fbtreeNext to get elements.
 * If all elements have score < given score, iterator is positioned past end. */
void fbtreeSeekToScore(fbtreeIndex *fbt, const char *score, fbtreeIterator *iterator);

/* Value seek - positions iterator at first element with value >= given value.
 * Uses full sds comparison (not just score prefix).
 * Always positions the iterator (even if no exact match). Use fbtreeNext to get elements.
 * If all elements have value < given value, iterator is positioned past end. */
void fbtreeSeekToValue(fbtreeIndex *fbt, const_sds value, fbtreeIterator *iterator);

/* Optional callback invoked for each item being deleted, before sdsfree.
 * Pass NULL for callback/callback_ctx to skip. */

/* Range deletion */
unsigned long fbtreeDeleteRangeByRank(fbtreeIndex *fbt, unsigned long start_rank, unsigned long end_rank, void (*callback)(sds item, void *ctx), void *callback_ctx);
unsigned long fbtreeDeleteRangeByScore(fbtreeIndex *fbt, const char *min_score, const char *max_score, int min_ex, int max_ex, void (*callback)(sds item, void *ctx), void *callback_ctx);
unsigned long fbtreeDeleteRangeByValue(fbtreeIndex *fbt, const_sds min_val, const_sds max_val, int min_ex, int max_ex, void (*callback)(sds item, void *ctx), void *callback_ctx);

/* Debug functions */
bool fbtreeDebugValidate(fbtreeIndex *fbt, bool verbose);
bool fbtreeDebugValidateMergeEnforcement(fbtreeIndex *fbt);

#endif /* FBTREE_ORDERED_INDEX_H */
