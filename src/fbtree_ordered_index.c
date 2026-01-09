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
    char features[NODE_SIZE][FEATURE_SIZE]; // TODO: impl SIMD parallel feature comparison: char features[FEATURE_SIZE][NODE_SIZE];
    static_string *anchors[NODE_SIZE]; /* pointers to leaf high_key strings */
    node *children[NODE_SIZE];
    uint32_t child_sizes[NODE_SIZE]; /* subtree element counts for rank queries */
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

typedef struct {
    static_string *updated_anchor; /* Pointer to updated anchor string if it's changed */
    bool delete_executed; /* True if key was found and deleted, False if not found no-op */
    // TODO: implement merging: bool node_underflowed; /* True if root of subtree has underflowed and should be merged with a sibling */
} deleteResult;

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
    memset(node->features, 0, sizeof(node->features));
    memset(node->anchors, 0, sizeof(node->anchors));
    memset(node->children, 0, sizeof(node->children));
    memset(node->child_sizes, 0, sizeof(node->child_sizes));
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
        for (int i = 0; i < inner->num_anchor_keys; i++) {
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

/* String comparison for sorting. Returns: <0 if a < b, 0 if a == b, >0 if a > b (standard strcmp convention) */
static int compareStrings(static_string *a, static_string *b) {
    size_t min_len = a->len < b->len ? a->len : b->len;
    int cmp = memcmp(a->buf, b->buf, min_len);
    if (cmp != 0) return cmp;
    /* Shorter string comes first if prefixes match */
    return (a->len < b->len) ? -1 : (a->len > b->len) ? 1 : 0;
}

static bool stringsEqual(static_string *a, static_string *b) {
    return (a->len == b->len) && (memcmp(a->buf, b->buf, a->len) == 0);
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
        if (feature_size > FEATURE_SIZE) feature_size = FEATURE_SIZE;
        memcpy(inner->features[i], &inner->anchors[i]->buf[inner->prefix_len], feature_size);
        memset(inner->features[i] + feature_size, 0, FEATURE_SIZE - feature_size);
    }
}

static void updateCommonPrefix(innerNode *inner) {
    if (inner->num_anchor_keys < 2) return; // TODO: update for key deletion
    // TODO: if we knew which one updated, we could optimize to avoid one of the anchor key fetches (probably)

    static_string *first_anchor = inner->anchors[0];
    static_string *last_anchor = inner->anchors[inner->num_anchor_keys - 1];
    size_t max_len = first_anchor->len < last_anchor->len ? first_anchor->len : last_anchor->len;
    size_t len = 0;
    while (len < max_len && first_anchor->buf[len] == last_anchor->buf[len]) len++;

    if (len != inner->prefix_len) {
        if (len > inner->prefix_len) {
            memcpy(inner->embedded_prefix, first_anchor->buf, len);
        }
        inner->prefix_len = len;
        recomputeFeatures(inner);
    }
}

/* Get size of a node's subtree */
static uint32_t getNodeSize(node *n) {
    if (n->flags.is_leaf) {
        return __builtin_popcountll(((leafNode *)n)->presence_bitmap);
    } else {
        innerNode *inner = (innerNode *)n;
        uint32_t total = 0;
        for (int i = 0; i < inner->num_anchor_keys; i++) {
            total += inner->child_sizes[i];
        }
        return total;
    }
}

/* Insert a child into an inner node in sorted order. Returns true if parent's anchor/feature needs to be updated */
static bool innerNodeInsert(innerNode *parent, const node *child, static_string *child_anchor, size_t insert_index) {
    assert(parent->num_anchor_keys < NODE_SIZE);

    /* shift higher elements to make space */
    size_t num_to_move = parent->num_anchor_keys - insert_index;
    if (num_to_move > 0) {
        memmove(&parent->features[insert_index + 1], &parent->features[insert_index], num_to_move * sizeof(parent->features[0]));
        memmove(&parent->anchors[insert_index + 1], &parent->anchors[insert_index], num_to_move * sizeof(parent->anchors[0]));
        memmove(&parent->children[insert_index + 1], &parent->children[insert_index], num_to_move * sizeof(parent->children[0]));
        memmove(&parent->child_sizes[insert_index + 1], &parent->child_sizes[insert_index], num_to_move * sizeof(parent->child_sizes[0]));
    }

    /* insert child */
    getStringFeature(child_anchor, parent->prefix_len, parent->features[insert_index]);
    parent->anchors[insert_index] = (static_string *)child_anchor;
    parent->children[insert_index] = (node *)child;
    parent->child_sizes[insert_index] = getNodeSize((node *)child);
    parent->num_anchor_keys++;

    /* update prefix - might need to initialize, or common length could become shorter */
    if (insert_index == 0 || insert_index + 1 == parent->num_anchor_keys) {
        updateCommonPrefix(parent);
    }
    
    bool anchor_changed = (num_to_move == 0);
    return anchor_changed;
}

