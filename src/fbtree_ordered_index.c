/* Feature B-Tree implementation of the ordered index interface. */

#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include "config.h"
#include "fbtree_ordered_index.h"
#include "ordered_index.h"
#include "serverassert.h"
#include "zmalloc.h"
#include "sds.h"

#if HAVE_X86_SIMD
#include <immintrin.h>
#elif HAVE_ARM_NEON
#include <arm_neon.h>
#endif

/* Anti-warning macro... */
#ifndef UNUSED
#define UNUSED(V) ((void)V)
#endif

#define NODE_SIZE 61
#define MIN_FILL (NODE_SIZE / 4) /* Minimum items before node underflows */
#define FEATURE_SIZE 4
#define EMBED_PREFIX_LEN 46
#define FEATURE_ROW_SIZE 64 /* size of cache line */

/* Common header for all node types */
typedef struct {
    bool is_leaf;
    uint8_t num_items;
} node;

typedef struct {
    node header;
    char embedded_prefix[EMBED_PREFIX_LEN]; // TODO: use pointer for larger prefix
    size_t prefix_len;
    char features[FEATURE_SIZE][FEATURE_ROW_SIZE];
    sds anchors[NODE_SIZE]; /* pointers to leaf high_key strings */
    node *children[NODE_SIZE];
    uint32_t child_sizes[NODE_SIZE]; /* subtree element counts for rank queries */
} innerNode;
static_assert(sizeof(innerNode) == 1536, "should fit perfectly in jemalloc size class");
static_assert(NODE_SIZE <= FEATURE_ROW_SIZE, "NODE_SIZE must fit in feature row");

typedef struct leafNode {
    node header;
    struct leafNode *prev;
    struct leafNode *next;
    // char tags[NODE_SIZE]; // TODO: add leaf hash tag stuff
    sds values[NODE_SIZE];
} leafNode;
static_assert(sizeof(leafNode) == 512, "should fit perfectly in jemalloc size class");

/* Get low_key (minimum) from leaf node - leaves are always kept sorted */
static inline sds leafNodeLowKey(leafNode *leaf) {
    return (leaf->header.num_items == 0) ? NULL : leaf->values[0];
}

/* Get high_key pointer from leaf node */
static inline sds leafNodeHighKey(leafNode *leaf) {
    return (leaf->header.num_items == 0) ? NULL : leaf->values[leaf->header.num_items - 1];
}

struct fbtreeIndex {
    node *root;
    leafNode *leftmost_leaf;  /* Cache for fast-path prepend */
    leafNode *rightmost_leaf; /* Cache for fast-path append */
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
    sds updated_anchor;  /* Pointer to updated anchor string if it's changed */
    node *new_node;      /* Pointer to new child node to insert (node split happened) */
    sds new_node_anchor; /* Pointer to new node's anchor string (node split happened) */
    sds inserted_item;   /* Pointer to the newly inserted item in the leaf */
} insertResult;

/* Hint for optimized insert path - allows skipping inner node searches */
typedef enum {
    HINT_NONE,      /* No hint - use normal search */
    HINT_RIGHTMOST, /* Insert goes to rightmost child at each level */
    HINT_LEFTMOST   /* Insert goes to leftmost child at each level */
} InsertHint;

typedef struct {
    sds updated_anchor;    /* Pointer to updated anchor string if it's changed */
    bool delete_executed;  /* True if key was found and deleted, False if not found no-op */
    bool node_underflowed; /* TODO: Currently unused. Implement node merging for memory efficiency if needed. */
} deleteResult;

/* Conversion from user-facing opaque iterator type to internal struct */
static inline iter *iteratorFromOpaque(fbtreeIterator *iterator) {
    return (iter *)(void *)iterator;
}

static innerNode *innerNodeCreate(void) {
    innerNode *node = zmalloc(sizeof(*node));
    node->header.is_leaf = false;
    node->header.num_items = 0;
    node->prefix_len = 0;
    memset(node->embedded_prefix, 0, sizeof(node->embedded_prefix));
    memset(node->features, 0, sizeof(node->features));
    memset(node->anchors, 0, sizeof(node->anchors));
    memset(node->children, 0, sizeof(node->children));
    memset(node->child_sizes, 0, sizeof(node->child_sizes));
    return node;
}

static leafNode *leafNodeCreate(void) {
    leafNode *node = zmalloc(sizeof(*node));
    node->header.is_leaf = true;
    node->header.num_items = 0;
    node->prev = NULL;
    node->next = NULL;
    return node;
}

static leafNode *leafNodeCreateWithItem(sds item) {
    leafNode *leaf = leafNodeCreate();
    leaf->values[0] = item;
    leaf->header.num_items = 1;
    return leaf;
}

fbtreeIndex *fbtreeCreate(void) {
    fbtreeIndex *fbt = zmalloc(sizeof(*fbt));
    fbt->root = NULL;
    fbt->leftmost_leaf = NULL;
    fbt->rightmost_leaf = NULL;
    return fbt;
}

