#ifndef FBTREE_ORDERED_INDEX_H
#define FBTREE_ORDERED_INDEX_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "static_string.h"

/* For zset entries, the string contains: [8-byte normalized score][element bytes]
 * The normalized score is stored in big-endian for lexicographic ordering.
 * TODO: Add endian conversion (htonu64/ntohu64) when packing/unpacking scores
 *       to support big-endian platforms. See endianconv.h */

typedef struct fbtreeIndex fbtreeIndex;

/* Opaque iterator type that can be stack allocated */
typedef uint64_t fbtreeIterator[3];

/* Internal API for testing */
fbtreeIndex *fbtreeCreate(void);
static_string fbtreeInsert(fbtreeIndex *fbt, static_string string);
bool fbtreeDelete(fbtreeIndex *fbt, const_static_string key);
void fbtreeFree(fbtreeIndex *fbt);
unsigned long fbtreeLength(fbtreeIndex *fbt);
void fbtreeInitIterator(fbtreeIterator *iterator, fbtreeIndex *fbt);
void fbtreeResetIterator(fbtreeIterator *iterator);
bool fbtreeNext(fbtreeIterator *iterator, const_static_string *pos);
bool fbtreePrev(fbtreeIterator *iterator, const_static_string *pos);

void fbtreeSeekToRank(fbtreeIterator *iterator, unsigned long rank);
const_static_string fbtreeGetAtRank(fbtreeIndex *fbt, unsigned long rank);
long fbtreeGetRankOfItem(fbtreeIndex *fbt, const_static_string item);

/* Score lookup - finds first element where 8-byte score prefix matches.
 * Returns true if found, with iterator positioned at that element.
 * Returns false if no such element exists. */
bool fbtreeLookupByScore(fbtreeIndex *fbt, const char *score, fbtreeIterator *iterator);

/* Debug functions */
bool fbtreeDebugValidate(fbtreeIndex *fbt, bool verbose);

#endif /* FBTREE_ORDERED_INDEX_H */