static innerNode *innerNodeSplit(innerNode *left_node) {
    assert(left_node->num_anchor_keys == NODE_SIZE);
    innerNode *right_node = innerNodeCreate();

    /* move higher half of elements to right node */
    const size_t num_left_keys = NODE_SIZE / 2;
    const size_t num_right_keys = NODE_SIZE - num_left_keys;

    memcpy(right_node->features, left_node->features + num_left_keys, num_right_keys * sizeof(left_node->features[0]));
    memcpy(right_node->anchors, left_node->anchors + num_left_keys, num_right_keys * sizeof(left_node->anchors[0]));
    memcpy(right_node->children, left_node->children + num_left_keys, num_right_keys * sizeof(left_node->children[0]));
    memcpy(right_node->child_sizes, left_node->child_sizes + num_left_keys, num_right_keys * sizeof(left_node->child_sizes[0]));

    right_node->num_anchor_keys = num_right_keys;
    left_node->num_anchor_keys = num_left_keys;

    /* each covers a smaller range of the dataset, so prefix could be longer now */
    memcpy(right_node->embedded_prefix, left_node->embedded_prefix, sizeof(left_node->embedded_prefix)); // TODO: only copy size of prefix // TODO: long prefix support
    right_node->prefix_len = left_node->prefix_len;
    updateCommonPrefix(right_node);
    updateCommonPrefix(left_node);
    return right_node;
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
    getStringFeature(string, inner->prefix_len, target_feature); // TODO: could use pointer into string if it's long enough

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

static insertResult innerNodeHandleChildSplit(innerNode *parent, node *new_child, static_string *new_child_anchor, size_t new_child_idx) {
    if (parent->num_anchor_keys == NODE_SIZE) {
        /* We're full - need to split */
        innerNode *new_right_parent = innerNodeSplit(parent);

        innerNode *insert_node = parent;
        bool insert_in_right_parent = new_child_idx > parent->num_anchor_keys;
        if (insert_in_right_parent) {
            insert_node = new_right_parent;
            new_child_idx -= parent->num_anchor_keys;
        }

        innerNodeInsert(insert_node, new_child, new_child_anchor, new_child_idx);
        insertResult result = {
            .updated_anchor = parent->anchors[parent->num_anchor_keys - 1],
            .new_node = (node *)new_right_parent,
            .new_node_anchor = new_right_parent->anchors[new_right_parent->num_anchor_keys - 1]
        };
        return result;
    } else {
        /* We're not full - just insert new child */
        bool anchor_changed = innerNodeInsert(parent, new_child, new_child_anchor, new_child_idx);
        insertResult result = {
            .updated_anchor = anchor_changed ? new_child_anchor : NULL,
        };
        return result;
    }
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
        insertResult child_insert_result = subtreeInsert(parent->children[child_idx], string);

        if (child_insert_result.updated_anchor) {
            parent->anchors[child_idx] = child_insert_result.updated_anchor;
            getStringFeature(child_insert_result.updated_anchor, parent->prefix_len, parent->features[child_idx]);
        }

        if (child_insert_result.new_node) {
            /* Our child split - recalculate original child's size since it lost elements */
            parent->child_sizes[child_idx] = getNodeSize(parent->children[child_idx]);
            /* Our child split, and we need to insert the new child just after the existing one */
            return innerNodeHandleChildSplit(parent, child_insert_result.new_node, child_insert_result.new_node_anchor, child_idx + 1);
        } else {
            /* No split - just increment size for the inserted element */
            parent->child_sizes[child_idx]++;
            /* subtree root did not split, so no new child node to deal with */
            bool parent_anchor_changed = (child_idx == parent->num_anchor_keys - 1);
            insertResult result = {
                .updated_anchor = parent_anchor_changed ? parent->anchors[child_idx] : NULL,
            };
            return result;
        }
    }
}

