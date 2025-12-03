/* Feature B-Tree implementation of the ordered index interface. */

#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include "ordered_index.h"
#include "serverassert.h"
#include "zmalloc.h"

/* Anti-warning macro... */
#ifndef UNUSED
#define UNUSED(V) ((void)V)
#endif

#define NODE_SIZE 64
#define FEATURE_SIZE 4

typedef struct static_string {
    size_t len;
    const char buf[];
} static_string; 

struct nodeFlags {
    uint8_t is_ordered : 1;
    uint8_t is_leaf : 1;
    // TODO could store is_rightmost when next pointer is last child instead of next sibling 
    // TODO there are unused bits here, and packing into node structs is inefficient
};

/* Abstract type: just for type checking and definition of common fields */
typedef struct {
    struct nodeFlags flags;
} node;

typedef struct {
    struct nodeFlags flags;
    uint8_t num_anchor_keys;
    // uint8_t prefix_len; // TODO add prefix compression stuff
    // char prefix[8];
    node *next; /* sibling or last child */
    char features[FEATURE_SIZE][NODE_SIZE];
    node *children[NODE_SIZE];
} innerNode;

typedef struct leafNode {
    struct nodeFlags flags;
    uint64_t presence_bitmap;
    char *high_key;
    struct leftNode *prev;
    struct leafNode *next;
    // char tags[NODE_SIZE]; // TODO add leaf hash tag stuff
    static_string *values[NODE_SIZE];
} leafNode;
static_assert(NODE_SIZE <= 64, "NODE_SIZE must be <= 64 to fit in presence_bitmap");

typedef struct {
    node *root;
    unsigned long length;
} fbtreeIndex;

typedef struct {
    leafNode *current_leaf;
    uint8_t current_index;
    uint8_t leaf_count;
} fbtreeIterator;
static_assert(sizeof(OrderedIndexIterator) >= sizeof(fbtreeIterator), "Iterator size");

static_string *staticStringCopy(const static_string* src) {
    size_t data_len = sizeof(static_string) + src->len;
    static_string *copy = zmalloc(data_len);
    memcpy(copy, src, data_len);
    return copy;
}

static innerNode *innerNodeCreate(void) {
    innerNode *node = zmalloc(sizeof(*node));
    node->flags.is_ordered = 0;
    node->flags.is_leaf = 0;
    node->num_anchor_keys = 0;
    // node->prefix_len = 0;
    // memset(node->prefix, 0, sizeof(node->prefix));
    node->next = NULL;
    memset(node->features, 0, sizeof(node->features));
    memset(node->children, 0, sizeof(node->children));
    return node;
}

static leafNode *leafNodeCreate(void) {
    leafNode *node = zmalloc(sizeof(*node));
    node->flags.is_ordered = 0;
    node->flags.is_leaf = 1;
    node->presence_bitmap = 0;
    node->high_key = NULL;
    node->prev = NULL;
    node->next = NULL;
    // memset(node->tags, 0, sizeof(node->tags));
    memset(node->values, 0, sizeof(node->values));
    return node;
}

fbtreeIndex *fbtreeCreate(void) {
    fbtreeIndex *fbt = zmalloc(sizeof(*fbt));
    fbt->root = NULL;
    fbt->length = 0;
    return fbt;
}

static void freeNodeRecursive(node *n) {
    if (!n) return;

    if (n->flags.is_leaf) {
        leafNode *leaf = (leafNode *)n;
        for (int i = 0; i < NODE_SIZE; i++) {
            if (leaf->presence_bitmap & (1ULL << i)) {
                zfree(leaf->values[i]);
            }
        }
        zfree(leaf);
    } else {
        innerNode *inner = (innerNode *)n;
        for (int i = 0; i < NODE_SIZE; i++) {
            if (inner->children[i] != NULL) {
                freeNodeRecursive(inner->children[i]);
            }
        }
        zfree(inner);
    }
}

void fbtreeFree(fbtreeIndex *fbt) {
    freeNodeRecursive(fbt->root);
    zfree(fbt);
}

/* Inline string comparison for sorting.
 * Returns: <0 if a < b, 0 if a == b, >0 if a > b (standard strcmp convention) */
static inline int compareStrings(const static_string *a, const static_string *b) {
    size_t min_len = a->len < b->len ? a->len : b->len;
    int cmp = memcmp(a->buf, b->buf, min_len);
    if (cmp != 0) return cmp;
    /* Shorter string comes first if prefixes match */
    return (a->len < b->len) ? -1 : (a->len > b->len) ? 1 : 0;
}