static void freeNodeRecursive(node *n) {
    if (!n) return;

    if (n->is_leaf) {
        leafNode *leaf = (leafNode *)n;
        for (int i = 0; i < leaf->header.num_items; i++) {
            sdsfree(leaf->values[i]);
        }
        zfree(leaf);
    } else {
        innerNode *inner = (innerNode *)n;
        for (int i = 0; i < inner->header.num_items; i++) {
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

/* Bias constant for SIMD unsigned comparison: XOR with 0x80 converts unsigned
 * [0,255] to signed [-128,127] while preserving order. Features are stored
 * pre-biased so we only need to bias the search target at lookup time. */
#define FEATURE_BIAS 0x80

/* Get feature byte j from string, returning 0 if string is too short.
 * Returns the biased value (XOR'd with 0x80) for SIMD comparison. */
static inline char getFeatureByte(const_sds s, size_t prefix_len, int j) {
    size_t idx = prefix_len + j;
    unsigned char raw = (idx < sdslen(s)) ? (unsigned char)s[idx] : 0;
    return (char)(raw ^ FEATURE_BIAS);
}

static void recomputeFeatures(innerNode *inner) {
    for (int i = 0; i < inner->header.num_items; i++) {
        assert(sdslen(inner->anchors[i]) >= inner->prefix_len);
        for (int j = 0; j < FEATURE_SIZE; j++)
            inner->features[j][i] = getFeatureByte(inner->anchors[i], inner->prefix_len, j);
    }
}

static void updateCommonPrefix(innerNode *inner) {
    if (inner->header.num_items < 2) return; // TODO: update for key deletion
    // TODO: if we knew which one updated, we could optimize to avoid one of the anchor key fetches (probably)

    const_sds first_anchor = inner->anchors[0];
    const_sds last_anchor = inner->anchors[inner->header.num_items - 1];
    size_t first_len = sdslen(first_anchor);
    size_t last_len = sdslen(last_anchor);
    size_t max_len = first_len < last_len ? first_len : last_len;
    size_t len = 0;
    while (len < max_len && first_anchor[len] == last_anchor[len]) len++;

    bool prefix_changed = (len != inner->prefix_len) ||
                          (len > 0 && memcmp(inner->embedded_prefix, first_anchor, len) != 0);
    if (prefix_changed) {
        memcpy(inner->embedded_prefix, first_anchor, len);
        inner->prefix_len = len;
        recomputeFeatures(inner);
    }
}

/* Get size of a node's subtree */
static uint32_t getSubtreeSize(node *n) {
    if (n->is_leaf) {
        return n->num_items;
    } else {
        innerNode *inner = (innerNode *)n;
        uint32_t total = 0;
        for (int i = 0; i < inner->header.num_items; i++) {
            total += inner->child_sizes[i];
        }
        return total;
    }
}

/* Insert a child into an inner node in sorted order. Returns true if parent's anchor/feature needs to be updated */
static bool innerNodeInsert(innerNode *parent, const node *child, sds child_anchor, size_t insert_index) {
    assert(parent->header.num_items < NODE_SIZE);

    /* shift higher elements to make space */
    size_t num_to_move = parent->header.num_items - insert_index;
    if (num_to_move > 0) {
        for (int j = 0; j < FEATURE_SIZE; j++)
            memmove(&parent->features[j][insert_index + 1], &parent->features[j][insert_index], num_to_move);
        memmove(&parent->anchors[insert_index + 1], &parent->anchors[insert_index], num_to_move * sizeof(parent->anchors[0]));
        memmove(&parent->children[insert_index + 1], &parent->children[insert_index], num_to_move * sizeof(parent->children[0]));
        memmove(&parent->child_sizes[insert_index + 1], &parent->child_sizes[insert_index], num_to_move * sizeof(parent->child_sizes[0]));
    }

    /* insert child - features stored pre-biased for SIMD comparison */
    for (int j = 0; j < FEATURE_SIZE; j++)
        parent->features[j][insert_index] = getFeatureByte(child_anchor, parent->prefix_len, j);
    parent->anchors[insert_index] = child_anchor;
    parent->children[insert_index] = (node *)child;
    parent->child_sizes[insert_index] = getSubtreeSize((node *)child);
    parent->header.num_items++;

    /* update prefix - might need to initialize, or common length could become shorter */
    if (insert_index == 0 || insert_index + 1 == parent->header.num_items) {
        updateCommonPrefix(parent);
    }

    bool anchor_changed = (num_to_move == 0);
    return anchor_changed;
}

/* Remove child at given index from inner node. Caller must free the child. */
static void innerNodeRemoveChild(innerNode *parent, int index) {
    assert(index < parent->header.num_items);
    int num_to_move = parent->header.num_items - index - 1;
    if (num_to_move > 0) {
        for (int j = 0; j < FEATURE_SIZE; j++)
            memmove(&parent->features[j][index], &parent->features[j][index + 1], num_to_move);
        memmove(&parent->anchors[index], &parent->anchors[index + 1], num_to_move * sizeof(parent->anchors[0]));
        memmove(&parent->children[index], &parent->children[index + 1], num_to_move * sizeof(parent->children[0]));
        memmove(&parent->child_sizes[index], &parent->child_sizes[index + 1], num_to_move * sizeof(parent->child_sizes[0]));
    }
    parent->header.num_items--;
    if (index == 0 || index == parent->header.num_items) {
        updateCommonPrefix(parent);
    }
}

static innerNode *innerNodeSplit(innerNode *left_node) {
    assert(left_node->header.num_items == NODE_SIZE);
    innerNode *right_node = innerNodeCreate();

    /* move higher half of elements to right node */
    const size_t num_left_keys = NODE_SIZE / 2;
    const size_t num_right_keys = NODE_SIZE - num_left_keys;

    for (int j = 0; j < FEATURE_SIZE; j++)
        memcpy(right_node->features[j], left_node->features[j] + num_left_keys, num_right_keys);
    memcpy(right_node->anchors, left_node->anchors + num_left_keys, num_right_keys * sizeof(left_node->anchors[0]));
    memcpy(right_node->children, left_node->children + num_left_keys, num_right_keys * sizeof(left_node->children[0]));
    memcpy(right_node->child_sizes, left_node->child_sizes + num_left_keys, num_right_keys * sizeof(left_node->child_sizes[0]));

    right_node->header.num_items = num_right_keys;
    left_node->header.num_items = num_left_keys;

    /* each covers a smaller range of the dataset, so prefix could be longer now */
    memcpy(right_node->embedded_prefix, left_node->embedded_prefix, sizeof(left_node->embedded_prefix)); // TODO: only copy size of prefix // TODO: long prefix support
    right_node->prefix_len = left_node->prefix_len;
    updateCommonPrefix(right_node);
    updateCommonPrefix(left_node);
    return right_node;
}

static int leafNodeBinarySearch(leafNode *leaf, const_sds string) {
    int left = 0, right = leaf->header.num_items;
    while (left < right) {
        int mid = (left + right) / 2;
        if (sdscmp(leaf->values[mid], string) < 0) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    return left;
}

static insertResult leafNodeInsert(leafNode *leaf, sds string) {
    assert(leaf->header.num_items < NODE_SIZE);

    int insert_index = leafNodeBinarySearch(leaf, string);
    int count = leaf->header.num_items;

    insertResult result = {
        .inserted_item = string,
        .updated_anchor = (insert_index == count) ? string : NULL};

    /* Shift elements right to make space */
    memmove(&leaf->values[insert_index + 1], &leaf->values[insert_index],
            (count - insert_index) * sizeof(sds));

    /* Insert at position - take ownership */
    leaf->values[insert_index] = string;
    leaf->header.num_items++;

    return result;
}

/* Helper to link a new right leaf after an existing left leaf */
static void linkLeafRight(leafNode *left, leafNode *right) {
    right->prev = left;
    right->next = left->next;
    left->next = right;
    if (right->next) right->next->prev = right;
}

/* Unlink a leaf from the doubly-linked list */
static void unlinkLeaf(leafNode *leaf) {
    if (leaf->prev) leaf->prev->next = leaf->next;
    if (leaf->next) leaf->next->prev = leaf->prev;
}

static insertResult leafNodeSplit(leafNode *left_leaf, sds string) {
    assert(left_leaf->header.num_items == NODE_SIZE);

    /* Binary search to find insertion point - reuse for pattern detection */
    int insert_index = leafNodeBinarySearch(left_leaf, string);

    if (insert_index == NODE_SIZE) {
        /* Append: create new node with just the new item, left unchanged */
        leafNode *right_leaf = leafNodeCreateWithItem(string);
        linkLeafRight(left_leaf, right_leaf);
        return (insertResult){
            .new_node = (node *)right_leaf,
            .new_node_anchor = string,
            .inserted_item = right_leaf->values[0]};
    }

    if (insert_index == 0) {
        /* Prepend: move all items to new right node, left gets just new item */
        leafNode *right_leaf = leafNodeCreate();
        memcpy(right_leaf->values, left_leaf->values, NODE_SIZE * sizeof(sds));
        right_leaf->header.num_items = NODE_SIZE;
        left_leaf->values[0] = string;
        left_leaf->header.num_items = 1;
        linkLeafRight(left_leaf, right_leaf);
        return (insertResult){
            .updated_anchor = string,
            .new_node = (node *)right_leaf,
            .new_node_anchor = leafNodeHighKey(right_leaf),
            .inserted_item = left_leaf->values[0]};
    }

    /* Middle insert: standard 50/50 split */
    leafNode *right_leaf = leafNodeCreate();
    size_t num_left = NODE_SIZE / 2;
    size_t num_right = NODE_SIZE - num_left;

    memcpy(right_leaf->values, &left_leaf->values[num_left], num_right * sizeof(sds));
    left_leaf->header.num_items = num_left;
    right_leaf->header.num_items = num_right;
    linkLeafRight(left_leaf, right_leaf);

    /* Insert into appropriate leaf */
    insertResult leaf_result = (insert_index <= (int)num_left)
                                   ? leafNodeInsert(left_leaf, string)
                                   : leafNodeInsert(right_leaf, string);

    return (insertResult){
        .updated_anchor = leafNodeHighKey(left_leaf),
        .new_node = (node *)right_leaf,
        .new_node_anchor = leafNodeHighKey(right_leaf),
        .inserted_item = leaf_result.inserted_item};
}

/* SIMD feature search: finds range [out_left, out_right) of keys matching target.
 * Features are stored pre-biased (XOR'd with 0x80), so we only bias the target.
 * Bitmasks track candidates (ge_mask: target >= key, le_mask: target <= key). */
#if HAVE_X86_SIMD

ATTRIBUTE_TARGET_AVX2
static void featureSearchSIMD_avx2(char features[FEATURE_SIZE][FEATURE_ROW_SIZE],
                                   int num_keys,
                                   const unsigned char target[FEATURE_SIZE],
                                   int *out_left,
                                   int *out_right) {
    uint64_t valid_mask = (num_keys >= 64) ? ~0ULL : (1ULL << num_keys) - 1;
    uint64_t ge_mask = valid_mask;
    uint64_t le_mask = valid_mask;

    for (int j = 0; j < FEATURE_SIZE && (ge_mask || le_mask); j++) {
        /* Bias target to match pre-biased features */
        __m256i target_biased = _mm256_set1_epi8((char)(target[j] ^ FEATURE_BIAS));
        uint64_t gt_this = 0, lt_this = 0;

        /* Process 64 feature bytes in 2x32-byte chunks */
        for (int chunk = 0; chunk < 2; chunk++) {
            __m256i feat = _mm256_loadu_si256((const __m256i *)&features[j][chunk * 32]);
            __m256i gt = _mm256_cmpgt_epi8(target_biased, feat);
            __m256i lt = _mm256_cmpgt_epi8(feat, target_biased);
            gt_this |= (uint64_t)(uint32_t)_mm256_movemask_epi8(gt) << (chunk * 32);
            lt_this |= (uint64_t)(uint32_t)_mm256_movemask_epi8(lt) << (chunk * 32);
        }

        /* Narrow candidate set: eliminate keys where comparison is decided */
        uint64_t undecided = ge_mask & le_mask;
        ge_mask &= ~(lt_this & undecided);
        le_mask &= ~(gt_this & undecided);
    }

    *out_left = le_mask ? __builtin_ctzll(le_mask) : num_keys;
    *out_right = (le_mask & ~ge_mask) ? __builtin_ctzll(le_mask & ~ge_mask) : num_keys;
}

ATTRIBUTE_TARGET_SSE2
static void featureSearchSIMD_sse2(char features[FEATURE_SIZE][FEATURE_ROW_SIZE],
                                   int num_keys,
                                   const unsigned char target[FEATURE_SIZE],
                                   int *out_left,
                                   int *out_right) {
    uint64_t valid_mask = (num_keys >= 64) ? ~0ULL : (1ULL << num_keys) - 1;
    uint64_t ge_mask = valid_mask;
    uint64_t le_mask = valid_mask;

    for (int j = 0; j < FEATURE_SIZE && (ge_mask || le_mask); j++) {
        /* Bias target to match pre-biased features */
        __m128i target_biased = _mm_set1_epi8((char)(target[j] ^ FEATURE_BIAS));
        uint64_t gt_this = 0, lt_this = 0;

        /* Process 64 feature bytes in 4x16-byte chunks */
        for (int chunk = 0; chunk < 4; chunk++) {
            __m128i feat = _mm_loadu_si128((const __m128i *)&features[j][chunk * 16]);
            __m128i gt = _mm_cmpgt_epi8(target_biased, feat);
            __m128i lt = _mm_cmplt_epi8(target_biased, feat);
            gt_this |= (uint64_t)(uint16_t)_mm_movemask_epi8(gt) << (chunk * 16);
            lt_this |= (uint64_t)(uint16_t)_mm_movemask_epi8(lt) << (chunk * 16);
        }

        uint64_t undecided = ge_mask & le_mask;
        ge_mask &= ~(lt_this & undecided);
        le_mask &= ~(gt_this & undecided);
    }

    *out_left = le_mask ? __builtin_ctzll(le_mask) : num_keys;
    *out_right = (le_mask & ~ge_mask) ? __builtin_ctzll(le_mask & ~ge_mask) : num_keys;
}

static void featureSearchSIMD(char features[FEATURE_SIZE][FEATURE_ROW_SIZE],
                              int num_keys,
                              const unsigned char target[FEATURE_SIZE],
                              int *out_left,
                              int *out_right) {
    if (__builtin_cpu_supports("avx2")) {
        featureSearchSIMD_avx2(features, num_keys, target, out_left, out_right);
    } else {
        featureSearchSIMD_sse2(features, num_keys, target, out_left, out_right);
    }
}

#elif HAVE_ARM_NEON
#error "TODO: Implement ARM NEON version of featureSearchSIMD"

#else
/* Scalar fallback - features are stored pre-biased, so bias target for comparison */
static void featureSearchSIMD(char features[FEATURE_SIZE][FEATURE_ROW_SIZE],
                              int num_keys,
                              const unsigned char target[FEATURE_SIZE],
                              int *out_left,
                              int *out_right) {
    int left = 0, right = num_keys;

    /* Find leftmost position where target <= feature (lower bound) */
    for (int i = 0; i < num_keys; i++) {
        int cmp = 0;
        for (int j = 0; j < FEATURE_SIZE && cmp == 0; j++) {
            signed char target_biased = (signed char)(target[j] ^ FEATURE_BIAS);
            cmp = target_biased - (signed char)features[j][i];
        }
        if (cmp <= 0) {
            left = i;
            break;
        }
        left = i + 1;
    }

    /* Find rightmost position where target >= feature (upper bound) */
    right = left;
    for (int i = left; i < num_keys; i++) {
        int cmp = 0;
        for (int j = 0; j < FEATURE_SIZE && cmp == 0; j++) {
            signed char target_biased = (signed char)(target[j] ^ FEATURE_BIAS);
            cmp = target_biased - (signed char)features[j][i];
        }
        if (cmp < 0) break;
        right = i + 1;
    }

    *out_left = left;
    *out_right = right;
}
#endif /* HAVE_X86_SIMD */

/* Test wrappers to expose static functions for unit testing */
void featureSearchSIMD_test_wrapper(char features[FEATURE_SIZE][FEATURE_ROW_SIZE],
                                    int num_keys,
                                    const unsigned char target[FEATURE_SIZE],
                                    int *out_left,
                                    int *out_right) {
    featureSearchSIMD(features, num_keys, target, out_left, out_right);
}

#if HAVE_X86_SIMD
void featureSearchSIMD_avx2_test_wrapper(char features[FEATURE_SIZE][FEATURE_ROW_SIZE],
                                         int num_keys,
                                         const unsigned char target[FEATURE_SIZE],
                                         int *out_left,
                                         int *out_right) {
    featureSearchSIMD_avx2(features, num_keys, target, out_left, out_right);
}

void featureSearchSIMD_sse2_test_wrapper(char features[FEATURE_SIZE][FEATURE_ROW_SIZE],
                                         int num_keys,
                                         const unsigned char target[FEATURE_SIZE],
                                         int *out_left,
                                         int *out_right) {
    featureSearchSIMD_sse2(features, num_keys, target, out_left, out_right);
}
#endif

/* Find child index for insertion using feature vectors and anchors.
 * Two-pass approach:
 * 1. SIMD pass: narrow candidates using feature comparison
 * 2. Anchor pass: binary search on anchors within narrowed range (only if needed)
 *
 * validated_len: bytes already validated by ancestors - skip comparing these.
 */
static int findChildIndex(innerNode *inner, const_sds string, size_t validated_len) {
    /* Only compare prefix bytes beyond what ancestors already validated */
    if (inner->prefix_len > validated_len) {
        size_t cmp_len = inner->prefix_len - validated_len;
        int cmp = memcmp(string + validated_len, inner->embedded_prefix + validated_len, cmp_len);
        if (cmp < 0) return 0;
        if (cmp > 0) return inner->header.num_items;
    }

    /* Extract target feature bytes */
    unsigned char target[FEATURE_SIZE];
    size_t slen = sdslen(string);
    for (int j = 0; j < FEATURE_SIZE; j++) {
        size_t idx = inner->prefix_len + j;
        target[j] = (idx < slen) ? (unsigned char)string[idx] : 0;
    }

    /* Pass 1: SIMD feature search to narrow range */
    int left, right;
    featureSearchSIMD(inner->features, inner->header.num_items, target, &left, &right);

    /* TODO: consider scalar path for small num_anchor_keys */

    /* Pass 2: Binary search on anchors within narrowed range (handles collisions) */
    while (left < right) {
        int mid = (left + right) / 2;
        int cmp = sdscmp(string, inner->anchors[mid]);
        if (cmp <= 0) {
            right = mid;
        } else {
            left = mid + 1;
        }
    }
    return left;
}

static insertResult innerNodeHandleChildSplit(innerNode *parent, node *new_child, sds new_child_anchor, size_t new_child_idx) {
    if (parent->header.num_items == NODE_SIZE) {
        /* We're full - need to split */
        innerNode *new_right_parent = innerNodeSplit(parent);

        innerNode *insert_node = parent;
        bool insert_in_right_parent = new_child_idx > parent->header.num_items;
        if (insert_in_right_parent) {
            insert_node = new_right_parent;
            new_child_idx -= parent->header.num_items;
        }

        innerNodeInsert(insert_node, new_child, new_child_anchor, new_child_idx);
        insertResult result = {
            .updated_anchor = parent->anchors[parent->header.num_items - 1],
            .new_node = (node *)new_right_parent,
            .new_node_anchor = new_right_parent->anchors[new_right_parent->header.num_items - 1]};
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

static insertResult subtreeInsert(node *n, sds string, InsertHint hint, size_t validated_len) {
    assert(n);
    if (n->is_leaf) {
        leafNode *leaf = (leafNode *)n;
        if (leaf->header.num_items == NODE_SIZE) {
            return leafNodeSplit(leaf, string);
        } else {
            return leafNodeInsert(leaf, string);
        }
    } else {
        /* inner node - find correct child for insert */
        innerNode *parent = (innerNode *)n;
        assert(parent->header.num_items > 0);

        /* Use hint to skip search when possible */
        int child_idx;
        if (hint == HINT_RIGHTMOST) {
            child_idx = parent->header.num_items - 1;
        } else if (hint == HINT_LEFTMOST) {
            child_idx = 0;
        } else {
            child_idx = findChildIndex(parent, string, validated_len);
            if (child_idx == parent->header.num_items) child_idx--;
        }

        /* Pass this node's prefix_len to child - it's now validated */
        insertResult child_insert_result = subtreeInsert(parent->children[child_idx], string, hint, parent->prefix_len);

        if (child_insert_result.updated_anchor) {
            parent->anchors[child_idx] = child_insert_result.updated_anchor;
            /* Anchor changed - prefix may need to shrink if this is first or last child */
            if (child_idx == 0 || child_idx == parent->header.num_items - 1) {
                updateCommonPrefix(parent);
            }
            for (int j = 0; j < FEATURE_SIZE; j++)
                parent->features[j][child_idx] = getFeatureByte(child_insert_result.updated_anchor, parent->prefix_len, j);
        }

        if (child_insert_result.new_node) {
            /* Our child split - recalculate original child's size since it lost elements */
            parent->child_sizes[child_idx] = getSubtreeSize(parent->children[child_idx]);
            /* Our child split, and we need to insert the new child just after the existing one */
            insertResult result = innerNodeHandleChildSplit(parent, child_insert_result.new_node, child_insert_result.new_node_anchor, child_idx + 1);
            result.inserted_item = child_insert_result.inserted_item;
            return result;
        } else {
            /* No split - just increment size for the inserted element */
            parent->child_sizes[child_idx]++;
            /* subtree root did not split, so no new child node to deal with */
            bool parent_anchor_changed = (child_idx == parent->header.num_items - 1);
            insertResult result = {
                .updated_anchor = parent_anchor_changed ? parent->anchors[child_idx] : NULL,
                .inserted_item = child_insert_result.inserted_item,
            };
            return result;
        }
    }
}

sds fbtreeInsert(fbtreeIndex *fbt, sds string) {
    if (fbt->root == NULL) {
        leafNode *leaf = leafNodeCreateWithItem(string);
        fbt->root = (node *)leaf;
        fbt->leftmost_leaf = leaf;
        fbt->rightmost_leaf = leaf;
        return leaf->values[0];
    }

    /* Detect append/prepend patterns for optimized insert path */
    InsertHint hint = HINT_NONE;
    leafNode *rightmost = fbt->rightmost_leaf;
    leafNode *leftmost = fbt->leftmost_leaf;

    if (rightmost && sdscmp(string, leafNodeHighKey(rightmost)) > 0) {
        hint = HINT_RIGHTMOST;
    } else if (leftmost && sdscmp(string, leafNodeLowKey(leftmost)) < 0) {
        hint = HINT_LEFTMOST;
    }

    /* Insert with hint - skips inner node searches for append/prepend */
    insertResult result = subtreeInsert(fbt->root, string, hint, 0);

    if (result.new_node) {
        innerNode *new_root = innerNodeCreate();
        sds left_anchor = result.updated_anchor;
        if (!left_anchor) {
            left_anchor = fbt->root->is_leaf
                              ? leafNodeHighKey((leafNode *)fbt->root)
                              : ((innerNode *)fbt->root)->anchors[((innerNode *)fbt->root)->header.num_items - 1];
        }
        innerNodeInsert(new_root, fbt->root, left_anchor, 0);
        innerNodeInsert(new_root, result.new_node, result.new_node_anchor, 1);
        fbt->root = (node *)new_root;
    }

    /* Update leaf caches using linked list - O(1) */
    if (leftmost && leftmost->prev) {
        fbt->leftmost_leaf = leftmost->prev;
    }
    if (rightmost && rightmost->next) {
        fbt->rightmost_leaf = rightmost->next;
    }
    return result.inserted_item;
}

static deleteResult leafNodeDelete(leafNode *leaf, const_sds item) {
    assert(leaf->header.num_items > 0);

    /* Find item by pointer comparison */
    int delete_index = -1;
    for (int i = 0; i < leaf->header.num_items; i++) {
        if (leaf->values[i] == item) {
            delete_index = i;
            break;
        }
    }
    if (delete_index < 0) return (deleteResult){0};

    sdsfree(leaf->values[delete_index]);
    leaf->header.num_items--;

    /* Shift elements to fill the gap */
    memmove(&leaf->values[delete_index], &leaf->values[delete_index + 1],
            (leaf->header.num_items - delete_index) * sizeof(sds));

    deleteResult result = {
        .delete_executed = true,
        .updated_anchor = (delete_index == leaf->header.num_items) ? leafNodeHighKey(leaf) : NULL,
        .node_underflowed = leaf->header.num_items < MIN_FILL};
    return result;
}

static deleteResult subtreeDelete(node *n, const_sds item) {
    if (n->is_leaf)
        return leafNodeDelete((leafNode *)n, item);

    innerNode *inner = (innerNode *)n;
    int index = findChildIndex(inner, item, 0);
    if (index == inner->header.num_items) return (deleteResult){0};

    deleteResult child_result = subtreeDelete(inner->children[index], item);
    if (!child_result.delete_executed) return child_result;

    /* Update child size after delete */
    inner->child_sizes[index]--;

    if (child_result.updated_anchor) {
        inner->anchors[index] = child_result.updated_anchor;
        for (int j = 0; j < FEATURE_SIZE; j++)
            inner->features[j][index] = getFeatureByte(child_result.updated_anchor, inner->prefix_len, j);
    }

    /* Remove empty child */
    if (inner->child_sizes[index] == 0) {
        node *empty_child = inner->children[index];
        if (empty_child->is_leaf) unlinkLeaf((leafNode *)empty_child);
        zfree(empty_child);
        innerNodeRemoveChild(inner, index);
        /* Anchor update: if we removed last child, new last child's anchor bubbles up */
        sds new_anchor = (inner->header.num_items > 0 && index == inner->header.num_items)
                             ? inner->anchors[inner->header.num_items - 1]
                             : NULL;
        return (deleteResult){
            .updated_anchor = new_anchor,
            .delete_executed = true,
            .node_underflowed = inner->header.num_items < MIN_FILL};
    }

    deleteResult result = {
        .updated_anchor = index == inner->header.num_items - 1 ? child_result.updated_anchor : NULL,
        .delete_executed = true,
        .node_underflowed = inner->header.num_items < MIN_FILL};
    return result;
}

/* Returns false if element was not found and deleted.
 * item must be the exact pointer returned from fbtreeInsert. */
bool fbtreeDelete(fbtreeIndex *fbt, const_sds item) {
    if (fbt->root == NULL) return false;

    /* Update leaf caches before delete - they may point to a leaf that gets freed */
    // TODO: optimize to avoid fetches - only on leaf node delete: compare pointers and update if needed
    if (fbt->leftmost_leaf && fbt->leftmost_leaf->header.num_items == 1 &&
        fbt->leftmost_leaf->values[0] == item) {
        fbt->leftmost_leaf = fbt->leftmost_leaf->next;
    }
    if (fbt->rightmost_leaf && fbt->rightmost_leaf->header.num_items == 1 &&
        fbt->rightmost_leaf->values[0] == item) {
        fbt->rightmost_leaf = fbt->rightmost_leaf->prev;
    }

    deleteResult result = subtreeDelete(fbt->root, item);
    if (!result.delete_executed) return false;

    if (getSubtreeSize(fbt->root) == 0) {
        zfree(fbt->root);
        fbt->root = NULL;
        fbt->leftmost_leaf = NULL;
        fbt->rightmost_leaf = NULL;
    } else if (!fbt->root->is_leaf && fbt->root->num_items == 1) {
        /* Root collapse: inner root with single child becomes that child */
        innerNode *old_root = (innerNode *)fbt->root;
        fbt->root = old_root->children[0];
        zfree(old_root);
    }
    return true;
}

/* Get element at given rank (0-indexed). Returns NULL if rank >= length */
const_sds fbtreeGetAtRank(fbtreeIndex *fbt, unsigned long rank) {
    if (!fbt->root) return NULL;

    node *current = fbt->root;
    unsigned long remaining = rank;

    while (!current->is_leaf) {
        innerNode *inner = (innerNode *)current;
        int i = 0;
        while (i < inner->header.num_items && remaining >= inner->child_sizes[i]) {
            remaining -= inner->child_sizes[i];
            i++;
        }
        if (i >= inner->header.num_items) return NULL;
        current = inner->children[i];
    }

    leafNode *leaf = (leafNode *)current;
    if (remaining >= leaf->header.num_items) return NULL;
    return leaf->values[remaining];
}

static OrderedIndexItem *fbtreeGetByRank(OrderedIndex *idx, unsigned long rank) {
    return (OrderedIndexItem *)fbtreeGetAtRank((fbtreeIndex *)idx, rank);
}

/* Get rank of an item given a direct pointer to it (from hashtable lookup).
 * The item pointer must be a valid pointer into a leaf node's values array. */
long fbtreeGetRankOfItem(fbtreeIndex *fbt, const_sds item) {
    if (!fbt->root || !item) return -1;

    long rank = 0;
    node *current = fbt->root;
    size_t validated_len = 0;

    while (!current->is_leaf) {
        innerNode *inner = (innerNode *)current;
        int child_idx = findChildIndex(inner, item, validated_len);
        if (child_idx >= inner->header.num_items) return -1;

        for (int i = 0; i < child_idx; i++) {
            rank += inner->child_sizes[i];
        }
        validated_len = inner->prefix_len;
        current = inner->children[child_idx];
    }

    leafNode *leaf = (leafNode *)current;

    /* Find position by pointer comparison (item is known to be in this leaf) */
    for (int i = 0; i < leaf->header.num_items; i++) {
        if (leaf->values[i] == item) {
            return rank + i;
        }
    }
    return -1; /* Not found */
}

static long fbtreeGetRank(OrderedIndex *idx, const OrderedIndexItem *pos) {
    return fbtreeGetRankOfItem((fbtreeIndex *)idx, (const_sds)pos);
}

unsigned long fbtreeLength(fbtreeIndex *fbt) {
    return fbt->root ? getSubtreeSize(fbt->root) : 0;
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
        it->current_leaf = leaf;
        it->leaf_count = leaf->header.num_items;
        it->current_index = last_child ? it->leaf_count : 0;
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

bool fbtreeNext(fbtreeIterator *iterator, const_sds *pos) {
    iter *it = iteratorFromOpaque(iterator);
    if (!it->fbt) return false;
    if (!it->current_leaf) {
        /* First call - use cached leftmost leaf for O(1) start */
        iteratorSetNode(it, it->fbt->leftmost_leaf, false);
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

bool fbtreePrev(fbtreeIterator *iterator, const_sds *pos) {
    iter *it = iteratorFromOpaque(iterator);
    if (!it->fbt) return false;
    if (!it->current_leaf) {
        /* First call - use cached rightmost leaf for O(1) start */
        iteratorSetNode(it, it->fbt->rightmost_leaf, true);
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
    if (!it->fbt || !it->fbt->root) {
        it->current_leaf = NULL;
        return;
    }

    // TODO: would it be faster to iterate from start/end instead of traversing down the tree sometimes? Worth doing?

    node *current = it->fbt->root;
    unsigned long remaining = rank;

    while (!current->is_leaf) {
        innerNode *inner = (innerNode *)current;
        int i = 0;
        while (i < inner->header.num_items && remaining >= inner->child_sizes[i]) {
            remaining -= inner->child_sizes[i];
            i++;
        }
        if (i >= inner->header.num_items) {
            it->current_leaf = NULL;
            return;
        }
        current = inner->children[i];
    }

    leafNode *leaf = (leafNode *)current;
    it->current_leaf = leaf;
    it->leaf_count = leaf->header.num_items;
    it->current_index = (uint8_t)remaining;
}

#define SCORE_SIZE 8 /* 8-byte normalized score prefix */

/* Find child index for score lookup (8-byte prefix). Optimized: no bounds checking.
 * validated_len: score bytes already matched by ancestors - skip comparing these. */
static int findChildIndexByScore(innerNode *inner, const char *score, size_t validated_len) {
    /* Compare unvalidated prefix bytes (up to 8) */
    if (inner->prefix_len > validated_len) {
        size_t cmp_len = (inner->prefix_len < SCORE_SIZE ? inner->prefix_len : SCORE_SIZE) - validated_len;
        if (cmp_len > 0) {
            int cmp = memcmp(score + validated_len, inner->embedded_prefix + validated_len, cmp_len);
            if (cmp < 0) return 0;
            if (cmp > 0) return inner->header.num_items;
        }
    }

    /* If node prefix covers entire score, first child has first match */
    if (inner->prefix_len >= SCORE_SIZE) return 0;

    /* Extract feature bytes - no bounds check, score is always 8 bytes */
    unsigned char target[FEATURE_SIZE];
    for (int j = 0; j < FEATURE_SIZE; j++)
        target[j] = (unsigned char)score[inner->prefix_len + j];

    int left, right;
    featureSearchSIMD(inner->features, inner->header.num_items, target, &left, &right);

    /* Binary search with fixed 8-byte comparison */
    while (left < right) {
        int mid = (left + right) / 2;
        if (memcmp(inner->anchors[mid], score, SCORE_SIZE) < 0)
            left = mid + 1;
        else
            right = mid;
    }
    return left;
}

/* Binary search in leaf for first element with score >= given score */
static int leafNodeBinarySearchByScore(leafNode *leaf, const char *score) {
    int left = 0, right = leaf->header.num_items;
    while (left < right) {
        int mid = (left + right) / 2;
        if (memcmp(leaf->values[mid], score, SCORE_SIZE) < 0)
            left = mid + 1;
        else
            right = mid;
    }
    return left;
}

/* Lookup by 8-byte score prefix. Positions iterator at first matching element.
 * Returns true if found, false if no element with this score exists. */
bool fbtreeLookupByScore(fbtreeIndex *fbt, const char *score, fbtreeIterator *iterator) {
    iter *it = iteratorFromOpaque(iterator);
    it->fbt = NULL;
    it->current_leaf = NULL;
    it->current_index = 0;
    it->leaf_count = 0;

    if (!fbt->root) return false;

    node *current = fbt->root;
    size_t validated_len = 0;

    while (!current->is_leaf) {
        innerNode *inner = (innerNode *)current;
        int child_idx = findChildIndexByScore(inner, score, validated_len);
        if (child_idx >= inner->header.num_items)
            child_idx = inner->header.num_items - 1;
        validated_len = inner->prefix_len;
        current = inner->children[child_idx];
    }

    leafNode *leaf = (leafNode *)current;
    int pos = leafNodeBinarySearchByScore(leaf, score);

    /* Check if we found a match */
    if (pos < leaf->header.num_items && memcmp(leaf->values[pos], score, SCORE_SIZE) == 0) {
        it->fbt = fbt;
        it->current_leaf = leaf;
        it->leaf_count = leaf->header.num_items;
        it->current_index = pos;
        return true;
    }

    return false;
}

/* ========== Debug Functions ========== */

typedef struct {
    bool valid;
    uint32_t size;
    leafNode *leftmost_leaf;
    leafNode *rightmost_leaf;
} validateResult;

static void printBinaryString(const_sds s) {
    size_t len = sdslen(s);
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
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

static validateResult validateLeaf(leafNode *leaf, int depth, bool verbose) {
    uint32_t count = leaf->header.num_items;
    bool valid = (count <= NODE_SIZE);

    if (verbose) {
        printf(" Leaf (%u items)\n", count);
        if (!valid) printf(" \033[31m[bad high_key]\033[0m");
        printf("\n");

        for (uint32_t i = 0; i < count; i++) {
            if (i % 8 == 0) {
                printIndent(depth);
                printf("├─");
            }
            printBinaryString(leaf->values[i]);
            printf(" ");
            if (i % 8 == 7) printf("\n");
        }
        if (count > 0 && count % 8 != 0) printf("\n");
    }
    return (validateResult){.valid = valid, .size = count, .leftmost_leaf = leaf, .rightmost_leaf = leaf};
}

static validateResult validateInner(innerNode *inner, int depth, size_t parent_prefix_len, bool verbose) {
    bool valid = inner->prefix_len >= parent_prefix_len;
    uint32_t total_size = 0;
    leafNode *leftmost = NULL;
    leafNode *rightmost = NULL;

    if (verbose) {
        printf(" Inner (prefix=%zu, keys=%d)\n", inner->prefix_len, inner->header.num_items);
    }

    for (int i = 0; i < inner->header.num_items; i++) {
        const_sds anchor = inner->anchors[i];
        node *child = inner->children[i];

        /* Validate anchor starts with embedded_prefix */
        bool prefix_ok = sdslen(anchor) >= inner->prefix_len &&
                         memcmp(anchor, inner->embedded_prefix, inner->prefix_len) == 0;

        /* Validate anchor matches child's high key */
        const_sds expected = child->is_leaf
                                 ? leafNodeHighKey((leafNode *)child)
                                 : ((innerNode *)child)->anchors[((innerNode *)child)->header.num_items - 1];
        bool anchor_ok = (expected == anchor);

        /* Validate feature matches anchor */
        bool feature_ok = true;
        for (int j = 0; j < FEATURE_SIZE && feature_ok; j++)
            feature_ok = (inner->features[j][i] == getFeatureByte(anchor, inner->prefix_len, j));

        /* Recursively validate child and get its size */
        if (verbose) {
            printIndent(depth);
            printf("\u251c\u2500[%02d] size=%u anchor=", i, inner->child_sizes[i]);
            printBinaryString(anchor);
        }

        validateResult child_result = validateNode(child, depth + 1, inner->prefix_len, verbose);

        /* Track leftmost/rightmost leaves */
        if (i == 0) leftmost = child_result.leftmost_leaf;
        rightmost = child_result.rightmost_leaf;

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
    return (validateResult){.valid = valid, .size = total_size, .leftmost_leaf = leftmost, .rightmost_leaf = rightmost};
}

static validateResult validateNode(node *n, int depth, size_t parent_prefix_len, bool verbose) {
    if (!n) return (validateResult){.valid = true, .size = 0};

    if (n->is_leaf) {
        return validateLeaf((leafNode *)n, depth, verbose);
    } else {
        return validateInner((innerNode *)n, depth, parent_prefix_len, verbose);
    }
}

bool fbtreeDebugValidate(fbtreeIndex *fbt, bool verbose) {
    unsigned long length = fbt->root ? getSubtreeSize(fbt->root) : 0;
    if (verbose) printf("FBTree (length=%lu)\n", length);
    if (!fbt->root) {
        /* Empty tree: caches must be NULL */
        if (fbt->leftmost_leaf || fbt->rightmost_leaf) {
            if (verbose) printf("\033[31mERROR: empty tree has non-NULL leaf cache\033[0m\n");
            return false;
        }
        return true;
    }

    validateResult result = validateNode(fbt->root, 0, 0, verbose);

    /* Also verify total size matches computed length */
    bool length_ok = (result.size == length);
    if (!length_ok && verbose) {
        printf("\033[31mERROR: tree size %u != computed length %lu\033[0m\n", result.size, length);
    }

    /* Verify leaf caches point to actual leftmost/rightmost leaves */
    leafNode *actual_leftmost = result.leftmost_leaf;
    leafNode *actual_rightmost = result.rightmost_leaf;
    bool caches_ok = (fbt->leftmost_leaf == actual_leftmost && fbt->rightmost_leaf == actual_rightmost);
    if (!caches_ok && verbose) {
        printf("\033[31mERROR: leaf cache mismatch (leftmost: %p vs %p, rightmost: %p vs %p)\033[0m\n",
               (void *)fbt->leftmost_leaf, (void *)actual_leftmost,
               (void *)fbt->rightmost_leaf, (void *)actual_rightmost);
    }

    return result.valid && length_ok && caches_ok;
}

/* Wrapper functions for OrderedIndexOps interface */

static OrderedIndexItem *fbtreeScoreEleInsert(OrderedIndex *idx, double score, const_sds ele) {
    UNUSED(idx);
    UNUSED(score);
    UNUSED(ele);
    assert(false); // TODO: implement insert with score/element
    return NULL;
}

void fbtreeDeleteWrapper(OrderedIndex *idx, OrderedIndexItem *pos) {
    UNUSED(idx);
    UNUSED(pos);
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
    UNUSED(idx);
    UNUSED(pos);
    UNUSED(newscore);
    assert(false); // TODO: pack score and element into binary string and use as key
    return NULL;
}

static unsigned long fbtreeDeleteRangeByScore(OrderedIndex *idx, double min, double max, int min_ex, int max_ex) {
    UNUSED(idx);
    UNUSED(min);
    UNUSED(max);
    UNUSED(min_ex);
    UNUSED(max_ex);
    assert(false); // TODO: pack score and element into binary string and use as key
    return 0;
}

static unsigned long fbtreeDeleteRangeByRank(OrderedIndex *idx, unsigned long start, unsigned long end) {
    UNUSED(idx);
    UNUSED(start);
    UNUSED(end);
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
    return fbtreeNext((fbtreeIterator *)iter, (const_sds *)pos);
}

static bool fbtreePrevWrapper(OrderedIndexIterator *iter, OrderedIndexItem **pos) {
    return fbtreePrev((fbtreeIterator *)iter, (const_sds *)pos);
}

static void fbtreeSeekToRankWrapper(OrderedIndexIterator *iter, unsigned long rank) {
    fbtreeSeekToRank((fbtreeIterator *)iter, rank);
}

static void fbtreeSeekToScoreRange(OrderedIndexIterator *iter, double min, double max, int min_ex, int max_ex, long offset) {
    UNUSED(iter);
    UNUSED(min);
    UNUSED(max);
    UNUSED(min_ex);
    UNUSED(max_ex);
    UNUSED(offset);
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