void fbtreeInsert(fbtreeIndex *fbt, static_string *string) {
    // TODO: handle overflow when tree exceeds UINT32_MAX elements
    assert(fbt->length < UINT32_MAX);

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
        return pos < count && stringsEqual(leaf->values[pos], key);
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
    if (fbt->root == NULL) return false;
    
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

static deleteResult leafNodeDelete(leafNode *leaf, static_string *key) {
    int count = __builtin_popcountll(leaf->presence_bitmap);
    assert(count > 0);

    if (leaf->flags.is_ordered) {
        const int delete_index = leafNodeBinarySearch(leaf, key);
        if (delete_index >= count || !stringsEqual(leaf->values[delete_index], key)) return (deleteResult){0};

        zfree(leaf->values[delete_index]);
        deleteResult result = {
            .delete_executed = true
        };
        count--;

        /* Shift elements to fill the gap */
        size_t num_to_shift = count - delete_index;
        memmove(&leaf->values[delete_index], &leaf->values[delete_index + 1], num_to_shift * sizeof(static_string *));

        /* Update presence bitmap */
        leaf->presence_bitmap = (1ULL << (count)) - 1;

        if (delete_index == count) {
            /* need to update high key */
            zfree(leaf->high_key);
            // TODO: implement node merge, deleting to empty set. handle empty node edge case.
            leaf->high_key = (count == 0) ? NULL : staticStringCopy(leaf->values[count - 1]);
            result.updated_anchor = leaf->high_key;
        }
        return result;
    } else {
        /* Linear scan for unordered nodes */
        // TODO: only iterate set bits
        for (int i = 0; i < NODE_SIZE; i++) {
            if (leaf->presence_bitmap & (1ULL << i)) {
                static_string *str = leaf->values[i];
                if (stringsEqual(str, key)) {
                    zfree(str);
                    leaf->presence_bitmap &= ~(1ULL << i);
                    deleteResult result = {
                        .delete_executed = true,
                    };
                    
                    if (stringsEqual(leaf->high_key, key)) {
                        /* Update high_key */
                        if (leaf->presence_bitmap == 0) {
                            zfree(leaf->high_key);
                            leaf->high_key = NULL;
                            return result; // TODO: implement node merge, deleting to empty set. handle empty node edge case.
                        }
                        
                        /* Find new maximum */
                        int max_index = -1;
                        for (int j = 0; j < NODE_SIZE; j++) {
                            if (leaf->presence_bitmap & (1ULL << j)) {
                                if (max_index < 0) {
                                    max_index = j;
                                } else if (compareStrings(leaf->values[j], leaf->values[max_index]) > 0) {
                                    max_index = j;
                                }
                            }
                        }
                        
                        zfree(leaf->high_key);
                        leaf->high_key = staticStringCopy(leaf->values[max_index]);
                        result.updated_anchor = leaf->high_key;
                    }
                    return result;
                }
            }
        }
        return (deleteResult){0};
    }
}

static deleteResult subtreeDelete(node *n, static_string *key) {
    if (n->flags.is_leaf)
        return leafNodeDelete((leafNode *)n, key);

    innerNode *inner = (innerNode *)n;
    int index = findChildIndex(inner, key);
    if (index == inner->num_anchor_keys) return (deleteResult){0};
    
    deleteResult child_result = subtreeDelete(inner->children[index], key);
    if (!child_result.delete_executed) return child_result;

    /* Update child size after delete */
    inner->child_sizes[index]--;

    if (child_result.updated_anchor) {
        inner->anchors[index] = child_result.updated_anchor;
        getStringFeature(child_result.updated_anchor, inner->prefix_len, inner->features[index]);
    }
    
    // TODO: check if we need to update prefix and features
    // TODO: handle underflow/merging

    deleteResult result = {
        .updated_anchor = index == inner->num_anchor_keys - 1 ? child_result.updated_anchor : NULL,
        .delete_executed = true
    };
    return result;
}

/* Returns false if element was not found and deleted */
bool fbtreeDelete(fbtreeIndex *fbt, static_string *key) {
    if (fbt->root == NULL) return false;

    deleteResult result = subtreeDelete(fbt->root, key);
    if (result.delete_executed) fbt->length--;
    if (fbt->length == 0) {
        zfree(fbt->root);
        fbt->root = NULL;
    }
    return result.delete_executed;
}

/* Get element at given rank (0-indexed). Returns NULL if rank >= length */
static_string *fbtreeGetAtRank(fbtreeIndex *fbt, unsigned long rank) {
    if (!fbt->root || rank >= fbt->length) return NULL;

    node *current = fbt->root;
    unsigned long remaining = rank;

    while (!current->flags.is_leaf) {
        innerNode *inner = (innerNode *)current;
        int i = 0;
        while (i < inner->num_anchor_keys && remaining >= inner->child_sizes[i]) {
            remaining -= inner->child_sizes[i];
            i++;
        }
        assert(i < inner->num_anchor_keys); // TODO this means there was a bug in our size tracking
        current = inner->children[i];
    }

    leafNode *leaf = (leafNode *)current;
    leafNodeEnsureSort(leaf);
    int count = __builtin_popcountll(leaf->presence_bitmap);
    assert((unsigned long)remaining < (unsigned long)count); // TODO this means there's a bug in size tracking
    return leaf->values[remaining];
}

static OrderedIndexItem *fbtreeGetByRank(OrderedIndex *idx, unsigned long rank) {
    return (OrderedIndexItem *)fbtreeGetAtRank((fbtreeIndex *)idx, rank);
}

/* Get rank of an element by key. Returns fbt->length if not found */
unsigned long fbtreeGetRankOfKey(fbtreeIndex *fbt, static_string *key) {
    if (!fbt->root) return fbt->length;

    unsigned long rank = 0;
    node *current = fbt->root;

    while (!current->flags.is_leaf) {
        innerNode *inner = (innerNode *)current;
        int child_idx = findChildIndex(inner, key);
        if (child_idx >= inner->num_anchor_keys) return fbt->length;
        
        /* Add sizes of all children before the one we descend into */
        for (int i = 0; i < child_idx; i++) {
            rank += inner->child_sizes[i];
        }
        current = inner->children[child_idx];
    }

    leafNode *leaf = (leafNode *)current;
    leafNodeEnsureSort(leaf);
    int pos = leafNodeBinarySearch(leaf, key);
    int count = __builtin_popcountll(leaf->presence_bitmap);
    if (pos >= count || !stringsEqual(leaf->values[pos], key)) {
        return fbt->length; /* Not found */ // TODO should we return the rank of the item if it were present?
    }
    return rank + pos;
}

static unsigned long fbtreeGetRank(OrderedIndex *idx, const OrderedIndexItem *pos) {
    UNUSED(idx); UNUSED(pos);
    assert(false); // TODO: implement get rank from item pointer
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
    iter *it = iteratorFromOpaque(iterator);
    if (!it->fbt || !it->fbt->root || rank >= it->fbt->length) {
        it->current_leaf = NULL;
        return;
    }

    node *current = it->fbt->root;
    unsigned long remaining = rank;

    while (!current->flags.is_leaf) {
        innerNode *inner = (innerNode *)current;
        int i = 0;
        while (i < inner->num_anchor_keys && remaining >= inner->child_sizes[i]) {
            remaining -= inner->child_sizes[i];
            i++;
        }
        if (i >= inner->num_anchor_keys) {
            it->current_leaf = NULL;
            return;
        }
        current = inner->children[i];
    }

    leafNode *leaf = (leafNode *)current;
    leafNodeEnsureSort(leaf);
    it->current_leaf = leaf;
    it->leaf_count = __builtin_popcountll(leaf->presence_bitmap);
    it->current_index = (uint8_t)remaining;
}

/* ========== Debug Functions ========== */

typedef struct {
    bool valid;
    uint32_t size;
} validateResult;

static void printBinaryString(const char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = buf[i];
        if (c >= 32 && c < 127) {
            printf("%c", c);
        } else {
            printf("\033[2m%02x\033[0m", (unsigned char)c);
        }
    }
}