static void leafNodeEnsureSort(leafNode *leaf) {
    if (leaf->flags.is_ordered) return;

    uint64_t bitmap = leaf->presence_bitmap;
    const int count = __builtin_popcountll(bitmap);
    if (count <= 1) {
        leaf->flags.is_ordered = 1;
        return;
    }
    
    /* Collect valid entries into temporary array */
    static_string *temp[NODE_SIZE];
    for (int j = 0; j < count; j++) {
        int i = __builtin_ctzll(bitmap);  // Find lowest set bit
        bitmap &= bitmap - 1;  // Clear lowest set bit
        temp[j] = leaf->values[i];
    }
    
    /* Insertion sort - optimal for small arrays */
    for (int i = 1; i < count; i++) {
        static_string *key = temp[i];
        
        /* Shift elements greater than key to the right */
        int j = i;
        while (j > 0 && compareStrings(temp[j - 1], key) > 0) {
            temp[j] = temp[j - 1];
            j--;
        }
        temp[j] = key;
    }
    
    /* Rebuild array compacted and sorted */
    memcpy(leaf->values, temp, count * sizeof(static_string *));
    leaf->presence_bitmap = (count == 64) ? ~0ULL : (1ULL << count) - 1;
    leaf->flags.is_ordered = 1;
}

static void leafNodeInsert(leafNode *leaf, const static_string *string) {
    assert(leaf->presence_bitmap != ~0ULL);

    if (leaf->flags.is_ordered) {
        /* Binary search for insertion point */
        const int count = __builtin_popcountll(leaf->presence_bitmap);
        int left = 0, right = count;
        while (left < right) {
            int mid = (left + right) / 2;
            if (compareStrings(leaf->values[mid], string) < 0) {
                left = mid + 1;
            } else {
                right = mid;
            }
        }
        
        /* Shift elements right to make space */
        memmove(&leaf->values[left + 1], &leaf->values[left], 
                (count - left) * sizeof(static_string *));
        
        /* Insert at position */
        leaf->values[left] = staticStringCopy(string);
        leaf->presence_bitmap = (1ULL << (count + 1)) - 1;
    } else {
        /* Unordered: find first empty slot */
        int slot = __builtin_ctzll(~leaf->presence_bitmap);
        leaf->values[slot] = staticStringCopy(string);
        leaf->presence_bitmap |= (1ULL << slot);
    }
}

void fbtreeInsert(fbtreeIndex *fbt, static_string *string) {
    if (fbt->root == NULL) {
        fbt->root = (node *)leafNodeCreate();
    }

    node *current = fbt->root;
    assert(current->flags.is_leaf); // TODO handle multi-level trees

    leafNode *leaf = (leafNode *)current;
    
    assert(leaf->presence_bitmap != ~0ULL); // TODO handle leaf splits

    leafNodeInsert(leaf, string);
    fbt->length++;
}

static static_string *leafNodeLookup(leafNode *leaf, const char *key, size_t key_len) {
    if (leaf->flags.is_ordered) {
        assert(false); // TODO: binary search for ordered nodes
    } else {
        /* Linear scan for unordered nodes */
        for (int i = 0; i < NODE_SIZE; i++) {
            if (leaf->presence_bitmap & (1ULL << i)) {
                static_string *str = (static_string *)leaf->values[i];
                if (str->len == key_len && memcmp(str->buf, key, key_len) == 0) {
                    return str;
                }
            }
        }
    }
    return NULL;
}

static_string *fbtreeLookup(fbtreeIndex *fbt, const char *key, size_t key_len) {
    if (fbt->root == NULL) {
        return NULL;
    }
    
    node *current = fbt->root;
    assert(current->flags.is_leaf); // TODO: handle multi-level trees
    
    leafNode *leaf = (leafNode *)current;
    return leafNodeLookup(leaf, key, key_len);
}

static OrderedIndexItem *fbtreeScoreEleInsert(OrderedIndex *idx, double score, const_sds ele) {
    UNUSED(idx); UNUSED(score); UNUSED(ele);
    assert(false); // TODO: implement insert with score/element
    return NULL;
}

static void fbtreeDelete(OrderedIndex *idx, OrderedIndexItem *pos) {
    UNUSED(idx); UNUSED(pos);
    assert(false); // TODO: implement delete
}

static OrderedIndexItem *fbtreeGetByRank(OrderedIndex *idx, unsigned long rank) {
    UNUSED(idx); UNUSED(rank);
    assert(false); // TODO: implement get by rank
    return NULL;
}

static unsigned long fbtreeGetRank(OrderedIndex *idx, const OrderedIndexItem *pos) {
    UNUSED(idx); UNUSED(pos);
    assert(false); // TODO: implement get rank
    return 0;
}

unsigned long fbtreeLength(fbtreeIndex *fbt) {
    return fbt->length;
}

void fbtreeResetIterator(fbtreeIterator *it) {
    it->current_leaf = NULL;
    it->current_index = 0;
    it->leaf_count = 0;
}

void fbtreeInitIterator(fbtreeIterator *it, fbtreeIndex *fbt) {
    if (!fbt->root) {
        fbtreeResetIterator(it);
        return;
    }

    assert(fbt->root->flags.is_leaf); // TODO: implement multi-level trees, traverse to first leaf

    leafNode *leaf = (leafNode *)fbt->root;
    leafNodeEnsureSort(leaf);
    it->current_leaf = leaf;
    it->current_index = 0;
    it->leaf_count = __builtin_popcountll(leaf->presence_bitmap);
}

