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
#define EMBED_PREFIX_LEN 224
// TODO: if feature_size is 4B, we could copy/compare/etc as 32-bit words for speed

struct nodeFlags {
    uint8_t is_ordered : 1;
    uint8_t is_leaf : 1;
    // TODO: could store is_rightmost when next pointer is last child instead of next sibling 
    // TODO: there are unused bits here, and packing into node structs is inefficient
};

/* Abstract type: just for type checking and definition of common fields */
typedef struct {
    struct nodeFlags flags;
} node;

typedef struct {
    struct nodeFlags flags;
    size_t prefix_len;
    char embedded_prefix[EMBED_PREFIX_LEN]; // TODO: use pointer for larger prefix
    uint8_t num_anchor_keys;
    node *next; /* sibling or last child */
    char features[FEATURE_SIZE][NODE_SIZE];
    static_string *anchors[NODE_SIZE]; /* pointers to leaf high_key strings */
    node *children[NODE_SIZE];
} innerNode;

typedef struct leafNode {
    struct nodeFlags flags;
    uint64_t presence_bitmap;
    static_string *high_key;
    struct leafNode *prev;
    struct leafNode *next;
    // char tags[NODE_SIZE]; // TODO: add leaf hash tag stuff
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

typedef struct {
    static_string *updated_anchor; /* Pointer to updated anchor string if it's changed */
    node *new_node; /* Pointer to new child node to insert (node split happened) */
    static_string *new_node_anchor; /* Pointer to new node's anchor string (node split happened) */
} insertResult;

/* Conversion from user-facing opaque iterator type to internal struct */
static inline iter *iteratorFromOpaque(fbtreeIterator *iterator) {
    return (iter *)(void *)iterator;
}

static_string *staticStringCopy(static_string* src) {
    size_t data_len = sizeof(static_string) + src->len;
    static_string *copy = zmalloc(data_len);
    memcpy(copy, src, data_len);
    return copy;
}

static innerNode *innerNodeCreate(void) {
    innerNode *node = zmalloc(sizeof(*node));
    node->flags.is_ordered = 0;
    node->flags.is_leaf = 0;
    node->prefix_len = 0;
    memset(node->embedded_prefix, 0, sizeof(node->embedded_prefix));
    node->num_anchor_keys = 0;
    node->next = NULL;
    memset(node->features, 0, sizeof(node->features));
    memset(node->anchors, 0, sizeof(node->anchors));
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
static inline int compareStrings(static_string *a, static_string *b) {
    size_t min_len = a->len < b->len ? a->len : b->len;
    int cmp = memcmp(a->buf, b->buf, min_len);
    if (cmp != 0) return cmp;
    /* Shorter string comes first if prefixes match */
    return (a->len < b->len) ? -1 : (a->len > b->len) ? 1 : 0;
}

/* Extract common prefix between two strings */
static bool checkNewCommonPrefix(innerNode *inner, static_string *new) {
    if (inner->num_anchor_keys == 0) return false;

    if (inner->num_anchor_keys == 1) {
        /* about to add second item, so initialize common prefix between them */
        size_t min_len = (inner->anchors[0]->len < new->len) ? inner->anchors[0]->len : new->len;
        const char *data = inner->anchors[0]->buf;
        size_t i = 0;
        while (i < min_len && data[i] == new->buf[i]) i++;
        inner->prefix_len = i;

        assert(inner->prefix_len <= EMBED_PREFIX_LEN); // TODO: support for longer prefixes
        memcpy(inner->embedded_prefix, data, inner->prefix_len);
        return true;
    } else {
        /* compare prefix against new item, update prefix_len */
        size_t min_len = (inner->prefix_len < new->len) ? inner->prefix_len : new->len;
        size_t i = 0;
        while (i < min_len && inner->embedded_prefix[i] == new->buf[i]) i++;
        bool prefix_length_changed = inner->prefix_len != i;
        inner->prefix_len = i;
        return prefix_length_changed;
    }
}

static void getStringFeature(static_string *string, size_t prefix_len, char *feature_out) {
    size_t feature_size = string->len - prefix_len;
    if (feature_size > FEATURE_SIZE) feature_size = FEATURE_SIZE;
    memcpy(feature_out, string->buf + prefix_len, feature_size);
    memset(feature_out + feature_size, 0, FEATURE_SIZE - feature_size);
}

static void recomputeFeatures(innerNode *inner) {
    for (int i = 0; i < inner->num_anchor_keys; i++) {
        assert(inner->anchors[i]->len >= inner->prefix_len);
        size_t feature_size = inner->anchors[i]->len - inner->prefix_len;
        assert(feature_size <= FEATURE_SIZE); // TODO: larger prefixes
        memcpy(inner->features[i], &inner->anchors[i]->buf[inner->prefix_len], feature_size);
        memset(inner->features[i] + feature_size, 0, FEATURE_SIZE - feature_size);
    }
}

/* Insert a child into an inner node in sorted order. Returns true if parent's anchor/feature needs to be updated */
static insertResult innerNodeInsert(innerNode *parent, const node *child, static_string *child_anchor, size_t insert_index) {
    assert(parent->num_anchor_keys < NODE_SIZE);

    bool prefix_changed = checkNewCommonPrefix(parent, child_anchor);
    if (prefix_changed) recomputeFeatures(parent);

    /* get new child's anchor string and feature vector */
    char child_feature[FEATURE_SIZE];
    getStringFeature(child_anchor, parent->prefix_len, child_feature);

    /* shift higher elements to make space */
    size_t num_to_move = parent->num_anchor_keys - insert_index;
    if (num_to_move > 0) {
        memmove(&parent->features[insert_index + 1], &parent->features[insert_index], num_to_move * sizeof(parent->features[0]));
        memmove(&parent->anchors[insert_index + 1], &parent->anchors[insert_index], num_to_move * sizeof(parent->anchors[0]));
        memmove(&parent->children[insert_index + 1], &parent->children[insert_index], num_to_move * sizeof(parent->children[0]));
    }
    /* insert child */
    memcpy(parent->features[insert_index], child_feature, FEATURE_SIZE);
    parent->anchors[insert_index] = (static_string *)child_anchor;
    parent->children[insert_index] = (node *)child;
    parent->num_anchor_keys++;
    
    bool anchor_changed = num_to_move == 0;
    insertResult result = {
        .updated_anchor = anchor_changed ? (static_string *)child_anchor : NULL,
        .new_node = NULL,
        .new_node_anchor = NULL
    };
    return result;
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

static int leafNodeBinarySearch(leafNode *leaf, static_string *string) {
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
    return left;
}

static insertResult leafNodeInsert(leafNode *leaf, static_string *string) {
    /* We assume here there is capacity to insert without splitting */
    assert(leaf->presence_bitmap != ~0ULL);

    insertResult result = {0};

    if (leaf->flags.is_ordered) {
        const int count = __builtin_popcountll(leaf->presence_bitmap);
        int left = leafNodeBinarySearch(leaf, string);
        
        /* Shift elements right to make space */
        memmove(&leaf->values[left + 1], &leaf->values[left], 
                (count - left) * sizeof(static_string *));
        
        /* Insert at position */
        leaf->values[left] = staticStringCopy(string);
        if (count + 1 == 64)
            leaf->presence_bitmap = ~0ULL; /* all bits set */
        else
            leaf->presence_bitmap = (1ULL << (count + 1)) - 1;

        /* Update high_key if inserting at the end (new maximum) */
        if (left == count) {
            if (leaf->high_key) zfree(leaf->high_key);
            leaf->high_key = staticStringCopy(string);
            result.updated_anchor = leaf->high_key;
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
            result.updated_anchor = leaf->high_key;
        }
    }
    return result;
}

static insertResult leafNodeSplit(leafNode *left_leaf, static_string *string) {
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

    insertResult result = {
        .updated_anchor = left_leaf->high_key,
        .new_node = (node *)right_leaf,
        .new_node_anchor = right_leaf->high_key
    };
    return result;
}

/* Find child index for insertion using feature vectors and anchors */
static int findChildIndex(innerNode *inner, static_string *string) {
    /* given string may not share common prefix */
    if (inner->prefix_len > 0) {
        // TODO: long prefix support
        int cmp = memcmp(string->buf, inner->embedded_prefix, inner->prefix_len);
        if (cmp < 0) return 0;
        if (cmp > 0) return inner->num_anchor_keys;
    }

    /* Compute feature for the string */
    char target_feature[FEATURE_SIZE];
    getStringFeature(string, inner->prefix_len, target_feature);

    /* Binary search on features */
    // TODO: do linear feature search for branch prediction
    // TODO: use SIMD for parallel feature comparison
    int left = 0, right = inner->num_anchor_keys;
    while (left < right) {
        int mid = (left + right) / 2;
        int cmp = memcmp(target_feature, inner->features[mid], FEATURE_SIZE);
        if (cmp == 0) {
            /* Feature collision - compare full anchor key */
            cmp = compareStrings(string, inner->anchors[mid]);
        }
        if (cmp <= 0) {
            right = mid;
        } else {
            left = mid + 1;
        }
    }
    return left;
}

static insertResult subtreeInsert(node *n, static_string *string) {
    assert(n);
    if (n->flags.is_leaf) {
        leafNode *leaf = (leafNode *)n;
        if (leaf->presence_bitmap == ~0ULL) {
            return leafNodeSplit(leaf, string);
        } else {
            return leafNodeInsert(leaf, string);
        }
    } else {
        /* inner node - find correct child for insert */
        innerNode *parent = (innerNode *)n;
        assert(parent->num_anchor_keys > 0);
        
        int child_idx = findChildIndex(parent, string);
        if (child_idx == parent->num_anchor_keys) child_idx--; /* If we would insert after last child, insert into last child subtree instead */
        const bool parent_anchor_changed = (child_idx == parent->num_anchor_keys - 1);
        insertResult child_insert_result = subtreeInsert(parent->children[child_idx], string);

        if (child_insert_result.updated_anchor) {
            parent->anchors[child_idx] = child_insert_result.updated_anchor;
        }
        
        if (child_insert_result.new_node) {
            /* Child split happened - insert new child after existing index */
            if (parent->num_anchor_keys == NODE_SIZE) {
                assert(false); // TODO: implement inner node split
            } else {
                return innerNodeInsert(parent, child_insert_result.new_node, child_insert_result.new_node_anchor, child_idx + 1);
            }
        } else {
            insertResult result = {
                .updated_anchor = parent_anchor_changed ? parent->anchors[child_idx] : NULL,
            };
            return result;
        }
    }
}

void fbtreeInsert(fbtreeIndex *fbt, static_string *string) {
    if (fbt->root == NULL) fbt->root = (node *)leafNodeCreate();

    insertResult result = subtreeInsert(fbt->root, string);
    if (result.new_node) {
        assert(result.updated_anchor);
        innerNode *new_root = innerNodeCreate();
        innerNodeInsert(new_root, fbt->root, result.updated_anchor, 0);
        innerNodeInsert(new_root, result.new_node, result.new_node_anchor, 1);
        fbt->root = (node *)new_root;
    }
    fbt->length++;
}

static bool leafNodeLookup(leafNode *leaf, static_string *key) {
    if (leaf->flags.is_ordered) {
        // TODO: is this actually faster than a linear scan?
        int pos = leafNodeBinarySearch(leaf, key);
        const int count = __builtin_popcountll(leaf->presence_bitmap);
        return pos < count && compareStrings(leaf->values[pos], key) == 0;
    } else {
        /* Linear scan for unordered nodes */
        // TODO: only iterate set bits
        for (int i = 0; i < NODE_SIZE; i++) {
            if (leaf->presence_bitmap & (1ULL << i)) {
                static_string *str = leaf->values[i];
                if (str->len == key->len && memcmp(str->buf, key->buf, key->len) == 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool fbtreeLookup(fbtreeIndex *fbt, static_string *key) {
    if (fbt->root == NULL) return NULL;
    
    node *current = fbt->root;
    while (!current->flags.is_leaf) {
        innerNode *inner = (innerNode *)current;
        int child_idx = findChildIndex(inner, key);
        if (child_idx == inner->num_anchor_keys) return false;
        current = inner->children[child_idx];
    }
    
    leafNode *leaf = (leafNode *)current;
    return leafNodeLookup(leaf, key);
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

/* ========== Debug Functions ========== */

static void printIndent(int depth, bool is_last) {
    for (int i = 0; i < depth; i++) {
        printf(i == depth - 1 ? (is_last ? "└─ " : "├─ ") : "│  ");
    }
}

static void printStringPreview(static_string *str, int max_len) {
    if (!str) {
        printf("(null)");
        return;
    }
    size_t len = str->len < (size_t)max_len ? str->len : (size_t)max_len;
    printf("\"");
    for (size_t i = 0; i < len; i++) {
        char c = str->buf[i];
        printf("%c", (c >= 32 && c < 127) ? c : '?');
    }
    if (str->len > (size_t)max_len) printf("...");
    printf("\"");
}

static void debugPrintNode(node *n, int depth, bool is_last) {
    if (!n) return;
    
    if (n->flags.is_leaf) {
        leafNode *leaf = (leafNode *)n;
        int count = __builtin_popcountll(leaf->presence_bitmap);
        printIndent(depth, is_last);
        printf("Leaf (%d items%s)\n", count, leaf->flags.is_ordered ? ", sorted" : "");
        for (int i = 0; i < count; i++) {
            for (int j = 0; j <= depth; j++) printf("│  ");
            printf("  ");
            printStringPreview(leaf->values[i], 40);
            printf("\n");
        }
    } else {
        innerNode *inner = (innerNode *)n;
        printIndent(depth, is_last);
        printf("Inner [prefix=\"");
        for (size_t i = 0; i < inner->prefix_len; i++) {
            char c = inner->embedded_prefix[i];
            printf("%c", (c >= 32 && c < 127) ? c : '?');
        }
        printf("\", keys=%d]\n", inner->num_anchor_keys);
        
        for (int i = 0; i < inner->num_anchor_keys; i++) {
            debugPrintNode(inner->children[i], depth + 1, i == inner->num_anchor_keys - 1);
        }
    }
}

void fbtreeDebugPrint(fbtreeIndex *fbt) {
    printf("FBTree (length=%lu)\n", fbt->length);
    if (!fbt->root) {
        printf("  (empty)\n");
    } else {
        debugPrintNode(fbt->root, 0, true);
    }
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
