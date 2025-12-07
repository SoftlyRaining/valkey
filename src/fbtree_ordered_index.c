/* Feature B-Tree implementation of the ordered index interface. */

#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include "fbtree_ordered_index.h"
#include "ordered_index.h"
#include "serverassert.h"
#include "zmalloc.h"

/* Anti-warning macro... */
#ifndef UNUSED
#define UNUSED(V) ((void)V)
#endif

#define NODE_SIZE 64
#define FEATURE_SIZE 4
// TODO if feature_size is 4B, we could copy/compare/etc as 32-bit words for speed

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
    static_string *high_key;
    struct leafNode *prev;
    struct leafNode *next;
    // char tags[NODE_SIZE]; // TODO add leaf hash tag stuff
    static_string *values[NODE_SIZE];
} leafNode;
static_assert(NODE_SIZE <= 64, "NODE_SIZE must be <= 64 to fit in presence_bitmap");

struct fbtreeIndex {
    node *root;
    unsigned long length;
};

typedef struct {
    fbtreeIndex *fbt;
    leafNode *current_leaf;
    uint8_t current_index;
    uint8_t leaf_count;
} iter;

static_assert(sizeof(fbtreeIterator) >= sizeof(iter), "Opaque iterator size check");
static_assert(sizeof(OrderedIndexIterator) >= sizeof(iter), "Opaque iterator size check");

/* Conversion from user-facing opaque iterator type to internal struct */
static inline iter *iteratorFromOpaque(fbtreeIterator *iterator) {
    return (iter *)(void *)iterator;
}

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
        if (leaf->high_key) zfree(leaf->high_key);
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

static void innerNodeInsert(innerNode *inner, node *child) {
    assert(inner->num_anchor_keys < NODE_SIZE);

    /* get feature value for new child */
    char child_feature[FEATURE_SIZE];
    if (child->flags.is_leaf) {
        leafNode *child_leaf = (leafNode *)child;
        if (child_leaf->high_key->len < FEATURE_SIZE) {
            memset(child_feature, 0, FEATURE_SIZE);
            memcpy(child_feature, child_leaf->high_key->buf, child_leaf->high_key->len);
        } else {
            memcpy(child_feature, child_leaf->high_key->buf, FEATURE_SIZE);
        }
    } else {
        /* get feature from inner node */
        assert(false); // TODO implement
    }

    /* insert new child node */
    // TODO if features are not sufficient to differentiate, need to do binary search and access child data for full comparison
    int i = inner->num_anchor_keys;
    while (i > 0 && memcmp(child_feature, inner->features[i - 1], FEATURE_SIZE) < 0) {
        memcpy(inner->features[i], inner->features[i - 1], FEATURE_SIZE);
        inner->children[i] = inner->children[i - 1];
        i--;
    }
    memcpy(inner->features[i], child_feature, FEATURE_SIZE);
    inner->children[i] = child;
    inner->num_anchor_keys++;
    // TODO should we update our max feature with our parent node?
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
    /* We assume here there is capacity to insert without splitting */
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

        /* Update high_key if inserting at the end (new maximum) */
        if (left == count) {
            if (leaf->high_key) zfree(leaf->high_key);
            leaf->high_key = staticStringCopy(string);
        }
    } else {
        /* Unordered: find first empty slot */
        int slot = __builtin_ctzll(~leaf->presence_bitmap);
        leaf->values[slot] = staticStringCopy(string);
        leaf->presence_bitmap |= (1ULL << slot);

        /* Update high_key if this is larger than current high_key */
        if (!leaf->high_key || compareStrings(string, (static_string *)leaf->high_key) > 0) {
            if (leaf->high_key) zfree(leaf->high_key);
            leaf->high_key = staticStringCopy(string);
        }
    }
}

