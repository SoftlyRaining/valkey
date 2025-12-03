#include "server.h"
#include "ordered_index.h"

/* Skiplist implementation of OrderedIndex interface */

static OrderedIndex *skiplistCreate(void) {
    return (OrderedIndex *)zslCreate();
}

static void skiplistFree(OrderedIndex *idx) {
    zslFree((zskiplist *)idx);
}

static OrderedIndexItem *skiplistInsert(OrderedIndex *idx, double score, const_sds ele) {
    return (OrderedIndexItem *)zslInsert((zskiplist *)idx, score, ele);
}

static void skiplistDelete(OrderedIndex *idx, OrderedIndexItem *node) {
    zslDelete((zskiplist *)idx, (zskiplistNode *)node);
}

static OrderedIndexItem *skiplistGetByRank(OrderedIndex *idx, unsigned long rank) {
    return (OrderedIndexItem *)zslGetElementByRank((zskiplist *)idx, rank);
}

static unsigned long skiplistGetRank(OrderedIndex *idx, const OrderedIndexItem *node) {
    return zslGetRank((zskiplist *)idx, (const zskiplistNode *)node);
}

static unsigned long skiplistLength(OrderedIndex *idx) {
    return zslGetLength((zskiplist *)idx);
}

static void skiplistInitIterator(OrderedIndexIterator *iter, OrderedIndex *idx) {
    zslInitIterator((zskiplistIterator *)iter, (zskiplist *)idx);
}

static void skiplistResetIterator(OrderedIndexIterator *iter) {
    zslResetIterator((zskiplistIterator *)iter);
}

static bool skiplistNext(OrderedIndexIterator *iter, OrderedIndexItem **pos) {
    return zslNext((zskiplistIterator *)iter, (zskiplistNode **)pos);
}

static bool skiplistPrev(OrderedIndexIterator *iter, OrderedIndexItem **pos) {
    return zslPrev((zskiplistIterator *)iter, (zskiplistNode **)pos);
}

static void skiplistSeekToRank(OrderedIndexIterator *iter, unsigned long rank) {
    zslSeekToRank((zskiplistIterator *)iter, rank);
}

static void skiplistSeekToScoreRange(OrderedIndexIterator *iter, double min, double max, int min_ex, int max_ex, long offset) {
    zslSeekToScoreRange((zskiplistIterator *)iter, min, max, min_ex, max_ex, offset);
}

static void skiplistGetElementRaw(const OrderedIndexItem *node, const char **ptr, size_t *len) {
    const zskiplistNode *znode = (const zskiplistNode *)node;
    sds ele = zslGetNodeElement(znode);
    *ptr = ele;
    *len = sdslen(ele);
}

static double skiplistGetScore(const OrderedIndexItem *node) {
    return ((const zskiplistNode *)node)->score;
}

static OrderedIndexItem *skiplistUpdateScore(OrderedIndex *idx, OrderedIndexItem *node, double newscore) {
    zskiplistNode *result = zslUpdateScore((zskiplist *)idx, (zskiplistNode *)node, newscore);
    return result ? (OrderedIndexItem *)result : (OrderedIndexItem *)node;
}

static unsigned long skiplistDeleteRangeByScore(OrderedIndex *idx, double min, double max, int min_ex, int max_ex) {
    zrangespec range = {.min = min, .max = max, .minex = min_ex, .maxex = max_ex};
    return zslDeleteRangeByScore((zskiplist *)idx, &range, NULL);
}

static unsigned long skiplistDeleteRangeByRank(OrderedIndex *idx, unsigned long start, unsigned long end) {
    return zslDeleteRangeByRank((zskiplist *)idx, start, end, NULL);
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
    .init_iterator = skiplistInitIterator,
    .reset_iterator = skiplistResetIterator,
    .next = skiplistNext,
    .prev = skiplistPrev,
    .seek_to_rank = skiplistSeekToRank,
    .seek_to_score_range = skiplistSeekToScoreRange,
    .get_element_raw = skiplistGetElementRaw,
    .get_score = skiplistGetScore,
    .update_score = skiplistUpdateScore,
    .delete_range_by_score = skiplistDeleteRangeByScore,
    .delete_range_by_rank = skiplistDeleteRangeByRank,
};
