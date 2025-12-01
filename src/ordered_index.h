#ifndef ORDERED_INDEX_H
#define ORDERED_INDEX_H

#include "sds.h"

/* Opaque types for ordered index, positions, and iterators */
typedef struct OrderedIndex OrderedIndex;
typedef struct OrderedIndexPosition OrderedIndexPosition;
typedef uint64_t OrderedIndexIterator[2];

/* Operations interface for ordered index implementations */
typedef struct OrderedIndexOps {
    /* Lifecycle */
    OrderedIndex *(*create)(void);
    void (*free)(OrderedIndex *idx);
    
    /* Modification */
    OrderedIndexPosition *(*insert)(OrderedIndex *idx, double score, const_sds ele);
    void (*delete)(OrderedIndex *idx, OrderedIndexPosition *pos);
    
    /* Query by rank (1-based) */
    OrderedIndexPosition *(*get_by_rank)(OrderedIndex *idx, unsigned long rank);
    unsigned long (*get_rank)(OrderedIndex *idx, const OrderedIndexPosition *pos);
    
    /* Metadata */
    unsigned long (*length)(OrderedIndex *idx);

    /* Iterator */
    void (*init_iterator)(OrderedIndexIterator *iter, OrderedIndex *idx);
    void (*reset_iterator)(OrderedIndexIterator *iter);
    bool (*next)(OrderedIndexIterator *iter, OrderedIndexPosition **pos);
    bool (*prev)(OrderedIndexIterator *iter, OrderedIndexPosition **pos);
    void (*seek_to_rank)(OrderedIndexIterator *iter, unsigned long rank);
    void (*seek_to_score_range)(OrderedIndexIterator *iter, double min, double max, int min_ex, int max_ex, long offset);

    /* Position access */
    void (*get_element_raw)(const OrderedIndexPosition *pos, const char **ptr, size_t *len);
    double (*get_score)(const OrderedIndexPosition *pos);
    
    /* Score update - returns new position (may relocate) */
    OrderedIndexPosition *(*update_score)(OrderedIndex *idx, OrderedIndexPosition *pos, double newscore);
    
    /* Range deletion - returns count deleted */
    unsigned long (*delete_range_by_score)(OrderedIndex *idx, double min, double max, int min_ex, int max_ex);
    unsigned long (*delete_range_by_rank)(OrderedIndex *idx, unsigned long start, unsigned long end);

    /* TODO: Add interface methods for memory management:
     * - Memory dismiss (for CoW optimization during fork/snapshot)
     * - Memory defrag (for active defragmentation when nodes are relocated)
     * These will eliminate direct access to forward/backward pointers in defrag.c and object.c */
} OrderedIndexOps;

/* Inline wrappers for performance (compiler can inline these) */
static inline OrderedIndex *orderedIndexCreate(const OrderedIndexOps *ops) {
    return ops->create();
}

static inline void orderedIndexFree(const OrderedIndexOps *ops, OrderedIndex *idx) {
    ops->free(idx);
}

static inline OrderedIndexPosition *orderedIndexInsert(const OrderedIndexOps *ops, OrderedIndex *idx, double score, const_sds ele) {
    return ops->insert(idx, score, ele);
}

static inline void orderedIndexDelete(const OrderedIndexOps *ops, OrderedIndex *idx, OrderedIndexPosition *pos) {
    ops->delete(idx, pos);
}

static inline OrderedIndexPosition *orderedIndexGetByRank(const OrderedIndexOps *ops, OrderedIndex *idx, unsigned long rank) {
    return ops->get_by_rank(idx, rank);
}

static inline unsigned long orderedIndexGetRank(const OrderedIndexOps *ops, OrderedIndex *idx, const OrderedIndexPosition *pos) {
    return ops->get_rank(idx, pos);
}

static inline unsigned long orderedIndexLength(const OrderedIndexOps *ops, OrderedIndex *idx) {
    return ops->length(idx);
}

static inline void orderedIndexInitIterator(const OrderedIndexOps *ops, OrderedIndexIterator *iter, OrderedIndex *idx) {
    ops->init_iterator(iter, idx);
}

static inline void orderedIndexResetIterator(const OrderedIndexOps *ops, OrderedIndexIterator *iter) {
    ops->reset_iterator(iter);
}

static inline bool orderedIndexNext(const OrderedIndexOps *ops, OrderedIndexIterator *iter, OrderedIndexPosition **pos) {
    return ops->next(iter, pos);
}

static inline bool orderedIndexPrev(const OrderedIndexOps *ops, OrderedIndexIterator *iter, OrderedIndexPosition **pos) {
    return ops->prev(iter, pos);
}

static inline void orderedIndexSeekToRank(const OrderedIndexOps *ops, OrderedIndexIterator *iter, unsigned long rank) {
    ops->seek_to_rank(iter, rank);
}

static inline void orderedIndexSeekToScoreRange(const OrderedIndexOps *ops, OrderedIndexIterator *iter, double min, double max, int min_ex, int max_ex, long offset) {
    ops->seek_to_score_range(iter, min, max, min_ex, max_ex, offset);
}

static inline void orderedIndexGetElementRaw(const OrderedIndexOps *ops, const OrderedIndexPosition *pos, const char **ptr, size_t *len) {
    ops->get_element_raw(pos, ptr, len);
}

static inline double orderedIndexGetScore(const OrderedIndexOps *ops, const OrderedIndexPosition *pos) {
    return ops->get_score(pos);
}

static inline OrderedIndexPosition *orderedIndexUpdateScore(const OrderedIndexOps *ops, OrderedIndex *idx, OrderedIndexPosition *pos, double newscore) {
    return ops->update_score(idx, pos, newscore);
}

static inline unsigned long orderedIndexDeleteRangeByScore(const OrderedIndexOps *ops, OrderedIndex *idx, double min, double max, int min_ex, int max_ex) {
    return ops->delete_range_by_score(idx, min, max, min_ex, max_ex);
}

static inline unsigned long orderedIndexDeleteRangeByRank(const OrderedIndexOps *ops, OrderedIndex *idx, unsigned long start, unsigned long end) {
    return ops->delete_range_by_rank(idx, start, end);
}

/* Available implementations */
extern const OrderedIndexOps skiplistOrderedIndexOps;

#endif /* ORDERED_INDEX_H */