bool fbtreeNext(fbtreeIterator *it, static_string **pos) {
    if (!it->current_leaf) return false;
    
    while (it->current_leaf) {
        while (it->current_index < NODE_SIZE) {
            if (it->current_leaf->presence_bitmap & (1ULL << it->current_index)) {
                *pos = it->current_leaf->values[it->current_index];
                it->current_index++;
                return true;
            }
            it->current_index++;
        }
        it->current_leaf = it->current_leaf->next;
        if (it->current_leaf) {
            leafNodeEnsureSort(it->current_leaf);
            it->current_index = 0;
        }
    }
    return false;
}

bool fbtreePrev(fbtreeIterator *it, static_string **pos) {
    UNUSED(it); UNUSED(pos);
    assert(false); // TODO: implement backward iteration
    return false;
}

void fbtreeSeekToRank(fbtreeIterator *it, unsigned long rank) {
    UNUSED(it); UNUSED(rank);
}

static void fbtreeGetElementRaw(const OrderedIndexItem *pos, const char **ptr, size_t *len) {
    UNUSED(pos);
    *ptr = NULL;
    *len = 0;
    assert(false); // TODO: pack score and element into binary string and use as key
}

static double fbtreeGetScore(const OrderedIndexItem *pos) {
    UNUSED(pos);
    assert(false); // TODO: pack score and element into binary string and use as key
    return 0.0;
}

static OrderedIndexItem *fbtreeUpdateScore(OrderedIndex *idx, OrderedIndexItem *pos, double newscore) {
    UNUSED(idx); UNUSED(pos); UNUSED(newscore);
    assert(false); // TODO: pack score and element into binary string and use as key
    return NULL;
}

static unsigned long fbtreeDeleteRangeByScore(OrderedIndex *idx, double min, double max, int min_ex, int max_ex) {
    UNUSED(idx); UNUSED(min); UNUSED(max); UNUSED(min_ex); UNUSED(max_ex);
    assert(false); // TODO: pack score and element into binary string and use as key
    return 0;
}

static unsigned long fbtreeDeleteRangeByRank(OrderedIndex *idx, unsigned long start, unsigned long end) {
    UNUSED(idx); UNUSED(start); UNUSED(end);
    assert(false); // TODO: implement delete range by rank
    return 0;
}

/* Wrapper functions for OrderedIndexOps interface */
static OrderedIndex *fbtreeCreateWrapper(void) {
    return (OrderedIndex *)fbtreeCreate();
}

static void fbtreeFreeWrapper(OrderedIndex *idx) {
    fbtreeFree((fbtreeIndex *)idx);
}

static unsigned long fbtreeLengthWrapper(OrderedIndex *idx) {
    return fbtreeLength((fbtreeIndex *)idx);
}

static void fbtreeInitIteratorWrapper(OrderedIndexIterator *iter, OrderedIndex *idx) {
    fbtreeInitIterator((fbtreeIterator *)iter, (fbtreeIndex *)idx);
}

static void fbtreeResetIteratorWrapper(OrderedIndexIterator *iter) {
    fbtreeResetIterator((fbtreeIterator *)iter);
}

static bool fbtreeNextWrapper(OrderedIndexIterator *iter, OrderedIndexItem **pos) {
    return fbtreeNext((fbtreeIterator *)iter, (static_string **)pos);
}

static bool fbtreePrevWrapper(OrderedIndexIterator *iter, OrderedIndexItem **pos) {
    return fbtreePrev((fbtreeIterator *)iter, (static_string **)pos);
}

static void fbtreeSeekToRankWrapper(OrderedIndexIterator *iter, unsigned long rank) {
    fbtreeSeekToRank((fbtreeIterator *)iter, rank);
}

static void fbtreeSeekToScoreRange(OrderedIndexIterator *iter, double min, double max, int min_ex, int max_ex, long offset) {
    UNUSED(iter); UNUSED(min); UNUSED(max); UNUSED(min_ex); UNUSED(max_ex); UNUSED(offset);
    assert(false); // TODO: pack score and element into binary string and use as key
}

const OrderedIndexOps fbtreeOrderedIndexOps = {
    .create = fbtreeCreateWrapper,
    .free = fbtreeFreeWrapper,
    .insert = fbtreeScoreEleInsert,
    .delete = fbtreeDelete,
    .get_by_rank = fbtreeGetByRank,
    .get_rank = fbtreeGetRank,
    .length = fbtreeLengthWrapper,
    .init_iterator = fbtreeInitIteratorWrapper,
    .reset_iterator = fbtreeResetIteratorWrapper,
    .next = fbtreeNextWrapper,
    .prev = fbtreePrevWrapper,
    .seek_to_rank = fbtreeSeekToRankWrapper,
    .seek_to_score_range = fbtreeSeekToScoreRange,
    .get_element_raw = fbtreeGetElementRaw,
    .get_score = fbtreeGetScore,
    .update_score = fbtreeUpdateScore,
    .delete_range_by_score = fbtreeDeleteRangeByScore,
    .delete_range_by_rank = fbtreeDeleteRangeByRank,
};