static void printIndent(int depth) {
    for (int j = 0; j < depth; j++) printf("│  ");
}

static validateResult validateNode(node *n, int depth, size_t parent_prefix_len, bool verbose);

static bool validateLeafHighKey(leafNode *leaf) {
    int count = __builtin_popcountll(leaf->presence_bitmap);
    if (count == 0) return leaf->high_key == NULL;
    if (!leaf->high_key) return false;
    
    if (leaf->flags.is_ordered) {
        return compareStrings(leaf->values[count - 1], leaf->high_key) == 0;
    }
    
    /* Find max in unordered leaf */
    static_string *max_item = NULL;
    uint64_t bitmap = leaf->presence_bitmap;
    while (bitmap) {
        int idx = __builtin_ctzll(bitmap);
        bitmap &= bitmap - 1;
        if (!max_item || compareStrings(leaf->values[idx], max_item) > 0) {
            max_item = leaf->values[idx];
        }
    }
    return max_item && compareStrings(max_item, leaf->high_key) == 0;
}

static validateResult validateLeaf(leafNode *leaf, int depth, bool verbose) {
    uint32_t count = __builtin_popcountll(leaf->presence_bitmap);
    bool valid = validateLeafHighKey(leaf);
    
    if (verbose) {
        printf(" Leaf (%u items%s)", count, leaf->flags.is_ordered ? ", sorted" : "");
        if (!valid) printf(" \033[31m[bad high_key]\033[0m");
        printf("\n");
        
        for (uint32_t i = 0; i < count; i++) {
            if (i % 8 == 0) {
                printIndent(depth);
                printf("├─");
            }
            printBinaryString(leaf->values[i]->buf, leaf->values[i]->len);
            printf(" ");
            if (i % 8 == 7) printf("\n");
        }
        if (count > 0 && count % 8 != 0) printf("\n");
    }
    return (validateResult){.valid = valid, .size = count};
}

