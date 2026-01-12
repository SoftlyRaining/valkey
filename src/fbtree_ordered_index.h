#ifndef FBTREE_ORDERED_INDEX_H
#define FBTREE_ORDERED_INDEX_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Static string type used internally.
 * For zset entries, buf contains: [8-byte normalized score][element bytes]
 * The normalized score is stored in big-endian for lexicographic ordering.
 * TODO: Add endian conversion (htonu64/ntohu64) when packing/unpacking scores
 *       to support big-endian platforms. See endianconv.h */
typedef struct static_string {
    const size_t len;
    const char buf[];
} static_string;

typedef struct fbtreeIndex fbtreeIndex;

/* Opaque iterator type that can be stack allocated */
typedef uint64_t fbtreeIterator[3];

/* Internal API for testing */
fbtreeIndex *fbtreeCreate(void);
void fbtreeInsert(fbtreeIndex *fbt, static_string *string);
bool fbtreeLookup(fbtreeIndex *fbt, static_string *key);
bool fbtreeDelete(fbtreeIndex *fbt, static_string *key);
void fbtreeFree(fbtreeIndex *fbt);
unsigned long fbtreeLength(fbtreeIndex *fbt);
void fbtreeInitIterator(fbtreeIterator *iterator, fbtreeIndex *fbt);
void fbtreeResetIterator(fbtreeIterator *iterator);
bool fbtreeNext(fbtreeIterator *iterator, static_string **pos);
bool fbtreePrev(fbtreeIterator *iterator, static_string **pos);

void fbtreeSeekToRank(fbtreeIterator *iterator, unsigned long rank);
static_string *fbtreeGetAtRank(fbtreeIndex *fbt, unsigned long rank);
unsigned long fbtreeGetRankOfKey(fbtreeIndex *fbt, static_string *key);

/* Debug functions */
bool fbtreeDebugValidate(fbtreeIndex *fbt, bool verbose);

#endif /* FBTREE_ORDERED_INDEX_H */
