#ifndef FBTREE_ORDERED_INDEX_H
#define FBTREE_ORDERED_INDEX_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
typedef struct OrderedIndex OrderedIndex;

/* Static string type used internally */
typedef struct static_string {
    size_t len;
    const char buf[];
} static_string;

typedef struct fbtreeIndex fbtreeIndex;

typedef struct fbtreeIterator {
    void *current_leaf;
    uint8_t current_index;
    uint8_t leaf_count;
} fbtreeIterator;

/* Internal API for testing */
fbtreeIndex *fbtreeCreate(void);
void fbtreeInsert(fbtreeIndex *fbt, static_string *string);
static_string *fbtreeLookup(fbtreeIndex *fbt, const char *key, size_t key_len);
void fbtreeFree(fbtreeIndex *fbt);
unsigned long fbtreeLength(fbtreeIndex *fbt);
void fbtreeInitIterator(fbtreeIterator *it, fbtreeIndex *fbt);
bool fbtreeNext(fbtreeIterator *it, static_string **pos);

#endif /* FBTREE_ORDERED_INDEX_H */
