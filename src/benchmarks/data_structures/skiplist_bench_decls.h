/*
 * Shared declarations for skiplist benchmarks.
 */

#ifndef SKIPLIST_BENCH_DECLS_H
#define SKIPLIST_BENCH_DECLS_H

extern "C" {
typedef char *sds;
typedef const char *const_sds;
sds sdsnewlen(const void *init, size_t initlen);
sds sdsdup(const_sds s);
void sdsfree(sds s);

/* Use opaque types and accessor functions - struct layout has changed */
typedef struct zskiplistNode zskiplistNode;
typedef struct zskiplist zskiplist;

zskiplist *zslCreate(void);
void zslFree(zskiplist *zsl);
zskiplistNode *zslInsert(zskiplist *zsl, double score, const_sds ele);
zskiplistNode *zslGetElementByRank(zskiplist *zsl, unsigned long rank);
unsigned long zslGetRank(zskiplist *zsl, const zskiplistNode *node);
unsigned long zslGetLength(const zskiplist *zsl);
zskiplistNode *zslGetFirst(const zskiplist *zsl);
zskiplistNode *zslGetTail(const zskiplist *zsl);
sds zslGetNodeElement(const zskiplistNode *node);
void zslDelete(zskiplist *zsl, zskiplistNode *node);
zskiplistNode *zslDetachNode(zskiplist *zsl, zskiplistNode *node);
void zslFreeNode(zskiplistNode *node);

typedef struct {
    double min, max;
    int minex, maxex;
} zrangespec;

zskiplistNode *zslNthInRange(zskiplist *zsl, zrangespec *range, long n, long *rank);

/* Iterator API */
typedef uint64_t zskiplistIterator[2];
void zslInitIterator(zskiplistIterator *iterator, zskiplist *zsl);
bool zslNext(zskiplistIterator *iterator, zskiplistNode **nodeptr);
bool zslPrev(zskiplistIterator *iterator, zskiplistNode **nodeptr);
void zslSeekToRank(zskiplistIterator *iterator, unsigned long rank);
}

#endif /* SKIPLIST_BENCH_DECLS_H */
