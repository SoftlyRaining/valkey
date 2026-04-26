#ifndef SKIPLIST_INTERNAL_H
#define SKIPLIST_INTERNAL_H

/* Internal skiplist node helpers shared between t_zset.c and
 * skiplist_ordered_index.c.  Not for use outside the skiplist
 * implementation.
 *
 * Callers must include server.h before this header for the full
 * definitions of zrangespec and zlexrangespec. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef ZSKIPLIST_MAXLEVEL
#define ZSKIPLIST_MAXLEVEL 32 /* Should be enough for 2^64 elements */
#endif
#define ZSKIPLIST_MAX_SEARCH 10

/* Skiplist iterator — used directly by the skiplist implementation and
 * cast from OrderedIndexIterator in skiplist_ordered_index.c. */
typedef struct {
    zskiplist *zsl;      /* The skiplist being iterated */
    zskiplistNode *node; /* Current node (NULL before first call) */
} zslIter;

/* Node creation and insertion (used by skiplist_ordered_index.c for detached items) */
zskiplistNode *zslCreateNode(int height, double score, const char *ele, size_t ele_len);
int zslRandomLevel(void);
zskiplistNode *zslInsertNode(zskiplist *zsl, zskiplistNode *node);

/* Additional modification functions */
void zslDelete(zskiplist *zsl, zskiplistNode *node);
zskiplistNode *zslDetachNode(zskiplist *zsl, zskiplistNode *node);
void zslFreeNode(zskiplistNode *node);
zskiplistNode *zslUpdateScore(zskiplist *zsl, zskiplistNode *node, double newscore);

/* Additional query functions */
zskiplistNode *zslGetFirst(const zskiplist *zsl);
double zslGetScore(const zskiplistNode *node);
unsigned long zslGetRank(zskiplist *zsl, const zskiplistNode *node);
zskiplistNode *zslGetElementByRank(zskiplist *zsl, unsigned long rank);
zskiplistNode *zslNthInLexRange(zskiplist *zsl, zlexrangespec *range, long n);

/* Iterator */
void zslInitIterator(zslIter *iter, zskiplist *zsl);
void zslResetIterator(zslIter *iter);
zslIter *zslCreateIterator(zskiplist *zsl);
void zslReleaseIterator(zslIter *iter);
bool zslNext(zslIter *iter, zskiplistNode **nodeptr);
bool zslPrev(zslIter *iter, zskiplistNode **nodeptr);
void zslSeekToRank(zslIter *iter, unsigned long rank);
void zslSeekToScoreRange(zslIter *iter, double min, double max, int min_ex, int max_ex, long offset);
void zslSeekToLexRange(zslIter *iter, const_sds min, const_sds max, int min_ex, int max_ex, long offset);

/* Internal unlink helper (used by skiplist_ordered_index.c for range deletion) */
void zslDeleteNode(zskiplist *zsl, zskiplistNode *x, zskiplistNode **update);

/* Level-0 span stores the node height, so span accessors treat it specially. */
static inline unsigned long zslGetNodeSpanAtLevel(const zskiplistNode *x, int level) {
    if (level > 0) return x->level[level].span;
    return x->level[level].forward ? 1 : 0;
}

static inline void zslSetNodeSpanAtLevel(zskiplistNode *x, int level, unsigned long span) {
    if (level > 0) x->level[level].span = span;
}

static inline void zslIncrNodeSpanAtLevel(zskiplistNode *x, int level, unsigned long incr) {
    if (level > 0) x->level[level].span += incr;
}

static inline void zslDecrNodeSpanAtLevel(zskiplistNode *x, int level, unsigned long decr) {
    if (level > 0) x->level[level].span -= decr;
}

static inline unsigned long zslGetNodeHeight(const zskiplistNode *x) {
    return x->level[0].span;
}

#endif /* SKIPLIST_INTERNAL_H */