static node *leafNodeSplit(leafNode *left_leaf, const static_string *string) {
    leafNodeEnsureSort(left_leaf);
    assert(left_leaf->presence_bitmap == ~0ULL);
    const size_t num_left = NODE_SIZE / 2;
    const size_t num_right = NODE_SIZE - num_left; /* needed if we ever made NODE_SIZE odd */

    /* Create new leaf, insert to right in doubly linked list */
    leafNode *right_leaf = leafNodeCreate();
    right_leaf->flags.is_ordered = 1;
    right_leaf->prev = left_leaf;
    right_leaf->next = left_leaf->next;
    left_leaf->next = right_leaf;
    if (right_leaf->next) right_leaf->next->prev = right_leaf;

    /* Move second half of elements to new leaf */
    memcpy(right_leaf->values, &left_leaf->values[num_left], num_right * sizeof(static_string *));
    left_leaf->presence_bitmap = (1ULL << num_left) - 1;
    right_leaf->presence_bitmap = (1ULL << num_right) - 1;

    /* Update high keys */
    right_leaf->high_key = left_leaf->high_key;
    left_leaf->high_key = staticStringCopy(left_leaf->values[num_left - 1]);

    /* Insert string into appropriate leaf */
    if (compareStrings(string, left_leaf->high_key) <= 0) {
        leafNodeInsert(left_leaf, string);
    } else {
        leafNodeInsert(right_leaf, string);
    }

    return (node *)right_leaf;
}

static node *subtreeInsert(node *n, const static_string *string) {
    assert(n);
    if (n->flags.is_leaf) {
        leafNode *leaf = (leafNode *)n;
        if (leaf->presence_bitmap == ~0ULL) {
            return leafNodeSplit(leaf, string);
        } else {
            leafNodeInsert(leaf, string);
            return NULL;
        }
    } else {
        /* inner node - find correct child for insert */
        assert(false); // TODO implement multi-level insert
    }
}

void fbtreeInsert(fbtreeIndex *fbt, static_string *string) {
    if (fbt->root == NULL) fbt->root = (node *)leafNodeCreate();

    node *new_child = subtreeInsert(fbt->root, string);
    if (new_child) {
        innerNode *new_root = innerNodeCreate();
        innerNodeInsert(new_root, fbt->root);
        innerNodeInsert(new_root, new_child);
        fbt->root = (node *)new_root;
    }
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

void fbtreeResetIterator(fbtreeIterator *iterator) {
    iter *it = iteratorFromOpaque(iterator);
    it->fbt = NULL;
    it->current_leaf = NULL;
    it->current_index = 0;
    it->leaf_count = 0;
}

static void iteratorSetNode(iter *it, leafNode *leaf, bool last_child) {
    if (!leaf) {
        it->fbt = NULL;
        it->current_leaf = NULL;
        it->current_index = 0;
        it->leaf_count = 0;
    } else {
        leafNodeEnsureSort(leaf);
        it->current_leaf = leaf;
        it->leaf_count = __builtin_popcountll(leaf->presence_bitmap);
        if (last_child)
            it->current_index = it->leaf_count;
        else
            it->current_index = 0;
    }
}

void fbtreeInitIterator(fbtreeIterator *iterator, fbtreeIndex *fbt) {
    iter *it = iteratorFromOpaque(iterator);
    if (!fbt->root) {
        it->fbt = NULL;
    } else {
        it->fbt = fbt;
    }
    it->current_leaf = NULL;
    it->current_index = 0;
    it->leaf_count = 0;
}

bool fbtreeNext(fbtreeIterator *iterator, static_string **pos) {
    iter *it = iteratorFromOpaque(iterator);
    if (!it->fbt) return false;
    if (!it->current_leaf) {
        /* This is the first call - start at first item */
        node *first_node = it->fbt->root;
        while (!first_node->flags.is_leaf) {
            first_node = ((innerNode *)first_node)->children[0];
        }
        iteratorSetNode(it, (leafNode *)first_node, false);
    }

    while (it->current_leaf) {
        if (it->current_index < it->leaf_count) {
            *pos = it->current_leaf->values[it->current_index];
            it->current_index++;
            return true;
        }
        iteratorSetNode(it, it->current_leaf->next, false);
    }
    return false;
}

bool fbtreePrev(fbtreeIterator *iterator, static_string **pos) {
    iter *it = iteratorFromOpaque(iterator);
    if (!it->fbt) return false;
    if (!it->current_leaf) {
        /* This is the first call - start at last item */
        node *last_node = it->fbt->root;
        while (!last_node->flags.is_leaf) {
            innerNode *inner = (innerNode *)last_node;
            last_node = inner->children[inner->num_anchor_keys - 1];
        }
        iteratorSetNode(it, (leafNode *)last_node, true);
    }

    while (it->current_leaf) {
        if (it->current_index > 0) {
            it->current_index--;
            *pos = it->current_leaf->values[it->current_index];
            return true;
        }
        iteratorSetNode(it, it->current_leaf->prev, true);
    }
    return false;
}

void fbtreeSeekToRank(fbtreeIterator *iterator, unsigned long rank) {
    UNUSED(iterator);
    UNUSED(rank);
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