static validateResult validateInner(innerNode *inner, int depth, size_t parent_prefix_len, bool verbose) {
    bool valid = inner->prefix_len >= parent_prefix_len;
    uint32_t total_size = 0;
    
    if (verbose) {
        printf(" Inner (prefix=%zu, keys=%d)\n", inner->prefix_len, inner->num_anchor_keys);
    }
    
    for (int i = 0; i < inner->num_anchor_keys; i++) {
        static_string *anchor = inner->anchors[i];
        node *child = inner->children[i];
        
        /* Validate anchor starts with embedded_prefix */
        bool prefix_ok = anchor->len >= inner->prefix_len &&
                         memcmp(anchor->buf, inner->embedded_prefix, inner->prefix_len) == 0;
        
        /* Validate anchor matches child's high key */
        static_string *expected = child->flags.is_leaf 
            ? ((leafNode *)child)->high_key 
            : ((innerNode *)child)->anchors[((innerNode *)child)->num_anchor_keys - 1];
        bool anchor_ok = (expected == anchor);
        
        /* Validate feature matches anchor */
        char expected_feature[FEATURE_SIZE];
        getStringFeature(anchor, inner->prefix_len, expected_feature);
        bool feature_ok = memcmp(inner->features[i], expected_feature, FEATURE_SIZE) == 0;
        
        /* Recursively validate child and get its size */
        if (verbose) {
            printIndent(depth);
            printf("\u251c\u2500[%02d] size=%u anchor=", i, inner->child_sizes[i]);
            printBinaryString(anchor->buf, anchor->len);
        }
        
        validateResult child_result = validateNode(child, depth + 1, inner->prefix_len, verbose);
        
        /* Validate stored size matches actual size */
        bool size_ok = (inner->child_sizes[i] == child_result.size);
        
        valid = valid && prefix_ok && anchor_ok && feature_ok && size_ok && child_result.valid;
        total_size += child_result.size;
        
        if (verbose && (!prefix_ok || !anchor_ok || !feature_ok || !size_ok)) {
            printIndent(depth);
            printf("   \033[31m");
            if (!prefix_ok) printf("prefix ");
            if (!anchor_ok) printf("anchor ");
            if (!feature_ok) printf("feature ");
            if (!size_ok) printf("size(%u!=%u) ", inner->child_sizes[i], child_result.size);
            printf("FAIL\033[0m\n");
        }
    }
    return (validateResult){.valid = valid, .size = total_size};
}

static validateResult validateNode(node *n, int depth, size_t parent_prefix_len, bool verbose) {
    if (!n) return (validateResult){.valid = true, .size = 0};
    
    if (n->flags.is_leaf) {
        return validateLeaf((leafNode *)n, depth, verbose);
    } else {
        return validateInner((innerNode *)n, depth, parent_prefix_len, verbose);
    }
}

bool fbtreeDebugValidate(fbtreeIndex *fbt, bool verbose) {
    if (verbose) printf("FBTree (length=%lu)\n", fbt->length);
    if (!fbt->root) return true;
    
    validateResult result = validateNode(fbt->root, 0, 0, verbose);
    
    /* Also verify total size matches fbt->length */
    bool length_ok = (result.size == fbt->length);
    if (!length_ok && verbose) {
        printf("\033[31mERROR: tree size %u != fbt->length %lu\033[0m\n", result.size, fbt->length);
    }
    
    return result.valid && length_ok;
}

/* Wrapper functions for OrderedIndexOps interface */

static OrderedIndexItem *fbtreeScoreEleInsert(OrderedIndex *idx, double score, const_sds ele) {
    UNUSED(idx); UNUSED(score); UNUSED(ele);
    assert(false); // TODO: implement insert with score/element
    return NULL;
}

void fbtreeDeleteWrapper(OrderedIndex *idx, OrderedIndexItem *pos) {
    UNUSED(idx); UNUSED(pos);
    assert(false); // TODO: implement
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
    .delete = fbtreeDeleteWrapper,
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
