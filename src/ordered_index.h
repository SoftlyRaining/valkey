#ifndef ORDERED_INDEX_H
#define ORDERED_INDEX_H

#include "sds.h"

/* Opaque types for ordered index and positions */
typedef struct OrderedIndex OrderedIndex;
typedef struct OrderedIndexPosition OrderedIndexPosition;

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
    
    /* Iteration */
    OrderedIndexPosition *(*first)(OrderedIndex *idx);
    OrderedIndexPosition *(*next)(OrderedIndexPosition *pos);
    OrderedIndexPosition *(*last)(OrderedIndex *idx);
    OrderedIndexPosition *(*prev)(OrderedIndexPosition *pos);
    
    /* Position access */
    void (*get_element_raw)(const OrderedIndexPosition *pos, const char **ptr, size_t *len);
    double (*get_score)(const OrderedIndexPosition *pos);
    
    /* Score update - returns new position (may relocate) */
    OrderedIndexPosition *(*update_score)(OrderedIndex *idx, OrderedIndexPosition *pos, double newscore);
    
    /* Range deletion - returns count deleted */
    unsigned long (*delete_range_by_score)(OrderedIndex *idx, double min, double max, int min_ex, int max_ex);
    unsigned long (*delete_range_by_rank)(OrderedIndex *idx, unsigned long start, unsigned long end);
    
    /* Range query - find nth element in score range (n can be negative for reverse) */
    OrderedIndexPosition *(*find_nth_in_range)(OrderedIndex *idx, double min, double max, int min_ex, int max_ex, long n);
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

static inline OrderedIndexPosition *orderedIndexFirst(const OrderedIndexOps *ops, OrderedIndex *idx) {
    return ops->first(idx);
}

static inline OrderedIndexPosition *orderedIndexNext(const OrderedIndexOps *ops, OrderedIndexPosition *pos) {
    return ops->next(pos);
}

static inline OrderedIndexPosition *orderedIndexLast(const OrderedIndexOps *ops, OrderedIndex *idx) {
    return ops->last(idx);
}

static inline OrderedIndexPosition *orderedIndexPrev(const OrderedIndexOps *ops, OrderedIndexPosition *pos) {
    return ops->prev(pos);
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

static inline OrderedIndexPosition *orderedIndexFindNthInRange(const OrderedIndexOps *ops, OrderedIndex *idx, double min, double max, int min_ex, int max_ex, long n) {
    return ops->find_nth_in_range(idx, min, max, min_ex, max_ex, n);
}

/* Available implementations */
extern const OrderedIndexOps skiplistOrderedIndexOps;

#endif /* ORDERED_INDEX_H */
