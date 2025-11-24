#include "server.h"
#include "ordered_index.h"

/* Skiplist implementation of OrderedIndex interface */

static OrderedIndex *skiplistCreate(void) {
    return (OrderedIndex *)zslCreate();
}

static void skiplistFree(OrderedIndex *idx) {
    zslFree((zskiplist *)idx);
}

static OrderedIndexPosition *skiplistInsert(OrderedIndex *idx, double score, const_sds ele) {
    return (OrderedIndexPosition *)zslInsert((zskiplist *)idx, score, ele);
}

static void skiplistDelete(OrderedIndex *idx, OrderedIndexPosition *node) {
    zslDelete((zskiplist *)idx, (zskiplistNode *)node);
}

static OrderedIndexPosition *skiplistGetByRank(OrderedIndex *idx, unsigned long rank) {
    return (OrderedIndexPosition *)zslGetElementByRank((zskiplist *)idx, rank);
}

static unsigned long skiplistGetRank(OrderedIndex *idx, const OrderedIndexPosition *node) {
    return zslGetRank((zskiplist *)idx, (const zskiplistNode *)node);
}

static unsigned long skiplistLength(OrderedIndex *idx) {
    return ((zskiplist *)idx)->length;
}

static OrderedIndexPosition *skiplistFirst(OrderedIndex *idx) {
    zskiplist *zsl = (zskiplist *)idx;
    return (OrderedIndexPosition *)zsl->header->level[0].forward;
}

static OrderedIndexPosition *skiplistNext(OrderedIndexPosition *node) {
    zskiplistNode *znode = (zskiplistNode *)node;
    return (OrderedIndexPosition *)znode->level[0].forward;
}

static OrderedIndexPosition *skiplistLast(OrderedIndex *idx) {
    return (OrderedIndexPosition *)((zskiplist *)idx)->tail;
}

static OrderedIndexPosition *skiplistPrev(OrderedIndexPosition *node) {
    return (OrderedIndexPosition *)((zskiplistNode *)node)->backward;
}

static void skiplistGetElementRaw(const OrderedIndexPosition *node, const char **ptr, size_t *len) {
    const zskiplistNode *znode = (const zskiplistNode *)node;
    sds ele = zslGetNodeElement(znode);
    *ptr = ele;
    *len = sdslen(ele);
}

static double skiplistGetScore(const OrderedIndexPosition *node) {
    return ((const zskiplistNode *)node)->score;
}

static OrderedIndexPosition *skiplistUpdateScore(OrderedIndex *idx, OrderedIndexPosition *node, double newscore) {
    zskiplistNode *result = zslUpdateScore((zskiplist *)idx, (zskiplistNode *)node, newscore);
    return result ? (OrderedIndexPosition *)result : (OrderedIndexPosition *)node;
}

static unsigned long skiplistDeleteRangeByScore(OrderedIndex *idx, double min, double max, int min_ex, int max_ex) {
    zrangespec range = {.min = min, .max = max, .minex = min_ex, .maxex = max_ex};
    return zslDeleteRangeByScore((zskiplist *)idx, &range, NULL);
}

static unsigned long skiplistDeleteRangeByRank(OrderedIndex *idx, unsigned long start, unsigned long end) {
    return zslDeleteRangeByRank((zskiplist *)idx, start, end, NULL);
}

static OrderedIndexPosition *skiplistFindNthInRange(OrderedIndex *idx, double min, double max, int min_ex, int max_ex, long n) {
    zrangespec range = {.min = min, .max = max, .minex = min_ex, .maxex = max_ex};
    return (OrderedIndexPosition *)zslNthInRange((zskiplist *)idx, &range, n, NULL);
}

/* Skiplist implementation ops table */
const OrderedIndexOps skiplistOrderedIndexOps = {
    .create = skiplistCreate,
    .free = skiplistFree,
    .insert = skiplistInsert,
    .delete = skiplistDelete,
    .get_by_rank = skiplistGetByRank,
    .get_rank = skiplistGetRank,
    .length = skiplistLength,
    .first = skiplistFirst,
    .next = skiplistNext,
    .last = skiplistLast,
    .prev = skiplistPrev,
    .get_element_raw = skiplistGetElementRaw,
    .get_score = skiplistGetScore,
    .update_score = skiplistUpdateScore,
    .delete_range_by_score = skiplistDeleteRangeByScore,
    .delete_range_by_rank = skiplistDeleteRangeByRank,
    .find_nth_in_range = skiplistFindNthInRange,
};
